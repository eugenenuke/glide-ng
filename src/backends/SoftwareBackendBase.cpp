#include "SoftwareBackendBase.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/Logger.h"
#include "core/SimdKernels.h"

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
  ClearBufferExt_Locked(color, alpha, z, 0u, clearMask & ~0x4u);
}

void SoftwareBackendBase::ClearBufferExt(uint32_t color, uint32_t alpha,
                                         float z, uint8_t stencil,
                                         uint32_t clearMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ClearBufferExt_Locked(color, alpha, z, stencil, clearMask);
}

void SoftwareBackendBase::ClearBuffer_Locked(uint32_t color, uint32_t alpha,
                                             float z, uint32_t clearMask) {
  ClearBufferExt_Locked(color, alpha, z, 0u, clearMask & ~0x4u);
}

void SoftwareBackendBase::ClearBufferExt_Locked(uint32_t color, uint32_t alpha,
                                                float z, uint8_t stencil,
                                                uint32_t clearMask) {
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
    if (m_colorMaskR) writeMask |= 0x00FF0000u;
    if (m_colorMaskG) writeMask |= 0x0000FF00u;
    if (m_colorMaskB) writeMask |= 0x000000FFu;
    if (m_colorMaskAlpha) writeMask |= 0xFF000000u;

    if (writeMask == 0) {
      GLIDE_LOG(DEBUG, "Rasterizer",
                "ClearBuffer color clear skipped due to zero color mask (write "
                "masking active).");
    } else {
      const uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
      const uint32_t keepMask = ~writeMask;
      const uint32_t maskedClearWord =
          (writeMask == 0xFFFFFFFFu) ? clearWord : (clearWord & writeMask);
      uint32_t* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
      const uint32_t spanW = x1 - x0;
      const auto& kernels = GetSimdKernels();
      const uint32_t stride = m_headlessWidth;

      if (isTbufMasked) {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
        for (uint32_t y = y0; y < y1; ++y) {
          const uint32_t rowMask = (tbufMask >> ((y & 1u) << 1)) & 3u;
          if (rowMask == 0u) continue;
          uint32_t* row = pixels + static_cast<size_t>(y) * stride;
          kernels.ClearColorTBufferRow(row, x0, x1, clearWord, keepMask,
                                       maskedClearWord, rowMask);
        }
      } else {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
        for (uint32_t y = y0; y < y1; ++y) {
          uint32_t* row = pixels + static_cast<size_t>(y) * stride + x0;
          kernels.ClearColorMasked(row, spanW, maskedClearWord, keepMask);
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
    const auto& kernels = GetSimdKernels();
    const uint32_t stride = m_headlessWidth;
    const uint32_t spanW = x1 - x0;
    if (fullClip && !isTbufMasked) {
      m_pendingCpuDepthClear = false;
      if (!m_headlessDepthBuffer.empty()) {
        float* depthData = m_headlessDepthBuffer.data();
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(m_headlessHeight) * stride > \
                             64000)
#endif
        for (uint32_t y = 0; y < m_headlessHeight; ++y) {
          kernels.ClearDepthTBufferRow(
              depthData + static_cast<size_t>(y) * stride, 0u, stride, z, 3u);
        }
        GLIDE_LOG(DEBUG, "Rasterizer", "Cleared depth buffer with value " << z);
      }
    } else {
      MaterializeLazyCpuDepthClear_Locked();
      if (!m_headlessDepthBuffer.empty()) {
        float* depthData = m_headlessDepthBuffer.data();
        if (isTbufMasked) {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
          for (uint32_t y = y0; y < y1; ++y) {
            const uint32_t rowMask = (tbufMask >> ((y & 1u) << 1)) & 3u;
            if (rowMask == 0u) continue;
            kernels.ClearDepthTBufferRow(
                depthData + static_cast<size_t>(y) * stride, x0, x1, z,
                rowMask);
          }
        } else {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
          for (uint32_t y = y0; y < y1; ++y) {
            kernels.ClearDepthTBufferRow(
                depthData + static_cast<size_t>(y) * stride, x0, x1, z, 3u);
          }
        }
        GLIDE_LOG(DEBUG, "Rasterizer",
                  "Cleared depth buffer rect (" << x0 << "," << y0 << ")-("
                                                << x1 << "," << y1
                                                << ") with value " << z);
      }
    }
  }

  // Stencil Clear (obeys GR_STENCIL_MODE_EXT, grStencilMaskExt, grClipWindow,
  // and T-Buffer write mask).
  if ((clearMask & 0x4) && m_stencilEnabled && m_stencilWriteMask != 0u &&
      !m_headlessStencilBuffer.empty()) {
    const uint8_t wmask = m_stencilWriteMask;
    const uint8_t keepMask = static_cast<uint8_t>(~wmask);
    const uint8_t maskedStencil =
        (wmask == 0xFFu) ? stencil : static_cast<uint8_t>(stencil & wmask);
    uint8_t* stencilBuf = m_headlessStencilBuffer.data();
    const uint32_t spanW = x1 - x0;
    const uint32_t stride = m_headlessWidth;
    const auto& kernels = GetSimdKernels();

    if (isTbufMasked) {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
      for (uint32_t y = y0; y < y1; ++y) {
        const uint32_t rowMask = (tbufMask >> ((y & 1u) << 1)) & 3u;
        if (rowMask == 0u) continue;
        uint8_t* sRow = stencilBuf + static_cast<size_t>(y) * stride;
        kernels.ClearStencilTBufferRow(sRow, x0, x1, stencil, maskedStencil,
                                       keepMask, rowMask);
      }
    } else {
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(y1 - y0) * spanW > 64000)
#endif
      for (uint32_t y = y0; y < y1; ++y) {
        uint8_t* sRow = stencilBuf + static_cast<size_t>(y) * stride + x0;
        kernels.ClearStencilMasked(sRow, spanW, maskedStencil, keepMask);
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
    const uint8_t mask4 = GetColorWriteMask4();
    if (mask4 != 0u) {
      auto& slotState = m_pendingCpuColorClear[activeSlot];
      if (slotState.pending && mask4 != 0xFu &&
          slotState.colorWriteMask4 != mask4) {
        MaterializeLazyCpuClear_Locked(activeSlot);
      }
      slotState.pending = true;
      slotState.color = color;
      slotState.alpha = alpha;
      slotState.pixelFormat = m_pixelFormatOverride;
      slotState.colorWriteMask4 = mask4;
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
  if (pending.colorWriteMask4 & 0x1u) writeMask |= 0x00FF0000u;
  if (pending.colorWriteMask4 & 0x2u) writeMask |= 0x0000FF00u;
  if (pending.colorWriteMask4 & 0x4u) writeMask |= 0x000000FFu;
  if (pending.colorWriteMask4 & 0x8u) writeMask |= 0xFF000000u;
  if (writeMask == 0) return;

  uint32_t a = (pending.alpha > 0) ? pending.alpha : 255;
  uint32_t r = (pending.color >> 16) & 0xFF;
  uint32_t g = (pending.color >> 8) & 0xFF;
  uint32_t b = pending.color & 0xFF;
  if (pending.pixelFormat == 1) {
    std::swap(r, b);
  }

  const uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
  const uint32_t keepMask = ~writeMask;
  const uint32_t maskedClearWord =
      (writeMask == 0xFFFFFFFFu) ? clearWord : (clearWord & writeMask);
  uint32_t* pixels =
      reinterpret_cast<uint32_t*>(m_cpuBuffers[bufferSlot].data());
  const auto& kernels = GetSimdKernels();
  const uint32_t stride = m_headlessWidth;
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(m_headlessHeight) * stride > \
                             64000)
#endif
  for (uint32_t y = 0; y < m_headlessHeight; ++y) {
    kernels.ClearColorMasked(pixels + static_cast<size_t>(y) * stride, stride,
                             maskedClearWord, keepMask);
  }
}

void SoftwareBackendBase::MaterializeLazyCpuDepthClear_Locked() {
  if (!m_pendingCpuDepthClear) return;
  m_pendingCpuDepthClear = false;
  if (!m_headlessDepthBuffer.empty()) {
    const auto& kernels = GetSimdKernels();
    float* depthData = m_headlessDepthBuffer.data();
    const uint32_t stride = m_headlessWidth;
    const float z = m_pendingCpuDepthValue;
#ifdef _OPENMP
#pragma omp parallel for if (static_cast<size_t>(m_headlessHeight) * stride > \
                             64000)
#endif
    for (uint32_t y = 0; y < m_headlessHeight; ++y) {
      kernels.ClearDepthTBufferRow(depthData + static_cast<size_t>(y) * stride,
                                   0u, stride, z, 3u);
    }
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

    const auto& kernels = GetSimdKernels();
    const uint32_t srcStride = m_headlessWidth;
#ifdef _OPENMP
#pragma omp parallel for if (nativeHeight * nativeWidth > 64000)
#endif
    for (uint32_t y = 0; y < nativeHeight; ++y) {
      const uint32_t* sRow0 =
          srcPixels + static_cast<size_t>(2u * y) * srcStride;
      const uint32_t* sRow1 =
          srcPixels + static_cast<size_t>(2u * y + 1u) * srcStride;
      uint32_t* dRow = dstPixels + static_cast<size_t>(y) * nativeWidth;
      kernels.ResolveSsaa2x2Row(sRow0, sRow1, dRow, nativeWidth);
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
    const auto& kernels = GetSimdKernels();
    for (uint32_t y = ry0; y < ry1; ++y) {
      const uint32_t* sRow0 =
          pixels + static_cast<size_t>(2u * y) * m_headlessWidth + 2u * rx0;
      const uint32_t* sRow1 =
          pixels + static_cast<size_t>(2u * y + 1u) * m_headlessWidth + 2u * rx0;
      uint32_t* dRow = resolved + static_cast<size_t>(y) * guestW + rx0;
      kernels.ResolveSsaa2x2Row(sRow0, sRow1, dRow, rw);
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
  // Include 3 guard rows so 2x4 (AVX2) and 4x4 (AVX-512) SIMD quad/tile
  // loads/stores stay strictly in-bounds even on heights not divisible by 4
  // (e.g., 640x350).
  size_t rawSize = static_cast<size_t>(width) * (height + 3u) * 4u;
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
  m_headlessStencilBuffer.clear();
  m_headlessStencilBuffer.shrink_to_fit();
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
