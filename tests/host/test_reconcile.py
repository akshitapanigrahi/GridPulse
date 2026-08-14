"""Host reconciliation and logging tests.

The reconciliation logic is what stands between "the host lost some events" and "the
reported score is wrong". These tests pin the rule that the DEVICE always wins and
that any disagreement is recorded rather than smoothed over.
"""

from __future__ import annotations

import csv
import json
import math
import os
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import logwriter, protocol, reconcile  # noqa: E402

VECTOR_DIR = os.path.join(REPO_ROOT, "tests", "vectors")


class TestBitRate(unittest.TestCase):
    """The host recomputes B independently; it must match the other two exactly."""

    def test_matches_golden_vectors(self):
        path = os.path.join(VECTOR_DIR, "scoring_cases.json")
        with open(path, "r", encoding="utf-8") as handle:
            vectors = json.load(handle)

        for case in vectors["cases"]:
            with self.subTest(case=case["name"], note=case["note"]):
                got = reconcile.bit_rate(
                    case["n"], case["correct"], case["incorrect"], case["elapsed_s"]
                )
                self.assertAlmostEqual(got, case["bit_rate"], places=12)
                self.assertEqual(
                    reconcile.bit_rate_mbps(
                        case["n"], case["correct"], case["incorrect"],
                        case["elapsed_s"]
                    ),
                    case["b_mbps"],
                )

    def test_clamps_and_guards(self):
        self.assertEqual(reconcile.bit_rate(25, 10, 20, 60.0), 0.0)
        self.assertEqual(reconcile.bit_rate(25, 10, 10, 60.0), 0.0)
        self.assertEqual(reconcile.bit_rate(25, 10, 0, 0.0), 0.0)
        self.assertEqual(reconcile.bit_rate(25, 10, 0, -1.0), 0.0)
        self.assertEqual(reconcile.bit_rate(2, 100, 0, 60.0), 0.0)
        self.assertEqual(reconcile.bit_rate(0, 100, 0, 60.0), 0.0)

    def test_design_points(self):
        self.assertAlmostEqual(reconcile.bit_rate(3, 60, 0, 60.0), 1.0, places=12)
        self.assertAlmostEqual(
            reconcile.bit_rate(25, 60, 0, 60.0), math.log2(24), places=12
        )


def _event(seq, device_us, kind, **fields):
    """Builds and parses one EV line.

    The header timestamp parameter is named device_us rather than t_us because END
    carries a *field* called t_us - the scored window length - and the two are
    different things.
    """
    parts = ["EV", str(seq), str(device_us), kind]
    for key, value in fields.items():
        if isinstance(value, bool):
            value = 1 if value else 0
        parts.append("%s=%s" % (key, value))
    error, event = protocol.parse_event(protocol.frame(" ".join(parts)))
    assert error == protocol.OK, error
    return event


class TestSessionMirror(unittest.TestCase):
    def test_folds_a_short_run(self):
        mirror = reconcile.SessionMirror()
        mirror.apply(_event(1, 0, "HELLO", proto=1, n=25))
        mirror.apply(_event(2, 100, "MODE", mode="EVAL", state="RUNNING",
                            seed=0xDEADBEEF, n=25))
        mirror.apply(_event(3, 110, "TARGET", cell=13, idx=1, repeat=0))
        mirror.apply(_event(4, 300, "HIT", cell=13, rt_us=190000, sc=1, si=0, streak=1))
        mirror.apply(_event(5, 310, "TARGET", cell=2, idx=2, repeat=0))
        mirror.apply(_event(6, 500, "MISS", pressed=7, target=2, sc=1, si=1))

        self.assertEqual(mirror.correct, 1)
        self.assertEqual(mirror.incorrect, 1)
        self.assertEqual(mirror.streak, 0)
        self.assertEqual(mirror.target_cell, 2)
        self.assertEqual(mirror.n, 25)

    def test_mode_running_resets_the_counters(self):
        mirror = reconcile.SessionMirror()
        mirror.apply(_event(1, 0, "MODE", mode="EVAL", state="RUNNING", seed=1, n=25))
        mirror.apply(_event(2, 1, "HIT", cell=0, rt_us=1, sc=5, si=2, streak=1))
        self.assertEqual(mirror.correct, 5)
        mirror.apply(_event(3, 2, "MODE", mode="EVAL", state="RUNNING", seed=2, n=25))
        self.assertEqual(mirror.correct, 0)
        self.assertEqual(mirror.incorrect, 0)

    def test_device_tally_wins_and_the_mismatch_is_recorded(self):
        mirror = reconcile.SessionMirror()
        mirror.apply(_event(1, 0, "MODE", mode="EVAL", state="RUNNING", seed=7, n=25))
        # The host saw only 5 hits...
        mirror.apply(_event(2, 10, "HIT", cell=1, rt_us=100, sc=5, si=0, streak=5))

        # ...but the device reports 241, because events were lost on the link.
        expected_mbps = reconcile.bit_rate_mbps(25, 241, 12, 60.0)
        mirror.apply(_event(3, 60_000_000, "END", n=25, sc=241, si=12,
                            t_us=60_000_000, b_mbps=expected_mbps,
                            reason="COMPLETE", mode="EVAL", seed=7,
                            draws=242, repeats=9, max_streak=38,
                            min_us=141002, p50_us=203118, p95_us=310447,
                            p99_us=402881))

        self.assertIsNotNone(mirror.final)
        self.assertEqual(mirror.final["correct"], 241)
        self.assertEqual(mirror.final["incorrect"], 12)
        self.assertFalse(mirror.reconciliation["agreed"])
        self.assertTrue(any("Sc" in m for m in mirror.reconciliation["mismatches"]))
        # And the device's numbers are what the mirror now reports.
        self.assertEqual(mirror.correct, 241)

    def test_agreement_is_recorded_when_they_match(self):
        mirror = reconcile.SessionMirror()
        mirror.apply(_event(1, 0, "MODE", mode="EVAL", state="RUNNING", seed=7, n=25))
        mirror.apply(_event(2, 10, "HIT", cell=1, rt_us=100, sc=3, si=1, streak=3))
        mbps = reconcile.bit_rate_mbps(25, 3, 1, 60.0)
        mirror.apply(_event(3, 60_000_000, "END", n=25, sc=3, si=1,
                            t_us=60_000_000, b_mbps=mbps, reason="COMPLETE",
                            mode="EVAL", seed=7, draws=4, repeats=0, max_streak=3,
                            min_us=1, p50_us=1, p95_us=1, p99_us=1))
        self.assertTrue(mirror.reconciliation["agreed"])
        self.assertEqual(mirror.reconciliation["mismatches"], [])

    def test_a_firmware_scoring_bug_is_caught(self):
        # The device sends a b_mbps that does not follow from its own Sc, Si, N and t.
        # Recomputing on the host is what catches it.
        mirror = reconcile.SessionMirror()
        mirror.apply(_event(1, 0, "MODE", mode="EVAL", state="RUNNING", seed=1, n=25))
        mirror.apply(_event(2, 60_000_000, "END", n=25, sc=100, si=0,
                            t_us=60_000_000, b_mbps=99999, reason="COMPLETE",
                            mode="EVAL", seed=1, draws=101, repeats=4, max_streak=100,
                            min_us=1, p50_us=1, p95_us=1, p99_us=1))
        self.assertFalse(mirror.reconciliation["agreed"])
        self.assertTrue(any("B device=" in m for m in mirror.reconciliation["mismatches"]))
        # The host's recomputation is what gets reported.
        self.assertEqual(mirror.final["b_mbps"],
                         reconcile.bit_rate_mbps(25, 100, 0, 60.0))

    def test_survives_events_with_missing_fields(self):
        # A truncated or unusual event must not take down the reader thread.
        mirror = reconcile.SessionMirror()
        for kind in ("HELLO", "MODE", "TARGET", "HIT", "MISS", "TICK", "END"):
            mirror.apply({"type": kind})
        self.assertIsInstance(mirror.snapshot(), dict)


class TestSessionLog(unittest.TestCase):
    def test_writes_jsonl_and_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = logwriter.SessionLog(log_dir=tmp, input_mode="hardware")
            log.open()
            log.write_record({"type": "TARGET", "cell": 3, "t_us": 1})
            log.write_record({"type": "HIT", "cell": 3, "t_us": 2})
            log.write_run_summary(
                {
                    "n": 25, "correct": 241, "incorrect": 12, "elapsed_s": 60.0,
                    "bit_rate": 17.494, "mode": "EVAL", "reason": "COMPLETE",
                    "max_streak": 38, "p50_us": 203118, "p95_us": 310447,
                    "p99_us": 402881, "draws": 242, "repeats": 9, "seed": 0xDEADBEEF,
                    "reconciliation": {"agreed": True, "mismatches": []},
                },
                seq_gaps=0,
            )
            log.close()

            with open(log.jsonl_path, "r", encoding="utf-8") as handle:
                records = [json.loads(line) for line in handle if line.strip()]
            self.assertEqual(records[0]["type"], "SESSION")
            self.assertEqual(records[1]["type"], "TARGET")
            self.assertEqual(records[2]["type"], "HIT")

            with open(log.csv_path, "r", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 1)
            # The four figures the assignment requires must all be in the summary.
            self.assertEqual(rows[0]["n"], "25")
            self.assertEqual(rows[0]["correct"], "241")
            self.assertEqual(rows[0]["incorrect"], "12")
            self.assertEqual(rows[0]["bit_rate_bps"], "17.4940")
            self.assertEqual(rows[0]["reconciled"], "yes")
            self.assertEqual(rows[0]["seed"], "0xDEADBEEF")

    def test_records_a_mismatch_in_the_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = logwriter.SessionLog(log_dir=tmp)
            log.open()
            log.write_run_summary(
                {
                    "n": 25, "correct": 1, "incorrect": 0, "elapsed_s": 60.0,
                    "bit_rate": 0.076, "seed": 0,
                    "reconciliation": {"agreed": False, "mismatches": ["Sc ..."]},
                },
                seq_gaps=4,
            )
            log.close()
            with open(log.csv_path, "r", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[0]["reconciled"], "MISMATCH")
            self.assertEqual(rows[0]["seq_gaps"], "4")

    def test_malformed_lines_are_recorded_not_discarded(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = logwriter.SessionLog(log_dir=tmp)
            log.open()
            log.write_raw_line(b"EV 1 2 GARB\xffAGE", error="crc_mismatch")
            log.close()
            with open(log.jsonl_path, "r", encoding="utf-8") as handle:
                records = [json.loads(line) for line in handle if line.strip()]
            raw = [r for r in records if r["type"] == "RAW"]
            self.assertEqual(len(raw), 1)
            self.assertEqual(raw[0]["error"], "crc_mismatch")


if __name__ == "__main__":
    unittest.main()
