"""Exclusive access to the keypad's serial port.

THE BUG THIS EXISTS FOR
-----------------------
A tty has no default owner. Two processes can hold the same device open and the kernel
hands each byte to whichever one reads first: nothing errors, nothing warns, the stream
is simply split between them at random.

So a stale host left running from an earlier session silently swallows part of the
device's output - including the HELLO the current host is waiting for. The failure then
surfaces as "a serial device was found but did not answer the handshake", which points
squarely at innocent hardware. The keypad is fine, the firmware is fine, and the
message sends you to a soldering iron.

That is what TIOCEXCL prevents: the second opener gets EBUSY, and the message names the
real cause.

A NOTE ON COVERAGE
------------------
The refusal itself cannot be exercised here. macOS does not honour TIOCEXCL on pseudo
terminals, which is all this suite has to work with, so a test that opened a pty twice
would pass on Linux and fail on macOS for reasons that have nothing to do with the
code. What is asserted instead is the two halves that are testable and that actually
carry the behaviour: that the claim is made on every open, and that a busy port
produces the message that names the cause. The end-to-end refusal was verified against
the real keypad.
"""

from __future__ import annotations

import errno
import fcntl
import os
import pty
import sys
import termios
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import serial_port  # noqa: E402


class TestExclusiveClaim(unittest.TestCase):
    def setUp(self) -> None:
        self.master_fd, self.slave_fd = pty.openpty()
        self.path = os.ttyname(self.slave_fd)

    def tearDown(self) -> None:
        for fd in (self.master_fd, self.slave_fd):
            try:
                os.close(fd)
            except OSError:
                pass

    def test_opening_the_port_claims_it_exclusively(self):
        """Every open asks for TIOCEXCL, so a second process cannot take half the
        stream without either side noticing."""
        seen = []
        real_ioctl = fcntl.ioctl

        def spy(fd, request, *args):
            seen.append(request)
            return real_ioctl(fd, request, *args)

        fcntl.ioctl = spy
        try:
            port = serial_port.SerialPort(self.path)
            port.open()
            port.close()
        finally:
            fcntl.ioctl = real_ioctl

        self.assertIn(termios.TIOCEXCL, seen,
                      "open() must claim the port exclusively")

    def test_a_port_that_cannot_be_claimed_is_still_usable(self):
        """The claim is a safeguard, not a requirement.

        Some platforms and some pty implementations reject the ioctl, and sharing a
        port is a hazard rather than a certainty - so a refused claim must not stop the
        game from running on hardware that is otherwise perfectly fine.
        """
        real_ioctl = fcntl.ioctl

        def refuse(fd, request, *args):
            if request == termios.TIOCEXCL:
                raise OSError(errno.ENOTTY, "not supported here")
            return real_ioctl(fd, request, *args)

        fcntl.ioctl = refuse
        try:
            port = serial_port.SerialPort(self.path)
            port.open()  # must not raise
            self.assertTrue(port.is_open() if hasattr(port, "is_open") else True)
            port.close()
        finally:
            fcntl.ioctl = real_ioctl

    def test_a_busy_port_names_the_real_cause(self):
        """EBUSY must not read as a hardware fault.

        This is the message someone sees when a previous run is still holding the
        keypad, so it has to point at the other process rather than at the device.
        """
        real_open = os.open

        def busy(path, *args, **kwargs):
            if path == self.path:
                raise OSError(errno.EBUSY, "Resource busy")
            return real_open(path, *args, **kwargs)

        os.open = busy
        try:
            port = serial_port.SerialPort(self.path)
            with self.assertRaises(serial_port.SerialError) as caught:
                port.open()
        finally:
            os.open = real_open

        message = str(caught.exception)
        self.assertIn("already open in another process", message)
        self.assertIn("GRID PULSE", message)
        # Must not blame the hardware.
        self.assertNotIn("handshake", message)


if __name__ == "__main__":
    unittest.main()
