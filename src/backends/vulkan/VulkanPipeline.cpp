#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"

// Include build-time generated shader SPIR-V bytecode headers!
#include "color_dualsrc_frag.spv.h"
#include "color_dualsrc_wbuf_frag.spv.h"
#include "color_frag.spv.h"
#include "color_vert.spv.h"
#include "color_wbuf_frag.spv.h"
#include "hud_frag.spv.h"
#include "hud_vert.spv.h"

namespace GlideWrapper {

static vk::BlendFactor MapBlendFactorVulkan(uint32_t factor, bool isDest,
                                            bool isAlpha) {
  switch (factor) {
    case 0:
      return vk::BlendFactor::eZero;  // GR_BLEND_ZERO
    case 1:
      return vk::BlendFactor::eSrcAlpha;  // GR_BLEND_SRC_ALPHA
    case 2:
      return isDest
                 ? vk::BlendFactor::eSrcColor
                 : vk::BlendFactor::eDstColor;  // 2 = GR_BLEND_SRC_COLOR (dest)
                                                // / GR_BLEND_DST_COLOR (src)
    case 3:
      return vk::BlendFactor::eDstAlpha;  // GR_BLEND_DST_ALPHA
    case 4:
      return vk::BlendFactor::eOne;  // GR_BLEND_ONE
    case 5:
      return vk::BlendFactor::
          eOneMinusSrcAlpha;  // GR_BLEND_ONE_MINUS_SRC_ALPHA
    case 6:
      return isDest ? vk::BlendFactor::eOneMinusSrcColor
                    : vk::BlendFactor::
                          eOneMinusDstColor;  // 6 = ONE_MINUS_SRC_COLOR (dest)
                                              // / ONE_MINUS_DST_COLOR (src)
    case 7:
      return vk::BlendFactor::
          eOneMinusDstAlpha;  // GR_BLEND_ONE_MINUS_DST_ALPHA
    case 8:
      return isDest ? vk::BlendFactor::eDstColor
                    : vk::BlendFactor::eSrcColor;  // GR_BLEND_SAME_COLOR_EXT
    case 9:
      return isDest
                 ? vk::BlendFactor::eOneMinusDstColor
                 : vk::BlendFactor::
                       eOneMinusSrcColor;  // GR_BLEND_ONE_MINUS_SAME_COLOR_EXT
    case 15:
      return isDest
                 ? (isAlpha ? vk::BlendFactor::eSrc1Alpha
                            : vk::BlendFactor::eSrc1Color)
                 : vk::BlendFactor::
                       eSrcAlphaSaturate;  // 15 = GR_BLEND_PREFOG_COLOR (dest)
                                           // / GR_BLEND_ALPHA_SATURATE (src)
    default:
      return vk::BlendFactor::eOne;
  }
}

static vk::BlendOp MapBlendOpVulkan(uint32_t op) {
  switch (op) {
    case 1:
      return vk::BlendOp::eSubtract;  // GR_BLEND_OP_SUB
    case 2:
      return vk::BlendOp::eReverseSubtract;  // GR_BLEND_OP_REVSUB
    case 0:
    default:
      return vk::BlendOp::eAdd;  // GR_BLEND_OP_ADD
  }
}

static vk::CompareOp MapCompareOpVulkan(uint32_t op) {
  switch (op) {
    case 0: return vk::CompareOp::eNever;
    case 1: return vk::CompareOp::eLess;
    case 2: return vk::CompareOp::eEqual;
    case 3: return vk::CompareOp::eLessOrEqual;
    case 4: return vk::CompareOp::eGreater;
    case 5: return vk::CompareOp::eNotEqual;
    case 6: return vk::CompareOp::eGreaterOrEqual;
    case 7: return vk::CompareOp::eAlways;
    default: return vk::CompareOp::eAlways;
  }
}

static vk::StencilOp MapStencilOpVulkan(uint32_t op) {
  switch (op) {
    case 0: return vk::StencilOp::eKeep;
    case 1: return vk::StencilOp::eZero;
    case 2: return vk::StencilOp::eReplace;
    case 3: return vk::StencilOp::eIncrementAndClamp;
    case 4: return vk::StencilOp::eDecrementAndClamp;
    case 5: return vk::StencilOp::eInvert;
    case 6: return vk::StencilOp::eIncrementAndWrap;
    case 7: return vk::StencilOp::eDecrementAndWrap;
    default: return vk::StencilOp::eKeep;
  }
}

bool VulkanBackend::CreateRenderPass() {
  const bool hasStencil = HasStencilAspect();
  auto buildPass = [this, hasStencil](vk::SampleCountFlagBits samples)
      -> vk::UniqueRenderPass {
    // Attachment 0: Color Target
    vk::AttachmentDescription colorAttachment(
        {}, m_colorFormat, samples, vk::AttachmentLoadOp::eLoad,
        vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference colorRef(0,
                                     vk::ImageLayout::eColorAttachmentOptimal);

    // Attachment 1: Depth (+ Stencil) Target
    vk::AttachmentDescription depthAttachment(
        {}, m_depthFormat, samples, vk::AttachmentLoadOp::eLoad,
        vk::AttachmentStoreOp::eStore,
        hasStencil ? vk::AttachmentLoadOp::eLoad
                   : vk::AttachmentLoadOp::eDontCare,
        hasStencil ? vk::AttachmentStoreOp::eStore
                   : vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::AttachmentReference depthRef(
        1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0,
                                   nullptr, 1, &colorRef, nullptr, &depthRef);

    std::array<vk::SubpassDependency, 2> dependencies;

    // Dependency 1: Sync Clear/Transfer/Prior writes to Color/Depth Attachments
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask =
        vk::PipelineStageFlagBits::eTransfer |
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].dstStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].srcAccessMask =
        vk::AccessFlagBits::eTransferWrite |
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dstAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eColorAttachmentRead |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentRead;
    dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    // Dependency 2: Sync Color/Depth Attachment writes to Transfer/Resolve/Shader
    // reads (SwapBuffers, explicit vkCmdResolveImage, or 1x->MSAA copy)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eTransfer |
                                   vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].srcAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask =
        vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment,
                                                            depthAttachment};
    vk::RenderPassCreateInfo passInfo(
        {}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1,
        &subpass, static_cast<uint32_t>(dependencies.size()),
        dependencies.data());
    return m_device->createRenderPassUnique(passInfo);
  };

  const vk::SampleCountFlagBits samples =
      static_cast<vk::SampleCountFlagBits>(m_msaaSamples);
  m_renderPass = buildPass(samples);
  if (m_msaaSamples > 1) {
    m_singleSampleRenderPass = buildPass(vk::SampleCountFlagBits::e1);
  } else {
    m_singleSampleRenderPass.reset();
  }

  return true;
}

// --- Phase 2: Dynamic Shader Modules & Pipeline State Creation ---
bool VulkanBackend::CreateShaderModules() {
  if (m_vertShaderModule && m_fragShaderModule && m_fragShaderModuleDualSrc &&
      m_fragShaderModuleWBuf && m_fragShaderModuleDualSrcWBuf) {
    return true;  // Already persistent across window resets
  }
  try {
    vk::ShaderModuleCreateInfo vertInfo({}, color_vert_spv_size,
                                        color_vert_spv);
    m_vertShaderModule = m_device->createShaderModuleUnique(vertInfo);

    vk::ShaderModuleCreateInfo fragInfo({}, color_frag_spv_size,
                                        color_frag_spv);
    m_fragShaderModule = m_device->createShaderModuleUnique(fragInfo);

    vk::ShaderModuleCreateInfo fragDualSrcInfo({}, color_dualsrc_frag_spv_size,
                                               color_dualsrc_frag_spv);
    m_fragShaderModuleDualSrc =
        m_device->createShaderModuleUnique(fragDualSrcInfo);

    vk::ShaderModuleCreateInfo fragWBufInfo({}, color_wbuf_frag_spv_size,
                                            color_wbuf_frag_spv);
    m_fragShaderModuleWBuf = m_device->createShaderModuleUnique(fragWBufInfo);

    vk::ShaderModuleCreateInfo fragDualSrcWBufInfo(
        {}, color_dualsrc_wbuf_frag_spv_size, color_dualsrc_wbuf_frag_spv);
    m_fragShaderModuleDualSrcWBuf =
        m_device->createShaderModuleUnique(fragDualSrcWBufInfo);

    GLIDE_LOG(INFO, "Vulkan",
              "Loaded build-time compiled SPIR-V vertex and fragment shader "
              "modules (Early-Z + W-buffer + dual-source variants).");
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Failed to create Vulkan Shader Modules: " << e.what());
    return false;
  }
}

PipelineStateKey VulkanBackend::GetCurrentStateKey() const {
  PipelineStateKey key;
  key.cullMode = m_cullMode;
  key.sstOrigin = m_sstOrigin;
  key.depthMode = m_depthMode;
  if (m_depthMode != 0) {
    key.depthMask = m_depthMask;
    key.depthCompareOp = m_depthCompareOp;
  } else {
    key.depthMask = false;
    key.depthCompareOp = 0;  // Canonical constant for depth disabled
  }
  key.rgbSrcBlend = m_rgbSrcBlend;
  key.rgbDstBlend = m_rgbDstBlend;
  key.rgbBlendOp = m_rgbBlendOp;
  key.alphaSrcBlend = m_alphaSrcBlend;
  key.alphaDstBlend = m_alphaDstBlend;
  key.alphaBlendOp = m_alphaBlendOp;
  key.colorWriteMask = GetColorWriteMask4();
  if (m_msaaSamples > 1) {
    key.singleSample = ShouldUseSingleSamplePass_Locked();
    key.sampleMask = key.singleSample ? 1u : GetEffectiveSampleMask_Locked();
  } else {
    key.singleSample = false;
    key.sampleMask = 1u;
  }
  key.stencilEnabled = m_stencilEnabled && HasStencilAspect();
  if (key.stencilEnabled) {
    key.stencilCompareOp = m_stencilCompareOp;
    key.stencilFailOp = m_stencilFailOp;
    key.stencilZFailOp = m_stencilZFailOp;
    key.stencilZPassOp = m_stencilZPassOp;
  } else {
    key.stencilCompareOp = 7u;
    key.stencilFailOp = 0u;
    key.stencilZFailOp = 0u;
    key.stencilZPassOp = 0u;
  }
  return key;
}

vk::UniquePipeline VulkanBackend::CompileSinglePipeline(
    const PipelineStateKey& key, vk::PrimitiveTopology topology) {
  try {
    // Determine if the pipeline key requires dual-source blending (factor 15 =
    // GR_BLEND_PREFOG_COLOR) or W-buffer gl_FragDepth writes (depthMode == 2).
    const bool useDualSrcBlend =
        (key.rgbSrcBlend == 15 || key.rgbDstBlend == 15 ||
         key.alphaSrcBlend == 15 || key.alphaDstBlend == 15);
    const bool useWBufferDepth =
        (key.depthMode == 2u || key.depthMode == 4u ||
         key.compareToBiasPass == 1u);
    vk::ShaderModule fragShader =
        useWBufferDepth
            ? (useDualSrcBlend ? m_fragShaderModuleDualSrcWBuf.get()
                               : m_fragShaderModuleWBuf.get())
            : (useDualSrcBlend ? m_fragShaderModuleDualSrc.get()
                               : m_fragShaderModule.get());

    // Shader Stages
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,
                                          m_vertShaderModule.get(), "main"),
        vk::PipelineShaderStageCreateInfo(
            {}, vk::ShaderStageFlagBits::eFragment, fragShader, "main")};

    // Vertex Input Bindings (matches ModernVertex exactly)
    vk::VertexInputBindingDescription bindingDesc(0, sizeof(ModernVertex),
                                                  vk::VertexInputRate::eVertex);
    std::array<vk::VertexInputAttributeDescription, 5> attrDescs = {
        vk::VertexInputAttributeDescription(
            0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, pos)),
        vk::VertexInputAttributeDescription(1, 0,
                                            vk::Format::eR32G32B32A32Sfloat,
                                            offsetof(ModernVertex, color)),
        vk::VertexInputAttributeDescription(
            2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, tex)),
        vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(ModernVertex, tmu_oow)),
        vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32Sfloat,
                                            offsetof(ModernVertex, fog))};

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
        {}, 1, &bindingDesc, static_cast<uint32_t>(attrDescs.size()),
        attrDescs.data());

    // Input Assembly Topology
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, topology, false);

    // Dynamic Viewport, Scissor, and Stencil Masks/Reference (prevents
    // pipeline rebuilds on resolution change or stencil ref/mask changes)
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor,
        vk::DynamicState::eStencilCompareMask,
        vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilReference};
    vk::PipelineDynamicStateCreateInfo dynamicState(
        {}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());
    vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1,
                                                      nullptr);

    // Rasterization Configuration (Dynamic Depth Clamping prevents
    // out-of-bounds Z clipping!)
    vk::CullModeFlags vkCullMode = vk::CullModeFlagBits::eNone;
    switch (key.cullMode) {
      case 0:
        vkCullMode = vk::CullModeFlagBits::eNone;
        break;  // GR_CULL_DISABLE
      case 1:
        vkCullMode = vk::CullModeFlagBits::eBack;
        break;  // GR_CULL_NEGATIVE (cull CCW / Back face)
      case 2:
        vkCullMode = vk::CullModeFlagBits::eFront;
        break;  // GR_CULL_POSITIVE (cull CW / Front face)
      default:
        break;
    }

    vk::FrontFace frontFace = (key.sstOrigin == 1)
                                  ? vk::FrontFace::eCounterClockwise
                                  : vk::FrontFace::eClockwise;
    vk::PipelineRasterizationStateCreateInfo rasterizer(
        {}, m_depthClampEnabled, false, vk::PolygonMode::eFill, vkCullMode,
        frontFace, false, 0.0f, 0.0f, 0.0f, 1.0f);

    // Multisample Configuration (supports live 1x/MSAA switching and
    // VSA-100 grTBufferWriteMaskExt per-sample write masks)
    const bool useMsaa = (m_msaaSamples > 1 && !key.singleSample);
    const vk::SampleCountFlagBits sampleCount =
        useMsaa ? static_cast<vk::SampleCountFlagBits>(m_msaaSamples)
                : vk::SampleCountFlagBits::e1;
    const uint32_t fullMask =
        (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
    vk::SampleMask vkSampleMask = static_cast<vk::SampleMask>(key.sampleMask);
    const bool applySampleMask = (useMsaa && key.sampleMask != fullMask);
    vk::PipelineMultisampleStateCreateInfo multisampling(
        {}, sampleCount, false, 1.0f,
        applySampleMask ? &vkSampleMask : nullptr, false, false);

    // Depth Stencil Configuration based on Glide's active states
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    if (key.depthMode != 0) {
      depthStencil.depthTestEnable = true;
      depthStencil.depthWriteEnable = key.depthMask ? true : false;

      vk::CompareOp op = vk::CompareOp::eLess;
      switch (key.depthCompareOp) {
        case 0:
          op = vk::CompareOp::eNever;
          break;
        case 1:
          op = vk::CompareOp::eLess;
          break;
        case 2:
          op = vk::CompareOp::eEqual;
          break;
        case 3:
          op = vk::CompareOp::eLessOrEqual;
          break;
        case 4:
          op = vk::CompareOp::eGreater;
          break;
        case 5:
          op = vk::CompareOp::eNotEqual;
          break;
        case 6:
          op = vk::CompareOp::eGreaterOrEqual;
          break;
        case 7:
          op = vk::CompareOp::eAlways;
          break;
        default:
          break;
      }
      depthStencil.depthCompareOp = op;
    } else {
      depthStencil.depthTestEnable = false;
      depthStencil.depthWriteEnable = false;
    }
    depthStencil.depthBoundsTestEnable = false;
    if (key.compareToBiasPass == 1u && HasStencilAspect()) {
      // Pass 1 of GR_DEPTHBUFFER_{Z,W}BUFFER_COMPARE_TO_BIAS:
      // Probe depthBias against existing depth buffer; write stencil bit 0x80
      // where the depth comparison passes without touching color or depth.
      depthStencil.depthTestEnable = true;
      depthStencil.depthWriteEnable = false;
      depthStencil.stencilTestEnable = true;
      vk::StencilOpState stOp(vk::StencilOp::eKeep, vk::StencilOp::eReplace,
                              vk::StencilOp::eKeep, vk::CompareOp::eAlways,
                              0x80u, 0x80u, 0x80u);
      depthStencil.front = stOp;
      depthStencil.back = stOp;
    } else if (key.compareToBiasPass == 2u && HasStencilAspect()) {
      // Pass 2 of COMPARE_TO_BIAS: draw color + true Z/W depth only where
      // stencil bit 0x80 was marked by Pass 1, clearing bit 0x80 on pass.
      depthStencil.depthTestEnable = true;
      depthStencil.depthCompareOp = vk::CompareOp::eAlways;
      depthStencil.depthWriteEnable = key.depthMask ? true : false;
      depthStencil.stencilTestEnable = true;
      vk::StencilOpState stOp(vk::StencilOp::eKeep, vk::StencilOp::eZero,
                              vk::StencilOp::eKeep, vk::CompareOp::eEqual,
                              0x80u, 0x80u, 0x80u);
      depthStencil.front = stOp;
      depthStencil.back = stOp;
    } else {
      depthStencil.stencilTestEnable = key.stencilEnabled && HasStencilAspect();
      if (depthStencil.stencilTestEnable) {
        vk::StencilOpState stOp(MapStencilOpVulkan(key.stencilFailOp),
                                MapStencilOpVulkan(key.stencilZPassOp),
                                MapStencilOpVulkan(key.stencilZFailOp),
                                MapCompareOpVulkan(key.stencilCompareOp),
                                0xFFu, 0xFFu, 0u);
        depthStencil.front = stOp;
        depthStencil.back = stOp;
      }
    }

    // Color Blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    vk::ColorComponentFlags mask{};
    if (key.compareToBiasPass != 1u) {
      if (key.colorWriteMask & 0x1u) mask |= vk::ColorComponentFlagBits::eR;
      if (key.colorWriteMask & 0x2u) mask |= vk::ColorComponentFlagBits::eG;
      if (key.colorWriteMask & 0x4u) mask |= vk::ColorComponentFlagBits::eB;
      if (key.colorWriteMask & 0x8u) mask |= vk::ColorComponentFlagBits::eA;
    }
    colorBlendAttachment.colorWriteMask = mask;

    colorBlendAttachment.blendEnable = true;
    colorBlendAttachment.colorBlendOp = MapBlendOpVulkan(key.rgbBlendOp);
    colorBlendAttachment.srcColorBlendFactor =
        MapBlendFactorVulkan(key.rgbSrcBlend, false, false);
    colorBlendAttachment.dstColorBlendFactor =
        MapBlendFactorVulkan(key.rgbDstBlend, true, false);
    colorBlendAttachment.alphaBlendOp = MapBlendOpVulkan(key.alphaBlendOp);
    colorBlendAttachment.srcAlphaBlendFactor =
        MapBlendFactorVulkan(key.alphaSrcBlend, false, true);
    colorBlendAttachment.dstAlphaBlendFactor =
        MapBlendFactorVulkan(key.alphaDstBlend, true, true);

    vk::PipelineColorBlendStateCreateInfo colorBlending(
        {}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment,
        {0.0f, 0.0f, 0.0f, 0.0f});

    // Push Constants Layout (ensure layout exists)
    if (!m_pipelineLayout) {
      vk::PushConstantRange pushRange(
          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
          0, sizeof(PushConstants));
      vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
          {}, 1, &m_descriptorSetLayout.get(), 1, &pushRange);
      m_pipelineLayout =
          m_device->createPipelineLayoutUnique(pipelineLayoutInfo);
    }

    // Assemble Graphics Pipeline
    const vk::RenderPass targetRenderPass =
        (m_msaaSamples > 1 && key.singleSample)
            ? m_singleSampleRenderPass.get()
            : m_renderPass.get();
    vk::GraphicsPipelineCreateInfo pipelineInfo(
        {}, 2, shaderStages, &vertexInputInfo, &inputAssembly, nullptr,
        &viewportState, &rasterizer, &multisampling, &depthStencil,
        &colorBlending, &dynamicState, m_pipelineLayout.get(),
        targetRenderPass, 0, {}, -1);

    auto result = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache.get(), pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to compile Vulkan pipeline!");
    }
    return std::move(result.value);
  } catch (const std::exception& e) {
    GLIDE_LOG(
        CRITICAL, "Vulkan",
        "Failed to compile single Vulkan graphics pipeline: " << e.what());
  }
  return vk::UniquePipeline();
}

vk::Pipeline VulkanBackend::GetOrCreatePipeline(
    const PipelineStateKey& key, vk::PrimitiveTopology topology) {
  PipelineCacheKey cacheKey{key, topology};
  auto it = m_pipelineCache.find(cacheKey);
  if (it == m_pipelineCache.end()) {
    GLIDE_LOG(
        INFO, "Vulkan",
        "Pipeline Cache Miss! Compiling new pipeline for state & topology. "
        "Topology="
            << vk::to_string(topology) << ", Cull=" << key.cullMode
            << ", Origin=" << key.sstOrigin << ", DepthMode=" << key.depthMode
            << ", DepthMask=" << key.depthMask
            << ", DepthOp=" << key.depthCompareOp << ", RGBBlend("
            << key.rgbSrcBlend << "," << key.rgbDstBlend << ")"
            << ", AlphaBlend(" << key.alphaSrcBlend << "," << key.alphaDstBlend
            << ")");

    vk::UniquePipeline pipeline = CompileSinglePipeline(key, topology);
    if (!pipeline) {
      GLIDE_LOG(CRITICAL, "Vulkan",
                "Failed to compile new pipeline in GetOrCreatePipeline!");
      return vk::Pipeline();
    }
    vk::Pipeline rawPipeline = pipeline.get();
    m_pipelineCache.emplace(cacheKey, std::move(pipeline));
    TelemetryManager::GetInstance().RecordPsoBuild();
    return rawPipeline;
  }
  return it->second.get();
}

vk::Sampler VulkanBackend::GetOrCreateSampler(uint32_t clampS, uint32_t clampT,
                                              uint32_t minFilter,
                                              uint32_t magFilter, float lodBias,
                                              uint32_t mipmapMode,
                                              bool lodBlend) {
  SamplerKey key{clampS,  clampT,     minFilter, magFilter,
                 lodBias, mipmapMode, lodBlend};
  auto it = m_samplerCache.find(key);
  if (it != m_samplerCache.end()) {
    return it->second.get();
  }

  vk::SamplerAddressMode addressModeS = vk::SamplerAddressMode::eRepeat;
  if (clampS == 1)
    addressModeS = vk::SamplerAddressMode::eClampToEdge;
  else if (clampS == 2)
    addressModeS = vk::SamplerAddressMode::eMirroredRepeat;

  vk::SamplerAddressMode addressModeT = vk::SamplerAddressMode::eRepeat;
  if (clampT == 1)
    addressModeT = vk::SamplerAddressMode::eClampToEdge;
  else if (clampT == 2)
    addressModeT = vk::SamplerAddressMode::eMirroredRepeat;

  // Dynamically map Glide filter modes:
  // minFilter:
  // 0: nearest, 1: bilinear, 2: nearest-nearest, 3: bilinear-nearest, 4:
  // nearest-bilinear, 5: trilinear
  bool minLinear = (minFilter == 1 || minFilter == 3 || minFilter == 5);
  vk::Filter minF = minLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
  vk::Filter magF =
      (magFilter == 1) ? vk::Filter::eLinear : vk::Filter::eNearest;

  // Mipmap blending mode and maxLod are determined by the mipmapMode and
  // lodBlend parameters
  vk::SamplerMipmapMode vkMipmapMode = vk::SamplerMipmapMode::eNearest;
  float maxLod = 0.0f;
  float effectiveLodBias = lodBias;

  if (mipmapMode != 0) {  // GR_MIPMAP_DISABLE is 0
    maxLod = 15.0f;
    if (lodBlend) {
      vkMipmapMode = vk::SamplerMipmapMode::eLinear;
    } else {
      vkMipmapMode = vk::SamplerMipmapMode::eNearest;
      // Per the official 3dfx Glide 2.4/3.0 Reference Manuals (grTexMipMapMode)
      // and SST-1/CVG silicon (>> 8 fixed-point truncation), GR_MIPMAP_NEAREST
      // truncates (floors) the computed LOD rather than rounding to nearest
      // (whereas GR_MIPMAP_NEAREST_DITHER adds a 4x4 matrix in [0, 15/16],
      // centering transitions near k + 0.5). Vulkan's VK_SAMPLER_MIPMAP_MODE_NEAREST
      // rounds via ceil(lambda + 0.5) - 1, so subtract 127/256 (~0.4961, within
      // 1 LSB of SST-1 8.8 fixed-point LOD) so [k, k + 0.996) truncates to mip k
      // while exact integer ratios (2x -> 1.0, 4x -> 2.0) select mip k.
      if (mipmapMode == 1) {
        effectiveLodBias = lodBias - (127.0f / 256.0f);
      }
    }
  } else {
    maxLod = 0.0f;
    vkMipmapMode = vk::SamplerMipmapMode::eNearest;
  }

  if (m_physicalDevice) {
    const float maxBias =
        m_physicalDevice.getProperties().limits.maxSamplerLodBias;
    effectiveLodBias = std::clamp(effectiveLodBias, -maxBias, maxBias);
  }

  vk::SamplerCreateInfo samplerInfo;
  samplerInfo.magFilter = magF;
  samplerInfo.minFilter = minF;
  samplerInfo.addressModeU = addressModeS;
  samplerInfo.addressModeV = addressModeT;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.anisotropyEnable = false;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = false;
  samplerInfo.compareEnable = false;
  samplerInfo.compareOp = vk::CompareOp::eAlways;
  samplerInfo.mipmapMode = vkMipmapMode;
  samplerInfo.mipLodBias = effectiveLodBias;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = maxLod;

  try {
    auto sampler = m_device->createSamplerUnique(samplerInfo);
    vk::Sampler res = sampler.get();
    m_samplerCache.emplace(key, std::move(sampler));
    return res;
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "GetOrCreateSampler failed ("
                  << m_samplerCache.size()
                  << " cached samplers): " << e.what());
    Logger::GetInstance().Flush();
    return m_dummySampler.get();
  }
}

vk::DescriptorSet VulkanBackend::GetOrCreateDescriptorSet(
    vk::ImageView imageView0, vk::Sampler sampler0, vk::ImageView imageView1,
    vk::Sampler sampler1) {
  DescriptorKey key{imageView0, sampler0, imageView1, sampler1};
  auto it = m_descriptorCache.find(key);
  if (it != m_descriptorCache.end()) {
    return it->second.set.get();
  }

  auto tryAllocFromPool = [&](size_t poolIdx,
                              TrackedDescriptorSet& outEntry) -> bool {
    if (poolIdx >= m_descriptorPools.size() || !m_descriptorPools[poolIdx].pool) {
      return false;
    }
    auto& chunk = m_descriptorPools[poolIdx];
    // Respect exact per-pool maxSets even on drivers (e.g. lavapipe) that
    // silently over-allocate without returning VK_ERROR_OUT_OF_POOL_MEMORY.
    if (chunk.allocatedSets >= kDescriptorPoolChunkSets) {
      return false;
    }
    vk::DescriptorSetAllocateInfo setAlloc(chunk.pool.get(), 1,
                                           &m_descriptorSetLayout.get());
    try {
      auto sets = m_device->allocateDescriptorSetsUnique(setAlloc);
      if (sets.empty()) return false;
      outEntry.set = std::move(sets[0]);
      outEntry.poolIdx = poolIdx;
      ++chunk.allocatedSets;
      m_activeDescriptorPoolIdx = poolIdx;
      return true;
    } catch (const vk::OutOfPoolMemoryError&) {
      return false;
    } catch (const vk::FragmentedPoolError&) {
      return false;
    }
  };

  TrackedDescriptorSet descriptorEntry;
  try {
    bool allocated = false;
    if (!m_descriptorPools.empty()) {
      const size_t startIdx =
          (m_activeDescriptorPoolIdx < m_descriptorPools.size())
              ? m_activeDescriptorPoolIdx
              : 0u;
      allocated = tryAllocFromPool(startIdx, descriptorEntry);
      if (!allocated) {
        // Retry any existing pool that may have regained free slots via
        // fence-reclaimed vkFreeDescriptorSets.
        for (size_t i = 0; i < m_descriptorPools.size(); ++i) {
          if (i == startIdx) continue;
          if (tryAllocFromPool(i, descriptorEntry)) {
            allocated = true;
            break;
          }
        }
      }
    }

    if (!allocated) {
      // Dynamically grow a fresh descriptor pool chunk (zero frame stall,
      // supports dual-TMU/high-texture scenes exceeding 4,096 unique pairs).
      const size_t newIdx = CreateTextureDescriptorPool_Locked();
      GLIDE_LOG(INFO, "Vulkan",
                "Allocated overflow texture descriptor pool #"
                    << m_descriptorPools.size() << " (+"
                    << kDescriptorPoolChunkSets
                    << " sets; cached=" << m_descriptorCache.size()
                    << ", retired=[" << m_retiredDescriptorSets[0].size()
                    << "," << m_retiredDescriptorSets[1].size() << "]).");
      allocated = tryAllocFromPool(newIdx, descriptorEntry);
    }

    if (!allocated || !descriptorEntry.set) {
      GLIDE_LOG(CRITICAL, "Vulkan",
                "Failed to allocate texture descriptor set from overflow pool; "
                "falling back to dummy descriptor.");
      Logger::GetInstance().Flush();
      return m_dummyDescriptorSet;
    }
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "GetOrCreateDescriptorSet failed (cached="
                  << m_descriptorCache.size()
                  << ", pools=" << m_descriptorPools.size()
                  << "): " << e.what());
    Logger::GetInstance().Flush();
    return m_dummyDescriptorSet;
  }

  vk::DescriptorSet rawSet = descriptorEntry.set.get();

  // Update Descriptor Set for both bindings
  std::array<vk::DescriptorImageInfo, 2> imageInfos = {
      vk::DescriptorImageInfo(sampler0, imageView0,
                              vk::ImageLayout::eShaderReadOnlyOptimal),
      vk::DescriptorImageInfo(sampler1, imageView1,
                              vk::ImageLayout::eShaderReadOnlyOptimal)};
  std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {
      vk::WriteDescriptorSet(rawSet, 0, 0, 1,
                             vk::DescriptorType::eCombinedImageSampler,
                             &imageInfos[0], nullptr, nullptr),
      vk::WriteDescriptorSet(rawSet, 1, 0, 1,
                             vk::DescriptorType::eCombinedImageSampler,
                             &imageInfos[1], nullptr, nullptr)};
  m_device->updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()),
                                 descriptorWrites.data(), 0, nullptr);

  m_descriptorCache.emplace(key, std::move(descriptorEntry));
  const vk::ImageView dummyView = m_dummyTexture.imageView.get();
  if (imageView0 && imageView0 != dummyView) {
    const uint64_t h0 = VkHandleToU64(static_cast<VkImageView>(imageView0));
    m_viewToDescriptorKeys[h0].insert(key);
  }
  if (imageView1 && imageView1 != imageView0 && imageView1 != dummyView) {
    const uint64_t h1 = VkHandleToU64(static_cast<VkImageView>(imageView1));
    m_viewToDescriptorKeys[h1].insert(key);
  }
  return rawSet;
}

uint32_t VulkanBackend::GetEffectiveSampleMask_Locked() const {
  if (m_msaaSamples <= 1) return 1u;
  const uint32_t fullMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  return m_tbufferWriteMask & fullMask;
}

bool VulkanBackend::ShouldUseSingleSamplePass_Locked() const {
  if (m_msaaSamples <= 1) return false;
  const uint32_t fullMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  return !m_aaMultiSample && ((m_tbufferWriteMask & fullMask) == fullMask);
}

void VulkanBackend::ResolveMsaaToSingle_Locked(uint32_t gpuIdx) {
  if (m_msaaSamples <= 1 || gpuIdx >= 2 || !m_msaaColorDirty[gpuIdx] ||
      !m_commandBuffer || !m_msaaColorImages[gpuIdx] ||
      !m_headlessImages[gpuIdx]) {
    return;
  }

  vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);
  TransitionImageLayout(*m_commandBuffer, m_msaaColorImages[gpuIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferSrcOptimal, colorRange);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[gpuIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eTransferDstOptimal, colorRange);

  vk::ImageResolve resolveRegion{};
  resolveRegion.srcSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  resolveRegion.dstSubresource =
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  resolveRegion.extent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);

  m_commandBuffer->resolveImage(
      m_msaaColorImages[gpuIdx].get(), vk::ImageLayout::eTransferSrcOptimal,
      m_headlessImages[gpuIdx].get(), vk::ImageLayout::eTransferDstOptimal, 1,
      &resolveRegion);

  TransitionImageLayout(*m_commandBuffer, m_msaaColorImages[gpuIdx].get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[gpuIdx].get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);

  m_msaaColorDirty[gpuIdx] = false;
  m_singleSampleColorDirty[gpuIdx] = false;
}

bool VulkanBackend::InitMsaaCopyResources_Locked() {
  if (!m_device || !m_renderPass || m_msaaSamples <= 1) return false;
  if (m_msaaCopyPipeline) return true;

  try {
    if (!m_msaaCopySampler) {
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eNearest;
      samplerInfo.minFilter = vk::Filter::eNearest;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      m_msaaCopySampler = m_device->createSamplerUnique(samplerInfo);
    }

    if (!m_msaaCopyDescSetLayout) {
      vk::DescriptorSetLayoutBinding binding{};
      binding.binding = 0;
      binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      binding.descriptorCount = 1;
      binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

      vk::DescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.bindingCount = 1;
      layoutInfo.pBindings = &binding;
      m_msaaCopyDescSetLayout =
          m_device->createDescriptorSetLayoutUnique(layoutInfo);
    }

    if (!m_msaaCopyDescPool) {
      vk::DescriptorPoolSize poolSize(
          vk::DescriptorType::eCombinedImageSampler, 2);
      vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.maxSets = 2;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      m_msaaCopyDescPool = m_device->createDescriptorPoolUnique(poolInfo);

      std::array<vk::DescriptorSetLayout, 2> layouts = {
          m_msaaCopyDescSetLayout.get(), m_msaaCopyDescSetLayout.get()};
      vk::DescriptorSetAllocateInfo allocInfo{};
      allocInfo.descriptorPool = m_msaaCopyDescPool.get();
      allocInfo.descriptorSetCount = 2;
      allocInfo.pSetLayouts = layouts.data();
      auto sets = m_device->allocateDescriptorSets(allocInfo);
      m_msaaCopyDescSets[0] = sets[0];
      m_msaaCopyDescSets[1] = sets[1];
      m_msaaCopyBoundView[0] = vk::ImageView{};
      m_msaaCopyBoundView[1] = vk::ImageView{};
    }

    if (!m_msaaCopyPipelineLayout) {
      vk::PushConstantRange pushRange{};
      pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
      pushRange.offset = 0;
      pushRange.size = sizeof(float) * 4;

      vk::DescriptorSetLayout setLayout = m_msaaCopyDescSetLayout.get();
      vk::PipelineLayoutCreateInfo plInfo{};
      plInfo.setLayoutCount = 1;
      plInfo.pSetLayouts = &setLayout;
      plInfo.pushConstantRangeCount = 1;
      plInfo.pPushConstantRanges = &pushRange;
      m_msaaCopyPipelineLayout = m_device->createPipelineLayoutUnique(plInfo);
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
    multisample.rasterizationSamples =
        static_cast<vk::SampleCountFlagBits>(m_msaaSamples);

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = vk::CompareOp::eAlways;

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
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = m_msaaCopyPipelineLayout.get();
    pipeInfo.renderPass = m_renderPass.get();
    pipeInfo.subpass = 0;

    auto res = m_device->createGraphicsPipelineUnique(
        m_vkPipelineCache ? m_vkPipelineCache.get() : vk::PipelineCache{},
        pipeInfo);
    if (res.result != vk::Result::eSuccess) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to create Vulkan 1x->MSAA copy pipeline: "
                    << vk::to_string(res.result));
      return false;
    }
    m_msaaCopyPipeline = std::move(res.value);
    return true;
  } catch (const std::exception& e) {
    GLIDE_LOG(WARN, "Vulkan",
              "Failed to initialize Vulkan 1x->MSAA copy resources: "
                  << e.what());
    return false;
  }
}

void VulkanBackend::DestroyMsaaCopyResources_Locked() {
  m_msaaCopyPipeline.reset();
  m_msaaCopyPipelineLayout.reset();
  m_msaaCopyDescSets[0] = vk::DescriptorSet{};
  m_msaaCopyDescSets[1] = vk::DescriptorSet{};
  m_msaaCopyBoundView[0] = vk::ImageView{};
  m_msaaCopyBoundView[1] = vk::ImageView{};
  m_msaaCopyDescPool.reset();
  m_msaaCopyDescSetLayout.reset();
  m_msaaCopySampler.reset();
}

void VulkanBackend::CopySingleToMsaa_Locked(uint32_t gpuIdx) {
  if (m_msaaSamples <= 1 || gpuIdx >= 2 || !m_singleSampleColorDirty[gpuIdx] ||
      !m_commandBuffer || !m_headlessImages[gpuIdx] ||
      !m_headlessFramebuffers[gpuIdx]) {
    return;
  }
  if (!InitMsaaCopyResources_Locked()) return;

  const vk::ImageView srcView = m_headlessImageViews[gpuIdx].get();
  if (m_msaaCopyBoundView[gpuIdx] != srcView) {
    vk::DescriptorImageInfo descImg(m_msaaCopySampler.get(), srcView,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet write{};
    write.dstSet = m_msaaCopyDescSets[gpuIdx];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &descImg;
    m_device->updateDescriptorSets(1, &write, 0, nullptr);
    m_msaaCopyBoundView[gpuIdx] = srcView;
  }

  vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);
  TransitionImageLayout(*m_commandBuffer, m_headlessImages[gpuIdx].get(),
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal, colorRange);

  vk::RenderPassBeginInfo rpBegin(
      m_renderPass.get(), m_headlessFramebuffers[gpuIdx].get(),
      vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, nullptr);
  m_commandBuffer->beginRenderPass(rpBegin, vk::SubpassContents::eInline);
  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_msaaCopyPipeline.get());
  m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      m_msaaCopyPipelineLayout.get(), 0, 1,
                                      &m_msaaCopyDescSets[gpuIdx], 0, nullptr);

  vk::Viewport vp(0.0f, 0.0f, static_cast<float>(m_headlessWidth),
                  static_cast<float>(m_headlessHeight), 0.0f, 1.0f);
  m_commandBuffer->setViewport(0, 1, &vp);
  vk::Rect2D sc({0, 0}, {m_headlessWidth, m_headlessHeight});
  m_commandBuffer->setScissor(0, 1, &sc);

  const float rectNdc[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
  m_commandBuffer->pushConstants(m_msaaCopyPipelineLayout.get(),
                                 vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(rectNdc), rectNdc);
  m_commandBuffer->draw(4, 1, 0, 0);
  m_commandBuffer->endRenderPass();

  TransitionImageLayout(*m_commandBuffer, m_headlessImages[gpuIdx].get(),
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageLayout::eColorAttachmentOptimal, colorRange);

  m_singleSampleColorDirty[gpuIdx] = false;
  m_msaaColorDirty[gpuIdx] = false;
}

}  // namespace GlideWrapper
