#include "VulkanBackend.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "core/Logger.h"

namespace GlideWrapper {

void* VulkanBackend::GetLfbWritePtr() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return nullptr;
  return m_lfbStaging.GetWritePtr(m_activeRenderBuffer);
}

bool VulkanBackend::WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                             uint32_t srcWidth, uint32_t srcHeight,
                             int32_t srcStride, uint32_t srcFmt,
                             const void* srcData, const void* refData,
                             const LfbPipelineConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;
  return m_lfbStaging.WriteLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                               srcStride, srcFmt, srcData, refData, config,
                               m_pixelFormatOverride);
}

bool VulkanBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                            uint32_t srcWidth, uint32_t srcHeight,
                            uint32_t dstStride, void* dstData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  // If the CPU LFB buffer has pending writes, flush them to the GPU FBO first
  // so they are included in the readback!
  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }

  // 1. Flush pending batch draws
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }

  // Resolve the GPU image index based on the requested buffer (0 = FRONT, 1 =
  // BACK)
  uint32_t readIdx = (buffer == m_activeRenderBuffer)
                         ? GetActiveGpuBufferIdx()
                         : (1u - GetActiveGpuBufferIdx());

  // If the target read FBO has pending clears, we must execute them first!
  if (!m_inRenderPass &&
      (m_clearColorPending[readIdx] || m_clearDepthPending[readIdx])) {
    uint32_t origActive = m_activeRenderBuffer;
    m_activeRenderBuffer = buffer;
    EnsureRenderPassActive();
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
    m_activeRenderBuffer = origActive;
  }

  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  // 2. Transition layout from ColorAttachmentOptimal to TransferSrcOptimal
  vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, range);

  // 3. Record copy from GPU image to staging buffer
  vk::BufferImageCopy region;
  region.imageSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
  m_commandBuffer->copyImageToBuffer(
      m_headlessImages[readIdx].get(), vk::ImageLayout::eTransferSrcOptimal,
      m_headlessPixelBuffers[m_currentFrameSlot].get(), 1, &region);

  // 4. End and submit the command buffer immediately
  m_commandBuffer->end();
  vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
  m_graphicsQueue.submit(submitInfo, {});
  m_device->waitIdle();

  // Copy the GPU-rendered framebuffer from mapped staging memory into our
  // lazily-allocated CPU LFB staging buffer
  void* readDst = m_lfbStaging.PrepareBufferForGpuReadback(buffer);
  if (m_gpuStagingMaps[m_currentFrameSlot] && readDst) {
    std::memcpy(readDst, m_gpuStagingMaps[m_currentFrameSlot],
                m_headlessWidth * m_headlessHeight * 4);
    if (m_pixelFormatOverride == 1) {
      auto* dest = reinterpret_cast<uint32_t*>(readDst);
      for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
        uint32_t argb = dest[i];
        uint32_t b = argb & 0xFF;
        uint32_t g = (argb >> 8) & 0xFF;
        uint32_t r = (argb >> 16) & 0xFF;
        uint32_t a = (argb >> 24) & 0xFF;
        dest[i] = (a << 24) | (b << 16) | (g << 8) | r;
      }
    }
  }

  // 5. Reset and begin a new command buffer for subsequent commands in the
  // frame!
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  // 6. Transition image layout back to ColorAttachmentOptimal so it is ready
  // for any subsequent draw calls!
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, range);

  // 7. Convert and copy the synchronized pixels to the client buffer!
  return m_lfbStaging.ReadRegion(buffer, srcX, srcY, srcWidth, srcHeight,
                                 dstStride, dstData);
}

void VulkanBackend::FlushLFBToGPU_Locked() {
  if (!m_lfbStaging.IsDirty()) return;

  GLIDE_LOG(DEBUG, "Vulkan", "FlushLFBToGPU: Checking dirty buffers...");
  if (!m_commandBuffer) {
    GLIDE_LOG(
        CRITICAL, "Vulkan",
        "FlushLFBToGPU: CRITICAL ERROR - m_commandBuffer is NULL pointer!");
    std::cerr << "[CRIT] FlushLFBToGPU: m_commandBuffer is NULL pointer!\n";
  } else if (!(*m_commandBuffer)) {
    GLIDE_LOG(
        CRITICAL, "Vulkan",
        "FlushLFBToGPU: CRITICAL ERROR - *m_commandBuffer is NULL handle!");
    std::cerr << "[CRIT] FlushLFBToGPU: *m_commandBuffer is NULL handle!\n";
  }

  // Resolve the GPU image indices for FRONT and BACK buffers
  uint32_t gpuFrontIdx = (0 == m_activeRenderBuffer)
                             ? GetActiveGpuBufferIdx()
                             : (1u - GetActiveGpuBufferIdx());
  uint32_t gpuBackIdx = (1 == m_activeRenderBuffer)
                            ? GetActiveGpuBufferIdx()
                            : (1u - GetActiveGpuBufferIdx());

  // We will flush the dirty buffer. In practice, only one is dirty at a time.
  // If both are dirty, we warn and flush the back buffer.
  uint32_t bufferToFlush = 2;  // 2 = none
  uint32_t targetGpuIdx = 0;
  const void* srcCpuData = nullptr;

  const bool frontDirty = m_lfbStaging.IsBufferDirty(0);
  const bool backDirty = m_lfbStaging.IsBufferDirty(1);
  if (frontDirty && backDirty) {
    GLIDE_LOG(WARN, "Vulkan",
              "FlushLFBToGPU: Both FRONT and BACK LFB buffers are dirty in the "
              "same flush. Flushing BACK.");
    bufferToFlush = 1;
    targetGpuIdx = gpuBackIdx;
    srcCpuData = m_lfbStaging.PrepareBufferForGpuUpload(1);
  } else if (frontDirty) {
    bufferToFlush = 0;
    targetGpuIdx = gpuFrontIdx;
    srcCpuData = m_lfbStaging.PrepareBufferForGpuUpload(0);
  } else if (backDirty) {
    bufferToFlush = 1;
    targetGpuIdx = gpuBackIdx;
    srcCpuData = m_lfbStaging.PrepareBufferForGpuUpload(1);
  }

  if (bufferToFlush == 2 || !srcCpuData) {
    m_lfbStaging.ClearDirtyFlags();
    return;
  }

  // If there are pending clears on the target FBO, we MUST execute them on the
  // GPU first! Otherwise, the subsequent copyBufferToImage will be overwritten
  // when the clears are eventually executed!
  if (!m_inRenderPass && (m_clearColorPending[targetGpuIdx] ||
                          m_clearDepthPending[targetGpuIdx])) {
    GLIDE_LOG(DEBUG, "Vulkan",
              "FlushLFBToGPU: Executing pending clears on target FBO "
                  << targetGpuIdx << " before LFB upload...");
    uint32_t origActive = m_activeRenderBuffer;
    m_activeRenderBuffer =
        bufferToFlush;  // Temporarily set active buffer so
                        // EnsureRenderPassActive targets bufferToFlush!
    EnsureRenderPassActive();
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
    m_activeRenderBuffer = origActive;  // Restore
  }

  // 1. If we are inside an active render pass, we must end it first!
  if (m_inRenderPass) {
    FlushBatch();
    GLIDE_LOG(DEBUG, "Vulkan",
              "FlushLFBToGPU: Ending active render pass to perform CPU->GPU "
              "transfer...");
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  // Copy from CPU-side active buffer to GPU staging memory
  GLIDE_LOG(DEBUG, "Vulkan",
            "FlushLFBToGPU: [MEMCPY TRACE] m_currentFrameSlot = "
                << m_currentFrameSlot
                << ", bufferToFlush = " << bufferToFlush
                << ", copySize = " << (m_headlessWidth * m_headlessHeight * 4));

  if (m_gpuStagingMaps[m_currentFrameSlot]) {
    std::memcpy(m_gpuStagingMaps[m_currentFrameSlot], srcCpuData,
                m_headlessWidth * m_headlessHeight * 4);
  }

  // 2. Transition image to TransferDstOptimal
  vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[targetGpuIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferDstOptimal, range);

  // 3. Record copy from buffer to image
  vk::BufferImageCopy region;
  region.imageSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);

  // Flush mapped memory to ensure host writes are visible to GPU
  vk::MappedMemoryRange memRange(
      m_headlessPixelMemories[m_currentFrameSlot].get(), 0, VK_WHOLE_SIZE);
  m_device->flushMappedMemoryRanges(memRange);

  m_commandBuffer->copyBufferToImage(
      m_headlessPixelBuffers[m_currentFrameSlot].get(),
      m_headlessImages[targetGpuIdx].get(),
      vk::ImageLayout::eTransferDstOptimal, 1, &region);

  // 4. Transition image back to ColorAttachmentOptimal
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[targetGpuIdx].get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, range);

  // End and submit the upload command buffer immediately to ensure GPU FBO is
  // fully updated before any subsequent reads/draws!
  m_commandBuffer->end();
  vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
  m_graphicsQueue.submit(submitInfo, {});
  m_device->waitIdle();

  // Reset and begin a new command buffer for subsequent operations in the
  // frame!
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  // 5. Clear the dirty flags
  m_lfbStaging.ClearDirtyFlags();
}

}  // namespace GlideWrapper
