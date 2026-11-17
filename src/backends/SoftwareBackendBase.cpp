#include "SoftwareBackendBase.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/Logger.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace GlideWrapper {

void SoftwareBackendBase::ResetState() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetState_Locked();
}

void SoftwareBackendBase::ResetState_Locked() {
  GlideStateTracker::ResetState_Locked();
  m_pendingCpuColorClear[0] = {};
  m_pendingCpuColorClear[1] = {};
  m_pendingCpuDepthClear = false;
  m_pendingCpuDepthValue = 1.0f;
}

void SoftwareBackendBase::ClearBuffer(uint32_t color, uint32_t alpha, float z,
                                      uint32_t clearMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ClearBuffer_Locked(color, alpha, z, clearMask);
}

void SoftwareBackendBase::ClearBuffer_Locked(uint32_t color, uint32_t alpha,
                                             float z, uint32_t clearMask) {
  FlushBins_Locked();
  GLIDE_LOG(DEBUG, "Rasterizer",
            "ClearBuffer CPU side invoked: color=0x"
                << std::hex << color << ", alpha=" << alpha << ", mask="
                << clearMask << std::dec << ", map=" << m_headlessPixelMap);
  if (!m_windowAttached || !m_headlessPixelMap) return;

  uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ComputeClipRectTopDown_Locked(x0, y0, x1, y1)) {
    return;
  }
  const bool fullClip =
      (x0 == 0 && y0 == 0 && x1 == m_headlessWidth && y1 == m_headlessHeight);

  uint32_t activeSlot =
      (m_activeRenderBuffer == 0) ? m_frontBufferIdx : m_backBufferIdx;
  const uint32_t tbufMask =
      (m_ssaaScale == 2) ? (m_tbufferWriteMask & 0x0Fu) : 0x0Fu;
  const bool isTbufMasked = (m_ssaaScale == 2 && tbufMask != 0x0Fu);
  if (isTbufMasked && tbufMask == 0u) {
    return;
  }

  if (clearMask & 0x1) {  // Color Clear
    if (fullClip && !isTbufMasked) {
      m_pendingCpuColorClear[activeSlot].pending = false;
    } else {
      // Materialize any prior deferred full-screen clear before modifying a
      // clipped sub-rectangle or T-Buffer sample subset so untouched
      // pixels/samples are preserved.
      MaterializeLazyCpuClear_Locked(activeSlot);
    }

    uint32_t a = (alpha > 0) ? alpha : 255;
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;

    if (m_pixelFormatOverride == 1) {  // Convert input ABGR to internal ARGB
      std::swap(r, b);
    }

    uint32_t writeMask = 0;
    if (m_colorMaskRgb) writeMask |= 0x00FFFFFF;
    if (m_colorMaskAlpha) writeMask |= 0xFF000000;

    if (writeMask == 0) {
      GLIDE_LOG(DEBUG, "Rasterizer",
                "ClearBuffer color clear skipped due to zero color mask (write "
                "masking active).");
    } else {
      uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
      uint32_t* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
      const uint32_t spanW = x1 - x0;

      if (isTbufMasked) {
        const uint32_t keepMask = ~writeMask;
        const uint32_t maskedClearWord = clearWord & writeMask;
        for (uint32_t y = y0; y < y1; ++y) {
          const uint32_t rowMask = (tbufMask >> ((y & 1u) << 1)) & 3u;
          if (rowMask == 0u) continue;
          uint32_t* row =
              pixels + static_cast<size_t>(y) * m_headlessWidth;
          if (writeMask == 0xFFFFFFFFu) {
            for (uint32_t x = x0; x < x1; ++x) {
              if ((rowMask >> (x & 1u)) & 1u) {
                row[x] = clearWord;
              }
            }
          } else {
            for (uint32_t x = x0; x < x1; ++x) {
              if ((rowMask >> (x & 1u)) & 1u) {
                row[x] = (row[x] & keepMask) | maskedClearWord;
              }
            }
          }
        }
      } else if (fullClip) {
        uint32_t totalPixels = m_headlessWidth * m_headlessHeight;
        if (writeMask == 0xFFFFFFFF) {
          std::fill_n(pixels, totalPixels, clearWord);
        } else {
          uint32_t keepMask = ~writeMask;
          uint32_t maskedClearWord = clearWord & writeMask;
          for (uint32_t i = 0; i < totalPixels; ++i) {
            pixels[i] = (pixels[i] & keepMask) | maskedClearWord;
          }
        }
      } else if (writeMask == 0xFFFFFFFF) {
        for (uint32_t y = y0; y < y1; ++y) {
          std::fill_n(pixels + static_cast<size_t>(y) * m_headlessWidth + x0,
                      spanW, clearWord);
        }
      } else {
        uint32_t keepMask = ~writeMask;
        uint32_t maskedClearWord = clearWord & writeMask;
        for (uint32_t y = y0; y < y1; ++y) {
          uint32_t* row =
              pixels + static_cast<size_t>(y) * m_headlessWidth + x0;
          for (uint32_t i = 0; i < spanW; ++i) {
            row[i] = (row[i] & keepMask) | maskedClearWord;
          }
        }
      }
      GLIDE_LOG(DEBUG, "Rasterizer",
                "Cleared color buffer rect (" << x0 << "," << y0 << ")-(" << x1
                                              << "," << y1 << ") with word 0x"
                                              << std::hex << clearWord
                                              << " using write mask 0x"
                                              << writeMask << std::dec);
    }
  }

  if ((clearMask & 0x2) && m_depthMask) {  // Depth Clear (obeys grDepthMask)
    if (fullClip && !isTbufMasked) {
      m_pendingCpuDepthClear = false;
      if (!m_headlessDepthBuffer.empty()) {
        std::fill(m_headlessDepthBuffer.begin(), m_headlessDepthBuffer.end(),
                  z);
        GLIDE_LOG(DEBUG, "Rasterizer", "Cleared depth buffer with value " << z);
      }
    } else {
      MaterializeLazyCpuDepthClear_Locked();
      if (!m_headlessDepthBuffer.empty()) {
        if (isTbufMasked) {
          for (uint32_t y = y0; y < y1; ++y) {
            const uint32_t rowMask = (tbufMask >> ((y & 1u) << 1)) & 3u;
            if (rowMask == 0u) continue;
            float* depthRow = m_headlessDepthBuffer.data() +
                              static_cast<size_t>(y) * m_headlessWidth;
            for (uint32_t x = x0; x < x1; ++x) {
              if ((rowMask >> (x & 1u)) & 1u) {
                depthRow[x] = z;
              }
            }
          }
        } else {
          const uint32_t spanW = x1 - x0;
          for (uint32_t y = y0; y < y1; ++y) {
            std::fill_n(m_headlessDepthBuffer.data() +
                            static_cast<size_t>(y) * m_headlessWidth + x0,
                        spanW, z);
          }
        }
        GLIDE_LOG(DEBUG, "Rasterizer",
                  "Cleared depth buffer rect (" << x0 << "," << y0 << ")-("
                                                << x1 << "," << y1
                                                << ") with value " << z);
      }
    }
  }
}

void SoftwareBackendBase::ScheduleLazyCpuClear(uint32_t color, uint32_t alpha,
                                               float z, uint32_t clearMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_windowAttached || !m_headlessPixelMap) return;

  if (!IsFullScreenClip_Locked() ||
      (m_ssaaScale == 2 && (m_tbufferWriteMask & 0x0Fu) != 0x0Fu)) {
    ClearBuffer_Locked(color, alpha, z, clearMask);
    return;
  }

  uint32_t activeSlot =
      (m_activeRenderBuffer == 0) ? m_frontBufferIdx : m_backBufferIdx;

  if (clearMask & 0x1) {
    if (m_colorMaskRgb || m_colorMaskAlpha) {
      auto& slotState = m_pendingCpuColorClear[activeSlot];
      if (slotState.pending && !(m_colorMaskRgb && m_colorMaskAlpha) &&
          (slotState.maskRgb != m_colorMaskRgb ||
           slotState.maskAlpha != m_colorMaskAlpha)) {
        MaterializeLazyCpuClear_Locked(activeSlot);
      }
      slotState.pending = true;
      slotState.color = color;
      slotState.alpha = alpha;
      slotState.pixelFormat = m_pixelFormatOverride;
      slotState.maskRgb = m_colorMaskRgb;
      slotState.maskAlpha = m_colorMaskAlpha;
    }
  }

  if ((clearMask & 0x2) && m_depthMask) {
    m_pendingCpuDepthClear = true;
    m_pendingCpuDepthValue = z;
  }
}

void SoftwareBackendBase::MaterializeLazyCpuClear_Locked(uint32_t bufferSlot) {
  if (bufferSlot >= 2 || !m_pendingCpuColorClear[bufferSlot].pending) return;

  const PendingCpuColorClear pending = m_pendingCpuColorClear[bufferSlot];
  m_pendingCpuColorClear[bufferSlot].pending = false;

  if (m_cpuBuffers[bufferSlot].empty()) return;

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

  uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
  uint32_t* pixels =
      reinterpret_cast<uint32_t*>(m_cpuBuffers[bufferSlot].data());
  uint32_t totalPixels = m_headlessWidth * m_headlessHeight;

  if (writeMask == 0xFFFFFFFF) {
    std::fill_n(pixels, totalPixels, clearWord);
  } else {
    uint32_t keepMask = ~writeMask;
    uint32_t maskedClearWord = clearWord & writeMask;
    for (uint32_t i = 0; i < totalPixels; ++i) {
      pixels[i] = (pixels[i] & keepMask) | maskedClearWord;
    }
  }
}

void SoftwareBackendBase::MaterializeLazyCpuDepthClear_Locked() {
  if (!m_pendingCpuDepthClear) return;
  m_pendingCpuDepthClear = false;
  if (!m_headlessDepthBuffer.empty()) {
    std::fill(m_headlessDepthBuffer.begin(), m_headlessDepthBuffer.end(),
              m_pendingCpuDepthValue);
  }
}

void SoftwareBackendBase::DiscardLazyCpuClear_Locked(uint32_t bufferSlot) {
  if (bufferSlot < 2) {
    m_pendingCpuColorClear[bufferSlot].pending = false;
  }
}

void* SoftwareBackendBase::GetLfbWritePtr() {
  std::lock_guard<std::mutex> lock(m_mutex);
  MaterializeLazyCpuClear_Locked(m_frontBufferIdx);
  MaterializeLazyCpuClear_Locked(m_backBufferIdx);
  m_lfbDirty = true;
  m_lfbBufferDirty[0] = true;
  m_lfbBufferDirty[1] = true;
  return m_headlessPixelMap;
}

bool SoftwareBackendBase::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                                  uint32_t srcWidth, uint32_t srcHeight,
                                  uint32_t dstStride, void* dstData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBins_Locked();
  if (!dstData || m_cpuBuffers[0].empty()) return false;
  uint32_t readSlot = (buffer == 0) ? m_frontBufferIdx : m_backBufferIdx;
  MaterializeLazyCpuClear_Locked(readSlot);

  const uint32_t* pixels = nullptr;
  uint32_t width = m_headlessWidth;
  uint32_t height = m_headlessHeight;

  if (m_ssaaScale > 1) {
    const uint32_t* srcPixels =
        reinterpret_cast<const uint32_t*>(m_cpuBuffers[readSlot].data());
    uint32_t* dstPixels = reinterpret_cast<uint32_t*>(m_resolvedBuffer.data());
    uint32_t nativeWidth = m_headlessWidth / m_ssaaScale;
    uint32_t nativeHeight = m_headlessHeight / m_ssaaScale;

#ifdef _OPENMP
#pragma omp parallel for collapse(2) if (nativeHeight * nativeWidth > 64000)
#endif
    for (uint32_t y = 0; y < nativeHeight; ++y) {
      for (uint32_t x = 0; x < nativeWidth; ++x) {
        uint32_t r = 0, g = 0, b = 0, a = 0;
        for (uint32_t dy = 0; dy < 2; ++dy) {
          for (uint32_t dx = 0; dx < 2; ++dx) {
            uint32_t srcIdx = (2 * y + dy) * m_headlessWidth + (2 * x + dx);
            uint32_t p = srcPixels[srcIdx];
            a += (p >> 24) & 0xFF;
            r += (p >> 16) & 0xFF;
            g += (p >> 8) & 0xFF;
            b += p & 0xFF;
          }
        }
        a /= 4;
        r /= 4;
        g /= 4;
        b /= 4;
        dstPixels[y * nativeWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    pixels = dstPixels;
    width = nativeWidth;
    height = nativeHeight;
  } else {
    pixels = reinterpret_cast<const uint32_t*>(m_cpuBuffers[readSlot].data());
  }

  return CpuLfbStaging::ReadPixelsRegion(pixels, width, height, srcX, srcY,
                                         srcWidth, srcHeight, dstStride,
                                         dstData);
}

bool SoftwareBackendBase::WriteLFB(uint32_t buffer, uint32_t dstX,
                                   uint32_t dstY, uint32_t srcWidth,
                                   uint32_t srcHeight, int32_t srcStride,
                                   uint32_t srcFmt, const void* srcData,
                                   const void* refData,
                                   const LfbPipelineConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBins_Locked();
  if (!srcData || m_cpuBuffers[0].empty()) return false;

  if (!CpuLfbStaging::IsValidSrcFormat(srcFmt)) {
    GLIDE_LOG(WARN, "WriteLFB",
              "Invalid or undefined LFB source format: " << srcFmt);
    return false;
  }

  uint32_t targetSlot = (buffer == 0) ? m_frontBufferIdx : m_backBufferIdx;
  MaterializeLazyCpuClear_Locked(targetSlot);
  uint32_t* pixels =
      reinterpret_cast<uint32_t*>(m_cpuBuffers[targetSlot].data());

  uint32_t modifiedCount = 0;
  if (m_ssaaScale == 2 && m_guestWidth > 0 && m_guestHeight > 0 &&
      !m_resolvedBuffer.empty()) {
    const uint32_t guestW = m_guestWidth;
    const uint32_t guestH = m_guestHeight;
    if (dstX >= guestW || dstY >= guestH || srcWidth == 0 || srcHeight == 0) {
      return true;
    }
    const uint32_t rx0 = dstX;
    const uint32_t ry0 = dstY;
    const uint32_t rx1 = std::min(guestW, dstX + srcWidth);
    const uint32_t ry1 = std::min(guestH, dstY + srcHeight);
    const uint32_t rw = rx1 - rx0;
    const uint32_t rh = ry1 - ry0;

    auto* resolved = reinterpret_cast<uint32_t*>(m_resolvedBuffer.data());
    const bool sparsePreserve =
        (refData != nullptr || config.chromakeyEnabled || config.pixelPipeline);

    // Resolve ONLY the destination rect into guest-resolution scratch so
    // blending / sparse overlays see authentic pre-existing pixels.
    for (uint32_t y = ry0; y < ry1; ++y) {
      const uint32_t* sRow0 =
          pixels + static_cast<size_t>(2u * y) * m_headlessWidth;
      const uint32_t* sRow1 =
          pixels + static_cast<size_t>(2u * y + 1u) * m_headlessWidth;
      uint32_t* dRow = resolved + static_cast<size_t>(y) * guestW;
      for (uint32_t x = rx0; x < rx1; ++x) {
        const uint32_t p00 = sRow0[2u * x];
        const uint32_t p10 = sRow0[2u * x + 1u];
        const uint32_t p01 = sRow1[2u * x];
        const uint32_t p11 = sRow1[2u * x + 1u];
        const uint32_t a = (((p00 >> 24) & 0xFFu) + ((p10 >> 24) & 0xFFu) +
                            ((p01 >> 24) & 0xFFu) + ((p11 >> 24) & 0xFFu)) >>
                           2;
        const uint32_t r = (((p00 >> 16) & 0xFFu) + ((p10 >> 16) & 0xFFu) +
                            ((p01 >> 16) & 0xFFu) + ((p11 >> 16) & 0xFFu)) >>
                           2;
        const uint32_t g = (((p00 >> 8) & 0xFFu) + ((p10 >> 8) & 0xFFu) +
                            ((p01 >> 8) & 0xFFu) + ((p11 >> 8) & 0xFFu)) >>
                           2;
        const uint32_t b = ((p00 & 0xFFu) + (p10 & 0xFFu) + (p01 & 0xFFu) +
                            (p11 & 0xFFu)) >>
                           2;
        dRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }

    std::vector<uint32_t> preRect;
    if (sparsePreserve) {
      preRect.resize(static_cast<size_t>(rw) * rh);
      for (uint32_t y = ry0; y < ry1; ++y) {
        std::memcpy(preRect.data() + static_cast<size_t>(y - ry0) * rw,
                    resolved + static_cast<size_t>(y) * guestW + rx0,
                    static_cast<size_t>(rw) * sizeof(uint32_t));
      }
    }

    modifiedCount = CpuLfbStaging::WritePixelsRegion(
        resolved, guestW, guestH, dstX, dstY, srcWidth, srcHeight, srcStride,
        srcFmt, srcData, refData, config, m_pixelFormatOverride);

    if (modifiedCount > 0) {
      for (uint32_t y = ry0; y < ry1; ++y) {
        const uint32_t* rRow = resolved + static_cast<size_t>(y) * guestW;
        uint32_t* dRow0 =
            pixels + static_cast<size_t>(2u * y) * m_headlessWidth;
        uint32_t* dRow1 =
            pixels + static_cast<size_t>(2u * y + 1u) * m_headlessWidth;
        const uint32_t* preRow =
            sparsePreserve
                ? (preRect.data() + static_cast<size_t>(y - ry0) * rw)
                : nullptr;
        for (uint32_t x = rx0; x < rx1; ++x) {
          const uint32_t val = rRow[x];
          if (sparsePreserve && val == preRow[x - rx0]) continue;
          dRow0[2u * x] = val;
          dRow0[2u * x + 1u] = val;
          dRow1[2u * x] = val;
          dRow1[2u * x + 1u] = val;
        }
      }
    }
  } else {
    modifiedCount = CpuLfbStaging::WritePixelsRegion(
        pixels, m_headlessWidth, m_headlessHeight, dstX, dstY, srcWidth,
        srcHeight, srcStride, srcFmt, srcData, refData, config,
        m_pixelFormatOverride);
  }

  const bool wrotePixel = (modifiedCount > 0);
  if (wrotePixel) {
    m_lfbDirty = true;
    m_lfbBufferDirty[buffer == 0 ? 0 : 1] = true;
  }
  GLIDE_LOG(DEBUG, "WriteLFB",
            "WriteLFB finished: wrotePixel="
                << (wrotePixel ? "Yes" : "No")
                << ", modifiedPixels=" << modifiedCount
                << ", m_lfbDirty=" << (m_lfbDirty ? "Yes" : "No"));
  return true;
}

void SoftwareBackendBase::AllocateCpuBuffers_Locked(uint32_t width,
                                                    uint32_t height) {
  size_t rawSize = static_cast<size_t>(width) * height * 4;
  size_t paddedSize = (rawSize + 63) & ~size_t(63);
  size_t allocatedSize = paddedSize + 64;

  m_cpuBuffers[0].resize(allocatedSize);
  m_cpuBuffers[1].resize(allocatedSize);

  size_t resolvedRawSize =
      static_cast<size_t>(width / m_ssaaScale) * (height / m_ssaaScale) * 4;
  size_t resolvedPaddedSize = (resolvedRawSize + 63) & ~size_t(63);
  size_t resolvedAllocatedSize = resolvedPaddedSize + 64;
  m_resolvedBuffer.resize(resolvedAllocatedSize);

  std::memset(m_cpuBuffers[0].data(), 0, m_cpuBuffers[0].size());
  std::memset(m_cpuBuffers[1].data(), 0, m_cpuBuffers[1].size());
  std::memset(m_resolvedBuffer.data(), 0, m_resolvedBuffer.size());
  m_pendingCpuColorClear[0] = {};
  m_pendingCpuColorClear[1] = {};
  m_pendingCpuDepthClear = false;
  m_frontBufferIdx = 0;
  m_backBufferIdx = 1;
  m_activeRenderBuffer = 1;  // BACKBUFFER
  m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
  GLIDE_LOG(INFO, "SoftwareBackendBase",
            "Allocated CPU double buffers (padded for SIMD safety): "
                << width << "x" << height << ", map=" << m_headlessPixelMap
                << ", resolved size=" << m_resolvedBuffer.size());
}

void SoftwareBackendBase::FreeCpuBuffers_Locked() {
  m_cpuBuffers[0].clear();
  m_cpuBuffers[0].shrink_to_fit();
  m_cpuBuffers[1].clear();
  m_cpuBuffers[1].shrink_to_fit();
  m_resolvedBuffer.clear();
  m_resolvedBuffer.shrink_to_fit();
  m_headlessDepthBuffer.clear();
  m_headlessDepthBuffer.shrink_to_fit();
  m_pendingCpuColorClear[0] = {};
  m_pendingCpuColorClear[1] = {};
  m_pendingCpuDepthClear = false;
  m_headlessPixelMap = nullptr;
  GLIDE_LOG(INFO, "SoftwareBackendBase", "Freed CPU double buffers.");
}

void SoftwareBackendBase::SetRenderBuffer(uint32_t target) {
  std::lock_guard<std::mutex> lock(m_mutex);
  GlideStateTracker::SetRenderBuffer_Locked(target);
  uint32_t slot = (m_activeRenderBuffer == 0) ? m_frontBufferIdx
                                              : m_backBufferIdx;
  m_headlessPixelMap =
      m_cpuBuffers[slot].empty() ? nullptr : m_cpuBuffers[slot].data();
  GLIDE_LOG(DEBUG, "SoftwareBackendBase",
            "SetRenderBuffer: target=" << target
                                       << ", map=" << m_headlessPixelMap);
}

void SoftwareBackendBase::ResolveAAMode(const std::string& aaMode) {
  m_softwareAaMode = "analytical";
  m_ssaaScale = 1;
  if (aaMode == "ssaa") {
    m_softwareAaMode = "ssaa";
    m_ssaaScale = 2;
    GLIDE_LOG(
        INFO, "Software",
        "Supersampling Anti-Aliasing (SSAA) mode enabled (2x scaling).");
  } else {
    GLIDE_LOG(INFO, "Software",
              "Analytical Point/Line Anti-Aliasing mode enabled (default).");
  }
}

}  // namespace GlideWrapper
