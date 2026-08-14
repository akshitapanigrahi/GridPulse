// GRID PULSE - named constants for the game core.
//
// PURPOSE
//   Every tunable and every threshold in the firmware, in one place, named with its
//   units. No magic numbers appear anywhere else in firmware/src/pure/.
//
// These values are normative and must match docs/GAME_CORE.md section 6 and the
// JavaScript implementation in web/core/. The golden vectors in tests/vectors/ are
// what actually enforce the match.

#ifndef GRIDPULSE_CONFIG_H_
#define GRIDPULSE_CONFIG_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse {

// Spec version. Must equal docs/GAME_CORE.md and web/core/session.js.
inline constexpr const char* kSpecVersion = "1.0.0";
inline constexpr const char* kFirmwareVersion = "1.0.0";
inline constexpr const char* kBoardId = "gridpulse-5x5";
inline constexpr std::uint32_t kProtocolVersion = 1;

// --- alphabet -----------------------------------------------------------------

// N. Fixed at 25; only the self-test may reduce it by excluding a dead cell.
inline constexpr std::size_t kDefaultAlphabetSize = 25;

// Below this, log2(N-1) is not positive and no run can score. The game refuses to
// start rather than reporting a meaningless number.
inline constexpr std::size_t kMinAlphabetSize = 3;

// --- timing -------------------------------------------------------------------

inline constexpr std::uint64_t kEvalDurationUs = 60ull * 1000ull * 1000ull;
inline constexpr std::uint64_t kCountdownUs = 3ull * 1000ull * 1000ull;

// Heartbeat rate. The assignment requires the displayed bit rate to update at least
// once per second; ticking four times faster keeps the display live even if a tick
// is dropped on the link.
inline constexpr std::uint64_t kTickIntervalUs = 250ull * 1000ull;

// --- input --------------------------------------------------------------------

// Eager detection with lockout: a press registers on the FIRST observed falling
// edge, then that key is ignored for this long. Integrate-then-confirm debouncing is
// deliberately not used, because its integration window would be added directly to
// every measured reaction time and therefore straight onto the score.
inline constexpr std::uint32_t kDebounceLockoutUs = 5000;

// How long a key's contacts must read OPEN continuously before that key is armed to
// fire again.
//
// Make-bounce and break-bounce are different phenomena, and treating them with one
// constant was a bug. Bounce on make settles in a millisecond or two and lands under
// the eager-detection lockout above. Bounce on break lasts longer, and on a worn or
// contaminated switch the contacts can chatter for tens of milliseconds as they
// separate - which the scanner read as a genuine new falling edge and reported as a
// second press. In this game that is not a cosmetic fault: the phantom press arrives
// while the NEXT target is already lit, so it scores as a miss, and a miss cancels a
// hit outright.
//
// Waiting for the release to settle costs nothing measurable, which is what makes the
// asymmetry legitimate. The press path must stay eager because its window is added to
// every reaction time and therefore to the score; the release path has no such
// constraint, because a key cannot legitimately be pressed again until it has first
// been released. Across 1566 same-key press intervals in recorded sessions the
// shortest was 54 ms and the 1st percentile was 452 ms, so 25 ms sits an order of
// magnitude below anything a player actually does.
inline constexpr std::uint32_t kReleaseSettleUs = 25000;

// Ceiling on how long a release may be obstructed before the key is armed anyway.
//
// The settle window above waits for a quiet stretch, and a switch bad enough to
// chatter without pause would never produce one - so without this bound a failing
// contact could hold a key un-armed indefinitely and it would simply stop working.
// That is a worse failure than the phantom press the window exists to prevent: a key
// that fires twice costs a hit, a key that never fires again costs the cell.
//
// Measured from the first open sample of a release, so it bounds the obstruction
// rather than the hold - a player who rests on a key for a second is not penalised.
// Giving up still requires the contacts to read OPEN at that moment; a switch that is
// genuinely closed stays held and stays silent, which is what a jammed switch should
// do. Observed break-chatter ran to about 200 ms, so 250 ms clears real chatter while
// keeping the worst-case lockout imperceptible.
inline constexpr std::uint32_t kReleaseGiveUpUs = 250000;

// Target scan period for the key matrix. 4 kHz gives a worst-case 250 us of
// detection latency, which is a fifth of a 60 Hz frame and well below human jitter.
inline constexpr std::uint32_t kScanIntervalUs = 250;

// A switch held closed at boot is stuck or shorted, not pressed by a player.
inline constexpr std::uint64_t kStuckKeyBootWindowUs = 250ull * 1000ull;

// --- self-test ----------------------------------------------------------------

// How long the self-test waits for each key before marking it unresponsive.
inline constexpr std::uint64_t kSelfTestKeyTimeoutUs = 5ull * 1000ull * 1000ull;

// A cell that fails is retried once before being declared dead, so one slow finger
// cannot permanently shrink the alphabet.
inline constexpr std::uint8_t kSelfTestPasses = 2;

// --- LEDs ---------------------------------------------------------------------

// Global brightness cap, 0-255 applied per channel. 255 permits the selected
// pixel to use the WS2812B's full per-channel output.
//
// POWER BUDGET: a WS2812B draws roughly 60 mA at full white. Twenty-five of them
// would be about 1.5 A, far beyond the 500 mA a USB port is obliged to supply. The
// game and self-test enforce exactly one lit pixel at a time, so full output remains
// within the device's declared 200 mA budget. An all-on presentation path must never
// be added without a separate aggregate-current limiter. See docs/HARDWARE.md.
inline constexpr std::uint8_t kGlobalBrightnessCap = 255;

// The countdown is shown on the keypad itself: the centre cell pulses once per second
// for the three seconds before the first target.
//
// Flashes rather than a steady light, because a cell held on for three seconds reads
// as a target you are already late for - which is why the steady version was removed.
// Three deliberate pulses cannot be mistaken for that, and unlike a "3 2 1" printed by
// the host they are actually timed against the clock that starts the run.
inline constexpr std::uint32_t kCountdownFlashes = 3;
inline constexpr std::uint32_t kCountdownFlashOnUs = 300000;

// Repeat disambiguation: on a repeat target the LED goes dark for this long before
// relighting, so the player sees flash then relight rather than nothing at all.
inline constexpr std::uint32_t kRepeatBlinkGapUs = 60000;

// How long hit and miss feedback colours persist before the LED returns to showing
// the target.
inline constexpr std::uint32_t kHitFlashUs = 45000;
inline constexpr std::uint32_t kMissFlashUs = 110000;

// --- buffers (all fixed size; the firmware allocates nothing after init) -------

// Maximum bytes in one protocol line including the terminator. See docs/PROTOCOL.md.
//
// Derived from the worst case rather than guessed at, because a line that does not
// fit is a line that does not get sent - and the longest message is END, which
// carries the run's entire result. With every field at its maximum width:
//
//   header   "EV " + seq(10) + " " + t_us(20) + " " + "END"          =  38
//   n=25                                                             =   5
//   sc, si, b_mbps, seed, draws, repeats, max_streak                 = 120
//   min_us, p50_us, p95_us, p99_us                                   =  72
//   t_us=<20>                                                        =  26
//   reason=COMPLETE, mode=PRACTICE                                   =  30
//   " " + crc(4) + "\n" + NUL                                        =   7
//                                                                     -----
//                                                                      298
//
// 320 leaves headroom for one more field without another round of this arithmetic.
// tests/native/test_protocol.cpp asserts the worst case actually fits.
inline constexpr std::size_t kMaxLineBytes = 320;

// Inter-core event queue depth. At 8 presses/second and a 4 Hz tick, the steady
// state is under 20 messages/second, so 64 slots is roughly three seconds of
// buffering if core 0 stalls on USB.
inline constexpr std::size_t kEventQueueDepth = 64;

// Reaction times retained for percentile computation. A 60-second run at a
// superhuman 10 presses/second would produce 600.
inline constexpr std::size_t kMaxReactionSamples = 1024;

// --- flash persistence ---------------------------------------------------------

// Magic number and format version prefixing the persisted healthy-cell mask, so an
// erased or foreign flash sector is recognised as absent rather than misread.
inline constexpr std::uint32_t kHealthRecordMagic = 0x47504C53;  // "GPLS"
inline constexpr std::uint32_t kHealthRecordVersion = 1;

}  // namespace gridpulse

#endif  // GRIDPULSE_CONFIG_H_
