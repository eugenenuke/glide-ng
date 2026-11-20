#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"

#include "hud_frag.spv.h"
#include "hud_vert.spv.h"

namespace GlideWrapper {

void* VulkanBackend::GetLfbWritePtr() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return nullptr;
  return m_lfbStaging.GetWritePtr(m_activeRenderBuffer);
}

bool VulkanBackend::InitLfbOverlayPipeline_Locked() {
  if (!m_device || !m_renderPass) {
    return false;
  }
  if (m_lfbOverlayPipeline) {
    return true;
  }

  try {
    if (!m_lfbOverlaySampler) {
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eNearest;
      samplerInfo.minFilter = vk::Filter::eNearest;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      m_lfbOverlaySampler = m_device->createSamplerUnique(samplerInfo);
    }

    if (!m_lfbOverlayDescSetLayout) {
      vk::DescriptorSetLayoutBinding binding{};
      binding.binding = 0;
      binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      binding.descriptorCount = 1;
      binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

      vk::DescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.bindingCount = 1;
      layoutInfo.pBindings = &binding;
      m_lfbOverlayDescSetLayout =
          m_device->createDescriptorSetLayoutUnique(layoutInfo);
    }

    if (!m_lfbOverlayDescPool) {
      vk::DescriptorPoolSize poolSize(
          vk::DescriptorType::eCombinedImageSampler, 1);
      vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      m_lfbOverlayDescPool = m_device->createDescriptorPoolUnique(poolInfo);

      vk::DescriptorSetLayout setLayout = m_lfbOverlayDescSetLayout.get();
      vk::DescriptorSetAllocateInfo allocInfo{};
      allocInfo.descriptorPool = m_lfbOverlayDescPool.get();
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &setLayout;
      auto sets = m_device->allocateDescriptorSets(allocInfo);
      m_lfbOverlayDescSet = sets[0];
    }

    if (!m_lfbOverlayPipelineLayout) {
      vk::PushConstantRange pushRange{};
      pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
      pushRange.offset = 0;
      pushRange.size = sizeof(float) * 4;

      vk::DescriptorSetLayout setLayout = m_lfbOverlayDescSetLayout.get();
      vk::PipelineLayoutCreateInfo plInfo{};
      plInfo.setLayoutCount = 1;
      plInfo.pSetLayouts = &setLayout;
      plInfo.pushConstantRangeCount = 1;
      plInfo.pPushConstantRanges = &pushRange;
      m_lfbOverlayPipelineLayout =
          m_device->createPipelineLayoutUnique(plInfo);
    }

    vk::UniqueShaderModule vertMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, hud_vert_spv_size, hud_vert_spv));
    vk::UniqueShaderModule fragMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, hud_frag_spv_size, hud_frag_spv));

    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,
                                          vertMod.get(), "main"),
        vk::PipelineShaderStageCreateInfo(
            {}, vk::ShaderStageFlagBits::eFragment, fragMod.get(), "main"),
    };

    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo inputAsm(
        {}, vk::PrimitiveTopology::eTriangleStrip, VK_FALSE);

    vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1,
                                                      nullptr);
    std::array<vk::DynamicState, 2> dynStates = {vk::DynamicState::eViewport,
                                                 vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState(
        {}, static_cast<uint32_t>(dynStates.size()), dynStates.data());

    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.polygonMode = vk::PolygonMode::eFill;
    raster.cullMode = vk::CullModeFlagBits::eNone;
    raster.frontFace = vk::FrontFace::eClockwise;
    raster.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisample{};
    // LFB overlays always composite onto the 1x single-sample color target.
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = vk::CompareOp::eAlways;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_TRUE;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;
    blendAtt.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAtt;

    vk::GraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipeInfo.pStages = stages.data();
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAsm;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_lfbOverlayPipelineLayout.get();
    pipeInfo.renderPass = (m_msaaSamples > 1 && m_singleSampleRenderPass)
                              ? m_singleSampleRenderPass.get()
                              : m_renderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create Vulkan LFB overlay graphics pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    m_lfbOverlayPipeline = std::move(res.value);
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan LFB overlay pipeline: " << e.what());
    return false;
  }
}

bool VulkanBackend::EnsureLfbOverlayResources_Locked(
    uint32_t width, uint32_t height, vk::DeviceSize requiredBytes) {
  if (!m_device || width == 0 || height == 0) return false;
  if (!InitLfbOverlayPipeline_Locked()) return false;

  const uint32_t slot = m_currentFrameSlot;
  const vk::DeviceSize fullFrameBytes =
      static_cast<vk::DeviceSize>(width) * height * 4;
  const vk::DeviceSize desiredSlotBytes =
      std::max(requiredBytes, fullFrameBytes * 4);

  try {
    if (!m_lfbOverlayStagingBuffers[slot] ||
        m_lfbOverlayStagingSizes[slot] < requiredBytes) {
      if (m_lfbOverlayStagingMaps[slot] && m_lfbOverlayStagingMemories[slot]) {
        m_device->unmapMemory(m_lfbOverlayStagingMemories[slot].get());
        m_lfbOverlayStagingMaps[slot] = nullptr;
      }
      m_lfbOverlayStagingBuffers[slot].reset();
      m_lfbOverlayStagingMemories[slot].reset();
      m_lfbOverlayStagingSizes[slot] = 0;
      m_lfbOverlayStagingOffsets[slot] = 0;

      vk::BufferCreateInfo bufInfo({}, desiredSlotBytes,
                                   vk::BufferUsageFlagBits::eTransferSrc,
                                   vk::SharingMode::eExclusive);
      m_lfbOverlayStagingBuffers[slot] = m_device->createBufferUnique(bufInfo);

      vk::MemoryRequirements memReq = m_device->getBufferMemoryRequirements(
          m_lfbOverlayStagingBuffers[slot].get());
      vk::MemoryAllocateInfo allocInfo(
          memReq.size,
          FindMemoryType(memReq.memoryTypeBits,
                         vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent));
      m_lfbOverlayStagingMemories[slot] =
          m_device->allocateMemoryUnique(allocInfo);
      m_device->bindBufferMemory(m_lfbOverlayStagingBuffers[slot].get(),
                                 m_lfbOverlayStagingMemories[slot].get(), 0);
      m_lfbOverlayStagingMaps[slot] = m_device->mapMemory(
          m_lfbOverlayStagingMemories[slot].get(), 0, desiredSlotBytes);
      m_lfbOverlayStagingSizes[slot] = desiredSlotBytes;
    }

    if (m_lfbOverlayImage && m_lfbOverlayTexW == width &&
        m_lfbOverlayTexH == height) {
      return true;
    }

    if (m_lfbOverlayImage) {
      m_device->waitIdle();
      m_lfbOverlayImageView.reset();
      m_lfbOverlayImage.reset();
      m_lfbOverlayImageMemory.reset();
    }

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType = vk::ImageType::e2D;
    // CpuLfbStaging stores canonical 0xAARRGGBB (little-endian bytes B,G,R,A),
    // which matches eB8G8R8A8Unorm directly with zero CPU swizzle!
    imgInfo.format = vk::Format::eB8G8R8A8Unorm;
    imgInfo.extent = vk::Extent3D(width, height, 1);
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = vk::SampleCountFlagBits::e1;
    imgInfo.tiling = vk::ImageTiling::eOptimal;
    imgInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled;
    imgInfo.sharingMode = vk::SharingMode::eExclusive;
    imgInfo.initialLayout = vk::ImageLayout::eUndefined;
    m_lfbOverlayImage = m_device->createImageUnique(imgInfo);

    vk::MemoryRequirements imgReq =
        m_device->getImageMemoryRequirements(m_lfbOverlayImage.get());
    vk::MemoryAllocateInfo imgAlloc(
        imgReq.size, FindMemoryType(imgReq.memoryTypeBits,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_lfbOverlayImageMemory = m_device->allocateMemoryUnique(imgAlloc);
    m_device->bindImageMemory(m_lfbOverlayImage.get(),
                              m_lfbOverlayImageMemory.get(), 0);

    vk::ImageViewCreateInfo viewInfo(
        {}, m_lfbOverlayImage.get(), vk::ImageViewType::e2D,
        vk::Format::eB8G8R8A8Unorm, {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_lfbOverlayImageView = m_device->createImageViewUnique(viewInfo);

    vk::DescriptorImageInfo descImg(m_lfbOverlaySampler.get(),
                                    m_lfbOverlayImageView.get(),
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet write{};
    write.dstSet = m_lfbOverlayDescSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &descImg;
    m_device->updateDescriptorSets(1, &write, 0, nullptr);

    m_lfbOverlayTexW = width;
    m_lfbOverlayTexH = height;
    m_lfbOverlayImageLayout = vk::ImageLayout::eUndefined;
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to allocate Vulkan LFB overlay resources: " << e.what());
    return false;
  }
}

void VulkanBackend::DestroyLfbOverlayResources_Locked() {
  for (int i = 0; i < 2; ++i) {
    if (m_lfbOverlayStagingMaps[i] && m_device &&
        m_lfbOverlayStagingMemories[i]) {
      m_device->unmapMemory(m_lfbOverlayStagingMemories[i].get());
      m_lfbOverlayStagingMaps[i] = nullptr;
    }
    m_lfbOverlayStagingBuffers[i].reset();
    m_lfbOverlayStagingMemories[i].reset();
    m_lfbOverlayStagingSizes[i] = 0;
    m_lfbOverlayStagingOffsets[i] = 0;
  }
  m_lfbOverlayImageView.reset();
  m_lfbOverlayImage.reset();
  m_lfbOverlayImageMemory.reset();
  m_lfbOverlayImageLayout = vk::ImageLayout::eUndefined;
  m_lfbOverlayTexW = 0;
  m_lfbOverlayTexH = 0;

  m_lfbOverlayPipeline.reset();
  m_lfbOverlayPipelineLayout.reset();
  m_lfbOverlayDescSet = vk::DescriptorSet{};
  m_lfbOverlayDescPool.reset();
  m_lfbOverlayDescSetLayout.reset();
  m_lfbOverlaySampler.reset();
}

void VulkanBackend::ReadbackGpuToStaging_Locked(uint32_t buffer) {
  // Ensure any pending 3D draws or LFB writes have landed on the GPU image.
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }
  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }

  const uint32_t readIdx = (buffer == 0) ? 1u : 0u;

  // Execute any pending clears on the target FBO first.
  if (!m_inRenderPass &&
      (m_clearColorPending[readIdx] || m_clearDepthPending[readIdx] ||
       m_clearDepthPendingSingle[readIdx])) {
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

  // Resolve any pending MSAA draws into m_headlessImages[readIdx] before readback.
  ResolveMsaaToSingle_Locked(readIdx);

  vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, range);

  vk::BufferImageCopy region;
  region.imageSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
  m_commandBuffer->copyImageToBuffer(
      m_headlessImages[readIdx].get(), vk::ImageLayout::eTransferSrcOptimal,
      m_headlessPixelBuffers[m_currentFrameSlot].get(), 1, &region);

  SubmitMidFrameAndWaitIdle_Locked(false);

  void* readDst = m_lfbStaging.PrepareBufferForGpuReadback(buffer);
  if (m_gpuStagingMaps[m_currentFrameSlot] && readDst) {
    std::memcpy(readDst, m_gpuStagingMaps[m_currentFrameSlot],
                static_cast<size_t>(m_headlessWidth) * m_headlessHeight * 4);
  }

  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, range);
}

bool VulkanBackend::WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                             uint32_t srcWidth, uint32_t srcHeight,
                             int32_t srcStride, uint32_t srcFmt,
                             const void* srcData, const void* refData,
                             const LfbPipelineConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  // Fast zero-readback path: composite sparse alpha-masked overlay quad on GPU
  // inside the active command buffer (zero waitIdle(), preserves 3D scene!).
  // Internal lossless RGBA8888 restore (srcFmt == 0x100u) uses full-buffer copy
  // to preserve all 4 channels (including alpha) verbatim.
  if (srcFmt != 0x100u && CpuLfbStaging::CanUseOverlayQuad(config)) {
    return m_lfbStaging.WriteOverlayLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                                        srcStride, srcFmt, srcData, refData,
                                        config, m_pixelFormatOverride);
  }

  // Fallback for exotic LFB pixel-pipeline blend modes: read back the current
  // GPU color target so CPU blending has authentic destination pixels.
  ReadbackGpuToStaging_Locked(buffer);
  return m_lfbStaging.WriteLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                               srcStride, srcFmt, srcData, refData, config,
                               m_pixelFormatOverride);
}

bool VulkanBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                            uint32_t srcWidth, uint32_t srcHeight,
                            uint32_t dstStride, void* dstData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  ReadbackGpuToStaging_Locked(buffer);

  if (!m_lfbStaging.ReadRegion(buffer, srcX, srcY, srcWidth, srcHeight,
                               dstStride, dstData)) {
    return false;
  }

  // Swizzle 32-bit client output from canonical ARGB -> active GrColorFormat_t
  // (0=ARGB, 1=ABGR, 2=RGBA, 3=BGRA).
  if (m_pixelFormatOverride != 0 && dstData && srcWidth > 0 &&
      dstStride >= srcWidth * 4) {
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);
    for (uint32_t y = 0; y < srcHeight; ++y) {
      auto* row32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
      for (uint32_t x = 0; x < srcWidth; ++x) {
        row32[x] = CanonicalArgbToGrColor(row32[x], m_pixelFormatOverride);
      }
    }
  }
  return true;
}

void VulkanBackend::CompositeLfbOverlay_Locked(
    uint32_t /*buffer*/, uint32_t targetGpuIdx, const uint32_t* pixels,
    uint32_t width, uint32_t height, const CpuLfbStaging::DirtyBand* bands,
    size_t bandCount) {
  if (!pixels || !bands || bandCount == 0 || width == 0 || height == 0) {
    return;
  }

  vk::DeviceSize totalBytes = 0;
  size_t validCount = 0;
  CpuLfbStaging::DirtyBand validBands[CpuLfbStaging::kMaxOverlayBands]{};
  for (size_t i = 0; i < bandCount && validCount < CpuLfbStaging::kMaxOverlayBands;
       ++i) {
    if (bands[i].minY <= bands[i].maxY && bands[i].maxY < height) {
      validBands[validCount++] = bands[i];
      const uint32_t rows = bands[i].maxY - bands[i].minY + 1;
      totalBytes += static_cast<vk::DeviceSize>(width) * rows * 4;
    }
  }
  if (validCount == 0 || totalBytes == 0) return;

  if (!EnsureLfbOverlayResources_Locked(width, height, totalBytes)) {
    return;
  }

  const uint32_t slot = m_currentFrameSlot;
  if (m_lfbOverlayStagingOffsets[slot] + totalBytes >
      m_lfbOverlayStagingSizes[slot]) {
    // Rare single-frame staging overflow (>4 fullscreen LFB flushes in 1
    // frame): flush current command buffer and reset slot staging offset.
    SubmitMidFrameAndWaitIdle_Locked(true);
    m_lfbOverlayStagingOffsets[slot] = 0;
  }

  vk::BufferImageCopy copyRegions[CpuLfbStaging::kMaxOverlayBands]{};
  uint8_t* stagingBase =
      static_cast<uint8_t*>(m_lfbOverlayStagingMaps[slot]);
  for (size_t i = 0; i < validCount; ++i) {
    const uint32_t minY = validBands[i].minY;
    const uint32_t numRows = validBands[i].maxY - minY + 1;
    const vk::DeviceSize bandBytes =
        static_cast<vk::DeviceSize>(width) * numRows * 4;
    const vk::DeviceSize stagingOffset = m_lfbOverlayStagingOffsets[slot];

    std::memcpy(stagingBase + stagingOffset,
                pixels + static_cast<size_t>(minY) * width,
                static_cast<size_t>(bandBytes));
    m_lfbOverlayStagingOffsets[slot] += bandBytes;

    vk::BufferImageCopy& region = copyRegions[i];
    region.bufferOffset = stagingOffset;
    region.imageSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageOffset = vk::Offset3D(0, static_cast<int32_t>(minY), 0);
    region.imageExtent = vk::Extent3D(width, numRows, 1);
  }

  vk::ImageSubresourceRange imgRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                     1);

  // Transition overlay texture to TransferDstOptimal (serializing after any
  // earlier fragment-shader sample of the overlay image in this frame).
  vk::ImageMemoryBarrier toTransfer{};
  toTransfer.oldLayout = m_lfbOverlayImageLayout;
  toTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = m_lfbOverlayImage.get();
  toTransfer.subresourceRange = imgRange;
  vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
  if (m_lfbOverlayImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
    toTransfer.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    srcStage = vk::PipelineStageFlagBits::eFragmentShader;
  } else {
    toTransfer.srcAccessMask = {};
  }
  toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  m_commandBuffer->pipelineBarrier(srcStage,
                                   vk::PipelineStageFlagBits::eTransfer, {}, 0,
                                   nullptr, 0, nullptr, 1, &toTransfer);

  m_commandBuffer->copyBufferToImage(
      m_lfbOverlayStagingBuffers[slot].get(), m_lfbOverlayImage.get(),
      vk::ImageLayout::eTransferDstOptimal, static_cast<uint32_t>(validCount),
      copyRegions);

  // Transition overlay texture to ShaderReadOnlyOptimal for fragment sampling.
  vk::ImageMemoryBarrier toSample{};
  toSample.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  toSample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toSample.image = m_lfbOverlayImage.get();
  toSample.subresourceRange = imgRange;
  toSample.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  toSample.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  m_commandBuffer->pipelineBarrier(
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1,
      &toSample);
  m_lfbOverlayImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  // If MSAA has newer 3D content and no full color clear supersedes it, resolve
  // MSAA -> 1x before compositing the LFB overlay onto the 1x target.
  if (m_msaaSamples > 1 && m_msaaColorDirty[targetGpuIdx] &&
      !m_clearColorPending[targetGpuIdx]) {
    ResolveMsaaToSingle_Locked(targetGpuIdx);
  }

  // Composite all dirty row bands onto the 1x color target inside a single
  // RenderPass (zero waitIdle(), preserves both 1x and MSAA 3D scenes!).
  const vk::RenderPass overlayRp =
      (m_msaaSamples > 1 && m_singleSampleRenderPass)
          ? m_singleSampleRenderPass.get()
          : m_renderPass.get();
  const vk::Framebuffer overlayFb =
      (m_msaaSamples > 1 && m_singleSampleFramebuffers[targetGpuIdx])
          ? m_singleSampleFramebuffers[targetGpuIdx].get()
          : m_headlessFramebuffers[targetGpuIdx].get();
  vk::RenderPassBeginInfo rpBegin(
      overlayRp, overlayFb,
      vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, nullptr);
  m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);

  // If the target framebuffer still has pending clears, apply them BEFORE
  // compositing the overlay so the overlay lands on top of the cleared color.
  bool& singleDepthPending = (m_msaaSamples > 1)
                                 ? m_clearDepthPendingSingle[targetGpuIdx]
                                 : m_clearDepthPending[targetGpuIdx];
  if (m_clearColorPending[targetGpuIdx] || singleDepthPending) {
    std::vector<vk::ClearAttachment> clears;
    if (m_clearColorPending[targetGpuIdx]) {
      clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0,
                                           m_clearColorValue[targetGpuIdx]));
      m_clearColorPending[targetGpuIdx] = false;
    }
    if (singleDepthPending) {
      clears.push_back(vk::ClearAttachment(
          vk::ImageAspectFlagBits::eDepth, 0,
          vk::ClearValue(
              vk::ClearDepthStencilValue(m_clearDepthValue[targetGpuIdx], 0))));
      singleDepthPending = false;
    }
    if (!clears.empty()) {
      vk::ClearRect clearRect(
          vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
      m_commandBuffer->clearAttachments(static_cast<uint32_t>(clears.size()),
                                        clears.data(), 1, &clearRect);
    }
  }

  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_lfbOverlayPipeline.get());
  m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      m_lfbOverlayPipelineLayout.get(), 0, 1,
                                      &m_lfbOverlayDescSet, 0, nullptr);

  vk::Viewport vp(0.0f, 0.0f, static_cast<float>(m_headlessWidth),
                  static_cast<float>(m_headlessHeight), 0.0f, 1.0f);
  m_commandBuffer->setViewport(0, 1, &vp);

  const float rectNdc[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
  m_commandBuffer->pushConstants(m_lfbOverlayPipelineLayout.get(),
                                 vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(rectNdc), rectNdc);

  const uint32_t scW = std::min(width, m_headlessWidth);
  for (size_t i = 0; i < validCount; ++i) {
    const uint32_t scY = std::min(validBands[i].minY, m_headlessHeight);
    const uint32_t numRows = validBands[i].maxY - validBands[i].minY + 1;
    const uint32_t scH = std::min(numRows, m_headlessHeight - scY);
    if (scW == 0 || scH == 0) continue;
    vk::Rect2D sc({0, static_cast<int32_t>(scY)}, {scW, scH});
    m_commandBuffer->setScissor(0, 1, &sc);
    m_commandBuffer->draw(4, 1, 0, 0);
  }
  m_commandBuffer->endRenderPass();

  if (m_msaaSamples > 1) {
    m_singleSampleColorDirty[targetGpuIdx] = true;
    m_msaaColorDirty[targetGpuIdx] = false;
  }
}

void VulkanBackend::FlushLFBToGPU_Locked() {
  if (!m_lfbStaging.IsDirty()) return;
  GLIDE_CPU_SCOPE(CpuBucket::Lfb);

  if (!m_commandBuffer || !(*m_commandBuffer)) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "FlushLFBToGPU: command buffer is NULL!");
    m_lfbStaging.ClearDirtyFlags();
    return;
  }

  // Terminate any active render pass so transfers / overlay passes can run.
  if (m_inRenderPass) {
    FlushBatch();
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  // Flush BOTH FRONT (0) and BACK (1) if dirty.
  for (uint32_t buf = 0; buf < 2; ++buf) {
    const uint32_t targetGpuIdx = (buf == 0) ? 1u : 0u;

    // 1. Full CPU buffer upload path (direct GetLfbWritePtr or exotic blend).
    if (m_lfbStaging.IsFullBufferDirty(buf)) {
      const void* srcCpuData = m_lfbStaging.PrepareBufferForGpuUpload(buf);
      if (srcCpuData) {
        if (m_clearColorPending[targetGpuIdx] ||
            m_clearDepthPending[targetGpuIdx]) {
          uint32_t origActive = m_activeRenderBuffer;
          m_activeRenderBuffer = buf;
          EnsureRenderPassActive();
          m_commandBuffer->endRenderPass();
          m_inRenderPass = false;
          m_activeRenderBuffer = origActive;
        }

        if (m_gpuStagingMaps[m_currentFrameSlot]) {
          std::memcpy(m_gpuStagingMaps[m_currentFrameSlot], srcCpuData,
                      static_cast<size_t>(m_headlessWidth) * m_headlessHeight *
                          4);
        }

        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1,
                                        0, 1);
        TransitionImageLayout(*m_commandBuffer,
                              m_headlessImages[targetGpuIdx].get(),
                              vk::ImageLayout::eColorAttachmentOptimal,
                              vk::ImageLayout::eTransferDstOptimal, range);

        vk::BufferImageCopy region;
        region.imageSubresource =
            vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);

        vk::MappedMemoryRange memRange(
            m_headlessPixelMemories[m_currentFrameSlot].get(), 0,
            VK_WHOLE_SIZE);
        m_device->flushMappedMemoryRanges(memRange);

        m_commandBuffer->copyBufferToImage(
            m_headlessPixelBuffers[m_currentFrameSlot].get(),
            m_headlessImages[targetGpuIdx].get(),
            vk::ImageLayout::eTransferDstOptimal, 1, &region);

        TransitionImageLayout(*m_commandBuffer,
                              m_headlessImages[targetGpuIdx].get(),
                              vk::ImageLayout::eTransferDstOptimal,
                              vk::ImageLayout::eColorAttachmentOptimal, range);

        if (m_msaaSamples > 1) {
          m_singleSampleColorDirty[targetGpuIdx] = true;
          m_msaaColorDirty[targetGpuIdx] = false;
        }
        m_clearColorPending[targetGpuIdx] = false;

        // Full-buffer path shares m_headlessPixelBuffers[slot], so synchronize
        // before returning to avoid clobbering on subsequent reads/uploads.
        SubmitMidFrameAndWaitIdle_Locked(true);
      }
      m_lfbStaging.ClearBufferDirty(buf);
    }

    // 2. Fast sparse alpha-masked overlay quad compositing (zero waitIdle()).
    if (m_lfbStaging.IsOverlayDirty(buf)) {
      const CpuLfbStaging::DirtyBand* bands = nullptr;
      size_t bandCount = 0;
      const uint32_t* overlayPixels =
          m_lfbStaging.GetOverlayBandsForGpuUpload(buf, bands, bandCount);
      if (overlayPixels && bands && bandCount > 0) {
        CompositeLfbOverlay_Locked(buf, targetGpuIdx, overlayPixels,
                                   m_lfbStaging.GetWidth(),
                                   m_lfbStaging.GetHeight(), bands, bandCount);
      }
      m_lfbStaging.ClearOverlay(buf);
    }
  }
}

}  // namespace GlideWrapper
