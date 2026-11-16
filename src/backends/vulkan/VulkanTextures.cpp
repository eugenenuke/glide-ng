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
  auto hash_integer = [&](uint32_t val) {
    for (int i = 0; i < 4; ++i) {
      hash ^= (val & 0xFF);
      hash *= prime;
      val >>= 8;
    }
  };

  hash_integer(tex.baseWidth);
  hash_integer(tex.baseHeight);
  hash_integer(static_cast<uint32_t>(tex.swizzledMipLevels.size()));

  // Hash raw swizzled mipmap pixel data
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

void VulkanBackend::RetireTextureAtAddress_Locked(uint32_t startAddress) {
  auto it = m_vulkanTextures.find(startAddress);
  if (it == m_vulkanTextures.end()) return;

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
  m_vulkanTextures.erase(it);
  m_descriptorStateDirty = true;
}

void VulkanBackend::ReclaimRetiredResources_Locked(uint32_t slot) {
  if (slot >= 2) return;
  m_retiredDescriptorSets[slot].clear();
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
  auto it = m_vulkanTextures.find(startAddress);
  if (it != m_vulkanTextures.end()) {
    if (it->second.dataHash == incomingHash) {
      GLIDE_LOG(DEBUG, "Vulkan",
                "UploadTexture: Address=0x"
                    << std::hex << startAddress << std::dec
                    << " data unmodified (hash match: 0x" << std::hex
                    << incomingHash << std::dec << "). Bypassing upload!");
      return;
    }
  }

  GLIDE_PROFILE_SCOPE("Vulkan::UploadTexture");

  GLIDE_LOG(DEBUG, "Vulkan",
            "UploadTexture TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " Size: " << tex.baseWidth << "x" << tex.baseHeight
                << " Mipmap levels: " << tex.swizzledMipLevels.size()
                << " (Hash: 0x" << std::hex << incomingHash << std::dec << ")");

  // Retire any existing texture at this address (and its cached descriptor
  // sets) into the current frame slot's deferred destruction queue so in-flight
  // GPU draws never read a freed image/view.
  RetireTextureAtAddress_Locked(startAddress);

  // Calculate size of this texture
  size_t totalBytes = 0;
  for (const auto& level : tex.swizzledMipLevels) {
    totalBytes += level.size() * sizeof(uint32_t);
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
    // 1. Create vk::Image
    vk::ImageCreateInfo imageInfo(
        {}, vk::ImageType::e2D, vk::Format::eB8G8R8A8Unorm,
        vk::Extent3D(tex.baseWidth, tex.baseHeight, 1),
        static_cast<uint32_t>(tex.swizzledMipLevels.size()), 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferDst);
    auto image = m_device->createImageUnique(imageInfo);

    // 2. Allocate and bind memory
    auto memReqs = m_device->getImageMemoryRequirements(image.get());
    uint32_t typeIndex = FindMemoryType(
        memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
    auto memory = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindImageMemory(image.get(), memory.get(), 0);

    // 3. Copy all mipmap levels data into our persistent staging memory map
    uint8_t* mappedData =
        static_cast<uint8_t*>(m_texStagingMap) + currentOffset;
    size_t offset = 0;
    std::vector<size_t> levelOffsets;
    for (const auto& level : tex.swizzledMipLevels) {
      levelOffsets.push_back(currentOffset + offset);
      size_t levelBytes = level.size() * sizeof(uint32_t);
      std::memcpy(mappedData + offset, level.data(), levelBytes);
      offset += levelBytes;
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

    // Transition entire image to TransferDstOptimal
    vk::ImageSubresourceRange entireImageRange(
        vk::ImageAspectFlagBits::eColor, 0,
        static_cast<uint32_t>(tex.swizzledMipLevels.size()), 0, 1);
    TransitionImageLayout(
        *m_commandBuffer, image.get(), vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal, entireImageRange);

    // Copy each mipmap level from staging buffer to image
    std::vector<vk::BufferImageCopy> copyRegions;
    uint32_t width = tex.baseWidth;
    uint32_t height = tex.baseHeight;
    for (uint32_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
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
        m_texStagingBuffer.get(), image.get(),
        vk::ImageLayout::eTransferDstOptimal,
        static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

    // Transition entire image to ShaderReadOnlyOptimal
    TransitionImageLayout(
        *m_commandBuffer, image.get(), vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, entireImageRange);

    // 6. Create Image View
    vk::ImageViewCreateInfo viewInfo(
        {}, image.get(), vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm, {},
        {vk::ImageAspectFlagBits::eColor, 0,
         static_cast<uint32_t>(tex.swizzledMipLevels.size()), 0, 1});
    auto imageView = m_device->createImageViewUnique(viewInfo);

    // 7. Save in texture cache map
    VulkanTexture vulkanTex;
    vulkanTex.image = std::move(image);
    vulkanTex.memory = std::move(memory);
    vulkanTex.imageView = std::move(imageView);
    vulkanTex.clampS = m_texClampS[0];
    vulkanTex.clampT = m_texClampT[0];
    vulkanTex.minFilter = m_texMinFilter[0];
    vulkanTex.magFilter = m_texMagFilter[0];
    vulkanTex.dataHash = incomingHash;

    m_vulkanTextures[startAddress] = std::move(vulkanTex);
    m_descriptorStateDirty = true;

    GLIDE_LOG(DEBUG, "Vulkan",
              "UploadTexture: Completed successfully for address 0x"
                  << std::hex << startAddress << std::dec
                  << " (No GPU block!)");
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

  auto it = m_vulkanTextures.find(startAddress);
  if (it != m_vulkanTextures.end()) {
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

  auto it = m_vulkanTextures.find(startAddress);
  if (it == m_vulkanTextures.end()) {
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
                << m_vulkanTextures.size() << " textures, "
                << m_descriptorCache.size() << " descriptors.");

  for (auto& kv : m_descriptorCache) {
    m_retiredDescriptorSets[m_currentFrameSlot].push_back(std::move(kv.second));
  }
  m_descriptorCache.clear();
  for (auto& kv : m_vulkanTextures) {
    m_retiredTextures[m_currentFrameSlot].push_back(std::move(kv.second));
  }
  m_vulkanTextures.clear();
  m_descriptorStateDirty = true;
  m_cachedDescSet = vk::DescriptorSet{};

  GLIDE_LOG(DEBUG, "Vulkan", "PurgeTextures completed successfully.");
}

}  // namespace GlideWrapper
