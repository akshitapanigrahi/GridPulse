"""Bridge handshake and reader tests, over a real pseudo-terminal.

WHY A PTY
---------
These exercise the actual code path a real keypad takes: a character device opened by
``SerialPort`` with termios in raw mode, bytes arriving asynchronously, and a reader
thread parsing them. A mock object in place of the port would have passed while the
real thing was broken - which is exactly what happened.

The bug these were written for: ``cli.main()`` called ``wait_for_hello()`` BEFORE
``bridge.start()``. HELLO arrives asynchronously and is parsed on the reader thread,
so with no reader running the handshake could never succeed, however healthy the
device. The board was fine; the host never listened.
"""

from __future__ import annotations

import io
import os
import pty
import sys
import threading
import time
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import cli, protocol  # noqa: E402

HELLO_LINE = "EV 2 7199553 HELLO proto=1 fw=1.0.0 board=gridpulse-5x5 n=25 pins_ok=1"


class FakeDevice:
    """The device end of a pty pair. Writes protocol lines the bridge must read."""

    def __init__(self) -> None:
        self.master_fd, self.slave_fd = pty.openpty()
        self.path = os.ttyname(self.slave_fd)

    def send(self, payload: str) -> None:
        os.write(self.master_fd, protocol.frame(payload))

    def read_available(self, timeout: float = 0.5) -> bytes:
        """Reads whatever the host has written to us."""
        os.set_blocking(self.master_fd, False)
        deadline = time.monotonic() + timeout
        data = b""
        while time.monotonic() < deadline:
            try:
                chunk = os.read(self.master_fd, 4096)
            except BlockingIOError:
                chunk = b""
            except OSError:
                break
            if chunk:
                data += chunk
            elif data:
                break
            else:
                time.sleep(0.01)
        return data

    def serve_ping(self, reply: str = None, timeout: float = 3.0) -> threading.Thread:
        """Answer the host's PING with a HELLO, the way the firmware does.

        Waits for an actual parsed PING rather than for "any bytes". Before the host
        calls open() the pty slave is still in its default line discipline with echo
        ON, so anything written to the device end comes straight back - and a responder
        that fired on the first bytes it saw would answer its own echo and then miss
        the real command.
        """
        payload = reply or HELLO_LINE

        def run() -> None:
            os.set_blocking(self.master_fd, False)
            buffer = bytearray()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                try:
                    chunk = os.read(self.master_fd, 4096)
                except (BlockingIOError, OSError):
                    chunk = b""
                if not chunk:
                    time.sleep(0.005)
                    continue
                buffer += chunk
                while b"\n" in buffer:
                    line, _, rest = bytes(buffer).partition(b"\n")
                    buffer = bytearray(rest)
                    error, command = protocol.parse_command(line)
                    if error == protocol.OK and command["name"] == "PING":
                        self.send(payload)
                        return

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        return thread

    def close(self) -> None:
        for fd in (self.master_fd, self.slave_fd):
            try:
                os.close(fd)
            except OSError:
                pass


class TestHandshake(unittest.TestCase):
    def setUp(self) -> None:
        self.device = FakeDevice()
        self.bridge = cli.Bridge(log=None)

    def tearDown(self) -> None:
        self.bridge.stop()
        self.device.close()

    def test_handshake_succeeds_when_the_reader_is_running(self):
        """THE REGRESSION TEST. Reader first, then handshake."""
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()

        self.device.send(HELLO_LINE)

        self.assertTrue(self.bridge.wait_for_hello(timeout=3.0),
                        "the bridge did not see a HELLO it was sent")
        self.assertEqual(self.bridge.hello["fw"], "1.0.0")
        self.assertEqual(self.bridge.hello["n"], 25)
        self.assertEqual(self.bridge.hello["proto"], protocol.PROTOCOL_VERSION)

    def test_handshake_cannot_succeed_without_a_reader(self):
        """Pins down the failure mode, so the ordering requirement is explicit.

        Without the reader thread nothing parses the reply, so the handshake times out
        even though the device answered correctly. If a future refactor makes this
        pass, the ordering constraint has been removed and this test should go with it.
        """
        self.assertTrue(self.bridge.connect(self.device.path))
        self.device.send(HELLO_LINE)
        # Deliberately NOT calling start().
        self.assertFalse(self.bridge.wait_for_hello(timeout=0.3))

    def test_the_handshake_actually_sends_a_ping(self):
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        self.bridge.wait_for_hello(timeout=0.3)  # will time out; we want the write

        written = self.device.read_available()
        self.assertTrue(written, "the bridge sent nothing to the device")
        error, command = protocol.parse_command(written.strip())
        self.assertEqual(error, protocol.OK)
        self.assertEqual(command["name"], "PING")

    def test_a_silent_device_fails_the_handshake(self):
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        self.assertFalse(self.bridge.wait_for_hello(timeout=0.3))
        self.assertIsNone(self.bridge.hello)

    def test_a_device_talking_nonsense_fails_the_handshake(self):
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        os.write(self.device.master_fd, b"not a protocol line at all\n")
        os.write(self.device.master_fd, b"\x00\xff\x01garbage\n")
        self.assertFalse(self.bridge.wait_for_hello(timeout=0.4))
        # And the reader thread must have survived it.
        self.device.send(HELLO_LINE)
        self.assertTrue(self.bridge.wait_for_hello(timeout=2.0))


class TestConnectAndHandshake(unittest.TestCase):
    """The real regression: the ordering that main() once got wrong."""

    def setUp(self) -> None:
        self.device = FakeDevice()
        self.bridge = cli.Bridge(log=None)

    def tearDown(self) -> None:
        self.bridge.stop()
        self.device.close()

    def test_a_healthy_device_completes_the_handshake(self):
        # A real device answers the PING rather than speaking unprompted.
        self.device.serve_ping()
        self.assertTrue(self.bridge.connect_and_handshake(self.device.path, timeout=3.0),
                        "a device that answers PING must complete the handshake")
        self.assertTrue(self.bridge.connected)
        self.assertEqual(self.bridge.hello["fw"], "1.0.0")
        self.assertEqual(self.bridge.handshake_path, self.device.path)

    def test_an_early_hello_does_not_break_the_handshake(self):
        """The firmware announces itself on DTR, i.e. during the host's own open().

        Anything already queued at that moment is discarded by the tcflush in
        SerialPort.open(), which is deliberate - stale bytes from a previous session
        must never be parsed as current. The handshake still completes because the
        device also answers the PING that follows.
        """
        self.device.send(HELLO_LINE)  # arrives before the port is opened
        self.device.serve_ping()
        self.assertTrue(self.bridge.connect_and_handshake(self.device.path, timeout=3.0))
        self.assertTrue(self.bridge.connected)

    def test_bytes_queued_before_open_are_discarded(self):
        """Pins the flush, so nobody 'helpfully' removes it later.

        A partial line left over from a previous session would otherwise be glued to
        the front of the first real line and corrupt it.
        """
        self.device.send(HELLO_LINE)
        os.write(self.device.master_fd, b"EV 99 1 PARTIAL-LINE-WITH-NO-TERMINATOR")

        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        # Nothing from before the open may reach the parser.
        time.sleep(0.2)
        self.assertIsNone(self.bridge.hello)

        # And a line sent afterwards is clean, not glued to the stale fragment.
        self.device.send(HELLO_LINE)
        self.assertTrue(self.bridge.wait_for_hello(timeout=2.0))
        self.assertEqual(self.bridge.hello["seq"], 2)

    def test_a_silent_device_is_rejected_and_the_port_released(self):
        self.assertFalse(self.bridge.connect_and_handshake(self.device.path, timeout=0.3))
        self.assertFalse(self.bridge.connected)
        self.assertIn("handshake", self.bridge.device_reason)
        # The port must not be left held open, or keyboard-mode fallback would block
        # anything else from using it.
        self.assertIsNone(self.bridge.port)
        # But the path is remembered so the message can name it.
        self.assertEqual(self.bridge.handshake_path, self.device.path)

    def test_a_missing_port_is_rejected_without_a_handshake_attempt(self):
        self.assertFalse(self.bridge.connect_and_handshake("/dev/definitely-not-here"))
        self.assertFalse(self.bridge.connected)
        self.assertIsNone(self.bridge.handshake_path)


class TestReaderThread(unittest.TestCase):
    def setUp(self) -> None:
        self.device = FakeDevice()
        self.bridge = cli.Bridge(log=None)
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()

    def tearDown(self) -> None:
        self.bridge.stop()
        self.device.close()

    def _wait_for(self, predicate, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.01)
        return predicate()

    def test_folds_a_run_into_the_mirror(self):
        self.device.send(HELLO_LINE)
        self.device.send("EV 3 8013001 MODE mode=EVAL state=RUNNING seed=3735928559 n=25")
        self.device.send("EV 4 8013042 TARGET cell=13 idx=1 repeat=0")
        self.device.send("EV 5 8231688 HIT cell=13 rt_us=218646 sc=1 si=0 streak=1")
        self.device.send("EV 6 8231701 TARGET cell=2 idx=2 repeat=0")
        self.device.send("EV 7 8462203 MISS pressed=7 target=2 sc=1 si=1")

        self.assertTrue(self._wait_for(lambda: self.bridge.mirror.incorrect == 1))
        self.assertEqual(self.bridge.mirror.correct, 1)
        self.assertEqual(self.bridge.mirror.target_cell, 2)
        self.assertEqual(self.bridge.mirror.n, 25)

    def test_detects_a_sequence_gap(self):
        # stderr captured only to keep the suite's own output clean; the message it
        # would print is asserted in the test below.
        real_stderr, sys.stderr = sys.stderr, io.StringIO()
        try:
            self.device.send(HELLO_LINE)
            self.assertTrue(self._wait_for(lambda: self.bridge.hello is not None))
            # Jump the sequence: events 3 and 4 were lost on the link.
            self.device.send("EV 5 9000000 TICK t_run_us=1000 sc=0 si=0 b_mbps=0")
            self.assertTrue(self._wait_for(lambda: self.bridge.sequence.gaps > 0))
        finally:
            sys.stderr = real_stderr
        self.assertEqual(self.bridge.sequence.missing, 2)

    def test_a_sequence_gap_is_reported_in_the_terminal(self):
        """Lost events are a diagnostic for whoever is running the bridge, not for the
        player, so they are printed here rather than surfaced in the UI. The message
        has to say the score is safe, or it reads as a scoring fault."""
        captured = io.StringIO()
        real_stderr, sys.stderr = sys.stderr, captured
        try:
            self.device.send(HELLO_LINE)
            self.assertTrue(self._wait_for(lambda: self.bridge.hello is not None))
            self.device.send("EV 5 9000000 TICK t_run_us=1000 sc=0 si=0 b_mbps=0")
            self.assertTrue(self._wait_for(lambda: self.bridge.sequence.gaps > 0))
            # The reader thread does the printing; give it a moment to flush.
            self._wait_for(lambda: "link:" in captured.getvalue())
        finally:
            sys.stderr = real_stderr

        printed = captured.getvalue()
        self.assertIn("2 event(s) lost", printed)
        self.assertIn("device tally is unaffected", printed)

    def test_lines_split_across_reads_are_reassembled(self):
        line = protocol.frame(HELLO_LINE)
        for index in range(len(line)):
            os.write(self.device.master_fd, line[index:index + 1])
            time.sleep(0.001)
        self.assertTrue(self._wait_for(lambda: self.bridge.hello is not None))

    def test_status_reflects_the_handshake(self):
        self.device.send(HELLO_LINE)
        self.assertTrue(self._wait_for(lambda: self.bridge.hello is not None))
        status = self.bridge.status()
        self.assertTrue(status["device"]["connected"])
        self.assertEqual(status["device"]["firmware"], "1.0.0")
        self.assertEqual(status["device"]["n"], 25)
        self.assertEqual(status["link"]["seq_gaps"], 0)

    def test_stop_releases_the_port(self):
        self.bridge.stop()
        self.assertIsNone(self.bridge.port)


if __name__ == "__main__":
    unittest.main()


class TestLinkMeasurement(unittest.TestCase):
    """The USB leg, measured rather than quoted.

    The architecture notes put the scored-to-browser leg at "1-5 ms", derived from CDC
    framing rather than observed. These cover the machinery that turns that estimate
    into a number from the actual cable, and the honesty constraints on how it is
    reported: it is a ROUND TRIP and an upper bound, because a one-way figure would
    need a shared clock and there isn't one.
    """

    def setUp(self) -> None:
        self.device = FakeDevice()
        self.bridge = cli.Bridge(log=None)

    def tearDown(self) -> None:
        self.bridge.stop()
        self.device.close()

    def test_round_trips_are_measured_against_a_real_device(self):
        self.device.serve_ping()
        self.assertTrue(
            self.bridge.connect_and_handshake(self.device.path, timeout=3.0))

        # serve_ping answers once, so keep answering for the rest of the samples.
        def keep_answering():
            while not stop.is_set():
                self.device.serve_ping()
                time.sleep(0.02)
        stop = threading.Event()
        threading.Thread(target=keep_answering, daemon=True).start()
        try:
            rtts = self.bridge.measure_link_rtt(samples=4, timeout=1.0)
        finally:
            stop.set()

        self.assertTrue(rtts, "no round trip completed against a device that answers")
        for rtt in rtts:
            self.assertGreater(rtt, 0.0)
            self.assertLess(rtt, 1.0)

    def test_a_silent_device_yields_no_samples_rather_than_hanging(self):
        """No serve_ping. A link that never answers must not block the launch path."""
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        self.bridge.connected = True
        started = time.monotonic()
        rtts = self.bridge.measure_link_rtt(samples=2, timeout=0.2)
        self.assertEqual(rtts, [])
        self.assertLess(time.monotonic() - started, 3.0)

    def test_the_summary_reports_a_round_trip_and_says_so(self):
        stats = cli.summarise_rtt([0.001, 0.002, 0.003, 0.004])
        self.assertEqual(stats["samples"], 4)
        self.assertAlmostEqual(stats["min_ms"], 1.0)
        self.assertAlmostEqual(stats["max_ms"], 4.0)
        # The caveats are part of the number, not decoration around it.
        self.assertIn("round trip", stats["note"])
        self.assertIn("upper bound", stats["note"])

    def test_no_samples_produces_no_claim(self):
        self.assertEqual(cli.summarise_rtt([]), {})

    def test_every_event_carries_when_the_host_learned_of_it(self):
        """host_us is what makes transport jitter recoverable from a log afterwards.
        Its absolute value is meaningless against the device's t_us - unrelated clock
        origins - but its spread is the delay a display would feel."""
        recorded = []
        self.bridge.log = None
        self.bridge.on_event = recorded.append
        self.assertTrue(self.bridge.connect(self.device.path))
        self.bridge.start()
        self.device.send(HELLO_LINE)
        self.assertTrue(self._wait_for(lambda: len(recorded) > 0))

        self.assertIn("host_us", recorded[0])
        self.assertIsInstance(recorded[0]["host_us"], int)
        # And it is a different clock from the device's, not a copy of it.
        self.assertNotEqual(recorded[0]["host_us"], recorded[0]["t_us"])

    def _wait_for(self, predicate, timeout: float = 3.0) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.01)
        return predicate()
