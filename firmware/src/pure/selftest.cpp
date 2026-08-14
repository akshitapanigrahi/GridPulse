#include "selftest.h"

namespace gridpulse {
namespace {

constexpr std::uint32_t CellBit(std::size_t cell) {
  return (cell < kCellCount) ? (std::uint32_t{1} << cell) : 0u;
}

}  // namespace

void SelfTest::Begin(std::uint64_t now_us, std::uint32_t stuck_mask, bool force,
                     const HealthMask& previous) {
  stuck_mask_ = stuck_mask & HealthMask::kAllHealthyBits;

  // Start from everything healthy on a forced run, or from the persisted mask on a
  // quick one.
  //
  // A stuck cell is NOT struck off here. It is walked like any other cell, so its LED
  // is lit and its verdict is reported, and it is resolved as STUCK when its deadline
  // passes. Excluding it before the walk made it invisible: the firmware emitted no
  // event for it at all, so the operator saw an LED that never lit and had no way to
  // tell a jammed switch from a dead pixel - which is exactly the wrong question to
  // leave someone holding. The switch is still condemned; it is just condemned out
  // loud, and the LED gets exercised on the way past.
  //
  // Walking it is safe because the two hazards the old exclusion guarded against are
  // already handled elsewhere. A closed-at-boot switch cannot fake a pass: KeyScanner
  // seeds held_mask_ with it at Reset, so it never produces a key-down until it has
  // genuinely opened and closed again. And it cannot disturb other cells either, as
  // OnKeyDown ignores any press that is not on the lit cell.
  mask_ = force ? HealthMask() : previous;

  pending_mask_ = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if (CellNeedsTesting(cell)) {
      pending_mask_ |= CellBit(cell);
    }
  }

  retry_mask_ = 0;
  pass_ = 1;
  complete_ = false;
  current_cell_ = kCellCount;
  AdvanceToNextCell(now_us);
}

bool SelfTest::CellNeedsTesting(std::size_t cell) const {
  // Stuck cells included deliberately - see Begin(). They light, they report, and
  // they are condemned on timeout rather than in silence.
  return mask_.IsHealthy(cell);
}

void SelfTest::AdvanceToNextCell(std::uint64_t now_us) {
  const std::size_t start = (current_cell_ >= kCellCount) ? 0 : (current_cell_ + 1);

  // Grid order, deliberately. See the header: a swapped cell/pixel table shows up as
  // a zig-zag rather than a sweep.
  for (std::size_t cell = start; cell < kCellCount; ++cell) {
    if ((pending_mask_ & CellBit(cell)) != 0) {
      current_cell_ = cell;
      cell_deadline_us_ = now_us + kSelfTestKeyTimeoutUs;
      return;
    }
  }

  // Pass finished. Retry the failures once before declaring anything dead.
  if (retry_mask_ != 0 && pass_ < kSelfTestPasses) {
    ++pass_;
    pending_mask_ = retry_mask_;
    retry_mask_ = 0;
    current_cell_ = kCellCount;
    AdvanceToNextCell(now_us);
    return;
  }

  // Anything still failing after the final pass is dead.
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if ((retry_mask_ & CellBit(cell)) != 0) {
      mask_.MarkDead(cell);
    }
  }
  current_cell_ = kCellCount;
  complete_ = true;
}

SelfTestOutcome SelfTest::ResolveCurrent(SelfTestResult result, std::uint64_t now_us) {
  SelfTestOutcome outcome;
  outcome.valid = true;
  outcome.cell = current_cell_;
  outcome.result = result;
  outcome.pass = pass_;

  pending_mask_ &= ~CellBit(current_cell_);
  if (result == SelfTestResult::kOk) {
    retry_mask_ &= ~CellBit(current_cell_);
  } else if (result == SelfTestResult::kStuck) {
    // A switch that was already closed before anyone touched the board is shorted or
    // jammed, not slow. A second pass cannot change that, so it is condemned on the
    // first one rather than costing another ten seconds to reach the same answer.
    retry_mask_ &= ~CellBit(current_cell_);
    mask_.MarkDead(current_cell_);
  } else {
    if (pass_ >= kSelfTestPasses) {
      mask_.MarkDead(current_cell_);
    } else {
      retry_mask_ |= CellBit(current_cell_);
    }
  }

  AdvanceToNextCell(now_us);
  return outcome;
}

SelfTestOutcome SelfTest::OnKeyDown(std::size_t cell, std::uint64_t now_us) {
  if (complete_ || current_cell_ >= kCellCount) {
    return SelfTestOutcome{};
  }
  // A press on the wrong cell says nothing about the hardware; people mis-press.
  if (cell != current_cell_) {
    return SelfTestOutcome{};
  }
  // Reaching here on a cell flagged stuck at boot means the switch has since opened
  // and closed under a finger, because KeyScanner cannot emit a key-down for a
  // contact that never released. Whatever it looked like at power-up, it works now,
  // so clear the flag and let the pass stand.
  stuck_mask_ &= ~CellBit(cell);
  return ResolveCurrent(SelfTestResult::kOk, now_us);
}

SelfTestOutcome SelfTest::Tick(std::uint64_t now_us) {
  if (complete_ || current_cell_ >= kCellCount) {
    return SelfTestOutcome{};
  }
  if (now_us < cell_deadline_us_) {
    return SelfTestOutcome{};
  }
  // A cell that was closed at boot and has not been pressed since is reported as
  // STUCK, not NO_KEY. Both mean "no key-down arrived", but only one of them tells
  // the operator why, and they call for different repairs.
  const bool stuck = (stuck_mask_ & CellBit(current_cell_)) != 0;
  return ResolveCurrent(stuck ? SelfTestResult::kStuck : SelfTestResult::kNoKey, now_us);
}

}  // namespace gridpulse
