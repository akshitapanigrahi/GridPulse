#include "healthmask.h"

#include "crc.h"

namespace gridpulse {

std::size_t HealthMask::Count() const {
  std::size_t count = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if ((bits_ & (std::uint32_t{1} << cell)) != 0) {
      ++count;
    }
  }
  return count;
}

std::size_t HealthMask::BuildAlphabet(std::uint8_t* out, std::size_t capacity) const {
  if (out == nullptr) {
    return 0;
  }
  const std::size_t needed = Count();
  if (needed > capacity) {
    // Refuse rather than truncate. A truncated alphabet would still look valid to
    // every downstream caller while quietly excluding real cells from play.
    return 0;
  }
  std::size_t written = 0;
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    if ((bits_ & (std::uint32_t{1} << cell)) != 0) {
      out[written++] = static_cast<std::uint8_t>(cell);
    }
  }
  return written;
}

HealthRecord HealthRecord::Make(const HealthMask& mask) {
  HealthRecord record{};
  record.magic = kHealthRecordMagic;
  record.version = kHealthRecordVersion;
  record.mask = mask.bits();
  record.crc32 = Crc32(&record, sizeof(record) - sizeof(record.crc32));
  return record;
}

bool HealthRecord::IsValid() const {
  if (magic != kHealthRecordMagic) {
    return false;
  }
  if (version != kHealthRecordVersion) {
    return false;
  }
  return crc32 == Crc32(this, sizeof(*this) - sizeof(crc32));
}

HealthMask HealthRecord::ToMask() const {
  if (!IsValid()) {
    // A corrupt or absent record means "never tested", not "everything is broken".
    return HealthMask();
  }
  return HealthMask(mask);
}

}  // namespace gridpulse
