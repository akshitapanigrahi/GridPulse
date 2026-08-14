// GRID PULSE - what a time-driven advance of a run puts on the wire.
//
// PURPOSE
//   Game (fsm.h) decides what happens; this decides what the host is told about it.
//   Those were the same statement inside Core1Main's loop, which meant the second
//   half was untestable: the loop needs the Pico SDK, so nothing in the native suite
//   could reach it, and the emission was the one part of the game with no coverage
//   at all. Two spec deviations lived there undetected until they showed up in
//   session logs - a missing TARGET for draw #1 (the browser mirrored an unlit grid
//   until the player's first press) and a duplicated MODE ENDED on every completed
//   run.
//
//   AdvanceRun is that second half, extracted whole. It takes the game and a clock
//   reading and returns the events, so a test can assert the exact wire sequence
//   against docs/PROTOCOL.md section 7 without a device, a queue, or a mock.
//
// WHY A RETURNED BUFFER AND NOT A SINK INTERFACE
//   The events per advance are bounded at two and Event is trivially copyable, so a
//   fixed array says so in the type. A virtual sink would put an indirect call in the
//   scan loop and buy nothing: the caller pushes into the lock-free queue, the test
//   reads the array, and neither needs to be polymorphic to do it.
//
// No Pico SDK dependency, so this compiles for the host and is covered by the native
// test target.

#ifndef GRIDPULSE_RUNEVENTS_H_
#define GRIDPULSE_RUNEVENTS_H_

#include <cstddef>
#include <cstdint>

#include "event.h"
#include "fsm.h"

namespace gridpulse {

// The most events one advance can produce: MODE plus TARGET, on entry to RUNNING.
inline constexpr std::size_t kMaxAdvanceEvents = 2;

// The outcome of advancing a run to a given instant.
struct RunAdvance {
  // Run state after the advance.
  GameState state = GameState::kIdle;

  // The countdown expired on this call: the run began, the clock started and the
  // first target was drawn.
  bool began = false;

  // The scored window expired on this call. The caller owns the end sequence
  // (MODE ENDED, END, HIST) rather than this function, because ABORT has to produce
  // the identical sequence from a path that never advances time. Emitting MODE ENDED
  // here as well is exactly the duplicate that reached every log.
  bool ended = false;

  // Events to emit, in wire order.
  Event events[kMaxAdvanceEvents] = {};
  std::size_t count = 0;
};

// Advances `game` to `now_us` and reports the events that advance produced.
//
// Pure with respect to everything except `game`: no clock, no LEDs, no queue.
RunAdvance AdvanceRun(Game* game, std::uint64_t now_us);

// Whether an ABORT should be acted on.
//
// An ABORT from the wire always applies - somebody pressed CANCEL or ESCAPE and meant
// it. One synthesised from the host closing the port applies only to a self-test, and
// the asymmetry is the point:
//
//   * A walk with nobody watching is destructive. It condemns a cell after two
//     five-second timeouts and then writes the mask to flash, so an abandoned walk
//     marks the whole board dead in about four minutes and the device afterwards
//     refuses to start a run at all. Stopping it costs a re-run of calibration.
//
//   * A run with nobody watching is merely pointless. It is scored on the device from
//     its own hardware timer, so the tally stays correct whether or not anything is
//     listening - and ending it on a control-line change would let a USB quirk or a
//     browser reload destroy a real result.
//
// In both cases this picks the action whose worst outcome is recoverable.
bool AbortShouldApply(const Command& command, bool self_testing);

}  // namespace gridpulse

#endif  // GRIDPULSE_RUNEVENTS_H_
