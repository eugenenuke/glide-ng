#include "core/SimdKernels.h"

#include <immintrin.h>

#include <algorithm>
#include <cstdint>

namespace GlideWrapper {

namespace {

// Exact 5-bit & 6-bit expansion helpers (verified 100% bit-exact with
// (i * 255) / 31 and (i * 255) / 63 across all 0..31 and 0..63 inputs).
inline __m256i Expand5x16(__m256i v5) {
  const __m256i m5 = _mm256_set1_epi16(14800);
  return _mm256_add_epi16(_mm256_slli_epi16(v5, 3), _mm256_mulhi_epu16(v5, m5));
}

inline __m256i Expand6x16(__m256i v6) {
  const __m256i m6 = _mm256_set1_epi16(3121);
  return _mm256_add_epi16(_mm256_slli_epi16(v6, 2), _mm256_mulhi_epu16(v6, m6));
}

void ResolveSsaa2x2RowAvx2(const uint32_t* srcRow0, const uint32_t* srcRow1,
                           uint32_t* dstRow, uint32_t dstW) {
  const __m256i zero = _mm256_setzero_si256();
  uint32_t x = 0;
  for (; x + 4u <= dstW; x += 4u) {
    const __m256i r0 = _mm256_loadu_si256(
        reinterpret_cast<const __m256i*>(srcRow0 + 2u * x));
    const __m256i r1 = _mm256_loadu_si256(
        reinterpret_cast<const __m256i*>(srcRow1 + 2u * x));

    // Reorder [p0,p1,p2,p3 | p4,p5,p6,p7] -> [p0,p2,p1,p3 | p4,p6,p5,p7]
    const __m256i s0 = _mm256_shuffle_epi32(r0, _MM_SHUFFLE(3, 1, 2, 0));
    const __m256i s1 = _mm256_shuffle_epi32(r1, _MM_SHUFFLE(3, 1, 2, 0));

    const __m256i ev0 = _mm256_unpacklo_epi8(s0, zero);
    const __m256i od0 = _mm256_unpackhi_epi8(s0, zero);
    const __m256i ev1 = _mm256_unpacklo_epi8(s1, zero);
    const __m256i od1 = _mm256_unpackhi_epi8(s1, zero);

    const __m256i sum =
        _mm256_add_epi16(_mm256_add_epi16(ev0, od0), _mm256_add_epi16(ev1, od1));
    const __m256i avg = _mm256_srli_epi16(sum, 2);
    const __m256i packed = _mm256_packus_epi16(avg, avg);
    const __m256i linear =
        _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dstRow + x),
                     _mm256_castsi256_si128(linear));
  }
  if (x < dstW) {
    GetScalarKernelTable().ResolveSsaa2x2Row(srcRow0 + 2u * x, srcRow1 + 2u * x,
                                             dstRow + x, dstW - x);
  }
}

void ApplyGammaLutAvx2(uint32_t* pixels, size_t count, const uint8_t lutR[256],
                       const uint8_t lutG[256], const uint8_t lutB[256]) {
  alignas(32) uint32_t lutR32[256];
  alignas(32) uint32_t lutG32[256];
  alignas(32) uint32_t lutB32[256];
  for (uint32_t i = 0; i < 256u; ++i) {
    lutR32[i] = static_cast<uint32_t>(lutR[i]) << 16;
    lutG32[i] = static_cast<uint32_t>(lutG[i]) << 8;
    lutB32[i] = static_cast<uint32_t>(lutB[i]);
  }

  const __m256i maskFF = _mm256_set1_epi32(0xFF);
  const __m256i maskAlpha = _mm256_set1_epi32(static_cast<int>(0xFF000000u));
  const int* pR = reinterpret_cast<const int*>(lutR32);
  const int* pG = reinterpret_cast<const int*>(lutG32);
  const int* pB = reinterpret_cast<const int*>(lutB32);

  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    const __m256i p =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pixels + i));
    const __m256i idxR = _mm256_and_si256(_mm256_srli_epi32(p, 16), maskFF);
    const __m256i idxG = _mm256_and_si256(_mm256_srli_epi32(p, 8), maskFF);
    const __m256i idxB = _mm256_and_si256(p, maskFF);

    const __m256i outR = _mm256_i32gather_epi32(pR, idxR, 4);
    const __m256i outG = _mm256_i32gather_epi32(pG, idxG, 4);
    const __m256i outB = _mm256_i32gather_epi32(pB, idxB, 4);
    const __m256i out = _mm256_or_si256(
        _mm256_or_si256(_mm256_and_si256(p, maskAlpha), outR),
        _mm256_or_si256(outG, outB));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels + i), out);
  }
  if (i < count) {
    GetScalarKernelTable().ApplyGammaLut(pixels + i, count - i, lutR, lutG,
                                         lutB);
  }
}

void ClearColorMaskedAvx2(uint32_t* dst, size_t count, uint32_t maskedClearWord,
                          uint32_t keepMask) {
  size_t i = 0;
  if (keepMask == 0u) {
    const __m256i vClear =
        _mm256_set1_epi32(static_cast<int>(maskedClearWord));
    for (; i + 8u <= count; i += 8u) {
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), vClear);
    }
  } else {
    const __m256i vKeep = _mm256_set1_epi32(static_cast<int>(keepMask));
    const __m256i vClear =
        _mm256_set1_epi32(static_cast<int>(maskedClearWord));
    for (; i + 8u <= count; i += 8u) {
      __m256i cur =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst + i));
      cur = _mm256_or_si256(_mm256_and_si256(cur, vKeep), vClear);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), cur);
    }
  }
  if (i < count) {
    GetScalarKernelTable().ClearColorMasked(dst + i, count - i, maskedClearWord,
                                            keepMask);
  }
}

void ClearColorTBufferRowAvx2(uint32_t* row, uint32_t x0, uint32_t x1,
                              uint32_t clearWord, uint32_t keepMask,
                              uint32_t maskedClearWord,
                              uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearColorMaskedAvx2(row + x0, x1 - x0,
                         (keepMask == 0u) ? clearWord : maskedClearWord,
                         keepMask);
    return;
  }

  // Build effective per-lane keepMask & orWord across 8 adjacent pixels:
  // active lane -> (keepMask, activeClear), untouched lane -> (0xFFFFFFFF, 0).
  const uint32_t activeClear = (keepMask == 0u) ? clearWord : maskedClearWord;
  const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
  const int k0 = static_cast<int>(startActive ? keepMask : 0xFFFFFFFFu);
  const int k1 = static_cast<int>(startActive ? 0xFFFFFFFFu : keepMask);
  const int c0 = static_cast<int>(startActive ? activeClear : 0u);
  const int c1 = static_cast<int>(startActive ? 0u : activeClear);

  const __m256i vKeep = _mm256_setr_epi32(k0, k1, k0, k1, k0, k1, k0, k1);
  const __m256i vOr = _mm256_setr_epi32(c0, c1, c0, c1, c0, c1, c0, c1);

  uint32_t x = x0;
  for (; x + 8u <= x1; x += 8u) {
    __m256i cur =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + x));
    cur = _mm256_or_si256(_mm256_and_si256(cur, vKeep), vOr);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + x), cur);
  }
  if (x < x1) {
    GetScalarKernelTable().ClearColorTBufferRow(row, x, x1, clearWord, keepMask,
                                                maskedClearWord, rowParityMask);
  }
}

void ClearDepthTBufferRowAvx2(float* row, uint32_t x0, uint32_t x1, float zVal,
                              uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  const __m256 vZ = _mm256_set1_ps(zVal);
  uint32_t x = x0;
  if (rowParityMask == 3u) {
    for (; x + 8u <= x1; x += 8u) {
      _mm256_storeu_ps(row + x, vZ);
    }
  } else {
    const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
    if (startActive) {
      for (; x + 8u <= x1; x += 8u) {
        const __m256 cur = _mm256_loadu_ps(row + x);
        _mm256_storeu_ps(row + x, _mm256_blend_ps(cur, vZ, 0x55));
      }
    } else {
      for (; x + 8u <= x1; x += 8u) {
        const __m256 cur = _mm256_loadu_ps(row + x);
        _mm256_storeu_ps(row + x, _mm256_blend_ps(cur, vZ, 0xAA));
      }
    }
  }
  if (x < x1) {
    GetScalarKernelTable().ClearDepthTBufferRow(row, x, x1, zVal,
                                                rowParityMask);
  }
}

void ClearStencilMaskedAvx2(uint8_t* dst, size_t count, uint8_t maskedClearVal,
                            uint8_t keepMask) {
  size_t i = 0;
  if (keepMask == 0u) {
    const __m256i vClear = _mm256_set1_epi8(static_cast<char>(maskedClearVal));
    for (; i + 32u <= count; i += 32u) {
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), vClear);
    }
  } else {
    const __m256i vKeep = _mm256_set1_epi8(static_cast<char>(keepMask));
    const __m256i vClear = _mm256_set1_epi8(static_cast<char>(maskedClearVal));
    for (; i + 32u <= count; i += 32u) {
      __m256i cur =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst + i));
      cur = _mm256_or_si256(_mm256_and_si256(cur, vKeep), vClear);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), cur);
    }
  }
  if (i < count) {
    GetScalarKernelTable().ClearStencilMasked(dst + i, count - i,
                                              maskedClearVal, keepMask);
  }
}

void ClearStencilTBufferRowAvx2(uint8_t* row, uint32_t x0, uint32_t x1,
                                uint8_t clearVal, uint8_t maskedClearVal,
                                uint8_t keepMask, uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearStencilMaskedAvx2(row + x0, x1 - x0,
                           (keepMask == 0u) ? clearVal : maskedClearVal,
                           keepMask);
    return;
  }

  const uint8_t activeClear = (keepMask == 0u) ? clearVal : maskedClearVal;
  const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
  const uint16_t keepPair =
      startActive ? static_cast<uint16_t>(0xFF00u | keepMask)
                  : static_cast<uint16_t>((static_cast<uint16_t>(keepMask) << 8) |
                                          0x00FFu);
  const uint16_t clearPair =
      startActive ? static_cast<uint16_t>(activeClear)
                  : static_cast<uint16_t>(static_cast<uint16_t>(activeClear) << 8);
  const __m256i vKeep = _mm256_set1_epi16(static_cast<short>(keepPair));
  const __m256i vOr = _mm256_set1_epi16(static_cast<short>(clearPair));

  uint32_t x = x0;
  for (; x + 32u <= x1; x += 32u) {
    __m256i cur =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + x));
    cur = _mm256_or_si256(_mm256_and_si256(cur, vKeep), vOr);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + x), cur);
  }
  if (x < x1) {
    GetScalarKernelTable().ClearStencilTBufferRow(
        row, x, x1, clearVal, maskedClearVal, keepMask, rowParityMask);
  }
}

void ConvertRgb565ToBgra32Avx2(const uint16_t* src, uint32_t* dst,
                               size_t count) {
  const __m256i mask5 = _mm256_set1_epi16(0x001F);
  const __m256i mask6 = _mm256_set1_epi16(0x003F);
  const __m256i alphaFF = _mm256_set1_epi16(static_cast<short>(0xFF00u));

  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m256i c16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
    const __m256i r5 = _mm256_srli_epi16(c16, 11);
    const __m256i g6 = _mm256_and_si256(_mm256_srli_epi16(c16, 5), mask6);
    const __m256i b5 = _mm256_and_si256(c16, mask5);

    const __m256i r8 = Expand5x16(r5);
    const __m256i g8 = Expand6x16(g6);
    const __m256i b8 = Expand5x16(b5);

    const __m256i bg = _mm256_or_si256(b8, _mm256_slli_epi16(g8, 8));
    const __m256i ra = _mm256_or_si256(r8, alphaFF);

    const __m256i lo = _mm256_unpacklo_epi16(bg, ra);  // [0..3 | 8..11]
    const __m256i hi = _mm256_unpackhi_epi16(bg, ra);  // [4..7 | 12..15]
    const __m256i out0 = _mm256_permute2x128_si256(lo, hi, 0x20);
    const __m256i out1 = _mm256_permute2x128_si256(lo, hi, 0x31);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), out0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i + 8u), out1);
  }
  if (i < count) {
    GetScalarKernelTable().ConvertRgb565ToBgra32(src + i, dst + i, count - i);
  }
}

void ConvertArgb1555ToBgra32Avx2(const uint16_t* src, uint32_t* dst,
                                 size_t count, bool preserveAlpha) {
  const __m256i mask5 = _mm256_set1_epi16(0x001F);
  const __m256i alphaFF = _mm256_set1_epi16(static_cast<short>(0xFF00u));

  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m256i c16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
    const __m256i r5 = _mm256_and_si256(_mm256_srli_epi16(c16, 10), mask5);
    const __m256i g5 = _mm256_and_si256(_mm256_srli_epi16(c16, 5), mask5);
    const __m256i b5 = _mm256_and_si256(c16, mask5);

    const __m256i r8 = Expand5x16(r5);
    const __m256i g8 = Expand5x16(g5);
    const __m256i b8 = Expand5x16(b5);

    __m256i aHi = alphaFF;
    if (preserveAlpha) {
      // Arithmetic shift right by 15 turns bit 15 into 0xFFFF or 0x0000
      aHi = _mm256_and_si256(_mm256_srai_epi16(c16, 15), alphaFF);
    }

    const __m256i bg = _mm256_or_si256(b8, _mm256_slli_epi16(g8, 8));
    const __m256i ra = _mm256_or_si256(r8, aHi);

    const __m256i lo = _mm256_unpacklo_epi16(bg, ra);
    const __m256i hi = _mm256_unpackhi_epi16(bg, ra);
    const __m256i out0 = _mm256_permute2x128_si256(lo, hi, 0x20);
    const __m256i out1 = _mm256_permute2x128_si256(lo, hi, 0x31);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), out0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i + 8u), out1);
  }
  if (i < count) {
    GetScalarKernelTable().ConvertArgb1555ToBgra32(src + i, dst + i, count - i,
                                                   preserveAlpha);
  }
}

void ConvertArgb4444ToBgra32Avx2(const uint16_t* src, uint32_t* dst,
                                 size_t count) {
  const __m256i mask4 = _mm256_set1_epi16(0x000F);

  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m256i c16 =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
    const __m256i a4 = _mm256_srli_epi16(c16, 12);
    const __m256i r4 = _mm256_and_si256(_mm256_srli_epi16(c16, 8), mask4);
    const __m256i g4 = _mm256_and_si256(_mm256_srli_epi16(c16, 4), mask4);
    const __m256i b4 = _mm256_and_si256(c16, mask4);

    const __m256i a8 = _mm256_or_si256(_mm256_slli_epi16(a4, 4), a4);
    const __m256i r8 = _mm256_or_si256(_mm256_slli_epi16(r4, 4), r4);
    const __m256i g8 = _mm256_or_si256(_mm256_slli_epi16(g4, 4), g4);
    const __m256i b8 = _mm256_or_si256(_mm256_slli_epi16(b4, 4), b4);

    const __m256i bg = _mm256_or_si256(b8, _mm256_slli_epi16(g8, 8));
    const __m256i ra = _mm256_or_si256(r8, _mm256_slli_epi16(a8, 8));

    const __m256i lo = _mm256_unpacklo_epi16(bg, ra);
    const __m256i hi = _mm256_unpackhi_epi16(bg, ra);
    const __m256i out0 = _mm256_permute2x128_si256(lo, hi, 0x20);
    const __m256i out1 = _mm256_permute2x128_si256(lo, hi, 0x31);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), out0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i + 8u), out1);
  }
  if (i < count) {
    GetScalarKernelTable().ConvertArgb4444ToBgra32(src + i, dst + i, count - i);
  }
}

void ConvertBgra32ToRgb565Avx2(const uint32_t* src, uint16_t* dst,
                               size_t count) {
  const __m256i maskFF = _mm256_set1_epi32(0xFF);
  const __m256i c31 = _mm256_set1_epi32(31);
  const __m256i c63 = _mm256_set1_epi32(63);
  const __m256i c127 = _mm256_set1_epi32(127);
  const __m256i c1 = _mm256_set1_epi32(1);

  auto div255 = [&](__m256i x) {
    // Exact for all 0..65535: (x + 1 + (x >> 8)) >> 8
    return _mm256_srli_epi32(
        _mm256_add_epi32(_mm256_add_epi32(x, c1), _mm256_srli_epi32(x, 8)), 8);
  };

  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    const __m256i p =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
    const __m256i r8 = _mm256_and_si256(_mm256_srli_epi32(p, 16), maskFF);
    const __m256i g8 = _mm256_and_si256(_mm256_srli_epi32(p, 8), maskFF);
    const __m256i b8 = _mm256_and_si256(p, maskFF);

    const __m256i r5 = div255(_mm256_add_epi32(_mm256_mullo_epi32(r8, c31), c127));
    const __m256i g6 = div255(_mm256_add_epi32(_mm256_mullo_epi32(g8, c63), c127));
    const __m256i b5 = div255(_mm256_add_epi32(_mm256_mullo_epi32(b8, c31), c127));

    const __m256i c565 = _mm256_or_si256(
        _mm256_or_si256(_mm256_slli_epi32(r5, 11), _mm256_slli_epi32(g6, 5)),
        b5);
    // Pack 8x 32-bit -> 8x 16-bit across lanes
    const __m128i lo = _mm256_castsi256_si128(c565);
    const __m128i hi = _mm256_extracti128_si256(c565, 1);
    const __m128i packed = _mm_packus_epi32(lo, hi);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), packed);
  }
  if (i < count) {
    GetScalarKernelTable().ConvertBgra32ToRgb565(src + i, dst + i, count - i);
  }
}

void DecodeP8ToBgra32Avx2(const uint8_t* src, const uint32_t palette[256],
                          uint32_t* dst, size_t count) {
  const int* pal = reinterpret_cast<const int*>(palette);
  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    const __m128i bytes =
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src + i));
    const __m256i idx = _mm256_cvtepu8_epi32(bytes);
    const __m256i colors = _mm256_i32gather_epi32(pal, idx, 4);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), colors);
  }
  if (i < count) {
    GetScalarKernelTable().DecodeP8ToBgra32(src + i, palette, dst + i,
                                            count - i);
  }
}

void SwapRB32Avx2(const uint32_t* src, uint32_t* dst, size_t count) {
  const __m256i shuf = _mm256_setr_epi8(
      2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15,
      2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    const __m256i v =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i),
                        _mm256_shuffle_epi8(v, shuf));
  }
  if (i < count) {
    GetScalarKernelTable().SwapRB32(src + i, dst + i, count - i);
  }
}

const SimdKernelTable kAvx2Kernels = {
    ResolveSsaa2x2RowAvx2,     ApplyGammaLutAvx2,
    ClearColorMaskedAvx2,      ClearColorTBufferRowAvx2,
    ClearDepthTBufferRowAvx2,  ClearStencilMaskedAvx2,
    ClearStencilTBufferRowAvx2, ConvertRgb565ToBgra32Avx2,
    ConvertArgb1555ToBgra32Avx2, ConvertArgb4444ToBgra32Avx2,
    ConvertBgra32ToRgb565Avx2, DecodeP8ToBgra32Avx2,
    SwapRB32Avx2,
};

}  // namespace

const SimdKernelTable& GetAvx2KernelTable() { return kAvx2Kernels; }

}  // namespace GlideWrapper
