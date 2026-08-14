#include "protocol.h"

#include "config.h"
#include "crc.h"

namespace gridpulse {
namespace {

constexpr char kEventPrefix[] = "EV";
constexpr char kCommandPrefix[] = "CMD";
constexpr std::size_t kCrcHexDigits = 4;

constexpr char kHexDigits[] = "0123456789ABCDEF";

bool IsPrintableAscii(char c) {
  return c >= 0x20 && c <= 0x7E;
}

bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

// Length of a NUL-terminated string. Written out rather than pulling in <cstring>,
// which keeps this translation unit free of anything the host and the device might
// implement differently.
std::size_t CStringLength(const char* s) {
  std::size_t n = 0;
  while (s != nullptr && s[n] != '\0') {
    ++n;
  }
  return n;
}

bool SpanEquals(const char* span, std::size_t span_length, const char* literal) {
  const std::size_t literal_length = CStringLength(literal);
  if (span_length != literal_length) {
    return false;
  }
  for (std::size_t i = 0; i < span_length; ++i) {
    if (span[i] != literal[i]) {
      return false;
    }
  }
  return true;
}

bool HexNibble(char c, std::uint16_t* out) {
  if (c >= '0' && c <= '9') {
    *out = static_cast<std::uint16_t>(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = static_cast<std::uint16_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

}  // namespace

// --- names --------------------------------------------------------------------

const char* EventTypeName(EventType type) {
  switch (type) {
    case EventType::kHello:
      return "HELLO";
    case EventType::kMode:
      return "MODE";
    case EventType::kSelfTest:
      return "SELFTEST";
    case EventType::kTarget:
      return "TARGET";
    case EventType::kHit:
      return "HIT";
    case EventType::kMiss:
      return "MISS";
    case EventType::kTick:
      return "TICK";
    case EventType::kEnd:
      return "END";
    case EventType::kHist:
      return "HIST";
    case EventType::kLog:
      return "LOG";
  }
  return "UNKNOWN";
}

const char* CommandNameToken(CommandName name) {
  switch (name) {
    case CommandName::kStart:
      return "START";
    case CommandName::kAbort:
      return "ABORT";
    case CommandName::kSelfTest:
      return "SELFTEST";
    case CommandName::kPing:
      return "PING";
    case CommandName::kBright:
      return "BRIGHT";
    case CommandName::kProto:
      return "PROTO";
  }
  return "UNKNOWN";
}

const char* RunModeToken(RunMode mode) {
  return (mode == RunMode::kEval) ? "EVAL" : "PRACTICE";
}

const char* ParseErrorText(ParseError error) {
  switch (error) {
    case ParseError::kOk:
      return "ok";
    case ParseError::kEmpty:
      return "empty_line";
    case ParseError::kTooLong:
      return "line_too_long";
    case ParseError::kBadPrefix:
      return "bad_prefix";
    case ParseError::kBadChar:
      return "non_printable_character";
    case ParseError::kMissingCrc:
      return "missing_crc_field";
    case ParseError::kMalformedCrc:
      return "malformed_crc_field";
    case ParseError::kBadCrc:
      return "crc_mismatch";
    case ParseError::kUnknownName:
      return "unknown_command";
    case ParseError::kMissingArg:
      return "missing_argument";
    case ParseError::kBadArg:
      return "bad_argument";
  }
  return "unknown_error";
}

// --- LineWriter ------------------------------------------------------------------

LineWriter::LineWriter(char* buffer, std::size_t capacity)
    : buffer_(buffer), capacity_(capacity) {
  if (buffer_ == nullptr || capacity_ == 0) {
    overflowed_ = true;
  }
}

void LineWriter::AppendChar(char c) {
  if (overflowed_) {
    return;
  }
  // Reserve room for " XXXX\n\0": the CRC field, the terminator and the NUL.
  if (length_ + 1 + kCrcHexDigits + 2 > capacity_) {
    overflowed_ = true;
    return;
  }
  buffer_[length_++] = c;
}

void LineWriter::AppendCString(const char* s) {
  if (s == nullptr) {
    return;
  }
  for (std::size_t i = 0; s[i] != '\0'; ++i) {
    AppendChar(s[i]);
  }
}

void LineWriter::AppendU64(std::uint64_t value) {
  // 2^64-1 is 20 digits.
  char digits[20];
  std::size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0 && count < sizeof(digits));
  while (count > 0) {
    AppendChar(digits[--count]);
  }
}

void LineWriter::BeginEvent(std::uint32_t seq, std::uint64_t t_us, EventType type) {
  length_ = 0;
  overflowed_ = (buffer_ == nullptr || capacity_ == 0);
  started_ = true;
  AppendCString(kEventPrefix);
  AppendChar(' ');
  AppendU64(seq);
  AppendChar(' ');
  AppendU64(t_us);
  AppendChar(' ');
  AppendCString(EventTypeName(type));
}

void LineWriter::BeginCommand(CommandName name) {
  length_ = 0;
  overflowed_ = (buffer_ == nullptr || capacity_ == 0);
  started_ = true;
  AppendCString(kCommandPrefix);
  AppendChar(' ');
  AppendCString(CommandNameToken(name));
}

void LineWriter::BeginField(const char* key) {
  AppendChar(' ');
  AppendCString(key);
  AppendChar('=');
}

LineWriter& LineWriter::AddU32(const char* key, std::uint32_t value) {
  BeginField(key);
  AppendU64(value);
  return *this;
}

LineWriter& LineWriter::AddU64(const char* key, std::uint64_t value) {
  BeginField(key);
  AppendU64(value);
  return *this;
}

LineWriter& LineWriter::AddBool(const char* key, bool value) {
  BeginField(key);
  AppendChar(value ? '1' : '0');
  return *this;
}

LineWriter& LineWriter::AddToken(const char* key, const char* value) {
  BeginField(key);
  if (value == nullptr || value[0] == '\0') {
    // An empty value would produce "key=" and parse as a zero-length token, which is
    // ambiguous with a missing field. Emit a placeholder instead.
    AppendChar('-');
    return *this;
  }
  for (std::size_t i = 0; value[i] != '\0'; ++i) {
    const char c = value[i];
    // The protocol has no escaping, so a space or an '=' inside a value would split
    // one field into two. Substitute rather than corrupt the line.
    AppendChar((!IsPrintableAscii(c) || c == ' ' || c == '=') ? '_' : c);
  }
  return *this;
}

LineWriter& LineWriter::AddU32Csv(const char* key, const std::uint32_t* values,
                                  std::size_t count) {
  BeginField(key);
  if (values == nullptr || count == 0) {
    AppendChar('-');
    return *this;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) {
      AppendChar(',');
    }
    AppendU64(values[i]);
  }
  return *this;
}

std::size_t LineWriter::Finish() {
  if (overflowed_ || !started_ || length_ == 0) {
    return 0;
  }
  const std::uint16_t crc = Crc16CcittFalse(buffer_, length_);

  // AppendChar reserves exactly this much room, so these cannot overflow. Written
  // directly rather than through AppendChar so the reservation logic is not applied
  // to the reserved bytes themselves.
  buffer_[length_++] = ' ';
  buffer_[length_++] = kHexDigits[(crc >> 12) & 0xF];
  buffer_[length_++] = kHexDigits[(crc >> 8) & 0xF];
  buffer_[length_++] = kHexDigits[(crc >> 4) & 0xF];
  buffer_[length_++] = kHexDigits[crc & 0xF];
  buffer_[length_++] = '\n';
  buffer_[length_] = '\0';

  started_ = false;
  return length_;
}

// --- validation and parsing --------------------------------------------------------

ParseError VerifyLine(const char* line, std::size_t length, std::size_t* payload_length) {
  if (line == nullptr) {
    return ParseError::kEmpty;
  }
  // Strip the terminator, tolerating CRLF as docs/PROTOCOL.md section 1 requires.
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    --length;
  }
  if (length == 0) {
    return ParseError::kEmpty;
  }
  if (length + 1 > kMaxLineBytes) {
    return ParseError::kTooLong;
  }
  for (std::size_t i = 0; i < length; ++i) {
    if (!IsPrintableAscii(line[i])) {
      return ParseError::kBadChar;
    }
  }

  // The CRC field is the last kCrcHexDigits characters, preceded by a space.
  if (length < kCrcHexDigits + 2) {
    return ParseError::kMissingCrc;
  }
  const std::size_t crc_start = length - kCrcHexDigits;
  if (line[crc_start - 1] != ' ') {
    return ParseError::kMissingCrc;
  }

  std::uint16_t expected = 0;
  for (std::size_t i = 0; i < kCrcHexDigits; ++i) {
    std::uint16_t nibble = 0;
    if (!HexNibble(line[crc_start + i], &nibble)) {
      return ParseError::kMalformedCrc;
    }
    expected = static_cast<std::uint16_t>((expected << 4) | nibble);
  }

  const std::size_t payload = crc_start - 1;
  if (Crc16CcittFalse(line, payload) != expected) {
    return ParseError::kBadCrc;
  }

  if (payload_length != nullptr) {
    *payload_length = payload;
  }
  return ParseError::kOk;
}

bool FindField(const char* payload, std::size_t payload_length, const char* key,
               const char** value, std::size_t* value_length) {
  if (payload == nullptr || key == nullptr) {
    return false;
  }
  const std::size_t key_length = CStringLength(key);
  if (key_length == 0) {
    return false;
  }

  std::size_t i = 0;
  while (i < payload_length) {
    // Advance to the start of a field: the character after a space.
    while (i < payload_length && payload[i] == ' ') {
      ++i;
    }
    const std::size_t field_start = i;
    while (i < payload_length && payload[i] != ' ') {
      ++i;
    }
    const std::size_t field_length = i - field_start;
    if (field_length == 0) {
      continue;
    }

    // Split on the FIRST '=' so a value containing one cannot shift the key.
    std::size_t eq = field_start;
    while (eq < field_start + field_length && payload[eq] != '=') {
      ++eq;
    }
    if (eq >= field_start + field_length) {
      continue;  // a bare token, not a key=value field
    }
    const std::size_t this_key_length = eq - field_start;
    if (this_key_length == key_length &&
        SpanEquals(payload + field_start, this_key_length, key)) {
      if (value != nullptr) {
        *value = payload + eq + 1;
      }
      if (value_length != nullptr) {
        *value_length = field_start + field_length - (eq + 1);
      }
      return true;
    }
  }
  return false;
}

bool ParseU64(const char* text, std::size_t length, std::uint64_t* out) {
  if (text == nullptr || length == 0 || out == nullptr) {
    return false;
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < length; ++i) {
    if (!IsDigit(text[i])) {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(text[i] - '0');
    // Overflow check before multiplying, so a long digit string is rejected rather
    // than silently wrapping to a small plausible number.
    if (value > (UINT64_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }
  *out = value;
  return true;
}

bool ParseU32(const char* text, std::size_t length, std::uint32_t* out) {
  std::uint64_t wide = 0;
  if (!ParseU64(text, length, &wide) || wide > UINT32_MAX || out == nullptr) {
    return false;
  }
  *out = static_cast<std::uint32_t>(wide);
  return true;
}

ParseError ParseCommand(const char* line, std::size_t length, Command* out) {
  if (out == nullptr) {
    return ParseError::kBadArg;
  }

  std::size_t payload_length = 0;
  const ParseError framing = VerifyLine(line, length, &payload_length);
  if (framing != ParseError::kOk) {
    return framing;
  }

  // Prefix must be exactly "CMD ".
  const std::size_t prefix_length = CStringLength(kCommandPrefix);
  if (payload_length < prefix_length + 1 ||
      !SpanEquals(line, prefix_length, kCommandPrefix) || line[prefix_length] != ' ') {
    return ParseError::kBadPrefix;
  }

  std::size_t name_start = prefix_length + 1;
  std::size_t name_end = name_start;
  while (name_end < payload_length && line[name_end] != ' ') {
    ++name_end;
  }
  const std::size_t name_length = name_end - name_start;
  if (name_length == 0) {
    return ParseError::kUnknownName;
  }

  // Build into a local and commit only on success, so a malformed argument cannot
  // leave the caller's Command half-updated.
  Command parsed;
  const char* span = line + name_start;

  if (SpanEquals(span, name_length, "START")) {
    parsed.name = CommandName::kStart;
    const char* value = nullptr;
    std::size_t value_length = 0;
    if (!FindField(line, payload_length, "mode", &value, &value_length)) {
      // Deliberately not defaulted. An unspecified mode must never silently start
      // the one run that counts.
      return ParseError::kMissingArg;
    }
    if (SpanEquals(value, value_length, "EVAL")) {
      parsed.mode = RunMode::kEval;
    } else if (SpanEquals(value, value_length, "PRACTICE")) {
      parsed.mode = RunMode::kPractice;
    } else {
      return ParseError::kBadArg;
    }
  } else if (SpanEquals(span, name_length, "ABORT")) {
    parsed.name = CommandName::kAbort;
  } else if (SpanEquals(span, name_length, "SELFTEST")) {
    parsed.name = CommandName::kSelfTest;
    const char* value = nullptr;
    std::size_t value_length = 0;
    if (FindField(line, payload_length, "force", &value, &value_length)) {
      if (SpanEquals(value, value_length, "1")) {
        parsed.force = true;
      } else if (SpanEquals(value, value_length, "0")) {
        parsed.force = false;
      } else {
        return ParseError::kBadArg;
      }
    }
  } else if (SpanEquals(span, name_length, "PING")) {
    parsed.name = CommandName::kPing;
  } else if (SpanEquals(span, name_length, "PROTO")) {
    parsed.name = CommandName::kProto;
  } else if (SpanEquals(span, name_length, "BRIGHT")) {
    parsed.name = CommandName::kBright;
    const char* value = nullptr;
    std::size_t value_length = 0;
    if (!FindField(line, payload_length, "pct", &value, &value_length)) {
      return ParseError::kMissingArg;
    }
    std::uint32_t pct = 0;
    if (!ParseU32(value, value_length, &pct) || pct > 100) {
      return ParseError::kBadArg;
    }
    parsed.brightness_pct = static_cast<std::uint8_t>(pct);
  } else {
    return ParseError::kUnknownName;
  }

  *out = parsed;
  return ParseError::kOk;
}

}  // namespace gridpulse
