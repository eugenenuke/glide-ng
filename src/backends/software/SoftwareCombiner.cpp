#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

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

  // 2. Color Combiner
  uint32_t combinerColor = EvaluateCombinerColor(v, state);
  float red = ((combinerColor >> 16) & 0xFF) / 255.0f;
  float green = ((combinerColor >> 8) & 0xFF) / 255.0f;
  float blue = (combinerColor & 0xFF) / 255.0f;
  float alpha = ((combinerColor >> 24) & 0xFF) / 255.0f;

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
        eyeW = v.fog;
      } else {
        eyeW = 1.0f / v.pos[3];
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

    float fogR = ((state.fogColor >> 16) & 0xFF) / 255.0f;
    float fogG = ((state.fogColor >> 8) & 0xFF) / 255.0f;
    float fogB = (state.fogColor & 0xFF) / 255.0f;
    if (state.pixelFormatOverride == 1) {
      std::swap(fogR, fogB);
    }

    float mult = ((state.fogMode & 0x100) != 0) ? 1.0f : 0.0f;
    float add = ((state.fogMode & 0x200) != 0) ? 1.0f : 0.0f;

    red = red * (1.0f - mult) * (1.0f - f) + fogR * (1.0f - add) * f;
    green = green * (1.0f - mult) * (1.0f - f) + fogG * (1.0f - add) * f;
    blue = blue * (1.0f - mult) * (1.0f - f) + fogB * (1.0f - add) * f;
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

  uint32_t colorLocal = state.colorCombinerLocal & 0xFFFF;
  if (colorLocal == 1 || state.colorCombinerFactor == 1) {
    r = (state.constantColor >> 16) & 0xFF;
    g = (state.constantColor >> 8) & 0xFF;
    b = state.constantColor & 0xFF;
    if (state.pixelFormatOverride == 1) {
      std::swap(r, b);
    }
  }
  uint32_t alphaLocal = state.alphaCombinerLocal & 0xFFFF;
  if (alphaLocal == 1 || state.alphaCombinerFactor == 1) {
    a = (state.constantColor >> 24) & 0xFF;
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

    auto plot = [&](int px, int py, float c, float t) {
      if (steep) std::swap(px, py);
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] =
            v1_temp.color[i] * (1.0f - t) + v2_temp.color[i] * t;
      }
      vInterpolated.fog = v1_temp.fog * (1.0f - t) + v2_temp.fog * t;
      vInterpolated.pos[3] = v1_temp.pos[3] * (1.0f - t) + v2_temp.pos[3] * t;

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
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] = v1.color[i] * (1.0f - t) + v2.color[i] * t;
      }
      vInterpolated.fog = v1.fog * (1.0f - t) + v2.fog * t;
      vInterpolated.pos[3] = v1.pos[3] * (1.0f - t) + v2.pos[3] * t;

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
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] = v1.color[i] * (1.0f - t) + v2.color[i] * t;
      }
      vInterpolated.fog = v1.fog * (1.0f - t) + v2.fog * t;
      vInterpolated.pos[3] = v1.pos[3] * (1.0f - t) + v2.pos[3] * t;

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
