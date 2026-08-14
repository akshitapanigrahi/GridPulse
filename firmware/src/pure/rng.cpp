#include "rng.h"

namespace gridpulse {

std::uint32_t SplitMix32(std::uint32_t* state) {
  *state += 0x9E3779B9u;
  std::uint32_t z = *state;
  z ^= z >> 16;
  z *= 0x21F0AAADu;
  z ^= z >> 15;
  z *= 0x735A2D97u;
  z ^= z >> 15;
  return z;
}

Xoshiro128StarStar::Xoshiro128StarStar(std::uint32_t seed) : seed_(seed) {
  std::uint32_t x = seed;
  for (int i = 0; i < 4; ++i) {
    s_[i] = SplitMix32(&x);
  }
  if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) {
    // Unreachable for any SplitMix32-reachable seed, but the all-zero state is
    // absorbing, so the guard is required by the spec in all implementations.
    s_[0] = 1;
  }
}

std::uint32_t Xoshiro128StarStar::Next() {
  const std::uint32_t result = Rotl32(s_[1] * 5u, 7) * 9u;
  const std::uint32_t t = s_[1] << 9;

  s_[2] ^= s_[0];
  s_[3] ^= s_[1];
  s_[1] ^= s_[2];
  s_[0] ^= s_[3];
  s_[2] ^= t;
  s_[3] = Rotl32(s_[3], 11);

  return result;
}

std::uint32_t Xoshiro128StarStar::DrawIndex(std::uint32_t n, std::uint32_t* rejections) {
  if (rejections != nullptr) {
    *rejections = 0;
  }
  if (n == 0) {
    return 0;
  }

  const std::uint64_t limit = RejectionLimit(n);
  for (;;) {
    const std::uint32_t r = Next();
    if (static_cast<std::uint64_t>(r) < limit) {
      return r % n;
    }
    if (rejections != nullptr) {
      ++(*rejections);
    }
  }
}

}  // namespace gridpulse
