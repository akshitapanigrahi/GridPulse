"""Terminal mode: playing and calibrating with no browser.

WHAT IS WORTH TESTING HERE
--------------------------
The formatting, and only the formatting. Terminal mode invents nothing about the game:
it waits on the same device events, folds them through the same SessionMirror, and
prints what that mirror says. So the risk is not that the numbers are wrong - that is
the device's job and is tested elsewhere - but that they are printed wrongly, or that
a caveat the browser raises is quietly dropped on the way to the terminal.

Every function under test is pure: state in, string out, no tty and no printing. That
is deliberate, and it is what lets the output be asserted character by character in a
suite that has no terminal attached.
"""

from __future__ import annotations

import io
import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import reconcile, replay, terminal  # noqa: E402

PLAIN = terminal.Palette(enabled=False)
COLOUR = terminal.Palette(enabled=True)


def mirror_after(events):
    m = reconcile.SessionMirror()
    for event in events:
        m.apply(event)
    return m


def mirror_of_a_real_run(**overrides):
    """A mirror that actually watched the run it is ending.

    Applying only the END leaves the host's own counters at zero, which is a REAL
    disagreement and is correctly flagged - so a fixture that skips the run would
    make the no-disagreement case impossible to test and the disagreement case pass
    for the wrong reason.
    """
    event = end_event(**overrides)
    return mirror_after([
        {"type": "MODE", "mode": event["mode"], "state": "RUNNING",
         "n": event["n"], "seed": event["seed"]},
        {"type": "TICK", "t_run_us": event["t_us"],
         "sc": event["sc"], "si": event["si"]},
        event,
    ])


def end_event(**overrides):
    event = {
        "type": "END", "n": 25, "sc": 40, "si": 4, "t_us": 60_000_000,
        "b_mbps": 2751, "reason": "COMPLETE", "mode": "EVAL", "seed": 0xDEADBEEF,
        "draws": 45, "repeats": 2, "max_streak": 17,
        "min_us": 280_000, "p50_us": 410_000, "p95_us": 900_000, "p99_us": 1_400_000,
    }
    event.update(overrides)
    return event


class TestLiveLine(unittest.TestCase):
    def test_it_reports_the_cumulative_rate_not_a_recent_one(self):
        """The same quantity the HUD's hero number shows: the score so far for the
        whole run. B = log2(24) * (10-2) / 20 = 1.834."""
        m = mirror_after([
            {"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 20_000_000, "sc": 10, "si": 2},
        ])
        line = terminal.format_live(m, PLAIN)
        self.assertIn("BPS   1.83", line)
        self.assertIn("Sc  10", line)
        self.assertIn("Si   2", line)
        # A scored run counts DOWN: 40 s of the 60 s window remain.
        self.assertIn("40.0 s left", line)

    def test_practice_counts_up_because_it_has_nothing_to_count_down_to(self):
        m = mirror_after([
            {"type": "MODE", "mode": "PRACTICE", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 20_000_000, "sc": 4, "si": 0},
        ])
        line = terminal.format_live(m, PLAIN)
        self.assertIn("20.0 s", line)
        self.assertNotIn("left", line)

    def test_the_clock_does_not_go_negative_past_the_window(self):
        """The device freezes t at exactly the scored duration, but a late tick or a
        clock skew must not print a negative countdown."""
        m = mirror_after([
            {"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 61_500_000, "sc": 40, "si": 1},
        ])
        self.assertIn("  0.0 s left", terminal.format_live(m, PLAIN))

    def test_a_net_negative_run_reads_zero_rather_than_negative(self):
        """max(Sc-Si, 0) is a property of the formula, and the terminal must not
        present a negative rate the scoring rule cannot produce."""
        m = mirror_after([
            {"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 10_000_000, "sc": 1, "si": 9},
        ])
        self.assertIn("BPS   0.00", terminal.format_live(m, PLAIN))

    def test_no_reaction_time_is_claimed_before_the_first_hit(self):
        m = mirror_after([
            {"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 1_000_000, "sc": 0, "si": 0},
        ])
        self.assertIn("— ms", terminal.format_live(m, PLAIN))

    def test_colour_is_only_used_when_the_output_is_a_terminal(self):
        """Same rule as run.sh: piping this to a file must produce something readable
        rather than escape soup."""
        m = mirror_after([
            {"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25, "seed": 1},
            {"type": "TICK", "t_run_us": 5_000_000, "sc": 3, "si": 0},
        ])
        self.assertNotIn("\x1b[", terminal.format_live(m, PLAIN))
        self.assertIn("\x1b[", terminal.format_live(m, COLOUR))


class TestRunSummary(unittest.TestCase):
    def test_it_shows_the_formula_with_the_numbers_substituted(self):
        """The results screen shows the working rather than just the answer, so that
        the score can be checked by hand. The terminal should not be a lesser
        artefact than the page."""
        m = mirror_after([end_event()])
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertIn("B = 2.751", text)
        self.assertIn("log2(N-1) * max(Sc-Si, 0) / t = 4.585 * 36 / 60.000 = 2.751",
                      text)
        self.assertIn("Sc  (correct)   : 40", text)
        self.assertIn("Si  (incorrect) : 4", text)
        self.assertIn("reaction p50/95/99 (ms): 410 / 900 / 1400", text)
        self.assertIn("0xDEADBEEF", text)

    def test_a_practice_run_is_labelled_unscored(self):
        m = mirror_after([end_event(mode="PRACTICE")])
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertIn("NOT SCORED", text)

    def test_an_aborted_run_says_its_rate_is_not_comparable(self):
        """t is short of the full window, so B is not comparable with a full run.
        Printing the number without that caveat would invite exactly that comparison."""
        m = mirror_after([end_event(reason="ABORT", t_us=12_000_000)])
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertIn("aborted", text)
        self.assertIn("not comparable", text)

    def test_a_device_host_disagreement_is_surfaced(self):
        """THE CAVEAT THAT MUST NOT BE LOST. The host recomputes B from the device's
        own Sc, Si, N and t; a mismatch means a firmware scoring bug, and the terminal
        has to say so rather than printing a number that looks fine."""
        # Counters agree; only the device's own B disagrees with the recomputation.
        m = mirror_of_a_real_run(b_mbps=9999)
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertIn("FLAGGED", text)
        self.assertIn("device=9.999", text)
        self.assertIn("authoritative", text)

    def test_a_clean_run_is_not_flagged(self):
        m = mirror_of_a_real_run()
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertNotIn("FLAGGED", text)

    def test_a_zero_length_run_does_not_divide_by_zero(self):
        m = mirror_after([end_event(t_us=0, sc=0, si=0, b_mbps=0)])
        text = "\n".join(terminal.format_run_summary(m.final, PLAIN))
        self.assertIn("B = 0.000", text)
        self.assertIn("accuracy        : 0.0%", text)


class TestCalibrationOutput(unittest.TestCase):
    def test_a_cell_verdict_names_the_pins_behind_it(self):
        """Printing the GPIO and pixel is what makes a mis-wire diagnosable from the
        output alone, rather than merely detected."""
        line = terminal.format_selftest_cell(
            {"cell": 8, "gpio": 12, "pixel": 6, "result": "STUCK", "pass": 1}, PLAIN)
        self.assertIn("cell  8", line)
        self.assertIn("GP12", line)
        self.assertIn("px6", line)
        self.assertIn("STUCK", line)

    def test_a_clean_walk_says_so(self):
        verdicts = {cell: "OK" for cell in range(25)}
        text = "\n".join(terminal.format_selftest_summary(verdicts, 25, PLAIN))
        self.assertIn("All 25 cells passed", text)
        self.assertIn("N is now 25", text)

    def test_failures_are_named_with_their_consequence(self):
        verdicts = {cell: "OK" for cell in range(25)}
        verdicts[8] = "STUCK"
        verdicts[17] = "NO_KEY"
        text = "\n".join(terminal.format_selftest_summary(verdicts, 23, PLAIN))
        self.assertIn("2 cell(s) did not pass", text)
        self.assertIn("cell 8 (STUCK)", text)
        self.assertIn("cell 17 (NO_KEY)", text)
        # The point of a dead cell is that it changes N, and the operator has to know.
        self.assertIn("N is now 23", text)


class TestRendering(unittest.TestCase):
    """The impure half, driven through a plain StringIO rather than a terminal."""

    def setUp(self) -> None:
        self.out = io.StringIO()
        self.mirror = reconcile.SessionMirror()
        self.sent = []
        self.ui = terminal.TerminalUI(
            self.mirror,
            lambda name, args: (self.sent.append(name), True)[1],
            stream=self.out,
        )

    def feed(self, event):
        self.mirror.apply(event)
        self.ui.on_event(event)

    def test_a_whole_run_renders_without_a_terminal(self):
        self.feed({"type": "MODE", "mode": "EVAL", "state": "COUNTDOWN", "n": 25,
                   "seed": 1})
        self.feed({"type": "MODE", "mode": "EVAL", "state": "RUNNING", "n": 25,
                   "seed": 1})
        for second in range(1, 6):
            self.feed({"type": "TICK", "t_run_us": second * 1_000_000,
                       "sc": second, "si": 0})
        self.feed(end_event(sc=5, si=0, t_us=5_000_000, b_mbps=4585))

        text = self.out.getvalue()
        # The countdown is on the keypad, timed against the device's own clock. The
        # terminal says what to look at and claims nothing about the timing.
        self.assertIn("get ready", text)
        self.assertNotIn("3… 2… 1…", text)
        # Redirected output is plain lines, never in-place cursor tricks.
        self.assertNotIn("\x1b[K", text)
        self.assertNotIn("\r", text)
        self.assertIn("EVALUATION COMPLETE", text)

    def test_the_end_of_a_run_releases_the_waiting_input_loop(self):
        """_play blocks on this; if END did not set it, the terminal would hang after
        every run with no way out but Ctrl-C."""
        self.assertFalse(self.ui._run_done.is_set())
        self.feed(end_event())
        self.assertTrue(self.ui._run_done.is_set())

    def test_a_walk_ends_on_the_hello_the_firmware_sends_afterwards(self):
        self.ui._calibrating = True
        self.feed({"type": "SELFTEST", "cell": 0, "gpio": 16, "pixel": 0,
                   "result": "OK", "pass": 1})
        self.assertFalse(self.ui._walk_done.is_set())
        self.feed({"type": "HELLO", "n": 25})
        self.assertTrue(self.ui._walk_done.is_set())
        self.assertIn("cell  0", self.out.getvalue())

    def test_a_hello_outside_a_walk_is_not_a_completion(self):
        """The device re-announces itself whenever a host attaches. Treating that as a
        finished walk would print a summary for a walk that never ran."""
        self.feed({"type": "HELLO", "n": 25})
        self.assertFalse(self.ui._walk_done.is_set())
        self.assertNotIn("cells passed", self.out.getvalue())

    def test_device_warnings_reach_the_operator(self):
        self.feed({"type": "LOG", "level": "W", "msg": "health_mask_not_persisted"})
        self.assertIn("health_mask_not_persisted", self.out.getvalue())


if __name__ == "__main__":
    unittest.main()


class TestReplayTiming(unittest.TestCase):
    """Dead air is collapsed; everything inside a run is reproduced exactly.

    A log starts when the HOST started, not when anyone played, so recordings routinely
    open with a minute of nothing. Reproducing that faithfully is technically honest and
    useless to watch - the default replay sat on a blank grid for 63 seconds.
    """

    def test_a_gap_inside_a_run_is_reproduced_exactly(self):
        """The device ticks four times a second, so a run is never quiet for longer
        than 250 ms. Every one of those gaps must survive untouched: they are the
        timing the replay exists to show."""
        self.assertAlmostEqual(replay.gap_seconds(1_000_000, 1_250_000), 0.25)
        self.assertAlmostEqual(replay.gap_seconds(0, 900_000), 0.9)

    def test_the_countdown_survives_intact(self):
        """The one long gap that carries meaning. Collapsing it would make the 3-2-1
        wrong, which is the one thing the replay must get right."""
        self.assertAlmostEqual(replay.gap_seconds(0, 3_000_000), 3.0)

    def test_idle_stretches_are_collapsed(self):
        for idle_s in (5.3, 63.3, 233.4, 301.6):
            self.assertEqual(replay.gap_seconds(0, int(idle_s * 1e6)),
                             replay.MAX_REPLAYED_GAP_S)

    def test_a_backwards_timestamp_is_not_a_wait(self):
        """END carries the run's elapsed time rather than device uptime, so it runs
        backwards against its neighbours. Treating that as a wait would stall."""
        self.assertEqual(replay.gap_seconds(293_352_627, 60_000_000), 0.0)

    def test_the_description_says_how_long_it_will_actually_take(self):
        events = [{"t_us": 0}, {"t_us": 63_000_000}, {"t_us": 63_250_000}]
        recorded, played = replay.replay_span(events)
        self.assertAlmostEqual(recorded, 63.25)
        self.assertAlmostEqual(played, replay.MAX_REPLAYED_GAP_S + 0.25)
