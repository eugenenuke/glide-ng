#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends/software/SimdVec.h"
#include "backends/software/SoftwareSimdInternal.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

using TmuColorSIMD = TmuColorVec<Simd8f>;
#define EvaluateTmuStageSIMD EvaluateTmuStageVec
#define ApplyBlendOpSIMD ApplyBlendOpVec
#define EvaluateExtTmuRgbSIMD EvaluateExtTmuRgbVec
#define EvaluateExtTmuAlphaSIMD EvaluateExtTmuAlphaVec
#define EvaluateExtFbiAlphaSIMD EvaluateExtFbiAlphaVec
#define EvaluateExtFbiColorSIMD EvaluateExtFbiColorVec

namespace {

inline bool IsPow2(uint32_t v) { return v != 0u && (v & (v - 1u)) == 0u; }

inline int Pow2Shift(uint32_t v) {
  int s = 0;
  while (v > 1u) {
    v >>= 1;
    ++s;
  }
  return s;
}

inline Simd8f GetBlendFactorSIMD(uint32_t mode, const Simd8f& srcC,
                                 const Simd8f& preFogC, const Simd8f& dstC,
                                 const Simd8f& srcA, const Simd8f& dstA,
                                 bool isDest) {
  switch (mode) {
    case 0:
      return Simd8f(0.0f);
    case 1:
      return srcA;
    case 2:
      return isDest ? srcC : dstC;
    case 3:
      return dstA;
    case 4:
      return Simd8f(1.0f);
    case 5:
      return Simd8f(1.0f) - srcA;
    case 6:
      return isDest ? (Simd8f(1.0f) - srcC) : (Simd8f(1.0f) - dstC);
    case 7:
      return Simd8f(1.0f) - dstA;
    case 8:
      return isDest ? dstC : srcC;
    case 9:
      return isDest ? (Simd8f(1.0f) - dstC) : (Simd8f(1.0f) - srcC);
    case 15:
      return isDest ? preFogC : Min(srcA, Simd8f(1.0f) - dstA);
    default:
      return Simd8f(1.0f);
  }
}

inline Simd8i MirrorCoordSIMD(const Simd8i& coord, uint32_t size) {
  Simd8i doubleSize(static_cast<int>(size * 2));
  Simd8i mask = doubleSize - Simd8i(1);
  Simd8i wrapped = coord & mask;
  Simd8i limit(static_cast<int>(size));
  SimdMask cond = wrapped < limit;
  return Select(cond, wrapped, mask - wrapped);
}

inline Simd8i ApplyTexChromaSIMD(const Simd8i& color, uint32_t tmu,
                                 const RasterizerState& state) {
  const Simd8i maskFF(0xFF);
  Simd8i r = (color >> 16) & maskFF;
  Simd8i g = (color >> 8) & maskFF;
  Simd8i b = color & maskFF;

  const bool rangeEnabled = ((state.texChromaRangeMode[tmu] >> 28) & 1u) == 1u;
  SimdMask chromaMask;
  if (rangeEnabled) {
    Simd8i minR(static_cast<int32_t>((state.texChromaMin[tmu] >> 16) & 0xFF));
    Simd8i minG(static_cast<int32_t>((state.texChromaMin[tmu] >> 8) & 0xFF));
    Simd8i minB(static_cast<int32_t>(state.texChromaMin[tmu] & 0xFF));

    Simd8i maxR(static_cast<int32_t>((state.texChromaMax[tmu] >> 16) & 0xFF));
    Simd8i maxG(static_cast<int32_t>((state.texChromaMax[tmu] >> 8) & 0xFF));
    Simd8i maxB(static_cast<int32_t>(state.texChromaMax[tmu] & 0xFF));

    SimdMask rMatch = (r >= minR) & (r <= maxR);
    SimdMask gMatch = (g >= minG) & (g <= maxG);
    SimdMask bMatch = (b >= minB) & (b <= maxB);

    const bool blueExcl = ((state.texChromaRangeMode[tmu] >> 24) & 1u) == 1u;
    const bool greenExcl = ((state.texChromaRangeMode[tmu] >> 25) & 1u) == 1u;
    const bool redExcl = ((state.texChromaRangeMode[tmu] >> 26) & 1u) == 1u;

    SimdMask rRes = redExcl ? !rMatch : rMatch;
    SimdMask gRes = greenExcl ? !gMatch : gMatch;
    SimdMask bRes = blueExcl ? !bMatch : bMatch;

    const bool unionMode = ((state.texChromaRangeMode[tmu] >> 27) & 1u) == 1u;
    chromaMask = unionMode ? (rRes | gRes | bRes) : (rRes & gRes & bRes);
  } else {
    Simd8i chromaR(
        static_cast<int32_t>((state.texChromaMin[tmu] >> 16) & 0xFF));
    Simd8i chromaG(
        static_cast<int32_t>((state.texChromaMin[tmu] >> 8) & 0xFF));
    Simd8i chromaB(static_cast<int32_t>(state.texChromaMin[tmu] & 0xFF));

    chromaMask = (r == chromaR) & (g == chromaG) & (b == chromaB);
  }
  return Select(chromaMask, Simd8i(0), color);
}

__attribute__((always_inline)) inline Simd8f ChannelFloatSIMD(
    const Simd8i& color, int shift, const Simd8i& maskFF) {
  return Simd8f::Convert((color >> shift) & maskFF);
}

__attribute__((always_inline)) inline TmuColorSIMD SampleTextureLevelSIMD(
    const uint32_t* levelPixels, uint32_t levelW, uint32_t levelH,
    uint32_t levelWMask, uint32_t levelHMask, bool isPow2W, int levelWShift,
    const Simd8f& lTexU, const Simd8f& lTexV, uint32_t clampS, uint32_t clampT,
    uint32_t minFilter, bool texChromaEnabled, uint32_t tmuIdx,
    const RasterizerState& state) {
  const Simd8i maskFF(0xFF);
  const Simd8f inv255(1.0f / 255.0f);

  if (minFilter == 1) {  // BILINEAR
    const Simd8f half(0.5f);
    Simd8f u_sub = lTexU - half;
    Simd8f v_sub = lTexV - half;
    Simd8f floor_u = Floor(u_sub);
    Simd8f floor_v = Floor(v_sub);
    Simd8i u0_vec = Simd8i::Convert(floor_u);
    Simd8i v0_vec = Simd8i::Convert(floor_v);

    const Simd8i oneI(1);
    Simd8i u1_vec = u0_vec + oneI;
    Simd8i v1_vec = v0_vec + oneI;

    Simd8i x0, x1, y0, y1;
    if (clampS == 0) {
      Simd8i mask(static_cast<int>(levelWMask));
      x0 = u0_vec & mask;
      x1 = u1_vec & mask;
    } else if (clampS == 1) {
      Simd8i zeroI(0);
      Simd8i limit(static_cast<int>(levelW - 1));
      x0 = Max(zeroI, Min(limit, u0_vec));
      x1 = Max(zeroI, Min(limit, u1_vec));
    } else {
      x0 = MirrorCoordSIMD(u0_vec, levelW);
      x1 = MirrorCoordSIMD(u1_vec, levelW);
    }

    if (clampT == 0) {
      Simd8i mask(static_cast<int>(levelHMask));
      y0 = v0_vec & mask;
      y1 = v1_vec & mask;
    } else if (clampT == 1) {
      Simd8i zeroI(0);
      Simd8i limit(static_cast<int>(levelH - 1));
      y0 = Max(zeroI, Min(limit, v0_vec));
      y1 = Max(zeroI, Min(limit, v1_vec));
    } else {
      y0 = MirrorCoordSIMD(v0_vec, levelH);
      y1 = MirrorCoordSIMD(v1_vec, levelH);
    }

    Simd8i y0Row, y1Row;
    if (isPow2W) {
      y0Row = y0 << levelWShift;
      y1Row = y1 << levelWShift;
    } else {
      Simd8i levelW_vec(static_cast<int>(levelW));
      y0Row = y0 * levelW_vec;
      y1Row = y1 * levelW_vec;
    }

    Simd8i c00 = Simd8i::Gather(levelPixels, y0Row + x0);
    Simd8i c10 = Simd8i::Gather(levelPixels, y0Row + x1);
    Simd8i c01 = Simd8i::Gather(levelPixels, y1Row + x0);
    Simd8i c11 = Simd8i::Gather(levelPixels, y1Row + x1);

    if (texChromaEnabled) {
      c00 = ApplyTexChromaSIMD(c00, tmuIdx, state);
      c10 = ApplyTexChromaSIMD(c10, tmuIdx, state);
      c01 = ApplyTexChromaSIMD(c01, tmuIdx, state);
      c11 = ApplyTexChromaSIMD(c11, tmuIdx, state);
    }

    const Simd8f one(1.0f);
    Simd8f fx = u_sub - floor_u;
    Simd8f fy = v_sub - floor_v;
    Simd8f om_fx = one - fx;
    Simd8f fy_s = fy * inv255;
    Simd8f om_fy_s = (one - fy) * inv255;

    Simd8f w00_f = om_fx * om_fy_s;
    Simd8f w10_f = fx * om_fy_s;
    Simd8f w01_f = om_fx * fy_s;
    Simd8f w11_f = fx * fy_s;

    Simd8f r = Fma(ChannelFloatSIMD(c00, 16, maskFF), w00_f,
                   Fma(ChannelFloatSIMD(c10, 16, maskFF), w10_f,
                       Fma(ChannelFloatSIMD(c01, 16, maskFF), w01_f,
                           ChannelFloatSIMD(c11, 16, maskFF) * w11_f)));
    Simd8f g = Fma(ChannelFloatSIMD(c00, 8, maskFF), w00_f,
                   Fma(ChannelFloatSIMD(c10, 8, maskFF), w10_f,
                       Fma(ChannelFloatSIMD(c01, 8, maskFF), w01_f,
                           ChannelFloatSIMD(c11, 8, maskFF) * w11_f)));
    Simd8f b = Fma(Simd8f::Convert(c00 & maskFF), w00_f,
                   Fma(Simd8f::Convert(c10 & maskFF), w10_f,
                       Fma(Simd8f::Convert(c01 & maskFF), w01_f,
                           Simd8f::Convert(c11 & maskFF) * w11_f)));
    Simd8f a_val = Fma(ChannelFloatSIMD(c00, 24, maskFF), w00_f,
                       Fma(ChannelFloatSIMD(c10, 24, maskFF), w10_f,
                           Fma(ChannelFloatSIMD(c01, 24, maskFF), w01_f,
                               ChannelFloatSIMD(c11, 24, maskFF) * w11_f)));

    return {r, g, b, a_val};
  } else {  // POINT
    const Simd8f eps(0.0001f);
    Simd8i u0_vec = Simd8i::Convert(lTexU + eps);
    Simd8i v0_vec = Simd8i::Convert(lTexV + eps);

    Simd8i x0, y0;
    if (clampS == 0) {
      x0 = u0_vec & Simd8i(static_cast<int>(levelWMask));
    } else if (clampS == 1) {
      x0 = Max(Simd8i(0), Min(Simd8i(static_cast<int>(levelW - 1)), u0_vec));
    } else {
      x0 = MirrorCoordSIMD(u0_vec, levelW);
    }

    if (clampT == 0) {
      y0 = v0_vec & Simd8i(static_cast<int>(levelHMask));
    } else if (clampT == 1) {
      y0 = Max(Simd8i(0), Min(Simd8i(static_cast<int>(levelH - 1)), v0_vec));
    } else {
      y0 = MirrorCoordSIMD(v0_vec, levelH);
    }

    Simd8i y0Row = isPow2W ? (y0 << levelWShift)
                           : (y0 * Simd8i(static_cast<int>(levelW)));
    Simd8i val = Simd8i::Gather(levelPixels, y0Row + x0);
    if (texChromaEnabled) {
      val = ApplyTexChromaSIMD(val, tmuIdx, state);
    }

    return {ChannelFloatSIMD(val, 16, maskFF) * inv255,
            ChannelFloatSIMD(val, 8, maskFF) * inv255,
            Simd8f::Convert(val & maskFF) * inv255,
            ChannelFloatSIMD(val, 24, maskFF) * inv255};
  }
}

__attribute__((always_inline)) inline TmuColorSIMD SampleMipLevelSIMD(
    const VirtualTexture* tex, int lodIdx, const Simd8f& trueS,
    const Simd8f& trueT, float scaleU, float scaleV, uint32_t clampS,
    uint32_t clampT, uint32_t minFilter, bool texChromaEnabled, uint32_t tmuIdx,
    const RasterizerState& state) {
  const uint32_t* levelPixels = tex->swizzledMipLevels[lodIdx].data();
  const uint32_t levelW = std::max(1u, tex->baseWidth >> lodIdx);
  const uint32_t levelH = std::max(1u, tex->baseHeight >> lodIdx);
  const bool isPow2W = IsPow2(levelW);
  const int levelWShift = isPow2W ? Pow2Shift(levelW) : 0;
  const float invPow = 1.0f / static_cast<float>(1 << lodIdx);
  const Simd8f lTexU = trueS * Simd8f(scaleU * invPow);
  const Simd8f lTexV = trueT * Simd8f(scaleV * invPow);
  return SampleTextureLevelSIMD(levelPixels, levelW, levelH, levelW - 1u,
                                levelH - 1u, isPow2W, levelWShift, lTexU, lTexV,
                                clampS, clampT, minFilter, texChromaEnabled,
                                tmuIdx, state);
}

}  // namespace

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
          bool Clockwise>
void SoftwareBackend::RasterizeTriangleLoopsSIMD(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int minX, int maxX, int minY, int maxY, const RasterizerState& state,
    float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
    float a2, float b2, float c2) {
  auto* pixels = reinterpret_cast<uint32_t*>(
      m_cpuBuffers[state.activeRenderBufferIdx].data());
  float* depthBuffer =
      m_headlessDepthBuffer.empty() ? nullptr : m_headlessDepthBuffer.data();
  uint8_t* stencilBuffer = m_headlessStencilBuffer.empty()
                               ? nullptr
                               : m_headlessStencilBuffer.data();
  const int width = m_headlessWidth;

  // Cache state variables
  const uint32_t stippleMode = state.stippleMode;
  const uint32_t stipplePattern = state.stipplePattern;
  const uint32_t depthMode = state.depthMode;
  const uint32_t depthCompareOp = state.depthCompareOp;
  const bool depthMask = state.depthMask;
  const int depthBiasLevel = state.depthBiasLevel;
  const bool stencilEnabled = state.stencilEnabled;
  const uint32_t stencilCompareOp = state.stencilCompareOp;
  const uint8_t stencilRef = state.stencilRef;
  const uint8_t stencilReadMask = state.stencilReadMask;
  const uint8_t stencilWriteMask = state.stencilWriteMask;
  const uint32_t stencilFailOp = state.stencilFailOp;
  const uint32_t stencilZFailOp = state.stencilZFailOp;
  const uint32_t stencilZPassOp = state.stencilZPassOp;
  const uint32_t fogMode = state.fogMode;
  const uint32_t fogColor = state.fogColor;
  const uint32_t chromakeyMode = state.chromakeyMode;
  const uint32_t chromakeyRangeMode = state.chromakeyRangeMode;
  const uint32_t chromakeyRangeMin = state.chromakeyRangeMin;
  const uint32_t chromakeyRangeMax = state.chromakeyRangeMax;
  const uint32_t chromakeyValue = state.chromakeyValue;
  const uint32_t pixelFormatOverride = state.pixelFormatOverride;
  const uint32_t alphaTestOp = state.alphaTestOp;
  const uint32_t alphaTestRefVal = state.alphaTestRefVal;
  const bool colorMaskR = state.colorMaskR;
  const bool colorMaskG = state.colorMaskG;
  const bool colorMaskB = state.colorMaskB;
  const bool colorMaskAlpha = state.colorMaskAlpha;
  const uint32_t rgbSrcBlend = state.rgbSrcBlend;
  const uint32_t rgbDstBlend = state.rgbDstBlend;
  const uint32_t rgbBlendOp = state.rgbBlendOp;
  const uint32_t alphaSrcBlend = state.alphaSrcBlend;
  const uint32_t alphaDstBlend = state.alphaDstBlend;
  const uint32_t alphaBlendOp = state.alphaBlendOp;
  const uint32_t constantColor = state.constantColor;
  const uint32_t stwHintMask = state.stwHintMask;
  const uint32_t alphaCombinerLocal = state.alphaCombinerLocal;
  const uint32_t alphaCombinerOther = state.alphaCombinerOther;
  const uint32_t colorCombinerLocal = state.colorCombinerLocal;
  const uint32_t colorCombinerOther = state.colorCombinerOther;
  const uint32_t colorCombinerFactor = state.colorCombinerFactor;
  const uint32_t colorCombinerFunc = state.colorCombinerFunc;
  const uint32_t alphaCombinerFactor = state.alphaCombinerFactor;
  const uint32_t alphaCombinerFunc = state.alphaCombinerFunc;
  const bool alphaCombinerInvert = state.alphaCombinerInvert;
  const bool colorCombinerInvert = state.colorCombinerInvert;
  const uint32_t extColorCombine = state.extColorCombine;
  const uint32_t extAlphaCombine = state.extAlphaCombine;
  const uint32_t extTexColorCombine0 = state.extTexColorCombine[0];
  const uint32_t extTexAlphaCombine0 = state.extTexAlphaCombine[0];
  const uint32_t extTexColorCombine1 = state.extTexColorCombine[1];
  const uint32_t extTexAlphaCombine1 = state.extTexAlphaCombine[1];
  const bool anyExtCombine =
      ((extColorCombine | extAlphaCombine | extTexColorCombine0 |
        extTexAlphaCombine0 | extTexColorCombine1 | extTexAlphaCombine1) &
       0x80000000u) != 0u;

  const float depthFar = state.depthFar;
  const float depthNear = state.depthNear;
  const float depthRangeInv = (std::abs(depthFar - depthNear) > 1e-6f)
                                  ? (1.0f / (depthFar - depthNear))
                                  : 0.0f;

  const float constantAlpha = ((constantColor >> 24) & 0xFF) / 255.0f;
  float constantR = ((constantColor >> 16) & 0xFF) / 255.0f;
  float constantG = ((constantColor >> 8) & 0xFF) / 255.0f;
  float constantB = (constantColor & 0xFF) / 255.0f;
  if (pixelFormatOverride == 1) std::swap(constantR, constantB);

  auto unpackTmuConstSimd = [&](uint32_t c) -> TmuColorSIMD {
    float r = ((c >> 16) & 0xFF) / 255.0f;
    float g = ((c >> 8) & 0xFF) / 255.0f;
    float b = (c & 0xFF) / 255.0f;
    float aVal = ((c >> 24) & 0xFF) / 255.0f;
    if (pixelFormatOverride == 1) std::swap(r, b);
    return {Simd8f(r), Simd8f(g), Simd8f(b), Simd8f(aVal)};
  };
  const TmuColorSIMD tmuConst0_vec =
      unpackTmuConstSimd(state.tmuConstantColor[0]);
  const TmuColorSIMD tmuConst1_vec =
      unpackTmuConstSimd(state.tmuConstantColor[1]);

  const float alphaTestRef = static_cast<float>(alphaTestRefVal) / 255.0f;

  float chromaR = 0.0f, chromaG = 0.0f, chromaB = 0.0f;
  float chromaMinR = 0.0f, chromaMinG = 0.0f, chromaMinB = 0.0f;
  float chromaMaxR = 0.0f, chromaMaxG = 0.0f, chromaMaxB = 0.0f;
  if (chromakeyMode == 1) {
    bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1) == 1;
    if (rangeEnabled) {
      chromaMinR = ((chromakeyRangeMin >> 16) & 0xFF) / 255.0f;
      chromaMinG = ((chromakeyRangeMin >> 8) & 0xFF) / 255.0f;
      chromaMinB = (chromakeyRangeMin & 0xFF) / 255.0f;
      chromaMaxR = ((chromakeyRangeMax >> 16) & 0xFF) / 255.0f;
      chromaMaxG = ((chromakeyRangeMax >> 8) & 0xFF) / 255.0f;
      chromaMaxB = (chromakeyRangeMax & 0xFF) / 255.0f;
    } else {
      chromaR = ((chromakeyValue >> 16) & 0xFF) / 255.0f;
      chromaG = ((chromakeyValue >> 8) & 0xFF) / 255.0f;
      chromaB = (chromakeyValue & 0xFF) / 255.0f;
      if (pixelFormatOverride == 1) {
        std::swap(chromaR, chromaB);
      }
    }
  }

  float fogR = 0.0f, fogG = 0.0f, fogB = 0.0f;
  if (fogMode != 0) {
    fogR = ((fogColor >> 16) & 0xFF) / 255.0f;
    fogG = ((fogColor >> 8) & 0xFF) / 255.0f;
    fogB = (fogColor & 0xFF) / 255.0f;
    if (pixelFormatOverride == 1) std::swap(fogR, fogB);
  }

  // TMU 0 Setup
  uint32_t texW = 0;
  uint32_t texH = 0;
  uint32_t clampS = 0;
  uint32_t clampT = 0;
  uint32_t minFilter = 0;
  float scaleU = 0.0f;
  float scaleV = 0.0f;
  bool mipmappingEnabled = false;
  bool lodBlend = false;
  const VirtualTexture* tex = nullptr;
  const uint32_t* tex0Mip0Pixels = nullptr;
  bool tex0IsPow2W = false;
  int tex0WShift = 0;
  const bool tex0ChromaEnabled = (state.texChromaMode[0] == 1);
  const bool tmu0Passthrough =
      (state.texCombinerRgbFunc[0] == 1 && state.texCombinerAlphaFunc[0] == 1 &&
       !state.texCombinerRgbInvert[0] && !state.texCombinerAlphaInvert[0] &&
       extTexColorCombine0 == 0u && extTexAlphaCombine0 == 0u);

  if constexpr (HasTexture) {
    tex = state.boundTex[0].get();
    if (tex && !tex->swizzledMipLevels.empty()) {
      texW = tex->baseWidth;
      texH = tex->baseHeight;
      tex0Mip0Pixels = tex->swizzledMipLevels[0].data();
      tex0IsPow2W = IsPow2(texW);
      tex0WShift = tex0IsPow2W ? Pow2Shift(texW) : 0;
      clampS = state.texClampS[0];
      clampT = state.texClampT[0];
      minFilter = state.texMinFilter[0];
      float maxDim = static_cast<float>(std::max(texW, texH));
      scaleU = maxDim / 256.0f;
      scaleV = maxDim / 256.0f;
      lodBlend = state.texLodBlend[0];
      mipmappingEnabled = (state.texMipMapMode[0] != 0);
    }
  }

  // TMU 1 Setup
  bool hasTexture1 = (state.boundTexAddress[1] != 0xFFFFFFFF);
  uint32_t texW1 = 0;
  uint32_t texH1 = 0;
  uint32_t clampS1 = 0;
  uint32_t clampT1 = 0;
  uint32_t minFilter1 = 0;
  float scaleU1 = 0.0f;
  float scaleV1 = 0.0f;
  bool mipmappingEnabled1 = false;
  bool lodBlend1 = false;
  const VirtualTexture* tex1 = nullptr;
  const uint32_t* tex1Mip0Pixels = nullptr;
  bool tex1IsPow2W = false;
  int tex1WShift = 0;
  const bool tex1ChromaEnabled = (state.texChromaMode[1] == 1);
  const bool tmu1Passthrough =
      (state.texCombinerRgbFunc[1] == 1 && state.texCombinerAlphaFunc[1] == 1 &&
       !state.texCombinerRgbInvert[1] && !state.texCombinerAlphaInvert[1] &&
       extTexColorCombine1 == 0u && extTexAlphaCombine1 == 0u);

  if (hasTexture1) {
    tex1 = state.boundTex[1].get();
    if (tex1 && !tex1->swizzledMipLevels.empty()) {
      texW1 = tex1->baseWidth;
      texH1 = tex1->baseHeight;
      tex1Mip0Pixels = tex1->swizzledMipLevels[0].data();
      tex1IsPow2W = IsPow2(texW1);
      tex1WShift = tex1IsPow2W ? Pow2Shift(texW1) : 0;
      clampS1 = state.texClampS[1];
      clampT1 = state.texClampT[1];
      minFilter1 = state.texMinFilter[1];
      float maxDim1 = static_cast<float>(std::max(texW1, texH1));
      scaleU1 = maxDim1 / 256.0f;
      scaleV1 = maxDim1 / 256.0f;
      lodBlend1 = state.texLodBlend[1];
      mipmappingEnabled1 = (state.texMipMapMode[1] != 0);
    } else {
      hasTexture1 = false;
    }
  }

  float dw0_dx = 0.0f, dw0_dy = 0.0f;
  float dw1_dx = 0.0f, dw1_dy = 0.0f;
  float dw2_dx = 0.0f, dw2_dy = 0.0f;
  float dSp_dx = 0.0f, dSp_dy = 0.0f;
  float dTp_dx = 0.0f, dTp_dy = 0.0f;
  float dQ_dx = 0.0f, dQ_dy = 0.0f;
  float dSp1_dx = 0.0f, dSp1_dy = 0.0f;
  float dTp1_dx = 0.0f, dTp1_dy = 0.0f;
  float dQ1_dx = 0.0f, dQ1_dy = 0.0f;

  auto usesLodFactor = [](uint32_t f) {
    return f == 4u || f == 5u || f == 12u || f == 13u;
  };
  const bool needLod0 =
      HasTexture && tex != nullptr &&
      (mipmappingEnabled || usesLodFactor(state.texCombinerRgbFactor[0]) ||
       usesLodFactor(state.texCombinerAlphaFactor[0]) ||
       ExtCombineUsesLod(extTexColorCombine0) ||
       ExtCombineUsesLod(extTexAlphaCombine0));
  const bool needLod1 =
      hasTexture1 && tex1 != nullptr &&
      (mipmappingEnabled1 || usesLodFactor(state.texCombinerRgbFactor[1]) ||
       usesLodFactor(state.texCombinerAlphaFactor[1]) ||
       ExtCombineUsesLod(extTexColorCombine1) ||
       ExtCombineUsesLod(extTexAlphaCombine1));

  const float detailScaleMul0 =
      static_cast<float>(1u << std::min<uint8_t>(state.texDetailScale[0], 7u)) /
      255.0f;
  const float detailScaleMul1 =
      static_cast<float>(1u << std::min<uint8_t>(state.texDetailScale[1], 7u)) /
      255.0f;

  bool needBarycentricDerivatives = needLod0 || needLod1;
  if (needBarycentricDerivatives) {
    dw0_dx = a0 * invArea;
    dw0_dy = b0 * invArea;
    dw1_dx = a1 * invArea;
    dw1_dy = b1 * invArea;
    dw2_dx = a2 * invArea;
    dw2_dy = b2 * invArea;
  }

  if (needLod0) {
    dSp_dx = dw0_dx * a.tex[0] + dw1_dx * b.tex[0] + dw2_dx * c.tex[0];
    dSp_dy = dw0_dy * a.tex[0] + dw1_dy * b.tex[0] + dw2_dy * c.tex[0];
    dTp_dx = dw0_dx * a.tex[1] + dw1_dx * b.tex[1] + dw2_dx * c.tex[1];
    dTp_dy = dw0_dy * a.tex[1] + dw1_dy * b.tex[1] + dw2_dy * c.tex[1];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (stwHintMask & 0x2) {
      qa = a.tmu_oow[0];
      qb = b.tmu_oow[0];
      qc = c.tmu_oow[0];
    }
    dQ_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  if (needLod1) {
    dSp1_dx = dw0_dx * a.tex[2] + dw1_dx * b.tex[2] + dw2_dx * c.tex[2];
    dSp1_dy = dw0_dy * a.tex[2] + dw1_dy * b.tex[2] + dw2_dy * c.tex[2];
    dTp1_dx = dw0_dx * a.tex[3] + dw1_dx * b.tex[3] + dw2_dx * c.tex[3];
    dTp1_dy = dw0_dy * a.tex[3] + dw1_dy * b.tex[3] + dw2_dy * c.tex[3];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (stwHintMask & 0x8) {
      qa = a.tmu_oow[1];
      qb = b.tmu_oow[1];
      qc = c.tmu_oow[1];
    }
    dQ1_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ1_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  // Hoist combiner & attribute flags out of the pixel loops
  const bool needIterRgb =
      anyExtCombine || (colorCombinerLocal == 0) || (colorCombinerOther == 0) ||
      hasTexture1 || (HasTexture && !tmu0Passthrough) ||
      (!HasTexture && !hasTexture1);
  const bool needIterAlpha =
      anyExtCombine || (alphaCombinerLocal == 0) || (alphaCombinerOther == 0) ||
      ((fogMode & 0x0Fu) == 4) || hasTexture1 ||
      (HasTexture && !tmu0Passthrough) || (!HasTexture && !hasTexture1);

  uint8_t ccFastPath = 0;
  if (!colorCombinerInvert && (extColorCombine & 0x80000000u) == 0u) {
    if (colorCombinerFunc == 3 && colorCombinerOther == 1 &&
        colorCombinerFactor == 8) {
      ccFastPath = 1;  // Decal texture: texRGB
    } else if (colorCombinerFunc == 3 && colorCombinerOther == 1 &&
               colorCombinerFactor == 1) {
      ccFastPath = 2;  // Modulated texture: texRGB * cLocalRGB
    } else if (colorCombinerFunc == 1) {
      ccFastPath = 3;  // Local only: cLocalRGB
    }
  }

  uint8_t acFastPath = 0;
  if (!alphaCombinerInvert && (extAlphaCombine & 0x80000000u) == 0u) {
    if (alphaCombinerFunc == 1) {
      acFastPath = 1;  // Local alpha
    } else if (alphaCombinerFunc == 3 && alphaCombinerOther == 1 &&
               alphaCombinerFactor == 8) {
      acFastPath = 2;  // Texture alpha
    } else if (alphaCombinerFunc == 3 && alphaCombinerOther == 1 &&
               (alphaCombinerFactor == 1 || alphaCombinerFactor == 3)) {
      acFastPath = 3;  // texA * aLocal
    }
  }

  const bool needDepthOp =
      (depthMode != 0 && depthBuffer != nullptr &&
       (depthCompareOp != 7 || depthMask));

  // Fast-path classification for BlendEnabled
  uint8_t blendFastPath = 0;
  bool blendNeedsDstA = true;
  if constexpr (BlendEnabled) {
    const bool alphaSimple =
        (alphaSrcBlend == 4 && alphaDstBlend == 0 && alphaBlendOp == 0);
    if (rgbBlendOp == 0 && rgbSrcBlend == 1 && rgbDstBlend == 5 && alphaSimple) {
      blendFastPath = 1;  // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
      blendNeedsDstA = false;
    } else if (rgbBlendOp == 0 && rgbSrcBlend == 2 && rgbDstBlend == 0 &&
               alphaSimple) {
      blendFastPath = 2;  // DST_COLOR, ZERO (lightmap multiply)
      blendNeedsDstA = false;
    } else if (rgbBlendOp == 0 && rgbSrcBlend == 4 && rgbDstBlend == 4 &&
               alphaSimple) {
      blendFastPath = 3;  // ONE, ONE (additive)
      blendNeedsDstA = false;
    }
  }

  const int startX = minX & ~3;
  const int startY = minY & ~1;

  // Pre-broadcast triangle constants outside the pixel loops
  const Simd8f invArea_vec(invArea);
  const Simd8f a_oow(a.pos[3]), db_oow(b.pos[3] - a.pos[3]),
      dc_oow(c.pos[3] - a.pos[3]);
  const Simd8f a_z(a.pos[2]), db_z(b.pos[2] - a.pos[2]),
      dc_z(c.pos[2] - a.pos[2]);
  const Simd8f a_r(a.color[0]), b_r(b.color[0]), c_r(c.color[0]);
  const Simd8f a_g(a.color[1]), b_g(b.color[1]), c_g(c.color[1]);
  const Simd8f a_b(a.color[2]), b_b(b.color[2]), c_b(c.color[2]);
  const Simd8f a_a(a.color[3]), b_a(b.color[3]), c_a(c.color[3]);
  const Simd8f a_s0(a.tex[0]), b_s0(b.tex[0]), c_s0(c.tex[0]);
  const Simd8f a_t0(a.tex[1]), b_t0(b.tex[1]), c_t0(c.tex[1]);
  const Simd8f scaleU0_vec(scaleU), scaleV0_vec(scaleV);
  const Simd8f one_vec(1.0f);
  const Simd8f zero_vec(0.0f);
  const Simd8f c255_vec(255.0f);
  const Simd8f half_vec(0.5f);
  const Simd8f inv255_vec(1.0f / 255.0f);
  const Simd8i maskFF_i(0xFF);
  const Simd8f constR_vec(constantR), constG_vec(constantG),
      constB_vec(constantB), constA_vec(constantAlpha);
  const Simd8f minX_f(static_cast<float>(minX));
  const Simd8f maxX_f(static_cast<float>(maxX) + 1.0f);
  const Simd8f minY_f(static_cast<float>(minY));
  const Simd8f maxY_f(static_cast<float>(maxY) + 1.0f);
  const Simd8f edgeEps_vec(Clockwise ? -1e-5f : 1e-5f);
  const uint32_t colorMaskVal =
      (colorMaskR ? 0x00FF0000u : 0u) | (colorMaskG ? 0x0000FF00u : 0u) |
      (colorMaskB ? 0x000000FFu : 0u) | (colorMaskAlpha ? 0xFF000000u : 0u);
  const bool fullColorMask = (colorMaskVal == 0xFFFFFFFFu);
  const Simd8i colorMask_vec(static_cast<int32_t>(colorMaskVal));
  const Simd8i invColorMask_vec(static_cast<int32_t>(~colorMaskVal));

  const uint8_t tbufMask =
      (m_ssaaScale == 2) ? static_cast<uint8_t>(state.tbufferWriteMask & 0x0Fu)
                         : static_cast<uint8_t>(0x0Fu);
  const bool applyTbufMask = (m_ssaaScale == 2 && tbufMask != 0x0Fu);
  const bool collapseAa = (m_ssaaScale == 2 && !state.aaMultiSample);
  if (applyTbufMask && tbufMask == 0u) return;

  const int32_t tm0 = (tbufMask & 1u) ? 1 : 0;
  const int32_t tm1 = (tbufMask & 2u) ? 1 : 0;
  const int32_t tm2 = (tbufMask & 4u) ? 1 : 0;
  const int32_t tm3 = (tbufMask & 8u) ? 1 : 0;
  const SimdMask tbufSimdMask =
      (Simd8i(tm0, tm1, tm0, tm1, tm2, tm3, tm2, tm3) != Simd8i(0));

  // Pre-broadcast hardware stencil & table-fog constants outside the pixel loop
  const Simd8i stencilRef_vec(static_cast<int32_t>(stencilRef));
  const Simd8i stencilReadMask_vec(static_cast<int32_t>(stencilReadMask));
  const Simd8i stencilMaskedRef_vec(
      static_cast<int32_t>(stencilRef & stencilReadMask));
  const Simd8i stencilWriteMask_vec(static_cast<int32_t>(stencilWriteMask));
  const Simd8i stencilInvWriteMask_vec(
      static_cast<int32_t>(~stencilWriteMask) & 0xFF);
  alignas(64) float fogTableF[64];
  if (fogMode != 0 && (fogMode & 0x0F) != 4) {
    for (int i = 0; i < 64; ++i) {
      fogTableF[i] = static_cast<float>(state.fogTable[i]) / 255.0f;
    }
  }

  // Incremental 2x4 edge equation setup.
  // When 2x2 SSAA has live GR_AA_MULTI_SAMPLE disabled, collapse all 4
  // subpixels of each 2x2 guest pixel onto the guest-pixel center
  // (startX+1.0, startX+3.0, startY+1.0) so edges render cleanly 1x aliased
  // with zero extra instructions inside the pixel loop.
  const Simd8f X_init =
      collapseAa
          ? Simd8f(static_cast<float>(startX) + 1.0f,
                   static_cast<float>(startX) + 1.0f,
                   static_cast<float>(startX) + 3.0f,
                   static_cast<float>(startX) + 3.0f,
                   static_cast<float>(startX) + 1.0f,
                   static_cast<float>(startX) + 1.0f,
                   static_cast<float>(startX) + 3.0f,
                   static_cast<float>(startX) + 3.0f)
          : Simd8f(static_cast<float>(startX) + 0.5f,
                   static_cast<float>(startX) + 1.5f,
                   static_cast<float>(startX) + 2.5f,
                   static_cast<float>(startX) + 3.5f,
                   static_cast<float>(startX) + 0.5f,
                   static_cast<float>(startX) + 1.5f,
                   static_cast<float>(startX) + 2.5f,
                   static_cast<float>(startX) + 3.5f);
  const Simd8f Y_init =
      collapseAa
          ? Simd8f(static_cast<float>(startY) + 1.0f)
          : Simd8f(static_cast<float>(startY) + 0.5f,
                   static_cast<float>(startY) + 0.5f,
                   static_cast<float>(startY) + 0.5f,
                   static_cast<float>(startY) + 0.5f,
                   static_cast<float>(startY) + 1.5f,
                   static_cast<float>(startY) + 1.5f,
                   static_cast<float>(startY) + 1.5f,
                   static_cast<float>(startY) + 1.5f);

  Simd8f E0_row = Fma(Simd8f(a0), X_init, Fma(Simd8f(b0), Y_init, Simd8f(c0)));
  Simd8f E1_row = Fma(Simd8f(a1), X_init, Fma(Simd8f(b1), Y_init, Simd8f(c1)));
  Simd8f E2_row = Fma(Simd8f(a2), X_init, Fma(Simd8f(b2), Y_init, Simd8f(c2)));
  const Simd8f E0_stepX(a0 * 4.0f), E1_stepX(a1 * 4.0f), E2_stepX(a2 * 4.0f);
  const Simd8f E0_stepY(b0 * 2.0f), E1_stepY(b1 * 2.0f), E2_stepY(b2 * 2.0f);

  for (int y = startY; y <= maxY;
       y += 2, E0_row += E0_stepY, E1_row += E1_stepY, E2_row += E2_stepY) {
    const bool rowInsideY = (y >= minY) && (y + 1 <= maxY);
    Simd8i rowDith;
    if constexpr (DitherEnabled) {
      rowDith = (state.ditherMode == 2) ? GetDitherVector4x4(y)
                                        : GetDitherVector2x2(y);
    }

    Simd8f E0 = E0_row;
    Simd8f E1 = E1_row;
    Simd8f E2 = E2_row;

    for (int x = startX; x <= maxX;
         x += 4, E0 += E0_stepX, E1 += E1_stepX, E2 += E2_stepX) {
      // 1. Edge Coverage Test
      SimdMask insideMask;
      if constexpr (Clockwise) {
        insideMask =
            (E0 >= edgeEps_vec) & (E1 >= edgeEps_vec) & (E2 >= edgeEps_vec);
      } else {
        insideMask =
            (E0 <= edgeEps_vec) & (E1 <= edgeEps_vec) & (E2 <= edgeEps_vec);
      }

      SimdMask coverageMask;
      if (rowInsideY && x >= minX && x + 3 <= maxX) {
        coverageMask = insideMask;
      } else {
        Simd8f X_vec(
            static_cast<float>(x) + 0.5f, static_cast<float>(x) + 1.5f,
            static_cast<float>(x) + 2.5f, static_cast<float>(x) + 3.5f,
            static_cast<float>(x) + 0.5f, static_cast<float>(x) + 1.5f,
            static_cast<float>(x) + 2.5f, static_cast<float>(x) + 3.5f);
        Simd8f Y_vec(
            static_cast<float>(y) + 0.5f, static_cast<float>(y) + 0.5f,
            static_cast<float>(y) + 0.5f, static_cast<float>(y) + 0.5f,
            static_cast<float>(y) + 1.5f, static_cast<float>(y) + 1.5f,
            static_cast<float>(y) + 1.5f, static_cast<float>(y) + 1.5f);
        SimdMask boundaryMask = (X_vec >= minX_f) & (X_vec <= maxX_f) &
                                (Y_vec >= minY_f) & (Y_vec <= maxY_f);
        coverageMask = insideMask & boundaryMask;
      }
      if (applyTbufMask) {
        coverageMask = coverageMask & tbufSimdMask;
      }
      if (coverageMask.AllZero()) continue;

      // 2. Barycentric & Perspective Correction
      Simd8f w0_vec = E0 * invArea_vec;
      Simd8f w1_vec = E1 * invArea_vec;
      Simd8f w2_vec = E2 * invArea_vec;

      // Anchor interpolation at vertex a so constant-oow quads are bit-exact
      // across triangles (prevents coplanar W-buffer Z-fighting).
      Simd8f wVal = Fma(w2_vec, dc_oow, Fma(w1_vec, db_oow, a_oow));
      Simd8f divW0 =
          (stwHintMask & 0x2)
              ? Fma(w2_vec, Simd8f(c.tmu_oow[0] - a.tmu_oow[0]),
                    Fma(w1_vec, Simd8f(b.tmu_oow[0] - a.tmu_oow[0]),
                        Simd8f(a.tmu_oow[0])))
              : wVal;
      Simd8f divW1 =
          (stwHintMask & 0x8)
              ? Fma(w2_vec, Simd8f(c.tmu_oow[1] - a.tmu_oow[1]),
                    Fma(w1_vec, Simd8f(b.tmu_oow[1] - a.tmu_oow[1]),
                        Simd8f(a.tmu_oow[1])))
              : wVal;

      if (depthMode == 2) {
        coverageMask =
            coverageMask & (wVal > zero_vec) & (wVal <= Simd8f(1.0001f));
        if (coverageMask.AllZero()) continue;
      } else {
        if constexpr (HasTexture) {
          coverageMask = coverageMask & (divW0 > zero_vec);
          if (coverageMask.AllZero()) continue;
        }
        if (hasTexture1) {
          coverageMask = coverageMask & (divW1 > zero_vec);
          if (coverageMask.AllZero()) continue;
        }
      }

      // 3. Stipple Transparency (fully vectorized via variable shift)
      if (stippleMode != 0) {
        Simd8i x_i(x, x + 1, x + 2, x + 3, x, x + 1, x + 2, x + 3);
        Simd8i y_i(y, y, y, y, y + 1, y + 1, y + 1, y + 1);
        Simd8i stippleX = (stippleMode == 2) ? ((x_i + y_i) & Simd8i(7))
                                             : (x_i & Simd8i(7));
        Simd8i stippleY = y_i & Simd8i(3);
        Simd8i stippleIndex = (stippleY << 3) | (Simd8i(7) - stippleX);
        Simd8i bit = Simd8i(static_cast<int32_t>(stipplePattern))
                         .Srlv(stippleIndex) &
                     Simd8i(1);
        coverageMask = coverageMask & (bit != Simd8i(0));
        if (coverageMask.AllZero()) continue;
      }

      // 4. Interpolate Iterated Attributes (only when referenced)
      Simd8f iterR = zero_vec;
      Simd8f iterG = zero_vec;
      Simd8f iterB = zero_vec;
      Simd8f iterA = zero_vec;
      if (needIterRgb) {
        iterR = Fma(w0_vec, a_r, Fma(w1_vec, b_r, w2_vec * c_r));
        iterG = Fma(w0_vec, a_g, Fma(w1_vec, b_g, w2_vec * c_g));
        iterB = Fma(w0_vec, a_b, Fma(w1_vec, b_b, w2_vec * c_b));
      }
      if (needIterAlpha) {
        iterA = Fma(w0_vec, a_a, Fma(w1_vec, b_a, w2_vec * c_a));
      }

      // 5. Texture Sampling Stage
      const TmuColorSIMD iterColorVec = {iterR, iterG, iterB, iterA};
      const TmuColorSIMD constColorVec = {constR_vec, constG_vec, constB_vec,
                                          constA_vec};
      TmuColorSIMD tmu0Out = iterColorVec;
      TmuColorSIMD tmu1Out = iterColorVec;

      // --- TMU 1 (Upstream) ---
      TmuColorSIMD tmu1Color = {one_vec, one_vec, one_vec, one_vec};
      float detailFactor1 = 0.0f;
      float lodFrac1 = 0.0f;
      if (hasTexture1) {
        Simd8f texS1 =
            Fma(w0_vec, Simd8f(a.tex[2]),
                Fma(w1_vec, Simd8f(b.tex[2]), w2_vec * Simd8f(c.tex[2])));
        Simd8f texT1 =
            Fma(w0_vec, Simd8f(a.tex[3]),
                Fma(w1_vec, Simd8f(b.tex[3]), w2_vec * Simd8f(c.tex[3])));

        Simd8f invW1 = one_vec / divW1;
        Simd8f trueS1 = texS1 * invW1;
        Simd8f trueT1 = texT1 * invW1;

        if (tex1) {
          if (!needLod1) {
            tmu1Color = SampleTextureLevelSIMD(
                tex1Mip0Pixels, texW1, texH1, texW1 - 1u, texH1 - 1u,
                tex1IsPow2W, tex1WShift, trueS1 * Simd8f(scaleU1),
                trueT1 * Simd8f(scaleV1), clampS1, clampT1, minFilter1,
                tex1ChromaEnabled, 1u, state);
          } else {
            float centerX = static_cast<float>(x) + 1.5f;
            float centerY = static_cast<float>(y) + 0.5f;
            float centerW0 = (a0 * centerX + b0 * centerY + c0) * invArea;
            float centerW1 = (a1 * centerX + b1 * centerY + c1) * invArea;
            float centerW2 = (a2 * centerX + b2 * centerY + c2) * invArea;
            float centerDivW1 =
                (stwHintMask & 0x8)
                    ? (centerW0 * a.tmu_oow[1] + centerW1 * b.tmu_oow[1] +
                       centerW2 * c.tmu_oow[1])
                    : (centerW0 * a.pos[3] + centerW1 * b.pos[3] +
                       centerW2 * c.pos[3]);
            float centerInvW1 = 1.0f / centerDivW1;
            float centerTrueS1 =
                (centerW0 * a.tex[2] + centerW1 * b.tex[2] +
                 centerW2 * c.tex[2]) *
                centerInvW1;
            float centerTrueT1 =
                (centerW0 * a.tex[3] + centerW1 * b.tex[3] +
                 centerW2 * c.tex[3]) *
                centerInvW1;

            float du_dx1 = (dSp1_dx - centerTrueS1 * dQ1_dx) * centerInvW1 *
                           scaleU1;
            float du_dy1 = (dSp1_dy - centerTrueS1 * dQ1_dy) * centerInvW1 *
                           scaleU1;
            float dv_dx1 = (dTp1_dx - centerTrueT1 * dQ1_dx) * centerInvW1 *
                           scaleV1;
            float dv_dy1 = (dTp1_dy - centerTrueT1 * dQ1_dy) * centerInvW1 *
                           scaleV1;

            float d1 = std::max(du_dx1 * du_dx1 + dv_dx1 * dv_dx1,
                                du_dy1 * du_dy1 + dv_dy1 * dv_dy1);
            float rawLod1 = (d1 > 1e-8f) ? 0.5f * fast_log2(d1) : -10.0f;
            detailFactor1 = std::clamp(
                (static_cast<float>(state.texDetailBias[1]) - rawLod1) *
                    detailScaleMul1,
                0.0f, state.texDetailMax[1]);
            float lod1 = rawLod1 + state.texLodBias[1];

            const int maxMipIdx1 =
                static_cast<int>(tex1->swizzledMipLevels.size()) - 1;
            float clampedLod1 =
                std::clamp(lod1, 0.0f, static_cast<float>(maxMipIdx1));
            lodFrac1 = clampedLod1 - std::floor(clampedLod1);

            if (!mipmappingEnabled1) {
              tmu1Color = SampleTextureLevelSIMD(
                  tex1Mip0Pixels, texW1, texH1, texW1 - 1u, texH1 - 1u,
                  tex1IsPow2W, tex1WShift, trueS1 * Simd8f(scaleU1),
                  trueT1 * Simd8f(scaleV1), clampS1, clampT1, minFilter1,
                  tex1ChromaEnabled, 1u, state);
            } else if (lodBlend1) {
              int level0 = static_cast<int>(std::floor(clampedLod1));
              int lodIdx0 = std::clamp(level0, 0, maxMipIdx1);
              int lodIdx1 = std::clamp(level0 + 1, 0, maxMipIdx1);
              float lodFraction = lodFrac1;

              TmuColorSIMD c0_m = SampleMipLevelSIMD(
                  tex1, lodIdx0, trueS1, trueT1, scaleU1, scaleV1, clampS1,
                  clampT1, minFilter1, tex1ChromaEnabled, 1u, state);
              TmuColorSIMD c1_m = SampleMipLevelSIMD(
                  tex1, lodIdx1, trueS1, trueT1, scaleU1, scaleV1, clampS1,
                  clampT1, minFilter1, tex1ChromaEnabled, 1u, state);

              Simd8f frac(lodFraction);
              tmu1Color.r = Fma(c1_m.r - c0_m.r, frac, c0_m.r);
              tmu1Color.g = Fma(c1_m.g - c0_m.g, frac, c0_m.g);
              tmu1Color.b = Fma(c1_m.b - c0_m.b, frac, c0_m.b);
              tmu1Color.a = Fma(c1_m.a - c0_m.a, frac, c0_m.a);
            } else {
              int lodIdx = std::clamp(
                  static_cast<int>(clampedLod1 + 0.5f), 0, maxMipIdx1);
              tmu1Color = SampleMipLevelSIMD(
                  tex1, lodIdx, trueS1, trueT1, scaleU1, scaleV1, clampS1,
                  clampT1, minFilter1, tex1ChromaEnabled, 1u, state);
            }
          }
        }

        if (tmu1Passthrough) {
          tmu1Out = tmu1Color;
        } else {
          tmu1Out = EvaluateTmuStageSIMD(
              state.texCombinerRgbFunc[1], state.texCombinerRgbFactor[1],
              state.texCombinerAlphaFunc[1], state.texCombinerAlphaFactor[1],
              state.texCombinerRgbInvert[1], state.texCombinerAlphaInvert[1],
              tmu1Color, iterColorVec, iterColorVec, Simd8f(detailFactor1),
              Simd8f(lodFrac1));
        }
      }
      if ((extTexColorCombine1 & 0x80000000u) != 0u) {
        EvaluateExtTmuRgbSIMD(extTexColorCombine1, tmu1Color, iterColorVec,
                              tmuConst1_vec, iterColorVec,
                              Simd8f(detailFactor1), Simd8f(lodFrac1),
                              tmu1Out.r, tmu1Out.g, tmu1Out.b);
      }
      if ((extTexAlphaCombine1 & 0x80000000u) != 0u) {
        tmu1Out.a = EvaluateExtTmuAlphaSIMD(
            extTexAlphaCombine1, tmu1Color, iterColorVec, tmuConst1_vec,
            iterColorVec, Simd8f(detailFactor1), Simd8f(lodFrac1));
      }

      // --- TMU 0 (Downstream) ---
      TmuColorSIMD tmu0Color = {one_vec, one_vec, one_vec, one_vec};
      float detailFactor0 = 0.0f;
      float lodFrac0 = 0.0f;
      if constexpr (HasTexture) {
        if (tex) {
          Simd8f texS = Fma(w0_vec, a_s0, Fma(w1_vec, b_s0, w2_vec * c_s0));
          Simd8f texT = Fma(w0_vec, a_t0, Fma(w1_vec, b_t0, w2_vec * c_t0));

          Simd8f invW = one_vec / divW0;
          Simd8f trueS = texS * invW;
          Simd8f trueT = texT * invW;

          if (!needLod0) {
            tmu0Color = SampleTextureLevelSIMD(
                tex0Mip0Pixels, texW, texH, texW - 1u, texH - 1u, tex0IsPow2W,
                tex0WShift, trueS * scaleU0_vec, trueT * scaleV0_vec, clampS,
                clampT, minFilter, tex0ChromaEnabled, 0u, state);
          } else {
            float centerX = static_cast<float>(x) + 1.5f;
            float centerY = static_cast<float>(y) + 0.5f;
            float centerW0 = (a0 * centerX + b0 * centerY + c0) * invArea;
            float centerW1 = (a1 * centerX + b1 * centerY + c1) * invArea;
            float centerW2 = (a2 * centerX + b2 * centerY + c2) * invArea;
            float centerDivW0 =
                (stwHintMask & 0x2)
                    ? (centerW0 * a.tmu_oow[0] + centerW1 * b.tmu_oow[0] +
                       centerW2 * c.tmu_oow[0])
                    : (centerW0 * a.pos[3] + centerW1 * b.pos[3] +
                       centerW2 * c.pos[3]);
            float centerInvW = 1.0f / centerDivW0;
            float centerTrueS =
                (centerW0 * a.tex[0] + centerW1 * b.tex[0] +
                 centerW2 * c.tex[0]) *
                centerInvW;
            float centerTrueT =
                (centerW0 * a.tex[1] + centerW1 * b.tex[1] +
                 centerW2 * c.tex[1]) *
                centerInvW;

            float du_dx =
                (dSp_dx - centerTrueS * dQ_dx) * centerInvW * scaleU;
            float du_dy =
                (dSp_dy - centerTrueS * dQ_dy) * centerInvW * scaleU;
            float dv_dx =
                (dTp_dx - centerTrueT * dQ_dx) * centerInvW * scaleV;
            float dv_dy =
                (dTp_dy - centerTrueT * dQ_dy) * centerInvW * scaleV;

            float d = std::max(du_dx * du_dx + dv_dx * dv_dx,
                               du_dy * du_dy + dv_dy * dv_dy);
            float rawLod = (d > 1e-8f) ? 0.5f * fast_log2(d) : -10.0f;
            detailFactor0 = std::clamp(
                (static_cast<float>(state.texDetailBias[0]) - rawLod) *
                    detailScaleMul0,
                0.0f, state.texDetailMax[0]);
            float lod = rawLod + state.texLodBias[0];

            const int maxMipIdx =
                static_cast<int>(tex->swizzledMipLevels.size()) - 1;
            float clampedLod =
                std::clamp(lod, 0.0f, static_cast<float>(maxMipIdx));
            lodFrac0 = clampedLod - std::floor(clampedLod);

            if (!mipmappingEnabled) {
              tmu0Color = SampleTextureLevelSIMD(
                  tex0Mip0Pixels, texW, texH, texW - 1u, texH - 1u, tex0IsPow2W,
                  tex0WShift, trueS * scaleU0_vec, trueT * scaleV0_vec, clampS,
                  clampT, minFilter, tex0ChromaEnabled, 0u, state);
            } else if (lodBlend) {
              int level0 = static_cast<int>(std::floor(clampedLod));
              int lodIdx0 = std::clamp(level0, 0, maxMipIdx);
              int lodIdx1 = std::clamp(level0 + 1, 0, maxMipIdx);
              float lodFraction = lodFrac0;

              TmuColorSIMD c0_m = SampleMipLevelSIMD(
                  tex, lodIdx0, trueS, trueT, scaleU, scaleV, clampS, clampT,
                  minFilter, tex0ChromaEnabled, 0u, state);
              TmuColorSIMD c1_m = SampleMipLevelSIMD(
                  tex, lodIdx1, trueS, trueT, scaleU, scaleV, clampS, clampT,
                  minFilter, tex0ChromaEnabled, 0u, state);

              Simd8f frac(lodFraction);
              tmu0Color.r = Fma(c1_m.r - c0_m.r, frac, c0_m.r);
              tmu0Color.g = Fma(c1_m.g - c0_m.g, frac, c0_m.g);
              tmu0Color.b = Fma(c1_m.b - c0_m.b, frac, c0_m.b);
              tmu0Color.a = Fma(c1_m.a - c0_m.a, frac, c0_m.a);
            } else {
              int lodIdx = std::clamp(
                  static_cast<int>(clampedLod + 0.5f), 0, maxMipIdx);
              tmu0Color = SampleMipLevelSIMD(
                  tex, lodIdx, trueS, trueT, scaleU, scaleV, clampS, clampT,
                  minFilter, tex0ChromaEnabled, 0u, state);
            }
          }
        }

        if (tmu0Passthrough) {
          tmu0Out = tmu0Color;
        } else {
          tmu0Out = EvaluateTmuStageSIMD(
              state.texCombinerRgbFunc[0], state.texCombinerRgbFactor[0],
              state.texCombinerAlphaFunc[0], state.texCombinerAlphaFactor[0],
              state.texCombinerRgbInvert[0], state.texCombinerAlphaInvert[0],
              tmu0Color, tmu1Out, iterColorVec, Simd8f(detailFactor0),
              Simd8f(lodFrac0));
        }
      } else {
        tmu0Out = tmu1Out;
      }
      if ((extTexColorCombine0 & 0x80000000u) != 0u) {
        EvaluateExtTmuRgbSIMD(extTexColorCombine0, tmu0Color, tmu1Out,
                              tmuConst0_vec, iterColorVec,
                              Simd8f(detailFactor0), Simd8f(lodFrac0),
                              tmu0Out.r, tmu0Out.g, tmu0Out.b);
      }
      if ((extTexAlphaCombine0 & 0x80000000u) != 0u) {
        tmu0Out.a = EvaluateExtTmuAlphaSIMD(
            extTexAlphaCombine0, tmu0Color, tmu1Out, tmuConst0_vec,
            iterColorVec, Simd8f(detailFactor0), Simd8f(lodFrac0));
      }

      // 6. Alpha & Color Combiner Stage
      const Simd8f texR = tmu0Out.r;
      const Simd8f texG = tmu0Out.g;
      const Simd8f texB = tmu0Out.b;
      const Simd8f texA = tmu0Out.a;

      Simd8f red, green, blue, alpha;
      Simd8f aLocal_fbi = (alphaCombinerLocal == 0) ? iterA : constA_vec;
      Simd8f aOther_fbi =
          (alphaCombinerOther == 0)
              ? iterA
              : ((alphaCombinerOther == 1) ? texA : constA_vec);

      // Fast paths for dominant Alpha Combiner configurations
      if ((extAlphaCombine & 0x80000000u) != 0u) {
        alpha = EvaluateExtFbiAlphaSIMD(extAlphaCombine, iterA, constA_vec,
                                        texA, aLocal_fbi, aOther_fbi);
      } else if (acFastPath == 1) {
        alpha = aLocal_fbi;
      } else if (acFastPath == 2) {
        alpha = texA;
      } else if (acFastPath == 3) {
        alpha = texA * aLocal_fbi;
      } else {
        Simd8f factA_comb = zero_vec;
        switch (alphaCombinerFactor) {
          case 1:
          case 3:
            factA_comb = aLocal_fbi;
            break;
          case 2:
            factA_comb = aOther_fbi;
            break;
          case 4:
            factA_comb = texA;
            break;
          case 8:
            factA_comb = one_vec;
            break;
          case 9:
          case 11:
            factA_comb = one_vec - aLocal_fbi;
            break;
          case 10:
            factA_comb = one_vec - aOther_fbi;
            break;
          case 12:
            factA_comb = one_vec - texA;
            break;
        }

        alpha = zero_vec;
        switch (alphaCombinerFunc) {
          case 1:
            alpha = aLocal_fbi;
            break;
          case 3:
            alpha = aOther_fbi * factA_comb;
            break;
          case 4:
          case 5:
            alpha = Fma(aOther_fbi, factA_comb, aLocal_fbi);
            break;
          case 6:
            alpha = (aOther_fbi - aLocal_fbi) * factA_comb;
            break;
          case 7:
          case 8:
            alpha = Fma(aOther_fbi - aLocal_fbi, factA_comb, aLocal_fbi);
            break;
          case 9:
          case 16:
            alpha = aLocal_fbi * (one_vec - factA_comb);
            break;
        }
        if (alphaCombinerInvert) alpha = one_vec - alpha;
      }

      // Fast paths for dominant Color Combiner configurations
      if ((extColorCombine & 0x80000000u) != 0u) {
        EvaluateExtFbiColorSIMD(extColorCombine, iterColorVec, constColorVec,
                                tmu0Out, aLocal_fbi, aOther_fbi, red, green,
                                blue);
      } else if (ccFastPath == 1) {
        red = texR;
        green = texG;
        blue = texB;
      } else if (ccFastPath == 2) {
        const Simd8f cLocalR = (colorCombinerLocal == 0) ? iterR : constR_vec;
        const Simd8f cLocalG = (colorCombinerLocal == 0) ? iterG : constG_vec;
        const Simd8f cLocalB = (colorCombinerLocal == 0) ? iterB : constB_vec;
        red = texR * cLocalR;
        green = texG * cLocalG;
        blue = texB * cLocalB;
      } else if (ccFastPath == 3) {
        red = (colorCombinerLocal == 0) ? iterR : constR_vec;
        green = (colorCombinerLocal == 0) ? iterG : constG_vec;
        blue = (colorCombinerLocal == 0) ? iterB : constB_vec;
      } else {
        Simd8f cLocalR_comb = (colorCombinerLocal == 0) ? iterR : constR_vec;
        Simd8f cLocalG_comb = (colorCombinerLocal == 0) ? iterG : constG_vec;
        Simd8f cLocalB_comb = (colorCombinerLocal == 0) ? iterB : constB_vec;

        Simd8f cOtherR_comb =
            (colorCombinerOther == 0)
                ? iterR
                : ((colorCombinerOther == 1) ? texR : constR_vec);
        Simd8f cOtherG_comb =
            (colorCombinerOther == 0)
                ? iterG
                : ((colorCombinerOther == 1) ? texG : constG_vec);
        Simd8f cOtherB_comb =
            (colorCombinerOther == 0)
                ? iterB
                : ((colorCombinerOther == 1) ? texB : constB_vec);

        Simd8f factR_comb = zero_vec, factG_comb = zero_vec,
               factB_comb = zero_vec;
        switch (colorCombinerFactor) {
          case 1:
            factR_comb = cLocalR_comb;
            factG_comb = cLocalG_comb;
            factB_comb = cLocalB_comb;
            break;
          case 2:
            factR_comb = aOther_fbi;
            factG_comb = aOther_fbi;
            factB_comb = aOther_fbi;
            break;
          case 3:
            factR_comb = aLocal_fbi;
            factG_comb = aLocal_fbi;
            factB_comb = aLocal_fbi;
            break;
          case 4:
            factR_comb = texA;
            factG_comb = texA;
            factB_comb = texA;
            break;
          case 5:
            factR_comb = texR;
            factG_comb = texG;
            factB_comb = texB;
            break;
          case 8:
            factR_comb = one_vec;
            factG_comb = one_vec;
            factB_comb = one_vec;
            break;
          case 9:
            factR_comb = one_vec - cLocalR_comb;
            factG_comb = one_vec - cLocalG_comb;
            factB_comb = one_vec - cLocalB_comb;
            break;
          case 10:
            factR_comb = one_vec - aOther_fbi;
            factG_comb = one_vec - aOther_fbi;
            factB_comb = one_vec - aOther_fbi;
            break;
          case 11:
            factR_comb = one_vec - aLocal_fbi;
            factG_comb = one_vec - aLocal_fbi;
            factB_comb = one_vec - aLocal_fbi;
            break;
          case 12:
            factR_comb = one_vec - texA;
            factG_comb = one_vec - texA;
            factB_comb = one_vec - texA;
            break;
        }

        red = zero_vec;
        green = zero_vec;
        blue = zero_vec;
        switch (colorCombinerFunc) {
          case 1:
            red = cLocalR_comb;
            green = cLocalG_comb;
            blue = cLocalB_comb;
            break;
          case 3:
            red = cOtherR_comb * factR_comb;
            green = cOtherG_comb * factG_comb;
            blue = cOtherB_comb * factB_comb;
            break;
          case 4:
            red = Fma(cOtherR_comb, factR_comb, cLocalR_comb);
            green = Fma(cOtherG_comb, factG_comb, cLocalG_comb);
            blue = Fma(cOtherB_comb, factB_comb, cLocalB_comb);
            break;
          case 5:
            red = Fma(cOtherR_comb, factR_comb, aLocal_fbi);
            green = Fma(cOtherG_comb, factG_comb, aLocal_fbi);
            blue = Fma(cOtherB_comb, factB_comb, aLocal_fbi);
            break;
          case 6:
            red = (cOtherR_comb - cLocalR_comb) * factR_comb;
            green = (cOtherG_comb - cLocalG_comb) * factG_comb;
            blue = (cOtherB_comb - cLocalB_comb) * factB_comb;
            break;
          case 7:
            red = Fma(cOtherR_comb - cLocalR_comb, factR_comb, cLocalR_comb);
            green = Fma(cOtherG_comb - cLocalG_comb, factG_comb, cLocalG_comb);
            blue = Fma(cOtherB_comb - cLocalB_comb, factB_comb, cLocalB_comb);
            break;
          case 8:
            red = Fma(cOtherR_comb - cLocalR_comb, factR_comb, aLocal_fbi);
            green = Fma(cOtherG_comb - cLocalG_comb, factG_comb, aLocal_fbi);
            blue = Fma(cOtherB_comb - cLocalB_comb, factB_comb, aLocal_fbi);
            break;
          case 9:
            red = cLocalR_comb * (one_vec - factR_comb);
            green = cLocalG_comb * (one_vec - factG_comb);
            blue = cLocalB_comb * (one_vec - factB_comb);
            break;
          case 16:
            red = aLocal_fbi - cLocalR_comb * factR_comb;
            green = aLocal_fbi - cLocalG_comb * factG_comb;
            blue = aLocal_fbi - cLocalB_comb * factB_comb;
            break;
        }
        if (colorCombinerInvert) {
          red = one_vec - red;
          green = one_vec - green;
          blue = one_vec - blue;
        }
      }

      red = Max(zero_vec, Min(one_vec, red));
      green = Max(zero_vec, Min(one_vec, green));
      blue = Max(zero_vec, Min(one_vec, blue));
      alpha = Max(zero_vec, Min(one_vec, alpha));

      // Preserve full 8-bit alpha into blending; RGB565 dithering runs
      // post-blend on color channels only (authentic 3dfx pipeline order).
      Simd8f blendAlphaVal = alpha;

      // 8. Chromakey Emulation
      if (chromakeyMode == 1) {
        const bool hasTex = HasTexture || hasTexture1;
        Simd8f testR = hasTex ? texR : red;
        Simd8f testG = hasTex ? texG : green;
        Simd8f testB = hasTex ? texB : blue;

        SimdMask chromaFail;
        const bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1u) == 1u;
        if (rangeEnabled) {
          SimdMask rMatch =
              (testR >= Simd8f(chromaMinR)) & (testR <= Simd8f(chromaMaxR));
          SimdMask gMatch =
              (testG >= Simd8f(chromaMinG)) & (testG <= Simd8f(chromaMaxG));
          SimdMask bMatch =
              (testB >= Simd8f(chromaMinB)) & (testB <= Simd8f(chromaMaxB));

          SimdMask rRes = ((chromakeyRangeMode >> 26) & 1) ? !rMatch : rMatch;
          SimdMask gRes = ((chromakeyRangeMode >> 25) & 1) ? !gMatch : gMatch;
          SimdMask bRes = ((chromakeyRangeMode >> 24) & 1) ? !bMatch : bMatch;

          if ((chromakeyRangeMode >> 27) & 1) {
            chromaFail = rRes | gRes | bRes;
          } else {
            chromaFail = rRes & gRes & bRes;
          }
        } else {
          chromaFail = (Abs(testR - Simd8f(chromaR)) < Simd8f(0.001f)) &
                       (Abs(testG - Simd8f(chromaG)) < Simd8f(0.001f)) &
                       (Abs(testB - Simd8f(chromaB)) < Simd8f(0.001f));
        }
        coverageMask = coverageMask & !chromaFail;
        if (coverageMask.AllZero()) continue;
      }

      // 9. Alpha Testing
      if (alphaTestOp != 7) {
        SimdMask alphaPass;
        Simd8f ref(alphaTestRef);
        switch (alphaTestOp) {
          case 0:
            alphaPass = SimdMask::False();
            break;
          case 1:
            alphaPass = alpha < ref;
            break;
          case 2:
            alphaPass = alpha == ref;
            break;
          case 3:
            alphaPass = alpha <= ref;
            break;
          case 4:
            alphaPass = alpha > ref;
            break;
          case 5:
            alphaPass = alpha != ref;
            break;
          case 6:
            alphaPass = alpha >= ref;
            break;
          default:
            alphaPass = SimdMask::True();
            break;
        }
        coverageMask = coverageMask & alphaPass;
        if (coverageMask.AllZero()) continue;
      }

      // 9.5. Hardware Stencil Testing (before Depth Test — fully vectorized)
      SimdMask stencilPassMask = coverageMask;
      Simd8i curStencil;
      uint8_t* stencilPtr = nullptr;
      bool stencilDirty = false;
      if (__builtin_expect(stencilEnabled && stencilBuffer != nullptr, 0)) {
        stencilPtr = &stencilBuffer[static_cast<size_t>(y) * width + x];
        curStencil = Simd8i::LoadStencil2x4(stencilPtr, width);
        const SimdMask sCmp = EvaluateStencilCompareVec<Simd8i, SimdMask>(
            stencilCompareOp, stencilMaskedRef_vec,
            curStencil & stencilReadMask_vec);
        stencilPassMask = coverageMask & sCmp;
        const SimdMask stencilFailMask = coverageMask & !sCmp;
        if (stencilWriteMask != 0u && stencilFailOp != 0u &&
            stencilFailMask.AnyTrue()) {
          const Simd8i failedVal = ApplyStencilOpVec(
              stencilFailOp, curStencil, stencilRef_vec, stencilWriteMask_vec,
              stencilInvWriteMask_vec);
          curStencil = Select(stencilFailMask, failedVal, curStencil);
          stencilDirty = true;
        }
        if (stencilPassMask.AllZero()) {
          if (stencilDirty) {
            Simd8i::StoreStencil2x4(stencilPtr, width, curStencil);
          }
          continue;
        }
        coverageMask = stencilPassMask;
      }

      // 10. Depth Testing (skipped completely when depthMode == 0)
      if (needDepthOp) {
        Simd8f targetDepth;
        if (depthMode == 2) {
          Simd8f w = one_vec / wVal;
          targetDepth = Max(
              zero_vec,
              Min(one_vec, (w - Simd8f(depthNear)) * Simd8f(depthRangeInv)));
        } else {
          targetDepth =
              Max(zero_vec, Min(one_vec, Fma(w2_vec, dc_z, Fma(w1_vec, db_z, a_z))));
        }
        if (depthBiasLevel != 0) {
          targetDepth = Max(
              zero_vec,
              Min(one_vec,
                  targetDepth +
                      Simd8f(static_cast<float>(depthBiasLevel) / 65535.0f)));
        }

        float* depthPtr = &depthBuffer[y * width + x];
        Simd8f currentDepth = Simd8f::Load2x4(depthPtr, width);
        if (depthCompareOp != 7) {
          SimdMask depthPass = SimdMask::True();
          switch (depthCompareOp) {
            case 0:
              depthPass = SimdMask::False();
              break;
            case 1:
              depthPass = targetDepth < currentDepth;
              break;
            case 2:
              depthPass = targetDepth == currentDepth;
              break;
            case 3:
              depthPass = targetDepth <= currentDepth;
              break;
            case 4:
              depthPass = targetDepth > currentDepth;
              break;
            case 5:
              depthPass = targetDepth != currentDepth;
              break;
            case 6:
              depthPass = targetDepth >= currentDepth;
              break;
          }
          coverageMask = coverageMask & depthPass;
          if (!stencilEnabled && coverageMask.AllZero()) continue;
        }

        if (depthMask && !coverageMask.AllZero()) {
          Simd8f newDepth = Select(coverageMask, targetDepth, currentDepth);
          Simd8f::Store2x4(depthPtr, width, newDepth);
        }
      }

      // 10.5. Hardware Stencil ZFail / ZPass updates (fully vectorized)
      if (__builtin_expect(stencilEnabled && stencilBuffer != nullptr, 0)) {
        const SimdMask zPassMask = coverageMask;
        const SimdMask zFailMask = stencilPassMask & !zPassMask;
        if (stencilWriteMask != 0u) {
          if (stencilZFailOp != 0u && zFailMask.AnyTrue()) {
            const Simd8i zfVal = ApplyStencilOpVec(
                stencilZFailOp, curStencil, stencilRef_vec,
                stencilWriteMask_vec, stencilInvWriteMask_vec);
            curStencil = Select(zFailMask, zfVal, curStencil);
            stencilDirty = true;
          }
          if (stencilZPassOp != 0u && zPassMask.AnyTrue()) {
            const Simd8i zpVal = ApplyStencilOpVec(
                stencilZPassOp, curStencil, stencilRef_vec,
                stencilWriteMask_vec, stencilInvWriteMask_vec);
            curStencil = Select(zPassMask, zpVal, curStencil);
            stencilDirty = true;
          }
        }
        if (stencilDirty) {
          Simd8i::StoreStencil2x4(stencilPtr, width, curStencil);
        }
        if (zPassMask.AllZero()) continue;
      }

      // Capture pre-fog color for GR_BLEND_PREFOG_COLOR (multi-pass fog)
      const Simd8f preFogR = red;
      const Simd8f preFogG = green;
      const Simd8f preFogB = blue;

      // 11. Fog Stage (fully vectorized table fog — 0-mismatch index vs scalar)
      if (fogMode != 0) {
        Simd8f f_vec = zero_vec;
        uint32_t fogSource = fogMode & 0x0F;
        if (fogSource == 4) {
          f_vec = iterA;
        } else {
          f_vec = EvaluateTableFogVec<Simd8f, Simd8i, SimdMask>(wVal, fogTableF);
        }

        Simd8f srcScale =
            ((fogMode & 0x100) != 0) ? zero_vec : (one_vec - f_vec);
        Simd8f fogScale = ((fogMode & 0x200) != 0) ? zero_vec : f_vec;

        red = Fma(red, srcScale, Simd8f(fogR) * fogScale);
        green = Fma(green, srcScale, Simd8f(fogG) * fogScale);
        blue = Fma(blue, srcScale, Simd8f(fogB) * fogScale);
      }

      // 12. Framebuffer Write & Alpha Blending
      uint32_t* pixPtr = &pixels[y * width + x];
      Simd8f blendedR = red;
      Simd8f blendedG = green;
      Simd8f blendedB = blue;
      Simd8f blendedA = blendAlphaVal;
      Simd8i dstWord;

      if constexpr (BlendEnabled) {
        dstWord = Simd8i::Load2x4(pixPtr, width);
        Simd8f dstR = Simd8f::Convert((dstWord >> 16) & maskFF_i) * inv255_vec;
        Simd8f dstG = Simd8f::Convert((dstWord >> 8) & maskFF_i) * inv255_vec;
        Simd8f dstB = Simd8f::Convert(dstWord & maskFF_i) * inv255_vec;

        if (blendFastPath == 1) {
          Simd8f omA = one_vec - blendAlphaVal;
          blendedR = Min(one_vec, Fma(red, blendAlphaVal, dstR * omA));
          blendedG = Min(one_vec, Fma(green, blendAlphaVal, dstG * omA));
          blendedB = Min(one_vec, Fma(blue, blendAlphaVal, dstB * omA));
          blendedA = Min(one_vec, blendAlphaVal);
        } else if (blendFastPath == 2) {
          blendedR = Min(one_vec, red * dstR);
          blendedG = Min(one_vec, green * dstG);
          blendedB = Min(one_vec, blue * dstB);
          blendedA = Min(one_vec, blendAlphaVal);
        } else if (blendFastPath == 3) {
          blendedR = Min(one_vec, red + dstR);
          blendedG = Min(one_vec, green + dstG);
          blendedB = Min(one_vec, blue + dstB);
          blendedA = Min(one_vec, blendAlphaVal);
        } else {
          Simd8f dstA = blendNeedsDstA
                            ? (Simd8f::Convert((dstWord >> 24) & maskFF_i) *
                               inv255_vec)
                            : zero_vec;
          blendedR = ApplyBlendOpSIMD(
              rgbBlendOp,
              red * GetBlendFactorSIMD(rgbSrcBlend, red, preFogR, dstR,
                                       blendAlphaVal, dstA, false),
              dstR * GetBlendFactorSIMD(rgbDstBlend, red, preFogR, dstR,
                                        blendAlphaVal, dstA, true));
          blendedG = ApplyBlendOpSIMD(
              rgbBlendOp,
              green * GetBlendFactorSIMD(rgbSrcBlend, green, preFogG, dstG,
                                         blendAlphaVal, dstA, false),
              dstG * GetBlendFactorSIMD(rgbDstBlend, green, preFogG, dstG,
                                        blendAlphaVal, dstA, true));
          blendedB = ApplyBlendOpSIMD(
              rgbBlendOp,
              blue * GetBlendFactorSIMD(rgbSrcBlend, blue, preFogB, dstB,
                                        blendAlphaVal, dstA, false),
              dstB * GetBlendFactorSIMD(rgbDstBlend, blue, preFogB, dstB,
                                        blendAlphaVal, dstA, true));
          blendedA = ApplyBlendOpSIMD(
              alphaBlendOp,
              blendAlphaVal *
                  GetBlendFactorSIMD(alphaSrcBlend, blendAlphaVal,
                                     blendAlphaVal, dstA, blendAlphaVal, dstA,
                                     false),
              dstA * GetBlendFactorSIMD(alphaDstBlend, blendAlphaVal,
                                        blendAlphaVal, dstA, blendAlphaVal,
                                        dstA, true));
        }
      }

      Simd8i rVal = Simd8i::Convert(
          Max(zero_vec, Min(c255_vec, Fma(blendedR, c255_vec, half_vec))));
      Simd8i gVal = Simd8i::Convert(
          Max(zero_vec, Min(c255_vec, Fma(blendedG, c255_vec, half_vec))));
      Simd8i bVal = Simd8i::Convert(
          Max(zero_vec, Min(c255_vec, Fma(blendedB, c255_vec, half_vec))));
      Simd8i aVal = Simd8i::Convert(
          Max(zero_vec, Min(c255_vec, Fma(blendedA, c255_vec, half_vec))));

      if constexpr (DitherEnabled) {
        Simd8i ditheredR =
            ((rVal << 1) - (rVal >> 4) + (rVal >> 7) + rowDith) >> 1;
        Simd8i r = Min(Simd8i(31), ditheredR >> 3);

        Simd8i ditheredG =
            ((gVal << 2) - (gVal >> 4) + (gVal >> 6) + rowDith) >> 2;
        Simd8i g = Min(Simd8i(63), ditheredG >> 2);

        Simd8i ditheredB =
            ((bVal << 1) - (bVal >> 4) + (bVal >> 7) + rowDith) >> 1;
        Simd8i b = Min(Simd8i(31), ditheredB >> 3);

        rVal = (r << 3) | (r >> 2);
        gVal = (g << 2) | (g >> 4);
        bVal = (b << 3) | (b >> 2);
      }

      Simd8i finalColor = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;

      if constexpr (!BlendEnabled) {
        if (fullColorMask && coverageMask.AllTrue()) {
          Simd8i::Store2x4(pixPtr, width, finalColor);
          continue;
        }
        dstWord = Simd8i::Load2x4(pixPtr, width);
      }

      if (!fullColorMask) {
        finalColor = (finalColor & colorMask_vec) | (dstWord & invColorMask_vec);
      }

      Simd8i writeColor = Select(coverageMask, finalColor, dstWord);
      Simd8i::Store2x4(pixPtr, width, writeColor);
    }
  }
}

// Force template instantiations for all compilation units to see them
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, false, false, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, false, false, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, false, true, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, false, true, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, true, false, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, true, false, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, true, true, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<false, true, true, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, false, false, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, false, false, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, false, true, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, false, true, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, true, false, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, true, false, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, true, true, true>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);
template void
SoftwareBackend::RasterizeTriangleLoopsSIMD<true, true, true, false>(
    const ModernVertex&, const ModernVertex&, const ModernVertex&, int, int,
    int, int, const RasterizerState&, float, float, float, float, float, float,
    float, float, float, float);

}  // namespace GlideWrapper
