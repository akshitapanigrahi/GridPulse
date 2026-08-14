// Native tests: wire protocol framing, formatting and parsing.
//
// Two properties matter most and are tested hardest:
//
//   1. Round trip. A line the device writes must be a line the device can validate,
//      for every message type and every plausible field value.
//   2. Total rejection. A malformed line must be discarded WHOLE. Partial
//      application - acting on a command whose arguments failed to parse - is the
//      failure mode that would let a corrupted byte start the scored run.

#include <cstdio>
#include "../../firmware/src/pure/config.h"
#include "../../firmware/src/pure/crc.h"
#include "../../firmware/src/pure/protocol.h"
#include "../vectors/vectors.gen.h"

#include "test_util.h"

using gridpulse::Command;
using gridpulse::CommandName;
using gridpulse::EventType;
using gridpulse::FindField;
using gridpulse::LineWriter;
using gridpulse::ParseCommand;
using gridpulse::ParseError;
using gridpulse::ParseErrorText;
using gridpulse::ParseU32;
using gridpulse::ParseU64;
using gridpulse::RunMode;
using gridpulse::VerifyLine;

namespace {

std::size_t Length(const char* s) {
  std::size_t n = 0;
  while (s != nullptr && s[n] != '\0') {
    ++n;
  }
  return n;
}

bool Equals(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return a == b;
  }
  std::size_t i = 0;
  while (a[i] != '\0' && a[i] == b[i]) {
    ++i;
  }
  return a[i] == b[i];
}

}  // namespace

TEST(Protocol, WritesAWellFormedEvent) {
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(42, 1234567, EventType::kTarget);
  writer.AddU32("cell", 13).AddU32("idx", 7).AddBool("repeat", false);
  const std::size_t length = writer.Finish();

  CHECK_TRUE(length > 0);
  CHECK_FALSE(writer.overflowed());
  CHECK_EQ(buffer[length - 1], '\n');
  CHECK_EQ(buffer[length], '\0');

  // Everything it wrote must validate.
  std::size_t payload_length = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload_length)),
           static_cast<int>(ParseError::kOk));

  const char* value = nullptr;
  std::size_t value_length = 0;
  CHECK_TRUE(FindField(buffer, payload_length, "cell", &value, &value_length));
  std::uint32_t cell = 0;
  CHECK_TRUE(ParseU32(value, value_length, &cell));
  CHECK_EQ_U(cell, 13u);

  CHECK_TRUE(FindField(buffer, payload_length, "repeat", &value, &value_length));
  CHECK_EQ_U(value_length, 1u);
  CHECK_EQ(value[0], '0');
}

TEST(Protocol, EveryEventTypeRoundTrips) {
  const EventType types[] = {
      EventType::kHello, EventType::kMode, EventType::kSelfTest, EventType::kTarget,
      EventType::kHit,   EventType::kMiss, EventType::kTick,     EventType::kEnd,
      EventType::kHist,  EventType::kLog,
  };
  for (const EventType type : types) {
    char buffer[gridpulse::kMaxLineBytes];
    LineWriter writer(buffer, sizeof(buffer));
    writer.BeginEvent(1, 2, type);
    writer.AddU32("x", 3);
    const std::size_t length = writer.Finish();
    CHECK_TRUE(length > 0);
    CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, nullptr)),
             static_cast<int>(ParseError::kOk));
  }
}

TEST(Protocol, WritesAFullEndMessageWithinTheLineLimit) {
  // END carries the whole tally. If it does not fit, the run's result cannot be
  // transmitted, so this is a hard bound worth asserting rather than assuming.
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(4294967295u, 18446744073709551615ull, EventType::kEnd);
  writer.AddU32("n", 25)
      .AddU32("sc", 4294967295u)
      .AddU32("si", 4294967295u)
      .AddU64("t_us", 60000000ull)
      .AddU32("b_mbps", 4294967295u)
      .AddToken("reason", "COMPLETE")
      .AddToken("mode", "EVAL")
      .AddU32("seed", 4294967295u)
      .AddU32("draws", 999999u)
      .AddU32("repeats", 999999u)
      .AddU32("max_streak", 999999u)
      .AddU32("min_us", 4294967295u)
      .AddU32("p50_us", 4294967295u)
      .AddU32("p95_us", 4294967295u)
      .AddU32("p99_us", 4294967295u);
  const std::size_t length = writer.Finish();
  CHECK_TRUE(length > 0);
  CHECK_TRUE(length <= gridpulse::kMaxLineBytes);
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, nullptr)),
           static_cast<int>(ParseError::kOk));
}

TEST(Protocol, HistogramChunkOfTwelveFits) {
  // The per-cell histogram is chunked so each HIST line stays within the limit.
  // Twelve values is the chunk size the firmware uses.
  const std::uint32_t values[12] = {999, 999, 999, 999, 999, 999,
                                    999, 999, 999, 999, 999, 999};
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(4294967295u, 18446744073709551615ull, EventType::kHist);
  writer.AddU32("off", 24).AddU32Csv("v", values, 12);
  const std::size_t length = writer.Finish();
  CHECK_TRUE(length > 0);
  CHECK_TRUE(length <= gridpulse::kMaxLineBytes);
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, nullptr)),
           static_cast<int>(ParseError::kOk));
}

TEST(Protocol, OverflowIsReportedNotTruncated) {
  // A truncated line would still carry a valid CRC over its truncated self and would
  // be accepted downstream as a complete, wrong message. Finish() must refuse.
  char buffer[48];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(1, 2, EventType::kEnd);
  for (int i = 0; i < 40; ++i) {
    writer.AddU32("padpadpad", 123456789u);
  }
  CHECK_TRUE(writer.overflowed());
  CHECK_EQ_U(writer.Finish(), 0u);
}

TEST(Protocol, TokenValuesAreSanitised) {
  // The protocol has no escaping, so a space or an '=' inside a value would split
  // one field into two and silently change the meaning of the line.
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(1, 2, EventType::kLog);
  writer.AddToken("msg", "hello world=danger");
  const std::size_t length = writer.Finish();
  CHECK_TRUE(length > 0);

  std::size_t payload_length = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload_length)),
           static_cast<int>(ParseError::kOk));

  const char* value = nullptr;
  std::size_t value_length = 0;
  CHECK_TRUE(FindField(buffer, payload_length, "msg", &value, &value_length));
  CHECK_EQ_U(value_length, 18u);
  for (std::size_t i = 0; i < value_length; ++i) {
    CHECK_TRUE(value[i] != ' ' && value[i] != '=');
  }
}

TEST(Protocol, EmptyTokenBecomesAPlaceholder) {
  // "key=" would be ambiguous with a missing field.
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginEvent(1, 2, EventType::kLog);
  writer.AddToken("msg", "");
  const std::size_t length = writer.Finish();
  std::size_t payload_length = 0;
  CHECK_EQ(static_cast<int>(VerifyLine(buffer, length, &payload_length)),
           static_cast<int>(ParseError::kOk));
  const char* value = nullptr;
  std::size_t value_length = 0;
  CHECK_TRUE(FindField(buffer, payload_length, "msg", &value, &value_length));
  CHECK_EQ_U(value_length, 1u);
  CHECK_EQ(value[0], '-');
}

TEST(Protocol, FindFieldMatchesWholeKeysOnly) {
  const char* payload = "EV 1 2 END n=25 sc=241 si=12 seed=7 max_streak=38";
  const std::size_t length = Length(payload);
  const char* value = nullptr;
  std::size_t value_length = 0;

  CHECK_TRUE(FindField(payload, length, "n", &value, &value_length));
  CHECK_EQ_U(value_length, 2u);
  CHECK_EQ(value[0], '2');

  // "s" is a prefix of "sc", "si" and "seed" but is not itself a key.
  CHECK_FALSE(FindField(payload, length, "s", &value, &value_length));
  // "streak" is a suffix of "max_streak" but is not itself a key.
  CHECK_FALSE(FindField(payload, length, "streak", &value, &value_length));
  CHECK_TRUE(FindField(payload, length, "max_streak", &value, &value_length));
  CHECK_FALSE(FindField(payload, length, "absent", &value, &value_length));
}

TEST(Protocol, ParsesUnsignedIntegersStrictly) {
  std::uint64_t wide = 0;
  std::uint32_t narrow = 0;

  CHECK_TRUE(ParseU64("0", 1, &wide));
  CHECK_EQ_U(wide, 0u);
  CHECK_TRUE(ParseU64("18446744073709551615", 20, &wide));
  CHECK_EQ_U(wide, 18446744073709551615ull);

  // Overflow must be rejected, not wrapped into a small plausible number.
  CHECK_FALSE(ParseU64("18446744073709551616", 20, &wide));
  CHECK_FALSE(ParseU64("99999999999999999999999", 23, &wide));

  CHECK_FALSE(ParseU64("", 0, &wide));
  CHECK_FALSE(ParseU64("12a", 3, &wide));
  CHECK_FALSE(ParseU64("-1", 2, &wide));
  CHECK_FALSE(ParseU64(" 1", 2, &wide));
  CHECK_FALSE(ParseU64("1.5", 3, &wide));

  CHECK_TRUE(ParseU32("4294967295", 10, &narrow));
  CHECK_EQ_U(narrow, 4294967295u);
  CHECK_FALSE(ParseU32("4294967296", 10, &narrow));
}

TEST(Protocol, MatchesGoldenVectors) {
  for (std::size_t i = 0; i < gridpulse_vectors::kProtocolCaseCount; ++i) {
    const auto& vec = gridpulse_vectors::kProtocolCases[i];

    Command command;
    // Poison the output so a parser that writes on failure is caught.
    command.name = CommandName::kBright;
    command.brightness_pct = 77;
    command.force = true;
    command.mode = RunMode::kEval;

    const ParseError error = ParseCommand(vec.line, vec.line_length, &command);
    if (!Equals(ParseErrorText(error), vec.error)) {
      // Name the case before the assertion records the mismatch; otherwise a failure
      // in a 40-case loop leaves the reader counting entries in a JSON file.
      std::printf("    (protocol vector case: %s -- %s)\n", vec.name, vec.note);
    }
    CHECK_EQ_STR(ParseErrorText(error), vec.error);

    if (!vec.ok) {
      // Total rejection: nothing may have been written to the output.
      CHECK_TRUE(command.name == CommandName::kBright);
      CHECK_EQ_U(command.brightness_pct, 77u);
      CHECK_TRUE(command.force);
      continue;
    }

    CHECK_TRUE(Equals(gridpulse::CommandNameToken(command.name), vec.command));
    if (vec.mode != nullptr) {
      CHECK_TRUE(Equals(gridpulse::RunModeToken(command.mode), vec.mode));
    }
    if (vec.force >= 0) {
      CHECK_EQ(command.force ? 1 : 0, vec.force);
    }
    if (vec.brightness_pct >= 0) {
      CHECK_EQ(static_cast<int>(command.brightness_pct), vec.brightness_pct);
    }
  }
}

TEST(Protocol, WrittenCommandsParseBack) {
  // The device writes commands only in tests and tooling, but the round trip is what
  // proves the writer and the parser agree about framing.
  const CommandName names[] = {CommandName::kStart,    CommandName::kAbort,
                               CommandName::kSelfTest, CommandName::kPing,
                               CommandName::kBright,   CommandName::kProto};
  for (const CommandName name : names) {
    char buffer[gridpulse::kMaxLineBytes];
    LineWriter writer(buffer, sizeof(buffer));
    writer.BeginCommand(name);
    if (name == CommandName::kStart) {
      writer.AddToken("mode", "EVAL");
    } else if (name == CommandName::kBright) {
      writer.AddU32("pct", 42);
    }
    const std::size_t length = writer.Finish();
    CHECK_TRUE(length > 0);

    Command parsed;
    CHECK_EQ(static_cast<int>(ParseCommand(buffer, length, &parsed)),
             static_cast<int>(ParseError::kOk));
    CHECK_TRUE(parsed.name == name);
    if (name == CommandName::kStart) {
      CHECK_TRUE(parsed.mode == RunMode::kEval);
    }
    if (name == CommandName::kBright) {
      CHECK_EQ_U(parsed.brightness_pct, 42u);
    }
  }
}

TEST(Protocol, RejectsEveryTruncationOfAValidLine) {
  // Fuzz-adjacent: no prefix of a valid line may parse as a valid command. A
  // truncated line that parsed would mean a dropped USB byte could start a run.
  char buffer[gridpulse::kMaxLineBytes];
  LineWriter writer(buffer, sizeof(buffer));
  writer.BeginCommand(CommandName::kStart);
  writer.AddToken("mode", "EVAL");
  const std::size_t length = writer.Finish();
  CHECK_TRUE(length > 0);

  // Cutting exactly the trailing newline is NOT truncation: the terminator is
  // optional and VerifyLine strips it, so that prefix is still a valid line. Every
  // shorter prefix must be rejected.
  Command unterminated;
  CHECK_EQ(static_cast<int>(ParseCommand(buffer, length - 1, &unterminated)),
           static_cast<int>(ParseError::kOk));

  for (std::size_t cut = 1; cut + 1 < length; ++cut) {
    Command parsed;
    const ParseError error = ParseCommand(buffer, cut, &parsed);
    CHECK_TRUE(error != ParseError::kOk);
  }
}

TEST(Protocol, RejectsEverySingleByteCorruptionOfAValidLine) {
  // Any one changed byte must be rejected, by the CRC if nothing else. This is the
  // property that makes a noisy link safe.
  char base[gridpulse::kMaxLineBytes];
  LineWriter writer(base, sizeof(base));
  writer.BeginCommand(CommandName::kStart);
  writer.AddToken("mode", "EVAL");
  const std::size_t length = writer.Finish();
  CHECK_TRUE(length > 0);

  // Exclude the trailing newline; corrupting it tests the terminator, not the CRC.
  for (std::size_t byte = 0; byte + 1 < length; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      char mutated[gridpulse::kMaxLineBytes];
      for (std::size_t j = 0; j < length; ++j) {
        mutated[j] = base[j];
      }
      mutated[byte] =
          static_cast<char>(static_cast<unsigned char>(mutated[byte]) ^ (1u << bit));

      Command parsed;
      const ParseError error = ParseCommand(mutated, length, &parsed);
      CHECK_TRUE(error != ParseError::kOk);
    }
  }
}

// The host may abort, but it may not claim to have gone away.
//
// `host_detached` is synthesised on the device when core 0 sees the port close, and it
// suppresses the abort for a run in progress. If a wire command could set it, a host
// could send an ABORT that the device quietly ignored - which is worse than either
// honouring or rejecting it, because the UI would show a run it thought it had ended.
TEST(Protocol, NoWireCommandCanClaimTheHostDetached) {
  const char* payloads[] = {
      "CMD ABORT",
      "CMD ABORT host_detached=1",
      "CMD START mode=EVAL",
      "CMD SELFTEST force=1",
      "CMD PING",
  };
  std::size_t accepted = 0;
  for (const char* payload : payloads) {
    char line[gridpulse::kMaxLineBytes];
    std::size_t i = 0;
    while (payload[i] != '\0') {
      line[i] = payload[i];
      ++i;
    }
    // Properly framed, or every line would be rejected on its CRC and this test would
    // assert nothing at all.
    const std::uint16_t crc = gridpulse::Crc16CcittFalse(line, i);
    const char* hex = "0123456789ABCDEF";
    line[i++] = ' ';
    line[i++] = hex[(crc >> 12) & 0xF];
    line[i++] = hex[(crc >> 8) & 0xF];
    line[i++] = hex[(crc >> 4) & 0xF];
    line[i++] = hex[crc & 0xF];
    line[i++] = '\n';

    Command command;
    command.host_detached = true;  // poisoned, so a parse that ignores it is caught
    if (ParseCommand(line, i, &command) == ParseError::kOk) {
      ++accepted;
      CHECK_FALSE(command.host_detached);
    }
  }
  // And the loop actually exercised something.
  CHECK_TRUE(accepted >= 4);
}

TEST(Protocol, ThereIsNoCommandThatInfluencesTheSequence) {
  // docs/PROTOCOL.md section 6 claims, structurally, that the host cannot reach the
  // sampler. Assert it: no command may carry a seed, a target or an N.
  const char* forbidden[] = {
      "CMD START mode=EVAL seed=1",
      "CMD START mode=EVAL n=5",
      "CMD SETSEED seed=12345",
      "CMD SETTARGET cell=7",
      "CMD SETN n=3",
  };
  for (const char* payload : forbidden) {
    char line[gridpulse::kMaxLineBytes];
    std::size_t i = 0;
    while (payload[i] != '\0') {
      line[i] = payload[i];
      ++i;
    }
    const std::uint16_t crc = gridpulse::Crc16CcittFalse(line, i);
    const char* hex = "0123456789ABCDEF";
    line[i++] = ' ';
    line[i++] = hex[(crc >> 12) & 0xF];
    line[i++] = hex[(crc >> 8) & 0xF];
    line[i++] = hex[(crc >> 4) & 0xF];
    line[i++] = hex[crc & 0xF];
    line[i++] = '\n';

    Command parsed;
    parsed.mode = RunMode::kPractice;
    const ParseError error = ParseCommand(line, i, &parsed);

    // The first two are well-formed START commands: they must parse, but the extra
    // fields must be ignored entirely rather than reaching anything. The rest are
    // unknown commands and must be rejected outright.
    if (error == ParseError::kOk) {
      CHECK_TRUE(parsed.name == CommandName::kStart);
      CHECK_TRUE(parsed.mode == RunMode::kEval);
    } else {
      CHECK_TRUE(error == ParseError::kUnknownName);
    }
  }
}
