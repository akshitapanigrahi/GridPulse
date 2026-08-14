// Native tests: the device-side game state machine.
//
// This mirrors web/tests/core_test.js assertion for assertion where the two
// implementations must agree, so the two modes cannot drift apart in behaviour even
// though they share no code. The rules being pinned down are the ones that define
// what "ground truth" means in this game:
//
//   - exactly one target is lit at any moment
//   - a miss does NOT change the target
//   - a key outside the alphabet is neither correct nor incorrect
//   - t starts when the first target is PRESENTED, not when START was received
//   - the scored window is exactly 60 seconds, frozen at the boundary

#include "../../firmware/src/pure/fsm.h"

#include "../../firmware/src/pure/rng.h"
#include "../../firmware/src/pure/scoring.h"
#include "test_util.h"

using gridpulse::EndReason;
using gridpulse::Game;
using gridpulse::GameState;
using gridpulse::HealthMask;
using gridpulse::kCellCount;
using gridpulse::kCountdownUs;
using gridpulse::kEvalDurationUs;
using gridpulse::PressResult;
using gridpulse::RunMode;
using gridpulse::RunReport;
using gridpulse::Xoshiro128StarStar;

namespace {

// Starts a game and runs the countdown out, leaving it in RUNNING with t0 at
// `countdown_end_us`. Returns that instant.
std::uint64_t StartAndRun(Game* game, RunMode mode, std::uint32_t seed,
                          std::uint64_t start_us) {
  game->Start(mode, seed, start_us);
  const std::uint64_t t0 = start_us + kCountdownUs;
  game->Tick(t0);
  return t0;
}

std::size_t AnyOtherCell(std::size_t cell) {
  return (cell + 7) % kCellCount;
}

}  // namespace

TEST(Fsm, StartsIdleWithNothingLit) {
  Game game;
  CHECK_TRUE(game.state() == GameState::kIdle);
  CHECK_EQ_U(game.target_cell(), kCellCount);
  CHECK_EQ_U(game.alphabet_size(), kCellCount);
  CHECK_EQ_U(game.correct(), 0u);
  CHECK_EQ_U(game.incorrect(), 0u);
}

TEST(Fsm, CountdownDoesNotConsumeTheClock) {
  Game game;
  game.Start(RunMode::kEval, 1, 1000);
  CHECK_TRUE(game.state() == GameState::kCountdown);
  CHECK_EQ_U(game.target_cell(), kCellCount);
  CHECK_EQ_U(game.ElapsedUs(1000 + kCountdownUs / 2), 0u);
  CHECK_EQ_U(game.CountdownRemainingUs(1000), kCountdownUs);
  CHECK_EQ_U(game.CountdownRemainingUs(1000 + kCountdownUs / 2), kCountdownUs / 2);

  // A press during the countdown scores nothing.
  CHECK_TRUE(game.Press(0, 1500) == PressResult::kIgnored);
  CHECK_EQ_U(game.correct(), 0u);
  CHECK_EQ_U(game.incorrect(), 0u);

  game.Tick(1000 + kCountdownUs);
  CHECK_TRUE(game.state() == GameState::kRunning);
}

TEST(Fsm, TZeroIsTheFirstTargetPresentationNotTheStartCommand) {
  Game game;
  const std::uint64_t start = 5'000'000;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 42, start);

  // Neither the countdown nor any transport delay is charged to the player.
  CHECK_EQ_U(game.ElapsedUs(t0), 0u);
  CHECK_EQ_U(game.ElapsedUs(t0 + 1'000'000), 1'000'000u);
  CHECK_TRUE(game.target_cell() < kCellCount);
}

TEST(Fsm, ExactlyOneTargetIsLitAtAnyMoment) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 7, 0);
  for (int i = 0; i < 500; ++i) {
    CHECK_TRUE(game.target_cell() < kCellCount);
    now += 100'000;
    game.Press(game.target_cell(), now);
  }
  // And nothing is lit once the run ends.
  game.Abort(now);
  CHECK_EQ_U(game.target_cell(), kCellCount);
}

TEST(Fsm, AMissDoesNotChangeTheTarget) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 99, 0);
  const std::size_t target = game.target_cell();
  const std::size_t wrong = AnyOtherCell(target);

  CHECK_TRUE(game.Press(wrong, t0 + 100'000) == PressResult::kMiss);
  CHECK_EQ_U(game.incorrect(), 1u);
  CHECK_EQ_U(game.correct(), 0u);
  CHECK_EQ_U(game.target_cell(), target);

  // Repeatedly, too. The player must still hit it.
  CHECK_TRUE(game.Press(wrong, t0 + 200'000) == PressResult::kMiss);
  CHECK_TRUE(game.Press(wrong, t0 + 300'000) == PressResult::kMiss);
  CHECK_EQ_U(game.incorrect(), 3u);
  CHECK_EQ_U(game.target_cell(), target);
  CHECK_EQ_U(game.draws(), 1u);

  CHECK_TRUE(game.Press(target, t0 + 400'000) == PressResult::kHit);
  CHECK_EQ_U(game.correct(), 1u);
  CHECK_EQ_U(game.draws(), 2u);
}

TEST(Fsm, StreakTracksConsecutiveHitsAndResetsOnAMiss) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 3, 0);
  for (int i = 0; i < 5; ++i) {
    now += 100'000;
    game.Press(game.target_cell(), now);
  }
  CHECK_EQ_U(game.streak(), 5u);

  now += 100'000;
  game.Press(AnyOtherCell(game.target_cell()), now);
  CHECK_EQ_U(game.streak(), 0u);

  now += 100'000;
  game.Press(game.target_cell(), now);
  CHECK_EQ_U(game.streak(), 1u);

  RunReport report;
  game.Abort(now);
  game.BuildReport(&report);
  CHECK_EQ_U(report.max_streak, 5u);
}

TEST(Fsm, TargetStreamIsExactlyTheSamplerStream) {
  // The strongest statement that repeats are not resampled: the sequence of targets
  // must be bit-for-bit the sampler's output, repeats and all. Resampling to avoid a
  // consecutive repeat would break i.i.d. sampling and leak information.
  const std::uint32_t seed = 0x5A5A5A5Au;
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kPractice, seed, 0);

  Xoshiro128StarStar oracle(seed);
  std::size_t repeats = 0;
  std::size_t previous = kCellCount;

  for (int i = 0; i < 2000; ++i) {
    const std::uint32_t expected = oracle.DrawIndex(kCellCount);
    CHECK_EQ_U(game.target_cell(), expected);
    if (game.target_cell() == previous) {
      ++repeats;
    }
    previous = game.target_cell();
    now += 10'000;
    game.Press(game.target_cell(), now);
  }

  // Repeats must actually occur; their absence would be the bug.
  CHECK_TRUE(repeats > 0);
  CHECK_TRUE(repeats > 40 && repeats < 140);
}

TEST(Fsm, RepeatTargetsAreFlaggedNotSuppressed) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kPractice, 0xABCDEF01u, 0);
  std::size_t previous = game.target_cell();
  std::size_t flagged = 0;
  std::size_t actual = 0;

  for (int i = 0; i < 3000; ++i) {
    now += 10'000;
    game.Press(game.target_cell(), now);
    if (game.target_cell() == previous) {
      ++actual;
      CHECK_TRUE(game.target_is_repeat());
    } else {
      CHECK_FALSE(game.target_is_repeat());
    }
    if (game.target_is_repeat()) {
      ++flagged;
    }
    previous = game.target_cell();
  }
  CHECK_EQ_U(flagged, actual);
  CHECK_TRUE(actual > 0);
}

TEST(Fsm, KeysOutsideTheAlphabetAreIgnoredEntirely) {
  HealthMask mask;
  mask.MarkDead(12);
  mask.MarkDead(7);

  Game game;
  CHECK_TRUE(game.Configure(mask));
  CHECK_EQ_U(game.alphabet_size(), kCellCount - 2);

  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 5, 0);
  const std::uint32_t sc = game.correct();
  const std::uint32_t si = game.incorrect();

  // A dead cell is neither a hit nor a miss - exactly like a key that is not part of
  // the game at all.
  CHECK_TRUE(game.Press(12, t0 + 1000) == PressResult::kIgnored);
  CHECK_TRUE(game.Press(7, t0 + 2000) == PressResult::kIgnored);
  CHECK_TRUE(game.Press(kCellCount, t0 + 3000) == PressResult::kIgnored);
  CHECK_TRUE(game.Press(9999, t0 + 4000) == PressResult::kIgnored);
  CHECK_EQ_U(game.correct(), sc);
  CHECK_EQ_U(game.incorrect(), si);
}

TEST(Fsm, DeadCellsAreNeverTargeted) {
  HealthMask mask;
  mask.MarkDead(0);
  mask.MarkDead(12);
  mask.MarkDead(24);

  Game game;
  CHECK_TRUE(game.Configure(mask));
  std::uint64_t now = StartAndRun(&game, RunMode::kPractice, 0xDEADBEEFu, 0);

  for (int i = 0; i < 5000; ++i) {
    const std::size_t target = game.target_cell();
    CHECK_TRUE(target != 0 && target != 12 && target != 24);
    now += 5000;
    game.Press(target, now);
  }
}

TEST(Fsm, RefusesAnUnscorableAlphabet) {
  HealthMask mask(0);
  mask.MarkHealthy(0);
  mask.MarkHealthy(1);

  Game game;
  // N=2 gives log2(1) = 0 bits per selection, so no positive rate exists.
  CHECK_FALSE(game.Configure(mask));
  // The previous configuration must be untouched.
  CHECK_EQ_U(game.alphabet_size(), kCellCount);

  mask.MarkHealthy(2);
  CHECK_TRUE(game.Configure(mask));
  CHECK_EQ_U(game.alphabet_size(), 3u);
}

TEST(Fsm, TheScoredWindowIsExactlySixtySeconds) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 11, 0);

  game.Press(game.target_cell(), t0 + 1'000'000);
  CHECK_EQ_U(game.correct(), 1u);

  // One microsecond before the boundary still scores.
  CHECK_TRUE(game.Press(game.target_cell(), t0 + kEvalDurationUs - 1) ==
             PressResult::kHit);
  CHECK_EQ_U(game.correct(), 2u);

  // Exactly at the boundary does not.
  CHECK_TRUE(game.Press(game.target_cell(), t0 + kEvalDurationUs) ==
             PressResult::kIgnored);
  CHECK_TRUE(game.state() == GameState::kEnded);
  CHECK_EQ_U(game.correct(), 2u);

  // And elapsed time is frozen at exactly the duration, not at whenever the last
  // tick happened to land.
  CHECK_EQ_U(game.ElapsedUs(t0 + 90'000'000), kEvalDurationUs);
}

TEST(Fsm, TickEndsTheRunAtTheBoundary) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 13, 0);
  CHECK_TRUE(game.Tick(t0 + kEvalDurationUs - 1) == GameState::kRunning);
  CHECK_TRUE(game.Tick(t0 + kEvalDurationUs + 12345) == GameState::kEnded);
  // Overrun past the boundary must not lengthen t and quietly shave the bit rate.
  CHECK_EQ_U(game.ElapsedUs(t0 + kEvalDurationUs + 12345), kEvalDurationUs);
}

TEST(Fsm, PracticeIsUntimed) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kPractice, 17, 0);
  CHECK_TRUE(game.Tick(t0 + 10 * kEvalDurationUs) == GameState::kRunning);
  CHECK_EQ_U(game.RemainingUs(t0), 0u);
  CHECK_TRUE(game.Press(game.target_cell(), t0 + 10 * kEvalDurationUs) ==
             PressResult::kHit);
}

TEST(Fsm, ScoresNetNegativeRunsAsExactlyZero) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 19, 0);

  for (int i = 0; i < 10; ++i) {
    now += 100'000;
    game.Press(game.target_cell(), now);
  }
  for (int i = 0; i < 20; ++i) {
    now += 100'000;
    game.Press(AnyOtherCell(game.target_cell()), now);
  }
  CHECK_EQ_U(game.correct(), 10u);
  CHECK_EQ_U(game.incorrect(), 20u);
  CHECK_EQ_DOUBLE(game.BitRate(now), 0.0);
  CHECK_EQ_U(game.BitRateMbps(now), 0u);

  game.Abort(now);
  RunReport report;
  game.BuildReport(&report);
  CHECK_EQ_DOUBLE(report.bit_rate, 0.0);
  CHECK_EQ_U(report.b_mbps, 0u);
  CHECK_TRUE(report.reason == EndReason::kAbort);
}

TEST(Fsm, ReportCarriesEverythingTheAssignmentAsksFor) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 0xC0FFEEu, 0);
  const std::uint64_t t0 = now;

  // 200 hits and 10 misses over the full window.
  for (int i = 0; i < 200; ++i) {
    now += 250'000;
    game.Press(game.target_cell(), now);
  }
  for (int i = 0; i < 10; ++i) {
    now += 250'000;
    game.Press(AnyOtherCell(game.target_cell()), now);
  }
  game.Tick(t0 + kEvalDurationUs);
  CHECK_TRUE(game.state() == GameState::kEnded);

  RunReport report;
  game.BuildReport(&report);

  CHECK_EQ_U(report.n, kCellCount);
  CHECK_EQ_U(report.correct, 200u);
  CHECK_EQ_U(report.incorrect, 10u);
  CHECK_EQ_U(report.elapsed_us, kEvalDurationUs);
  CHECK_EQ_U(report.seed, 0xC0FFEEu);
  CHECK_TRUE(report.reason == EndReason::kComplete);
  CHECK_TRUE(report.mode == RunMode::kEval);
  CHECK_EQ_U(report.draws, 201u);

  // B recomputed here independently of the class under test.
  const double expected = gridpulse::BitsPerSelection(kCellCount) * (200 - 10) / 60.0;
  CHECK_NEAR(report.bit_rate, expected, 1e-12);
  CHECK_EQ_U(report.b_mbps, static_cast<std::uint32_t>(expected * 1000.0 + 0.5));

  // The histogram must account for exactly the targets presented.
  std::uint32_t total = 0;
  for (std::size_t i = 0; i < kCellCount; ++i) {
    total += report.target_histogram[i];
  }
  CHECK_EQ_U(total, report.draws);
}

TEST(Fsm, ReactionPercentilesAreOrdered) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 23, 0);

  // Reaction times of 100 ms, 200 ms, ... 1000 ms.
  for (int i = 1; i <= 10; ++i) {
    now += static_cast<std::uint64_t>(i) * 100'000;
    game.Press(game.target_cell(), now);
  }
  game.Abort(now);

  RunReport report;
  game.BuildReport(&report);
  CHECK_EQ_U(report.min_reaction_us, 100'000u);
  CHECK_EQ_U(report.p50_reaction_us, 500'000u);
  CHECK_EQ_U(report.p95_reaction_us, 1'000'000u);
  CHECK_TRUE(report.p50_reaction_us <= report.p95_reaction_us);
  CHECK_TRUE(report.p95_reaction_us <= report.p99_reaction_us);
}

TEST(Fsm, MissesRecordNoReactionTime) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 29, 0);
  now += 200'000;
  game.Press(game.target_cell(), now);
  now += 5'000'000;
  game.Press(AnyOtherCell(game.target_cell()), now);
  game.Abort(now);

  RunReport report;
  game.BuildReport(&report);
  // The 5-second miss must not appear as a reaction time and skew the percentiles.
  CHECK_EQ_U(report.p99_reaction_us, 200'000u);
}

TEST(Fsm, CannotStartTwice) {
  Game game;
  CHECK_TRUE(game.Start(RunMode::kEval, 1, 0));
  CHECK_FALSE(game.Start(RunMode::kEval, 2, 100));
  game.Tick(kCountdownUs);
  CHECK_FALSE(game.Start(RunMode::kEval, 3, kCountdownUs + 100));
  // But a finished run may be restarted.
  game.Abort(kCountdownUs + 200);
  CHECK_TRUE(game.Start(RunMode::kEval, 4, kCountdownUs + 300));
}

TEST(Fsm, StartResetsEveryCounter) {
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kEval, 31, 0);
  for (int i = 0; i < 10; ++i) {
    now += 100'000;
    game.Press(game.target_cell(), now);
    now += 100'000;
    game.Press(AnyOtherCell(game.target_cell()), now);
  }
  game.Abort(now);
  CHECK_TRUE(game.correct() > 0);

  StartAndRun(&game, RunMode::kEval, 37, now + 1'000'000);
  CHECK_EQ_U(game.correct(), 0u);
  CHECK_EQ_U(game.incorrect(), 0u);
  CHECK_EQ_U(game.streak(), 0u);
  CHECK_EQ_U(game.draws(), 1u);
  CHECK_EQ_U(game.seed(), 37u);
}

TEST(Fsm, AbortDuringCountdownLeavesNoRun) {
  Game game;
  game.Start(RunMode::kEval, 41, 1000);
  game.Abort(1500);
  // The clock never started, so there is nothing to report.
  CHECK_TRUE(game.state() == GameState::kIdle);
  CHECK_EQ_U(game.target_cell(), kCellCount);
  CHECK_TRUE(game.Start(RunMode::kEval, 43, 2000));
}

TEST(Fsm, EndIsIdempotent) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 47, 0);
  game.Press(game.target_cell(), t0 + 500'000);
  game.Abort(t0 + 1'000'000);
  const std::uint64_t frozen = game.ElapsedUs(t0 + 9'000'000);
  game.Abort(t0 + 5'000'000);
  CHECK_EQ_U(game.ElapsedUs(t0 + 9'000'000), frozen);
  CHECK_EQ_U(frozen, 1'000'000u);
}

TEST(Fsm, PressesAfterTheRunAreIgnored) {
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 53, 0);
  game.Press(game.target_cell(), t0 + 100'000);
  game.Abort(t0 + 200'000);
  const std::uint32_t sc = game.correct();
  CHECK_TRUE(game.Press(0, t0 + 300'000) == PressResult::kIgnored);
  CHECK_TRUE(game.Press(1, t0 + 400'000) == PressResult::kIgnored);
  CHECK_EQ_U(game.correct(), sc);
}

TEST(Fsm, ReactionSampleBufferCannotOverflow) {
  // A superhuman run must not walk off the end of the fixed sample array.
  Game game;
  std::uint64_t now = StartAndRun(&game, RunMode::kPractice, 59, 0);
  for (std::size_t i = 0; i < gridpulse::kMaxReactionSamples + 500; ++i) {
    now += 1000;
    game.Press(game.target_cell(), now);
  }
  game.Abort(now);
  RunReport report;
  game.BuildReport(&report);
  CHECK_EQ_U(report.correct, gridpulse::kMaxReactionSamples + 500);
  // Percentiles still come out of the retained window rather than crashing.
  CHECK_TRUE(report.p50_reaction_us > 0);
}

TEST(Fsm, MatchesTheJavaScriptImplementationOnAScriptedRun) {
  // A fixed script of presses, replayed here and in web/tests/core_test.js against
  // the same seed. Both must produce identical Sc, Si and B. This is the check that
  // Mode A and Mode B are the same game rather than two similar ones.
  Game game;
  const std::uint64_t t0 = StartAndRun(&game, RunMode::kEval, 0xDEADBEEFu, 0);
  std::uint64_t now = t0;

  // hit, miss, miss, hit, hit, miss, then 20 hits - 22 hits, 3 misses.
  const bool script[] = {true, false, false, true, true, false};
  for (const bool hit : script) {
    now += 200'000;
    game.Press(hit ? game.target_cell() : AnyOtherCell(game.target_cell()), now);
  }
  for (int i = 0; i < 20; ++i) {
    now += 200'000;
    game.Press(game.target_cell(), now);
  }

  CHECK_EQ_U(game.correct(), 23u);
  CHECK_EQ_U(game.incorrect(), 3u);

  game.Tick(t0 + kEvalDurationUs);
  RunReport report;
  game.BuildReport(&report);
  CHECK_EQ_U(report.correct, 23u);
  CHECK_EQ_U(report.incorrect, 3u);
  CHECK_EQ_U(report.elapsed_us, kEvalDurationUs);
  CHECK_NEAR(report.bit_rate, gridpulse::BitsPerSelection(25) * 20.0 / 60.0, 1e-12);
}
