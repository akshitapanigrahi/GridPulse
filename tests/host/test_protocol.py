"""Host protocol tests, driven by the same golden vectors as the firmware parser.

If the Python and C++ implementations ever disagree about framing, one of them fails
these cases and the suite says which.
"""

from __future__ import annotations

import json
import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import protocol  # noqa: E402

VECTOR_DIR = os.path.join(REPO_ROOT, "tests", "vectors")


def load_vector(name):
    with open(os.path.join(VECTOR_DIR, "%s.json" % name), "r", encoding="utf-8") as fh:
        return json.load(fh)


class TestCrc(unittest.TestCase):
    def test_canonical_check_value(self):
        self.assertEqual(protocol.crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_empty_input_is_the_init_value(self):
        # CCITT-FALSE has no final XOR.
        self.assertEqual(protocol.crc16_ccitt_false(b""), 0xFFFF)

    def test_matches_golden_vectors(self):
        vectors = load_vector("crc_cases")
        for case in vectors["cases"]:
            with self.subTest(case=case["name"]):
                data = case["input"].encode("latin-1")
                self.assertEqual(protocol.crc16_ccitt_false(data), case["crc16"])

    def test_detects_every_single_bit_flip(self):
        vectors = load_vector("crc_cases")
        base = vectors["bit_flip_base"]["input"].encode("latin-1")
        base_crc = vectors["bit_flip_base"]["crc16"]
        self.assertEqual(protocol.crc16_ccitt_false(base), base_crc)

        for flip in vectors["bit_flips"]:
            with self.subTest(byte=flip["byte_index"], bit=flip["bit"]):
                mutated = bytearray(base)
                mutated[flip["byte_index"]] ^= 1 << flip["bit"]
                crc = protocol.crc16_ccitt_false(bytes(mutated))
                self.assertEqual(crc, flip["crc16"])
                self.assertNotEqual(crc, base_crc)

    def test_detects_every_bit_flip_exhaustively(self):
        base = b"EV 42 1234567 TARGET cell=13 idx=7 repeat=0"
        base_crc = protocol.crc16_ccitt_false(base)
        for index in range(len(base)):
            for bit in range(8):
                mutated = bytearray(base)
                mutated[index] ^= 1 << bit
                self.assertNotEqual(protocol.crc16_ccitt_false(bytes(mutated)), base_crc)


class TestCommandParsing(unittest.TestCase):
    """Same 40 golden cases the C++ parser is held to."""

    def test_matches_golden_vectors(self):
        vectors = load_vector("protocol_cases")
        self.assertEqual(vectors["max_line_bytes"], protocol.MAX_LINE_BYTES)

        for case in vectors["cases"]:
            with self.subTest(case=case["name"], note=case["note"]):
                line = case["line"].encode("latin-1")
                error, command = protocol.parse_command(line)
                self.assertEqual(error, case["error"])

                if case["error"] != "ok":
                    # Total rejection: nothing is returned for a bad line.
                    self.assertIsNone(command)
                    continue

                self.assertIsNotNone(command)
                expected = case["command"]
                self.assertEqual(command["name"], expected["name"])
                for key in ("mode", "force", "brightness_pct"):
                    if key in expected:
                        self.assertEqual(command[key], expected[key])

    def test_start_without_mode_is_refused(self):
        # A malformed command must never silently start the run that counts.
        error, command = protocol.parse_command(protocol.frame("CMD START"))
        self.assertEqual(error, protocol.MISSING_ARG)
        self.assertIsNone(command)

    def test_round_trips_every_command(self):
        for name, kwargs in [
            ("START", {"mode": "EVAL"}),
            ("START", {"mode": "PRACTICE"}),
            ("ABORT", {}),
            ("PING", {}),
            ("PROTO", {}),
            ("SELFTEST", {"force": True}),
            ("SELFTEST", {"force": False}),
            ("BRIGHT", {"pct": 42}),
        ]:
            with self.subTest(name=name, kwargs=kwargs):
                line = protocol.build_command(name, **kwargs)
                error, command = protocol.parse_command(line)
                self.assertEqual(error, protocol.OK)
                self.assertEqual(command["name"], name)

    def test_builder_rejects_bad_arguments(self):
        with self.assertRaises(protocol.ProtocolError):
            protocol.build_command("START", mode="SOMETHING")
        with self.assertRaises(protocol.ProtocolError):
            protocol.build_command("BRIGHT", pct=101)
        with self.assertRaises(protocol.ProtocolError):
            protocol.build_command("NOPE")

    def test_rejects_every_truncation(self):
        line = protocol.build_command("START", mode="EVAL")
        # Cutting exactly the newline is not truncation; the terminator is optional.
        self.assertEqual(protocol.parse_command(line[:-1])[0], protocol.OK)
        for cut in range(1, len(line) - 1):
            with self.subTest(cut=cut):
                self.assertNotEqual(protocol.parse_command(line[:cut])[0], protocol.OK)

    def test_rejects_every_single_byte_corruption(self):
        line = protocol.build_command("START", mode="EVAL")
        for index in range(len(line) - 1):
            for bit in range(8):
                mutated = bytearray(line)
                mutated[index] ^= 1 << bit
                with self.subTest(index=index, bit=bit):
                    self.assertNotEqual(
                        protocol.parse_command(bytes(mutated))[0], protocol.OK
                    )


class TestEventParsing(unittest.TestCase):
    def test_parses_a_hit(self):
        line = protocol.frame(
            "EV 43 1240000 HIT cell=13 rt_us=213000 sc=7 si=1 streak=3")
        error, event = protocol.parse_event(line)
        self.assertEqual(error, protocol.OK)
        self.assertEqual(event["seq"], 43)
        self.assertEqual(event["t_us"], 1240000)
        self.assertEqual(event["type"], "HIT")
        self.assertEqual(event["cell"], 13)
        self.assertEqual(event["rt_us"], 213000)
        self.assertEqual(event["streak"], 3)

    def test_integer_fields_come_back_as_integers(self):
        line = protocol.frame("EV 1 2 TARGET cell=7 idx=42 repeat=1")
        _, event = protocol.parse_event(line)
        self.assertIsInstance(event["cell"], int)
        self.assertIsInstance(event["idx"], int)
        self.assertIs(event["repeat"], True)

    def test_unknown_event_types_are_not_errors(self):
        # docs/PROTOCOL.md section 8: a newer device may add a type, and an older
        # host must ignore it rather than refuse the whole stream.
        line = protocol.frame("EV 9 100 SOMETHINGNEW foo=1")
        error, event = protocol.parse_event(line)
        self.assertEqual(error, protocol.OK)
        self.assertEqual(event["type"], "SOMETHINGNEW")

    def test_rejects_a_command_line(self):
        error, event = protocol.parse_event(protocol.frame("CMD PING"))
        self.assertEqual(error, protocol.BAD_PREFIX)
        self.assertIsNone(event)

    def test_rejects_bad_crc(self):
        line = bytearray(protocol.frame("EV 1 2 TICK sc=1 si=0 b_mbps=100"))
        line[4] ^= 0x01
        error, event = protocol.parse_event(bytes(line))
        self.assertEqual(error, protocol.BAD_CRC)
        self.assertIsNone(event)

    def test_rejects_non_numeric_header_fields(self):
        error, _ = protocol.parse_event(protocol.frame("EV x 2 TICK sc=1"))
        self.assertEqual(error, protocol.BAD_ARG)

    def test_over_length_line(self):
        payload = "EV 1 2 LOG msg=" + "x" * 400
        error, _ = protocol.parse_event(protocol.frame(payload))
        self.assertEqual(error, protocol.LINE_TOO_LONG)

    def test_non_printable_characters(self):
        error, _ = protocol.parse_event(b"EV 1 2 TIC\x00K 0000")
        self.assertEqual(error, protocol.BAD_CHAR)

    def test_never_raises_on_arbitrary_bytes(self):
        """Fuzz: the reader thread must never die because of a USB glitch."""
        import random

        rng = random.Random(1234)
        alphabet = bytes(range(0, 256))
        for _ in range(4000):
            length = rng.randrange(0, 80)
            junk = bytes(rng.choice(alphabet) for _ in range(length))
            error, event = protocol.parse_event(junk)
            self.assertIsInstance(error, str)
            if error == protocol.OK:
                self.assertIsInstance(event, dict)
            else:
                self.assertIsNone(event)


class TestLineReader(unittest.TestCase):
    def test_reassembles_split_lines(self):
        reader = protocol.LineReader()
        line = protocol.frame("EV 1 2 TICK sc=1 si=0 b_mbps=5")
        # Deliver one byte at a time, as a slow USB endpoint might.
        out = []
        for index in range(len(line)):
            out.extend(reader.feed(line[index:index + 1]))
        self.assertEqual(len(out), 1)
        self.assertEqual(protocol.parse_event(out[0])[0], protocol.OK)

    def test_splits_multiple_lines_in_one_chunk(self):
        reader = protocol.LineReader()
        chunk = protocol.frame("EV 1 2 PING") + protocol.frame("EV 2 3 PING")
        self.assertEqual(len(list(reader.feed(chunk))), 2)

    def test_drops_overlong_lines_whole(self):
        reader = protocol.LineReader()
        # A line far past the limit, followed by a good one.
        chunk = b"x" * 1000 + b"\n" + protocol.frame("EV 1 2 TICK sc=0 si=0 b_mbps=0")
        lines = list(reader.feed(chunk))
        self.assertEqual(reader.dropped_overlong, 1)
        self.assertEqual(len(lines), 1)
        # The good line that followed must be intact, not polluted by the bad one.
        self.assertEqual(protocol.parse_event(lines[0])[0], protocol.OK)

    def test_ignores_empty_lines(self):
        reader = protocol.LineReader()
        self.assertEqual(list(reader.feed(b"\n\n\n")), [])


class TestSequenceTracker(unittest.TestCase):
    def test_no_gap_on_consecutive(self):
        tracker = protocol.SequenceTracker()
        for seq in range(1, 100):
            self.assertEqual(tracker.observe(seq), 0)
        self.assertEqual(tracker.gaps, 0)

    def test_detects_a_gap_and_counts_the_missing(self):
        tracker = protocol.SequenceTracker()
        tracker.observe(1)
        self.assertEqual(tracker.observe(5), 3)
        self.assertEqual(tracker.gaps, 1)
        self.assertEqual(tracker.missing, 3)

    def test_first_observation_is_never_a_gap(self):
        tracker = protocol.SequenceTracker()
        self.assertEqual(tracker.observe(9999), 0)
        self.assertEqual(tracker.gaps, 0)

    def test_device_reboot_is_a_reset_not_a_gap(self):
        tracker = protocol.SequenceTracker()
        tracker.observe(500)
        self.assertEqual(tracker.observe(1), 0)
        self.assertEqual(tracker.gaps, 0)


if __name__ == "__main__":
    unittest.main()


class TestSequenceTrackerNeverReportsNegativeLoss(unittest.TestCase):
    """`missing` is a running total, so a negative would subtract from it and could
    hide a real loss later in the same run."""

    def test_a_repeated_sequence_number_is_not_a_negative_gap(self):
        tracker = protocol.SequenceTracker()
        tracker.observe(2)
        self.assertEqual(tracker.observe(2), 0)
        self.assertEqual(tracker.missing, 0)
        self.assertEqual(tracker.gaps, 0)

    def test_a_reboot_restarting_the_counter_is_not_a_gap(self):
        tracker = protocol.SequenceTracker()
        for seq in (40, 41, 42):
            tracker.observe(seq)
        self.assertEqual(tracker.observe(1), 0)
        self.assertEqual(tracker.missing, 0)

    def test_a_real_gap_is_still_counted_after_a_repeat(self):
        tracker = protocol.SequenceTracker()
        tracker.observe(2)
        tracker.observe(2)          # duplicate: must not bank a -1
        self.assertEqual(tracker.observe(6), 3)
        self.assertEqual(tracker.missing, 3)
        self.assertEqual(tracker.gaps, 1)
