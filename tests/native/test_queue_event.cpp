// Native tests: the cross-core ring buffer and event serialisation.
//
// The queue carries every scoring event from core 1 to core 0. Two properties are
// non-negotiable: a full queue must never overwrite unread data (a silently dropped
// HIT is a silently wrong score), and every event type must serialise to a line that
// fits and validates.

#include "../../firmware/src/pure/event.h"
#include "../../firmware/src/pure/spsc_queue.h"

#include "../../firmware/src/pure/config.h"
#include "../../firmware/src/pure/fsm.h"
#include "../../firmware/src/pure/protocol.h"
#include "test_util.h"

using gridpulse::EndReason;
using gridpulse::Event;
using gridpulse::EventType;
using gridpulse::FindField;
using gridpulse::FormatEvent;
using gridpulse::FormatHistogramChunk;
using gridpulse::GameState;
using gridpulse::kCellCount;
using gridpulse::kHistogramChunkSize;
using gridpulse::kMaxLineBytes;
using gridpulse::LogLevel;
using gridpulse::ParseError;
using gridpulse::ParseU32;
using gridpulse::RunMode;
using gridpulse::RunReport;
using gridpulse::SelfTestResult;
using gridpulse::SpscQueue;
using gridpulse::VerifyLine;

namespace {

RunReport SampleReport() {
  RunReport report;
  report.n = 25;
  report.correct = 241;
  report.incorrect = 12;
  report.elapsed_us = 60'000'000;
  report.b_mbps = 17494;
  report.bit_rate = 17.494;
  report.reason = EndReason::kComplete;
  report.mode = RunMode::kEval;
  report.seed = 0xDEADBEEF;
  report.draws = 242;
  report.repeats = 9;
  report.max_streak = 38;
  report.min_reaction_us = 141002;
  report.p50_reaction_us = 203118;
  report.p95_reaction_us = 310447;
  report.p99_reaction_us = 402881;
  for (std::size_t i = 0; i < kCellCount; ++i) {
    report.target_histogram[i] = static_cast<std::uint32_t>(i + 1);
  }
  return report;
}

}  // namespace

// --- ring buffer ---------------------------------------------------------------

TEST(SpscQueue, PushAndPopInOrder) {
  SpscQueue<int, 8> queue;
  CHECK_TRUE(queue.Empty());
  CHECK_EQ_U(queue.Size(), 0u);

  for (int i = 0; i < 7; ++i) {
    CHECK_TRUE(queue.Push(i));
  }
  CHECK_EQ_U(queue.Size(), 7u);
  CHECK_FALSE(queue.Empty());

  for (int i = 0; i < 7; ++i) {
    int value = -1;
    CHECK_TRUE(queue.Pop(&value));
    CHECK_EQ(value, i);
  }
  CHECK_TRUE(queue.Empty());
}

TEST(SpscQueue, HoldsCapacityMinusOne) {
  // One slot stays empty so full and empty are distinguishable.
  SpscQueue<int, 8> queue;
  CHECK_EQ_U(queue.MaxSize(), 7u);
  for (int i = 0; i < 7; ++i) {
    CHECK_TRUE(queue.Push(i));
  }
  CHECK_FALSE(queue.Push(99));
}

TEST(SpscQueue, RefusesRatherThanOverwriting) {
  // The critical property: a full queue must not clobber unread data. A dropped HIT
  // that overwrote an earlier one would produce a silently wrong score.
  SpscQueue<int, 4> queue;
  CHECK_TRUE(queue.Push(1));
  CHECK_TRUE(queue.Push(2));
  CHECK_TRUE(queue.Push(3));
  CHECK_FALSE(queue.Push(4));
  CHECK_FALSE(queue.Push(5));
  CHECK_EQ_U(queue.dropped(), 2u);

  int value = 0;
  CHECK_TRUE(queue.Pop(&value));
  CHECK_EQ(value, 1);
  CHECK_TRUE(queue.Pop(&value));
  CHECK_EQ(value, 2);
  CHECK_TRUE(queue.Pop(&value));
  CHECK_EQ(value, 3);
  CHECK_FALSE(queue.Pop(&value));
}

TEST(SpscQueue, PopOnEmptyLeavesTheOutputUntouched) {
  SpscQueue<int, 4> queue;
  int value = 4242;
  CHECK_FALSE(queue.Pop(&value));
  CHECK_EQ(value, 4242);
}

TEST(SpscQueue, WrapsAroundIndefinitely) {
  // Interleaved push/pop far past the capacity, which is what the index wrap has to
  // survive for a session lasting minutes.
  SpscQueue<int, 4> queue;
  for (int i = 0; i < 10000; ++i) {
    CHECK_TRUE(queue.Push(i));
    int value = -1;
    CHECK_TRUE(queue.Pop(&value));
    CHECK_EQ(value, i);
  }
  CHECK_TRUE(queue.Empty());
  CHECK_EQ_U(queue.dropped(), 0u);
}

TEST(SpscQueue, ResetClearsEverything) {
  SpscQueue<int, 4> queue;
  queue.Push(1);
  queue.Push(2);
  queue.Push(3);
  queue.Push(4);  // dropped
  queue.Reset();
  CHECK_TRUE(queue.Empty());
  CHECK_EQ_U(queue.dropped(), 0u);
  CHECK_TRUE(queue.Push(9));
}

TEST(SpscQueue, CarriesFullEventsByValue) {
  // Events are copied, not referenced: core 0 must never read core 1's live state.
  SpscQueue<Event, 8> queue;
  Event original = gridpulse::MakeHit(1234, 13, 210000, 7, 1, 3);
  CHECK_TRUE(queue.Push(original));

  // Mutating the source afterwards must not change what is in the queue.
  original.sc = 999;

  Event popped;
  CHECK_TRUE(queue.Pop(&popped));
  CHECK_EQ_U(popped.sc, 7u);
  CHECK_EQ_U(popped.cell, 13u);
  CHECK_EQ_U(popped.t_us, 1234u);
}

TEST(SpscQueue, StaticMemoryBudgetIsWhatTheDocsClaim) {
  // These figures are quoted in firmware/src/pure/event.h and docs/ARCHITECTURE.md.
  // Pinning them here means a struct that quietly doubles in size fails the suite
  // rather than silently eating a chunk of the RP2040's 264 kB.
  CHECK_EQ_U(sizeof(RunReport), 184u);
  CHECK_EQ_U(sizeof(Event), 304u);
  CHECK_EQ_U(sizeof(SpscQueue<Event, gridpulse::kEventQueueDepth>), 19472u);

  // Whole-device sanity: the three big statics must stay well inside RAM.
  const std::size_t total =
      sizeof(SpscQueue<Event, gridpulse::kEventQueueDepth>) + sizeof(gridpulse::Game);
  CHECK_TRUE(total < 64u * 1024u);
}

TEST(SpscQueue, DepthMatchesTheConfiguredBudget) {
  SpscQueue<Event, gridpulse::kEventQueueDepth> queue;
  CHECK_EQ_U(queue.MaxSize(), gridpulse::kEventQueueDepth - 1);
  // Steady state is well under 20 events/second, so the queue is about three seconds
  // of buffering if core 0 stalls on USB.
  CHECK_TRUE(gridpulse::kEventQueueDepth >= 32);
}

// --- event serialisation ---------------------------------------------------------

TEST(Event, EveryTypeSerialisesToAValidLine) {
  const Event events[] = {
      gridpulse::MakeHello(1, 25, true),
      gridpulse::MakeMode(2, RunMode::kEval, GameState::kRunning, 0xDEADBEEF, 25),
      gridpulse::MakeSelfTest(3, 7, 10, 8, SelfTestResult::kNoKey, 2),
      gridpulse::MakeTarget(4, 13, 42, true),
      gridpulse::MakeHit(5, 13, 210000, 7, 1, 3),
      gridpulse::MakeMiss(6, 9, 13, 7, 2),
      gridpulse::MakeTick(7, 749959, 2, 1, 6113),
      gridpulse::MakeEnd(8, SampleReport()),
      gridpulse::MakeLog(9, LogLevel::kWarn, "queue overflow"),
  };

  for (const Event& event : events) {
    char buffer[kMaxLineBytes];
    const std::size_t length = FormatEvent(event, 1, buffer, sizeof(buffer));
    CHECK_TRUE(length > 0);
    CHECK_TRUE(length <= kMaxLineBytes);
    CHECK_EQ(buffer[length - 1], '\n');
    CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, nullptr)),
             static_cast<int>(ParseError::kOk));
  }
}

TEST(Event, HitCarriesTheFieldsTheHostNeeds) {
  const Event event = gridpulse::MakeHit(1'234'567, 13, 218646, 1, 0, 1);
  char buffer[kMaxLineBytes];
  const std::size_t length = FormatEvent(event, 5, buffer, sizeof(buffer));
  std::size_t payload = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload)),
           static_cast<int>(ParseError::kOk));

  const char* value = nullptr;
  std::size_t value_length = 0;
  std::uint32_t parsed = 0;

  CHECK_TRUE(FindField(buffer, payload, "cell", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 13u);

  CHECK_TRUE(FindField(buffer, payload, "rt_us", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 218646u);

  CHECK_TRUE(FindField(buffer, payload, "streak", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 1u);
}

TEST(Event, EndCarriesAllFourReportedFigures) {
  // The assignment requires B, N, Sc and Si. They must all be on the wire.
  const Event event = gridpulse::MakeEnd(60'000'000, SampleReport());
  char buffer[kMaxLineBytes];
  const std::size_t length = FormatEvent(event, 812, buffer, sizeof(buffer));
  std::size_t payload = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload)),
           static_cast<int>(ParseError::kOk));

  const char* value = nullptr;
  std::size_t value_length = 0;
  std::uint32_t parsed = 0;

  CHECK_TRUE(FindField(buffer, payload, "n", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 25u);

  CHECK_TRUE(FindField(buffer, payload, "sc", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 241u);

  CHECK_TRUE(FindField(buffer, payload, "si", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 12u);

  CHECK_TRUE(FindField(buffer, payload, "b_mbps", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 17494u);

  CHECK_TRUE(FindField(buffer, payload, "seed", &value, &value_length));
  CHECK_TRUE(FindField(buffer, payload, "t_us", &value, &value_length));
  CHECK_TRUE(FindField(buffer, payload, "reason", &value, &value_length));
}

TEST(Event, SelfTestPutsBothMappingsOnTheWire) {
  // gpio and pixel travel alongside cell so a swapped table is diagnosable from a
  // log alone, without the board in hand.
  const Event event = gridpulse::MakeSelfTest(1000, 6, 20, 11, SelfTestResult::kStuck, 1);
  char buffer[kMaxLineBytes];
  const std::size_t length = FormatEvent(event, 3, buffer, sizeof(buffer));
  std::size_t payload = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload)),
           static_cast<int>(ParseError::kOk));

  const char* value = nullptr;
  std::size_t value_length = 0;
  std::uint32_t parsed = 0;

  CHECK_TRUE(FindField(buffer, payload, "cell", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 6u);
  CHECK_TRUE(FindField(buffer, payload, "gpio", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 20u);
  CHECK_TRUE(FindField(buffer, payload, "pixel", &value, &value_length));
  CHECK_TRUE(ParseU32(value, value_length, &parsed));
  CHECK_EQ_U(parsed, 11u);
}

TEST(Event, LogTextWithSpacesIsSanitised) {
  const Event event = gridpulse::MakeLog(1, LogLevel::kError, "bad crc on line");
  char buffer[kMaxLineBytes];
  const std::size_t length = FormatEvent(event, 1, buffer, sizeof(buffer));
  std::size_t payload = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload)),
           static_cast<int>(ParseError::kOk));
  const char* value = nullptr;
  std::size_t value_length = 0;
  CHECK_TRUE(FindField(buffer, payload, "msg", &value, &value_length));
  // The whole message survives as one field rather than splitting into several.
  CHECK_EQ_U(value_length, 15u);
}

TEST(Event, LogTextIsTruncatedNotOverflowed) {
  const char* very_long =
      "this message is considerably longer than the fixed log buffer and must be "
      "truncated rather than overrunning it";
  const Event event = gridpulse::MakeLog(1, LogLevel::kInfo, very_long);
  CHECK_TRUE(event.text[gridpulse::kMaxLogTextBytes] == '\0');
  char buffer[kMaxLineBytes];
  const std::size_t length = FormatEvent(event, 1, buffer, sizeof(buffer));
  CHECK_TRUE(length > 0);
  CHECK_TRUE(length <= kMaxLineBytes);
}

TEST(Event, HistogramIsChunkedAndCoversEveryCell) {
  const RunReport report = SampleReport();
  std::size_t offset = 0;
  std::uint32_t recovered[kCellCount] = {};
  std::size_t chunks = 0;

  for (;;) {
    char buffer[kMaxLineBytes];
    const std::size_t start = offset;
    const std::size_t length =
        FormatHistogramChunk(report, 900 + static_cast<std::uint32_t>(chunks), 60'000'000,
                             &offset, buffer, sizeof(buffer));
    if (length == 0) {
      break;
    }
    ++chunks;
    CHECK_TRUE(length <= kMaxLineBytes);

    std::size_t payload = 0;
    CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload)),
             static_cast<int>(ParseError::kOk));

    const char* value = nullptr;
    std::size_t value_length = 0;
    CHECK_TRUE(FindField(buffer, payload, "off", &value, &value_length));
    std::uint32_t parsed_offset = 0;
    CHECK_TRUE(ParseU32(value, value_length, &parsed_offset));
    CHECK_EQ_U(parsed_offset, start);

    CHECK_TRUE(FindField(buffer, payload, "v", &value, &value_length));

    // Parse the comma-separated counts back out.
    std::size_t index = start;
    std::size_t cursor = 0;
    while (cursor < value_length && index < kCellCount) {
      std::size_t field_end = cursor;
      while (field_end < value_length && value[field_end] != ',') {
        ++field_end;
      }
      std::uint32_t count = 0;
      CHECK_TRUE(ParseU32(value + cursor, field_end - cursor, &count));
      recovered[index++] = count;
      cursor = field_end + 1;
    }
  }

  CHECK_EQ_U(offset, kCellCount);
  // 25 cells at 12 per chunk is three lines.
  CHECK_EQ_U(chunks, (kCellCount + kHistogramChunkSize - 1) / kHistogramChunkSize);
  for (std::size_t i = 0; i < kCellCount; ++i) {
    CHECK_EQ_U(recovered[i], report.target_histogram[i]);
  }
}

TEST(Event, SequenceNumbersAppearOnTheWire) {
  // A gap in seq is how the host detects lost bytes, so the value must actually be
  // the one the caller supplied.
  char buffer[kMaxLineBytes];
  const Event event = gridpulse::MakeTick(1, 2, 3, 4, 5);
  const std::size_t length = FormatEvent(event, 987654, buffer, sizeof(buffer));
  CHECK_TRUE(length > 0);
  // "EV 987654 ..." - check the prefix directly.
  const char* expected = "EV 987654 ";
  for (std::size_t i = 0; expected[i] != '\0'; ++i) {
    CHECK_EQ(buffer[i], expected[i]);
  }
}
