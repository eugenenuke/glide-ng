#include "CpuLfbStaging.h"

#include <algorithm>
#include <cstring>

#include "core/Logger.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace GlideWrapper {

namespace {

inline uint16_t Color24ToRgb565(uint32_t color, uint32_t pixelFormatOverride) {
  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;
  if (pixelFormatOverride == 1) {  // GR_COLORFORMAT_ABGR
    std::swap(r, b);
  }
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t Color24ToRgb555(uint32_t color, uint32_t pixelFormatOverride) {
  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;
  if (pixelFormatOverride == 1) {  // GR_COLORFORMAT_ABGR
    std::swap(r, b);
  }
  return static_cast<uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

inline uint32_t Color24ToRgb888(uint32_t color, uint32_t pixelFormatOverride) {
  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;
  if (pixelFormatOverride == 1) {  // GR_COLORFORMAT_ABGR
    std::swap(r, b);
  }
  return (r << 16) | (g << 8) | b;
}

inline void EvalRgbBlendFactor(uint32_t factorEnum, bool isSrc, uint32_t srcA,
                               uint32_t dstA, uint32_t srcR, uint32_t srcG,
                               uint32_t srcB, uint32_t dstR, uint32_t dstG,
                               uint32_t dstB, float& fR, float& fG,
                               float& fB) {
  switch (factorEnum) {
    case 0:  // GR_BLEND_ZERO
      fR = fG = fB = 0.0f;
      return;
    case 1:  // GR_BLEND_SRC_ALPHA
      fR = fG = fB = srcA / 255.0f;
      return;
    case 2:  // GR_BLEND_DST_COLOR (src) / GR_BLEND_SRC_COLOR (dst)
      if (isSrc) {
        fR = dstR / 255.0f;
        fG = dstG / 255.0f;
        fB = dstB / 255.0f;
      } else {
        fR = srcR / 255.0f;
        fG = srcG / 255.0f;
        fB = srcB / 255.0f;
      }
      return;
    case 3:  // GR_BLEND_DST_ALPHA
      fR = fG = fB = dstA / 255.0f;
      return;
    case 4:  // GR_BLEND_ONE
      fR = fG = fB = 1.0f;
      return;
    case 5:  // GR_BLEND_ONE_MINUS_SRC_ALPHA
      fR = fG = fB = 1.0f - (srcA / 255.0f);
      return;
    case 6:  // GR_BLEND_ONE_MINUS_DST_COLOR (src) / ONE_MINUS_SRC_COLOR (dst)
      if (isSrc) {
        fR = 1.0f - (dstR / 255.0f);
        fG = 1.0f - (dstG / 255.0f);
        fB = 1.0f - (dstB / 255.0f);
      } else {
        fR = 1.0f - (srcR / 255.0f);
        fG = 1.0f - (srcG / 255.0f);
        fB = 1.0f - (srcB / 255.0f);
      }
      return;
    case 7:  // GR_BLEND_ONE_MINUS_DST_ALPHA
      fR = fG = fB = 1.0f - (dstA / 255.0f);
      return;
    default:
      fR = fG = fB = isSrc ? 1.0f : 0.0f;
      return;
  }
}

template <uint32_t SrcFmt, bool ChromakeyEnabled, bool BlendEnabled,
          bool HasRefData>
struct LfbRowProcessor {
  static inline void ProcessRow(uint32_t y, uint32_t dstX, uint32_t dstY,
                                uint32_t srcWidth, uint32_t fbWidth,
                                uint32_t fbHeight,
                                uint32_t pixelFormatOverride, uint32_t* pixels,
                                const uint8_t* inBytes, const uint8_t* refBytes,
                                int32_t srcStride,
                                const LfbPipelineConfig& config,
                                uint32_t& rowModifiedCount) {
    uint32_t writeY = dstY + y;
    if (writeY >= fbHeight) return;

    const std::ptrdiff_t rowOffset =
        static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(srcStride);
    const uint8_t* rowIn = inBytes + rowOffset;
    const uint8_t* rowRef = HasRefData ? (refBytes + rowOffset) : nullptr;

    uint16_t chroma16 = 0;
    uint32_t chroma24 = 0;
    if constexpr (ChromakeyEnabled) {
      if constexpr (SrcFmt == 0 || SrcFmt == 12) {
        chroma16 = Color24ToRgb565(config.chromakeyValue, pixelFormatOverride);
      } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                           SrcFmt == 14) {
        chroma16 = Color24ToRgb555(config.chromakeyValue, pixelFormatOverride);
      } else {
        chroma24 = Color24ToRgb888(config.chromakeyValue, pixelFormatOverride);
      }
    }

    for (uint32_t x = 0; x < srcWidth; ++x) {
      uint32_t writeX = dstX + x;
      if (writeX >= fbWidth) break;

      // 1. Extract raw source pixel
      constexpr bool is16Bit =
          (SrcFmt == 0 || SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 12 ||
           SrcFmt == 13 || SrcFmt == 14 || SrcFmt == 15);
      uint16_t c16 = 0;
      uint32_t c32 = 0;
      if constexpr (is16Bit) {
        if constexpr (SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 14) {
          c16 = static_cast<uint16_t>(
              reinterpret_cast<const uint32_t*>(rowIn)[x] & 0xFFFF);
        } else {
          c16 = reinterpret_cast<const uint16_t*>(rowIn)[x];
        }
      } else {
        c32 = reinterpret_cast<const uint32_t*>(rowIn)[x];
      }

      // 2. Dirty tracking check
      if constexpr (HasRefData) {
        if constexpr (is16Bit) {
          uint16_t ref16;
          if constexpr (SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 14) {
            ref16 = static_cast<uint16_t>(
                reinterpret_cast<const uint32_t*>(rowRef)[x] & 0xFFFF);
          } else {
            ref16 = reinterpret_cast<const uint16_t*>(rowRef)[x];
          }
          if (c16 == ref16) continue;
        } else {
          uint32_t ref32 = reinterpret_cast<const uint32_t*>(rowRef)[x];
          if (c32 == ref32) continue;
        }
      }

      // 3. Chroma Keying check
      if constexpr (ChromakeyEnabled) {
        if constexpr (SrcFmt == 0 || SrcFmt == 12) {
          if (c16 == chroma16) continue;
        } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                             SrcFmt == 14) {
          if ((c16 & 0x7FFFu) == chroma16) continue;
        } else if constexpr (!is16Bit) {
          if ((c32 & 0x00FFFFFFu) == chroma24) continue;
        }
      }

      // 4. Unpack pixel to 32-bit ARGB
      uint32_t pWord = 0xFF000000;
      if constexpr (SrcFmt == 0 || SrcFmt == 12) {
        uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
        uint32_t g = ((c16 >> 5) & 0x3F) * 255 / 63;
        uint32_t b = (c16 & 0x1F) * 255 / 31;
        pWord = (255u << 24) | (r << 16) | (g << 8) | b;
      } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                           SrcFmt == 14) {
        uint32_t r = ((c16 >> 10) & 0x1F) * 255 / 31;
        uint32_t g = ((c16 >> 5) & 0x1F) * 255 / 31;
        uint32_t b = (c16 & 0x1F) * 255 / 31;
        uint32_t a =
            (SrcFmt == 2 || SrcFmt == 14) ? (((c16 >> 15) & 1) ? 255 : 0) : 255;
        pWord = (a << 24) | (r << 16) | (g << 8) | b;
      } else {
        // Glide LFB 888/8888 pixel words are always 0x00RRGGBB / 0xAARRGGBB
        // regardless of grSstWinOpen's GrColorFormat_t (which only governs
        // GrColor_t API parameters).
        (void)pixelFormatOverride;
        uint32_t r = ((c32 >> 16) & 0xFF) >> 3;
        uint32_t g = ((c32 >> 8) & 0xFF) >> 2;
        uint32_t b = (c32 & 0xFF) >> 3;
        uint32_t r8 = r * 255 / 31;
        uint32_t g8 = g * 255 / 63;
        uint32_t b8 = b * 255 / 31;
        uint32_t a8 = (c32 >> 24) & 0xFF;
        if constexpr (SrcFmt == 4 || SrcFmt == 15) {
          a8 = 255;
        }
        pWord = (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
      }

      // 5. Alpha Blending
      if constexpr (BlendEnabled) {
        uint32_t srcA = (pWord >> 24) & 0xFF;
        uint32_t srcR = (pWord >> 16) & 0xFF;
        uint32_t srcG = (pWord >> 8) & 0xFF;
        uint32_t srcB = pWord & 0xFF;

        if constexpr (SrcFmt == 0 || SrcFmt == 1 || SrcFmt == 4 ||
                      SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 15) {
          srcA = config.constantAlpha;
        }

        uint32_t dstWord = pixels[writeY * fbWidth + writeX];
        uint32_t dstA = (dstWord >> 24) & 0xFF;
        uint32_t dstR = (dstWord >> 16) & 0xFF;
        uint32_t dstG = (dstWord >> 8) & 0xFF;
        uint32_t dstB = dstWord & 0xFF;

        float sR, sG, sB, dR, dG, dB;
        EvalRgbBlendFactor(config.rgbSrcBlend, true, srcA, dstA, srcR, srcG,
                           srcB, dstR, dstG, dstB, sR, sG, sB);
        EvalRgbBlendFactor(config.rgbDstBlend, false, srcA, dstA, srcR, srcG,
                           srcB, dstR, dstG, dstB, dR, dG, dB);

        uint32_t finalR =
            static_cast<uint32_t>(srcR * sR + dstR * dR + 0.5f);
        uint32_t finalG =
            static_cast<uint32_t>(srcG * sG + dstG * dG + 0.5f);
        uint32_t finalB =
            static_cast<uint32_t>(srcB * sB + dstB * dB + 0.5f);

        finalR = std::min(255u, finalR);
        finalG = std::min(255u, finalG);
        finalB = std::min(255u, finalB);

        pWord = (255u << 24) | (finalR << 16) | (finalG << 8) | finalB;
      }

      pixels[writeY * fbWidth + writeX] = pWord;
      rowModifiedCount++;
    }
  }
};

template <uint32_t SrcFmt, bool ChromakeyEnabled, bool AlphaBlendOverlay,
          bool HasRefData>
struct LfbOverlayRowProcessor {
  static inline void ProcessRow(uint32_t y, uint32_t dstX, uint32_t dstY,
                                uint32_t srcWidth, uint32_t fbWidth,
                                uint32_t fbHeight,
                                uint32_t pixelFormatOverride, uint32_t* overlay,
                                const uint8_t* inBytes, const uint8_t* refBytes,
                                int32_t srcStride,
                                const LfbPipelineConfig& config,
                                uint32_t& rowModifiedCount) {
    uint32_t writeY = dstY + y;
    if (writeY >= fbHeight) return;

    const std::ptrdiff_t rowOffset =
        static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(srcStride);
    const uint8_t* rowIn = inBytes + rowOffset;
    const uint8_t* rowRef = HasRefData ? (refBytes + rowOffset) : nullptr;

    uint16_t chroma16 = 0;
    uint32_t chroma24 = 0;
    if constexpr (ChromakeyEnabled) {
      if constexpr (SrcFmt == 0 || SrcFmt == 12) {
        chroma16 = Color24ToRgb565(config.chromakeyValue, pixelFormatOverride);
      } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                           SrcFmt == 14) {
        chroma16 = Color24ToRgb555(config.chromakeyValue, pixelFormatOverride);
      } else {
        chroma24 = Color24ToRgb888(config.chromakeyValue, pixelFormatOverride);
      }
    }

    uint32_t* dstRow = overlay + static_cast<size_t>(writeY) * fbWidth;
    for (uint32_t x = 0; x < srcWidth; ++x) {
      uint32_t writeX = dstX + x;
      if (writeX >= fbWidth) break;

      constexpr bool is16Bit =
          (SrcFmt == 0 || SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 12 ||
           SrcFmt == 13 || SrcFmt == 14 || SrcFmt == 15);
      uint16_t c16 = 0;
      uint32_t c32 = 0;
      if constexpr (is16Bit) {
        if constexpr (SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 14) {
          c16 = static_cast<uint16_t>(
              reinterpret_cast<const uint32_t*>(rowIn)[x] & 0xFFFF);
        } else {
          c16 = reinterpret_cast<const uint16_t*>(rowIn)[x];
        }
      } else {
        c32 = reinterpret_cast<const uint32_t*>(rowIn)[x];
      }

      if constexpr (HasRefData) {
        if constexpr (is16Bit) {
          uint16_t ref16;
          if constexpr (SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 14) {
            ref16 = static_cast<uint16_t>(
                reinterpret_cast<const uint32_t*>(rowRef)[x] & 0xFFFF);
          } else {
            ref16 = reinterpret_cast<const uint16_t*>(rowRef)[x];
          }
          if (c16 == ref16) continue;
        } else {
          uint32_t ref32 = reinterpret_cast<const uint32_t*>(rowRef)[x];
          if (c32 == ref32) continue;
        }
      }

      if constexpr (ChromakeyEnabled) {
        if constexpr (SrcFmt == 0 || SrcFmt == 12) {
          if (c16 == chroma16) continue;
        } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                             SrcFmt == 14) {
          if ((c16 & 0x7FFFu) == chroma16) continue;
        } else if constexpr (!is16Bit) {
          if ((c32 & 0x00FFFFFFu) == chroma24) continue;
        }
      }

      uint32_t r = 0, g = 0, b = 0, a = 255;
      if constexpr (SrcFmt == 0 || SrcFmt == 12) {
        r = ((c16 >> 11) & 0x1F) * 255 / 31;
        g = ((c16 >> 5) & 0x3F) * 255 / 63;
        b = (c16 & 0x1F) * 255 / 31;
      } else if constexpr (SrcFmt == 1 || SrcFmt == 2 || SrcFmt == 13 ||
                           SrcFmt == 14) {
        r = ((c16 >> 10) & 0x1F) * 255 / 31;
        g = ((c16 >> 5) & 0x1F) * 255 / 31;
        b = (c16 & 0x1F) * 255 / 31;
        a = (SrcFmt == 2 || SrcFmt == 14) ? (((c16 >> 15) & 1) ? 255 : 0) : 255;
      } else {
        (void)pixelFormatOverride;
        r = (((c32 >> 16) & 0xFF) >> 3) * 255 / 31;
        g = (((c32 >> 8) & 0xFF) >> 2) * 255 / 63;
        b = ((c32 & 0xFF) >> 3) * 255 / 31;
        a = (SrcFmt == 4 || SrcFmt == 15) ? 255 : ((c32 >> 24) & 0xFF);
      }

      uint32_t outA = 255;
      if constexpr (AlphaBlendOverlay) {
        if constexpr (SrcFmt == 0 || SrcFmt == 1 || SrcFmt == 4 ||
                      SrcFmt == 12 || SrcFmt == 13 || SrcFmt == 15) {
          outA = config.constantAlpha;
        } else {
          outA = a;
        }
      }

      if (outA == 0) {
        continue;
      }

      const uint32_t existing = dstRow[writeX];
      const uint32_t existingA = (existing >> 24) & 0xFF;
      if (outA == 255 || existingA == 0) {
        dstRow[writeX] = (outA << 24) | (r << 16) | (g << 8) | b;
      } else {
        // Porter-Duff SRC OVER DST when stacking two LFB overlays before flush
        // (e.g., persistent guest LFB write followed by grShamelessPlug).
        const float sa = outA / 255.0f;
        const float da = existingA / 255.0f;
        const float resA = sa + da * (1.0f - sa);
        const float invResA = (resA > 0.0f) ? (1.0f / resA) : 0.0f;
        const uint32_t er = (existing >> 16) & 0xFF;
        const uint32_t eg = (existing >> 8) & 0xFF;
        const uint32_t eb = existing & 0xFF;
        const uint32_t rr = std::min(
            255u, static_cast<uint32_t>((r * sa + er * da * (1.0f - sa)) * invResA + 0.5f));
        const uint32_t rg = std::min(
            255u, static_cast<uint32_t>((g * sa + eg * da * (1.0f - sa)) * invResA + 0.5f));
        const uint32_t rb = std::min(
            255u, static_cast<uint32_t>((b * sa + eb * da * (1.0f - sa)) * invResA + 0.5f));
        const uint32_t ra = std::min(255u, static_cast<uint32_t>(resA * 255.0f + 0.5f));
        dstRow[writeX] = (ra << 24) | (rr << 16) | (rg << 8) | rb;
      }
      rowModifiedCount++;
    }
  }
};

template <uint32_t SrcFmt, bool ChromakeyEnabled, bool AlphaBlendOverlay,
          bool HasRefData>
void RunWriteOverlayParallel(uint32_t srcHeight, uint32_t dstX, uint32_t dstY,
                             uint32_t srcWidth, uint32_t fbWidth,
                             uint32_t fbHeight, uint32_t pixelFormatOverride,
                             uint32_t* overlay, const uint8_t* inBytes,
                             const uint8_t* refBytes, int32_t srcStride,
                             const LfbPipelineConfig& config,
                             uint32_t& modifiedCount, uint32_t& outMinY,
                             uint32_t& outMaxY) {
  uint32_t localMinY = fbHeight;
  uint32_t localMaxY = 0;
  for (uint32_t y = 0; y < srcHeight; ++y) {
    uint32_t rowModifiedCount = 0;
    LfbOverlayRowProcessor<SrcFmt, ChromakeyEnabled, AlphaBlendOverlay,
                           HasRefData>::ProcessRow(y, dstX, dstY, srcWidth,
                                                   fbWidth, fbHeight,
                                                   pixelFormatOverride, overlay,
                                                   inBytes, refBytes, srcStride,
                                                   config, rowModifiedCount);
    if (rowModifiedCount > 0) {
      const uint32_t wy = dstY + y;
      if (wy < localMinY) localMinY = wy;
      if (wy > localMaxY) localMaxY = wy;
      modifiedCount += rowModifiedCount;
    }
  }
  outMinY = localMinY;
  outMaxY = localMaxY;
}

template <uint32_t SrcFmt, bool ChromakeyEnabled, bool BlendEnabled,
          bool HasRefData>
void RunWriteLFBParallel(uint32_t srcHeight, uint32_t dstX, uint32_t dstY,
                         uint32_t srcWidth, uint32_t fbWidth, uint32_t fbHeight,
                         uint32_t pixelFormatOverride, uint32_t* pixels,
                         const uint8_t* inBytes, const uint8_t* refBytes,
                         int32_t srcStride, const LfbPipelineConfig& config,
                         uint32_t& modifiedCount) {
#pragma omp parallel for reduction(+ : modifiedCount)
  for (uint32_t y = 0; y < srcHeight; ++y) {
    uint32_t rowModifiedCount = 0;
    LfbRowProcessor<SrcFmt, ChromakeyEnabled, BlendEnabled,
                    HasRefData>::ProcessRow(y, dstX, dstY, srcWidth, fbWidth,
                                            fbHeight, pixelFormatOverride,
                                            pixels, inBytes, refBytes,
                                            srcStride, config,
                                            rowModifiedCount);
    modifiedCount += rowModifiedCount;
  }
}

}  // namespace

void CpuLfbStaging::Configure(uint32_t width, uint32_t height,
                              bool allocateEagerly) {
  m_width = width;
  m_height = height;
  m_frontIdx = 0;
  m_backIdx = 1;
  m_dirty = false;
  m_bufferDirty[0] = false;
  m_bufferDirty[1] = false;
  m_overlayDirty[0] = false;
  m_overlayDirty[1] = false;
  m_overlayMinY[0] = m_overlayMinY[1] = 0;
  m_overlayMaxY[0] = m_overlayMaxY[1] = 0;
  m_pendingClear[0] = {};
  m_pendingClear[1] = {};

  m_overlayBuffers[0].clear();
  m_overlayBuffers[0].shrink_to_fit();
  m_overlayBuffers[1].clear();
  m_overlayBuffers[1].shrink_to_fit();

  if (allocateEagerly) {
    EnsureAllocated();
  } else {
    // Release any stale buffers from a previous resolution; allocate lazily on
    // first LFB access so pure 3D GPU workloads incur zero CPU framebuffer RSS.
    m_buffers[0].clear();
    m_buffers[0].shrink_to_fit();
    m_buffers[1].clear();
    m_buffers[1].shrink_to_fit();
  }
}

void CpuLfbStaging::Release() {
  m_buffers[0].clear();
  m_buffers[0].shrink_to_fit();
  m_buffers[1].clear();
  m_buffers[1].shrink_to_fit();
  m_overlayBuffers[0].clear();
  m_overlayBuffers[0].shrink_to_fit();
  m_overlayBuffers[1].clear();
  m_overlayBuffers[1].shrink_to_fit();
  m_width = 0;
  m_height = 0;
  m_frontIdx = 0;
  m_backIdx = 1;
  m_dirty = false;
  m_bufferDirty[0] = false;
  m_bufferDirty[1] = false;
  m_overlayDirty[0] = false;
  m_overlayDirty[1] = false;
  m_overlayMinY[0] = m_overlayMinY[1] = 0;
  m_overlayMaxY[0] = m_overlayMaxY[1] = 0;
  m_pendingClear[0] = {};
  m_pendingClear[1] = {};
}

void CpuLfbStaging::ResetPendingAndDirty() {
  m_dirty = false;
  m_bufferDirty[0] = false;
  m_bufferDirty[1] = false;
  ClearOverlay(0);
  ClearOverlay(1);
  m_pendingClear[0] = {};
  m_pendingClear[1] = {};
}

bool CpuLfbStaging::EnsureOverlayAllocated() {
  if (m_width == 0 || m_height == 0) return false;
  const size_t totalPixels = static_cast<size_t>(m_width) * m_height;
  if (m_overlayBuffers[0].size() == totalPixels &&
      m_overlayBuffers[1].size() == totalPixels) {
    return true;
  }
  m_overlayBuffers[0].assign(totalPixels, 0u);
  m_overlayBuffers[1].assign(totalPixels, 0u);
  m_overlayDirty[0] = false;
  m_overlayDirty[1] = false;
  m_overlayMinY[0] = m_overlayMinY[1] = m_height;
  m_overlayMaxY[0] = m_overlayMaxY[1] = 0;
  GLIDE_LOG(INFO, "CpuLfbStaging",
            "Lazily allocated sparse LFB overlay buffers: "
                << m_width << "x" << m_height << " ("
                << (totalPixels * sizeof(uint32_t) * 2) << " bytes).");
  return true;
}

void CpuLfbStaging::ClearOverlay(uint32_t logicalBuffer) {
  const uint32_t idx = (logicalBuffer == 0) ? 0u : 1u;
  if (m_overlayDirty[idx] && !m_overlayBuffers[idx].empty() && m_width > 0 &&
      m_overlayMinY[idx] <= m_overlayMaxY[idx] && m_overlayMaxY[idx] < m_height) {
    const size_t startPx = static_cast<size_t>(m_overlayMinY[idx]) * m_width;
    const size_t clearRows =
        static_cast<size_t>(m_overlayMaxY[idx] - m_overlayMinY[idx] + 1);
    std::memset(m_overlayBuffers[idx].data() + startPx, 0,
                clearRows * m_width * sizeof(uint32_t));
  }
  m_overlayDirty[idx] = false;
  m_overlayMinY[idx] = m_height;
  m_overlayMaxY[idx] = 0;
}

bool CpuLfbStaging::CanUseOverlayQuad(const LfbPipelineConfig& config) {
  if (!config.pixelPipeline) return true;
  // Opaque replacement (ONE, ZERO) or standard alpha blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA).
  if (config.rgbSrcBlend == 4u && config.rgbDstBlend == 0u) return true;
  if (config.rgbSrcBlend == 1u && config.rgbDstBlend == 5u) return true;
  return false;
}

bool CpuLfbStaging::WriteOverlayLFB(uint32_t logicalBuffer, uint32_t dstX,
                                    uint32_t dstY, uint32_t srcWidth,
                                    uint32_t srcHeight, int32_t srcStride,
                                    uint32_t srcFmt, const void* srcData,
                                    const void* refData,
                                    const LfbPipelineConfig& config,
                                    uint32_t pixelFormatOverride) {
  if (!srcData || m_width == 0 || m_height == 0) return false;
  if (!IsValidSrcFormat(srcFmt)) {
    GLIDE_LOG(WARN, "WriteLFB",
              "Invalid or undefined LFB source format: " << srcFmt);
    return false;
  }
  if (!EnsureOverlayAllocated()) return false;

  const uint32_t idx = (logicalBuffer == 0) ? 0u : 1u;
  uint32_t* overlay = m_overlayBuffers[idx].data();
  const auto* inBytes = reinterpret_cast<const uint8_t*>(srcData);
  const auto* refBytes = reinterpret_cast<const uint8_t*>(refData);

  const bool chromakeyEnabled = config.pixelPipeline && config.chromakeyEnabled;
  const bool alphaBlendOverlay =
      config.pixelPipeline &&
      (config.rgbSrcBlend == 1u && config.rgbDstBlend == 5u);
  const bool hasRefData = (refBytes != nullptr);

  uint32_t modifiedCount = 0;
  uint32_t minY = m_height;
  uint32_t maxY = 0;

#define DISPATCH_OVERLAY_CASE(Fmt)                                           \
  case Fmt:                                                                  \
    if (chromakeyEnabled) {                                                  \
      if (alphaBlendOverlay) {                                               \
        if (hasRefData) {                                                    \
          RunWriteOverlayParallel<Fmt, true, true, true>(                    \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        } else {                                                             \
          RunWriteOverlayParallel<Fmt, true, true, false>(                   \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        }                                                                    \
      } else {                                                               \
        if (hasRefData) {                                                    \
          RunWriteOverlayParallel<Fmt, true, false, true>(                   \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        } else {                                                             \
          RunWriteOverlayParallel<Fmt, true, false, false>(                  \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        }                                                                    \
      }                                                                      \
    } else {                                                                 \
      if (alphaBlendOverlay) {                                               \
        if (hasRefData) {                                                    \
          RunWriteOverlayParallel<Fmt, false, true, true>(                   \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        } else {                                                             \
          RunWriteOverlayParallel<Fmt, false, true, false>(                  \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        }                                                                    \
      } else {                                                               \
        if (hasRefData) {                                                    \
          RunWriteOverlayParallel<Fmt, false, false, true>(                  \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        } else {                                                             \
          RunWriteOverlayParallel<Fmt, false, false, false>(                 \
              srcHeight, dstX, dstY, srcWidth, m_width, m_height,            \
              pixelFormatOverride, overlay, inBytes, refBytes, srcStride,    \
              config, modifiedCount, minY, maxY);                            \
        }                                                                    \
      }                                                                      \
    }                                                                        \
    break;

  switch (srcFmt) {
    DISPATCH_OVERLAY_CASE(0)
    DISPATCH_OVERLAY_CASE(1)
    DISPATCH_OVERLAY_CASE(2)
    DISPATCH_OVERLAY_CASE(4)
    DISPATCH_OVERLAY_CASE(5)
    DISPATCH_OVERLAY_CASE(12)
    DISPATCH_OVERLAY_CASE(13)
    DISPATCH_OVERLAY_CASE(14)
    DISPATCH_OVERLAY_CASE(15)
    default:
      break;
  }
#undef DISPATCH_OVERLAY_CASE

  if (modifiedCount > 0) {
    if (!m_overlayDirty[idx]) {
      m_overlayDirty[idx] = true;
      m_overlayMinY[idx] = minY;
      m_overlayMaxY[idx] = maxY;
    } else {
      m_overlayMinY[idx] = std::min(m_overlayMinY[idx], minY);
      m_overlayMaxY[idx] = std::max(m_overlayMaxY[idx], maxY);
    }
  }
  return true;
}

const uint32_t* CpuLfbStaging::GetOverlayForGpuUpload(uint32_t logicalBuffer,
                                                      uint32_t& outMinY,
                                                      uint32_t& outMaxY) const {
  const uint32_t idx = (logicalBuffer == 0) ? 0u : 1u;
  if (!m_overlayDirty[idx] || m_overlayBuffers[idx].empty()) {
    return nullptr;
  }
  outMinY = m_overlayMinY[idx];
  outMaxY = m_overlayMaxY[idx];
  return m_overlayBuffers[idx].data();
}

bool CpuLfbStaging::EnsureAllocated() {
  if (m_width == 0 || m_height == 0) return false;
  const size_t rawSize = static_cast<size_t>(m_width) * m_height * 4;
  const size_t paddedSize = (rawSize + 63) & ~size_t(63);
  const size_t allocatedSize = paddedSize + 64;

  if (m_buffers[0].size() == allocatedSize &&
      m_buffers[1].size() == allocatedSize) {
    return true;
  }

  m_buffers[0].resize(allocatedSize);
  m_buffers[1].resize(allocatedSize);
  std::memset(m_buffers[0].data(), 0, m_buffers[0].size());
  std::memset(m_buffers[1].data(), 0, m_buffers[1].size());
  GLIDE_LOG(INFO, "CpuLfbStaging",
            "Lazily allocated CPU LFB staging double buffers: "
                << m_width << "x" << m_height << " (" << (allocatedSize * 2)
                << " bytes).");
  return true;
}

void CpuLfbStaging::ScheduleLazyClear(uint32_t logicalBuffer, uint32_t color,
                                      uint32_t alpha,
                                      uint32_t pixelFormatOverride,
                                      bool maskRgb, bool maskAlpha) {
  if (m_width == 0 || m_height == 0) return;
  if (!maskRgb && !maskAlpha) return;

  const uint32_t slot = SlotFor(logicalBuffer);
  auto& slotState = m_pendingClear[slot];

  // If a prior partial-mask clear is pending and this clear also uses a
  // different partial mask, materialize the earlier clear first so masked-out
  // channels retain their prior cleared bits.
  if (slotState.pending && !(maskRgb && maskAlpha) &&
      (slotState.maskRgb != maskRgb || slotState.maskAlpha != maskAlpha)) {
    MaterializeSlot(slot);
  }

  slotState.pending = true;
  slotState.color = color;
  slotState.alpha = alpha;
  slotState.pixelFormat = pixelFormatOverride;
  slotState.maskRgb = maskRgb;
  slotState.maskAlpha = maskAlpha;
}

void CpuLfbStaging::MaterializeLazyClear(uint32_t logicalBuffer) {
  MaterializeSlot(SlotFor(logicalBuffer));
}

void CpuLfbStaging::MaterializeSlot(uint32_t slot) {
  if (slot >= 2 || !m_pendingClear[slot].pending) return;

  const PendingColorClear pending = m_pendingClear[slot];
  m_pendingClear[slot].pending = false;

  if (!EnsureAllocated()) return;

  uint32_t writeMask = 0;
  if (pending.maskRgb) writeMask |= 0x00FFFFFF;
  if (pending.maskAlpha) writeMask |= 0xFF000000;
  if (writeMask == 0) return;

  uint32_t a = (pending.alpha > 0) ? pending.alpha : 255;
  uint32_t r = (pending.color >> 16) & 0xFF;
  uint32_t g = (pending.color >> 8) & 0xFF;
  uint32_t b = pending.color & 0xFF;
  if (pending.pixelFormat == 1) {
    std::swap(r, b);
  }

  const uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
  auto* pixels = reinterpret_cast<uint32_t*>(m_buffers[slot].data());
  const uint32_t totalPixels = m_width * m_height;

  if (writeMask == 0xFFFFFFFF) {
    std::fill_n(pixels, totalPixels, clearWord);
  } else {
    const uint32_t keepMask = ~writeMask;
    const uint32_t maskedClearWord = clearWord & writeMask;
    for (uint32_t i = 0; i < totalPixels; ++i) {
      pixels[i] = (pixels[i] & keepMask) | maskedClearWord;
    }
  }
}

void CpuLfbStaging::DiscardLazyClear(uint32_t logicalBuffer) {
  m_pendingClear[SlotFor(logicalBuffer)].pending = false;
}

bool CpuLfbStaging::HasPendingLazyClear(uint32_t logicalBuffer) const {
  return m_pendingClear[SlotFor(logicalBuffer)].pending;
}

void CpuLfbStaging::SwapBuffers() {
  std::swap(m_frontIdx, m_backIdx);
  std::swap(m_bufferDirty[0], m_bufferDirty[1]);
}

uint8_t* CpuLfbStaging::PrepareBufferForGpuReadback(uint32_t logicalBuffer) {
  if (!EnsureAllocated()) return nullptr;
  const uint32_t slot = SlotFor(logicalBuffer);
  m_pendingClear[slot].pending = false;
  return m_buffers[slot].data();
}

const uint8_t* CpuLfbStaging::PrepareBufferForGpuUpload(
    uint32_t logicalBuffer) {
  if (!IsAllocated()) return nullptr;
  const uint32_t slot = SlotFor(logicalBuffer);
  MaterializeSlot(slot);
  return m_buffers[slot].data();
}

void* CpuLfbStaging::GetWritePtr(uint32_t activeRenderBuffer) {
  if (!EnsureAllocated()) return nullptr;
  MaterializeSlot(m_frontIdx);
  MaterializeSlot(m_backIdx);
  m_dirty = true;
  m_bufferDirty[0] = true;
  m_bufferDirty[1] = true;
  return m_buffers[SlotFor(activeRenderBuffer)].data();
}

bool CpuLfbStaging::IsValidSrcFormat(uint32_t srcFmt) {
  switch (srcFmt) {
    case 0:   // GR_LFB_SRC_FMT_565
    case 1:   // GR_LFB_SRC_FMT_555
    case 2:   // GR_LFB_SRC_FMT_1555
    case 4:   // GR_LFB_SRC_FMT_888
    case 5:   // GR_LFB_SRC_FMT_8888
    case 12:  // GR_LFB_SRC_FMT_565_DEPTH
    case 13:  // GR_LFB_SRC_FMT_555_DEPTH
    case 14:  // GR_LFB_SRC_FMT_1555_DEPTH
    case 15:  // GR_LFB_SRC_FMT_ZA16
      return true;
    default:
      return false;
  }
}

bool CpuLfbStaging::ReadPixelsRegion(const uint32_t* pixels, uint32_t width,
                                     uint32_t height, uint32_t srcX,
                                     uint32_t srcY, uint32_t srcWidth,
                                     uint32_t srcHeight, uint32_t dstStride,
                                     void* dstData) {
  if (!pixels || !dstData) return false;
  auto* outBytes = reinterpret_cast<uint8_t*>(dstData);

  const bool is32Bit = (dstStride >= srcWidth * 4);
  GLIDE_LOG(DEBUG, "ReadLFB",
            "ReadLFB invoked: srcX="
                << srcX << ", srcY=" << srcY << ", srcWidth=" << srcWidth
                << ", srcHeight=" << srcHeight << ", dstStride=" << dstStride
                << ", is32Bit=" << is32Bit);

  // VULN-04: Prevent out-of-bounds row writes if dstStride is too small
  const size_t requiredStride = is32Bit ? (srcWidth * 4) : (srcWidth * 2);
  if (dstStride < requiredStride) {
    GLIDE_LOG(WARN, "Backend",
              "ReadLFB: dstStride ("
                  << dstStride << ") is smaller than required stride ("
                  << requiredStride << ") for width " << srcWidth
                  << ". Rejecting read to prevent buffer overflow.");
    return false;
  }

  for (uint32_t y = 0; y < srcHeight; ++y) {
    uint32_t readY = srcY + y;
    if (readY >= height) break;

    if (is32Bit) {
      auto* destRow32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
      uint32_t safeWidth =
          (srcX >= width) ? 0 : std::min(srcWidth, width - srcX);
      if (safeWidth > 0) {
        std::memcpy(destRow32, &pixels[readY * width + srcX],
                    safeWidth * sizeof(uint32_t));
      }
    } else {
      auto* destRow16 = reinterpret_cast<uint16_t*>(outBytes + y * dstStride);
      for (uint32_t x = 0; x < srcWidth; ++x) {
        uint32_t readX = srcX + x;
        if (readX >= width) break;

        uint32_t pWord = pixels[readY * width + readX];
        uint32_t rVal = (pWord >> 16) & 0xFF;
        uint32_t gVal = (pWord >> 8) & 0xFF;
        uint32_t bVal = pWord & 0xFF;
        uint16_t r = static_cast<uint16_t>((rVal * 31 + 127) / 255);
        uint16_t g = static_cast<uint16_t>((gVal * 63 + 127) / 255);
        uint16_t b = static_cast<uint16_t>((bVal * 31 + 127) / 255);
        uint16_t c16 = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        destRow16[x] = c16;
      }
    }
  }
  return true;
}

uint32_t CpuLfbStaging::WritePixelsRegion(
    uint32_t* pixels, uint32_t width, uint32_t height, uint32_t dstX,
    uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
    uint32_t srcFmt, const void* srcData, const void* refData,
    const LfbPipelineConfig& config, uint32_t pixelFormatOverride) {
  uint32_t modifiedCount = 0;
  const auto* inBytes = reinterpret_cast<const uint8_t*>(srcData);
  const auto* refBytes = reinterpret_cast<const uint8_t*>(refData);

  const bool chromakeyEnabled = config.pixelPipeline && config.chromakeyEnabled;
  const bool blendEnabled = config.pixelPipeline;
  const bool hasRefData = (refBytes != nullptr);

#define DISPATCH_CASE(Fmt)                                                 \
  case Fmt:                                                                \
    if (chromakeyEnabled) {                                                \
      if (blendEnabled) {                                                  \
        if (hasRefData) {                                                  \
          RunWriteLFBParallel<Fmt, true, true, true>(                      \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        } else {                                                           \
          RunWriteLFBParallel<Fmt, true, true, false>(                     \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        }                                                                  \
      } else {                                                             \
        if (hasRefData) {                                                  \
          RunWriteLFBParallel<Fmt, true, false, true>(                     \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        } else {                                                           \
          RunWriteLFBParallel<Fmt, true, false, false>(                    \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        }                                                                  \
      }                                                                    \
    } else {                                                               \
      if (blendEnabled) {                                                  \
        if (hasRefData) {                                                  \
          RunWriteLFBParallel<Fmt, false, true, true>(                     \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        } else {                                                           \
          RunWriteLFBParallel<Fmt, false, true, false>(                    \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        }                                                                  \
      } else {                                                             \
        if (hasRefData) {                                                  \
          RunWriteLFBParallel<Fmt, false, false, true>(                    \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        } else {                                                           \
          RunWriteLFBParallel<Fmt, false, false, false>(                   \
              srcHeight, dstX, dstY, srcWidth, width, height,              \
              pixelFormatOverride, pixels, inBytes, refBytes, srcStride,   \
              config, modifiedCount);                                      \
        }                                                                  \
      }                                                                    \
    }                                                                      \
    break;

  switch (srcFmt) {
    DISPATCH_CASE(0)
    DISPATCH_CASE(1)
    DISPATCH_CASE(2)
    DISPATCH_CASE(4)
    DISPATCH_CASE(5)
    DISPATCH_CASE(12)
    DISPATCH_CASE(13)
    DISPATCH_CASE(14)
    DISPATCH_CASE(15)
    default:
      break;
  }
#undef DISPATCH_CASE

  return modifiedCount;
}

bool CpuLfbStaging::WriteLFB(uint32_t logicalBuffer, uint32_t dstX,
                             uint32_t dstY, uint32_t srcWidth,
                             uint32_t srcHeight, int32_t srcStride,
                             uint32_t srcFmt, const void* srcData,
                             const void* refData,
                             const LfbPipelineConfig& config,
                             uint32_t pixelFormatOverride) {
  if (!srcData || m_width == 0 || m_height == 0) return false;
  if (!IsValidSrcFormat(srcFmt)) {
    GLIDE_LOG(WARN, "WriteLFB",
              "Invalid or undefined LFB source format: " << srcFmt);
    return false;
  }

  if (!EnsureAllocated()) return false;

  GLIDE_LOG(DEBUG, "WriteLFB",
            "WriteLFB invoked: buffer=" << logicalBuffer << ", dst=(" << dstX
                                        << "," << dstY << "), size=" << srcWidth
                                        << "x" << srcHeight << ", stride="
                                        << srcStride << ", fmt=" << srcFmt);

  const uint32_t targetSlot = SlotFor(logicalBuffer);
  MaterializeSlot(targetSlot);

  auto* pixels = reinterpret_cast<uint32_t*>(m_buffers[targetSlot].data());
  const uint32_t modifiedCount = WritePixelsRegion(
      pixels, m_width, m_height, dstX, dstY, srcWidth, srcHeight, srcStride,
      srcFmt, srcData, refData, config, pixelFormatOverride);

  if (modifiedCount > 0) {
    m_dirty = true;
    m_bufferDirty[logicalBuffer == 0 ? 0 : 1] = true;
  }
  GLIDE_LOG(DEBUG, "WriteLFB",
            "WriteLFB finished: wrotePixel="
                << (modifiedCount > 0 ? "Yes" : "No")
                << ", modifiedPixels=" << modifiedCount
                << ", m_lfbDirty=" << (m_dirty ? "Yes" : "No"));
  return true;
}

bool CpuLfbStaging::ReadRegion(uint32_t logicalBuffer, uint32_t srcX,
                               uint32_t srcY, uint32_t srcWidth,
                               uint32_t srcHeight, uint32_t dstStride,
                               void* dstData) {
  if (!dstData || m_width == 0 || m_height == 0) return false;
  if (!EnsureAllocated()) return false;
  const uint32_t readSlot = SlotFor(logicalBuffer);
  MaterializeSlot(readSlot);
  const auto* pixels =
      reinterpret_cast<const uint32_t*>(m_buffers[readSlot].data());
  return ReadPixelsRegion(pixels, m_width, m_height, srcX, srcY, srcWidth,
                          srcHeight, dstStride, dstData);
}

}  // namespace GlideWrapper
