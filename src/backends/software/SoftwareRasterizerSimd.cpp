#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends/software/SimdVec.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

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
  const int width = m_headlessWidth;

  // Cache state variables
  const uint32_t stippleMode = state.stippleMode;
  const uint32_t stipplePattern = state.stipplePattern;
  const uint32_t depthMode = state.depthMode;
  const uint32_t depthCompareOp = state.depthCompareOp;
  const bool depthMask = state.depthMask;
  const int depthBiasLevel = state.depthBiasLevel;
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
  const bool colorMaskRgb = state.colorMaskRgb;
  const bool colorMaskAlpha = state.colorMaskAlpha;
  const uint32_t rgbSrcBlend = state.rgbSrcBlend;
  const uint32_t rgbDstBlend = state.rgbDstBlend;
  const uint32_t alphaSrcBlend = state.alphaSrcBlend;
  const uint32_t alphaDstBlend = state.alphaDstBlend;
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
  uint32_t mipmapMode = 0;
  bool lodBlend = false;
  const VirtualTexture* tex = nullptr;

  if constexpr (HasTexture) {
    tex = TextureManager::GetInstance().GetTexture(0, state.boundTexAddress[0]);
    if (tex && !tex->swizzledMipLevels.empty()) {
      texW = tex->baseWidth;
      texH = tex->baseHeight;
      clampS = state.texClampS[0];
      clampT = state.texClampT[0];
      minFilter = state.texMinFilter[0];
      float maxDim = static_cast<float>(std::max(texW, texH));
      scaleU = maxDim / 256.0f;
      scaleV = maxDim / 256.0f;
      mipmapMode = state.texMipMapMode[0];
      lodBlend = state.texLodBlend[0];
      mipmappingEnabled = (mipmapMode != 0);
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
  uint32_t mipmapMode1 = 0;
  bool lodBlend1 = false;
  const VirtualTexture* tex1 = nullptr;

  if (hasTexture1) {
    tex1 =
        TextureManager::GetInstance().GetTexture(1, state.boundTexAddress[1]);
    if (tex1 && !tex1->swizzledMipLevels.empty()) {
      texW1 = tex1->baseWidth;
      texH1 = tex1->baseHeight;
      clampS1 = state.texClampS[1];
      clampT1 = state.texClampT[1];
      minFilter1 = state.texMinFilter[1];
      float maxDim1 = static_cast<float>(std::max(texW1, texH1));
      scaleU1 = maxDim1 / 256.0f;
      scaleV1 = maxDim1 / 256.0f;
      mipmapMode1 = state.texMipMapMode[1];
      lodBlend1 = state.texLodBlend[1];
      mipmappingEnabled1 = (mipmapMode1 != 0);
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

  bool needBarycentricDerivatives = mipmappingEnabled || mipmappingEnabled1;
  if (needBarycentricDerivatives) {
    dw0_dx = a0 * invArea;
    dw0_dy = b0 * invArea;
    dw1_dx = a1 * invArea;
    dw1_dy = b1 * invArea;
    dw2_dx = a2 * invArea;
    dw2_dy = b2 * invArea;
  }

  if (mipmappingEnabled) {
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

  if (mipmappingEnabled1) {
    dSp1_dx = dw0_dx * a.tex[2] + dw1_dx * b.tex[2] + dw2_dx * c.tex[2];
    dSp1_dy = dw0_dy * a.tex[2] + dw1_dy * b.tex[2] + dw2_dy * c.tex[2];
    dTp1_dx = dw0_dx * a.tex[3] + dw1_dx * b.tex[3] + dw2_dx * c.tex[3];
    dTp1_dy = dw0_dy * a.tex[3] + dw1_dy * b.tex[3] + dw2_dy * c.tex[3];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (stwHintMask & 0x4) {
      qa = a.tmu_oow[1];
      qb = b.tmu_oow[1];
      qc = c.tmu_oow[1];
    }
    dQ1_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ1_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  int startX = minX & ~3;
  int startY = minY & ~1;

  auto getFSIMD = [](uint32_t mode, const Simd8f& srcC, const Simd8f& preFogC,
                     const Simd8f& dstC, const Simd8f& srcA, const Simd8f& dstA,
                     bool isDest) -> Simd8f {
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
      case 15:
        return isDest ? preFogC : Min(srcA, Simd8f(1.0f) - dstA);
      default:
        return Simd8f(1.0f);
    }
  };

  auto mirrorCoordSIMD = [](const Simd8i& coord, uint32_t size) -> Simd8i {
    Simd8i doubleSize(static_cast<int>(size * 2));
    Simd8i mask = doubleSize - Simd8i(1);
    Simd8i wrapped = coord & mask;
    Simd8i limit(static_cast<int>(size));
    SimdMask cond = wrapped < limit;
    return Select(cond, wrapped, mask - wrapped);
  };

  auto applyTexChromaSIMD = [&](const Simd8i& color, uint32_t tmu) -> Simd8i {
    if (state.texChromaMode[tmu] != 1) return color;

    Simd8i r = (color >> 16) & Simd8i(0xFF);
    Simd8i g = (color >> 8) & Simd8i(0xFF);
    Simd8i b = color & Simd8i(0xFF);

    bool rangeEnabled = ((state.texChromaRangeMode[tmu] >> 28) & 1) == 1;
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

      bool blueExcl = ((state.texChromaRangeMode[tmu] >> 24) & 1) == 1;
      bool greenExcl = ((state.texChromaRangeMode[tmu] >> 25) & 1) == 1;
      bool redExcl = ((state.texChromaRangeMode[tmu] >> 26) & 1) == 1;

      SimdMask rRes = redExcl ? !rMatch : rMatch;
      SimdMask gRes = greenExcl ? !gMatch : gMatch;
      SimdMask bRes = blueExcl ? !bMatch : bMatch;

      bool unionMode = ((state.texChromaRangeMode[tmu] >> 27) & 1) == 1;
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
  };

  auto getChannelFloat = [](const Simd8i& color, int shift) -> Simd8f {
    return Simd8f::Convert((color >> shift) & Simd8i(0xFF));
  };

  for (int y = startY; y <= maxY; y += 2) {
    for (int x = startX; x <= maxX; x += 4) {
      // 1. Coordinate Setup
      Simd8f X_vec(static_cast<float>(x) + 0.5f, static_cast<float>(x) + 1.5f,
                   static_cast<float>(x) + 2.5f, static_cast<float>(x) + 3.5f,
                   static_cast<float>(x) + 0.5f, static_cast<float>(x) + 1.5f,
                   static_cast<float>(x) + 2.5f, static_cast<float>(x) + 3.5f);
      Simd8f Y_vec(static_cast<float>(y) + 0.5f, static_cast<float>(y) + 0.5f,
                   static_cast<float>(y) + 0.5f, static_cast<float>(y) + 0.5f,
                   static_cast<float>(y) + 1.5f, static_cast<float>(y) + 1.5f,
                   static_cast<float>(y) + 1.5f, static_cast<float>(y) + 1.5f);

      // 2. Edge Equations
      Simd8f E0 = Fma(Simd8f(a0), X_vec, Fma(Simd8f(b0), Y_vec, Simd8f(c0)));
      Simd8f E1 = Fma(Simd8f(a1), X_vec, Fma(Simd8f(b1), Y_vec, Simd8f(c1)));
      Simd8f E2 = Fma(Simd8f(a2), X_vec, Fma(Simd8f(b2), Y_vec, Simd8f(c2)));

      SimdMask insideMask;
      if constexpr (Clockwise) {
        insideMask = (E0 >= Simd8f(-1e-5f)) & (E1 >= Simd8f(-1e-5f)) &
                     (E2 >= Simd8f(-1e-5f));
      } else {
        insideMask = (E0 <= Simd8f(1e-5f)) & (E1 <= Simd8f(1e-5f)) &
                     (E2 <= Simd8f(1e-5f));
      }

      SimdMask boundaryMask =
          (X_vec >= Simd8f(static_cast<float>(minX))) &
          (X_vec <= Simd8f(static_cast<float>(maxX) + 1.0f)) &
          (Y_vec >= Simd8f(static_cast<float>(minY))) &
          (Y_vec <= Simd8f(static_cast<float>(maxY) + 1.0f));

      SimdMask coverageMask = insideMask & boundaryMask;
      if (coverageMask.AllZero()) continue;

      // 3. Barycentric & Perspective Correction
      Simd8f w0_vec = E0 * Simd8f(invArea);
      Simd8f w1_vec = E1 * Simd8f(invArea);
      Simd8f w2_vec = E2 * Simd8f(invArea);

      Simd8f wVal =
          Fma(w0_vec, Simd8f(a.pos[3]),
              Fma(w1_vec, Simd8f(b.pos[3]), w2_vec * Simd8f(c.pos[3])));

      SimdMask depthClipMask = SimdMask::True();
      if (depthMode == 2) {
        depthClipMask = wVal <= Simd8f(1.0001f);
      } else if constexpr (HasTexture) {
        depthClipMask = wVal > Simd8f(0.0f);
      }
      coverageMask = coverageMask & depthClipMask;
      if (coverageMask.AllZero()) continue;

      // 4. Stipple Transparency
      if (stippleMode != 0) {
        Simd8i stippleX;
        if (stippleMode == 2) {
          stippleX = (Simd8i::Convert(X_vec - Simd8f(0.5f)) +
                      Simd8i::Convert(Y_vec - Simd8f(0.5f))) &
                     Simd8i(7);
        } else {
          stippleX = Simd8i::Convert(X_vec - Simd8f(0.5f)) & Simd8i(7);
        }
        Simd8i stippleY = Simd8i::Convert(Y_vec - Simd8f(0.5f)) & Simd8i(3);
        Simd8i stippleIndex = (stippleY << 3) | (Simd8i(7) - stippleX);

        alignas(32) int32_t index_arr[8];
        Simd8i::Store(index_arr, stippleIndex);
        alignas(32) int32_t result_arr[8];
        for (int i = 0; i < 8; ++i) {
          result_arr[i] = (stipplePattern >> index_arr[i]) & 1;
        }
        Simd8i bit = Simd8i::Load(result_arr);

        coverageMask = coverageMask & (bit != Simd8i(0));
        if (coverageMask.AllZero()) continue;
      }

      // 5. Interpolate Attributes
      Simd8f iterR =
          Fma(w0_vec, Simd8f(a.color[0]),
              Fma(w1_vec, Simd8f(b.color[0]), w2_vec * Simd8f(c.color[0])));
      Simd8f iterG =
          Fma(w0_vec, Simd8f(a.color[1]),
              Fma(w1_vec, Simd8f(b.color[1]), w2_vec * Simd8f(c.color[1])));
      Simd8f iterB =
          Fma(w0_vec, Simd8f(a.color[2]),
              Fma(w1_vec, Simd8f(b.color[2]), w2_vec * Simd8f(c.color[2])));
      Simd8f iterA =
          Fma(w0_vec, Simd8f(a.color[3]),
              Fma(w1_vec, Simd8f(b.color[3]), w2_vec * Simd8f(c.color[3])));

      // 6. Texture Sampling Stage
      TmuColorSIMD tmu0Out = {iterR, iterG, iterB, iterA};

      // --- TMU 1 (Upstream) ---
      TmuColorSIMD tmu1Out = {iterR, iterG, iterB, iterA};
      if (hasTexture1) {
        Simd8f texS1 =
            Fma(w0_vec, Simd8f(a.tex[2]),
                Fma(w1_vec, Simd8f(b.tex[2]), w2_vec * Simd8f(c.tex[2])));
        Simd8f texT1 =
            Fma(w0_vec, Simd8f(a.tex[3]),
                Fma(w1_vec, Simd8f(b.tex[3]), w2_vec * Simd8f(c.tex[3])));

        Simd8f divW1 = wVal;
        if (stwHintMask & 0x4) {
          divW1 = Fma(
              w0_vec, Simd8f(a.tmu_oow[1]),
              Fma(w1_vec, Simd8f(b.tmu_oow[1]), w2_vec * Simd8f(c.tmu_oow[1])));
        }
        Simd8f invW1 = Simd8f(1.0f) / divW1;
        Simd8f trueS1 = texS1 * invW1;
        Simd8f trueT1 = texT1 * invW1;

        TmuColorSIMD tmu1Color = {Simd8f(1.0f), Simd8f(1.0f), Simd8f(1.0f),
                                  Simd8f(1.0f)};
        if (tex1) {
          float lod1 = 0.0f;
          if (mipmappingEnabled1) {
            float centerX = static_cast<float>(x) + 1.5f;
            float centerY = static_cast<float>(y) + 0.5f;
            float centerW0 = (a0 * centerX + b0 * centerY + c0) * invArea;
            float centerW1 = (a1 * centerX + b1 * centerY + c1) * invArea;
            float centerW2 = (a2 * centerX + b2 * centerY + c2) * invArea;
            float centerWVal =
                centerW0 * a.pos[3] + centerW1 * b.pos[3] + centerW2 * c.pos[3];
            float centerDivW1 = centerWVal;
            if (stwHintMask & 0x4) {
              centerDivW1 = centerW0 * a.tmu_oow[1] + centerW1 * b.tmu_oow[1] +
                            centerW2 * c.tmu_oow[1];
            }
            float centerInvW1 = 1.0f / centerDivW1;
            float centerTexS1 =
                centerW0 * a.tex[2] + centerW1 * b.tex[2] + centerW2 * c.tex[2];
            float centerTexT1 =
                centerW0 * a.tex[3] + centerW1 * b.tex[3] + centerW2 * c.tex[3];
            float centerTrueS1 = centerTexS1 * centerInvW1;
            float centerTrueT1 = centerTexT1 * centerInvW1;

            float ds_dx1 = (dSp1_dx - centerTrueS1 * dQ1_dx) * centerInvW1;
            float ds_dy1 = (dSp1_dy - centerTrueS1 * dQ1_dy) * centerInvW1;
            float dt_dx1 = (dTp1_dx - centerTrueT1 * dQ1_dx) * centerInvW1;
            float dt_dy1 = (dTp1_dy - centerTrueT1 * dQ1_dy) * centerInvW1;

            float du_dx1 = ds_dx1 * scaleU1;
            float du_dy1 = ds_dy1 * scaleU1;
            float dv_dx1 = dt_dx1 * scaleV1;
            float dv_dy1 = dt_dy1 * scaleV1;

            float d1 = std::max(du_dx1 * du_dx1 + dv_dx1 * dv_dx1,
                                du_dy1 * du_dy1 + dv_dy1 * dv_dy1);
            lod1 = (d1 > 1e-8f) ? 0.5f * fast_log2(d1) : -10.0f;
            lod1 += state.texLodBias[1];
          }

          const int maxMipIdx1 =
              static_cast<int>(tex1->swizzledMipLevels.size()) - 1;
          float clampedLod1 =
              std::clamp(lod1, 0.0f, static_cast<float>(maxMipIdx1));

          auto sampleLevel = [&](int lodIdx) -> TmuColorSIMD {
            const uint32_t* levelPixels =
                tex1->swizzledMipLevels[lodIdx].data();
            uint32_t levelW = std::max(1u, tex1->baseWidth >> lodIdx);
            uint32_t levelH = std::max(1u, tex1->baseHeight >> lodIdx);
            uint32_t levelWMask = levelW - 1;
            uint32_t levelHMask = levelH - 1;

            Simd8f lScale(1.0f / static_cast<float>(1 << lodIdx));
            Simd8f lTexU = trueS1 * Simd8f(scaleU1) * lScale;
            Simd8f lTexV = trueT1 * Simd8f(scaleV1) * lScale;

            Simd8i levelW_vec(static_cast<int>(levelW));

            if (minFilter1 == 1) {  // BILINEAR
              Simd8f u_sub = lTexU - Simd8f(0.5f);
              Simd8f v_sub = lTexV - Simd8f(0.5f);
              Simd8f floor_u = Floor(u_sub);
              Simd8f floor_v = Floor(v_sub);
              Simd8i u0_vec = Simd8i::Convert(floor_u);
              Simd8i v0_vec = Simd8i::Convert(floor_v);

              Simd8f fx_f = (u_sub - floor_u) * Simd8f(256.0f);
              Simd8f fy_f = (v_sub - floor_v) * Simd8f(256.0f);

              Simd8i x0, x1, y0, y1;
              if (clampS1 == 1) {
                Simd8i limit(static_cast<int>(levelW - 1));
                x0 = Max(Simd8i(0), Min(limit, u0_vec));
                x1 = Max(Simd8i(0), Min(limit, u0_vec + Simd8i(1)));
              } else if (clampS1 == 2) {
                x0 = mirrorCoordSIMD(u0_vec, levelW);
                x1 = mirrorCoordSIMD(u0_vec + Simd8i(1), levelW);
              } else {
                Simd8i mask(static_cast<int>(levelWMask));
                x0 = u0_vec & mask;
                x1 = (u0_vec + Simd8i(1)) & mask;
              }

              if (clampT1 == 1) {
                Simd8i limit(static_cast<int>(levelH - 1));
                y0 = Max(Simd8i(0), Min(limit, v0_vec));
                y1 = Max(Simd8i(0), Min(limit, v0_vec + Simd8i(1)));
              } else if (clampT1 == 2) {
                y0 = mirrorCoordSIMD(v0_vec, levelH);
                y1 = mirrorCoordSIMD(v0_vec + Simd8i(1), levelH);
              } else {
                Simd8i mask(static_cast<int>(levelHMask));
                y0 = v0_vec & mask;
                y1 = (v0_vec + Simd8i(1)) & mask;
              }

              Simd8i c00 = Simd8i::Gather(levelPixels, y0 * levelW_vec + x0);
              Simd8i c10 = Simd8i::Gather(levelPixels, y0 * levelW_vec + x1);
              Simd8i c01 = Simd8i::Gather(levelPixels, y1 * levelW_vec + x0);
              Simd8i c11 = Simd8i::Gather(levelPixels, y1 * levelW_vec + x1);

              c00 = applyTexChromaSIMD(c00, 1u);
              c10 = applyTexChromaSIMD(c10, 1u);
              c01 = applyTexChromaSIMD(c01, 1u);
              c11 = applyTexChromaSIMD(c11, 1u);

              Simd8f w00_f = (Simd8f(256.0f) - fx_f) * (Simd8f(256.0f) - fy_f) *
                             Simd8f(1.0f / 65536.0f);
              Simd8f w10_f =
                  fx_f * (Simd8f(256.0f) - fy_f) * Simd8f(1.0f / 65536.0f);
              Simd8f w01_f =
                  (Simd8f(256.0f) - fx_f) * fy_f * Simd8f(1.0f / 65536.0f);
              Simd8f w11_f = fx_f * fy_f * Simd8f(1.0f / 65536.0f);

              Simd8f r = Fma(getChannelFloat(c00, 16), w00_f,
                             Fma(getChannelFloat(c10, 16), w10_f,
                                 Fma(getChannelFloat(c01, 16), w01_f,
                                     getChannelFloat(c11, 16) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f g = Fma(getChannelFloat(c00, 8), w00_f,
                             Fma(getChannelFloat(c10, 8), w10_f,
                                 Fma(getChannelFloat(c01, 8), w01_f,
                                     getChannelFloat(c11, 8) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f b = Fma(getChannelFloat(c00, 0), w00_f,
                             Fma(getChannelFloat(c10, 0), w10_f,
                                 Fma(getChannelFloat(c01, 0), w01_f,
                                     getChannelFloat(c11, 0) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f a_val = Fma(getChannelFloat(c00, 24), w00_f,
                                 Fma(getChannelFloat(c10, 24), w10_f,
                                     Fma(getChannelFloat(c01, 24), w01_f,
                                         getChannelFloat(c11, 24) * w11_f))) *
                             Simd8f(1.0f / 255.0f);

              return {r, g, b, a_val};
            } else {  // POINT
              Simd8i u0_vec = Simd8i::Convert(lTexU + Simd8f(0.0001f));
              Simd8i v0_vec = Simd8i::Convert(lTexV + Simd8f(0.0001f));

              Simd8i x0, y0;
              if (clampS1 == 1) {
                x0 = Max(Simd8i(0), Min(Simd8i(levelW - 1), u0_vec));
              } else if (clampS1 == 2) {
                x0 = mirrorCoordSIMD(u0_vec, levelW);
              } else {
                x0 = u0_vec & Simd8i(levelWMask);
              }

              if (clampT1 == 1) {
                y0 = Max(Simd8i(0), Min(Simd8i(levelH - 1), v0_vec));
              } else if (clampT1 == 2) {
                y0 = mirrorCoordSIMD(v0_vec, levelH);
              } else {
                y0 = v0_vec & Simd8i(levelHMask);
              }

              Simd8i val = Simd8i::Gather(levelPixels, y0 * levelW_vec + x0);
              val = applyTexChromaSIMD(val, 1u);

              return {getChannelFloat(val, 16) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 8) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 0) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 24) * Simd8f(1.0f / 255.0f)};
            }
          };

          if (lodBlend1) {  // Trilinear
            int level0 = static_cast<int>(std::floor(clampedLod1));
            int level1 = level0 + 1;
            int lodIdx0 = std::clamp(level0, 0, maxMipIdx1);
            int lodIdx1 = std::clamp(level1, 0, maxMipIdx1);
            float lodFraction = clampedLod1 - std::floor(clampedLod1);

            TmuColorSIMD c0 = sampleLevel(lodIdx0);
            TmuColorSIMD c1 = sampleLevel(lodIdx1);

            Simd8f frac(lodFraction);
            tmu1Color.r = Fma(c1.r - c0.r, frac, c0.r);
            tmu1Color.g = Fma(c1.g - c0.g, frac, c0.g);
            tmu1Color.b = Fma(c1.b - c0.b, frac, c0.b);
            tmu1Color.a = Fma(c1.a - c0.a, frac, c0.a);
          } else {  // Nearest
            int level = static_cast<int>(std::round(clampedLod1));
            int lodIdx = std::clamp(level, 0, maxMipIdx1);
            tmu1Color = sampleLevel(lodIdx);
          }
        }

        tmu1Out = EvaluateTmuStageSIMD(
            state.texCombinerRgbFunc[1], state.texCombinerRgbFactor[1],
            state.texCombinerAlphaFunc[1], state.texCombinerAlphaFactor[1],
            state.texCombinerRgbInvert[1], state.texCombinerAlphaInvert[1],
            tmu1Color, tmu0Out, tmu0Out);
      }

      // --- TMU 0 (Downstream) ---
      if constexpr (HasTexture) {
        Simd8f texS =
            Fma(w0_vec, Simd8f(a.tex[0]),
                Fma(w1_vec, Simd8f(b.tex[0]), w2_vec * Simd8f(c.tex[0])));
        Simd8f texT =
            Fma(w0_vec, Simd8f(a.tex[1]),
                Fma(w1_vec, Simd8f(b.tex[1]), w2_vec * Simd8f(c.tex[1])));

        Simd8f divW0 = wVal;
        if (stwHintMask & 0x2) {
          divW0 = Fma(
              w0_vec, Simd8f(a.tmu_oow[0]),
              Fma(w1_vec, Simd8f(b.tmu_oow[0]), w2_vec * Simd8f(c.tmu_oow[0])));
        }
        Simd8f invW = Simd8f(1.0f) / divW0;
        Simd8f trueS = texS * invW;
        Simd8f trueT = texT * invW;

        TmuColorSIMD tmu0Color = {Simd8f(1.0f), Simd8f(1.0f), Simd8f(1.0f),
                                  Simd8f(1.0f)};
        if (tex) {
          float lod = 0.0f;
          if (mipmappingEnabled) {
            float centerX = static_cast<float>(x) + 1.5f;
            float centerY = static_cast<float>(y) + 0.5f;
            float centerW0 = (a0 * centerX + b0 * centerY + c0) * invArea;
            float centerW1 = (a1 * centerX + b1 * centerY + c1) * invArea;
            float centerW2 = (a2 * centerX + b2 * centerY + c2) * invArea;
            float centerWVal =
                centerW0 * a.pos[3] + centerW1 * b.pos[3] + centerW2 * c.pos[3];
            float centerDivW0 = centerWVal;
            if (stwHintMask & 0x2) {
              centerDivW0 = centerW0 * a.tmu_oow[0] + centerW1 * b.tmu_oow[0] +
                            centerW2 * c.tmu_oow[0];
            }
            float centerInvW = 1.0f / centerDivW0;
            float centerTexS =
                centerW0 * a.tex[0] + centerW1 * b.tex[0] + centerW2 * c.tex[0];
            float centerTexT =
                centerW0 * a.tex[1] + centerW1 * b.tex[1] + centerW2 * c.tex[1];
            float centerTrueS = centerTexS * centerInvW;
            float centerTrueT = centerTexT * centerInvW;

            float ds_dx = (dSp_dx - centerTrueS * dQ_dx) * centerInvW;
            float ds_dy = (dSp_dy - centerTrueS * dQ_dy) * centerInvW;
            float dt_dx = (dTp_dx - centerTrueT * dQ_dx) * centerInvW;
            float dt_dy = (dTp_dy - centerTrueT * dQ_dy) * centerInvW;

            float du_dx = ds_dx * scaleU;
            float du_dy = ds_dy * scaleU;
            float dv_dx = dt_dx * scaleV;
            float dv_dy = dt_dy * scaleV;

            float d = std::max(du_dx * du_dx + dv_dx * dv_dx,
                               du_dy * du_dy + dv_dy * dv_dy);
            lod = (d > 1e-8f) ? 0.5f * fast_log2(d) : -10.0f;
            lod += state.texLodBias[0];
          }

          const int maxMipIdx =
              static_cast<int>(tex->swizzledMipLevels.size()) - 1;
          float clampedLod =
              std::clamp(lod, 0.0f, static_cast<float>(maxMipIdx));

          auto sampleLevel = [&](int lodIdx) -> TmuColorSIMD {
            const uint32_t* levelPixels = tex->swizzledMipLevels[lodIdx].data();
            uint32_t levelW = std::max(1u, tex->baseWidth >> lodIdx);
            uint32_t levelH = std::max(1u, tex->baseHeight >> lodIdx);
            uint32_t levelWMask = levelW - 1;
            uint32_t levelHMask = levelH - 1;

            Simd8f lScale(1.0f / static_cast<float>(1 << lodIdx));
            Simd8f lTexU = trueS * Simd8f(scaleU) * lScale;
            Simd8f lTexV = trueT * Simd8f(scaleV) * lScale;

            Simd8i levelW_vec(static_cast<int>(levelW));

            if (minFilter == 1) {  // BILINEAR
              Simd8f u_sub = lTexU - Simd8f(0.5f);
              Simd8f v_sub = lTexV - Simd8f(0.5f);
              Simd8f floor_u = Floor(u_sub);
              Simd8f floor_v = Floor(v_sub);
              Simd8i u0_vec = Simd8i::Convert(floor_u);
              Simd8i v0_vec = Simd8i::Convert(floor_v);

              Simd8f fx_f = (u_sub - floor_u) * Simd8f(256.0f);
              Simd8f fy_f = (v_sub - floor_v) * Simd8f(256.0f);

              Simd8i x0, x1, y0, y1;
              if (clampS == 1) {
                Simd8i limit(static_cast<int>(levelW - 1));
                x0 = Max(Simd8i(0), Min(limit, u0_vec));
                x1 = Max(Simd8i(0), Min(limit, u0_vec + Simd8i(1)));
              } else if (clampS == 2) {
                x0 = mirrorCoordSIMD(u0_vec, levelW);
                x1 = mirrorCoordSIMD(u0_vec + Simd8i(1), levelW);
              } else {
                Simd8i mask(static_cast<int>(levelWMask));
                x0 = u0_vec & mask;
                x1 = (u0_vec + Simd8i(1)) & mask;
              }

              if (clampT == 1) {
                Simd8i limit(static_cast<int>(levelH - 1));
                y0 = Max(Simd8i(0), Min(limit, v0_vec));
                y1 = Max(Simd8i(0), Min(limit, v0_vec + Simd8i(1)));
              } else if (clampT == 2) {
                y0 = mirrorCoordSIMD(v0_vec, levelH);
                y1 = mirrorCoordSIMD(v0_vec + Simd8i(1), levelH);
              } else {
                Simd8i mask(static_cast<int>(levelHMask));
                y0 = v0_vec & mask;
                y1 = (v0_vec + Simd8i(1)) & mask;
              }

              Simd8i c00 = Simd8i::Gather(levelPixels, y0 * levelW_vec + x0);
              Simd8i c10 = Simd8i::Gather(levelPixels, y0 * levelW_vec + x1);
              Simd8i c01 = Simd8i::Gather(levelPixels, y1 * levelW_vec + x0);
              Simd8i c11 = Simd8i::Gather(levelPixels, y1 * levelW_vec + x1);

              c00 = applyTexChromaSIMD(c00, 0u);
              c10 = applyTexChromaSIMD(c10, 0u);
              c01 = applyTexChromaSIMD(c01, 0u);
              c11 = applyTexChromaSIMD(c11, 0u);

              Simd8f w00_f = (Simd8f(256.0f) - fx_f) * (Simd8f(256.0f) - fy_f) *
                             Simd8f(1.0f / 65536.0f);
              Simd8f w10_f =
                  fx_f * (Simd8f(256.0f) - fy_f) * Simd8f(1.0f / 65536.0f);
              Simd8f w01_f =
                  (Simd8f(256.0f) - fx_f) * fy_f * Simd8f(1.0f / 65536.0f);
              Simd8f w11_f = fx_f * fy_f * Simd8f(1.0f / 65536.0f);

              Simd8f r = Fma(getChannelFloat(c00, 16), w00_f,
                             Fma(getChannelFloat(c10, 16), w10_f,
                                 Fma(getChannelFloat(c01, 16), w01_f,
                                     getChannelFloat(c11, 16) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f g = Fma(getChannelFloat(c00, 8), w00_f,
                             Fma(getChannelFloat(c10, 8), w10_f,
                                 Fma(getChannelFloat(c01, 8), w01_f,
                                     getChannelFloat(c11, 8) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f b = Fma(getChannelFloat(c00, 0), w00_f,
                             Fma(getChannelFloat(c10, 0), w10_f,
                                 Fma(getChannelFloat(c01, 0), w01_f,
                                     getChannelFloat(c11, 0) * w11_f))) *
                         Simd8f(1.0f / 255.0f);
              Simd8f a_val = Fma(getChannelFloat(c00, 24), w00_f,
                                 Fma(getChannelFloat(c10, 24), w10_f,
                                     Fma(getChannelFloat(c01, 24), w01_f,
                                         getChannelFloat(c11, 24) * w11_f))) *
                             Simd8f(1.0f / 255.0f);

              return {r, g, b, a_val};
            } else {  // POINT
              Simd8i u0_vec = Simd8i::Convert(lTexU + Simd8f(0.0001f));
              Simd8i v0_vec = Simd8i::Convert(lTexV + Simd8f(0.0001f));

              Simd8i x0, y0;
              if (clampS == 1) {
                x0 = Max(Simd8i(0), Min(Simd8i(levelW - 1), u0_vec));
              } else if (clampS == 2) {
                x0 = mirrorCoordSIMD(u0_vec, levelW);
              } else {
                x0 = u0_vec & Simd8i(levelWMask);
              }

              if (clampT == 1) {
                y0 = Max(Simd8i(0), Min(Simd8i(levelH - 1), v0_vec));
              } else if (clampT == 2) {
                y0 = mirrorCoordSIMD(v0_vec, levelH);
              } else {
                y0 = v0_vec & Simd8i(levelHMask);
              }

              Simd8i val = Simd8i::Gather(levelPixels, y0 * levelW_vec + x0);
              val = applyTexChromaSIMD(val, 0u);

              return {getChannelFloat(val, 16) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 8) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 0) * Simd8f(1.0f / 255.0f),
                      getChannelFloat(val, 24) * Simd8f(1.0f / 255.0f)};
            }
          };

          if (lodBlend) {  // Trilinear
            int level0 = static_cast<int>(std::floor(clampedLod));
            int level1 = level0 + 1;
            int lodIdx0 = std::clamp(level0, 0, maxMipIdx);
            int lodIdx1 = std::clamp(level1, 0, maxMipIdx);
            float lodFraction = clampedLod - std::floor(clampedLod);

            TmuColorSIMD c0 = sampleLevel(lodIdx0);
            TmuColorSIMD c1 = sampleLevel(lodIdx1);

            Simd8f frac(lodFraction);
            tmu0Color.r = Fma(c1.r - c0.r, frac, c0.r);
            tmu0Color.g = Fma(c1.g - c0.g, frac, c0.g);
            tmu0Color.b = Fma(c1.b - c0.b, frac, c0.b);
            tmu0Color.a = Fma(c1.a - c0.a, frac, c0.a);
          } else {  // Nearest
            int level = static_cast<int>(std::round(clampedLod));
            int lodIdx = std::clamp(level, 0, maxMipIdx);
            tmu0Color = sampleLevel(lodIdx);
          }
        }

        tmu0Out = EvaluateTmuStageSIMD(
            state.texCombinerRgbFunc[0], state.texCombinerRgbFactor[0],
            state.texCombinerAlphaFunc[0], state.texCombinerAlphaFactor[0],
            state.texCombinerRgbInvert[0], state.texCombinerAlphaInvert[0],
            tmu0Color, tmu1Out, tmu0Out);
      }

      Simd8f cOtherR_chroma = tmu0Out.r;
      Simd8f cOtherG_chroma = tmu0Out.g;
      Simd8f cOtherB_chroma = tmu0Out.b;

      // 7. Color Combiner Stage
      Simd8f texR = tmu0Out.r;
      Simd8f texG = tmu0Out.g;
      Simd8f texB = tmu0Out.b;
      Simd8f texA = tmu0Out.a;

      Simd8f aLocal_comb =
          (alphaCombinerLocal == 0) ? iterA : Simd8f(constantAlpha);
      Simd8f aOther_comb =
          (alphaCombinerOther == 0)
              ? iterA
              : ((alphaCombinerOther == 1) ? texA : Simd8f(constantAlpha));

      Simd8f cLocalR_comb, cLocalG_comb, cLocalB_comb;
      if (colorCombinerLocal == 0) {
        cLocalR_comb = iterR;
        cLocalG_comb = iterG;
        cLocalB_comb = iterB;
      } else {
        cLocalR_comb = Simd8f(constantR);
        cLocalG_comb = Simd8f(constantG);
        cLocalB_comb = Simd8f(constantB);
      }

      Simd8f cOtherR_comb =
          (colorCombinerOther == 0)
              ? iterR
              : ((colorCombinerOther == 1) ? texR : Simd8f(constantR));
      Simd8f cOtherG_comb =
          (colorCombinerOther == 0)
              ? iterG
              : ((colorCombinerOther == 1) ? texG : Simd8f(constantG));
      Simd8f cOtherB_comb =
          (colorCombinerOther == 0)
              ? iterB
              : ((colorCombinerOther == 1) ? texB : Simd8f(constantB));

      Simd8f factR_comb(0.0f), factG_comb(0.0f), factB_comb(0.0f);
      switch (colorCombinerFactor) {
        case 1:
          factR_comb = cLocalR_comb;
          factG_comb = cLocalG_comb;
          factB_comb = cLocalB_comb;
          break;
        case 2:
          factR_comb = aOther_comb;
          factG_comb = aOther_comb;
          factB_comb = aOther_comb;
          break;
        case 3:
          factR_comb = aLocal_comb;
          factG_comb = aLocal_comb;
          factB_comb = aLocal_comb;
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
          factR_comb = Simd8f(1.0f);
          factG_comb = Simd8f(1.0f);
          factB_comb = Simd8f(1.0f);
          break;
        case 9:
          factR_comb = Simd8f(1.0f) - cLocalR_comb;
          factG_comb = Simd8f(1.0f) - cLocalG_comb;
          factB_comb = Simd8f(1.0f) - cLocalB_comb;
          break;
        case 10:
          factR_comb = Simd8f(1.0f) - aOther_comb;
          factG_comb = Simd8f(1.0f) - aOther_comb;
          factB_comb = Simd8f(1.0f) - aOther_comb;
          break;
        case 11:
          factR_comb = Simd8f(1.0f) - aLocal_comb;
          factG_comb = Simd8f(1.0f) - aLocal_comb;
          factB_comb = Simd8f(1.0f) - aLocal_comb;
          break;
        case 12:
          factR_comb = Simd8f(1.0f) - texA;
          factG_comb = Simd8f(1.0f) - texA;
          factB_comb = Simd8f(1.0f) - texA;
          break;
      }

      Simd8f finalR_comb(0.0f), finalG_comb(0.0f), finalB_comb(0.0f);
      switch (colorCombinerFunc) {
        case 1:
          finalR_comb = cLocalR_comb;
          finalG_comb = cLocalG_comb;
          finalB_comb = cLocalB_comb;
          break;
        case 3:
          finalR_comb = cOtherR_comb * factR_comb;
          finalG_comb = cOtherG_comb * factG_comb;
          finalB_comb = cOtherB_comb * factB_comb;
          break;
        case 4:
          finalR_comb = Fma(cOtherR_comb, factR_comb, cLocalR_comb);
          finalG_comb = Fma(cOtherG_comb, factG_comb, cLocalG_comb);
          finalB_comb = Fma(cOtherB_comb, factB_comb, cLocalB_comb);
          break;
        case 5:
          finalR_comb = Fma(cOtherR_comb, factR_comb, aLocal_comb);
          finalG_comb = Fma(cOtherG_comb, factG_comb, aLocal_comb);
          finalB_comb = Fma(cOtherB_comb, factB_comb, aLocal_comb);
          break;
        case 6:
          finalR_comb = (cOtherR_comb - cLocalR_comb) * factR_comb;
          finalG_comb = (cOtherG_comb - cLocalG_comb) * factR_comb;
          finalB_comb = (cOtherB_comb - cLocalB_comb) * factR_comb;
          break;
        case 7:
          finalR_comb =
              Fma(cOtherR_comb - cLocalR_comb, factR_comb, cLocalR_comb);
          finalG_comb =
              Fma(cOtherG_comb - cLocalG_comb, factR_comb, cLocalG_comb);
          finalB_comb =
              Fma(cOtherB_comb - cLocalB_comb, factR_comb, cLocalB_comb);
          break;
        case 8:
          finalR_comb =
              Fma(cOtherR_comb - cLocalR_comb, factR_comb, aLocal_comb);
          finalG_comb =
              Fma(cOtherG_comb - cLocalG_comb, factR_comb, aLocal_comb);
          finalB_comb =
              Fma(cOtherB_comb - cLocalB_comb, factR_comb, aLocal_comb);
          break;
      }
      if (colorCombinerInvert) {
        finalR_comb = Simd8f(1.0f) - finalR_comb;
        finalG_comb = Simd8f(1.0f) - finalG_comb;
        finalB_comb = Simd8f(1.0f) - finalB_comb;
      }

      Simd8f factA_comb(0.0f);
      switch (alphaCombinerFactor) {
        case 1:
        case 3:
          factA_comb = aLocal_comb;
          break;
        case 2:
          factA_comb = aOther_comb;
          break;
        case 4:
          factA_comb = texA;
          break;
        case 8:
          factA_comb = Simd8f(1.0f);
          break;
        case 9:
        case 11:
          factA_comb = Simd8f(1.0f) - aLocal_comb;
          break;
        case 10:
          factA_comb = Simd8f(1.0f) - aOther_comb;
          break;
        case 12:
          factA_comb = Simd8f(1.0f) - texA;
          break;
      }

      Simd8f finalA_comb(0.0f);
      switch (alphaCombinerFunc) {
        case 1:
          finalA_comb = aLocal_comb;
          break;
        case 3:
          finalA_comb = aOther_comb * factA_comb;
          break;
        case 4:
        case 5:
          finalA_comb = aOther_comb * factA_comb + aLocal_comb;
          break;
        case 6:
          finalA_comb = (aOther_comb - aLocal_comb) * factA_comb;
          break;
        case 7:
        case 8:
          finalA_comb = (aOther_comb - aLocal_comb) * factA_comb + aLocal_comb;
          break;
      }
      if (alphaCombinerInvert) finalA_comb = Simd8f(1.0f) - finalA_comb;

      Simd8f red = finalR_comb;
      Simd8f green = finalG_comb;
      Simd8f blue = finalB_comb;
      Simd8f alpha = finalA_comb;
      Simd8f blendAlphaVal = alpha;

      // 8. Pre-blend Alpha Dithering / Quantization
      if constexpr (DitherEnabled) {
        Simd8i dith = (state.ditherMode == 2) ? GetDitherVector4x4(y)
                                              : GetDitherVector2x2(y);
        Simd8f dith_f = Simd8f::Convert(dith);
        Simd8f alpha255 = Floor(alpha * Simd8f(255.0f) + Simd8f(0.5f));
        float matrixSize = (state.ditherMode == 2) ? 16.0f : 4.0f;
        Simd8f ditherVal = dith_f * Simd8f(128.0f / matrixSize);

        SimdMask ditherPass = (alpha255 + ditherVal) >= Simd8f(128.0f);
        SimdMask applies =
            (Simd8f(alphaTestOp) != Simd8f(7.0f)) | (alpha < Simd8f(0.99f));

        coverageMask = coverageMask & ((!applies) | ditherPass);
        blendAlphaVal =
            Select(applies & ditherPass, Simd8f(1.0f), blendAlphaVal);
        if (coverageMask.AllZero()) continue;
      }

      // 9. Chromakey Emulation
      if (chromakeyMode == 1) {
        bool hasTex = HasTexture || hasTexture1;
        Simd8f testR = hasTex ? cOtherR_chroma : red;
        Simd8f testG = hasTex ? cOtherG_chroma : green;
        Simd8f testB = hasTex ? cOtherB_chroma : blue;

        SimdMask chromaFail;
        bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1) == 1;
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

      // 10. Alpha Testing
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

      // 11. Depth Testing
      Simd8f targetDepth;
      if (depthMode == 2) {
        Simd8f w = Simd8f(1.0f) / wVal;
        targetDepth = (w - Simd8f(depthNear)) * Simd8f(depthRangeInv);
      } else {
        targetDepth =
            Fma(w0_vec, Simd8f(a.pos[2]),
                Fma(w1_vec, Simd8f(b.pos[2]), w2_vec * Simd8f(c.pos[2])));
      }
      if (depthBiasLevel != 0) {
        targetDepth =
            Max(Simd8f(0.0f),
                Min(Simd8f(1.0f),
                    targetDepth +
                        Simd8f(static_cast<float>(depthBiasLevel) / 65535.0f)));
      }

      if (depthMode != 0 && depthCompareOp != 7 && depthBuffer) {
        Simd8f currentDepth =
            Simd8f::Load2x4(&depthBuffer[y * width + x], width);
        SimdMask depthPass = SimdMask::True();
        Simd8f ref = targetDepth;
        switch (depthCompareOp) {
          case 1:
            depthPass = ref < currentDepth;
            break;
          case 2:
            depthPass = ref == currentDepth;
            break;
          case 3:
            depthPass = ref <= currentDepth;
            break;
          case 4:
            depthPass = ref > currentDepth;
            break;
          case 5:
            depthPass = ref != currentDepth;
            break;
          case 6:
            depthPass = ref >= currentDepth;
            break;
        }
        coverageMask = coverageMask & depthPass;
        if (coverageMask.AllZero()) continue;
      }

      if (depthMode != 0 && depthMask && depthBuffer) {
        Simd8f currentDepth =
            Simd8f::Load2x4(&depthBuffer[y * width + x], width);
        Simd8f newDepth = Select(coverageMask, targetDepth, currentDepth);
        Simd8f::Store2x4(&depthBuffer[y * width + x], width, newDepth);
      }

      // Capture pre-fog color for GR_BLEND_PREFOG_COLOR (multi-pass fog)
      const Simd8f preFogR = red;
      const Simd8f preFogG = green;
      const Simd8f preFogB = blue;

      // 12. Fog Stage
      if (fogMode != 0) {
        Simd8f f_vec(0.0f);
        uint32_t fogSource = fogMode & 0x0F;
        if (fogSource == 4) {
          f_vec = iterA;
        } else {
          alignas(32) float eyeW_arr[8];
          Simd8f eyeW = Simd8f(1.0f) / wVal;
          Simd8f::Store(eyeW_arr, eyeW);

          float f_arr[8];
          for (int i = 0; i < 8; ++i) {
            float ew = eyeW_arr[i];
            if (ew < 1.0f) ew = 1.0f;

            int idx = 0;
            if (ew >= s_tableIndexToW[32]) idx += 32;
            if (ew >= s_tableIndexToW[idx + 16]) idx += 16;
            if (ew >= s_tableIndexToW[idx + 8]) idx += 8;
            if (ew >= s_tableIndexToW[idx + 4]) idx += 4;
            if (ew >= s_tableIndexToW[idx + 2]) idx += 2;
            if (ew >= s_tableIndexToW[idx + 1]) idx += 1;
            idx = std::min(idx, 62);

            float w0_fog = s_tableIndexToW[idx];
            float w1_fog = s_tableIndexToW[idx + 1];
            float f0 = state.fogTable[idx] / 255.0f;
            float f1 = state.fogTable[idx + 1] / 255.0f;

            float t = 0.0f;
            if (w1_fog > w0_fog) {
              t = (ew - w0_fog) / (w1_fog - w0_fog);
            }
            t = std::max(0.0f, std::min(1.0f, t));
            f_arr[i] = f0 * (1.0f - t) + f1 * t;
          }
          f_vec = Simd8f(f_arr[0], f_arr[1], f_arr[2], f_arr[3], f_arr[4],
                         f_arr[5], f_arr[6], f_arr[7]);
        }

        Simd8f mult = ((fogMode & 0x100) != 0) ? Simd8f(1.0f) : Simd8f(0.0f);
        Simd8f add = ((fogMode & 0x200) != 0) ? Simd8f(1.0f) : Simd8f(0.0f);

        red = red * (Simd8f(1.0f) - mult) * (Simd8f(1.0f) - f_vec) +
              Simd8f(fogR) * (Simd8f(1.0f) - add) * f_vec;
        green = green * (Simd8f(1.0f) - mult) * (Simd8f(1.0f) - f_vec) +
                Simd8f(fogG) * (Simd8f(1.0f) - add) * f_vec;
        blue = blue * (Simd8f(1.0f) - mult) * (Simd8f(1.0f) - f_vec) +
               Simd8f(fogB) * (Simd8f(1.0f) - add) * f_vec;
      }

      // 13. Framebuffer Write & Alpha Blending
      Simd8i dstWord = Simd8i::Load2x4(&pixels[y * width + x], width);

      Simd8f dstR = Simd8f::Convert((dstWord >> 16) & Simd8i(0xFF)) *
                    Simd8f(1.0f / 255.0f);
      Simd8f dstG = Simd8f::Convert((dstWord >> 8) & Simd8i(0xFF)) *
                    Simd8f(1.0f / 255.0f);
      Simd8f dstB =
          Simd8f::Convert(dstWord & Simd8i(0xFF)) * Simd8f(1.0f / 255.0f);
      Simd8f dstA = Simd8f::Convert((dstWord >> 24) & Simd8i(0xFF)) *
                    Simd8f(1.0f / 255.0f);

      Simd8f blendedR = red;
      Simd8f blendedG = green;
      Simd8f blendedB = blue;
      Simd8f blendedA = alpha;

      if constexpr (BlendEnabled) {
        blendedR = Min(
            Simd8f(1.0f),
            Fma(red,
                getFSIMD(rgbSrcBlend, red, preFogR, dstR, blendAlphaVal, dstA,
                         false),
                dstR * getFSIMD(rgbDstBlend, red, preFogR, dstR, blendAlphaVal,
                                dstA, true)));
        blendedG = Min(
            Simd8f(1.0f),
            Fma(green,
                getFSIMD(rgbSrcBlend, green, preFogG, dstG, blendAlphaVal, dstA,
                         false),
                dstG * getFSIMD(rgbDstBlend, green, preFogG, dstG,
                                blendAlphaVal, dstA, true)));
        blendedB = Min(
            Simd8f(1.0f),
            Fma(blue,
                getFSIMD(rgbSrcBlend, blue, preFogB, dstB, blendAlphaVal, dstA,
                         false),
                dstB * getFSIMD(rgbDstBlend, blue, preFogB, dstB, blendAlphaVal,
                                dstA, true)));
        blendedA =
            Min(Simd8f(1.0f),
                Fma(blendAlphaVal,
                    getFSIMD(alphaSrcBlend, Simd8f(0.0f), Simd8f(0.0f),
                             Simd8f(0.0f), blendAlphaVal, dstA, false),
                    dstA * getFSIMD(alphaDstBlend, Simd8f(0.0f), Simd8f(0.0f),
                                    Simd8f(0.0f), blendAlphaVal, dstA, true)));
      }

      Simd8i rVal = Simd8i::Convert(
          Min(Simd8f(255.0f), blendedR * Simd8f(255.0f) + Simd8f(0.5f)));
      Simd8i gVal = Simd8i::Convert(
          Min(Simd8f(255.0f), blendedG * Simd8f(255.0f) + Simd8f(0.5f)));
      Simd8i bVal = Simd8i::Convert(
          Min(Simd8f(255.0f), blendedB * Simd8f(255.0f) + Simd8f(0.5f)));
      Simd8i aVal = Simd8i::Convert(
          Min(Simd8f(255.0f), blendedA * Simd8f(255.0f) + Simd8f(0.5f)));

      if constexpr (DitherEnabled) {
        Simd8i dith = (state.ditherMode == 2) ? GetDitherVector4x4(y)
                                              : GetDitherVector2x2(y);

        Simd8i ditheredR =
            ((rVal << 1) - (rVal >> 4) + (rVal >> 7) + dith) >> 1;
        Simd8i r = Min(Simd8i(31), ditheredR >> 3);

        Simd8i ditheredG =
            ((gVal << 2) - (gVal >> 4) + (gVal >> 6) + dith) >> 2;
        Simd8i g = Min(Simd8i(63), ditheredG >> 2);

        Simd8i ditheredB =
            ((bVal << 1) - (bVal >> 4) + (bVal >> 7) + dith) >> 1;
        Simd8i b = Min(Simd8i(31), ditheredB >> 3);

        rVal = (r << 3) | (r >> 2);
        gVal = (g << 2) | (g >> 4);
        bVal = (b << 3) | (b >> 2);
      }

      Simd8i finalColor = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
      if (!colorMaskRgb || !colorMaskAlpha) {
        uint32_t maskVal =
            (colorMaskRgb ? 0x00FFFFFF : 0) | (colorMaskAlpha ? 0xFF000000 : 0);
        Simd8i mask(static_cast<int32_t>(maskVal));
        finalColor = (finalColor & mask) | (dstWord & ~mask);
      }

      Simd8i writeColor = Select(coverageMask, finalColor, dstWord);
      Simd8i::Store2x4(&pixels[y * width + x], width, writeColor);
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
