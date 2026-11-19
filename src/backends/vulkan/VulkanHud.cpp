#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "core/Logger.h"
#include "core/Telemetry.h"

#include "hud_frag.spv.h"
#include "hud_vert.spv.h"
#include "present_gamma_frag.spv.h"

namespace GlideWrapper {

bool VulkanBackend::InitHudPipeline_Locked() {
  if (!m_device || m_swapchainImageFormat == vk::Format::eUndefined) {
    return false;
  }

  try {
    if (!m_hudSampler) {
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eNearest;
      samplerInfo.minFilter = vk::Filter::eNearest;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      m_hudSampler = m_device->createSamplerUnique(samplerInfo);
    }

    if (!m_hudDescSetLayout) {
      vk::DescriptorSetLayoutBinding binding{};
      binding.binding = 0;
      binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      binding.descriptorCount = 1;
      binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

      vk::DescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.bindingCount = 1;
      layoutInfo.pBindings = &binding;
      m_hudDescSetLayout = m_device->createDescriptorSetLayoutUnique(layoutInfo);
    }

    if (!m_hudDescPool) {
      vk::DescriptorPoolSize poolSize(
          vk::DescriptorType::eCombinedImageSampler, 1);
      vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      m_hudDescPool = m_device->createDescriptorPoolUnique(poolInfo);

      vk::DescriptorSetLayout setLayout = m_hudDescSetLayout.get();
      vk::DescriptorSetAllocateInfo allocInfo{};
      allocInfo.descriptorPool = m_hudDescPool.get();
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &setLayout;
      auto sets = m_device->allocateDescriptorSets(allocInfo);
      m_hudDescSet = sets[0];
    }

    if (!m_hudPipelineLayout) {
      vk::PushConstantRange pushRange{};
      pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
      pushRange.offset = 0;
      pushRange.size = sizeof(float) * 4;

      vk::DescriptorSetLayout setLayout = m_hudDescSetLayout.get();
      vk::PipelineLayoutCreateInfo plInfo{};
      plInfo.setLayoutCount = 1;
      plInfo.pSetLayouts = &setLayout;
      plInfo.pushConstantRangeCount = 1;
      plInfo.pPushConstantRanges = &pushRange;
      m_hudPipelineLayout = m_device->createPipelineLayoutUnique(plInfo);
    }

    if (m_hudPipeline && m_hudRenderPass &&
        m_hudPipelineFormat == m_swapchainImageFormat) {
      return true;
    }

    m_hudPipeline.reset();
    m_hudRenderPass.reset();

    // Render pass loads the already-blitted swapchain image (eTransferDstOptimal)
    // and transitions it cleanly to ePresentSrcKHR upon completion.
    vk::AttachmentDescription colorAtt(
        {}, m_swapchainImageFormat, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::ePresentSrcKHR);
    vk::AttachmentReference colorRef(0,
                                     vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0,
                                   nullptr, 1, &colorRef);

    vk::SubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = vk::PipelineStageFlagBits::eTransfer |
                       vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dep.srcAccessMask = vk::AccessFlagBits::eTransferWrite |
                        vk::AccessFlagBits::eColorAttachmentWrite;
    dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                        vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo rpInfo({}, 1, &colorAtt, 1, &subpass, 1, &dep);
    m_hudRenderPass = m_device->createRenderPassUnique(rpInfo);

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
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

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
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_hudPipelineLayout.get();
    pipeInfo.renderPass = m_hudRenderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create Vulkan HUD graphics pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    m_hudPipeline = std::move(res.value);
    m_hudPipelineFormat = m_swapchainImageFormat;
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan HUD pipeline: " << e.what());
    return false;
  }
}

bool VulkanBackend::CreateHudSwapchainFramebuffers_Locked() {
  m_hudFramebuffers.clear();
  if (!m_hudOverlay.IsEnabled() || m_swapchainImageViews.empty() ||
      m_swapchainExtent.width == 0 || m_swapchainExtent.height == 0) {
    return false;
  }
  if (!InitHudPipeline_Locked()) {
    return false;
  }
  try {
    m_hudFramebuffers.reserve(m_swapchainImageViews.size());
    for (const auto& view : m_swapchainImageViews) {
      vk::ImageView att = view.get();
      vk::FramebufferCreateInfo fbInfo({}, m_hudRenderPass.get(), 1, &att,
                                       m_swapchainExtent.width,
                                       m_swapchainExtent.height, 1);
      m_hudFramebuffers.push_back(m_device->createFramebufferUnique(fbInfo));
    }
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to create Vulkan HUD framebuffers: " << e.what());
    m_hudFramebuffers.clear();
    return false;
  }
}

void VulkanBackend::DestroyHudSwapchainFramebuffers_Locked() {
  m_hudFramebuffers.clear();
}

void VulkanBackend::DestroyHudResources_Locked() {
  m_hudFramebuffers.clear();
  for (int i = 0; i < 2; ++i) {
    if (m_hudStagingMaps[i] && m_device && m_hudStagingMemories[i]) {
      m_device->unmapMemory(m_hudStagingMemories[i].get());
      m_hudStagingMaps[i] = nullptr;
    }
    m_hudStagingBuffers[i].reset();
    m_hudStagingMemories[i].reset();
    m_hudStagingSizes[i] = 0;
  }
  m_hudImageView.reset();
  m_hudImage.reset();
  m_hudImageMemory.reset();
  m_hudImageLayout = vk::ImageLayout::eUndefined;
  m_hudTexW = 0;
  m_hudTexH = 0;
  m_hudUploadedRev = 0;

  m_hudPipeline.reset();
  m_hudPipelineLayout.reset();
  m_hudRenderPass.reset();
  m_hudPipelineFormat = vk::Format::eUndefined;
  m_hudDescSet = vk::DescriptorSet{};
  m_hudDescPool.reset();
  m_hudDescSetLayout.reset();
  m_hudSampler.reset();
}

bool VulkanBackend::EnsureHudTexture_Locked(uint32_t width, uint32_t height) {
  if (!m_device || width == 0 || height == 0) return false;

  const vk::DeviceSize byteSize =
      static_cast<vk::DeviceSize>(width) * height * 4;
  const uint32_t slot = m_currentFrameSlot;

  try {
    // Ensure per-frame-slot staging buffer is big enough (avoid reallocating on
    // minor text width changes by starting at >= 64 KB).
    if (!m_hudStagingBuffers[slot] || m_hudStagingSizes[slot] < byteSize) {
      if (m_hudStagingMaps[slot] && m_hudStagingMemories[slot]) {
        m_device->unmapMemory(m_hudStagingMemories[slot].get());
        m_hudStagingMaps[slot] = nullptr;
      }
      m_hudStagingBuffers[slot].reset();
      m_hudStagingMemories[slot].reset();
      m_hudStagingSizes[slot] = 0;

      const vk::DeviceSize allocSize =
          std::max(byteSize, vk::DeviceSize{65536});
      vk::BufferCreateInfo bufInfo({}, allocSize,
                                   vk::BufferUsageFlagBits::eTransferSrc,
                                   vk::SharingMode::eExclusive);
      m_hudStagingBuffers[slot] = m_device->createBufferUnique(bufInfo);

      vk::MemoryRequirements memReq =
          m_device->getBufferMemoryRequirements(m_hudStagingBuffers[slot].get());
      vk::MemoryAllocateInfo allocInfo(
          memReq.size,
          FindMemoryType(memReq.memoryTypeBits,
                         vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent));
      m_hudStagingMemories[slot] = m_device->allocateMemoryUnique(allocInfo);
      m_device->bindBufferMemory(m_hudStagingBuffers[slot].get(),
                                 m_hudStagingMemories[slot].get(), 0);
      m_hudStagingMaps[slot] = m_device->mapMemory(
          m_hudStagingMemories[slot].get(), 0, allocSize);
      m_hudStagingSizes[slot] = allocSize;
    }

    if (m_hudImage && m_hudTexW == width && m_hudTexH == height) {
      return true;
    }

    // Dimensions changed: wait for any in-flight HUD draw to finish before
    // replacing the device-local image referenced by m_hudDescSet.
    if (m_hudImage) {
      m_device->waitIdle();
      m_hudImageView.reset();
      m_hudImage.reset();
      m_hudImageMemory.reset();
    }

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType = vk::ImageType::e2D;
    imgInfo.format = vk::Format::eR8G8B8A8Unorm;
    imgInfo.extent = vk::Extent3D(width, height, 1);
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = vk::SampleCountFlagBits::e1;
    imgInfo.tiling = vk::ImageTiling::eOptimal;
    imgInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled;
    imgInfo.sharingMode = vk::SharingMode::eExclusive;
    imgInfo.initialLayout = vk::ImageLayout::eUndefined;
    m_hudImage = m_device->createImageUnique(imgInfo);

    vk::MemoryRequirements imgReq =
        m_device->getImageMemoryRequirements(m_hudImage.get());
    vk::MemoryAllocateInfo imgAlloc(
        imgReq.size, FindMemoryType(imgReq.memoryTypeBits,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_hudImageMemory = m_device->allocateMemoryUnique(imgAlloc);
    m_device->bindImageMemory(m_hudImage.get(), m_hudImageMemory.get(), 0);

    vk::ImageViewCreateInfo viewInfo(
        {}, m_hudImage.get(), vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Unorm, {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_hudImageView = m_device->createImageViewUnique(viewInfo);

    vk::DescriptorImageInfo descImg(m_hudSampler.get(), m_hudImageView.get(),
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet write{};
    write.dstSet = m_hudDescSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &descImg;
    m_device->updateDescriptorSets(1, &write, 0, nullptr);

    m_hudTexW = width;
    m_hudTexH = height;
    m_hudImageLayout = vk::ImageLayout::eUndefined;
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to allocate Vulkan HUD texture: " << e.what());
    return false;
  }
}

bool VulkanBackend::RenderHudToSwapchain_Locked(uint32_t imageIdx,
                                                const PresentRect& dst) {
  if (!m_hudOverlay.IsEnabled() || !m_hudPipeline || !m_hudRenderPass ||
      imageIdx >= m_hudFramebuffers.size() || m_swapchainExtent.width == 0 ||
      m_swapchainExtent.height == 0) {
    return false;
  }

  m_hudOverlay.Update(TelemetryManager::GetInstance().GetLastFrameMetrics());
  const uint32_t hudW = m_hudOverlay.GetWidth();
  const uint32_t hudH = m_hudOverlay.GetHeight();
  const uint8_t* rgba = m_hudOverlay.GetRgbaTopDown();
  if (hudW == 0 || hudH == 0 || !rgba) {
    return false;
  }

  const bool needsUpload = (!m_hudImage || m_hudTexW != hudW ||
                            m_hudTexH != hudH ||
                            m_hudUploadedRev != m_hudOverlay.GetRevision());
  if (needsUpload) {
    if (!EnsureHudTexture_Locked(hudW, hudH)) {
      return false;
    }

    const uint32_t slot = m_currentFrameSlot;
    std::memcpy(m_hudStagingMaps[slot], rgba,
                static_cast<size_t>(hudW) * hudH * 4);

    vk::ImageSubresourceRange hudRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);

    // Transition HUD image to TransferDstOptimal
    vk::ImageMemoryBarrier toTransfer{};
    toTransfer.oldLayout = m_hudImageLayout;
    toTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_hudImage.get();
    toTransfer.subresourceRange = hudRange;
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    if (m_hudImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      toTransfer.srcAccessMask = vk::AccessFlagBits::eShaderRead;
      srcStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      toTransfer.srcAccessMask = {};
    }
    toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    m_commandBuffer->pipelineBarrier(srcStage,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     0, nullptr, 0, nullptr, 1, &toTransfer);

    vk::BufferImageCopy region{};
    region.imageSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(hudW, hudH, 1);
    m_commandBuffer->copyBufferToImage(
        m_hudStagingBuffers[slot].get(), m_hudImage.get(),
        vk::ImageLayout::eTransferDstOptimal, 1, &region);

    // Transition HUD image to ShaderReadOnlyOptimal for fragment sampling
    vk::ImageMemoryBarrier toSample{};
    toSample.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    toSample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.image = m_hudImage.get();
    toSample.subresourceRange = hudRange;
    toSample.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    toSample.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    m_commandBuffer->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr,
        1, &toSample);

    m_hudImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    m_hudUploadedRev = m_hudOverlay.GetRevision();
  }

  int32_t hudX = 0;
  int32_t hudY = 0;
  uint32_t drawW = 0;
  uint32_t drawH = 0;
  m_hudOverlay.ComputePlacement(dst.x, dst.y, dst.width, dst.height, hudX, hudY,
                                drawW, drawH);
  if (drawW == 0 || drawH == 0) {
    return false;
  }

  const float swW = static_cast<float>(m_swapchainExtent.width);
  const float swH = static_cast<float>(m_swapchainExtent.height);
  const float rectNdc[4] = {
      (static_cast<float>(hudX) / swW) * 2.0f - 1.0f,
      (static_cast<float>(hudY) / swH) * 2.0f - 1.0f,
      (static_cast<float>(hudX + static_cast<int32_t>(drawW)) / swW) * 2.0f -
          1.0f,
      (static_cast<float>(hudY + static_cast<int32_t>(drawH)) / swH) * 2.0f -
          1.0f,
  };

  vk::RenderPassBeginInfo rpBegin{};
  rpBegin.renderPass = m_hudRenderPass.get();
  rpBegin.framebuffer = m_hudFramebuffers[imageIdx].get();
  rpBegin.renderArea = vk::Rect2D({0, 0}, m_swapchainExtent);

  m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);
  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_hudPipeline.get());
  m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      m_hudPipelineLayout.get(), 0, 1,
                                      &m_hudDescSet, 0, nullptr);

  vk::Viewport vp(0.0f, 0.0f, swW, swH, 0.0f, 1.0f);
  vk::Rect2D sc({0, 0}, m_swapchainExtent);
  m_commandBuffer->setViewport(0, 1, &vp);
  m_commandBuffer->setScissor(0, 1, &sc);
  m_commandBuffer->pushConstants(m_hudPipelineLayout.get(),
                                 vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(rectNdc), rectNdc);
  m_commandBuffer->draw(4, 1, 0, 0);
  m_commandBuffer->endRenderPass();
  return true;
}

// =============================================================================
// Silicon-Faithful Guest-Resolution RAMDAC Gamma LUT Pass
// =============================================================================

bool VulkanBackend::InitGammaPresentPipeline_Locked() {
  if (!m_device || m_colorFormat == vk::Format::eUndefined) {
    return false;
  }

  try {
    if (!m_gammaSrcSampler) {
      // Guest-resolution 1:1 RAMDAC lookup: sample exact guest texels before
      // the display presentation blit scales to the host swapchain.
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eNearest;
      samplerInfo.minFilter = vk::Filter::eNearest;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      m_gammaSrcSampler = m_device->createSamplerUnique(samplerInfo);
    }

    if (!m_gammaLutSampler) {
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eNearest;
      samplerInfo.minFilter = vk::Filter::eNearest;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      m_gammaLutSampler = m_device->createSamplerUnique(samplerInfo);
    }

    if (!m_gammaPresentDescSetLayout) {
      std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
      bindings[0].binding = 0;
      bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

      bindings[1].binding = 1;
      bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
      bindings[1].descriptorCount = 1;
      bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

      vk::DescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
      layoutInfo.pBindings = bindings.data();
      m_gammaPresentDescSetLayout =
          m_device->createDescriptorSetLayoutUnique(layoutInfo);
    }

    if (!m_gammaPresentDescPool) {
      vk::DescriptorPoolSize poolSize(
          vk::DescriptorType::eCombinedImageSampler, 4);
      vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.maxSets = 2;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      m_gammaPresentDescPool = m_device->createDescriptorPoolUnique(poolInfo);

      std::array<vk::DescriptorSetLayout, 2> setLayouts = {
          m_gammaPresentDescSetLayout.get(), m_gammaPresentDescSetLayout.get()};
      vk::DescriptorSetAllocateInfo allocInfo{};
      allocInfo.descriptorPool = m_gammaPresentDescPool.get();
      allocInfo.descriptorSetCount = 2;
      allocInfo.pSetLayouts = setLayouts.data();
      auto sets = m_device->allocateDescriptorSets(allocInfo);
      m_gammaPresentDescSets[0] = sets[0];
      m_gammaPresentDescSets[1] = sets[1];
      m_gammaPresentBoundSrcView[0] = vk::ImageView{};
      m_gammaPresentBoundSrcView[1] = vk::ImageView{};
    }

    if (!m_gammaPresentPipelineLayout) {
      vk::PushConstantRange pushRange{};
      pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
      pushRange.offset = 0;
      pushRange.size = sizeof(float) * 4;

      vk::DescriptorSetLayout setLayout = m_gammaPresentDescSetLayout.get();
      vk::PipelineLayoutCreateInfo plInfo{};
      plInfo.setLayoutCount = 1;
      plInfo.pSetLayouts = &setLayout;
      plInfo.pushConstantRangeCount = 1;
      plInfo.pPushConstantRanges = &pushRange;
      m_gammaPresentPipelineLayout =
          m_device->createPipelineLayoutUnique(plInfo);
    }

    if (m_gammaPresentPipeline && m_gammaPresentRenderPass &&
        m_gammaPresentPipelineFormat == m_colorFormat) {
      return true;
    }

    m_gammaPresentPipeline.reset();
    m_gammaPresentRenderPass.reset();

    // Applies the 256-entry RGB RAMDAC LUT across the guest-resolution target
    // (1:1 fullscreen quad, no clear needed) and finishes in eTransferSrcOptimal
    // so the subsequent vkCmdBlitImage scales directly into the swapchain.
    vk::AttachmentDescription colorAtt(
        {}, m_colorFormat, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);
    vk::AttachmentReference colorRef(0,
                                     vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0,
                                   nullptr, 1, &colorRef);

    std::array<vk::SubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                           vk::PipelineStageFlagBits::eTransfer;
    deps[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    deps[0].srcAccessMask = vk::AccessFlagBits::eTransferRead |
                            vk::AccessFlagBits::eColorAttachmentWrite;
    deps[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    deps[1].dstStageMask = vk::PipelineStageFlagBits::eTransfer;
    deps[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    deps[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;

    vk::RenderPassCreateInfo rpInfo({}, 1, &colorAtt, 1, &subpass,
                                    static_cast<uint32_t>(deps.size()),
                                    deps.data());
    m_gammaPresentRenderPass = m_device->createRenderPassUnique(rpInfo);

    vk::UniqueShaderModule vertMod = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, hud_vert_spv_size, hud_vert_spv));
    vk::UniqueShaderModule fragMod =
        m_device->createShaderModuleUnique(vk::ShaderModuleCreateInfo(
            {}, present_gamma_frag_spv_size, present_gamma_frag_spv));

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
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_FALSE;
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
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_gammaPresentPipelineLayout.get();
    pipeInfo.renderPass = m_gammaPresentRenderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create Vulkan Gamma presentation pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    m_gammaPresentPipeline = std::move(res.value);
    m_gammaPresentPipelineFormat = m_colorFormat;
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan Gamma presentation pipeline: "
                  << e.what());
    return false;
  }
}

bool VulkanBackend::EnsureGammaResolvedTargets_Locked() {
  if (!m_device || m_headlessWidth == 0 || m_headlessHeight == 0) {
    return false;
  }
  if (!InitGammaPresentPipeline_Locked()) {
    return false;
  }
  if (m_gammaResolvedWidth == m_headlessWidth &&
      m_gammaResolvedHeight == m_headlessHeight &&
      m_gammaResolvedFramebuffers[0] && m_gammaResolvedFramebuffers[1]) {
    return true;
  }

  for (int i = 0; i < 2; ++i) {
    m_gammaResolvedFramebuffers[i].reset();
    m_gammaResolvedImageViews[i].reset();
    m_gammaResolvedImages[i].reset();
    m_gammaResolvedImageMemories[i].reset();
  }
  m_gammaResolvedWidth = 0;
  m_gammaResolvedHeight = 0;

  try {
    for (int i = 0; i < 2; ++i) {
      vk::ImageCreateInfo imgInfo{};
      imgInfo.imageType = vk::ImageType::e2D;
      imgInfo.format = m_colorFormat;
      imgInfo.extent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
      imgInfo.mipLevels = 1;
      imgInfo.arrayLayers = 1;
      imgInfo.samples = vk::SampleCountFlagBits::e1;
      imgInfo.tiling = vk::ImageTiling::eOptimal;
      imgInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferSrc;
      imgInfo.initialLayout = vk::ImageLayout::eUndefined;
      m_gammaResolvedImages[i] = m_device->createImageUnique(imgInfo);

      auto memReqs =
          m_device->getImageMemoryRequirements(m_gammaResolvedImages[i].get());
      uint32_t memType = FindMemoryType(
          memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      m_gammaResolvedImageMemories[i] = m_device->allocateMemoryUnique(
          vk::MemoryAllocateInfo(memReqs.size, memType));
      m_device->bindImageMemory(m_gammaResolvedImages[i].get(),
                                m_gammaResolvedImageMemories[i].get(), 0);

      vk::ImageViewCreateInfo viewInfo(
          {}, m_gammaResolvedImages[i].get(), vk::ImageViewType::e2D,
          m_colorFormat, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
      m_gammaResolvedImageViews[i] = m_device->createImageViewUnique(viewInfo);

      vk::ImageView att = m_gammaResolvedImageViews[i].get();
      vk::FramebufferCreateInfo fbInfo({}, m_gammaPresentRenderPass.get(), 1,
                                       &att, m_headlessWidth, m_headlessHeight,
                                       1);
      m_gammaResolvedFramebuffers[i] = m_device->createFramebufferUnique(fbInfo);
    }
    m_gammaResolvedWidth = m_headlessWidth;
    m_gammaResolvedHeight = m_headlessHeight;
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to allocate guest-resolution Vulkan Gamma targets: "
                  << e.what());
    for (int i = 0; i < 2; ++i) {
      m_gammaResolvedFramebuffers[i].reset();
      m_gammaResolvedImageViews[i].reset();
      m_gammaResolvedImages[i].reset();
      m_gammaResolvedImageMemories[i].reset();
    }
    m_gammaResolvedWidth = 0;
    m_gammaResolvedHeight = 0;
    return false;
  }
}

void VulkanBackend::DestroyGammaPresentResources_Locked() {
  for (int i = 0; i < 2; ++i) {
    m_gammaResolvedFramebuffers[i].reset();
    m_gammaResolvedImageViews[i].reset();
    m_gammaResolvedImages[i].reset();
    m_gammaResolvedImageMemories[i].reset();
  }
  m_gammaResolvedWidth = 0;
  m_gammaResolvedHeight = 0;
  m_gammaPresentPipeline.reset();
  m_gammaPresentRenderPass.reset();
  m_gammaPresentPipelineFormat = vk::Format::eUndefined;
  m_gammaPresentPipelineLayout.reset();
  m_gammaPresentDescPool.reset();
  m_gammaPresentDescSetLayout.reset();
  m_gammaPresentDescSets[0] = vk::DescriptorSet{};
  m_gammaPresentDescSets[1] = vk::DescriptorSet{};
  m_gammaPresentBoundSrcView[0] = vk::ImageView{};
  m_gammaPresentBoundSrcView[1] = vk::ImageView{};
  m_gammaSrcSampler.reset();
  m_gammaLutSampler.reset();

  m_gammaLutImageView.reset();
  m_gammaLutImage.reset();
  m_gammaLutImageMemory.reset();
  m_gammaLutImageLayout = vk::ImageLayout::eUndefined;

  for (int i = 0; i < 2; ++i) {
    if (m_gammaLutStagingMaps[i] && m_device && m_gammaLutStagingMemories[i]) {
      m_device->unmapMemory(m_gammaLutStagingMemories[i].get());
      m_gammaLutStagingMaps[i] = nullptr;
    }
    m_gammaLutStagingBuffers[i].reset();
    m_gammaLutStagingMemories[i].reset();
  }
  m_gammaLutDirty = true;
}

bool VulkanBackend::EnsureGammaLutTexture_Locked() {
  if (!m_device) return false;
  if (m_gammaLutImage && m_gammaLutImageView && m_gammaLutStagingMaps[0] &&
      m_gammaLutStagingMaps[1]) {
    return true;
  }

  try {
    constexpr vk::DeviceSize kLutBytes = 256 * 4;
    if (!m_gammaLutImage) {
      vk::ImageCreateInfo imgInfo{};
      imgInfo.imageType = vk::ImageType::e2D;
      imgInfo.format = vk::Format::eR8G8B8A8Unorm;
      imgInfo.extent = vk::Extent3D(256, 1, 1);
      imgInfo.mipLevels = 1;
      imgInfo.arrayLayers = 1;
      imgInfo.samples = vk::SampleCountFlagBits::e1;
      imgInfo.tiling = vk::ImageTiling::eOptimal;
      imgInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
      imgInfo.initialLayout = vk::ImageLayout::eUndefined;
      m_gammaLutImage = m_device->createImageUnique(imgInfo);

      auto memReqs =
          m_device->getImageMemoryRequirements(m_gammaLutImage.get());
      uint32_t memType = FindMemoryType(
          memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      m_gammaLutImageMemory = m_device->allocateMemoryUnique(
          vk::MemoryAllocateInfo(memReqs.size, memType));
      m_device->bindImageMemory(m_gammaLutImage.get(),
                                m_gammaLutImageMemory.get(), 0);

      vk::ImageViewCreateInfo viewInfo(
          {}, m_gammaLutImage.get(), vk::ImageViewType::e2D,
          vk::Format::eR8G8B8A8Unorm, {},
          {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
      m_gammaLutImageView = m_device->createImageViewUnique(viewInfo);
      m_gammaLutImageLayout = vk::ImageLayout::eUndefined;
      m_gammaLutDirty = true;
    }

    for (int i = 0; i < 2; ++i) {
      if (!m_gammaLutStagingBuffers[i]) {
        vk::BufferCreateInfo bufInfo({}, kLutBytes,
                                     vk::BufferUsageFlagBits::eTransferSrc,
                                     vk::SharingMode::eExclusive);
        m_gammaLutStagingBuffers[i] = m_device->createBufferUnique(bufInfo);
        auto memReqs = m_device->getBufferMemoryRequirements(
            m_gammaLutStagingBuffers[i].get());
        uint32_t memType =
            FindMemoryType(memReqs.memoryTypeBits,
                           vk::MemoryPropertyFlagBits::eHostVisible |
                               vk::MemoryPropertyFlagBits::eHostCoherent);
        m_gammaLutStagingMemories[i] = m_device->allocateMemoryUnique(
            vk::MemoryAllocateInfo(memReqs.size, memType));
        m_device->bindBufferMemory(m_gammaLutStagingBuffers[i].get(),
                                   m_gammaLutStagingMemories[i].get(), 0);
        m_gammaLutStagingMaps[i] = m_device->mapMemory(
            m_gammaLutStagingMemories[i].get(), 0, kLutBytes);
      }
    }
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to allocate Vulkan Gamma LUT texture: " << e.what());
    return false;
  }
}

bool VulkanBackend::ApplyGammaToGuestImage_Locked(uint32_t activeIdx,
                                                  vk::Image& outResolvedImage) {
  if (!m_useGammaLut || !m_device || !m_commandBuffer ||
      m_headlessWidth == 0 || m_headlessHeight == 0 ||
      !m_headlessImages[activeIdx] || !m_headlessImageViews[activeIdx]) {
    return false;
  }

  if (!EnsureGammaResolvedTargets_Locked() || !EnsureGammaLutTexture_Locked()) {
    return false;
  }

  const uint32_t slot = m_currentFrameSlot;
  if (!m_gammaPresentPipeline || !m_gammaPresentRenderPass ||
      !m_gammaResolvedFramebuffers[slot] || !m_gammaResolvedImages[slot]) {
    return false;
  }

  vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);

  if (m_gammaLutDirty) {
    auto* lutBytes = static_cast<uint8_t*>(m_gammaLutStagingMaps[slot]);
    for (int i = 0; i < 256; ++i) {
      lutBytes[i * 4 + 0] = m_lutR[i];
      lutBytes[i * 4 + 1] = m_lutG[i];
      lutBytes[i * 4 + 2] = m_lutB[i];
      lutBytes[i * 4 + 3] = 255;
    }

    vk::ImageMemoryBarrier toTransfer{};
    toTransfer.oldLayout = m_gammaLutImageLayout;
    toTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_gammaLutImage.get();
    toTransfer.subresourceRange = colorRange;
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    if (m_gammaLutImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
      toTransfer.srcAccessMask = vk::AccessFlagBits::eShaderRead;
      srcStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    m_commandBuffer->pipelineBarrier(srcStage,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     0, nullptr, 0, nullptr, 1, &toTransfer);

    vk::BufferImageCopy region{};
    region.imageSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(256, 1, 1);
    m_commandBuffer->copyBufferToImage(
        m_gammaLutStagingBuffers[slot].get(), m_gammaLutImage.get(),
        vk::ImageLayout::eTransferDstOptimal, 1, &region);

    vk::ImageMemoryBarrier toSample{};
    toSample.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    toSample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSample.image = m_gammaLutImage.get();
    toSample.subresourceRange = colorRange;
    toSample.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    toSample.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    m_commandBuffer->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr,
        1, &toSample);

    m_gammaLutImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    m_gammaLutDirty = false;
  }

  vk::ImageView srcView = m_headlessImageViews[activeIdx].get();
  if (m_gammaPresentBoundSrcView[slot] != srcView) {
    vk::DescriptorImageInfo srcInfo(m_gammaSrcSampler.get(), srcView,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::DescriptorImageInfo lutInfo(m_gammaLutSampler.get(),
                                    m_gammaLutImageView.get(),
                                    vk::ImageLayout::eShaderReadOnlyOptimal);

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0].dstSet = m_gammaPresentDescSets[slot];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[0].pImageInfo = &srcInfo;

    writes[1].dstSet = m_gammaPresentDescSets[slot];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[1].pImageInfo = &lutInfo;

    m_device->updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                   writes.data(), 0, nullptr);
    m_gammaPresentBoundSrcView[slot] = srcView;
  }

  // Transition offscreen guest framebuffer for fragment sampling
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal, colorRange);

  const vk::Extent2D guestExtent(m_headlessWidth, m_headlessHeight);
  constexpr float kFullQuadNdc[4] = {-1.0f, -1.0f, 1.0f, 1.0f};

  vk::RenderPassBeginInfo rpBegin{};
  rpBegin.renderPass = m_gammaPresentRenderPass.get();
  rpBegin.framebuffer = m_gammaResolvedFramebuffers[slot].get();
  rpBegin.renderArea = vk::Rect2D({0, 0}, guestExtent);

  m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);
  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_gammaPresentPipeline.get());
  m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      m_gammaPresentPipelineLayout.get(), 0, 1,
                                      &m_gammaPresentDescSets[slot], 0,
                                      nullptr);

  vk::Viewport vp(0.0f, 0.0f, static_cast<float>(m_headlessWidth),
                  static_cast<float>(m_headlessHeight), 0.0f, 1.0f);
  vk::Rect2D sc({0, 0}, guestExtent);
  m_commandBuffer->setViewport(0, 1, &vp);
  m_commandBuffer->setScissor(0, 1, &sc);
  m_commandBuffer->pushConstants(m_gammaPresentPipelineLayout.get(),
                                 vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(kFullQuadNdc), kFullQuadNdc);
  m_commandBuffer->draw(4, 1, 0, 0);
  m_commandBuffer->endRenderPass();

  // Transition offscreen guest framebuffer back for next frame's rendering
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);

  outResolvedImage = m_gammaResolvedImages[slot].get();
  return true;
}

}  // namespace GlideWrapper
