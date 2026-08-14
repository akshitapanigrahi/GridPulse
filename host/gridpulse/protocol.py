"""GRID PULSE wire protocol: framing, validation and parsing.

Implements docs/PROTOCOL.md v1. This is an independent implementation of the same
spec as ``firmware/src/pure/protocol.cpp``; the two are validated against the same
golden cases in ``tests/vectors/protocol_cases.json``, so a framing disagreement
between the device and the host cannot survive the test suite.

Design notes
------------
* Every function is total: a malformed line produces an error token, never an
  exception. The reader thread must never die because a USB glitch delivered half a
  line.
* Nothing is applied partially. A line either validates completely - length,
  character set, CRC, structure - or it is discarded whole.
* Error tokens match ``ParseErrorText()`` in the C++ implementation exactly, so the
  same vectors drive both.

Standard library only.
"""

from __future__ import annotations

from typing import Dict, Optional, Tuple

PROTOCOL_VERSION = 1

# Must match kMaxLineBytes in firmware/src/pure/config.h. See docs/PROTOCOL.md §1 for
# how the figure is derived from the worst-case END message.
MAX_LINE_BYTES = 320

CRC_HEX_DIGITS = 4

EVENT_PREFIX = "EV"
COMMAND_PREFIX = "CMD"

# Error tokens, identical to the C++ ParseErrorText() strings.
OK = "ok"
EMPTY_LINE = "empty_line"
LINE_TOO_LONG = "line_too_long"
BAD_PREFIX = "bad_prefix"
BAD_CHAR = "non_printable_character"
MISSING_CRC = "missing_crc_field"
MALFORMED_CRC = "malformed_crc_field"
BAD_CRC = "crc_mismatch"
UNKNOWN_NAME = "unknown_command"
MISSING_ARG = "missing_argument"
BAD_ARG = "bad_argument"

EVENT_TYPES = frozenset(
    {"HELLO", "MODE", "SELFTEST", "TARGET", "HIT", "MISS", "TICK", "END", "HIST", "LOG"}
)

# Fields that carry an unsigned integer, so callers get numbers rather than strings.
_INT_FIELDS = frozenset(
    {
        "proto", "n", "seed", "cell", "gpio", "pixel", "pass", "idx", "rt_us",
        "sc", "si", "streak", "pressed", "target", "t_run_us", "t_us", "b_mbps",
        "draws", "repeats", "max_streak", "min_us", "p50_us", "p95_us", "p99_us",
        "off", "pct",
    }
)

_BOOL_FIELDS = frozenset({"repeat", "pins_ok", "force"})


class ProtocolError(Exception):
    """Raised only by the command *builder*, never by the parsers."""


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.

    Canonical check value: ``crc16_ccitt_false(b"123456789") == 0x29B1``.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def frame(payload: str) -> bytes:
    """Wraps a payload in the CRC framing and terminates it."""
    crc = crc16_ccitt_false(payload.encode("ascii"))
    return ("%s %04X\n" % (payload, crc)).encode("ascii")


def verify_line(line: bytes) -> Tuple[str, Optional[str]]:
    """Validates framing and the CRC.

    Returns ``(error_token, payload)``. On success the token is ``OK`` and the
    payload is everything before the space preceding the CRC field. On failure the
    payload is ``None``.

    Mirrors ``VerifyLine`` in firmware/src/pure/protocol.cpp step for step.
    """
    if line is None:
        return EMPTY_LINE, None

    # Tolerate CRLF, as docs/PROTOCOL.md §1 requires.
    while line and line[-1:] in (b"\n", b"\r"):
        line = line[:-1]

    if not line:
        return EMPTY_LINE, None
    if len(line) + 1 > MAX_LINE_BYTES:
        return LINE_TOO_LONG, None

    for byte in line:
        if byte < 0x20 or byte > 0x7E:
            return BAD_CHAR, None

    text = line.decode("ascii")

    if len(text) < CRC_HEX_DIGITS + 2:
        return MISSING_CRC, None
    crc_start = len(text) - CRC_HEX_DIGITS
    if text[crc_start - 1] != " ":
        return MISSING_CRC, None

    crc_field = text[crc_start:]
    expected = 0
    for char in crc_field:
        if char in "0123456789":
            nibble = ord(char) - ord("0")
        elif char in "ABCDEF":
            nibble = ord(char) - ord("A") + 10
        else:
            return MALFORMED_CRC, None
        expected = (expected << 4) | nibble

    payload = text[: crc_start - 1]
    if crc16_ccitt_false(payload.encode("ascii")) != expected:
        return BAD_CRC, None

    return OK, payload


def _parse_fields(tokens) -> Tuple[str, Dict[str, object]]:
    """Turns ``key=value`` tokens into a dict, with typed values."""
    fields: Dict[str, object] = {}
    for token in tokens:
        if "=" not in token:
            return BAD_ARG, {}
        # Split on the FIRST '=' so a value containing one cannot shift the key.
        key, _, raw = token.partition("=")
        if not key:
            return BAD_ARG, {}
        if key in _INT_FIELDS:
            if not raw or not raw.isdigit():
                return BAD_ARG, {}
            fields[key] = int(raw)
        elif key in _BOOL_FIELDS:
            if raw not in ("0", "1"):
                return BAD_ARG, {}
            fields[key] = raw == "1"
        else:
            fields[key] = raw
    return OK, fields


def parse_event(line: bytes) -> Tuple[str, Optional[Dict[str, object]]]:
    """Parses a device -> host ``EV`` line.

    Returns ``(error_token, event)``. The event dict always carries ``seq``, ``t_us``
    and ``type``, plus every ``key=value`` field with integers already converted.

    An unrecognised TYPE is *not* an error: docs/PROTOCOL.md §8 requires unknown
    types to be ignored so a newer device can add one without breaking an older host.
    The type is returned as-is and the caller decides.
    """
    error, payload = verify_line(line)
    if error != OK:
        return error, None
    assert payload is not None

    parts = payload.split(" ")
    if len(parts) < 4 or parts[0] != EVENT_PREFIX:
        return BAD_PREFIX, None

    if not parts[1].isdigit() or not parts[2].isdigit():
        return BAD_ARG, None

    field_error, fields = _parse_fields(parts[4:])
    if field_error != OK:
        return field_error, None

    event: Dict[str, object] = {
        "seq": int(parts[1]),
        "t_us": int(parts[2]),
        "type": parts[3],
    }
    event.update(fields)
    return OK, event


def parse_command(line: bytes) -> Tuple[str, Optional[Dict[str, object]]]:
    """Parses a host -> device ``CMD`` line.

    Present so the host suite can be driven by the same golden vectors as the
    firmware parser - the host does not normally receive commands. Applies exactly
    the same validation, including rejecting a START with no mode.
    """
    error, payload = verify_line(line)
    if error != OK:
        return error, None
    assert payload is not None

    parts = payload.split(" ")
    if not parts or parts[0] != COMMAND_PREFIX:
        return BAD_PREFIX, None
    if len(parts) < 2 or not parts[1]:
        return UNKNOWN_NAME, None

    name = parts[1]
    field_error, fields = _parse_fields(parts[2:])
    if field_error != OK:
        return field_error, None

    if name == "START":
        mode = fields.get("mode")
        if mode is None:
            # Deliberately not defaulted: a malformed command must never silently
            # start the one run that counts.
            return MISSING_ARG, None
        if mode not in ("EVAL", "PRACTICE"):
            return BAD_ARG, None
        return OK, {"name": "START", "mode": mode}

    if name == "SELFTEST":
        force = fields.get("force", False)
        if not isinstance(force, bool):
            return BAD_ARG, None
        return OK, {"name": "SELFTEST", "force": force}

    if name == "BRIGHT":
        pct = fields.get("pct")
        if pct is None:
            return MISSING_ARG, None
        if not isinstance(pct, int) or pct > 100:
            return BAD_ARG, None
        return OK, {"name": "BRIGHT", "brightness_pct": pct}

    if name in ("ABORT", "PING", "PROTO"):
        return OK, {"name": name}

    return UNKNOWN_NAME, None


def build_command(name: str, **args) -> bytes:
    """Builds a host -> device command line, CRC and all.

    Raises rather than returning an error token: a malformed command here is a bug in
    this program, not bad input from elsewhere.
    """
    if name == "START":
        mode = args.get("mode")
        if mode not in ("EVAL", "PRACTICE"):
            raise ProtocolError("START requires mode=EVAL or mode=PRACTICE, got %r" % mode)
        return frame("CMD START mode=%s" % mode)

    if name == "SELFTEST":
        return frame("CMD SELFTEST force=%d" % (1 if args.get("force") else 0))

    if name == "BRIGHT":
        pct = args.get("pct")
        if not isinstance(pct, int) or not 0 <= pct <= 100:
            raise ProtocolError("BRIGHT requires pct in 0..100, got %r" % pct)
        return frame("CMD BRIGHT pct=%d" % pct)

    if name in ("ABORT", "PING", "PROTO"):
        return frame("CMD %s" % name)

    raise ProtocolError("unknown command %r" % name)


class LineReader:
    """Reassembles protocol lines from arbitrary byte chunks.

    USB CDC delivers whatever happens to be in the endpoint buffer, so a read can
    split a line anywhere or deliver several at once. This buffers until a newline
    and enforces the length limit.

    An over-length line is dropped WHOLE, and the drop is reported, rather than being
    truncated: a truncated line could carry a plausible CRC over its own prefix and
    would then be accepted as a complete, wrong message.
    """

    def __init__(self, max_line_bytes: int = MAX_LINE_BYTES) -> None:
        self._buffer = bytearray()
        self._max = max_line_bytes
        self._overflowed = False
        self.dropped_overlong = 0

    def feed(self, chunk: bytes):
        """Yields complete lines, each including no terminator."""
        for byte in chunk:
            if byte == 0x0A:  # '\n'
                if self._overflowed:
                    self.dropped_overlong += 1
                    self._overflowed = False
                    self._buffer.clear()
                    continue
                line = bytes(self._buffer)
                self._buffer.clear()
                if line:
                    yield line
                continue

            if len(self._buffer) + 1 >= self._max:
                # Keep consuming to the newline so this line's tail does not corrupt
                # the next one.
                self._overflowed = True
                continue
            self._buffer.append(byte)

    def reset(self) -> None:
        self._buffer.clear()
        self._overflowed = False


class SequenceTracker:
    """Detects gaps in the device's monotonic event sequence.

    A gap means bytes were lost on the link. The host must surface that rather than
    silently displaying a wrong tally; the device's own END figures remain correct
    because they are computed from state the host never touched.
    """

    def __init__(self) -> None:
        self.last_seq: Optional[int] = None
        self.gaps = 0
        self.missing = 0

    def observe(self, seq: int) -> int:
        """Returns how many events appear to have been missed before this one."""
        if self.last_seq is None:
            self.last_seq = seq
            return 0

        expected = (self.last_seq + 1) & 0xFFFFFFFF
        if seq == expected:
            self.last_seq = seq
            return 0

        # A device reboot restarts the counter, and a duplicated line repeats one.
        # Neither is a gap, and neither may produce a NEGATIVE count: `missing` is a
        # running total, so a negative would subtract from it and could hide a real
        # loss later in the run. `<=` rather than `<` is the whole fix - an equal seq
        # used to fall through to the arithmetic below and yield -1.
        if seq <= self.last_seq:
            self.last_seq = seq
            return 0

        missed = seq - expected
        self.gaps += 1
        self.missing += missed
        self.last_seq = seq
        return missed

    def reset(self) -> None:
        self.last_seq = None
        self.gaps = 0
        self.missing = 0
