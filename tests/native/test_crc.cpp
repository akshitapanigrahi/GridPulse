// Native tests: CRC-16/CCITT-FALSE and CRC-32.
//
// The CRC is what stands between a corrupted USB byte and a silently mis-scored run,
// so "it produces some number" is not enough - it has to be the RIGHT number
// (checked against the canonical check values and the golden vectors) and it has to
// actually detect the errors it is there to detect.

#include "../../firmware/src/pure/crc.h"
#include "../vectors/vectors.gen.h"
#include "test_util.h"

using gridpulse::Crc16CcittFalse;
using gridpulse::Crc32;

namespace {

std::size_t Length(const char* s) {
  std::size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

}  // namespace

TEST(Crc, CanonicalCheckValues) {
  // The published check values for these two algorithms. If either of these fails,
  // the implementation is a different CRC than the protocol specifies.
  CHECK_EQ_U(Crc16CcittFalse("123456789", 9), 0x29B1u);
  CHECK_EQ_U(Crc32("123456789", 9), 0xCBF43926u);
}

TEST(Crc, EmptyInput) {
  // CCITT-FALSE has no final XOR, so an empty message hashes to the init value.
  CHECK_EQ_U(Crc16CcittFalse("", 0), 0xFFFFu);
  CHECK_EQ_U(Crc32("", 0), 0x00000000u);
}

TEST(Crc, MatchesGoldenVectors) {
  for (std::size_t i = 0; i < gridpulse_vectors::kCrcCaseCount; ++i) {
    const auto& vec = gridpulse_vectors::kCrcCases[i];
    CHECK_EQ_U(Crc16CcittFalse(vec.input, vec.input_length), vec.crc16);
  }
}

TEST(Crc, DetectsEverySingleBitFlip) {
  // The property the protocol actually depends on. Each vector is the base line with
  // exactly one bit flipped; every one must produce a different CRC than the base,
  // and the specific CRC the reference implementation computed.
  const std::size_t base_length = gridpulse_vectors::kCrcFlipBaseLength;
  CHECK_EQ_U(Crc16CcittFalse(gridpulse_vectors::kCrcFlipBase, base_length),
             gridpulse_vectors::kCrcFlipBaseCrc);

  for (std::size_t i = 0; i < gridpulse_vectors::kCrcFlipCaseCount; ++i) {
    const auto& flip = gridpulse_vectors::kCrcFlipCases[i];
    char mutated[256];
    CHECK_TRUE(base_length < sizeof(mutated));
    for (std::size_t j = 0; j < base_length; ++j) {
      mutated[j] = gridpulse_vectors::kCrcFlipBase[j];
    }
    mutated[flip.byte_index] = static_cast<char>(
        static_cast<unsigned char>(mutated[flip.byte_index]) ^ (1u << flip.bit));

    const std::uint16_t crc = Crc16CcittFalse(mutated, base_length);
    CHECK_EQ_U(crc, flip.crc16);
    CHECK_TRUE(crc != gridpulse_vectors::kCrcFlipBaseCrc);
  }
}

TEST(Crc, DetectsEveryBitFlipExhaustively) {
  // Stronger than the vector cases: flip every bit of every byte of a realistic
  // protocol line and assert the CRC changes each time. A 16-bit CRC cannot
  // guarantee this in general, but it does for single-bit errors, which is exactly
  // the failure mode a noisy USB line produces.
  const char* base = "EV 42 1234567 TARGET cell=13 idx=7 repeat=0";
  const std::size_t length = Length(base);
  const std::uint16_t base_crc = Crc16CcittFalse(base, length);

  char mutated[128];
  CHECK_TRUE(length < sizeof(mutated));
  for (std::size_t byte = 0; byte < length; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      for (std::size_t j = 0; j < length; ++j) {
        mutated[j] = base[j];
      }
      mutated[byte] =
          static_cast<char>(static_cast<unsigned char>(mutated[byte]) ^ (1u << bit));
      CHECK_TRUE(Crc16CcittFalse(mutated, length) != base_crc);
    }
  }
}

TEST(Crc, DetectsByteTransposition) {
  // Adjacent-byte swaps are a classic framing failure that a plain checksum would
  // miss entirely.
  const char* a = "EV 12 3456 HIT cell=7 sc=3 si=1";
  const char* b = "EV 21 3456 HIT cell=7 sc=3 si=1";
  CHECK_TRUE(Crc16CcittFalse(a, Length(a)) != Crc16CcittFalse(b, Length(b)));
}

TEST(Crc, DetectsTruncation) {
  const char* full = "EV 42 1234567 TARGET cell=13 idx=7 repeat=0";
  const std::size_t length = Length(full);
  const std::uint16_t full_crc = Crc16CcittFalse(full, length);
  for (std::size_t cut = 1; cut < length; ++cut) {
    CHECK_TRUE(Crc16CcittFalse(full, cut) != full_crc);
  }
}

TEST(Crc, IsOrderSensitive) {
  CHECK_TRUE(Crc16CcittFalse("AB", 2) != Crc16CcittFalse("BA", 2));
  CHECK_TRUE(Crc32("AB", 2) != Crc32("BA", 2));
}

TEST(Crc, HandlesHighBytes) {
  // The protocol restricts itself to printable ASCII, but the CRC must still be
  // correct over arbitrary bytes: it also guards the flash health record.
  const unsigned char data[] = {0x00, 0xFF, 0x80, 0x7F, 0x01};
  const std::uint16_t crc16 = Crc16CcittFalse(data, sizeof(data));
  const std::uint32_t crc32 = Crc32(data, sizeof(data));
  // Recomputing must be stable, and a single changed byte must change both.
  CHECK_EQ_U(Crc16CcittFalse(data, sizeof(data)), crc16);
  const unsigned char changed[] = {0x00, 0xFF, 0x81, 0x7F, 0x01};
  CHECK_TRUE(Crc16CcittFalse(changed, sizeof(changed)) != crc16);
  CHECK_TRUE(Crc32(changed, sizeof(changed)) != crc32);
}
