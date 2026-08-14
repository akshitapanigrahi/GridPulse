// GRID PULSE - CRC-16/CCITT-FALSE and CRC-32, for the wire protocol and for the
// persisted healthy-cell record.
//
// PURPOSE
//   CRC-16/CCITT-FALSE guards every protocol line in both directions
//   (docs/PROTOCOL.md section 1). CRC-32 guards the self-test result written to the
//   last flash sector, so an erased or partially written sector is recognised as
//   absent rather than misread as a mask that would silently shrink the alphabet.
//
// PARAMETERS
//   CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
//                       Canonical check value: Crc16("123456789") == 0x29B1.
//   CRC-32 (zlib/PKZIP): poly 0xEDB88320 reflected, init 0xFFFFFFFF, reflected
//                       in and out, final XOR 0xFFFFFFFF.
//                       Canonical check value: Crc32("123456789") == 0xCBF43926.
//
// Both are the bitwise form rather than a table-driven one. The protocol lines are
// at most 256 bytes and are produced a few dozen times per second, so the table
// would buy microseconds at the cost of 512 bytes of flash and a second thing to
// get wrong.

#ifndef GRIDPULSE_CRC_H_
#define GRIDPULSE_CRC_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse {

std::uint16_t Crc16CcittFalse(const void* data, std::size_t length);

std::uint32_t Crc32(const void* data, std::size_t length);

}  // namespace gridpulse

#endif  // GRIDPULSE_CRC_H_
