// GRID PULSE - the self-test / calibration state machine.
//
// PURPOSE
//   Determines which of the 25 cells have a working switch and a working LED, and
//   produces the HealthMask that fixes N for subsequent runs. A dead cell is excluded
//   from the alphabet permanently and is never targeted, because a target the player
//   cannot register would make the run unwinnable.
//
// PROCEDURE
//   1. At entry, any switch already closed is recorded as STUCK. It is still walked:
//      its LED lights like every other cell, and it is reported as STUCK when its
//      deadline passes rather than being dropped in silence. That matters because a
//      cell excluded before the walk emits no event at all, leaving the operator
//      staring at an LED that never lit with no way to tell a jammed switch from a
//      dead pixel. It is condemned either way - just audibly.
//      A press on such a cell rescues it: KeyScanner cannot emit a key-down for a
//      contact that never opened, so a key-down proves the switch works after all.
//   2. Walk the cells in GRID order, 0..24 - not strip order. Lighting them in grid
//      order makes a swapped cell/pixel table immediately obvious: the operator sees
//      a zig-zag instead of a left-to-right sweep. This is the single best defence
//      against the most likely bug in the project, and it costs nothing.
//   3. Each lit cell waits kSelfTestKeyTimeoutUs for its own key. Pressing the right
//      key passes it; the timeout marks it NO_KEY.
//   4. Every cell that failed pass 1 is retried in pass 2. Only a cell that fails
//      twice is declared dead, so one slow finger cannot permanently shrink N.
//
//   Note what step 3 verifies: the operator can only press the correct key if the
//   correct LED lit, so a single pass exercises the switch, the LED, and BOTH
//   mapping tables at once.
//
// No Pico SDK dependency: driven entirely by injected timestamps and key events, so
// the whole procedure is host-testable without hardware.

#ifndef GRIDPULSE_SELFTEST_H_
#define GRIDPULSE_SELFTEST_H_

#include <cstddef>
#include <cstdint>

#include "../../include/board_map.h"
#include "config.h"
#include "event.h"
#include "healthmask.h"

namespace gridpulse {

// The verdict for one cell on one pass.
struct SelfTestOutcome {
  bool valid = false;
  std::size_t cell = kCellCount;
  SelfTestResult result = SelfTestResult::kOk;
  std::uint8_t pass = 0;
};

class SelfTest {
 public:
  SelfTest() = default;

  // Begins a self-test.
  //
  // `stuck_mask` is a bitmask of cells whose switch was already closed when the test
  // started; those are recorded as STUCK and never waited for.
  //
  // `force` retests cells that a previous run marked dead. Without it, the test
  // starts from the persisted mask and only re-checks cells currently believed
  // healthy, which is the quick path a grader would want.
  void Begin(std::uint64_t now_us, std::uint32_t stuck_mask, bool force,
             const HealthMask& previous);

  // The cell that should currently be lit, or kCellCount when the test is finished.
  std::size_t current_cell() const { return current_cell_; }

  std::uint8_t pass() const { return pass_; }

  bool complete() const { return complete_; }

  // Applies a key-down. Returns an outcome when it resolves the current cell.
  //
  // A press on any cell OTHER than the lit one is ignored rather than treated as a
  // failure: during familiarisation people press the wrong key, and that says
  // nothing about whether the hardware works.
  SelfTestOutcome OnKeyDown(std::size_t cell, std::uint64_t now_us);

  // Advances the timeout. Returns an outcome when the current cell times out.
  SelfTestOutcome Tick(std::uint64_t now_us);

  // The resulting healthy-cell mask. Meaningful once complete() is true.
  HealthMask mask() const { return mask_; }

  // Cells that failed the current pass and will be retried on the next one.
  std::uint32_t retry_mask() const { return retry_mask_; }

 private:
  void AdvanceToNextCell(std::uint64_t now_us);
  bool CellNeedsTesting(std::size_t cell) const;
  SelfTestOutcome ResolveCurrent(SelfTestResult result, std::uint64_t now_us);

  HealthMask mask_;
  std::uint32_t stuck_mask_ = 0;
  std::uint32_t pending_mask_ = 0;  // cells still to test on this pass
  std::uint32_t retry_mask_ = 0;    // cells that failed this pass
  std::size_t current_cell_ = kCellCount;
  std::uint64_t cell_deadline_us_ = 0;
  std::uint8_t pass_ = 0;
  bool complete_ = true;
};

}  // namespace gridpulse

#endif  // GRIDPULSE_SELFTEST_H_
