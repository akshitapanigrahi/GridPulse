#!/usr/bin/env python3
"""GRID PULSE reference implementation of the game core.

Purpose
-------
This is the *reference* implementation of ``docs/GAME_CORE.md`` (spec 1.0.0). It is
the generator for the golden vectors in ``tests/vectors/``, against which the C++
implementation (``firmware/src/pure/``) and the JavaScript implementation
(``web/core/``) are both validated.

It deliberately shares no code with either of them. Three independent implementations
means a shared misreading of the spec would have to be made three times before it could
hide in the vectors.

Invariants
----------
* Every arithmetic operation on RNG state is masked to 32 bits (``_M32``). Python
  integers are arbitrary precision, so the masking is explicit and load-bearing, not
  decorative.
* ``draw_index`` uses rejection sampling. Rejected draws advance the RNG stream.
* ``bit_rate`` clamps ``Sc - Si`` at zero and guards ``t <= 0``.

Standard library only. No third-party imports anywhere in this file.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

SPEC_VERSION = "1.0.0"

# --- Named constants (docs/GAME_CORE.md section 6) --------------------------------

K_GRID_ROWS = 5
K_GRID_COLS = 5
K_CELL_COUNT = K_GRID_ROWS * K_GRID_COLS
K_DEFAULT_ALPHABET_SIZE = 25
K_MIN_ALPHABET_SIZE = 3
K_EVAL_DURATION_MS = 60000
K_COUNTDOWN_MS = 3000
K_REPEAT_BLINK_GAP_MS = 60
K_DEBOUNCE_LOCKOUT_US = 5000

_M32 = 0xFFFFFFFF
_TWO32 = 1 << 32


# --- RNG (docs/GAME_CORE.md section 2) --------------------------------------------


def _rotl32(x: int, k: int) -> int:
    """Rotate a 32-bit value left by k bits."""
    x &= _M32
    return ((x << k) | (x >> (32 - k))) & _M32


def splitmix32(state: int) -> Tuple[int, int]:
    """One SplitMix32 step.

    Returns ``(new_state, output)``. The caller threads the state, because Python has
    no out-parameters and the spec defines the counter as mutated in place.
    """
    state = (state + 0x9E3779B9) & _M32
    z = state
    z ^= z >> 16
    z = (z * 0x21F0AAAD) & _M32
    z ^= z >> 15
    z = (z * 0x735A2D97) & _M32
    z ^= z >> 15
    return state, z & _M32


class Xoshiro128StarStar:
    """xoshiro128** PRNG, 32-bit, seeded via SplitMix32.

    The state update runs on every ``next_u32()`` call including calls whose result is
    later rejected by :meth:`draw_index`; rejected draws advance the stream.
    """

    __slots__ = ("s",)

    def __init__(self, seed: int) -> None:
        seed &= _M32
        x = seed
        words: List[int] = []
        for _ in range(4):
            x, out = splitmix32(x)
            words.append(out)
        if (words[0] | words[1] | words[2] | words[3]) == 0:
            # Unreachable for any SplitMix32-reachable seed, but the all-zero state is
            # absorbing so the guard is required by the spec in all implementations.
            words[0] = 1
        self.s: List[int] = words

    def next_u32(self) -> int:
        s = self.s
        result = (_rotl32((s[1] * 5) & _M32, 7) * 9) & _M32
        t = (s[1] << 9) & _M32
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = _rotl32(s[3], 11)
        return result

    def state(self) -> List[int]:
        return list(self.s)


def rejection_limit(n: int) -> int:
    """Largest multiple of ``n`` that is <= 2**32.

    Values at or above this are rejected, which removes the modulo bias that plain
    ``next() % n`` would introduce.
    """
    if n <= 0:
        raise ValueError("alphabet size must be positive, got %d" % n)
    return _TWO32 - (_TWO32 % n)


def draw_index(rng: Xoshiro128StarStar, n: int) -> Tuple[int, int]:
    """Draw one uniform index in ``[0, n)``.

    Returns ``(index, rejections)`` where ``rejections`` is how many draws were thrown
    away before this one succeeded. The count is part of the golden vectors because it
    pins the RNG stream position, not merely the emitted values.
    """
    limit = rejection_limit(n)
    rejections = 0
    while True:
        r = rng.next_u32()
        if r < limit:
            return r % n, rejections
        rejections += 1


def draw_sequence(seed: int, n: int, count: int) -> Dict[str, object]:
    """Draw ``count`` indices and report everything the vectors need to pin down."""
    rng = Xoshiro128StarStar(seed)
    indices: List[int] = []
    total_rejections = 0
    for _ in range(count):
        idx, rejections = draw_index(rng, n)
        indices.append(idx)
        total_rejections += rejections
    histogram = [0] * n
    for idx in indices:
        histogram[idx] += 1
    return {
        "indices": indices,
        "rejections": total_rejections,
        "state_after": rng.state(),
        "histogram": histogram,
    }


def raw_outputs(seed: int, count: int) -> List[int]:
    """First ``count`` raw ``next_u32()`` outputs for a seed."""
    rng = Xoshiro128StarStar(seed)
    return [rng.next_u32() for _ in range(count)]


# --- Scoring (docs/GAME_CORE.md section 3) -----------------------------------------


def bit_rate(n: int, correct: int, incorrect: int, elapsed_s: float) -> float:
    """B = log2(N - 1) * max(Sc - Si, 0) / t, in bits per second.

    Returns 0.0 rather than raising for the degenerate cases, because the live display
    calls this every frame including before the first target is presented.
    """
    if n < K_MIN_ALPHABET_SIZE:
        return 0.0
    if elapsed_s <= 0.0:
        return 0.0
    net = correct - incorrect
    if net <= 0:
        return 0.0
    return math.log2(n - 1) * net / elapsed_s


def bit_rate_mbps(n: int, correct: int, incorrect: int, elapsed_s: float) -> int:
    """Bit rate as integer milli-bits/second, the wire and reconciliation form."""
    return int(math.floor(bit_rate(n, correct, incorrect, elapsed_s) * 1000.0 + 0.5))


def bits_per_selection(n: int) -> float:
    """log2(N - 1): the information content credited to one correct selection."""
    if n < K_MIN_ALPHABET_SIZE:
        return 0.0
    return math.log2(n - 1)


# --- CRC16/CCITT-FALSE (docs/PROTOCOL.md) ------------------------------------------


def crc16_ccitt_false(data: bytes) -> int:
    """CRC16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# --- Session simulation (docs/GAME_CORE.md sections 4 and 5) -----------------------


class Session:
    """A scored session, driven by explicit timestamps.

    Deterministic and clock-free: the caller supplies every timestamp in milliseconds
    relative to an arbitrary origin. That is what makes it testable and what makes the
    JS and C++ ports comparable against the same vectors.
    """

    def __init__(
        self,
        seed: int,
        alphabet: Optional[Sequence[int]] = None,
        duration_ms: int = K_EVAL_DURATION_MS,
    ) -> None:
        if alphabet is None:
            alphabet = list(range(K_DEFAULT_ALPHABET_SIZE))
        self.alphabet: List[int] = sorted(alphabet)
        if len(set(self.alphabet)) != len(self.alphabet):
            raise ValueError("alphabet contains duplicate cells")
        self.n = len(self.alphabet)
        if self.n < K_MIN_ALPHABET_SIZE:
            raise ValueError(
                "alphabet size %d is below the minimum of %d; log2(N-1) would not be "
                "positive" % (self.n, K_MIN_ALPHABET_SIZE)
            )
        self.seed = seed & _M32
        self.rng = Xoshiro128StarStar(self.seed)
        self.duration_ms = duration_ms
        self.correct = 0
        self.incorrect = 0
        self.streak = 0
        self.max_streak = 0
        self.draws = 0
        self.target_cell: Optional[int] = None
        self.t0_ms: Optional[float] = None
        self.t_present_ms: Optional[float] = None
        self.ended = False
        self.reaction_times_ms: List[float] = []
        self.per_cell_hits: Dict[int, int] = {c: 0 for c in self.alphabet}

    # -- lifecycle --

    def start(self, now_ms: float) -> int:
        """Present the first target. ``now_ms`` becomes t0, the origin for all of t."""
        if self.t0_ms is not None:
            raise RuntimeError("session already started")
        self.t0_ms = now_ms
        return self._present(now_ms)

    def _present(self, now_ms: float) -> int:
        idx, _ = draw_index(self.rng, self.n)
        self.draws += 1
        self.target_cell = self.alphabet[idx]
        self.t_present_ms = now_ms
        return self.target_cell

    def elapsed_s(self, now_ms: float) -> float:
        if self.t0_ms is None:
            return 0.0
        end_ms = min(now_ms, self.t0_ms + self.duration_ms) if self.ended else now_ms
        return max(0.0, (end_ms - self.t0_ms) / 1000.0)

    def expired(self, now_ms: float) -> bool:
        if self.t0_ms is None:
            return False
        return (now_ms - self.t0_ms) >= self.duration_ms

    def end(self) -> None:
        self.ended = True
        self.target_cell = None

    # -- input --

    def press(self, cell: int, now_ms: float) -> str:
        """Apply one key-down. Returns 'hit', 'miss', or 'ignored'.

        'ignored' covers presses outside the alphabet, presses before the session
        starts, and presses after the run has expired. None of them touch the tally.
        """
        if self.t0_ms is None or self.ended:
            return "ignored"
        if self.expired(now_ms):
            self.end()
            return "ignored"
        if cell not in self.per_cell_hits:
            return "ignored"
        if cell == self.target_cell:
            self.correct += 1
            self.streak += 1
            self.max_streak = max(self.max_streak, self.streak)
            self.per_cell_hits[cell] += 1
            if self.t_present_ms is not None:
                self.reaction_times_ms.append(now_ms - self.t_present_ms)
            self._present(now_ms)
            return "hit"
        self.incorrect += 1
        self.streak = 0
        return "miss"

    # -- reporting --

    def bit_rate(self, now_ms: float) -> float:
        return bit_rate(self.n, self.correct, self.incorrect, self.elapsed_s(now_ms))

    def report(self, now_ms: float) -> Dict[str, object]:
        elapsed = self.elapsed_s(now_ms)
        return {
            "spec_version": SPEC_VERSION,
            "seed": self.seed,
            "n": self.n,
            "correct": self.correct,
            "incorrect": self.incorrect,
            "elapsed_s": elapsed,
            "bit_rate": bit_rate(self.n, self.correct, self.incorrect, elapsed),
            "b_mbps": bit_rate_mbps(self.n, self.correct, self.incorrect, elapsed),
            "bits_per_selection": bits_per_selection(self.n),
            "max_streak": self.max_streak,
            "draws": self.draws,
        }
