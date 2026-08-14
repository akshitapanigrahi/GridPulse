"""GRID PULSE session logging.

Writes two artefacts per session into ``logs/``:

* ``gridpulse-<ISO8601>.jsonl`` - every event, one JSON object per line, in the order
  received. This is what ``tools/validate_sequence.py`` and
  ``tools/latency_report.py`` consume, and what ``--replay`` plays back.
* ``gridpulse-<ISO8601>.csv`` - one row per completed run with the reported figures,
  so a panel of graders ends up with a single readable summary table.

Both are flushed after every write. A session that ends by someone pulling the USB
cable should still leave a complete log up to that moment.

Standard library only.
"""

from __future__ import annotations

import csv
import datetime
import json
import os
from typing import Any, Dict, Optional, TextIO

# Columns of the CSV summary. Ordered so the four figures the assignment requires -
# B, N, Sc, Si - come first and are readable at a glance.
CSV_COLUMNS = [
    "timestamp",
    "bit_rate_bps",
    "n",
    "correct",
    "incorrect",
    "elapsed_s",
    "input_mode",
    "mode",
    "reason",
    "accuracy",
    "presses_per_s",
    "max_streak",
    "p50_reaction_ms",
    "p95_reaction_ms",
    "p99_reaction_ms",
    "draws",
    "repeats",
    "seed",
    "seq_gaps",
    "reconciled",
    "spec_version",
]


def default_log_dir() -> str:
    """``logs/`` beside the repository root."""
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(os.path.dirname(here))
    return os.path.join(repo_root, "logs")


def _timestamp() -> str:
    """ISO-8601 in local time, filesystem-safe.

    Colons are illegal in filenames on Windows and awkward everywhere, so they become
    dashes. The value still sorts chronologically as a plain string.
    """
    return datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S")


class SessionLog:
    """Writes the JSON Lines event log and the CSV run summary."""

    def __init__(self, log_dir: Optional[str] = None, input_mode: str = "hardware"):
        self.log_dir = log_dir or default_log_dir()
        self.input_mode = input_mode
        self.stamp = _timestamp()
        self.jsonl_path = os.path.join(self.log_dir, "gridpulse-%s.jsonl" % self.stamp)
        self.csv_path = os.path.join(self.log_dir, "gridpulse-%s.csv" % self.stamp)
        self._jsonl: Optional[TextIO] = None
        self._csv: Optional[TextIO] = None
        self._csv_writer = None
        self.event_count = 0
        self.run_count = 0

    def open(self, link: Optional[Dict[str, Any]] = None) -> None:
        os.makedirs(self.log_dir, exist_ok=True)
        self._jsonl = open(self.jsonl_path, "w", encoding="utf-8")
        self._csv = open(self.csv_path, "w", encoding="utf-8", newline="")
        self._csv_writer = csv.DictWriter(self._csv, fieldnames=CSV_COLUMNS)
        self._csv_writer.writeheader()
        self._csv.flush()

        self.write_record(
            {
                "type": "SESSION",
                "spec_version": "1.0.0",
                "protocol_version": 1,
                "input_mode": self.input_mode,
                "started_at": datetime.datetime.now().isoformat(),
                # Measured USB round trip, when there was a device to measure. Written
                # here so a log is self-describing about the link it was recorded over,
                # rather than leaving the transport delay to be assumed later.
                "link": link or {},
            }
        )

    def write_record(self, record: Dict[str, Any]) -> None:
        """Appends one JSON object. Flushed immediately."""
        if self._jsonl is None:
            return
        self._jsonl.write(json.dumps(record, separators=(",", ":")) + "\n")
        # Flushed every line on purpose: a log that survives an unplugged cable is
        # worth far more than the microseconds buffering would save.
        self._jsonl.flush()
        self.event_count += 1

    def write_raw_line(self, line: bytes, error: Optional[str] = None) -> None:
        """Records a line that failed to parse, with the reason.

        Malformed input is logged rather than discarded silently, so a link problem
        during a scored run is visible afterwards instead of merely showing up as a
        gap in the numbers.
        """
        self.write_record(
            {
                "type": "RAW",
                "error": error,
                "line": line.decode("latin-1", errors="replace"),
            }
        )

    def write_run_summary(self, final: Dict[str, Any], seq_gaps: int = 0) -> None:
        """Appends one CSV row for a completed run."""
        if self._csv_writer is None or self._csv is None:
            return

        total = final.get("correct", 0) + final.get("incorrect", 0)
        elapsed = final.get("elapsed_s", 0.0) or 0.0
        reconciliation = final.get("reconciliation") or {}

        row = {
            "timestamp": datetime.datetime.now().isoformat(),
            "bit_rate_bps": "%.4f" % final.get("bit_rate", 0.0),
            "n": final.get("n", 0),
            "correct": final.get("correct", 0),
            "incorrect": final.get("incorrect", 0),
            "elapsed_s": "%.3f" % elapsed,
            "input_mode": self.input_mode,
            "mode": final.get("mode", ""),
            "reason": final.get("reason", ""),
            "accuracy": "%.4f" % (final.get("correct", 0) / total) if total else "0",
            "presses_per_s": "%.3f" % (total / elapsed) if elapsed > 0 else "0",
            "max_streak": final.get("max_streak", 0),
            "p50_reaction_ms": "%.1f" % (final.get("p50_us", 0) / 1000.0),
            "p95_reaction_ms": "%.1f" % (final.get("p95_us", 0) / 1000.0),
            "p99_reaction_ms": "%.1f" % (final.get("p99_us", 0) / 1000.0),
            "draws": final.get("draws", 0),
            "repeats": final.get("repeats", 0),
            "seed": "0x%08X" % int(final.get("seed", 0)),
            "seq_gaps": seq_gaps,
            "reconciled": "yes" if reconciliation.get("agreed", True) else "MISMATCH",
            "spec_version": "1.0.0",
        }
        self._csv_writer.writerow(row)
        self._csv.flush()
        self.run_count += 1

    def close(self) -> None:
        for handle in (self._jsonl, self._csv):
            if handle is not None:
                try:
                    handle.flush()
                    handle.close()
                except OSError:
                    # Nothing useful to do at shutdown; the data is already flushed.
                    pass
        self._jsonl = None
        self._csv = None
        self._csv_writer = None

    def __enter__(self) -> "SessionLog":
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()
