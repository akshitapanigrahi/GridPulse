#include "crc.h"

namespace gridpulse {

std::uint16_t Crc16CcittFalse(const void* data, std::size_t length) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  // The accumulator is 32-bit and masked back to 16 each round rather than being a
  // uint16_t. Shifting a uint16_t promotes it to int, which makes every step an
  // implicit signed/unsigned conversion; working in an explicit 32-bit unsigned
  // domain keeps the arithmetic obviously correct and warning-clean.
  std::uint32_t crc = 0xFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<std::uint32_t>(bytes[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000u) != 0) {
        crc = ((crc << 1) ^ 0x1021u) & 0xFFFFu;
      } else {
        crc = (crc << 1) & 0xFFFFu;
      }
    }
  }
  return static_cast<std::uint16_t>(crc);
}

std::uint32_t Crc32(const void* data, std::size_t length) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace gridpulse
