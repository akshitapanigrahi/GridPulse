"""Hot-plug: the keypad appearing or vanishing while the host is already running.

WHY THIS EXISTS
---------------
Everything about device presence used to be decided once, at launch, in three
independent places - and all three had to change for a mid-session plug to work:

  * ``run.sh`` passed ``--keyboard`` when it found nothing, which switches the serial
    path off entirely. Plugging a keypad in afterwards could not possibly be noticed.
  * ``main()`` bound ``command_sink`` only ``if bridge.connected``, so even a device
    found later had nowhere to send commands: the UI would offer the keypad and every
    START would come back rejected.
  * A failed handshake called ``bridge.stop()``, which sets the shutdown flag. That
    ended the watcher along with the connection and made the first failure permanent.

The tests below drive the watcher's steps directly rather than waiting on its timer,
so they assert the behaviour without being timing-dependent.
"""

from __future__ import annotations

import io
import os
import pty
import sys
import time
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import cli, protocol, sse  # noqa: E402

HELLO_LINE = "EV 2 7199553 HELLO proto=1 fw=1.0.0 board=gridpulse-5x5 n=25 pins_ok=1"


class FakeDevice:
    """A pty standing in for the keypad, which answers PING like the firmware does."""

    def __init__(self) -> None:
        self.master_fd, self.slave_fd = pty.openpty()
        self.path = os.ttyname(self.slave_fd)
        self._stop = False
        self._thread = None

    def serve_ping(self) -> None:
        import threading

        def loop() -> None:
            os.set_blocking(self.master_fd, False)
            while not self._stop:
                try:
                    chunk = os.read(self.master_fd, 4096)
                except (BlockingIOError, OSError):
                    chunk = b""
                if b"PING" in chunk:
                    os.write(self.master_fd, protocol.frame(HELLO_LINE))
                time.sleep(0.005)

        self._thread = threading.Thread(target=loop, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop = True
        if self._thread is not None:
            self._thread.join(timeout=1.0)
        for fd in (self.master_fd, self.slave_fd):
            try:
                os.close(fd)
            except OSError:
                pass


class CollectingBroadcaster(sse.Broadcaster):
    """Records published events so a test can assert on what the browser would see."""

    def __init__(self) -> None:
        super().__init__()
        self.events = []

    def publish(self, event) -> None:
        self.events.append(event)
        super().publish(event)

    def links(self):
        return [e for e in self.events if e.get("type") == "LINK"]


class TestHotPlug(unittest.TestCase):
    def setUp(self) -> None:
        self.device = FakeDevice()
        self.bridge = cli.Bridge(log=None)
        self.bridge.broadcaster = CollectingBroadcaster()
        # The bridge reports plug and unplug on stdout. Capture it for the whole class:
        # one test asserts on it, and the rest would otherwise scatter device chatter
        # through the suite's output.
        self.printed = io.StringIO()
        self._real_stdout, sys.stdout = sys.stdout, self.printed

    def tearDown(self) -> None:
        sys.stdout = self._real_stdout
        self.bridge.stop()
        self.device.close()

    def test_a_keypad_plugged_in_after_launch_is_picked_up(self):
        """The headline case: started with nothing, plugged in later."""
        self.bridge._explicit_path = self.device.path
        self.assertFalse(self.bridge.connected)

        self.device.serve_ping()
        self.bridge._try_attach()

        self.assertTrue(self.bridge.connected, self.bridge.device_reason)
        links = self.bridge.broadcaster.links()
        self.assertEqual(len(links), 1)
        self.assertTrue(links[0]["connected"])
        # The browser needs the alphabet size to render the grid correctly.
        self.assertEqual(links[0]["n"], 25)

    def test_an_unplug_is_announced_without_waiting_for_a_read_to_fail(self):
        """The device node disappears at once; a read only fails when the driver
        notices. Watching the node is what makes the UI grey out as the cable leaves
        rather than whenever the next byte happened to be due."""
        self.device.serve_ping()
        self.assertTrue(
            self.bridge.connect_and_handshake(self.device.path, timeout=3.0)
        )
        self.bridge.broadcaster.events.clear()

        # Simulate the node vanishing.
        real_exists = os.path.exists
        os.path.exists = lambda p: False if p == self.device.path else real_exists(p)
        try:
            self.bridge._check_still_there()
        finally:
            os.path.exists = real_exists

        self.assertFalse(self.bridge.connected)
        links = self.bridge.broadcaster.links()
        self.assertEqual(len(links), 1)
        self.assertFalse(links[0]["connected"])

    def test_releasing_the_device_leaves_the_bridge_watching(self):
        """THE REGRESSION. Releasing must not set the shutdown flag.

        connect_and_handshake used to call stop() when a device failed to identify
        itself, which ends the watcher too - so one non-keypad serial device on the
        machine would stop the host ever noticing a real one.
        """
        self.device.serve_ping()
        self.assertTrue(
            self.bridge.connect_and_handshake(self.device.path, timeout=3.0)
        )
        self.bridge._release("unplugged", announce=True)

        self.assertFalse(self.bridge.connected)
        self.assertIsNone(self.bridge.port)
        self.assertFalse(
            self.bridge._stop.is_set(),
            "releasing the device must not shut the bridge down",
        )

    def test_plugging_and_unplugging_are_reported_in_the_terminal(self):
        """Losing the keypad mid-session is the sort of thing someone notices ten
        minutes later and cannot explain. The browser only ever shows the current
        state; the terminal is what records that it changed, and when."""
        self.device.serve_ping()
        self.bridge._explicit_path = self.device.path

        self.bridge._try_attach()
        self.bridge._handle_disconnect("the keypad was unplugged")
        self.bridge._try_attach()

        printed = self.printed.getvalue()
        self.assertIn("Keypad disconnected", printed)
        self.assertIn("the keypad was unplugged", printed)
        # And it says the host is still looking, so nobody restarts it needlessly.
        self.assertIn("Watching for it to come back", printed)
        # Both attaches announce themselves, so a reconnect is visible too.
        self.assertEqual(printed.count("Keypad connected on"), 2)

    def test_a_failed_handshake_is_not_announced_as_a_disconnect(self):
        """The watcher retries every second. A non-keypad serial device sitting on the
        machine would otherwise print a disconnect line per second forever."""
        # No serve_ping: the port opens but never identifies itself.
        self.bridge._explicit_path = self.device.path
        self.bridge._try_attach()
        self.bridge._try_attach()

        self.assertFalse(self.bridge.connected)
        self.assertNotIn("Keypad disconnected", self.printed.getvalue())
        self.assertNotIn("Keypad connected", self.printed.getvalue())

    def test_a_device_can_be_picked_up_again_after_an_unplug(self):
        """Plug, unplug, plug. The second attach must be as good as the first."""
        self.device.serve_ping()
        self.bridge._explicit_path = self.device.path

        self.bridge._try_attach()
        self.assertTrue(self.bridge.connected)

        self.bridge._handle_disconnect("unplugged")
        self.assertFalse(self.bridge.connected)

        self.bridge._try_attach()
        self.assertTrue(self.bridge.connected, self.bridge.device_reason)

        links = self.bridge.broadcaster.links()
        self.assertEqual([e["connected"] for e in links], [True, False, True])

    def test_a_reattach_does_not_report_a_phantom_sequence_gap(self):
        """A reconnect is a fresh device boot, so seq restarts from 1. Carrying the
        old tracker over would report every event of the new session as missing and
        flag a clean run as having lost data."""
        self.device.serve_ping()
        self.bridge._explicit_path = self.device.path
        self.bridge._try_attach()

        self.bridge.sequence.observe(500)
        self.bridge._handle_disconnect("unplugged")
        self.bridge._try_attach()

        self.assertEqual(self.bridge.sequence.missing, 0)

    def test_the_command_sink_survives_a_device_arriving_late(self):
        """main() binds the sink to the bridge, not to launch-time presence."""
        sink_calls = []

        def command_sink(name, args):
            sink_calls.append(name)
            return self.bridge.send_command(name, args)

        # No device yet: the sink exists and simply reports failure.
        self.assertFalse(command_sink("PING", {}))

        self.device.serve_ping()
        self.bridge._explicit_path = self.device.path
        self.bridge._try_attach()

        self.assertTrue(command_sink("PING", {}))
        self.assertEqual(sink_calls, ["PING", "PING"])


if __name__ == "__main__":
    unittest.main()
