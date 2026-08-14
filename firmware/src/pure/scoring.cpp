#include "scoring.h"

#include <cmath>

#include "config.h"

namespace gridpulse {
namespace {

// Microseconds per second, as a double, for the us -> s conversions below.
constexpr double kMicrosecondsPerSecond = 1000000.0;

}  // namespace

double BitsPerSelection(std::size_t n) {
  if (n < kMinAlphabetSize) {
    return 0.0;
  }
  return std::log2(static_cast<double>(n - 1));
}

double BitRate(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
               double elapsed_s) {
  if (n < kMinAlphabetSize) {
    return 0.0;
  }
  // Also catches NaN: a NaN comparison is false, so a NaN elapsed time returns 0.
  if (!(elapsed_s > 0.0)) {
    return 0.0;
  }
  // Signed arithmetic on purpose. Doing this in the unsigned domain would wrap
  // Sc - Si to a huge positive number the moment Si exceeds Sc - which is exactly
  // the case the max(..., 0) clamp exists to handle.
  const std::int64_t net =
      static_cast<std::int64_t>(correct) - static_cast<std::int64_t>(incorrect);
  if (net <= 0) {
    return 0.0;
  }
  return BitsPerSelection(n) * static_cast<double>(net) / elapsed_s;
}

double BitRateUs(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                 std::uint64_t elapsed_us) {
  return BitRate(n, correct, incorrect,
                 static_cast<double>(elapsed_us) / kMicrosecondsPerSecond);
}

std::uint32_t BitRateMbps(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                          double elapsed_s) {
  const double bits = BitRate(n, correct, incorrect, elapsed_s);
  const double milli = std::floor(bits * 1000.0 + 0.5);
  if (milli <= 0.0) {
    return 0;
  }
  // A 60-second run would need an implausible rate to overflow, but saturating is
  // cheaper than reasoning about it and cannot produce a wrapped, wrong number.
  if (milli >= 4294967295.0) {
    return 4294967295u;
  }
  return static_cast<std::uint32_t>(milli);
}

std::uint32_t BitRateMbpsUs(std::size_t n, std::uint32_t correct, std::uint32_t incorrect,
                            std::uint64_t elapsed_us) {
  return BitRateMbps(n, correct, incorrect,
                     static_cast<double>(elapsed_us) / kMicrosecondsPerSecond);
}

std::uint32_t PercentileSorted(const std::uint32_t* sorted_samples, std::size_t count,
                               unsigned percentile) {
  if (sorted_samples == nullptr || count == 0) {
    return 0;
  }
  if (percentile == 0) {
    return sorted_samples[0];
  }
  // Nearest-rank: rank = ceil(p/100 * count), 1-based, then clamped.
  const std::size_t rank = (static_cast<std::size_t>(percentile) * count + 99) / 100;
  const std::size_t index = (rank == 0) ? 0 : rank - 1;
  return sorted_samples[(index >= count) ? (count - 1) : index];
}

}  // namespace gridpulse
