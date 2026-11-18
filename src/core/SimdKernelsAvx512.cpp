#include "core/SimdKernels.h"

#include <immintrin.h>

#include <algorithm>
#include <cstdint>

namespace GlideWrapper {

namespace {

inline __m512i Expand5x32(__m512i v5) {
  const __m512i m5 = _mm512_set1_epi16(14800);
  return _mm512_add_epi16(_mm512_slli_epi16(v5, 3), _mm512_mulhi_epu16(v5, m5));
}

inline __m512i Expand6x32(__m512i v6) {
  const __m512i m6 = _mm512_set1_epi16(3121);
  return _mm512_add_epi16(_mm512_slli_epi16(v6, 2), _mm512_mulhi_epu16(v6, m6));
}

void ResolveSsaa2x2RowAvx512(const uint32_t* srcRow0, const uint32_t* srcRow1,
                             uint32_t* dstRow, uint32_t dstW) {
  const __m512i zero = _mm512_setzero_si512();
  const __m512i permIdx = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
  uint32_t x = 0;
  for (; x + 8u <= dstW; x += 8u) {
    const __m512i r0 = _mm512_loadu_si512(
        reinterpret_cast<const void*>(srcRow0 + 2u * x));
    const __m512i r1 = _mm512_loadu_si512(
        reinterpret_cast<const void*>(srcRow1 + 2u * x));

    // Per 128-bit lane: [p0,p1,p2,p3] -> [p0,p2,p1,p3]
    const __m512i s0 = _mm512_shuffle_epi32(r0, static_cast<_MM_PERM_ENUM>(0xD8));
    const __m512i s1 = _mm512_shuffle_epi32(r1, static_cast<_MM_PERM_ENUM>(0xD8));

    const __m512i ev0 = _mm512_unpacklo_epi8(s0, zero);
    const __m512i od0 = _mm512_unpackhi_epi8(s0, zero);
    const __m512i ev1 = _mm512_unpacklo_epi8(s1, zero);
    const __m512i od1 = _mm512_unpackhi_epi8(s1, zero);

    const __m512i sum = _mm512_add_epi16(_mm512_add_epi16(ev0, od0),
                                         _mm512_add_epi16(ev1, od1));
    const __m512i avg = _mm512_srli_epi16(sum, 2);
    // packus_epi16 puts 2 output pixels in the low 64-bit qword of each 128-bit lane
    const __m512i packed = _mm512_packus_epi16(avg, avg);
    const __m512i linear = _mm512_permutexvar_epi64(permIdx, packed);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dstRow + x),
                        _mm512_castsi512_si256(linear));
  }
  if (x < dstW) {
    GetAvx2KernelTable().ResolveSsaa2x2Row(srcRow0 + 2u * x, srcRow1 + 2u * x,
                                           dstRow + x, dstW - x);
  }
}

void ApplyGammaLutAvx512(uint32_t* pixels, size_t count,
                         const uint8_t lutR[256], const uint8_t lutG[256],
                         const uint8_t lutB[256]) {
  alignas(64) uint32_t lutR32[256];
  alignas(64) uint32_t lutG32[256];
  alignas(64) uint32_t lutB32[256];
  for (uint32_t i = 0; i < 256u; ++i) {
    lutR32[i] = static_cast<uint32_t>(lutR[i]) << 16;
    lutG32[i] = static_cast<uint32_t>(lutG[i]) << 8;
    lutB32[i] = static_cast<uint32_t>(lutB[i]);
  }

  const __m512i maskFF = _mm512_set1_epi32(0xFF);
  const __m512i maskAlpha = _mm512_set1_epi32(static_cast<int>(0xFF000000u));

  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m512i p =
        _mm512_loadu_si512(reinterpret_cast<const void*>(pixels + i));
    const __m512i idxR = _mm512_and_si512(_mm512_srli_epi32(p, 16), maskFF);
    const __m512i idxG = _mm512_and_si512(_mm512_srli_epi32(p, 8), maskFF);
    const __m512i idxB = _mm512_and_si512(p, maskFF);

    const __m512i outR = _mm512_i32gather_epi32(idxR, lutR32, 4);
    const __m512i outG = _mm512_i32gather_epi32(idxG, lutG32, 4);
    const __m512i outB = _mm512_i32gather_epi32(idxB, lutB32, 4);
    const __m512i out = _mm512_or_si512(
        _mm512_or_si512(_mm512_and_si512(p, maskAlpha), outR),
        _mm512_or_si512(outG, outB));
    _mm512_storeu_si512(reinterpret_cast<void*>(pixels + i), out);
  }
  if (i < count) {
    GetAvx2KernelTable().ApplyGammaLut(pixels + i, count - i, lutR, lutG, lutB);
  }
}

void ClearColorMaskedAvx512(uint32_t* dst, size_t count,
                            uint32_t maskedClearWord, uint32_t keepMask) {
  size_t i = 0;
  if (keepMask == 0u) {
    const __m512i vClear =
        _mm512_set1_epi32(static_cast<int>(maskedClearWord));
    for (; i + 16u <= count; i += 16u) {
      _mm512_storeu_si512(reinterpret_cast<void*>(dst + i), vClear);
    }
  } else {
    const __m512i vKeep = _mm512_set1_epi32(static_cast<int>(keepMask));
    const __m512i vClear =
        _mm512_set1_epi32(static_cast<int>(maskedClearWord));
    for (; i + 16u <= count; i += 16u) {
      __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(dst + i));
      cur = _mm512_or_si512(_mm512_and_si512(cur, vKeep), vClear);
      _mm512_storeu_si512(reinterpret_cast<void*>(dst + i), cur);
    }
  }
  if (i < count) {
    GetAvx2KernelTable().ClearColorMasked(dst + i, count - i, maskedClearWord,
                                          keepMask);
  }
}

void ClearColorTBufferRowAvx512(uint32_t* row, uint32_t x0, uint32_t x1,
                                uint32_t clearWord, uint32_t keepMask,
                                uint32_t maskedClearWord,
                                uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearColorMaskedAvx512(row + x0, x1 - x0,
                           (keepMask == 0u) ? clearWord : maskedClearWord,
                           keepMask);
    return;
  }

  const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
  const __mmask16 kMask = startActive ? static_cast<__mmask16>(0x5555u)
                                      : static_cast<__mmask16>(0xAAAAu);
  uint32_t x = x0;
  if (keepMask == 0u) {
    const __m512i vClear = _mm512_set1_epi32(static_cast<int>(clearWord));
    for (; x + 16u <= x1; x += 16u) {
      const __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(row + x));
      _mm512_storeu_si512(reinterpret_cast<void*>(row + x),
                          _mm512_mask_blend_epi32(kMask, cur, vClear));
    }
  } else {
    const __m512i vKeep = _mm512_set1_epi32(static_cast<int>(keepMask));
    const __m512i vClear =
        _mm512_set1_epi32(static_cast<int>(maskedClearWord));
    for (; x + 16u <= x1; x += 16u) {
      const __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(row + x));
      const __m512i upd = _mm512_or_si512(_mm512_and_si512(cur, vKeep), vClear);
      _mm512_storeu_si512(reinterpret_cast<void*>(row + x),
                          _mm512_mask_blend_epi32(kMask, cur, upd));
    }
  }
  if (x < x1) {
    GetScalarKernelTable().ClearColorTBufferRow(row, x, x1, clearWord, keepMask,
                                                maskedClearWord, rowParityMask);
  }
}

void ClearDepthTBufferRowAvx512(float* row, uint32_t x0, uint32_t x1,
                                float zVal, uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  const __m512 vZ = _mm512_set1_ps(zVal);
  uint32_t x = x0;
  if (rowParityMask == 3u) {
    for (; x + 16u <= x1; x += 16u) {
      _mm512_storeu_ps(row + x, vZ);
    }
  } else {
    const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
    const __mmask16 kMask = startActive ? static_cast<__mmask16>(0x5555u)
                                        : static_cast<__mmask16>(0xAAAAu);
    for (; x + 16u <= x1; x += 16u) {
      const __m512 cur = _mm512_loadu_ps(row + x);
      _mm512_storeu_ps(row + x, _mm512_mask_blend_ps(kMask, cur, vZ));
    }
  }
  if (x < x1) {
    GetScalarKernelTable().ClearDepthTBufferRow(row, x, x1, zVal,
                                                rowParityMask);
  }
}

void ClearStencilMaskedAvx512(uint8_t* dst, size_t count,
                              uint8_t maskedClearVal, uint8_t keepMask) {
  size_t i = 0;
  if (keepMask == 0u) {
    const __m512i vClear = _mm512_set1_epi8(static_cast<char>(maskedClearVal));
    for (; i + 64u <= count; i += 64u) {
      _mm512_storeu_si512(reinterpret_cast<void*>(dst + i), vClear);
    }
  } else {
    const __m512i vKeep = _mm512_set1_epi8(static_cast<char>(keepMask));
    const __m512i vClear = _mm512_set1_epi8(static_cast<char>(maskedClearVal));
    for (; i + 64u <= count; i += 64u) {
      __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(dst + i));
      cur = _mm512_or_si512(_mm512_and_si512(cur, vKeep), vClear);
      _mm512_storeu_si512(reinterpret_cast<void*>(dst + i), cur);
    }
  }
  if (i < count) {
    GetAvx2KernelTable().ClearStencilMasked(dst + i, count - i, maskedClearVal,
                                            keepMask);
  }
}

void ClearStencilTBufferRowAvx512(uint8_t* row, uint32_t x0, uint32_t x1,
                                  uint8_t clearVal, uint8_t maskedClearVal,
                                  uint8_t keepMask, uint32_t rowParityMask) {
  if (rowParityMask == 0u || x0 >= x1) return;
  if (rowParityMask == 3u) {
    ClearStencilMaskedAvx512(row + x0, x1 - x0,
                             (keepMask == 0u) ? clearVal : maskedClearVal,
                             keepMask);
    return;
  }

  const bool startActive = ((rowParityMask >> (x0 & 1u)) & 1u) != 0u;
  const __mmask64 kMask =
      startActive ? static_cast<__mmask64>(0x5555555555555555ULL)
                  : static_cast<__mmask64>(0xAAAAAAAAAAAAAAAAULL);
  uint32_t x = x0;
  if (keepMask == 0u) {
    const __m512i vClear = _mm512_set1_epi8(static_cast<char>(clearVal));
    for (; x + 64u <= x1; x += 64u) {
      const __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(row + x));
      _mm512_storeu_si512(reinterpret_cast<void*>(row + x),
                          _mm512_mask_blend_epi8(kMask, cur, vClear));
    }
  } else {
    const __m512i vKeep = _mm512_set1_epi8(static_cast<char>(keepMask));
    const __m512i vClear = _mm512_set1_epi8(static_cast<char>(maskedClearVal));
    for (; x + 64u <= x1; x += 64u) {
      const __m512i cur =
          _mm512_loadu_si512(reinterpret_cast<const void*>(row + x));
      const __m512i upd = _mm512_or_si512(_mm512_and_si512(cur, vKeep), vClear);
      _mm512_storeu_si512(reinterpret_cast<void*>(row + x),
                          _mm512_mask_blend_epi8(kMask, cur, upd));
    }
  }
  if (x < x1) {
    GetScalarKernelTable().ClearStencilTBufferRow(
        row, x, x1, clearVal, maskedClearVal, keepMask, rowParityMask);
  }
}

inline void StoreInterleavedBgRa512(__m512i bg, __m512i ra, uint32_t* dst) {
  // Each 128-bit lane has 8 16-bit elements:
  //   lo = [0..3 | 8..11 | 16..19 | 24..27]
  //   hi = [4..7 | 12..15 | 20..23 | 28..31]
  // Reassemble linear [0..15] and [16..31] via two 128-bit permutes.
  const __m512i lo = _mm512_unpacklo_epi16(bg, ra);
  const __m512i hi = _mm512_unpackhi_epi16(bg, ra);
  const __m512i idx0 = _mm512_setr_epi64(0, 1, 8, 9, 2, 3, 10, 11);
  const __m512i idx1 = _mm512_setr_epi64(4, 5, 12, 13, 6, 7, 14, 15);
  const __m512i out0 = _mm512_permutex2var_epi64(lo, idx0, hi);
  const __m512i out1 = _mm512_permutex2var_epi64(lo, idx1, hi);
  _mm512_storeu_si512(reinterpret_cast<void*>(dst), out0);
  _mm512_storeu_si512(reinterpret_cast<void*>(dst + 16u), out1);
}

void ConvertRgb565ToBgra32Avx512(const uint16_t* src, uint32_t* dst,
                                 size_t count) {
  const __m512i mask5 = _mm512_set1_epi16(0x001F);
  const __m512i mask6 = _mm512_set1_epi16(0x003F);
  const __m512i alphaFF = _mm512_set1_epi16(static_cast<short>(0xFF00u));

  size_t i = 0;
  for (; i + 32u <= count; i += 32u) {
    const __m512i c16 =
        _mm512_loadu_si512(reinterpret_cast<const void*>(src + i));
    const __m512i r5 = _mm512_srli_epi16(c16, 11);
    const __m512i g6 = _mm512_and_si512(_mm512_srli_epi16(c16, 5), mask6);
    const __m512i b5 = _mm512_and_si512(c16, mask5);

    const __m512i r8 = Expand5x32(r5);
    const __m512i g8 = Expand6x32(g6);
    const __m512i b8 = Expand5x32(b5);

    const __m512i bg = _mm512_or_si512(b8, _mm512_slli_epi16(g8, 8));
    const __m512i ra = _mm512_or_si512(r8, alphaFF);
    StoreInterleavedBgRa512(bg, ra, dst + i);
  }
  if (i < count) {
    GetAvx2KernelTable().ConvertRgb565ToBgra32(src + i, dst + i, count - i);
  }
}

void ConvertArgb1555ToBgra32Avx512(const uint16_t* src, uint32_t* dst,
                                   size_t count, bool preserveAlpha) {
  const __m512i mask5 = _mm512_set1_epi16(0x001F);
  const __m512i alphaFF = _mm512_set1_epi16(static_cast<short>(0xFF00u));

  size_t i = 0;
  for (; i + 32u <= count; i += 32u) {
    const __m512i c16 =
        _mm512_loadu_si512(reinterpret_cast<const void*>(src + i));
    const __m512i r5 = _mm512_and_si512(_mm512_srli_epi16(c16, 10), mask5);
    const __m512i g5 = _mm512_and_si512(_mm512_srli_epi16(c16, 5), mask5);
    const __m512i b5 = _mm512_and_si512(c16, mask5);

    const __m512i r8 = Expand5x32(r5);
    const __m512i g8 = Expand5x32(g5);
    const __m512i b8 = Expand5x32(b5);

    __m512i aHi = alphaFF;
    if (preserveAlpha) {
      aHi = _mm512_and_si512(_mm512_srai_epi16(c16, 15), alphaFF);
    }

    const __m512i bg = _mm512_or_si512(b8, _mm512_slli_epi16(g8, 8));
    const __m512i ra = _mm512_or_si512(r8, aHi);
    StoreInterleavedBgRa512(bg, ra, dst + i);
  }
  if (i < count) {
    GetAvx2KernelTable().ConvertArgb1555ToBgra32(src + i, dst + i, count - i,
                                                 preserveAlpha);
  }
}

void ConvertArgb4444ToBgra32Avx512(const uint16_t* src, uint32_t* dst,
                                   size_t count) {
  const __m512i mask4 = _mm512_set1_epi16(0x000F);

  size_t i = 0;
  for (; i + 32u <= count; i += 32u) {
    const __m512i c16 =
        _mm512_loadu_si512(reinterpret_cast<const void*>(src + i));
    const __m512i a4 = _mm512_srli_epi16(c16, 12);
    const __m512i r4 = _mm512_and_si512(_mm512_srli_epi16(c16, 8), mask4);
    const __m512i g4 = _mm512_and_si512(_mm512_srli_epi16(c16, 4), mask4);
    const __m512i b4 = _mm512_and_si512(c16, mask4);

    const __m512i a8 = _mm512_or_si512(_mm512_slli_epi16(a4, 4), a4);
    const __m512i r8 = _mm512_or_si512(_mm512_slli_epi16(r4, 4), r4);
    const __m512i g8 = _mm512_or_si512(_mm512_slli_epi16(g4, 4), g4);
    const __m512i b8 = _mm512_or_si512(_mm512_slli_epi16(b4, 4), b4);

    const __m512i bg = _mm512_or_si512(b8, _mm512_slli_epi16(g8, 8));
    const __m512i ra = _mm512_or_si512(r8, _mm512_slli_epi16(a8, 8));
    StoreInterleavedBgRa512(bg, ra, dst + i);
  }
  if (i < count) {
    GetAvx2KernelTable().ConvertArgb4444ToBgra32(src + i, dst + i, count - i);
  }
}

void ConvertBgra32ToRgb565Avx512(const uint32_t* src, uint16_t* dst,
                                 size_t count) {
  const __m512i maskFF = _mm512_set1_epi32(0xFF);
  const __m512i c31 = _mm512_set1_epi32(31);
  const __m512i c63 = _mm512_set1_epi32(63);
  const __m512i c127 = _mm512_set1_epi32(127);
  const __m512i c1 = _mm512_set1_epi32(1);

  auto div255 = [&](__m512i x) {
    return _mm512_srli_epi32(
        _mm512_add_epi32(_mm512_add_epi32(x, c1), _mm512_srli_epi32(x, 8)), 8);
  };

  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m512i p =
        _mm512_loadu_si512(reinterpret_cast<const void*>(src + i));
    const __m512i r8 = _mm512_and_si512(_mm512_srli_epi32(p, 16), maskFF);
    const __m512i g8 = _mm512_and_si512(_mm512_srli_epi32(p, 8), maskFF);
    const __m512i b8 = _mm512_and_si512(p, maskFF);

    const __m512i r5 = div255(_mm512_add_epi32(_mm512_mullo_epi32(r8, c31), c127));
    const __m512i g6 = div255(_mm512_add_epi32(_mm512_mullo_epi32(g8, c63), c127));
    const __m512i b5 = div255(_mm512_add_epi32(_mm512_mullo_epi32(b8, c31), c127));

    const __m512i c565 = _mm512_or_si512(
        _mm512_or_si512(_mm512_slli_epi32(r5, 11), _mm512_slli_epi32(g6, 5)),
        b5);
    // Native AVX-512F 32-bit -> 16-bit truncation across all 16 lanes in 1 op!
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i),
                        _mm512_cvtepi32_epi16(c565));
  }
  if (i < count) {
    GetAvx2KernelTable().ConvertBgra32ToRgb565(src + i, dst + i, count - i);
  }
}

void DecodeP8ToBgra32Avx512(const uint8_t* src, const uint32_t palette[256],
                            uint32_t* dst, size_t count) {
  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m128i bytes =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
    const __m512i idx = _mm512_cvtepu8_epi32(bytes);
    const __m512i colors = _mm512_i32gather_epi32(idx, palette, 4);
    _mm512_storeu_si512(reinterpret_cast<void*>(dst + i), colors);
  }
  if (i < count) {
    GetAvx2KernelTable().DecodeP8ToBgra32(src + i, palette, dst + i, count - i);
  }
}

void SwapRB32Avx512(const uint32_t* src, uint32_t* dst, size_t count) {
  const __m128i shuf128 =
      _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
  const __m512i shuf = _mm512_broadcast_i32x4(shuf128);
  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    const __m512i v =
        _mm512_loadu_si512(reinterpret_cast<const void*>(src + i));
    _mm512_storeu_si512(reinterpret_cast<void*>(dst + i),
                        _mm512_shuffle_epi8(v, shuf));
  }
  if (i < count) {
    GetAvx2KernelTable().SwapRB32(src + i, dst + i, count - i);
  }
}

const SimdKernelTable kAvx512Kernels = {
    ResolveSsaa2x2RowAvx512,     ApplyGammaLutAvx512,
    ClearColorMaskedAvx512,      ClearColorTBufferRowAvx512,
    ClearDepthTBufferRowAvx512,  ClearStencilMaskedAvx512,
    ClearStencilTBufferRowAvx512, ConvertRgb565ToBgra32Avx512,
    ConvertArgb1555ToBgra32Avx512, ConvertArgb4444ToBgra32Avx512,
    ConvertBgra32ToRgb565Avx512, DecodeP8ToBgra32Avx512,
    SwapRB32Avx512,
};

}  // namespace

const SimdKernelTable& GetAvx512KernelTable() { return kAvx512Kernels; }

}  // namespace GlideWrapper
