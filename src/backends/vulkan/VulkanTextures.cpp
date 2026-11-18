#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

static bool ResolveVulkanCompressedFormat(const struct VirtualTexture& tex,
                                          bool bcEnabled,
                                          vk::Format& outFormat,
                                          uint32_t& outBlockBytes) {
  if (!bcEnabled || tex.rawCompressedMipLevels.empty()) {
    return false;
  }
  switch (tex.format) {
    case 0x16u:  // GR_TEXFMT_ARGB_CMP_DXT1
      outFormat = vk::Format::eBc1RgbaUnormBlock;
      outBlockBytes = 8u;
      return true;
    case 0x17u:  // GR_TEXFMT_ARGB_CMP_DXT2
    case 0x18u:  // GR_TEXFMT_ARGB_CMP_DXT3
      outFormat = vk::Format::eBc2UnormBlock;
      outBlockBytes = 16u;
      return true;
    case 0x19u:  // GR_TEXFMT_ARGB_CMP_DXT4
    case 0x1Au:  // GR_TEXFMT_ARGB_CMP_DXT5
      outFormat = vk::Format::eBc3UnormBlock;
      outBlockBytes = 16u;
      return true;
    default:
      return false;
  }
}

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

  const uint32_t mipCount = static_cast<uint32_t>(
      !tex.swizzledMipLevels.empty() ? tex.swizzledMipLevels.size()
                                     : tex.rawCompressedMipLevels.size());
  hash_u32(tex.baseWidth);
  hash_u32(tex.baseHeight);
  hash_u32(mipCount);

  // Fast O(1) path when TextureManager already hashed the full raw mipmap chain
  // (rawDataHash == 0 after partial sub-updates or direct unit-test setup).
  if (tex.rawDataHash != 0) {
    hash_u32(tex.format);
    hash_u32(tex.swizzledWithTable ? 1u : 0u);
    hash_u64(tex.rawDataHash);
    hash_u64(tex.swizzledTableHash);
    return hash;
  }

  if (!tex.swizzledMipLevels.empty()) {
    // Fallback: hash raw swizzled mipmap pixel data
    for (const auto& level : tex.swizzledMipLevels) {
      const uint8_t* byteData = reinterpret_cast<const uint8_t*>(level.data());
      size_t byteLength = level.size() * sizeof(uint32_t);
      for (size_t i = 0; i < byteLength; ++i) {
        hash ^= byteData[i];
        hash *= prime;
      }
    }
  } else {
    hash_u32(tex.format);
    for (const auto& level : tex.rawCompressedMipLevels) {
      for (uint8_t byte : level) {
        hash ^= byte;
        hash *= prime;
      }
    }
  }
  return hash;
}

void VulkanBackend::EvictDescriptorsForView_Locked(vk::ImageView view,
                                                   bool deferDestruction) {
  if (!view) return;
  const uint64_t viewHandle = VkHandleToU64(static_cast<VkImageView>(view));
  auto vit = m_viewToDescriptorKeys.find(viewHandle);
  if (vit == m_viewToDescriptorKeys.end()) return;

  for (const auto& dkey : vit->second) {
    auto dit = m_descriptorCache.find(dkey);
    if (dit != m_descriptorCache.end()) {
      if (m_cachedDescSet == dit->second.get()) {
        m_cachedDescSet = vk::DescriptorSet{};
        m_descriptorStateDirty = true;
      }
      if (deferDestruction) {
        m_retiredDescriptorSets[m_currentFrameSlot].push_back(
            std::move(dit->second));
      }
      m_descriptorCache.erase(dit);
    }
  }
  m_viewToDescriptorKeys.erase(vit);
}

void VulkanBackend::RetireTextureAtAddress_Locked(uint32_t tmu,
                                                  uint32_t startAddress) {
  if (tmu >= 2) return;
  auto& tmuMap = m_vulkanTextures[tmu];
  auto it = tmuMap.find(startAddress);
  if (it == tmuMap.end()) return;

  // Keep cached VkDescriptorSets alive while the VkImageView moves through
  // m_retiredTextures into m_texturePool so pooled texture reuse hits
  // m_descriptorCache with zero vkAllocateDescriptorSets/vkUpdateDescriptorSets
  // calls. Only evict early if approaching descriptor pool capacity.
  if (m_descriptorCache.size() >= kDescriptorCacheHighWater) {
    EvictDescriptorsForView_Locked(it->second.imageView.get(), true);
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
    } else if (tex.imageView) {
      // Texture is not pooled and will be destroyed immediately below (its
      // GPU fence already signaled), so evict its cached descriptor sets now.
      EvictDescriptorsForView_Locked(tex.imageView.get(), false);
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

  vk::Format targetFormat = vk::Format::eB8G8R8A8Unorm;
  uint32_t bcBlockBytes = 0u;
  const bool useCompressed = ResolveVulkanCompressedFormat(
      tex, m_textureCompressionBcEnabled, targetFormat, bcBlockBytes);
  const uint32_t mipCount = static_cast<uint32_t>(
      useCompressed ? tex.rawCompressedMipLevels.size()
                    : tex.swizzledMipLevels.size());

  if (tex.baseWidth == 0 || tex.baseHeight == 0 || mipCount == 0) {
    return;
  }

  GLIDE_PROFILE_SCOPE("Vulkan::UploadTexture");

  GLIDE_LOG(DEBUG, "Vulkan",
            "UploadTexture TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " Size: " << tex.baseWidth << "x" << tex.baseHeight
                << " Mipmap levels: " << mipCount
                << (useCompressed ? " (BCn hardware compressed)" : "")
                << " (Hash: 0x" << std::hex << incomingHash << std::dec << ")");

  // Flush any queued geometry referencing the previous texture before retiring
  // its descriptor sets and image views.
  FlushBatch();

  // Retire any existing texture at this address (and its cached descriptor
  // sets) into the current frame slot's deferred destruction queue so in-flight
  // GPU draws never read a freed image/view.
  RetireTextureAtAddress_Locked(tmu, startAddress);

  // Align staging offsets to 16 bytes (required for BC1/BC2/BC3 texel blocks
  // and optimal for BGRA8 DMA transfers).
  size_t totalBytes = 0;
  {
    uint32_t lw = tex.baseWidth;
    uint32_t lh = tex.baseHeight;
    for (uint32_t i = 0; i < mipCount; ++i) {
      const size_t levelBytes =
          useCompressed
              ? (static_cast<size_t>((lw + 3u) / 4u) * ((lh + 3u) / 4u) *
                 bcBlockBytes)
              : (static_cast<size_t>(lw) * lh * sizeof(uint32_t));
      totalBytes += (levelBytes + 15u) & ~size_t(15u);
      lw = std::max(1u, lw / 2);
      lh = std::max(1u, lh / 2);
    }
  }

  // Check if we have enough space in the active slot's staging buffer segment
  uint32_t slotBase = m_currentFrameSlot * m_texStagingSlotSize;
  uint32_t slotEnd = (m_currentFrameSlot + 1) * m_texStagingSlotSize;
  uint32_t alignedStart = (m_texStagingOffsets[m_currentFrameSlot] + 15u) & ~15u;

  if (alignedStart + totalBytes > slotEnd) {
    GLIDE_LOG(WARN, "Vulkan",
              "Texture staging slot size exceeded! Flushing mid-frame and "
              "wrapping around...");
    SubmitMidFrameAndWaitIdle_Locked(true);
    m_texStagingOffsets[m_currentFrameSlot] = slotBase;
    alignedStart = (slotBase + 15u) & ~15u;
  }

  uint32_t currentOffset = alignedStart;

  try {
    VulkanTexture vulkanTex;
    bool reusedFromPool = false;

    // 1. Try to reuse a fence-reclaimed VkImage/DeviceMemory/ImageView of the
    //    exact same dimensions, mip count, and format before allocating new GPU
    //    memory.
    for (auto pit = m_texturePool.begin(); pit != m_texturePool.end(); ++pit) {
      if (pit->width == tex.baseWidth && pit->height == tex.baseHeight &&
          pit->mipLevels == mipCount && pit->format == targetFormat) {
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
          {}, vk::ImageType::e2D, targetFormat,
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
          {}, vulkanTex.image.get(), vk::ImageViewType::e2D, targetFormat, {},
          {vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, 1});
      vulkanTex.imageView = m_device->createImageViewUnique(viewInfo);
      vulkanTex.format = targetFormat;
      vulkanTex.width = tex.baseWidth;
      vulkanTex.height = tex.baseHeight;
      vulkanTex.mipLevels = mipCount;
    }

    // 3. Copy all mipmap levels data into our persistent HOST_COHERENT staging
    //    memory map (no flushMappedMemoryRanges needed).
    uint8_t* mappedData =
        static_cast<uint8_t*>(m_texStagingMap) + currentOffset;
    size_t offset = 0;
    constexpr size_t kMaxStackMips = 16;
    std::array<size_t, kMaxStackMips> stackLevelOffsets{};
    std::vector<size_t> heapLevelOffsets;
    const bool useStack = (mipCount <= kMaxStackMips);
    if (!useStack) {
      heapLevelOffsets.reserve(mipCount);
    }
    uint32_t lw = tex.baseWidth;
    uint32_t lh = tex.baseHeight;
    for (uint32_t i = 0; i < mipCount; ++i) {
      if (useStack) {
        stackLevelOffsets[i] = currentOffset + offset;
      } else {
        heapLevelOffsets.push_back(currentOffset + offset);
      }
      size_t expectedBytes = 0;
      const void* srcPtr = nullptr;
      size_t availBytes = 0;
      if (useCompressed) {
        const auto& level = tex.rawCompressedMipLevels[i];
        expectedBytes = static_cast<size_t>((lw + 3u) / 4u) *
                        ((lh + 3u) / 4u) * bcBlockBytes;
        srcPtr = level.data();
        availBytes = level.size();
      } else {
        const auto& level = tex.swizzledMipLevels[i];
        expectedBytes = static_cast<size_t>(lw) * lh * sizeof(uint32_t);
        srcPtr = level.data();
        availBytes = level.size() * sizeof(uint32_t);
      }
      const size_t copyBytes = std::min(expectedBytes, availBytes);
      if (copyBytes > 0) {
        std::memcpy(mappedData + offset, srcPtr, copyBytes);
      }
      if (copyBytes < expectedBytes) {
        std::memset(mappedData + offset + copyBytes, 0,
                    expectedBytes - copyBytes);
      }
      offset += (expectedBytes + 15u) & ~size_t(15u);
      lw = std::max(1u, lw / 2);
      lh = std::max(1u, lh / 2);
    }

    // 4. Update the offset in the staging buffer
    m_texStagingOffsets[m_currentFrameSlot] =
        static_cast<uint32_t>(currentOffset + totalBytes);

    // 5. Record copy commands into the per-frame pre-render upload command
    //    buffer! Because UploadTexture_Locked is Copy-on-Write (every upload
    //    targets a fresh or fence-reclaimed VkImage never read by earlier draws
    //    in this frame), recording here avoids ending/splitting the active
    //    RenderPass mid-frame.
    vk::CommandBuffer& uploadCmd = BeginUploadCommandBuffer_Locked();

    // Transition entire image to TransferDstOptimal (eUndefined discards any
    // previous contents for pooled images, which we fully overwrite below)
    vk::ImageSubresourceRange entireImageRange(vk::ImageAspectFlagBits::eColor,
                                               0, mipCount, 0, 1);
    TransitionImageLayout(uploadCmd, vulkanTex.image.get(),
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal,
                          entireImageRange);

    // Copy each mipmap level from staging buffer to image
    std::array<vk::BufferImageCopy, kMaxStackMips> stackCopyRegions{};
    std::vector<vk::BufferImageCopy> heapCopyRegions;
    if (!useStack) {
      heapCopyRegions.resize(mipCount);
    }
    vk::BufferImageCopy* copyRegionsPtr =
        useStack ? stackCopyRegions.data() : heapCopyRegions.data();
    const size_t* levelOffsetsPtr =
        useStack ? stackLevelOffsets.data() : heapLevelOffsets.data();

    uint32_t width = tex.baseWidth;
    uint32_t height = tex.baseHeight;
    for (uint32_t i = 0; i < mipCount; ++i) {
      vk::BufferImageCopy& region = copyRegionsPtr[i];
      region.bufferOffset = levelOffsetsPtr[i];
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
      region.imageOffset = vk::Offset3D(0, 0, 0);
      region.imageExtent = vk::Extent3D(width, height, 1);

      width = std::max(1u, width / 2);
      height = std::max(1u, height / 2);
    }

    uploadCmd.copyBufferToImage(
        m_texStagingBuffer.get(), vulkanTex.image.get(),
        vk::ImageLayout::eTransferDstOptimal, mipCount, copyRegionsPtr);

    // Transition entire image to ShaderReadOnlyOptimal
    TransitionImageLayout(uploadCmd, vulkanTex.image.get(),
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

void VulkanBackend::SetTexDetailControl(uint32_t tmu, int32_t lodBias,
                                        uint8_t detailScale, float detailMax) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  const int32_t clampedBias = std::clamp(lodBias, -32, 31);
  const uint8_t clampedScale = std::min<uint8_t>(detailScale, 7u);
  const float clampedMax = std::clamp(detailMax, 0.0f, 1.0f);
  if (m_texDetailBias[tmu] != clampedBias ||
      m_texDetailScale[tmu] != clampedScale ||
      m_texDetailMax[tmu] != clampedMax) {
    FlushBatch(true);
    GlideStateTracker::SetTexDetailControl_Locked(tmu, clampedBias,
                                                  clampedScale, clampedMax);
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
  if (!m_initialized || !m_device || !m_commandBuffer) return;

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
  if (vTex.format != vk::Format::eB8G8R8A8Unorm) {
    UploadTexture_Locked(tmu, startAddress, tex);
    return;
  }

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
              "Texture staging slot size exceeded during partial! Flushing "
              "mid-frame and wrapping around...");
    SubmitMidFrameAndWaitIdle_Locked(true);
    m_texStagingOffsets[m_currentFrameSlot] = slotBase;
  }

  // If any pre-frame COW uploads are queued, flush them so vTex is guaranteed
  // to already be in eShaderReadOnlyOptimal before this in-order partial update.
  if (m_uploadCmdHasWork[m_currentFrameSlot]) {
    SubmitPendingUploadsAndWaitIdle_Locked();
  }

  uint32_t currentOffset = m_texStagingOffsets[m_currentFrameSlot];

  const uint32_t* srcPixels = tex.swizzledMipLevels[lodLevel].data();
  uint8_t* mappedData = static_cast<uint8_t*>(m_texStagingMap) + currentOffset;

  std::memcpy(mappedData, srcPixels + startRow * w, partialBytes);

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
  region.bufferImageHeight = numRows;
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

  // Evict descriptor sets for idle pooled textures before destroying them so
  // no descriptor set ever outlives its VkImageView.
  for (auto& tex : m_texturePool) {
    if (tex.imageView) {
      EvictDescriptorsForView_Locked(tex.imageView.get(), false);
    }
  }
  m_texturePool.clear();

  for (auto& kv : m_descriptorCache) {
    m_retiredDescriptorSets[m_currentFrameSlot].push_back(std::move(kv.second));
  }
  m_descriptorCache.clear();
  m_viewToDescriptorKeys.clear();
  for (uint32_t t = 0; t < 2; ++t) {
    for (auto& kv : m_vulkanTextures[t]) {
      m_retiredTextures[m_currentFrameSlot].push_back(std::move(kv.second));
    }
    m_vulkanTextures[t].clear();
  }
  m_descriptorStateDirty = true;
  m_cachedDescSet = vk::DescriptorSet{};

  GLIDE_LOG(DEBUG, "Vulkan", "PurgeTextures completed successfully.");
}

bool VulkanBackend::BeginGpuTextureBufferMode() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_device || !m_windowAttached || !m_commandBuffer ||
      !(*m_commandBuffer) || !m_headlessImages[0] || m_headlessWidth == 0 ||
      m_headlessHeight == 0) {
    return false;
  }

  GLIDE_PROFILE_SCOPE("Vulkan::BeginGpuTextureBufferMode");
  FlushBatch();
  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }
  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }
  // Realize any deferred clear on BACKBUFFER (physical slot 0) before saving.
  if (m_clearColorPending[0]) {
    const uint32_t origActive = m_activeRenderBuffer;
    m_activeRenderBuffer = 1;  // GR_BUFFER_BACKBUFFER -> physical slot 0
    EnsureRenderPassActive(true);
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
    m_activeRenderBuffer = origActive;
  }
  ResolveMsaaToSingle_Locked(0);
  m_lfbStaging.ClearOverlay(1);
  m_lfbStaging.ClearBufferDirty(1);

  if (!m_rttSaveImage || m_rttSaveWidth != m_headlessWidth ||
      m_rttSaveHeight != m_headlessHeight) {
    if (m_rttSaveImage) {
      m_device->waitIdle();
      m_rttSaveImage.reset();
      m_rttSaveImageMemory.reset();
    }
    try {
      vk::ImageCreateInfo saveInfo(
          {}, vk::ImageType::e2D, m_colorFormat,
          vk::Extent3D(m_headlessWidth, m_headlessHeight, 1), 1, 1,
          vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eTransferSrc |
              vk::ImageUsageFlagBits::eTransferDst);
      m_rttSaveImage = m_device->createImageUnique(saveInfo);
      auto memReqs =
          m_device->getImageMemoryRequirements(m_rttSaveImage.get());
      const uint32_t typeIdx = FindMemoryType(
          memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      m_rttSaveImageMemory = m_device->allocateMemoryUnique(
          vk::MemoryAllocateInfo(memReqs.size, typeIdx));
      m_device->bindImageMemory(m_rttSaveImage.get(),
                                m_rttSaveImageMemory.get(), 0);
      m_rttSaveWidth = m_headlessWidth;
      m_rttSaveHeight = m_headlessHeight;
      m_rttSaveImageInitialized = false;
    } catch (const std::exception& e) {
      GLIDE_LOG(WARN, "Vulkan",
                "BeginGpuTextureBufferMode allocation failed: " << e.what());
      m_rttSaveImage.reset();
      m_rttSaveImageMemory.reset();
      m_rttSaveWidth = 0;
      m_rttSaveHeight = 0;
      m_rttSaveImageInitialized = false;
      return false;
    }
  }

  const vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0,
                                             1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, colorRange);

  if (!m_rttSaveImageInitialized) {
    TransitionImageLayout(*m_commandBuffer, m_rttSaveImage.get(),
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, colorRange);
  } else {
    vk::ImageMemoryBarrier warBarrier;
    warBarrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    warBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    warBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    warBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    warBarrier.image = m_rttSaveImage.get();
    warBarrier.subresourceRange = colorRange;
    warBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    warBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    m_commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     0, nullptr, 0, nullptr, 1, &warBarrier);
  }

  vk::ImageCopy copyRegion;
  copyRegion.srcSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.dstSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.extent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
  m_commandBuffer->copyImage(
      m_headlessImages[0].get(), vk::ImageLayout::eTransferSrcOptimal,
      m_rttSaveImage.get(), vk::ImageLayout::eTransferDstOptimal, 1,
      &copyRegion);

  TransitionImageLayout(*m_commandBuffer, m_rttSaveImage.get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, colorRange);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);
  m_rttSaveImageInitialized = true;
  return true;
}

bool VulkanBackend::CommitGpuTextureBuffer(
    uint32_t tmu, uint32_t startAddress, uint32_t baseWidth,
    uint32_t baseHeight, uint32_t mipLevel, uint32_t totalMipLevels,
    uint32_t copyWidth, uint32_t copyHeight, uint32_t origin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !m_initialized || !m_device || !m_windowAttached ||
      !m_commandBuffer || !(*m_commandBuffer) || !m_headlessImages[0] ||
      baseWidth == 0 || baseHeight == 0 || totalMipLevels == 0 ||
      mipLevel >= totalMipLevels) {
    return false;
  }

  GLIDE_PROFILE_SCOPE("Vulkan::CommitGpuTextureBuffer");
  FlushBatch();
  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }
  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }
  if (m_clearColorPending[0]) {
    const uint32_t origActive = m_activeRenderBuffer;
    m_activeRenderBuffer = 1;  // GR_BUFFER_BACKBUFFER -> physical slot 0
    EnsureRenderPassActive(true);
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
    m_activeRenderBuffer = origActive;
  }
  ResolveMsaaToSingle_Locked(0);

  const uint32_t mipW = std::max(1u, baseWidth >> mipLevel);
  const uint32_t mipH = std::max(1u, baseHeight >> mipLevel);
  const uint32_t cw = std::min(copyWidth, std::min(mipW, m_headlessWidth));
  const uint32_t ch = std::min(copyHeight, std::min(mipH, m_headlessHeight));
  if (cw == 0 || ch == 0) return false;
  const uint32_t srcY = (origin == 1u) ? (m_headlessHeight - ch) : 0u;

  auto& tmuMap = m_vulkanTextures[tmu];
  auto it = tmuMap.find(startAddress);
  const bool reuseInPlace =
      (it != tmuMap.end() && it->second.image &&
       it->second.width == baseWidth && it->second.height == baseHeight &&
       it->second.mipLevels == totalMipLevels &&
       it->second.format == m_colorFormat);

  vk::Image dstImage;
  if (reuseInPlace) {
    dstImage = it->second.image.get();
    it->second.dataHash = 0;
  } else {
    RetireTextureAtAddress_Locked(tmu, startAddress);
    try {
      VulkanTexture vulkanTex;
      bool reusedFromPool = false;
      for (auto pit = m_texturePool.begin(); pit != m_texturePool.end();
           ++pit) {
        if (pit->width == baseWidth && pit->height == baseHeight &&
            pit->mipLevels == totalMipLevels && pit->format == m_colorFormat) {
          vulkanTex = std::move(*pit);
          m_texturePool.erase(pit);
          reusedFromPool = true;
          ++m_texturePoolReuses;
          break;
        }
      }
      if (!reusedFromPool) {
        vk::ImageCreateInfo imageInfo(
            {}, vk::ImageType::e2D, m_colorFormat,
            vk::Extent3D(baseWidth, baseHeight, 1), totalMipLevels, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferDst);
        vulkanTex.image = m_device->createImageUnique(imageInfo);
        auto memReqs =
            m_device->getImageMemoryRequirements(vulkanTex.image.get());
        const uint32_t typeIndex = FindMemoryType(
            memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vulkanTex.memory = m_device->allocateMemoryUnique(
            vk::MemoryAllocateInfo(memReqs.size, typeIndex));
        m_device->bindImageMemory(vulkanTex.image.get(), vulkanTex.memory.get(),
                                  0);
        vk::ImageViewCreateInfo viewInfo(
            {}, vulkanTex.image.get(), vk::ImageViewType::e2D, m_colorFormat,
            {}, {vk::ImageAspectFlagBits::eColor, 0, totalMipLevels, 0, 1});
        vulkanTex.imageView = m_device->createImageViewUnique(viewInfo);
        vulkanTex.format = m_colorFormat;
        vulkanTex.width = baseWidth;
        vulkanTex.height = baseHeight;
        vulkanTex.mipLevels = totalMipLevels;
      }
      vulkanTex.clampS = m_texClampS[tmu];
      vulkanTex.clampT = m_texClampT[tmu];
      vulkanTex.minFilter = m_texMinFilter[tmu];
      vulkanTex.magFilter = m_texMagFilter[tmu];
      vulkanTex.dataHash = 0;
      dstImage = vulkanTex.image.get();
      tmuMap[startAddress] = std::move(vulkanTex);
      m_descriptorStateDirty = true;
    } catch (const std::exception& e) {
      GLIDE_LOG(WARN, "Vulkan",
                "CommitGpuTextureBuffer allocation failed: " << e.what());
      return false;
    }
  }

  const vk::ImageSubresourceRange fbRange(vk::ImageAspectFlagBits::eColor, 0, 1,
                                          0, 1);
  const vk::ImageSubresourceRange dstRange(
      vk::ImageAspectFlagBits::eColor, reuseInPlace ? mipLevel : 0u,
      reuseInPlace ? 1u : totalMipLevels, 0, 1);

  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, fbRange);
  TransitionImageLayout(*m_commandBuffer, dstImage,
                        reuseInPlace ? vk::ImageLayout::eShaderReadOnlyOptimal
                                     : vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eTransferDstOptimal, dstRange);

  const bool needsClear =
      (cw < mipW || ch < mipH || (!reuseInPlace && totalMipLevels > 1));
  if (needsClear) {
    const vk::ClearColorValue black(
        std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    m_commandBuffer->clearColorImage(
        dstImage, vk::ImageLayout::eTransferDstOptimal, &black, 1, &dstRange);
    vk::ImageMemoryBarrier clearToCopy;
    clearToCopy.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    clearToCopy.newLayout = vk::ImageLayout::eTransferDstOptimal;
    clearToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearToCopy.image = dstImage;
    clearToCopy.subresourceRange = dstRange;
    clearToCopy.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    clearToCopy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    m_commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     0, nullptr, 0, nullptr, 1, &clearToCopy);
  }

  vk::ImageCopy copyRegion;
  copyRegion.srcSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.srcOffset = vk::Offset3D(0, static_cast<int32_t>(srcY), 0);
  copyRegion.dstSubresource = vk::ImageSubresourceLayers(
      vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1);
  copyRegion.dstOffset = vk::Offset3D(0, 0, 0);
  copyRegion.extent = vk::Extent3D(cw, ch, 1);

  m_commandBuffer->copyImage(
      m_headlessImages[0].get(), vk::ImageLayout::eTransferSrcOptimal, dstImage,
      vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

  TransitionImageLayout(*m_commandBuffer, dstImage,
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal, dstRange);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, fbRange);
  return true;
}

void VulkanBackend::EndGpuTextureBufferMode() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_device || !m_windowAttached || !m_commandBuffer ||
      !(*m_commandBuffer) || !m_rttSaveImage || !m_rttSaveImageInitialized ||
      !m_headlessImages[0]) {
    return;
  }

  GLIDE_PROFILE_SCOPE("Vulkan::EndGpuTextureBufferMode");
  FlushBatch();
  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }
  m_lfbStaging.ClearOverlay(1);
  m_lfbStaging.ClearBufferDirty(1);

  const vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0,
                                             1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferDstOptimal, colorRange);

  vk::ImageCopy copyRegion;
  copyRegion.srcSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.dstSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.extent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
  m_commandBuffer->copyImage(
      m_rttSaveImage.get(), vk::ImageLayout::eTransferSrcOptimal,
      m_headlessImages[0].get(), vk::ImageLayout::eTransferDstOptimal, 1,
      &copyRegion);

  TransitionImageLayout(*m_commandBuffer, m_headlessImages[0].get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);
  m_clearColorPending[0] = false;
  if (m_msaaSamples > 1) {
    m_singleSampleColorDirty[0] = true;
    m_msaaColorDirty[0] = false;
  }
}

}  // namespace GlideWrapper
