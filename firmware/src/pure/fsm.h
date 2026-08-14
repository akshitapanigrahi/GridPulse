// GRID PULSE - the device-side game state machine.
//
// PURPOSE
//   Implements docs/GAME_CORE.md sections 4 and 5 on the device. This is the
//   authority: it draws the targets, classifies every press, owns the clock, and
//   produces the END tally that gets reported. The host displays what this object
//   decides and never contributes to it.
//
//       IDLE --Start--> COUNTDOWN --3s--> RUNNING --60s--> ENDED
//
// TIME IS INJECTED, NEVER READ
//   Every method takes an explicit `now_us`. This class calls no clock and owns no
//   timer, which is what makes it deterministic under test and directly comparable
//   to web/core/session.js against the same golden vectors. On the device the caller
//   passes time_us_64(), so every timestamp originates from the RP2040 hardware
//   timer and USB scheduling jitter can never contaminate a measurement.
//
// ALLOCATION
//   None. All storage is fixed-size members sized from constants in config.h.
//
// No Pico SDK dependency, so this compiles for the host and is covered by the
// native test target.

#ifndef GRIDPULSE_FSM_H_
#define GRIDPULSE_FSM_H_

#include <cstddef>
#include <cstdint>

#include "../../include/board_map.h"
#include "config.h"
#include "healthmask.h"
#include "protocol.h"
#include "rng.h"

namespace gridpulse {

enum class GameState : std::uint8_t {
  kIdle,
  kCountdown,
  kRunning,
  kEnded,
};

const char* GameStateToken(GameState state);

enum class PressResult : std::uint8_t {
  kHit,
  kMiss,
  // Not a selection at all: a dead or out-of-range cell, or a press outside the
  // scored window. Neither Sc nor Si.
  kIgnored,
};

enum class EndReason : std::uint8_t {
  kComplete,
  kAbort,
};

const char* EndReasonToken(EndReason reason);

// The complete end-of-run tally, matching the END and HIST messages in
// docs/PROTOCOL.md.
struct RunReport {
  std::size_t n = 0;
  std::uint32_t correct = 0;
  std::uint32_t incorrect = 0;
  std::uint64_t elapsed_us = 0;
  std::uint32_t b_mbps = 0;
  double bit_rate = 0.0;
  EndReason reason = EndReason::kComplete;
  RunMode mode = RunMode::kEval;
  std::uint32_t seed = 0;
  std::uint32_t draws = 0;
  std::uint32_t repeats = 0;
  std::uint32_t max_streak = 0;
  std::uint32_t ignored_presses = 0;
  std::uint32_t premature_presses = 0;
  std::uint32_t min_reaction_us = 0;
  std::uint32_t p50_reaction_us = 0;
  std::uint32_t p95_reaction_us = 0;
  std::uint32_t p99_reaction_us = 0;
  std::uint32_t target_histogram[kCellCount] = {};
};

class Game {
 public:
  Game();

  // Sets the healthy-cell set, which fixes N for subsequent runs.
  //
  // Returns false and changes nothing if fewer than kMinAlphabetSize cells are
  // healthy: below that, log2(N-1) is not positive and no run can be scored.
  bool Configure(const HealthMask& mask);

  // Enters COUNTDOWN. The clock does NOT start here.
  //
  // Returns false if a run is already in progress. `seed` comes from the device's
  // ring oscillator; the host has no way to supply one (docs/PROTOCOL.md section 6).
  bool Start(RunMode mode, std::uint32_t seed, std::uint64_t now_us);

  // Advances time-driven transitions: countdown expiry and end of run. Cheap enough
  // to call from the scan loop at kilohertz rates.
  GameState Tick(std::uint64_t now_us);

  // Applies one key-down. `cell` is a grid cell index.
  PressResult Press(std::size_t cell, std::uint64_t now_us);

  void Abort(std::uint64_t now_us);

  // Fills `out` with the final tally. Non-const because computing the reaction-time
  // percentiles sorts the retained samples in place.
  void BuildReport(RunReport* out);

  // --- observation ---------------------------------------------------------

  GameState state() const { return state_; }
  RunMode mode() const { return mode_; }
  std::size_t alphabet_size() const { return n_; }
  std::uint32_t seed() const { return seed_; }

  // The currently lit cell, or kCellCount when nothing is lit.
  std::size_t target_cell() const { return target_cell_; }
  bool target_is_repeat() const { return target_is_repeat_; }

  std::uint32_t correct() const { return correct_; }
  std::uint32_t incorrect() const { return incorrect_; }
  std::uint32_t streak() const { return streak_; }
  std::uint32_t draws() const { return draws_; }

  // Reaction time of the most recent hit, in microseconds. Zero before the first
  // hit of a run. Exposed so the event layer can report it without needing access
  // to the presentation timestamp.
  std::uint32_t last_reaction_us() const { return last_reaction_us_; }

  // Scored elapsed time since t0, frozen once the run has ended.
  std::uint64_t ElapsedUs(std::uint64_t now_us) const;

  // Microseconds remaining in the countdown, or 0 when not counting down.
  std::uint64_t CountdownRemainingUs(std::uint64_t now_us) const;

  // Microseconds left in the scored window; 0 for an untimed practice run.
  std::uint64_t RemainingUs(std::uint64_t now_us) const;

  double BitRate(std::uint64_t now_us) const;
  std::uint32_t BitRateMbps(std::uint64_t now_us) const;

 private:
  bool IsExpired(std::uint64_t now_us) const;
  void BeginRun(std::uint64_t now_us);
  std::size_t PresentNextTarget(std::uint64_t now_us);
  void End(EndReason reason, std::uint64_t now_us);

  HealthMask health_;
  std::uint8_t alphabet_[kCellCount] = {};
  std::size_t n_ = 0;

  Xoshiro128StarStar rng_;
  std::uint32_t seed_ = 0;

  GameState state_ = GameState::kIdle;
  RunMode mode_ = RunMode::kEval;
  EndReason end_reason_ = EndReason::kComplete;

  std::uint64_t start_command_us_ = 0;  // countdown origin
  std::uint64_t t0_us_ = 0;             // first target presentation: the origin for t
  std::uint64_t present_us_ = 0;        // current target presentation
  std::uint64_t ended_at_us_ = 0;
  std::uint64_t duration_us_ = kEvalDurationUs;

  std::uint32_t correct_ = 0;
  std::uint32_t incorrect_ = 0;
  std::uint32_t streak_ = 0;
  std::uint32_t max_streak_ = 0;
  std::uint32_t draws_ = 0;
  std::uint32_t repeats_ = 0;
  std::uint32_t ignored_presses_ = 0;
  std::uint32_t premature_presses_ = 0;
  std::uint32_t last_reaction_us_ = 0;

  std::size_t target_cell_ = kCellCount;
  std::size_t previous_target_cell_ = kCellCount;
  bool target_is_repeat_ = false;

  std::uint32_t reactions_us_[kMaxReactionSamples] = {};
  std::size_t reaction_count_ = 0;
  std::uint32_t target_histogram_[kCellCount] = {};
};

}  // namespace gridpulse

#endif  // GRIDPULSE_FSM_H_
