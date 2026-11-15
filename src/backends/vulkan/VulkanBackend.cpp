#include "VulkanBackend.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
#include <X11/Xlib.h>
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#include <vulkan/vulkan_xlib.h>
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

// Authoritative Vulkan validation layer declarations
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

const bool enableValidationLayers = false;

void Logger::LogHostVulkanCapabilities() {
  if (GetLevel() < LogLevel::INFO) return;

  try {
    vk::ApplicationInfo appInfo("3dfxGlideWrapper", 1, "AntigravityEngine", 1,
                                VK_API_VERSION_1_2);
    vk::InstanceCreateInfo createInfo({}, &appInfo);

    vk::UniqueInstance instance = vk::createInstanceUnique(createInfo);
    std::vector<vk::PhysicalDevice> devices =
        instance->enumeratePhysicalDevices();

    std::ostringstream ss;
    ss << "\n--- Host Vulkan Execution Adapters Discovered ---\n";
    for (size_t i = 0; i < devices.size(); ++i) {
      vk::PhysicalDeviceProperties props = devices[i].getProperties();
      ss << "  Adapter [" << i << "] : " << props.deviceName.data() << "\n"
         << "    API Version : " << VK_VERSION_MAJOR(props.apiVersion) << "."
         << VK_VERSION_MINOR(props.apiVersion) << "."
         << VK_VERSION_PATCH(props.apiVersion) << "\n"
         << "    Device Type : " << vk::to_string(props.deviceType) << "\n";
    }
    ss << "-------------------------------------------------";
    Log(LogLevel::INFO, "Startup", ss.str());
  } catch (const std::exception& e) {
    Log(LogLevel::WARN, "Startup",
        std::string("Vulkan Host Polling unavailable: ") + e.what());
  }
}

bool VulkanBackend::Initialize(const WrapperConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) return true;

  GLIDE_LOG(INFO, "Vulkan",
            "Initializing Hybrid hardware Vulkan GPU context...");
  m_config = config;
  m_msaaSamples = config.msaaSamples;

  Logger::GetInstance().LogHostVulkanCapabilities();

  if (!CreateInstance()) return false;
  if (!SelectPhysicalDevice()) return false;

  // Resolve and clamp MSAA sample count based on physical device limits
  {
    vk::PhysicalDeviceProperties props = m_physicalDevice.getProperties();
    vk::SampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                  props.limits.framebufferDepthSampleCounts;

    uint32_t maxSupported = 1;
    if (counts & vk::SampleCountFlagBits::e16)
      maxSupported = 16;
    else if (counts & vk::SampleCountFlagBits::e8)
      maxSupported = 8;
    else if (counts & vk::SampleCountFlagBits::e4)
      maxSupported = 4;
    else if (counts & vk::SampleCountFlagBits::e2)
      maxSupported = 2;

    if (m_msaaSamples > maxSupported) {
      GLIDE_LOG(WARN, "Vulkan",
                "Requested MSAA count "
                    << m_msaaSamples << " exceeds hardware limits. Clamping to "
                    << maxSupported);
      m_msaaSamples = maxSupported;
    }
    if (m_msaaSamples < 1) {
      m_msaaSamples = 1;
    }
  }

  if (!CreateLogicalDevice()) return false;

  // Create descriptor set layout
  std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
      vk::DescriptorSetLayoutBinding(
          0, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr),
      vk::DescriptorSetLayoutBinding(
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr)};
  vk::DescriptorSetLayoutCreateInfo layoutInfo(
      {}, static_cast<uint32_t>(bindings.size()), bindings.data());
  m_descriptorSetLayout = m_device->createDescriptorSetLayoutUnique(layoutInfo);

  // Create descriptor pool (raised capacity for complex games)
  std::array<vk::DescriptorPoolSize, 1> poolSizes = {
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 8192)};
  vk::DescriptorPoolCreateInfo poolInfo(
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 4096,
      static_cast<uint32_t>(poolSizes.size()), poolSizes.data());
  m_descriptorPool = m_device->createDescriptorPoolUnique(poolInfo);

  // Create 1x1 dummy white texture
  {
    uint32_t whitePixel = 0xFFFFFFFF;

    vk::ImageCreateInfo imgInfo(
        {}, vk::ImageType::e2D, vk::Format::eB8G8R8A8Unorm,
        vk::Extent3D(1, 1, 1), 1, 1, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferDst);
    m_dummyTexture.image = m_device->createImageUnique(imgInfo);

    auto memReqs =
        m_device->getImageMemoryRequirements(m_dummyTexture.image.get());
    uint32_t typeIndex = FindMemoryType(
        memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
    m_dummyTexture.memory = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindImageMemory(m_dummyTexture.image.get(),
                              m_dummyTexture.memory.get(), 0);

    // Create staging buffer for 4 bytes
    vk::BufferCreateInfo stagingInfo({}, 4,
                                     vk::BufferUsageFlagBits::eTransferSrc);
    auto stagingBuffer = m_device->createBufferUnique(stagingInfo);
    auto stagingReqs =
        m_device->getBufferMemoryRequirements(stagingBuffer.get());
    uint32_t stagingTypeIndex =
        FindMemoryType(stagingReqs.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo stagingAlloc(stagingReqs.size, stagingTypeIndex);
    auto stagingMemory = m_device->allocateMemoryUnique(stagingAlloc);
    m_device->bindBufferMemory(stagingBuffer.get(), stagingMemory.get(), 0);

    void* mapped = m_device->mapMemory(stagingMemory.get(), 0, 4);
    std::memcpy(mapped, &whitePixel, 4);
    m_device->unmapMemory(stagingMemory.get());

    // Command buffer for one-time upload
    vk::CommandBufferAllocateInfo cmdAlloc(m_commandPools[0].get(),
                                           vk::CommandBufferLevel::ePrimary, 1);
    auto cmds = m_device->allocateCommandBuffersUnique(cmdAlloc);
    auto cmd = std::move(cmds[0]);

    cmd->begin(vk::CommandBufferBeginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1);
    TransitionImageLayout(cmd.get(), m_dummyTexture.image.get(),
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, range);

    vk::BufferImageCopy region;
    region.imageSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(1, 1, 1);
    cmd->copyBufferToImage(stagingBuffer.get(), m_dummyTexture.image.get(),
                           vk::ImageLayout::eTransferDstOptimal, 1, &region);

    TransitionImageLayout(cmd.get(), m_dummyTexture.image.get(),
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal, range);

    cmd->end();

    vk::SubmitInfo submit(0, nullptr, nullptr, 1, &cmd.get());
    m_graphicsQueue.submit(submit, {});
    m_device->waitIdle();

    // Image View
    vk::ImageViewCreateInfo viewInfo(
        {}, m_dummyTexture.image.get(), vk::ImageViewType::e2D,
        vk::Format::eB8G8R8A8Unorm, {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_dummyTexture.imageView = m_device->createImageViewUnique(viewInfo);

    // Sampler
    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    m_dummySampler = m_device->createSamplerUnique(samplerInfo);

    // Descriptor Set
    vk::DescriptorSetAllocateInfo setAlloc(m_descriptorPool.get(), 1,
                                           &m_descriptorSetLayout.get());
    auto sets = m_device->allocateDescriptorSets(setAlloc);
    m_dummyDescriptorSet = sets[0];

    std::array<vk::DescriptorImageInfo, 2> imageInfos = {
        vk::DescriptorImageInfo(m_dummySampler.get(),
                                m_dummyTexture.imageView.get(),
                                vk::ImageLayout::eShaderReadOnlyOptimal),
        vk::DescriptorImageInfo(m_dummySampler.get(),
                                m_dummyTexture.imageView.get(),
                                vk::ImageLayout::eShaderReadOnlyOptimal)};
    std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {
        vk::WriteDescriptorSet(m_dummyDescriptorSet, 0, 0, 1,
                               vk::DescriptorType::eCombinedImageSampler,
                               &imageInfos[0], nullptr, nullptr),
        vk::WriteDescriptorSet(m_dummyDescriptorSet, 1, 0, 1,
                               vk::DescriptorType::eCombinedImageSampler,
                               &imageInfos[1], nullptr, nullptr)};
    m_device->updateDescriptorSets(
        static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(),
        0, nullptr);

    GLIDE_LOG(INFO, "Vulkan", "Created 1x1 dummy white texture.");
  }

  if (!CreateRenderPass()) return false;

  m_initialized = true;
  GLIDE_LOG(INFO, "Vulkan", "Vulkan GPU context established successfully.");
  return true;
}

void VulkanBackend::Shutdown() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) return;

  GLIDE_LOG(INFO, "Vulkan", "Shutting down Hybrid Vulkan context.");

  // Clear sampler and descriptor caches
  m_samplerCache.clear();
  m_descriptorCache.clear();

  // 1. Force-detach window resources first (handles active frame command
  // buffer, SDL, etc.)
  DetachWindow_Locked();
  DestroyHudResources_Locked();

  // 2. Explicitly destroy all device-owned pipelines, shaders, and caches
  m_pipelineCache.clear();
  m_pipelineLayout.reset();
  m_vertShaderModule.reset();
  m_fragShaderModule.reset();
  m_fragShaderModuleDualSrc.reset();

  // Reset descriptor layout, pool, and textures
  m_vulkanTextures.clear();
  m_dummyTexture.imageView.reset();
  m_dummySampler.reset();
  m_dummyTexture.memory.reset();
  m_dummyTexture.image.reset();
  m_descriptorPool.reset();
  m_descriptorSetLayout.reset();

  // 3. Explicitly destroy all device-owned buffers and memories
  m_vertexBuffer.reset();
  m_vertexBufferMemory.reset();
  for (int i = 0; i < 2; ++i) {
    m_headlessPixelBuffers[i].reset();
    m_headlessPixelMemories[i].reset();
  }

  // 4. Explicitly destroy all device-owned image resources (Double-Buffered)
  for (int i = 0; i < 2; ++i) {
    m_headlessFramebuffers[i].reset();
    m_headlessImageViews[i].reset();
    m_headlessImageMemories[i].reset();
    m_headlessImages[i].reset();
  }

  m_depthImageView.reset();
  m_depthImageMemory.reset();
  m_depthImage.reset();

  // Explicitly destroy MSAA resources
  for (int i = 0; i < 2; ++i) {
    m_msaaColorImageViews[i].reset();
    m_msaaColorImageMemories[i].reset();
    m_msaaColorImages[i].reset();
  }
  m_msaaDepthImageView.reset();
  m_msaaDepthImageMemory.reset();
  m_msaaDepthImage.reset();

  // Explicitly destroy Swapchain and Presentation Surface resources
  m_swapchainImageViews.clear();
  m_swapchain.reset();
  m_surface.reset();

  // Explicitly destroy presentation synchronization semaphores
  for (int i = 0; i < 2; ++i) {
    m_imageAvailableSemaphores[i].reset();
  }
  m_renderFinishedSemaphores.clear();

  // 2.5. Save persistent Vulkan pipeline cache back to disk!
  if (m_device && m_vkPipelineCache) {
    const std::string& cachePath =
        m_config.vkCachePath.empty() ? std::string(".glide-vulkan-cache.bin")
                                     : m_config.vkCachePath;

    try {
      auto data = m_device->getPipelineCacheData(m_vkPipelineCache.get());
      if (!data.empty()) {
        std::ofstream cacheFile(cachePath, std::ios::binary | std::ios::trunc);
        if (cacheFile.is_open()) {
          cacheFile.write(reinterpret_cast<const char*>(data.data()),
                          data.size());
          cacheFile.close();
          GLIDE_LOG(INFO, "Vulkan",
                    "Successfully serialized persistent pipeline cache binary ("
                        << data.size() << " bytes) to " << cachePath);
        } else {
          GLIDE_LOG(
              WARN, "Vulkan",
              "Failed to open pipeline cache file for writing: " << cachePath);
        }
      }
    } catch (const std::exception& e) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to retrieve or save pipeline cache data: " << e.what());
    }
  }

  // 5. Reset command submission and render pass infrastructure
  // (Double-Buffered)
  m_commandBuffer = nullptr;
  for (int i = 0; i < 2; ++i) {
    m_commandBuffers[i].reset();
    m_commandPools[i].reset();
    m_fences[i].reset();
  }
  m_renderPass.reset();
  m_vkPipelineCache.reset();

  if (m_device && m_texStagingMap) {
    m_device->unmapMemory(m_texStagingMemory.get());
    m_texStagingMap = nullptr;
  }
  m_texStagingBuffer.reset();
  m_texStagingMemory.reset();

  // 6. Now it is 100% safe to destroy the logical device and instance!
  m_device.reset();
  m_instance.reset();

  m_initialized = false;
}

void VulkanBackend::ResetState() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetState_Locked();
}

void VulkanBackend::ResetState_Locked() {
  GlideStateTracker::ResetState_Locked();
  m_lfbStaging.ResetPendingAndDirty();
}

bool VulkanBackend::AttachWindow(void* nativeWindowHandle, uint32_t width,
                                 uint32_t height, bool windowed) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) return false;
  if (m_windowAttached) DetachWindow_Locked();

  GLIDE_LOG(INFO, "Vulkan",
            "Attaching Hybrid Vulkan window ("
                << width << "x" << height
                << "), Windowed=" << (windowed ? "Yes" : "No"));

  m_headlessWidth = width;
  m_headlessHeight = height;
  m_guestWidth = width;
  m_guestHeight = height;

  ResetState_Locked();
  m_headlessMode = true;  // Headless-driven for visual regression

  if (!CreateHeadlessRenderTarget(width, height)) return false;

  // Phase 2: Compile Shaders & Graphics Pipelines
  if (!CreateShaderModules()) {
    GLIDE_LOG(CRITICAL, "Vulkan", "Failed to compile Vulkan shader modules!");
    return false;
  }

  // Pipeline Pre-warming (Warming the cache to prevent mid-frame shader
  // compilation stutters!)
  GLIDE_LOG(
      INFO, "Vulkan",
      "Pre-warming Vulkan pipeline cache with common rendering states...");
  {
    std::vector<PipelineStateKey> warmStates;

    // State 1: Default opaque 3D rendering (both lower-left and upper-left
    // origins, cull negative and disabled) Warm 4 pipelines for common 3D
    // rendering
    for (uint32_t origin : {0u, 1u}) {
      for (uint32_t cull : {0u, 1u}) {
        PipelineStateKey k{};
        k.cullMode = cull;
        k.sstOrigin = origin;
        k.depthMode = 1;  // Enabled
        k.depthMask = true;
        k.depthCompareOp = 3;  // LessEqual
        k.rgbSrcBlend = 4;     // GR_BLEND_ONE
        k.rgbDstBlend = 0;     // GR_BLEND_ZERO
        k.alphaSrcBlend = 4;   // GR_BLEND_ONE
        k.alphaDstBlend = 0;   // GR_BLEND_ZERO
        k.colorMaskRgb = true;
        k.colorMaskAlpha = true;
        warmStates.push_back(k);
      }
    }

    // State 2: Standard Translucent Text/UI (e.g. tlConRender - 2 pipelines)
    // Note: Separate Alpha factors (RGB: SrcAlpha/OneMinusSrcAlpha, Alpha:
    // One/Zero)
    for (uint32_t origin : {0u, 1u}) {
      PipelineStateKey k{};
      k.cullMode = 0;  // None
      k.sstOrigin = origin;
      k.depthMode = 0;  // Disabled (canonicalized)
      k.depthMask = false;
      k.depthCompareOp = 0;
      k.rgbSrcBlend = 1;    // GR_BLEND_SRC_ALPHA
      k.rgbDstBlend = 5;    // GR_BLEND_ONE_MINUS_SRC_ALPHA
      k.alphaSrcBlend = 4;  // GR_BLEND_ONE (separate Alpha factor!)
      k.alphaDstBlend = 0;  // GR_BLEND_ZERO
      k.colorMaskRgb = true;
      k.colorMaskAlpha = true;
      warmStates.push_back(k);
    }

    // State 3: Opaque 2D Overlays/Backgrounds (2 pipelines)
    for (uint32_t origin : {0u, 1u}) {
      PipelineStateKey k{};
      k.cullMode = 0;  // None
      k.sstOrigin = origin;
      k.depthMode = 0;  // Disabled (canonicalized)
      k.depthMask = false;
      k.depthCompareOp = 0;
      k.rgbSrcBlend = 4;    // GR_BLEND_ONE
      k.rgbDstBlend = 0;    // GR_BLEND_ZERO
      k.alphaSrcBlend = 4;  // GR_BLEND_ONE
      k.alphaDstBlend = 0;  // GR_BLEND_ZERO
      k.colorMaskRgb = true;
      k.colorMaskAlpha = true;
      warmStates.push_back(k);
    }

    // Warm all 8 of them for Triangle List topology (which is 99% of drawing)
    for (const auto& state : warmStates) {
      GetOrCreatePipeline(state, vk::PrimitiveTopology::eTriangleList);
    }
    GLIDE_LOG(
        INFO, "Vulkan",
        "Pipeline cache pre-warming completed successfully. Total warmed: "
            << warmStates.size());
  }

  // Phase 2: Allocate dynamic ring Vertex Buffer (1MB)
  try {
    vk::BufferCreateInfo bufferInfo({}, m_vertexBufferSize,
                                    vk::BufferUsageFlagBits::eVertexBuffer);
    m_vertexBuffer = m_device->createBufferUnique(bufferInfo);

    auto memReqs = m_device->getBufferMemoryRequirements(m_vertexBuffer.get());
    uint32_t typeIndex = FindMemoryType(
        memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);

    m_vertexBufferMemory = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindBufferMemory(m_vertexBuffer.get(), m_vertexBufferMemory.get(),
                               0);

    m_vertexBufferMap =
        m_device->mapMemory(m_vertexBufferMemory.get(), 0, m_vertexBufferSize);
    m_vertexBufferOffset = 0;
    m_batchedPipeline = nullptr;
    m_batchedDescSet = nullptr;
    m_batchedVertexCount = 0;
    m_batchedFirstVertex = 0;
    GLIDE_LOG(
        INFO, "Vulkan",
        "Allocated " << (m_vertexBufferSize / (1024 * 1024))
                     << "MB host-visible dynamic ring vertex buffer and reset "
                        "batching state.");
  } catch (const std::exception& e) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Failed to allocate Vulkan vertex buffer: " << e.what());
    return false;
  }

  // Query GPU details
  auto props = m_physicalDevice.getProperties();
  std::string devName(props.deviceName);

  std::string depthStr = "D24S0";
  if (m_depthFormat == vk::Format::eD32Sfloat) {
    depthStr = "D32S0";
  } else if (m_depthFormat == vk::Format::eD24UnormS8Uint) {
    depthStr = "D24S8";
  }
  uint32_t samplesVal = (m_msaaSamples > 1) ? m_msaaSamples : 0;

  std::cout << "Info: InitialiseVulkanWindow(wnd=" << nativeWindowHandle
            << ", res=" << width << "x" << height << ")\n";
  std::cout << "Info: Host Vulkan Adapter: " << devName << " (Vulkan API "
            << (props.apiVersion >> 22) << "."
            << ((props.apiVersion >> 12) & 0x3FF) << "."
            << (props.apiVersion & 0xFFF) << ")\n";
  std::cout << "Info: Pixel Format RGBA8888 " << depthStr << " nAux 0 nSamples "
            << samplesVal << " " << samplesVal << "\n";
  std::cout << "Info: Drawable Size: " << width << "x" << height << "\n"
            << std::flush;

  SetClipWindow_Locked(0, 0, width, height);

  // Configure M3 on-screen HUD before swapchain creation so swapchain
  // framebuffers can lazily build the HUD pass only when enabled.
  m_hudOverlay.Configure(m_config, {"VK", width, height});

  // Unconditionally render to offscreen guest targets and present via blit.
  m_headlessMode = true;

  if (!m_surfaceHost.Attach(nativeWindowHandle, width, height, windowed,
                            m_config.forceNoWindow,
                            "3dfx glide-ng Presentation Console (Vulkan)",
                            SDL_WINDOW_VULKAN, m_config.windowScale)) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Failed to attach NativeSurfaceHost for Vulkan presentation.");
    return false;
  }

  // Create Vulkan WSI surface and swapchain when not in Headless mode.
  if (m_surfaceHost.GetMode() != NativeSurfaceHost::Mode::Headless) {
    if (!CreatePresentationSurface()) {
      m_surfaceHost.Detach();
      return false;
    }

    uint32_t surfW = width;
    uint32_t surfH = height;
    m_surfaceHost.GetDrawableSize(surfW, surfH);
    if (surfW == 0 || surfH == 0) {
      surfW = width;
      surfH = height;
    }

    if (!CreateSwapchain(surfW, surfH)) {
      GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan swapchain!");
      m_surface.reset();
      m_surfaceHost.Detach();
      return false;
    }
  }

  // Begin recording our persistent frame command buffer!
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  // One-time layout transition and initial clear of color images to transparent
  // Black (0x00000000) to ensure a clean, deterministic initial state
  GLIDE_LOG(INFO, "Vulkan",
            "Performing one-time startup layout transitions and clearing "
            "render targets...");
  vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                       1);
  vk::ClearColorValue clearZero(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
  for (int i = 0; i < 2; ++i) {
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[i].get(),
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, colorRange);
    m_commandBuffer->clearColorImage(m_headlessImages[i].get(),
                                     vk::ImageLayout::eTransferDstOptimal,
                                     &clearZero, 1, &colorRange);
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[i].get(),
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eColorAttachmentOptimal, colorRange);
  }

  vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                                       1);
  TransitionImageLayout(
      *m_commandBuffer, m_depthImage.get(), vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal, depthRange);

  m_commandBuffer->end();
  vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
  m_graphicsQueue.submit(submitInfo, {});
  m_device->waitIdle();  // Block until transitions complete

  // Restart command buffer for frame recording
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  m_windowAttached = true;
  return true;
}

void VulkanBackend::DetachWindow() {
  std::lock_guard<std::mutex> lock(m_mutex);
  DetachWindow_Locked();
}

void VulkanBackend::DetachWindow_Locked() {
  if (!m_windowAttached) return;
  GLIDE_LOG(INFO, "Vulkan", "Detaching Hybrid Vulkan window resources.");

  // Strict WSI teardown order:
  // 1. Wait for GPU idle
  // 2. Destroy swapchain
  // 3. Destroy VkSurfaceKHR
  // 4. Detach NativeSurfaceHost (destroys X11 child / wl_subsurface / SDL window)
  if (m_device) {
    m_device->waitIdle();
  }

  DestroySwapchain_Locked();
  DestroyHudResources_Locked();
  m_hudOverlay.Reset();
  m_surface.reset();
  m_surfaceHost.Detach();

  // Phase 2: Unmap and reset vertex buffer
  if (m_vertexBufferMap) {
    m_device->unmapMemory(m_vertexBufferMemory.get());
    m_vertexBufferMap = nullptr;
  }
  m_vertexBufferMemory.reset();
  m_vertexBuffer.reset();

  for (uint32_t i = 0; i < 2; ++i) {
    if (m_gpuStagingMaps[i]) {
      m_device->unmapMemory(m_headlessPixelMemories[i].get());
      m_gpuStagingMaps[i] = nullptr;
    }
    m_headlessPixelBuffers[i].reset();
    m_headlessPixelMemories[i].reset();
  }
  m_lfbStaging.Release();
  for (int i = 0; i < 2; ++i) {
    m_headlessFramebuffers[i].reset();
    m_headlessImageViews[i].reset();
    m_headlessImageMemories[i].reset();
    m_headlessImages[i].reset();
  }

  // Explicitly reset depth buffer resources to prevent leaks and shutdown
  // crashes!
  m_depthImageView.reset();
  m_depthImageMemory.reset();
  m_depthImage.reset();

  // Explicitly reset MSAA resources
  for (int i = 0; i < 2; ++i) {
    m_msaaColorImageViews[i].reset();
    m_msaaColorImageMemories[i].reset();
    m_msaaColorImages[i].reset();
  }
  m_msaaDepthImageView.reset();
  m_msaaDepthImageMemory.reset();
  m_msaaDepthImage.reset();

  m_windowAttached = false;
  m_headlessMode = false;
}

bool VulkanBackend::CreatePresentationSurface() {
  m_surface.reset();
  switch (m_surfaceHost.GetMode()) {
    case NativeSurfaceHost::Mode::Detached:
    case NativeSurfaceHost::Mode::Headless:
      return true;

    case NativeSurfaceHost::Mode::X11Child: {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
      auto createXlibSurface = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
          m_instance->getProcAddr("vkCreateXlibSurfaceKHR"));
      if (!createXlibSurface) {
        GLIDE_LOG(CRITICAL, "Vulkan",
                  "vkCreateXlibSurfaceKHR is unavailable on Vulkan instance.");
        return false;
      }

      VkXlibSurfaceCreateInfoKHR info{};
      info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
      info.dpy = static_cast<Display*>(m_surfaceHost.GetX11Display());
      info.window = static_cast<Window>(m_surfaceHost.GetX11Window());

      VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
      VkResult res = createXlibSurface(
          static_cast<VkInstance>(m_instance.get()), &info, nullptr,
          &rawSurface);
      if (res != VK_SUCCESS || rawSurface == VK_NULL_HANDLE) {
        GLIDE_LOG(CRITICAL, "Vulkan",
                  "vkCreateXlibSurfaceKHR failed with VkResult=" << res);
        return false;
      }
      m_surface = vk::UniqueSurfaceKHR(rawSurface, m_instance.get());
      GLIDE_LOG(INFO, "Vulkan",
                "Created VK_KHR_xlib_surface on X11 child window 0x"
                    << std::hex << m_surfaceHost.GetX11Window() << std::dec);
      return true;
#else
      GLIDE_LOG(CRITICAL, "Vulkan",
                "X11Child surface requested but HAVE_X11 is not compiled in.");
      return false;
#endif
    }

    case NativeSurfaceHost::Mode::WaylandSubsurface: {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
      auto createWaylandSurface =
          reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
              m_instance->getProcAddr("vkCreateWaylandSurfaceKHR"));
      if (!createWaylandSurface) {
        GLIDE_LOG(
            CRITICAL, "Vulkan",
            "vkCreateWaylandSurfaceKHR is unavailable on Vulkan instance.");
        return false;
      }

      VkWaylandSurfaceCreateInfoKHR info{};
      info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
      info.display = static_cast<wl_display*>(m_surfaceHost.GetWaylandDisplay());
      info.surface = static_cast<wl_surface*>(m_surfaceHost.GetWaylandSurface());

      VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
      VkResult res = createWaylandSurface(
          static_cast<VkInstance>(m_instance.get()), &info, nullptr,
          &rawSurface);
      if (res != VK_SUCCESS || rawSurface == VK_NULL_HANDLE) {
        GLIDE_LOG(CRITICAL, "Vulkan",
                  "vkCreateWaylandSurfaceKHR failed with VkResult=" << res);
        return false;
      }
      m_surface = vk::UniqueSurfaceKHR(rawSurface, m_instance.get());
      GLIDE_LOG(INFO, "Vulkan",
                "Created VK_KHR_wayland_surface on Wayland subsurface "
                    << m_surfaceHost.GetWaylandSurface());
      return true;
#else
      GLIDE_LOG(
          CRITICAL, "Vulkan",
          "WaylandSubsurface requested but HAVE_WAYLAND is not compiled in.");
      return false;
#endif
    }

    case NativeSurfaceHost::Mode::StandaloneSDL2: {
      VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
      if (SDL_Vulkan_CreateSurface(m_surfaceHost.GetSDLWindow(),
                                   static_cast<VkInstance>(m_instance.get()),
                                   &rawSurface) != SDL_TRUE) {
        GLIDE_LOG(CRITICAL, "Vulkan",
                  "SDL_Vulkan_CreateSurface failed for StandaloneSDL2: "
                      << SDL_GetError());
        return false;
      }
      m_surface = vk::UniqueSurfaceKHR(rawSurface, m_instance.get());
      GLIDE_LOG(INFO, "Vulkan",
                "Created Vulkan surface for StandaloneSDL2 window "
                    << m_surfaceHost.GetSDLWindow());
      return true;
    }
  }
  return false;
}

vk::PresentModeKHR VulkanBackend::SelectPresentMode(
    const std::vector<vk::PresentModeKHR>& availableModes, bool vsyncEnabled,
    bool isWaylandSurface, bool forceStrictFifo) {
  bool foundMailbox = false;
  bool foundImmediate = false;
  for (const auto& pm : availableModes) {
    if (pm == vk::PresentModeKHR::eMailbox) {
      foundMailbox = true;
    } else if (pm == vk::PresentModeKHR::eImmediate) {
      foundImmediate = true;
    }
  }

  if (!vsyncEnabled) {
    if (foundMailbox) return vk::PresentModeKHR::eMailbox;
    if (foundImmediate) return vk::PresentModeKHR::eImmediate;
    return vk::PresentModeKHR::eFifo;
  }

  if (isWaylandSurface && !forceStrictFifo && foundMailbox) {
    return vk::PresentModeKHR::eMailbox;
  }

  return vk::PresentModeKHR::eFifo;
}

bool VulkanBackend::CreateSwapchain(uint32_t width, uint32_t height) {
  if (!m_surface) {
    GLIDE_LOG(WARN, "Vulkan",
              "Cannot create swapchain: no presentation surface available.");
    return false;
  }

  // Verify that our graphics queue family supports presentation to the surface!
  vk::Bool32 presentSupport = false;
  vk::Result supportRes = m_physicalDevice.getSurfaceSupportKHR(
      m_graphicsQueueFamilyIndex, m_surface.get(), &presentSupport);
  if (supportRes != vk::Result::eSuccess || !presentSupport) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Graphics queue family index "
                  << m_graphicsQueueFamilyIndex
                  << " does NOT support surface presentation! Res="
                  << vk::to_string(supportRes));
    return false;
  }

  // 1. Query surface capabilities, formats, and present modes
  vk::SurfaceCapabilitiesKHR capabilities =
      m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface.get());
  std::vector<vk::SurfaceFormatKHR> formats =
      m_physicalDevice.getSurfaceFormatsKHR(m_surface.get());
  std::vector<vk::PresentModeKHR> presentModes =
      m_physicalDevice.getSurfacePresentModesKHR(m_surface.get());

  if (formats.empty()) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "No supported surface formats found for Vulkan presentation.");
    return false;
  }

  // 2. Select the best surface format (UNORM preferred for authentic retro
  // linear presentation without double gamma correction!)
  vk::SurfaceFormatKHR selectedFormat = formats[0];
  bool found_unorm = false;
  for (const auto& f : formats) {
    if ((f.format == vk::Format::eB8G8R8A8Unorm ||
         f.format == vk::Format::eR8G8B8A8Unorm) &&
        f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      selectedFormat = f;
      found_unorm = true;
      break;
    }
  }
  if (!found_unorm) {
    for (const auto& f : formats) {
      if ((f.format == vk::Format::eB8G8R8A8Srgb ||
           f.format == vk::Format::eR8G8B8A8Srgb) &&
          f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
        selectedFormat = f;
        break;
      }
    }
  }
  m_swapchainImageFormat = selectedFormat.format;
  GLIDE_LOG(
      INFO, "Vulkan",
      "Selected swapchain format: " << vk::to_string(m_swapchainImageFormat));

  // 3. Select the best present mode.
  // On Wayland, prefer Mailbox even with VSync enabled: Wayland compositors
  // always composite atomically (tear-free), while Mailbox avoids Mesa WSI's
  // wp_fifo_v1 / wp_commit_timer_v1 cadence stalls under Mutter/RDP. Users can
  // still opt into strict FIFO via GLIDE_NG_VSYNC=fifo.
  bool isWaylandSurface = m_surfaceHost.IsWaylandSurface();
  bool forceStrictFifo = m_config.vsyncStrictFifo;

  vk::PresentModeKHR presentMode = SelectPresentMode(
      presentModes, m_config.vsync, isWaylandSurface, forceStrictFifo);

  if (presentMode == vk::PresentModeKHR::eMailbox) {
    if (m_config.vsync && isWaylandSurface) {
      GLIDE_LOG(INFO, "Vulkan",
                "Wayland surface detected with VSync enabled: using Mailbox "
                "present mode (tear-free via Wayland compositor, avoids "
                "wp_fifo_v1 stalls; set GLIDE_NG_VSYNC=fifo to force FIFO).");
    } else {
      GLIDE_LOG(
          INFO, "Vulkan",
          "VSync Disabled: Using Mailbox present mode (Uncapped, no tearing).");
    }
  } else if (presentMode == vk::PresentModeKHR::eImmediate) {
    GLIDE_LOG(INFO, "Vulkan",
              "VSync Disabled: Using Immediate present mode (Uncapped, "
              "screen tearing may occur).");
  } else if (!m_config.vsync) {
    GLIDE_LOG(WARN, "Vulkan",
              "VSync Disabled: Uncapped present modes not supported by GPU, "
              "falling back to FIFO.");
  } else {
    GLIDE_LOG(INFO, "Vulkan",
              "VSync Enabled: Using FIFO present mode (Sync to vertical "
              "blanking, locked to refresh rate).");
  }

  // 4. Determine swapchain extent (size)
  vk::Extent2D extent;
  if (capabilities.currentExtent.width != UINT32_MAX) {
    extent = capabilities.currentExtent;
  } else {
    extent.width = std::clamp(width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0) {
    GLIDE_LOG(WARN, "Vulkan",
              "Deferring swapchain creation for zero-sized surface extent.");
    return false;
  }
  m_swapchainExtent = extent;
  GLIDE_LOG(
      INFO, "Vulkan",
      "Selected swapchain extent: " << extent.width << "x" << extent.height);

  // 5. Determine number of images (double or triple buffering)
  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }
  GLIDE_LOG(INFO, "Vulkan",
            "Creating swapchain with " << imageCount << " images.");

  // Pick a supported composite alpha mode (prefer opaque so letterbox bars
  // are solid black and Wayland compositors don't blend behind the subsurface).
  vk::CompositeAlphaFlagBitsKHR compositeAlpha =
      vk::CompositeAlphaFlagBitsKHR::eOpaque;
  if (!(capabilities.supportedCompositeAlpha &
        vk::CompositeAlphaFlagBitsKHR::eOpaque)) {
    if (capabilities.supportedCompositeAlpha &
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied) {
      compositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
    } else if (capabilities.supportedCompositeAlpha &
               vk::CompositeAlphaFlagBitsKHR::eInherit) {
      compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
    }
  }

  // 6. Create the Swapchain!
  vk::SwapchainCreateInfoKHR createInfo(
      {}, m_surface.get(), imageCount, m_swapchainImageFormat,
      selectedFormat.colorSpace, m_swapchainExtent,
      1,  // imageArrayLayers
      vk::ImageUsageFlagBits::eColorAttachment |
          vk::ImageUsageFlagBits::eTransferDst,  // We will clear/blit to it!
      vk::SharingMode::eExclusive, 0,
      nullptr,  // Queue family indices (exclusive mode doesn't need them)
      capabilities.currentTransform, compositeAlpha,
      presentMode,
      VK_TRUE,  // clipped
      {}        // oldSwapchain
  );

  m_swapchain = m_device->createSwapchainKHRUnique(createInfo);

  // 7. Retrieve swapchain images
  m_swapchainImages = m_device->getSwapchainImagesKHR(m_swapchain.get());

  // 8. Create Image Views for each swapchain image
  m_swapchainImageViews.clear();
  m_swapchainImageViews.reserve(m_swapchainImages.size());
  for (const auto& img : m_swapchainImages) {
    vk::ImageViewCreateInfo viewInfo(
        {}, img, vk::ImageViewType::e2D, m_swapchainImageFormat,
        {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
         vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_swapchainImageViews.push_back(m_device->createImageViewUnique(viewInfo));
  }

  // 9. Create synchronization semaphores for swapchain image acquisition &
  // presentation pacing (renderFinished is sized per swapchain image!)
  vk::SemaphoreCreateInfo semaphoreInfo;
  for (int i = 0; i < 2; ++i) {
    m_imageAvailableSemaphores[i] =
        m_device->createSemaphoreUnique(semaphoreInfo);
  }
  m_renderFinishedSemaphores.clear();
  m_renderFinishedSemaphores.reserve(m_swapchainImages.size());
  for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
    m_renderFinishedSemaphores.push_back(
        m_device->createSemaphoreUnique(semaphoreInfo));
  }

  if (m_hudOverlay.IsEnabled()) {
    CreateHudSwapchainFramebuffers_Locked();
  }

  GLIDE_LOG(INFO, "Vulkan",
            "Vulkan Swapchain and " << m_swapchainImageViews.size()
                                    << " image views successfully allocated.");
  return true;
}

void VulkanBackend::DestroySwapchain_Locked() {
  if (m_device) {
    m_device->waitIdle();
  }
  DestroyHudSwapchainFramebuffers_Locked();
  for (int i = 0; i < 2; ++i) {
    if (m_imageAvailableSemaphores[i]) m_imageAvailableSemaphores[i].reset();
  }
  m_renderFinishedSemaphores.clear();
  m_swapchainImageViews.clear();
  m_swapchainImages.clear();
  if (m_swapchain) m_swapchain.reset();
  GLIDE_LOG(INFO, "Vulkan",
            "Vulkan Swapchain resources successfully released.");
}

void VulkanBackend::RecreateSwapchain(uint32_t width, uint32_t height) {
  if (!m_device || !m_surface || width == 0 || height == 0) return;
  m_device->waitIdle();
  GLIDE_LOG(INFO, "Vulkan",
            "Recreating swapchain with size: " << width << "x" << height);
  DestroySwapchain_Locked();
  CreateSwapchain(width, height);
}

void VulkanBackend::SetDepthState(uint32_t depthMode, uint32_t compareOp,
                                  bool depthMask, int32_t biasLevel) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (depthMode != m_depthMode || compareOp != m_depthCompareOp ||
      depthMask != m_depthMask || biasLevel != m_depthBiasLevel) {
    FlushBatch(true);
  }
  GlideStateTracker::SetDepthState_Locked(depthMode, compareOp, depthMask, biasLevel);
}

void VulkanBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                                  uint32_t alphaSrcFactor,
                                  uint32_t alphaDstFactor) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (rgbSrcFactor != m_rgbSrcBlend || rgbDstFactor != m_rgbDstBlend ||
      alphaSrcFactor != m_alphaSrcBlend || alphaDstFactor != m_alphaDstBlend) {
    FlushBatch(true);
  }
  GlideStateTracker::SetBlendState_Locked(rgbSrcFactor, rgbDstFactor, alphaSrcFactor,
                                   alphaDstFactor);
}

void VulkanBackend::SetCullState(uint32_t cullMode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (cullMode != m_cullMode) {
    FlushBatch(true);
  }
  GlideStateTracker::SetCullState_Locked(cullMode);
}

void VulkanBackend::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (compareOp != m_alphaTestOp || refVal != m_alphaTestRefVal) {
    FlushBatch(true);
  }
  GlideStateTracker::SetAlphaTestState_Locked(compareOp, refVal);
  // Note: Emulated in fragment shader via push constants. No pipeline
  // recompilation needed!
}

void VulkanBackend::SetSstOrigin(uint32_t origin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (origin != m_sstOrigin) {
    FlushBatch(true);
  }
  GlideStateTracker::SetSstOrigin_Locked(origin);
}

void VulkanBackend::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX,
                                  uint32_t maxY) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (minX != m_clipMinX || minY != m_clipMinY || maxX != m_clipMaxX ||
      maxY != m_clipMaxY) {
    FlushBatch(true);
  }
  GlideStateTracker::SetClipWindow_Locked(minX, minY, maxX, maxY);

  if (m_initialized && m_device && m_inRenderPass) {
    vk::Rect2D scissor = GetScissorRectVulkan_Locked();
    m_commandBuffer->setScissor(0, 1, &scissor);
  }
}

void VulkanBackend::SetColorMask(bool rgb, bool alpha) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (rgb != m_colorMaskRgb || alpha != m_colorMaskAlpha) {
    FlushBatch(true);
  }
  GlideStateTracker::SetColorMask_Locked(rgb, alpha);
}

void VulkanBackend::SetDepthRange(float nearVal, float farVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (nearVal != m_depthNear || farVal != m_depthFar) {
    FlushBatch(true);
  }
  GlideStateTracker::SetDepthRange_Locked(nearVal, farVal);
}

void VulkanBackend::SetDitherMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (mode != m_ditherMode) {
    FlushBatch(true);
  }
  GlideStateTracker::SetDitherMode_Locked(mode);
}

void VulkanBackend::SetStippleState(uint32_t mode, uint32_t pattern) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (mode != m_stippleMode || pattern != m_stipplePattern) {
    FlushBatch(true);
  }
  GlideStateTracker::SetStippleState_Locked(mode, pattern);
}

void VulkanBackend::SetConstantColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (color != m_constantColor) {
    FlushBatch(true);
  }
  GlideStateTracker::SetConstantColor_Locked(color);
}

void VulkanBackend::SetRenderBuffer(uint32_t target) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (target != m_activeRenderBuffer) {
    FlushBatch(true);
  }
  GlideStateTracker::SetRenderBuffer_Locked(target);
}

void VulkanBackend::SetPixelFormat(uint32_t format) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (format != m_pixelFormatOverride) {
    FlushBatch(true);
  }
  GlideStateTracker::SetPixelFormat_Locked(format);
}

void VulkanBackend::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor,
                                    uint32_t colorLocal, uint32_t colorOther,
                                    bool colorInvert, uint32_t alphaFunc,
                                    uint32_t alphaFactor, uint32_t alphaLocal,
                                    uint32_t alphaOther, bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (colorFunc != m_colorCombinerFunc ||
      colorFactor != m_colorCombinerFactor ||
      colorLocal != m_colorCombinerLocal ||
      colorOther != m_colorCombinerOther ||
      colorInvert != m_colorCombinerInvert ||
      alphaFunc != m_alphaCombinerFunc ||
      alphaFactor != m_alphaCombinerFactor ||
      alphaLocal != m_alphaCombinerLocal ||
      alphaOther != m_alphaCombinerOther ||
      alphaInvert != m_alphaCombinerInvert) {
    FlushBatch(true);
  }
  GlideStateTracker::SetCombinerMode_Locked(colorFunc, colorFactor, colorLocal,
                                     colorOther, colorInvert, alphaFunc,
                                     alphaFactor, alphaLocal, alphaOther,
                                     alphaInvert);
}

void VulkanBackend::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc,
                                       uint32_t rgbFactor, uint32_t alphaFunc,
                                       uint32_t alphaFactor, bool rgbInvert,
                                       bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && (rgbFunc != m_texCombinerRgbFunc[tmu] ||
                  rgbFactor != m_texCombinerRgbFactor[tmu] ||
                  alphaFunc != m_texCombinerAlphaFunc[tmu] ||
                  alphaFactor != m_texCombinerAlphaFactor[tmu] ||
                  rgbInvert != m_texCombinerRgbInvert[tmu] ||
                  alphaInvert != m_texCombinerAlphaInvert[tmu])) {
    FlushBatch(true);
  }
  GlideStateTracker::SetTexCombinerMode_Locked(tmu, rgbFunc, rgbFactor, alphaFunc,
                                        alphaFactor, rgbInvert, alphaInvert);
}

void VulkanBackend::SetSTWHintState(uint32_t hintMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (hintMask != m_stwHintMask) {
    FlushBatch(true);
  }
  GlideStateTracker::SetSTWHintState_Locked(hintMask);
}

void VulkanBackend::SetChromakeyMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (mode != m_chromakeyMode) {
    FlushBatch(true);
  }
  GlideStateTracker::SetChromakeyMode_Locked(mode);
}

void VulkanBackend::SetChromakeyValue(uint32_t value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (value != m_chromakeyValue) {
    FlushBatch(true);
  }
  GlideStateTracker::SetChromakeyValue_Locked(value);
}

void VulkanBackend::SetChromakeyRange(uint32_t minColor, uint32_t maxColor,
                                      uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (minColor != m_chromakeyRangeMin || maxColor != m_chromakeyRangeMax ||
      mode != m_chromakeyRangeMode) {
    FlushBatch(true);
  }
  GlideStateTracker::SetChromakeyRange_Locked(minColor, maxColor, mode);
}

void VulkanBackend::SetFogMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (mode != m_fogMode) {
    FlushBatch(true);
  }
  GlideStateTracker::SetFogMode_Locked(mode);
}

void VulkanBackend::SetFogColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (color != m_fogColor) {
    FlushBatch(true);
  }
  GlideStateTracker::SetFogColor_Locked(color);
}

void VulkanBackend::SetFogTable(const uint8_t* table) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (table && std::memcmp(table, m_fogTable, 64) != 0) {
    FlushBatch(true);
  }
  GlideStateTracker::SetFogTable_Locked(table);
}

bool VulkanBackend::SwapBuffers() {
  std::unique_lock<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  // Pump SDL events ONLY in StandaloneSDL2 mode (guaranteed no-op for DOSBox
  // X11Child / WaylandSubsurface / Headless).
  m_surfaceHost.PollEvents(/*force=*/true);
  if (m_surfaceHost.GetMode() == NativeSurfaceHost::Mode::StandaloneSDL2 &&
      m_surfaceHost.WasQuitRequested()) {
    GLIDE_LOG(INFO, "Vulkan",
              "Standalone window close requested; exiting cleanly.");
    DetachWindow_Locked();
    lock.unlock();
    std::exit(0);
  }

  // Flush any pending CPU LFB writes to the GPU active image before we
  // swap/present!
  FlushLFBToGPU_Locked();

  GLIDE_PROFILE_SCOPE("Vulkan::SwapBuffers");

  // Sync child/standalone surface bounds with host window and recreate
  // swapchain cleanly on resize or recovery from minimized state.
  if (m_surface) {
    uint32_t hostW = 0;
    uint32_t hostH = 0;
    bool sizeChanged = m_surfaceHost.SyncBounds(hostW, hostH);
    if (sizeChanged && hostW > 0 && hostH > 0) {
      RecreateSwapchain(hostW, hostH);
    } else if (!m_swapchain && hostW > 0 && hostH > 0) {
      CreateSwapchain(hostW, hostH);
    }
  }

  // 1. Flush any pending batch draws and terminate the active render pass
  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }

  // Check if we have any pending LoadOp clears. If yes, execute them now.
  uint32_t activeIdx = GetActiveGpuBufferIdx();
  if (!m_inRenderPass &&
      (m_clearColorPending[activeIdx] || m_clearDepthPending[activeIdx])) {
    EnsureRenderPassActive();
  }

  if (m_inRenderPass) {
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  const auto presentStart = std::chrono::steady_clock::now();
  bool useSwapchain = (m_swapchain.get() != nullptr);
  uint32_t imageIdx = 0;

  if (useSwapchain) {
    GLIDE_PROFILE_SCOPE("Vulkan::SwapBuffers_NativePresent");

    vk::Result acquireRes = vk::Result::eTimeout;
    try {
      acquireRes = m_device->acquireNextImageKHR(
          m_swapchain.get(), UINT64_MAX,
          m_imageAvailableSemaphores[m_currentFrameSlot].get(), {}, &imageIdx);
    } catch (const vk::OutOfDateKHRError&) {
      acquireRes = vk::Result::eErrorOutOfDateKHR;
    }

    if (acquireRes == vk::Result::eErrorOutOfDateKHR) {
      uint32_t hostW = 0;
      uint32_t hostH = 0;
      m_surfaceHost.SyncBounds(hostW, hostH);
      if (hostW > 0 && hostH > 0) {
        RecreateSwapchain(hostW, hostH);
      }
      m_commandBuffer = &m_commandBuffers[m_currentFrameSlot].get();
      m_commandBuffer->reset({});
      m_commandBuffer->begin(vk::CommandBufferBeginInfo(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
      return true;
    } else if (acquireRes != vk::Result::eSuccess &&
               acquireRes != vk::Result::eSuboptimalKHR) {
      GLIDE_LOG(
          CRITICAL, "Vulkan",
          "Failed to acquire swapchain image: " << vk::to_string(acquireRes));
      return false;
    }

    vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1,
                                         0, 1);

    // Transition swapchain image to TransferDstOptimal
    TransitionImageLayout(*m_commandBuffer, m_swapchainImages[imageIdx],
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, colorRange);

    // Transition offscreen image to TransferSrcOptimal
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                          vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::eTransferSrcOptimal, colorRange);

    // Compute aspect-ratio-preserving presentation rectangle (black
    // letterbox/pillarbox bars when host aspect != guest aspect).
    const PresentRect dst = NativeSurfaceHost::ComputePresentRect(
        m_headlessWidth, m_headlessHeight, m_swapchainExtent.width,
        m_swapchainExtent.height, /*preserveAspect=*/true);

    const bool needsBars =
        (dst.x > 0 || dst.y > 0 || dst.width < m_swapchainExtent.width ||
         dst.height < m_swapchainExtent.height);
    if (needsBars) {
      vk::ClearColorValue blackClear(
          std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
      m_commandBuffer->clearColorImage(m_swapchainImages[imageIdx],
                                       vk::ImageLayout::eTransferDstOptimal,
                                       &blackClear, 1, &colorRange);

      vk::ImageMemoryBarrier clearToBlitBarrier;
      clearToBlitBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      clearToBlitBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
      clearToBlitBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      clearToBlitBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      clearToBlitBarrier.image = m_swapchainImages[imageIdx];
      clearToBlitBarrier.subresourceRange = colorRange;
      clearToBlitBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      clearToBlitBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
      m_commandBuffer->pipelineBarrier(
          vk::PipelineStageFlagBits::eTransfer,
          vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1,
          &clearToBlitBarrier);
    }

    // Blit offscreen guest framebuffer into the centered destination rect
    if (dst.width > 0 && dst.height > 0) {
      vk::ImageBlit blitRegion;
      blitRegion.srcSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
      blitRegion.srcOffsets[0] = vk::Offset3D(0, 0, 0);
      blitRegion.srcOffsets[1] =
          vk::Offset3D(static_cast<int32_t>(m_headlessWidth),
                       static_cast<int32_t>(m_headlessHeight), 1);
      blitRegion.dstSubresource =
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
      blitRegion.dstOffsets[0] = vk::Offset3D(dst.x, dst.y, 0);
      blitRegion.dstOffsets[1] =
          vk::Offset3D(dst.x + static_cast<int32_t>(dst.width),
                       dst.y + static_cast<int32_t>(dst.height), 1);

      m_commandBuffer->blitImage(
          m_headlessImages[activeIdx].get(),
          vk::ImageLayout::eTransferSrcOptimal, m_swapchainImages[imageIdx],
          vk::ImageLayout::eTransferDstOptimal, 1, &blitRegion,
          vk::Filter::eLinear);
    }

    // Render M3 post-scene HUD directly onto the swapchain image (when
    // enabled); the HUD render pass transitions eTransferDstOptimal ->
    // ePresentSrcKHR. When disabled, do the layout transition directly.
    if (!RenderHudToSwapchain_Locked(imageIdx, dst)) {
      TransitionImageLayout(*m_commandBuffer, m_swapchainImages[imageIdx],
                            vk::ImageLayout::eTransferDstOptimal,
                            vk::ImageLayout::ePresentSrcKHR, colorRange);
    }

    // Transition offscreen image back to ColorAttachmentOptimal
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                          vk::ImageLayout::eTransferSrcOptimal,
                          vk::ImageLayout::eColorAttachmentOptimal, colorRange);

    m_commandBuffer->end();

    (void)m_device->resetFences(1, &m_fences[m_currentFrameSlot].get());

    vk::PipelineStageFlags waitStages[] = {
        vk::PipelineStageFlagBits::eTransfer |
        vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore waitSemaphores[] = {
        m_imageAvailableSemaphores[m_currentFrameSlot].get()};
    vk::Semaphore signalSemaphores[] = {
        m_renderFinishedSemaphores[imageIdx].get()};

    vk::SubmitInfo submitInfo(1, waitSemaphores, waitStages, 1, m_commandBuffer,
                              1, signalSemaphores);

    m_graphicsQueue.submit(submitInfo, m_fences[m_currentFrameSlot].get());

    vk::SwapchainKHR swapchains[] = {m_swapchain.get()};
    uint32_t indices[] = {imageIdx};
    vk::PresentInfoKHR presentInfo(1, signalSemaphores, 1, swapchains, indices,
                                   nullptr);

    vk::Result presentRes = vk::Result::eSuccess;
    try {
      presentRes = m_graphicsQueue.presentKHR(presentInfo);
    } catch (const vk::OutOfDateKHRError&) {
      presentRes = vk::Result::eErrorOutOfDateKHR;
    }

    if (presentRes == vk::Result::eErrorOutOfDateKHR ||
        presentRes == vk::Result::eSuboptimalKHR) {
      uint32_t hostW = 0;
      uint32_t hostH = 0;
      m_surfaceHost.SyncBounds(hostW, hostH);
      if (hostW > 0 && hostH > 0) {
        RecreateSwapchain(hostW, hostH);
      }
    } else if (presentRes != vk::Result::eSuccess) {
      GLIDE_LOG(
          CRITICAL, "Vulkan",
          "Failed to present swapchain image: " << vk::to_string(presentRes));
      return false;
    }

    uint32_t nextSlot = (m_currentFrameSlot + 1) % 2;
    if (m_device->waitForFences(1, &m_fences[nextSlot].get(), VK_TRUE,
                                UINT64_MAX) != vk::Result::eSuccess) {
      GLIDE_LOG(CRITICAL, "Vulkan",
                "Failed to wait for fence of next frame slot " << nextSlot);
    }
    m_currentFrameSlot = nextSlot;
    m_texStagingOffsets[m_currentFrameSlot] =
        m_currentFrameSlot * m_texStagingSlotSize;

  } else {
    // Headless fallback (pure offscreen render copy to CPU staging)
    GLIDE_PROFILE_SCOPE("Vulkan::SwapBuffers_HeadlessCopy");
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1);
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                          vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::eTransferSrcOptimal, range);

    vk::BufferImageCopy region;
    region.imageSubresource =
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);

    m_commandBuffer->copyImageToBuffer(
        m_headlessImages[activeIdx].get(), vk::ImageLayout::eTransferSrcOptimal,
        m_headlessPixelBuffers[m_currentFrameSlot].get(), 1, &region);

    TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(),
                          vk::ImageLayout::eTransferSrcOptimal,
                          vk::ImageLayout::eColorAttachmentOptimal, range);

    m_commandBuffer->end();

    (void)m_device->resetFences(1, &m_fences[m_currentFrameSlot].get());

    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
    m_graphicsQueue.submit(submitInfo, m_fences[m_currentFrameSlot].get());

    uint32_t nextSlot = (m_currentFrameSlot + 1) % 2;
    if (m_device->waitForFences(1, &m_fences[nextSlot].get(), VK_TRUE,
                                UINT64_MAX) != vk::Result::eSuccess) {
      GLIDE_LOG(CRITICAL, "Vulkan",
                "Failed to wait for fence of next frame slot " << nextSlot);
    }

    m_currentFrameSlot = nextSlot;
    m_texStagingOffsets[m_currentFrameSlot] =
        m_currentFrameSlot * m_texStagingSlotSize;
  }

  m_commandBuffer = &m_commandBuffers[m_currentFrameSlot].get();
  m_commandBuffer->reset({});
  m_commandBuffer->begin(vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  // Swap CPU LFB staging indices for double-buffering (zero-cost when unused)
  m_lfbStaging.SwapBuffers();

  // Swap the GPU color targets for physical double-buffering
  std::swap(m_headlessImages[0], m_headlessImages[1]);
  std::swap(m_headlessImageMemories[0], m_headlessImageMemories[1]);
  std::swap(m_headlessImageViews[0], m_headlessImageViews[1]);
  std::swap(m_headlessFramebuffers[0], m_headlessFramebuffers[1]);

  const auto presentUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - presentStart)
                             .count();
  const float maxFps = m_config.maxFps;
  if (m_surfaceHost.GetMode() == NativeSurfaceHost::Mode::StandaloneSDL2) {
    // Dispatch any post-commit Wayland/X11 events (configure, ping, move)
    // immediately so single-frame-then-block apps stay responsive.
    m_surfaceHost.PollEvents(/*force=*/true);
  }
  lock.unlock();

  auto& telemetry = TelemetryManager::GetInstance();
  telemetry.RecordPresentTimeUs(presentUs);
  auto& tracker = telemetry.GetFrameTracker();
  tracker.MarkFrameEnd(maxFps);
  tracker.MarkFrameStart();

  return true;
}

void VulkanBackend::PollEvents() {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Safe no-op in Headless, X11Child, and WaylandSubsurface modes; throttled
  // SDL event pumping in StandaloneSDL2 mode.
  m_surfaceHost.PollEvents(/*force=*/false);
}

void VulkanBackend::SstIdle() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_device) {
    // Blocks the host CPU until all submitted GPU commands in all queues have
    // finished execution!
    m_device->waitIdle();
  }
}

// Private Vulkan Setup Helpers (Identical to before)
bool VulkanBackend::CreateInstance() {
  vk::ApplicationInfo appInfo("glide-ng", VK_MAKE_VERSION(1, 0, 0), "No Engine",
                              VK_MAKE_VERSION(1, 0, 0), VK_API_VERSION_1_1);
  vk::InstanceCreateInfo createInfo({}, &appInfo);

  std::vector<const char*> requiredExtensions;

  // Load Vulkan instance extensions required by SDL2 for surface creation!
  bool shouldLoadExtensions = !m_config.forceNoWindow;
#if defined(__linux__)
  const char* display = std::getenv("DISPLAY");
  const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  if (!display && !waylandDisplay) {
    shouldLoadExtensions = false;
  }
#endif

  if (shouldLoadExtensions) {
    std::unordered_set<std::string> availableExtensions;
    try {
      for (const auto& ext : vk::enumerateInstanceExtensionProperties()) {
        availableExtensions.insert(ext.extensionName.data());
      }
    } catch (const std::exception& e) {
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to enumerate Vulkan instance extensions: " << e.what());
    }

    auto enableIfAvailable = [&](const char* extName) {
      if (availableExtensions.count(extName) > 0) {
        requiredExtensions.push_back(extName);
        GLIDE_LOG(INFO, "Vulkan",
                  "Enabled Vulkan WSI instance extension: " << extName);
      }
    };

    enableIfAvailable(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
    enableIfAvailable(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    enableIfAvailable("VK_KHR_xcb_surface");
#endif
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
    enableIfAvailable(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
#if defined(_WIN32)
    enableIfAvailable(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
  } else {
    GLIDE_LOG(INFO, "Vulkan",
              "No active display environment or forceNoWindow is set. "
              "Skipping Vulkan WSI instance extensions.");
  }

  createInfo.setEnabledExtensionCount(
      static_cast<uint32_t>(requiredExtensions.size()));
  createInfo.setPpEnabledExtensionNames(requiredExtensions.data());

  if (enableValidationLayers) {
    createInfo.setEnabledLayerCount(
        static_cast<uint32_t>(validationLayers.size()));
    createInfo.setPpEnabledLayerNames(validationLayers.data());
  } else {
    createInfo.setEnabledLayerCount(0);
  }

  m_instance = vk::createInstanceUnique(createInfo);
  return true;
}

bool VulkanBackend::SelectPhysicalDevice() {
  auto devices = m_instance->enumeratePhysicalDevices();
  if (devices.empty()) {
    GLIDE_LOG(
        CRITICAL, "Vulkan",
        "Zero Vulkan compatible hardware/software devices found on host.");
    return false;
  }
  m_physicalDevice = devices[0];
  vk::PhysicalDeviceProperties props = m_physicalDevice.getProperties();

  std::ostringstream logStream;
  logStream << "\n--- Active Vulkan Execution Adapter ---\n"
            << "  Adapter     : " << props.deviceName.data() << "\n"
            << "    API Ver   : " << VK_VERSION_MAJOR(props.apiVersion) << "."
            << VK_VERSION_MINOR(props.apiVersion) << "."
            << VK_VERSION_PATCH(props.apiVersion) << "\n"
            << "    Dev Type  : " << vk::to_string(props.deviceType) << "\n"
            << "----------------------------------------";
  GLIDE_LOG(DEBUG, "Vulkan", logStream.str());

  return true;
}

bool VulkanBackend::CreateLogicalDevice() {
  auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
  int index = 0;
  bool found = false;
  for (const auto& q : queueFamilies) {
    if (q.queueFlags & vk::QueueFlagBits::eGraphics) {
      m_graphicsQueueFamilyIndex = index;
      found = true;
      break;
    }
    index++;
  }

  if (!found) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Vulkan adapter lacks graphics command submission queue.");
    return false;
  }

  float priority = 1.0f;
  vk::DeviceQueueCreateInfo queueInfo({}, m_graphicsQueueFamilyIndex, 1,
                                      &priority);

  // Query and enable dynamic depth clamping to prevent out-of-bounds Z clipping
  vk::PhysicalDeviceFeatures deviceFeatures;
  auto supportedFeatures = m_physicalDevice.getFeatures();
  if (supportedFeatures.depthClamp) {
    deviceFeatures.depthClamp = true;
    m_depthClampEnabled = true;
    GLIDE_LOG(INFO, "Vulkan",
              "Vulkan Depth Clamping physical device feature enabled.");
  } else {
    m_depthClampEnabled = false;
    GLIDE_LOG(WARN, "Vulkan",
              "Vulkan Depth Clamping NOT supported on host device!");
  }

  if (supportedFeatures.dualSrcBlend) {
    deviceFeatures.dualSrcBlend = true;
    GLIDE_LOG(INFO, "Vulkan",
              "Vulkan Dual-Source Blending physical device feature enabled.");
  } else {
    GLIDE_LOG(WARN, "Vulkan",
              "Vulkan Dual-Source Blending NOT supported by host GPU! Pre-fog "
              "blending will be degraded.");
  }

  std::vector<const char*> deviceExtensions;
  auto availableExtensions =
      m_physicalDevice.enumerateDeviceExtensionProperties();
  bool swapchainSupported = false;
  for (const auto& ext : availableExtensions) {
    if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
      swapchainSupported = true;
      break;
    }
  }

  if (swapchainSupported) {
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    GLIDE_LOG(
        INFO, "Vulkan",
        "VK_KHR_swapchain logical device extension supported and enabled.");
  } else if (!m_config.forceNoWindow) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "VK_KHR_swapchain extension is required for windowed "
              "presentation but not supported by host GPU.");
    return false;
  }

  vk::DeviceCreateInfo deviceInfo({}, 1, &queueInfo);
  deviceInfo.setPEnabledFeatures(&deviceFeatures);
  deviceInfo.setEnabledExtensionCount(
      static_cast<uint32_t>(deviceExtensions.size()));
  deviceInfo.setPpEnabledExtensionNames(deviceExtensions.data());

  m_device = m_physicalDevice.createDeviceUnique(deviceInfo);
  m_graphicsQueue = m_device->getQueue(m_graphicsQueueFamilyIndex, 0);

  // 2.5. Initialize persistent Vulkan pipeline cache from disk!
  const std::string& cachePath =
      m_config.vkCachePath.empty() ? std::string(".glide-vulkan-cache.bin")
                                   : m_config.vkCachePath;

  std::vector<char> cacheData;
  std::ifstream cacheFile(cachePath, std::ios::binary | std::ios::ate);
  if (cacheFile.is_open()) {
    std::streamsize size = cacheFile.tellg();
    cacheFile.seekg(0, std::ios::beg);
    cacheData.resize(size);
    if (cacheFile.read(cacheData.data(), size)) {
      GLIDE_LOG(INFO, "Vulkan",
                "Successfully loaded persistent pipeline cache binary ("
                    << size << " bytes) from " << cachePath);
    } else {
      cacheData.clear();
      GLIDE_LOG(WARN, "Vulkan",
                "Failed to read pipeline cache file: " << cachePath);
    }
    cacheFile.close();
  } else {
    GLIDE_LOG(INFO, "Vulkan",
              "No pre-existing pipeline cache file found at "
                  << cachePath << ". Starting with a fresh cache.");
  }

  vk::PipelineCacheCreateInfo cacheInfo;
  if (!cacheData.empty()) {
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();
  }
  m_vkPipelineCache = m_device->createPipelineCacheUnique(cacheInfo);

  vk::CommandPoolCreateInfo poolInfo(
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      m_graphicsQueueFamilyIndex);
  vk::FenceCreateInfo fenceInfo(
      vk::FenceCreateFlagBits::eSignaled);  // Start signaled so first wait
                                            // succeeds immediately!

  for (uint32_t i = 0; i < 2; ++i) {
    m_commandPools[i] = m_device->createCommandPoolUnique(poolInfo);

    vk::CommandBufferAllocateInfo allocInfo(
        m_commandPools[i].get(), vk::CommandBufferLevel::ePrimary, 1);
    auto bufs = m_device->allocateCommandBuffersUnique(allocInfo);
    m_commandBuffers[i] = std::move(bufs[0]);

    m_fences[i] = m_device->createFenceUnique(fenceInfo);
  }

  m_currentFrameSlot = 0;
  m_commandBuffer = &m_commandBuffers[0].get();

  // 2.6. Pre-allocate and permanently map persistent Texture Staging Buffer
  // (dynamically sized based on m_texStagingSlotSize * 2)
  const uint32_t stagingSize = m_texStagingSlotSize * 2;
  vk::BufferCreateInfo texStagingInfo({}, stagingSize,
                                      vk::BufferUsageFlagBits::eTransferSrc);
  m_texStagingBuffer = m_device->createBufferUnique(texStagingInfo);

  auto texStagingReqs =
      m_device->getBufferMemoryRequirements(m_texStagingBuffer.get());
  uint32_t texStagingTypeIndex =
      FindMemoryType(texStagingReqs.memoryTypeBits,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);
  vk::MemoryAllocateInfo texStagingAlloc(texStagingReqs.size,
                                         texStagingTypeIndex);
  m_texStagingMemory = m_device->allocateMemoryUnique(texStagingAlloc);
  m_device->bindBufferMemory(m_texStagingBuffer.get(), m_texStagingMemory.get(),
                             0);

  m_texStagingMap =
      m_device->mapMemory(m_texStagingMemory.get(), 0, stagingSize);

  // Initialize double-buffered offsets
  m_texStagingOffsets[0] = 0;
  m_texStagingOffsets[1] = m_texStagingSlotSize;

  return true;
}

bool VulkanBackend::CreateHeadlessRenderTarget(uint32_t width,
                                               uint32_t height) {
  vk::SampleCountFlagBits samples =
      static_cast<vk::SampleCountFlagBits>(m_msaaSamples);

  // 1. Allocate depth image for hardware depth-testing support (Shared!)
  vk::ImageCreateInfo depthImageInfo(
      {}, vk::ImageType::e2D, m_depthFormat, vk::Extent3D(width, height, 1), 1,
      1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eDepthStencilAttachment |
          vk::ImageUsageFlagBits::eTransferDst);
  m_depthImage = m_device->createImageUnique(depthImageInfo);

  auto depthMemReqs = m_device->getImageMemoryRequirements(m_depthImage.get());
  uint32_t depthTypeIndex = FindMemoryType(
      depthMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
  vk::MemoryAllocateInfo depthAllocInfo(depthMemReqs.size, depthTypeIndex);
  m_depthImageMemory = m_device->allocateMemoryUnique(depthAllocInfo);
  m_device->bindImageMemory(m_depthImage.get(), m_depthImageMemory.get(), 0);

  vk::ImageViewCreateInfo depthViewInfo(
      {}, m_depthImage.get(), vk::ImageViewType::e2D, m_depthFormat, {},
      {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
  m_depthImageView = m_device->createImageViewUnique(depthViewInfo);

  // 1.1 Allocate Multi-sampled Depth image if MSAA is enabled (Transient
  // VRAM-only tile-local)
  if (m_msaaSamples > 1) {
    vk::ImageCreateInfo msaaDepthInfo(
        {}, vk::ImageType::e2D, m_depthFormat, vk::Extent3D(width, height, 1),
        1, 1, samples, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment |
            vk::ImageUsageFlagBits::eTransientAttachment);
    m_msaaDepthImage = m_device->createImageUnique(msaaDepthInfo);

    auto msaaDepthMemReqs =
        m_device->getImageMemoryRequirements(m_msaaDepthImage.get());
    uint32_t msaaDepthTypeIndex =
        FindMemoryType(msaaDepthMemReqs.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo msaaDepthAlloc(msaaDepthMemReqs.size,
                                          msaaDepthTypeIndex);
    m_msaaDepthImageMemory = m_device->allocateMemoryUnique(msaaDepthAlloc);
    m_device->bindImageMemory(m_msaaDepthImage.get(),
                              m_msaaDepthImageMemory.get(), 0);

    vk::ImageViewCreateInfo msaaDepthViewInfo(
        {}, m_msaaDepthImage.get(), vk::ImageViewType::e2D, m_depthFormat, {},
        {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    m_msaaDepthImageView = m_device->createImageViewUnique(msaaDepthViewInfo);
  }

  // 2. Allocate Double-Buffered Color Images and Framebuffers (i=0 for BACK,
  // i=1 for FRONT)
  for (int i = 0; i < 2; ++i) {
    // 2.1 Allocate Single-sampled Resolve/Present Target
    vk::ImageCreateInfo imageInfo(
        {}, vk::ImageType::e2D, m_colorFormat, vk::Extent3D(width, height, 1),
        1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst);

    m_headlessImages[i] = m_device->createImageUnique(imageInfo);

    auto memReqs =
        m_device->getImageMemoryRequirements(m_headlessImages[i].get());
    uint32_t typeIndex = FindMemoryType(
        memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);

    m_headlessImageMemories[i] = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindImageMemory(m_headlessImages[i].get(),
                              m_headlessImageMemories[i].get(), 0);

    vk::ImageViewCreateInfo viewInfo(
        {}, m_headlessImages[i].get(), vk::ImageViewType::e2D, m_colorFormat,
        {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_headlessImageViews[i] = m_device->createImageViewUnique(viewInfo);

    // 2.2 Allocate Multi-sampled Color target if MSAA is enabled (Transient
    // VRAM-only tile-local)
    if (m_msaaSamples > 1) {
      vk::ImageCreateInfo msaaColorInfo(
          {}, vk::ImageType::e2D, m_colorFormat, vk::Extent3D(width, height, 1),
          1, 1, samples, vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eColorAttachment |
              vk::ImageUsageFlagBits::eTransientAttachment);
      m_msaaColorImages[i] = m_device->createImageUnique(msaaColorInfo);

      auto msaaMemReqs =
          m_device->getImageMemoryRequirements(m_msaaColorImages[i].get());
      uint32_t msaaTypeIndex = FindMemoryType(
          msaaMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      vk::MemoryAllocateInfo msaaAllocInfo(msaaMemReqs.size, msaaTypeIndex);

      m_msaaColorImageMemories[i] =
          m_device->allocateMemoryUnique(msaaAllocInfo);
      m_device->bindImageMemory(m_msaaColorImages[i].get(),
                                m_msaaColorImageMemories[i].get(), 0);

      vk::ImageViewCreateInfo msaaViewInfo(
          {}, m_msaaColorImages[i].get(), vk::ImageViewType::e2D, m_colorFormat,
          {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
      m_msaaColorImageViews[i] = m_device->createImageViewUnique(msaaViewInfo);
    }

    // 2.3 Create Framebuffer binding color, depth, and resolve attachments
    std::vector<vk::ImageView> fboAttachments;
    if (m_msaaSamples > 1) {
      fboAttachments = {
          m_msaaColorImageViews[i].get(), m_msaaDepthImageView.get(),
          m_headlessImageViews[i].get()  // Resolve target
      };
    } else {
      fboAttachments = {m_headlessImageViews[i].get(), m_depthImageView.get()};
    }

    vk::FramebufferCreateInfo fbInfo(
        {}, m_renderPass.get(), static_cast<uint32_t>(fboAttachments.size()),
        fboAttachments.data(), width, height, 1);
    m_headlessFramebuffers[i] = m_device->createFramebufferUnique(fbInfo);
  }

  // Create the CPU staging buffers (Double-Buffered)
  vk::BufferCreateInfo bufInfo({}, width * height * 4,
                               vk::BufferUsageFlagBits::eTransferDst |
                                   vk::BufferUsageFlagBits::eTransferSrc);

  for (uint32_t i = 0; i < 2; ++i) {
    m_headlessPixelBuffers[i] = m_device->createBufferUnique(bufInfo);

    auto bufReqs =
        m_device->getBufferMemoryRequirements(m_headlessPixelBuffers[i].get());
    uint32_t bufTypeIndex = FindMemoryType(
        bufReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo bufAlloc(bufReqs.size, bufTypeIndex);

    m_headlessPixelMemories[i] = m_device->allocateMemoryUnique(bufAlloc);
    m_device->bindBufferMemory(m_headlessPixelBuffers[i].get(),
                               m_headlessPixelMemories[i].get(), 0);

    m_gpuStagingMaps[i] = m_device->mapMemory(m_headlessPixelMemories[i].get(),
                                              0, width * height * 4);
    if (!m_gpuStagingMaps[i]) {
      GLIDE_LOG(CRITICAL, "Vulkan",
                "Failed to map headless host memory map for slot " << i << "!");
      return false;
    }

    // Pre-initialize GPU staging buffer to opaque black
    std::memset(m_gpuStagingMaps[i], 0xFF, width * height * 4);
  }

  // Configure lazily-allocated CPU LFB staging (0 bytes until LFB is touched)
  m_lfbStaging.Configure(width, height, /*allocateEagerly=*/false);
  m_activeRenderBuffer = 1;  // GR_BUFFER_BACKBUFFER

  return true;
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeFilter,
                                       vk::MemoryPropertyFlags properties) {
  auto memProps = m_physicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  GLIDE_LOG(CRITICAL, "Vulkan",
            "No matching physical memory heap type filter found!");
  return 0;
}

void VulkanBackend::TransitionImageLayout(vk::CommandBuffer cmd,
                                          vk::Image image,
                                          vk::ImageLayout oldLayout,
                                          vk::ImageLayout newLayout,
                                          vk::ImageSubresourceRange range) {
  GLIDE_LOG(DEBUG, "Vulkan",
            "TransitionImageLayout: [TRACE] cmd = "
                << (VkCommandBuffer)cmd << ", image = " << (VkImage)image
                << ", oldLayout = " << (int)oldLayout
                << ", newLayout = " << (int)newLayout);
  vk::ImageMemoryBarrier barrier;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = range;

  vk::PipelineStageFlags sourceStage;
  vk::PipelineStageFlags destinationStage;

  if (oldLayout == vk::ImageLayout::eUndefined &&
      newLayout == vk::ImageLayout::eTransferDstOptimal) {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
    destinationStage = vk::PipelineStageFlagBits::eTransfer;
  } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
             newLayout == vk::ImageLayout::eTransferSrcOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    sourceStage = vk::PipelineStageFlagBits::eTransfer;
    destinationStage = vk::PipelineStageFlagBits::eTransfer;
  } else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal &&
             newLayout == vk::ImageLayout::eTransferSrcOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    destinationStage = vk::PipelineStageFlagBits::eTransfer;
  } else if (oldLayout == vk::ImageLayout::eTransferSrcOptimal &&
             newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                            vk::AccessFlagBits::eColorAttachmentRead;
    sourceStage = vk::PipelineStageFlagBits::eTransfer;
    destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
             newLayout == vk::ImageLayout::ePresentSrcKHR) {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = {};
    sourceStage = vk::PipelineStageFlagBits::eTransfer;
    destinationStage = vk::PipelineStageFlagBits::eBottomOfPipe;
  } else if (oldLayout == vk::ImageLayout::eUndefined &&
             newLayout == vk::ImageLayout::ePresentSrcKHR) {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = {};
    sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
    destinationStage = vk::PipelineStageFlagBits::eBottomOfPipe;
  } else {
    barrier.srcAccessMask =
        vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
    barrier.dstAccessMask =
        vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
    sourceStage = vk::PipelineStageFlagBits::eAllCommands;
    destinationStage = vk::PipelineStageFlagBits::eAllCommands;
  }

  cmd.pipelineBarrier(sourceStage, destinationStage, {}, 0, nullptr, 0, nullptr,
                      1, &barrier);
}

void VulkanBackend::SetGamma(float gamma) {
  std::lock_guard<std::mutex> lock(m_mutex);
  GLIDE_LOG(INFO, "Vulkan", "SetGamma: value=" << gamma);
  m_gammaCorrectionValue = gamma;
  if (gamma <= 0.0f) gamma = 1.0f;
  for (int i = 0; i < 256; i++) {
    m_lutR[i] = static_cast<uint8_t>(
        std::pow(i / 255.0f, 1.0f / gamma) * 255.0f + 0.5f);
    m_lutG[i] = m_lutR[i];
    m_lutB[i] = m_lutR[i];
  }
  m_useGammaLut = (gamma != 1.0f);
}

void VulkanBackend::LoadGammaTable(uint32_t nentries, const uint32_t* rTable,
                                   const uint32_t* gTable,
                                   const uint32_t* bTable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  GLIDE_LOG(INFO, "Vulkan", "LoadGammaTable: nentries=" << nentries);
  if (nentries != 256 || !rTable || !gTable || !bTable) return;
  for (int i = 0; i < 256; i++) {
    m_lutR[i] = static_cast<uint8_t>(rTable[i]);
    m_lutG[i] = static_cast<uint8_t>(gTable[i]);
    m_lutB[i] = static_cast<uint8_t>(bTable[i]);
  }
  m_useGammaLut = true;
}

}  // namespace GlideWrapper
