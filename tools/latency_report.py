#!/usr/bin/env python3
"""Reaction-time and end-to-end latency report from a session log.

Two different things are measured and they must not be confused:

REACTION TIME (``rt_us`` on each HIT)
    Target presentation to key-down, measured entirely on the device by its hardware
    timer. This is the player's performance plus the device's own detection latency
    (at most one 250 us scan period). It is what the bit rate is made of.

PRESENTATION INTERVAL (gap between consecutive TARGET events)
    Includes the reaction plus the time the device spends drawing and lighting the
    next target. The difference between this and the reaction time is the machine's
    contribution to the loop, and it should be small - hundreds of microseconds.

TRANSPORT DELAY (``host_us`` vs ``t_us``)
    When the HOST learned of an event, against when the DEVICE says it happened. The
    two clocks have unrelated origins, so the absolute difference is meaningless - but
    its SPREAD across a run is the transport's jitter, and that is the part that would
    actually show up as a laggy or stuttering display. Reported as delay above the
    best-observed case, which is the honest way to read it without a shared clock.

The USB leg is deliberately NOT in the reaction or presentation figures. The device
timestamps everything itself, so transport delay affects when the host learns about an
event, never when the event is recorded as having happened. That is the claim this
report lets you check rather than take on trust. See docs/ARCHITECTURE.md.

Usage:
    python3 tools/latency_report.py logs/gridpulse-2026-08-11T14-22-01.jsonl

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from typing import Dict, List, Optional, Sequence


# Event types whose t_us is NOT a reading of the device clock.
#
# END carries the run's elapsed time rather than device uptime - 4.5 s against an uptime
# of 2590 s in a real log - so differencing it against the host clock produces a delay of
# forty-three minutes and swamps every genuine sample. Every other type is uptime.
NOT_DEVICE_UPTIME = frozenset({"END"})


# The 16 cells on the border of the 5x5 grid. The remaining 9 are the interior.
EDGE_CELLS = frozenset(
    c for c in range(25)
    if c // 5 in (0, 4) or c % 5 in (0, 4)
)


def mann_whitney_p(a: List[float], b: List[float]) -> float:
    """Two-sided p for "these two samples come from the same distribution".

    Mann-Whitney U with a normal approximation and a tie correction, done by hand
    because scipy is not available and must not be. Rank-based rather than
    mean-based, which matters here: reaction times are heavily right-skewed and a
    single fumbled press would drag a t-test around by itself.
    """
    n1, n2 = len(a), len(b)
    if n1 < 8 or n2 < 8:
        return float("nan")

    merged = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(merged)
    tie_term = 0.0
    i = 0
    while i < len(merged):
        j = i
        while j + 1 < len(merged) and merged[j + 1][0] == merged[i][0]:
            j += 1
        average_rank = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            ranks[k] = average_rank
        run = j - i + 1
        tie_term += run ** 3 - run
        i = j + 1

    rank_sum_a = sum(r for r, (_, group) in zip(ranks, merged) if group == 0)
    u = rank_sum_a - n1 * (n1 + 1) / 2.0

    total = n1 + n2
    mean_u = n1 * n2 / 2.0
    var_u = (n1 * n2 / 12.0) * ((total + 1) - tie_term / (total * (total - 1)))
    if var_u <= 0:
        return float("nan")

    z = (u - mean_u) / math.sqrt(var_u)
    return math.erfc(abs(z) / math.sqrt(2.0))


def percentile(sorted_values: Sequence[float], p: float) -> float:
    """Nearest-rank percentile, matching the firmware and the browser exactly."""
    if not sorted_values:
        return 0.0
    rank = max(1, min(len(sorted_values),
                      int(-(-p * len(sorted_values) // 100))))
    return sorted_values[rank - 1]


def summarise(values: List[float], unit: str = "ms") -> Dict[str, float]:
    if not values:
        return {}
    ordered = sorted(values)
    total = sum(ordered)
    mean = total / len(ordered)
    variance = sum((v - mean) ** 2 for v in ordered) / len(ordered)
    return {
        "count": len(ordered),
        "min": ordered[0],
        "p50": percentile(ordered, 50),
        "p95": percentile(ordered, 95),
        "p99": percentile(ordered, 99),
        "max": ordered[-1],
        "mean": mean,
        "stdev": variance ** 0.5,
        "unit": unit,
    }


def print_summary(title: str, stats: Dict[str, float], note: str = "") -> None:
    print(title)
    if not stats:
        print("   (no samples)")
        print()
        return
    if note:
        print("   %s" % note)
    print("   n = %d" % stats["count"])
    print("   min %8.1f   p50 %8.1f   p95 %8.1f   p99 %8.1f   max %8.1f  %s"
          % (stats["min"], stats["p50"], stats["p95"], stats["p99"], stats["max"],
             stats["unit"]))
    print("   mean %7.1f   sd  %8.1f %s" % (stats["mean"], stats["stdev"], stats["unit"]))
    print()


def load(path: str) -> List[Dict]:
    records: List[Dict] = []
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except ValueError:
                    continue  # a log cut short mid-write
                if isinstance(record, dict):
                    records.append(record)
    except OSError as exc:
        raise SystemExit("cannot read %s: %s" % (path, exc.strerror))
    return records


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logfile", help="a logs/*.jsonl session log")
    parser.add_argument("--per-cell", action="store_true",
                        help="also break reaction time down by grid cell")
    args = parser.parse_args(argv)

    records = load(args.logfile)
    if not records:
        print("No records in %s" % args.logfile, file=sys.stderr)
        return 1

    reactions_ms: List[float] = []
    transport_ms: List[float] = []
    link: Dict = {}
    seq_gaps: List[str] = []
    seq_missing = 0
    seq_seen = 0
    previous_seq: Optional[int] = None
    per_cell: Dict[int, List[float]] = {}
    target_times_us: List[int] = []
    intervals_ms: List[float] = []
    final: Optional[Dict] = None
    input_mode = "unknown"

    previous_target_us: Optional[int] = None
    for record in records:
        kind = record.get("type")
        if kind == "SESSION":
            input_mode = record.get("input_mode", input_mode)
            link = record.get("link") or {}
        if "seq" in record:
            seq = int(record["seq"])
            seq_seen += 1
            if previous_seq is not None and seq > previous_seq + 1:
                lost = seq - previous_seq - 1
                seq_missing += lost
                seq_gaps.append("seq %d -> %d (%d lost)" % (previous_seq, seq, lost))
            # A device reboot restarts the counter and a duplicate repeats one; neither
            # is a loss. Same rule as the host's live tracker.
            if previous_seq is None or seq > previous_seq:
                previous_seq = seq
            else:
                previous_seq = seq

        if ("host_us" in record and "t_us" in record
                and kind not in NOT_DEVICE_UPTIME):
            # Both clocks, same event. Their difference is arbitrary; its spread is not.
            transport_ms.append((int(record["host_us"]) - int(record["t_us"])) / 1000.0)

        if kind == "HIT" and "rt_us" in record:
            ms = int(record["rt_us"]) / 1000.0
            reactions_ms.append(ms)
            cell = int(record.get("cell", -1))
            per_cell.setdefault(cell, []).append(ms)
        elif kind == "TARGET" and "t_us" in record:
            now = int(record["t_us"])
            target_times_us.append(now)
            if previous_target_us is not None:
                intervals_ms.append((now - previous_target_us) / 1000.0)
            previous_target_us = now
        elif kind == "END":
            final = record

    print("=" * 72)
    print("GRID PULSE - latency report")
    print("=" * 72)
    print("%s  (input mode: %s)" % (args.logfile, input_mode))
    print()

    print_summary(
        "REACTION TIME  (target lit -> key down, device hardware timer)",
        summarise(reactions_ms),
        "this is what the bit rate is made of",
    )

    print_summary(
        "PRESENTATION INTERVAL  (consecutive targets)",
        summarise(intervals_ms),
        "reaction plus the device's own draw-and-light time",
    )

    if reactions_ms and intervals_ms:
        # Compare like with like: the median of each.
        overhead = percentile(sorted(intervals_ms), 50) - percentile(sorted(reactions_ms), 50)
        print("MACHINE OVERHEAD  (median interval - median reaction)")
        print("   %.2f ms" % overhead)
        if abs(overhead) < 1e-9 and input_mode == "hardware":
            # Not a suspiciously good result: a structural zero. On the keypad a hit and
            # the TARGET it draws are emitted with the same timestamp, so the interval
            # between targets IS the reaction and the difference cannot be anything but
            # zero. The device's draw-and-light time is real but invisible from a log;
            # measuring it needs an instrument on the LED line.
            print("   Structurally zero on hardware: the device emits a HIT and the")
            print("   TARGET it draws at the same timestamp, so this difference cannot")
            print("   see the draw-and-light time. It is not evidence that there is none.")
        else:
            print("   The device's share of the loop: drawing the next target and")
            print("   clocking it out to the LED strip. Everything else is the player.")
        print()

    if seq_seen:
        print("LINK LOSS  (gaps in the device's event sequence)")
        if not seq_gaps:
            print("   none: all %d events arrived, in order" % seq_seen)
        else:
            print("   %d gap(s), %d event(s) never arrived, out of %d received"
                  % (len(seq_gaps), seq_missing, seq_seen))
            for entry in seq_gaps[:8]:
                print("     %s" % entry)
            if len(seq_gaps) > 8:
                print("     ... and %d more" % (len(seq_gaps) - 8))
        print("   Display only. The device numbers every event from boot, so a")
        print("   discontinuity here means bytes were lost between it and whoever")
        print("   wrote this log - never that the score is wrong, which is computed")
        print("   on the device from state the host never touched.")
        print()

    if transport_ms:
        # Normalised against the fastest event seen: with no shared clock the absolute
        # offset says nothing, but everything above the minimum is queueing and
        # scheduling delay that a display would feel.
        floor = min(transport_ms)
        above = sorted(delay - floor for delay in transport_ms)
        print("TRANSPORT DELAY  (device -> host, above the best observed case)")
        print("   median %.2f ms   p95 %.2f ms   max %.2f ms   over %d events"
              % (percentile(above, 50), percentile(above, 95), above[-1], len(above)))
        print("   Jitter on the link, not an absolute one-way delay - the device and")
        print("   host clocks have unrelated origins. This is why the score is")
        print("   timestamped on the device: none of this can reach it.")
        print()

    if link:
        print("USB ROUND TRIP  (measured at connect, PING -> HELLO)")
        print("   median %.2f ms   p95 %.2f ms   min %.2f ms   over %d samples"
              % (link.get("median_ms", 0.0), link.get("p95_ms", 0.0),
                 link.get("min_ms", 0.0), link.get("samples", 0)))
        print("   Upper bound: a round trip, including the host's read-poll")
        print("   granularity. One way is roughly half of it.")
        print()

    if args.per_cell and per_cell:
        print("REACTION TIME BY CELL  (median, ms)")
        for row in range(5):
            line = "   "
            for col in range(5):
                cell = row * 5 + col
                samples = per_cell.get(cell)
                if samples:
                    line += "%7.0f" % percentile(sorted(samples), 50)
                else:
                    line += "      -"
            print(line)
        # Say what a dash is, and do not offer an interpretation the data cannot
        # carry. A 60-second run draws a few hundred targets across 25 cells; a short
        # one leaves most of the grid blank and the rest on a single press each, where
        # "variation between cells" is just noise with a shape.
        covered = sorted(len(v) for v in per_cell.values() if v)
        median_samples = covered[len(covered) // 2] if covered else 0
        print("   %d of 25 cells were hit. A dash is a cell this run never reached."
              % len(covered))
        if len(covered) >= 15 and median_samples >= 3:
            # Test the layout claim rather than assert it. The README argues that the
            # far cells cost reaction time; whether THIS run shows that is a question
            # about this data, and printing the conclusion either way would make the
            # report decoration rather than evidence.
            edge = [v for cell, vals in per_cell.items() if cell in EDGE_CELLS
                    for v in vals]
            interior = [v for cell, vals in per_cell.items()
                        if cell not in EDGE_CELLS for v in vals]
            if edge and interior:
                edge_median = percentile(sorted(edge), 50)
                interior_median = percentile(sorted(interior), 50)
                p_value = mann_whitney_p(edge, interior)
                print("   edge cells %.0f ms (n=%d)   interior %.0f ms (n=%d)"
                      % (edge_median, len(edge), interior_median, len(interior)))
                if p_value != p_value:  # NaN: too few samples to say anything
                    print("   Too few samples to test whether the layout costs time.")
                elif p_value < 0.05 and edge_median > interior_median:
                    print("   The edge is slower, and by more than this run's spread")
                    print("   explains (p = %.3f). That is the layout cost." % p_value)
                elif p_value < 0.05:
                    print("   The interior is SLOWER here (p = %.3f), which is not what"
                          % p_value)
                    print("   the layout would predict - worth a second run.")
                else:
                    print("   No difference this run can distinguish from noise")
                    print("   (p = %.3f). Pool more runs before concluding either way."
                          % p_value)
        else:
            print("   Median %d sample(s) per cell - too few to read anything into the"
                  % median_samples)
            print("   differences. Pool several runs before comparing cells.")
        print()

    if final is not None:
        elapsed_s = int(final.get("t_us", 0)) / 1e6
        print("RUN RESULT")
        print("   B  = %.3f bits/sec" % (int(final.get("b_mbps", 0)) / 1000.0))
        print("   N  = %s" % final.get("n"))
        print("   Sc = %s" % final.get("sc"))
        print("   Si = %s" % final.get("si"))
        print("   t  = %.3f s" % elapsed_s)
        if elapsed_s > 0:
            presses = int(final.get("sc", 0)) + int(final.get("si", 0))
            print("   %.2f presses/sec" % (presses / elapsed_s))
        print()
        print("   Device-reported percentiles (computed on the RP2040):")
        print("     p50 %.1f ms   p95 %.1f ms   p99 %.1f ms"
              % (int(final.get("p50_us", 0)) / 1000.0,
                 int(final.get("p95_us", 0)) / 1000.0,
                 int(final.get("p99_us", 0)) / 1000.0))
        # Cross-check: the device sorts on-board, this script sorts here. They must
        # agree, and a disagreement means one of them is wrong.
        if reactions_ms:
            here = percentile(sorted(reactions_ms), 50)
            there = int(final.get("p50_us", 0)) / 1000.0
            if there and abs(here - there) > 1.0:
                print("     NOTE: this log's p50 is %.1f ms, which differs from the"
                      % here)
                print("           device's %.1f ms - some HIT events were lost on the"
                      % there)
                print("           link. The device's figure is authoritative.")
        print()

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
