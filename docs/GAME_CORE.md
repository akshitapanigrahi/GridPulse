# GRID PULSE — Game Core Specification

**Spec version: 1.0.0**

This document is *normative*. It is implemented three times:

| Implementation | Location | Role |
|---|---|---|
| Python | `tools/refimpl.py` | reference; **generates** the golden vectors |
| C++17 | `firmware/src/pure/` | runs on the RP2040 (Mode A) |
| JavaScript | `web/core/` | runs in the browser (Mode B) |

The Python implementation is the generator, and C++ and JS are both *consumers* validated
against it. None of the three share code. A shared misreading of this document therefore
cannot hide inside the vectors — it would have to be made three times independently.

Any change to this document requires regenerating `tests/vectors/` (`tools/gen_vectors.py`)
and bumping the spec version below.

---

## 1. Definitions

| Symbol | Meaning | Units |
|---|---|---|
| `N` | alphabet size — number of selectable targets | count, `N >= 3` |
| `Sc` | correct selections in the window | count |
| `Si` | incorrect selections in the window | count |
| `t` | elapsed session time | seconds |
| `B` | achieved bit rate | bits/second |

### 1.1 Cells and the alphabet

A **cell** is a physical grid position, indexed `0..24` in **row-major grid order**
(cell = `row * 5 + column`, row 0 = top, column 0 = left). Cell indices are the *only*
coordinate system this spec uses. GPIO numbers and LED strip indices are hardware
mapping concerns and appear nowhere in the game core — see `docs/HARDWARE.md`.

The **alphabet** is an ordered list of the currently selectable cells, ascending.
`N = len(alphabet)`.

- Mode B (keyboard): the alphabet is always all 25 cells, so `N = 25` and
  `alphabet[i] == i`.
- Mode A (hardware): the alphabet is the healthy cells reported by `SELFTEST`, in
  ascending cell order. A cell with a dead switch or dead LED is excluded permanently
  and is never targeted. `N` is therefore `<= 25` and the final report states the
  actual `N` used.

Targets are drawn as an **alphabet index** in `[0, N)`, then mapped through the
alphabet to a cell. This matters for the vectors: the drawn sequence is a sequence of
alphabet indices, not raw cell numbers, so a reduced-`N` run and a full run with the
same seed share a prefix of the *index* stream, not of the *cell* stream.

---

## 2. Random number generator

### 2.1 Algorithm — xoshiro128\*\*

State is four 32-bit words `s[0..3]`. All arithmetic is **unsigned 32-bit with
wraparound**; all shifts are logical.

```
rotl(x, k)  =  ((x << k) | (x >> (32 - k)))            all 32-bit

next():
    result = rotl(s[1] * 5, 7) * 9
    t      = s[1] << 9
    s[2] ^= s[0]
    s[3] ^= s[1]
    s[1] ^= s[2]
    s[0] ^= s[3]
    s[2] ^= t
    s[3]  = rotl(s[3], 11)
    return result
```

The state update happens on **every** call to `next()`, including calls whose result is
rejected by §2.3. Rejected draws still advance the stream. The vectors record rejection
counts precisely so that an implementation which "retries without advancing" fails.

> **JavaScript implementation warning.** `a * b` on values above 2³¹ loses precision in
> IEEE-754 doubles, and `<<`/`^` coerce via `ToInt32`, producing *signed* results.
> Every multiplication must use `Math.imul`, and every intermediate and returned value
> must be normalised with `>>> 0`. `s[1] * 5` and `... * 9` are both `Math.imul` sites.
> This is the single most likely source of divergence and is exactly what the golden
> vectors exist to catch.

### 2.2 Seeding — SplitMix32

The RNG is seeded from a single 32-bit `seed`. The four state words are produced by
running SplitMix32 four times over a counter initialised to `seed`:

```
splitmix32(state):        # state is a 32-bit variable, mutated in place
    state = (state + 0x9E3779B9) mod 2^32
    z = state
    z ^= z >> 16
    z  = (z * 0x21F0AAAD) mod 2^32
    z ^= z >> 15
    z  = (z * 0x735A2D97) mod 2^32
    z ^= z >> 15
    return z

seed_rng(seed):
    x = seed
    s[0] = splitmix32(x)
    s[1] = splitmix32(x)
    s[2] = splitmix32(x)
    s[3] = splitmix32(x)
    if s[0] | s[1] | s[2] | s[3] == 0:      # all-zero state is absorbing
        s[0] = 1
```

The all-zero guard is unreachable for any seed reachable by SplitMix32, but is required
in all three implementations so that the code is correct by construction rather than by
argument.

### 2.3 Uniform draw — rejection sampling, never modulo

To draw uniformly from `[0, N)`:

```
LIMIT(N) = 2^32 - (2^32 mod N)          # largest multiple of N that is <= 2^32

draw_index(N):
    loop:
        r = next()
        if r < LIMIT(N):
            return r mod N
```

`LIMIT` requires a 64-bit intermediate (or an equivalent identity) because `2^32` is not
representable in 32 bits. Implementations must compute it exactly:

- C++: `uint64_t limit = (1ull << 32) - ((1ull << 32) % n);`
- Python: exact integer arithmetic, no special handling.
- JS: `4294967296 - (4294967296 % n)` — both operands are exactly representable doubles
  and the result is exact. The comparison `r < limit` is a double comparison on an
  unsigned value, also exact.

Plain `next() % N` is **forbidden**: it biases the low indices, and at `N = 25` the bias
is small but real and would be visible to a chi-square test over a long session.

For `N = 25`, `LIMIT = 4294967275` and the per-draw rejection probability is
`21 / 2^32 ≈ 4.9e-9` — roughly one rejection per 200 million draws.

### 2.4 Repeats are not resampled

Because sampling is with replacement, `draw_index` may return the same target twice or
more in a row. **The core must not resample to avoid a repeat.** Doing so would make the
sequence non-i.i.d. and would leak exploitable information (after seeing target *x*, the
player would know the next target is not *x*, reducing the real entropy per selection
below `log2(N)`).

A repeat is handled at the *presentation* layer, not the sampling layer: the LED / cell
is extinguished for `kRepeatBlinkGapMs = 60 ms` and then re-lit, so a repeat reads
unambiguously as "flash → relight" rather than "nothing happened". See §5.

---

## 3. Scoring

### 3.1 The formula

```
B = log2(N - 1) * max(Sc - Si, 0) / t
```

- `log2(N - 1)`, not `log2(N)`: one selection must be reserved for error correction.
- `max(Sc - Si, 0)`: a session with `Si >= Sc` scores **exactly 0**, never a negative
  number.
- `t` is **all elapsed session time**, not a rolling window. There is deliberately no
  windowed variant anywhere in the system: a second bit-rate figure alongside this one
  would only ever be a chance to read, photograph or record the wrong value.

Guards, required in all implementations:

| Condition | Result |
|---|---|
| `t <= 0` | `B = 0` (avoids division by zero before the first frame) |
| `N < 3` | session refuses to start; `B` is undefined and reported as 0 |
| `Sc - Si <= 0` | `B = 0` |

`log2(N - 1)` is computed in double precision. At `N = 25` it is
`log2(24) = 4.584962500721156`.

### 3.2 Wire and log representation

To keep floating-point formatting differences off the wire and out of the equality
tests, `B` is transmitted and logged as an integer **milli-bits per second**:

```
b_mbps = floor(B * 1000 + 0.5)
```

Displays render `B` from the double; the integer form is what is compared for
device/host reconciliation.

---

## 4. Session state machine

```
      ┌────────┐  START(mode)   ┌───────────┐  3s elapsed   ┌─────────┐
      │  IDLE  │───────────────>│ COUNTDOWN │──────────────>│ RUNNING │
      └────────┘                └───────────┘               └─────────┘
           ^                          │                       │     │
           │                          │ ABORT                 │     │ ABORT
           │                          v                       │     v
           │                     ┌────────┐   t >= duration   │  ┌───────┐
           └─────────────────────│ ENDED  │<──────────────────┘  │ ENDED │
                    RESET        └────────┘                      └───────┘
```

- **IDLE** — nothing lit, no clock, no scoring. Key presses are ignored.
- **COUNTDOWN** — `kCountdownMs = 3000`. Presented as 3‑2‑1. **The clock is not
  running and no target is shown.** Key presses during countdown are ignored entirely
  (neither `Sc` nor `Si`) so that an early press cannot be punished or farmed.
- **RUNNING** — the first target is drawn and presented. `t0` is the instant of that
  presentation, and is the origin for all of `t`. Scoring is live.
- **ENDED** — configuration and tallies are frozen. The report is emitted once.

### 4.1 Timing authority

- `t0` is captured **at target presentation**, not at the `START` command, so neither
  the countdown nor any transport delay is charged to the player.
- Mode A: all timestamps come from the RP2040 hardware timer (`time_us_64`). The host's
  live counters are display-only and are reconciled against the device's `END` tally.
- Mode B: all timestamps come from `performance.now()`. **`Date.now()` is forbidden** —
  it is not monotonic and can step backwards on NTP correction, which would corrupt `t`.

### 4.2 Run duration

`kEvalDurationMs = 60000` for `EVAL`. `PRACTICE` is untimed and produces no report.
The run ends at the first event processed at or after `t0 + kEvalDurationMs`, and a key
press timestamped after that boundary does not score.

---

## 5. Input handling

Exactly one target is presented at any moment while `RUNNING`.

On a **key-down** naming cell `c`, with current target `T`:

| Condition | Effect |
|---|---|
| `c == T` | `Sc += 1`; streak `+= 1`; **draw a new target immediately** and present it; record reaction time `now - t_present` |
| `c != T`, `c` in alphabet | `Si += 1`; streak `= 0`; **the target does not change** — the player must still hit `T` |
| `c` not in alphabet | **ignored entirely** — neither `Sc` nor `Si`, no streak change, not logged as a selection |

Rules that apply to both modes:

- **Key-down only.** Key-up is never a selection.
- **No auto-repeat.** A held key produces exactly one selection. Mode B checks
  `event.repeat === true` and discards; Mode A is edge-triggered by construction and
  additionally enforces a per-key lockout.
- **No debounce latency on healthy inputs.** Mode A normally registers the press on
  the *first* observed falling edge and then ignores that key for
  `kDebounceLockoutUs = 5000`. This particular hardware build temporarily conditions
  GP12 with a 20 ms closed / 50 ms open confirmation window because that physical
  switch was observed chattering. The resulting per-cell timing bias is documented;
  replacing the switch and removing the exception is the long-term repair.

The target persisting through a miss is what makes ground truth unambiguous at every
instant: there is always exactly one correct action, it is visible, and a wrong action
does not change what the correct action is.

### 5.1 Presentation of a new target

On a correct hit the sequence is:

1. Hit feedback on the *hit* cell (a flash distinct from the target lighting).
2. New target drawn.
3. New target presented.

If the new target is the same cell as the one just hit, the hit flash still runs, so the
player sees flash → dark → relight. `kRepeatBlinkGapMs = 60`.

Feedback must not rely on a red/green distinction alone — hit and miss use **distinct
colours and distinct animations** so the game is fully playable with colour vision
deficiency.

---

## 6. Named constants

All of these are named in code with units in the name. No magic numbers.

| Name | Value | Notes |
|---|---|---|
| `kGridRows`, `kGridCols` | 5, 5 | |
| `kCellCount` | 25 | `kGridRows * kGridCols` |
| `kDefaultAlphabetSize` | 25 | `N`; only `SELFTEST` may reduce it |
| `kMinAlphabetSize` | 3 | below this `log2(N-1) <= 0`; session refuses to start |
| `kEvalDurationMs` | 60000 | the single scored window |
| `kCountdownMs` | 3000 | clock not running |
| `kRepeatBlinkGapMs` | 60 | repeat disambiguation |
| `kDebounceLockoutUs` | 5000 | Mode A only |
| `kSpecVersion` | `"1.0.0"` | must match across all three implementations |

---

## 7. Golden vectors

`tools/gen_vectors.py` writes, from the Python reference implementation:

### `tests/vectors/sequences.json`

For each of seeds `0x00000000`, `0x00000001`, `0xDEADBEEF`, `0x5A5A5A5A`, `0xFFFFFFFF`
and each `N` in `{3, 24, 25}`, over a run of `draw_count = 10000` draws:

- `raw16`: the first 16 raw `next()` outputs, as unsigned decimal. Isolates the RNG
  from the sampler, so a failure here means the 32-bit arithmetic is wrong.
- `prefix`: the first 256 drawn alphabet indices, explicitly. Human-auditable, and any
  arithmetic divergence shows up within the first handful of draws, so the test can
  report *where* it first differs.
- `sha256`: SHA-256 over all 10 000 indices rendered as comma-joined decimal, UTF-8.
  A single differing index anywhere in the run changes this digest, so all 10 000 are
  checked exactly without committing a megabyte of integers.
- `rejections`: the number of rejected draws over those 10 000, which pins the *stream
  position*, not merely the output values.
- `state_after`: `s[0..3]` after the 10 000 draws. An independent, very strong check
  that the stream advanced exactly the right number of times.
- `histogram`: counts per index, so a sampling bias regression is visible by eye
  directly in the vector file.

Together, `prefix` localises a divergence and `sha256` + `state_after` + `rejections`
prove there is none anywhere in the run.

### `tests/vectors/scoring_cases.json`

Named scenarios with expected `B` and `b_mbps`, covering: the nominal case, `Si > Sc`
clamping to exactly 0, `Sc == Si` at exactly 0, `t = 0` guard, `N = 3` (1 bit per
selection), `N = 25`, a full 60-second run at several accuracies, and rounding
boundaries for `b_mbps`.

### `tests/vectors/crc_cases.json`

CRC16/CCITT-FALSE over known inputs — including the canonical `"123456789"` check
value `0x29B1` — plus single-bit-flip cases proving the CRC detects them.

### `tests/vectors/protocol_cases.json`

Well-formed / malformed / truncated protocol lines with their expected parse outcome.
Defined once the wire protocol is frozen; see `docs/PROTOCOL.md`.

Both the C++ native test target and the JS test runner must reproduce every one of these
exactly. `./run.sh --test` runs both and exits nonzero on any divergence.

Because `file://` origins cannot `fetch` a JSON file, `tools/gen_vectors.py` also emits
`web/vectors.gen.js`, which assigns the same data to a global. It embeds a SHA-256 of
the JSON, and the test suite fails if the generated file is stale.
