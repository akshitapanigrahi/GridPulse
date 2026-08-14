// Native tests: key edge detection and debouncing.
//
// Latency is the product here, so the assertions are mostly about WHEN a press is
// registered, not just whether. The debounce must reject bounce without adding a
// single microsecond to the time between the first electrical edge and the recorded
// press - that time is charged to the player's reaction and therefore to the score.

#include "../../firmware/src/pure/keyscan.h"

#include "../../firmware/include/board_map.h"
#include "../../firmware/src/pure/config.h"
#include "test_util.h"

using gridpulse::CellsFromGpioLevels;
using gridpulse::kCellCount;
using gridpulse::kCellToGpio;
using gridpulse::kDebounceLockoutUs;
using gridpulse::KeyScanner;
using gridpulse::kReleaseSettleUs;

namespace {

// All switches open: every pin pulled high.
constexpr std::uint32_t kAllReleased = 0xFFFFFFFFu;

// Builds a port reading with the given cells pressed (their pins pulled low).
std::uint32_t LevelsWithPressed(std::uint32_t cell_mask) {
  std::uint32_t levels = kAllReleased;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if ((cell_mask & (std::uint32_t{1} << cell)) != 0) {
      levels &= ~(std::uint32_t{1} << kCellToGpio[cell]);
    }
  }
  return levels;
}

constexpr std::uint32_t Bit(std::size_t cell) {
  return std::uint32_t{1} << cell;
}

}  // namespace

TEST(KeyScan, MapsEveryPinToTheRightCell) {
  // This is where the cell -> GPIO table is applied. A mistake here would map every
  // key to the wrong cell while still behaving like a working keypad.
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    const std::uint32_t levels = LevelsWithPressed(Bit(cell));
    CHECK_EQ_U(CellsFromGpioLevels(levels), Bit(cell));
  }
  CHECK_EQ_U(CellsFromGpioLevels(kAllReleased), 0u);
}

TEST(KeyScan, ReadsAllTwentyFiveSimultaneously) {
  // Direct-wired switches give true N-key rollover: there is no matrix and no
  // ghosting, so every key can be down at once.
  std::uint32_t all_cells = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    all_cells |= Bit(cell);
  }
  CHECK_EQ_U(CellsFromGpioLevels(LevelsWithPressed(all_cells)), all_cells);
}

TEST(KeyScan, IgnoresPinsThatAreNotSwitches) {
  // The LED data line and the internal pins must never appear as a key.
  std::uint32_t levels = kAllReleased;
  levels &= ~(std::uint32_t{1} << gridpulse::kLedDataGpio);
  levels &= ~(std::uint32_t{1} << 23);
  levels &= ~(std::uint32_t{1} << 24);
  levels &= ~(std::uint32_t{1} << 25);
  CHECK_EQ_U(CellsFromGpioLevels(levels), 0u);
}

TEST(KeyScan, RegistersOnTheFirstEdgeWithNoDelay) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 1000);

  // Nothing pressed yet.
  CHECK_EQ_U(scanner.Scan(kAllReleased, 1100), 0u);

  // The very first scan that sees the pin low reports the press. No confirmation
  // window, no second sample.
  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(7)), 1200), Bit(7));
  CHECK_EQ_U(scanner.press_count(), 1u);
}

TEST(KeyScan, AHeldKeyProducesExactlyOneEvent) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);

  const std::uint32_t down = LevelsWithPressed(Bit(3));
  CHECK_EQ_U(scanner.Scan(down, 1000), Bit(3));

  // Leaned on for a full second at 4 kHz: not one extra event.
  std::uint32_t extra = 0;
  for (std::uint64_t t = 1000 + gridpulse::kScanIntervalUs; t < 1'001'000;
       t += gridpulse::kScanIntervalUs) {
    extra |= scanner.Scan(down, t);
  }
  CHECK_EQ_U(extra, 0u);
  CHECK_EQ_U(scanner.press_count(), 1u);
}

TEST(KeyScan, RejectsContactBounce) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);

  const std::uint32_t down = LevelsWithPressed(Bit(11));

  // The real edge.
  CHECK_EQ_U(scanner.Scan(down, 10'000), Bit(11));

  // Now bounce violently for 4 ms, inside the lockout window.
  std::uint32_t spurious = 0;
  for (std::uint64_t t = 10'250; t < 10'000 + kDebounceLockoutUs;
       t += gridpulse::kScanIntervalUs) {
    spurious |= scanner.Scan(kAllReleased, t);
    spurious |= scanner.Scan(down, t);
  }
  CHECK_EQ_U(spurious, 0u);
  CHECK_EQ_U(scanner.press_count(), 1u);
}

TEST(KeyScan, AcceptsARealSecondPressAfterACleanRelease) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t down = LevelsWithPressed(Bit(5));

  CHECK_EQ_U(scanner.Scan(down, 0), Bit(5));

  // Release, and hold it open long enough for the release to be believed.
  const std::uint64_t release_us = kDebounceLockoutUs + 1;
  CHECK_EQ_U(scanner.Scan(kAllReleased, release_us), 0u);
  CHECK_EQ_U(scanner.Scan(kAllReleased, release_us + kReleaseSettleUs), 0u);
  CHECK_EQ_U(scanner.held_mask(), 0u);

  // Now an ordinary second press, registered on its first edge as always.
  const std::uint64_t second_us = release_us + kReleaseSettleUs + 1;
  CHECK_EQ_U(scanner.Scan(down, second_us), Bit(5));
  CHECK_EQ_U(scanner.press_count(), 2u);
}

// THE REGRESSION. GP12 (cell 8) chattered as it opened, and the scanner re-armed on
// the first open sample and reported the chatter as a fresh press. In recorded
// sessions that cell showed an 87.9% miss rate against a 1.4% median for every other
// cell: the phantom landed after the next target was lit, so it scored as a miss, and
// a miss cancels a hit outright.
TEST(KeyScan, ChatterWhileTheContactsSeparateIsNotASecondPress) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t down = LevelsWithPressed(Bit(8));

  CHECK_EQ_U(scanner.Scan(down, 0), Bit(8));

  // Held for 100 ms, the observed hold time, scanned at the firmware's real rate.
  std::uint64_t t = gridpulse::kScanIntervalUs;
  for (; t < 100'000; t += gridpulse::kScanIntervalUs) {
    CHECK_EQ_U(scanner.Scan(down, t), 0u);
  }

  // Then released dirtily: 20 ms of the contacts making and breaking as they
  // separate, far longer than make-bounce and far longer than the press lockout.
  std::uint32_t spurious = 0;
  for (; t < 120'000; t += gridpulse::kScanIntervalUs) {
    spurious |= scanner.Scan(((t / 1000) % 2 == 0) ? kAllReleased : down, t);
  }
  CHECK_EQ_U(spurious, 0u);
  CHECK_EQ_U(scanner.press_count(), 1u);

  // The key is still considered down while it is misbehaving, which is what stops it
  // firing again.
  CHECK_EQ_U(scanner.held_mask(), Bit(8));

  // Once it finally settles open, it re-arms and behaves like any other key.
  for (; t < 120'000 + kReleaseSettleUs + 1000; t += gridpulse::kScanIntervalUs) {
    CHECK_EQ_U(scanner.Scan(kAllReleased, t), 0u);
  }
  CHECK_EQ_U(scanner.held_mask(), 0u);
  CHECK_EQ_U(scanner.Scan(down, t), Bit(8));
  CHECK_EQ_U(scanner.press_count(), 2u);
}

// The settle window must never be able to disable a key permanently. A contact too
// dirty to ever go quiet would otherwise obstruct its own release forever, and a key
// that stops working is a worse failure than the phantom the window prevents: a
// phantom costs a hit, a dead key costs the cell. This is the symptom that showed up
// in calibration as "sometimes it registers, sometimes it doesn't".
TEST(KeyScan, AContactTooDirtyToSettleStillRecovers) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t down = LevelsWithPressed(Bit(8));

  CHECK_EQ_U(scanner.Scan(down, 0), Bit(8));

  // Unbroken chatter for a second: the settle window never gets its quiet stretch.
  std::uint64_t t = gridpulse::kScanIntervalUs;
  for (; t < 1'000'000; t += gridpulse::kScanIntervalUs) {
    scanner.Scan(((t / 1000) % 2 == 0) ? kAllReleased : down, t);
  }

  // It recovers rather than staying held forever...
  CHECK_TRUE(scanner.press_count() > 1u);
  // ...but the give-up ceiling rate-limits what a chattering switch can produce. One
  // second of unbroken chatter yields a handful of presses, not the ~200 that
  // re-arming on every open sample produced.
  CHECK_TRUE(scanner.press_count() <= 6u);

  // And once it finally behaves, it is an ordinary key again.
  for (; t < 1'100'000; t += gridpulse::kScanIntervalUs) {
    scanner.Scan(kAllReleased, t);
  }
  CHECK_EQ_U(scanner.held_mask(), 0u);
  const std::uint32_t before = scanner.press_count();
  CHECK_EQ_U(scanner.Scan(down, t), Bit(8));
  CHECK_EQ_U(scanner.press_count(), before + 1);
}

// Giving up requires the contacts to read OPEN at that moment. A switch jammed
// closed - which is what cell 8 looked like at boot - must stay held and stay silent
// however long it is jammed for, or the ceiling would turn a shorted switch into a
// press every 250 ms.
TEST(KeyScan, AJammedSwitchIsNeverArmedByTheGiveUpCeiling) {
  KeyScanner scanner;
  scanner.Reset(LevelsWithPressed(Bit(8)), 0);
  CHECK_EQ_U(scanner.stuck_mask(), Bit(8));

  std::uint32_t events = 0;
  for (std::uint64_t t = gridpulse::kScanIntervalUs; t < 5'000'000;
       t += gridpulse::kScanIntervalUs) {
    events |= scanner.Scan(LevelsWithPressed(Bit(8)), t);
  }
  CHECK_EQ_U(events, 0u);
  CHECK_EQ_U(scanner.press_count(), 0u);
  CHECK_EQ_U(scanner.held_mask(), Bit(8));
}

// A single stray open sample mid-hold - electrical noise rather than a real release -
// must not re-arm the key either, or the very next low sample fires a phantom.
TEST(KeyScan, OneStrayOpenSampleDoesNotRearmTheKey) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t down = LevelsWithPressed(Bit(3));

  CHECK_EQ_U(scanner.Scan(down, 0), Bit(3));
  std::uint64_t t = gridpulse::kScanIntervalUs;
  for (; t < 50'000; t += gridpulse::kScanIntervalUs) {
    CHECK_EQ_U(scanner.Scan(down, t), 0u);
  }
  CHECK_EQ_U(scanner.Scan(kAllReleased, t), 0u);              // one stray sample
  CHECK_EQ_U(scanner.Scan(down, t + gridpulse::kScanIntervalUs), 0u);  // no new press
  CHECK_EQ_U(scanner.held_mask(), Bit(3));
  CHECK_EQ_U(scanner.press_count(), 1u);
}

// The settle window must not be in the scored path. A player rolling across the board
// presses a DIFFERENT key within milliseconds, and that must never be delayed or lost
// by another key still settling.
TEST(KeyScan, ReleaseSettlingOnOneKeyDoesNotDelayAnother) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);

  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(0)), 1000), Bit(0));
  CHECK_EQ_U(scanner.Scan(kAllReleased, 2000), 0u);  // cell 0 begins releasing
  // 1 ms later, deep inside cell 0's settle window, a different key goes down and is
  // registered on its first edge with no delay at all.
  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(1)), 3000), Bit(1));
}

TEST(KeyScan, ReleaseIsNeverASelection) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t down = LevelsWithPressed(Bit(9));

  scanner.Scan(down, 0);
  // Every scan across the release must report nothing.
  for (std::uint64_t t = kDebounceLockoutUs + 1; t < kDebounceLockoutUs + 50'000;
       t += gridpulse::kScanIntervalUs) {
    CHECK_EQ_U(scanner.Scan(kAllReleased, t), 0u);
  }
  CHECK_EQ_U(scanner.press_count(), 1u);
}

TEST(KeyScan, LockoutIsPerKeyNotGlobal) {
  // A player rolling from one key to the next presses the second within a few
  // milliseconds of the first. A global lockout would swallow it and cost a hit.
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);

  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(0)), 1000), Bit(0));
  // 1 ms later - well inside cell 0's lockout - a different key goes down.
  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(0) | Bit(1)), 2000), Bit(1));
  CHECK_EQ_U(scanner.press_count(), 2u);
}

TEST(KeyScan, ReportsSimultaneousPressesTogether) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  const std::uint32_t both = Bit(4) | Bit(20);
  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(both), 1000), both);
  CHECK_EQ_U(scanner.press_count(), 2u);
}

TEST(KeyScan, DetectsSwitchesStuckClosedAtBoot) {
  // A switch shorted before anyone has touched the board is a hardware fault. It
  // must be reported, and it must NOT fire as a key-down the instant the game runs.
  KeyScanner scanner;
  const std::uint32_t stuck = Bit(6) | Bit(18);
  scanner.Reset(LevelsWithPressed(stuck), 0);

  CHECK_EQ_U(scanner.stuck_mask(), stuck);
  CHECK_EQ_U(scanner.held_mask(), stuck);

  // Still closed a second later: still no events.
  std::uint32_t events = 0;
  for (std::uint64_t t = 1000; t < 1'000'000; t += 50'000) {
    events |= scanner.Scan(LevelsWithPressed(stuck), t);
  }
  CHECK_EQ_U(events, 0u);
  CHECK_EQ_U(scanner.press_count(), 0u);
}

TEST(KeyScan, AStuckKeyThatFreesItselfWorksNormally) {
  KeyScanner scanner;
  scanner.Reset(LevelsWithPressed(Bit(2)), 0);
  CHECK_EQ_U(scanner.stuck_mask(), Bit(2));

  // It releases.
  CHECK_EQ_U(scanner.Scan(kAllReleased, 100'000), 0u);
  // And is then a perfectly ordinary key.
  CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(2)), 200'000), Bit(2));
  CHECK_EQ_U(scanner.press_count(), 1u);
}

TEST(KeyScan, HeldMaskTracksActualState) {
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  CHECK_EQ_U(scanner.held_mask(), 0u);

  scanner.Scan(LevelsWithPressed(Bit(12)), 1000);
  CHECK_EQ_U(scanner.held_mask(), Bit(12));

  scanner.Scan(LevelsWithPressed(Bit(12) | Bit(13)), 2000);
  CHECK_EQ_U(scanner.held_mask(), Bit(12) | Bit(13));

  // Both release together, and are only cleared once the release has settled.
  scanner.Scan(kAllReleased, 2000 + kDebounceLockoutUs + 1);
  CHECK_EQ_U(scanner.held_mask(), Bit(12) | Bit(13));
  scanner.Scan(kAllReleased, 2000 + kDebounceLockoutUs + 1 + kReleaseSettleUs);
  CHECK_EQ_U(scanner.held_mask(), 0u);
}

TEST(KeyScan, EveryCellCanBePressedIndependently) {
  // Sweeps the whole board through the scanner, which exercises all 25 entries of
  // the GPIO table through the debounce path rather than just the mapping function.
  KeyScanner scanner;
  scanner.Reset(kAllReleased, 0);
  std::uint64_t now = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    now += kDebounceLockoutUs * 2;
    CHECK_EQ_U(scanner.Scan(LevelsWithPressed(Bit(cell)), now), Bit(cell));
    now += kDebounceLockoutUs * 2;
    CHECK_EQ_U(scanner.Scan(kAllReleased, now), 0u);
  }
  CHECK_EQ_U(scanner.press_count(), kCellCount);
}

TEST(KeyScan, ScanIntervalMeetsTheLatencyBudget) {
  // The scan period bounds the worst-case detection latency. Assert the constant is
  // actually fast enough rather than trusting the comment on it: 250 us is a fifth
  // of a 60 Hz frame and far below human timing jitter.
  CHECK_TRUE(gridpulse::kScanIntervalUs <= 1000);
  // And the lockout must be long enough to cover real switch bounce, which for a
  // mechanical keyswitch settles within about 1-2 ms.
  CHECK_TRUE(kDebounceLockoutUs >= 2000);
  // But short enough not to cap a fast player: 5 ms allows 200 presses/second on a
  // single key, far above anything a human does.
  CHECK_TRUE(kDebounceLockoutUs <= 10000);

  // The release settle window must outlast break-bounce, which is the longer of the
  // two and reaches tens of milliseconds on a worn switch...
  CHECK_TRUE(kReleaseSettleUs > kDebounceLockoutUs);
  CHECK_TRUE(kReleaseSettleUs >= 15000);
  // ...while staying far below any real same-key press interval. The shortest
  // observed in recorded sessions was 54 ms, so 40 ms is the outer bound at which
  // this could start swallowing genuine input.
  CHECK_TRUE(kReleaseSettleUs <= 40000);

  // The give-up ceiling must sit above real break-chatter, which ran to about 200 ms
  // in recorded sessions, so an ordinary dirty release resolves through the settle
  // path rather than through the ceiling...
  CHECK_TRUE(gridpulse::kReleaseGiveUpUs > kReleaseSettleUs);
  CHECK_TRUE(gridpulse::kReleaseGiveUpUs >= 200000);
  // ...and below the point where a failing key would feel dead to the player. Half a
  // second of a key not responding is noticed; a quarter is not.
  CHECK_TRUE(gridpulse::kReleaseGiveUpUs <= 500000);
}
