// GRID PULSE - persistence of the self-test result.
//
// PURPOSE
//   Stores the healthy-cell mask in the last sector of on-board flash so a device
//   that has been calibrated once does not need calibrating again. Re-running a full
//   self-test before every game would burn familiarisation time a grader does not
//   have.
//
// FORMAT
//   A single HealthRecord (magic, version, mask, CRC-32) at the start of the last
//   4 kB sector. See firmware/src/pure/healthmask.h. An erased sector, a sector
//   written by other firmware, and a write interrupted by power loss all fail the
//   check and read back as "no record" rather than as a plausible mask that would
//   silently shrink N and change the score.
//
// WHY THE LAST SECTOR
//   The program image is linked from the start of flash and grows upward. Taking the
//   last sector means the store cannot collide with the image regardless of how the
//   firmware grows, and a UF2 flash of the program leaves the record intact.
//
// CONCURRENCY
//   Erasing or programming flash stalls XIP, so any core executing from flash halts
//   mid-instruction-fetch. Writes therefore go through the SDK's flash_safe_execute,
//   which parks the other core first. Core 1 calls multicore_lockout_victim_init at
//   startup so it can be parked. Writes only ever happen at the end of a self-test,
//   never during a run.

#ifndef GRIDPULSE_FLASHSTORE_H_
#define GRIDPULSE_FLASHSTORE_H_

#include "../pure/healthmask.h"

namespace gridpulse {

// Reads the persisted mask.
//
// Returns an all-healthy mask when no valid record is present, because a device that
// has never been tested should play, not refuse.
HealthMask FlashLoadHealthMask();

// Writes the mask. Returns false if the erase or program could not be performed
// safely, in which case the previous contents are unchanged and the caller should
// report it rather than assume the write happened.
//
// Must not be called while a run is in progress.
bool FlashStoreHealthMask(const HealthMask& mask);

// True if a valid record exists, i.e. the device has been calibrated at least once.
bool FlashHasHealthRecord();

}  // namespace gridpulse

#endif  // GRIDPULSE_FLASHSTORE_H_
