#include "keyscan.h"

namespace gridpulse {

std::uint32_t CellsFromGpioLevels(std::uint32_t gpio_levels) {
  std::uint32_t pressed = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    // Active low: the pin is pulled up and the switch shorts it to ground, so a
    // clear bit means this key is down.
    if ((gpio_levels & (std::uint32_t{1} << kCellToGpio[cell])) == 0) {
      pressed |= (std::uint32_t{1} << cell);
    }
  }
  return pressed;
}

void KeyScanner::Reset(std::uint32_t gpio_levels, std::uint64_t now_us) {
  const std::uint32_t pressed = CellsFromGpioLevels(gpio_levels);

  stuck_mask_ = pressed;
  // Treat a stuck key as already held, so it produces no key-down event now and none
  // later either - not until it has actually been released and pressed again.
  held_mask_ = pressed;
  open_seen_mask_ = 0;
  press_count_ = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    lockout_until_us_[cell] = now_us;
    last_closed_us_[cell] = now_us;
    first_open_us_[cell] = now_us;
  }
}

std::uint32_t KeyScanner::Scan(std::uint32_t gpio_levels, std::uint64_t now_us) {
  const std::uint32_t pressed = CellsFromGpioLevels(gpio_levels);
  std::uint32_t went_down = 0;

  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    const std::uint32_t bit = std::uint32_t{1} << cell;
    const bool is_pressed = (pressed & bit) != 0;

    if ((held_mask_ & bit) != 0) {
      // The key is down. The only transition available to it is a release.
      if (is_pressed) {
        // Still closed, or chattered back closed. Any quiet stretch it had
        // accumulated is void.
        last_closed_us_[cell] = now_us;
        continue;
      }

      if ((open_seen_mask_ & bit) == 0) {
        open_seen_mask_ |= bit;
        first_open_us_[cell] = now_us;
      }

      // A release is believed once the contacts have read open for the whole settle
      // window - anything shorter is the switch chattering as it breaks, and
      // re-arming on that is what manufactured a phantom second press.
      //
      // Or once the release has been obstructed past the give-up ceiling: a contact
      // too dirty to ever go quiet must not be able to disable its key forever. Both
      // paths require the switch to read open right now, so a genuinely jammed one is
      // never armed and never fires.
      if (now_us - last_closed_us_[cell] >= kReleaseSettleUs ||
          now_us - first_open_us_[cell] >= kReleaseGiveUpUs) {
        held_mask_ &= ~bit;
        open_seen_mask_ &= ~bit;
      }
      continue;
    }

    // The key is up and armed. Within the lockout window its electrical state is
    // meaningless, so leave the debounced state exactly as it is.
    if (now_us < lockout_until_us_[cell]) {
      continue;
    }

    if (is_pressed) {
      // First observed falling edge. Register it immediately - this instant is what
      // becomes the player's reaction timestamp, and nothing is allowed to delay it.
      held_mask_ |= bit;
      open_seen_mask_ &= ~bit;
      last_closed_us_[cell] = now_us;
      lockout_until_us_[cell] = now_us + kDebounceLockoutUs;
      went_down |= bit;
      ++press_count_;
    }
  }

  return went_down;
}

}  // namespace gridpulse
