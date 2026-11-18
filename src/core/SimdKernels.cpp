#include "core/SimdKernels.h"

#include <algorithm>
#include <array>

#include "core/SimdDispatch.h"

namespace GlideWrapper {

namespace {

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

void ResolveSsaa2x2RowScalar(const uint32_t* srcRow0, const uint32_t* srcRow1,
                             uint32_t* dstRow, uint32_t dstW) {
  for (uint32_t x = 0; x < dstW; ++x) {
    const uint32_t p00 = srcRow0[2u * x];
    const uint32_t p10 = srcRow0[2u * x + 1u];
    const uint32_t p01 = srcRow1[2u * x];
    const uint32_t p11 = srcRow1[2u * x + 1u];
    const uint32_t a = (((p00 >> 24) & 0xFFu) + ((p10 >> 24) & 0xFFu) +
                        ((p01 >> 24) & 0xFFu) + ((p11 >> 24) & 0xFFu)) >>
                       2;
    const uint32_t r = (((p00 >> 16) & 0xFFu) + ((p10 >> 16) & 0xFFu) +
                        ((p01 >> 16) & 0xFFu) + ((p11 >> 16) & 0xFFu)) >>
                       2;
    const uint32_t g = (((p00 >> 8) & 0xFFu) + ((p10 >> 8) & 0xFFu) +
                        ((p01 >> 8) & 0xFFu) + ((p11 >> 8) & 0xFFu)) >>
                       2;
    const uint32_t b =
        ((p00 & 0xFFu) + (p10 & 0xFFu) + (p01 & 0xFFu) + (p11 & 0xFFu)) >> 2;
    dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
  }
}

void ApplyGammaLutScalar(uint32_t* pixels, size_t count,
                         const uint8_t lutR[256], const uint8_t lutG[256],
                         const uint8_t lutB[256]) {
  for (size_t i = 0; i < count; ++i) {
    const uint32_t p = pixels[i];
    pixels[i] = (p & 0xFF000000u) |
                (static_cast<uint32_t>(lutR[(p >> 16) & 0xFFu]) << 16) |
                (static_cast<uint32_t>(lutG[(p >> 8) & 0xFFu]) << 8) |
                static_cast<uint32_t>(lutB[p & 0xFFu]);
  }
}

void ClearColorMaskedScalar(uint32_t* dst, size_t count,
                            uint32_t maskedClearWord, uint32_t keepMask) {
  if (keepMask == 0u) {
    std::fill_n(dst, count, maskedClearWord);
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    dst[i] = (dst[i] & keepMask) | maskedClearWord;
  }
}

void ClearColorTBufferRowScalar(uint32_t* row, uint32_t x0, uint32_t x1,
                                uint32_t clearWord, uint32_t keepMask,
                                uint32_t maskedClearWord,
                                uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearColorMaskedScalar(row + x0, x1 - x0,
                           (keepMask == 0u) ? clearWord : maskedClearWord,
                           keepMask);
    return;
  }
  const uint32_t targetParity = (rowParityMask == 1u) ? 0u : 1u;
  uint32_t x = x0;
  if ((x & 1u) != targetParity) ++x;
  if (keepMask == 0u) {
    for (; x < x1; x += 2u) {
      row[x] = clearWord;
    }
  } else {
    for (; x < x1; x += 2u) {
      row[x] = (row[x] & keepMask) | maskedClearWord;
    }
  }
}

void ClearDepthTBufferRowScalar(float* row, uint32_t x0, uint32_t x1,
                                float zVal, uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    std::fill_n(row + x0, x1 - x0, zVal);
    return;
  }
  const uint32_t targetParity = (rowParityMask == 1u) ? 0u : 1u;
  uint32_t x = x0;
  if ((x & 1u) != targetParity) ++x;
  for (; x < x1; x += 2u) {
    row[x] = zVal;
  }
}

void ClearStencilMaskedScalar(uint8_t* dst, size_t count,
                              uint8_t maskedClearVal, uint8_t keepMask) {
  if (keepMask == 0u) {
    std::fill_n(dst, count, maskedClearVal);
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    dst[i] = static_cast<uint8_t>((dst[i] & keepMask) | maskedClearVal);
  }
}

void ClearStencilTBufferRowScalar(uint8_t* row, uint32_t x0, uint32_t x1,
                                  uint8_t clearVal, uint8_t maskedClearVal,
                                  uint8_t keepMask, uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearStencilMaskedScalar(row + x0, x1 - x0,
                             (keepMask == 0u) ? clearVal : maskedClearVal,
                             keepMask);
    return;
  }
  const uint32_t targetParity = (rowParityMask == 1u) ? 0u : 1u;
  uint32_t x = x0;
  if ((x & 1u) != targetParity) ++x;
  if (keepMask == 0u) {
    for (; x < x1; x += 2u) {
      row[x] = clearVal;
    }
  } else {
    for (; x < x1; x += 2u) {
      row[x] = static_cast<uint8_t>((row[x] & keepMask) | maskedClearVal);
    }
  }
}

void ConvertRgb565ToBgra32Scalar(const uint16_t* src, uint32_t* dst,
                                 size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const uint16_t c16 = src[i];
    const uint32_t r = kExpand5[(c16 >> 11) & 0x1Fu];
    const uint32_t g = kExpand6[(c16 >> 5) & 0x3Fu];
    const uint32_t b = kExpand5[c16 & 0x1Fu];
    dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
}

void ConvertArgb1555ToBgra32Scalar(const uint16_t* src, uint32_t* dst,
                                   size_t count, bool preserveAlpha) {
  for (size_t i = 0; i < count; ++i) {
    const uint16_t c16 = src[i];
    const uint32_t r = kExpand5[(c16 >> 10) & 0x1Fu];
    const uint32_t g = kExpand5[(c16 >> 5) & 0x1Fu];
    const uint32_t b = kExpand5[c16 & 0x1Fu];
    const uint32_t a =
        preserveAlpha ? ((c16 & 0x8000u) ? 0xFF000000u : 0u) : 0xFF000000u;
    dst[i] = a | (r << 16) | (g << 8) | b;
  }
}

void ConvertArgb4444ToBgra32Scalar(const uint16_t* src, uint32_t* dst,
                                   size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const uint16_t val = src[i];
    const uint32_t a = ((val >> 12) & 0xFu) * 17u;
    const uint32_t r = ((val >> 8) & 0xFu) * 17u;
    const uint32_t g = ((val >> 4) & 0xFu) * 17u;
    const uint32_t b = (val & 0xFu) * 17u;
    dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
  }
}

void ConvertBgra32ToRgb565Scalar(const uint32_t* src, uint16_t* dst,
                                 size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const uint32_t pWord = src[i];
    const uint32_t rVal = (pWord >> 16) & 0xFFu;
    const uint32_t gVal = (pWord >> 8) & 0xFFu;
    const uint32_t bVal = pWord & 0xFFu;
    const uint16_t r = static_cast<uint16_t>((rVal * 31u + 127u) / 255u);
    const uint16_t g = static_cast<uint16_t>((gVal * 63u + 127u) / 255u);
    const uint16_t b = static_cast<uint16_t>((bVal * 31u + 127u) / 255u);
    dst[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  }
}

void DecodeP8ToBgra32Scalar(const uint8_t* src, const uint32_t palette[256],
                            uint32_t* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    dst[i] = palette[src[i]];
  }
}

void SwapRB32Scalar(const uint32_t* src, uint32_t* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const uint32_t p = src[i];
    dst[i] = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
  }
}

const SimdKernelTable kScalarKernels = {
    ResolveSsaa2x2RowScalar,     ApplyGammaLutScalar,
    ClearColorMaskedScalar,      ClearColorTBufferRowScalar,
    ClearDepthTBufferRowScalar,  ClearStencilMaskedScalar,
    ClearStencilTBufferRowScalar, ConvertRgb565ToBgra32Scalar,
    ConvertArgb1555ToBgra32Scalar, ConvertArgb4444ToBgra32Scalar,
    ConvertBgra32ToRgb565Scalar, DecodeP8ToBgra32Scalar,
    SwapRB32Scalar,
};

}  // namespace

const SimdKernelTable& GetScalarKernelTable() { return kScalarKernels; }

const SimdKernelTable& GetSimdKernelsForLevel(SwSimdLevel level) {
  const SwSimdLevel eff = ResolveEffectiveSimdLevel(level, nullptr);
  switch (eff) {
    case SwSimdLevel::Avx512:
      return GetAvx512KernelTable();
    case SwSimdLevel::Avx2:
      return GetAvx2KernelTable();
    case SwSimdLevel::Scalar:
    case SwSimdLevel::Auto:
    default:
      return kScalarKernels;
  }
}

const SimdKernelTable& GetSimdKernels() {
  switch (GetActiveSimdLevel()) {
    case SwSimdLevel::Avx512:
      return GetAvx512KernelTable();
    case SwSimdLevel::Avx2:
      return GetAvx2KernelTable();
    case SwSimdLevel::Scalar:
    case SwSimdLevel::Auto:
    default:
      return kScalarKernels;
  }
}

}  // namespace GlideWrapper
