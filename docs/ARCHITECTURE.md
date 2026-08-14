# GRID PULSE — Architecture
---

## 1. The whole system

```
 MODE A — HARDWARE                                MODE B — KEYBOARD
 ════════════════════                             ══════════════════

 ┌───────────────────────────────┐
 │  RP2040  (the authority)      │
 │                               │
 │  ┌─────────┐   ┌───────────┐  │
 │  │ core 1  │   │  core 0   │  │
 │  │         │   │           │  │
 │  │ scan    │   │ TinyUSB   │  │
 │  │ 4 kHz   │──▶│ CDC       │  │              ┌──────────────────────┐
 │  │ debounce│ q │ framing   │  │              │      browser         │
 │  │ RNG     │ u │ CRC-16    │  │              │                      │
 │  │ scoring │ e │ parsing   │  │              │  ┌────────────────┐  │
 │  │ FSM     │ u │           │  │              │  │  web/core/     │  │
 │  │ LEDs    │ e │           │  │              │  │  the SAME game │  │
 │  └─────────┘   └─────┬─────┘  │              │  │  in JavaScript │  │
 │   PIO+DMA            │        │              │  └────────┬───────┘  │
 └──────┬───────────────┼────────┘              │           │          │
        │               │ USB CDC               │  ┌────────▼───────┐  │
   ┌────▼─────┐         │                       │  │ web/ui/        │  │
   │ 25 WS2812│    ┌────▼───────────┐           │  │ grid, HUD, FX  │  │
   │ 25 keys  │    │ host bridge    │  SSE      │  └────────────────┘  │
   └──────────┘    │ (Python,       │──────────▶│                      │
                   │  stdlib only)  │           │   keydown ─┐         │
                   │                │◀──────────│            │         │
                   │ display+logger │  POST     └────────────┼─────────┘
                   └────────┬───────┘                        │
                            │                                │
                       logs/*.jsonl                    performance.now()
                       logs/*.csv
```

In Mode A the game runs on the device and the browser is a display.
In Mode B there is no device, so the game runs in the browser — which is still as
close to the input as it can possibly get.

---

## 2. The one decision everything else follows from

**The game core always runs as close to the input device as possible.**

### Mode A: on the RP2040

A USB CDC round trip is roughly 5 ms. If the host decided hits, every press would pay
it:

```
   4 presses/sec × 5 ms = 20 ms of every second spent waiting on USB = 2%

   At a realistic B ≈ 17 bits/s, that is 0.34 bits/s given away for nothing.
```

Worse than the mean is the variance. USB is polled on 1 ms frames and scheduled by the
host OS; a busy machine can delay a transfer by tens of milliseconds. That jitter would
land inside every measured reaction time, so the run would be measuring the grader's
laptop as much as the grader.

Putting target selection, hit classification and the clock on the device removes both.
Every timestamp comes from the RP2040 hardware timer. The host learns about an event
1–5 ms later, but *when the host learns* has no effect on *what was recorded*.

The screen therefore trails the board by about a millisecond — sub-frame at 60 Hz, and
imperceptible.

### Mode B: in the browser

With no device, the browser *is* the closest point to the input. Routing keystrokes
through a local Python server would add a round trip for no benefit whatsoever. The
press is timestamped with `performance.now()` on the first line of the `keydown`
handler, before any lookup, scoring or rendering.

---

## 3. Latency budget

For Mode A. These are derived from the scan period, the fixed-cost logic path
and the WS2812B clock-in time; `tools/latency_report.py` reports the measured
end-to-end figures from a recorded run.

| Stage | Time | In the player's loop? |
|---|---|---|
| Key contact closes → detected by the scan | ≤ 250 µs normally; ≈ 20 ms on GP12 | **yes** (4 kHz scan period plus documented GP12 conditioning) |
| Detected → scored, next target drawn | < 10 µs | **yes** (fixed-cost logic, no allocation) |
| Scored → new LED lit | ≈ 380 µs | **yes** (DMA trigger + strip clock-in) |
| **Total machine contribution** | **≈ 640 µs** | |
| Scored → host receives the event | 1–5 ms | no — display only |
| Host → browser over SSE | < 1 ms | no — display only |

Against a human reaction time of 200–400 ms, the machine contributes roughly 0.2%.

For Mode B the equivalent figures are the browser's `keydown` delivery (typically
1–5 ms, and not something the page can improve on) plus one paint. `tools/latency_report.py`
prints the real numbers from any recorded session.

---

## 4. Why core 1 never blocks

Core 1 owns the game; core 0 owns USB. They share only two lock-free ring buffers.

USB has unbounded, host-controlled timing: a host that stops reading, a terminal that
disconnects, a browser that stalls. If the game shared a core with it, any of those
could delay a key scan — and a delayed scan is a delayed press is a lower score.

Everything core 1 does is bounded:

* a single 32-bit port read for all 25 switches,
* a fixed 25-iteration debounce pass,
* one rejection-sampled draw (expected rejections at N = 25: 5 × 10⁻⁹),
* a DMA trigger.

No allocation, no blocking call, no unbounded loop.

### Why not the SDK's `queue.h`

`queue_t` is correct and multicore-safe, but it takes a hardware spinlock on both push
and pop, which makes the producer's worst-case latency depend on the consumer — and
the consumer is the USB core. A single-producer/single-consumer ring needs no lock at
all: with one writer of `head` and one writer of `tail`, an acquire/release pair
suffices and the producer's push is wait-free.

The RP2040's Cortex-M0+ implements ARMv6-M, which has `DMB` and lock-free 32-bit
atomic load/store, so `std::atomic<uint32_t>` compiles to plain loads, stores and
barriers here — no library calls.

### Backpressure

If the host stops draining, the CDC FIFO fills. Core 0 then drops **presentation**
events (ticks, logs, histogram chunks) and never drops **scoring** events (targets,
hits, misses, mode changes, the final tally). A dropped tick makes the display briefly
stale; a dropped hit would make it wrong.

If even that is not enough, the event queue fills and core 1's `Push` returns false —
it never overwrites unread data. The count is reported. **None of this can affect the
score**, because the device's `END` tally is computed on core 1 from state the host
never touches.

---

## 5. One spec, three implementations

`docs/GAME_CORE.md` is normative. It is implemented three times:

| Implementation | Location | Role |
|---|---|---|
| Python | `tools/refimpl.py` | reference; **generates** the golden vectors |
| C++17 | `firmware/src/pure/` | runs on the RP2040 |
| JavaScript | `web/core/` | runs in the browser |

The Python one is the *generator*; C++ and JS are both *consumers* validated against
an implementation neither shares code with. A shared misreading of the spec would have
to be made three times independently before it could hide.

```
      docs/GAME_CORE.md
             │  normative
             ▼
      tools/refimpl.py ────generates────▶ tests/vectors/*.json
             │                                    │
             │                        ┌───────────┴───────────┐
             │                        ▼                       ▼
             │              tests/vectors/vectors.gen.h  web/vectors.gen.js
             │                        │                       │
             ▼                        ▼                       ▼
      host/gridpulse/         firmware/src/pure/         web/core/
       (3rd impl of                  C++                    JS
        the formula)                  │                       │
             └──────────── all validated against ─────────────┘
```

The vectors pin, for five seeds × three alphabet sizes:

* the first 16 raw generator outputs (isolates the RNG from the sampler),
* the first 256 drawn indices explicitly (localises any divergence),
* a digest over all 10 000 indices (SHA-256 for JS, CRC-32 for C++ — the latter
  reuses production code rather than carrying a second hash for tests),
* the number of rejected draws (pins the *stream position*, not just the values),
* the exact RNG state after 10 000 draws,
* the full per-index histogram.

**Why this apparatus exists.** JavaScript has no 32-bit integers. `a * b` loses low
bits above 2⁵³ and `<<`/`^` produce *signed* results. `Math.imul` and `>>> 0` are
mandatory in every single expression, and getting one wrong produces a sequence that
looks perfectly random and is wrong. The vectors are what turn that from a silent
divergence into a failing test.

---

## 6. Purity boundary

`firmware/src/pure/` may not include a Pico SDK header. That separation is what makes
the game logic compilable and testable on the host, and it is enforced mechanically by
the `check-purity` target in `tests/native/Makefile` rather than left to discipline.

```
firmware/src/pure/     no SDK    ← host-compiled, 145 tests, 225k assertions
firmware/src/hal/      SDK       ← GPIO, PIO, DMA, flash
firmware/src/          SDK       ← the two core entry points
```

`tests/native/Makefile` is the primary test path and needs only a C++ compiler and
`make`. `firmware/CMakeLists.txt` defines an equivalent CTest target over the same
sources for anyone building the firmware anyway — but CMake is not installed by
default on a stock macOS or Linux machine, and requiring it for `./run.sh --test`
would break the "no exotic setup" promise.

---

## 7. Static memory

The firmware performs **no dynamic allocation at any point**, before or after init.

| Object | Bytes | Notes |
|---|---|---|
| Event ring buffer | 19 472 | 64 slots × 304 |
| `Game` | 4 384 | mostly the reaction-time sample array |
| LED pixel buffer | 100 | 25 × uint32 |
| Command ring buffer | 44 | 8 slots |
| **Total** | **≈ 24 kB** | of the RP2040's 264 kB |

These figures are pinned by `tests/native/test_queue_event.cpp`, so a struct that
quietly doubles in size fails the suite rather than silently eating RAM.

`sizeof(Event)` is 304 bytes rather than the ~40 a union would give. That is
deliberate: the saving is 10 kB out of 264, and a union would buy it with type punning
across two cores — exactly the kind of cleverness that produces a bug nobody can
reproduce.

---

## 8. Timing authority

| | Mode A | Mode B |
|---|---|---|
| Clock | RP2040 hardware timer (`time_us_64`) | `performance.now()` |
| `t₀` | first target presentation, on-device | first target presentation, in-page |
| Authority | device `END` message | the page's own session |
| Never used | — | `Date.now()` (not monotonic) |

`t₀` is the instant the **first target is presented**, not the instant `START` was
issued, so neither the 3-second countdown nor any transport delay is charged to the
player.

Both implementations freeze elapsed time at exactly 60.000 s when the window expires,
so a run that ends between scan ticks does not report 60.0002 s and quietly shave the
final bit rate.

---

## 9. Data flow for one keypress (Mode A)

```
  1. finger closes a switch                              t = 0
  2. core 1's next scan reads the port                   t ≤ 250 µs
       └─ single 32-bit load, all 25 keys at once
  3. KeyScanner reports a falling edge                   t + ~2 µs normally
       ├─ healthy inputs: FIRST edge, then a 5 ms per-key lockout
       └─ GP12 workaround: 20 ms closed confirmation, 50 ms open to rearm
  4. Game::Press classifies it                           t + ~5 µs
       ├─ hit  → Sc++, draw a new target, record rt_us
       └─ miss → Si++, TARGET DOES NOT CHANGE
  5. new target queued to the LED frame                  t + ~8 µs
  6. DMA kicked; PIO clocks out 25 pixels                t + ~380 µs
  7. HIT + TARGET events pushed to the ring              t + ~10 µs   (parallel)
  8. core 0 formats them, appends CRC-16, writes to CDC  t + 1–5 ms   (display only)
  9. host parses, checks CRC, folds into its mirror, logs
 10. SSE → browser → grid repaints
```

Steps 1–6 are the player's loop. Steps 7–10 are a display and cannot affect the score.

---

## 10. Failure modes and what happens

| Failure | Behaviour |
|---|---|
| No keypad plugged in | Keyboard mode, one friendly line, fully playable |
| No Python at all | `web/play.html` opens from the filesystem and plays |
| Keypad unplugged mid-run | Link indicator turns red; the partial log is on disk |
| USB bytes lost | Sequence gap surfaced in the UI; device tally still correct |
| Host falls behind | Ticks dropped, scoring events never dropped |
| Browser tab loses focus (Mode B) | Clock pauses, run flagged, **both** rates reported |
| Dead switch or LED | Excluded by self-test; `N` reduced; report states the real `N` |
| Fewer than 3 healthy cells | Game refuses to start rather than report a meaningless number |
| Corrupt flash health record | Reads as "never tested" — every cell healthy |
| Malformed protocol line | Dropped whole, logged with the reason, never partially applied |
