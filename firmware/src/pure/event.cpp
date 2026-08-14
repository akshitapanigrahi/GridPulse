#include "event.h"

#include "config.h"

namespace gridpulse {
namespace {

void CopyText(char* destination, const char* source, std::size_t capacity) {
  std::size_t i = 0;
  if (source != nullptr) {
    while (i + 1 < capacity && source[i] != '\0') {
      destination[i] = source[i];
      ++i;
    }
  }
  destination[i] = '\0';
}

}  // namespace

const char* SelfTestResultToken(SelfTestResult result) {
  switch (result) {
    case SelfTestResult::kOk:
      return "OK";
    case SelfTestResult::kNoKey:
      return "NO_KEY";
    case SelfTestResult::kStuck:
      return "STUCK";
  }
  return "UNKNOWN";
}

char LogLevelChar(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return 'I';
    case LogLevel::kWarn:
      return 'W';
    case LogLevel::kError:
      return 'E';
  }
  return '?';
}

Event MakeHello(std::uint64_t t_us, std::uint8_t n, bool pins_ok) {
  Event event;
  event.type = EventType::kHello;
  event.t_us = t_us;
  event.hello_n = n;
  event.hello_pins_ok = pins_ok;
  return event;
}

Event MakeMode(std::uint64_t t_us, RunMode mode, GameState state, std::uint32_t seed,
               std::uint8_t n) {
  Event event;
  event.type = EventType::kMode;
  event.t_us = t_us;
  event.mode = mode;
  event.state = state;
  event.seed = seed;
  event.n = n;
  return event;
}

Event MakeSelfTest(std::uint64_t t_us, std::uint8_t cell, std::uint8_t gpio,
                   std::uint8_t pixel, SelfTestResult result, std::uint8_t pass) {
  Event event;
  event.type = EventType::kSelfTest;
  event.t_us = t_us;
  event.st_cell = cell;
  event.st_gpio = gpio;
  event.st_pixel = pixel;
  event.st_result = result;
  event.st_pass = pass;
  return event;
}

Event MakeTarget(std::uint64_t t_us, std::uint8_t cell, std::uint32_t draw_index,
                 bool repeat) {
  Event event;
  event.type = EventType::kTarget;
  event.t_us = t_us;
  event.cell = cell;
  event.draw_index = draw_index;
  event.repeat = repeat;
  return event;
}

Event MakeHit(std::uint64_t t_us, std::uint8_t cell, std::uint32_t reaction_us,
              std::uint32_t sc, std::uint32_t si, std::uint32_t streak) {
  Event event;
  event.type = EventType::kHit;
  event.t_us = t_us;
  event.cell = cell;
  event.reaction_us = reaction_us;
  event.sc = sc;
  event.si = si;
  event.streak = streak;
  return event;
}

Event MakeMiss(std::uint64_t t_us, std::uint8_t pressed, std::uint8_t target,
               std::uint32_t sc, std::uint32_t si) {
  Event event;
  event.type = EventType::kMiss;
  event.t_us = t_us;
  event.pressed = pressed;
  event.target = target;
  event.sc = sc;
  event.si = si;
  return event;
}

Event MakeTick(std::uint64_t t_us, std::uint64_t t_run_us, std::uint32_t sc,
               std::uint32_t si, std::uint32_t b_mbps) {
  Event event;
  event.type = EventType::kTick;
  event.t_us = t_us;
  event.t_run_us = t_run_us;
  event.sc = sc;
  event.si = si;
  event.b_mbps = b_mbps;
  return event;
}

Event MakeEnd(std::uint64_t t_us, const RunReport& report) {
  Event event;
  event.type = EventType::kEnd;
  event.t_us = t_us;
  event.report = report;
  return event;
}

Event MakeHist(std::uint64_t t_us, const RunReport& report) {
  Event event;
  event.type = EventType::kHist;
  event.t_us = t_us;
  event.report = report;
  return event;
}

Event MakeLog(std::uint64_t t_us, LogLevel level, const char* text) {
  Event event;
  event.type = EventType::kLog;
  event.t_us = t_us;
  event.level = level;
  CopyText(event.text, text, sizeof(event.text));
  return event;
}

std::size_t FormatEvent(const Event& event, std::uint32_t seq, char* buffer,
                        std::size_t capacity) {
  LineWriter writer(buffer, capacity);
  writer.BeginEvent(seq, event.t_us, event.type);

  switch (event.type) {
    case EventType::kHello:
      writer.AddU32("proto", kProtocolVersion)
          .AddToken("fw", kFirmwareVersion)
          .AddToken("board", kBoardId)
          .AddU32("n", event.hello_n)
          .AddBool("pins_ok", event.hello_pins_ok);
      break;

    case EventType::kMode:
      writer.AddToken("mode", RunModeToken(event.mode))
          .AddToken("state", GameStateToken(event.state))
          .AddU32("seed", event.seed)
          .AddU32("n", event.n);
      break;

    case EventType::kSelfTest:
      // gpio and pixel travel alongside cell deliberately: it puts both independent
      // mappings on the wire, so a swapped table is diagnosable from a log alone.
      writer.AddU32("cell", event.st_cell)
          .AddU32("gpio", event.st_gpio)
          .AddU32("pixel", event.st_pixel)
          .AddToken("result", SelfTestResultToken(event.st_result))
          .AddU32("pass", event.st_pass);
      break;

    case EventType::kTarget:
      writer.AddU32("cell", event.cell)
          .AddU32("idx", event.draw_index)
          .AddBool("repeat", event.repeat);
      break;

    case EventType::kHit:
      writer.AddU32("cell", event.cell)
          .AddU32("rt_us", event.reaction_us)
          .AddU32("sc", event.sc)
          .AddU32("si", event.si)
          .AddU32("streak", event.streak);
      break;

    case EventType::kMiss:
      writer.AddU32("pressed", event.pressed)
          .AddU32("target", event.target)
          .AddU32("sc", event.sc)
          .AddU32("si", event.si);
      break;

    case EventType::kTick:
      writer.AddU64("t_run_us", event.t_run_us)
          .AddU32("sc", event.sc)
          .AddU32("si", event.si)
          .AddU32("b_mbps", event.b_mbps);
      break;

    case EventType::kEnd: {
      const RunReport& r = event.report;
      writer.AddU32("n", static_cast<std::uint32_t>(r.n))
          .AddU32("sc", r.correct)
          .AddU32("si", r.incorrect)
          .AddU64("t_us", r.elapsed_us)
          .AddU32("b_mbps", r.b_mbps)
          .AddToken("reason", EndReasonToken(r.reason))
          .AddToken("mode", RunModeToken(r.mode))
          .AddU32("seed", r.seed)
          .AddU32("draws", r.draws)
          .AddU32("repeats", r.repeats)
          .AddU32("max_streak", r.max_streak)
          .AddU32("min_us", r.min_reaction_us)
          .AddU32("p50_us", r.p50_reaction_us)
          .AddU32("p95_us", r.p95_reaction_us)
          .AddU32("p99_us", r.p99_reaction_us);
      break;
    }

    case EventType::kHist:
      // Histograms are emitted by FormatHistogramChunk, which knows how to split
      // them; reaching here means a caller queued the wrong event type.
      return 0;

    case EventType::kLog:
      writer
          .AddToken("level", (event.level == LogLevel::kInfo)   ? "I"
                             : (event.level == LogLevel::kWarn) ? "W"
                                                                : "E")
          .AddToken("msg", event.text);
      break;
  }

  return writer.Finish();
}

std::size_t FormatHistogramChunk(const RunReport& report, std::uint32_t seq,
                                 std::uint64_t t_us, std::size_t* offset, char* buffer,
                                 std::size_t capacity) {
  if (offset == nullptr || *offset >= kCellCount) {
    return 0;
  }

  const std::size_t start = *offset;
  std::size_t count = kCellCount - start;
  if (count > kHistogramChunkSize) {
    count = kHistogramChunkSize;
  }

  LineWriter writer(buffer, capacity);
  writer.BeginEvent(seq, t_us, EventType::kHist);
  writer.AddU32("off", static_cast<std::uint32_t>(start))
      .AddU32Csv("v", &report.target_histogram[start], count);

  const std::size_t written = writer.Finish();
  if (written == 0) {
    return 0;
  }
  *offset = start + count;
  return written;
}

}  // namespace gridpulse
