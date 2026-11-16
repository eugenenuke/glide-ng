#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"

// Include build-time generated shader SPIR-V bytecode headers!
#include "color_dualsrc_frag.spv.h"
#include "color_frag.spv.h"
#include "color_vert.spv.h"

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

bool VulkanBackend::CreateRenderPass() {
  vk::SampleCountFlagBits samples =
      static_cast<vk::SampleCountFlagBits>(m_msaaSamples);

  if (m_msaaSamples > 1) {
    // --- MSAA Render Pass Configuration (3 Attachments) ---

    // Attachment 0: Multi-sampled Color Target
    vk::AttachmentDescription colorAttachment(
        {}, m_colorFormat, samples, vk::AttachmentLoadOp::eLoad,
        vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference colorRef(0,
                                     vk::ImageLayout::eColorAttachmentOptimal);

    // Attachment 1: Multi-sampled Depth Target
    vk::AttachmentDescription depthAttachment(
        {}, m_depthFormat, samples, vk::AttachmentLoadOp::eLoad,
        vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::AttachmentReference depthRef(
        1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    // Attachment 2: Single-sampled Resolve Target (our presentation FBO color
    // image)
    vk::AttachmentDescription resolveAttachment(
        {}, m_colorFormat, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference resolveRef(
        2, vk::ImageLayout::eColorAttachmentOptimal);

    // Subpass: renders to colorRef (0) with depthRef (1), and automatically
    // resolves color to resolveRef (2)
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0,
                                   nullptr, 1, &colorRef, &resolveRef,
                                   &depthRef);

    // Setup subpass dependencies to enforce strict GPU synchronization between
    // clear, draw, and copy stages
    std::array<vk::SubpassDependency, 2> dependencies;

    // Dependency 1: Sync Clear/Transfer writes to Color/Depth Attachments
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

    // Dependency 2: Sync Color/Depth Attachment writes to Transfer/Copy reads
    // (in SwapBuffers)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eTransfer;
    dependencies[1].srcAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::AttachmentDescription, 3> attachments = {
        colorAttachment, depthAttachment, resolveAttachment};
    vk::RenderPassCreateInfo passInfo(
        {}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1,
        &subpass, static_cast<uint32_t>(dependencies.size()),
        dependencies.data());
    m_renderPass = m_device->createRenderPassUnique(passInfo);
  } else {
    // --- Standard Render Pass Configuration (2 Attachments, 1x MSAA) ---

    // 1. Color Attachment Description
    vk::AttachmentDescription colorAttachment(
        {}, m_colorFormat, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference colorRef(0,
                                     vk::ImageLayout::eColorAttachmentOptimal);

    // 2. Depth Attachment Description
    vk::AttachmentDescription depthAttachment(
        {}, m_depthFormat, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::AttachmentReference depthRef(
        1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    // 3. Subpass Description (binds both color reference and depth stencil
    // reference)
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0,
                                   nullptr, 1, &colorRef, nullptr, &depthRef);

    // Setup subpass dependencies to enforce strict GPU synchronization between
    // clear, draw, and copy stages
    std::array<vk::SubpassDependency, 2> dependencies;

    // Dependency 1: Sync Clear/Transfer writes to Color/Depth Attachments
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

    // Dependency 2: Sync Color/Depth Attachment writes to Transfer/Copy reads
    // (in SwapBuffers)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eTransfer;
    dependencies[1].srcAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment,
                                                            depthAttachment};
    vk::RenderPassCreateInfo passInfo(
        {}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1,
        &subpass, static_cast<uint32_t>(dependencies.size()),
        dependencies.data());
    m_renderPass = m_device->createRenderPassUnique(passInfo);
  }

  return true;
}

// --- Phase 2: Dynamic Shader Modules & Pipeline State Creation ---
bool VulkanBackend::CreateShaderModules() {
  if (m_vertShaderModule && m_fragShaderModule && m_fragShaderModuleDualSrc) {
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

    GLIDE_LOG(INFO, "Vulkan",
              "Loaded build-time compiled SPIR-V vertex and fragment shader "
              "modules (including dual-source variant).");
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
  key.alphaSrcBlend = m_alphaSrcBlend;
  key.alphaDstBlend = m_alphaDstBlend;
  key.colorMaskRgb = m_colorMaskRgb;
  key.colorMaskAlpha = m_colorMaskAlpha;
  return key;
}

vk::UniquePipeline VulkanBackend::CompileSinglePipeline(
    const PipelineStateKey& key, vk::PrimitiveTopology topology) {
  try {
    // Determine if the pipeline key requires dual-source blending (factor 15 =
    // GR_BLEND_PREFOG_COLOR)
    bool useDualSrcBlend = (key.rgbSrcBlend == 15 || key.rgbDstBlend == 15 ||
                            key.alphaSrcBlend == 15 || key.alphaDstBlend == 15);
    vk::ShaderModule fragShader = useDualSrcBlend
                                      ? m_fragShaderModuleDualSrc.get()
                                      : m_fragShaderModule.get();

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

    // Dynamic Viewport and Scissor States (prevents pipeline rebuilds on
    // resolution change)
    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
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

    // Multisample Configuration
    vk::PipelineMultisampleStateCreateInfo multisampling(
        {}, static_cast<vk::SampleCountFlagBits>(m_msaaSamples), false, 1.0f,
        nullptr, false, false);

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
    depthStencil.stencilTestEnable = false;

    // Color Blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    vk::ColorComponentFlags mask{};
    if (key.colorMaskRgb) {
      mask |= vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
              vk::ColorComponentFlagBits::eB;
    }
    if (key.colorMaskAlpha) {
      mask |= vk::ColorComponentFlagBits::eA;
    }
    colorBlendAttachment.colorWriteMask = mask;

    colorBlendAttachment.blendEnable = true;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcColorBlendFactor =
        MapBlendFactorVulkan(key.rgbSrcBlend, false, false);
    colorBlendAttachment.dstColorBlendFactor =
        MapBlendFactorVulkan(key.rgbDstBlend, true, false);
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
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
    vk::GraphicsPipelineCreateInfo pipelineInfo(
        {}, 2, shaderStages, &vertexInputInfo, &inputAssembly, nullptr,
        &viewportState, &rasterizer, &multisampling, &depthStencil,
        &colorBlending, &dynamicState, m_pipelineLayout.get(),
        m_renderPass.get(), 0, {}, -1);

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

static inline uint32_t PackPipelineCacheKey(const PipelineStateKey& state,
                                            vk::PrimitiveTopology topology) {
  uint32_t key = 0;
  key |= (state.cullMode & 0x3) << 0;
  key |= (state.sstOrigin & 0x1) << 2;
  key |= (state.depthMode & 0x3) << 3;
  key |= (state.depthMask ? 1 : 0) << 5;
  key |= (state.depthCompareOp & 0x7) << 6;
  key |= (state.rgbSrcBlend & 0xF) << 9;
  key |= (state.rgbDstBlend & 0xF) << 13;
  key |= (state.alphaSrcBlend & 0xF) << 17;
  key |= (state.alphaDstBlend & 0xF) << 21;
  key |= (state.colorMaskRgb ? 1 : 0) << 25;
  key |= (state.colorMaskAlpha ? 1 : 0) << 26;
  key |= (static_cast<uint32_t>(topology) & 0x7) << 27;
  return key;
}

vk::Pipeline VulkanBackend::GetOrCreatePipeline(
    const PipelineStateKey& key, vk::PrimitiveTopology topology) {
  PipelineCacheKey cacheKey = PackPipelineCacheKey(key, topology);
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

  if (mipmapMode != 0) {  // GR_MIPMAP_DISABLE is 0
    maxLod = 15.0f;
    if (lodBlend) {
      vkMipmapMode = vk::SamplerMipmapMode::eLinear;
    } else {
      vkMipmapMode = vk::SamplerMipmapMode::eNearest;
    }
  } else {
    maxLod = 0.0f;
    vkMipmapMode = vk::SamplerMipmapMode::eNearest;
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
  samplerInfo.mipLodBias = lodBias;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = maxLod;

  auto sampler = m_device->createSamplerUnique(samplerInfo);
  vk::Sampler res = sampler.get();
  m_samplerCache.emplace(key, std::move(sampler));
  return res;
}

vk::DescriptorSet VulkanBackend::GetOrCreateDescriptorSet(
    vk::ImageView imageView0, vk::Sampler sampler0, vk::ImageView imageView1,
    vk::Sampler sampler1) {
  DescriptorKey key{imageView0, sampler0, imageView1, sampler1};
  auto it = m_descriptorCache.find(key);
  if (it != m_descriptorCache.end()) {
    return it->second.get();
  }

  // Allocate Descriptor Set
  vk::DescriptorSetAllocateInfo setAlloc(m_descriptorPool.get(), 1,
                                         &m_descriptorSetLayout.get());
  auto sets = m_device->allocateDescriptorSetsUnique(setAlloc);
  vk::UniqueDescriptorSet descriptorSet = std::move(sets[0]);
  vk::DescriptorSet rawSet = descriptorSet.get();

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

  m_descriptorCache.emplace(key, std::move(descriptorSet));
  const vk::ImageView dummyView = m_dummyTexture.imageView.get();
  if (imageView0 && imageView0 != dummyView) {
    const uint64_t h0 = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(static_cast<VkImageView>(imageView0)));
    m_viewToDescriptorKeys[h0].push_back(key);
  }
  if (imageView1 && imageView1 != imageView0 && imageView1 != dummyView) {
    const uint64_t h1 = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(static_cast<VkImageView>(imageView1)));
    m_viewToDescriptorKeys[h1].push_back(key);
  }
  return rawSet;
}

}  // namespace GlideWrapper
