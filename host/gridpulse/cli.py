"""GRID PULSE host bridge entry point.

Wires the pieces together: find the keypad (or don't), read its event stream on a
background thread, fold it into a display mirror, log everything, broadcast to the
browser over SSE, and serve the UI.

THE ABSENCE OF HARDWARE IS NOT AN ERROR. If no keypad is found, the server still
starts, still serves the same page, and the UI preselects keyboard mode with a
plain-language explanation. ``./run.sh`` on a machine that has never seen the device
must land the user in a playable game, and it does.

Standard library only.
"""

from __future__ import annotations

import argparse
import os
import platform
import signal
import sys
import threading
import time
import webbrowser
from typing import Any, Callable, Dict, List, Optional

from . import logwriter, protocol, reconcile, replay, serial_port, server, sse, terminal

# How long to wait for the device's HELLO before deciding the port is not a keypad.
HELLO_TIMEOUT_SECONDS = 2.0

# Reader poll interval when the port has nothing to say. Short enough that the
# shutdown flag is noticed promptly, long enough not to spin a core.
READ_POLL_SECONDS = 0.002

# How often to look for a keypad appearing or vanishing.
#
# The keypad is a USB device and people plug it in after starting the game at least as
# often as before. Probing once at launch made that unrecoverable without a restart,
# which is a poor answer for hardware whose whole selling point is that you can pick it
# up and use it. A second is well under the time it takes to move a hand from a cable
# to a keyboard, and a scan is a directory listing.
DEVICE_SCAN_SECONDS = 1.0

# Handshake timeout for a device discovered by the watcher rather than at launch.
#
# Shorter than HELLO_TIMEOUT_SECONDS because this one runs on a loop: a serial device
# that is not a keypad would otherwise block the watcher for two seconds out of every
# scan, and there is no operator waiting on this particular attempt.
HOTPLUG_HELLO_TIMEOUT_SECONDS = 1.0


class Bridge:
    """Owns the device connection, the reader thread and the shared state."""

    def __init__(self, log: Optional[logwriter.SessionLog], verbose: bool = False):
        self.broadcaster = sse.Broadcaster()
        self.mirror = reconcile.SessionMirror()
        self.sequence = protocol.SequenceTracker()
        self.reader = protocol.LineReader()
        self.log = log
        self.verbose = verbose
        # Set by --selftest. Surfaced through /api/status so the page can open the
        # calibration screen; the walk itself is started by the operator clicking,
        # not by the host. See the comment on the status field below.
        self.selftest_requested = False
        # True while a recorded session is being played back instead of a live device.
        self.replaying = False
        # Optional sink for every parsed device event, used by terminal mode. The
        # browser gets the same stream through the broadcaster; this is a second
        # subscriber rather than a second source.
        self.on_event: Optional[Callable[[Dict[str, Any]], None]] = None

        self.port: Optional[serial_port.SerialPort] = None
        self.device_reason = "no keypad detected"
        self.connected = False
        self.hello: Optional[Dict[str, Any]] = None
        # Remembered across a failed handshake, when the port has been released.
        self.handshake_path: Optional[str] = None

        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._write_lock = threading.Lock()

        # Guards attach/detach so the watcher thread and the reader thread cannot
        # tear down or stand up a connection at the same time.
        self._device_lock = threading.RLock()
        # Stops the CURRENT reader thread without ending the process. Distinct from
        # _stop, which means the whole bridge is going away: a device that unplugs must
        # release its reader and leave the watcher running to pick it up again.
        self._reader_stop = threading.Event()
        self._watch_thread: Optional[threading.Thread] = None
        # Path pinned by --port, so the watcher reattaches to the same device rather
        # than wandering onto whatever else appears.
        self._explicit_path: Optional[str] = None
        # Origin for the host-side receipt clock stamped onto every event.
        self._started_monotonic = time.monotonic()

    # --- device ---------------------------------------------------------

    def connect(self, explicit_path: Optional[str] = None) -> bool:
        """Opens a candidate port. Returns False with a plain-language reason on failure.

        Opening is NOT connecting. ``connected`` stays false until the device has
        identified itself, because a port that opens is only a port: it might be a
        modem, a debug probe, or a keypad still booting. The watcher retries every
        second, so a window in which an unverified port reported itself as a connected
        keypad would be sampled constantly - the mode card would flicker on and off,
        and a START could be sent to something that is not a keypad at all.
        """
        candidates: List[str]
        if explicit_path:
            candidates = [explicit_path]
        else:
            candidates = serial_port.find_ports()

        if not candidates:
            self.device_reason = "no keypad found on this machine"
            return False

        for path in candidates:
            port = serial_port.SerialPort(path)
            try:
                port.open()
            except serial_port.PermissionDeniedError:
                self.device_reason = (
                    "found a keypad at %s but this user cannot open it "
                    "(see the note printed in the terminal)" % path
                )
                if platform.system() == "Linux":
                    print(serial_port.LINUX_PERMISSION_HELP.format(path=path),
                          file=sys.stderr)
                continue
            except serial_port.SerialError as exc:
                self.device_reason = "could not open %s: %s" % (path, exc)
                continue

            self.port = port
            self.device_reason = "connected on %s" % path
            return True

        return False

    def send_command(self, name: str, args: Dict[str, Any]) -> bool:
        """Builds, frames and writes a command. Returns False if it was rejected."""
        if self.port is None:
            return False
        try:
            line = protocol.build_command(name, **args)
        except (protocol.ProtocolError, TypeError) as exc:
            if self.verbose:
                print("rejected command %r %r: %s" % (name, args, exc), file=sys.stderr)
            return False

        try:
            with self._write_lock:
                self.port.write(line)
            return True
        except serial_port.SerialError as exc:
            self._handle_disconnect(str(exc))
            return False

    def _handle_disconnect(self, reason: str) -> None:
        self._release(reason, announce=True)

    def _release(self, reason: str, announce: bool = False) -> None:
        """Gives up the device without ending the bridge.

        Deliberately not ``stop()``. Stopping sets the shutdown flag, which would also
        end the watcher and make an unplug permanent - the exact behaviour that forced
        a restart. This tears down only the connection, leaving the watcher free to
        pick the device up again when it comes back.
        """
        with self._device_lock:
            was_connected = self.connected
            self._reader_stop.set()
            thread, self._thread = self._thread, None
            port, self.port = self.port, None
            self.connected = False
            self.hello = None
            self.device_reason = reason

        # A reader thread that discovered the disconnect itself is the caller here;
        # joining it would deadlock.
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=2.0)
        if port is not None:
            try:
                port.close()
            except serial_port.SerialError:
                pass

        if announce and was_connected:
            # Say so in the terminal as well as in the UI. Losing the keypad mid-session
            # is the sort of thing someone notices ten minutes later and cannot explain,
            # and the browser only shows the current state - not when it changed.
            print("Keypad disconnected: %s. Watching for it to come back." % reason,
                  flush=True)
            self.broadcaster.publish(
                {"type": "LINK", "connected": False, "reason": reason}
            )

    # --- hot-plug watcher -------------------------------------------------

    def start_watching(self) -> None:
        """Watches for the keypad appearing or vanishing, for as long as we run."""
        self._watch_thread = threading.Thread(
            target=self._watch, name="gridpulse-watch", daemon=True
        )
        self._watch_thread.start()

    def _watch(self) -> None:
        while not self._stop.wait(DEVICE_SCAN_SECONDS):
            try:
                if self.connected:
                    self._check_still_there()
                else:
                    self._try_attach()
            except Exception as exc:  # noqa: BLE001
                # A watcher that dies on one bad scan silently stops reporting the
                # device for the rest of the session, which is worse than any single
                # failure it could be reporting.
                if self.verbose:
                    print("device watch error: %s" % exc, file=sys.stderr)

    def _check_still_there(self) -> None:
        """Notices an unplug the reader thread has not tripped over yet.

        A read only fails once the driver notices; the device node disappears at once.
        Checking it means the UI greys out as the cable leaves rather than whenever the
        next byte happens to be due.
        """
        path = self.port.path if self.port else None
        if path is not None and not os.path.exists(path):
            self._handle_disconnect("the keypad was unplugged")

    def _try_attach(self) -> None:
        if not self.connect_and_handshake(
            self._explicit_path, timeout=HOTPLUG_HELLO_TIMEOUT_SECONDS
        ):
            return
        # Only reached by the watcher, so this is a keypad that appeared AFTER launch.
        # The one found at startup is announced by main() instead, and printing in both
        # places would double up on the ordinary case.
        print("Keypad connected on %s (firmware %s, N=%s)"
              % (self.port.path if self.port else "?",
                 (self.hello or {}).get("fw", "?"),
                 (self.hello or {}).get("n", "?")),
              flush=True)
        self.broadcaster.publish({
            "type": "LINK",
            "connected": True,
            "reason": self.device_reason,
            "port": self.port.path if self.port else None,
            "n": (self.hello or {}).get("n"),
            "fw": (self.hello or {}).get("fw"),
        })

    # --- reader thread ---------------------------------------------------

    def start(self) -> None:
        self._reader_stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run, name="gridpulse-reader", daemon=True
        )
        self._thread.start()

    def _run(self) -> None:
        # Pinned for the life of this thread. self.port is cleared the instant the
        # device is released, and a reader still dereferencing it would race.
        port = self.port
        reader_stop = self._reader_stop
        assert port is not None
        while not self._stop.is_set() and not reader_stop.is_set():
            try:
                chunk = port.read()
            except serial_port.SerialError as exc:
                self._handle_disconnect(str(exc))
                return

            if not chunk:
                time.sleep(READ_POLL_SECONDS)
                continue

            for line in self.reader.feed(chunk):
                self._handle_line(line)

    def _handle_line(self, line: bytes) -> None:
        error, event = protocol.parse_event(line)
        if error != protocol.OK or event is None:
            # Malformed input is logged, never silently discarded: a link problem
            # during a scored run must be visible afterwards.
            if self.log is not None:
                self.log.write_raw_line(line, error)
            self.broadcaster.publish(
                {"type": "LOG", "level": "W", "msg": "dropped_line_%s" % error}
            )
            return

        missed = self.sequence.observe(int(event["seq"]))
        if missed:
            # Reported here rather than in the UI. It is a link diagnostic for whoever
            # is running the bridge, and nothing a player mid-run can act on: the score
            # is unaffected either way, because the device computes its own tally from
            # state the host never touches. One line per discontinuity, not per lost
            # event, so a bad cable does not bury the rest of the output.
            print(
                "link: %d event(s) lost between the keypad and the browser "
                "(%d total this session). Display only; the device tally is unaffected."
                % (missed, self.sequence.missing),
                file=sys.stderr,
                flush=True,
            )
            self.broadcaster.publish(
                {
                    "type": "SEQGAP",
                    "missing": missed,
                    "total_missing": self.sequence.missing,
                }
            )

        if event.get("type") == "HELLO":
            # Identifying itself is what makes a port a keypad, so this - not opening
            # the port - is the moment the device counts as connected. Setting it here
            # rather than in connect_and_handshake means every path that hears a HELLO
            # agrees, including the re-announcement the firmware sends after a
            # self-test.
            self.hello = event
            self.connected = True

        # When the HOST learned of this event, in microseconds since the bridge
        # started. Its absolute value means nothing next to the device's t_us - the two
        # clocks have unrelated origins - but the SPREAD of (host_us - t_us) across a
        # run is the transport's jitter, which is the part that would actually show up
        # as a laggy display. tools/latency_report.py reports it.
        event["host_us"] = int((time.monotonic() - self._started_monotonic) * 1e6)

        self.mirror.apply(event)
        if self.log is not None:
            self.log.write_record(event)

        self.broadcaster.publish(event)
        if self.on_event is not None:
            # After the mirror, so a renderer reading it sees this event folded in.
            self.on_event(event)

        if event.get("type") == "END" and self.mirror.final is not None:
            if self.log is not None:
                self.log.write_run_summary(self.mirror.final, self.sequence.missing)
            self.broadcaster.publish({"type": "STATUS", **self.mirror.snapshot()})

    def connect_and_handshake(self, explicit_path: Optional[str] = None,
                              timeout: float = HELLO_TIMEOUT_SECONDS) -> bool:
        """Open the port, start the reader, and wait for the device to identify itself.

        THE ORDER IS THE POINT. The reader thread must be running before the
        handshake, because HELLO arrives asynchronously and is parsed on that thread;
        waiting for it with no reader is a guaranteed timeout however healthy the
        device is.

        This lives here rather than in main() specifically so it can be tested end to
        end over a pseudo-terminal. It was once inlined in main() with the two steps
        the wrong way round, which no unit test could see.

        On failure the port is released and ``device_reason`` explains why in plain
        language.
        """
        with self._device_lock:
            if not self.connect(explicit_path):
                return False

            self.handshake_path = self.port.path if self.port else None
            # A reconnect is a fresh device boot: sequence numbers restart from 1 and
            # any half-line from the old connection is meaningless. Carrying either
            # over would report a gap that never happened.
            self.sequence = protocol.SequenceTracker()
            self.reader = protocol.LineReader()
            self.start()

        if self.wait_for_hello(timeout):
            # connected was set by _handle_line when the HELLO landed.
            return True

        # Release, do not stop: a device that fails the handshake must not end the
        # watcher, or plugging in a real keypad afterwards would go unnoticed.
        self._release("a serial device was found but did not answer the handshake")
        return False

    def wait_for_hello(self, timeout: float = HELLO_TIMEOUT_SECONDS) -> bool:
        """Asks the device to identify itself and waits for the reply."""
        self.send_command("PING", {})
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.hello is not None:
                return True
            time.sleep(0.02)
        return False

    def measure_link_rtt(self, samples: int = 12,
                         timeout: float = 1.0) -> List[float]:
        """Times PING -> HELLO round trips, in seconds.

        The USB leg is the one number in this project that was asserted rather than
        measured: the architecture notes quote "1-5 ms" for scored-to-browser, derived
        from CDC framing rather than observed. This observes it.

        WHAT THIS IS AND IS NOT. It is a round trip: host write, device notices in its
        tud_task loop, device emits, host reads. One-way is roughly half, but only
        roughly - the two directions are not symmetric, and the figure also carries the
        reader thread's poll granularity (READ_POLL_SECONDS), which puts a floor under
        the resolution. It is an upper bound on the transport delay, honestly obtained,
        and that is exactly what it is reported as.

        It cannot be a one-way measurement. That would need a shared clock, and the
        RP2040's timer and the host's have unrelated origins - which is also why the
        device timestamps every event itself rather than trusting arrival time.
        """
        rtts: List[float] = []
        for _ in range(samples):
            if not self.connected or self.port is None:
                break
            self.hello = None
            started = time.monotonic()
            if not self.send_command("PING", {}):
                break
            deadline = started + timeout
            while time.monotonic() < deadline:
                if self.hello is not None:
                    rtts.append(time.monotonic() - started)
                    break
                time.sleep(0.0005)
            time.sleep(0.01)  # let the link settle between samples
        return rtts

    def status(self) -> Dict[str, Any]:
        return {
            "version": "1.0.0",
            "protocol_version": protocol.PROTOCOL_VERSION,
            # A request to *open* the calibration screen, not to run the walk. The
            # walk allows five seconds per cell and marks a cell dead after two
            # misses, so starting it before the operator is looking at the screen
            # would silently shrink the alphabet. The page starts it on a click.
            "selftest_requested": self.selftest_requested,
            "replaying": self.replaying,
            "device": {
                "connected": self.connected,
                "port": self.port.path if self.port else None,
                "reason": self.device_reason,
                "firmware": (self.hello or {}).get("fw"),
                "n": (self.hello or {}).get("n"),
            },
            "link": {
                "seq_gaps": self.sequence.gaps,
                "missing_events": self.sequence.missing,
                "overlong_lines": self.reader.dropped_overlong,
            },
        }

    def stop(self) -> None:
        self._stop.set()
        self._reader_stop.set()
        if self._watch_thread is not None:
            self._watch_thread.join(timeout=2.0)
            self._watch_thread = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self.port is not None:
            self.port.close()
            self.port = None
        self.broadcaster.close_all()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gridpulse",
        description="GRID PULSE host bridge: serves the game and streams keypad events.",
    )
    parser.add_argument("--keyboard", action="store_true",
                        help="serve the UI without opening a serial port at all")
    parser.add_argument("--hardware", action="store_true",
                        help="require the keypad; exit with an error if absent")
    parser.add_argument("--port", metavar="DEV",
                        help="explicit serial device path, skipping auto-detection")
    parser.add_argument("--replay", metavar="LOGFILE",
                        help="replay a recorded .jsonl session at its original timing")
    parser.add_argument("--replay-speed", type=float, default=1.0,
                        help="replay speed multiplier (default 1.0)")
    parser.add_argument("--selftest", action="store_true",
                        help="open the keypad calibration screen on launch")
    parser.add_argument("--http-port", type=int, default=8765,
                        help="preferred localhost port (default 8765)")
    parser.add_argument("--link", action="store_true",
                        help="measure and report the USB link, then exit")
    parser.add_argument("--terminal", action="store_true",
                        help="play and calibrate from the terminal; no browser, no "
                             "HTTP server, keypad required")
    parser.add_argument("--no-browser", action="store_true",
                        help="do not open a browser window")
    parser.add_argument("--no-log", action="store_true",
                        help="do not write session logs to logs/")
    parser.add_argument("--verbose", action="store_true",
                        help="print HTTP and protocol diagnostics")
    return parser


def summarise_rtt(rtts: List[float]) -> Dict[str, Any]:
    """Median and p95 of a set of round trips, in milliseconds."""
    if not rtts:
        return {}
    ordered = sorted(rtts)
    return {
        "samples": len(ordered),
        "min_ms": ordered[0] * 1000.0,
        "median_ms": ordered[len(ordered) // 2] * 1000.0,
        "p95_ms": ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))] * 1000.0,
        "max_ms": ordered[-1] * 1000.0,
        "note": "round trip, upper bound; includes host read-poll granularity",
    }


def _report_link(bridge: "Bridge") -> int:
    """Measure the USB leg and explain what the figure is, and is not."""
    if not bridge.connected:
        print("error: no keypad to measure — %s" % bridge.device_reason,
              file=sys.stderr)
        bridge.stop()
        return 1

    stats = summarise_rtt(bridge.measure_link_rtt(samples=30))
    bridge.stop()
    if not stats:
        print("error: the keypad did not answer", file=sys.stderr)
        return 1

    one_way = stats["median_ms"] / 2.0
    print("")
    print("USB LINK  —  %s" % (bridge.handshake_path or "?"))
    print("")
    print("  round trip    min %.2f ms    median %.2f ms    p95 %.2f ms    max %.2f ms"
          % (stats["min_ms"], stats["median_ms"], stats["p95_ms"], stats["max_ms"]))
    print("                over %d samples" % stats["samples"])
    print("")
    print("  A round trip is host -> keypad -> host, so one way is roughly half:")
    print("  about %.1f ms for an event to reach this machine." % one_way)
    print("")
    print("  This is an UPPER BOUND. The figure includes the host's own read-poll")
    print("  granularity and the device's USB service loop, so the true transport")
    print("  delay is at or below it.")
    print("")
    print("  It does not touch the score. Every event is timestamped on the RP2040")
    print("  by its hardware timer before it is sent, so transport delay changes when")
    print("  the host LEARNS of something, never when it happened. What this bounds is")
    print("  how far the screen trails the board: %.1f ms against a 16.7 ms frame at"
          % one_way)
    print("  60 Hz, so under a frame.")
    print("")
    return 0


def _run_terminal(bridge: "Bridge", log: Optional[logwriter.SessionLog]) -> int:
    """Play and calibrate from the terminal, with no browser and no server.

    The device is authoritative here exactly as it is behind the page: this waits on
    the same events, folds them through the same SessionMirror, and prints what that
    mirror says. Nothing about the score changes with the front end.
    """
    ui = terminal.TerminalUI(bridge.mirror, bridge.send_command)
    bridge.on_event = ui.on_event
    bridge.start_watching()

    stop = threading.Event()

    def shutdown(signum, frame) -> None:  # noqa: ARG001
        stop.set()

    signal.signal(signal.SIGTERM, shutdown)
    try:
        ui.run(bridge.handshake_path or "?", stop)
    except KeyboardInterrupt:
        pass
    finally:
        print("")
        bridge.on_event = None
        bridge.stop()
        if log is not None:
            log.close()
            if log.run_count:
                print("Wrote %d run(s) to %s" % (log.run_count, log.csv_path))
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    if args.keyboard and args.hardware:
        print("error: --keyboard and --hardware are mutually exclusive", file=sys.stderr)
        return 2

    input_mode = "keyboard" if args.keyboard else "hardware"
    log: Optional[logwriter.SessionLog] = None

    bridge = Bridge(log, verbose=args.verbose)
    link_stats: Dict[str, Any] = {}
    replay_source: Optional[replay.ReplaySource] = None

    # --- device or replay or neither -------------------------------------
    if args.replay:
        try:
            replay_source = replay.ReplaySource(args.replay, speed=args.replay_speed)
        except replay.ReplayError as exc:
            print("error: %s" % exc, file=sys.stderr)
            return 1
        # Not a failure to find the keypad - the keypad is deliberately never opened
        # during a replay. Two event streams sharing one sequence counter would report
        # enormous phantom gaps and leave the mirror describing neither session. Say
        # that plainly, because "not connected" reads as a fault the operator should go
        # and fix.
        bridge.replaying = True
        bridge.device_reason = (
            "replaying a recorded session — the keypad is not opened during a replay")
        print("Replaying %s: %s" % (args.replay, replay_source.describe()))

    elif not args.keyboard:
        bridge._explicit_path = args.port
        if bridge.connect_and_handshake(args.port):
            print("Keypad connected on %s (firmware %s, N=%s)"
                  % (bridge.handshake_path or "?",
                     (bridge.hello or {}).get("fw", "?"),
                     (bridge.hello or {}).get("n", "?")))
            # Measured on every launch but not announced: it costs a dozen PINGs and
            # a sixth of a second, and it is what makes a session log self-describing
            # about the link it was recorded over. Nobody starting a game needs to read
            # it, so `--link` is where it is reported.
            link_stats.update(summarise_rtt(bridge.measure_link_rtt()))
        elif bridge.handshake_path is not None:
            print("A serial port was found at %s but it did not identify itself as a "
                  "GRID PULSE keypad. Continuing in keyboard mode."
                  % bridge.handshake_path)
        if not bridge.connected:
            if args.hardware or args.terminal:
                print("error: %s was requested but %s"
                      % ("--terminal" if args.terminal else "--hardware",
                         bridge.device_reason),
                      file=sys.stderr)
                bridge.stop()
                if log is not None:
                    log.close()
                return 1
            print("No keypad detected — starting in keyboard mode. %s"
                  % bridge.device_reason)
            print("Plug the keypad in at any time; it will be picked up without a "
                  "restart.")

    if args.link:
        return _report_link(bridge)

    # Opened here rather than before the device, so the session header can record the
    # link it was measured over. A log that describes its own transport is worth more
    # than one written a second earlier.
    if not args.no_log:
        log = logwriter.SessionLog(input_mode=input_mode)
        try:
            log.open(link=link_stats)
        except OSError as exc:
            print("warning: cannot write logs (%s); continuing without them" % exc,
                  file=sys.stderr)
            log = None
    bridge.log = log

    # --- terminal mode ------------------------------------------------------
    #
    # Returns here rather than falling through: no HTTP server, no port bound, no page
    # served. "Without the GUI" ought to mean the GUI is not running, not that it is
    # running and merely unopened.
    if args.terminal:
        return _run_terminal(bridge, log)

    # --- server ------------------------------------------------------------
    #
    # The sink is bound to the bridge, not to whether a device happened to be present
    # at launch. Binding it to the launch-time state was why plugging the keypad in
    # afterwards left every command rejected: the device was found, the browser
    # offered it, and the POST had nowhere to go.
    command_sink = None
    if not args.keyboard:
        def command_sink(name: str, sink_args: Dict[str, Any]) -> bool:  # noqa: F811
            return bridge.send_command(name, sink_args)

    context = server.ServerContext(
        broadcaster=bridge.broadcaster,
        status_provider=bridge.status,
        command_sink=command_sink,
    )

    http_port = server.find_free_port(args.http_port)
    try:
        httpd = server.GridPulseServer(("127.0.0.1", http_port), context,
                                       verbose=args.verbose)
    except OSError as exc:
        print("error: cannot bind 127.0.0.1:%d (%s)" % (http_port, exc), file=sys.stderr)
        bridge.stop()
        if log is not None:
            log.close()
        return 1

    url = "http://127.0.0.1:%d/" % http_port

    # --- shutdown handling ---------------------------------------------------
    def shutdown(signum, frame) -> None:  # noqa: ARG001
        print("\nShutting down…")
        context.shutdown_event.set()
        threading.Thread(target=httpd.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # --selftest opens the calibration screen; it no longer fires SELFTEST here.
    # Firing it at connect time meant the walk began before the browser had even been
    # launched, so the operator never saw it - and with a five-second timeout per cell
    # and a cell marked dead after two misses, an unattended walk could quietly shrink
    # the alphabet. The page now shows the instructions and starts the walk on a click.
    bridge.selftest_requested = bool(args.selftest)
    # Replay owns the event stream, and --keyboard means no serial port is opened at
    # all; watching for a device in either case would be wrong rather than merely
    # useless.
    if replay_source is None and not args.keyboard:
        bridge.start_watching()
    if replay_source is not None:
        replay_source.start(lambda event: (
            bridge.mirror.apply(event),
            bridge.broadcaster.publish(event),
        ))

    print("GRID PULSE is serving at %s" % url)
    if log is not None:
        print("Session log: %s" % log.jsonl_path)
    print("Press Ctrl-C to stop.")

    if not args.no_browser:
        # Opened on a timer so the server is already accepting connections.
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        context.shutdown_event.set()
        httpd.server_close()
        if replay_source is not None:
            replay_source.stop()
        bridge.stop()
        if log is not None:
            log.close()
            if log.run_count:
                print("Wrote %d run(s) to %s" % (log.run_count, log.csv_path))

    return 0
