#pragma once

// ISA-generic SIMD combiner & blend helpers shared across AVX2 (8-wide) and
// AVX-512 (16-wide) rasterizer translation units.
// IMPORTANT: Include ONLY from SIMD-enabled translation units (never from
// baseline scalar TUs) so ISA-specific vector types stay strictly isolated.

#include <algorithm>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define GLIDE_SIMD_NOINLINE __attribute__((noinline))
#else
#define GLIDE_SIMD_NOINLINE
#endif

namespace GlideWrapper {

template <typename SimdF>
struct TmuColorVec {
  SimdF r, g, b, a;
};

template <typename SimdF>
inline TmuColorVec<SimdF> EvaluateTmuStageVec(
    uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc,
    uint32_t alphaFactor, bool rgbInvert, bool alphaInvert,
    const TmuColorVec<SimdF>& localVal, const TmuColorVec<SimdF>& otherVal,
    const TmuColorVec<SimdF>& /*iteratedCol*/,
    const SimdF& detailFactor = SimdF(0.0f),
    const SimdF& lodFraction = SimdF(0.0f)) {
  const SimdF one(1.0f);
  SimdF aLocal = localVal.a;
  SimdF aOther = otherVal.a;
  SimdF factA = SimdF(0.0f);

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

  SimdF finalA = SimdF(0.0f);
  switch (alphaFunc) {
    case 0:
      break;
    case 1:
    case 2:
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

  SimdF cLocalR = localVal.r;
  SimdF cLocalG = localVal.g;
  SimdF cLocalB = localVal.b;
  SimdF cOtherR = otherVal.r;
  SimdF cOtherG = otherVal.g;
  SimdF cOtherB = otherVal.b;

  SimdF factCR = SimdF(0.0f);
  SimdF factCG = SimdF(0.0f);
  SimdF factCB = SimdF(0.0f);
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

  SimdF finalCR = SimdF(0.0f);
  SimdF finalCG = SimdF(0.0f);
  SimdF finalCB = SimdF(0.0f);
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

template <typename SimdF>
inline SimdF ApplyBlendOpVec(uint32_t op, const SimdF& srcTerm,
                             const SimdF& dstTerm) {
  const SimdF zero(0.0f);
  const SimdF one(1.0f);
  SimdF v;
  if (op == 1u) {
    v = srcTerm - dstTerm;
  } else if (op == 2u) {
    v = dstTerm - srcTerm;
  } else {
    v = srcTerm + dstTerm;
  }
  return Max(zero, Min(one, v));
}

template <typename SimdF>
inline SimdF ApplyCombineModeVec(const SimdF& x, uint32_t mode) {
  switch (mode) {
    case 0u: return SimdF(0.0f);
    case 1u: return x;
    case 2u: return SimdF(1.0f) - x;
    case 3u: return SimdF(0.0f) - x;
    case 4u: return x - SimdF(0.5f);
    case 5u: return SimdF(1.0f);
    case 6u: return SimdF(-0.5f);
    default: return x;
  }
}

template <typename SimdF>
inline void FetchTmuRgbVec(uint32_t src, const TmuColorVec<SimdF>& localVal,
                           const TmuColorVec<SimdF>& otherVal,
                           const TmuColorVec<SimdF>& tmuConst,
                           const TmuColorVec<SimdF>& iterCol,
                           const SimdF& detailFactor, const SimdF& lodFrac,
                           SimdF& outR, SimdF& outG, SimdF& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = SimdF(0.0f); break;
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
    default: outR = outG = outB = SimdF(0.0f); break;
  }
}

template <typename SimdF>
inline SimdF FetchTmuAlphaVec(uint32_t src, const TmuColorVec<SimdF>& localVal,
                              const TmuColorVec<SimdF>& otherVal,
                              const TmuColorVec<SimdF>& tmuConst,
                              const TmuColorVec<SimdF>& iterCol,
                              const SimdF& detailFactor, const SimdF& lodFrac) {
  switch (src) {
    case 0x00u: return SimdF(0.0f);
    case 0x01u:
    case 0x0Au: return localVal.a;
    case 0x07u: return detailFactor;
    case 0x08u: return iterCol.a;
    case 0x0Cu: return lodFrac;
    case 0x0Du: return otherVal.a;
    case 0x10u: return tmuConst.a;
    default: return SimdF(0.0f);
  }
}

template <typename SimdF>
GLIDE_SIMD_NOINLINE static inline void EvaluateExtTmuRgbVec(uint32_t packed,
                                 const TmuColorVec<SimdF>& localVal,
                                 const TmuColorVec<SimdF>& otherVal,
                                 const TmuColorVec<SimdF>& tmuConst,
                                 const TmuColorVec<SimdF>& iterCol,
                                 const SimdF& detailFactor,
                                 const SimdF& lodFrac, SimdF& outR, SimdF& outG,
                                 SimdF& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const SimdF scale(
      static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const SimdF zero(0.0f);
  const SimdF one(1.0f);

  SimdF aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchTmuRgbVec(aSrc, localVal, otherVal, tmuConst, iterCol, detailFactor,
                 lodFrac, aR, aG, aB);
  FetchTmuRgbVec(bSrc, localVal, otherVal, tmuConst, iterCol, detailFactor,
                 lodFrac, bR, bG, bB);
  if (cSrc == 0x04u) {
    cR = bR; cG = bG; cB = bB;
  } else {
    FetchTmuRgbVec(cSrc, localVal, otherVal, tmuConst, iterCol, detailFactor,
                   lodFrac, cR, cG, cB);
  }
  if (dSrc == 0x04u) {
    dR = bR; dG = bG; dB = bB;
  } else {
    FetchTmuRgbVec(dSrc, localVal, otherVal, tmuConst, iterCol, detailFactor,
                   lodFrac, dR, dG, dB);
  }

  auto evalCh = [&](const SimdF& a, const SimdF& b, const SimdF& c,
                    const SimdF& d) {
    const SimdF av = ApplyCombineModeVec(a, aMode);
    const SimdF bv = ApplyCombineModeVec(b, bMode);
    const SimdF cv = cInv ? (one - c) : c;
    const SimdF dv = dInv ? (one - d) : d;
    SimdF res = Fma(av + bv, cv, dv) * scale;
    if (inv) res = one - res;
    return Max(zero, Min(one, res));
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

template <typename SimdF>
GLIDE_SIMD_NOINLINE static inline SimdF EvaluateExtTmuAlphaVec(uint32_t packed,
                                    const TmuColorVec<SimdF>& localVal,
                                    const TmuColorVec<SimdF>& otherVal,
                                    const TmuColorVec<SimdF>& tmuConst,
                                    const TmuColorVec<SimdF>& iterCol,
                                    const SimdF& detailFactor,
                                    const SimdF& lodFrac) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const SimdF scale(
      static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const SimdF zero(0.0f);
  const SimdF one(1.0f);

  const SimdF aRaw = FetchTmuAlphaVec(aSrc, localVal, otherVal, tmuConst,
                                      iterCol, detailFactor, lodFrac);
  const SimdF bRaw = FetchTmuAlphaVec(bSrc, localVal, otherVal, tmuConst,
                                      iterCol, detailFactor, lodFrac);
  const SimdF cRaw = (cSrc == 0x04u)
                         ? bRaw
                         : FetchTmuAlphaVec(cSrc, localVal, otherVal, tmuConst,
                                            iterCol, detailFactor, lodFrac);
  const SimdF dRaw = (dSrc == 0x04u)
                         ? bRaw
                         : FetchTmuAlphaVec(dSrc, localVal, otherVal, tmuConst,
                                            iterCol, detailFactor, lodFrac);
  const SimdF av = ApplyCombineModeVec(aRaw, aMode);
  const SimdF bv = ApplyCombineModeVec(bRaw, bMode);
  const SimdF cv = cInv ? (one - cRaw) : cRaw;
  const SimdF dv = dInv ? (one - dRaw) : dRaw;
  SimdF res = Fma(av + bv, cv, dv) * scale;
  if (inv) res = one - res;
  return Max(zero, Min(one, res));
}

template <typename SimdF>
inline SimdF FetchFbiAlphaBaseVec(uint32_t src, const SimdF& iterA,
                                  const SimdF& constA, const SimdF& texA) {
  switch (src) {
    case 0x00u: return SimdF(0.0f);
    case 0x01u:
    case 0x0Fu: return texA;
    case 0x05u:
    case 0x06u: return constA;
    case 0x08u:
    case 0x09u: return iterA;
    default: return SimdF(0.0f);
  }
}

template <typename SimdF>
GLIDE_SIMD_NOINLINE static inline SimdF EvaluateExtFbiAlphaVec(uint32_t packed, const SimdF& iterA,
                                    const SimdF& constA, const SimdF& texA,
                                    SimdF& outALocal, SimdF& outAOther) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const SimdF scale(
      static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const SimdF zero(0.0f);
  const SimdF one(1.0f);

  const SimdF aRaw = FetchFbiAlphaBaseVec(aSrc, iterA, constA, texA);
  const SimdF bRaw = FetchFbiAlphaBaseVec(bSrc, iterA, constA, texA);
  outAOther = aRaw;
  outALocal = bRaw;
  const SimdF cRaw =
      (cSrc == 0x04u || cSrc == 0x02u)
          ? bRaw
          : ((cSrc == 0x03u) ? aRaw
                             : FetchFbiAlphaBaseVec(cSrc, iterA, constA, texA));
  const SimdF dRaw =
      (dSrc == 0x04u || dSrc == 0x02u)
          ? bRaw
          : ((dSrc == 0x03u) ? aRaw
                             : FetchFbiAlphaBaseVec(dSrc, iterA, constA, texA));
  const SimdF av = ApplyCombineModeVec(aRaw, aMode);
  const SimdF bv = ApplyCombineModeVec(bRaw, bMode);
  const SimdF cv = cInv ? (one - cRaw) : cRaw;
  const SimdF dv = dInv ? (one - dRaw) : dRaw;
  SimdF res = Fma(av + bv, cv, dv) * scale;
  if (inv) res = one - res;
  return Max(zero, Min(one, res));
}

template <typename SimdF>
inline void FetchFbiColorVec(uint32_t src, const TmuColorVec<SimdF>& iterCol,
                             const TmuColorVec<SimdF>& constCol,
                             const TmuColorVec<SimdF>& texCol,
                             const SimdF& aLocal, const SimdF& aOther,
                             SimdF& outR, SimdF& outG, SimdF& outB) {
  switch (src) {
    case 0x00u: outR = outG = outB = SimdF(0.0f); break;
    case 0x01u: outR = outG = outB = texCol.a; break;
    case 0x02u: outR = outG = outB = aLocal; break;
    case 0x03u: outR = outG = outB = aOther; break;
    case 0x05u: outR = outG = outB = constCol.a; break;
    case 0x06u: outR = constCol.r; outG = constCol.g; outB = constCol.b; break;
    case 0x08u: outR = outG = outB = iterCol.a; break;
    case 0x09u: outR = iterCol.r; outG = iterCol.g; outB = iterCol.b; break;
    case 0x0Fu: outR = texCol.r; outG = texCol.g; outB = texCol.b; break;
    default: outR = outG = outB = SimdF(0.0f); break;
  }
}

template <typename SimdF>
GLIDE_SIMD_NOINLINE static inline void EvaluateExtFbiColorVec(uint32_t packed,
                                   const TmuColorVec<SimdF>& iterCol,
                                   const TmuColorVec<SimdF>& constCol,
                                   const TmuColorVec<SimdF>& texCol,
                                   const SimdF& aLocal, const SimdF& aOther,
                                   SimdF& outR, SimdF& outG, SimdF& outB) {
  const uint32_t aSrc = packed & 0x1Fu;
  const uint32_t aMode = (packed >> 5u) & 0x7u;
  const uint32_t bSrc = (packed >> 8u) & 0x1Fu;
  const uint32_t bMode = (packed >> 13u) & 0x7u;
  const uint32_t cSrc = (packed >> 16u) & 0x1Fu;
  const bool cInv = ((packed >> 21u) & 1u) != 0u;
  const uint32_t dSrc = (packed >> 22u) & 0x1Fu;
  const bool dInv = ((packed >> 27u) & 1u) != 0u;
  const SimdF scale(
      static_cast<float>(1u << std::min((packed >> 28u) & 0x3u, 2u)));
  const bool inv = ((packed >> 30u) & 1u) != 0u;
  const SimdF zero(0.0f);
  const SimdF one(1.0f);

  SimdF aR, aG, aB, bR, bG, bB, cR, cG, cB, dR, dG, dB;
  FetchFbiColorVec(aSrc, iterCol, constCol, texCol, aLocal, aOther, aR, aG, aB);
  FetchFbiColorVec(bSrc, iterCol, constCol, texCol, aLocal, aOther, bR, bG, bB);
  if (cSrc == 0x04u) {
    cR = bR; cG = bG; cB = bB;
  } else {
    FetchFbiColorVec(cSrc, iterCol, constCol, texCol, aLocal, aOther, cR, cG,
                     cB);
  }
  if (dSrc == 0x04u) {
    dR = bR; dG = bG; dB = bB;
  } else {
    FetchFbiColorVec(dSrc, iterCol, constCol, texCol, aLocal, aOther, dR, dG,
                     dB);
  }

  auto evalCh = [&](const SimdF& a, const SimdF& b, const SimdF& c,
                    const SimdF& d) {
    const SimdF av = ApplyCombineModeVec(a, aMode);
    const SimdF bv = ApplyCombineModeVec(b, bMode);
    const SimdF cv = cInv ? (one - c) : c;
    const SimdF dv = dInv ? (one - d) : d;
    SimdF res = Fma(av + bv, cv, dv) * scale;
    if (inv) res = one - res;
    return Max(zero, Min(one, res));
  };
  outR = evalCh(aR, bR, cR, dR);
  outG = evalCh(aG, bG, cG, dG);
  outB = evalCh(aB, bB, cB, dB);
}

// ISA-generic SIMD Hardware Stencil compare (ref & readMask) <op> (buf & readMask)
template <typename SimdI, typename SimdM>
static inline SimdM EvaluateStencilCompareVec(uint32_t op,
                                              const SimdI& maskedRef,
                                              const SimdI& maskedBuf) {
  switch (op) {
    case 0: return SimdM::False();         // GR_CMP_NEVER
    case 1: return maskedRef < maskedBuf;  // GR_CMP_LESS
    case 2: return maskedRef == maskedBuf; // GR_CMP_EQUAL
    case 3: return maskedRef <= maskedBuf; // GR_CMP_LEQUAL
    case 4: return maskedRef > maskedBuf;  // GR_CMP_GREATER
    case 5: return maskedRef != maskedBuf; // GR_CMP_NOTEQUAL
    case 6: return maskedRef >= maskedBuf; // GR_CMP_GEQUAL
    case 7: return SimdM::True();          // GR_CMP_ALWAYS
    default: return SimdM::True();
  }
}

// ISA-generic SIMD Hardware Stencil op respecting 8-bit write mask
template <typename SimdI>
static inline SimdI ApplyStencilOpVec(uint32_t op, const SimdI& bufVal,
                                      const SimdI& refVec,
                                      const SimdI& wmaskVec,
                                      const SimdI& invWmaskVec) {
  SimdI res = bufVal;
  switch (op) {
    case 0: return bufVal;  // KEEP
    case 1: res = SimdI(0); break;  // ZERO
    case 2: res = refVec; break;  // REPLACE
    case 3: res = Min(SimdI(255), bufVal + SimdI(1)); break;  // INCR_CLAMP
    case 4: res = Max(SimdI(0), bufVal - SimdI(1)); break;    // DECR_CLAMP
    case 5: res = (~bufVal) & SimdI(0xFF); break;             // INVERT
    case 6: res = (bufVal + SimdI(1)) & SimdI(0xFF); break;   // INCR_WRAP
    case 7: res = (bufVal - SimdI(1)) & SimdI(0xFF); break;   // DECR_WRAP
    default: return bufVal;
  }
  return (bufVal & invWmaskVec) | (res & wmaskVec);
}

// ISA-generic SIMD Table Fog evaluation (bit-exact index vs binary search
// across all IEEE-754 float32 wVal inputs via reciprocal-exponent estimate +
// exact 1-step boundary refinement against s_tableIndexToW[64]).
template <typename SimdF, typename SimdI, typename SimdM>
static inline SimdF EvaluateTableFogVec(const SimdF& wVal,
                                        const float fogTableF[64]) {
  const SimdF zero_vec(0.0f);
  const SimdF one_vec(1.0f);
  const SimdF minOow(1e-6f);

  const SimdF clampedOow = Max(wVal, minOow);
  const SimdF eyeW = Max(one_vec / clampedOow, one_vec);
  const SimdF oowForIdx = Min(clampedOow, one_vec);

  const SimdI bits = SimdI::CastFromFloat(oowForIdx);
  const SimdI exp = bits.Srl(23) & SimdI(0xFF);
  const SimdI sub2 = bits.Srl(21) & SimdI(3);
  const SimdI zeroI(0);
  const SimdI maxIdxI(62);
  const SimdI oneI(1);

  SimdI idx = ((SimdI(127) - exp) << 2) - oneI - sub2;
  idx = Max(zeroI, Min(maxIdxI, idx));

  SimdF w0 = SimdF::Gather(s_tableIndexToW, idx);
  SimdF w1 = SimdF::Gather(s_tableIndexToW + 1, idx);

  const SimdM fixLo = (eyeW < w0) & (idx > zeroI);
  const SimdM fixHi = (eyeW >= w1) & (idx < maxIdxI);
  if (__builtin_expect((fixLo | fixHi).AnyTrue(), 0)) {
    idx = Select(fixLo, idx - oneI, idx);
    idx = Select(fixHi, idx + oneI, idx);
    w0 = SimdF::Gather(s_tableIndexToW, idx);
    w1 = SimdF::Gather(s_tableIndexToW + 1, idx);
  }

  const SimdF t = Max(Min((eyeW - w0) / (w1 - w0), one_vec), zero_vec);
  const SimdF f0 = SimdF::Gather(fogTableF, idx);
  const SimdF f1 = SimdF::Gather(fogTableF + 1, idx);
  return Fma(f1 - f0, t, f0);
}

}  // namespace GlideWrapper
