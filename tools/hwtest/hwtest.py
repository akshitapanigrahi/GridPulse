# GRID PULSE - standalone hardware test. MicroPython, for Thonny.
#
# WHAT THIS IS
#   A bare hardware probe that shares nothing with the GRID PULSE firmware: no health
#   mask, no alphabet, no self-test state machine, no notion of a cell being "dead".
#   It drives all 25 LEDs and reads all 25 switches directly, unconditionally.
#
# WHY IT EXISTS
#   The firmware filters excluded cells out of BOTH of its paths - the self-test drops
#   them from pending_mask_, and the game drops them from the alphabet - so a cell the
#   firmware has written off cannot be exercised through either. That is the correct
#   behaviour for a scoring instrument and useless for diagnosing a board, because the
#   one cell you most need to look at is the one it refuses to touch. Nothing here
#   consults any mask, so every cell is always testable.
#
# HOW TO USE IT
#   1. Hold BOOTSEL, plug the board in, and copy a MicroPython .uf2 onto RPI-RP2.
#      This ERASES the GRID PULSE firmware. To get the game back, flash
#      firmware/prebuilt/grid_pulse.uf2 the same way.
#   2. Open this file in Thonny, set the interpreter to "MicroPython (Raspberry Pi
#      Pico)", and press Run.
#
# THE TESTS
#   1  led_sweep()     every LED in turn, no input needed - a purely visual check
#   2  switch_watch()  live read of all 25 switches - finds a stuck or dead switch
#   3  walk()          lights a cell, waits for its key: the full paired check
#
#   Test 2 is the one that answers "is this switch shorted?", because it reports the
#   pin state continuously and needs nothing to be pressed for a fault to show up.

import time

import rp2
from machine import Pin

# --- wiring -----------------------------------------------------------------------
#
# Copied verbatim from firmware/include/board_map.h. These are the two tables the
# whole design hinges on, and conflating them is the most likely bug in the project,
# so they are kept separate and named here exactly as they are there.
#
# Cells are indexed 0..24 in row-major grid order: cell = row * 5 + col.

# Cell -> switch GPIO. Wiring order, not derivable from anything.
CELL_TO_GPIO = [
    16, 17, 18, 15, 14,
    19, 10, 11, 12, 13,
    20, 21,  6,  9,  8,
     2,  3,  4,  5,  7,
    22, 26, 27,  1,  0,
]

# Cell -> WS2812B strip index. Serpentine from the top-left.
CELL_TO_PIXEL = [
     0,  1,  2,  3,  4,
     9,  8,  7,  6,  5,
    10, 11, 12, 13, 14,
    19, 18, 17, 16, 15,
    20, 21, 22, 23, 24,
]

LED_DATA_GPIO = 28
CELL_COUNT = 25

# Well under the firmware's own cap. One lit pixel is ~20 mA at this level, and only
# led_sweep() ever lights more than one at a time.
BRIGHTNESS = 60


# --- WS2812B driver ----------------------------------------------------------------
#
# The standard PIO program from the MicroPython docs: 800 kHz, GRB order, one 24-bit
# word per pixel. Independent of the firmware's PIO program in every respect.

@rp2.asm_pio(sideset_init=rp2.PIO.OUT_LOW,
             out_shiftdir=rp2.PIO.SHIFT_LEFT,
             autopull=True,
             pull_thresh=24)
def ws2812():
    T1, T2, T3 = 2, 5, 3
    wrap_target()
    label("bitloop")
    out(x, 1)               .side(0)[T3 - 1]
    jmp(not_x, "do_zero")   .side(1)[T1 - 1]
    jmp("bitloop")          .side(1)[T2 - 1]
    label("do_zero")
    nop()                   .side(0)[T2 - 1]
    wrap()


class Strip:
    def __init__(self, gpio, count):
        self.count = count
        self.buf = [0] * count
        self.sm = rp2.StateMachine(0, ws2812, freq=8_000_000,
                                   sideset_base=Pin(gpio))
        self.sm.active(1)
        self.clear()

    def set_pixel(self, pixel, r, g, b):
        self.buf[pixel] = (g << 16) | (r << 8) | b

    def clear(self):
        for i in range(self.count):
            self.buf[i] = 0
        self.show()

    def show(self):
        for word in self.buf:
            self.sm.put(word, 8)
        time.sleep_us(300)   # reset latch

    def only(self, pixel, r, g, b):
        """Light exactly one pixel. Mirrors the firmware's single-lit-LED rule, which
        is also what keeps the current draw to a single pixel's worth."""
        for i in range(self.count):
            self.buf[i] = 0
        self.set_pixel(pixel, r, g, b)
        self.show()


# --- switches -----------------------------------------------------------------------

class Switches:
    """All 25 switch inputs, active-low with internal pull-ups - the same electrical
    arrangement the firmware uses, so a reading here means the same thing there."""

    def __init__(self):
        self.pins = [Pin(g, Pin.IN, Pin.PULL_UP) for g in CELL_TO_GPIO]
        # The pull-ups need a moment to charge the pin capacitance. Reading too early
        # shows every switch closed, which is precisely the false alarm this whole
        # exercise is trying to rule out.
        time.sleep_ms(50)

    def closed(self, cell):
        return self.pins[cell].value() == 0

    def closed_mask(self):
        return [c for c in range(CELL_COUNT) if self.closed(c)]


# --- helpers -------------------------------------------------------------------------

def describe(cell):
    return "cell %2d (row %d col %d, GP%-2d, px%-2d)" % (
        cell, cell // 5, cell % 5, CELL_TO_GPIO[cell], CELL_TO_PIXEL[cell])


def banner(text):
    print()
    print("=" * 62)
    print(text)
    print("=" * 62)


# --- test 1: LEDs only ----------------------------------------------------------------

def led_sweep(strip, laps=2, dwell_ms=180):
    """Light every LED in turn. No switch involved, no input needed.

    Watch for a clean left-to-right, top-to-bottom sweep. A cell that stays dark is a
    dead pixel or a break in the chain. A zig-zag means CELL_TO_PIXEL is wrong.
    """
    banner("TEST 1 - LED sweep (watch the board; nothing to press)")
    print("Every cell lights in grid order. Expect a left-to-right sweep.")
    print("Note any cell that stays dark.\n")

    for lap in range(laps):
        for cell in range(CELL_COUNT):
            strip.only(CELL_TO_PIXEL[cell], BRIGHTNESS, BRIGHTNESS, BRIGHTNESS)
            print("  lit  " + describe(cell))
            time.sleep_ms(dwell_ms)
    strip.clear()
    print("\nSweep done. Any cell that never lit has an LED or wiring fault.")


# --- test 2: switches only -------------------------------------------------------------

def switch_watch(switches, seconds=30):
    """Report every closed switch, continuously. Nothing needs to be pressed.

    This is the test that identifies a stuck switch. With nothing touching the board,
    the closed list must be empty. Any cell listed while you are not touching it is
    shorted or jammed - that is a hardware fault, not a firmware verdict.
    """
    banner("TEST 2 - switch watch (take your hands off the board first)")
    print("With nothing pressed, the list below must stay EMPTY.")
    print("Anything listed while untouched is a stuck switch.\n")

    resting = switches.closed_mask()
    if resting:
        print("!! STUCK AT REST:")
        for cell in resting:
            print("     " + describe(cell) + "  <-- closed with nothing pressed")
        print()
        print("   That switch is shorted or mechanically held down. It reads as")
        print("   pressed to any firmware, which is why the game excludes it.")
    else:
        print("   OK - no switch is closed at rest. All 25 read open.")

    print("\nNow press keys one at a time; each press should appear below.")
    print("Watching for %d seconds...\n" % seconds)

    last = None
    deadline = time.ticks_add(time.ticks_ms(), seconds * 1000)
    seen = set()
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        now = switches.closed_mask()
        if now != last:
            if now:
                for cell in now:
                    if cell not in resting:
                        seen.add(cell)
                print("  closed: " + ", ".join("cell %d (GP%d)" % (c, CELL_TO_GPIO[c])
                                               for c in now))
            else:
                print("  closed: -")
            last = now
        time.sleep_ms(30)

    never = [c for c in range(CELL_COUNT) if c not in seen and c not in resting]
    print("\nRegistered a press on %d of %d cells." % (len(seen), CELL_COUNT))
    if never:
        print("Never saw a press on: " + ", ".join(str(c) for c in never))
        print("(Only meaningful if you actually pressed all of them.)")


# --- test 3: LED and switch together ------------------------------------------------------

def walk(strip, switches, timeout_ms=8000):
    """Light a cell, wait for its key. The paired check, with no cell excluded.

    This is what the firmware's self-test does, minus the health mask - so it covers
    the cells the firmware refuses to walk. Pressing the right key can only happen if
    the right LED lit, so one pass exercises the switch, the LED and both tables.
    """
    banner("TEST 3 - guided walk (press the key under each lit cell)")
    print("Every cell is tested, including any the firmware has written off.")
    print("%d seconds per cell; do nothing to skip one.\n" % (timeout_ms // 1000))

    results = {}
    for cell in range(CELL_COUNT):
        # A switch already closed cannot produce a fresh press, so say so and move on
        # rather than making the operator wait out a timeout that cannot succeed.
        if switches.closed(cell):
            strip.only(CELL_TO_PIXEL[cell], BRIGHTNESS, 0, BRIGHTNESS)
            print("  %s  STUCK - already closed, LED lit anyway so you can see it"
                  % describe(cell))
            results[cell] = "STUCK"
            time.sleep_ms(900)
            continue

        strip.only(CELL_TO_PIXEL[cell], 0, BRIGHTNESS, BRIGHTNESS)
        deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
        got = False
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            if switches.closed(cell):
                got = True
                break
            time.sleep_ms(5)

        results[cell] = "OK" if got else "NO_KEY"
        print("  %s  %s" % (describe(cell), results[cell]))

        # Wait for release so one long press cannot satisfy the next cell too.
        while switches.closed(cell):
            time.sleep_ms(5)
        time.sleep_ms(120)

    strip.clear()

    banner("RESULT")
    ok = [c for c, r in results.items() if r == "OK"]
    print("%d of %d cells fully working." % (len(ok), CELL_COUNT))
    for state in ("STUCK", "NO_KEY"):
        bad = [c for c, r in results.items() if r == state]
        if bad:
            print("\n%s:" % state)
            for cell in bad:
                print("   " + describe(cell))
    return results


# --- entry point ---------------------------------------------------------------------------

def main():
    print("GRID PULSE - standalone hardware test")
    print("Independent of the game firmware: no health mask, no alphabet,")
    print("no cell is ever excluded from testing.")

    strip = Strip(LED_DATA_GPIO, CELL_COUNT)
    switches = Switches()

    led_sweep(strip)
    switch_watch(switches)
    walk(strip, switches)

    print("\nDone. To restore the game, hold BOOTSEL while plugging in and copy")
    print("firmware/prebuilt/grid_pulse.uf2 onto the RPI-RP2 drive.")


if __name__ == "__main__":
    main()
