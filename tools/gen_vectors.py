#!/usr/bin/env python3
"""Generate the golden test vectors from the Python reference implementation.

Writes:
    tests/vectors/sequences.json       RNG + sampler vectors
    tests/vectors/scoring_cases.json   bit-rate scoring scenarios
    tests/vectors/crc_cases.json       CRC16/CCITT-FALSE cases
    tests/vectors/protocol_cases.json  wire framing and command parsing
    tests/vectors/vectors.gen.h        the same data as C++ constexpr tables
    web/vectors.gen.js                 the same data as a classic-script global

The JSON files are the readable, reviewable artefact. The two mirrors exist because
neither consumer can read JSON where it runs: a ``file://`` origin cannot ``fetch``,
and the native C++ test target has no JSON parser by design - a parser would be one
more thing to get wrong sitting between the tests and the data they check.

The ``.js`` mirror embeds a SHA-256 of each JSON source, and ``--check`` recomputes
every output, so a stale mirror fails the test suite instead of passing it.

Usage:
    python3 tools/gen_vectors.py            regenerate everything
    python3 tools/gen_vectors.py --check    verify on-disk files are current (exit 1 if not)

Standard library only.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
import sys
from typing import Any, Dict, List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import refimpl as ref  # noqa: E402  (path set up immediately above)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VECTOR_DIR = os.path.join(REPO_ROOT, "tests", "vectors")
WEB_VECTORS_JS = os.path.join(REPO_ROOT, "web", "vectors.gen.js")
CPP_VECTORS_H = os.path.join(VECTOR_DIR, "vectors.gen.h")

SEEDS = [0x00000000, 0x00000001, 0xDEADBEEF, 0x5A5A5A5A, 0xFFFFFFFF]
ALPHABET_SIZES = [3, 24, 25]
DRAW_COUNT = 10000
PREFIX_LEN = 256
RAW_LEN = 16


def _canonical_indices(indices: List[int]) -> bytes:
    """Canonical rendering of a full index sequence: comma-joined decimal, UTF-8."""
    return ",".join(str(i) for i in indices).encode("utf-8")


def _sha256_indices(indices: List[int]) -> str:
    return hashlib.sha256(_canonical_indices(indices)).hexdigest()


def _crc32_indices(indices: List[int]) -> int:
    """CRC-32 (zlib/PKZIP) over the same canonical rendering.

    binascii.crc32 uses exactly the parameters implemented in
    firmware/src/pure/crc.cpp, so the C++ suite can verify a full 10 000-draw
    sequence using nothing but production code.
    """
    return binascii.crc32(_canonical_indices(indices)) & 0xFFFFFFFF


def build_sequences() -> Dict[str, Any]:
    cases = []
    for seed in SEEDS:
        for n in ALPHABET_SIZES:
            drawn = ref.draw_sequence(seed, n, DRAW_COUNT)
            indices = drawn["indices"]
            cases.append(
                {
                    "name": "seed_0x%08X_n%d" % (seed, n),
                    "seed": seed,
                    "n": n,
                    "draw_count": DRAW_COUNT,
                    "raw16": ref.raw_outputs(seed, RAW_LEN),
                    "prefix": indices[:PREFIX_LEN],
                    "sha256": _sha256_indices(indices),
                    # Same canonical rendering as sha256, hashed with the CRC-32 the
                    # firmware already implements. The C++ suite checks this instead
                    # of SHA-256 so it reuses production code rather than carrying a
                    # second hash implementation purely for tests.
                    "crc32": _crc32_indices(indices),
                    "rejections": drawn["rejections"],
                    "state_after": drawn["state_after"],
                    "histogram": drawn["histogram"],
                }
            )
    return {
        "spec_version": ref.SPEC_VERSION,
        "description": "xoshiro128** + SplitMix32 seeding + rejection sampling",
        "prefix_len": PREFIX_LEN,
        "cases": cases,
    }


def build_scoring_cases() -> Dict[str, Any]:
    # (name, N, Sc, Si, t_seconds, note)
    raw = [
        ("zero_time_guard", 25, 10, 0, 0.0, "t <= 0 must return 0, not divide by zero"),
        ("negative_time_guard", 25, 10, 0, -1.0, "defensive: negative t returns 0"),
        ("no_events", 25, 0, 0, 60.0, "nothing pressed scores 0"),
        ("si_exceeds_sc", 25, 10, 20, 60.0, "max(Sc-Si,0) clamps to exactly 0"),
        ("si_equals_sc", 25, 10, 10, 60.0, "net zero scores exactly 0"),
        ("si_one_less", 25, 11, 10, 60.0, "net one is the smallest positive score"),
        ("n_below_min", 2, 50, 0, 60.0, "N < 3 is not a scorable session"),
        ("n_min_three", 3, 60, 0, 60.0, "log2(2)=1 bit per selection -> exactly 1.0 bps"),
        ("n_three_with_misses", 3, 60, 15, 60.0, "N=3 with misses"),
        ("n_24_selftest_reduced", 24, 200, 10, 60.0, "one dead cell excluded by SELFTEST"),
        ("n_25_nominal", 25, 200, 10, 60.0, "the expected shape of a good run"),
        ("n_25_perfect_fast", 25, 300, 0, 60.0, "5 presses/sec, no misses"),
        ("n_25_sloppy_fast", 25, 300, 100, 60.0, "fast but inaccurate; misses cost double"),
        ("n_25_slow_accurate", 25, 120, 0, 60.0, "2 presses/sec, no misses"),
        ("n_25_single_hit", 25, 1, 0, 60.0, "one hit in the whole window"),
        ("n_25_one_second", 25, 4, 0, 1.0, "early in the run, small t"),
        ("n_25_sub_second", 25, 1, 0, 0.25, "first hit lands very early"),
        ("n_25_long_session", 25, 1000, 50, 600.0, "practice-length session"),
        ("n_25_partial_window", 25, 137, 9, 42.5, "non-integer elapsed time"),
        ("rounding_half_up", 3, 1, 0, 2.0, "B=0.5 exactly -> 500 mbps"),
        ("rounding_tiny", 25, 1, 0, 4584.962500721156, "B ~= 0.001 -> rounding boundary"),
        ("high_rate", 25, 480, 0, 60.0, "8 presses/sec, no misses"),
        ("n_25_all_misses", 25, 0, 50, 60.0, "only misses scores 0"),
        ("n_25_equal_large", 25, 500, 500, 60.0, "large equal counts still exactly 0"),
    ]
    cases = []
    for name, n, sc, si, t, note in raw:
        cases.append(
            {
                "name": name,
                "n": n,
                "correct": sc,
                "incorrect": si,
                "elapsed_s": t,
                "note": note,
                "bit_rate": ref.bit_rate(n, sc, si, t),
                "b_mbps": ref.bit_rate_mbps(n, sc, si, t),
                "bits_per_selection": ref.bits_per_selection(n),
            }
        )
    return {"spec_version": ref.SPEC_VERSION, "cases": cases}


def build_crc_cases() -> Dict[str, Any]:
    inputs = [
        ("canonical_check_value", "123456789"),
        ("empty", ""),
        ("single_byte_A", "A"),
        ("ev_hello", "EV 1 0 HELLO proto=1 fw=1.0.0 board=gridpulse-5x5"),
        ("ev_target", "EV 42 1234567 TARGET cell=13 idx=7 repeat=0"),
        ("ev_hit", "EV 43 1240000 HIT cell=13 rt_us=213000 sc=7 si=1 streak=3"),
        ("ev_miss", "EV 44 1250000 MISS pressed=9 target=13 sc=7 si=2"),
        ("ev_end", "EV 900 60000000 END n=25 sc=241 si=12 t_us=60000000 b_mbps=17494"),
        ("cmd_start", "CMD START EVAL N=25"),
        ("cmd_abort", "CMD ABORT"),
    ]
    cases = [
        {"name": name, "input": text, "crc16": ref.crc16_ccitt_false(text.encode("ascii"))}
        for name, text in inputs
    ]

    # Single-bit-flip detection: flipping any one bit must change the CRC.
    base = "EV 42 1234567 TARGET cell=13 idx=7 repeat=0"
    base_crc = ref.crc16_ccitt_false(base.encode("ascii"))
    flips = []
    for byte_index in (0, 3, 14, len(base) - 1):
        for bit in (0, 7):
            mutated = bytearray(base.encode("ascii"))
            mutated[byte_index] ^= 1 << bit
            flips.append(
                {
                    "byte_index": byte_index,
                    "bit": bit,
                    "mutated": mutated.decode("latin-1"),
                    "crc16": ref.crc16_ccitt_false(bytes(mutated)),
                }
            )
    return {
        "spec_version": ref.SPEC_VERSION,
        "cases": cases,
        "bit_flip_base": {"input": base, "crc16": base_crc},
        "bit_flips": flips,
    }


def _frame(payload: str) -> str:
    """Wrap a payload in the protocol's CRC framing: `<payload> <CRC16>`."""
    return "%s %04X" % (payload, ref.crc16_ccitt_false(payload.encode("ascii")))


def build_protocol_cases() -> Dict[str, Any]:
    """Framing and command-parsing cases for docs/PROTOCOL.md v1.

    Every malformed case is CONSTRUCTED to exhibit exactly one defect, rather than
    produced by arbitrary corruption and then guessed at. That way the expected error
    follows from how the case was built, and a disagreement between the C++ parser and
    this file is a real disagreement about the spec rather than an artefact.
    """
    ok_cases = [
        ("ping", "CMD PING", {"name": "PING"}),
        ("proto", "CMD PROTO", {"name": "PROTO"}),
        ("abort", "CMD ABORT", {"name": "ABORT"}),
        ("start_eval", "CMD START mode=EVAL", {"name": "START", "mode": "EVAL"}),
        ("start_practice", "CMD START mode=PRACTICE",
         {"name": "START", "mode": "PRACTICE"}),
        ("selftest_default", "CMD SELFTEST", {"name": "SELFTEST", "force": False}),
        ("selftest_force", "CMD SELFTEST force=1", {"name": "SELFTEST", "force": True}),
        ("selftest_no_force", "CMD SELFTEST force=0",
         {"name": "SELFTEST", "force": False}),
        ("bright_zero", "CMD BRIGHT pct=0", {"name": "BRIGHT", "brightness_pct": 0}),
        ("bright_mid", "CMD BRIGHT pct=50", {"name": "BRIGHT", "brightness_pct": 50}),
        ("bright_max", "CMD BRIGHT pct=100", {"name": "BRIGHT", "brightness_pct": 100}),
    ]

    cases = []
    for name, payload, expected in ok_cases:
        cases.append(
            {
                "name": name,
                "line": _frame(payload),
                "error": "ok",
                "command": expected,
                "note": "well-formed",
            }
        )

    # Carriage returns must be tolerated: a host may send CRLF.
    cases.append(
        {
            "name": "crlf_tolerated",
            "line": _frame("CMD PING") + "\r\n",
            "error": "ok",
            "command": {"name": "PING"},
            "note": "trailing CRLF is stripped before validation",
        }
    )
    cases.append(
        {
            "name": "lf_tolerated",
            "line": _frame("CMD PING") + "\n",
            "error": "ok",
            "command": {"name": "PING"},
            "note": "trailing LF is stripped before validation",
        }
    )

    def bad(name, line, error, note):
        cases.append({"name": name, "line": line, "error": error,
                      "command": None, "note": note})

    # --- argument errors (framing is valid; the payload is not) ---
    bad("start_without_mode", _frame("CMD START"), "missing_argument",
        "an unspecified mode must never silently start the scored run")
    bad("start_bad_mode", _frame("CMD START mode=BOGUS"), "bad_argument",
        "mode must be EVAL or PRACTICE")
    bad("start_empty_mode", _frame("CMD START mode="), "bad_argument",
        "an empty value is not a valid mode")
    bad("selftest_bad_force", _frame("CMD SELFTEST force=2"), "bad_argument",
        "force is strictly 0 or 1")
    bad("bright_without_pct", _frame("CMD BRIGHT"), "missing_argument",
        "pct is required")
    bad("bright_out_of_range", _frame("CMD BRIGHT pct=101"), "bad_argument",
        "pct is 0..100")
    bad("bright_not_a_number", _frame("CMD BRIGHT pct=abc"), "bad_argument",
        "non-digit value")
    bad("bright_negative", _frame("CMD BRIGHT pct=-5"), "bad_argument",
        "the wire format is unsigned decimal only")
    bad("bright_overflow", _frame("CMD BRIGHT pct=99999999999999999999999"),
        "bad_argument", "a value too large for 64 bits must be rejected, not wrapped")
    bad("unknown_command", _frame("CMD FLY"), "unknown_command",
        "unrecognised command name")
    bad("empty_command_name", _frame("CMD "), "unknown_command",
        "no command name at all")

    # --- prefix errors ---
    bad("event_line_to_command_parser",
        _frame("EV 1 118293 HELLO proto=1 fw=1.0.0"), "bad_prefix",
        "a device->host event is not a command")
    bad("lowercase_prefix", _frame("cmd PING"), "bad_prefix",
        "the prefix is case-sensitive")
    bad("no_prefix", _frame("PING"), "bad_prefix", "missing CMD prefix")

    # --- framing errors ---
    bad("empty_line", "", "empty_line", "nothing at all")
    bad("newline_only", "\n", "empty_line", "terminator with no payload")
    # "CMD PING" has no CRC, but its last four characters ARE preceded by a space, so
    # the field is structurally present and simply not hexadecimal. Either rejection
    # is safe; the parser reports the more specific one.
    bad("no_crc_field", "CMD PING", "malformed_crc_field",
        "no CRC appended: the trailing four characters occupy the CRC position but "
        "are not hex")
    bad("crc_not_hex", "CMD PING ZZZZ", "malformed_crc_field",
        "constructed with four non-hex characters where the CRC belongs")
    bad("crc_wrong_length", "CMD PING AB", "missing_crc_field",
        "constructed with a two-character CRC field")

    # A valid line with exactly one payload byte altered. The framing is intact, so
    # this must fail on the CRC and nowhere else - which is the whole point of the CRC.
    valid = _frame("CMD START mode=EVAL")
    mutated = "CMD START mode=EVAI" + valid[len("CMD START mode=EVAL"):]
    bad("single_character_corruption", mutated, "crc_mismatch",
        "one payload character changed; the CRC must catch it")

    flipped = bytearray(valid.encode("ascii"))
    flipped[4] ^= 0x01
    bad("single_bit_flip", flipped.decode("latin-1"), "crc_mismatch",
        "one payload bit flipped; the CRC must catch it")

    bad("crc_of_a_different_line", "CMD PING " + _frame("CMD ABORT")[-4:],
        "crc_mismatch", "a valid CRC, but for different content")

    # Over-length: constructed past kMaxLineBytes (256).
    long_payload = "CMD BRIGHT pct=50 pad=" + ("x" * 300)
    bad("over_length", _frame(long_payload), "line_too_long",
        "longer than kMaxLineBytes; must be dropped, never truncated")

    # Non-printable characters are outside the permitted 0x20-0x7E range.
    bad("embedded_nul", "CMD PI\x00NG " + "0000", "non_printable_character",
        "NUL inside the payload")
    bad("embedded_tab", "CMD\tPING " + "0000", "non_printable_character",
        "tab inside the payload")
    bad("high_bit_set", "CMD PING\x80 " + "0000", "non_printable_character",
        "byte above 0x7E")

    # --- boundary: the longest line that is still legal ---
    # kMaxLineBytes is 320 including the terminator, so the longest valid line is 319
    # characters plus '\n'. Built to land exactly on that boundary.
    pad_length = 319 - len(_frame("CMD BRIGHT pct=50 pad="))
    boundary_payload = "CMD BRIGHT pct=50 pad=" + ("x" * pad_length)
    cases.append(
        {
            "name": "exactly_at_length_limit",
            "line": _frame(boundary_payload),
            "error": "ok",
            "command": {"name": "BRIGHT", "brightness_pct": 50},
            "note": "319 characters plus the terminator is the longest legal line",
        }
    )

    return {
        "spec_version": ref.SPEC_VERSION,
        "protocol_version": 1,
        "max_line_bytes": 320,
        "cases": cases,
    }


def _dump(obj: Any) -> str:
    return json.dumps(obj, indent=2, sort_keys=False) + "\n"


def _js_mirror(payloads: Dict[str, str]) -> str:
    """Build web/vectors.gen.js from the already-serialised JSON payloads."""
    lines = [
        "// GENERATED FILE - do not edit by hand.",
        "// Regenerate with: python3 tools/gen_vectors.py",
        "//",
        "// A file:// origin cannot fetch() a JSON file, and Mode B must be playable and",
        "// testable straight off the filesystem, so the golden vectors are mirrored here",
        "// as a classic script that assigns a global. The digests below are SHA-256 of",
        "// the corresponding tests/vectors/*.json bytes; tools/gen_vectors.py --check",
        "// compares them so a stale mirror fails the test suite instead of passing it.",
        "'use strict';",
        "var GRID_PULSE_VECTORS = (function () {",
    ]
    digests = {}
    for key, text in sorted(payloads.items()):
        digests[key] = hashlib.sha256(text.encode("utf-8")).hexdigest()
        lines.append("  var %s = %s;" % (key, text.strip()))
    lines.append("  return {")
    for key in sorted(payloads):
        lines.append("    %s: %s," % (key, key))
    lines.append("    digests: %s," % json.dumps(digests, indent=6, sort_keys=True).replace("\n", "\n    "))
    lines.append("  };")
    lines.append("})();")
    lines.append("")
    lines.append("if (typeof module !== 'undefined' && module.exports) {")
    lines.append("  module.exports = GRID_PULSE_VECTORS;")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def _c_escape(raw: str) -> str:
    """Escape a string for a C++ string literal, byte by byte.

    Protocol cases deliberately contain NUL, tab and bytes above 0x7E, so this must
    handle arbitrary bytes rather than assuming printable ASCII. Hex escapes are
    always followed by a string-literal break, because `"\\x0" "0"` is well defined
    while `"\\x00"` followed by a digit would swallow it into the escape.
    """
    out = []
    for ch in raw:
        code = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append('\\t""')
        elif 0x20 <= code <= 0x7E:
            out.append(ch)
        else:
            out.append('\\x%02X""' % code)
    return "".join(out)


def _cpp_array(name: str, values: List[int]) -> str:
    body = ", ".join(str(v) + "u" for v in values)
    return "inline constexpr std::uint32_t %s[] = {%s};" % (name, body)


def _cpp_header(sequences: Dict[str, Any], scoring: Dict[str, Any],
                crc: Dict[str, Any], protocol: Dict[str, Any]) -> str:
    """Emit the golden vectors as a C++ header.

    The native test target has no JSON parser and should not grow one: a parser would
    be a second thing to get wrong sitting between the tests and the data they check.
    Generating constexpr tables instead means the vectors are compiled in, cannot
    drift from the JSON, and cost nothing at run time.
    """
    lines = [
        "// GENERATED FILE - do not edit by hand.",
        "// Regenerate with: python3 tools/gen_vectors.py",
        "//",
        "// The golden vectors from tests/vectors/*.json, as compile-time tables. The",
        "// native test target has no JSON parser by design; see tools/gen_vectors.py.",
        "",
        "#ifndef GRIDPULSE_TEST_VECTORS_GEN_H_",
        "#define GRIDPULSE_TEST_VECTORS_GEN_H_",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace gridpulse_vectors {",
        "",
        'inline constexpr const char* kSpecVersion = "%s";' % sequences["spec_version"],
        "inline constexpr std::size_t kPrefixLen = %d;" % sequences["prefix_len"],
        "inline constexpr std::uint32_t kProtocolVersion = %d;"
        % protocol["protocol_version"],
        "inline constexpr std::size_t kMaxLineBytes = %d;" % protocol["max_line_bytes"],
        "",
        "// --- RNG and sampler ---------------------------------------------------",
        "",
        "struct SequenceCase {",
        "  const char* name;",
        "  std::uint32_t seed;",
        "  std::uint32_t n;",
        "  std::uint32_t draw_count;",
        "  const std::uint32_t* raw16;",
        "  std::size_t raw16_count;",
        "  const std::uint32_t* prefix;",
        "  std::size_t prefix_count;",
        "  std::uint32_t crc32;          // over all draw_count indices, comma-joined",
        "  std::uint32_t rejections;",
        "  const std::uint32_t* state_after;",
        "  const std::uint32_t* histogram;",
        "  std::size_t histogram_count;",
        "};",
        "",
    ]

    for i, case in enumerate(sequences["cases"]):
        lines.append(_cpp_array("kSeqRaw%d" % i, case["raw16"]))
        lines.append(_cpp_array("kSeqPrefix%d" % i, case["prefix"]))
        lines.append(_cpp_array("kSeqState%d" % i, case["state_after"]))
        lines.append(_cpp_array("kSeqHist%d" % i, case["histogram"]))
    lines.append("")

    lines.append("inline constexpr SequenceCase kSequenceCases[] = {")
    for i, case in enumerate(sequences["cases"]):
        lines.append(
            '    {"%s", %du, %du, %du, kSeqRaw%d, %d, kSeqPrefix%d, %d, '
            "0x%08Xu, %du, kSeqState%d, kSeqHist%d, %d},"
            % (case["name"], case["seed"], case["n"], case["draw_count"], i,
               len(case["raw16"]), i, len(case["prefix"]), case["crc32"],
               case["rejections"], i, i, len(case["histogram"]))
        )
    lines.append("};")
    lines.append("inline constexpr std::size_t kSequenceCaseCount = %d;"
                 % len(sequences["cases"]))
    lines.append("")

    lines += [
        "// --- scoring -----------------------------------------------------------",
        "",
        "struct ScoringCase {",
        "  const char* name;",
        "  const char* note;",
        "  std::uint32_t n;",
        "  std::uint32_t correct;",
        "  std::uint32_t incorrect;",
        "  double elapsed_s;",
        "  double bit_rate;",
        "  std::uint32_t b_mbps;",
        "  double bits_per_selection;",
        "};",
        "",
        "inline constexpr ScoringCase kScoringCases[] = {",
    ]
    for case in scoring["cases"]:
        lines.append(
            '    {"%s", "%s", %du, %du, %du, %s, %s, %du, %s},'
            % (case["name"], _c_escape(case["note"]), case["n"], case["correct"],
               case["incorrect"], repr(float(case["elapsed_s"])),
               repr(float(case["bit_rate"])), case["b_mbps"],
               repr(float(case["bits_per_selection"])))
        )
    lines.append("};")
    lines.append("inline constexpr std::size_t kScoringCaseCount = %d;"
                 % len(scoring["cases"]))
    lines.append("")

    lines += [
        "// --- CRC ---------------------------------------------------------------",
        "",
        "struct CrcCase {",
        "  const char* name;",
        "  const char* input;",
        "  std::size_t input_length;",
        "  std::uint32_t crc16;",
        "};",
        "",
        "inline constexpr CrcCase kCrcCases[] = {",
    ]
    for case in crc["cases"]:
        lines.append(
            '    {"%s", "%s", %d, 0x%04Xu},'
            % (case["name"], _c_escape(case["input"]), len(case["input"]),
               case["crc16"])
        )
    lines.append("};")
    lines.append("inline constexpr std::size_t kCrcCaseCount = %d;"
                 % len(crc["cases"]))
    lines.append("")

    lines.append("// Single-bit-flip detection cases, all derived from one base line.")
    lines.append('inline constexpr const char* kCrcFlipBase = "%s";'
                 % _c_escape(crc["bit_flip_base"]["input"]))
    lines.append("inline constexpr std::size_t kCrcFlipBaseLength = %d;"
                 % len(crc["bit_flip_base"]["input"]))
    lines.append("inline constexpr std::uint32_t kCrcFlipBaseCrc = 0x%04Xu;"
                 % crc["bit_flip_base"]["crc16"])
    lines.append("")
    lines.append("struct CrcFlipCase {")
    lines.append("  std::size_t byte_index;")
    lines.append("  int bit;")
    lines.append("  std::uint32_t crc16;")
    lines.append("};")
    lines.append("inline constexpr CrcFlipCase kCrcFlipCases[] = {")
    for flip in crc["bit_flips"]:
        lines.append("    {%d, %d, 0x%04Xu},"
                     % (flip["byte_index"], flip["bit"], flip["crc16"]))
    lines.append("};")
    lines.append("inline constexpr std::size_t kCrcFlipCaseCount = %d;"
                 % len(crc["bit_flips"]))
    lines.append("")

    lines += [
        "// --- protocol ----------------------------------------------------------",
        "",
        "struct ProtocolCase {",
        "  const char* name;",
        "  const char* note;",
        "  const char* line;",
        "  std::size_t line_length;",
        "  const char* error;         // matches ParseErrorText()",
        "  bool ok;",
        "  const char* command;       // null unless ok",
        "  const char* mode;          // START only, else null",
        "  int force;                 // SELFTEST only, else -1",
        "  int brightness_pct;        // BRIGHT only, else -1",
        "};",
        "",
        "inline constexpr ProtocolCase kProtocolCases[] = {",
    ]
    for case in protocol["cases"]:
        command = case["command"]
        ok = case["error"] == "ok"
        name = ("nullptr" if command is None
                else '"%s"' % command["name"])
        mode = ("nullptr" if not command or "mode" not in command
                else '"%s"' % command["mode"])
        force = (-1 if not command or "force" not in command
                 else (1 if command["force"] else 0))
        pct = (-1 if not command or "brightness_pct" not in command
               else command["brightness_pct"])
        lines.append(
            '    {"%s", "%s", "%s", %d, "%s", %s, %s, %s, %d, %d},'
            % (case["name"], _c_escape(case["note"]), _c_escape(case["line"]),
               len(case["line"]), case["error"], "true" if ok else "false",
               name, mode, force, pct)
        )
    lines.append("};")
    lines.append("inline constexpr std::size_t kProtocolCaseCount = %d;"
                 % len(protocol["cases"]))
    lines.append("")
    lines.append("}  // namespace gridpulse_vectors")
    lines.append("")
    lines.append("#endif  // GRIDPULSE_TEST_VECTORS_GEN_H_")
    lines.append("")
    return "\n".join(lines)


def generate() -> Dict[str, str]:
    """Return a mapping of absolute path -> file contents."""
    sequences = build_sequences()
    scoring = build_scoring_cases()
    crc = build_crc_cases()
    protocol = build_protocol_cases()

    payloads = {
        "sequences": _dump(sequences),
        "scoring_cases": _dump(scoring),
        "crc_cases": _dump(crc),
        "protocol_cases": _dump(protocol),
    }
    files = {
        os.path.join(VECTOR_DIR, "%s.json" % key): text for key, text in payloads.items()
    }
    files[WEB_VECTORS_JS] = _js_mirror(payloads)
    files[CPP_VECTORS_H] = _cpp_header(sequences, scoring, crc, protocol)
    return files


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify on-disk files match what would be generated; exit 1 if not",
    )
    args = parser.parse_args(argv)

    files = generate()

    if args.check:
        stale = []
        for path, want in sorted(files.items()):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    have = handle.read()
            except OSError as exc:
                stale.append("%s: cannot read (%s)" % (os.path.relpath(path, REPO_ROOT), exc))
                continue
            if have != want:
                stale.append("%s: out of date" % os.path.relpath(path, REPO_ROOT))
        if stale:
            for line in stale:
                print("STALE  %s" % line, file=sys.stderr)
            print(
                "\nGolden vectors are stale. Run: python3 tools/gen_vectors.py",
                file=sys.stderr,
            )
            return 1
        print("vectors up to date (%d files)" % len(files))
        return 0

    for path, text in sorted(files.items()):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        print("wrote %s (%d bytes)" % (os.path.relpath(path, REPO_ROOT), len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
