"""GRID PULSE host bridge.

Reads the line protocol from the RP2040 keypad over USB CDC and serves the web UI
with a live event stream.

HARD CONSTRAINT: standard library only. No pip, no venv, no third-party packages,
not even pyserial - the CDC device is opened directly and configured with termios.
A grader must be able to run ./run.sh on a machine with nothing installed but a
system Python.

The host is a display and a logger. It never generates a target, never decides a hit,
and never contributes to the score; the device does all of that and sends its own
authoritative tally at end of run. See docs/ARCHITECTURE.md.
"""

__all__ = [
    "protocol",
    "serial_port",
    "reconcile",
    "logwriter",
    "sse",
    "server",
    "replay",
    "cli",
]

VERSION = "1.0.0"
