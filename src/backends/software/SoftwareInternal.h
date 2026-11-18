#pragma once

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
                                 const TmuColor& /*iteratedCol*/) {
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
    case 4:
      factA = localVal.a;
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
    case 12:
      factA = 1.0f - localVal.a;
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
      finalA = (aLocal - aOther) * factA + aOther;
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
    case 4:
      factCR = localVal.a;
      factCG = localVal.a;
      factCB = localVal.a;
      break;
    case 5:
      factCR = localVal.r;
      factCG = localVal.g;
      factCB = localVal.b;
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
    case 12:
      factCR = 1.0f - localVal.a;
      factCG = 1.0f - localVal.a;
      factCB = 1.0f - localVal.a;
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
      finalCR = (cLocalR - cOtherR) * factCR + cOtherR;
      finalCG = (cLocalG - cOtherG) * factCG + cOtherG;
      finalCB = (cLocalB - cOtherB) * factCB + cOtherB;
      break;
    case 16:
      finalCR = (aLocal - cOtherR) * factCR + cOtherR;
      finalCG = (aLocal - cOtherG) * factCG + cOtherG;
      finalCB = (aLocal - cOtherB) * factCB + cOtherB;
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
                                         const TmuColorSIMD& /*iteratedCol*/) {
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
    case 4:
      factA = localVal.a;
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
    case 12:
      factA = one - localVal.a;
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
      finalA = Fma(aLocal - aOther, factA, aOther);
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
    case 4:
      factCR = localVal.a;
      factCG = localVal.a;
      factCB = localVal.a;
      break;
    case 5:
      factCR = localVal.r;
      factCG = localVal.g;
      factCB = localVal.b;
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
    case 12:
      factCR = one - localVal.a;
      factCG = one - localVal.a;
      factCB = one - localVal.a;
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
      finalCR = Fma(cLocalR - cOtherR, factCR, cOtherR);
      finalCG = Fma(cLocalG - cOtherG, factCG, cOtherG);
      finalCB = Fma(cLocalB - cOtherB, factCB, cOtherB);
      break;
    case 16:
      finalCR = Fma(aLocal - cOtherR, factCR, cOtherR);
      finalCG = Fma(aLocal - cOtherG, factCG, cOtherG);
      finalCB = Fma(aLocal - cOtherB, factCB, cOtherB);
      break;
  }
  if (rgbInvert) {
    finalCR = one - finalCR;
    finalCG = one - finalCG;
    finalCB = one - finalCB;
  }

  return {finalCR, finalCG, finalCB, finalA};
}

}  // namespace GlideWrapper
