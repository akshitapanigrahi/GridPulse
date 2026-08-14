// GRID PULSE - the two hardware mapping tables.
//
// PURPOSE
//   Single source of truth for how a logical grid cell relates to the physical
//   board. Nothing else in the firmware may hardcode a GPIO number or an LED strip
//   index. web/core/boardmap.js mirrors this file for the browser diagnostics view.
//
// THERE ARE TWO INDEPENDENT MAPPINGS AND THEY ARE NOT THE SAME
//
//   kCellToGpio    cell -> switch GPIO. Follows the physical wiring, which is
//                  scrambled relative to grid position. It cannot be derived
//                  arithmetically from anything; it is written out explicitly here
//                  and nowhere else.
//
//   kCellToPixel   cell -> WS2812B strip index. Follows the serpentine layout of
//                  the strip: row 0 left-to-right, row 1 right-to-left, and so on.
//
//   Conflating the two is the single most likely source of a subtle bug in this
//   project. They are kept as separate named tables, the round trip is unit tested
//   in both directions, and the self-test walks the grid in GRID order rather than
//   strip order so that a swap shows up immediately as a zig-zag rather than a
//   sweep.
//
// INVARIANTS (all enforced by static_assert below, so a bad edit fails to compile)
//   - both tables have exactly kCellCount entries
//   - kCellToGpio contains no duplicates
//   - kCellToGpio never uses GP23, GP24 or GP25, which are internal to the Pico
//   - kCellToGpio never collides with kLedDataGpio
//   - kCellToPixel is a permutation of 0..kCellCount-1, so every LED is reachable
//
// PIN BUDGET
//   A Pico exposes 26 usable GPIOs: GP0-GP22 (23) plus GP26-GP28 (3). This design
//   consumes 25 for switches and 1 for LED data. There is NO SPARE PIN. See
//   docs/HARDWARE.md.
//
// Cells are indexed 0..24 in row-major grid order: cell = row * 5 + col,
// row 0 = top, col 0 = left.

#ifndef GRIDPULSE_BOARD_MAP_H_
#define GRIDPULSE_BOARD_MAP_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse {

inline constexpr std::size_t kGridRows = 5;
inline constexpr std::size_t kGridCols = 5;
inline constexpr std::size_t kCellCount = kGridRows * kGridCols;

// WS2812B data line. Deliberately not a member of kCellToGpio.
inline constexpr std::uint8_t kLedDataGpio = 28;

// Lowest and highest GPIO numbers usable on a Pico header.
inline constexpr std::uint8_t kMinUsableGpio = 0;
inline constexpr std::uint8_t kMaxUsableGpio = 28;

// GP23 (SMPS mode), GP24 (VBUS sense) and GP25 (onboard LED) are wired to internal
// functions and are not brought out to the header.
inline constexpr std::uint8_t kFirstInternalGpio = 23;
inline constexpr std::uint8_t kLastInternalGpio = 25;

// Cell -> switch GPIO. WIRING ORDER. Not derivable.
//
//   row 0:   16  17  18  15  14
//   row 1:   19  10  11  12  13
//   row 2:   20  21   6   9   8
//   row 3:    2   3   4   5   7
//   row 4:   22  26  27   1   0
// clang-format off  -- the 5x5 shape IS the wiring table; flattening it makes it
// impossible to check against the physical board.
inline constexpr std::uint8_t kCellToGpio[kCellCount] = {
    16, 17, 18, 15, 14,
    19, 10, 11, 12, 13,
    20, 21,  6,  9,  8,
     2,  3,  4,  5,  7,
    22, 26, 27,  1,  0,
};
// clang-format on

// Cell -> WS2812B strip index. SERPENTINE from the top-left.
//
//   row 0:    0   1   2   3   4
//   row 1:    9   8   7   6   5
//   row 2:   10  11  12  13  14
//   row 3:   19  18  17  16  15
//   row 4:   20  21  22  23  24
// clang-format off
inline constexpr std::uint8_t kCellToPixel[kCellCount] = {
     0,  1,  2,  3,  4,
     9,  8,  7,  6,  5,
    10, 11, 12, 13, 14,
    19, 18, 17, 16, 15,
    20, 21, 22, 23, 24,
};
// clang-format on

// --- compile-time verification of every invariant above ----------------------

namespace detail {

constexpr bool GpioTableHasNoDuplicates() {
  for (std::size_t i = 0; i < kCellCount; ++i) {
    for (std::size_t j = i + 1; j < kCellCount; ++j) {
      if (kCellToGpio[i] == kCellToGpio[j]) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool GpioTableAvoidsReservedPins() {
  for (std::size_t i = 0; i < kCellCount; ++i) {
    const std::uint8_t gpio = kCellToGpio[i];
    if (gpio > kMaxUsableGpio) {
      return false;
    }
    if (gpio >= kFirstInternalGpio && gpio <= kLastInternalGpio) {
      return false;
    }
    if (gpio == kLedDataGpio) {
      return false;
    }
  }
  return true;
}

constexpr bool PixelTableIsPermutation() {
  bool seen[kCellCount] = {};
  for (std::size_t i = 0; i < kCellCount; ++i) {
    const std::uint8_t pixel = kCellToPixel[i];
    if (pixel >= kCellCount) {
      return false;
    }
    if (seen[pixel]) {
      return false;
    }
    seen[pixel] = true;
  }
  return true;
}

// The two tables must not be accidentally identical, which would mean someone has
// pasted one over the other.
constexpr bool TablesAreDistinct() {
  for (std::size_t i = 0; i < kCellCount; ++i) {
    if (kCellToGpio[i] != kCellToPixel[i]) {
      return true;
    }
  }
  return false;
}

}  // namespace detail

static_assert(sizeof(kCellToGpio) / sizeof(kCellToGpio[0]) == kCellCount,
              "kCellToGpio must have exactly one entry per grid cell");
static_assert(sizeof(kCellToPixel) / sizeof(kCellToPixel[0]) == kCellCount,
              "kCellToPixel must have exactly one entry per grid cell");
static_assert(detail::GpioTableHasNoDuplicates(), "two cells are wired to the same GPIO");
static_assert(detail::GpioTableAvoidsReservedPins(),
              "a switch is mapped to GP23-GP25 (internal), to the LED data pin, "
              "or to a pin that does not exist");
static_assert(detail::PixelTableIsPermutation(),
              "kCellToPixel is not a permutation of 0..24; some LED is unreachable "
              "or driven twice");
static_assert(detail::TablesAreDistinct(),
              "kCellToGpio and kCellToPixel are identical - one has probably been "
              "pasted over the other");

// --- accessors ---------------------------------------------------------------

// Returns the switch GPIO for a grid cell. Out-of-range yields kMaxUsableGpio + 1,
// which is not a valid pin, so a bad index cannot silently address a real GPIO.
constexpr std::uint8_t GpioForCell(std::size_t cell) {
  return (cell < kCellCount) ? kCellToGpio[cell]
                             : static_cast<std::uint8_t>(kMaxUsableGpio + 1);
}

// Returns the LED strip index for a grid cell, or kCellCount for a bad index.
constexpr std::uint8_t PixelForCell(std::size_t cell) {
  return (cell < kCellCount) ? kCellToPixel[cell] : static_cast<std::uint8_t>(kCellCount);
}

// Reverse lookup: which cell owns this GPIO? Returns kCellCount if none does.
constexpr std::size_t CellForGpio(std::uint8_t gpio) {
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if (kCellToGpio[cell] == gpio) {
      return cell;
    }
  }
  return kCellCount;
}

// Reverse lookup: which cell owns this strip index? Returns kCellCount if none does.
constexpr std::size_t CellForPixel(std::uint8_t pixel) {
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if (kCellToPixel[cell] == pixel) {
      return cell;
    }
  }
  return kCellCount;
}

constexpr std::size_t RowForCell(std::size_t cell) {
  return cell / kGridCols;
}
constexpr std::size_t ColForCell(std::size_t cell) {
  return cell % kGridCols;
}

// A bitmask of every GPIO used by a switch. The key scanner reads all 25 inputs in
// one 32-bit port read using this, rather than polling pins one at a time.
constexpr std::uint32_t SwitchGpioMask() {
  std::uint32_t mask = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    mask |= (1u << kCellToGpio[cell]);
  }
  return mask;
}

}  // namespace gridpulse

#endif  // GRIDPULSE_BOARD_MAP_H_
