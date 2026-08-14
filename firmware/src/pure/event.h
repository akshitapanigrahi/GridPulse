// GRID PULSE - the event record passed from core 1 to core 0.
//
// PURPOSE
//   Core 1 owns the game and produces events; core 0 formats them onto the USB CDC
//   link. This is the record they agree on. Keeping it a plain, fixed-size,
//   trivially-copyable struct means the ring buffer copies bytes and nothing else -
//   no pointers to core 1's stack, no lifetime reasoning across cores.
//
// WHY NOT A UNION
//   A union would shrink the struct from about 200 bytes to about 40. At a queue
//   depth of 64 that saves 10 kB out of the RP2040's 264 kB, which is not a
//   constraint here, and it would buy that with type punning across two cores -
//   precisely the kind of cleverness that produces a bug nobody can reproduce.
//   Flat named fields cost memory this system has and remove a whole class of
//   error.
//
// MEMORY BUDGET (measured, and pinned by a test in tests/native/test_queue_event.cpp)
//   sizeof(RunReport)   184 bytes
//   sizeof(Event)       304 bytes
//   event queue         19472 bytes  (64 slots)
//   Game object          4384 bytes  (mostly the reaction-time sample array)
//
//   Total static footprint is about 33 kB of the RP2040's 264 kB. All of it is
//   allocated at link time; the firmware performs no dynamic allocation at any point,
//   before or after init.

#ifndef GRIDPULSE_EVENT_H_
#define GRIDPULSE_EVENT_H_

#include <cstddef>
#include <cstdint>

#include "fsm.h"
#include "protocol.h"

namespace gridpulse {

enum class SelfTestResult : std::uint8_t {
  kOk,
  kNoKey,
  kStuck,
};

const char* SelfTestResultToken(SelfTestResult result);

enum class LogLevel : std::uint8_t {
  kInfo,
  kWarn,
  kError,
};

char LogLevelChar(LogLevel level);

// Maximum characters in a LOG message, excluding the terminator. Log text is always
// a compile-time string in this firmware, so this is generous.
inline constexpr std::size_t kMaxLogTextBytes = 48;

struct Event {
  EventType type = EventType::kLog;

  // Device hardware timer at the moment the event was produced on core 1. This is
  // the timestamp that reaches the wire; core 0 never restamps, so USB scheduling
  // jitter cannot contaminate a measurement.
  std::uint64_t t_us = 0;

  // HELLO
  std::uint8_t hello_n = 0;
  bool hello_pins_ok = false;

  // MODE
  GameState state = GameState::kIdle;
  RunMode mode = RunMode::kEval;
  std::uint32_t seed = 0;
  std::uint8_t n = 0;

  // SELFTEST
  std::uint8_t st_cell = 0;
  std::uint8_t st_gpio = 0;
  std::uint8_t st_pixel = 0;
  std::uint8_t st_pass = 0;
  SelfTestResult st_result = SelfTestResult::kOk;

  // TARGET
  std::uint8_t cell = 0;
  std::uint32_t draw_index = 0;
  bool repeat = false;

  // HIT and MISS
  std::uint8_t pressed = 0;
  std::uint8_t target = 0;
  std::uint32_t reaction_us = 0;
  std::uint32_t sc = 0;
  std::uint32_t si = 0;
  std::uint32_t streak = 0;

  // TICK
  std::uint64_t t_run_us = 0;
  std::uint32_t b_mbps = 0;

  // LOG
  LogLevel level = LogLevel::kInfo;
  char text[kMaxLogTextBytes + 1] = {};

  // END. Carried by value so core 0 never reads core 1's live state.
  RunReport report;
};

// Trivially copyable is what makes the ring buffer a byte copy rather than a
// construction. If someone adds a std::string or a pointer here, this fails to
// compile rather than producing a cross-core lifetime bug.
static_assert(sizeof(Event) > 0, "Event must be a complete type");

// --- constructors for each event type ----------------------------------------
//
// Free functions rather than a fat constructor: each one names exactly the fields
// that type carries, so a caller cannot silently forget one or set a field that
// does not apply.

Event MakeHello(std::uint64_t t_us, std::uint8_t n, bool pins_ok);
Event MakeMode(std::uint64_t t_us, RunMode mode, GameState state, std::uint32_t seed,
               std::uint8_t n);
Event MakeSelfTest(std::uint64_t t_us, std::uint8_t cell, std::uint8_t gpio,
                   std::uint8_t pixel, SelfTestResult result, std::uint8_t pass);
Event MakeTarget(std::uint64_t t_us, std::uint8_t cell, std::uint32_t draw_index,
                 bool repeat);
Event MakeHit(std::uint64_t t_us, std::uint8_t cell, std::uint32_t reaction_us,
              std::uint32_t sc, std::uint32_t si, std::uint32_t streak);
Event MakeMiss(std::uint64_t t_us, std::uint8_t pressed, std::uint8_t target,
               std::uint32_t sc, std::uint32_t si);
Event MakeTick(std::uint64_t t_us, std::uint64_t t_run_us, std::uint32_t sc,
               std::uint32_t si, std::uint32_t b_mbps);
Event MakeEnd(std::uint64_t t_us, const RunReport& report);
Event MakeHist(std::uint64_t t_us, const RunReport& report);
Event MakeLog(std::uint64_t t_us, LogLevel level, const char* text);

// --- serialisation ------------------------------------------------------------

// Formats `event` as a protocol line into `buffer`.
//
// Returns the byte count written including the terminating newline, or 0 if the
// event did not fit - which the caller must treat as a bug rather than a runtime
// condition, since kMaxLineBytes is derived from the worst case.
std::size_t FormatEvent(const Event& event, std::uint32_t seq, char* buffer,
                        std::size_t capacity);

// Formats the per-cell target histogram as one or more HIST lines.
//
// Chunked because 25 counts plus a header would exceed kMaxLineBytes. Returns the
// number of bytes written for the chunk starting at `offset`, and advances `offset`
// past it; returns 0 when there is nothing left to send.
inline constexpr std::size_t kHistogramChunkSize = 12;

std::size_t FormatHistogramChunk(const RunReport& report, std::uint32_t seq,
                                 std::uint64_t t_us, std::size_t* offset, char* buffer,
                                 std::size_t capacity);

}  // namespace gridpulse

#endif  // GRIDPULSE_EVENT_H_
