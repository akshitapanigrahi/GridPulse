// Native tests: healthy-cell masking and its persisted form.
//
// The stakes here are that a dead cell must NEVER be targeted - the player would be
// asked to press a key that cannot register, and the run would be unwinnable - and
// that a corrupt flash sector must never be mistaken for a real mask, which would
// silently shrink N and change the score.

#include "../../firmware/src/pure/healthmask.h"
#include "test_util.h"

using gridpulse::HealthMask;
using gridpulse::HealthRecord;
using gridpulse::kCellCount;
using gridpulse::kMinAlphabetSize;

TEST(HealthMask, DefaultsToEverythingHealthy) {
  // A device that has never been self-tested should play, not refuse.
  HealthMask mask;
  CHECK_EQ_U(mask.Count(), kCellCount);
  CHECK_TRUE(mask.IsScorable());
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    CHECK_TRUE(mask.IsHealthy(cell));
  }
}

TEST(HealthMask, MarkingCellsDead) {
  HealthMask mask;
  mask.MarkDead(12);
  CHECK_FALSE(mask.IsHealthy(12));
  CHECK_TRUE(mask.IsHealthy(11));
  CHECK_TRUE(mask.IsHealthy(13));
  CHECK_EQ_U(mask.Count(), kCellCount - 1);

  // Idempotent.
  mask.MarkDead(12);
  CHECK_EQ_U(mask.Count(), kCellCount - 1);

  mask.MarkHealthy(12);
  CHECK_TRUE(mask.IsHealthy(12));
  CHECK_EQ_U(mask.Count(), kCellCount);
}

TEST(HealthMask, OutOfRangeCellsAreNeverHealthy) {
  HealthMask mask;
  CHECK_FALSE(mask.IsHealthy(kCellCount));
  CHECK_FALSE(mask.IsHealthy(1000));
  // And marking one must not corrupt the mask.
  mask.MarkDead(kCellCount);
  mask.MarkDead(9999);
  CHECK_EQ_U(mask.Count(), kCellCount);
}

TEST(HealthMask, UnusedHighBitsAreMaskedOff) {
  // A stray high bit from a corrupt read must not inflate the count above 25.
  HealthMask mask(0xFFFFFFFFu);
  CHECK_EQ_U(mask.Count(), kCellCount);
  CHECK_EQ_U(mask.bits(), HealthMask::kAllHealthyBits);
}

TEST(HealthMask, BuildsAnAscendingAlphabet) {
  // Ascending order is what makes a run reproducible from its seed: the sampler
  // draws an alphabet INDEX, so the index-to-cell mapping must be deterministic.
  HealthMask mask;
  mask.MarkDead(0);
  mask.MarkDead(7);
  mask.MarkDead(24);

  std::uint8_t alphabet[kCellCount] = {};
  const std::size_t n = mask.BuildAlphabet(alphabet, kCellCount);
  CHECK_EQ_U(n, kCellCount - 3);

  for (std::size_t i = 1; i < n; ++i) {
    CHECK_TRUE(alphabet[i] > alphabet[i - 1]);
  }
  for (std::size_t i = 0; i < n; ++i) {
    CHECK_TRUE(alphabet[i] != 0 && alphabet[i] != 7 && alphabet[i] != 24);
    CHECK_TRUE(mask.IsHealthy(alphabet[i]));
  }
}

TEST(HealthMask, RefusesToTruncateAnAlphabet) {
  // A truncated alphabet would look entirely valid downstream while quietly
  // excluding real cells from play. Refusing is the only safe answer.
  HealthMask mask;
  std::uint8_t small[10] = {};
  CHECK_EQ_U(mask.BuildAlphabet(small, 10), 0u);
  CHECK_EQ_U(mask.BuildAlphabet(nullptr, 100), 0u);

  // Exactly enough room is fine.
  std::uint8_t exact[kCellCount] = {};
  CHECK_EQ_U(mask.BuildAlphabet(exact, kCellCount), kCellCount);
}

TEST(HealthMask, ScorabilityThreshold) {
  HealthMask mask(0);
  CHECK_FALSE(mask.IsScorable());

  mask.MarkHealthy(0);
  mask.MarkHealthy(1);
  CHECK_EQ_U(mask.Count(), 2u);
  CHECK_FALSE(mask.IsScorable());  // N=2 gives log2(1) = 0 bits per selection

  mask.MarkHealthy(2);
  CHECK_EQ_U(mask.Count(), kMinAlphabetSize);
  CHECK_TRUE(mask.IsScorable());
}

TEST(HealthRecord, RoundTripsThroughItsPersistedForm) {
  HealthMask mask;
  mask.MarkDead(3);
  mask.MarkDead(17);

  const HealthRecord record = HealthRecord::Make(mask);
  CHECK_TRUE(record.IsValid());
  CHECK_TRUE(record.ToMask() == mask);
  CHECK_EQ_U(record.ToMask().Count(), kCellCount - 2);
}

TEST(HealthRecord, RejectsAnErasedSector) {
  // Erased flash reads as all ones.
  HealthRecord erased{};
  erased.magic = 0xFFFFFFFFu;
  erased.version = 0xFFFFFFFFu;
  erased.mask = 0xFFFFFFFFu;
  erased.crc32 = 0xFFFFFFFFu;
  CHECK_FALSE(erased.IsValid());
  // And degrades to "never tested", not "everything is broken".
  CHECK_EQ_U(erased.ToMask().Count(), kCellCount);

  // A zeroed sector likewise.
  HealthRecord zeroed{};
  CHECK_FALSE(zeroed.IsValid());
  CHECK_EQ_U(zeroed.ToMask().Count(), kCellCount);
}

TEST(HealthRecord, RejectsAWrongMagic) {
  HealthMask mask;
  mask.MarkDead(5);
  HealthRecord record = HealthRecord::Make(mask);
  record.magic ^= 0x1;
  CHECK_FALSE(record.IsValid());
}

TEST(HealthRecord, RejectsAnUnknownVersion) {
  HealthMask mask;
  mask.MarkDead(5);
  HealthRecord record = HealthRecord::Make(mask);
  record.version += 1;
  CHECK_FALSE(record.IsValid());
}

TEST(HealthRecord, DetectsCorruptionOfEveryBitOfTheMask) {
  // A single flipped bit in the mask would silently change which cells are playable
  // and therefore change N and the score. The CRC must catch every one.
  HealthMask mask;
  mask.MarkDead(9);
  const HealthRecord good = HealthRecord::Make(mask);
  CHECK_TRUE(good.IsValid());

  for (int bit = 0; bit < 32; ++bit) {
    HealthRecord corrupted = good;
    corrupted.mask ^= (1u << bit);
    CHECK_FALSE(corrupted.IsValid());
  }
}

TEST(HealthRecord, DetectsCorruptionOfTheCrcItself) {
  HealthMask mask;
  const HealthRecord good = HealthRecord::Make(mask);
  for (int bit = 0; bit < 32; ++bit) {
    HealthRecord corrupted = good;
    corrupted.crc32 ^= (1u << bit);
    CHECK_FALSE(corrupted.IsValid());
  }
}

TEST(HealthRecord, LayoutIsStable) {
  // The record is written to flash as raw bytes, so its size is part of the
  // persisted format. static_assert in the header covers this too; restated here so
  // the reason is visible in the test output.
  CHECK_EQ_U(sizeof(HealthRecord), 16u);
}
