#!/usr/bin/env python3
"""Empirically demonstrate that the target sequence is i.i.d. uniform.

The assignment's first requirement is that targets are "sampled uniformly at random
with replacement ... no patterns, no structure, no exploitable statistics". This
script tests that claim on real data rather than asserting it in prose.

Three tests, each aimed at a different way the claim could fail:

1. CHI-SQUARE GOODNESS OF FIT
   Are the 25 cells equally likely? Catches a biased sampler - notably the modulo
   bias that ``next() % N`` would introduce.

2. SERIAL CORRELATION (a chi-square test of independence on consecutive pairs)
   Does the previous target tell you anything about the next one? Catches the most
   tempting mistake in this whole project: resampling to avoid a consecutive repeat.
   That feels like a kindness to the player and is in fact a leak - after seeing x
   you would know x cannot be next, so the real entropy per selection would be below
   log2(N) and the reported bit rate would overstate the information transferred.

3. RUNS TEST
   Are repeats as common as chance requires? Under i.i.d. sampling a repeat occurs
   with probability exactly 1/N. Too few is the resampling bug; too many suggests a
   stuck key or a duplicated event.

Usage:
    python3 tools/validate_sequence.py logs/gridpulse-2026-08-11T14-22-01.jsonl
    python3 tools/validate_sequence.py --simulate 100000
    python3 tools/validate_sequence.py --simulate 100000 --seed 0xDEADBEEF

With --simulate the sequence comes from the same reference implementation that
generates the golden vectors, which the firmware and the browser are both validated
against. So a simulated run tests the sampler the real device actually uses.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from typing import Dict, List, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import refimpl as ref  # noqa: E402


def chi_square_upper_tail(statistic: float, degrees_of_freedom: int) -> float:
    """P(X > statistic) for a chi-square with the given degrees of freedom.

    Computed from the regularised upper incomplete gamma function via a series and a
    continued fraction, because scipy is not available and must not be. Accurate to
    well within what a goodness-of-fit judgement needs.
    """
    if statistic <= 0:
        return 1.0
    a = degrees_of_freedom / 2.0
    x = statistic / 2.0

    if x < a + 1.0:
        # Series expansion for the lower tail, then complement.
        term = 1.0 / a
        total = term
        for n in range(1, 1000):
            term *= x / (a + n)
            total += term
            if abs(term) < abs(total) * 1e-15:
                break
        lower = total * math.exp(-x + a * math.log(x) - math.lgamma(a))
        return max(0.0, min(1.0, 1.0 - lower))

    # Continued fraction for the upper tail (Lentz's algorithm).
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, 1000):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < tiny:
            d = tiny
        c = b + an / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < 1e-15:
            break
    upper = math.exp(-x + a * math.log(x) - math.lgamma(a)) * h
    return max(0.0, min(1.0, upper))


def load_targets(path: str) -> Tuple[List[int], int]:
    """Extracts the target sequence and N from a session log."""
    targets: List[int] = []
    alphabet_size = 0
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
                if not isinstance(record, dict):
                    continue
                if record.get("type") == "TARGET" and "cell" in record:
                    targets.append(int(record["cell"]))
                elif record.get("type") in ("MODE", "HELLO", "END") and "n" in record:
                    alphabet_size = max(alphabet_size, int(record["n"]))
    except OSError as exc:
        raise SystemExit("cannot read %s: %s" % (path, exc.strerror))
    return targets, alphabet_size


def uniformity_test(targets: Sequence[int], symbols: Sequence[int]) -> Dict[str, object]:
    counts = {symbol: 0 for symbol in symbols}
    for value in targets:
        if value in counts:
            counts[value] += 1

    total = sum(counts.values())
    expected = total / len(symbols)
    statistic = sum((count - expected) ** 2 / expected for count in counts.values())
    dof = len(symbols) - 1
    return {
        "counts": counts,
        "total": total,
        "expected": expected,
        "statistic": statistic,
        "dof": dof,
        "p_value": chi_square_upper_tail(statistic, dof),
    }


def independence_test(targets: Sequence[int], symbols: Sequence[int]) -> Dict[str, object]:
    """Chi-square test of independence on (previous, next) pairs.

    A sampler that resamples to avoid repeats would leave the diagonal of this table
    empty, which produces an enormous statistic and a vanishing p-value.
    """
    index = {symbol: i for i, symbol in enumerate(symbols)}
    size = len(symbols)
    table = [[0] * size for _ in range(size)]

    pairs = 0
    for previous, following in zip(targets, targets[1:]):
        if previous in index and following in index:
            table[index[previous]][index[following]] += 1
            pairs += 1

    if pairs == 0:
        return {"pairs": 0, "statistic": 0.0, "dof": 0, "p_value": 1.0,
                "diagonal": 0, "expected_diagonal": 0.0, "low_expected": 0}

    row_totals = [sum(row) for row in table]
    col_totals = [sum(table[r][c] for r in range(size)) for c in range(size)]

    statistic = 0.0
    low_expected = 0
    for r in range(size):
        for c in range(size):
            expected = row_totals[r] * col_totals[c] / pairs
            if expected <= 0:
                continue
            if expected < 5:
                low_expected += 1
            statistic += (table[r][c] - expected) ** 2 / expected

    dof = (size - 1) ** 2
    diagonal = sum(table[i][i] for i in range(size))
    return {
        "pairs": pairs,
        "statistic": statistic,
        "dof": dof,
        "p_value": chi_square_upper_tail(statistic, dof),
        "diagonal": diagonal,
        "expected_diagonal": pairs / size,
        "low_expected": low_expected,
    }


def runs_test(targets: Sequence[int], n: int) -> Dict[str, object]:
    """Are consecutive repeats as frequent as 1/N requires?

    Uses the normal approximation to the binomial. A two-sided |z| above about 3 is
    worth investigating; a z far below zero is the signature of resampling.
    """
    pairs = max(len(targets) - 1, 0)
    if pairs == 0:
        return {"pairs": 0, "repeats": 0, "expected": 0.0, "z": 0.0}

    repeats = sum(1 for a, b in zip(targets, targets[1:]) if a == b)
    p = 1.0 / n
    expected = pairs * p
    variance = pairs * p * (1 - p)
    z = (repeats - expected) / math.sqrt(variance) if variance > 0 else 0.0
    return {"pairs": pairs, "repeats": repeats, "expected": expected, "z": z}


def verdict(p_value: float) -> str:
    if p_value < 0.001:
        return "FAIL   (p < 0.001: this is not consistent with i.i.d. uniform)"
    if p_value < 0.01:
        return "SUSPECT (p < 0.01: worth a second, longer sample)"
    return "PASS"


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logfile", nargs="?", help="a logs/*.jsonl session log")
    parser.add_argument("--simulate", type=int, metavar="COUNT",
                        help="test COUNT draws from the reference sampler instead")
    parser.add_argument("--seed", default="0xDEADBEEF",
                        help="seed for --simulate (default 0xDEADBEEF)")
    parser.add_argument("--n", type=int, default=25,
                        help="alphabet size for --simulate (default 25)")
    args = parser.parse_args(argv)

    if args.simulate:
        seed = int(args.seed, 0)
        rng = ref.Xoshiro128StarStar(seed)
        targets = [ref.draw_index(rng, args.n)[0] for _ in range(args.simulate)]
        alphabet_size = args.n
        source = "simulated: %d draws, seed 0x%08X, N=%d" % (args.simulate, seed, args.n)
    elif args.logfile:
        targets, alphabet_size = load_targets(args.logfile)
        if not targets:
            print("No TARGET events found in %s" % args.logfile, file=sys.stderr)
            return 1
        alphabet_size = alphabet_size or (max(targets) + 1)
        source = "%s: %d targets, N=%d" % (args.logfile, len(targets), alphabet_size)
    else:
        parser.error("give a logfile or --simulate COUNT")
        return 2

    symbols = sorted(set(targets)) if args.logfile and not args.simulate \
        else list(range(alphabet_size))
    if len(symbols) < 2:
        print("Not enough distinct targets to test.", file=sys.stderr)
        return 1

    print("=" * 72)
    print("GRID PULSE - target sequence validation")
    print("=" * 72)
    print(source)
    print()

    if len(targets) < 200:
        print("NOTE: %d samples is a small sample. These tests are only meaningful"
              % len(targets))
        print("      over a few thousand draws; a 60-second run produces a few hundred,")
        print("      so pool several runs or use --simulate for a decisive answer.")
        print()

    # --- 1. uniformity ---
    uniform = uniformity_test(targets, symbols)
    print("1. UNIFORMITY  (chi-square goodness of fit)")
    print("   Are all %d cells equally likely?" % len(symbols))
    print("   chi2 = %.3f, dof = %d, p = %.4f" %
          (uniform["statistic"], uniform["dof"], uniform["p_value"]))
    print("   %s" % verdict(float(uniform["p_value"])))
    counts = uniform["counts"]
    low = min(counts, key=lambda k: counts[k])
    high = max(counts, key=lambda k: counts[k])
    print("   least seen: cell %d (%d)   most seen: cell %d (%d)   expected %.1f"
          % (low, counts[low], high, counts[high], uniform["expected"]))
    print()

    # --- 2. independence ---
    independence = independence_test(targets, symbols)
    print("2. INDEPENDENCE  (chi-square on consecutive pairs)")
    print("   Does the previous target predict the next one?")
    if independence["pairs"] == 0:
        print("   not enough pairs")
    else:
        print("   chi2 = %.3f, dof = %d, p = %.4f"
              % (independence["statistic"], independence["dof"],
                 independence["p_value"]))
        print("   %s" % verdict(float(independence["p_value"])))
        if independence["low_expected"]:
            print("   (%d of %d table cells have an expected count below 5, so the"
                  % (independence["low_expected"], len(symbols) ** 2))
            print("    approximation is rough here - more samples would sharpen it)")
    print()

    # --- 3. repeats ---
    runs = runs_test(targets, len(symbols))
    print("3. REPEATS  (are consecutive repeats as common as 1/N demands?)")
    print("   observed %d repeats in %d pairs; expected %.1f; z = %+.2f"
          % (runs["repeats"], runs["pairs"], runs["expected"], runs["z"]))
    if runs["pairs"] == 0:
        print("   not enough data")
    elif runs["z"] < -3.0:
        print("   FAIL: far too few repeats. This is the signature of a sampler that")
        print("         resamples to avoid consecutive repeats, which breaks i.i.d.")
        print("         sampling and leaks information about the next target.")
    elif runs["z"] > 3.0:
        print("   FAIL: far too many repeats. Suspect a stuck key or duplicated events.")
    else:
        print("   PASS")
    print()

    print("-" * 72)
    failed = (float(uniform["p_value"]) < 0.001
              or (independence["pairs"] and float(independence["p_value"]) < 0.001)
              or abs(float(runs["z"])) > 3.0)
    if failed:
        print("VERDICT: the sequence shows structure. It is NOT i.i.d. uniform.")
        return 1
    print("VERDICT: consistent with i.i.d. uniform sampling with replacement.")
    print()
    print("The sampler is xoshiro128** with rejection sampling (never modulo), seeded")
    print("on-device from the RP2040 ring oscillator. See docs/GAME_CORE.md section 2.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
