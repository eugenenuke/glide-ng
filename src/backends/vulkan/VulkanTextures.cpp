#include "VulkanBackend.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

static uint64_t CalculateTextureHash(const struct VirtualTexture& tex) {
  uint64_t hash = 14695981039346656037ULL;
  const uint64_t prime = 1099511628211ULL;

  // Hash dimensions and mipmap count to protect against collisions
  auto hash_u32 = [&](uint32_t val) {
    for (int i = 0; i < 4; ++i) {
      hash ^= (val & 0xFF);
      hash *= prime;
      val >>= 8;
    }
  };
  auto hash_u64 = [&](uint64_t val) {
    for (int i = 0; i < 8; ++i) {
      hash ^= (val & 0xFF);
      hash *= prime;
      val >>= 8;
    }
  };

  hash_u32(tex.baseWidth);
  hash_u32(tex.baseHeight);
  hash_u32(static_cast<uint32_t>(tex.swizzledMipLevels.size()));

  // Fast O(1) path when TextureManager already hashed the full raw mipmap chain
  // (rawDataHash == 0 after partial sub-updates or direct unit-test setup).
  if (tex.rawDataHash != 0) {
    hash_u32(tex.format);
    hash_u32(tex.swizzledWithTable ? 1u : 0u);
    hash_u64(tex.rawDataHash);
    hash_u64(tex.swizzledTableHash);
    return hash;
  }

  // Fallback: hash raw swizzled mipmap pixel data
  for (const auto& level : tex.swizzledMipLevels) {
    const uint8_t* byteData = reinterpret_cast<const uint8_t*>(level.data());
    size_t byteLength = level.size() * sizeof(uint32_t);
    for (size_t i = 0; i < byteLength; ++i) {
      hash ^= byteData[i];
      hash *= prime;
    }
  }
  return hash;
}

void VulkanBackend::RetireTextureAtAddress_Locked(uint32_t tmu,
                                                  uint32_t startAddress) {
  if (tmu >= 2) return;
  auto& tmuMap = m_vulkanTextures[tmu];
  auto it = tmuMap.find(startAddress);
  if (it == tmuMap.end()) return;

  vk::ImageView oldView = it->second.imageView.get();
  if (oldView) {
    for (auto dit = m_descriptorCache.begin(); dit != m_descriptorCache.end();) {
      if (dit->first.imageView0 == oldView ||
          dit->first.imageView1 == oldView) {
        m_retiredDescriptorSets[m_currentFrameSlot].push_back(
            std::move(dit->second));
        dit = m_descriptorCache.erase(dit);
      } else {
        ++dit;
      }
    }
  }

  m_retiredTextures[m_currentFrameSlot].push_back(std::move(it->second));
  tmuMap.erase(it);
  m_descriptorStateDirty = true;
}

void VulkanBackend::ReclaimRetiredResources_Locked(uint32_t slot) {
  if (slot >= 2) return;
  // Release retired descriptor sets first so no set references reclaimed views.
  m_retiredDescriptorSets[slot].clear();
  for (auto& tex : m_retiredTextures[slot]) {
    if (m_texturePool.size() < kMaxPooledTextures && tex.image && tex.memory &&
        tex.imageView && tex.width > 0 && tex.height > 0 && tex.mipLevels > 0) {
      m_texturePool.push_back(std::move(tex));
    }
  }
  m_retiredTextures[slot].clear();
  m_lfbOverlayStagingOffsets[slot] = 0;
}

void VulkanBackend::UploadTexture(uint32_t tmu, uint32_t startAddress,
                                  const struct VirtualTexture& tex) {
  std::lock_guard<std::mutex> lock(m_mutex);
  UploadTexture_Locked(tmu, startAddress, tex);
}

void VulkanBackend::UploadTexture_Locked(uint32_t tmu, uint32_t startAddress,
                                         const struct VirtualTexture& tex) {
  if (!m_initialized || !m_device) return;

  if (tmu >= 2) {
    GLIDE_LOG(WARN, "Vulkan",
              "UploadTexture ignored for unsupported TMU " << tmu);
    return;
  }

  // 1. Calculate incoming texture hash
  uint64_t incomingHash = CalculateTextureHash(tex);

  // 2. Check if texture already exists at this address and has matching data
  auto& tmuMap = m_vulkanTextures[tmu];
  auto it = tmuMap.find(startAddress);
  if (it != tmuMap.end()) {
    if (it->second.dataHash == incomingHash) {
      GLIDE_LOG(DEBUG, "Vulkan",
                "UploadTexture: Address=0x"
                    << std::hex << startAddress << std::dec
                    << " data unmodified (hash match: 0x" << std::hex
                    << incomingHash << std::dec << "). Bypassing upload!");
      return;
    }
  }

  if (tex.baseWidth == 0 || tex.baseHeight == 0 ||
      tex.swizzledMipLevels.empty()) {
    return;
  }

  GLIDE_PROFILE_SCOPE("Vulkan::UploadTexture");

  GLIDE_LOG(DEBUG, "Vulkan",
            "UploadTexture TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " Size: " << tex.baseWidth << "x" << tex.baseHeight
                << " Mipmap levels: " << tex.swizzledMipLevels.size()
                << " (Hash: 0x" << std::hex << incomingHash << std::dec << ")");

  // Flush any queued geometry referencing the previous texture before retiring
  // its descriptor sets and image views.
  FlushBatch();

  // Retire any existing texture at this address (and its cached descriptor
  // sets) into the current frame slot's deferred destruction queue so in-flight
  // GPU draws never read a freed image/view.
  RetireTextureAtAddress_Locked(tmu, startAddress);

  // Calculate exact expected staging size across all mip levels (matches
  // copyBufferToImage extents even if a mip level is defensively short).
  size_t totalBytes = 0;
  {
    uint32_t lw = tex.baseWidth;
    uint32_t lh = tex.baseHeight;
    for (size_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
      totalBytes += static_cast<size_t>(lw) * lh * sizeof(uint32_t);
      lw = std::max(1u, lw / 2);
      lh = std::max(1u, lh / 2);
    }
  }

  // Check if we have enough space in the active slot's staging buffer segment
  uint32_t slotBase = m_currentFrameSlot * m_texStagingSlotSize;
  uint32_t slotEnd = (m_currentFrameSlot + 1) * m_texStagingSlotSize;

  if (m_texStagingOffsets[m_currentFrameSlot] + totalBytes > slotEnd) {
    GLIDE_LOG(WARN, "Vulkan",
              "Texture staging slot size exceeded! Stalling GPU on waitIdle() "
              "and wrapping around...");
    m_device->waitIdle();
    m_texStagingOffsets[m_currentFrameSlot] = slotBase;
  }

  uint32_t currentOffset = m_texStagingOffsets[m_currentFrameSlot];

  try {
    const uint32_t mipCount =
        static_cast<uint32_t>(tex.swizzledMipLevels.size());
    VulkanTexture vulkanTex;
    bool reusedFromPool = false;

    // 1. Try to reuse a fence-reclaimed VkImage/DeviceMemory/ImageView of the
    //    exact same dimensions & mip count before allocating new GPU memory.
    for (auto pit = m_texturePool.begin(); pit != m_texturePool.end(); ++pit) {
      if (pit->width == tex.baseWidth && pit->height == tex.baseHeight &&
          pit->mipLevels == mipCount) {
        vulkanTex = std::move(*pit);
        m_texturePool.erase(pit);
        reusedFromPool = true;
        ++m_texturePoolReuses;
        break;
      }
    }

    if (!reusedFromPool) {
      // Create vk::Image
      vk::ImageCreateInfo imageInfo(
          {}, vk::ImageType::e2D, vk::Format::eB8G8R8A8Unorm,
          vk::Extent3D(tex.baseWidth, tex.baseHeight, 1), mipCount, 1,
          vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eSampled |
              vk::ImageUsageFlagBits::eTransferDst);
      vulkanTex.image = m_device->createImageUnique(imageInfo);

      // Allocate and bind memory
      auto memReqs = m_device->getImageMemoryRequirements(vulkanTex.image.get());
      uint32_t typeIndex = FindMemoryType(
          memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
      vulkanTex.memory = m_device->allocateMemoryUnique(allocInfo);
      m_device->bindImageMemory(vulkanTex.image.get(), vulkanTex.memory.get(),
                                0);

      // Create Image View
      vk::ImageViewCreateInfo viewInfo(
          {}, vulkanTex.image.get(), vk::ImageViewType::e2D,
          vk::Format::eB8G8R8A8Unorm, {},
          {vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, 1});
      vulkanTex.imageView = m_device->createImageViewUnique(viewInfo);
      vulkanTex.width = tex.baseWidth;
      vulkanTex.height = tex.baseHeight;
      vulkanTex.mipLevels = mipCount;
    }

    // 3. Copy all mipmap levels data into our persistent staging memory map
    uint8_t* mappedData =
        static_cast<uint8_t*>(m_texStagingMap) + currentOffset;
    size_t offset = 0;
    std::vector<size_t> levelOffsets;
    levelOffsets.reserve(tex.swizzledMipLevels.size());
    uint32_t lw = tex.baseWidth;
    uint32_t lh = tex.baseHeight;
    for (const auto& level : tex.swizzledMipLevels) {
      levelOffsets.push_back(currentOffset + offset);
      const size_t expectedBytes =
          static_cast<size_t>(lw) * lh * sizeof(uint32_t);
      const size_t availBytes = level.size() * sizeof(uint32_t);
      const size_t copyBytes = std::min(expectedBytes, availBytes);
      if (copyBytes > 0) {
        std::memcpy(mappedData + offset, level.data(), copyBytes);
      }
      if (copyBytes < expectedBytes) {
        std::memset(mappedData + offset + copyBytes, 0,
                    expectedBytes - copyBytes);
      }
      offset += expectedBytes;
      lw = std::max(1u, lw / 2);
      lh = std::max(1u, lh / 2);
    }

    // Flush mapped memory range to ensure visibility to GPU
    vk::MappedMemoryRange memRange(m_texStagingMemory.get(), currentOffset,
                                   totalBytes);
    m_device->flushMappedMemoryRanges(memRange);

    // 4. Update the offset in the staging buffer
    m_texStagingOffsets[m_currentFrameSlot] += totalBytes;

    // 5. Record copy commands into the active frame's command buffer!
    // First, if we are in an active render pass, we must temporarily end it!
    if (m_inRenderPass) {
      FlushBatch();
      m_commandBuffer->endRenderPass();
      m_inRenderPass = false;
    }

    // Transition entire image to TransferDstOptimal (eUndefined discards any
    // previous contents for pooled images, which we fully overwrite below)
    vk::ImageSubresourceRange entireImageRange(vk::ImageAspectFlagBits::eColor,
                                               0, mipCount, 0, 1);
    TransitionImageLayout(*m_commandBuffer, vulkanTex.image.get(),
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal,
                          entireImageRange);

    // Copy each mipmap level from staging buffer to image
    std::vector<vk::BufferImageCopy> copyRegions;
    copyRegions.reserve(mipCount);
    uint32_t width = tex.baseWidth;
    uint32_t height = tex.baseHeight;
    for (uint32_t i = 0; i < mipCount; ++i) {
      vk::BufferImageCopy region;
      region.bufferOffset = levelOffsets[i];
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
      region.imageOffset = vk::Offset3D(0, 0, 0);
      region.imageExtent = vk::Extent3D(width, height, 1);
      copyRegions.push_back(region);

      width = std::max(1u, width / 2);
      height = std::max(1u, height / 2);
    }

    m_commandBuffer->copyBufferToImage(
        m_texStagingBuffer.get(), vulkanTex.image.get(),
        vk::ImageLayout::eTransferDstOptimal,
        static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

    // Transition entire image to ShaderReadOnlyOptimal
    TransitionImageLayout(*m_commandBuffer, vulkanTex.image.get(),
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          entireImageRange);

    // 6. Save in texture cache map
    vulkanTex.clampS = m_texClampS[tmu];
    vulkanTex.clampT = m_texClampT[tmu];
    vulkanTex.minFilter = m_texMinFilter[tmu];
    vulkanTex.magFilter = m_texMagFilter[tmu];
    vulkanTex.dataHash = incomingHash;

    tmuMap[startAddress] = std::move(vulkanTex);
    m_descriptorStateDirty = true;

    GLIDE_LOG(DEBUG, "Vulkan",
              "UploadTexture: Completed successfully for address 0x"
                  << std::hex << startAddress << std::dec
                  << (reusedFromPool ? " (reused pooled VkImage)"
                                     : " (allocated new VkImage)"));
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "UploadTexture failed for Address=0x"
                  << std::hex << startAddress << std::dec << ": " << e.what());
  }
}

void VulkanBackend::BindTexture(uint32_t tmu, uint32_t startAddress,
                                uint32_t clampS, uint32_t clampT,
                                uint32_t minFilter, uint32_t magFilter) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && m_boundTexAddress[tmu] == startAddress &&
      m_texClampS[tmu] == clampS && m_texClampT[tmu] == clampT &&
      m_texMinFilter[tmu] == minFilter && m_texMagFilter[tmu] == magFilter) {
    return;
  }
  FlushBatch(true);

  // 1. Call base class GlideStateTracker::BindTexture_Locked to update pipeline
  // states
  GlideStateTracker::BindTexture_Locked(tmu, startAddress, clampS, clampT,
                                        minFilter, magFilter);
  m_descriptorStateDirty = true;

  if (!m_initialized || !m_device) return;

  if (tmu >= 2) return;

  if (startAddress == 0xFFFFFFFF) return;

  auto it = m_vulkanTextures[tmu].find(startAddress);
  if (it != m_vulkanTextures[tmu].end()) {
    auto& tex = it->second;
    tex.clampS = clampS;
    tex.clampT = clampT;
    tex.minFilter = minFilter;
    tex.magFilter = magFilter;
  }
}

void VulkanBackend::SetTexLodBias(uint32_t tmu, float bias) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2) {
    if (m_texLodBias[tmu] != bias) {
      FlushBatch(true);
      m_texLodBias[tmu] = bias;
      m_descriptorStateDirty = true;
      GLIDE_LOG(DEBUG, "Vulkan",
                "SetTexLodBias: TMU" << tmu << " set to " << bias);
    }
  }
}

void VulkanBackend::SetTexMipMapMode(uint32_t tmu, uint32_t mode,
                                     bool lodBlend) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2) {
    if (m_texMipMapMode[tmu] != mode || m_texLodBlend[tmu] != lodBlend) {
      FlushBatch(true);
      m_texMipMapMode[tmu] = mode;
      m_texLodBlend[tmu] = lodBlend;
      m_descriptorStateDirty = true;
      GLIDE_LOG(DEBUG, "Vulkan",
                "SetTexMipMapMode: TMU"
                    << tmu << " set to Mode=" << mode
                    << ", LodBlend=" << (lodBlend ? "Yes" : "No"));
    }
  }
}

void VulkanBackend::UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                                         const struct VirtualTexture& tex,
                                         uint32_t lodLevel, uint32_t startRow,
                                         uint32_t endRow) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_device) return;

  if (tmu >= 2) {
    GLIDE_LOG(WARN, "Vulkan",
              "UploadTexturePartial ignored for unsupported TMU " << tmu);
    return;
  }

  auto it = m_vulkanTextures[tmu].find(startAddress);
  if (it == m_vulkanTextures[tmu].end()) {
    GLIDE_LOG(WARN, "Vulkan",
              "UploadTexturePartial: texture at address 0x"
                  << std::hex << startAddress << std::dec
                  << " does not exist! Promoting to full upload.");
    UploadTexture_Locked(tmu, startAddress, tex);
    return;
  }

  auto& vTex = it->second;

  if (lodLevel >= tex.swizzledMipLevels.size()) {
    GLIDE_LOG(WARN, "Vulkan",
              "UploadTexturePartial: invalid lodLevel " << lodLevel);
    return;
  }

  uint32_t w = std::max(1u, tex.baseWidth >> lodLevel);
  uint32_t h = std::max(1u, tex.baseHeight >> lodLevel);
  if (startRow >= h || endRow > h || startRow >= endRow) {
    GLIDE_LOG(WARN, "Vulkan",
              "UploadTexturePartial: invalid row range "
                  << startRow << ".." << endRow << " for level height " << h);
    return;
  }

  uint32_t numRows = endRow - startRow;
  size_t rowBytes = w * sizeof(uint32_t);
  size_t partialBytes = numRows * rowBytes;

  uint32_t slotBase = m_currentFrameSlot * m_texStagingSlotSize;
  uint32_t slotEnd = (m_currentFrameSlot + 1) * m_texStagingSlotSize;

  if (m_texStagingOffsets[m_currentFrameSlot] + partialBytes > slotEnd) {
    GLIDE_LOG(WARN, "Vulkan",
              "Texture staging slot size exceeded during partial! Stalling GPU "
              "and wrapping around...");
    m_device->waitIdle();
    m_texStagingOffsets[m_currentFrameSlot] = slotBase;
  }

  uint32_t currentOffset = m_texStagingOffsets[m_currentFrameSlot];

  const uint32_t* srcPixels = tex.swizzledMipLevels[lodLevel].data();
  uint8_t* mappedData = static_cast<uint8_t*>(m_texStagingMap) + currentOffset;

  std::memcpy(mappedData, srcPixels + startRow * w, partialBytes);

  vk::MappedMemoryRange memRange(m_texStagingMemory.get(), currentOffset,
                                 partialBytes);
  m_device->flushMappedMemoryRanges(memRange);

  m_texStagingOffsets[m_currentFrameSlot] += partialBytes;

  if (m_inRenderPass) {
    FlushBatch();
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  vk::ImageSubresourceRange levelRange(vk::ImageAspectFlagBits::eColor,
                                       lodLevel, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, vTex.image.get(),
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageLayout::eTransferDstOptimal, levelRange);

  vk::BufferImageCopy region;
  region.bufferOffset = currentOffset;
  region.bufferRowLength = w;
  region.bufferImageHeight = h;
  region.imageSubresource = vk::ImageSubresourceLayers(
      vk::ImageAspectFlagBits::eColor, lodLevel, 0, 1);
  region.imageOffset = vk::Offset3D(0, startRow, 0);
  region.imageExtent = vk::Extent3D(w, numRows, 1);

  m_commandBuffer->copyBufferToImage(m_texStagingBuffer.get(), vTex.image.get(),
                                     vk::ImageLayout::eTransferDstOptimal, 1,
                                     &region);

  TransitionImageLayout(*m_commandBuffer, vTex.image.get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal, levelRange);

  vTex.dataHash = CalculateTextureHash(tex);

  GLIDE_LOG(DEBUG, "Vulkan",
            "UploadTexturePartial: Completed successfully for address 0x"
                << std::hex << startAddress << std::dec << " level=" << lodLevel
                << " rows=" << startRow << ".." << endRow);
}

void VulkanBackend::PurgeTextures() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_device) return;

  FlushBatch();

  GLIDE_LOG(DEBUG, "Vulkan",
            "PurgeTextures: retiring "
                << (m_vulkanTextures[0].size() + m_vulkanTextures[1].size())
                << " textures, " << m_descriptorCache.size()
                << " descriptors.");

  for (auto& kv : m_descriptorCache) {
    m_retiredDescriptorSets[m_currentFrameSlot].push_back(std::move(kv.second));
  }
  m_descriptorCache.clear();
  for (uint32_t t = 0; t < 2; ++t) {
    for (auto& kv : m_vulkanTextures[t]) {
      m_retiredTextures[m_currentFrameSlot].push_back(std::move(kv.second));
    }
    m_vulkanTextures[t].clear();
  }
  m_texturePool.clear();
  m_descriptorStateDirty = true;
  m_cachedDescSet = vk::DescriptorSet{};

  GLIDE_LOG(DEBUG, "Vulkan", "PurgeTextures completed successfully.");
}

}  // namespace GlideWrapper
