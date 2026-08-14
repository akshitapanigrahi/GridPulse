// GRID PULSE - healthy-cell mask and its persisted form.
//
// PURPOSE
//   The self-test discovers which of the 25 cells have a working switch and a
//   working LED. Dead cells are excluded from the alphabet permanently and are never
//   targeted, so N is the count of healthy cells and the final report states the N
//   that was actually used.
//
// WHY IT IS PERSISTED
//   Re-running a full self-test before every game would be tedious and would burn
//   familiarisation time a grader does not have. The mask is written to the last
//   flash sector with a magic number, a format version and a CRC-32, so that an
//   erased sector, a sector written by other firmware, or a write interrupted by
//   power loss all read back as "no record" rather than as a plausible-looking mask
//   that would silently shrink the alphabet.
//
// INVARIANTS
//   - Only the low kCellCount bits of the mask are meaningful; the rest are zero.
//   - BuildAlphabet() emits cells in ascending order, which is what makes a run
//     reproducible from its seed: the sampler draws an alphabet INDEX, and the index
//     to cell mapping must be deterministic.
//   - A mask with fewer than kMinAlphabetSize healthy cells is not scorable and the
//     caller must refuse to start.

#ifndef GRIDPULSE_HEALTHMASK_H_
#define GRIDPULSE_HEALTHMASK_H_

#include <cstddef>
#include <cstdint>

#include "../../include/board_map.h"
#include "config.h"

namespace gridpulse {

class HealthMask {
 public:
  // Defaults to every cell healthy, which is the correct state before any self-test
  // has run: a device that has never been tested should play, not refuse.
  constexpr HealthMask() : bits_(kAllHealthyBits) {}
  explicit constexpr HealthMask(std::uint32_t bits) : bits_(bits & kAllHealthyBits) {}

  static constexpr std::uint32_t kAllHealthyBits =
      (kCellCount >= 32) ? 0xFFFFFFFFu : ((std::uint32_t{1} << kCellCount) - 1u);

  constexpr bool IsHealthy(std::size_t cell) const {
    return cell < kCellCount && (bits_ & (std::uint32_t{1} << cell)) != 0;
  }

  void MarkDead(std::size_t cell) {
    if (cell < kCellCount) {
      bits_ &= ~(std::uint32_t{1} << cell);
    }
  }

  void MarkHealthy(std::size_t cell) {
    if (cell < kCellCount) {
      bits_ |= (std::uint32_t{1} << cell);
    }
  }

  void MarkAllHealthy() { bits_ = kAllHealthyBits; }

  // Number of healthy cells. This is N.
  std::size_t Count() const;

  // Whether a run can be scored at all. Below kMinAlphabetSize, log2(N-1) is not
  // positive and no positive bit rate exists.
  bool IsScorable() const { return Count() >= kMinAlphabetSize; }

  // Writes the healthy cells in ascending order into `out` and returns how many were
  // written. Writes nothing and returns 0 if `capacity` is too small, rather than
  // truncating into a silently wrong alphabet.
  std::size_t BuildAlphabet(std::uint8_t* out, std::size_t capacity) const;

  constexpr std::uint32_t bits() const { return bits_; }

  friend constexpr bool operator==(const HealthMask& a, const HealthMask& b) {
    return a.bits_ == b.bits_;
  }
  friend constexpr bool operator!=(const HealthMask& a, const HealthMask& b) {
    return !(a == b);
  }

 private:
  std::uint32_t bits_;
};

// The on-flash record. Trivially copyable and fixed size, written and read as raw
// bytes; no serialisation library and no allocation.
struct HealthRecord {
  std::uint32_t magic;
  std::uint32_t version;
  std::uint32_t mask;
  std::uint32_t crc32;

  // Builds a record with a correct CRC over the first three fields.
  static HealthRecord Make(const HealthMask& mask);

  // True only if the magic, the version and the CRC all check out.
  bool IsValid() const;

  // The mask this record carries. Callers must check IsValid() first; an invalid
  // record yields an all-healthy mask so a corrupt sector degrades to "untested"
  // rather than to "everything is dead".
  HealthMask ToMask() const;
};

static_assert(sizeof(HealthRecord) == 16,
              "HealthRecord is written to flash as raw bytes; its layout is part of "
              "the persisted format and must not change silently");

}  // namespace gridpulse

#endif  // GRIDPULSE_HEALTHMASK_H_
