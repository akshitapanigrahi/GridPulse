// Native tests: the events a time-driven run advance puts on the wire.
//
// This is the coverage that was missing. The game state machine was tested and
// correct; what the device SAID about it was not tested at all, because the emission
// lived inside Core1Main's loop behind the Pico SDK. Two spec deviations survived
// there into recorded sessions:
//
//   - no TARGET was ever emitted for draw #1, so the browser mirrored an unlit grid
//     from the first target presentation until the player's first hit
//   - MODE ENDED was emitted twice on every completed run, once for the transition
//     and once by the end sequence
//
// Both are asserted against below. The reference for the expected sequence is the
// worked example in docs/PROTOCOL.md section 7.

#include "../../firmware/src/pure/runevents.h"

#include "../../firmware/src/pure/event.h"
#include "../../firmware/src/pure/fsm.h"
#include "test_util.h"

using gridpulse::AbortShouldApply;
using gridpulse::AdvanceRun;
using gridpulse::Command;
using gridpulse::CommandName;
using gridpulse::EventType;
using gridpulse::Game;
using gridpulse::GameState;
using gridpulse::HealthMask;
using gridpulse::kCellCount;
using gridpulse::kCountdownUs;
using gridpulse::kEvalDurationUs;
using gridpulse::kMaxAdvanceEvents;
using gridpulse::PressResult;
using gridpulse::RunAdvance;
using gridpulse::RunMode;

namespace {

constexpr std::uint64_t kStartUs = 5'000'000;

}  // namespace

TEST(RunEvents, CountdownProducesNothingUntilItExpires) {
  Game game;
  game.Start(RunMode::kEval, 0xDEADBEEF, kStartUs);

  const RunAdvance mid = AdvanceRun(&game, kStartUs + kCountdownUs / 2);
  CHECK_TRUE(mid.state == GameState::kCountdown);
  CHECK_EQ_U(mid.count, 0u);
  CHECK_FALSE(mid.began);
  CHECK_FALSE(mid.ended);

  // One microsecond short of the deadline is still silence.
  const RunAdvance almost = AdvanceRun(&game, kStartUs + kCountdownUs - 1);
  CHECK_EQ_U(almost.count, 0u);
  CHECK_FALSE(almost.began);
}

// THE REGRESSION. Entering RUNNING must announce the target the device just lit.
// Without the TARGET below, the host has no idea which cell is lit until a HIT
// announces the NEXT one - which is what the hardware-mode grid was showing.
TEST(RunEvents, EnteringRunningAnnouncesTheFirstTarget) {
  Game game;
  game.Start(RunMode::kEval, 0xDEADBEEF, kStartUs);

  const std::uint64_t t0 = kStartUs + kCountdownUs;
  const RunAdvance advance = AdvanceRun(&game, t0);

  CHECK_TRUE(advance.state == GameState::kRunning);
  CHECK_TRUE(advance.began);
  CHECK_FALSE(advance.ended);
  CHECK_EQ_U(advance.count, 2u);

  // MODE first, then TARGET, in the order the two things happened.
  CHECK_TRUE(advance.events[0].type == EventType::kMode);
  CHECK_TRUE(advance.events[0].state == GameState::kRunning);
  CHECK_EQ_U(advance.events[0].seed, 0xDEADBEEFu);
  CHECK_EQ_U(advance.events[0].n, kCellCount);
  CHECK_EQ_U(advance.events[0].t_us, t0);

  CHECK_TRUE(advance.events[1].type == EventType::kTarget);
  CHECK_EQ_U(advance.events[1].t_us, t0);
  // Draw ordinals run from 1, so the first target is idx=1 and not idx=2.
  CHECK_EQ_U(advance.events[1].draw_index, 1u);
  CHECK_FALSE(advance.events[1].repeat);
  // And it is the cell the device actually lit, not merely a plausible one.
  CHECK_EQ_U(advance.events[1].cell, game.target_cell());
  CHECK_TRUE(game.target_cell() < kCellCount);
}

// The first target reaching the host is what makes the run playable on screen, so
// pin it for practice too - practice is untimed, which is a different code path
// through IsExpired.
TEST(RunEvents, PracticeAlsoAnnouncesTheFirstTarget) {
  Game game;
  game.Start(RunMode::kPractice, 7, kStartUs);

  const RunAdvance advance = AdvanceRun(&game, kStartUs + kCountdownUs);
  CHECK_TRUE(advance.began);
  CHECK_EQ_U(advance.count, 2u);
  CHECK_TRUE(advance.events[0].type == EventType::kMode);
  CHECK_TRUE(advance.events[0].mode == RunMode::kPractice);
  CHECK_TRUE(advance.events[1].type == EventType::kTarget);
  CHECK_EQ_U(advance.events[1].draw_index, 1u);
  CHECK_EQ_U(advance.events[1].cell, game.target_cell());

  // An untimed run never expires on its own.
  const RunAdvance later = AdvanceRun(&game, kStartUs + kCountdownUs + kEvalDurationUs * 10);
  CHECK_TRUE(later.state == GameState::kRunning);
  CHECK_FALSE(later.ended);
  CHECK_EQ_U(later.count, 0u);
}

// The start is announced exactly once. A loop calling AdvanceRun at kilohertz must
// not re-announce the target on every pass - that would relight the cell on screen
// and reset the player's reaction-time anchor visually on every frame.
TEST(RunEvents, TheStartIsAnnouncedOnlyOnce) {
  Game game;
  game.Start(RunMode::kEval, 99, kStartUs);

  const std::uint64_t t0 = kStartUs + kCountdownUs;
  CHECK_EQ_U(AdvanceRun(&game, t0).count, 2u);

  for (std::uint64_t step = 1; step <= 100; ++step) {
    const RunAdvance again = AdvanceRun(&game, t0 + step * 250);
    CHECK_EQ_U(again.count, 0u);
    CHECK_FALSE(again.began);
  }
}

// THE SECOND REGRESSION. Expiry reports `ended` and emits nothing: the caller owns
// the end sequence, because ABORT has to produce the identical one without ever
// advancing time. When both emitted, every completed run carried two MODE ENDED
// lines - and docs/PROTOCOL.md section 3 says MODE is "every state transition",
// singular.
TEST(RunEvents, ExpiryEmitsNothingAndDelegatesTheEndSequence) {
  Game game;
  game.Start(RunMode::kEval, 1234, kStartUs);
  const std::uint64_t t0 = kStartUs + kCountdownUs;
  AdvanceRun(&game, t0);

  const RunAdvance still_running = AdvanceRun(&game, t0 + kEvalDurationUs - 1);
  CHECK_TRUE(still_running.state == GameState::kRunning);
  CHECK_FALSE(still_running.ended);

  const RunAdvance expired = AdvanceRun(&game, t0 + kEvalDurationUs);
  CHECK_TRUE(expired.state == GameState::kEnded);
  CHECK_TRUE(expired.ended);
  CHECK_FALSE(expired.began);
  CHECK_EQ_U(expired.count, 0u);

  // And it ends once. A loop that keeps ticking a finished run must not re-run the
  // end sequence and send a second END tally.
  const RunAdvance after = AdvanceRun(&game, t0 + kEvalDurationUs + 5'000'000);
  CHECK_FALSE(after.ended);
  CHECK_EQ_U(after.count, 0u);
}

// The whole opening of the worked example in docs/PROTOCOL.md section 7, as the loop
// actually produces it: MODE COUNTDOWN comes from the START command, then this.
TEST(RunEvents, MatchesTheProtocolWorkedExample) {
  Game game;
  game.Start(RunMode::kEval, 3735928559u, kStartUs);

  const std::uint64_t t0 = kStartUs + kCountdownUs;
  const RunAdvance advance = AdvanceRun(&game, t0);
  CHECK_EQ_U(advance.count, 2u);

  const std::size_t first_target = game.target_cell();

  // A hit draws the next target, which the press path announces - so the run
  // transition is responsible for idx=1 and nothing else.
  CHECK_TRUE(game.Press(first_target, t0 + 218'646) == PressResult::kHit);
  CHECK_EQ_U(game.draws(), 2u);

  // Advancing again mid-run stays silent: the press path already spoke.
  const RunAdvance quiet = AdvanceRun(&game, t0 + 300'000);
  CHECK_EQ_U(quiet.count, 0u);
  CHECK_TRUE(quiet.state == GameState::kRunning);
}

// A dead cell lowers N, and the MODE that opens the run must carry the N actually in
// play - the host locks its display and its independent B recomputation to it.
TEST(RunEvents, OpeningModeCarriesTheAlphabetActuallyInPlay) {
  HealthMask mask;
  mask.MarkDead(4);
  mask.MarkDead(17);

  Game game;
  CHECK_TRUE(game.Configure(mask));
  game.Start(RunMode::kEval, 5, kStartUs);

  const RunAdvance advance = AdvanceRun(&game, kStartUs + kCountdownUs);
  CHECK_EQ_U(advance.count, 2u);
  CHECK_EQ_U(advance.events[0].n, kCellCount - 2);
  CHECK_EQ_U(game.alphabet_size(), kCellCount - 2);

  // And a dead cell is never the target the player is asked to hit.
  CHECK_TRUE(advance.events[1].cell != 4);
  CHECK_TRUE(advance.events[1].cell != 17);
}

// A single advance can never overrun the fixed buffer, and it can never collapse both
// deadlines into one call. The bound is a property of the FSM - entering RUNNING sets
// t0 to now, so nothing is expired on the same call - not something the caller has to
// trust, and it is what makes returning a two-element array honest rather than lucky.
//
// The same property is why a stalled loop cannot shorten a run: t0 is the instant the
// first target was actually presented, so the player still gets the full scored window
// from there however late the loop arrived.
TEST(RunEvents, ALateAdvanceStillBeginsTheRunAndKeepsTheFullWindow) {
  Game game;
  game.Start(RunMode::kEval, 2024, kStartUs);

  // Arrive long after the countdown deadline, the case a stalled loop produces.
  const std::uint64_t late = kStartUs + kCountdownUs + kEvalDurationUs * 2;
  const RunAdvance advance = AdvanceRun(&game, late);
  CHECK_TRUE(advance.count <= kMaxAdvanceEvents);
  CHECK_TRUE(advance.began);
  CHECK_FALSE(advance.ended);
  CHECK_EQ_U(advance.count, 2u);
  // The first target is still announced rather than being lost to the overshoot.
  CHECK_EQ_U(advance.events[1].draw_index, 1u);
  CHECK_EQ_U(advance.events[1].cell, game.target_cell());
  CHECK_EQ_U(game.ElapsedUs(late), 0u);

  // The window is measured from that presentation, not from the missed deadline.
  const RunAdvance one_short = AdvanceRun(&game, late + kEvalDurationUs - 1);
  CHECK_FALSE(one_short.ended);

  const RunAdvance expired = AdvanceRun(&game, late + kEvalDurationUs);
  CHECK_TRUE(expired.ended);
  CHECK_EQ_U(expired.count, 0u);
}

// --- ABORT applicability ---------------------------------------------------------
//
// An abort synthesised from the host closing the port must stop a self-test walk and
// must NOT stop a run. Guarding the start of a walk was never enough on its own: the
// operator can close the terminal in the middle of one, and the walk then condemns
// every remaining cell on a five-second timeout and writes the mask to flash.

TEST(RunEvents, AWireAbortAlwaysApplies) {
  Command command;
  command.name = CommandName::kAbort;
  // host_detached defaults false, and ParseCommand never sets it.
  CHECK_FALSE(command.host_detached);
  CHECK_TRUE(AbortShouldApply(command, /*self_testing=*/true));
  CHECK_TRUE(AbortShouldApply(command, /*self_testing=*/false));
}

TEST(RunEvents, TheHostGoingAwayStopsAWalk) {
  Command command;
  command.name = CommandName::kAbort;
  command.host_detached = true;
  CHECK_TRUE(AbortShouldApply(command, /*self_testing=*/true));
}

TEST(RunEvents, TheHostGoingAwayDoesNotStopARun) {
  // The run is scored on the device from its own timer, so it stays correct with
  // nothing listening. Ending it on a control-line change would let a USB quirk or a
  // browser reload destroy a real result.
  Command command;
  command.name = CommandName::kAbort;
  command.host_detached = true;
  CHECK_FALSE(AbortShouldApply(command, /*self_testing=*/false));
}
