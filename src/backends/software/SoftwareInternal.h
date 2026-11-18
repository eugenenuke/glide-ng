#pragma once

#include <algorithm>
#include <cstdint>

#include "backends/software/SimdVec.h"

namespace GlideWrapper {

// Fast log2 approximation using IEEE-754 float exponent extraction
inline float fast_log2(float val) {
  union {
    float f;
    uint32_t i;
  } vx = {val};
  float y = static_cast<float>(vx.i);
  y *= 1.1920928955078125e-7f;  // 1 / 2^23
  return y - 126.94269504f;
}

inline constexpr float s_tableIndexToW[64] = {
    1.000000f,     1.142857f,     1.333333f,     1.600000f,     2.000000f,
    2.285714f,     2.666667f,     3.200000f,     4.000000f,     4.571429f,
    5.333333f,     6.400000f,     8.000000f,     9.142858f,     10.666667f,
    12.800000f,    16.000000f,    18.285715f,    21.333334f,    25.600000f,
    32.000000f,    36.571430f,    42.666668f,    51.200001f,    64.000000f,
    73.142860f,    85.333336f,    102.400002f,   128.000000f,   146.285721f,
    170.666672f,   204.800003f,   256.000000f,   292.571442f,   341.333344f,
    409.600006f,   512.000000f,   585.142883f,   682.666687f,   819.200012f,
    1024.000000f,  1170.285767f,  1365.333374f,  1638.400024f,  2048.000000f,
    2340.571533f,  2730.666748f,  3276.800049f,  4096.000000f,  4681.143066f,
    5461.333496f,  6553.600098f,  8192.000000f,  9362.286133f,  10922.666992f,
    13107.200195f, 16384.000000f, 18724.572266f, 21845.333984f, 26214.400391f,
    32768.000000f, 37449.144531f, 43690.667969f, 52428.800781f};

struct TmuColor {
  float r, g, b, a;
};

struct TmuColorSIMD {
  Simd8f r, g, b, a;
};

inline TmuColor EvaluateTmuStage(uint32_t rgbFunc, uint32_t rgbFactor,
                                 uint32_t alphaFunc, uint32_t alphaFactor,
                                 bool rgbInvert, bool alphaInvert,
                                 const TmuColor& localVal,
                                 const TmuColor& otherVal,
                                 const TmuColor& /*iteratedCol*/,
                                 float detailFactor = 0.0f,
                                 float lodFraction = 0.0f) {
  // 1. Evaluate Alpha
  float aLocal = localVal.a;
  float aOther = otherVal.a;
  float factA = 0.0f;
  switch (alphaFactor) {
    case 1:
    case 3:
      factA = aLocal;
      break;
    case 2:
      factA = aOther;
      break;
    case 4:  // GR_COMBINE_FACTOR_DETAIL_FACTOR
      factA = detailFactor;
      break;
    case 5:  // GR_COMBINE_FACTOR_LOD_FRACTION
      factA = lodFraction;
      break;
    case 8:
      factA = 1.0f;
      break;
    case 9:
    case 11:
      factA = 1.0f - aLocal;
      break;
    case 10:
      factA = 1.0f - aOther;
      break;
    case 12:  // GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR
      factA = 1.0f - detailFactor;
      break;
    case 13:  // GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION
      factA = 1.0f - lodFraction;
      break;
  }

  float finalA = 0.0f;
  switch (alphaFunc) {
    case 0:
      break;
    case 1:
      finalA = aLocal;
      break;
    case 3:
      finalA = aOther * factA;
      break;
    case 4:
    case 5:
      finalA = aOther * factA + aLocal;
      break;
    case 6:
      finalA = (aOther - aLocal) * factA;
      break;
    case 7:
    case 8:
      finalA = (aOther - aLocal) * factA + aLocal;
      break;
    case 9:
    case 16:
      finalA = (-aLocal) * factA + aLocal;
      break;
  }
  if (alphaInvert) finalA = 1.0f - finalA;

  // 2. Evaluate RGB
  float cLocalR = localVal.r;
  float cLocalG = localVal.g;
  float cLocalB = localVal.b;
  float cOtherR = otherVal.r;
  float cOtherG = otherVal.g;
  float cOtherB = otherVal.b;

  float factCR = 0.0f;
  float factCG = 0.0f;
  float factCB = 0.0f;
  switch (rgbFactor) {
    case 1:
      factCR = cLocalR;
      factCG = cLocalG;
      factCB = cLocalB;
      break;
    case 2:
      factCR = aOther;
      factCG = aOther;
      factCB = aOther;
      break;
    case 3:
      factCR = aLocal;
      factCG = aLocal;
      factCB = aLocal;
      break;
    case 4:  // GR_COMBINE_FACTOR_DETAIL_FACTOR
      factCR = detailFactor;
      factCG = detailFactor;
      factCB = detailFactor;
      break;
    case 5:  // GR_COMBINE_FACTOR_LOD_FRACTION
      factCR = lodFraction;
      factCG = lodFraction;
      factCB = lodFraction;
      break;
    case 8:
      factCR = 1.0f;
      factCG = 1.0f;
      factCB = 1.0f;
      break;
    case 9:
      factCR = 1.0f - cLocalR;
      factCG = 1.0f - cLocalG;
      factCB = 1.0f - cLocalB;
      break;
    case 10:
      factCR = 1.0f - aOther;
      factCG = 1.0f - aOther;
      factCB = 1.0f - aOther;
      break;
    case 11:
      factCR = 1.0f - aLocal;
      factCG = 1.0f - aLocal;
      factCB = 1.0f - aLocal;
      break;
    case 12:  // GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR
      factCR = 1.0f - detailFactor;
      factCG = 1.0f - detailFactor;
      factCB = 1.0f - detailFactor;
      break;
    case 13:  // GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION
      factCR = 1.0f - lodFraction;
      factCG = 1.0f - lodFraction;
      factCB = 1.0f - lodFraction;
      break;
  }

  float finalCR = 0.0f;
  float finalCG = 0.0f;
  float finalCB = 0.0f;
  switch (rgbFunc) {
    case 0:
      break;
    case 1:
      finalCR = cLocalR;
      finalCG = cLocalG;
      finalCB = cLocalB;
      break;
    case 3:
      finalCR = cOtherR * factCR;
      finalCG = cOtherG * factCG;
      finalCB = cOtherB * factCB;
      break;
    case 4:
      finalCR = cOtherR * factCR + cLocalR;
      finalCG = cOtherG * factCG + cLocalG;
      finalCB = cOtherB * factCB + cLocalB;
      break;
    case 5:
      finalCR = cOtherR * factCR + aLocal;
      finalCG = cOtherG * factCG + aLocal;
      finalCB = cOtherB * factCB + aLocal;
      break;
    case 6:
      finalCR = (cOtherR - cLocalR) * factCR;
      finalCG = (cOtherG - cLocalG) * factCG;
      finalCB = (cOtherB - cLocalB) * factCB;
      break;
    case 7:
      finalCR = (cOtherR - cLocalR) * factCR + cLocalR;
      finalCG = (cOtherG - cLocalG) * factCG + cLocalG;
      finalCB = (cOtherB - cLocalB) * factCB + cLocalB;
      break;
    case 8:
      finalCR = (cOtherR - cLocalR) * factCR + aLocal;
      finalCG = (cOtherG - cLocalG) * factCG + aLocal;
      finalCB = (cOtherB - cLocalB) * factCB + aLocal;
      break;
    case 9:
      finalCR = (-cLocalR) * factCR + cLocalR;
      finalCG = (-cLocalG) * factCG + cLocalG;
      finalCB = (-cLocalB) * factCB + cLocalB;
      break;
    case 16:
      finalCR = (-cLocalR) * factCR + aLocal;
      finalCG = (-cLocalG) * factCG + aLocal;
      finalCB = (-cLocalB) * factCB + aLocal;
      break;
  }
  if (rgbInvert) {
    finalCR = 1.0f - finalCR;
    finalCG = 1.0f - finalCG;
    finalCB = 1.0f - finalCB;
  }

  return {finalCR, finalCG, finalCB, finalA};
}

inline TmuColorSIMD EvaluateTmuStageSIMD(uint32_t rgbFunc, uint32_t rgbFactor,
                                         uint32_t alphaFunc,
                                         uint32_t alphaFactor, bool rgbInvert,
                                         bool alphaInvert,
                                         const TmuColorSIMD& localVal,
                                         const TmuColorSIMD& otherVal,
                                         const TmuColorSIMD& /*iteratedCol*/,
                                         const Simd8f& detailFactor = Simd8f(0.0f),
                                         const Simd8f& lodFraction = Simd8f(0.0f)) {
  const Simd8f one(1.0f);
  // 1. Evaluate Alpha
  Simd8f aLocal = localVal.a;
  Simd8f aOther = otherVal.a;
  Simd8f factA = Simd8f(0.0f);
  switch (alphaFactor) {
    case 1:
    case 3:
      factA = aLocal;
      break;
    case 2:
      factA = aOther;
      break;
    case 4:  // GR_COMBINE_FACTOR_DETAIL_FACTOR
      factA = detailFactor;
      break;
    case 5:  // GR_COMBINE_FACTOR_LOD_FRACTION
      factA = lodFraction;
      break;
    case 8:
      factA = one;
      break;
    case 9:
    case 11:
      factA = one - aLocal;
      break;
    case 10:
      factA = one - aOther;
      break;
    case 12:  // GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR
      factA = one - detailFactor;
      break;
    case 13:  // GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION
      factA = one - lodFraction;
      break;
  }

  Simd8f finalA = Simd8f(0.0f);
  switch (alphaFunc) {
    case 0:
      break;
    case 1:
      finalA = aLocal;
      break;
    case 3:
      finalA = aOther * factA;
      break;
    case 4:
    case 5:
      finalA = Fma(aOther, factA, aLocal);
      break;
    case 6:
      finalA = (aOther - aLocal) * factA;
      break;
    case 7:
    case 8:
      finalA = Fma(aOther - aLocal, factA, aLocal);
      break;
    case 9:
    case 16:
      finalA = aLocal * (one - factA);
      break;
  }
  if (alphaInvert) finalA = one - finalA;

  // 2. Evaluate RGB
  Simd8f cLocalR = localVal.r;
  Simd8f cLocalG = localVal.g;
  Simd8f cLocalB = localVal.b;
  Simd8f cOtherR = otherVal.r;
  Simd8f cOtherG = otherVal.g;
  Simd8f cOtherB = otherVal.b;

  Simd8f factCR = Simd8f(0.0f);
  Simd8f factCG = Simd8f(0.0f);
  Simd8f factCB = Simd8f(0.0f);
  switch (rgbFactor) {
    case 1:
      factCR = cLocalR;
      factCG = cLocalG;
      factCB = cLocalB;
      break;
    case 2:
      factCR = aOther;
      factCG = aOther;
      factCB = aOther;
      break;
    case 3:
      factCR = aLocal;
      factCG = aLocal;
      factCB = aLocal;
      break;
    case 4:  // GR_COMBINE_FACTOR_DETAIL_FACTOR
      factCR = detailFactor;
      factCG = detailFactor;
      factCB = detailFactor;
      break;
    case 5:  // GR_COMBINE_FACTOR_LOD_FRACTION
      factCR = lodFraction;
      factCG = lodFraction;
      factCB = lodFraction;
      break;
    case 8:
      factCR = one;
      factCG = one;
      factCB = one;
      break;
    case 9:
      factCR = one - cLocalR;
      factCG = one - cLocalG;
      factCB = one - cLocalB;
      break;
    case 10:
      factCR = one - aOther;
      factCG = one - aOther;
      factCB = one - aOther;
      break;
    case 11:
      factCR = one - aLocal;
      factCG = one - aLocal;
      factCB = one - aLocal;
      break;
    case 12:  // GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR
      factCR = one - detailFactor;
      factCG = one - detailFactor;
      factCB = one - detailFactor;
      break;
    case 13:  // GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION
      factCR = one - lodFraction;
      factCG = one - lodFraction;
      factCB = one - lodFraction;
      break;
  }

  Simd8f finalCR = Simd8f(0.0f);
  Simd8f finalCG = Simd8f(0.0f);
  Simd8f finalCB = Simd8f(0.0f);
  switch (rgbFunc) {
    case 0:
      break;
    case 1:
      finalCR = cLocalR;
      finalCG = cLocalG;
      finalCB = cLocalB;
      break;
    case 3:
      finalCR = cOtherR * factCR;
      finalCG = cOtherG * factCG;
      finalCB = cOtherB * factCB;
      break;
    case 4:
      finalCR = Fma(cOtherR, factCR, cLocalR);
      finalCG = Fma(cOtherG, factCG, cLocalG);
      finalCB = Fma(cOtherB, factCB, cLocalB);
      break;
    case 5:
      finalCR = Fma(cOtherR, factCR, aLocal);
      finalCG = Fma(cOtherG, factCG, aLocal);
      finalCB = Fma(cOtherB, factCB, aLocal);
      break;
    case 6:
      finalCR = (cOtherR - cLocalR) * factCR;
      finalCG = (cOtherG - cLocalG) * factCG;
      finalCB = (cOtherB - cLocalB) * factCB;
      break;
    case 7:
      finalCR = Fma(cOtherR - cLocalR, factCR, cLocalR);
      finalCG = Fma(cOtherG - cLocalG, factCG, cLocalG);
      finalCB = Fma(cOtherB - cLocalB, factCB, cLocalB);
      break;
    case 8:
      finalCR = Fma(cOtherR - cLocalR, factCR, aLocal);
      finalCG = Fma(cOtherG - cLocalG, factCG, aLocal);
      finalCB = Fma(cOtherB - cLocalB, factCB, aLocal);
      break;
    case 9:
      finalCR = cLocalR * (one - factCR);
      finalCG = cLocalG * (one - factCG);
      finalCB = cLocalB * (one - factCB);
      break;
    case 16:
      finalCR = aLocal - cLocalR * factCR;
      finalCG = aLocal - cLocalG * factCG;
      finalCB = aLocal - cLocalB * factCB;
      break;
  }
  if (rgbInvert) {
    finalCR = one - finalCR;
    finalCG = one - finalCG;
    finalCB = one - finalCB;
  }

  return {finalCR, finalCG, finalCB, finalA};
}

// VSA-100 / OpenGL / Vulkan stencil compare: (ref & readMask) <op> (buf & readMask)
static inline bool EvaluateStencilCompare(uint32_t op, uint8_t refVal,
                                          uint8_t readMask, uint8_t bufVal) {
  const uint8_t lhs = static_cast<uint8_t>(refVal & readMask);
  const uint8_t rhs = static_cast<uint8_t>(bufVal & readMask);
  switch (op) {
    case 0: return false;         // GR_CMP_NEVER
    case 1: return lhs < rhs;     // GR_CMP_LESS
    case 2: return lhs == rhs;    // GR_CMP_EQUAL
    case 3: return lhs <= rhs;    // GR_CMP_LEQUAL
    case 4: return lhs > rhs;     // GR_CMP_GREATER
    case 5: return lhs != rhs;    // GR_CMP_NOTEQUAL
    case 6: return lhs >= rhs;    // GR_CMP_GEQUAL
    case 7: return true;          // GR_CMP_ALWAYS
    default: return true;
  }
}

// VSA-100 GR_STENCILOP_* update respecting 8-bit write mask
static inline uint8_t ApplyStencilOp(uint32_t op, uint8_t bufVal,
                                     uint8_t refVal, uint8_t writeMask) {
  uint8_t res = bufVal;
  switch (op) {
    case 0: res = bufVal; break;                                   // KEEP
    case 1: res = 0u; break;                                       // ZERO
    case 2: res = refVal; break;                                   // REPLACE
    case 3: res = (bufVal < 255u) ? static_cast<uint8_t>(bufVal + 1u) : 255u; break;  // INCR_CLAMP
    case 4: res = (bufVal > 0u) ? static_cast<uint8_t>(bufVal - 1u) : 0u; break;      // DECR_CLAMP
    case 5: res = static_cast<uint8_t>(~bufVal); break;            // INVERT
    case 6: res = static_cast<uint8_t>(bufVal + 1u); break;        // INCR_WRAP
    case 7: res = static_cast<uint8_t>(bufVal - 1u); break;        // DECR_WRAP
    default: break;
  }
  return static_cast<uint8_t>((bufVal & ~writeMask) | (res & writeMask));
}

// Check if a packed VSA-100 COMBINE register references DETAIL_FACTOR (0x07) or
// LOD_FRAC (0x0C) so software rasterizers compute mip/detail derivatives.
static inline bool ExtCombineUsesLod(uint32_t packed) {
  if ((packed & 0x80000000u) == 0u) return false;
  const uint32_t a = packed & 0x1Fu;
  const uint32_t b = (packed >> 8u) & 0x1Fu;
  const uint32_t c = (packed >> 16u) & 0x1Fu;
  const uint32_t d = (packed >> 22u) & 0x1Fu;
  auto isLod = [](uint32_t s) { return s == 0x07u || s == 0x0Cu; };
  return isLod(a) || isLod(b) || isLod(c) || isLod(d);
}

static inline float ApplyBlendOpScalar(uint32_t op, float srcTerm,
                                       float dstTerm) {
  float v;
  if (op == 1u) {
    v = srcTerm - dstTerm;
  } else if (op == 2u) {
    v = dstTerm - srcTerm;
  } else {
    v = srcTerm + dstTerm;
  }
  return std::clamp(v, 0.0f, 1.0f);
}

static inline Simd8f ApplyBlendOpSIMD(uint32_t op, const Simd8f& srcTerm,
                                      const Simd8f& dstTerm) {
  const Simd8f zero(0.0f);
  const Simd8f one(1.0f);
  Simd8f v;
  if (op == 1u) {
    v = srcTerm - dstTerm;
  } else if (op == 2u) {
    v = dstTerm - srcTerm;
  } else {
    v = srcTerm + dstTerm;
  }
  return Max(zero, Min(one, v));
}

static inline float ApplyCombineModeScalar(float x, uint32_t mode) {
  switch (mode) {
    case 0u: return 0.0f;
    case 1u: return x;
    case 2u: return 1.0f - x;
    case 3u: return -x;
    case 4u: return x - 0.5f;
    case 5u: return 1.0f;
    case 6u: return -0.5f;
    default: return x;
  }
}

static inline Simd8f ApplyCombineModeSIMD(const Simd8f& x, uint32_t mode) {
  switch (mode) {
    case 0u: return Simd8f(0.0f);
    case 1u: return x;
    case 2u: return Simd8f(1.0f) - x;
    case 3u: return Simd8f(0.0f) - x;
    case 4u: return x - Simd8f(0.5f);
    case 5u: return Simd8f(1.0f);
    case 6u: return Simd8f(-0.5f);
    default: return x;
  }
}

static inline void FetchTmuRgbScalar(uint32_t src, const TmuColor& localVal,
                                     const TmuColor& otherVal,
                                     const TmuColor& tmuConst,
                                     const TmuColor& iterCol,
                                     float detailFactor, float lodFrac,
                                     float& outR, float& outG, float& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = 0.0f; break;
    case 0x01u:
    case 0x0Au: outR = outG = outB = localVal.a; break;
    case 0x07u: outR = outG = outB = detailFactor; break;
    case 0x08u: outR = outG = outB = iterCol.a; break;
    case 0x09u: outR = iterCol.r; outG = iterCol.g; outB = iterCol.b; break;
    case 0x0Bu:
    case 0x0Fu: outR = localVal.r; outG = localVal.g; outB = localVal.b; break;
    case 0x0Cu: outR = outG = outB = lodFrac; break;
    case 0x0Du: outR = outG = outB = otherVal.a; break;
    case 0x0Eu: outR = otherVal.r; outG = otherVal.g; outB = otherVal.b; break;
    case 0x10u: outR = outG = outB = tmuConst.a; break;
    case 0x11u: outR = tmuConst.r; outG = tmuConst.g; outB = tmuConst.b; break;
    default: outR = outG = outB = 0.0f; break;
  }
}

static inline float FetchTmuAlphaScalar(uint32_t src, const TmuColor& localVal,
                                        const TmuColor& otherVal,
                                        const TmuColor& tmuConst,
                                        const TmuColor& iterCol,
                                        float detailFactor, float lodFrac) {
  switch (src) {
    case 0x00u: return 0.0f;
    case 0x01u:
    case 0x0Au: return localVal.a;
    case 0x07u: return detailFactor;
    case 0x08u: return iterCol.a;
    case 0x0Cu: return lodFrac;
    case 0x0Du: return otherVal.a;
    case 0x10u: return tmuConst.a;
    default: return 0.0f;
  }
}

#if defined(__GNUC__) || defined(__clang__)
#define GLIDE_NOINLINE __attribute__((noinline))
#else
#define GLIDE_NOINLINE
#endif

GLIDE_NOINLINE static inline void EvaluateExtTmuRgbScalar(
    uint32_t packed, const TmuColor& localVal, const TmuColor& otherVal,
    const TmuColor& tmuConst, const TmuColor& iterCol, float detailFactor,
    float lodFrac, float& outR, float& outG, float& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const float scale = static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u));
  const bool inv = ((packed >> 30u) & 1u) != 0u;

  float aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchTmuRgbScalar(aSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, aR, aG, aB);
  FetchTmuRgbScalar(bSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, bR, bG, bB);
  if (cSrc == 0x04u) { cR = bR; cG = bG; cB = bB; }
  else FetchTmuRgbScalar(cSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, cR, cG, cB);
  if (dSrc == 0x04u) { dR = bR; dG = bG; dB = bB; }
  else FetchTmuRgbScalar(dSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, dR, dG, dB);

  auto evalCh = [&](float a, float b, float c, float d) {
    const float av = ApplyCombineModeScalar(a, aMode);
    const float bv = ApplyCombineModeScalar(b, bMode);
    const float cv = cInv ? (1.0f - c) : c;
    const float dv = dInv ? (1.0f - d) : d;
    float res = ((av + bv) * cv + dv) * scale;
    if (inv) res = 1.0f - res;
    return std::clamp(res, 0.0f, 1.0f);
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

GLIDE_NOINLINE static inline float EvaluateExtTmuAlphaScalar(
    uint32_t packed, const TmuColor& localVal, const TmuColor& otherVal,
    const TmuColor& tmuConst, const TmuColor& iterCol, float detailFactor,
    float lodFrac) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const float scale = static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u));
  const bool inv = ((packed >> 30u) & 1u) != 0u;

  const float aRaw = FetchTmuAlphaScalar(aSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const float bRaw = FetchTmuAlphaScalar(bSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const float cRaw = (cSrc == 0x04u) ? bRaw : FetchTmuAlphaScalar(cSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const float dRaw = (dSrc == 0x04u) ? bRaw : FetchTmuAlphaScalar(dSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const float av = ApplyCombineModeScalar(aRaw, aMode);
  const float bv = ApplyCombineModeScalar(bRaw, bMode);
  const float cv = cInv ? (1.0f - cRaw) : cRaw;
  const float dv = dInv ? (1.0f - dRaw) : dRaw;
  float res = ((av + bv) * cv + dv) * scale;
  if (inv) res = 1.0f - res;
  return std::clamp(res, 0.0f, 1.0f);
}

static inline float FetchFbiAlphaBaseScalar(uint32_t src, float iterA,
                                            float constA, float texA) {
  switch (src) {
    case 0x00u: return 0.0f;
    case 0x01u:
    case 0x0Fu: return texA;
    case 0x05u:
    case 0x06u: return constA;
    case 0x08u:
    case 0x09u: return iterA;
    default: return 0.0f;
  }
}

GLIDE_NOINLINE static inline float EvaluateExtFbiAlphaScalar(uint32_t packed, float iterA,
                                              float constA, float texA,
                                              float& outALocal,
                                              float& outAOther) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const float scale = static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u));
  const bool inv = ((packed >> 30u) & 1u) != 0u;

  const float aRaw = FetchFbiAlphaBaseScalar(aSrc, iterA, constA, texA);
  const float bRaw = FetchFbiAlphaBaseScalar(bSrc, iterA, constA, texA);
  outAOther = aRaw;
  outALocal = bRaw;
  const float cRaw = (cSrc == 0x04u || cSrc == 0x02u)
                         ? bRaw
                         : ((cSrc == 0x03u) ? aRaw : FetchFbiAlphaBaseScalar(cSrc, iterA, constA, texA));
  const float dRaw = (dSrc == 0x04u || dSrc == 0x02u)
                         ? bRaw
                         : ((dSrc == 0x03u) ? aRaw : FetchFbiAlphaBaseScalar(dSrc, iterA, constA, texA));
  const float av = ApplyCombineModeScalar(aRaw, aMode);
  const float bv = ApplyCombineModeScalar(bRaw, bMode);
  const float cv = cInv ? (1.0f - cRaw) : cRaw;
  const float dv = dInv ? (1.0f - dRaw) : dRaw;
  float res = ((av + bv) * cv + dv) * scale;
  if (inv) res = 1.0f - res;
  return std::clamp(res, 0.0f, 1.0f);
}

static inline void FetchFbiColorScalar(uint32_t src, const TmuColor& iterCol,
                                       const TmuColor& constCol,
                                       const TmuColor& texCol, float aLocal,
                                       float aOther, float& outR, float& outG,
                                       float& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = 0.0f; break;
    case 0x01u: outR = outG = outB = texCol.a; break;
    case 0x02u: outR = outG = outB = aLocal; break;
    case 0x03u: outR = outG = outB = aOther; break;
    case 0x05u: outR = outG = outB = constCol.a; break;
    case 0x06u: outR = constCol.r; outG = constCol.g; outB = constCol.b; break;
    case 0x08u: outR = outG = outB = iterCol.a; break;
    case 0x09u: outR = iterCol.r; outG = iterCol.g; outB = iterCol.b; break;
    case 0x0Fu: outR = texCol.r; outG = texCol.g; outB = texCol.b; break;
    default: outR = outG = outB = 0.0f; break;
  }
}

GLIDE_NOINLINE static inline void EvaluateExtFbiColorScalar(
    uint32_t packed, const TmuColor& iterCol, const TmuColor& constCol,
    const TmuColor& texCol, float aLocal, float aOther, float& outR,
    float& outG, float& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const float scale = static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u));
  const bool inv = ((packed >> 30u) & 1u) != 0u;

  float aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchFbiColorScalar(aSrc, iterCol, constCol, texCol, aLocal, aOther, aR, aG, aB);
  FetchFbiColorScalar(bSrc, iterCol, constCol, texCol, aLocal, aOther, bR, bG, bB);
  if (cSrc == 0x04u) { cR = bR; cG = bG; cB = bB; }
  else FetchFbiColorScalar(cSrc, iterCol, constCol, texCol, aLocal, aOther, cR, cG, cB);
  if (dSrc == 0x04u) { dR = bR; dG = bG; dB = bB; }
  else FetchFbiColorScalar(dSrc, iterCol, constCol, texCol, aLocal, aOther, dR, dG, dB);

  auto evalCh = [&](float a, float b, float c, float d) {
    const float av = ApplyCombineModeScalar(a, aMode);
    const float bv = ApplyCombineModeScalar(b, bMode);
    const float cv = cInv ? (1.0f - c) : c;
    const float dv = dInv ? (1.0f - d) : d;
    float res = ((av + bv) * cv + dv) * scale;
    if (inv) res = 1.0f - res;
    return std::clamp(res, 0.0f, 1.0f);
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

// SIMD VSA-100 COMBINE evaluators (8-wide)
static inline void FetchTmuRgbSIMD(uint32_t src, const TmuColorSIMD& localVal,
                                   const TmuColorSIMD& otherVal,
                                   const TmuColorSIMD& tmuConst,
                                   const TmuColorSIMD& iterCol,
                                   const Simd8f& detailFactor,
                                   const Simd8f& lodFrac, Simd8f& outR,
                                   Simd8f& outG, Simd8f& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = Simd8f(0.0f); break;
    case 0x01u:
    case 0x0Au: outR = outG = outB = localVal.a; break;
    case 0x07u: outR = outG = outB = detailFactor; break;
    case 0x08u: outR = outG = outB = iterCol.a; break;
    case 0x09u: outR = iterCol.r; outG = iterCol.g; outB = iterCol.b; break;
    case 0x0Bu:
    case 0x0Fu: outR = localVal.r; outG = localVal.g; outB = localVal.b; break;
    case 0x0Cu: outR = outG = outB = lodFrac; break;
    case 0x0Du: outR = outG = outB = otherVal.a; break;
    case 0x0Eu: outR = otherVal.r; outG = otherVal.g; outB = otherVal.b; break;
    case 0x10u: outR = outG = outB = tmuConst.a; break;
    case 0x11u: outR = tmuConst.r; outG = tmuConst.g; outB = tmuConst.b; break;
    default: outR = outG = outB = Simd8f(0.0f); break;
  }
}

static inline Simd8f FetchTmuAlphaSIMD(uint32_t src,
                                       const TmuColorSIMD& localVal,
                                       const TmuColorSIMD& otherVal,
                                       const TmuColorSIMD& tmuConst,
                                       const TmuColorSIMD& iterCol,
                                       const Simd8f& detailFactor,
                                       const Simd8f& lodFrac) {
  switch (src) {
    case 0x00u: return Simd8f(0.0f);
    case 0x01u:
    case 0x0Au: return localVal.a;
    case 0x07u: return detailFactor;
    case 0x08u: return iterCol.a;
    case 0x0Cu: return lodFrac;
    case 0x0Du: return otherVal.a;
    case 0x10u: return tmuConst.a;
    default: return Simd8f(0.0f);
  }
}

GLIDE_NOINLINE static inline void EvaluateExtTmuRgbSIMD(
    uint32_t packed, const TmuColorSIMD& localVal, const TmuColorSIMD& otherVal,
    const TmuColorSIMD& tmuConst, const TmuColorSIMD& iterCol,
    const Simd8f& detailFactor, const Simd8f& lodFrac, Simd8f& outR,
    Simd8f& outG, Simd8f& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const Simd8f scale(static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const Simd8f zero(0.0f);
  const Simd8f one(1.0f);

  Simd8f aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchTmuRgbSIMD(aSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, aR, aG, aB);
  FetchTmuRgbSIMD(bSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, bR, bG, bB);
  if (cSrc == 0x04u) { cR = bR; cG = bG; cB = bB; }
  else FetchTmuRgbSIMD(cSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, cR, cG, cB);
  if (dSrc == 0x04u) { dR = bR; dG = bG; dB = bB; }
  else FetchTmuRgbSIMD(dSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac, dR, dG, dB);

  auto evalCh = [&](const Simd8f& a, const Simd8f& b, const Simd8f& c, const Simd8f& d) {
    const Simd8f av = ApplyCombineModeSIMD(a, aMode);
    const Simd8f bv = ApplyCombineModeSIMD(b, bMode);
    const Simd8f cv = cInv ? (one - c) : c;
    const Simd8f dv = dInv ? (one - d) : d;
    Simd8f res = Fma(av + bv, cv, dv) * scale;
    if (inv) res = one - res;
    return Max(zero, Min(one, res));
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

GLIDE_NOINLINE static inline Simd8f EvaluateExtTmuAlphaSIMD(
    uint32_t packed, const TmuColorSIMD& localVal, const TmuColorSIMD& otherVal,
    const TmuColorSIMD& tmuConst, const TmuColorSIMD& iterCol,
    const Simd8f& detailFactor, const Simd8f& lodFrac) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const Simd8f scale(static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const Simd8f zero(0.0f);
  const Simd8f one(1.0f);

  const Simd8f aRaw = FetchTmuAlphaSIMD(aSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const Simd8f bRaw = FetchTmuAlphaSIMD(bSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const Simd8f cRaw = (cSrc == 0x04u) ? bRaw : FetchTmuAlphaSIMD(cSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const Simd8f dRaw = (dSrc == 0x04u) ? bRaw : FetchTmuAlphaSIMD(dSrc, localVal, otherVal, tmuConst, iterCol, detailFactor, lodFrac);
  const Simd8f av = ApplyCombineModeSIMD(aRaw, aMode);
  const Simd8f bv = ApplyCombineModeSIMD(bRaw, bMode);
  const Simd8f cv = cInv ? (one - cRaw) : cRaw;
  const Simd8f dv = dInv ? (one - dRaw) : dRaw;
  Simd8f res = Fma(av + bv, cv, dv) * scale;
  if (inv) res = one - res;
  return Max(zero, Min(one, res));
}

static inline Simd8f FetchFbiAlphaBaseSIMD(uint32_t src, const Simd8f& iterA,
                                           const Simd8f& constA,
                                           const Simd8f& texA) {
  switch (src) {
    case 0x00u: return Simd8f(0.0f);
    case 0x01u:
    case 0x0Fu: return texA;
    case 0x05u:
    case 0x06u: return constA;
    case 0x08u:
    case 0x09u: return iterA;
    default: return Simd8f(0.0f);
  }
}

GLIDE_NOINLINE static inline Simd8f EvaluateExtFbiAlphaSIMD(uint32_t packed,
                                             const Simd8f& iterA,
                                             const Simd8f& constA,
                                             const Simd8f& texA,
                                             Simd8f& outALocal,
                                             Simd8f& outAOther) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const Simd8f scale(static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const Simd8f zero(0.0f);
  const Simd8f one(1.0f);

  const Simd8f aRaw = FetchFbiAlphaBaseSIMD(aSrc, iterA, constA, texA);
  const Simd8f bRaw = FetchFbiAlphaBaseSIMD(bSrc, iterA, constA, texA);
  outAOther = aRaw;
  outALocal = bRaw;
  const Simd8f cRaw = (cSrc == 0x04u || cSrc == 0x02u)
                          ? bRaw
                          : ((cSrc == 0x03u) ? aRaw : FetchFbiAlphaBaseSIMD(cSrc, iterA, constA, texA));
  const Simd8f dRaw = (dSrc == 0x04u || dSrc == 0x02u)
                          ? bRaw
                          : ((dSrc == 0x03u) ? aRaw : FetchFbiAlphaBaseSIMD(dSrc, iterA, constA, texA));
  const Simd8f av = ApplyCombineModeSIMD(aRaw, aMode);
  const Simd8f bv = ApplyCombineModeSIMD(bRaw, bMode);
  const Simd8f cv = cInv ? (one - cRaw) : cRaw;
  const Simd8f dv = dInv ? (one - dRaw) : dRaw;
  Simd8f res = Fma(av + bv, cv, dv) * scale;
  if (inv) res = one - res;
  return Max(zero, Min(one, res));
}

static inline void FetchFbiColorSIMD(uint32_t src, const TmuColorSIMD& iterCol,
                                     const TmuColorSIMD& constCol,
                                     const TmuColorSIMD& texCol,
                                     const Simd8f& aLocal,
                                     const Simd8f& aOther, Simd8f& outR,
                                     Simd8f& outG, Simd8f& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = Simd8f(0.0f); break;
    case 0x01u: outR = outG = outB = texCol.a; break;
    case 0x02u: outR = outG = outB = aLocal; break;
    case 0x03u: outR = outG = outB = aOther; break;
    case 0x05u: outR = outG = outB = constCol.a; break;
    case 0x06u: outR = constCol.r; outG = constCol.g; outB = constCol.b; break;
    case 0x08u: outR = outG = outB = iterCol.a; break;
    case 0x09u: outR = iterCol.r; outG = iterCol.g; outB = iterCol.b; break;
    case 0x0Fu: outR = texCol.r; outG = texCol.g; outB = texCol.b; break;
    default: outR = outG = outB = Simd8f(0.0f); break;
  }
}

GLIDE_NOINLINE static inline void EvaluateExtFbiColorSIMD(
    uint32_t packed, const TmuColorSIMD& iterCol, const TmuColorSIMD& constCol,
    const TmuColorSIMD& texCol, const Simd8f& aLocal, const Simd8f& aOther,
    Simd8f& outR, Simd8f& outG, Simd8f& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const Simd8f scale(static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const Simd8f zero(0.0f);
  const Simd8f one(1.0f);

  Simd8f aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchFbiColorSIMD(aSrc, iterCol, constCol, texCol, aLocal, aOther, aR, aG, aB);
  FetchFbiColorSIMD(bSrc, iterCol, constCol, texCol, aLocal, aOther, bR, bG, bB);
  if (cSrc == 0x04u) { cR = bR; cG = bG; cB = bB; }
  else FetchFbiColorSIMD(cSrc, iterCol, constCol, texCol, aLocal, aOther, cR, cG, cB);
  if (dSrc == 0x04u) { dR = bR; dG = bG; dB = bB; }
  else FetchFbiColorSIMD(dSrc, iterCol, constCol, texCol, aLocal, aOther, dR, dG, dB);

  auto evalCh = [&](const Simd8f& a, const Simd8f& b, const Simd8f& c, const Simd8f& d) {
    const Simd8f av = ApplyCombineModeSIMD(a, aMode);
    const Simd8f bv = ApplyCombineModeSIMD(b, bMode);
    const Simd8f cv = cInv ? (one - c) : c;
    const Simd8f dv = dInv ? (one - d) : d;
    Simd8f res = Fma(av + bv, cv, dv) * scale;
    if (inv) res = one - res;
    return Max(zero, Min(one, res));
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

}  // namespace GlideWrapper
