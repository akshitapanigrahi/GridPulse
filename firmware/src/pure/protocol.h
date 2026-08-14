// GRID PULSE - wire protocol framing and parsing.
//
// PURPOSE
//   Implements docs/PROTOCOL.md v1: line-oriented ASCII, CRC-16/CCITT-FALSE over
//   every line in both directions.
//
// INVARIANTS
//   - No dynamic allocation. LineWriter formats into a caller-supplied buffer and
//     reports overflow rather than truncating; a truncated line would still carry a
//     valid-looking CRC over its truncated self and would be accepted downstream.
//   - Partial application is never allowed. ParseCommand() either fully validates a
//     line - length, prefix, character set, CRC, name and every argument - and fills
//     the output, or it touches nothing and returns an error.
//   - The CRC covers the payload only: every byte before the single space that
//     precedes the four-hex-digit CRC field.
//
// The functions here are also what the Python host is tested against: the same
// golden cases in tests/vectors/protocol_cases.json drive both, so a framing
// disagreement between the two ends cannot survive the test suite.

#ifndef GRIDPULSE_PROTOCOL_H_
#define GRIDPULSE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse {

// --- event types (device -> host) ---------------------------------------------

enum class EventType : std::uint8_t {
  kHello,
  kMode,
  kSelfTest,
  kTarget,
  kHit,
  kMiss,
  kTick,
  kEnd,
  kHist,
  kLog,
};

// The wire token for an event type, e.g. "TARGET". Never null.
const char* EventTypeName(EventType type);

// --- commands (host -> device) --------------------------------------------------

enum class CommandName : std::uint8_t {
  kStart,
  kAbort,
  kSelfTest,
  kPing,
  kBright,
  kProto,
};

enum class RunMode : std::uint8_t {
  kPractice,
  kEval,
};

const char* CommandNameToken(CommandName name);
const char* RunModeToken(RunMode mode);

struct Command {
  CommandName name = CommandName::kPing;

  // START only. Defaulted to the scored mode is deliberately NOT done: an
  // unspecified mode is a parse error, so a malformed command can never silently
  // begin the one run that counts.
  RunMode mode = RunMode::kPractice;

  // SELFTEST only: retest cells already marked dead.
  bool force = false;

  // BRIGHT only: 0..100.
  std::uint8_t brightness_pct = 100;

  // ABORT only, and never from the wire: set when core 0 sees the host close the
  // port, as opposed to a person clicking CANCEL.
  //
  // The two want different things. A self-test with nobody watching is destructive -
  // it condemns a cell after two five-second timeouts and writes the result to flash,
  // so an unattended walk marks the whole board dead in about four minutes and the
  // device then refuses to start a run. A scored RUN with nobody watching is merely
  // pointless, and aborting it on a control-line change would let a USB quirk destroy
  // a real result. So this abort stops a walk and deliberately leaves a run alone.
  //
  // ParseCommand never sets it, which is what keeps the wire contract intact: the
  // host still has no way to ask for this.
  bool host_detached = false;
};

enum class ParseError : std::uint8_t {
  kOk,
  kEmpty,
  kTooLong,
  kBadPrefix,
  kBadChar,
  kMissingCrc,
  kMalformedCrc,
  kBadCrc,
  kUnknownName,
  kMissingArg,
  kBadArg,
};

// A short human-readable description, for log messages. Never null.
const char* ParseErrorText(ParseError error);

// --- line construction -----------------------------------------------------------

// Builds one protocol line into a fixed buffer.
//
// Usage:
//     char buf[kMaxLineBytes];
//     LineWriter w(buf, sizeof(buf));
//     w.BeginEvent(seq, t_us, EventType::kHit);
//     w.AddU32("cell", 13).AddU32("sc", 7);
//     const std::size_t n = w.Finish();   // 0 means it did not fit
//
// Every Add* call is a no-op once the writer has overflowed, so a caller may chain
// freely and check once at the end.
class LineWriter {
 public:
  LineWriter(char* buffer, std::size_t capacity);

  void BeginEvent(std::uint32_t seq, std::uint64_t t_us, EventType type);
  void BeginCommand(CommandName name);

  LineWriter& AddU32(const char* key, std::uint32_t value);
  LineWriter& AddU64(const char* key, std::uint64_t value);
  LineWriter& AddBool(const char* key, bool value);

  // Adds a token value. Any character outside printable ASCII, or a space or an '=',
  // is replaced with '_', because the protocol has no escaping and a raw space would
  // silently split one field into two.
  LineWriter& AddToken(const char* key, const char* value);

  // Adds a comma-separated list, e.g. the per-cell histogram. Callers chunk long
  // lists themselves so each line stays within kMaxLineBytes.
  LineWriter& AddU32Csv(const char* key, const std::uint32_t* values, std::size_t count);

  // Appends the CRC and the terminating newline. Returns the total byte count, or 0
  // if the line did not fit. The buffer is NUL-terminated on success.
  std::size_t Finish();

  bool overflowed() const { return overflowed_; }
  std::size_t length() const { return length_; }

 private:
  void AppendChar(char c);
  void AppendCString(const char* s);
  void AppendU64(std::uint64_t value);
  void BeginField(const char* key);

  char* buffer_;
  std::size_t capacity_;
  std::size_t length_ = 0;
  bool overflowed_ = false;
  bool started_ = false;
};

// --- line validation and parsing --------------------------------------------------

// Strips a trailing "\r\n" or "\n", verifies length and character set, locates the
// CRC field, and checks it.
//
// On success writes the payload length (everything before the space preceding the
// CRC) to `payload_length`. On failure `payload_length` is untouched.
ParseError VerifyLine(const char* line, std::size_t length, std::size_t* payload_length);

// Finds `key` in a payload's "key=value" fields.
//
// Returns true and points `value`/`value_length` at the value on success. Matching
// is exact and whole-key, so looking up "n" does not match "sc=1" or "seed=2".
bool FindField(const char* payload, std::size_t payload_length, const char* key,
               const char** value, std::size_t* value_length);

// Parses an unsigned decimal integer. Rejects an empty string, any non-digit, and
// any value that would overflow 64 bits.
bool ParseU64(const char* text, std::size_t length, std::uint64_t* out);
bool ParseU32(const char* text, std::size_t length, std::uint32_t* out);

// Fully validates and parses a host -> device command line.
//
// `out` is written only on ParseError::kOk.
ParseError ParseCommand(const char* line, std::size_t length, Command* out);

}  // namespace gridpulse

#endif  // GRIDPULSE_PROTOCOL_H_
