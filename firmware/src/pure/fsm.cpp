#include "fsm.h"

#include "scoring.h"

namespace gridpulse {
namespace {

// Insertion sort over the retained reaction times.
//
// Used instead of std::sort because it is eight lines with obvious bounds and no
// template instantiation, and because it runs exactly once, at end of run, over at
// most kMaxReactionSamples entries. A 60-second run at a superhuman ten presses per
// second leaves 600 samples, which is well under a millisecond of work on an RP2040.
void SortAscending(std::uint32_t* values, std::size_t count) {
  for (std::size_t i = 1; i < count; ++i) {
    const std::uint32_t key = values[i];
    std::size_t j = i;
    while (j > 0 && values[j - 1] > key) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = key;
  }
}

}  // namespace

const char* GameStateToken(GameState state) {
  switch (state) {
    case GameState::kIdle:
      return "IDLE";
    case GameState::kCountdown:
      return "COUNTDOWN";
    case GameState::kRunning:
      return "RUNNING";
    case GameState::kEnded:
      return "ENDED";
  }
  return "UNKNOWN";
}

const char* EndReasonToken(EndReason reason) {
  return (reason == EndReason::kAbort) ? "ABORT" : "COMPLETE";
}

Game::Game() : rng_(0) {
  Configure(HealthMask());
}

bool Game::Configure(const HealthMask& mask) {
  if (!mask.IsScorable()) {
    return false;
  }
  const std::size_t written = mask.BuildAlphabet(alphabet_, kCellCount);
  if (written < kMinAlphabetSize) {
    return false;
  }
  health_ = mask;
  n_ = written;
  return true;
}

bool Game::Start(RunMode mode, std::uint32_t seed, std::uint64_t now_us) {
  if (state_ == GameState::kCountdown || state_ == GameState::kRunning) {
    return false;
  }
  if (n_ < kMinAlphabetSize) {
    return false;
  }

  mode_ = mode;
  seed_ = seed;
  rng_ = Xoshiro128StarStar(seed);

  // A practice run is untimed; only EVAL is a scored 60-second window.
  duration_us_ = (mode == RunMode::kEval) ? kEvalDurationUs : 0;

  state_ = GameState::kCountdown;
  start_command_us_ = now_us;
  t0_us_ = 0;
  present_us_ = 0;
  ended_at_us_ = 0;
  end_reason_ = EndReason::kComplete;

  correct_ = 0;
  incorrect_ = 0;
  streak_ = 0;
  max_streak_ = 0;
  draws_ = 0;
  repeats_ = 0;
  ignored_presses_ = 0;
  premature_presses_ = 0;
  last_reaction_us_ = 0;
  reaction_count_ = 0;

  target_cell_ = kCellCount;
  previous_target_cell_ = kCellCount;
  target_is_repeat_ = false;

  for (std::size_t i = 0; i < kCellCount; ++i) {
    target_histogram_[i] = 0;
  }

  return true;
}

void Game::BeginRun(std::uint64_t now_us) {
  // t0 is the instant the FIRST target is presented, not the instant START was
  // received, so neither the countdown nor any transport delay is charged to the
  // player. See docs/GAME_CORE.md section 4.1.
  t0_us_ = now_us;
  state_ = GameState::kRunning;
  PresentNextTarget(now_us);
}

std::size_t Game::PresentNextTarget(std::uint64_t now_us) {
  const std::uint32_t index = rng_.DrawIndex(static_cast<std::uint32_t>(n_));
  const std::size_t cell = alphabet_[index];

  previous_target_cell_ = target_cell_;
  // Sampling is with replacement, so the same cell can come up twice in a row. It is
  // NOT resampled - that would break i.i.d. sampling and leak information. It is
  // flagged so the presentation layer can blink distinctly.
  target_is_repeat_ = (cell == previous_target_cell_);
  if (target_is_repeat_) {
    ++repeats_;
  }

  target_cell_ = cell;
  present_us_ = now_us;
  ++draws_;
  if (cell < kCellCount) {
    ++target_histogram_[cell];
  }
  return cell;
}

GameState Game::Tick(std::uint64_t now_us) {
  if (state_ == GameState::kCountdown && CountdownRemainingUs(now_us) == 0) {
    BeginRun(now_us);
  }
  if (state_ == GameState::kRunning && IsExpired(now_us)) {
    End(EndReason::kComplete, now_us);
  }
  return state_;
}

bool Game::IsExpired(std::uint64_t now_us) const {
  if (duration_us_ == 0) {
    return false;  // untimed practice
  }
  return ElapsedUs(now_us) >= duration_us_;
}

void Game::End(EndReason reason, std::uint64_t now_us) {
  if (state_ == GameState::kEnded || state_ == GameState::kIdle) {
    return;
  }
  // Freeze at exactly the scored duration, so a run that ends between scan ticks
  // does not report 60.0002 s and quietly shave the final bit rate.
  if (duration_us_ > 0 && state_ == GameState::kRunning &&
      ElapsedUs(now_us) > duration_us_) {
    ended_at_us_ = t0_us_ + duration_us_;
  } else {
    ended_at_us_ = now_us;
  }
  end_reason_ = reason;
  state_ = GameState::kEnded;
  target_cell_ = kCellCount;
}

void Game::Abort(std::uint64_t now_us) {
  if (state_ == GameState::kCountdown) {
    // Aborting before the clock ever started leaves no run to report.
    state_ = GameState::kIdle;
    target_cell_ = kCellCount;
    return;
  }
  End(EndReason::kAbort, now_us);
}

PressResult Game::Press(std::size_t cell, std::uint64_t now_us) {
  if (state_ != GameState::kRunning) {
    ++premature_presses_;
    return PressResult::kIgnored;
  }
  if (IsExpired(now_us)) {
    End(EndReason::kComplete, now_us);
    ++premature_presses_;
    return PressResult::kIgnored;
  }
  // A dead cell is not in the alphabet and is never targeted, so pressing it is
  // neither correct nor incorrect - exactly like pressing a key that is not part of
  // the game at all.
  if (cell >= kCellCount || !health_.IsHealthy(cell)) {
    ++ignored_presses_;
    return PressResult::kIgnored;
  }

  if (cell == target_cell_) {
    const std::uint64_t reaction_us = (now_us > present_us_) ? (now_us - present_us_) : 0;

    last_reaction_us_ =
        (reaction_us > UINT32_MAX) ? UINT32_MAX : static_cast<std::uint32_t>(reaction_us);
    ++correct_;
    ++streak_;
    if (streak_ > max_streak_) {
      max_streak_ = streak_;
    }
    if (reaction_count_ < kMaxReactionSamples) {
      reactions_us_[reaction_count_++] = (reaction_us > UINT32_MAX)
                                             ? UINT32_MAX
                                             : static_cast<std::uint32_t>(reaction_us);
    }
    PresentNextTarget(now_us);
    return PressResult::kHit;
  }

  // The target deliberately does NOT change. The player must still hit it, which is
  // what keeps the ground truth unambiguous at every instant.
  ++incorrect_;
  streak_ = 0;
  return PressResult::kMiss;
}

std::uint64_t Game::ElapsedUs(std::uint64_t now_us) const {
  if (state_ == GameState::kIdle || state_ == GameState::kCountdown) {
    return 0;
  }
  const std::uint64_t end = (state_ == GameState::kEnded) ? ended_at_us_ : now_us;
  return (end > t0_us_) ? (end - t0_us_) : 0;
}

std::uint64_t Game::CountdownRemainingUs(std::uint64_t now_us) const {
  if (state_ != GameState::kCountdown) {
    return 0;
  }
  const std::uint64_t elapsed =
      (now_us > start_command_us_) ? (now_us - start_command_us_) : 0;
  return (elapsed >= kCountdownUs) ? 0 : (kCountdownUs - elapsed);
}

std::uint64_t Game::RemainingUs(std::uint64_t now_us) const {
  if (duration_us_ == 0) {
    return 0;
  }
  const std::uint64_t elapsed = ElapsedUs(now_us);
  return (elapsed >= duration_us_) ? 0 : (duration_us_ - elapsed);
}

double Game::BitRate(std::uint64_t now_us) const {
  return BitRateUs(n_, correct_, incorrect_, ElapsedUs(now_us));
}

std::uint32_t Game::BitRateMbps(std::uint64_t now_us) const {
  return BitRateMbpsUs(n_, correct_, incorrect_, ElapsedUs(now_us));
}

void Game::BuildReport(RunReport* out) {
  if (out == nullptr) {
    return;
  }

  const std::uint64_t elapsed = ElapsedUs(ended_at_us_);

  RunReport report;
  report.n = n_;
  report.correct = correct_;
  report.incorrect = incorrect_;
  report.elapsed_us = elapsed;
  report.bit_rate = BitRateUs(n_, correct_, incorrect_, elapsed);
  report.b_mbps = BitRateMbpsUs(n_, correct_, incorrect_, elapsed);
  report.reason = end_reason_;
  report.mode = mode_;
  report.seed = seed_;
  report.draws = draws_;
  report.repeats = repeats_;
  report.max_streak = max_streak_;
  report.ignored_presses = ignored_presses_;
  report.premature_presses = premature_presses_;

  SortAscending(reactions_us_, reaction_count_);
  report.min_reaction_us = (reaction_count_ > 0) ? reactions_us_[0] : 0;
  report.p50_reaction_us = PercentileSorted(reactions_us_, reaction_count_, 50);
  report.p95_reaction_us = PercentileSorted(reactions_us_, reaction_count_, 95);
  report.p99_reaction_us = PercentileSorted(reactions_us_, reaction_count_, 99);

  for (std::size_t i = 0; i < kCellCount; ++i) {
    report.target_histogram[i] = target_histogram_[i];
  }

  *out = report;
}

}  // namespace gridpulse
