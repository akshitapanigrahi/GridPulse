// Native tests: the achieved bit-rate metric.
//
//     B = log2(N - 1) * max(Sc - Si, 0) / t
//
// The clamp and the guards are the parts worth testing hardest: every one of them is
// a case where a naive implementation produces a negative rate, an infinity, or a
// NaN that would then propagate into the reported score.

#include "../../firmware/src/pure/config.h"
#include "../../firmware/src/pure/scoring.h"
#include "../vectors/vectors.gen.h"
#include "test_util.h"

using gridpulse::BitRate;
using gridpulse::BitRateMbps;
using gridpulse::BitRateMbpsUs;
using gridpulse::BitRateUs;
using gridpulse::BitsPerSelection;
using gridpulse::PercentileSorted;

TEST(Scoring, MatchesGoldenVectors) {
  for (std::size_t i = 0; i < gridpulse_vectors::kScoringCaseCount; ++i) {
    const auto& vec = gridpulse_vectors::kScoringCases[i];
    CHECK_NEAR(BitRate(vec.n, vec.correct, vec.incorrect, vec.elapsed_s), vec.bit_rate,
               1e-12);
    CHECK_EQ_U(BitRateMbps(vec.n, vec.correct, vec.incorrect, vec.elapsed_s), vec.b_mbps);
    CHECK_NEAR(BitsPerSelection(vec.n), vec.bits_per_selection, 1e-12);
  }
}

TEST(Scoring, ClampsNetNegativeToExactlyZero) {
  // Si > Sc must be exactly 0, not a small negative number and not a wrapped huge
  // positive one. The unsigned-subtraction trap is the whole reason this is tested
  // at several magnitudes.
  CHECK_EQ_DOUBLE(BitRate(25, 10, 20, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 0, 1, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 0, 4000000000u, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 1, 4294967295u, 60.0), 0.0);
  CHECK_EQ_U(BitRateMbps(25, 10, 20, 60.0), 0u);
}

TEST(Scoring, NetZeroIsExactlyZero) {
  CHECK_EQ_DOUBLE(BitRate(25, 10, 10, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 0, 0, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 500, 500, 60.0), 0.0);
}

TEST(Scoring, NetOneIsTheSmallestPositiveScore) {
  const double b = BitRate(25, 11, 10, 60.0);
  CHECK_TRUE(b > 0.0);
  CHECK_NEAR(b, BitsPerSelection(25) / 60.0, 1e-12);
}

TEST(Scoring, GuardsDegenerateElapsedTime) {
  // Called every tick, including before the first target exists.
  CHECK_EQ_DOUBLE(BitRate(25, 10, 0, 0.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(25, 10, 0, -1.0), 0.0);
  CHECK_EQ_U(BitRateUs(25, 10, 0, 0), 0u);
}

TEST(Scoring, GuardsAlphabetSizesBelowTheMinimum) {
  // log2(N-1) is zero at N=2 and undefined below it. None of these may produce a
  // negative, infinite or NaN rate.
  CHECK_EQ_DOUBLE(BitRate(2, 100, 0, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(1, 100, 0, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitRate(0, 100, 0, 60.0), 0.0);
  CHECK_EQ_DOUBLE(BitsPerSelection(2), 0.0);
  CHECK_EQ_DOUBLE(BitsPerSelection(1), 0.0);
  CHECK_EQ_DOUBLE(BitsPerSelection(0), 0.0);
}

TEST(Scoring, BitsPerSelectionAtTheDesignPoints) {
  // N=3 is the minimum scorable alphabet and carries exactly one bit.
  CHECK_NEAR(BitsPerSelection(3), 1.0, 1e-15);
  // N=25 is what this game ships with.
  CHECK_NEAR(BitsPerSelection(25), 4.584962500721156, 1e-15);
  // N=24 is a run after the self-test excluded one dead cell.
  CHECK_NEAR(BitsPerSelection(24), 4.523561956057013, 1e-12);
}

TEST(Scoring, NeverReturnsNegative) {
  for (std::uint32_t sc = 0; sc < 40; ++sc) {
    for (std::uint32_t si = 0; si < 40; ++si) {
      CHECK_TRUE(BitRate(25, sc, si, 60.0) >= 0.0);
    }
  }
}

TEST(Scoring, MicrosecondOverloadAgreesWithTheSecondsOverload) {
  CHECK_NEAR(BitRateUs(25, 200, 10, 60000000ull), BitRate(25, 200, 10, 60.0), 1e-12);
  CHECK_EQ_U(BitRateMbpsUs(25, 200, 10, 60000000ull), BitRateMbps(25, 200, 10, 60.0));
  // A 60-second run is exactly 60 000 000 microseconds; the conversion must not
  // introduce drift at that scale.
  CHECK_NEAR(BitRateUs(25, 1, 0, 60000000ull), BitsPerSelection(25) / 60.0, 1e-15);
}

TEST(Scoring, MilliBitsRoundsHalfUp) {
  // B = 0.5 exactly at N=3, Sc=1, t=2.
  CHECK_EQ_U(BitRateMbps(3, 1, 0, 2.0), 500u);
  // B = 1.0 exactly.
  CHECK_EQ_U(BitRateMbps(3, 1, 0, 1.0), 1000u);
  // A rate of zero must produce zero, not a rounded-up 1.
  CHECK_EQ_U(BitRateMbps(25, 0, 0, 60.0), 0u);
}

TEST(Scoring, PercentilesUseNearestRank) {
  const std::uint32_t values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  CHECK_EQ_U(PercentileSorted(values, 10, 50), 5u);
  CHECK_EQ_U(PercentileSorted(values, 10, 95), 10u);
  CHECK_EQ_U(PercentileSorted(values, 10, 99), 10u);
  CHECK_EQ_U(PercentileSorted(values, 10, 100), 10u);
  CHECK_EQ_U(PercentileSorted(values, 10, 10), 1u);
  CHECK_EQ_U(PercentileSorted(values, 10, 0), 1u);
}

TEST(Scoring, PercentilesHandleDegenerateInput) {
  const std::uint32_t single[] = {42};
  CHECK_EQ_U(PercentileSorted(single, 1, 50), 42u);
  CHECK_EQ_U(PercentileSorted(single, 1, 99), 42u);
  CHECK_EQ_U(PercentileSorted(nullptr, 0, 50), 0u);
  CHECK_EQ_U(PercentileSorted(single, 0, 50), 0u);
}

TEST(Scoring, PercentilesMatchTheJavaScriptImplementation) {
  // web/core/session.js uses ceil(p/100 * n) - 1 on a 0-based sorted array. These
  // cases pin the two to the same rank so a latency figure means the same thing in
  // both modes.
  const std::uint32_t seven[] = {10, 20, 30, 40, 50, 60, 70};
  CHECK_EQ_U(PercentileSorted(seven, 7, 50), 40u);
  CHECK_EQ_U(PercentileSorted(seven, 7, 95), 70u);
  const std::uint32_t three[] = {100, 200, 300};
  CHECK_EQ_U(PercentileSorted(three, 3, 50), 200u);
  CHECK_EQ_U(PercentileSorted(three, 3, 99), 300u);
}

TEST(Scoring, RealisticRunProducesAPlausibleRate) {
  // A strong 60-second run: 4 presses/second at 95% accuracy.
  const std::uint32_t presses = 240;
  const std::uint32_t incorrect = 12;
  const std::uint32_t correct = presses - incorrect;
  const double b = BitRate(gridpulse::kDefaultAlphabetSize, correct, incorrect, 60.0);
  CHECK_NEAR(b, 4.584962500721156 * (228 - 12) / 60.0, 1e-12);
  CHECK_TRUE(b > 16.0 && b < 17.0);
}
