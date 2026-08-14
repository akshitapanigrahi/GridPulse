// GENERATED FILE - do not edit by hand.
// Regenerate with: python3 tools/gen_vectors.py
//
// The golden vectors from tests/vectors/*.json, as compile-time tables. The
// native test target has no JSON parser by design; see tools/gen_vectors.py.

#ifndef GRIDPULSE_TEST_VECTORS_GEN_H_
#define GRIDPULSE_TEST_VECTORS_GEN_H_

#include <cstddef>
#include <cstdint>

namespace gridpulse_vectors {

inline constexpr const char* kSpecVersion = "1.0.0";
inline constexpr std::size_t kPrefixLen = 256;
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaxLineBytes = 320;

// --- RNG and sampler ---------------------------------------------------

struct SequenceCase {
  const char* name;
  std::uint32_t seed;
  std::uint32_t n;
  std::uint32_t draw_count;
  const std::uint32_t* raw16;
  std::size_t raw16_count;
  const std::uint32_t* prefix;
  std::size_t prefix_count;
  std::uint32_t crc32;          // over all draw_count indices, comma-joined
  std::uint32_t rejections;
  const std::uint32_t* state_after;
  const std::uint32_t* histogram;
  std::size_t histogram_count;
};

inline constexpr std::uint32_t kSeqRaw0[] = {1789933344u, 44971166u, 2521387044u, 3848737593u, 1138324114u, 749234105u, 1899511038u, 1995189375u, 3629653958u, 19166872u, 1711213692u, 48427069u, 3762117657u, 2857799264u, 4183157539u, 3474655728u};
inline constexpr std::uint32_t kSeqPrefix0[] = {0u, 2u, 0u, 0u, 1u, 2u, 0u, 0u, 2u, 1u, 0u, 1u, 0u, 2u, 1u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 2u, 0u, 2u, 0u, 0u, 2u, 1u, 0u, 2u, 2u, 1u, 2u, 2u, 2u, 1u, 1u, 2u, 0u, 1u, 2u, 0u, 1u, 0u, 2u, 2u, 2u, 1u, 2u, 0u, 1u, 1u, 0u, 0u, 2u, 2u, 1u, 0u, 0u, 1u, 0u, 2u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, 2u, 1u, 1u, 1u, 2u, 0u, 0u, 0u, 2u, 0u, 0u, 2u, 1u, 1u, 1u, 2u, 0u, 2u, 1u, 0u, 0u, 0u, 1u, 0u, 2u, 1u, 0u, 0u, 2u, 2u, 1u, 0u, 1u, 2u, 1u, 0u, 1u, 1u, 2u, 0u, 0u, 1u, 0u, 0u, 2u, 1u, 1u, 1u, 2u, 1u, 0u, 2u, 2u, 0u, 1u, 0u, 2u, 0u, 0u, 0u, 2u, 1u, 2u, 0u, 1u, 0u, 1u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 2u, 1u, 0u, 1u, 1u, 1u, 1u, 0u, 1u, 2u, 2u, 1u, 2u, 1u, 0u, 1u, 2u, 0u, 1u, 1u, 2u, 0u, 1u, 1u, 0u, 2u, 2u, 1u, 2u, 2u, 1u, 0u, 2u, 1u, 2u, 0u, 2u, 0u, 2u, 1u, 0u, 1u, 1u, 0u, 2u, 1u, 1u, 2u, 0u, 2u, 2u, 2u, 0u, 0u, 2u, 0u, 1u, 2u, 1u, 1u, 1u, 2u, 0u, 1u, 1u, 1u, 1u, 1u, 0u, 2u, 1u, 1u, 2u, 2u, 0u, 0u, 0u, 0u, 1u, 0u, 2u, 2u, 1u, 1u, 0u, 2u, 1u, 1u, 0u, 0u, 2u, 2u, 2u, 1u, 0u, 2u, 0u, 0u, 0u, 1u, 2u, 1u, 2u, 0u, 2u, 0u, 1u, 0u, 2u, 1u};
inline constexpr std::uint32_t kSeqState0[] = {3558086348u, 4006129454u, 14646249u, 1790571880u};
inline constexpr std::uint32_t kSeqHist0[] = {3303u, 3302u, 3395u};
inline constexpr std::uint32_t kSeqRaw1[] = {1789933344u, 44971166u, 2521387044u, 3848737593u, 1138324114u, 749234105u, 1899511038u, 1995189375u, 3629653958u, 19166872u, 1711213692u, 48427069u, 3762117657u, 2857799264u, 4183157539u, 3474655728u};
inline constexpr std::uint32_t kSeqPrefix1[] = {0u, 14u, 12u, 9u, 10u, 17u, 6u, 15u, 14u, 16u, 12u, 13u, 9u, 8u, 19u, 0u, 16u, 19u, 4u, 1u, 19u, 16u, 17u, 0u, 8u, 18u, 6u, 2u, 22u, 3u, 2u, 5u, 10u, 11u, 8u, 11u, 7u, 16u, 17u, 18u, 10u, 8u, 15u, 13u, 12u, 20u, 14u, 11u, 22u, 14u, 0u, 4u, 4u, 21u, 3u, 2u, 20u, 1u, 3u, 12u, 1u, 12u, 5u, 21u, 22u, 13u, 3u, 18u, 9u, 9u, 23u, 7u, 16u, 7u, 14u, 9u, 18u, 0u, 17u, 12u, 3u, 23u, 22u, 4u, 13u, 17u, 12u, 8u, 16u, 9u, 9u, 12u, 1u, 15u, 2u, 1u, 12u, 9u, 11u, 23u, 1u, 12u, 22u, 14u, 22u, 15u, 13u, 1u, 20u, 15u, 6u, 4u, 0u, 12u, 8u, 19u, 19u, 4u, 2u, 19u, 0u, 5u, 14u, 3u, 4u, 0u, 5u, 21u, 0u, 9u, 14u, 19u, 17u, 0u, 19u, 9u, 22u, 0u, 23u, 9u, 6u, 21u, 15u, 3u, 1u, 21u, 23u, 13u, 12u, 10u, 4u, 19u, 7u, 3u, 22u, 5u, 14u, 1u, 23u, 7u, 0u, 10u, 8u, 21u, 7u, 1u, 11u, 15u, 4u, 7u, 0u, 2u, 11u, 16u, 17u, 2u, 1u, 3u, 14u, 13u, 20u, 12u, 20u, 3u, 17u, 1u, 12u, 19u, 16u, 15u, 2u, 4u, 1u, 14u, 9u, 8u, 5u, 8u, 9u, 6u, 8u, 0u, 19u, 11u, 16u, 22u, 22u, 2u, 12u, 7u, 13u, 13u, 10u, 1u, 21u, 14u, 13u, 16u, 2u, 2u, 3u, 12u, 6u, 12u, 1u, 18u, 14u, 23u, 16u, 10u, 3u, 5u, 1u, 10u, 18u, 15u, 20u, 2u, 11u, 1u, 0u, 2u, 0u, 21u, 21u, 19u, 17u, 10u, 8u, 18u, 14u, 6u, 4u, 9u, 2u, 4u};
inline constexpr std::uint32_t kSeqState1[] = {3558086348u, 4006129454u, 14646249u, 1790571880u};
inline constexpr std::uint32_t kSeqHist1[] = {420u, 381u, 394u, 395u, 428u, 435u, 401u, 408u, 427u, 408u, 411u, 424u, 450u, 431u, 448u, 428u, 417u, 434u, 373u, 407u, 426u, 428u, 419u, 407u};
inline constexpr std::uint32_t kSeqRaw2[] = {1789933344u, 44971166u, 2521387044u, 3848737593u, 1138324114u, 749234105u, 1899511038u, 1995189375u, 3629653958u, 19166872u, 1711213692u, 48427069u, 3762117657u, 2857799264u, 4183157539u, 3474655728u};
inline constexpr std::uint32_t kSeqPrefix2[] = {19u, 16u, 19u, 18u, 14u, 5u, 13u, 0u, 8u, 22u, 17u, 19u, 7u, 14u, 14u, 3u, 4u, 18u, 9u, 8u, 17u, 0u, 10u, 24u, 5u, 5u, 8u, 3u, 22u, 11u, 0u, 15u, 16u, 9u, 14u, 16u, 19u, 23u, 6u, 1u, 2u, 2u, 12u, 1u, 1u, 23u, 15u, 23u, 19u, 13u, 3u, 13u, 8u, 11u, 15u, 16u, 15u, 1u, 2u, 0u, 4u, 12u, 4u, 9u, 17u, 1u, 12u, 19u, 17u, 18u, 6u, 20u, 22u, 7u, 17u, 20u, 24u, 19u, 1u, 23u, 22u, 15u, 4u, 13u, 1u, 19u, 0u, 3u, 5u, 5u, 21u, 1u, 24u, 2u, 17u, 16u, 21u, 9u, 5u, 1u, 17u, 3u, 15u, 7u, 10u, 15u, 22u, 0u, 2u, 3u, 5u, 10u, 9u, 11u, 22u, 1u, 14u, 18u, 20u, 17u, 18u, 2u, 21u, 16u, 11u, 22u, 1u, 23u, 9u, 10u, 9u, 22u, 13u, 23u, 20u, 4u, 6u, 6u, 5u, 18u, 15u, 18u, 20u, 9u, 7u, 3u, 19u, 21u, 17u, 18u, 21u, 18u, 22u, 9u, 11u, 23u, 19u, 19u, 7u, 12u, 21u, 10u, 13u, 9u, 3u, 12u, 0u, 17u, 14u, 24u, 20u, 22u, 20u, 12u, 9u, 22u, 24u, 16u, 6u, 0u, 7u, 23u, 24u, 11u, 16u, 21u, 18u, 13u, 9u, 6u, 5u, 8u, 19u, 5u, 0u, 9u, 1u, 13u, 0u, 2u, 6u, 11u, 21u, 18u, 10u, 17u, 13u, 11u, 16u, 17u, 17u, 6u, 7u, 0u, 14u, 9u, 1u, 17u, 1u, 5u, 12u, 6u, 7u, 5u, 5u, 18u, 7u, 20u, 23u, 11u, 17u, 4u, 4u, 24u, 10u, 23u, 1u, 0u, 23u, 2u, 2u, 7u, 16u, 21u, 23u, 18u, 8u, 23u, 1u, 4u, 15u, 9u, 5u, 12u, 14u, 14u};
inline constexpr std::uint32_t kSeqState2[] = {3558086348u, 4006129454u, 14646249u, 1790571880u};
inline constexpr std::uint32_t kSeqHist2[] = {403u, 377u, 413u, 390u, 408u, 414u, 383u, 411u, 405u, 428u, 376u, 385u, 428u, 410u, 407u, 401u, 402u, 386u, 359u, 395u, 404u, 426u, 411u, 375u, 403u};
inline constexpr std::uint32_t kSeqRaw3[] = {393288148u, 2174103013u, 3814759091u, 2092745082u, 1865176206u, 2179171167u, 3207394750u, 2858353069u, 559075315u, 3395495274u, 4035540825u, 1929427096u, 4080585408u, 498941776u, 2789075627u, 3924381082u};
inline constexpr std::uint32_t kSeqPrefix3[] = {1u, 1u, 2u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, 0u, 1u, 0u, 1u, 2u, 1u, 0u, 2u, 0u, 1u, 2u, 0u, 2u, 2u, 1u, 0u, 2u, 2u, 1u, 2u, 2u, 1u, 2u, 2u, 0u, 1u, 1u, 1u, 0u, 2u, 1u, 2u, 1u, 0u, 2u, 1u, 2u, 0u, 2u, 0u, 1u, 0u, 0u, 2u, 2u, 2u, 1u, 2u, 0u, 2u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 2u, 0u, 1u, 1u, 0u, 0u, 0u, 2u, 0u, 1u, 0u, 2u, 0u, 1u, 1u, 2u, 2u, 2u, 1u, 0u, 0u, 2u, 2u, 2u, 1u, 2u, 0u, 2u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 2u, 2u, 0u, 0u, 0u, 2u, 1u, 2u, 1u, 1u, 2u, 1u, 1u, 0u, 2u, 1u, 1u, 2u, 1u, 1u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 0u, 1u, 2u, 1u, 2u, 2u, 1u, 1u, 0u, 2u, 2u, 2u, 1u, 1u, 2u, 0u, 2u, 2u, 0u, 1u, 1u, 2u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, 2u, 0u, 1u, 2u, 0u, 2u, 1u, 0u, 0u, 1u, 0u, 2u, 0u, 1u, 0u, 0u, 1u, 1u, 2u, 1u, 1u, 2u, 2u, 2u, 0u, 1u, 2u, 2u, 1u, 1u, 0u, 0u, 1u, 0u, 1u, 0u, 0u, 2u, 1u, 2u, 1u, 1u, 1u, 0u, 1u, 1u, 0u, 2u, 1u, 2u, 1u, 2u, 2u, 1u, 0u, 0u, 2u, 0u, 0u, 1u, 0u, 1u, 1u, 2u, 0u, 2u, 2u, 1u, 2u, 2u, 0u, 2u, 1u, 1u, 1u, 2u, 0u, 1u, 0u, 2u, 1u, 1u, 0u, 2u, 2u, 0u, 2u, 0u, 1u, 0u, 2u, 1u, 1u, 1u, 2u, 1u, 2u};
inline constexpr std::uint32_t kSeqState3[] = {204756164u, 1489857618u, 1205362732u, 2603601668u};
inline constexpr std::uint32_t kSeqHist3[] = {3419u, 3282u, 3299u};
inline constexpr std::uint32_t kSeqRaw4[] = {393288148u, 2174103013u, 3814759091u, 2092745082u, 1865176206u, 2179171167u, 3207394750u, 2858353069u, 559075315u, 3395495274u, 4035540825u, 1929427096u, 4080585408u, 498941776u, 2789075627u, 3924381082u};
inline constexpr std::uint32_t kSeqPrefix4[] = {4u, 13u, 11u, 18u, 6u, 15u, 22u, 13u, 19u, 18u, 9u, 16u, 0u, 16u, 11u, 10u, 9u, 17u, 18u, 7u, 11u, 18u, 23u, 11u, 10u, 0u, 2u, 8u, 10u, 23u, 2u, 16u, 2u, 2u, 9u, 16u, 13u, 19u, 9u, 8u, 7u, 14u, 7u, 9u, 17u, 22u, 17u, 9u, 11u, 18u, 4u, 3u, 9u, 5u, 23u, 11u, 16u, 8u, 0u, 17u, 0u, 22u, 9u, 16u, 15u, 3u, 3u, 5u, 3u, 7u, 10u, 9u, 18u, 18u, 17u, 0u, 1u, 18u, 8u, 0u, 7u, 22u, 20u, 2u, 23u, 19u, 18u, 21u, 5u, 8u, 2u, 10u, 20u, 21u, 8u, 18u, 10u, 20u, 1u, 3u, 9u, 21u, 5u, 14u, 15u, 9u, 3u, 23u, 1u, 23u, 10u, 10u, 5u, 16u, 4u, 15u, 5u, 4u, 22u, 5u, 7u, 22u, 12u, 19u, 1u, 19u, 10u, 8u, 20u, 2u, 21u, 1u, 8u, 4u, 2u, 20u, 22u, 13u, 0u, 20u, 23u, 8u, 7u, 22u, 20u, 12u, 17u, 11u, 15u, 19u, 16u, 8u, 3u, 12u, 21u, 3u, 18u, 8u, 0u, 20u, 3u, 10u, 5u, 6u, 17u, 16u, 9u, 15u, 10u, 0u, 11u, 3u, 13u, 18u, 18u, 13u, 10u, 5u, 16u, 10u, 14u, 11u, 20u, 21u, 13u, 23u, 20u, 22u, 16u, 12u, 18u, 4u, 9u, 7u, 12u, 9u, 8u, 4u, 11u, 10u, 10u, 16u, 9u, 22u, 22u, 6u, 11u, 22u, 17u, 22u, 17u, 11u, 19u, 12u, 3u, 5u, 18u, 15u, 10u, 9u, 19u, 22u, 2u, 0u, 17u, 11u, 22u, 17u, 17u, 12u, 20u, 19u, 10u, 7u, 8u, 15u, 16u, 15u, 2u, 10u, 1u, 15u, 17u, 20u, 15u, 14u, 9u, 13u, 3u, 8u, 10u, 7u, 16u, 17u, 4u, 5u};
inline constexpr std::uint32_t kSeqState4[] = {204756164u, 1489857618u, 1205362732u, 2603601668u};
inline constexpr std::uint32_t kSeqHist4[] = {420u, 388u, 445u, 433u, 408u, 406u, 423u, 407u, 429u, 411u, 416u, 440u, 441u, 392u, 400u, 414u, 405u, 376u, 438u, 430u, 418u, 439u, 436u, 385u};
inline constexpr std::uint32_t kSeqRaw5[] = {393288148u, 2174103013u, 3814759091u, 2092745082u, 1865176206u, 2179171167u, 3207394750u, 2858353069u, 559075315u, 3395495274u, 4035540825u, 1929427096u, 4080585408u, 498941776u, 2789075627u, 3924381082u};
inline constexpr std::uint32_t kSeqPrefix5[] = {23u, 13u, 16u, 7u, 6u, 17u, 0u, 19u, 15u, 24u, 0u, 21u, 8u, 1u, 2u, 7u, 22u, 4u, 1u, 24u, 9u, 18u, 18u, 7u, 17u, 2u, 20u, 21u, 20u, 20u, 22u, 1u, 21u, 0u, 1u, 11u, 5u, 1u, 24u, 13u, 6u, 21u, 1u, 17u, 3u, 11u, 15u, 7u, 10u, 19u, 12u, 16u, 0u, 11u, 14u, 4u, 16u, 2u, 9u, 22u, 6u, 5u, 13u, 4u, 1u, 12u, 5u, 1u, 10u, 12u, 4u, 14u, 6u, 7u, 7u, 8u, 12u, 0u, 23u, 20u, 16u, 9u, 20u, 0u, 1u, 22u, 16u, 13u, 16u, 15u, 6u, 4u, 9u, 1u, 0u, 18u, 19u, 11u, 9u, 20u, 23u, 14u, 17u, 23u, 20u, 2u, 2u, 11u, 13u, 23u, 7u, 12u, 1u, 16u, 22u, 24u, 16u, 11u, 10u, 18u, 0u, 4u, 10u, 11u, 16u, 5u, 21u, 11u, 22u, 8u, 12u, 16u, 12u, 5u, 14u, 12u, 12u, 14u, 14u, 13u, 2u, 19u, 2u, 2u, 15u, 2u, 12u, 0u, 19u, 20u, 11u, 7u, 7u, 4u, 9u, 12u, 1u, 8u, 21u, 11u, 3u, 13u, 12u, 13u, 22u, 24u, 19u, 8u, 6u, 3u, 4u, 23u, 17u, 21u, 3u, 23u, 18u, 17u, 21u, 12u, 11u, 4u, 21u, 12u, 16u, 1u, 18u, 3u, 11u, 14u, 7u, 24u, 0u, 20u, 23u, 22u, 4u, 17u, 4u, 3u, 4u, 18u, 2u, 8u, 14u, 9u, 8u, 21u, 8u, 5u, 12u, 12u, 3u, 13u, 10u, 7u, 11u, 1u, 12u, 13u, 20u, 24u, 18u, 24u, 16u, 14u, 2u, 14u, 17u, 16u, 24u, 2u, 15u, 22u, 7u, 23u, 2u, 24u, 12u, 2u, 17u, 20u, 21u, 21u, 17u, 15u, 0u, 16u, 14u, 6u, 20u, 16u, 19u, 22u, 2u, 21u};
inline constexpr std::uint32_t kSeqState5[] = {204756164u, 1489857618u, 1205362732u, 2603601668u};
inline constexpr std::uint32_t kSeqHist5[] = {393u, 428u, 436u, 405u, 383u, 386u, 418u, 384u, 394u, 419u, 379u, 414u, 387u, 362u, 427u, 384u, 415u, 422u, 387u, 400u, 416u, 390u, 380u, 399u, 392u};
inline constexpr std::uint32_t kSeqRaw6[] = {3842467093u, 879304004u, 3694663928u, 2788030634u, 934155191u, 702880729u, 3422146658u, 169081873u, 3034898518u, 3260532345u, 2050117273u, 1236536921u, 914240050u, 2351145002u, 1248889433u, 3824328429u};
inline constexpr std::uint32_t kSeqPrefix6[] = {1u, 2u, 2u, 2u, 2u, 1u, 2u, 1u, 1u, 0u, 1u, 2u, 1u, 2u, 2u, 0u, 2u, 0u, 2u, 2u, 1u, 1u, 0u, 1u, 0u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 1u, 0u, 1u, 2u, 1u, 1u, 2u, 1u, 1u, 1u, 2u, 2u, 1u, 2u, 2u, 0u, 0u, 2u, 0u, 2u, 0u, 0u, 2u, 2u, 0u, 2u, 2u, 2u, 0u, 0u, 1u, 2u, 0u, 1u, 1u, 1u, 0u, 2u, 1u, 2u, 0u, 1u, 2u, 0u, 0u, 0u, 2u, 1u, 0u, 2u, 1u, 1u, 2u, 0u, 0u, 0u, 2u, 2u, 0u, 2u, 0u, 1u, 1u, 0u, 2u, 1u, 2u, 2u, 0u, 2u, 2u, 0u, 1u, 2u, 2u, 1u, 2u, 1u, 2u, 1u, 0u, 2u, 2u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 2u, 2u, 2u, 2u, 0u, 0u, 0u, 1u, 1u, 0u, 2u, 1u, 0u, 0u, 1u, 1u, 0u, 2u, 2u, 1u, 2u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 2u, 1u, 2u, 2u, 2u, 1u, 2u, 1u, 0u, 1u, 2u, 1u, 2u, 2u, 1u, 1u, 1u, 2u, 2u, 0u, 2u, 1u, 1u, 2u, 2u, 1u, 0u, 2u, 0u, 0u, 1u, 1u, 1u, 0u, 2u, 2u, 0u, 1u, 2u, 0u, 0u, 2u, 2u, 2u, 0u, 1u, 2u, 2u, 1u, 0u, 0u, 0u, 2u, 0u, 2u, 0u, 2u, 2u, 1u, 2u, 0u, 1u, 1u, 0u, 2u, 1u, 2u, 1u, 1u, 2u, 2u, 2u, 0u, 1u, 0u, 1u, 1u, 2u, 1u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 2u, 2u, 0u, 2u, 2u, 2u, 2u, 0u, 2u, 1u, 0u, 2u, 0u, 0u, 0u, 1u};
inline constexpr std::uint32_t kSeqState6[] = {440119618u, 1562922650u, 3577121411u, 2334695291u};
inline constexpr std::uint32_t kSeqHist6[] = {3241u, 3336u, 3423u};
inline constexpr std::uint32_t kSeqRaw7[] = {3842467093u, 879304004u, 3694663928u, 2788030634u, 934155191u, 702880729u, 3422146658u, 169081873u, 3034898518u, 3260532345u, 2050117273u, 1236536921u, 914240050u, 2351145002u, 1248889433u, 3824328429u};
inline constexpr std::uint32_t kSeqPrefix7[] = {13u, 20u, 8u, 2u, 23u, 1u, 2u, 1u, 22u, 9u, 1u, 17u, 10u, 2u, 17u, 21u, 8u, 9u, 23u, 17u, 1u, 19u, 9u, 13u, 0u, 20u, 5u, 2u, 2u, 8u, 2u, 17u, 4u, 21u, 22u, 23u, 4u, 10u, 8u, 13u, 22u, 7u, 23u, 2u, 1u, 8u, 14u, 3u, 3u, 5u, 9u, 17u, 12u, 3u, 5u, 20u, 21u, 8u, 14u, 17u, 12u, 12u, 1u, 20u, 3u, 19u, 13u, 7u, 3u, 14u, 19u, 17u, 15u, 7u, 8u, 9u, 9u, 0u, 17u, 19u, 3u, 11u, 16u, 22u, 8u, 18u, 6u, 12u, 2u, 11u, 0u, 11u, 9u, 7u, 19u, 12u, 11u, 22u, 2u, 8u, 15u, 8u, 23u, 21u, 13u, 23u, 14u, 19u, 14u, 19u, 5u, 7u, 9u, 8u, 23u, 9u, 7u, 1u, 19u, 13u, 4u, 13u, 18u, 5u, 20u, 14u, 8u, 18u, 18u, 18u, 7u, 13u, 9u, 17u, 4u, 0u, 18u, 19u, 13u, 0u, 17u, 5u, 10u, 8u, 7u, 9u, 19u, 21u, 1u, 9u, 12u, 20u, 19u, 2u, 8u, 11u, 16u, 20u, 19u, 6u, 22u, 5u, 19u, 20u, 5u, 13u, 13u, 7u, 2u, 8u, 6u, 23u, 22u, 19u, 14u, 11u, 16u, 6u, 11u, 9u, 3u, 10u, 22u, 10u, 21u, 23u, 20u, 15u, 10u, 5u, 9u, 9u, 2u, 8u, 20u, 0u, 22u, 11u, 8u, 22u, 18u, 6u, 12u, 20u, 12u, 14u, 9u, 20u, 5u, 4u, 23u, 21u, 22u, 13u, 21u, 14u, 16u, 14u, 16u, 16u, 20u, 11u, 23u, 0u, 16u, 15u, 19u, 1u, 17u, 7u, 6u, 22u, 1u, 0u, 12u, 15u, 21u, 17u, 16u, 3u, 20u, 8u, 12u, 11u, 20u, 14u, 14u, 21u, 5u, 22u, 21u, 23u, 3u, 18u, 15u, 13u};
inline constexpr std::uint32_t kSeqState7[] = {440119618u, 1562922650u, 3577121411u, 2334695291u};
inline constexpr std::uint32_t kSeqHist7[] = {421u, 426u, 454u, 376u, 408u, 394u, 397u, 400u, 433u, 409u, 436u, 420u, 391u, 417u, 429u, 396u, 404u, 392u, 424u, 426u, 460u, 427u, 419u, 441u};
inline constexpr std::uint32_t kSeqRaw8[] = {3842467093u, 879304004u, 3694663928u, 2788030634u, 934155191u, 702880729u, 3422146658u, 169081873u, 3034898518u, 3260532345u, 2050117273u, 1236536921u, 914240050u, 2351145002u, 1248889433u, 3824328429u};
inline constexpr std::uint32_t kSeqPrefix8[] = {18u, 4u, 3u, 9u, 16u, 4u, 8u, 23u, 18u, 20u, 23u, 21u, 0u, 2u, 8u, 4u, 14u, 6u, 19u, 4u, 8u, 15u, 4u, 2u, 9u, 0u, 22u, 23u, 4u, 1u, 4u, 2u, 4u, 13u, 19u, 20u, 11u, 17u, 6u, 9u, 14u, 9u, 14u, 7u, 3u, 1u, 24u, 16u, 15u, 14u, 8u, 7u, 12u, 8u, 5u, 5u, 23u, 20u, 1u, 19u, 4u, 22u, 9u, 0u, 19u, 9u, 17u, 15u, 9u, 3u, 8u, 18u, 6u, 22u, 10u, 9u, 19u, 15u, 4u, 8u, 21u, 23u, 17u, 14u, 0u, 3u, 21u, 8u, 9u, 20u, 22u, 6u, 14u, 0u, 10u, 8u, 15u, 10u, 23u, 3u, 6u, 6u, 21u, 10u, 11u, 12u, 1u, 3u, 22u, 3u, 16u, 23u, 0u, 2u, 11u, 22u, 21u, 11u, 9u, 16u, 7u, 18u, 1u, 24u, 2u, 13u, 8u, 6u, 22u, 22u, 20u, 8u, 5u, 2u, 14u, 17u, 20u, 1u, 23u, 13u, 23u, 9u, 16u, 18u, 12u, 2u, 22u, 20u, 9u, 19u, 13u, 2u, 24u, 12u, 24u, 11u, 5u, 20u, 12u, 5u, 23u, 9u, 13u, 22u, 14u, 2u, 1u, 3u, 10u, 14u, 0u, 19u, 11u, 24u, 19u, 13u, 2u, 6u, 1u, 14u, 1u, 21u, 12u, 15u, 2u, 0u, 15u, 17u, 18u, 7u, 7u, 14u, 7u, 8u, 17u, 6u, 1u, 16u, 15u, 0u, 10u, 22u, 22u, 6u, 10u, 15u, 23u, 8u, 3u, 9u, 16u, 14u, 18u, 9u, 1u, 20u, 10u, 17u, 4u, 0u, 22u, 15u, 12u, 19u, 6u, 4u, 17u, 2u, 4u, 1u, 4u, 2u, 4u, 6u, 3u, 17u, 8u, 2u, 23u, 23u, 14u, 14u, 22u, 19u, 24u, 13u, 5u, 15u, 17u, 15u, 5u, 5u, 23u, 24u, 11u, 21u};
inline constexpr std::uint32_t kSeqState8[] = {440119618u, 1562922650u, 3577121411u, 2334695291u};
inline constexpr std::uint32_t kSeqHist8[] = {406u, 383u, 411u, 389u, 384u, 397u, 408u, 373u, 402u, 441u, 415u, 422u, 411u, 392u, 416u, 399u, 361u, 369u, 430u, 386u, 402u, 378u, 424u, 371u, 430u};
inline constexpr std::uint32_t kSeqRaw9[] = {2105519156u, 680133323u, 2853761255u, 2919389124u, 498989864u, 1097644218u, 3330385019u, 1824078962u, 1913740331u, 2349908020u, 3719027213u, 401084845u, 1007520160u, 335465982u, 790195838u, 537862080u};
inline constexpr std::uint32_t kSeqPrefix9[] = {2u, 2u, 2u, 0u, 2u, 0u, 2u, 2u, 2u, 1u, 2u, 1u, 1u, 0u, 2u, 0u, 1u, 2u, 0u, 0u, 0u, 2u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, 2u, 1u, 2u, 2u, 2u, 2u, 0u, 2u, 1u, 1u, 1u, 1u, 0u, 0u, 0u, 2u, 0u, 2u, 0u, 2u, 2u, 0u, 0u, 1u, 0u, 1u, 2u, 2u, 0u, 1u, 1u, 1u, 1u, 0u, 2u, 2u, 0u, 1u, 0u, 1u, 2u, 1u, 0u, 0u, 2u, 0u, 2u, 2u, 2u, 0u, 2u, 0u, 1u, 1u, 1u, 0u, 2u, 2u, 0u, 0u, 2u, 0u, 0u, 0u, 1u, 1u, 2u, 0u, 1u, 2u, 2u, 0u, 1u, 1u, 1u, 0u, 0u, 0u, 2u, 1u, 1u, 1u, 1u, 0u, 2u, 2u, 2u, 0u, 0u, 1u, 2u, 0u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 0u, 1u, 0u, 2u, 0u, 2u, 1u, 2u, 1u, 0u, 1u, 1u, 2u, 0u, 0u, 2u, 2u, 2u, 0u, 1u, 0u, 1u, 2u, 0u, 0u, 0u, 2u, 1u, 1u, 1u, 0u, 2u, 1u, 0u, 1u, 2u, 0u, 2u, 1u, 0u, 2u, 1u, 2u, 0u, 2u, 1u, 1u, 0u, 2u, 1u, 2u, 1u, 1u, 0u, 1u, 2u, 2u, 0u, 0u, 2u, 0u, 0u, 2u, 1u, 1u, 0u, 0u, 0u, 0u, 1u, 0u, 2u, 0u, 2u, 1u, 2u, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 2u, 2u, 0u, 1u, 2u, 2u, 1u, 2u, 2u, 2u, 0u, 2u, 0u, 2u, 1u, 2u, 2u, 2u, 1u, 2u, 1u, 2u, 2u, 0u, 2u, 2u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 1u, 2u, 0u, 2u, 2u, 1u, 1u, 0u};
inline constexpr std::uint32_t kSeqState9[] = {2914542367u, 193326613u, 1579524980u, 2737769970u};
inline constexpr std::uint32_t kSeqHist9[] = {3384u, 3300u, 3316u};
inline constexpr std::uint32_t kSeqRaw10[] = {2105519156u, 680133323u, 2853761255u, 2919389124u, 498989864u, 1097644218u, 3330385019u, 1824078962u, 1913740331u, 2349908020u, 3719027213u, 401084845u, 1007520160u, 335465982u, 790195838u, 537862080u};
inline constexpr std::uint32_t kSeqPrefix10[] = {20u, 11u, 23u, 12u, 8u, 18u, 11u, 2u, 11u, 4u, 5u, 13u, 16u, 6u, 14u, 0u, 1u, 5u, 3u, 12u, 0u, 23u, 21u, 6u, 17u, 19u, 18u, 12u, 9u, 6u, 17u, 1u, 2u, 23u, 5u, 2u, 12u, 5u, 10u, 16u, 1u, 4u, 9u, 12u, 6u, 20u, 12u, 11u, 15u, 20u, 20u, 21u, 9u, 10u, 21u, 13u, 14u, 11u, 0u, 16u, 19u, 19u, 16u, 21u, 23u, 20u, 21u, 22u, 21u, 1u, 8u, 4u, 0u, 15u, 20u, 6u, 17u, 17u, 20u, 6u, 5u, 12u, 4u, 7u, 7u, 12u, 17u, 20u, 6u, 9u, 17u, 9u, 15u, 12u, 1u, 7u, 5u, 9u, 10u, 2u, 5u, 0u, 1u, 16u, 22u, 6u, 9u, 3u, 11u, 19u, 1u, 7u, 22u, 6u, 8u, 5u, 8u, 18u, 3u, 13u, 5u, 9u, 8u, 8u, 22u, 19u, 8u, 10u, 22u, 3u, 7u, 18u, 17u, 9u, 11u, 13u, 11u, 10u, 3u, 7u, 7u, 11u, 0u, 9u, 14u, 2u, 5u, 6u, 10u, 0u, 22u, 5u, 0u, 21u, 18u, 11u, 4u, 7u, 4u, 15u, 17u, 4u, 6u, 19u, 14u, 6u, 14u, 13u, 6u, 14u, 13u, 17u, 15u, 20u, 22u, 19u, 0u, 14u, 1u, 23u, 7u, 16u, 12u, 4u, 23u, 17u, 9u, 15u, 8u, 9u, 6u, 11u, 1u, 10u, 21u, 15u, 15u, 6u, 10u, 0u, 17u, 9u, 17u, 4u, 20u, 19u, 22u, 19u, 10u, 22u, 10u, 12u, 8u, 23u, 18u, 16u, 17u, 8u, 7u, 2u, 17u, 2u, 6u, 5u, 15u, 20u, 7u, 14u, 23u, 11u, 13u, 11u, 19u, 5u, 23u, 0u, 23u, 2u, 18u, 13u, 0u, 7u, 0u, 9u, 0u, 7u, 0u, 6u, 7u, 20u, 18u, 14u, 2u, 10u, 22u, 12u};
inline constexpr std::uint32_t kSeqState10[] = {2914542367u, 193326613u, 1579524980u, 2737769970u};
inline constexpr std::uint32_t kSeqHist10[] = {425u, 405u, 387u, 431u, 421u, 423u, 407u, 393u, 415u, 425u, 405u, 390u, 474u, 411u, 388u, 412u, 445u, 440u, 383u, 428u, 425u, 427u, 392u, 448u};
inline constexpr std::uint32_t kSeqRaw11[] = {2105519156u, 680133323u, 2853761255u, 2919389124u, 498989864u, 1097644218u, 3330385019u, 1824078962u, 1913740331u, 2349908020u, 3719027213u, 401084845u, 1007520160u, 335465982u, 790195838u, 537862080u};
inline constexpr std::uint32_t kSeqPrefix11[] = {6u, 23u, 5u, 24u, 14u, 18u, 19u, 12u, 6u, 20u, 13u, 20u, 10u, 7u, 13u, 5u, 6u, 13u, 8u, 2u, 12u, 24u, 22u, 18u, 0u, 6u, 8u, 0u, 12u, 14u, 5u, 21u, 13u, 16u, 4u, 17u, 7u, 24u, 17u, 24u, 4u, 16u, 12u, 17u, 12u, 19u, 8u, 22u, 22u, 21u, 5u, 15u, 13u, 5u, 22u, 9u, 11u, 16u, 4u, 3u, 16u, 13u, 24u, 16u, 10u, 22u, 14u, 13u, 8u, 6u, 1u, 6u, 1u, 16u, 12u, 11u, 22u, 13u, 12u, 3u, 16u, 17u, 3u, 17u, 15u, 7u, 10u, 0u, 1u, 14u, 19u, 9u, 14u, 24u, 13u, 7u, 23u, 9u, 14u, 22u, 9u, 22u, 13u, 23u, 24u, 19u, 1u, 0u, 8u, 24u, 12u, 16u, 1u, 14u, 6u, 24u, 6u, 16u, 2u, 11u, 22u, 13u, 6u, 10u, 16u, 22u, 18u, 0u, 10u, 21u, 6u, 13u, 24u, 19u, 9u, 17u, 17u, 1u, 22u, 4u, 14u, 8u, 14u, 5u, 0u, 24u, 11u, 21u, 11u, 5u, 3u, 20u, 12u, 9u, 18u, 15u, 19u, 13u, 5u, 21u, 22u, 2u, 2u, 8u, 11u, 11u, 18u, 8u, 20u, 3u, 6u, 23u, 8u, 19u, 17u, 2u, 10u, 6u, 19u, 20u, 24u, 14u, 9u, 7u, 11u, 13u, 0u, 6u, 0u, 22u, 7u, 14u, 23u, 4u, 6u, 11u, 8u, 18u, 18u, 22u, 21u, 11u, 0u, 15u, 1u, 20u, 14u, 0u, 12u, 17u, 2u, 2u, 10u, 8u, 0u, 7u, 2u, 11u, 2u, 6u, 18u, 0u, 9u, 17u, 10u, 2u, 8u, 0u, 4u, 10u, 7u, 20u, 24u, 12u, 10u, 2u, 16u, 11u, 10u, 12u, 11u, 6u, 20u, 14u, 23u, 15u, 21u, 24u, 8u, 24u, 12u, 1u, 12u, 18u, 15u, 14u};
inline constexpr std::uint32_t kSeqState11[] = {2914542367u, 193326613u, 1579524980u, 2737769970u};
inline constexpr std::uint32_t kSeqHist11[] = {434u, 412u, 394u, 400u, 424u, 390u, 414u, 377u, 392u, 385u, 389u, 408u, 408u, 388u, 357u, 408u, 406u, 407u, 415u, 374u, 409u, 407u, 399u, 398u, 405u};
inline constexpr std::uint32_t kSeqRaw12[] = {4104197751u, 1825856343u, 1152209388u, 2427537429u, 3685145430u, 609215610u, 4161674276u, 1502890106u, 904255344u, 859094872u, 3596089546u, 219971322u, 4205410045u, 925431481u, 1008337578u, 3360333762u};
inline constexpr std::uint32_t kSeqPrefix12[] = {0u, 0u, 0u, 0u, 0u, 0u, 2u, 2u, 0u, 1u, 1u, 0u, 1u, 1u, 0u, 0u, 1u, 1u, 2u, 0u, 1u, 0u, 2u, 1u, 2u, 1u, 1u, 1u, 2u, 1u, 1u, 0u, 1u, 2u, 1u, 1u, 1u, 0u, 0u, 2u, 2u, 0u, 2u, 2u, 2u, 0u, 0u, 1u, 2u, 1u, 2u, 1u, 0u, 0u, 0u, 1u, 2u, 2u, 2u, 1u, 1u, 1u, 0u, 1u, 0u, 2u, 1u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 2u, 0u, 1u, 1u, 0u, 2u, 1u, 0u, 2u, 1u, 0u, 0u, 1u, 1u, 0u, 0u, 1u, 1u, 0u, 2u, 1u, 2u, 1u, 2u, 2u, 2u, 1u, 1u, 1u, 2u, 1u, 2u, 0u, 1u, 1u, 0u, 2u, 0u, 2u, 1u, 0u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 1u, 0u, 0u, 0u, 2u, 2u, 1u, 2u, 2u, 2u, 2u, 1u, 2u, 2u, 1u, 1u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 0u, 1u, 2u, 1u, 1u, 0u, 2u, 1u, 1u, 2u, 1u, 0u, 1u, 1u, 0u, 1u, 2u, 2u, 1u, 0u, 0u, 2u, 2u, 2u, 0u, 2u, 2u, 0u, 0u, 2u, 1u, 2u, 2u, 0u, 2u, 1u, 0u, 0u, 0u, 1u, 1u, 2u, 0u, 1u, 0u, 1u, 0u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 0u, 2u, 2u, 2u, 2u, 1u, 2u, 0u, 0u, 2u, 2u, 0u, 2u, 2u, 0u, 0u, 0u, 2u, 2u, 0u, 0u, 0u, 1u, 2u, 1u, 2u, 0u, 1u, 0u, 2u, 1u, 1u, 2u, 0u, 2u, 2u, 1u, 1u, 2u, 1u, 1u, 1u, 2u, 2u};
inline constexpr std::uint32_t kSeqState12[] = {87016938u, 725578845u, 614833149u, 934054032u};
inline constexpr std::uint32_t kSeqHist12[] = {3343u, 3268u, 3389u};
inline constexpr std::uint32_t kSeqRaw13[] = {4104197751u, 1825856343u, 1152209388u, 2427537429u, 3685145430u, 609215610u, 4161674276u, 1502890106u, 904255344u, 859094872u, 3596089546u, 219971322u, 4205410045u, 925431481u, 1008337578u, 3360333762u};
inline constexpr std::uint32_t kSeqPrefix13[] = {15u, 15u, 12u, 21u, 6u, 18u, 20u, 2u, 0u, 16u, 10u, 18u, 13u, 1u, 18u, 18u, 10u, 13u, 23u, 12u, 4u, 15u, 2u, 13u, 17u, 1u, 16u, 7u, 23u, 19u, 4u, 21u, 19u, 2u, 13u, 22u, 10u, 12u, 18u, 8u, 2u, 21u, 8u, 5u, 14u, 12u, 15u, 10u, 11u, 7u, 11u, 22u, 18u, 21u, 12u, 4u, 8u, 20u, 2u, 4u, 7u, 4u, 9u, 13u, 9u, 20u, 19u, 0u, 20u, 21u, 3u, 15u, 21u, 9u, 12u, 19u, 17u, 6u, 4u, 7u, 6u, 14u, 22u, 9u, 20u, 19u, 18u, 15u, 1u, 1u, 18u, 3u, 19u, 16u, 3u, 2u, 1u, 17u, 1u, 23u, 11u, 20u, 16u, 10u, 7u, 5u, 22u, 11u, 9u, 10u, 19u, 0u, 11u, 3u, 2u, 7u, 12u, 3u, 4u, 18u, 4u, 15u, 21u, 18u, 7u, 10u, 22u, 22u, 23u, 20u, 20u, 13u, 15u, 12u, 15u, 8u, 20u, 7u, 5u, 23u, 8u, 5u, 19u, 17u, 14u, 7u, 7u, 12u, 6u, 6u, 22u, 16u, 1u, 22u, 17u, 11u, 23u, 21u, 19u, 20u, 10u, 16u, 3u, 8u, 1u, 10u, 17u, 13u, 18u, 1u, 19u, 15u, 19u, 14u, 11u, 19u, 3u, 18u, 5u, 5u, 11u, 9u, 8u, 11u, 0u, 21u, 11u, 22u, 23u, 17u, 12u, 11u, 7u, 0u, 15u, 6u, 7u, 1u, 2u, 21u, 4u, 15u, 16u, 0u, 11u, 14u, 5u, 2u, 14u, 8u, 8u, 23u, 15u, 17u, 2u, 11u, 23u, 4u, 17u, 12u, 21u, 8u, 20u, 6u, 11u, 8u, 15u, 0u, 0u, 20u, 2u, 0u, 0u, 6u, 16u, 2u, 16u, 14u, 21u, 7u, 15u, 2u, 22u, 22u, 5u, 0u, 8u, 14u, 19u, 1u, 23u, 7u, 16u, 4u, 11u, 20u};
inline constexpr std::uint32_t kSeqState13[] = {87016938u, 725578845u, 614833149u, 934054032u};
inline constexpr std::uint32_t kSeqHist13[] = {416u, 385u, 430u, 411u, 415u, 431u, 447u, 432u, 419u, 418u, 399u, 413u, 425u, 410u, 413u, 377u, 429u, 404u, 423u, 409u, 448u, 426u, 389u, 431u};
inline constexpr std::uint32_t kSeqRaw14[] = {4104197751u, 1825856343u, 1152209388u, 2427537429u, 3685145430u, 609215610u, 4161674276u, 1502890106u, 904255344u, 859094872u, 3596089546u, 219971322u, 4205410045u, 925431481u, 1008337578u, 3360333762u};
inline constexpr std::uint32_t kSeqPrefix14[] = {1u, 18u, 13u, 4u, 5u, 10u, 1u, 6u, 19u, 22u, 21u, 22u, 20u, 6u, 3u, 12u, 14u, 15u, 2u, 5u, 7u, 24u, 0u, 18u, 8u, 0u, 3u, 3u, 23u, 22u, 0u, 18u, 11u, 14u, 16u, 11u, 5u, 15u, 23u, 0u, 15u, 22u, 6u, 15u, 9u, 17u, 10u, 15u, 19u, 3u, 2u, 6u, 3u, 18u, 22u, 18u, 15u, 19u, 4u, 9u, 2u, 17u, 21u, 24u, 6u, 17u, 10u, 9u, 19u, 20u, 19u, 22u, 23u, 6u, 5u, 15u, 23u, 23u, 8u, 9u, 23u, 19u, 13u, 8u, 17u, 23u, 1u, 0u, 10u, 6u, 8u, 3u, 14u, 23u, 20u, 0u, 4u, 20u, 20u, 2u, 7u, 16u, 16u, 17u, 7u, 24u, 11u, 17u, 10u, 20u, 14u, 5u, 15u, 9u, 18u, 19u, 2u, 17u, 24u, 1u, 3u, 0u, 11u, 13u, 15u, 14u, 24u, 14u, 5u, 5u, 13u, 8u, 11u, 23u, 8u, 23u, 18u, 6u, 13u, 2u, 23u, 15u, 10u, 12u, 5u, 6u, 13u, 0u, 7u, 10u, 1u, 24u, 16u, 21u, 3u, 2u, 4u, 17u, 6u, 0u, 13u, 8u, 12u, 5u, 4u, 23u, 7u, 8u, 9u, 11u, 10u, 0u, 16u, 23u, 24u, 5u, 17u, 7u, 20u, 18u, 9u, 24u, 22u, 16u, 7u, 15u, 14u, 3u, 12u, 2u, 20u, 3u, 7u, 15u, 3u, 16u, 1u, 15u, 4u, 24u, 7u, 20u, 23u, 5u, 20u, 14u, 11u, 23u, 7u, 5u, 1u, 13u, 14u, 11u, 22u, 6u, 3u, 6u, 17u, 18u, 2u, 13u, 7u, 23u, 12u, 17u, 2u, 4u, 15u, 5u, 19u, 1u, 23u, 15u, 18u, 2u, 6u, 2u, 19u, 24u, 23u, 11u, 8u, 5u, 20u, 23u, 1u, 16u, 0u, 11u, 21u, 0u, 15u, 3u, 8u, 15u};
inline constexpr std::uint32_t kSeqState14[] = {87016938u, 725578845u, 614833149u, 934054032u};
inline constexpr std::uint32_t kSeqHist14[] = {415u, 420u, 427u, 397u, 399u, 367u, 426u, 380u, 381u, 403u, 389u, 397u, 408u, 424u, 352u, 400u, 395u, 421u, 394u, 392u, 385u, 381u, 434u, 420u, 393u};

inline constexpr SequenceCase kSequenceCases[] = {
    {"seed_0x00000000_n3", 0u, 3u, 10000u, kSeqRaw0, 16, kSeqPrefix0, 256, 0x182A73ECu, 0u, kSeqState0, kSeqHist0, 3},
    {"seed_0x00000000_n24", 0u, 24u, 10000u, kSeqRaw1, 16, kSeqPrefix1, 256, 0x362F3F5Eu, 0u, kSeqState1, kSeqHist1, 24},
    {"seed_0x00000000_n25", 0u, 25u, 10000u, kSeqRaw2, 16, kSeqPrefix2, 256, 0xCE649523u, 0u, kSeqState2, kSeqHist2, 25},
    {"seed_0x00000001_n3", 1u, 3u, 10000u, kSeqRaw3, 16, kSeqPrefix3, 256, 0xB5647B73u, 0u, kSeqState3, kSeqHist3, 3},
    {"seed_0x00000001_n24", 1u, 24u, 10000u, kSeqRaw4, 16, kSeqPrefix4, 256, 0x15F6E651u, 0u, kSeqState4, kSeqHist4, 24},
    {"seed_0x00000001_n25", 1u, 25u, 10000u, kSeqRaw5, 16, kSeqPrefix5, 256, 0x7F89589Bu, 0u, kSeqState5, kSeqHist5, 25},
    {"seed_0xDEADBEEF_n3", 3735928559u, 3u, 10000u, kSeqRaw6, 16, kSeqPrefix6, 256, 0xCA5CA97Eu, 0u, kSeqState6, kSeqHist6, 3},
    {"seed_0xDEADBEEF_n24", 3735928559u, 24u, 10000u, kSeqRaw7, 16, kSeqPrefix7, 256, 0x69E7577Cu, 0u, kSeqState7, kSeqHist7, 24},
    {"seed_0xDEADBEEF_n25", 3735928559u, 25u, 10000u, kSeqRaw8, 16, kSeqPrefix8, 256, 0xADAE237Au, 0u, kSeqState8, kSeqHist8, 25},
    {"seed_0x5A5A5A5A_n3", 1515870810u, 3u, 10000u, kSeqRaw9, 16, kSeqPrefix9, 256, 0x6A744623u, 0u, kSeqState9, kSeqHist9, 3},
    {"seed_0x5A5A5A5A_n24", 1515870810u, 24u, 10000u, kSeqRaw10, 16, kSeqPrefix10, 256, 0xDB1BA2C5u, 0u, kSeqState10, kSeqHist10, 24},
    {"seed_0x5A5A5A5A_n25", 1515870810u, 25u, 10000u, kSeqRaw11, 16, kSeqPrefix11, 256, 0x7D526AC2u, 0u, kSeqState11, kSeqHist11, 25},
    {"seed_0xFFFFFFFF_n3", 4294967295u, 3u, 10000u, kSeqRaw12, 16, kSeqPrefix12, 256, 0x987DB45Eu, 0u, kSeqState12, kSeqHist12, 3},
    {"seed_0xFFFFFFFF_n24", 4294967295u, 24u, 10000u, kSeqRaw13, 16, kSeqPrefix13, 256, 0xE66A50D9u, 0u, kSeqState13, kSeqHist13, 24},
    {"seed_0xFFFFFFFF_n25", 4294967295u, 25u, 10000u, kSeqRaw14, 16, kSeqPrefix14, 256, 0xFCB06927u, 0u, kSeqState14, kSeqHist14, 25},
};
inline constexpr std::size_t kSequenceCaseCount = 15;

// --- scoring -----------------------------------------------------------

struct ScoringCase {
  const char* name;
  const char* note;
  std::uint32_t n;
  std::uint32_t correct;
  std::uint32_t incorrect;
  double elapsed_s;
  double bit_rate;
  std::uint32_t b_mbps;
  double bits_per_selection;
};

inline constexpr ScoringCase kScoringCases[] = {
    {"zero_time_guard", "t <= 0 must return 0, not divide by zero", 25u, 10u, 0u, 0.0, 0.0, 0u, 4.584962500721156},
    {"negative_time_guard", "defensive: negative t returns 0", 25u, 10u, 0u, -1.0, 0.0, 0u, 4.584962500721156},
    {"no_events", "nothing pressed scores 0", 25u, 0u, 0u, 60.0, 0.0, 0u, 4.584962500721156},
    {"si_exceeds_sc", "max(Sc-Si,0) clamps to exactly 0", 25u, 10u, 20u, 60.0, 0.0, 0u, 4.584962500721156},
    {"si_equals_sc", "net zero scores exactly 0", 25u, 10u, 10u, 60.0, 0.0, 0u, 4.584962500721156},
    {"si_one_less", "net one is the smallest positive score", 25u, 11u, 10u, 60.0, 0.07641604167868593, 76u, 4.584962500721156},
    {"n_below_min", "N < 3 is not a scorable session", 2u, 50u, 0u, 60.0, 0.0, 0u, 0.0},
    {"n_min_three", "log2(2)=1 bit per selection -> exactly 1.0 bps", 3u, 60u, 0u, 60.0, 1.0, 1000u, 1.0},
    {"n_three_with_misses", "N=3 with misses", 3u, 60u, 15u, 60.0, 0.75, 750u, 1.0},
    {"n_24_selftest_reduced", "one dead cell excluded by SELFTEST", 24u, 200u, 10u, 60.0, 14.324612860847207, 14325u, 4.523561956057013},
    {"n_25_nominal", "the expected shape of a good run", 25u, 200u, 10u, 60.0, 14.519047918950328, 14519u, 4.584962500721156},
    {"n_25_perfect_fast", "5 presses/sec, no misses", 25u, 300u, 0u, 60.0, 22.92481250360578, 22925u, 4.584962500721156},
    {"n_25_sloppy_fast", "fast but inaccurate; misses cost double", 25u, 300u, 100u, 60.0, 15.283208335737188, 15283u, 4.584962500721156},
    {"n_25_slow_accurate", "2 presses/sec, no misses", 25u, 120u, 0u, 60.0, 9.169925001442312, 9170u, 4.584962500721156},
    {"n_25_single_hit", "one hit in the whole window", 25u, 1u, 0u, 60.0, 0.07641604167868593, 76u, 4.584962500721156},
    {"n_25_one_second", "early in the run, small t", 25u, 4u, 0u, 1.0, 18.339850002884624, 18340u, 4.584962500721156},
    {"n_25_sub_second", "first hit lands very early", 25u, 1u, 0u, 0.25, 18.339850002884624, 18340u, 4.584962500721156},
    {"n_25_long_session", "practice-length session", 25u, 1000u, 50u, 600.0, 7.259523959475164, 7260u, 4.584962500721156},
    {"n_25_partial_window", "non-integer elapsed time", 25u, 137u, 9u, 42.5, 13.80882823746607, 13809u, 4.584962500721156},
    {"rounding_half_up", "B=0.5 exactly -> 500 mbps", 3u, 1u, 0u, 2.0, 0.5, 500u, 1.0},
    {"rounding_tiny", "B ~= 0.001 -> rounding boundary", 25u, 1u, 0u, 4584.962500721156, 0.001, 1u, 4.584962500721156},
    {"high_rate", "8 presses/sec, no misses", 25u, 480u, 0u, 60.0, 36.67970000576925, 36680u, 4.584962500721156},
    {"n_25_all_misses", "only misses scores 0", 25u, 0u, 50u, 60.0, 0.0, 0u, 4.584962500721156},
    {"n_25_equal_large", "large equal counts still exactly 0", 25u, 500u, 500u, 60.0, 0.0, 0u, 4.584962500721156},
};
inline constexpr std::size_t kScoringCaseCount = 24;

// --- CRC ---------------------------------------------------------------

struct CrcCase {
  const char* name;
  const char* input;
  std::size_t input_length;
  std::uint32_t crc16;
};

inline constexpr CrcCase kCrcCases[] = {
    {"canonical_check_value", "123456789", 9, 0x29B1u},
    {"empty", "", 0, 0xFFFFu},
    {"single_byte_A", "A", 1, 0xB915u},
    {"ev_hello", "EV 1 0 HELLO proto=1 fw=1.0.0 board=gridpulse-5x5", 49, 0x7543u},
    {"ev_target", "EV 42 1234567 TARGET cell=13 idx=7 repeat=0", 43, 0x9A9Du},
    {"ev_hit", "EV 43 1240000 HIT cell=13 rt_us=213000 sc=7 si=1 streak=3", 57, 0x9D83u},
    {"ev_miss", "EV 44 1250000 MISS pressed=9 target=13 sc=7 si=2", 48, 0xE06Bu},
    {"ev_end", "EV 900 60000000 END n=25 sc=241 si=12 t_us=60000000 b_mbps=17494", 64, 0x3346u},
    {"cmd_start", "CMD START EVAL N=25", 19, 0x4B08u},
    {"cmd_abort", "CMD ABORT", 9, 0x9884u},
};
inline constexpr std::size_t kCrcCaseCount = 10;

// Single-bit-flip detection cases, all derived from one base line.
inline constexpr const char* kCrcFlipBase = "EV 42 1234567 TARGET cell=13 idx=7 repeat=0";
inline constexpr std::size_t kCrcFlipBaseLength = 43;
inline constexpr std::uint32_t kCrcFlipBaseCrc = 0x9A9Du;

struct CrcFlipCase {
  std::size_t byte_index;
  int bit;
  std::uint32_t crc16;
};
inline constexpr CrcFlipCase kCrcFlipCases[] = {
    {0, 0, 0xF017u},
    {0, 7, 0xB96Bu},
    {3, 0, 0x7439u},
    {3, 7, 0xC6EDu},
    {14, 0, 0x6A7Bu},
    {14, 7, 0x1602u},
    {42, 0, 0x8ABCu},
    {42, 7, 0x0B15u},
};
inline constexpr std::size_t kCrcFlipCaseCount = 8;

// --- protocol ----------------------------------------------------------

struct ProtocolCase {
  const char* name;
  const char* note;
  const char* line;
  std::size_t line_length;
  const char* error;         // matches ParseErrorText()
  bool ok;
  const char* command;       // null unless ok
  const char* mode;          // START only, else null
  int force;                 // SELFTEST only, else -1
  int brightness_pct;        // BRIGHT only, else -1
};

inline constexpr ProtocolCase kProtocolCases[] = {
    {"ping", "well-formed", "CMD PING BC9F", 13, "ok", true, "PING", nullptr, -1, -1},
    {"proto", "well-formed", "CMD PROTO 24D4", 14, "ok", true, "PROTO", nullptr, -1, -1},
    {"abort", "well-formed", "CMD ABORT 9884", 14, "ok", true, "ABORT", nullptr, -1, -1},
    {"start_eval", "well-formed", "CMD START mode=EVAL 5274", 24, "ok", true, "START", "EVAL", -1, -1},
    {"start_practice", "well-formed", "CMD START mode=PRACTICE 31A6", 28, "ok", true, "START", "PRACTICE", -1, -1},
    {"selftest_default", "well-formed", "CMD SELFTEST 59EE", 17, "ok", true, "SELFTEST", nullptr, 0, -1},
    {"selftest_force", "well-formed", "CMD SELFTEST force=1 F561", 25, "ok", true, "SELFTEST", nullptr, 1, -1},
    {"selftest_no_force", "well-formed", "CMD SELFTEST force=0 E540", 25, "ok", true, "SELFTEST", nullptr, 0, -1},
    {"bright_zero", "well-formed", "CMD BRIGHT pct=0 7601", 21, "ok", true, "BRIGHT", nullptr, -1, 0},
    {"bright_mid", "well-formed", "CMD BRIGHT pct=50 D6F7", 22, "ok", true, "BRIGHT", nullptr, -1, 50},
    {"bright_max", "well-formed", "CMD BRIGHT pct=100 B628", 23, "ok", true, "BRIGHT", nullptr, -1, 100},
    {"crlf_tolerated", "trailing CRLF is stripped before validation", "CMD PING BC9F\r\n", 15, "ok", true, "PING", nullptr, -1, -1},
    {"lf_tolerated", "trailing LF is stripped before validation", "CMD PING BC9F\n", 14, "ok", true, "PING", nullptr, -1, -1},
    {"start_without_mode", "an unspecified mode must never silently start the scored run", "CMD START FF62", 14, "missing_argument", false, nullptr, nullptr, -1, -1},
    {"start_bad_mode", "mode must be EVAL or PRACTICE", "CMD START mode=BOGUS E06E", 25, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"start_empty_mode", "an empty value is not a valid mode", "CMD START mode= DDF8", 20, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"selftest_bad_force", "force is strictly 0 or 1", "CMD SELFTEST force=2 C502", 25, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"bright_without_pct", "pct is required", "CMD BRIGHT 9F6E", 15, "missing_argument", false, nullptr, nullptr, -1, -1},
    {"bright_out_of_range", "pct is 0..100", "CMD BRIGHT pct=101 A609", 23, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"bright_not_a_number", "non-digit value", "CMD BRIGHT pct=abc EAAD", 23, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"bright_negative", "the wire format is unsigned decimal only", "CMD BRIGHT pct=-5 0C88", 22, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"bright_overflow", "a value too large for 64 bits must be rejected, not wrapped", "CMD BRIGHT pct=99999999999999999999999 434A", 43, "bad_argument", false, nullptr, nullptr, -1, -1},
    {"unknown_command", "unrecognised command name", "CMD FLY 53A3", 12, "unknown_command", false, nullptr, nullptr, -1, -1},
    {"empty_command_name", "no command name at all", "CMD  CB16", 9, "unknown_command", false, nullptr, nullptr, -1, -1},
    {"event_line_to_command_parser", "a device->host event is not a command", "EV 1 118293 HELLO proto=1 fw=1.0.0 C39C", 39, "bad_prefix", false, nullptr, nullptr, -1, -1},
    {"lowercase_prefix", "the prefix is case-sensitive", "cmd PING 9C09", 13, "bad_prefix", false, nullptr, nullptr, -1, -1},
    {"no_prefix", "missing CMD prefix", "PING 6427", 9, "bad_prefix", false, nullptr, nullptr, -1, -1},
    {"empty_line", "nothing at all", "", 0, "empty_line", false, nullptr, nullptr, -1, -1},
    {"newline_only", "terminator with no payload", "\n", 1, "empty_line", false, nullptr, nullptr, -1, -1},
    {"no_crc_field", "no CRC appended: the trailing four characters occupy the CRC position but are not hex", "CMD PING", 8, "malformed_crc_field", false, nullptr, nullptr, -1, -1},
    {"crc_not_hex", "constructed with four non-hex characters where the CRC belongs", "CMD PING ZZZZ", 13, "malformed_crc_field", false, nullptr, nullptr, -1, -1},
    {"crc_wrong_length", "constructed with a two-character CRC field", "CMD PING AB", 11, "missing_crc_field", false, nullptr, nullptr, -1, -1},
    {"single_character_corruption", "one payload character changed; the CRC must catch it", "CMD START mode=EVAI 5274", 24, "crc_mismatch", false, nullptr, nullptr, -1, -1},
    {"single_bit_flip", "one payload bit flipped; the CRC must catch it", "CMD RTART mode=EVAL 5274", 24, "crc_mismatch", false, nullptr, nullptr, -1, -1},
    {"crc_of_a_different_line", "a valid CRC, but for different content", "CMD PING 9884", 13, "crc_mismatch", false, nullptr, nullptr, -1, -1},
    {"over_length", "longer than kMaxLineBytes; must be dropped, never truncated", "CMD BRIGHT pct=50 pad=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx 8ED5", 327, "line_too_long", false, nullptr, nullptr, -1, -1},
    {"embedded_nul", "NUL inside the payload", "CMD PI\x00""NG 0000", 14, "non_printable_character", false, nullptr, nullptr, -1, -1},
    {"embedded_tab", "tab inside the payload", "CMD\t""PING 0000", 13, "non_printable_character", false, nullptr, nullptr, -1, -1},
    {"high_bit_set", "byte above 0x7E", "CMD PING\x80"" 0000", 14, "non_printable_character", false, nullptr, nullptr, -1, -1},
    {"exactly_at_length_limit", "319 characters plus the terminator is the longest legal line", "CMD BRIGHT pct=50 pad=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx 2D97", 319, "ok", true, "BRIGHT", nullptr, -1, 50},
};
inline constexpr std::size_t kProtocolCaseCount = 40;

}  // namespace gridpulse_vectors

#endif  // GRIDPULSE_TEST_VECTORS_GEN_H_
