#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"

#include "hud_vert.spv.h"
#include "lfb_depth_frag.spv.h"
#include "lfb_overlay_frag.spv.h"
#include "msaa_depth_resolve_frag.spv.h"

namespace GlideWrapper {

void* VulkanBackend::GetLfbWritePtr() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return nullptr;
  return m_lfbStaging.GetWritePtr(m_activeRenderBuffer);
}

bool VulkanBackend::InitLfbOverlayPipeline_Locked(
    CpuLfbStaging::OverlayBlendMode mode) {
  if (!m_device || !m_renderPass) {
    return false;
  }
  const size_t modeIdx = static_cast<size_t>(mode);
  if (modeIdx >= m_lfbOverlayPipelines.size()) {
    return false;
  }
  if (m_lfbOverlayPipelines[modeIdx]) {
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
        vk::ShaderModuleCreateInfo({}, lfb_overlay_frag_spv_size,
                                   lfb_overlay_frag_spv));

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
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;
    switch (mode) {
      case CpuLfbStaging::OverlayBlendMode::Additive:
        blendAtt.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstColorBlendFactor = vk::BlendFactor::eOne;
        blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        break;
      case CpuLfbStaging::OverlayBlendMode::Multiply:
        blendAtt.srcColorBlendFactor = vk::BlendFactor::eZero;
        blendAtt.dstColorBlendFactor = vk::BlendFactor::eSrcColor;
        blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        break;
      case CpuLfbStaging::OverlayBlendMode::Screen:
        blendAtt.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor;
        blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        break;
      case CpuLfbStaging::OverlayBlendMode::AlphaOver:
      default:
        blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        break;
    }
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
    m_lfbOverlayPipelines[modeIdx] = std::move(res.value);
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan LFB overlay pipeline: " << e.what());
    return false;
  }
}

bool VulkanBackend::EnsureLfbOverlayResources_Locked(
    uint32_t width, uint32_t height, vk::DeviceSize requiredBytes,
    CpuLfbStaging::OverlayBlendMode mode) {
  if (!m_device || width == 0 || height == 0) return false;
  if (!InitLfbOverlayPipeline_Locked(mode)) return false;

  const uint32_t slot = m_currentFrameSlot;
  const vk::DeviceSize fullFrameBytes =
      static_cast<vk::DeviceSize>(width) * height * 4;
  const vk::DeviceSize desiredSlotBytes =
      std::max(requiredBytes, fullFrameBytes * 8);

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

  m_msaaDepthResolvePipeline.reset();
  m_msaaDepthResolveDescSet = vk::DescriptorSet{};
  m_msaaDepthResolveDescPool.reset();
  m_lfbDepthPipeline.reset();
  m_lfbDepthPipelineMsaa.reset();
  for (auto& pipe : m_lfbOverlayPipelines) {
    pipe.reset();
  }
  m_lfbOverlayPipelineLayout.reset();
  m_lfbOverlayDescSet = vk::DescriptorSet{};
  m_lfbOverlayDescPool.reset();
  m_lfbOverlayDescSetLayout.reset();
  m_lfbOverlaySampler.reset();
  m_auxDepthScratch.clear();
  m_auxDepthScratch.shrink_to_fit();
  m_auxDepthShadow16.clear();
  m_auxDepthShadow16.shrink_to_fit();
  m_auxDepthShadowValid = false;
  m_msaaDepthDirty = false;
  m_lfbReadCacheValid[0] = false;
  m_lfbReadCacheValid[1] = false;
  m_frontBufferNeedsPresent = false;
}

bool VulkanBackend::InitLfbDepthPipeline_Locked(bool msaa) {
  if (!m_device || !m_renderPass) return false;
  if (!InitLfbOverlayPipeline_Locked()) return false;
  vk::UniquePipeline& target = msaa ? m_lfbDepthPipelineMsaa : m_lfbDepthPipeline;
  if (target) return true;

  try {
    vk::UniqueShaderModule vertMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, hud_vert_spv_size, hud_vert_spv));
    vk::UniqueShaderModule fragMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, lfb_depth_frag_spv_size,
                                   lfb_depth_frag_spv));

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
    multisample.rasterizationSamples =
        msaa ? static_cast<vk::SampleCountFlagBits>(m_msaaSamples)
             : vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eAlways;

    // Color writes completely disabled — depth-only LFB AUXBUFFER upload!
    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_FALSE;
    blendAtt.colorWriteMask = vk::ColorComponentFlags{};

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
    pipeInfo.renderPass =
        (msaa || m_msaaSamples <= 1 || !m_singleSampleRenderPass)
            ? m_renderPass.get()
            : m_singleSampleRenderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create Vulkan LFB depth pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    target = std::move(res.value);
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan LFB depth pipeline: " << e.what());
    return false;
  }
}

bool VulkanBackend::WriteAuxDepthLFB_Locked(
    uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight,
    int32_t srcStride, const void* srcData, const void* refData) {
  if (!srcData || m_headlessWidth == 0 || m_headlessHeight == 0) return false;
  if (dstX >= m_headlessWidth || dstY >= m_headlessHeight || srcWidth == 0 ||
      srcHeight == 0) {
    return true;
  }
  const uint32_t copyW = std::min(srcWidth, m_headlessWidth - dstX);
  const uint32_t copyH = std::min(srcHeight, m_headlessHeight - dstY);

  // Flush any pending 3D draws so the AUXBUFFER write lands AFTER them in API order.
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }
  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  const size_t totalPx =
      static_cast<size_t>(m_headlessWidth) * m_headlessHeight;
  if (m_auxDepthShadow16.size() != totalPx) {
    m_auxDepthShadow16.assign(totalPx, 0xFFFFu);
  }
  const size_t rectRowsBytes =
      static_cast<size_t>(m_headlessWidth) * copyH * sizeof(uint32_t);
  if (m_auxDepthScratch.size() < static_cast<size_t>(m_headlessWidth) * copyH) {
    m_auxDepthScratch.resize(static_cast<size_t>(m_headlessWidth) * copyH);
  }
  std::memset(m_auxDepthScratch.data(), 0, rectRowsBytes);

  const auto* inBase = reinterpret_cast<const uint8_t*>(srcData);
  const auto* refBase = reinterpret_cast<const uint8_t*>(refData);
  uint32_t modifiedCount = 0;
  uint32_t zeroCount = 0;
  for (uint32_t y = 0; y < copyH; ++y) {
    const auto* rowIn = reinterpret_cast<const uint16_t*>(
        inBase + static_cast<std::ptrdiff_t>(y) * srcStride);
    const auto* rowRef =
        refBase ? reinterpret_cast<const uint16_t*>(
                      refBase + static_cast<std::ptrdiff_t>(y) * srcStride)
                : nullptr;
    uint32_t* dstRow =
        m_auxDepthScratch.data() + static_cast<size_t>(y) * m_headlessWidth;
    uint16_t* shadowRow =
        m_auxDepthShadow16.data() + static_cast<size_t>(dstY + y) * m_headlessWidth;
    for (uint32_t x = 0; x < copyW; ++x) {
      const uint16_t d16 = rowIn[x];
      if (rowRef && d16 == rowRef[x]) continue;
      shadowRow[dstX + x] = d16;
      const float dNorm =
          DecodeAuxDepth16ToNorm(d16, m_depthMode, m_depthNear, m_depthFar);
      const uint32_t u24 = std::min(
          kDepth24MaxU32, static_cast<uint32_t>(dNorm * kDepth24MaxF + 0.5f));
      // B8G8R8A8_UNORM little-endian 0xAARRGGBB: R=[23:16], G=[15:8], B=[7:0]
      dstRow[dstX + x] = 0xFF000000u | (u24 & 0x00FFFFFFu);
      ++modifiedCount;
      if (d16 == 0u) ++zeroCount;
    }
  }
  if (modifiedCount == 0) {
    return true;
  }
  const bool coversFullUnmasked =
      (dstX == 0u && dstY == 0u && copyW == m_headlessWidth &&
       copyH == m_headlessHeight && refData == nullptr);
  if (coversFullUnmasked) {
    m_auxDepthShadowValid = true;
    m_msaaDepthDirty = false;
  }
  // If m_auxDepthShadowValid was already true, every modified pixel was updated
  // in m_auxDepthShadow16 above so it remains valid. If it was false and only a
  // sparse sub-rect changed, keep it false so the next ReadLFB(AUXBUFFER) pulls
  // the complete combined depth attachment instead of stale background fill.

  if (!EnsureLfbOverlayResources_Locked(m_headlessWidth, m_headlessHeight,
                                        rectRowsBytes)) {
    return false;
  }
  if (!InitLfbDepthPipeline_Locked(/*msaa=*/false)) return false;
  if (m_msaaSamples > 1 && !InitLfbDepthPipeline_Locked(/*msaa=*/true)) {
    return false;
  }

  const uint32_t slot = m_currentFrameSlot;
  if (m_lfbOverlayStagingOffsets[slot] + rectRowsBytes >
      m_lfbOverlayStagingSizes[slot]) {
    SubmitMidFrameAndWaitIdle_Locked(true);
    m_lfbOverlayStagingOffsets[slot] = 0;
  }

  const vk::DeviceSize stagingOffset = m_lfbOverlayStagingOffsets[slot];
  std::memcpy(
      static_cast<uint8_t*>(m_lfbOverlayStagingMaps[slot]) + stagingOffset,
      m_auxDepthScratch.data(), rectRowsBytes);
  m_lfbOverlayStagingOffsets[slot] += rectRowsBytes;

  vk::ImageSubresourceRange imgRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                     1);
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
  }
  toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  m_commandBuffer->pipelineBarrier(srcStage,
                                   vk::PipelineStageFlagBits::eTransfer, {}, 0,
                                   nullptr, 0, nullptr, 1, &toTransfer);

  vk::BufferImageCopy copyRegion{};
  copyRegion.bufferOffset = stagingOffset;
  copyRegion.imageSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  copyRegion.imageOffset = vk::Offset3D(0, static_cast<int32_t>(dstY), 0);
  copyRegion.imageExtent = vk::Extent3D(m_headlessWidth, copyH, 1);
  m_commandBuffer->copyBufferToImage(
      m_lfbOverlayStagingBuffers[slot].get(), m_lfbOverlayImage.get(),
      vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

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

  auto emitDepthPass = [&](vk::RenderPass rp, vk::Framebuffer fb,
                           vk::Pipeline pipe, bool isSingle) {
    vk::RenderPassBeginInfo rpBegin(
        rp, fb, vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0,
        nullptr);
    m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);

    // Materialize any pending depth clear BEFORE stamping the ZA16 values so
    // an older lazy depth clear cannot later erase our cockpit/stencil mask!
    for (uint32_t i = 0; i < 2; ++i) {
      bool& dp =
          isSingle ? m_clearDepthPendingSingle[i] : m_clearDepthPending[i];
      if (dp) {
        vk::ClearAttachment ca(
            vk::ImageAspectFlagBits::eDepth, 0,
            vk::ClearValue(vk::ClearDepthStencilValue(m_clearDepthValue[i], 0)));
        vk::ClearRect cr(
            vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
        m_commandBuffer->clearAttachments(1, &ca, 1, &cr);
        dp = false;
      }
    }

    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
    m_commandBuffer->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, m_lfbOverlayPipelineLayout.get(), 0,
        1, &m_lfbOverlayDescSet, 0, nullptr);
    vk::Viewport vp(0.0f, 0.0f, static_cast<float>(m_headlessWidth),
                    static_cast<float>(m_headlessHeight), 0.0f, 1.0f);
    m_commandBuffer->setViewport(0, 1, &vp);
    const float rectNdc[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    m_commandBuffer->pushConstants(m_lfbOverlayPipelineLayout.get(),
                                   vk::ShaderStageFlagBits::eVertex, 0,
                                   sizeof(rectNdc), rectNdc);
    vk::Rect2D sc({static_cast<int32_t>(dstX), static_cast<int32_t>(dstY)},
                  {copyW, copyH});
    m_commandBuffer->setScissor(0, 1, &sc);
    m_commandBuffer->draw(4, 1, 0, 0);
    m_commandBuffer->endRenderPass();
  };

  // Write to 1x depth attachment
  const vk::RenderPass rp1x =
      (m_msaaSamples > 1 && m_singleSampleRenderPass)
          ? m_singleSampleRenderPass.get()
          : m_renderPass.get();
  const vk::Framebuffer fb1x =
      (m_msaaSamples > 1 && m_singleSampleFramebuffers[0])
          ? m_singleSampleFramebuffers[0].get()
          : m_headlessFramebuffers[0].get();
  emitDepthPass(rp1x, fb1x, m_lfbDepthPipeline.get(), m_msaaSamples > 1);

  // Under MSAA, also seed the multisampled depth attachment identically across
  // all coverage samples so 3D hardware depth tests clip against the same mask.
  if (m_msaaSamples > 1 && m_lfbDepthPipelineMsaa) {
    emitDepthPass(m_renderPass.get(), m_headlessFramebuffers[0].get(),
                  m_lfbDepthPipelineMsaa.get(), false);
  }

  GLIDE_LOG(DEBUG, "Vulkan",
            "WriteAuxDepthLFB: rect=(" << dstX << "," << dstY << " " << copyW
                                       << "x" << copyH << "), modified="
                                       << modifiedCount << ", zeroMask="
                                       << zeroCount);
  return true;
}

void VulkanBackend::PresentFrontBuffer_Locked() {
  if (!m_swapchain || !m_device || !m_commandBuffer || !(*m_commandBuffer)) {
    return;
  }
  if (m_passthruSuspended) return;

  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }
  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }
  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  // Front buffer physically lives in m_headlessImages[1].
  constexpr uint32_t kFrontIdx = 1u;
  if (m_clearColorPending[kFrontIdx]) {
    const uint32_t origActive = m_activeRenderBuffer;
    m_activeRenderBuffer = 0u;  // GR_BUFFER_FRONTBUFFER
    EnsureRenderPassActive(ShouldUseSingleSamplePass_Locked());
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
    m_activeRenderBuffer = origActive;
  }
  ResolveMsaaToSingle_Locked(kFrontIdx);

  uint32_t imageIdx = 0;
  vk::Result acquireRes = vk::Result::eTimeout;
  try {
    acquireRes = m_device->acquireNextImageKHR(
        m_swapchain.get(), UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrameSlot].get(), {}, &imageIdx);
  } catch (...) {
    return;
  }
  if (acquireRes != vk::Result::eSuccess &&
      acquireRes != vk::Result::eSuboptimalKHR) {
    return;
  }

  vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);
  const PresentRect dst = NativeSurfaceHost::ComputePresentRect(
      m_headlessWidth, m_headlessHeight, m_swapchainExtent.width,
      m_swapchainExtent.height, /*preserveAspect=*/true);

  vk::Image presentSrc = m_headlessImages[kFrontIdx].get();
  TransitionImageLayout(*m_commandBuffer, m_swapchainImages[imageIdx],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eTransferDstOptimal, colorRange);
  TransitionImageLayout(*m_commandBuffer, presentSrc,
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, colorRange);
  if (dst.x > 0 || dst.y > 0 || dst.width < m_swapchainExtent.width ||
      dst.height < m_swapchainExtent.height) {
    vk::ClearColorValue blackClear(std::array<float, 4>{0.f, 0.f, 0.f, 1.f});
    m_commandBuffer->clearColorImage(m_swapchainImages[imageIdx],
                                     vk::ImageLayout::eTransferDstOptimal,
                                     &blackClear, 1, &colorRange);
  }
  if (dst.width > 0 && dst.height > 0) {
    vk::ImageBlit blitRegion;
    blitRegion.srcSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    blitRegion.srcOffsets[0] = vk::Offset3D(0, 0, 0);
    blitRegion.srcOffsets[1] = vk::Offset3D(
        static_cast<int32_t>(m_headlessWidth),
        static_cast<int32_t>(m_headlessHeight), 1);
    blitRegion.dstSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    blitRegion.dstOffsets[0] = vk::Offset3D(dst.x, dst.y, 0);
    blitRegion.dstOffsets[1] =
        vk::Offset3D(dst.x + static_cast<int32_t>(dst.width),
                     dst.y + static_cast<int32_t>(dst.height), 1);
    const vk::Filter presentFilter =
        (m_config.presentationFilter == 1) ? vk::Filter::eLinear
                                           : vk::Filter::eNearest;
    m_commandBuffer->blitImage(
        presentSrc, vk::ImageLayout::eTransferSrcOptimal,
        m_swapchainImages[imageIdx], vk::ImageLayout::eTransferDstOptimal, 1,
        &blitRegion, presentFilter);
  }
  TransitionImageLayout(*m_commandBuffer, presentSrc,
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);
  TransitionImageLayout(*m_commandBuffer, m_swapchainImages[imageIdx],
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::ePresentSrcKHR, colorRange);

  m_commandBuffer->end();

  vk::CommandBuffer submitCmds[2];
  uint32_t submitCmdCount = 0;
  if (m_uploadCmdHasWork[m_currentFrameSlot]) {
    auto& uploadCmd = m_uploadCommandBuffers[m_currentFrameSlot].get();
    uploadCmd.end();
    submitCmds[submitCmdCount++] = uploadCmd;
    m_uploadCmdHasWork[m_currentFrameSlot] = false;
  }
  submitCmds[submitCmdCount++] = *m_commandBuffer;

  vk::PipelineStageFlags waitStages[] = {
      vk::PipelineStageFlagBits::eTransfer |
      vk::PipelineStageFlagBits::eColorAttachmentOutput};
  vk::Semaphore waitSem = m_imageAvailableSemaphores[m_currentFrameSlot].get();
  vk::Semaphore sigSem = m_renderFinishedSemaphores[imageIdx].get();
  (void)m_device->resetFences(1, &m_fences[m_currentFrameSlot].get());
  vk::SubmitInfo submitInfo(1, &waitSem, waitStages, submitCmdCount, submitCmds,
                            1, &sigSem);
  bool submitted = false;
  try {
    m_graphicsQueue.submit(submitInfo, m_fences[m_currentFrameSlot].get());
    submitted = true;
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "PresentFrontBuffer vkQueueSubmit failed: " << e.what());
    Logger::GetInstance().Flush();
    ResetFrameFenceSignaled_Locked(m_currentFrameSlot);
  }
  if (submitted) {
    try {
      vk::SwapchainKHR sc = m_swapchain.get();
      vk::PresentInfoKHR pInfo(1, &sigSem, 1, &sc, &imageIdx, nullptr);
      (void)m_graphicsQueue.presentKHR(pInfo);
    } catch (const std::exception&) {
      // OutOfDate/Suboptimal/SurfaceLostKHR will be handled cleanly on the
      // next SwapBuffers() cycle; still wait on the fence below before
      // resetting the in-flight command buffer.
    }
    try {
      const vk::Result waitRes = m_device->waitForFences(
          1, &m_fences[m_currentFrameSlot].get(), VK_TRUE, UINT64_MAX);
      if (waitRes == vk::Result::eSuccess) {
        // Every draw/upload submitted in m_currentFrameSlot is now retired;
        // recycle vertex, texture-staging, and retired descriptor pools so
        // un-swapped GR_BUFFER_FRONTBUFFER menu loops never overflow slots.
        m_texStagingOffsets[m_currentFrameSlot] =
            m_currentFrameSlot * m_texStagingSlotSize;
        m_vertexBufferOffset = m_currentFrameSlot * kVertexSlotBytes;
        ReclaimRetiredResources_Locked(m_currentFrameSlot);
      }
    } catch (const std::exception&) {
    }
  }

  m_lfbOverlayStagingOffsets[m_currentFrameSlot] = 0;
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
  m_frontBufferNeedsPresent = false;
  m_frontFlushTimer.Clear();
  m_lastFrontPresentTime = std::chrono::steady_clock::now();
}

void VulkanBackend::NoteFrontBufferMutation_Locked(bool forceImmediate) {
  if (!m_swapchain || m_passthruSuspended || !m_windowAttached) {
    return;
  }
  if (forceImmediate) {
    PresentFrontBuffer_Locked();
    return;
  }
  constexpr auto kFrontPresentInterval = std::chrono::milliseconds(33);
  constexpr auto kBurstCoalesceDelay = std::chrono::milliseconds(16);
  const auto now = std::chrono::steady_clock::now();
  const bool hasPrev = (m_lastFrontPresentTime.time_since_epoch().count() != 0);
  if (m_frontBufferNeedsPresent &&
      (!hasPrev || (now - m_lastFrontPresentTime) >= kFrontPresentInterval)) {
    PresentFrontBuffer_Locked();
    return;
  }
  m_frontBufferNeedsPresent = true;
  if (!hasPrev) {
    m_lastFrontPresentTime = now;
  }
  const auto nextDue =
      (hasPrev && (now - m_lastFrontPresentTime) < kFrontPresentInterval)
          ? (m_lastFrontPresentTime + kFrontPresentInterval)
          : (now + kBurstCoalesceDelay);
  m_frontFlushTimer.Arm(nextDue);
}

void VulkanBackend::OnFrontBufferFlushTimer() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached || m_passthruSuspended ||
      !m_frontBufferNeedsPresent || !m_swapchain) {
    return;
  }
  PresentFrontBuffer_Locked();
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

  // GR_BUFFER_AUXBUFFER (2) / GR_LFB_SRC_FMT_ZA16 (0x0f): upload 16-bit depth
  // values to the hardware depth attachment(s), never the color framebuffer!
  if (buffer == kLfbBufferAux || srcFmt == kLfbSrcFmtZa16) {
    return WriteAuxDepthLFB_Locked(dstX, dstY, srcWidth, srcHeight, srcStride,
                                   srcData, refData);
  }

  const uint32_t logicalSlot = (buffer == 0u) ? 0u : 1u;
  bool ok = false;

  // Fast zero-readback path: composite sparse alpha-masked overlay quad on GPU
  // inside the active command buffer (zero waitIdle(), preserves 3D scene!).
  // Internal lossless RGBA8888 restore (srcFmt == 0x100u) uses full-buffer copy
  // to preserve all 4 channels (including alpha) verbatim.
  CpuLfbStaging::OverlayBlendMode overlayMode =
      CpuLfbStaging::OverlayBlendMode::AlphaOver;
  if (srcFmt != 0x100u &&
      CpuLfbStaging::ResolveOverlayBlendMode(config, overlayMode)) {
    if (m_lfbStaging.IsFullBufferDirty(buffer) ||
        (m_lfbStaging.IsOverlayDirty(buffer) &&
         m_lfbStaging.GetOverlayBlendMode(buffer) != overlayMode)) {
      FlushLFBToGPU_Locked();
    }
    ok = m_lfbStaging.WriteOverlayLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                                      srcStride, srcFmt, srcData, refData,
                                      config, m_pixelFormatOverride);
    if (ok && m_lfbReadCacheValid[logicalSlot]) {
      // Keep cached CPU readback coherent with this LFB write so subsequent
      // scanline ReadLFB calls avoid stalling the GPU pipeline!
      m_lfbStaging.MirrorWriteToReadCache(buffer, dstX, dstY, srcWidth,
                                          srcHeight, srcStride, srcFmt, srcData,
                                          refData, config, m_pixelFormatOverride);
    }
  } else {
    // Fallback for exotic LFB pixel-pipeline blend modes: read back the current
    // GPU color target once so CPU blending has authentic destination pixels.
    if (m_batchedVertexCount > 0) {
      FlushBatch();
    }
    if (m_lfbStaging.IsOverlayDirty(buffer)) {
      FlushLFBToGPU_Locked();
    }
    if (!m_lfbReadCacheValid[logicalSlot] &&
        !m_lfbStaging.IsFullBufferDirty(buffer)) {
      ReadbackGpuToStaging_Locked(buffer);
      m_lfbReadCacheValid[logicalSlot] = true;
    }
    ok = m_lfbStaging.WriteLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                               srcStride, srcFmt, srcData, refData, config,
                               m_pixelFormatOverride);
  }

  // On real 3dfx hardware, direct writes to GR_BUFFER_FRONTBUFFER (buffer == 0)
  // appear immediately via the RAMDAC without requiring grBufferSwap(). Present
  // full-frame uploads immediately (video playback), coalesce partial bursts via
  // the trailing-edge flush timer, and stream continuous loops at ~30 Hz.
  if (ok && buffer == 0u && m_swapchain && !m_passthruSuspended) {
    const bool isFullFrame =
        (dstX == 0u && dstY == 0u && srcWidth >= m_headlessWidth &&
         srcHeight >= m_headlessHeight);
    NoteFrontBufferMutation_Locked(/*forceImmediate=*/isFullFrame);
  }
  return ok;
}

bool VulkanBackend::InitMsaaDepthResolvePipeline_Locked() {
  if (!m_device || m_msaaSamples <= 1 || !m_msaaDepthSampleable ||
      !m_msaaDepthSampleView || !m_singleSampleRenderPass) {
    return false;
  }
  if (m_msaaDepthResolvePipeline && m_msaaDepthResolveDescSet) {
    return true;
  }
  if (!InitLfbOverlayPipeline_Locked()) {
    return false;
  }

  try {
    if (!m_msaaDepthResolveDescPool) {
      vk::DescriptorPoolSize poolSize(
          vk::DescriptorType::eCombinedImageSampler, 1);
      vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      m_msaaDepthResolveDescPool = m_device->createDescriptorPoolUnique(poolInfo);

      vk::DescriptorSetLayout setLayout = m_lfbOverlayDescSetLayout.get();
      vk::DescriptorSetAllocateInfo allocInfo{};
      allocInfo.descriptorPool = m_msaaDepthResolveDescPool.get();
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &setLayout;
      auto sets = m_device->allocateDescriptorSets(allocInfo);
      m_msaaDepthResolveDescSet = sets[0];

      vk::DescriptorImageInfo descImg(m_lfbOverlaySampler.get(),
                                      m_msaaDepthSampleView.get(),
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
      vk::WriteDescriptorSet write{};
      write.dstSet = m_msaaDepthResolveDescSet;
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      write.pImageInfo = &descImg;
      m_device->updateDescriptorSets(1, &write, 0, nullptr);
    }

    vk::UniqueShaderModule vertMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, hud_vert_spv_size, hud_vert_spv));
    vk::UniqueShaderModule fragMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, msaa_depth_resolve_frag_spv_size,
                                   msaa_depth_resolve_frag_spv));

    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,
                                          vertMod.get(), "main"),
        vk::PipelineShaderStageCreateInfo({},
                                          vk::ShaderStageFlagBits::eFragment,
                                          fragMod.get(), "main"),
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
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eAlways;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_FALSE;
    blendAtt.colorWriteMask = vk::ColorComponentFlags{};

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
    pipeInfo.renderPass = m_singleSampleRenderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create MSAA depth resolve pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    m_msaaDepthResolvePipeline = std::move(res.value);
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize MSAA depth resolve pipeline: " << e.what());
    return false;
  }
}

bool VulkanBackend::ResolveMsaaDepthToSingle_Locked() {
  if (m_msaaSamples <= 1 || !m_msaaDepthDirty || !m_msaaDepthImage ||
      !m_depthImage || !m_singleSampleRenderPass ||
      !m_singleSampleFramebuffers[0]) {
    return false;
  }
  if (!InitMsaaDepthResolvePipeline_Locked()) {
    return false;
  }

  // Transition the multisampled depth attachment to shader-read so sample 0
  // can be copied into the single-sampled depth attachment via texelFetch.
  vk::ImageSubresourceRange dsRange(DepthStencilAspectMask(), 0, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_msaaDepthImage.get(),
                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal, dsRange);

  vk::RenderPassBeginInfo rpBegin(
      m_singleSampleRenderPass.get(), m_singleSampleFramebuffers[0].get(),
      vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, nullptr);
  m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);
  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_msaaDepthResolvePipeline.get());
  m_commandBuffer->bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, m_lfbOverlayPipelineLayout.get(), 0, 1,
      &m_msaaDepthResolveDescSet, 0, nullptr);
  vk::Viewport vp(0.0f, 0.0f, static_cast<float>(m_headlessWidth),
                  static_cast<float>(m_headlessHeight), 0.0f, 1.0f);
  m_commandBuffer->setViewport(0, 1, &vp);
  vk::Rect2D sc({0, 0}, {m_headlessWidth, m_headlessHeight});
  m_commandBuffer->setScissor(0, 1, &sc);
  const float rectNdc[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
  m_commandBuffer->pushConstants(m_lfbOverlayPipelineLayout.get(),
                                 vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(rectNdc), rectNdc);
  m_commandBuffer->draw(4, 1, 0, 0);
  m_commandBuffer->endRenderPass();

  TransitionImageLayout(*m_commandBuffer, m_msaaDepthImage.get(),
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        dsRange);

  m_clearDepthPendingSingle[0] = false;
  m_clearDepthPendingSingle[1] = false;
  m_msaaDepthDirty = false;
  return true;
}

bool VulkanBackend::ReadbackAuxDepthToShadow_Locked() {
  if (!m_depthImage || !m_commandBuffer || m_headlessWidth == 0 ||
      m_headlessHeight == 0) {
    return false;
  }
  const size_t totalPx =
      static_cast<size_t>(m_headlessWidth) * m_headlessHeight;
  if (m_auxDepthShadow16.size() != totalPx) {
    m_auxDepthShadow16.resize(totalPx);
  }

  // Flush any pending 3D draws so the depth buffer reflects exact API order.
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }

  const uint32_t activeIdx = GetActiveGpuBufferIdx();

  // Zero-stall fast path: a full-screen depth clear was queued outside a render
  // pass and no geometry has executed since, so every pixel holds the exact
  // clear depth value.
  if (!m_inRenderPass && !m_msaaDepthDirty) {
    const bool singleCleared =
        (m_msaaSamples > 1) ? m_clearDepthPendingSingle[activeIdx]
                            : m_clearDepthPending[activeIdx];
    const bool msaaCleared =
        (m_msaaSamples > 1) ? m_clearDepthPending[activeIdx] : true;
    if (singleCleared && msaaCleared) {
      const uint16_t packed = EncodeNormDepthToAux16(
          m_clearDepthValue[activeIdx], m_depthMode, m_depthNear, m_depthFar);
      std::fill(m_auxDepthShadow16.begin(), m_auxDepthShadow16.end(), packed);
      m_auxDepthShadowValid = true;
      return true;
    }
  }

  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  if (m_msaaSamples > 1 && m_msaaDepthDirty) {
    if (!ResolveMsaaDepthToSingle_Locked()) {
      GLIDE_LOG(WARN, "Vulkan",
                "MSAA depth resolve unavailable; falling back to 1x depth "
                "readback.");
    }
  }

  // Materialize any lingering single-sample pending depth clear when no MSAA
  // resolve overwrote m_depthImage (e.g., mixed AA toggle within one frame).
  for (uint32_t i = 0; i < 2; ++i) {
    bool& dp =
        (m_msaaSamples > 1) ? m_clearDepthPendingSingle[i] : m_clearDepthPending[i];
    if (dp) {
      vk::RenderPass rp =
          (m_msaaSamples > 1 && m_singleSampleRenderPass)
              ? m_singleSampleRenderPass.get()
              : m_renderPass.get();
      vk::Framebuffer fb =
          (m_msaaSamples > 1 && m_singleSampleFramebuffers[i])
              ? m_singleSampleFramebuffers[i].get()
              : m_headlessFramebuffers[i].get();
      vk::RenderPassBeginInfo rpBegin(
          rp, fb, vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0,
          nullptr);
      m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);
      vk::ClearAttachment ca(
          vk::ImageAspectFlagBits::eDepth, 0,
          vk::ClearValue(vk::ClearDepthStencilValue(m_clearDepthValue[i], 0)));
      vk::ClearRect cr(
          vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
      m_commandBuffer->clearAttachments(1, &ca, 1, &cr);
      m_commandBuffer->endRenderPass();
      dp = false;
    }
  }

  const uint32_t slot = m_currentFrameSlot;
  vk::ImageSubresourceRange dsRange(DepthStencilAspectMask(), 0, 1, 0, 1);
  TransitionImageLayout(*m_commandBuffer, m_depthImage.get(),
                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, dsRange);

  vk::BufferImageCopy region{};
  region.imageSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eDepth, 0, 0, 1);
  region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
  m_commandBuffer->copyImageToBuffer(
      m_depthImage.get(), vk::ImageLayout::eTransferSrcOptimal,
      m_headlessPixelBuffers[slot].get(), 1, &region);

  SubmitMidFrameAndWaitIdle_Locked(false);

  const void* src = m_gpuStagingMaps[slot];
  if (src) {
    const uint32_t depthMode = m_depthMode;
    const float depthNear = m_depthNear;
    const float depthFar = m_depthFar;
    if (m_depthFormat == vk::Format::eD32Sfloat ||
        m_depthFormat == vk::Format::eD32SfloatS8Uint) {
      const auto* fSrc = reinterpret_cast<const float*>(src);
      for (size_t i = 0; i < totalPx; ++i) {
        m_auxDepthShadow16[i] =
            EncodeNormDepthToAux16(fSrc[i], depthMode, depthNear, depthFar);
      }
    } else if (m_depthFormat == vk::Format::eD24UnormS8Uint ||
               m_depthFormat == vk::Format::eX8D24UnormPack32) {
      const auto* uSrc = reinterpret_cast<const uint32_t*>(src);
      for (size_t i = 0; i < totalPx; ++i) {
        const float dNorm =
            static_cast<float>(uSrc[i] & kDepth24MaxU32) * kInvDepth24MaxF;
        m_auxDepthShadow16[i] =
            EncodeNormDepthToAux16(dNorm, depthMode, depthNear, depthFar);
      }
    } else if (m_depthFormat == vk::Format::eD16Unorm ||
               m_depthFormat == vk::Format::eD16UnormS8Uint) {
      const auto* u16Src = reinterpret_cast<const uint16_t*>(src);
      constexpr float kInvD16 = 1.0f / 65535.0f;
      for (size_t i = 0; i < totalPx; ++i) {
        const float dNorm = static_cast<float>(u16Src[i]) * kInvD16;
        m_auxDepthShadow16[i] =
            EncodeNormDepthToAux16(dNorm, depthMode, depthNear, depthFar);
      }
    } else {
      std::fill(m_auxDepthShadow16.begin(), m_auxDepthShadow16.end(),
                static_cast<uint16_t>(0xFFFFu));
    }
    m_auxDepthShadowValid = true;
  }

  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
  TransitionImageLayout(*m_commandBuffer, m_depthImage.get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        dsRange);
  return m_auxDepthShadowValid;
}

bool VulkanBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                            uint32_t srcWidth, uint32_t srcHeight,
                            uint32_t dstStride, void* dstData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  if (buffer == kLfbBufferAux) {
    if (!dstData || srcWidth == 0 || srcHeight == 0) return true;
    // Flush any buffered 3D vertices BEFORE checking cache validity so newly
    // submitted triangles invalidate m_auxDepthShadowValid first.
    if (m_batchedVertexCount > 0) {
      FlushBatch();
    }
    const size_t totalPx =
        static_cast<size_t>(m_headlessWidth) * m_headlessHeight;
    if (!m_auxDepthShadowValid || m_auxDepthShadow16.size() != totalPx) {
      (void)ReadbackAuxDepthToShadow_Locked();
    }
    auto* outBase = reinterpret_cast<uint8_t*>(dstData);
    const uint32_t copyW =
        (srcX < m_headlessWidth) ? std::min(srcWidth, m_headlessWidth - srcX) : 0u;
    const uint32_t copyH =
        (srcY < m_headlessHeight) ? std::min(srcHeight, m_headlessHeight - srcY) : 0u;
    for (uint32_t y = 0; y < copyH; ++y) {
      auto* rowOut = reinterpret_cast<uint16_t*>(
          outBase + static_cast<size_t>(y) * dstStride);
      if (m_auxDepthShadowValid && m_auxDepthShadow16.size() == totalPx) {
        const uint16_t* sRow = m_auxDepthShadow16.data() +
                               static_cast<size_t>(srcY + y) * m_headlessWidth;
        std::memcpy(rowOut, sRow + srcX, static_cast<size_t>(copyW) * sizeof(uint16_t));
      } else {
        std::fill_n(rowOut, copyW, static_cast<uint16_t>(0xFFFFu));
      }
    }
    return true;
  }

  // Flush any buffered 3D vertices BEFORE consulting the CPU readback cache
  // so newly drawn triangles always invalidate m_lfbReadCacheValid first!
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }

  const uint32_t logicalSlot = (buffer == 0u) ? 0u : 1u;
  if (!m_lfbReadCacheValid[logicalSlot]) {
    ReadbackGpuToStaging_Locked(buffer);
    m_lfbReadCacheValid[logicalSlot] = true;
  }

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
    size_t bandCount, CpuLfbStaging::OverlayBlendMode blendMode) {
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

  if (!EnsureLfbOverlayResources_Locked(width, height, totalBytes, blendMode)) {
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

  const size_t modeIdx = static_cast<size_t>(blendMode);
  vk::Pipeline overlayPipeline =
      (modeIdx < m_lfbOverlayPipelines.size())
          ? m_lfbOverlayPipelines[modeIdx].get()
          : vk::Pipeline{};
  if (!overlayPipeline) {
    m_commandBuffer->endRenderPass();
    return;
  }
  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                overlayPipeline);
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

  // Flush batched 3D vertices BEFORE terminating any active render pass so
  // 3D draws queued while !m_inRenderPass always rasterize prior to overlays.
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }
  // Terminate any active render pass so transfers / overlay passes can run.
  if (m_inRenderPass) {
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
      const auto overlayMode = m_lfbStaging.GetOverlayBlendMode(buf);
      if (overlayPixels && bands && bandCount > 0) {
        CompositeLfbOverlay_Locked(buf, targetGpuIdx, overlayPixels,
                                   m_lfbStaging.GetWidth(),
                                   m_lfbStaging.GetHeight(), bands, bandCount,
                                   overlayMode);
      }
      m_lfbStaging.ClearOverlay(buf);
    }
  }
}

}  // namespace GlideWrapper
