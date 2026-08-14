# GRID PULSE

A game to maximise bit rate by pressing the lit cell in a 5×5 grid. One cell lights
up, you hit it. A miss leaves it lit, so you still have to hit it. The run lasts sixty
seconds.

```
B = log₂(N − 1) × max(Sc − Si, 0) / t        N = 25,  log₂(24) = 4.585 bits/selection
```

`Sc` is correct selections, `Si` incorrect, `t` elapsed seconds.

## Overview

The game runs in two modes.

**Mode A — the custom keypad.** A 5×5 illuminated keypad built on a Raspberry Pi Pico:
25 linear mechanical switches, each with its own GPIO, and a 25-pixel WS2812B strip
behind them. The target lights up in the same physical object you press. The whole
game loop (sampling, timing, scoring) runs on the RP2040. The browser is only a
display.

**Mode B — the keyboard.** The same game core running in the browser, using three
QWERTY rows plus two as the 5×5 grid. It is the fallback if the device is not
available or fails, and it makes the game playable on any machine with a browser and
nothing else.

Both produce real scores. A badge on screen always says whether the keypad is live.

---

## Quick start

Plug in the device with a micro USB cable, and launch the shell script:

```bash
./run.sh
```

It uses the keypad if one is plugged in and the keyboard if
not, opens a browser, and writes a session log to `logs/`.

If Python is not available, or you would rather not run a server:

```bash
open web/play.html          # macOS
xdg-open web/play.html      # Linux
```

That gives you Mode B with no server, no build step and no network. Double-clicking
the file in a file manager works too.

**How to play.** One cell lights up, press that key. Press PRACTICE first; it is
untimed and unscored. Then press START RUN. A miss keeps the same cell lit, so you
still have to hit it.

On the keyboard the grid follows your keyboard rows, so the shape on screen is the
shape under your fingers:

```
Q W E R T
Y U I O P
A S D F G
H J K L Z          M is unused.
X C V B N
```

---

## Every command

### Playing

| Command | What it does |
|---|---|
| `./run.sh` | Keypad if present, keyboard if not. Opens a browser. |
| `./run.sh --terminal` | Play and calibrate entirely in the terminal. No browser, no HTTP server. Needs the keypad. |
| `./run.sh --keyboard` | Mode B only. Never opens a serial port. |
| `./run.sh --hardware` | Require the keypad. Exits with the reason if it is missing. |
| `./run.sh --selftest` | Launch straight onto the calibration screen. |
| `./run.sh --replay FILE` | Replay a recorded session at its original timing. |
| `./run.sh --replay-speed N` | Multiplier for the above. `20` skips the idle stretches. |
| `open web/play.html` | Mode B with nothing installed at all. |

In terminal mode the menu keys are **enter** for a scored run, **p** for practice,
**c** to calibrate, **q** to quit. Ctrl-C gets out of anything, including mid-run.

### Options

| Flag | What it does |
|---|---|
| `--port /dev/cu.usbmodem101` | Skip auto-detection and use this device. |
| `--no-browser` | Serve the page but do not open it. |
| `--no-log` | Do not write to `logs/`. |
| `--http-port 8765` | Preferred localhost port. |
| `--verbose` | Extra HTTP and protocol diagnostics. |

### Firmware

| Command | What it does |
|---|---|
| `./run.sh --flash` | Copy the prebuilt `.uf2` onto a Pico in BOOTSEL mode. |
| `./run.sh --build-firmware` | Rebuild from source. Needs `PICO_SDK_PATH` and an ARM toolchain. |

### Analysis

| Command | What it does |
|---|---|
| `python3 tools/latency_report.py logs/FILE.jsonl` | Reaction time, presentation interval, link loss, transport jitter, USB round trip. |
| `python3 tools/latency_report.py logs/FILE.jsonl --per-cell` | Adds median reaction time laid out as the 5×5 grid. |
| `python3 tools/validate_sequence.py logs/FILE.jsonl` | Chi-square uniformity, chi-square on pairs, runs test on repeats, against your actual sequence. |
| `python3 tools/validate_sequence.py --simulate 100000` | The same three tests against the shipped sampler. |
| `python3 tools/validate_sequence.py --simulate 100000 --seed 0x9A3F21C4` | Reproduce one specific run's sequence from the seed its report printed. |
| `./run.sh --link` | Measure the USB link and explain the figure, then exit. |

### Testing and maintenance

| Command | What it does |
|---|---|
| `./run.sh --test` | All six suites. Exits non-zero if any fail. |
| `make -C tests/native` | Just the C++ suite. Faster while working on firmware logic. |
| `python3 -m unittest discover -s tests/host` | Just the Python suite. |
| `node web/tests/ui_test.js` | Just the Mode B UI suite. |
| `open web/tests.html` | Run the JavaScript suites in a browser instead of node. |
| `python3 tools/gen_vectors.py` | Regenerate the golden vectors from the reference implementation. |
| `python3 tools/gen_vectors.py --check` | Verify the committed vectors still match. |
| `python3 tools/check_offline.py` | Verify Mode B still works from `file://`. |

`tools/refimpl.py` is not a command. It is the Python reference implementation the
vectors are generated from.

---

## The hardware

A homemade 5×5 illuminated keypad on a Raspberry Pi Pico (RP2040).

### Wiring

Every switch is wired directly between its own GPIO and ground, with the RP2040's
internal pull-up enabled.

```
        Each switch:  GPIO ──○ ○── GND      (25 of them, internal pull-up on)

        WS2812B strip: DIN ← GP28 through 470 Ω,  +5V ← VBUS,  GND ← GND
```

### Pinout

Cells are indexed `0..24` row-major: `cell = row × 5 + column`, row 0 top, column 0
left. The switch wiring is scrambled relative to grid position, following how the
board was physically assembled. It lives in one place in the code,
`firmware/include/board_map.h`, mirrored for the browser in `web/core/boardmap.js`.

Cell → switch GPIO:

```
          col 0   col 1   col 2   col 3   col 4
row 0      16      17      18      15      14
row 1      19      10      11      12      13
row 2      20      21       6       9       8
row 3       2       3       4       5       7
row 4      22      26      27       1       0
```

Cell → LED strip index, serpentine from the top-left:

```
          col 0   col 1   col 2   col 3   col 4
row 0       0       1       2       3       4
row 1       9       8       7       6       5
row 2      10      11      12      13      14
row 3      19      18      17      16      15
row 4      20      21      22      23      24
```

These are separate `constexpr` tables. `static_assert`s prove at compile time that the
GPIO table has no duplicates, avoids GP23–GP25 and the LED pin, and that the pixel
table is a permutation of 0–24. The self-test walks the grid in grid order.

### Switches

Linear switches were used, which means no bump and no click mechanism, leading to lower
actuation force and the shorter travel to actuation. Less force
and less distance is less time per press.

A press registers on the first observed falling edge, and the key is then ignored for
5 ms. The usual alternative, integrate-then-confirm, adds its integration window to
every press, and in this game that window lands straight on the score. Bounce happens
after the first edge rather than before it, so eager detection rejects it just as well
at no latency cost.

Releases are handled the opposite way. Break-bounce
outlasts make-bounce; a worn switch can chatter for tens of milliseconds as the
contacts separate. So a key is re-armed only once it has read open continuously for
25 ms. Re-arming on the first open sample turned that chatter into a phantom second
press, and because the phantom lands after the next target is already lit, it scored
as a miss and cancelled the hit before it. Waiting costs nothing measurable: it delays
only the arming, never the measurement, because a key cannot legitimately be pressed
again until it has been released.

That window is bounded. A contact too dirty to ever go quiet would otherwise obstruct
its own release forever and the key would simply stop working, so after 250 ms of
obstruction the key is armed anyway, provided the switch reads open at that moment. A
jammed switch therefore stays held and silent rather than firing four times a second.

### Power

The game lights exactly one LED at a time. `ShowOnly()` is the only function in the
firmware that turns an LED on, and it clears the frame first. Typical draw during play
is about 50 mA, well inside the 500 mA a USB 2.0 port must supply.

---

## Building and flashing

The compiled firmware is committed at `firmware/prebuilt/grid_pulse.uf2`, so flashing
needs no toolchain at all:

```bash
./run.sh --flash
```

Unplug the board, hold BOOTSEL, plug it back in, release. A drive called `RPI-RP2`
appears, the command copies the `.uf2` across, and the board reboots as a serial
device. The drive disappearing is the success signal.

To build from source instead:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
./run.sh --build-firmware
```

That needs CMake and an ARM toolchain including newlib. Homebrew's
`arm-none-eabi-gcc` does not include newlib; ARM's own toolchain does. The build
writes `firmware/build/grid_pulse.uf2` and copies it over the prebuilt one.

Both the firmware and the native test suite build with `-Wall -Wextra -Werror` plus
`-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`, `-Wshadow` and `-Wpedantic`,
and pass with zero warnings.

---

## How the firmware works

The RP2040 splits the job across its two cores. Core 1 scans all 25 active-low switches
at 4 kHz, debounces each press, advances the game state, scores it against the hardware
timer, and sends the next one-pixel LED frame through PIO and DMA. Core 0 handles USB
serial commands and events. Lock-free queues connect the two, which keeps browser and
USB delays off the timing-critical input path. The final score is computed on the keypad
itself.

```
   CORE 1  the game                        CORE 0  the link
   ─────────────────────                   ─────────────────────
   scan 25 GPIOs @ 4 kHz                   read USB CDC
   debounce                     ── events ──►  frame, CRC, write
   advance state, score                    ◄── commands ──
   hardware timer                          
   ShowOnly() → PIO → DMA → WS2812B        
```

Neither core ever waits on the other. The two ring buffers between them are
single-producer, single-consumer and lock-free, so core 1's loop has no blocking call in
it anywhere: not on USB, not on the LED write (DMA carries the frame out after core 1
has moved on), and not on the host. If the host stops reading, outbound lines are
dropped rather than queued, because a full queue that blocked would stall the game
itself.

The source is split by what it depends on rather than by what it does:

| | |
|---|---|
| `firmware/src/pure/` | The game: RNG, scoring, FSM, debounce, protocol, self-test, health mask. No Pico SDK include anywhere, so all of it compiles and runs on a laptop, which is what the 163-test C++ suite exercises. The suite's `check-purity` target runs before the tests do and fails if an SDK header ever appears here, so the boundary is enforced rather than merely intended. |
| `firmware/src/hal/` | The parts that can only run on the chip: GPIO, the WS2812 PIO program and its DMA, flash persistence, USB. |
| `firmware/include/board_map.h` | The two hardware tables, and nowhere else. |

That split is why almost all of the firmware is testable without hardware. The logic
that decides what a press means has no idea a GPIO exists.

---

## Calibration

To calibrate the hardware:

```bash
./run.sh --selftest              # opens the calibration screen
```

or press RE-CALIBRATE KEYPAD on the launch screen, or `c` in terminal mode.

The device lights each cell in turn in grid order and waits for that cell's key. One
walk exercises the switch, the LED and both mapping tables at once, because you can
only press the right key if the right LED lit. 

The browser mirrors the walk cell by cell, printing each verdict next to the GPIO and
pixel that cell is wired to, so a mis-wire is not merely detected but named. The
screen also cross-checks every pin the firmware reports against `web/core/boardmap.js`
and flags any disagreement, which is the one failure the walk itself cannot see.

The walk does not begin until you press START on that screen. It allows five seconds
per cell and marks a cell dead after two misses.

A walk also stops if you close the host. The device sees the USB port close and
abandons it.

### What happens when a cell fails

A cell that fails gets a second confirmation pass before anything is declared dead, so
one slow finger cannot shrink the alphabet. The result is persisted to the last flash
sector with a magic number and a CRC-32. A corrupt or erased record reads back as
"never tested" rather than as a plausible mask that would silently change `N`. A forced
walk retests cells previously marked dead, so a re-soldered joint is recoverable
without reflashing.

**A dead cell changes N.** It is excluded permanently and never targeted, because a
target the player cannot register would make the run unwinnable. With one dead cell the
game runs at N = 24, `log₂(N−1)` drops from 4.585 to 4.524 bits per selection, and the
final report states the `N` actually used, so scores from a reduced board are not
silently compared with a full one. Below three healthy cells `log₂(N−1)` is not
positive, and the game refuses to start rather than report a meaningless number.

### Diagnosing a switch the firmware has written off

`tools/hwtest/hwtest.py` is a standalone MicroPython probe that shares nothing with the
game firmware: no health mask, no alphabet, no notion of a cell being dead. It drives
all 25 LEDs and reads all 25 switches unconditionally.

The firmware filters excluded cells out of both of its paths, so a
cell it has written off cannot be exercised through either, and the one cell you most
need to look at is the one it refuses to touch.

Open it in Thonny with the interpreter set to MicroPython, and use `switch_watch()` to
see all 25 pins live. A switch closing on its own with nothing touching it shows up
immediately. Note that flashing MicroPython erases the game firmware; put it back with
`./run.sh --flash`.

---

## Prototype status and next steps

This is a a hand-built prototype, which introduces some limitations:

**Light leakage between keys.** The keycaps are translucent, and light from one lit
cell spills into its neighbours. I cut walls to sit between the LEDs and glued them in,
but they were laser-cut and hand-glued rather than printed, so the fit is not
consistent from cell to cell and the leakage varies around the grid. The practical
consequence is that a lit cell can look ambiguous from a shallow angle. I'd recommend looking at the
keypad close to straight on. 

For a next iteration:

- **3D-printed enclosure with integrated light baffles**, so the walls are part of the
  case rather than glued in afterwards, and every cell is isolated identically.
- **A PCB instead of hand wiring.** Twenty-five switches each on their own GPIO is a
  lot of point-to-point soldering, and it is the source of the one marginal joint the
  firmware currently has to work around.
- **Opaque keycaps with a diffused window**, so the lit area is defined by the cap
  rather than by how far light travels through it.
- **An LCD or OLED on the device.** The score, the countdown and the reaction time all
  exist on the RP2040 already; putting a small display on the board would make the
  keypad fully self-contained and remove the host from the loop entirely for anyone who
  just wants to play.
- **Everything in house.** Printed case, fabricated PCB, and a firmware image that
  ships with the board, so the whole thing is one object rather than a board and a
  laptop.

---

## Where the game actually runs

The game loop runs on the keypad, not in the browser, which  was an important
design decision in the project.

A USB CDC round trip is a few milliseconds. At four presses per second that is a couple
of percent of the score given away, and worse than the mean is the variance: USB is
polled on 1 ms frames and scheduled by the host OS, so that jitter would land inside every
measured reaction time.

Running the core on the RP2040 means every timestamp comes from its hardware timer. The
host learns about an event a millisecond or two later, but when the host learns has no
effect on what was recorded.

| Stage | Time | In the player's loop? |
|---|---|---|
| Key closes → detected | ≤ 250 µs | yes (4 kHz scan) |
| Detected → scored | < 10 µs | yes |
| Scored → new LED lit | ≈ 380 µs | yes |
| **Machine total** | **≈ 640 µs** | ≈ 0.2% of a 300 ms reaction |
| Scored → host → browser | 1–3 ms | no, display only |

Core 1 runs the game and never blocks. Core 0 runs USB. They share only lock-free ring
buffers.

### What this means in practice

**The run survives the browser.** If you close the page or kill the host mid-run, the
device keeps playing: the LEDs keep advancing, the presses keep scoring, and the run
ends on the device's own clock.

---

## Reaction times

Every hit records the gap between the target lighting and the key going down, measured
on the RP2040's hardware timer. That number is the raw material for everything the
report says about speed.

**LAST** in the HUD is the reaction time of your most recent hit. It only updates on a
hit, since a miss produces no reaction time, and it reads `— ms` until your first hit
of the run. It is one sample, so it jumps around far more than your actual pace does.

**p50 / p95 / p99** on the results screen are percentiles over every hit in the run.
Sort them fastest to slowest: p50 is the median, p95 means one press in twenty was
slower than that, p99 means one in a hundred was. `460 / 1270 / 3800 ms` reads as
typically 460 ms, but one press in twenty took over a second and one in a hundred took
nearly four.

They are computed on the device by nearest rank, so every value reported is an actual
measured press rather than an interpolation between two. With few samples p95 and p99
can land on the same press, which is expected rather than a bug.

---

## Session logs

Every run through `./run.sh` writes two files to `logs/`:

```
logs/gridpulse-2026-08-14T10-20-20.jsonl     every event, in order
logs/gridpulse-2026-08-14T10-20-20.csv       one row per completed run
```

The `.jsonl` is the complete raw record, not just the final numbers:

```
{"type":"SESSION","input_mode":"hardware","link":{"median_ms":2.57,...}}
{"seq":33,"t_us":36008990,"type":"MODE","state":"RUNNING","seed":4075599884,"n":25}
{"seq":42,"t_us":37211059,"type":"TARGET","cell":6,"idx":2,"repeat":false}
{"seq":43,"t_us":37251296,"type":"MISS","pressed":8,"target":6,"sc":1,"si":3}
…
{"type":"REPORT","report":{ …the full result… }}
```

Every target, every press, every device timestamp in microseconds, then the final
tally. Four things you can do with it:

**Verify the sampler was not rigged.**

```bash
python3 tools/validate_sequence.py logs/gridpulse-2026-08-14T10-20-20.jsonl
```

Chi-square on uniformity, chi-square on consecutive pairs, and a runs test on repeats,
against your actual target sequence rather than a simulation.

**Analyse your latency.**

```bash
python3 tools/latency_report.py logs/gridpulse-2026-08-14T10-20-20.jsonl --per-cell
```

Separates reaction time from presentation interval, reports link loss and transport
jitter, and breaks reaction time down by grid position.

**Replay it.**

```bash
./run.sh --replay logs/gridpulse-2026-08-14T10-20-20.jsonl --replay-speed 20
```

Feeds the recording back through the same pipeline a live device uses. Useful for
showing the interface to someone with no keypad present. Timing inside a run is
reproduced exactly; idle stretches between runs are collapsed, because a log starts
when the host started and can otherwise open with a minute of nothing.

The browser also has a DOWNLOAD LOG button, which produces the same shape of file from
the events the page received. That matters when you opened `play.html` directly from
disk, since a `file://` page cannot write anywhere and this is the only way a Mode B
run produces an auditable artefact.

---

## Playing without a browser

```bash
./run.sh --terminal
```

Serves nothing and opens no port. The game is played on the keypad and reported in the
terminal:

```
  GRID PULSE  keypad on /dev/cu.usbmodem101, N=25
  [enter] 60-second run   [p] practice   [c] calibrate   [q] quit

  get ready — the centre key flashes three times
   47.6 s left  BPS  10.72  Sc  31  Si   2  streak  9  last  284 ms
```

The end-of-run block carries the same figures and the same caveats as the results
screen, including the flag raised if the host's recomputed `B` ever differs from the
device's own. Calibration works the same way, printing each cell's verdict with the
GPIO and pixel behind it.

This is possible because the browser was never doing any of the work. The run is timed,
scored and tallied on the RP2040, and every front end is a presentation of the same
event stream.

---

## Why this input modality

The point of the keypad is that the stimulus and the response occupy the same physical
place. The cell that lights up is the cell you press. You find the light with your
hand, and the hand is already there.

On a screen-plus-keyboard setup the stimulus appears in one object and the response
happens in a different one. Even with a good spatial mapping, the player is looking at
a grid over there and pressing a key down here, and the mapping between them is
something the brain maintains rather than something the world provides. That mapping is
the part of the task that is not reaction time, and it is exactly the part the physical
keypad deletes.

That is also why Mode B orders the grid by keyboard position rather than
alphabetically. It makes the on-screen grid and the keys under the fingers the same
shape, which is as close as a screen can get to the keypad's property.

## Why the keyboard layout is not alphabetical

```
Q W E R T          not          A B C D E
Y U I O P                       F G H I J
A S D F G                       K L M N O
H J K L Z                       P Q R S T
X C V B N                       U V W X Y
```

Alphabetical placement forces a lookup on every selection: see the lit cell, read its
letter, recall where that letter lives, move. That is a translation stage in the
critical path and it roughly halves throughput. Keyboard-ordered placement lets typing
muscle memory carry the spatial mapping instead.

Matching is on `event.code`, the physical key position, never `event.key`, so play is
correct on QWERTY, AZERTY and Dvorak.

## Why N = 25

The bit rate factors cleanly:

```
B = log₂(N − 1) × r × (2p − 1)

     r = presses per second        p = per-press accuracy
```

Because a miss cancels a hit rather than merely failing to score, dropping from 95% to
85% accuracy costs 22% of the score outright, before counting the time the miss
consumed. Accuracy is worth roughly twice what raw speed is, so the game never rewards
mashing.

Raising `N` raises `log₂(N − 1)` but lowers `r`. Hick–Hyman says choice reaction time
grows roughly with `log₂(N)`, so the two effects partly cancel:

| N | bits/selection | vs N = 25 |
|---|---|---|
| 3 | 1.00 | −78% |
| 9 | 3.00 | −35% |
| 16 | 3.91 | −15% |
| **25** | **4.58** | — |
| 36 | 5.13 | +12%, but needs a hand reposition |

25 sits at a good point for a physical reason: a 5×5 grid is the largest a hand span
covers without repositioning, and on a keyboard it is exactly three QWERTY rows plus
two. Going to 36 buys 12% more per selection and costs more than that in travel,
because the fingers must move rather than pivot. Going down to 16 gives up 15% for a
speed gain that does not materialise, since the limiting factor at that size is visual
search rather than travel.

`N` is threaded through the core as a single named constant, so a selectable N is a
small change. There is no UI for it.

---

## Mode A and Mode B are the same game core

The game core is one specification implemented three times: a Python reference that
*generates* golden vectors, and C++ and JavaScript implementations that are both
validated against them.

A golden vector is a recorded correct answer. The vectors pin, for five seeds and three
alphabet sizes, the first 16 raw generator outputs, the first 256 drawn indices
explicitly, a digest over all 10 000, the number of rejected draws, and the exact RNG
state afterwards. Nothing checks the C++ against the JavaScript directly. Both are
checked against the same fixed answers, which also catches the case where both drift
the same way. If they diverge by a single draw, the suite fails and names the index.

| | Mode A — Hardware | Mode B — Keyboard |
|---|---|---|
| Input | 25 direct-wired linear switches | keyboard |
| Game core runs on | the RP2040 | the browser |
| Clock | RP2040 hardware timer | `performance.now()` |
| N | 25, less any cell the self-test excludes | always 25 |

That apparatus exists for a specific reason. JavaScript has no 32-bit integers: `a * b`
silently loses low bits above 2⁵³, and `<<`/`^` yield signed results. `Math.imul` and
`>>> 0` are mandatory in every expression of the generator, and getting one wrong
produces a sequence that looks random and is wrong. 

---

## How ground truth is unambiguous

At every instant there is exactly one correct action, it is visible, and a wrong action
does not change what the correct action is.

* **Exactly one target.** One LED lit on the device, one cell lit on screen. This is an
  invariant of the interface: `ShowOnly()` is the only function in the firmware that
  turns an LED on, and it clears the frame first.
* **A miss does not change the target.** `Si` increments and the same cell stays lit.
* **Key-down only.** Key-up is never a selection. A held key produces exactly one event,
  and there is no auto-repeat anywhere.
* **Keys outside the alphabet are inert.** Not a hit, not a miss, no effect on the
  score. The end-of-run report states how many were pressed.
* **The clock starts at the first target presentation**, not at the START command, so
  neither the countdown nor any transport delay is charged to the player.

## How i.i.d. sampling is guaranteed, and how to check

**Generation.** xoshiro128\*\*, seeded on-device from the RP2040 ring oscillator, with
rejection sampling rather than modulo. Plain `next() % N` biases the low indices; at
N = 25 the bias is about one part in 2×10⁸ per draw, which is small but real and
detectable over a long session.

**Repeats are never resampled.** Sampling is with replacement, so the same cell can come
up twice in a row, and it is left alone. Resampling to avoid a repeat is a leak: after
seeing *x* you would know *x* cannot be next, so real entropy per selection would fall
below log₂(N) and the reported bit rate would overstate the information transferred. A
repeat is disambiguated at the presentation layer instead. The cell goes dark for 60 ms
and relights, so it reads as flash-then-relight rather than as nothing happening.

**The host cannot influence the sequence.** There is no command that supplies a seed,
sets a target, or changes `N`. That is a structural claim about the command set, and
`tests/native/test_protocol.cpp` asserts it by feeding the parser `CMD SETSEED
seed=12345` and requiring rejection. The seed is reported in `MODE` and `END`, so any
run is reproducible and auditable without being controllable.

**Verify it:**

```bash
python3 tools/validate_sequence.py --simulate 100000              # the shipped sampler
python3 tools/validate_sequence.py --simulate 100000 --seed 0x9A3F21C4
python3 tools/validate_sequence.py logs/gridpulse-*.jsonl         # a real session
```

The `--seed` form is what makes a score auditable. The seed *is* the sequence: given
the one printed on the results screen, anyone can regenerate the exact targets you were
shown and confirm you were not handed an easy run.

Three tests, each aimed at a different way the claim could fail: chi-square goodness of
fit catches bias, chi-square on consecutive pairs catches resampling, and a runs test
catches repeat manipulation.

```
1. UNIFORMITY      chi2 =  27.663, dof =  24, p = 0.2745   PASS
2. INDEPENDENCE    chi2 = 565.379, dof = 576, p = 0.6161   PASS
3. REPEATS         3889 in 99999 pairs; expected 4000.0; z = -1.79   PASS

VERDICT: consistent with i.i.d. uniform sampling with replacement.
```

Pointed at a sampler that resamples to avoid repeats it returns z = −50 and p = 0 on
the independence test. Pointed at a modulo-biased one it returns p = 0 on uniformity.
A single 60-second run draws only a few hundred targets, so the tool says outright when
the sample is too small to be decisive.

---

## Measured latency

```bash
python3 tools/latency_report.py logs/gridpulse-*.jsonl --per-cell
```

Five things come out of a recorded session:

**Reaction time.** Target lit to key down, from the device's hardware timer. This is
what the bit rate is made of.

**Presentation interval.** The gap between consecutive targets.

**Link loss.** The device numbers every event from boot, so a discontinuity in the log
means bytes were lost between it and whoever wrote the file. Display only: the score is
computed on the device from state the host never touched.

**Transport delay.** Every event carries both the device's timestamp and the moment the
host learned of it. The absolute difference is meaningless, since the two clocks have
unrelated origins, but the spread is the link's jitter, which is the part a display
would feel.

**USB round trip.** Measured at connect with a dozen `PING`/`HELLO` exchanges and
recorded in the log header, so a log is self-describing about the link it was recorded
over. `./run.sh --link` reports it on demand:

```
  round trip    min 1.34 ms    median 2.61 ms    p95 3.27 ms    max 3.28 ms

  A round trip is host -> keypad -> host, so one way is roughly half:
  about 1.3 ms for an event to reach this machine.
```

Both figures come with their caveats attached. The round trip is an upper bound; it
includes the host's read-poll granularity. The transport delay is jitter, not a one-way
figure. Neither can reach the score, and that is the whole reason the score is
timestamped on the device.

`--per-cell` breaks reaction time down by grid position and then tests the layout claim
rather than asserting it. It pools the 16 edge cells against the 9 interior ones and
runs a Mann-Whitney U, reporting either a difference or that this run cannot
distinguish one from noise. Across 24 recorded sessions here it found the edge slower
in 7 and inconclusive in 17, and never once found the interior slower. The effect is
real in direction, but one 60-second run is usually too small to establish it.

---

## Testing

```bash
./run.sh --test
```

Six suites: 266 named tests in C++ and Python, plus 553 assertions in the two
JavaScript suites. 

| Suite | Size | What it covers |
|---|---|---|
| **Golden vectors** | 6 files | Regenerates from the reference implementation and diffs, so a stale mirror fails rather than passes. |
| **`file://` reachability** | 2 pages, 16 sources | No ES modules, no `fetch`, no XHR, no remote or absolute assets on the Mode B path. The file list is derived from the pages' own script tags, not hand-maintained. |
| **Native C++** | 163 tests, 226 256 assertions | RNG vs vectors, scoring clamps, CRC, protocol framing, board map, health mask, FSM, debounce and release settling, run-transition events, self-test, ring buffer. |
| **Host Python** | 103 tests | Protocol parity with the firmware, fuzz, reconciliation, logging, event-stream delivery over a real socket, hot-plug, exclusive port access, terminal rendering, replay timing. |
| **JavaScript core** | 321 assertions | The same RNG and scoring vectors, session rules, 32-bit arithmetic regressions. |
| **Mode B UI** | 232 assertions | Boots the real page, plays a full 60 s run, asserts the results screen, the stream/command ordering, hot-plug, calibration and audio wiring. |

### Native C++, by file

| File | Tests | Covers |
|---|---|---|
| `test_fsm.cpp` | 25 | State machine, scoring clamps, the 60 s window |
| `test_keyscan.cpp` | 20 | Debounce, release settling, chatter rejection |
| `test_queue_event.cpp` | 17 | The lock-free ring buffer between the cores |
| `test_protocol.cpp` | 15 | Framing, CRC, commands that must not exist |
| `test_healthmask.cpp` | 14 | Dead cells, N, the persisted format |
| `test_selftest.cpp` | 14 | The calibration walk |
| `test_scoring.cpp` | 14 | B, percentiles |
| `test_boardmap.cpp` | 12 | The two hardware tables |
| `test_rng.cpp` | 12 | xoshiro128\*\* against the vectors |
| `test_crc.cpp` | 9 | CRC-16/CCITT-FALSE |
| `test_runevents.cpp` | 11 | What the device *says* about a run |


### Host Python, by file

| File | Tests | Covers |
|---|---|---|
| `test_protocol.py` | 31 | Parser parity with the firmware, fuzz |
| `test_terminal.py` | 25 | Terminal rendering, replay timing |
| `test_bridge.py` | 21 | Handshake over a real pty, link measurement |
| `test_reconcile.py` | 12 | Device-versus-host tally reconciliation |
| `test_hotplug.py` | 8 | Plug, unplug, reattach |
| `test_serial_port.py` | 3 | Exclusive port access |
| `test_server.py` | 3 | SSE delivery over a real socket |

`test_bridge.py` runs against a real pseudo-terminal rather than a mock, because the
bug it was written for (handshaking before starting the reader thread) was invisible to
mocks. `test_server.py` boots a real HTTP server on a real socket.

### The Mode B UI suite

With no browser automation available, it boots the real `play.html` markup and the real
UI code against a small DOM harness (`web/tests/dom_harness.js`), synthesises keystrokes
on a controlled clock, plays a full 60-second run, and asserts the reported `B` against
the formula recomputed from first principles.

The JavaScript suites run under node, or macOS's built-in JavaScriptCore, or in a
browser via `web/tests.html`. If no engine is found, `--test` fails rather than silently
skipping.

---

## Layout

```
run.sh                  one command, zero installation
web/play.html           the whole game, double-clickable, no build step
web/core/               game core: RNG, scoring, alphabet, session, board map
web/ui/                 grid, HUD, effects, sparkline, calibration, sound, controller
web/transport/          keyboard (Mode B) and SSE (Mode A)
host/gridpulse/         Python bridge, stdlib only, no pip, no pyserial
firmware/src/pure/      game logic, no SDK dependency, host-testable
firmware/src/hal/       GPIO, WS2812 PIO + DMA, flash persistence
firmware/include/       board_map.h, the two hardware tables, and only here
firmware/prebuilt/      grid_pulse.uf2, committed so no toolchain is needed
tests/                  native C++, host Python, golden vectors
tools/                  reference implementation, vector generator, analysis
```

**Dependencies: none.** No npm, no pip, no bundler, no CDN, no pyserial. The CDC device
is opened directly with `termios`. The only hard requirement is a browser. Python 3.8+
is optional and adds hardware support and session logs.

**Platform.** macOS and Linux are supported. Windows can play Mode B by opening
`web/play.html`, but the host is POSIX-only, so the keypad and the local server are not
available there.

---

## Design decisions

**Focus loss pauses the clock, and says so.** An OS notification stealing focus should
not wreck a run. But pausing means the official `t` is shorter than wall time, so the
run is flagged and the results screen reports both the paused-clock and wall-clock
rates. An uninterrupted run reports the two as identical, so a discrepancy is itself the
signal.

**Feedback never relies on colour alone.** A hit is a cyan expanding ring with a radial
pulse; a miss is an amber X with a lateral shake. Different hue, different shape,
different motion axis, so the game is fully playable with colour vision deficiency.

Sound follows the same principle. A hit is a short rising sine, a miss a lower falling
triangle, opposite in pitch, direction and timbre. The countdown gets a flat beat per
number and the run starting gets a longer rising one; flat is deliberate, since
everything that means *act* here glides. All of it is synthesised from two oscillators
rather than played from files, because `play.html` has to work from a `file://` origin
where no asset can be fetched. It toggles from the speaker icon and the choice is
remembered.

**The device's tally is authoritative.** The host keeps its own counters so the display
has something to show between ticks, but at end of run it takes the device's figures and
records any disagreement rather than smoothing it over. It also recomputes `B`
independently from the device's own `Sc`, `Si`, `N` and `t`, so a firmware scoring bug
shows up as a mismatch instead of being taken on trust.

**The keypad can be plugged in at any time.** The host watches for it, so plugging in
mid-session makes Mode A selectable within a second and unplugging greys it out. No
restart, and the launch screen updates on its own rather than when you next press START.
