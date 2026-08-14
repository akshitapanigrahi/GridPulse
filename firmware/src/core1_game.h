// GRID PULSE - core 1: the game itself.
//
// PURPOSE
//   Core 1 owns key scanning, the game state machine, the self-test and the LEDs.
//   Core 0 owns USB. They communicate only through the two lock-free queues declared
//   here.
//
// WHY THE GAME LIVES ON CORE 1
//   Core 1 must never block. A stall here delays a key press, and a delayed key press
//   lands directly on the player's measured reaction time and therefore on the score.
//   Core 0 talks to USB, which is the one part of this system with unbounded,
//   host-controlled timing. Keeping them apart means nothing the host does - not a
//   slow read, not a disconnected terminal, not a stalled browser - can affect what
//   the device measures.
//
//   Everything core 1 does is bounded: a 32-bit port read, a fixed 25-iteration
//   debounce pass, a rejection-sampled draw, and a DMA trigger. There is no
//   allocation, no blocking call, and no unbounded loop in the scan path.
//
// LATENCY BUDGET (see docs/ARCHITECTURE.md)
//   key closes -> detected            <= 250 us   (4 kHz scan)
//   detected   -> scored              <   10 us   (fixed-cost logic)
//   scored     -> new LED lit         ~  380 us   (DMA + strip clock-in)
//   scored     -> host sees the event ~ 1-5 ms    (USB, off the critical path)
//
//   Only the first three are in the player's loop. The USB leg is display only.

#ifndef GRIDPULSE_CORE1_GAME_H_
#define GRIDPULSE_CORE1_GAME_H_

#include "pure/config.h"
#include "pure/event.h"
#include "pure/protocol.h"
#include "pure/spsc_queue.h"

namespace gridpulse {

// Core 1 -> core 0. Game events awaiting serialisation onto the wire.
//
// Producer: core 1. Consumer: core 0. Exactly one of each, which is what makes the
// lock-free ring valid.
extern SpscQueue<Event, kEventQueueDepth> g_event_queue;

// Core 0 -> core 1. Commands received from the host.
//
// Shallow on purpose: commands arrive at human speed, and a backlog would mean the
// host is misbehaving rather than that the device is slow.
inline constexpr std::size_t kCommandQueueDepth = 8;
extern SpscQueue<Command, kCommandQueueDepth> g_command_queue;

// Entry point for core 1. Never returns.
void Core1Main();

}  // namespace gridpulse

#endif  // GRIDPULSE_CORE1_GAME_H_
