// Native tests: the two hardware mapping tables.
//
// board_map.h enforces most of this at compile time with static_asserts, so if these
// tables were broken this file would not build. These tests exist for the parts a
// static_assert cannot express - the round trips, the reverse lookups, the exact
// wiring the physical board was built to - and so that a reviewer can see the
// invariants stated as executable claims rather than only as comments.
//
// The single most likely bug in this project is confusing cell -> GPIO with
// cell -> pixel. Every test below is aimed at that.

#include "../../firmware/include/board_map.h"
#include "test_util.h"

using namespace gridpulse;

TEST(BoardMap, GeometryIsFiveByFive) {
  CHECK_EQ_U(kGridRows, 5u);
  CHECK_EQ_U(kGridCols, 5u);
  CHECK_EQ_U(kCellCount, 25u);
}

TEST(BoardMap, GpioTableMatchesTheAsBuiltWiring) {
  // Transcribed independently from the wiring notes. If someone "tidies" the table
  // into something arithmetic, this fails.
  // clang-format off
  const std::uint8_t expected[kCellCount] = {
      16, 17, 18, 15, 14,
      19, 10, 11, 12, 13,
      20, 21,  6,  9,  8,
       2,  3,  4,  5,  7,
      22, 26, 27,  1,  0,
  };
  // clang-format on
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    CHECK_EQ_U(kCellToGpio[cell], expected[cell]);
  }
}

TEST(BoardMap, PixelTableIsSerpentine) {
  // Row 0 left to right, row 1 right to left, and so on. Derived here rather than
  // transcribed, so this checks the table against the RULE the strip is wired to.
  for (std::size_t row = 0; row < kGridRows; ++row) {
    for (std::size_t col = 0; col < kGridCols; ++col) {
      const std::size_t cell = row * kGridCols + col;
      const std::size_t expected = (row % 2 == 0)
                                       ? (row * kGridCols + col)
                                       : (row * kGridCols + (kGridCols - 1 - col));
      CHECK_EQ_U(kCellToPixel[cell], expected);
    }
  }
}

TEST(BoardMap, TheTwoTablesAreNotTheSame) {
  // The whole hazard, stated as a test: if these ever agree everywhere, one has been
  // pasted over the other.
  std::size_t agreements = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if (kCellToGpio[cell] == kCellToPixel[cell]) {
      ++agreements;
    }
  }
  CHECK_TRUE(agreements < kCellCount);
}

TEST(BoardMap, GpioAssignmentsAreUniqueAndLegal) {
  bool seen[64] = {};
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    const std::uint8_t gpio = kCellToGpio[cell];
    CHECK_TRUE(gpio <= kMaxUsableGpio);
    CHECK_FALSE(seen[gpio]);
    seen[gpio] = true;
    // GP23-GP25 are internal to the Pico and are not on the header.
    CHECK_FALSE(gpio >= kFirstInternalGpio && gpio <= kLastInternalGpio);
    // The LED data line is not a switch.
    CHECK_TRUE(gpio != kLedDataGpio);
  }
}

TEST(BoardMap, PinBudgetIsExactlyFull) {
  // A Pico exposes GP0-GP22 and GP26-GP28: 26 pins. This design uses 25 for switches
  // and one for LED data, leaving nothing. Documented in docs/HARDWARE.md; asserted
  // here so a future feature that needs a spare pin fails loudly rather than
  // quietly stealing one.
  std::size_t usable = 0;
  for (std::uint8_t gpio = kMinUsableGpio; gpio <= kMaxUsableGpio; ++gpio) {
    if (gpio >= kFirstInternalGpio && gpio <= kLastInternalGpio) {
      continue;
    }
    ++usable;
  }
  CHECK_EQ_U(usable, 26u);

  std::size_t consumed = kCellCount + 1;  // 25 switches plus the LED data line
  CHECK_EQ_U(consumed, 26u);
  CHECK_EQ_U(usable - consumed, 0u);
}

TEST(BoardMap, PixelIndicesAreAPermutation) {
  bool seen[kCellCount] = {};
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    const std::uint8_t pixel = kCellToPixel[cell];
    CHECK_TRUE(pixel < kCellCount);
    CHECK_FALSE(seen[pixel]);
    seen[pixel] = true;
  }
  for (std::size_t i = 0; i < kCellCount; ++i) {
    CHECK_TRUE(seen[i]);
  }
}

TEST(BoardMap, RoundTripsBothWays) {
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    CHECK_EQ_U(CellForGpio(GpioForCell(cell)), cell);
    CHECK_EQ_U(CellForPixel(PixelForCell(cell)), cell);
  }
  for (std::uint8_t pixel = 0; pixel < kCellCount; ++pixel) {
    CHECK_EQ_U(PixelForCell(CellForPixel(pixel)), pixel);
  }
}

TEST(BoardMap, OutOfRangeLookupsFailSafely) {
  // A bad index must not silently address a real pin or a real LED.
  CHECK_TRUE(GpioForCell(kCellCount) > kMaxUsableGpio);
  CHECK_TRUE(GpioForCell(9999) > kMaxUsableGpio);
  CHECK_EQ_U(PixelForCell(kCellCount), kCellCount);
  CHECK_EQ_U(CellForGpio(kLedDataGpio), kCellCount);
  CHECK_EQ_U(CellForGpio(23), kCellCount);
  CHECK_EQ_U(CellForPixel(kCellCount), kCellCount);
}

TEST(BoardMap, RowAndColumnDecomposition) {
  CHECK_EQ_U(RowForCell(0), 0u);
  CHECK_EQ_U(ColForCell(0), 0u);
  CHECK_EQ_U(RowForCell(4), 0u);
  CHECK_EQ_U(ColForCell(4), 4u);
  CHECK_EQ_U(RowForCell(7), 1u);
  CHECK_EQ_U(ColForCell(7), 2u);
  CHECK_EQ_U(RowForCell(24), 4u);
  CHECK_EQ_U(ColForCell(24), 4u);
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    CHECK_EQ_U(RowForCell(cell) * kGridCols + ColForCell(cell), cell);
  }
}

TEST(BoardMap, SwitchMaskCoversExactlyTheSwitchPins) {
  const std::uint32_t mask = SwitchGpioMask();

  std::size_t bits_set = 0;
  for (int bit = 0; bit < 32; ++bit) {
    if ((mask & (1u << bit)) != 0) {
      ++bits_set;
      CHECK_TRUE(CellForGpio(static_cast<std::uint8_t>(bit)) < kCellCount);
    }
  }
  CHECK_EQ_U(bits_set, kCellCount);

  // The LED data pin must not be in the input mask, or the scanner would fight the
  // PIO for it.
  CHECK_EQ_U(mask & (1u << kLedDataGpio), 0u);
  for (std::uint8_t gpio = kFirstInternalGpio; gpio <= kLastInternalGpio; ++gpio) {
    CHECK_EQ_U(mask & (1u << gpio), 0u);
  }
}

TEST(BoardMap, SelfTestOrderIsGridOrderNotStripOrder) {
  // The self-test walks cells 0..24 in grid order. If it walked strip order instead,
  // rows 1 and 3 would light right-to-left and the operator would see a zig-zag.
  // This documents the intended visual: rows 1 and 3 must have DESCENDING pixel
  // indices as the grid walk advances, which is precisely what makes a swapped table
  // obvious to the eye.
  for (std::size_t row = 0; row < kGridRows; ++row) {
    const std::size_t first = PixelForCell(row * kGridCols);
    const std::size_t last = PixelForCell(row * kGridCols + kGridCols - 1);
    if (row % 2 == 0) {
      CHECK_TRUE(last > first);
    } else {
      CHECK_TRUE(last < first);
    }
  }
}
