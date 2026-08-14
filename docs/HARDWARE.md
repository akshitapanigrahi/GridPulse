# GRID PULSE — Hardware

A homemade 5×5 illuminated keypad built on a Raspberry Pi Pico (RP2040).

---

## 1. Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Raspberry Pi Pico (RP2040) | **not** a Pico W — see §5 |
| 25 | Mechanical keyswitches, **linear** | linear on purpose; see §6 |
| 1 | WS2812B addressable strip, 25 pixels | serpentine layout, data on GP28 |
| 1 | USB micro-B cable | data, not charge-only |
| — | Wire | one leg of every switch to a GPIO, the other to any ground pin |

---

## 2. Pinout

Grid cells are indexed `0..24` in **row-major grid order**: `cell = row × 5 + column`,
row 0 at the top, column 0 at the left.

### Cell → switch GPIO

This table follows the physical wiring and is **scrambled** relative to grid position.
It is not derivable from anything and lives in exactly one place in the code:
`firmware/include/board_map.h` (mirrored for the browser in `web/core/boardmap.js`).

```
          col 0   col 1   col 2   col 3   col 4
row 0      16      17      18      15      14
row 1      19      10      11      12      13
row 2      20      21       6       9       8
row 3       2       3       4       5       7
row 4      22      26      27       1       0
```

### Cell → LED strip index

The strip is wired **serpentine** from the top-left: row 0 left→right, row 1
right→left, and so on.

```
          col 0   col 1   col 2   col 3   col 4
row 0       0       1       2       3       4
row 1       9       8       7       6       5
row 2      10      11      12      13      14
row 3      19      18      17      16      15
row 4      20      21      22      23      24
```

### Other pins

| Pin | Function |
|---|---|
| GP28 | WS2812B data out |
| GND (any) | common ground for switches and the strip |
| VBUS (pin 40) | 5 V for the strip, if powering from USB |

---

### GP26 and GP27 are ADC pins used as digital inputs

That is fine and fully supported: their digital input buffers and internal pull-ups
work exactly as on any other pin. Nothing in the firmware enables the ADC or selects
an analogue function on them, and `firmware/src/hal/keys.cpp` says so at the point of
initialisation so a future edit does not casually add an `adc_init()`.

---

## 3. Power budget

| Condition | Current | Notes |
|---|---|---|
| RP2040 running both cores | ≈ 35 mA | |
| One WS2812B at full configured output | ≈ 60 mA | maximum normal game state |
| One WS2812B at full white | ≈ 60 mA | |
| **All 25 at full white** | **≈ 1500 mA** | **exceeds the USB budget by 3×** |
| Pico plus one full-white pixel | **≈ 95 mA** | before the strip's small quiescent load |

A USB 2.0 port is only obliged to supply 500 mA. Twenty-five pixels at full white
would draw three times that, which would brown out the regulator and reset the board.

Two things keep the design safely inside the budget:

* **The game lights exactly one LED at a time.** This is a hard invariant, not a
  tendency: `ShowOnly()` in `core1_game.cpp` is the only function in the firmware that
  turns an LED on, and it clears the frame first.
* **The USB descriptor requests 200 mA.** One full-white pixel plus the Pico is about
  95 mA before the strip's small quiescent load, leaving ample declared margin. The
  brightness setting is 255/255; safety therefore depends on the one-pixel invariant.
  Any future multi-pixel presentation must add an aggregate-current limiter first.

The USB configuration descriptor requests 200 mA, which is honest for the design and
still well below the 500 mA available to a configured USB 2.0 device.

---

## 4. Why this input modality

**Direct-wired switches, no scan matrix.** Every switch has its own GPIO. This costs
almost the entire pin budget, and it buys:

* **True N-key rollover with no ghosting.** A matrix without diodes misreports certain
  three-key combinations entirely. In a game where a player rolls from one key to the
  next before releasing the first, that is not a corner case.
* **No scan latency.** A matrix must drive one row, settle, read, and repeat. This
  design reads all 25 switches in a **single 32-bit port load**, so every key in a
  scan shares one timestamp and there is no skew between the first and last key of a
  chord.

In a game whose score *is* a latency measurement, both are on the right side of the
trade.

**Linear switches, not tactile or clicky.** A linear switch has no tactile bump and no
click mechanism, so it has the lowest actuation force and the shortest travel to
actuation of the three families. Less force and less distance is less time per press,
directly.

**Eager debounce.** The firmware registers a press on the *first* observed falling
edge and then ignores that key for 5 ms. The conventional alternative —
integrate-then-confirm, where a key must read closed for several consecutive samples —
adds its integration window to *every* press, which in this game lands straight on the
score. See `firmware/src/pure/keyscan.h`.

**GP12 bench workaround.** The installed switch on GP12 was observed producing false
closures after an apparent release. That input alone must remain continuously closed
for 20 ms before it registers and continuously open for 50 ms before it rearms. This
suppresses the faulty contact at the acknowledged cost of adding about 20 ms to that
cell's measured reaction time. The other 24 inputs remain eager; replacing the switch
and removing the special case is the correct long-term repair. If GP12 reads closed
at boot, a one-time 5 ms stable release allows calibration to rescue it; the 50 ms
release requirement applies after the first accepted press.

---

## 5. Wiring diagram

```
                   Raspberry Pi Pico
              ┌───────────────────────────┐
              │  ┌─────┐                  │
   GP0  ──────┤ 1│ USB │              40 ├────── VBUS (5V) ─────┐
   GP1  ──────┤ 2└─────┘              39 ├── VSYS              │
   GND  ──────┤ 3                     38 ├── GND ──────────┐   │
   GP2  ──────┤ 4                     37 ├── 3V3_EN        │   │
   GP3  ──────┤ 5                     36 ├── 3V3(OUT)      │   │
   GP4  ──────┤ 6                     35 ├──               │   │
   GP5  ──────┤ 7                     34 ├── GP28 ──[470R]─┼───┼──┐
   GND  ──────┤ 8                     33 ├── GND           │   │  │
   GP6  ──────┤ 9                     32 ├── GP27          │   │  │
   GP7  ──────┤10                     31 ├── GP26          │   │  │
   ...        │            ...           │   ...           │   │  │
              └───────────────────────────┘                 │   │  │
                                                            │   │  │
        Each switch:  GPIO ──○ ○── GND      (25 of them)     │   │  │
                       (internal pull-up enabled)            │   │  │
                                                            │   │  │
        WS2812B strip (25 px, serpentine):                  │   │  │
                             ┌──────────────────────────────┘   │  │
                             │  ┌───────────────────────────────┘  │
                             │  │  ┌───────────────────────────────┘
                            GND +5V DIN
                             │  │  │
                          ┌──┴──┴──┴──┐
                          │  pixel 0  │ ← top-left cell
                          │  pixel 1  │
                          │    ...    │
                          │ pixel 24  │ ← bottom-right cell
                          └───────────┘
                             │
                        1000 µF across +5V/GND, close to the strip
```

Physical layout, with the LED strip snaking through it:

```
   ┌────┬────┬────┬────┬────┐
   │ 0 →│ 1 →│ 2 →│ 3 →│ 4 ─┐    row 0, left to right
   ├────┼────┼────┼────┼────┤│
   │ 9 │← 8 ←│ 7 ←│ 6 ←│ 5 ←┘    row 1, right to left
   ├─┼──┼────┼────┼────┼────┤
   └─┘ 10 →│11 →│12 →│13 →│14 ┐  row 2, left to right
   ├────┼────┼────┼────┼────┤ │
   │19 │←18 ←│17 ←│16 ←│15 ←──┘  row 3, right to left
   ├─┼──┼────┼────┼────┼────┤
   └─┘ 20 →│21 →│22 →│23 →│24    row 4, left to right
   └────┴────┴────┴────┴────┘
```

---

## 6. Self-test and calibration

Run it with `./run.sh --selftest`, or press the button in the UI.

**Procedure.** The device lights each cell in turn, **in grid order**, and waits up to
10 s for that cell's key. Pressing the correct key passes it. Note what a single pass
verifies: the operator can only press the right key if the right LED lit, so one walk
exercises the switch, the LED, *and* both mapping tables at once.

* Any switch already closed at the start is marked `STUCK` immediately and never
  waited for.
* A cell that fails gets a **second confirmation pass** before anything is declared
  dead, so one slow finger cannot permanently shrink the alphabet.
* The result is a healthy-cell bitmask, persisted to the **last flash sector** with a
  magic number, a format version and a CRC-32.

**How a dead cell changes N.** Dead cells are excluded from the alphabet permanently
and are **never targeted** — a target the player cannot register would make the run
unwinnable. `N` becomes the count of healthy cells, and the final report states the
`N` actually used. With one dead cell the game runs at N = 24 and each selection is
worth log₂(23) = 4.52 bits instead of log₂(24) = 4.58.

If fewer than 3 cells are healthy, `log₂(N−1)` is not positive and **the game refuses
to start** rather than reporting a meaningless number.

**Recovering a cell.** A forced self-test (`--selftest`) retests everything including
cells previously marked dead, so a re-soldered joint is recoverable without
reflashing.

**Corrupt or absent record.** An erased sector, a sector written by other firmware,
and a write interrupted by power loss all fail the CRC and read back as "never
tested" — every cell healthy — rather than as a plausible mask that would silently
shrink `N` and change the score.

---

## 7. Flashing

```bash
./run.sh --flash
```

1. Unplug the board.
2. Hold **BOOTSEL** down.
3. Plug it back in, then release BOOTSEL.
4. A drive called `RPI-RP2` appears.
5. Run the command above. It copies `firmware/prebuilt/grid_pulse.uf2` across; the
   board reboots and reconnects as a serial device.

To build from source instead (needs CMake and an ARM toolchain that includes newlib —
Homebrew's `arm-none-eabi-gcc` does **not**; use
[ARM's own toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)):

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
./run.sh --build-firmware
```

---

## 8. Troubleshooting

| Symptom | Likely cause |
|---|---|
| No LEDs at all | Data on the wrong pin, or strip not powered. Check GP28 and the 5 V feed. |
| First pixel wrong, rest fine | Logic level. See §4. |
| Colours look shifted along the strip | Data line noise; add the 470 Ω series resistor. |
| One cell never lights | Dead LED, or a break in the strip at that pixel. |
| One cell never registers | Dead switch or an unsoldered joint. The self-test will exclude it and the game will run at a lower `N`. |
| Every key reads as pressed at boot | Pull-ups not enabled, or the switches are wired to 3V3 instead of GND. |
| Board resets under load | Power. See §5 — something is lighting more LEDs than it should. |
| Enumerates but the game says "did not answer the handshake" | Old or unrelated firmware on the board. Reflash. |
