// GRID PULSE - switch input hardware.
//
// PURPOSE
//   Owns the 25 GPIOs the switches are wired to and produces raw port snapshots for
//   the pure KeyScanner in firmware/src/pure/keyscan.h. All the debounce and edge
//   logic lives there, host-tested; this file is only the parts that touch metal.
//
// WIRING
//   Every switch has its own GPIO, with the other leg to ground. Internal pull-ups
//   are enabled, so a pin reads LOW when its key is down.
//
//   There is no scan matrix and there are no diodes. That is a deliberate trade: 25
//   pins is almost the entire budget of a Pico, but it buys true N-key rollover with
//   no ghosting, and it removes matrix scan latency from the critical path
//   altogether. In a game where the score is a latency measurement, that is the
//   right side of the trade.
//
// SCAN COST
//   All 25 inputs are read with a single 32-bit load from the SIO block. A scan is
//   therefore one instruction plus the pure-logic pass, which is what makes a 4 kHz
//   scan rate affordable alongside everything else on core 1.

#ifndef GRIDPULSE_KEYS_H_
#define GRIDPULSE_KEYS_H_

#include <cstdint>

namespace gridpulse {

// Configures every switch GPIO as an input with its pull-up enabled.
//
// Deliberately does NOT touch kLedDataGpio, which belongs to the PIO.
void KeysInit();

// Reads all 25 switch inputs at once.
//
// Returns the raw port value: bit `g` is the level of GPIO `g`, active low. Passed
// straight to KeyScanner::Scan.
std::uint32_t KeysReadRaw();

}  // namespace gridpulse

#endif  // GRIDPULSE_KEYS_H_
