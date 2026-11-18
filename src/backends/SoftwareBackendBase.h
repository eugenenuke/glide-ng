#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backends/CpuLfbStaging.h"
#include "backends/GlideStateTracker.h"

namespace GlideWrapper {

/**
 * @brief CPU framebuffer, depth buffer, SSAA resolve buffer, and LFB base
 * exclusively for SoftwareBackend (CPU rasterizer).
 *
 * GPU backends (VulkanBackend, OpenGLESBackend) inherit directly from
 * GlideStateTracker and compose CpuLfbStaging instead, avoiding software depth
 * buffers, SSAA arrays, and eager CPU framebuffer allocations.
 */
class SoftwareBackendBase : public GlideStateTracker {
 public:
  SoftwareBackendBase() = default;
  ~SoftwareBackendBase() override = default;

  void ResetState() override;
  uint32_t GetMsaaSamples() const override { return m_ssaaScale * m_ssaaScale; }

  bool HasPendingLazyCpuClear(uint32_t buffer = 1) const {
    uint32_t slot = (buffer == 0) ? m_frontBufferIdx : m_backBufferIdx;
    return m_pendingCpuColorClear[slot].pending;
  }

  float GetDepthBufferPixel(uint32_t x, uint32_t y) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* self = const_cast<SoftwareBackendBase*>(this);
    self->FlushBins_Locked();
    self->MaterializeLazyCpuDepthClear_Locked();
    uint32_t pIdx = y * m_headlessWidth + x;
    if (pIdx < m_headlessDepthBuffer.size()) return m_headlessDepthBuffer[pIdx];
    return 0.0f;
  }

  uint8_t GetStencilBufferPixel(uint32_t x, uint32_t y) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* self = const_cast<SoftwareBackendBase*>(this);
    self->FlushBins_Locked();
    uint32_t pIdx = y * m_headlessWidth + x;
    if (pIdx < m_headlessStencilBuffer.size()) return m_headlessStencilBuffer[pIdx];
    return 0u;
  }

  void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                   uint32_t clearMask) override;
  void ClearBufferExt(uint32_t color, uint32_t alpha, float z, uint8_t stencil,
                      uint32_t clearMask) override;
  void SetRenderBuffer(uint32_t target) override;

  // Linear Framebuffer (LFB) read/write utilities backed by m_cpuBuffers
  void* GetLfbWritePtr() override;
  bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth,
               uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
  bool WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                uint32_t srcFmt, const void* srcData,
                const void* refData = nullptr,
                const LfbPipelineConfig& config = {}) override;

 protected:
  void ResetState_Locked() override;

  void* m_headlessPixelMap{nullptr};
  bool m_lfbDirty{false};
  bool m_lfbBufferDirty[2]{
      false, false};  // 0 = FRONTBUFFER dirty, 1 = BACKBUFFER dirty

  void AllocateCpuBuffers_Locked(uint32_t width, uint32_t height);
  void FreeCpuBuffers_Locked();

  // Deferred CPU-side clear tracking (used by lazy-clear probes and software
  // LFB materialization).
  struct PendingCpuColorClear {
    bool pending{false};
    uint32_t color{0};
    uint32_t alpha{255};
    uint32_t pixelFormat{1};
    uint8_t colorWriteMask4{0xFu};
  };
  PendingCpuColorClear m_pendingCpuColorClear[2];
  bool m_pendingCpuDepthClear{false};
  float m_pendingCpuDepthValue{1.0f};

  void ScheduleLazyCpuClear(uint32_t color, uint32_t alpha, float z,
                            uint32_t clearMask);
  void ClearBuffer_Locked(uint32_t color, uint32_t alpha, float z,
                          uint32_t clearMask);
  void ClearBufferExt_Locked(uint32_t color, uint32_t alpha, float z,
                             uint8_t stencil, uint32_t clearMask);
  void MaterializeLazyCpuClear_Locked(uint32_t bufferSlot);
  void MaterializeLazyCpuDepthClear_Locked();
  void DiscardLazyCpuClear_Locked(uint32_t bufferSlot);

  std::vector<uint8_t, AlignedAllocator<uint8_t, 64>> m_cpuBuffers[2];
  std::vector<uint8_t, AlignedAllocator<uint8_t, 64>> m_resolvedBuffer;
  std::vector<float, AlignedAllocator<float, 64>> m_headlessDepthBuffer;
  std::vector<uint8_t, AlignedAllocator<uint8_t, 64>> m_headlessStencilBuffer;
  uint32_t m_frontBufferIdx{0};
  uint32_t m_backBufferIdx{1};

  // Software Anti-Aliasing (AA / SSAA) configuration
  std::string m_softwareAaMode{"analytical"};
  uint32_t m_ssaaScale{1};
  void ResolveAAMode(const std::string& aaMode);
};

}  // namespace GlideWrapper
