#include "TextureManager.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "core/BackendManager.h"
#include "core/GlideEntryCommon.h"
#include "core/IGraphicsBackend.h"
#include "core/SimdKernels.h"
#include "core/Telemetry.h"
#include "core/WrapperConfig.h"

namespace GlideWrapper {

namespace {
uint64_t CalcSwizzledUploadBytes(const VirtualTexture& tex) {
  uint64_t total = 0;
  if (tex.swizzledMipLevels.empty() && !tex.rawCompressedMipLevels.empty()) {
    for (const auto& lvl : tex.rawCompressedMipLevels) {
      total += static_cast<uint64_t>(lvl.size());
    }
    return total;
  }
  for (const auto& lvl : tex.swizzledMipLevels) {
    total += static_cast<uint64_t>(lvl.size()) * sizeof(uint32_t);
  }
  return total;
}
GuNccTable GetActiveNccTableOrDefault(const GuNccTable* nccTables,
                                      const bool* nccTablesLoaded,
                                      uint32_t activeNccTable) {
  if (nccTablesLoaded[activeNccTable]) {
    return nccTables[activeNccTable];
  }
  // Return a default identity table (grayscale mapping)
  GuNccTable ncc;
  for (int y = 0; y < 16; ++y) {
    int val = y * 255 / 15;
    ncc.yRGB[y] = val;
  }
  std::memset(ncc.iRGB, 0, sizeof(ncc.iRGB));
  std::memset(ncc.qRGB, 0, sizeof(ncc.qRGB));
  return ncc;
}

inline int16_t sign_extend_9bit(int16_t val) {
  // If bit 8 is set, it represents a negative 9-bit two's complement number
  if (val & 0x0100) {
    return val - 512;
  }
  return val;
}

uint32_t swizzleYiq(uint8_t val, const GuNccTable& ncc) {
  uint8_t y = (val >> 4) & 0xF;
  uint8_t i = (val >> 2) & 0x3;
  uint8_t q = val & 0x3;

  int r = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][0]) +
          sign_extend_9bit(ncc.qRGB[q][0]);
  int g = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][1]) +
          sign_extend_9bit(ncc.qRGB[q][1]);
  int b = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][2]) +
          sign_extend_9bit(ncc.qRGB[q][2]);

  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  return (255u << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t swizzleAyiq(uint16_t val, const GuNccTable& ncc) {
  uint8_t a = (val >> 8) & 0xFF;
  uint8_t y = (val >> 4) & 0xF;
  uint8_t i = (val >> 2) & 0x3;
  uint8_t q = val & 0x3;

  int r = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][0]) +
          sign_extend_9bit(ncc.qRGB[q][0]);
  int g = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][1]) +
          sign_extend_9bit(ncc.qRGB[q][1]);
  int b = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][2]) +
          sign_extend_9bit(ncc.qRGB[q][2]);

  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t swizzleAp88(uint16_t val, const uint32_t* palette) {
  uint8_t a = (val >> 8) & 0xFF;
  uint8_t idx = val & 0xFF;
  uint32_t color = palette[idx];
  return (static_cast<uint32_t>(a) << 24) | (color & 0x00FFFFFF);
}

uint64_t HashTableBytes(const void* data, size_t len) {
  uint64_t h = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= kPrime;
  }
  return (h == 0) ? 1ULL : h;
}

// Fast 64-bit word-at-a-time hash for raw texture mipmap deduplication.
// Processes 8 bytes per iteration (aligned or unaligned via memcpy) and mixes
// with a 64-bit odd multiplier + xorshift for strong avalanche.
inline uint64_t FastHashBytes64(uint64_t h, const uint8_t* data, size_t len) {
  constexpr uint64_t kMul = 0x9E3779B97F4A7C15ULL;
  constexpr uint64_t kMul2 = 0xBF58476D1CE4E5B9ULL;
  size_t i = 0;
  for (; i + 8 <= len; i += 8) {
    uint64_t w = 0;
    std::memcpy(&w, data + i, sizeof(w));
    h ^= w * kMul;
    h = (h ^ (h >> 27)) * kMul2;
  }
  if (i < len) {
    uint64_t tail = 0;
    std::memcpy(&tail, data + i, len - i);
    h ^= (tail ^ static_cast<uint64_t>(len)) * kMul;
    h = (h ^ (h >> 27)) * kMul2;
  }
  h ^= (h >> 31);
  return (h == 0) ? 1ULL : h;
}

template <size_t N, uint32_t MaxVal>
constexpr std::array<uint8_t, N> MakeExpandLut() {
  std::array<uint8_t, N> lut{};
  for (size_t i = 0; i < N; ++i) {
    lut[i] = static_cast<uint8_t>((i * 255u) / MaxVal);
  }
  return lut;
}

constexpr auto kExpand5 = MakeExpandLut<32, 31>();
constexpr auto kExpand6 = MakeExpandLut<64, 63>();
constexpr auto kExpand3 = MakeExpandLut<8, 7>();

constexpr uint32_t kTexFmtFxt1 = 0x11u;
constexpr uint32_t kTexFmtArgb8888 = 0x12u;
constexpr uint32_t kTexFmtYuyv422 = 0x13u;
constexpr uint32_t kTexFmtUyvy422 = 0x14u;
constexpr uint32_t kTexFmtAyuv444 = 0x15u;
constexpr uint32_t kTexFmtDxt1 = 0x16u;
constexpr uint32_t kTexFmtDxt2 = 0x17u;
constexpr uint32_t kTexFmtDxt3 = 0x18u;
constexpr uint32_t kTexFmtDxt4 = 0x19u;
constexpr uint32_t kTexFmtDxt5 = 0x1Au;
constexpr uint32_t kTexFmtRgb888 = 0xFFu;

inline bool IsHardwareBcnFormat(uint32_t format) {
  return format >= kTexFmtDxt1 && format <= kTexFmtDxt5;
}

inline uint32_t CalculateLevelByteSize(uint32_t w, uint32_t h,
                                       uint32_t format) {
  switch (format) {
    case kTexFmtFxt1:
      return ((w + 7u) / 8u) * ((h + 3u) / 4u) * 16u;
    case kTexFmtDxt1:
      return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 8u;
    case kTexFmtDxt2:
    case kTexFmtDxt3:
    case kTexFmtDxt4:
    case kTexFmtDxt5:
      return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 16u;
    case kTexFmtArgb8888:
    case kTexFmtAyuv444:
      return w * h * 4u;
    case kTexFmtRgb888:
      return w * h * 3u;
    case kTexFmtYuyv422:
    case kTexFmtUyvy422:
      return w * h * 2u;
    default: {
      const uint32_t bpp = (format >= 0x10u) ? 4u : ((format >= 0x8u) ? 2u : 1u);
      return w * h * bpp;
    }
  }
}

// BT.601 studio-swing YUV -> RGB matching 3dfx Texus2 (_txCalcRGBFromYUV).
inline uint32_t CalcRgbFromYuv(uint32_t y, uint32_t u, uint32_t v,
                               uint32_t alpha) {
  const int y16 = static_cast<int>(y) - 16;
  const int u128 = static_cast<int>(u) - 128;
  const int v128 = static_cast<int>(v) - 128;
  const int r = std::clamp(
      static_cast<int>(1.164f * y16 + 1.596f * v128 + 0.5f), 0, 255);
  const int g = std::clamp(
      static_cast<int>(1.164f * y16 - 0.391f * u128 - 0.813f * v128 + 0.5f), 0,
      255);
  const int b = std::clamp(
      static_cast<int>(1.164f * y16 + 2.018f * u128 + 0.5f), 0, 255);
  return (alpha << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

inline void DecodeDxtColorBlock(const uint8_t blk[8], bool allowTransparent,
                                uint32_t out16[16]) {
  uint16_t c0 = 0, c1 = 0;
  uint32_t indices = 0;
  std::memcpy(&c0, blk + 0, 2);
  std::memcpy(&c1, blk + 2, 2);
  std::memcpy(&indices, blk + 4, 4);

  uint32_t pal[4];
  const uint32_t r0 = kExpand5[(c0 >> 11) & 0x1Fu];
  const uint32_t g0 = kExpand6[(c0 >> 5) & 0x3Fu];
  const uint32_t b0 = kExpand5[c0 & 0x1Fu];
  const uint32_t r1 = kExpand5[(c1 >> 11) & 0x1Fu];
  const uint32_t g1 = kExpand6[(c1 >> 5) & 0x3Fu];
  const uint32_t b1 = kExpand5[c1 & 0x1Fu];

  pal[0] = 0xFF000000u | (r0 << 16) | (g0 << 8) | b0;
  pal[1] = 0xFF000000u | (r1 << 16) | (g1 << 8) | b1;
  if (!allowTransparent || c0 > c1) {
    const uint32_t r2 = (2u * r0 + r1 + 1u) / 3u;
    const uint32_t g2 = (2u * g0 + g1 + 1u) / 3u;
    const uint32_t b2 = (2u * b0 + b1 + 1u) / 3u;
    const uint32_t r3 = (r0 + 2u * r1 + 1u) / 3u;
    const uint32_t g3 = (g0 + 2u * g1 + 1u) / 3u;
    const uint32_t b3 = (b0 + 2u * b1 + 1u) / 3u;
    pal[2] = 0xFF000000u | (r2 << 16) | (g2 << 8) | b2;
    pal[3] = 0xFF000000u | (r3 << 16) | (g3 << 8) | b3;
  } else {
    const uint32_t r2 = (r0 + r1) / 2u;
    const uint32_t g2 = (g0 + g1) / 2u;
    const uint32_t b2 = (b0 + b1) / 2u;
    pal[2] = 0xFF000000u | (r2 << 16) | (g2 << 8) | b2;
    pal[3] = 0x00000000u;
  }

  for (int i = 0; i < 16; ++i) {
    out16[i] = pal[(indices >> (i * 2)) & 3u];
  }
}

inline void DecodeDxt3AlphaBlock(const uint8_t blk[8], uint8_t outAlpha[16]) {
  uint64_t bits = 0;
  std::memcpy(&bits, blk, 8);
  for (int i = 0; i < 16; ++i) {
    outAlpha[i] = static_cast<uint8_t>(((bits >> (i * 4)) & 0xFu) * 17u);
  }
}

inline void DecodeDxt5AlphaBlock(const uint8_t blk[8], uint8_t outAlpha[16]) {
  const uint32_t a0 = blk[0];
  const uint32_t a1 = blk[1];
  uint8_t pal[8];
  pal[0] = static_cast<uint8_t>(a0);
  pal[1] = static_cast<uint8_t>(a1);
  if (a0 > a1) {
    for (uint32_t k = 1; k <= 6; ++k) {
      pal[1 + k] = static_cast<uint8_t>(((7u - k) * a0 + k * a1 + 3u) / 7u);
    }
  } else {
    for (uint32_t k = 1; k <= 4; ++k) {
      pal[1 + k] = static_cast<uint8_t>(((5u - k) * a0 + k * a1 + 2u) / 5u);
    }
    pal[6] = 0u;
    pal[7] = 255u;
  }
  uint64_t idxBits = 0;
  for (int b = 0; b < 6; ++b) {
    idxBits |= static_cast<uint64_t>(blk[2 + b]) << (b * 8);
  }
  for (int i = 0; i < 16; ++i) {
    outAlpha[i] = pal[(idxBits >> (i * 3)) & 7u];
  }
}

inline uint32_t ReadFxt1Bits(const uint32_t cc[4], int bitPos, int count) {
  const int word = bitPos >> 5;
  const int shift = bitPos & 31;
  uint64_t val = cc[word];
  if (word < 3) {
    val |= (static_cast<uint64_t>(cc[word + 1]) << 32);
  }
  return static_cast<uint32_t>((val >> shift) & ((1u << count) - 1u));
}

inline uint32_t Fxt1Lerp(uint32_t n, uint32_t t, uint32_t c0, uint32_t c1) {
  return ((n - t) * c0 + t * c1 + n / 2u) / n;
}

inline void DecodeFxt1Block(const uint8_t blk[16], uint32_t out8x4[32]) {
  uint32_t cc[4];
  std::memcpy(cc, blk, 16);
  const uint32_t mode = ReadFxt1Bits(cc, 125, 3);

  uint32_t linear[32];

  if ((mode & 6u) == 0u) {
    // CC_HI (mode "00?"): 2 RGB555 endpoints + 3-bit indices (idx 7 = transparent)
    const uint32_t b0 = kExpand5[ReadFxt1Bits(cc, 96, 5)];
    const uint32_t g0 = kExpand5[ReadFxt1Bits(cc, 101, 5)];
    const uint32_t r0 = kExpand5[ReadFxt1Bits(cc, 106, 5)];
    const uint32_t b1 = kExpand5[ReadFxt1Bits(cc, 111, 5)];
    const uint32_t g1 = kExpand5[ReadFxt1Bits(cc, 116, 5)];
    const uint32_t r1 = kExpand5[ReadFxt1Bits(cc, 121, 5)];
    for (int t = 0; t < 32; ++t) {
      const uint32_t idx = ReadFxt1Bits(cc, t * 3, 3);
      if (idx == 7u) {
        linear[t] = 0x00000000u;
      } else {
        const uint32_t r = Fxt1Lerp(6u, idx, r0, r1);
        const uint32_t g = Fxt1Lerp(6u, idx, g0, g1);
        const uint32_t b = Fxt1Lerp(6u, idx, b0, b1);
        linear[t] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
    }
  } else if (mode == 2u) {
    // CC_CHROMA (mode "010"): 4 opaque RGB555 colors + 2-bit direct indices
    uint32_t pal[4];
    for (int k = 0; k < 4; ++k) {
      const int base = 64 + k * 15;
      const uint32_t b = kExpand5[ReadFxt1Bits(cc, base, 5)];
      const uint32_t g = kExpand5[ReadFxt1Bits(cc, base + 5, 5)];
      const uint32_t r = kExpand5[ReadFxt1Bits(cc, base + 10, 5)];
      pal[k] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    for (int t = 0; t < 32; ++t) {
      linear[t] = pal[ReadFxt1Bits(cc, t * 2, 2)];
    }
  } else if (mode == 3u) {
    // CC_ALPHA (mode "011"): 3 ARGB5555 colors + lerp bit 124
    uint32_t cr[3], cg[3], cb[3], ca[3];
    for (int k = 0; k < 3; ++k) {
      const int base = 64 + k * 15;
      cb[k] = kExpand5[ReadFxt1Bits(cc, base, 5)];
      cg[k] = kExpand5[ReadFxt1Bits(cc, base + 5, 5)];
      cr[k] = kExpand5[ReadFxt1Bits(cc, base + 10, 5)];
      ca[k] = kExpand5[ReadFxt1Bits(cc, 109 + k * 5, 5)];
    }
    const bool lerp = (ReadFxt1Bits(cc, 124, 1) != 0u);
    if (lerp) {
      for (int t = 0; t < 32; ++t) {
        const int cLo = (t < 16) ? 0 : 2;
        const uint32_t idx = ReadFxt1Bits(cc, t * 2, 2);
        const uint32_t r = Fxt1Lerp(3u, idx, cr[cLo], cr[1]);
        const uint32_t g = Fxt1Lerp(3u, idx, cg[cLo], cg[1]);
        const uint32_t b = Fxt1Lerp(3u, idx, cb[cLo], cb[1]);
        const uint32_t a = Fxt1Lerp(3u, idx, ca[cLo], ca[1]);
        linear[t] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    } else {
      for (int t = 0; t < 32; ++t) {
        const uint32_t idx = ReadFxt1Bits(cc, t * 2, 2);
        if (idx == 3u) {
          linear[t] = 0x00000000u;
        } else {
          linear[t] =
              (ca[idx] << 24) | (cr[idx] << 16) | (cg[idx] << 8) | cb[idx];
        }
      }
    }
  } else {
    // CC_MIXED (mode "1??"): two 4x4 sub-blocks, each with its own endpoint pair
    const bool hasAlpha = (ReadFxt1Bits(cc, 124, 1) != 0u);
    for (int half = 0; half < 2; ++half) {
      const int base0 = 64 + half * 30;
      const int base1 = base0 + 15;
      const uint32_t b0 = kExpand5[ReadFxt1Bits(cc, base0, 5)];
      const uint32_t g0Raw = ReadFxt1Bits(cc, base0 + 5, 5);
      const uint32_t r0 = kExpand5[ReadFxt1Bits(cc, base0 + 10, 5)];
      const uint32_t b1 = kExpand5[ReadFxt1Bits(cc, base1, 5)];
      const uint32_t g1Raw = ReadFxt1Bits(cc, base1 + 5, 5);
      const uint32_t r1 = kExpand5[ReadFxt1Bits(cc, base1 + 10, 5)];
      const uint32_t glsb = ReadFxt1Bits(cc, 125 + half, 1);
      const uint32_t selb = ReadFxt1Bits(cc, half == 0 ? 1 : 33, 1);

      const uint32_t g1 = kExpand6[(g1Raw << 1) | glsb];
      if (hasAlpha) {
        const uint32_t g0 = kExpand5[g0Raw];
        for (int k = 0; k < 16; ++k) {
          const int t = half * 16 + k;
          const uint32_t idx = ReadFxt1Bits(cc, t * 2, 2);
          if (idx == 3u) {
            linear[t] = 0x00000000u;
          } else if (idx == 0u) {
            linear[t] = 0xFF000000u | (r0 << 16) | (g0 << 8) | b0;
          } else if (idx == 2u) {
            linear[t] = 0xFF000000u | (r1 << 16) | (g1 << 8) | b1;
          } else {
            const uint32_t rm = (r0 + r1) / 2u;
            const uint32_t gm = (g0 + g1) / 2u;
            const uint32_t bm = (b0 + b1) / 2u;
            linear[t] = 0xFF000000u | (rm << 16) | (gm << 8) | bm;
          }
        }
      } else {
        const uint32_t g0 = kExpand6[(g0Raw << 1) | (glsb ^ selb)];
        for (int k = 0; k < 16; ++k) {
          const int t = half * 16 + k;
          const uint32_t idx = ReadFxt1Bits(cc, t * 2, 2);
          const uint32_t r = Fxt1Lerp(3u, idx, r0, r1);
          const uint32_t g = Fxt1Lerp(3u, idx, g0, g1);
          const uint32_t b = Fxt1Lerp(3u, idx, b0, b1);
          linear[t] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
      }
    }
  }

  // Remap two 4x4 sub-blocks (0..15 left, 16..31 right) into row-major 8x4.
  for (int t = 0; t < 32; ++t) {
    const int lx = ((t & 16) ? 4 : 0) + (t & 3);
    const int ly = (t >> 2) & 3;
    out8x4[ly * 8 + lx] = linear[t];
  }
}

// Hoisted per-format texel decoder producing canonical 32-bit BGRA (0xAARRGGBB).
// Optional raw output pointers are populated when non-null for paletted/NCC
// formats so later palette/NCC table reloads can re-swizzle without guest data.
void DecodeTexelsToBgra(uint32_t format, const uint8_t* rawSrc, uint32_t width,
                        uint32_t height, uint32_t* dstBgra, bool paletteLoaded,
                        const uint32_t* paletteTable,
                        const GuNccTable* activeNcc, uint8_t* dstRaw8,
                        uint8_t* dstRawYiq, uint16_t* dstRawAyiq,
                        uint16_t* dstRawAp88) {
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (count == 0) return;

  switch (format) {
    case kTexFmtArgb8888: {
      // 32-bit ARGB_8888 is already canonical 0xAARRGGBB in little-endian.
      std::memcpy(dstBgra, rawSrc, count * sizeof(uint32_t));
      return;
    }
    case kTexFmtRgb888: {
      for (size_t i = 0; i < count; ++i) {
        const uint32_t r = rawSrc[i * 3u + 0u];
        const uint32_t g = rawSrc[i * 3u + 1u];
        const uint32_t b = rawSrc[i * 3u + 2u];
        dstBgra[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
      return;
    }
    case kTexFmtYuyv422:
    case kTexFmtUyvy422: {
      const bool isYuyv = (format == kTexFmtYuyv422);
      const auto* src16 = reinterpret_cast<const uint16_t*>(rawSrc);
      size_t i = 0;
      for (; i + 1 < count; i += 2) {
        const uint16_t w0 = src16[i];
        const uint16_t w1 = src16[i + 1];
        const uint32_t y0 = isYuyv ? (w0 & 0xFFu) : (w0 >> 8);
        const uint32_t u = isYuyv ? (w0 >> 8) : (w0 & 0xFFu);
        const uint32_t y1 = isYuyv ? (w1 & 0xFFu) : (w1 >> 8);
        const uint32_t v = isYuyv ? (w1 >> 8) : (w1 & 0xFFu);
        dstBgra[i] = CalcRgbFromYuv(y0, u, v, 0xFFu);
        dstBgra[i + 1] = CalcRgbFromYuv(y1, u, v, 0xFFu);
      }
      if (i < count) {
        const uint16_t w0 = src16[i];
        const uint32_t y0 = isYuyv ? (w0 & 0xFFu) : (w0 >> 8);
        const uint32_t u = isYuyv ? (w0 >> 8) : (w0 & 0xFFu);
        dstBgra[i] = CalcRgbFromYuv(y0, u, 128u, 0xFFu);
      }
      return;
    }
    case kTexFmtAyuv444: {
      const auto* src32 = reinterpret_cast<const uint32_t*>(rawSrc);
      for (size_t i = 0; i < count; ++i) {
        const uint32_t val = src32[i];
        const uint32_t a = (val >> 24) & 0xFFu;
        const uint32_t y = (val >> 16) & 0xFFu;
        const uint32_t u = (val >> 8) & 0xFFu;
        const uint32_t v = val & 0xFFu;
        dstBgra[i] = CalcRgbFromYuv(y, u, v, a);
      }
      return;
    }
    case kTexFmtFxt1: {
      const uint32_t blocksX = (width + 7u) / 8u;
      const uint32_t blocksY = (height + 3u) / 4u;
      const uint8_t* blk = rawSrc;
      uint32_t decoded[32];
      for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
          DecodeFxt1Block(blk, decoded);
          blk += 16;
          for (uint32_t ly = 0; ly < 4u; ++ly) {
            const uint32_t py = by * 4u + ly;
            if (py >= height) break;
            for (uint32_t lx = 0; lx < 8u; ++lx) {
              const uint32_t px = bx * 8u + lx;
              if (px >= width) break;
              dstBgra[static_cast<size_t>(py) * width + px] =
                  decoded[ly * 8u + lx];
            }
          }
        }
      }
      return;
    }
    case kTexFmtDxt1: {
      const uint32_t blocksX = (width + 3u) / 4u;
      const uint32_t blocksY = (height + 3u) / 4u;
      const uint8_t* blk = rawSrc;
      uint32_t decoded[16];
      for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
          DecodeDxtColorBlock(blk, /*allowTransparent=*/true, decoded);
          blk += 8;
          for (uint32_t ly = 0; ly < 4u; ++ly) {
            const uint32_t py = by * 4u + ly;
            if (py >= height) break;
            for (uint32_t lx = 0; lx < 4u; ++lx) {
              const uint32_t px = bx * 4u + lx;
              if (px >= width) break;
              dstBgra[static_cast<size_t>(py) * width + px] =
                  decoded[ly * 4u + lx];
            }
          }
        }
      }
      return;
    }
    case kTexFmtDxt2:
    case kTexFmtDxt3:
    case kTexFmtDxt4:
    case kTexFmtDxt5: {
      const bool isDxt5 = (format == kTexFmtDxt4 || format == kTexFmtDxt5);
      const uint32_t blocksX = (width + 3u) / 4u;
      const uint32_t blocksY = (height + 3u) / 4u;
      const uint8_t* blk = rawSrc;
      uint8_t alphas[16];
      uint32_t colors[16];
      for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
          if (isDxt5) {
            DecodeDxt5AlphaBlock(blk, alphas);
          } else {
            DecodeDxt3AlphaBlock(blk, alphas);
          }
          DecodeDxtColorBlock(blk + 8, /*allowTransparent=*/false, colors);
          blk += 16;
          for (uint32_t ly = 0; ly < 4u; ++ly) {
            const uint32_t py = by * 4u + ly;
            if (py >= height) break;
            for (uint32_t lx = 0; lx < 4u; ++lx) {
              const uint32_t px = bx * 4u + lx;
              if (px >= width) break;
              const uint32_t idx = ly * 4u + lx;
              dstBgra[static_cast<size_t>(py) * width + px] =
                  (static_cast<uint32_t>(alphas[idx]) << 24) |
                  (colors[idx] & 0x00FFFFFFu);
            }
          }
        }
      }
      return;
    }
    case 0xa: {  // RGB_565
      GetSimdKernels().ConvertRgb565ToBgra32(
          reinterpret_cast<const uint16_t*>(rawSrc), dstBgra, count);
      return;
    }
    case 0xc: {  // ARGB_4444
      GetSimdKernels().ConvertArgb4444ToBgra32(
          reinterpret_cast<const uint16_t*>(rawSrc), dstBgra, count);
      return;
    }
    case 0xb: {  // ARGB_1555
      GetSimdKernels().ConvertArgb1555ToBgra32(
          reinterpret_cast<const uint16_t*>(rawSrc), dstBgra, count,
          /*preserveAlpha=*/true);
      return;
    }
    case 0xd: {  // ALPHA_INTENSITY_88
      const auto* src16 = reinterpret_cast<const uint16_t*>(rawSrc);
      for (size_t i = 0; i < count; ++i) {
        const uint16_t val = src16[i];
        const uint32_t a = (val >> 8) & 0xFFu;
        const uint32_t intensity = val & 0xFFu;
        dstBgra[i] = (a << 24) | (intensity * 0x00010101u);
      }
      return;
    }
    case 0x8: {  // ARGB_8332
      const auto* src16 = reinterpret_cast<const uint16_t*>(rawSrc);
      for (size_t i = 0; i < count; ++i) {
        const uint16_t val = src16[i];
        const uint32_t a = (val >> 8) & 0xFFu;
        const uint32_t r = kExpand3[(val >> 5) & 0x7u];
        const uint32_t g = kExpand3[(val >> 2) & 0x7u];
        const uint32_t b = (val & 0x3u) * 85u;
        dstBgra[i] = (a << 24) | (r << 16) | (g << 8) | b;
      }
      return;
    }
    case 0x9: {  // AYIQ_8422
      const auto* src16 = reinterpret_cast<const uint16_t*>(rawSrc);
      if (dstRawAyiq) {
        std::memcpy(dstRawAyiq, src16, count * sizeof(uint16_t));
      }
      for (size_t i = 0; i < count; ++i) {
        dstBgra[i] = swizzleAyiq(src16[i], *activeNcc);
      }
      return;
    }
    case 0xe: {  // AP_88
      const auto* src16 = reinterpret_cast<const uint16_t*>(rawSrc);
      if (dstRawAp88) {
        std::memcpy(dstRawAp88, src16, count * sizeof(uint16_t));
      }
      if (paletteLoaded) {
        for (size_t i = 0; i < count; ++i) {
          dstBgra[i] = swizzleAp88(src16[i], paletteTable);
        }
      } else {
        for (size_t i = 0; i < count; ++i) {
          const uint16_t val = src16[i];
          const uint32_t a = (val >> 8) & 0xFFu;
          const uint32_t idx = val & 0xFFu;
          dstBgra[i] = (a << 24) | (idx * 0x00010101u);
        }
      }
      return;
    }
    case 0x5:
    case 0x6: {  // P_8 or P_8_6666
      if (dstRaw8) {
        std::memcpy(dstRaw8, rawSrc, count);
      }
      if (paletteLoaded) {
        GetSimdKernels().DecodeP8ToBgra32(rawSrc, paletteTable, dstBgra, count);
      } else {
        for (size_t i = 0; i < count; ++i) {
          const uint32_t val = rawSrc[i];
          dstBgra[i] = 0xFF000000u | (val * 0x00010101u);
        }
      }
      return;
    }
    case 0x2: {  // ALPHA_8
      for (size_t i = 0; i < count; ++i) {
        const uint32_t val = rawSrc[i];
        dstBgra[i] = val * 0x01010101u;
      }
      return;
    }
    case 0x3: {  // INTENSITY_8
      for (size_t i = 0; i < count; ++i) {
        const uint32_t val = rawSrc[i];
        dstBgra[i] = 0xFF000000u | (val * 0x00010101u);
      }
      return;
    }
    case 0x0: {  // RGB_332
      for (size_t i = 0; i < count; ++i) {
        const uint8_t val = rawSrc[i];
        const uint32_t r = kExpand3[(val >> 5) & 0x7u];
        const uint32_t g = kExpand3[(val >> 2) & 0x7u];
        const uint32_t b = (val & 0x3u) * 85u;
        dstBgra[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
      return;
    }
    case 0x1: {  // YIQ_422
      if (dstRawYiq) {
        std::memcpy(dstRawYiq, rawSrc, count);
      }
      for (size_t i = 0; i < count; ++i) {
        dstBgra[i] = swizzleYiq(rawSrc[i], *activeNcc);
      }
      return;
    }
    case 0x4: {  // ALPHA_INTENSITY_44
      for (size_t i = 0; i < count; ++i) {
        const uint8_t val = rawSrc[i];
        const uint32_t a = ((val >> 4) & 0xFu) * 17u;
        const uint32_t intensity = (val & 0xFu) * 17u;
        dstBgra[i] = (a << 24) | (intensity * 0x00010101u);
      }
      return;
    }
    default: {
      if (format >= 0x8) {
        // Unknown 16-bit format fallback (matches prior default: a=255, r=g=b=0)
        std::fill(dstBgra, dstBgra + count, 0xFF000000u);
      } else {
        for (size_t i = 0; i < count; ++i) {
          const uint32_t val = rawSrc[i];
          dstBgra[i] = 0xFF000000u | (val * 0x00010101u);
        }
      }
      return;
    }
  }
}

uint32_t LodToDimension(int32_t lod) {
  return (lod >= 0) ? (256 >> lod) : (256 << (-lod));
}

void ComputeLodDimensions(int32_t lod, uint32_t aspect, uint32_t& outW,
                          uint32_t& outH) {
  const uint32_t maxDim = LodToDimension(lod);
  outW = maxDim;
  outH = maxDim;
  switch (aspect) {
    case 0:
      outH = std::max(1u, maxDim / 8);
      break;  // 8x1
    case 1:
      outH = std::max(1u, maxDim / 4);
      break;  // 4x1
    case 2:
      outH = std::max(1u, maxDim / 2);
      break;  // 2x1
    case 3:
      break;  // 1x1
    case 4:
      outW = std::max(1u, maxDim / 2);
      break;  // 1x2
    case 5:
      outW = std::max(1u, maxDim / 4);
      break;  // 1x4
    case 6:
      outW = std::max(1u, maxDim / 8);
      break;  // 1x8
  }
}
}  // namespace

uint64_t TextureManager::GetActiveTableHash_Locked(const TmuState& state,
                                                   const VirtualTexture& tex) {
  if (tex.isPaletted) {
    return state.paletteTableLoaded ? state.paletteHash : 0;
  }
  if (tex.isNcc) {
    return state.nccTablesLoaded[state.activeNccTable]
               ? state.nccHashes[state.activeNccTable]
               : 0;
  }
  return 0;
}

void TextureManager::ReswizzleTexture_Locked(const TmuState& state,
                                             VirtualTexture& tex) {
  if (tex.isPaletted) {
    if (tex.format == 0x5 || tex.format == 0x6) {  // P_8 or P_8_6666
      const size_t nLevels = tex.raw8BitMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw8 = tex.raw8BitMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw8.size());
        if (state.paletteTableLoaded) {
          GetSimdKernels().DecodeP8ToBgra32(raw8.data(), state.paletteTable,
                                            dst.data(), raw8.size());
        } else {
          for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
            uint8_t val = raw8[pIdx];
            dst[pIdx] = (255u << 24) | (static_cast<uint32_t>(val) << 16) |
                        (static_cast<uint32_t>(val) << 8) | val;
          }
        }
      }
    } else if (tex.format == 0xe) {  // AP_88
      const size_t nLevels = tex.rawAp88MipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw16 = tex.rawAp88MipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw16.size());
        if (state.paletteTableLoaded) {
          for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
            dst[pIdx] = swizzleAp88(raw16[pIdx], state.paletteTable);
          }
        } else {
          for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
            uint8_t a = static_cast<uint8_t>((raw16[pIdx] >> 8) & 0xFF);
            uint8_t idx = static_cast<uint8_t>(raw16[pIdx] & 0xFF);
            dst[pIdx] = (static_cast<uint32_t>(a) << 24) |
                        (static_cast<uint32_t>(idx) << 16) |
                        (static_cast<uint32_t>(idx) << 8) | idx;
          }
        }
      }
    }
    tex.swizzledWithTable = state.paletteTableLoaded;
    tex.swizzledTableHash = GetActiveTableHash_Locked(state, tex);
  } else if (tex.isNcc) {
    const GuNccTable activeNcc = GetActiveNccTableOrDefault(
        state.nccTables, state.nccTablesLoaded, state.activeNccTable);
    if (tex.format == 0x1) {  // YIQ_422
      const size_t nLevels = tex.rawYiqMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw8 = tex.rawYiqMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw8.size());
        for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
          dst[pIdx] = swizzleYiq(raw8[pIdx], activeNcc);
        }
      }
    } else if (tex.format == 0x9) {  // AYIQ_8422
      const size_t nLevels = tex.rawAyiqMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw16 = tex.rawAyiqMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw16.size());
        for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
          dst[pIdx] = swizzleAyiq(raw16[pIdx], activeNcc);
        }
      }
    }
    tex.swizzledWithTable = state.nccTablesLoaded[state.activeNccTable];
    tex.swizzledTableHash = GetActiveTableHash_Locked(state, tex);
  }
}

void TextureManager::EnsureCompressedSwizzled_Locked(VirtualTexture& tex) {
  if (!tex.swizzledMipLevels.empty() || tex.rawCompressedMipLevels.empty()) {
    return;
  }
  const size_t nLevels = tex.rawCompressedMipLevels.size();
  tex.swizzledMipLevels.resize(nLevels);
  for (size_t i = 0; i < nLevels; ++i) {
    uint32_t lw = std::max(1u, tex.baseWidth >> i);
    uint32_t lh = std::max(1u, tex.baseHeight >> i);
    const size_t texelCount = static_cast<size_t>(lw) * lh;
    tex.swizzledMipLevels[i].resize(texelCount, 0u);
    const auto& rawLvl = tex.rawCompressedMipLevels[i];
    if (rawLvl.size() >= CalculateLevelByteSize(lw, lh, tex.format)) {
      DecodeTexelsToBgra(tex.format, rawLvl.data(), lw, lh,
                         tex.swizzledMipLevels[i].data(), false, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
    }
  }
}

void TextureManager::EnsureTextureReady_Locked(
    uint32_t tmu, std::shared_ptr<VirtualTexture>& texPtr,
    bool uploadToBackend) {
  if (!texPtr || tmu >= 2) return;

  if (texPtr->isPaletted || texPtr->isNcc) {
    const auto& state = TableState_Locked(tmu);
    const uint64_t activeHash = GetActiveTableHash_Locked(state, *texPtr);

    const bool needsSwizzle =
        (texPtr->isPaletted && state.paletteTableLoaded &&
         (!texPtr->swizzledWithTable ||
          texPtr->swizzledTableHash != activeHash)) ||
        (texPtr->isNcc && (texPtr->swizzledTableHash != activeHash));

    if (needsSwizzle) {
      // Copy-on-write: never mutate a VirtualTexture snapshot still held by
      // SoftwareBackend's deferred tile bins.
      if (texPtr.use_count() > 1) {
        texPtr = std::make_shared<VirtualTexture>(*texPtr);
      }
      ReswizzleTexture_Locked(state, *texPtr);
    }
  }

  if (uploadToBackend) {
    const bool hasCpuData = !texPtr->swizzledMipLevels.empty() ||
                            !texPtr->rawCompressedMipLevels.empty();
    const bool tableChanged =
        (texPtr->isPaletted || texPtr->isNcc) &&
        (texPtr->uploadedTableHashPerTmu[tmu] != texPtr->swizzledTableHash);
    if (hasCpuData && (!texPtr->uploadedToBackendPerTmu[tmu] || tableChanged)) {
      if (auto* b = BackendManager::GetInstance().GetBackend()) {
        TelemetryManager::GetInstance().RecordTexUpload(
            CalcSwizzledUploadBytes(*texPtr));
        b->UploadTexture(tmu, texPtr->startAddress, *texPtr);
        texPtr->uploadedToBackend = true;
        texPtr->uploadedTableHash = texPtr->swizzledTableHash;
        texPtr->uploadedToBackendPerTmu[tmu] = true;
        texPtr->uploadedTableHashPerTmu[tmu] = texPtr->swizzledTableHash;
      }
    }
  }
}

uint32_t TextureManager::CalculateMemoryRequired(int32_t lodMin,
                                                 int32_t lodMax,
                                                 uint32_t aspect,
                                                 uint32_t format, bool packed) {
  int32_t start = std::min(lodMin, lodMax);
  int32_t end = std::max(lodMin, lodMax);

  uint32_t totalBytes = 0;

  for (int32_t i = start; i <= end; ++i) {
    uint32_t w = 1, h = 1;
    ComputeLodDimensions(i, aspect, w, h);

    const uint32_t levelBytes = CalculateLevelByteSize(w, h, format);
    if (packed) {
      totalBytes += levelBytes;
    } else {
      // Align MIP level footprint to 8-byte silicon TMU memory bounds
      totalBytes += (levelBytes + 7u) & ~7u;
    }
  }

  GLIDE_LOG(DEBUG, "TextureManager",
            "Calculated MIP-map footprint: " << totalBytes << " bytes for LODs "
                                             << start << ".." << end
                                             << ", Format=" << format);
  return totalBytes;
}

bool TextureManager::DownloadMipMap(uint32_t tmu, uint32_t startAddress,
                                    int32_t lodMin, int32_t lodMax,
                                    uint32_t aspect, uint32_t format,
                                    const void* data, bool replaceExisting,
                                    int32_t baseLargeLod) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // VULN-05: Strict validation of LOD limits and aspect ratios to prevent out-of-bounds memory access
  if (lodMin > lodMax) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: lodMin (" << lodMin << ") is greater than lodMax (" << lodMax << ")!");
    return false;
  }
  if (aspect > 6) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: aspect ratio (" << aspect << ") is out of range (0..6)!");
    return false;
  }

  // Card-specific bounds check (Voodoo3+: 2048 -> LOD -3, Voodoo1/2: 256 -> LOD 0)
  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  int32_t minValidLod = (model >= CardModel::Voodoo3) ? -3 : 0;
  if (lodMin < minValidLod || lodMin > 8 || lodMax < minValidLod || lodMax > 8) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: LOD index out of emulated card bounds!");
    return false;
  }

  const int32_t start = std::min(lodMin, lodMax);
  const int32_t end = std::max(lodMin, lodMax);
  const int32_t effectiveDeclaredLarge =
      (!replaceExisting && baseLargeLod != kNoBaseLargeLod) ? baseLargeLod
                                                            : start;

  uint32_t rawBytes = CalculateMemoryRequired(
      lodMin, lodMax, aspect, format,
      true);  // Use packed unaligned size for raw input data hashing!

  // 1. Fast 64-bit word hash of metadata + raw input mipmap bytes
  uint64_t metaSeed = 14695981039346656037ULL;
  metaSeed ^= (static_cast<uint64_t>(static_cast<uint32_t>(lodMin)) << 32) |
              static_cast<uint64_t>(static_cast<uint32_t>(lodMax));
  metaSeed *= 0x9E3779B97F4A7C15ULL;
  metaSeed ^= (static_cast<uint64_t>(aspect) << 32) |
              static_cast<uint64_t>(format);
  metaSeed = (metaSeed ^ (metaSeed >> 27)) * 0xBF58476D1CE4E5B9ULL;

  const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data);
  const uint64_t incomingHash = FastHashBytes64(metaSeed, byteData, rawBytes);

  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  const auto& tableState = TableState_Locked(tmu);

  // 2. Intercept and bypass if an identical complete texture is already loaded
  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it != m_tmus[storeTmu].textures.end()) {
    auto& cachedTex = it->second;
    if (cachedTex->rawDataHash == incomingHash && cachedTex->format == format &&
        cachedTex->aspectRatio == aspect && cachedTex->largeLod == start &&
        cachedTex->smallLod == end &&
        (replaceExisting ||
         cachedTex->declaredLargeLod == effectiveDeclaredLarge)) {
      EnsureTextureReady_Locked(tmu, it->second, true);
      GLIDE_LOG(DEBUG, "TextureManager",
                "DownloadMipMap Address=0x"
                    << std::hex << startAddress << std::dec
                    << " raw data unmodified (hash match: 0x" << std::hex
                    << incomingHash << std::dec
                    << "). Bypassing swizzle and download!");
      return true;
    }
  }

  // Determine whether this call merges a single level into an existing mipmap
  // chain (grTexDownloadMipMapLevel) or replaces the texture at startAddress
  // (grTexDownloadMipMap, or a new texture reusing the address).
  std::shared_ptr<VirtualTexture> existingToMerge;
  int32_t newLarge = start;
  int32_t newSmall = end;

  if (!replaceExisting && it != m_tmus[storeTmu].textures.end()) {
    const auto& existing = it->second;
    const bool sameBaseChain =
        (existing->format == format && existing->aspectRatio == aspect &&
         (existing->declaredLargeLod == effectiveDeclaredLarge ||
          existing->largeLod == effectiveDeclaredLarge) &&
         start >= effectiveDeclaredLarge);
    if (sameBaseChain) {
      existingToMerge = existing;
      newLarge = std::min(existing->largeLod, start);
      newSmall = std::max(existing->smallLod, end);
    }
  }

  const uint32_t newBytes =
      CalculateMemoryRequired(newLarge, newSmall, aspect, format);

  // Identify all resident textures in this TMU whose byte range overlaps the
  // incoming texture footprint [startAddress, startAddress + newBytes).
  const uint64_t newRangeStart = static_cast<uint64_t>(startAddress);
  const uint64_t newRangeEnd = newRangeStart + static_cast<uint64_t>(newBytes);
  uint32_t overlapStack[8];
  size_t overlapStackCount = 0;
  std::vector<uint32_t> overlapOverflow;
  uint64_t evictedBytes = 0;

  for (const auto& entry : m_tmus[storeTmu].textures) {
    if (entry.first >= kMultibaseCompositeBase) continue;
    const uint64_t eStart = static_cast<uint64_t>(entry.first);
    const uint64_t eSize =
        (entry.second->allocatedByteSize > 0)
            ? static_cast<uint64_t>(entry.second->allocatedByteSize)
            : static_cast<uint64_t>(CalculateMemoryRequired(
                  entry.second->largeLod, entry.second->smallLod,
                  entry.second->aspectRatio, entry.second->format));
    const uint64_t eEnd = eStart + eSize;
    if (entry.first == startAddress ||
        (eStart < newRangeEnd && newRangeStart < eEnd)) {
      if (overlapStackCount < 8) {
        overlapStack[overlapStackCount++] = entry.first;
      } else {
        overlapOverflow.push_back(entry.first);
      }
      evictedBytes += eSize;
    }
  }

  // Enforce user-configured TmuMemoryMb silicon ceiling atomically before
  // evicting any existing textures.
  const uint64_t budgetLimit =
      m_umaEnabled.load(std::memory_order_relaxed)
          ? (static_cast<uint64_t>(m_tmuMemoryLimits[0]) +
             static_cast<uint64_t>(m_tmuMemoryLimits[1]))
          : static_cast<uint64_t>(m_tmuMemoryLimits[tmu]);
  const uint64_t currentUsed = m_tmus[storeTmu].allocatedBytes;
  const uint64_t usedAfterEvict =
      (currentUsed >= evictedBytes) ? (currentUsed - evictedBytes) : 0ULL;
  if (usedAfterEvict + newBytes > budgetLimit) {
    GLIDE_LOG(CRITICAL, "TextureManager",
              "TMU" << tmu << " silicon budget exceeded!");
    return false;
  }

  // Always construct a fresh VirtualTexture instance so active copy-on-write
  // snapshots held by SoftwareBackend's deferred tile bins remain immutable.
  auto tex = std::make_shared<VirtualTexture>();
  tex->startAddress = startAddress;
  tex->format = format;
  tex->aspectRatio = aspect;
  tex->declaredLargeLod = effectiveDeclaredLarge;
  tex->allocatedByteSize = newBytes;

  uint32_t numLevels = static_cast<uint32_t>(newSmall - newLarge + 1);
  auto* activeBackend = BackendManager::GetInstance().GetBackend();
  const bool isHwBcn = IsHardwareBcnFormat(format);
  const bool useNativeBcn =
      isHwBcn && activeBackend != nullptr &&
      activeBackend->SupportsCompressedBcnTextures();

  std::vector<std::vector<uint32_t>> newSwizzled;
  if (!useNativeBcn) newSwizzled.resize(numLevels);
  std::vector<std::vector<uint8_t>> newRawCompressed;
  if (isHwBcn) newRawCompressed.resize(numLevels);
  std::vector<std::vector<uint8_t>> newRaw8Bit;
  std::vector<std::vector<uint8_t>> newRawYiq;
  std::vector<std::vector<uint16_t>> newRawAyiq;
  std::vector<std::vector<uint16_t>> newRawAp88;

  if (format == 0x5 || format == 0x6) newRaw8Bit.resize(numLevels);
  if (format == 0x1) newRawYiq.resize(numLevels);
  if (format == 0x9) newRawAyiq.resize(numLevels);
  if (format == 0xe) newRawAp88.resize(numLevels);

  if (existingToMerge) {
    tex->isPaletted = existingToMerge->isPaletted;
    tex->isNcc = existingToMerge->isNcc;
    if (!useNativeBcn && existingToMerge->swizzledMipLevels.empty() &&
        !existingToMerge->rawCompressedMipLevels.empty()) {
      EnsureCompressedSwizzled_Locked(*existingToMerge);
    }
    for (int32_t L = existingToMerge->largeLod; L <= existingToMerge->smallLod;
         ++L) {
      uint32_t oldIdx = static_cast<uint32_t>(L - existingToMerge->largeLod);
      uint32_t newIdx = static_cast<uint32_t>(L - newLarge);
      if (!useNativeBcn && oldIdx < existingToMerge->swizzledMipLevels.size()) {
        newSwizzled[newIdx] = existingToMerge->swizzledMipLevels[oldIdx];
      }
      if (isHwBcn && oldIdx < existingToMerge->rawCompressedMipLevels.size()) {
        newRawCompressed[newIdx] =
            existingToMerge->rawCompressedMipLevels[oldIdx];
      }
      if (existingToMerge->format == 0x5 || existingToMerge->format == 0x6) {
        if (oldIdx < existingToMerge->raw8BitMipLevels.size()) {
          newRaw8Bit[newIdx] = existingToMerge->raw8BitMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0x1) {
        if (oldIdx < existingToMerge->rawYiqMipLevels.size()) {
          newRawYiq[newIdx] = existingToMerge->rawYiqMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0x9) {
        if (oldIdx < existingToMerge->rawAyiqMipLevels.size()) {
          newRawAyiq[newIdx] = existingToMerge->rawAyiqMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0xe) {
        if (oldIdx < existingToMerge->rawAp88MipLevels.size()) {
          newRawAp88[newIdx] = existingToMerge->rawAp88MipLevels[oldIdx];
        }
      }
    }
  }

  const auto* rawData = reinterpret_cast<const uint8_t*>(data);
  size_t byteOffset = 0;

  const bool needsNcc = (format == 0x1 || format == 0x9);
  GuNccTable activeNcc{};
  if (needsNcc) {
    activeNcc = GetActiveNccTableOrDefault(tableState.nccTables,
                                           tableState.nccTablesLoaded,
                                           tableState.activeNccTable);
  }
  const bool paletteLoaded = tableState.paletteTableLoaded;
  const uint32_t* paletteTable = tableState.paletteTable;

  for (int32_t i = start; i <= end; ++i) {
    uint32_t maxDim = LodToDimension(i);
    uint32_t w = maxDim;
    uint32_t h = maxDim;

    switch (aspect) {
      case 0:
        h = std::max(1u, maxDim / 8);
        break;  // 8x1
      case 1:
        h = std::max(1u, maxDim / 4);
        break;  // 4x1
      case 2:
        h = std::max(1u, maxDim / 2);
        break;  // 2x1
      case 3:
        break;  // 1x1
      case 4:
        w = std::max(1u, maxDim / 2);
        break;  // 1x2
      case 5:
        w = std::max(1u, maxDim / 4);
        break;  // 1x4
      case 6:
        w = std::max(1u, maxDim / 8);
        break;  // 1x8
    }

    const uint32_t levelBytes = CalculateLevelByteSize(w, h, format);
    uint32_t destIdx = static_cast<uint32_t>(i - newLarge);
    if (isHwBcn) {
      std::vector<uint8_t> compBytes(levelBytes);
      std::memcpy(compBytes.data(), rawData + byteOffset, levelBytes);
      newRawCompressed[destIdx] = std::move(compBytes);
    }

    if (!useNativeBcn) {
      const size_t texelCount = static_cast<size_t>(w) * h;
      std::vector<uint32_t> levelPixels(texelCount);
      std::vector<uint8_t> levelRaw;
      if (format == 0x5 ||
          format == 0x6) {  // GR_TEXFMT_P_8 or GR_TEXFMT_P_8_6666
        tex->isPaletted = true;
        levelRaw.resize(texelCount);
      }
      std::vector<uint8_t> levelYiq;
      if (format == 0x1) {  // GR_TEXFMT_YIQ_422
        tex->isNcc = true;
        levelYiq.resize(texelCount);
      }
      std::vector<uint16_t> levelAyiq;
      if (format == 0x9) {  // GR_TEXFMT_AYIQ_8422
        tex->isNcc = true;
        levelAyiq.resize(texelCount);
      }
      std::vector<uint16_t> levelAp88;
      if (format == 0xe) {  // GR_TEXFMT_AP_88
        tex->isPaletted = true;
        levelAp88.resize(texelCount);
      }

      DecodeTexelsToBgra(
          format, rawData + byteOffset, w, h, levelPixels.data(),
          paletteLoaded, paletteTable, needsNcc ? &activeNcc : nullptr,
          levelRaw.empty() ? nullptr : levelRaw.data(),
          levelYiq.empty() ? nullptr : levelYiq.data(),
          levelAyiq.empty() ? nullptr : levelAyiq.data(),
          levelAp88.empty() ? nullptr : levelAp88.data());

      newSwizzled[destIdx] = std::move(levelPixels);
      if (format == 0x5 || format == 0x6) {
        newRaw8Bit[destIdx] = std::move(levelRaw);
      } else if (format == 0x1) {
        newRawYiq[destIdx] = std::move(levelYiq);
      } else if (format == 0x9) {
        newRawAyiq[destIdx] = std::move(levelAyiq);
      } else if (format == 0xe) {
        newRawAp88[destIdx] = std::move(levelAp88);
      }
    }
    byteOffset += levelBytes;  // Advancing packed unaligned input bytes offset!
  }

  // Guarantee every mip level slot in [newLarge..newSmall] is populated to its
  // exact size so no backend can ever dereference an empty gap mip level.
  for (int32_t L = newLarge; L <= newSmall; ++L) {
    const uint32_t idx = static_cast<uint32_t>(L - newLarge);
    uint32_t lw = 1, lh = 1;
    ComputeLodDimensions(L, aspect, lw, lh);
    if (isHwBcn) {
      const size_t expectedBytes = CalculateLevelByteSize(lw, lh, format);
      if (newRawCompressed[idx].size() != expectedBytes) {
        newRawCompressed[idx].resize(expectedBytes, 0);
      }
    }
    if (!useNativeBcn) {
      const size_t expectedTexels = static_cast<size_t>(lw) * lh;
      if (newSwizzled[idx].size() != expectedTexels) {
        newSwizzled[idx].resize(expectedTexels, 0);
      }
      if ((format == 0x5 || format == 0x6) &&
          newRaw8Bit[idx].size() != expectedTexels) {
        newRaw8Bit[idx].resize(expectedTexels, 0);
      } else if (format == 0x1 && newRawYiq[idx].size() != expectedTexels) {
        newRawYiq[idx].resize(expectedTexels, 0);
      } else if (format == 0x9 && newRawAyiq[idx].size() != expectedTexels) {
        newRawAyiq[idx].resize(expectedTexels, 0);
      } else if (format == 0xe && newRawAp88[idx].size() != expectedTexels) {
        newRawAp88[idx].resize(expectedTexels, 0);
      }
    }
  }

  tex->swizzledMipLevels = std::move(newSwizzled);
  if (isHwBcn) tex->rawCompressedMipLevels = std::move(newRawCompressed);
  if (format == 0x5 || format == 0x6)
    tex->raw8BitMipLevels = std::move(newRaw8Bit);
  else if (format == 0x1)
    tex->rawYiqMipLevels = std::move(newRawYiq);
  else if (format == 0x9)
    tex->rawAyiqMipLevels = std::move(newRawAyiq);
  else if (format == 0xe)
    tex->rawAp88MipLevels = std::move(newRawAp88);

  tex->largeLod = newLarge;
  tex->smallLod = newSmall;

  // Update base dimensions to correspond to newLarge
  ComputeLodDimensions(newLarge, aspect, tex->baseWidth, tex->baseHeight);

  // Only store rawDataHash when the stored chain exactly matches the hashed
  // download range; merged chains should not falsely bypass subsequent calls.
  tex->rawDataHash =
      (newLarge == start && newSmall == end) ? incomingHash : 0ULL;
  tex->swizzledTableHash = GetActiveTableHash_Locked(tableState, *tex);
  tex->swizzledWithTable =
      tex->isPaletted
          ? tableState.paletteTableLoaded
          : (tex->isNcc
                 ? tableState.nccTablesLoaded[tableState.activeNccTable]
                 : true);
  tex->uploadedToBackend = false;
  tex->uploadedToBackendPerTmu[0] = false;
  tex->uploadedToBackendPerTmu[1] = false;
  tex->uploadedTableHashPerTmu[0] = 0;
  tex->uploadedTableHashPerTmu[1] = 0;

  // Commit overlapping evictions and insert the new VirtualTexture
  for (size_t oi = 0; oi < overlapStackCount; ++oi) {
    m_tmus[storeTmu].textures.erase(overlapStack[oi]);
  }
  for (uint32_t addr : overlapOverflow) {
    m_tmus[storeTmu].textures.erase(addr);
  }
  m_tmus[storeTmu].allocatedBytes =
      static_cast<uint32_t>(usedAfterEvict + newBytes);
  m_tmus[storeTmu].textures[startAddress] = tex;

  GLIDE_LOG(DEBUG, "TextureManager",
            "Successfully downloaded Virtual Texture MIP Chain at Address=0x"
                << std::hex << startAddress << std::dec << " Base("
                << tex->baseWidth << "x" << tex->baseHeight << "), Levels="
                << std::max(tex->swizzledMipLevels.size(),
                            tex->rawCompressedMipLevels.size())
                << " into TMU" << tmu
                << ". Allocated pool: " << m_tmus[storeTmu].allocatedBytes
                << " bytes.");

  // If this texture is fully swizzled (either not paletted, or paletted with a
  // pre-loaded table), upload it immediately!
  if (!tex->isPaletted || tableState.paletteTableLoaded) {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
      TelemetryManager::GetInstance().RecordTexUpload(
          CalcSwizzledUploadBytes(*tex));
      b->UploadTexture(tmu, startAddress, *tex);
      tex->uploadedToBackend = true;
      tex->uploadedTableHash = tex->swizzledTableHash;
      tex->uploadedToBackendPerTmu[tmu] = true;
      tex->uploadedTableHashPerTmu[tmu] = tex->swizzledTableHash;
    }
  }

  return true;
}

bool TextureManager::DownloadMipMapPartial(uint32_t tmu, uint32_t startAddress,
                                           int32_t thisLod, int32_t largeLod,
                                           uint32_t aspectRatio,
                                           uint32_t format, const void* data,
                                           int startRow, int endRow) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // VULN-05: Strict validation of LOD limits and aspect ratios to prevent out-of-bounds memory access
  if (thisLod < largeLod) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: thisLod (" << thisLod << ") is smaller than base largeLod (" << largeLod << ")!");
    return false;
  }
  if (aspectRatio > 6) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: aspect ratio (" << aspectRatio << ") is out of range (0..6)!");
    return false;
  }

  // Card-specific bounds check (Voodoo3+: 2048 -> LOD -3, Voodoo1/2: 256 -> LOD 0)
  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  int32_t minValidLod = (model >= CardModel::Voodoo3) ? -3 : 0;
  if (thisLod < minValidLod || thisLod > 8 || largeLod < minValidLod || largeLod > 8) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: LOD index out of emulated card bounds!");
    return false;
  }

  // Find the existing texture
  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it == m_tmus[storeTmu].textures.end()) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: texture at address 0x"
                  << std::hex << startAddress << std::dec
                  << " does not exist! Cannot do partial update.");
    return false;
  }

  // Copy-on-write before mutating pixels so deferred SoftwareBackend snapshots
  // remain immutable.
  if (it->second.use_count() > 1) {
    it->second = std::make_shared<VirtualTexture>(*it->second);
  }
  auto& tex = it->second;

  // Validate LOD level
  if (thisLod < tex->largeLod || thisLod > tex->smallLod) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: LOD level "
                  << thisLod << " is out of texture LOD range " << tex->largeLod
                  << ".." << tex->smallLod);
    return false;
  }

  uint32_t lodIdx = static_cast<uint32_t>(thisLod - tex->largeLod);
  if (lodIdx >= tex->swizzledMipLevels.size()) {
    GLIDE_LOG(
        WARN, "TextureManager",
        "DownloadMipMapPartial: LOD index " << lodIdx << " out of bounds");
    return false;
  }

  uint32_t maxDim = LodToDimension(thisLod);
  uint32_t w = maxDim;
  uint32_t h = maxDim;
  switch (aspectRatio) {
    case 0:
      h = std::max(1u, maxDim / 8);
      break;
    case 1:
      h = std::max(1u, maxDim / 4);
      break;
    case 2:
      h = std::max(1u, maxDim / 2);
      break;
    case 3:
      break;
    case 4:
      w = std::max(1u, maxDim / 2);
      break;
    case 5:
      w = std::max(1u, maxDim / 4);
      break;
    case 6:
      w = std::max(1u, maxDim / 8);
      break;
  }

  if (endRow > static_cast<int>(h)) {
    endRow = static_cast<int>(h);
  }
  if (startRow < 0 || startRow >= static_cast<int>(h) || endRow <= startRow) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: invalid row range ["
                  << startRow << ".." << endRow << ") for height " << h);
    return false;
  }

  // Decode only the partial rows
  int numRows = endRow - startRow;
  size_t partialTexels = numRows * w;

  const auto* rawData = reinterpret_cast<const uint8_t*>(data);
  const bool needsNcc = (format == 0x1 || format == 0x9);
  GuNccTable activeNcc{};
  if (needsNcc) {
    activeNcc = GetActiveNccTableOrDefault(m_tmus[tmu].nccTables,
                                           m_tmus[tmu].nccTablesLoaded,
                                           m_tmus[tmu].activeNccTable);
  }

  // If a paletted/NCC texture was previously swizzled with a different table,
  // re-swizzle its existing levels first so partial rows don't mix palettes.
  if (tex->isPaletted || tex->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  if (tex->swizzledMipLevels.empty() && !tex->rawCompressedMipLevels.empty()) {
    EnsureCompressedSwizzled_Locked(*tex);
    tex->rawCompressedMipLevels.clear();
  }
  tex->rawDataHash = 0;  // Partial edit invalidates full-mipmap raw hash

  // Update swizzled mip levels
  auto& levelPixels = tex->swizzledMipLevels[lodIdx];
  const size_t baseOffset = static_cast<size_t>(startRow) * w;

  uint8_t* dstRaw8 =
      ((format == 0x5 || format == 0x6) && lodIdx < tex->raw8BitMipLevels.size())
          ? (tex->raw8BitMipLevels[lodIdx].data() + baseOffset)
          : nullptr;
  uint8_t* dstRawYiq =
      (format == 0x1 && lodIdx < tex->rawYiqMipLevels.size())
          ? (tex->rawYiqMipLevels[lodIdx].data() + baseOffset)
          : nullptr;
  uint16_t* dstRawAyiq =
      (format == 0x9 && lodIdx < tex->rawAyiqMipLevels.size())
          ? (tex->rawAyiqMipLevels[lodIdx].data() + baseOffset)
          : nullptr;
  uint16_t* dstRawAp88 =
      (format == 0xe && lodIdx < tex->rawAp88MipLevels.size())
          ? (tex->rawAp88MipLevels[lodIdx].data() + baseOffset)
          : nullptr;

  DecodeTexelsToBgra(format, rawData, w, static_cast<uint32_t>(numRows),
                     levelPixels.data() + baseOffset,
                     m_tmus[tmu].paletteTableLoaded, m_tmus[tmu].paletteTable,
                     needsNcc ? &activeNcc : nullptr, dstRaw8, dstRawYiq,
                     dstRawAyiq, dstRawAp88);

  // Call the backend to perform the partial (or full, if palette changed) GPU upload
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    if (!tex->uploadedToBackendPerTmu[tmu] ||
        ((tex->isPaletted || tex->isNcc) &&
         tex->uploadedTableHashPerTmu[tmu] != tex->swizzledTableHash)) {
      TelemetryManager::GetInstance().RecordTexUpload(
          CalcSwizzledUploadBytes(*tex));
      b->UploadTexture(tmu, startAddress, *tex);
      tex->uploadedToBackend = true;
      tex->uploadedTableHash = tex->swizzledTableHash;
      tex->uploadedToBackendPerTmu[tmu] = true;
      tex->uploadedTableHashPerTmu[tmu] = tex->swizzledTableHash;
    } else {
      TelemetryManager::GetInstance().RecordTexUpload(
          static_cast<uint64_t>(partialTexels) * sizeof(uint32_t));
      b->UploadTexturePartial(tmu, startAddress, *tex, lodIdx, startRow,
                              endRow);
    }
    // In UMA mode, invalidate the other TMU's upload flag so next bind syncs.
    if (m_umaEnabled.load(std::memory_order_relaxed)) {
      tex->uploadedToBackendPerTmu[1u - tmu] = false;
    }
  }

  return true;
}
bool TextureManager::DownloadTable(uint32_t tmu, uint32_t type,
                                   const void* data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // type can be: GR_TEXTABLE_NCC0 (0), GR_TEXTABLE_NCC1 (1),
  // GR_TEXTABLE_PALETTE (2), or GR_TEXTABLE_PALETTE_6666_EXT (3)
  if (type > 3) {
    GLIDE_LOG(WARN, "TextureManager",
              "Unsupported texture table type: " << type << ". Skipping.");
    return false;
  }

  if (type == 0 || type == 1) {
    const auto* incomingNcc = reinterpret_cast<const GuNccTable*>(data);
    if (m_tmus[tmu].nccTablesLoaded[type] &&
        std::memcmp(&m_tmus[tmu].nccTables[type], incomingNcc,
                    sizeof(GuNccTable)) == 0) {
      return true;  // Identical NCC table — zero work!
    }

    const bool wasLoaded = m_tmus[tmu].nccTablesLoaded[type];
    m_tmus[tmu].nccTables[type] = *incomingNcc;
    m_tmus[tmu].nccTablesLoaded[type] = true;
    m_tmus[tmu].nccHashes[type] = HashTableBytes(incomingNcc, sizeof(GuNccTable));

    if (m_tmus[tmu].activeNccTable == type) {
      m_tmuTableDirty[tmu].store(true, std::memory_order_release);
      s_anyTableDirty.store(true, std::memory_order_release);
      EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);
      if (!wasLoaded) {
        // First-ever NCC table load: decode any pre-downloaded NCC textures
        // on CPU immediately (GPU upload remains lazy on bind/draw).
        int swizzledCount = 0;
        for (auto& pair : m_tmus[tmu].textures) {
          auto& tex = pair.second;
          if (tex->isNcc && !tex->swizzledWithTable) {
            if (tex.use_count() > 1) {
              tex = std::make_shared<VirtualTexture>(*tex);
            }
            ReswizzleTexture_Locked(m_tmus[tmu], *tex);
            swizzledCount++;
          }
        }
        GLIDE_LOG(DEBUG, "TextureManager",
                  "Initial NCC" << type << " download on TMU" << tmu
                                << " decoded " << swizzledCount
                                << " queued textures (GPU upload deferred).");
      } else {
        GLIDE_LOG(DEBUG, "TextureManager",
                  "Updated NCC" << type << " table on TMU" << tmu
                                << " (lazy per-bind swizzle enabled).");
      }
    }
  } else if (type == 2 || type == 3) {
    uint32_t newPalette[256];
    const auto* paletteData = reinterpret_cast<const uint32_t*>(data);
    if (type == 2) {
      // GR_TEXTABLE_PALETTE entries are 24-bit 0x00RRGGBB; top byte is unused
      // by hardware and P_8 texels have implicit opaque alpha (0xFF).
      for (int i = 0; i < 256; ++i) {
        newPalette[i] = paletteData[i] | 0xFF000000u;
      }
    } else {
      for (int i = 0; i < 256; ++i) {
        uint32_t val = paletteData[i];
        newPalette[i] =
            (((val & 0x3F) << 2) | ((val >> 4) & 0x3)) |
            ((((val >> 6) & 0x3F) << 10) | (((val >> 10) & 0x3) << 8)) |
            ((((val >> 12) & 0x3F) << 18) | (((val >> 16) & 0x3) << 16)) |
            ((((val >> 18) & 0x3F) << 26) | (((val >> 22) & 0x3) << 24));
      }
    }

    if (m_tmus[tmu].paletteTableLoaded &&
        std::memcmp(m_tmus[tmu].paletteTable, newPalette, sizeof(newPalette)) ==
            0) {
      return true;  // Identical palette — zero work!
    }

    const bool wasLoaded = m_tmus[tmu].paletteTableLoaded;
    std::memcpy(m_tmus[tmu].paletteTable, newPalette, sizeof(newPalette));
    m_tmus[tmu].paletteTableLoaded = true;
    m_tmus[tmu].paletteHash = HashTableBytes(newPalette, sizeof(newPalette));
    m_tmuTableDirty[tmu].store(true, std::memory_order_release);
    s_anyTableDirty.store(true, std::memory_order_release);
    EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);

    if (!wasLoaded) {
      // First-ever palette load: decode any pre-downloaded paletted textures
      // on CPU immediately (GPU upload remains lazy on bind/draw).
      int swizzledCount = 0;
      for (auto& pair : m_tmus[tmu].textures) {
        auto& tex = pair.second;
        if (tex->isPaletted && !tex->swizzledWithTable) {
          if (tex.use_count() > 1) {
            tex = std::make_shared<VirtualTexture>(*tex);
          }
          ReswizzleTexture_Locked(m_tmus[tmu], *tex);
          swizzledCount++;
        }
      }
      GLIDE_LOG(DEBUG, "TextureManager",
                "Initial palette download (type=" << type << ") on TMU" << tmu
                                                  << " decoded "
                                                  << swizzledCount
                                                  << " queued textures (GPU upload deferred).");
    } else {
      GLIDE_LOG(DEBUG, "TextureManager",
                "Updated palette (type=" << type << ") on TMU" << tmu
                                         << " (lazy per-bind swizzle enabled).");
    }
  }

  return true;
}

void TextureManager::SetActiveNccTable(uint32_t tmu, uint32_t table) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || table >= 2) return;

  if (m_tmus[tmu].activeNccTable == table) return;  // No change

  m_tmus[tmu].activeNccTable = table;
  m_tmuTableDirty[tmu].store(true, std::memory_order_release);
  s_anyTableDirty.store(true, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);
  GLIDE_LOG(DEBUG, "TextureManager",
            "TMU" << tmu << " active NCC table switched to: NCC" << table
                  << " (lazy per-bind swizzle enabled).");
}

void TextureManager::SetTmuMemoryLimitMb(uint32_t tmu, uint32_t limitMb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2) {
    m_tmuMemoryLimits[tmu] = limitMb * 1024 * 1024;
    GLIDE_LOG(
        INFO, "TextureManager",
        "Configured TMU" << tmu << " silicon budget to " << limitMb << " MB.");
  }
}

void TextureManager::SetUmaMode(bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const bool wasEnabled = m_umaEnabled.exchange(enabled, std::memory_order_acq_rel);
  if (wasEnabled != enabled) {
    GLIDE_LOG(INFO, "TextureManager",
              "TEXUMA (GR_TEXTURE_UMA_EXT) " << (enabled ? "ENABLED" : "DISABLED"));
  }
}

void TextureManager::SetMultibase(uint32_t tmu, bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  m_tmus[tmu].multibaseEnabled = enabled;
  GLIDE_LOG(DEBUG, "TextureManager",
            "SetMultibase TMU" << tmu << ": " << (enabled ? "ON" : "OFF"));
}

void TextureManager::SetMultibaseAddress(uint32_t tmu, uint32_t canonicalRange,
                                         uint32_t startAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || canonicalRange >= 8) return;
  m_tmus[tmu].multibaseAddr[canonicalRange] = startAddress;
  GLIDE_LOG(DEBUG, "TextureManager",
            "SetMultibaseAddress TMU" << tmu << " range=" << canonicalRange
                                      << " addr=0x" << std::hex << startAddress
                                      << std::dec);
}

bool TextureManager::IsMultibaseEnabled(uint32_t tmu) const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
  return (tmu < 2) ? m_tmus[tmu].multibaseEnabled : false;
}

uint32_t TextureManager::ResolveMultibaseSource(uint32_t tmu,
                                                uint32_t startAddress,
                                                int32_t glide2LargeLod,
                                                int32_t glide2SmallLod,
                                                uint32_t aspect,
                                                uint32_t format) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !m_tmus[tmu].multibaseEnabled) return startAddress;

  const int32_t start = std::min(glide2LargeLod, glide2SmallLod);
  const int32_t end = std::max(glide2LargeLod, glide2SmallLod);
  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  auto& texMap = m_tmus[storeTmu].textures;

  auto pickCandidate = [&](uint32_t maxDim, int32_t lod)
      -> std::shared_ptr<VirtualTexture> {
    uint32_t cands[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, startAddress};
    if (maxDim >= 2048u) {
      cands[0] = m_tmus[tmu].multibaseAddr[7];  // GR_TEXBASE_2048
      cands[1] = m_tmus[tmu].multibaseAddr[3];  // GR_TEXBASE_256 (test39 style)
    } else if (maxDim == 1024u) {
      cands[0] = m_tmus[tmu].multibaseAddr[6];  // GR_TEXBASE_1024
      cands[1] = m_tmus[tmu].multibaseAddr[3];
    } else if (maxDim == 512u) {
      cands[0] = m_tmus[tmu].multibaseAddr[5];  // GR_TEXBASE_512
      cands[1] = m_tmus[tmu].multibaseAddr[3];
    } else if (maxDim == 256u) {
      cands[0] = m_tmus[tmu].multibaseAddr[3];  // GR_TEXBASE_256
      cands[1] = m_tmus[tmu].multibaseAddr[4];  // GR_TEXBASE_256_TO_1
    } else if (maxDim == 128u) {
      cands[0] = m_tmus[tmu].multibaseAddr[2];  // GR_TEXBASE_128
      cands[1] = m_tmus[tmu].multibaseAddr[4];
    } else if (maxDim == 64u) {
      cands[0] = m_tmus[tmu].multibaseAddr[1];  // GR_TEXBASE_64
      cands[1] = m_tmus[tmu].multibaseAddr[4];
    } else {
      cands[0] = m_tmus[tmu].multibaseAddr[0];  // GR_TEXBASE_32_TO_1
      cands[1] = m_tmus[tmu].multibaseAddr[4];
    }
    for (uint32_t addr : cands) {
      if (addr == 0xFFFFFFFFu) continue;
      auto it = texMap.find(addr);
      if (it == texMap.end()) continue;
      auto& src = it->second;
      if (lod >= src->largeLod && lod <= src->smallLod) {
        EnsureTextureReady_Locked(tmu, src, false);
        return src;
      }
    }
    return nullptr;
  };

  const uint32_t compositeAddr = kMultibaseCompositeBase + tmu;
  auto comp = std::make_shared<VirtualTexture>();
  comp->startAddress = compositeAddr;
  comp->format = format;
  comp->aspectRatio = aspect;
  comp->largeLod = start;
  comp->smallLod = end;
  comp->declaredLargeLod = start;
  ComputeLodDimensions(start, aspect, comp->baseWidth, comp->baseHeight);

  const uint32_t numLevels = static_cast<uint32_t>(end - start + 1);
  comp->swizzledMipLevels.resize(numLevels);

  uint64_t compHash = 14695981039346656037ULL;
  bool anyMatched = false;
  for (int32_t L = start; L <= end; ++L) {
    uint32_t lw = 1, lh = 1;
    ComputeLodDimensions(L, aspect, lw, lh);
    const uint32_t maxDim = std::max(lw, lh);
    const uint32_t dstIdx = static_cast<uint32_t>(L - start);
    auto src = pickCandidate(maxDim, L);
    if (src) {
      if (src->swizzledMipLevels.empty() &&
          !src->rawCompressedMipLevels.empty()) {
        EnsureCompressedSwizzled_Locked(*src);
      }
      const uint32_t srcIdx = static_cast<uint32_t>(L - src->largeLod);
      if (srcIdx < src->swizzledMipLevels.size() &&
          src->swizzledMipLevels[srcIdx].size() == static_cast<size_t>(lw) * lh) {
        comp->swizzledMipLevels[dstIdx] = src->swizzledMipLevels[srcIdx];
        anyMatched = true;
      }
    }
    if (comp->swizzledMipLevels[dstIdx].size() != static_cast<size_t>(lw) * lh) {
      comp->swizzledMipLevels[dstIdx].resize(static_cast<size_t>(lw) * lh, 0);
    }
    compHash = FastHashBytes64(
        compHash,
        reinterpret_cast<const uint8_t*>(comp->swizzledMipLevels[dstIdx].data()),
        comp->swizzledMipLevels[dstIdx].size() * sizeof(uint32_t));
  }

  if (!anyMatched) return startAddress;

  comp->rawDataHash = compHash;
  auto existingIt = texMap.find(compositeAddr);
  if (existingIt != texMap.end() &&
      existingIt->second->rawDataHash == compHash &&
      existingIt->second->baseWidth == comp->baseWidth &&
      existingIt->second->baseHeight == comp->baseHeight &&
      existingIt->second->swizzledMipLevels.size() == numLevels &&
      existingIt->second->uploadedToBackendPerTmu[tmu]) {
    return compositeAddr;
  }

  texMap[compositeAddr] = comp;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    TelemetryManager::GetInstance().RecordTexUpload(
        CalcSwizzledUploadBytes(*comp));
    b->UploadTexture(tmu, compositeAddr, *comp);
    comp->uploadedToBackend = true;
    comp->uploadedToBackendPerTmu[tmu] = true;
  }
  return compositeAddr;
}

VirtualTexture* TextureManager::GetTexture(uint32_t tmu,
                                           uint32_t startAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return nullptr;
  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it == m_tmus[storeTmu].textures.end()) return nullptr;
  if (it->second->isPaletted || it->second->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  if (it->second->swizzledMipLevels.empty() &&
      !it->second->rawCompressedMipLevels.empty()) {
    EnsureCompressedSwizzled_Locked(*it->second);
  }
  return it->second.get();
}

std::shared_ptr<const VirtualTexture> TextureManager::GetTextureSnapshot(
    uint32_t tmu, uint32_t startAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || startAddress == 0xFFFFFFFFu) return nullptr;
  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it == m_tmus[storeTmu].textures.end()) return nullptr;
  if (it->second->isPaletted || it->second->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  if (it->second->swizzledMipLevels.empty() &&
      !it->second->rawCompressedMipLevels.empty()) {
    EnsureCompressedSwizzled_Locked(*it->second);
  }
  return it->second;
}

void TextureManager::PrepareTextureForBind(uint32_t tmu,
                                           uint32_t startAddress) {
  GLIDE_CPU_SCOPE(CpuBucket::Tex);
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  m_tmus[tmu].boundAddress = startAddress;
  if (startAddress == 0xFFFFFFFFu) return;
  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it != m_tmus[storeTmu].textures.end()) {
    EnsureTextureReady_Locked(tmu, it->second, true);
  }
}

void TextureManager::SyncBoundTextures() {
  GLIDE_CPU_SCOPE(CpuBucket::Tex);
  std::lock_guard<std::mutex> lock(m_mutex);
  s_anyTableDirty.store(false, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(false, std::memory_order_release);
  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    if (!m_tmuTableDirty[tmu].exchange(false, std::memory_order_acq_rel)) {
      continue;
    }
    const uint32_t addr = m_tmus[tmu].boundAddress;
    if (addr == 0xFFFFFFFFu) continue;
    const uint32_t storeTmu = StorageTmu_Locked(tmu);
    auto it = m_tmus[storeTmu].textures.find(addr);
    if (it != m_tmus[storeTmu].textures.end()) {
      EnsureTextureReady_Locked(tmu, it->second, true);
    }
  }
}

void TextureManager::ClearTextures() {
  std::lock_guard<std::mutex> lock(m_mutex);
  s_anyTableDirty.store(false, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(false, std::memory_order_release);
  for (int i = 0; i < 2; ++i) {
    m_tmus[i].allocatedBytes = 0;
    m_tmus[i].textures.clear();
    m_tmus[i].boundAddress = 0xFFFFFFFFu;
    for (int r = 0; r < 8; ++r) {
      m_tmus[i].multibaseAddr[r] = 0xFFFFFFFFu;
    }
    m_tmuTableDirty[i].store(false, std::memory_order_release);
  }
  GLIDE_LOG(INFO, "TextureManager",
            "Cleared Virtual TMU texture pools (palette & NCC tables "
            "preserved).");
}

void TextureManager::Reset() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_umaEnabled.store(false, std::memory_order_release);
  s_anyTableDirty.store(false, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(false, std::memory_order_release);
  for (int i = 0; i < 2; ++i) {
    m_tmus[i].allocatedBytes = 0;
    m_tmus[i].textures.clear();
    m_tmus[i].paletteTableLoaded = false;
    m_tmus[i].paletteHash = 0;
    std::memset(m_tmus[i].paletteTable, 0, sizeof(m_tmus[i].paletteTable));
    std::memset(m_tmus[i].nccTables, 0, sizeof(m_tmus[i].nccTables));
    m_tmus[i].nccTablesLoaded[0] = false;
    m_tmus[i].nccTablesLoaded[1] = false;
    m_tmus[i].nccHashes[0] = 0;
    m_tmus[i].nccHashes[1] = 0;
    m_tmus[i].activeNccTable = 0;
    m_tmus[i].boundAddress = 0xFFFFFFFFu;
    m_tmus[i].multibaseEnabled = false;
    for (int r = 0; r < 8; ++r) {
      m_tmus[i].multibaseAddr[r] = 0xFFFFFFFFu;
    }
    m_tmuTableDirty[i].store(false, std::memory_order_release);
  }
  GLIDE_LOG(INFO, "TextureManager",
            "Purged and reset all Virtual TMU state pools.");
}

bool TextureManager::CommitRenderedTexture(
    uint32_t tmu, uint32_t startAddress, int32_t thisLod, int32_t largeLod,
    uint32_t aspect, uint32_t format,
    const std::vector<uint32_t>& canonicalArgb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || aspect > 6) return false;
  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  const int32_t minValidLod = (model >= CardModel::Voodoo3) ? -3 : 0;
  if (thisLod < minValidLod || thisLod > 8 || largeLod < minValidLod ||
      largeLod > 8 || thisLod < largeLod) {
    return false;
  }

  uint32_t w = 1, h = 1;
  ComputeLodDimensions(thisLod, aspect, w, h);
  if (canonicalArgb.size() != static_cast<size_t>(w) * h) {
    return false;
  }

  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  std::shared_ptr<VirtualTexture> existingToMerge;
  int32_t newLarge = thisLod;
  int32_t newSmall = thisLod;

  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it != m_tmus[storeTmu].textures.end()) {
    const auto& existing = it->second;
    if (existing->format == format && existing->aspectRatio == aspect &&
        (existing->declaredLargeLod == largeLod ||
         existing->largeLod == largeLod)) {
      existingToMerge = existing;
      newLarge = std::min(existing->largeLod, thisLod);
      newSmall = std::max(existing->smallLod, thisLod);
    }
  }

  const uint32_t newBytes =
      CalculateMemoryRequired(newLarge, newSmall, aspect, format);
  const uint64_t newRangeStart = static_cast<uint64_t>(startAddress);
  const uint64_t newRangeEnd = newRangeStart + static_cast<uint64_t>(newBytes);

  std::vector<uint32_t> overlaps;
  uint64_t evictedBytes = 0;
  for (const auto& entry : m_tmus[storeTmu].textures) {
    if (entry.first >= kMultibaseCompositeBase) continue;
    const uint64_t eStart = static_cast<uint64_t>(entry.first);
    const uint64_t eSize =
        (entry.second->allocatedByteSize > 0)
            ? static_cast<uint64_t>(entry.second->allocatedByteSize)
            : static_cast<uint64_t>(CalculateMemoryRequired(
                  entry.second->largeLod, entry.second->smallLod,
                  entry.second->aspectRatio, entry.second->format));
    const uint64_t eEnd = eStart + eSize;
    if (entry.first == startAddress ||
        (eStart < newRangeEnd && newRangeStart < eEnd)) {
      overlaps.push_back(entry.first);
      evictedBytes += eSize;
    }
  }

  const uint64_t currentUsed = m_tmus[storeTmu].allocatedBytes;
  const uint64_t usedAfterEvict =
      (currentUsed >= evictedBytes) ? (currentUsed - evictedBytes) : 0ULL;

  auto tex = std::make_shared<VirtualTexture>();
  tex->startAddress = startAddress;
  tex->format = format;
  tex->aspectRatio = aspect;
  tex->largeLod = newLarge;
  tex->smallLod = newSmall;
  tex->declaredLargeLod = largeLod;
  tex->allocatedByteSize = newBytes;
  tex->isPaletted = false;
  tex->isNcc = false;
  tex->rawDataHash = 0;
  tex->swizzledWithTable = true;
  ComputeLodDimensions(newLarge, aspect, tex->baseWidth, tex->baseHeight);

  const uint32_t numLevels = static_cast<uint32_t>(newSmall - newLarge + 1);
  tex->swizzledMipLevels.resize(numLevels);
  if (existingToMerge) {
    for (int32_t L = existingToMerge->largeLod; L <= existingToMerge->smallLod;
         ++L) {
      const uint32_t oldIdx =
          static_cast<uint32_t>(L - existingToMerge->largeLod);
      const uint32_t newIdx = static_cast<uint32_t>(L - newLarge);
      if (oldIdx < existingToMerge->swizzledMipLevels.size()) {
        tex->swizzledMipLevels[newIdx] =
            existingToMerge->swizzledMipLevels[oldIdx];
      }
    }
  }
  tex->swizzledMipLevels[static_cast<uint32_t>(thisLod - newLarge)] =
      canonicalArgb;

  for (int32_t L = newLarge; L <= newSmall; ++L) {
    const uint32_t idx = static_cast<uint32_t>(L - newLarge);
    uint32_t lw = 1, lh = 1;
    ComputeLodDimensions(L, aspect, lw, lh);
    const size_t expected = static_cast<size_t>(lw) * lh;
    if (tex->swizzledMipLevels[idx].size() != expected) {
      tex->swizzledMipLevels[idx].resize(expected, 0xFF000000u);
    }
  }

  for (uint32_t addr : overlaps) {
    m_tmus[storeTmu].textures.erase(addr);
  }
  m_tmus[storeTmu].allocatedBytes =
      static_cast<uint32_t>(usedAfterEvict + newBytes);
  m_tmus[storeTmu].textures[startAddress] = tex;

  tex->uploadedToBackend = false;
  tex->uploadedToBackendPerTmu[0] = false;
  tex->uploadedToBackendPerTmu[1] = false;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    TelemetryManager::GetInstance().RecordTexUpload(
        CalcSwizzledUploadBytes(*tex));
    b->UploadTexture(tmu, startAddress, *tex);
    tex->uploadedToBackend = true;
    tex->uploadedToBackendPerTmu[tmu] = true;
  }
  return true;
}

bool TextureManager::CommitGpuRenderedTexture(
    uint32_t tmu, uint32_t startAddress, int32_t thisLod, int32_t largeLod,
    uint32_t aspect, uint32_t format, uint32_t copyWidth, uint32_t copyHeight,
    uint32_t origin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || aspect > 6 || copyWidth == 0 || copyHeight == 0) return false;
  auto* backend = BackendManager::GetInstance().GetBackend();
  if (!backend) return false;

  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  const int32_t minValidLod = (model >= CardModel::Voodoo3) ? -3 : 0;
  if (thisLod < minValidLod || thisLod > 8 || largeLod < minValidLod ||
      largeLod > 8 || thisLod < largeLod) {
    return false;
  }

  const uint32_t storeTmu = StorageTmu_Locked(tmu);
  const bool umaActive = m_umaEnabled.load(std::memory_order_acquire);
  std::shared_ptr<VirtualTexture> existingToMerge;
  int32_t newLarge = thisLod;
  int32_t newSmall = thisLod;

  auto it = m_tmus[storeTmu].textures.find(startAddress);
  if (it != m_tmus[storeTmu].textures.end()) {
    const auto& existing = it->second;
    if (existing->format == format && existing->aspectRatio == aspect &&
        (existing->declaredLargeLod == largeLod ||
         existing->largeLod == largeLod)) {
      if (thisLod < existing->largeLod) {
        // Growing the base mip level requires preserving smaller mips; fall
        // back to CPU merge path for this rare transition.
        return false;
      }
      existingToMerge = existing;
      newLarge = std::min(existing->largeLod, thisLod);
      newSmall = std::max(existing->smallLod, thisLod);
    }
  }

  uint32_t baseW = 1, baseH = 1;
  ComputeLodDimensions(newLarge, aspect, baseW, baseH);
  const uint32_t totalMipLevels = static_cast<uint32_t>(newSmall - newLarge + 1);
  const uint32_t mipLevel = static_cast<uint32_t>(thisLod - newLarge);

  if (!backend->CommitGpuTextureBuffer(tmu, startAddress, baseW, baseH,
                                       mipLevel, totalMipLevels, copyWidth,
                                       copyHeight, origin)) {
    return false;
  }
  bool otherTmuCommitted = false;
  if (umaActive) {
    otherTmuCommitted = backend->CommitGpuTextureBuffer(
        1u - tmu, startAddress, baseW, baseH, mipLevel, totalMipLevels,
        copyWidth, copyHeight, origin);
  }

  // O(1) steady-state fast path when re-rendering to the exact same RTT target
  // (zero heap allocation, zero overlap scan).
  if (existingToMerge && existingToMerge->largeLod == newLarge &&
      existingToMerge->smallLod == newSmall &&
      existingToMerge->declaredLargeLod == largeLod &&
      existingToMerge->baseWidth == baseW &&
      existingToMerge->baseHeight == baseH &&
      existingToMerge->swizzledMipLevels.empty() &&
      existingToMerge->rawCompressedMipLevels.empty()) {
    existingToMerge->rawDataHash = 0;
    existingToMerge->uploadedToBackend = true;
    existingToMerge->uploadedToBackendPerTmu[tmu] = true;
    if (otherTmuCommitted) {
      existingToMerge->uploadedToBackendPerTmu[1u - tmu] = true;
    }
    return true;
  }

  const uint32_t newBytes =
      CalculateMemoryRequired(newLarge, newSmall, aspect, format);
  const uint64_t newRangeStart = static_cast<uint64_t>(startAddress);
  const uint64_t newRangeEnd = newRangeStart + static_cast<uint64_t>(newBytes);

  std::vector<uint32_t> overlaps;
  uint64_t evictedBytes = 0;
  for (const auto& entry : m_tmus[storeTmu].textures) {
    if (entry.first >= kMultibaseCompositeBase) continue;
    const uint64_t eStart = static_cast<uint64_t>(entry.first);
    const uint64_t eSize =
        (entry.second->allocatedByteSize > 0)
            ? static_cast<uint64_t>(entry.second->allocatedByteSize)
            : static_cast<uint64_t>(CalculateMemoryRequired(
                  entry.second->largeLod, entry.second->smallLod,
                  entry.second->aspectRatio, entry.second->format));
    const uint64_t eEnd = eStart + eSize;
    if (entry.first == startAddress ||
        (eStart < newRangeEnd && newRangeStart < eEnd)) {
      overlaps.push_back(entry.first);
      evictedBytes += eSize;
    }
  }

  const uint64_t currentUsed = m_tmus[storeTmu].allocatedBytes;
  const uint64_t usedAfterEvict =
      (currentUsed >= evictedBytes) ? (currentUsed - evictedBytes) : 0ULL;

  auto tex = std::make_shared<VirtualTexture>();
  tex->startAddress = startAddress;
  tex->format = format;
  tex->aspectRatio = aspect;
  tex->largeLod = newLarge;
  tex->smallLod = newSmall;
  tex->declaredLargeLod = largeLod;
  tex->allocatedByteSize = newBytes;
  tex->baseWidth = baseW;
  tex->baseHeight = baseH;
  tex->isPaletted = false;
  tex->isNcc = false;
  tex->rawDataHash = 0;
  tex->swizzledWithTable = true;
  tex->uploadedToBackend = true;
  tex->uploadedToBackendPerTmu[0] = (tmu == 0) || otherTmuCommitted;
  tex->uploadedToBackendPerTmu[1] = (tmu == 1) || otherTmuCommitted;

  for (uint32_t addr : overlaps) {
    m_tmus[storeTmu].textures.erase(addr);
  }
  m_tmus[storeTmu].allocatedBytes =
      static_cast<uint32_t>(usedAfterEvict + newBytes);
  m_tmus[storeTmu].textures[startAddress] = std::move(tex);
  return true;
}

}  // namespace GlideWrapper
