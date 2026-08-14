"""GRID PULSE serial transport: finding and opening the keypad's CDC device.

Standard library only.
"""

from __future__ import annotations

import errno
import fcntl
import glob
import os
import platform
import termios
import time
from typing import List, Optional

# The device's USB identity. See firmware/src/usb_descriptors.c.
USB_VID = 0x2E8A
USB_PID = 0x000A

# CDC-ACM ignores the baud rate, but the tty layer still wants a valid one.
BAUD_RATE = termios.B115200

# Longest a command write may take before the device is declared unresponsive. A
# command is a few dozen bytes and drains in microseconds; a second is enormous.
WRITE_TIMEOUT_SECONDS = 1.0


class SerialError(Exception):
    """Raised for any failure to find, open or configure the device."""


class PermissionDeniedError(SerialError):
    """The device exists but this user cannot open it.

    Separate from SerialError because on Linux this has a specific, actionable fix
    that the CLI prints verbatim rather than making the user search for it.
    """


def candidate_ports() -> List[str]:
    """Lists plausible CDC device paths, most likely first.

    macOS exposes both ``/dev/tty.usbmodem*`` and ``/dev/cu.usbmodem*``. The ``cu``
    (call-out) node is the correct one: opening the ``tty`` node blocks waiting for
    DCD, which for a USB CDC device never arrives, so the open hangs forever.
    """
    system = platform.system()
    patterns: List[str]
    if system == "Darwin":
        patterns = ["/dev/cu.usbmodem*"]
    elif system == "Linux":
        patterns = ["/dev/ttyACM*", "/dev/serial/by-id/*"]
    else:
        # Nothing is guaranteed elsewhere; the caller falls back to keyboard mode.
        patterns = ["/dev/ttyACM*", "/dev/cu.usbmodem*"]

    found: List[str] = []
    for pattern in patterns:
        for path in sorted(glob.glob(pattern)):
            if path not in found:
                found.append(path)
    return found


def _linux_usb_ids(device_path: str) -> Optional[tuple]:
    """Reads VID:PID for a Linux tty device from sysfs, or None if unavailable."""
    name = os.path.basename(device_path)
    base = "/sys/class/tty/%s/device" % name
    # Walk up to the USB device node that carries the id files.
    for _ in range(6):
        vid_path = os.path.join(base, "idVendor")
        pid_path = os.path.join(base, "idProduct")
        if os.path.isfile(vid_path) and os.path.isfile(pid_path):
            try:
                with open(vid_path, "r", encoding="ascii") as handle:
                    vid = int(handle.read().strip(), 16)
                with open(pid_path, "r", encoding="ascii") as handle:
                    pid = int(handle.read().strip(), 16)
                return vid, pid
            except (OSError, ValueError):
                return None
        base = os.path.join(base, "..")
    return None


def matches_device(device_path: str) -> bool:
    """Best-effort check that a port really is a GRID PULSE keypad.

    On Linux the VID:PID is readable from sysfs, so the check is exact. macOS does
    not expose the ids on the device node without IOKit, so any ``cu.usbmodem`` is
    treated as a candidate and the real confirmation is the HELLO handshake - which
    is the authoritative check on every platform anyway.
    """
    if platform.system() == "Linux":
        ids = _linux_usb_ids(device_path)
        if ids is not None:
            return ids == (USB_VID, USB_PID)
    return True


def find_ports() -> List[str]:
    """Candidate ports that plausibly belong to the keypad."""
    return [path for path in candidate_ports() if matches_device(path)]


class SerialPort:
    """A raw, non-blocking CDC connection.

    Reads return whatever bytes are available and never block, so the reader thread
    can poll a shutdown flag between reads and exit promptly on Ctrl-C.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        self._fd: Optional[int] = None
        self._saved_attrs = None

    def open(self) -> None:
        if self._fd is not None:
            return
        try:
            # O_NONBLOCK on open matters: without it, opening a tty waits for carrier
            # detect. O_NOCTTY stops the device becoming this process's controlling
            # terminal, which would make a modem hangup deliver SIGHUP to us.
            fd = os.open(self.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except PermissionError as exc:
            raise PermissionDeniedError(
                "permission denied opening %s" % self.path
            ) from exc
        except OSError as exc:
            if exc.errno == errno.EBUSY:
                raise SerialError(
                    "%s is already open in another process - another copy of GRID "
                    "PULSE is probably still running" % self.path
                ) from exc
            raise SerialError("cannot open %s: %s" % (self.path, exc.strerror)) from exc

        # Claim the port exclusively, so a second process cannot open it behind our
        # back.
        #
        # A tty has no default owner: two processes can hold the same device and the
        # kernel hands each byte to whichever reads first. Nothing errors, nothing
        # warns - the bytes are simply split between them at random. A stale host left
        # running from an earlier session therefore swallows the HELLO the current one
        # is waiting for, and the failure surfaces as "the device did not answer the
        # handshake", which points squarely at innocent hardware. That is a genuinely
        # nasty way to lose an afternoon, and TIOCEXCL is a one-line fix: the second
        # opener now gets EBUSY and a message naming the real cause.
        try:
            fcntl.ioctl(fd, termios.TIOCEXCL)
        except OSError:
            # Not fatal. Some platforms and some pty implementations do not support it,
            # and sharing a port is only a hazard, not a certainty.
            pass

        try:
            self._saved_attrs = termios.tcgetattr(fd)

            # NOT tty.setraw(). That helper calls tcsetattr with a DRAINING mode
            # (TCSAFLUSH), which blocks until pending I/O has been dealt with - and
            # with nothing yet reading the port, that block is permanent.
            #
            # This is a live race, not a theoretical one: the firmware announces
            # itself with HELLO the moment DTR is asserted, which happens during this
            # very open(). Bytes can therefore already be queued before the first
            # tcsetattr runs. Every call below uses TCSANOW, which applies
            # immediately and never waits.
            #
            # tty.setraw was in any case redundant - the explicit configuration below
            # sets every flag it would have.
            attrs = termios.tcgetattr(fd)
            iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs

            # Raw in every direction: no CR/LF translation, no echo, no signal
            # characters, no flow control. The protocol is binary-clean ASCII and any
            # tty "helpfulness" would corrupt it.
            iflag &= ~(termios.IXON | termios.IXOFF | termios.IXANY | termios.ICRNL
                       | termios.INLCR | termios.IGNCR | termios.ISTRIP
                       | termios.INPCK | termios.BRKINT | termios.IGNBRK
                       | termios.PARMRK)
            oflag &= ~termios.OPOST
            lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON
                       | termios.ISIG | termios.IEXTEN)
            cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
            cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL

            cc = list(cc)
            cc[termios.VMIN] = 0
            cc[termios.VTIME] = 0

            termios.tcsetattr(
                fd, termios.TCSANOW,
                [iflag, oflag, cflag, lflag, BAUD_RATE, BAUD_RATE, cc],
            )
            termios.tcflush(fd, termios.TCIOFLUSH)
        except termios.error as exc:
            os.close(fd)
            raise SerialError(
                "cannot configure %s as a raw serial port: %s" % (self.path, exc)
            ) from exc

        self._fd = fd

    def read(self, max_bytes: int = 4096) -> bytes:
        """Reads whatever is available. Returns b"" when nothing is."""
        if self._fd is None:
            raise SerialError("read from a closed port")
        try:
            return os.read(self._fd, max_bytes)
        except BlockingIOError:
            return b""
        except OSError as exc:
            if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                return b""
            if exc.errno in (errno.EIO, errno.ENXIO, errno.ENODEV):
                raise SerialError("device disconnected: %s" % self.path) from exc
            raise SerialError("read error on %s: %s" % (self.path, exc.strerror)) from exc

    def write(self, data: bytes, timeout: float = WRITE_TIMEOUT_SECONDS) -> int:
        """Writes a command, retrying partial writes until `timeout` elapses.

        The port is non-blocking, so a full endpoint raises EAGAIN. Retrying is right,
        but retrying FOREVER is not: a device that stops draining would spin this loop
        at 100% CPU while holding the caller's lock, freezing the command path with no
        error and no way out. Commands are a couple of dozen bytes and the endpoint
        drains in microseconds, so anything approaching the timeout means the device is
        gone - which is a SerialError, not something to wait on.
        """
        if self._fd is None:
            raise SerialError("write to a closed port")

        deadline = time.monotonic() + timeout
        written = 0
        while written < len(data):
            try:
                written += os.write(self._fd, data[written:])
                continue
            except (BlockingIOError, OSError) as exc:
                if isinstance(exc, OSError) and not isinstance(exc, BlockingIOError):
                    if exc.errno not in (errno.EAGAIN, errno.EWOULDBLOCK):
                        raise SerialError(
                            "write error on %s: %s" % (self.path, exc.strerror)
                        ) from exc
            if time.monotonic() >= deadline:
                raise SerialError(
                    "timed out writing %d of %d bytes to %s; the device is not "
                    "draining its endpoint" % (written, len(data), self.path)
                )
            # Yield rather than spin: the endpoint drains on a USB frame boundary.
            time.sleep(0.001)
        return written

    def close(self) -> None:
        """Restores the original tty settings and closes the descriptor.

        Restoring matters: leaving a tty in raw mode can confuse whatever opens the
        device next, including a plain ``screen`` session a user tries afterwards.
        """
        if self._fd is None:
            return
        try:
            if self._saved_attrs is not None:
                termios.tcsetattr(self._fd, termios.TCSANOW, self._saved_attrs)
        except termios.error:
            # The device may already be gone; closing is still the right move.
            pass
        finally:
            try:
                os.close(self._fd)
            except OSError:
                pass
            self._fd = None

    def __enter__(self) -> "SerialPort":
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()


LINUX_PERMISSION_HELP = """\
The keypad was found at {path}, but this user cannot open it.

On most Linux distributions serial devices belong to the 'dialout' group. Add
yourself to it and log out and back in:

    sudo usermod -aG dialout $USER

Or install the udev rule shipped with this project, which grants access to this
device specifically without changing your groups:

    sudo cp docs/99-grid-pulse.rules /etc/udev/rules.d/
    sudo udevadm control --reload-rules && sudo udevadm trigger

Until then the game still runs on the keyboard: ./run.sh --keyboard
"""
