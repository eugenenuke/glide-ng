#pragma once

#include <cstddef>
#include <cstdint>

#include "core/WrapperConfig.h"

namespace GlideWrapper {

struct SimdKernelTable {
  // 2x2 SSAA row downsample: dstRow[x] = avg(srcRow0[2x..2x+1], srcRow1[2x..2x+1])
  void (*ResolveSsaa2x2Row)(const uint32_t* srcRow0, const uint32_t* srcRow1,
                            uint32_t* dstRow, uint32_t dstW);

  // In-place RAMDAC 8-bit per-channel Gamma LUT (preserves alpha)
  void (*ApplyGammaLut)(uint32_t* pixels, size_t count,
                        const uint8_t lutR[256], const uint8_t lutG[256],
                        const uint8_t lutB[256]);

  // Masked color clear: dst[i] = (dst[i] & keepMask) | maskedClearWord
  void (*ClearColorMasked)(uint32_t* dst, size_t count,
                           uint32_t maskedClearWord, uint32_t keepMask);

  // T-Buffer sample-masked color row clear (rowParityMask: 1=even, 2=odd, 3=both)
  void (*ClearColorTBufferRow)(uint32_t* row, uint32_t x0, uint32_t x1,
                               uint32_t clearWord, uint32_t keepMask,
                               uint32_t maskedClearWord,
                               uint32_t rowParityMask);

  // T-Buffer sample-masked depth row clear
  void (*ClearDepthTBufferRow)(float* row, uint32_t x0, uint32_t x1, float zVal,
                               uint32_t rowParityMask);

  // Masked stencil clear: dst[i] = (dst[i] & keepMask) | maskedClearVal
  void (*ClearStencilMasked)(uint8_t* dst, size_t count, uint8_t maskedClearVal,
                             uint8_t keepMask);

  // T-Buffer sample-masked stencil row clear
  void (*ClearStencilTBufferRow)(uint8_t* row, uint32_t x0, uint32_t x1,
                                 uint8_t clearVal, uint8_t maskedClearVal,
                                 uint8_t keepMask, uint32_t rowParityMask);

  // Pixel format conversions (100% bit-exact with scalar kExpand5/kExpand6)
  void (*ConvertRgb565ToBgra32)(const uint16_t* src, uint32_t* dst,
                                size_t count);
  void (*ConvertArgb1555ToBgra32)(const uint16_t* src, uint32_t* dst,
                                  size_t count, bool preserveAlpha);
  void (*ConvertArgb4444ToBgra32)(const uint16_t* src, uint32_t* dst,
                                  size_t count);
  void (*ConvertBgra32ToRgb565)(const uint32_t* src, uint16_t* dst,
                                size_t count);

  // Palette lookup: dst[i] = palette[src[i]]
  void (*DecodeP8ToBgra32)(const uint8_t* src, const uint32_t palette[256],
                           uint32_t* dst, size_t count);

  // BGRA <-> RGBA channel swap (safe for src == dst in-place)
  void (*SwapRB32)(const uint32_t* src, uint32_t* dst, size_t count);
};

// Returns kernel table matching the active runtime SwSimdLevel.
const SimdKernelTable& GetSimdKernels();

// Returns kernel table for a specific resolved level (host-capability guarded;
// intended for unit tests comparing Scalar vs AVX2 vs AVX-512 bit-exactness).
const SimdKernelTable& GetSimdKernelsForLevel(SwSimdLevel level);

// Internal per-ISA table accessors (implemented in isolated TUs).
const SimdKernelTable& GetScalarKernelTable();
const SimdKernelTable& GetAvx2KernelTable();
const SimdKernelTable& GetAvx512KernelTable();

}  // namespace GlideWrapper
