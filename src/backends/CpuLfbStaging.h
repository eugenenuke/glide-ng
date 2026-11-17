#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#include "core/IGraphicsBackend.h"

namespace GlideWrapper {

template <typename T, std::size_t Alignment = 64>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;
  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    void* ptr = nullptr;
    std::size_t actual_alignment = std::max(Alignment, sizeof(void*));
    std::size_t size = n * sizeof(T);
    std::size_t remainder = size % actual_alignment;
    if (remainder != 0) {
      size += actual_alignment - remainder;
    }
    if (posix_memalign(&ptr, actual_alignment, size) != 0) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(ptr);
  }

  void deallocate(T* p, std::size_t) noexcept { std::free(p); }

  template <typename U>
  struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  bool operator==(const AlignedAllocator&) const noexcept { return true; }
  bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

/**
 * @brief Shared CPU-side Linear Framebuffer (LFB) staging & format-conversion
 * helper composed by GPU backends (Vulkan, OpenGL ES) and reused by
 * SoftwareBackendBase.
 *
 * When configured with allocateEagerly=false (GPU backend default), zero bytes
 * of CPU framebuffer memory are allocated until the guest actually performs an
 * LFB read, write, or lock operation.
 */
class CpuLfbStaging {
 public:
  void Configure(uint32_t width, uint32_t height, bool allocateEagerly = false);
  void Release();
  void ResetPendingAndDirty();

  bool IsAllocated() const {
    return !m_buffers[0].empty() || !m_overlayBuffers[0].empty();
  }
  uint32_t GetWidth() const { return m_width; }
  uint32_t GetHeight() const { return m_height; }

  void ScheduleLazyClear(uint32_t logicalBuffer, uint32_t color, uint32_t alpha,
                         uint32_t pixelFormatOverride, bool maskRgb,
                         bool maskAlpha);
  void MaterializeLazyClear(uint32_t logicalBuffer);
  void DiscardLazyClear(uint32_t logicalBuffer);
  bool HasPendingLazyClear(uint32_t logicalBuffer = 1) const;

  void SwapBuffers();

  bool IsDirty() const { return m_dirty || m_overlayDirty[0] || m_overlayDirty[1]; }
  bool IsFullBufferDirty(uint32_t logicalBuffer) const {
    return m_bufferDirty[logicalBuffer == 0 ? 0 : 1];
  }
  bool IsBufferDirty(uint32_t logicalBuffer) const {
    const uint32_t idx = logicalBuffer == 0 ? 0 : 1;
    return m_bufferDirty[idx] || m_overlayDirty[idx];
  }
  bool IsOverlayDirty(uint32_t logicalBuffer) const {
    return m_overlayDirty[logicalBuffer == 0 ? 0 : 1];
  }
  void ClearDirtyFlags() {
    m_dirty = false;
    m_bufferDirty[0] = false;
    m_bufferDirty[1] = false;
    ClearOverlay(0);
    ClearOverlay(1);
  }
  void ClearBufferDirty(uint32_t logicalBuffer) {
    m_bufferDirty[logicalBuffer == 0 ? 0 : 1] = false;
    if (!m_bufferDirty[0] && !m_bufferDirty[1]) {
      m_dirty = false;
    }
  }
  void ClearOverlay(uint32_t logicalBuffer);

  // Returns true if the given LFB pipeline configuration can be composited
  // directly onto a GPU framebuffer via a standard (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
  // overlay quad without reading back the GPU framebuffer.
  static bool CanUseOverlayQuad(const LfbPipelineConfig& config);

  // Write modified/non-chromakeyed pixels into a sparse ARGB overlay buffer
  // (untouched pixels keep alpha=0) for zero-readback GPU compositing.
  bool WriteOverlayLFB(uint32_t logicalBuffer, uint32_t dstX, uint32_t dstY,
                       uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                       uint32_t srcFmt, const void* srcData,
                       const void* refData, const LfbPipelineConfig& config,
                       uint32_t pixelFormatOverride);

  // Inclusive row band [minY..maxY] in top-down framebuffer coordinates.
  struct DirtyBand {
    uint32_t minY{0};
    uint32_t maxY{0};
  };
  static constexpr size_t kMaxOverlayBands = 8;

  // Returns pointer to the full ARGB overlay buffer (row 0 = top) and outputs
  // the inclusive dirty row range [outMinY, outMaxY]. Returns nullptr if clean.
  const uint32_t* GetOverlayForGpuUpload(uint32_t logicalBuffer,
                                         uint32_t& outMinY,
                                         uint32_t& outMaxY) const;

  // Returns pointer to the full ARGB overlay buffer and outputs the sorted list
  // of disjoint dirty row bands (at most kMaxOverlayBands). Returns nullptr if clean.
  const uint32_t* GetOverlayBandsForGpuUpload(uint32_t logicalBuffer,
                                              const DirtyBand*& outBands,
                                              size_t& outCount) const;

  uint8_t* PrepareBufferForGpuReadback(uint32_t logicalBuffer);
  const uint8_t* PrepareBufferForGpuUpload(uint32_t logicalBuffer);
  void* GetWritePtr(uint32_t activeRenderBuffer);

  bool WriteLFB(uint32_t logicalBuffer, uint32_t dstX, uint32_t dstY,
                uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                uint32_t srcFmt, const void* srcData, const void* refData,
                const LfbPipelineConfig& config, uint32_t pixelFormatOverride);

  bool ReadRegion(uint32_t logicalBuffer, uint32_t srcX, uint32_t srcY,
                  uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride,
                  void* dstData);

  // Shared static pixel-conversion helpers reused by SoftwareBackendBase
  static bool IsValidSrcFormat(uint32_t srcFmt);
  static bool ReadPixelsRegion(const uint32_t* pixels, uint32_t width,
                               uint32_t height, uint32_t srcX, uint32_t srcY,
                               uint32_t srcWidth, uint32_t srcHeight,
                               uint32_t dstStride, void* dstData);
  static uint32_t WritePixelsRegion(uint32_t* pixels, uint32_t width,
                                    uint32_t height, uint32_t dstX,
                                    uint32_t dstY, uint32_t srcWidth,
                                    uint32_t srcHeight, int32_t srcStride,
                                    uint32_t srcFmt, const void* srcData,
                                    const void* refData,
                                    const LfbPipelineConfig& config,
                                    uint32_t pixelFormatOverride);

 private:
  struct PendingColorClear {
    bool pending{false};
    uint32_t color{0};
    uint32_t alpha{255};
    uint32_t pixelFormat{1};
    bool maskRgb{true};
    bool maskAlpha{true};
  };

  uint32_t SlotFor(uint32_t logicalBuffer) const {
    return (logicalBuffer == 0) ? m_frontIdx : m_backIdx;
  }
  bool EnsureAllocated();
  bool EnsureOverlayAllocated();
  void MaterializeSlot(uint32_t slot);
  void AddOverlayBand(uint32_t idx, uint32_t minY, uint32_t maxY);

  std::vector<uint8_t, AlignedAllocator<uint8_t, 64>> m_buffers[2];
  std::vector<uint32_t, AlignedAllocator<uint32_t, 64>> m_overlayBuffers[2];
  uint32_t m_width{0};
  uint32_t m_height{0};
  uint32_t m_frontIdx{0};
  uint32_t m_backIdx{1};
  bool m_dirty{false};
  bool m_bufferDirty[2]{false, false};
  bool m_overlayDirty[2]{false, false};
  DirtyBand m_overlayBands[2][kMaxOverlayBands]{};
  size_t m_overlayBandCount[2]{0, 0};
  PendingColorClear m_pendingClear[2];
};

}  // namespace GlideWrapper
