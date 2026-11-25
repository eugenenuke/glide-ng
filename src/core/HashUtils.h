#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

namespace GlideWrapper::Hash {

// Standard 64-bit FNV-1a offset basis (0xCBF29CE484222325) and prime
// (0x00000100000001B3) per Fowler-Noll-Vo specification.
inline constexpr uint64_t kFnv1aOffsetBasis64 = 14695981039346656037ULL;
inline constexpr uint64_t kFnv1aPrime64 = 1099511628211ULL;

// 32-bit golden-ratio Weyl constant floor(2^32 / phi) used by Boost
// hash_combine to disperse sequential / small integer keys.
inline constexpr uint32_t kGoldenRatio32 = 0x9E3779B9u;

// 64-bit golden-ratio Weyl constant floor(2^64 / phi) (SplitMix64 increment).
inline constexpr uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ULL;

// SplitMix64 / MurmurHash3 64-bit avalanche multiplier.
inline constexpr uint64_t kSplitMix64Mul1 = 0xBF58476D1CE4E5B9ULL;

// Standard Boost hash_combine step for composite unordered_map keys.
inline void HashCombine(std::size_t& seed, std::size_t value) noexcept {
  seed ^= value + static_cast<std::size_t>(kGoldenRatio32) + (seed << 6) +
          (seed >> 2);
}

// Fast 64-bit word-at-a-time mixer for texture / metadata deduplication.
// Processes 8 bytes per iteration (safe for unaligned inputs via memcpy) and
// applies SplitMix64-style xorshift + multiply avalanche.
inline uint64_t MixBytes64(uint64_t h, const uint8_t* data,
                           size_t len) noexcept {
  size_t i = 0;
  for (; i + 8 <= len; i += 8) {
    uint64_t w = 0;
    std::memcpy(&w, data + i, sizeof(w));
    h ^= w * kGoldenRatio64;
    h = (h ^ (h >> 27)) * kSplitMix64Mul1;
  }
  if (i < len) {
    uint64_t tail = 0;
    std::memcpy(&tail, data + i, len - i);
    h ^= (tail ^ static_cast<uint64_t>(len)) * kGoldenRatio64;
    h = (h ^ (h >> 27)) * kSplitMix64Mul1;
  }
  h ^= (h >> 31);
  return h;
}

// Ensure a hash value is never 0 (0 is reserved as "unhashed / invalid").
inline constexpr uint64_t NonZeroHash64(uint64_t h) noexcept {
  return (h == 0ULL) ? 1ULL : h;
}

// 64-bit FNV-1a byte hash (guaranteed non-zero).
inline uint64_t Fnv1aBytes64(const void* data, size_t len) noexcept {
  uint64_t h = kFnv1aOffsetBasis64;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= kFnv1aPrime64;
  }
  return NonZeroHash64(h);
}

}  // namespace GlideWrapper::Hash
