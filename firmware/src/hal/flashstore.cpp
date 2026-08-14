#include "flashstore.h"

#include <cstring>

#include "hardware/flash.h"
#include "pico/flash.h"

namespace gridpulse {
namespace {

// Total on-board flash on a Pico. The store lives in the last sector.
constexpr std::uint32_t kFlashTotalBytes = 2u * 1024u * 1024u;
constexpr std::uint32_t kStoreOffset = kFlashTotalBytes - FLASH_SECTOR_SIZE;

static_assert(sizeof(HealthRecord) <= FLASH_PAGE_SIZE,
              "the record must fit in a single flash page so it is written "
              "atomically enough that a torn write fails its CRC rather than "
              "producing a valid-looking wrong mask");

const HealthRecord* StoredRecord() {
  return reinterpret_cast<const HealthRecord*>(XIP_BASE + kStoreOffset);
}

// Payload for flash_safe_execute, which takes a void*.
struct WriteRequest {
  HealthRecord record;
};

// Runs with the other core parked and interrupts disabled. Must touch nothing but
// the flash API.
void PerformWrite(void* argument) {
  auto* request = static_cast<WriteRequest*>(argument);

  flash_range_erase(kStoreOffset, FLASH_SECTOR_SIZE);

  // flash_range_program requires a whole page. The record is smaller, so it is
  // padded into a page-sized buffer rather than programming past the record and
  // leaving the remainder as whatever the erase left behind.
  std::uint8_t page[FLASH_PAGE_SIZE];
  std::memset(page, 0xFF, sizeof(page));
  std::memcpy(page, &request->record, sizeof(request->record));

  flash_range_program(kStoreOffset, page, FLASH_PAGE_SIZE);
}

}  // namespace

bool FlashHasHealthRecord() {
  return StoredRecord()->IsValid();
}

HealthMask FlashLoadHealthMask() {
  // ToMask() already degrades an invalid record to all-healthy; going through it
  // rather than branching here keeps that policy in one place.
  return StoredRecord()->ToMask();
}

bool FlashStoreHealthMask(const HealthMask& mask) {
  WriteRequest request;
  request.record = HealthRecord::Make(mask);

  // Parks the other core before stalling XIP. Without this, whichever core is
  // executing from flash halts mid-fetch and the device hangs.
  const int result = flash_safe_execute(PerformWrite, &request, 2000);
  if (result != PICO_OK) {
    return false;
  }

  // Read it back rather than trusting the write. A store that silently failed would
  // mean the next boot quietly plays with the wrong alphabet.
  return StoredRecord()->IsValid() && StoredRecord()->ToMask() == mask;
}

}  // namespace gridpulse
