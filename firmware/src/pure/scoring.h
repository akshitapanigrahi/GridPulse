// GRID PULSE - the achieved bit-rate metric.
//
// PURPOSE
//   Implements docs/GAME_CORE.md section 3:
//
//       B = log2(N - 1) * max(Sc - Si, 0) / t        [bits per second]
//
//   `N - 1` because one selection must be reserved for error correction. `Sc - Si`
//   because a miss cancels a hit rather than merely failing to score.
//
// INVARIANTS
//   - t is ALL elapsed session time, never a rolling window.
//   - A session with Si >= Sc scores exactly 0, never a negative number.
//   - Every degenerate input returns 0 rather than a NaN, an infinity, or a trap:
//     these functions are called from the tick path many times per second,
//     including before the first target has been presented.
//
// The device computes the authoritative final tally with these functions and sends
// it in the END message. The host recomputes independently and flags any mismatch,
// so a bug here cannot pass unnoticed.

#ifndef GRIDPULSE_SCORING_H_
#define GRIDPULSE_SCORING_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse {

// log2(N - 1): information credited to one correct selection.
// Returns 0.0 for N < kMinAlphabetSize rather than a negative or infinite value.
double BitsPerSelection(std::size_t n);

// The official bit rate in bits per second. Never negative, never NaN.
double BitRate(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
               double elapsed_s);

// Convenience overload taking the elapsed time as device microseconds, which is the
// form every caller in the firmware actually holds.
double BitRateUs(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                 std::uint64_t elapsed_us);

// The bit rate as integer milli-bits per second.
//
// This is the wire and reconciliation form. Keeping floats off the wire means the
// RP2040's formatting and the browser's can never make two agreeing implementations
// look like they disagree.
std::uint32_t BitRateMbps(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                          double elapsed_s);

std::uint32_t BitRateMbpsUs(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                            std::uint64_t elapsed_us);

// Nearest-rank percentile over a sample array. Sorts a copy in place is NOT done -
// `samples` must already be sorted ascending. Returns 0 for an empty array.
//
// The caller sorts once at end of run rather than per query, because this is only
// ever needed three times, at the end, and sorting inside would make the function
// quietly O(n log n) per call on a microcontroller.
std::uint32_t PercentileSorted(const std::uint32_t* sorted_samples, std::size_t count,
                               unsigned percentile);

}  // namespace gridpulse

#endif  // GRIDPULSE_SCORING_H_
