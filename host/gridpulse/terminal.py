"""GRID PULSE terminal mode: play and calibrate with no browser at all.

WHY THIS EXISTS
---------------
The browser is a display. Everything it shows during a run - the bit rate, the
counters, the reaction time, the final tally - is folded from the same event stream
this process already parses, and the score itself is computed on the RP2040 either
way. So the page is convenient rather than necessary, and on a machine where opening
one is awkward (over ssh, on a headless box, or simply when you would rather not) the
game should still be playable and the keypad should still be calibratable.

WHAT IS AND IS NOT DUPLICATED
-----------------------------
The formatting here is a presentation of the device's numbers, not a second opinion on
them. The one exception is deliberate: B is recomputed from the device's own Sc, Si, N
and t by reconcile.bit_rate, exactly as the browser does, so a firmware scoring bug
shows up as a disagreement rather than being taken on trust. That check already lives
in SessionMirror; this module only prints what it found.

LAYOUT DISCIPLINE
-----------------
Every function that produces text is pure: state in, string out, no printing and no
terminal state. That is what makes the output testable without a tty, and it is why
the live line and the summary can be asserted character by character in the suite.
The impure part - cursor tricks, colour, reading stdin - is confined to TerminalUI.

Standard library only.
"""

from __future__ import annotations

import sys
import threading
from typing import Any, Callable, Dict, List, Optional

from . import reconcile

# Rewritten in place on a tty; printed as ordinary lines when redirected, at a rate a
# log file can live with.
LIVE_REDRAW_INTERVAL_US = 250_000
PLAIN_LINE_INTERVAL_US = 1_000_000


class Palette:
    """ANSI colours, or nothing at all when the output is not a terminal.

    Same rule as run.sh: pretty on a tty, plain when redirected, so piping the output
    to a file produces something readable rather than escape soup.
    """

    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def _wrap(self, code: str, text: str) -> str:
        return ("\x1b[%sm%s\x1b[0m" % (code, text)) if self.enabled else text

    def dim(self, text: str) -> str:
        return self._wrap("2", text)

    def bold(self, text: str) -> str:
        return self._wrap("1", text)

    def cyan(self, text: str) -> str:
        return self._wrap("36", text)

    def green(self, text: str) -> str:
        return self._wrap("32", text)

    def amber(self, text: str) -> str:
        return self._wrap("33", text)

    def red(self, text: str) -> str:
        return self._wrap("31", text)


# --- pure formatting ---------------------------------------------------------------


def format_live(mirror: reconcile.SessionMirror, palette: Palette) -> str:
    """The one-line readout shown while a run is in progress.

    The bit rate is cumulative over elapsed time, exactly as the HUD's hero number is:
    it is the score so far for the whole run, not a speed over some recent window.
    """
    elapsed_s = mirror.elapsed_us / 1e6
    rate = reconcile.bit_rate(mirror.n, mirror.correct, mirror.incorrect, elapsed_s)
    reaction = ("%4d ms" % round(mirror.last_reaction_us / 1000.0)
                if mirror.last_reaction_us > 0 else "   — ms")
    # A scored run counts DOWN, because what the player needs to know is how much of
    # the window is left, not how much has gone. Practice is untimed and has nothing
    # to count down to, so it counts up.
    clock = ("%5.1f s" % elapsed_s if mirror.mode == "PRACTICE"
             else "%5.1f s left" % max(0.0, reconcile.EVAL_DURATION_S - elapsed_s))
    return "  %s  %s  %s  %s  %s  last %s" % (
        palette.dim(clock),
        palette.bold(palette.cyan("BPS %6.2f" % rate)),
        palette.green("Sc %3d" % mirror.correct),
        palette.amber("Si %3d" % mirror.incorrect),
        palette.dim("streak %2d" % mirror.streak),
        reaction,
    )


def format_run_summary(final: Dict[str, Any], palette: Palette) -> List[str]:
    """The end-of-run block. Mirrors the results screen, including its caveats."""
    n = int(final.get("n", 0))
    sc = int(final.get("correct", 0))
    si = int(final.get("incorrect", 0))
    elapsed_s = float(final.get("elapsed_s", 0.0))
    bits = float(final.get("bits_per_selection", 0.0))
    rate = float(final.get("bit_rate", 0.0))
    net = max(sc - si, 0)
    presses = sc + si
    unscored = final.get("mode") == "PRACTICE"

    lines = [""]
    lines.append(palette.bold(
        "  PRACTICE COMPLETE — NOT SCORED" if unscored
        else "  60-SECOND EVALUATION COMPLETE"))
    lines.append("")
    lines.append("  %s  %s" % (
        palette.bold(palette.cyan("B = %.3f" % rate)), palette.dim("bits / second")))
    lines.append("")
    lines.append(palette.dim(
        "  B = log2(N-1) * max(Sc-Si, 0) / t = %.3f * %d / %.3f = %.3f"
        % (bits, net, elapsed_s, rate)))
    lines.append("")
    lines.append("  N               : %d   (%.3f bits / selection)" % (n, bits))
    lines.append("  Sc  (correct)   : %d" % sc)
    lines.append("  Si  (incorrect) : %d" % si)
    lines.append("  t   (seconds)   : %.3f" % elapsed_s)
    lines.append("  accuracy        : %.1f%%"
                 % ((sc / presses * 100.0) if presses else 0.0))
    lines.append("  presses/sec     : %.2f"
                 % ((presses / elapsed_s) if elapsed_s > 0 else 0.0))
    lines.append("  best streak     : %d" % int(final.get("max_streak", 0)))
    lines.append("  reaction p50/95/99 (ms): %d / %d / %d" % (
        round(int(final.get("p50_us", 0)) / 1000.0),
        round(int(final.get("p95_us", 0)) / 1000.0),
        round(int(final.get("p99_us", 0)) / 1000.0)))
    lines.append("  repeat targets  : %d of %d"
                 % (int(final.get("repeats", 0)), int(final.get("draws", 0))))
    lines.append("  RNG seed        : 0x%08X" % (int(final.get("seed", 0)) & 0xFFFFFFFF))

    if final.get("reason") == "ABORT":
        lines.append("")
        lines.append(palette.amber("  This run was aborted, so t is short of the full "
                                   "window and B is not comparable."))

    # The same disagreement the results screen raises. Silence here means the device
    # and this process independently agree on the score.
    rec = final.get("reconciliation") or {}
    if not rec.get("agreed", True):
        lines.append("")
        lines.append(palette.amber(
            "  FLAGGED: %s. The device's figures are authoritative and are what is "
            "shown above." % "; ".join(rec.get("mismatches", []))))
    lines.append("")
    return lines


def format_selftest_cell(event: Dict[str, Any], palette: Palette) -> str:
    """One line per cell verdict, naming the pins behind it.

    The GPIO and pixel are printed for the same reason the calibration screen prints
    them: a mis-wire is then not merely detected but named.
    """
    result = str(event.get("result", "?"))
    colour = {"OK": palette.green, "NO_KEY": palette.amber, "STUCK": palette.red}
    paint = colour.get(result, palette.dim)
    return "  cell %2d   GP%-2d  px%-2d   pass %d   %s" % (
        int(event.get("cell", -1)),
        int(event.get("gpio", -1)),
        int(event.get("pixel", -1)),
        int(event.get("pass", 0)),
        paint(result),
    )


def format_selftest_summary(verdicts: Dict[int, str], n: int,
                            palette: Palette) -> List[str]:
    """The verdict for the walk as a whole."""
    bad = sorted(cell for cell, result in verdicts.items() if result != "OK")
    lines = [""]
    if not bad:
        lines.append(palette.green("  All %d cells passed." % len(verdicts)))
    else:
        lines.append(palette.amber(
            "  %d cell(s) did not pass: %s."
            % (len(bad), ", ".join("cell %d (%s)" % (c, verdicts[c]) for c in bad))))
        lines.append(palette.dim(
            "  These are excluded from the alphabet and will never be targeted."))
    lines.append(palette.dim("  N is now %d." % n))
    lines.append("")
    return lines


def format_menu(port: str, n: int, palette: Palette) -> List[str]:
    return [
        "",
        "  %s  %s" % (palette.bold("GRID PULSE"),
                      palette.dim("keypad on %s, N=%d" % (port, n))),
        "  %s" % palette.dim(
            "[enter] 60-second run   [p] practice   [c] calibrate   [q] quit"),
        "",
    ]


# --- the driver --------------------------------------------------------------------


class TerminalUI:
    """Renders the device's event stream to a terminal and reads commands from stdin.

    Everything stateful and impure lives here: cursor control, colour, the input
    thread. The formatting above is pure and is what the tests exercise.
    """

    def __init__(self, mirror: reconcile.SessionMirror,
                 send: Callable[[str, Dict[str, Any]], bool],
                 stream=None) -> None:
        self.mirror = mirror
        self.send = send
        self.out = stream or sys.stdout
        self.tty = bool(getattr(self.out, "isatty", lambda: False)())
        self.palette = Palette(self.tty)

        self._run_done = threading.Event()
        self._walk_done = threading.Event()
        self._calibrating = False
        self._verdicts: Dict[int, str] = {}
        self._last_live_us = 0
        self._live_open = False

    # -- output helpers ------------------------------------------------------

    def _write(self, text: str) -> None:
        self.out.write(text)
        self.out.flush()

    def _lines(self, lines: List[str]) -> None:
        self._end_live()
        self._write("\n".join(lines) + "\n")

    def _end_live(self) -> None:
        """Close off an in-place live line so the next output starts cleanly."""
        if self._live_open:
            self._write("\n")
            self._live_open = False

    # -- event handling ------------------------------------------------------

    def on_event(self, event: Dict[str, Any]) -> None:
        """Called for every parsed device event, from the reader thread."""
        kind = event.get("type")

        if kind == "MODE":
            state = event.get("state")
            if state == "COUNTDOWN":
                # The countdown itself is on the keypad - the centre cell pulses three
                # times, timed against the device's own clock. Printing "3 2 1" here
                # only guessed at where that clock had got to, so this says what to
                # look at and claims nothing about the timing.
                self._lines([
                    "  " + self.palette.dim("get ready — the centre key flashes three "
                                            "times")])
            elif state == "RUNNING":
                self._last_live_us = 0

        elif kind in ("TICK", "HIT", "MISS"):
            self._maybe_live()

        elif kind == "END":
            self._end_live()
            if self.mirror.final is not None:
                self._lines(format_run_summary(self.mirror.final, self.palette))
            self._run_done.set()

        elif kind == "SELFTEST":
            self._verdicts[int(event.get("cell", -1))] = str(event.get("result", "?"))
            self._lines([format_selftest_cell(event, self.palette)])

        elif kind == "HELLO" and self._calibrating:
            # The firmware re-announces itself when the walk finishes, so this is the
            # completion signal and its `n` is the alphabet it will play with now.
            self._lines(format_selftest_summary(
                self._verdicts, int(event.get("n", self.mirror.n)), self.palette))
            self._walk_done.set()

        elif kind == "LOG":
            level = event.get("level")
            if level in ("W", "E"):
                self._lines(["  " + self.palette.amber(
                    "device: %s" % event.get("msg", "?"))])

    def _maybe_live(self) -> None:
        interval = LIVE_REDRAW_INTERVAL_US if self.tty else PLAIN_LINE_INTERVAL_US
        if self.mirror.elapsed_us - self._last_live_us < interval:
            return
        self._last_live_us = self.mirror.elapsed_us
        line = format_live(self.mirror, self.palette)
        if self.tty:
            # Carriage return and clear-to-end: one line, rewritten in place.
            self._write("\r\x1b[K" + line)
            self._live_open = True
        else:
            self._write(line + "\n")

    # -- the input loop ------------------------------------------------------

    def run(self, port: str, stop: threading.Event) -> None:
        """Blocks until the operator quits. Ctrl-C also unwinds through here."""
        while not stop.is_set():
            self._lines(format_menu(port, self.mirror.n, self.palette))
            try:
                typed = sys.stdin.readline()
            except (KeyboardInterrupt, ValueError):
                return
            if typed == "":            # stdin closed
                return

            choice = typed.strip().lower()
            if choice in ("q", "quit", "exit"):
                return
            if choice == "c":
                self._calibrate()
            elif choice == "p":
                self._play("PRACTICE")
            elif choice == "":
                self._play("EVAL")
            else:
                self._lines(["  " + self.palette.dim("not a choice: %s" % choice)])

    def _play(self, mode: str) -> None:
        self._run_done.clear()
        if not self.send("START", {"mode": mode}):
            self._lines(["  " + self.palette.red("the device refused to start")])
            return
        if mode == "PRACTICE":
            self._lines(["  " + self.palette.dim(
                "practice is untimed and unscored — press ctrl-c to stop")])
        # The device owns the clock; wait for it to say it is done. Polled rather
        # than blocked outright: an untimed wait can swallow SIGINT on CPython, and
        # being unable to Ctrl-C out of a sixty-second run would be its own bug.
        while not self._run_done.wait(0.2):
            pass

    def _calibrate(self) -> None:
        self._walk_done.clear()
        self._verdicts = {}
        self._calibrating = True
        try:
            if not self.send("SELFTEST", {"force": True}):
                self._lines(["  " + self.palette.red(
                    "the device refused to calibrate")])
                return
            self._lines(["  " + self.palette.dim(
                "each cell lights in turn — press the key under the lit key "
                "(5 s per cell)")])
            while not self._walk_done.wait(0.2):
                pass
        finally:
            self._calibrating = False
