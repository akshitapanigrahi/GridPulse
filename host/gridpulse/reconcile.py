"""GRID PULSE device/host reconciliation.

The host maintains its own running counters from the event stream so the UI has
something to display between ticks. Those counters are DISPLAY ONLY. At end of run
the device sends its own tally, computed on core 1 from state the host never touched,
and that is the number that gets reported.

This module holds the two side by side and records any disagreement. A mismatch means
the host lost events - which the sequence tracker will usually have already flagged -
and never that the score is in doubt.

Standard library only.
"""

from __future__ import annotations

import math
from typing import Any, Dict, List, Optional

MIN_ALPHABET_SIZE = 3

# The scored window. Must match kEvalDurationUs in firmware/src/pure/config.h and
# K_EVAL_DURATION_MS in web/core/session.js.
EVAL_DURATION_S = 60.0


def bit_rate(n: int, correct: int, incorrect: int, elapsed_s: float) -> float:
    """B = log2(N - 1) * max(Sc - Si, 0) / t.

    An independent third implementation of the formula (the device and the browser
    have their own). Recomputing here means a firmware scoring bug shows up as a
    mismatch rather than being taken on trust.
    """
    if n < MIN_ALPHABET_SIZE:
        return 0.0
    if not elapsed_s > 0:
        return 0.0
    net = correct - incorrect
    if net <= 0:
        return 0.0
    return math.log2(n - 1) * net / elapsed_s


def bit_rate_mbps(n: int, correct: int, incorrect: int, elapsed_s: float) -> int:
    return int(math.floor(bit_rate(n, correct, incorrect, elapsed_s) * 1000.0 + 0.5))


class SessionMirror:
    """A display-only mirror of the device's state, folded from the event stream."""

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.state = "IDLE"
        self.mode = "IDLE"
        self.n = 25
        self.seed = 0
        self.correct = 0
        self.incorrect = 0
        self.streak = 0
        self.target_cell = -1
        self.target_is_repeat = False
        self.elapsed_us = 0
        self.last_reaction_us = 0
        self.draws = 0
        self.histogram: List[int] = [0] * 25
        self.final: Optional[Dict[str, Any]] = None
        self.reconciliation: Optional[Dict[str, Any]] = None

    def apply(self, event: Dict[str, Any]) -> None:
        """Folds one parsed event into the mirror. Never raises on odd input."""
        kind = event.get("type")

        if kind == "HELLO":
            self.n = int(event.get("n", self.n))

        elif kind == "MODE":
            self.mode = str(event.get("mode", self.mode))
            self.state = str(event.get("state", self.state))
            self.n = int(event.get("n", self.n))
            self.seed = int(event.get("seed", self.seed))
            if self.state == "RUNNING":
                self.correct = 0
                self.incorrect = 0
                self.streak = 0
                self.elapsed_us = 0
                self.draws = 0
                self.histogram = [0] * 25
                self.final = None
                self.reconciliation = None

        elif kind == "TARGET":
            self.target_cell = int(event.get("cell", -1))
            self.target_is_repeat = bool(event.get("repeat", False))
            self.draws = int(event.get("idx", self.draws))
            if 0 <= self.target_cell < len(self.histogram):
                self.histogram[self.target_cell] += 1

        elif kind == "HIT":
            self.correct = int(event.get("sc", self.correct))
            self.incorrect = int(event.get("si", self.incorrect))
            self.streak = int(event.get("streak", 0))
            self.last_reaction_us = int(event.get("rt_us", 0))

        elif kind == "MISS":
            self.correct = int(event.get("sc", self.correct))
            self.incorrect = int(event.get("si", self.incorrect))
            self.streak = 0

        elif kind == "TICK":
            self.correct = int(event.get("sc", self.correct))
            self.incorrect = int(event.get("si", self.incorrect))
            self.elapsed_us = int(event.get("t_run_us", self.elapsed_us))

        elif kind == "END":
            self.state = "ENDED"
            self.target_cell = -1
            self._accept_final(event)

    def _accept_final(self, event: Dict[str, Any]) -> None:
        """Takes the device's tally as authoritative and records any disagreement."""
        n = int(event.get("n", self.n))
        sc = int(event.get("sc", 0))
        si = int(event.get("si", 0))
        elapsed_s = int(event.get("t_us", 0)) / 1e6
        device_mbps = int(event.get("b_mbps", 0))

        mismatches: List[str] = []
        if self.correct != sc:
            mismatches.append("Sc display=%d device=%d" % (self.correct, sc))
        if self.incorrect != si:
            mismatches.append("Si display=%d device=%d" % (self.incorrect, si))

        # Recompute B from the device's own Sc, Si, N and t. Comparing against the
        # device's own b_mbps catches a firmware scoring bug, rather than trusting a
        # single number computed once on the device.
        recomputed_mbps = bit_rate_mbps(n, sc, si, elapsed_s)
        if device_mbps != recomputed_mbps:
            mismatches.append(
                "B device=%.3f host_recomputed=%.3f"
                % (device_mbps / 1000.0, recomputed_mbps / 1000.0)
            )

        self.reconciliation = {
            "agreed": not mismatches,
            "mismatches": mismatches,
            "display_sc": self.correct,
            "display_si": self.incorrect,
        }

        # The device's figures win. It is the only party that saw every keypress with
        # a hardware timestamp.
        self.correct = sc
        self.incorrect = si
        self.n = n
        self.elapsed_us = int(event.get("t_us", 0))

        self.final = {
            "n": n,
            "correct": sc,
            "incorrect": si,
            "elapsed_s": elapsed_s,
            "bit_rate": bit_rate(n, sc, si, elapsed_s),
            "b_mbps": recomputed_mbps,
            "device_b_mbps": device_mbps,
            "bits_per_selection": math.log2(n - 1) if n >= MIN_ALPHABET_SIZE else 0.0,
            "reason": event.get("reason", "COMPLETE"),
            "mode": event.get("mode", "EVAL"),
            "seed": int(event.get("seed", self.seed)),
            "draws": int(event.get("draws", 0)),
            "repeats": int(event.get("repeats", 0)),
            "max_streak": int(event.get("max_streak", 0)),
            "min_us": int(event.get("min_us", 0)),
            "p50_us": int(event.get("p50_us", 0)),
            "p95_us": int(event.get("p95_us", 0)),
            "p99_us": int(event.get("p99_us", 0)),
            "reconciliation": self.reconciliation,
        }

    def snapshot(self) -> Dict[str, Any]:
        """The state the UI renders from."""
        elapsed_s = self.elapsed_us / 1e6
        return {
            "state": self.state,
            "mode": self.mode,
            "n": self.n,
            "sc": self.correct,
            "si": self.incorrect,
            "streak": self.streak,
            "target_cell": self.target_cell,
            "elapsed_s": elapsed_s,
            "bit_rate": bit_rate(self.n, self.correct, self.incorrect, elapsed_s),
            "last_reaction_ms": self.last_reaction_us / 1000.0,
            "final": self.final,
        }
