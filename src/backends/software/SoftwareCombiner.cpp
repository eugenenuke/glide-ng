#include "SoftwareBackend.h"
#include "SoftwareInternal.h"
#include "core/TextureManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends/software/SimdVec.h"

namespace GlideWrapper {

void SoftwareBackend::WritePixelPipeline(int x, int y, const ModernVertex& v,
                                         float coverage, int minX, int maxX,
                                         int minY, int maxY,
                                         const RasterizerState& state) {
  if (x < minX || x >= maxX || y < minY || y >= maxY) {
    return;
  }

  if (m_ssaaScale == 2) {
    const uint32_t sampleBit = ((static_cast<uint32_t>(y) & 1u) << 1) |
                               (static_cast<uint32_t>(x) & 1u);
    if (((state.tbufferWriteMask >> sampleBit) & 1u) == 0u) {
      return;
    }
  }

  // Stipple Transparency
  if (state.stippleMode != 0) {
    int stippleX = (state.stippleMode == 2) ? ((x + y) & 7) : (x & 7);
    int stippleY = y & 3;
    int stippleIndex = (stippleY << 3) | (7 - stippleX);
    if (((state.stipplePattern >> stippleIndex) & 1) == 0) {
      return;
    }
  }

  // 1. Guard non-positive W in W-buffer mode before any reciprocal
  if (state.depthMode == 2 && v.pos[3] <= 0.0f) {
    return;
  }

  // 2. Texture (TMU1 -> TMU0) & Color/Alpha Combiner (matching Vulkan/GLES & triangle rasterizer)
  const float iterR = v.color[0];
  const float iterG = v.color[1];
  const float iterB = v.color[2];
  const float iterA = v.color[3];
  const TmuColor iteratedColor = {iterR, iterG, iterB, iterA};

  float constantR = 0.0f, constantG = 0.0f, constantB = 0.0f,
        constantAlpha = 1.0f;
  UnpackGrColorNorm(state.constantColor, state.pixelFormatOverride, constantR,
                    constantG, constantB, constantAlpha);
  const TmuColor constantCol = {constantR, constantG, constantB, constantAlpha};

  auto unpackTmuConst = [&](uint32_t c) -> TmuColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    UnpackGrColorNorm(c, state.pixelFormatOverride, r, g, b, a);
    return {r, g, b, a};
  };
  const TmuColor tmuConst0 = unpackTmuConst(state.tmuConstantColor[0]);
  const TmuColor tmuConst1 = unpackTmuConst(state.tmuConstantColor[1]);

  const VirtualTexture* tex0 =
      (state.boundTexAddress[0] != 0xFFFFFFFFu) ? state.boundTex[0].get()
                                                : nullptr;
  const bool hasTex0 = (tex0 != nullptr && !tex0->swizzledMipLevels.empty());
  const VirtualTexture* tex1 =
      (state.boundTexAddress[1] != 0xFFFFFFFFu) ? state.boundTex[1].get()
                                                : nullptr;
  const bool hasTex1 = (tex1 != nullptr && !tex1->swizzledMipLevels.empty());

  const float divW0 =
      (state.stwHintMask & 0x2u) ? v.tmu_oow[0] : v.pos[3];
  const float divW1 =
      (state.stwHintMask & 0x8u) ? v.tmu_oow[1] : v.pos[3];

  // Upstream TMU 1 stage
  TmuColor tmu1Color = {1.0f, 1.0f, 1.0f, 1.0f};
  TmuColor tmu1Out = iteratedColor;
  if (hasTex1) {
    const float invW1 = (divW1 > 0.0f) ? (1.0f / divW1) : 1.0f;
    const uint32_t tWord1 = SampleTextureLevelSlow(
        tex1, 0, v.tex[2] * invW1, v.tex[3] * invW1, state.texClampS[1],
        state.texClampT[1], state.texMinFilter[1], 1u, state);
    tmu1Color.r = ((tWord1 >> 16) & 0xFFu) / 255.0f;
    tmu1Color.g = ((tWord1 >> 8) & 0xFFu) / 255.0f;
    tmu1Color.b = (tWord1 & 0xFFu) / 255.0f;
    tmu1Color.a = ((tWord1 >> 24) & 0xFFu) / 255.0f;
    if (state.texCombinerRgbFunc[1] == 1 &&
        state.texCombinerAlphaFunc[1] == 1 &&
        !state.texCombinerRgbInvert[1] &&
        !state.texCombinerAlphaInvert[1]) {
      tmu1Out = tmu1Color;
    } else {
      tmu1Out = EvaluateTmuStage(
          state.texCombinerRgbFunc[1], state.texCombinerRgbFactor[1],
          state.texCombinerAlphaFunc[1], state.texCombinerAlphaFactor[1],
          state.texCombinerRgbInvert[1], state.texCombinerAlphaInvert[1],
          tmu1Color, iteratedColor, iteratedColor, 0.0f, 0.0f);
    }
  }
  if ((state.extTexColorCombine[1] & 0x80000000u) != 0u) {
    EvaluateExtTmuRgbScalar(state.extTexColorCombine[1], tmu1Color,
                            iteratedColor, tmuConst1, iteratedColor, 0.0f,
                            0.0f, tmu1Out.r, tmu1Out.g, tmu1Out.b);
  }
  if ((state.extTexAlphaCombine[1] & 0x80000000u) != 0u) {
    tmu1Out.a = EvaluateExtTmuAlphaScalar(
        state.extTexAlphaCombine[1], tmu1Color, iteratedColor, tmuConst1,
        iteratedColor, 0.0f, 0.0f);
  }

  // Downstream TMU 0 stage
  TmuColor tmu0Color = {1.0f, 1.0f, 1.0f, 1.0f};
  TmuColor tmu0Out = tmu1Out;
  if (hasTex0) {
    const float invW0 = (divW0 > 0.0f) ? (1.0f / divW0) : 1.0f;
    const uint32_t tWord0 = SampleTextureLevelSlow(
        tex0, 0, v.tex[0] * invW0, v.tex[1] * invW0, state.texClampS[0],
        state.texClampT[0], state.texMinFilter[0], 0u, state);
    tmu0Color = {
        ((tWord0 >> 16) & 0xFFu) / 255.0f,
        ((tWord0 >> 8) & 0xFFu) / 255.0f,
        (tWord0 & 0xFFu) / 255.0f,
        ((tWord0 >> 24) & 0xFFu) / 255.0f,
    };
    if (state.texCombinerRgbFunc[0] == 1 &&
        state.texCombinerAlphaFunc[0] == 1 &&
        !state.texCombinerRgbInvert[0] &&
        !state.texCombinerAlphaInvert[0]) {
      tmu0Out = tmu0Color;
    } else {
      tmu0Out = EvaluateTmuStage(
          state.texCombinerRgbFunc[0], state.texCombinerRgbFactor[0],
          state.texCombinerAlphaFunc[0], state.texCombinerAlphaFactor[0],
          state.texCombinerRgbInvert[0], state.texCombinerAlphaInvert[0],
          tmu0Color, tmu1Out, iteratedColor, 0.0f, 0.0f);
    }
  }
  if ((state.extTexColorCombine[0] & 0x80000000u) != 0u) {
    EvaluateExtTmuRgbScalar(state.extTexColorCombine[0], tmu0Color, tmu1Out,
                            tmuConst0, iteratedColor, 0.0f, 0.0f, tmu0Out.r,
                            tmu0Out.g, tmu0Out.b);
  }
  if ((state.extTexAlphaCombine[0] & 0x80000000u) != 0u) {
    tmu0Out.a = EvaluateExtTmuAlphaScalar(
        state.extTexAlphaCombine[0], tmu0Color, tmu1Out, tmuConst0,
        iteratedColor, 0.0f, 0.0f);
  }

  const float texR = tmu0Out.r;
  const float texG = tmu0Out.g;
  const float texB = tmu0Out.b;
  const float texA = tmu0Out.a;

  // Alpha Combiner Stage (ACU)
  float aLocal = 1.0f;
  float aOther = 1.0f;
  float finalAlphaF = 0.0f;
  if ((state.extAlphaCombine & 0x80000000u) != 0u) {
    finalAlphaF = EvaluateExtFbiAlphaScalar(
        state.extAlphaCombine, iterA, constantAlpha, texA, aLocal, aOther);
  } else {
    aLocal = (state.alphaCombinerLocal == 0u) ? iterA : constantAlpha;
    aOther = (state.alphaCombinerOther == 0u)
                 ? iterA
                 : ((state.alphaCombinerOther == 1u) ? texA : constantAlpha);
    float factA = 0.0f;
    switch (state.alphaCombinerFactor) {
      case 1:
      case 3: factA = aLocal; break;
      case 2: factA = aOther; break;
      case 4: factA = texA; break;
      case 8: factA = 1.0f; break;
      case 9:
      case 11: factA = 1.0f - aLocal; break;
      case 10: factA = 1.0f - aOther; break;
      case 12: factA = 1.0f - texA; break;
    }
    switch (state.alphaCombinerFunc) {
      case 1: finalAlphaF = aLocal; break;
      case 3: finalAlphaF = aOther * factA; break;
      case 4:
      case 5: finalAlphaF = aOther * factA + aLocal; break;
      case 6: finalAlphaF = (aOther - aLocal) * factA; break;
      case 7:
      case 8: finalAlphaF = (aOther - aLocal) * factA + aLocal; break;
      case 9:
      case 16: finalAlphaF = (-aLocal) * factA + aLocal; break;
    }
    if (state.alphaCombinerInvert) finalAlphaF = 1.0f - finalAlphaF;
  }

  // Color Combiner Stage (CCU)
  float finalRF = 0.0f, finalGF = 0.0f, finalBF = 0.0f;
  if ((state.extColorCombine & 0x80000000u) != 0u) {
    EvaluateExtFbiColorScalar(state.extColorCombine, iteratedColor, constantCol,
                              tmu0Out, aLocal, aOther, finalRF, finalGF,
                              finalBF);
  } else {
    float cLocalR = 0.0f, cLocalG = 0.0f, cLocalB = 0.0f;
    if (state.colorCombinerLocal == 0u) {
      cLocalR = iterR;
      cLocalG = iterG;
      cLocalB = iterB;
    } else if (state.colorCombinerLocal == 1u) {
      cLocalR = constantR;
      cLocalG = constantG;
      cLocalB = constantB;
    }
    const float cOtherR =
        (state.colorCombinerOther == 0u)
            ? iterR
            : ((state.colorCombinerOther == 1u) ? texR : constantR);
    const float cOtherG =
        (state.colorCombinerOther == 0u)
            ? iterG
            : ((state.colorCombinerOther == 1u) ? texG : constantG);
    const float cOtherB =
        (state.colorCombinerOther == 0u)
            ? iterB
            : ((state.colorCombinerOther == 1u) ? texB : constantB);
    float factR = 0.0f, factG = 0.0f, factB = 0.0f;
    switch (state.colorCombinerFactor) {
      case 1: factR = cLocalR; factG = cLocalG; factB = cLocalB; break;
      case 2: factR = aOther; factG = aOther; factB = aOther; break;
      case 3: factR = aLocal; factG = aLocal; factB = aLocal; break;
      case 4: factR = texA; factG = texA; factB = texA; break;
      case 5: factR = texR; factG = texG; factB = texB; break;
      case 8: factR = 1.0f; factG = 1.0f; factB = 1.0f; break;
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
    switch (state.colorCombinerFunc) {
      case 1:
        finalRF = cLocalR; finalGF = cLocalG; finalBF = cLocalB;
        break;
      case 3:
        finalRF = cOtherR * factR;
        finalGF = cOtherG * factG;
        finalBF = cOtherB * factB;
        break;
      case 4:
        finalRF = cOtherR * factR + cLocalR;
        finalGF = cOtherG * factG + cLocalG;
        finalBF = cOtherB * factB + cLocalB;
        break;
      case 5:
        finalRF = cOtherR * factR + aLocal;
        finalGF = cOtherG * factG + aLocal;
        finalBF = cOtherB * factB + aLocal;
        break;
      case 6:
        finalRF = (cOtherR - cLocalR) * factR;
        finalGF = (cOtherG - cLocalG) * factG;
        finalBF = (cOtherB - cLocalB) * factB;
        break;
      case 7:
        finalRF = (cOtherR - cLocalR) * factR + cLocalR;
        finalGF = (cOtherG - cLocalG) * factG + cLocalG;
        finalBF = (cOtherB - cLocalB) * factB + cLocalB;
        break;
      case 8:
        finalRF = (cOtherR - cLocalR) * factR + aLocal;
        finalGF = (cOtherG - cLocalG) * factG + aLocal;
        finalBF = (cOtherB - cLocalB) * factB + aLocal;
        break;
      case 9:
        finalRF = (-cLocalR) * factR + cLocalR;
        finalGF = (-cLocalG) * factG + cLocalG;
        finalBF = (-cLocalB) * factB + cLocalB;
        break;
      case 16:
        finalRF = (-cLocalR) * factR + aLocal;
        finalGF = (-cLocalG) * factG + aLocal;
        finalBF = (-cLocalB) * factB + aLocal;
        break;
    }
    if (state.colorCombinerInvert) {
      finalRF = 1.0f - finalRF;
      finalGF = 1.0f - finalGF;
      finalBF = 1.0f - finalBF;
    }
  }

  float red = std::clamp(finalRF, 0.0f, 1.0f);
  float green = std::clamp(finalGF, 0.0f, 1.0f);
  float blue = std::clamp(finalBF, 0.0f, 1.0f);
  float alpha = std::clamp(finalAlphaF, 0.0f, 1.0f);

  // 2.5. Chromakey Emulation (compare TMU output when textured, else combined RGB)
  if (state.chromakeyMode == 1) {
    const bool hasAnyTex = hasTex0 || hasTex1;
    const float testR = hasAnyTex ? texR : red;
    const float testG = hasAnyTex ? texG : green;
    const float testB = hasAnyTex ? texB : blue;
    bool chromaFail = false;
    const bool rangeEnabled = ((state.chromakeyRangeMode >> 28) & 1u) == 1u;
    if (rangeEnabled) {
      float minR = 0.0f, minG = 0.0f, minB = 0.0f, minAUnused = 0.0f;
      float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f, maxAUnused = 0.0f;
      UnpackGrColorNorm(state.chromakeyRangeMin, state.pixelFormatOverride,
                        minR, minG, minB, minAUnused);
      UnpackGrColorNorm(state.chromakeyRangeMax, state.pixelFormatOverride,
                        maxR, maxG, maxB, maxAUnused);
      bool rMatch = (testR >= minR && testR <= maxR);
      bool gMatch = (testG >= minG && testG <= maxG);
      bool bMatch = (testB >= minB && testB <= maxB);
      bool rRes = ((state.chromakeyRangeMode >> 26) & 1u) ? !rMatch : rMatch;
      bool gRes = ((state.chromakeyRangeMode >> 25) & 1u) ? !gMatch : gMatch;
      bool bRes = ((state.chromakeyRangeMode >> 24) & 1u) ? !bMatch : bMatch;
      chromaFail = ((state.chromakeyRangeMode >> 27) & 1u)
                       ? (rRes || gRes || bRes)
                       : (rRes && gRes && bRes);
    } else {
      float chR = 0.0f, chG = 0.0f, chB = 0.0f, chAUnused = 0.0f;
      UnpackGrColorNorm(state.chromakeyValue, state.pixelFormatOverride, chR,
                        chG, chB, chAUnused);
      chromaFail = (std::abs(testR - chR) < 0.01f &&
                    std::abs(testG - chG) < 0.01f &&
                    std::abs(testB - chB) < 0.01f);
    }
    if (chromaFail) {
      return;
    }
  }

  // 2.6. Alpha Testing (evaluated prior to AA coverage modulation)
  if (state.alphaTestOp != 7) {
    const float alphaTestRef = static_cast<float>(state.alphaTestRefVal) / 255.0f;
    bool alphaPass = false;
    switch (state.alphaTestOp) {
      case 0: alphaPass = false; break;
      case 1: alphaPass = (alpha < alphaTestRef); break;
      case 2: alphaPass = (alpha == alphaTestRef); break;
      case 3: alphaPass = (alpha <= alphaTestRef); break;
      case 4: alphaPass = (alpha > alphaTestRef); break;
      case 5: alphaPass = (alpha != alphaTestRef); break;
      case 6: alphaPass = (alpha >= alphaTestRef); break;
      default: alphaPass = true; break;
    }
    if (!alphaPass) {
      return;
    }
  }

  alpha *= coverage;

  // Capture pre-fog color for GR_BLEND_PREFOG_COLOR
  const float preFogR = red;
  const float preFogG = green;
  const float preFogB = blue;

  // 3. Fogging
  uint32_t fogSource = state.fogMode & 0x0F;
  if (fogSource != 0) {
    float f = 0.0f;
    if (fogSource == 4) {
      f = v.color[3];
    } else {
      float eyeW = 0.0f;
      if (fogSource == 1) {
        // Authentic 3dfx GR_PARAM_FOG_EXT supplies reciprocal 1/W_fog.
        eyeW = 1.0f / std::max(v.fog, 1e-6f);
      } else {
        eyeW = 1.0f / std::max(v.pos[3], 1e-6f);
      }
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

    float fogR = 0.0f, fogG = 0.0f, fogB = 0.0f, fogAUnused = 1.0f;
    UnpackGrColorNorm(state.fogColor, state.pixelFormatOverride, fogR, fogG,
                      fogB, fogAUnused);

    float mult = ((state.fogMode & 0x100) != 0) ? 1.0f : 0.0f;
    float add = ((state.fogMode & 0x200) != 0) ? 1.0f : 0.0f;

    red = red * (1.0f - mult) * (1.0f - f) + fogR * (1.0f - add) * f;
    green = green * (1.0f - mult) * (1.0f - f) + fogG * (1.0f - add) * f;
    blue = blue * (1.0f - mult) * (1.0f - f) + fogB * (1.0f - add) * f;
  }

  const size_t pIdx = static_cast<size_t>(y) * m_headlessWidth +
                      static_cast<size_t>(x);

  // 3.5. Hardware Stencil Testing (before Depth Test)
  uint8_t* stencilBuffer = m_headlessStencilBuffer.empty()
                               ? nullptr
                               : m_headlessStencilBuffer.data();
  if (state.stencilEnabled && stencilBuffer) {
    const uint8_t sVal = stencilBuffer[pIdx];
    if (!EvaluateStencilCompare(state.stencilCompareOp, state.stencilRef,
                                state.stencilReadMask, sVal)) {
      if (state.stencilFailOp != 0u && state.stencilWriteMask != 0u) {
        stencilBuffer[pIdx] = ApplyStencilOp(
            state.stencilFailOp, sVal, state.stencilRef,
            state.stencilWriteMask);
      }
      return;
    }
  }

  // 3.6. Depth Testing & Depth Write + Stencil ZFail/ZPass Update
  float* depthBuffer =
      m_headlessDepthBuffer.empty() ? nullptr : m_headlessDepthBuffer.data();
  if (state.depthMode != 0 && depthBuffer &&
      (state.depthCompareOp != 7 || state.depthMask)) {
    float targetDepth = 0.0f;
    const bool compareToBias = (state.depthMode == 3 || state.depthMode == 4);
    const bool isWBuffer = (state.depthMode == 2 || state.depthMode == 4);
    if (isWBuffer) {  // GR_DEPTHBUFFER_WBUFFER / COMPARE_TO_BIAS
      const float depthRange = state.depthFar - state.depthNear;
      const float depthRangeInv =
          (std::abs(depthRange) > 1e-6f) ? (1.0f / depthRange) : 0.0f;
      const float w = (v.pos[3] > 0.0f) ? (1.0f / v.pos[3]) : state.depthFar;
      targetDepth = std::clamp((w - state.depthNear) * depthRangeInv, 0.0f, 1.0f);
    } else {
      targetDepth = std::clamp(v.pos[2], 0.0f, 1.0f);
    }
    float cmpDepth = targetDepth;
    if (compareToBias) {
      cmpDepth = DecodeAuxDepth16ToNorm(
          static_cast<uint16_t>(state.depthBiasLevel), state.depthMode,
          state.depthNear, state.depthFar);
    } else if (state.depthBiasLevel != 0) {
      targetDepth = std::clamp(
          targetDepth + static_cast<float>(state.depthBiasLevel) / 65535.0f,
          0.0f, 1.0f);
      cmpDepth = targetDepth;
    }

    bool depthPass = true;
    if (state.depthCompareOp != 7) {
      const float currentDepth = depthBuffer[pIdx];
      switch (state.depthCompareOp) {
        case 0: depthPass = false; break;
        case 1: depthPass = (cmpDepth < currentDepth); break;
        case 2: depthPass = (cmpDepth == currentDepth); break;
        case 3: depthPass = (cmpDepth <= currentDepth); break;
        case 4: depthPass = (cmpDepth > currentDepth); break;
        case 5: depthPass = (cmpDepth != currentDepth); break;
        case 6: depthPass = (cmpDepth >= currentDepth); break;
        default: depthPass = true; break;
      }
    }

    if (state.stencilEnabled && stencilBuffer && state.stencilWriteMask != 0u) {
      const uint32_t op =
          depthPass ? state.stencilZPassOp : state.stencilZFailOp;
      if (op != 0u) {
        stencilBuffer[pIdx] = ApplyStencilOp(
            op, stencilBuffer[pIdx], state.stencilRef, state.stencilWriteMask);
      }
    }

    if (!depthPass) {
      return;
    }
    if (state.depthMask) {
      depthBuffer[pIdx] = targetDepth;
    }
  } else if (state.stencilEnabled && stencilBuffer &&
             state.stencilZPassOp != 0u && state.stencilWriteMask != 0u) {
    stencilBuffer[pIdx] = ApplyStencilOp(
        state.stencilZPassOp, stencilBuffer[pIdx], state.stencilRef,
        state.stencilWriteMask);
  }

  // Preserve full 8-bit alpha into blending; RGB565 dithering runs
  // post-blend on color channels only (authentic 3dfx pipeline order).
  float blendAlphaVal = alpha;

  uint32_t aVal =
      static_cast<uint32_t>(std::min(255.0f, blendAlphaVal * 255.0f + 0.5f));

  // 4. Alpha Blending
  uint32_t* pixels = reinterpret_cast<uint32_t*>(
      m_cpuBuffers[state.activeRenderBufferIdx].data());
  uint32_t dstWord = pixels[y * m_headlessWidth + x];
  float dstR = ((dstWord >> 16) & 0xFF) / 255.0f;
  float dstG = ((dstWord >> 8) & 0xFF) / 255.0f;
  float dstB = (dstWord & 0xFF) / 255.0f;
  float dstA = ((dstWord >> 24) & 0xFF) / 255.0f;

  auto getF = [](uint32_t mode, float srcC, float preFogC, float dstC,
                 float srcA, float dstA, bool isDest) -> float {
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
      case 8:
        return isDest ? dstC : srcC;
      case 9:
        return isDest ? (1.0f - dstC) : (1.0f - srcC);
      case 15:
        return isDest ? preFogC : std::min(srcA, 1.0f - dstA);
      default:
        return isDest ? 0.0f : 1.0f;
    }
  };

  float outR = ApplyBlendOpScalar(
      state.rgbBlendOp,
      red * getF(state.rgbSrcBlend, red, preFogR, dstR, blendAlphaVal, dstA,
                 false),
      dstR * getF(state.rgbDstBlend, red, preFogR, dstR, blendAlphaVal, dstA,
                  true));
  float outG = ApplyBlendOpScalar(
      state.rgbBlendOp,
      green * getF(state.rgbSrcBlend, green, preFogG, dstG, blendAlphaVal, dstA,
                   false),
      dstG * getF(state.rgbDstBlend, green, preFogG, dstG, blendAlphaVal, dstA,
                  true));
  float outB = ApplyBlendOpScalar(
      state.rgbBlendOp,
      blue * getF(state.rgbSrcBlend, blue, preFogB, dstB, blendAlphaVal, dstA,
                  false),
      dstB * getF(state.rgbDstBlend, blue, preFogB, dstB, blendAlphaVal, dstA,
                  true));
  float outA = ApplyBlendOpScalar(
      state.alphaBlendOp,
      blendAlphaVal * getF(state.alphaSrcBlend, blendAlphaVal, blendAlphaVal,
                           dstA, blendAlphaVal, dstA, false),
      dstA * getF(state.alphaDstBlend, blendAlphaVal, blendAlphaVal, dstA,
                  blendAlphaVal, dstA, true));

  uint32_t finalR = static_cast<uint32_t>(outR * 255.0f + 0.5f);
  uint32_t finalG = static_cast<uint32_t>(outG * 255.0f + 0.5f);
  uint32_t finalB = static_cast<uint32_t>(outB * 255.0f + 0.5f);
  uint32_t finalA =
      (state.alphaSrcBlend == 4 && state.alphaDstBlend == 0 &&
       state.alphaBlendOp == 0)
          ? aVal
          : std::min(255u, static_cast<uint32_t>(outA * 255.0f + 0.5f));

  finalR = std::min(255u, finalR);
  finalG = std::min(255u, finalG);
  finalB = std::min(255u, finalB);

  if (state.ditherMode != 0) {
    static const uint8_t dither_matrix_4x4[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                                  3, 11, 1, 9,  15, 7, 13, 5};
    static const uint8_t dither_matrix_2x2[16] = {2, 10, 2, 10, 14, 6, 14, 6,
                                                  2, 10, 2, 10, 14, 6, 14, 6};
    int ditherX = x & 3;
    int ditherY = y & 3;
    uint8_t dith =
        (state.ditherMode == 2)   ? dither_matrix_4x4[ditherY * 4 + ditherX]
        : (state.ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX]
                                  : 0;

    uint32_t ditheredR =
        (((finalR << 1) - (finalR >> 4) + (finalR >> 7) + dith) >> 1);
    uint32_t quantizedR = std::min(31u, ditheredR >> 3);

    uint32_t ditheredG =
        (((finalG << 2) - (finalG >> 4) + (finalG >> 6) + dith) >> 2);
    uint32_t quantizedG = std::min(63u, ditheredG >> 2);

    uint32_t ditheredB =
        (((finalB << 1) - (finalB >> 4) + (finalB >> 7) + dith) >> 1);
    uint32_t quantizedB = std::min(31u, ditheredB >> 3);

    finalR = (quantizedR << 3) | (quantizedR >> 2);
    finalG = (quantizedG << 2) | (quantizedG >> 4);
    finalB = (quantizedB << 3) | (quantizedB >> 2);
  }

  uint32_t finalColor =
      (finalA << 24) | (finalR << 16) | (finalG << 8) | finalB;

  if (!state.colorMaskR || !state.colorMaskG || !state.colorMaskB ||
      !state.colorMaskAlpha) {
    uint32_t mask = (state.colorMaskR ? 0x00FF0000u : 0u) |
                    (state.colorMaskG ? 0x0000FF00u : 0u) |
                    (state.colorMaskB ? 0x000000FFu : 0u) |
                    (state.colorMaskAlpha ? 0xFF000000u : 0u);
    finalColor = (finalColor & mask) | (dstWord & ~mask);
  }

  pixels[y * m_headlessWidth + x] = finalColor;
}

uint32_t SoftwareBackend::EvaluateCombinerColor(const ModernVertex& v,
                                                const RasterizerState& state) {
  uint32_t a = static_cast<uint32_t>(v.color[3] * 255.0f + 0.5f);
  uint32_t r = static_cast<uint32_t>(v.color[0] * 255.0f + 0.5f);
  uint32_t g = static_cast<uint32_t>(v.color[1] * 255.0f + 0.5f);
  uint32_t b = static_cast<uint32_t>(v.color[2] * 255.0f + 0.5f);

  const uint32_t constArgb =
      GrColorToCanonicalArgb(state.constantColor, state.pixelFormatOverride);
  uint32_t colorLocal = state.colorCombinerLocal & 0xFFFF;
  if (colorLocal == 1 || state.colorCombinerFactor == 1) {
    r = (constArgb >> 16) & 0xFF;
    g = (constArgb >> 8) & 0xFF;
    b = constArgb & 0xFF;
  }
  uint32_t alphaLocal = state.alphaCombinerLocal & 0xFFFF;
  if (alphaLocal == 1 || state.alphaCombinerFactor == 1) {
    a = (constArgb >> 24) & 0xFF;
  }

  if (a < 128) a = 255;

  return (a << 24) | (r << 16) | (g << 8) | b;
}

void SoftwareBackend::RasterizeSoftwareLineTile(const ModernVertex& v1,
                                                const ModernVertex& v2,
                                                int minX, int maxX, int minY,
                                                int maxY,
                                                const RasterizerState& state) {
  if (state.aaEnabled) {
    float x0_f = v1.pos[0];
    float y0_f = v1.pos[1];
    float x1_f = v2.pos[0];
    float y1_f = v2.pos[1];

    bool steep = std::abs(y1_f - y0_f) > std::abs(x1_f - x0_f);
    if (steep) {
      std::swap(x0_f, y0_f);
      std::swap(x1_f, y1_f);
    }
    ModernVertex v1_temp = v1;
    ModernVertex v2_temp = v2;
    if (x0_f > x1_f) {
      std::swap(x0_f, x1_f);
      std::swap(y0_f, y1_f);
      std::swap(v1_temp, v2_temp);
    }

    float dx = x1_f - x0_f;
    float dy = y1_f - y0_f;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    auto lerpLineVertex = [](const ModernVertex& a, const ModernVertex& b,
                             float t) -> ModernVertex {
      ModernVertex out{};
      const float omt = 1.0f - t;
      for (int i = 0; i < 4; ++i) {
        out.color[i] = a.color[i] * omt + b.color[i] * t;
        out.tex[i] = a.tex[i] * omt + b.tex[i] * t;
      }
      out.tmu_oow[0] = a.tmu_oow[0] * omt + b.tmu_oow[0] * t;
      out.tmu_oow[1] = a.tmu_oow[1] * omt + b.tmu_oow[1] * t;
      out.fog = a.fog * omt + b.fog * t;
      out.pos[2] = a.pos[2] * omt + b.pos[2] * t;
      out.pos[3] = a.pos[3] * omt + b.pos[3] * t;
      return out;
    };

    auto plot = [&](int px, int py, float c, float t) {
      if (steep) std::swap(px, py);
      ModernVertex vInterpolated = lerpLineVertex(v1_temp, v2_temp, t);
      WritePixelPipeline(px, py, vInterpolated, c, minX, maxX, minY, maxY,
                         state);
    };

    float intery = y0_f;
    for (int x = static_cast<int>(std::round(x0_f));
         x <= static_cast<int>(std::round(x1_f)); ++x) {
      float t = (dx == 0.0f) ? 0.0f : (x - x0_f) / dx;
      t = std::max(0.0f, std::min(1.0f, t));
      int ipart = static_cast<int>(std::floor(intery));
      float fpart = intery - ipart;
      plot(x, ipart, 1.0f - fpart, t);
      plot(x, ipart + 1, fpart, t);
      intery += gradient;
    }
  } else if (m_ssaaScale == 2 && !state.aaMultiSample) {
    int x0 = static_cast<int>(v1.pos[0] * 0.5f);
    int y0 = static_cast<int>(v1.pos[1] * 0.5f);
    int x1 = static_cast<int>(v2.pos[0] * 0.5f);
    int y1 = static_cast<int>(v2.pos[1] * 0.5f);

    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
      float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
      const float omt = 1.0f - t;
      ModernVertex vInterpolated{};
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] = v1.color[i] * omt + v2.color[i] * t;
        vInterpolated.tex[i] = v1.tex[i] * omt + v2.tex[i] * t;
      }
      vInterpolated.tmu_oow[0] = v1.tmu_oow[0] * omt + v2.tmu_oow[0] * t;
      vInterpolated.tmu_oow[1] = v1.tmu_oow[1] * omt + v2.tmu_oow[1] * t;
      vInterpolated.fog = v1.fog * omt + v2.fog * t;
      vInterpolated.pos[2] = v1.pos[2] * omt + v2.pos[2] * t;
      vInterpolated.pos[3] = v1.pos[3] * omt + v2.pos[3] * t;

      const int bx = x0 * 2;
      const int by = y0 * 2;
      WritePixelPipeline(bx, by, vInterpolated, 1.0f, minX, maxX, minY, maxY,
                         state);
      WritePixelPipeline(bx + 1, by, vInterpolated, 1.0f, minX, maxX, minY,
                         maxY, state);
      WritePixelPipeline(bx, by + 1, vInterpolated, 1.0f, minX, maxX, minY,
                         maxY, state);
      WritePixelPipeline(bx + 1, by + 1, vInterpolated, 1.0f, minX, maxX, minY,
                         maxY, state);

      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
      step++;
    }
  } else {
    int x0 = static_cast<int>(v1.pos[0]);
    int y0 = static_cast<int>(v1.pos[1]);
    int x1 = static_cast<int>(v2.pos[0]);
    int y1 = static_cast<int>(v2.pos[1]);

    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
      float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
      const float omt = 1.0f - t;
      ModernVertex vInterpolated{};
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] = v1.color[i] * omt + v2.color[i] * t;
        vInterpolated.tex[i] = v1.tex[i] * omt + v2.tex[i] * t;
      }
      vInterpolated.tmu_oow[0] = v1.tmu_oow[0] * omt + v2.tmu_oow[0] * t;
      vInterpolated.tmu_oow[1] = v1.tmu_oow[1] * omt + v2.tmu_oow[1] * t;
      vInterpolated.fog = v1.fog * omt + v2.fog * t;
      vInterpolated.pos[2] = v1.pos[2] * omt + v2.pos[2] * t;
      vInterpolated.pos[3] = v1.pos[3] * omt + v2.pos[3] * t;

      WritePixelPipeline(x0, y0, vInterpolated, 1.0f, minX, maxX, minY, maxY,
                         state);

      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
      step++;
    }
  }
}

void SoftwareBackend::RasterizeSoftwarePointTile(const ModernVertex& pt,
                                                 int minX, int maxX, int minY,
                                                 int maxY,
                                                 const RasterizerState& state) {
  if (state.aaEnabled) {
    float x_f = pt.pos[0];
    float y_f = pt.pos[1];
    float r_max = 0.75f * static_cast<float>(m_ssaaScale);

    int pmin_x = static_cast<int>(std::floor(x_f - r_max));
    int pmax_x = static_cast<int>(std::ceil(x_f + r_max));
    int pmin_y = static_cast<int>(std::floor(y_f - r_max));
    int pmax_y = static_cast<int>(std::ceil(y_f + r_max));

    for (int py = pmin_y; py <= pmax_y; ++py) {
      for (int px = pmin_x; px <= pmax_x; ++px) {
        float dx = (static_cast<float>(px) + 0.5f) - x_f;
        float dy = (static_cast<float>(py) + 0.5f) - y_f;
        float d = std::sqrt(dx * dx + dy * dy);
        float C = 1.0f - (d / r_max);
        if (C > 0.0f) {
          C = std::max(0.0f, std::min(1.0f, C));
          WritePixelPipeline(px, py, pt, C, minX, maxX, minY, maxY, state);
        }
      }
    }
  } else if (m_ssaaScale == 2 && !state.aaMultiSample) {
    int bx = static_cast<int>(pt.pos[0] * 0.5f) * 2;
    int by = static_cast<int>(pt.pos[1] * 0.5f) * 2;
    WritePixelPipeline(bx, by, pt, 1.0f, minX, maxX, minY, maxY, state);
    WritePixelPipeline(bx + 1, by, pt, 1.0f, minX, maxX, minY, maxY, state);
    WritePixelPipeline(bx, by + 1, pt, 1.0f, minX, maxX, minY, maxY, state);
    WritePixelPipeline(bx + 1, by + 1, pt, 1.0f, minX, maxX, minY, maxY, state);
  } else {
    int x = static_cast<int>(pt.pos[0]);
    int y = static_cast<int>(pt.pos[1]);
    WritePixelPipeline(x, y, pt, 1.0f, minX, maxX, minY, maxY, state);
  }
}

}  // namespace GlideWrapper
