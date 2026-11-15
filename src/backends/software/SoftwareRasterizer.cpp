#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Logger.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

inline bool SoftwareBackend::IsTexChromaMatch(
    uint32_t color, uint32_t tmu, const RasterizerState& state) const {
  if (state.texChromaMode[tmu] != 1) return false;

  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;

  bool rangeEnabled = ((state.texChromaRangeMode[tmu] >> 28) & 1) == 1;
  if (rangeEnabled) {
    uint32_t minR = (state.texChromaMin[tmu] >> 16) & 0xFF;
    uint32_t minG = (state.texChromaMin[tmu] >> 8) & 0xFF;
    uint32_t minB = state.texChromaMin[tmu] & 0xFF;

    uint32_t maxR = (state.texChromaMax[tmu] >> 16) & 0xFF;
    uint32_t maxG = (state.texChromaMax[tmu] >> 8) & 0xFF;
    uint32_t maxB = state.texChromaMax[tmu] & 0xFF;

    bool rMatch = (r >= minR && r <= maxR);
    bool gMatch = (g >= minG && g <= maxG);
    bool bMatch = (b >= minB && b <= maxB);

    bool blueExcl = ((state.texChromaRangeMode[tmu] >> 24) & 1) == 1;
    bool greenExcl = ((state.texChromaRangeMode[tmu] >> 25) & 1) == 1;
    bool redExcl = ((state.texChromaRangeMode[tmu] >> 26) & 1) == 1;

    bool rRes = rMatch != redExcl;
    bool gRes = gMatch != greenExcl;
    bool bRes = bMatch != blueExcl;

    bool unionMode = ((state.texChromaRangeMode[tmu] >> 27) & 1) == 1;
    return unionMode ? (rRes || gRes || bRes) : (rRes && gRes && bRes);
  } else {
    uint32_t chromaR = (state.texChromaMin[tmu] >> 16) & 0xFF;
    uint32_t chromaG = (state.texChromaMin[tmu] >> 8) & 0xFF;
    uint32_t chromaB = state.texChromaMin[tmu] & 0xFF;

    return (r == chromaR && g == chromaG && b == chromaB);
  }
}

inline uint32_t SoftwareBackend::SampleTextureLevel(
    const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS,
    float levelTrueT, uint32_t targetClampS, uint32_t targetClampT,
    uint32_t targetMinFilter, uint32_t tmuIdx,
    const RasterizerState& state) const {
  auto mirrorCoord = [](int coord, uint32_t size) -> int {
    int doubleSize = static_cast<int>(size * 2);
    int mask = doubleSize - 1;
    int wrapped = coord & mask;
    return (wrapped < static_cast<int>(size)) ? wrapped : (mask - wrapped);
  };

  const uint32_t* levelPixels = targetTex->swizzledMipLevels[lodIdx].data();
  uint32_t targetW = targetTex->baseWidth;
  uint32_t targetH = targetTex->baseHeight;
  uint32_t levelW = std::max(1u, targetW >> lodIdx);
  uint32_t levelH = std::max(1u, targetH >> lodIdx);
  uint32_t levelWMask = levelW - 1;
  uint32_t levelHMask = levelH - 1;

  float levelScale = 1.0f / static_cast<float>(1 << lodIdx);
  float maxDim = static_cast<float>(std::max(targetW, targetH));
  float targetScaleU = maxDim / 256.0f;
  float targetScaleV = maxDim / 256.0f;
  float levelTexU = levelTrueS * targetScaleU * levelScale;
  float levelTexV = levelTrueT * targetScaleV * levelScale;

  if (targetMinFilter == 1) {  // BILINEAR
    float u_sub = levelTexU - 0.5f;
    float v_sub = levelTexV - 0.5f;
    int u0 = static_cast<int>(std::floor(u_sub));
    int v0 = static_cast<int>(std::floor(v_sub));

    int fx = static_cast<int>((u_sub - u0) * 256.0f);
    int fy = static_cast<int>((v_sub - v0) * 256.0f);

    int x0, x1, y0, y1;
    if (targetClampS == 1) {  // CLAMP
      x0 = std::max(0, std::min(static_cast<int>(levelW - 1), u0));
      x1 = std::max(0, std::min(static_cast<int>(levelW - 1), u0 + 1));
    } else if (targetClampS == 2) {  // MIRROR
      x0 = mirrorCoord(u0, levelW);
      x1 = mirrorCoord(u0 + 1, levelW);
    } else {  // WRAP
      x0 = u0 & levelWMask;
      x1 = (u0 + 1) & levelWMask;
    }

    if (targetClampT == 1) {  // CLAMP
      y0 = std::max(0, std::min(static_cast<int>(levelH - 1), v0));
      y1 = std::max(0, std::min(static_cast<int>(levelH - 1), v0 + 1));
    } else if (targetClampT == 2) {  // MIRROR
      y0 = mirrorCoord(v0, levelH);
      y1 = mirrorCoord(v0 + 1, levelH);
    } else {  // WRAP
      y0 = v0 & levelHMask;
      y1 = (v0 + 1) & levelHMask;
    }

    uint32_t c00 = levelPixels[y0 * levelW + x0];
    uint32_t c10 = levelPixels[y0 * levelW + x1];
    uint32_t c01 = levelPixels[y1 * levelW + x0];
    uint32_t c11 = levelPixels[y1 * levelW + x1];

    if (IsTexChromaMatch(c00, tmuIdx, state)) c00 = 0;
    if (IsTexChromaMatch(c10, tmuIdx, state)) c10 = 0;
    if (IsTexChromaMatch(c01, tmuIdx, state)) c01 = 0;
    if (IsTexChromaMatch(c11, tmuIdx, state)) c11 = 0;

    int w00 = (256 - fx) * (256 - fy);
    int w10 = fx * (256 - fy);
    int w01 = (256 - fx) * fy;
    int w11 = fx * fy;

    auto blendChannel = [&](int shift) -> uint32_t {
      uint32_t val00 = (c00 >> shift) & 0xFF;
      uint32_t val10 = (c10 >> shift) & 0xFF;
      uint32_t val01 = (c01 >> shift) & 0xFF;
      uint32_t val11 = (c11 >> shift) & 0xFF;
      return (val00 * w00 + val10 * w10 + val01 * w01 + val11 * w11) >> 16;
    };

    uint32_t r = blendChannel(16);
    uint32_t g = blendChannel(8);
    uint32_t b = blendChannel(0);
    uint32_t a_val = blendChannel(24);

    return (a_val << 24) | (r << 16) | (g << 8) | b;
  } else {  // POINT
    int u0 = static_cast<int>(levelTexU + 0.0001f);
    int v0 = static_cast<int>(levelTexV + 0.0001f);

    int x0, y0;
    if (targetClampS == 1) {
      x0 = std::max(0, std::min(static_cast<int>(levelW - 1), u0));
    } else if (targetClampS == 2) {
      x0 = mirrorCoord(u0, levelW);
    } else {
      x0 = u0 & levelWMask;
    }

    if (targetClampT == 1) {
      y0 = std::max(0, std::min(static_cast<int>(levelH - 1), v0));
    } else if (targetClampT == 2) {
      y0 = mirrorCoord(v0, levelH);
    } else {
      y0 = v0 & levelHMask;
    }

    uint32_t val = levelPixels[y0 * levelW + x0];
    if (IsTexChromaMatch(val, tmuIdx, state)) {
      val = 0;
    }
    return val;
  }
}

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled>
inline void SoftwareBackend::RasterizeTriangleTemplate(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int tileMinX, int tileMaxX, int tileMinY, int tileMaxY,
    const RasterizerState& state) {
  int minX = std::max(
      tileMinX,
      static_cast<int>(std::floor(std::min({a.pos[0], b.pos[0], c.pos[0]}))));
  int maxX = std::min(
      tileMaxX - 1,
      static_cast<int>(std::ceil(std::max({a.pos[0], b.pos[0], c.pos[0]}))));
  int minY = std::max(
      tileMinY,
      static_cast<int>(std::floor(std::min({a.pos[1], b.pos[1], c.pos[1]}))));
  int maxY = std::min(
      tileMaxY - 1,
      static_cast<int>(std::ceil(std::max({a.pos[1], b.pos[1], c.pos[1]}))));

  if (minX > maxX || minY > maxY) {
    return;
  }

  // Compute triangle area
  float area = (c.pos[0] - a.pos[0]) * (b.pos[1] - a.pos[1]) -
               (c.pos[1] - a.pos[1]) * (b.pos[0] - a.pos[0]);

  if (std::abs(area) < 0.0001f) {
    return;
  }

  float invArea = 1.0f / area;

  // Incremental Edge Setup for Pineda's Algorithm
  float a0 = c.pos[1] - b.pos[1];
  float b0 = b.pos[0] - c.pos[0];
  float c0 = b.pos[1] * c.pos[0] - b.pos[0] * c.pos[1];

  float a1 = a.pos[1] - c.pos[1];
  float b1 = c.pos[0] - a.pos[0];
  float c1 = c.pos[1] * a.pos[0] - c.pos[0] * a.pos[1];
  float a2 = b.pos[1] - a.pos[1];
  float b2 = a.pos[0] - b.pos[0];
  float c2 = a.pos[1] * b.pos[0] - a.pos[0] * b.pos[1];

  if (m_useSimd) {
    if (invArea >= 0.0f) {
      RasterizeTriangleLoopsSIMD<HasTexture, BlendEnabled, DitherEnabled, true>(
          a, b, c, minX, maxX, minY, maxY, state, invArea, a0, b0, c0, a1, b1,
          c1, a2, b2, c2);
    } else {
      RasterizeTriangleLoopsSIMD<HasTexture, BlendEnabled, DitherEnabled,
                                 false>(a, b, c, minX, maxX, minY, maxY, state,
                                        invArea, a0, b0, c0, a1, b1, c1, a2, b2,
                                        c2);
    }
  } else {
    if (invArea >= 0.0f) {
      RasterizeTriangleLoops<HasTexture, BlendEnabled, DitherEnabled, true>(
          a, b, c, minX, maxX, minY, maxY, state, invArea, a0, b0, c0, a1, b1,
          c1, a2, b2, c2);
    } else {
      RasterizeTriangleLoops<HasTexture, BlendEnabled, DitherEnabled, false>(
          a, b, c, minX, maxX, minY, maxY, state, invArea, a0, b0, c0, a1, b1,
          c1, a2, b2, c2);
    }
  }
}

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
          bool Clockwise>
inline void SoftwareBackend::RasterizeTriangleLoops(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int minX, int maxX, int minY, int maxY, const RasterizerState& state,
    float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
    float a2, float b2, float c2) {
  auto* pixels = reinterpret_cast<uint32_t*>(
      m_cpuBuffers[state.activeRenderBufferIdx].data());
  float* depthBuffer =
      m_headlessDepthBuffer.empty() ? nullptr : m_headlessDepthBuffer.data();
  const int width = m_headlessWidth;

  // Cache read-only state variables outside the loops to maximize register
  // allocation
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

  // Pre-calculate expensive division and format operations outside the loop
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

  // TMU 0 Setup (Downstream - runtime resolved)
  uint32_t texW = 0;
  uint32_t texH = 0;
  uint32_t clampS = 0;
  uint32_t clampT = 0;
  uint32_t minFilter = 0;
  float scaleU = 0.0f;
  float scaleV = 0.0f;

  float dw0_dx = 0.0f, dw0_dy = 0.0f;
  float dw1_dx = 0.0f, dw1_dy = 0.0f;
  float dw2_dx = 0.0f, dw2_dy = 0.0f;
  float dSp_dx = 0.0f, dSp_dy = 0.0f;
  float dTp_dx = 0.0f, dTp_dy = 0.0f;
  float dQ_dx = 0.0f, dQ_dy = 0.0f;

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

  // TMU 1 Setup (Upstream - runtime resolved)
  bool hasTexture1 = (state.boundTexAddress[1] != 0xFFFFFFFF);
  uint32_t texW1 = 0;
  uint32_t texH1 = 0;
  uint32_t clampS1 = 0;
  uint32_t clampT1 = 0;
  uint32_t minFilter1 = 0;
  float scaleU1 = 0.0f;
  float scaleV1 = 0.0f;

  float dSp1_dx = 0.0f, dSp1_dy = 0.0f;
  float dTp1_dx = 0.0f, dTp1_dy = 0.0f;
  float dQ1_dx = 0.0f, dQ1_dy = 0.0f;

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
    if (stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
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
    if (stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
      qa = a.tmu_oow[1];
      qb = b.tmu_oow[1];
      qc = c.tmu_oow[1];
    }
    dQ1_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ1_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  float startX = minX + 0.5f;

  for (int y = minY; y <= maxY; ++y) {
    float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
    float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
    float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
    for (int x = minX; x <= maxX; ++x) {
      bool inside = false;
      if constexpr (Clockwise) {
        inside = (E0 >= -1e-5f && E1 >= -1e-5f && E2 >= -1e-5f);
      } else {
        inside = (E0 <= 1e-5f && E1 <= 1e-5f && E2 <= 1e-5f);
      }
      if (inside) {
        float w0 = E0 * invArea;
        float w1 = E1 * invArea;
        float w2 = E2 * invArea;

        int pIdx = y * width + x;
        float cOtherR = 0.0f;
        float cOtherG = 0.0f;
        float cOtherB = 0.0f;
        bool keepPixel = true;
        float blendAlphaVal = 1.0f;

        // Stipple Transparency Check
        if (stippleMode != 0) {
          int stippleX = (stippleMode == 2) ? ((x + y) & 7) : (x & 7);
          int stippleY = y & 3;
          int stippleIndex = (stippleY << 3) | (7 - stippleX);
          if (((stipplePattern >> stippleIndex) & 1) == 0) {
            keepPixel = false;
          }
        }

        float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];

        // Near-plane clipping
        if (depthMode == 2) {  // GR_DEPTHBUFFER_WBUFFER
          if (wVal > 1.0001f) keepPixel = false;
        } else if constexpr (HasTexture) {
          if (wVal <= 0.0f) keepPixel = false;
        }

        float iterR = 0.0f;
        float iterG = 0.0f;
        float iterB = 0.0f;
        float iterA = 0.0f;
        if (keepPixel) {
          iterR = w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0];
          iterG = w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1];
          iterB = w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2];
          iterA = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];
        }

        // 2. Sample TMU 1 (Upstream)
        TmuColor tmu1Color = {1.0f, 1.0f, 1.0f, 1.0f};
        if (keepPixel && hasTexture1) {
          float texS1 = w0 * a.tex[2] + w1 * b.tex[2] + w2 * c.tex[2];
          float texT1 = w0 * a.tex[3] + w1 * b.tex[3] + w2 * c.tex[3];

          float divW1 = wVal;
          if (stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
            divW1 = w0 * a.tmu_oow[1] + w1 * b.tmu_oow[1] + w2 * c.tmu_oow[1];
          }
          float invW1 = 1.0f / divW1;
          float trueS1 = texS1 * invW1;
          float trueT1 = texT1 * invW1;

          uint32_t tWord1;
          if (mipmappingEnabled1) {
            float ds_dx1 = (dSp1_dx - trueS1 * dQ1_dx) * invW1;
            float ds_dy1 = (dSp1_dy - trueS1 * dQ1_dy) * invW1;
            float dt_dx1 = (dTp1_dx - trueT1 * dQ1_dx) * invW1;
            float dt_dy1 = (dTp1_dy - trueT1 * dQ1_dy) * invW1;

            float du_dx1 = ds_dx1 * scaleU1;
            float du_dy1 = ds_dy1 * scaleU1;
            float dv_dx1 = dt_dx1 * scaleV1;
            float dv_dy1 = dt_dy1 * scaleV1;

            float d1 = std::max(du_dx1 * du_dx1 + dv_dx1 * dv_dx1,
                                du_dy1 * du_dy1 + dv_dy1 * dv_dy1);
            float lod1 = (d1 > 1e-8f) ? 0.5f * fast_log2(d1) : -10.0f;
            lod1 += state.texLodBias[1];

            const int maxMipIdx1 =
                static_cast<int>(tex1->swizzledMipLevels.size()) - 1;
            float clampedLod1 =
                std::clamp(lod1, 0.0f, static_cast<float>(maxMipIdx1));

            if (lodBlend1) {  // Trilinear
              int level0 = static_cast<int>(std::floor(clampedLod1));
              int level1 = level0 + 1;
              int lodIdx0 = std::clamp(level0, 0, maxMipIdx1);
              int lodIdx1 = std::clamp(level1, 0, maxMipIdx1);
              float lodFraction = clampedLod1 - std::floor(clampedLod1);

              uint32_t c0 =
                  SampleTextureLevel(tex1, lodIdx0, trueS1, trueT1, clampS1,
                                     clampT1, minFilter1, 1u, state);
              uint32_t c1 =
                  SampleTextureLevel(tex1, lodIdx1, trueS1, trueT1, clampS1,
                                     clampT1, minFilter1, 1u, state);

              int w1_frac = static_cast<int>(lodFraction * 256.0f);
              int w0_frac = 256 - w1_frac;

              auto blendMip = [&](int shift) -> uint32_t {
                uint32_t val0 = (c0 >> shift) & 0xFF;
                uint32_t val1 = (c1 >> shift) & 0xFF;
                return (val0 * w0_frac + val1 * w1_frac) >> 8;
              };

              uint32_t r = blendMip(16);
              uint32_t g = blendMip(8);
              uint32_t b = blendMip(0);
              uint32_t a_val = blendMip(24);
              tWord1 = (a_val << 24) | (r << 16) | (g << 8) | b;
            } else {  // Nearest Mipmap
              int level = static_cast<int>(std::round(clampedLod1));
              int lodIdx = std::clamp(level, 0, maxMipIdx1);
              tWord1 = SampleTextureLevel(tex1, lodIdx, trueS1, trueT1, clampS1,
                                          clampT1, minFilter1, 1u, state);
            }
          } else {
            tWord1 = SampleTextureLevel(tex1, 0, trueS1, trueT1, clampS1,
                                        clampT1, minFilter1, 1u, state);
          }

          tmu1Color.r = ((tWord1 >> 16) & 0xFF) / 255.0f;
          tmu1Color.g = ((tWord1 >> 8) & 0xFF) / 255.0f;
          tmu1Color.b = (tWord1 & 0xFF) / 255.0f;
          tmu1Color.a = ((tWord1 >> 24) & 0xFF) / 255.0f;
        }

        // Evaluate TMU 1 Stage
        TmuColor iteratedColor = {iterR, iterG, iterB, iterA};
        bool tmu1Active = hasTexture1;
        TmuColor tmu1Out = iteratedColor;
        if (keepPixel && tmu1Active) {
          tmu1Out = EvaluateTmuStage(
              state.texCombinerRgbFunc[1], state.texCombinerRgbFactor[1],
              state.texCombinerAlphaFunc[1], state.texCombinerAlphaFactor[1],
              state.texCombinerRgbInvert[1], state.texCombinerAlphaInvert[1],
              tmu1Color, iteratedColor, iteratedColor);
        }

        // 3. Sample TMU 0 (Downstream)
        TmuColor tmu0Color = {1.0f, 1.0f, 1.0f, 1.0f};
        if constexpr (HasTexture) {
          if (keepPixel && tex) {
            float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
            float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];

            float divW0 = wVal;
            if (stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
              divW0 = w0 * a.tmu_oow[0] + w1 * b.tmu_oow[0] + w2 * c.tmu_oow[0];
            }
            float invW = 1.0f / divW0;
            float trueS = texS * invW;
            float trueT = texT * invW;

            uint32_t tWord;
            if (mipmappingEnabled) {
              float ds_dx = (dSp_dx - trueS * dQ_dx) * invW;
              float ds_dy = (dSp_dy - trueS * dQ_dy) * invW;
              float dt_dx = (dTp_dx - trueT * dQ_dx) * invW;
              float dt_dy = (dTp_dy - trueT * dQ_dy) * invW;

              float du_dx = ds_dx * scaleU;
              float du_dy = ds_dy * scaleU;
              float dv_dx = dt_dx * scaleV;
              float dv_dy = dt_dy * scaleV;

              float d = std::max(du_dx * du_dx + dv_dx * dv_dx,
                                 du_dy * du_dy + dv_dy * dv_dy);
              float lod = (d > 1e-8f) ? 0.5f * fast_log2(d) : -10.0f;
              lod += state.texLodBias[0];

              const int maxMipIdx =
                  static_cast<int>(tex->swizzledMipLevels.size()) - 1;
              float clampedLod =
                  std::clamp(lod, 0.0f, static_cast<float>(maxMipIdx));

              if (lodBlend) {  // Trilinear
                int level0 = static_cast<int>(std::floor(clampedLod));
                int level1 = level0 + 1;
                int lodIdx0 = std::clamp(level0, 0, maxMipIdx);
                int lodIdx1 = std::clamp(level1, 0, maxMipIdx);
                float lodFraction = clampedLod - std::floor(clampedLod);

                uint32_t c0 =
                    SampleTextureLevel(tex, lodIdx0, trueS, trueT, clampS,
                                       clampT, minFilter, 0u, state);
                uint32_t c1 =
                    SampleTextureLevel(tex, lodIdx1, trueS, trueT, clampS,
                                       clampT, minFilter, 0u, state);

                int w1_frac = static_cast<int>(lodFraction * 256.0f);
                int w0_frac = 256 - w1_frac;

                auto blendMip = [&](int shift) -> uint32_t {
                  uint32_t val0 = (c0 >> shift) & 0xFF;
                  uint32_t val1 = (c1 >> shift) & 0xFF;
                  return (val0 * w0_frac + val1 * w1_frac) >> 8;
                };

                uint32_t r = blendMip(16);
                uint32_t g = blendMip(8);
                uint32_t b = blendMip(0);
                uint32_t a_val = blendMip(24);
                tWord = (a_val << 24) | (r << 16) | (g << 8) | b;
              } else {  // Nearest Mipmap
                int level = static_cast<int>(std::round(clampedLod));
                int lodIdx = std::clamp(level, 0, maxMipIdx);
                tWord = SampleTextureLevel(tex, lodIdx, trueS, trueT, clampS,
                                           clampT, minFilter, 0u, state);
              }
            } else {
              tWord = SampleTextureLevel(tex, 0, trueS, trueT, clampS, clampT,
                                         minFilter, 0u, state);
            }

            tmu0Color.r = ((tWord >> 16) & 0xFF) / 255.0f;
            tmu0Color.g = ((tWord >> 8) & 0xFF) / 255.0f;
            tmu0Color.b = (tWord & 0xFF) / 255.0f;
            tmu0Color.a = ((tWord >> 24) & 0xFF) / 255.0f;
          }
        }

        // Evaluate TMU 0 Stage
        TmuColor tmu0Out = tmu1Out;
        if constexpr (HasTexture) {
          if (keepPixel) {
            tmu0Out = EvaluateTmuStage(
                state.texCombinerRgbFunc[0], state.texCombinerRgbFactor[0],
                state.texCombinerAlphaFunc[0], state.texCombinerAlphaFactor[0],
                state.texCombinerRgbInvert[0], state.texCombinerAlphaInvert[0],
                tmu0Color, tmu1Out, iteratedColor);
          }
        }

        // Save original color from texture stage for chromakeying reference
        if (keepPixel && chromakeyMode == 1) {
          cOtherR = tmu0Out.r;
          cOtherG = tmu0Out.g;
          cOtherB = tmu0Out.b;
        }

        // 4. Color Combiner Stage
        TmuColor texColor = tmu0Out;
        float texR = texColor.r;
        float texG = texColor.g;
        float texB = texColor.b;
        float texA = texColor.a;

        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;

        if (keepPixel) {
          float aLocal = (alphaCombinerLocal == 0) ? iterA : constantAlpha;
          float aOther =
              (alphaCombinerOther == 0)
                  ? iterA
                  : ((alphaCombinerOther == 1) ? texA : constantAlpha);

          float cLocalR = 0.0f;
          float cLocalG = 0.0f;
          float cLocalB = 0.0f;
          if (colorCombinerLocal == 0) {  // GR_COMBINE_LOCAL_ITERATED
            cLocalR = iterR;
            cLocalG = iterG;
            cLocalB = iterB;
          } else if (colorCombinerLocal ==
                     1) {  // GR_COMBINE_LOCAL_CONSTANT / NONE
            cLocalR = constantR;
            cLocalG = constantG;
            cLocalB = constantB;
          }

          cOtherR = (colorCombinerOther == 0)
                        ? iterR
                        : ((colorCombinerOther == 1) ? texR : constantR);
          cOtherG = (colorCombinerOther == 0)
                        ? iterG
                        : ((colorCombinerOther == 1) ? texG : constantG);
          cOtherB = (colorCombinerOther == 0)
                        ? iterB
                        : ((colorCombinerOther == 1) ? texB : constantB);

          // Evaluate Color Combiner Factor
          float factR = 0.0f;
          float factG = 0.0f;
          float factB = 0.0f;
          switch (colorCombinerFactor) {
            case 1:
              factR = cLocalR;
              factG = cLocalG;
              factB = cLocalB;
              break;
            case 2:
              factR = aOther;
              factG = aOther;
              factB = aOther;
              break;
            case 3:
              factR = aLocal;
              factG = aLocal;
              factB = aLocal;
              break;
            case 4:
              factR = texA;
              factG = texA;
              factB = texA;
              break;
            case 5:
              factR = texR;
              factG = texG;
              factB = texB;
              break;
            case 8:
              factR = 1.0f;
              factG = 1.0f;
              factB = 1.0f;
              break;
            case 9:
              factR = 1.0f - cLocalR;
              factG = 1.0f - cLocalG;
              factB = 1.0f - cLocalB;
              break;
            case 10:
              factR = 1.0f - aOther;
              factG = 1.0f - aOther;
              factB = 1.0f - aOther;
              break;
            case 11:
              factR = 1.0f - aLocal;
              factG = 1.0f - aLocal;
              factB = 1.0f - aLocal;
              break;
            case 12:
              factR = 1.0f - texA;
              factG = 1.0f - texA;
              factB = 1.0f - texA;
              break;
          }

          // Evaluate Color Combiner Function
          float finalR = 0.0f;
          float finalG = 0.0f;
          float finalB = 0.0f;
          switch (colorCombinerFunc) {
            case 1:
              finalR = cLocalR;
              finalG = cLocalG;
              finalB = cLocalB;
              break;
            case 3:
              finalR = cOtherR * factR;
              finalG = cOtherG * factG;
              finalB = cOtherB * factB;
              break;
            case 4:
              finalR = cOtherR * factR + cLocalR;
              finalG = cOtherG * factG + cLocalG;
              finalB = cOtherB * factB + cLocalB;
              break;
            case 5:
              finalR = cOtherR * factR + aLocal;
              finalG = cOtherG * factG + aLocal;
              finalB = cOtherB * factB + aLocal;
              break;
            case 6:
              finalR = (cOtherR - cLocalR) * factR;
              finalG = (cOtherG - cLocalG) * factG;
              finalB = (cOtherB - cLocalB) * factB;
              break;
            case 7:
              finalR = (cOtherR - cLocalR) * factR + cLocalR;
              finalG = (cOtherG - cLocalG) * factR + cLocalG;
              finalB = (cOtherB - cLocalB) * factR + cLocalB;
              break;
            case 8:
              finalR = (cOtherR - cLocalR) * factR + aLocal;
              finalG = (cOtherG - cLocalG) * factR + aLocal;
              finalB = (cOtherB - cLocalB) * factR + aLocal;
              break;
          }
          if (colorCombinerInvert) {
            finalR = 1.0f - finalR;
            finalG = 1.0f - finalG;
            finalB = 1.0f - finalB;
          }

          // Evaluate Alpha Combiner Factor
          float factA = 0.0f;
          switch (alphaCombinerFactor) {
            case 1:
            case 3:
              factA = aLocal;
              break;
            case 2:
              factA = aOther;
              break;
            case 4:
              factA = texA;
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
              factA = 1.0f - texA;
              break;
          }

          // Evaluate Alpha Combiner Function
          float finalA = 0.0f;
          switch (alphaCombinerFunc) {
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
          }
          if (alphaCombinerInvert) finalA = 1.0f - finalA;

          red = finalR;
          green = finalG;
          blue = finalB;
          alpha = finalA;
          blendAlphaVal = alpha;

          // Pre-blend Alpha Dithering / Quantization
          if constexpr (DitherEnabled) {
            static const uint8_t dither_matrix_4x4[16] = {
                0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            static const uint8_t dither_matrix_2x2[16] = {
                0, 2, 0, 2, 3, 1, 3, 1, 0, 2, 0, 2, 3, 1, 3, 1};
            int ditherX = x & 3;
            int ditherY = y & 3;
            uint8_t dith = (state.ditherMode == 2)
                               ? dither_matrix_4x4[ditherY * 4 + ditherX]
                               : dither_matrix_2x2[ditherY * 4 + ditherX];
            if ((alphaTestOp != 7) || (alpha < 0.99f)) {
              float alpha255 = std::round(alpha * 255.0f);
              if (alpha255 + (static_cast<float>(dith) * 128.0f /
                              (state.ditherMode == 2 ? 16.0f : 4.0f)) <
                  128.0f)
                keepPixel = false;
              else
                blendAlphaVal = 1.0f;
            }
          }
        }

        // Chromakey Emulation
        if (keepPixel && chromakeyMode == 1) {  // GR_CHROMAKEY_ENABLE
          bool hasTex = HasTexture || hasTexture1;
          float testR = hasTex ? cOtherR : red;
          float testG = hasTex ? cOtherG : green;
          float testB = hasTex ? cOtherB : blue;

          bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1) == 1;
          if (rangeEnabled) {
            bool rMatch = (testR >= chromaMinR && testR <= chromaMaxR);
            bool gMatch = (testG >= chromaMinG && testG <= chromaMaxG);
            bool bMatch = (testB >= chromaMinB && testB <= chromaMaxB);
            bool rRes = rMatch != ((chromakeyRangeMode >> 26) & 1);
            bool gRes = gMatch != ((chromakeyRangeMode >> 25) & 1);
            bool bRes = bMatch != ((chromakeyRangeMode >> 24) & 1);
            if (((chromakeyRangeMode >> 27) & 1) ? (rRes || gRes || bRes)
                                                 : (rRes && gRes && bRes))
              keepPixel = false;
          } else {
            if (std::abs(testR - chromaR) < 0.001f &&
                std::abs(testG - chromaG) < 0.001f &&
                std::abs(testB - chromaB) < 0.001f)
              keepPixel = false;
          }
        }

        // Alpha Testing
        if (keepPixel && alphaTestOp != 7) {
          bool alphaPass = false;
          switch (alphaTestOp) {
            case 0:
              alphaPass = false;
              break;
            case 1:
              alphaPass = (alpha < alphaTestRef);
              break;
            case 2:
              alphaPass = (alpha == alphaTestRef);
              break;
            case 3:
              alphaPass = (alpha <= alphaTestRef);
              break;
            case 4:
              alphaPass = (alpha > alphaTestRef);
              break;
            case 5:
              alphaPass = (alpha != alphaTestRef);
              break;
            case 6:
              alphaPass = (alpha >= alphaTestRef);
              break;
          }
          if (!alphaPass) keepPixel = false;
        }

        // Depth Testing
        if (keepPixel) {
          float targetDepth = 0.0f;
          if (depthMode == 2) {
            float w = 1.0f / wVal;
            targetDepth = (w - depthNear) * depthRangeInv;
          } else {
            targetDepth = w0 * a.pos[2] + w1 * b.pos[2] + w2 * c.pos[2];
          }
          if (depthBiasLevel != 0)
            targetDepth = std::max(
                0.0f, std::min(1.0f, targetDepth +
                                         static_cast<float>(depthBiasLevel) /
                                             65535.0f));
          if (depthMode != 0 && depthCompareOp != 7 && depthBuffer) {
            float currentDepth = depthBuffer[pIdx];
            bool depthPass = false;
            switch (depthCompareOp) {
              case 1:
                depthPass = (targetDepth < currentDepth);
                break;
              case 2:
                depthPass = (targetDepth == currentDepth);
                break;
              case 3:
                depthPass = (targetDepth <= currentDepth);
                break;
              case 4:
                depthPass = (targetDepth > currentDepth);
                break;
              case 5:
                depthPass = (targetDepth != currentDepth);
                break;
              case 6:
                depthPass = (targetDepth >= currentDepth);
                break;
            }
            if (!depthPass) keepPixel = false;
          }

          if (keepPixel) {
            if (depthMode != 0 && depthMask && depthBuffer) {
              depthBuffer[pIdx] = targetDepth;
            }
          }
        }

        // Capture pre-fog color for GR_BLEND_PREFOG_COLOR (multi-pass fog)
        const float preFogR = red;
        const float preFogG = green;
        const float preFogB = blue;

        // Fog Stage
        if (keepPixel && (fogMode != 0)) {
          uint32_t fogSource = fogMode & 0x0F;
          float f = 0.0f;
          if (fogSource == 4) {  // UNIFIED_FOG_WITH_ITERATED_ALPHA
            f = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];
          } else {  // Table-based fog
            float eyeW = 1.0f / wVal;
            if (eyeW < 1.0f) eyeW = 1.0f;

            int idx = 0;
            if (eyeW >= s_tableIndexToW[32]) idx += 32;
            if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
            if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
            if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
            if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
            if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;
            idx = std::min(idx, 62);

            float w0_fog = s_tableIndexToW[idx];
            float w1_fog = s_tableIndexToW[idx + 1];
            float f0 = state.fogTable[idx] / 255.0f;
            float f1 = state.fogTable[idx + 1] / 255.0f;

            float t = 0.0f;
            if (w1_fog > w0_fog) {
              t = (eyeW - w0_fog) / (w1_fog - w0_fog);
            }
            t = std::max(0.0f, std::min(1.0f, t));
            f = f0 * (1.0f - t) + f1 * t;
          }

          float mult = ((fogMode & 0x100) != 0) ? 1.0f : 0.0f;
          float add = ((fogMode & 0x200) != 0) ? 1.0f : 0.0f;

          red = red * (1.0f - mult) * (1.0f - f) + fogR * (1.0f - add) * f;
          green = green * (1.0f - mult) * (1.0f - f) + fogG * (1.0f - add) * f;
          blue = blue * (1.0f - mult) * (1.0f - f) + fogB * (1.0f - add) * f;
        }

        // Framebuffer Write / Alpha Blending
        if (keepPixel) {
          uint32_t rVal = 0, gVal = 0, bVal = 0, aVal = 255;
          if constexpr (BlendEnabled) {
            uint32_t currentPixel = pixels[pIdx];
            float dstR = ((currentPixel >> 16) & 0xFF) / 255.0f;
            float dstG = ((currentPixel >> 8) & 0xFF) / 255.0f;
            float dstB = (currentPixel & 0xFF) / 255.0f;
            float dstA = ((currentPixel >> 24) & 0xFF) / 255.0f;

            auto getF = [&](uint32_t mode, float srcC, float preFogC,
                            float dstC, float srcA, float dstA, bool isDest) {
              switch (mode) {
                case 0:
                  return 0.0f;
                case 1:
                  return srcA;
                case 2:
                  return isDest ? srcC : dstC;
                case 3:
                  return dstA;
                case 4:
                  return 1.0f;
                case 5:
                  return 1.0f - srcA;
                case 6:
                  return isDest ? (1.0f - srcC) : (1.0f - dstC);
                case 7:
                  return 1.0f - dstA;
                case 15:
                  return isDest ? preFogC : std::min(srcA, 1.0f - dstA);
                default:
                  return 1.0f;
              }
            };
            red = std::min(
                1.0f,
                red * getF(rgbSrcBlend, red, preFogR, dstR, blendAlphaVal, dstA,
                           false) +
                    dstR * getF(rgbDstBlend, red, preFogR, dstR, blendAlphaVal,
                                dstA, true));
            green = std::min(
                1.0f,
                green * getF(rgbSrcBlend, green, preFogG, dstG, blendAlphaVal,
                             dstA, false) +
                    dstG * getF(rgbDstBlend, green, preFogG, dstG,
                                blendAlphaVal, dstA, true));
            blue = std::min(
                1.0f,
                blue * getF(rgbSrcBlend, blue, preFogB, dstB, blendAlphaVal,
                            dstA, false) +
                    dstB * getF(rgbDstBlend, blue, preFogB, dstB, blendAlphaVal,
                                dstA, true));
            aVal = static_cast<uint32_t>(
                std::min(1.0f,
                         blendAlphaVal * getF(alphaSrcBlend, 0, 0, 0,
                                              blendAlphaVal, dstA, false) +
                             dstA * getF(alphaDstBlend, 0, 0, 0, blendAlphaVal,
                                         dstA, true)) *
                    255.0f +
                0.5f);
            rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
            gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
            bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
          } else {
            aVal = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
            rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
            gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
            bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
          }
          if constexpr (DitherEnabled) {
            static const uint8_t dither_matrix_4x4[16] = {
                0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            static const uint8_t dither_matrix_2x2[16] = {
                2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6};
            uint8_t dith = (state.ditherMode == 2)
                               ? dither_matrix_4x4[(y & 3) * 4 + (x & 3)]
                               : dither_matrix_2x2[(y & 3) * 4 + (x & 3)];
            uint32_t r = std::min(
                31u,
                (((rVal << 1) - (rVal >> 4) + (rVal >> 7) + dith) >> 1) >> 3);
            uint32_t g = std::min(
                63u,
                (((gVal << 2) - (gVal >> 4) + (gVal >> 6) + dith) >> 2) >> 2);
            uint32_t b = std::min(
                31u,
                (((bVal << 1) - (bVal >> 4) + (bVal >> 7) + dith) >> 1) >> 3);
            rVal = (r << 3) | (r >> 2);
            gVal = (g << 2) | (g >> 4);
            bVal = (b << 3) | (b >> 2);
          }
          uint32_t finalColor =
              (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
          if (!colorMaskRgb || !colorMaskAlpha) {
            uint32_t mask = (colorMaskRgb ? 0x00FFFFFF : 0) |
                            (colorMaskAlpha ? 0xFF000000 : 0);
            finalColor = (finalColor & mask) | (pixels[pIdx] & ~mask);
          }
          pixels[pIdx] = finalColor;
        }
      }
      E0 += a0;
      E1 += a1;
      E2 += a2;
    }
  }
}

void SoftwareBackend::RasterizeSoftwareTriangleTile(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int minX, int maxX, int minY, int maxY, const RasterizerState& state) {
  bool hasTexture = (state.boundTexAddress[0] != 0xFFFFFFFF);
  bool blendEnabled = (state.rgbSrcBlend != 4 || state.rgbDstBlend != 0);
  bool ditherEnabled = (state.ditherMode != 0);
  uint32_t key =
      (hasTexture ? 4 : 0) | (blendEnabled ? 2 : 0) | (ditherEnabled ? 1 : 0);
  switch (key) {
    case 0:
      RasterizeTriangleTemplate<false, false, false>(a, b, c, minX, maxX, minY,
                                                     maxY, state);
      break;
    case 1:
      RasterizeTriangleTemplate<false, false, true>(a, b, c, minX, maxX, minY,
                                                    maxY, state);
      break;
    case 2:
      RasterizeTriangleTemplate<false, true, false>(a, b, c, minX, maxX, minY,
                                                    maxY, state);
      break;
    case 3:
      RasterizeTriangleTemplate<false, true, true>(a, b, c, minX, maxX, minY,
                                                   maxY, state);
      break;
    case 4:
      RasterizeTriangleTemplate<true, false, false>(a, b, c, minX, maxX, minY,
                                                    maxY, state);
      break;
    case 5:
      RasterizeTriangleTemplate<true, false, true>(a, b, c, minX, maxX, minY,
                                                   maxY, state);
      break;
    case 6:
      RasterizeTriangleTemplate<true, true, false>(a, b, c, minX, maxX, minY,
                                                   maxY, state);
      break;
    case 7:
      RasterizeTriangleTemplate<true, true, true>(a, b, c, minX, maxX, minY,
                                                  maxY, state);
      break;
  }
}

}  // namespace GlideWrapper
