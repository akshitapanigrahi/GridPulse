// Native tests: the self-test / calibration procedure.
//
// What is at stake: a cell wrongly marked dead is removed from the alphabet
// permanently and shrinks N, which changes the score. A cell wrongly left alive
// becomes a target the player cannot register, which makes the run unwinnable. Both
// directions are tested.

#include "../../firmware/src/pure/selftest.h"

#include "../../firmware/src/pure/config.h"
#include "test_util.h"

using gridpulse::HealthMask;
using gridpulse::kCellCount;
using gridpulse::kSelfTestKeyTimeoutUs;
using gridpulse::SelfTest;
using gridpulse::SelfTestOutcome;
using gridpulse::SelfTestResult;

namespace {

constexpr std::uint32_t Bit(std::size_t cell) {
  return std::uint32_t{1} << cell;
}

// Runs a self-test to completion, pressing the lit key every time except for the
// cells named in `never_press`.
HealthMask RunWithFailures(std::uint32_t never_press, std::uint32_t stuck_mask = 0,
                           bool force = true) {
  SelfTest test;
  std::uint64_t now = 1000;
  test.Begin(now, stuck_mask, force, HealthMask());

  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    if (cell >= kCellCount) {
      break;
    }
    if ((never_press & Bit(cell)) != 0) {
      now += kSelfTestKeyTimeoutUs;
      test.Tick(now);
    } else {
      now += 250'000;
      test.OnKeyDown(cell, now);
    }
  }
  return test.mask();
}

}  // namespace

TEST(SelfTest, WalksEveryCellInGridOrder) {
  // Grid order is what makes a swapped cell/pixel table visible to the eye: the
  // operator sees a left-to-right sweep, not a zig-zag.
  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, 0, true, HealthMask());

  for (std::size_t expected = 0; expected < kCellCount; ++expected) {
    CHECK_EQ_U(test.current_cell(), expected);
    now += 1000;
    const SelfTestOutcome outcome = test.OnKeyDown(expected, now);
    CHECK_TRUE(outcome.valid);
    CHECK_EQ_U(outcome.cell, expected);
    CHECK_TRUE(outcome.result == SelfTestResult::kOk);
    CHECK_EQ_U(outcome.pass, 1u);
  }
  CHECK_TRUE(test.complete());
  CHECK_EQ_U(test.mask().Count(), kCellCount);
}

TEST(SelfTest, APerfectBoardLosesNoCells) {
  const HealthMask mask = RunWithFailures(0);
  CHECK_EQ_U(mask.Count(), kCellCount);
  CHECK_TRUE(mask.IsScorable());
}

TEST(SelfTest, PressingTheWrongKeyIsIgnored) {
  // During familiarisation people mis-press. That says nothing about the hardware
  // and must not resolve the cell under test either way.
  SelfTest test;
  test.Begin(0, 0, true, HealthMask());
  const std::size_t lit = test.current_cell();

  const SelfTestOutcome outcome = test.OnKeyDown((lit + 5) % kCellCount, 1000);
  CHECK_FALSE(outcome.valid);
  CHECK_EQ_U(test.current_cell(), lit);

  const SelfTestOutcome correct = test.OnKeyDown(lit, 2000);
  CHECK_TRUE(correct.valid);
  CHECK_TRUE(correct.result == SelfTestResult::kOk);
}

TEST(SelfTest, ASlowFingerGetsASecondChance) {
  // A cell that times out on pass 1 is retried before anything is declared dead.
  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, 0, true, HealthMask());

  // Miss cell 0 on the first pass, press everything else.
  CHECK_EQ_U(test.current_cell(), 0u);
  now += kSelfTestKeyTimeoutUs;
  SelfTestOutcome outcome = test.Tick(now);
  CHECK_TRUE(outcome.valid);
  CHECK_TRUE(outcome.result == SelfTestResult::kNoKey);
  CHECK_EQ_U(outcome.pass, 1u);
  // Not dead yet - queued for retry.
  CHECK_TRUE(test.mask().IsHealthy(0));
  CHECK_EQ_U(test.retry_mask(), Bit(0));

  while (!test.complete() && test.current_cell() < kCellCount && test.pass() == 1) {
    now += 1000;
    test.OnKeyDown(test.current_cell(), now);
  }

  // Pass 2 revisits cell 0 alone.
  CHECK_EQ_U(test.pass(), 2u);
  CHECK_EQ_U(test.current_cell(), 0u);

  now += 1000;
  outcome = test.OnKeyDown(0, now);
  CHECK_TRUE(outcome.valid);
  CHECK_TRUE(outcome.result == SelfTestResult::kOk);
  CHECK_EQ_U(outcome.pass, 2u);
  CHECK_TRUE(test.complete());
  CHECK_EQ_U(test.mask().Count(), kCellCount);
}

TEST(SelfTest, ACellThatFailsTwiceIsExcluded) {
  const HealthMask mask = RunWithFailures(Bit(13));
  CHECK_FALSE(mask.IsHealthy(13));
  CHECK_EQ_U(mask.Count(), kCellCount - 1);
  // Everything else survives.
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if (cell != 13) {
      CHECK_TRUE(mask.IsHealthy(cell));
    }
  }
}

TEST(SelfTest, SeveralDeadCellsAreAllExcluded) {
  const std::uint32_t dead = Bit(0) | Bit(12) | Bit(24);
  const HealthMask mask = RunWithFailures(dead);
  CHECK_FALSE(mask.IsHealthy(0));
  CHECK_FALSE(mask.IsHealthy(12));
  CHECK_FALSE(mask.IsHealthy(24));
  CHECK_EQ_U(mask.Count(), kCellCount - 3);
  CHECK_TRUE(mask.IsScorable());
}

TEST(SelfTest, StuckSwitchesAreStillLitAndReportedNotSkippedSilently) {
  // A switch closed at boot is shorted, and must not reach the alphabet. But it is
  // walked anyway so its LED lights and a verdict is emitted for it: a cell dropped
  // before the walk produces no event at all, which leaves the operator unable to
  // tell a jammed switch from a dead pixel.
  SelfTest test;
  const std::uint32_t stuck = Bit(4) | Bit(17);
  std::uint64_t now = 0;
  test.Begin(now, stuck, true, HealthMask());

  // Not condemned up front - that is now the walk's job to report.
  CHECK_TRUE(test.mask().IsHealthy(4));

  bool lit4 = false;
  bool lit17 = false;
  SelfTestResult result4 = SelfTestResult::kOk;
  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    if (cell >= kCellCount) {
      break;
    }
    if (cell == 4 || cell == 17) {
      // The stuck cell IS reached, so its LED is driven exactly like any other.
      lit4 = lit4 || (cell == 4);
      lit17 = lit17 || (cell == 17);
      now += kSelfTestKeyTimeoutUs;
      const SelfTestOutcome outcome = test.Tick(now);
      CHECK_TRUE(outcome.valid);
      if (cell == 4) {
        result4 = outcome.result;
      }
    } else {
      now += 1000;
      test.OnKeyDown(cell, now);
    }
  }

  CHECK_TRUE(lit4);
  CHECK_TRUE(lit17);
  // Reported as STUCK, which names the fault, rather than NO_KEY, which does not.
  CHECK_TRUE(result4 == SelfTestResult::kStuck);
  // Condemned on the first pass: a second five-second wait cannot change a short.
  CHECK_EQ_U(static_cast<std::uint32_t>(test.pass()), 1u);
  CHECK_FALSE(test.mask().IsHealthy(4));
  CHECK_FALSE(test.mask().IsHealthy(17));
  CHECK_EQ_U(test.mask().Count(), kCellCount - 2);
}

TEST(SelfTest, AStuckCellThatCanActuallyBePressedIsRescued) {
  // KeyScanner seeds held_mask_ with everything closed at boot, so it cannot emit a
  // key-down for a contact that never opened. A key-down on a supposedly stuck cell
  // therefore proves the switch released and closed again under a finger - it works,
  // whatever it looked like at power-up, and must not be condemned for a transient.
  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, Bit(0), true, HealthMask());

  CHECK_EQ_U(test.current_cell(), 0u);
  now += 1000;
  const SelfTestOutcome outcome = test.OnKeyDown(0, now);
  CHECK_TRUE(outcome.valid);
  CHECK_TRUE(outcome.result == SelfTestResult::kOk);

  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    if (cell >= kCellCount) {
      break;
    }
    now += 1000;
    test.OnKeyDown(cell, now);
  }
  CHECK_TRUE(test.mask().IsHealthy(0));
  CHECK_EQ_U(test.mask().Count(), kCellCount);
}

TEST(SelfTest, TimeoutOnlyFiresAtTheDeadline) {
  SelfTest test;
  test.Begin(1000, 0, true, HealthMask());

  // Well before the deadline, nothing happens.
  CHECK_FALSE(test.Tick(1000 + kSelfTestKeyTimeoutUs / 2).valid);
  CHECK_EQ_U(test.current_cell(), 0u);

  // One microsecond short.
  CHECK_FALSE(test.Tick(1000 + kSelfTestKeyTimeoutUs - 1).valid);
  // And at it.
  CHECK_TRUE(test.Tick(1000 + kSelfTestKeyTimeoutUs).valid);
}

TEST(SelfTest, QuickRunOnlyRechecksHealthyCells) {
  // Without force, the test starts from the persisted mask and does not revisit
  // cells already known dead - the fast path a grader would use.
  HealthMask previous;
  previous.MarkDead(8);

  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, 0, false, previous);

  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    CHECK_TRUE(cell != 8);
    now += 1000;
    test.OnKeyDown(cell, now);
  }
  CHECK_FALSE(test.mask().IsHealthy(8));
  CHECK_EQ_U(test.mask().Count(), kCellCount - 1);
}

TEST(SelfTest, ForcedRunRevivesAPreviouslyDeadCell) {
  // A cell marked dead because of a loose wire must be recoverable once the wire is
  // fixed, without reflashing.
  HealthMask previous;
  previous.MarkDead(8);

  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, 0, true, previous);

  bool visited_eight = false;
  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    if (cell == 8) {
      visited_eight = true;
    }
    now += 1000;
    test.OnKeyDown(cell, now);
  }
  CHECK_TRUE(visited_eight);
  CHECK_TRUE(test.mask().IsHealthy(8));
  CHECK_EQ_U(test.mask().Count(), kCellCount);
}

TEST(SelfTest, ABoardWithTooFewWorkingCellsIsNotScorable) {
  // Every cell but two fails. The resulting mask must be refused by the game rather
  // than producing a meaningless bit rate.
  std::uint32_t dead = 0;
  for (std::size_t cell = 2; cell < kCellCount; ++cell) {
    dead |= Bit(cell);
  }
  const HealthMask mask = RunWithFailures(dead);
  CHECK_EQ_U(mask.Count(), 2u);
  CHECK_FALSE(mask.IsScorable());
}

TEST(SelfTest, EventsAreEmittedForEveryCellIncludingRetries) {
  // The host renders progress from these, so every resolution must produce exactly
  // one outcome - no silent passes.
  SelfTest test;
  std::uint64_t now = 0;
  test.Begin(now, 0, true, HealthMask());

  std::size_t outcomes = 0;
  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    const std::size_t cell = test.current_cell();
    SelfTestOutcome outcome;
    if (cell == 3) {
      now += kSelfTestKeyTimeoutUs;
      outcome = test.Tick(now);
    } else {
      now += 1000;
      outcome = test.OnKeyDown(cell, now);
    }
    if (outcome.valid) {
      ++outcomes;
    }
  }
  // 25 on the first pass, plus one retry of cell 3.
  CHECK_EQ_U(outcomes, kCellCount + 1);
}

TEST(SelfTest, IsInertOnceComplete) {
  SelfTest test;
  test.Begin(0, 0, true, HealthMask());
  std::uint64_t now = 0;
  int guard = 0;
  while (!test.complete() && guard++ < 200) {
    now += 1000;
    test.OnKeyDown(test.current_cell(), now);
  }
  CHECK_TRUE(test.complete());
  CHECK_EQ_U(test.current_cell(), kCellCount);
  CHECK_FALSE(test.OnKeyDown(0, now + 1000).valid);
  CHECK_FALSE(test.Tick(now + 10'000'000).valid);
}
