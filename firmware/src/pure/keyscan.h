// GRID PULSE - key edge detection and debouncing.
//
// PURPOSE
//   Turns a stream of raw GPIO port snapshots into key-down events. Pure logic: it
//   is handed a 32-bit port read and a timestamp and returns which cells just went
//   down, so the whole debounce behaviour is host-testable without hardware.
//
// EAGER DETECTION WITH LOCKOUT
//   A press registers on the FIRST observed falling edge. The key is then ignored
//   for kDebounceLockoutUs, which is longer than any mechanical bounce, after which
//   its state is resampled.
//
//   The usual alternative - integrate-then-confirm, where a key must read closed for
//   N consecutive samples before it counts - is deliberately NOT used. Its
//   integration window is added to every single press, and in this game that window
//   lands directly on the measured reaction time and therefore straight onto the
//   score. Eager detection costs nothing in latency and rejects bounce just as well,
//   because bounce happens after the first edge, not before it.
//
// ASYMMETRIC: EAGER ON MAKE, PATIENT ON BREAK
//   The release path does the opposite, and deliberately so. A held key is only
//   re-armed once its contacts have read open continuously for kReleaseSettleUs.
//   Break-bounce outlasts make-bounce, and a worn switch can chatter for tens of
//   milliseconds while separating; re-arming on the first open sample turned that
//   chatter into a phantom second press, which arrives after the next target is lit
//   and therefore scores as a miss.
//
//   The asymmetry costs nothing that matters. The settle window is not in the
//   player's critical path, because a key cannot legitimately be pressed again until
//   it has been released - so waiting for the release to be real delays only the
//   arming, never the measurement.
//
// KEY-DOWN ONLY
//   Releases are tracked so the next press can be detected, but a release is never
//   a selection and is never reported. A held key produces exactly one event, no
//   matter how long it is held: there is no auto-repeat anywhere in this firmware.
//
// ELECTRICAL CONTRACT
//   Switches are wired directly to GPIO with the other leg to ground, and internal
//   pull-ups are enabled, so a pin reads LOW when its key is pressed. There is no
//   scan matrix and no diodes: every key has its own pin, which gives true N-key
//   rollover with no ghosting and no matrix scan latency.
//
// No Pico SDK dependency.

#ifndef GRIDPULSE_KEYSCAN_H_
#define GRIDPULSE_KEYSCAN_H_

#include <cstddef>
#include <cstdint>

#include "../../include/board_map.h"
#include "config.h"

namespace gridpulse {

class KeyScanner {
 public:
  KeyScanner() = default;

  // Initialises from a first port reading.
  //
  // Any key already closed at this moment is recorded in stuck_mask(): a switch that
  // is shut before anyone has touched the board is shorted or jammed, not pressed.
  // It is NOT reported as a key-down, because it would otherwise fire the instant
  // the game started.
  void Reset(std::uint32_t gpio_levels, std::uint64_t now_us);

  // Processes one port snapshot.
  //
  // MUST be called at kScanIntervalUs. The release logic reasons about how long a key
  // has read open by comparing against the last scan at which it read closed, which
  // is only meaningful if scans actually happen at that rate: sample it sparsely and
  // a key looks quiescent during gaps it was never observed across. Core1Main drives
  // this from its 4 kHz path unconditionally, whatever else the device is doing.
  //
  // @param gpio_levels raw port read; bit `g` is the level of GPIO `g`. Active-low,
  //        so a zero bit means that key is pressed.
  // @return bitmask of CELLS (not GPIOs) that transitioned to pressed on this scan.
  std::uint32_t Scan(std::uint32_t gpio_levels, std::uint64_t now_us);

  // Cells whose switch was closed at Reset time.
  std::uint32_t stuck_mask() const { return stuck_mask_; }

  // Cells currently held down, after debouncing. Used by the self-test to notice a
  // key that never releases.
  std::uint32_t held_mask() const { return held_mask_; }

  // Total key-down events since Reset, for diagnostics.
  std::uint32_t press_count() const { return press_count_; }

 private:
  std::uint64_t lockout_until_us_[kCellCount] = {};
  // Last scan at which each held key read closed. A release needs kReleaseSettleUs of
  // clear air after this.
  std::uint64_t last_closed_us_[kCellCount] = {};
  // First open sample of the current release attempt, which bounds how long a
  // chattering contact may obstruct its own release. Not reset by chatter.
  std::uint64_t first_open_us_[kCellCount] = {};
  std::uint32_t held_mask_ = 0;
  // Held cells that have read open at least once since going down, so first_open_us_
  // is meaningful for them.
  std::uint32_t open_seen_mask_ = 0;
  std::uint32_t stuck_mask_ = 0;
  std::uint32_t press_count_ = 0;
};

// Extracts the pressed-cell bitmask from a raw port read.
//
// Separated out and exposed so it can be tested directly: this is where the
// cell -> GPIO table is applied, and a mistake here would map every key to the wrong
// cell while still looking like a working keypad.
std::uint32_t CellsFromGpioLevels(std::uint32_t gpio_levels);

}  // namespace gridpulse

#endif  // GRIDPULSE_KEYSCAN_H_
