#include "VulkanBackend.h"
#include "core/Logger.h"
#include "core/TextureManager.h"
#include "core/Telemetry.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

// Include build-time generated shader SPIR-V bytecode headers!
#include "color_vert.spv.h"
#include "color_frag.spv.h"

extern "C" {
    extern volatile int g_glideWrapperSdlKeyHit;
}

namespace GlideWrapper {



// Authoritative Vulkan validation layer declarations
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

static vk::BlendFactor MapBlendFactorVulkan(uint32_t factor, bool isDest) {
    switch (factor) {
        case 0: return vk::BlendFactor::eZero;                  // GR_BLEND_ZERO
        case 1: return vk::BlendFactor::eSrcAlpha;              // GR_BLEND_SRC_ALPHA
        case 2: return isDest ? vk::BlendFactor::eSrcColor : vk::BlendFactor::eDstColor; // 2 = GR_BLEND_SRC_COLOR (dest) / GR_BLEND_DST_COLOR (src)
        case 3: return vk::BlendFactor::eDstAlpha;              // GR_BLEND_DST_ALPHA
        case 4: return vk::BlendFactor::eOne;                   // GR_BLEND_ONE
        case 5: return vk::BlendFactor::eOneMinusSrcAlpha;      // GR_BLEND_ONE_MINUS_SRC_ALPHA
        case 6: return isDest ? vk::BlendFactor::eOneMinusSrcColor : vk::BlendFactor::eOneMinusDstColor; // 6 = ONE_MINUS_SRC_COLOR (dest) / ONE_MINUS_DST_COLOR (src)
        case 7: return vk::BlendFactor::eOneMinusDstAlpha;      // GR_BLEND_ONE_MINUS_DST_ALPHA
        case 15: return vk::BlendFactor::eSrcAlphaSaturate;     // GR_BLEND_ALPHA_SATURATE
        default: return vk::BlendFactor::eOne;
    }
}

const bool enableValidationLayers = false;

bool VulkanBackend::Initialize(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    GLIDE_LOG(INFO, "Vulkan", "Initializing Hybrid hardware Vulkan GPU context...");
    m_config = config;

    Logger::GetInstance().LogHostVulkanCapabilities();

    if (!CreateInstance()) return false;
    if (!SelectPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;

    // Create descriptor set layout
    vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);
    vk::DescriptorSetLayoutCreateInfo layoutInfo({}, 1, &binding);
    m_descriptorSetLayout = m_device->createDescriptorSetLayoutUnique(layoutInfo);
    
    // Create descriptor pool
    std::array<vk::DescriptorPoolSize, 1> poolSizes = {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 128)
    };
    vk::DescriptorPoolCreateInfo poolInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 128, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());
    m_descriptorPool = m_device->createDescriptorPoolUnique(poolInfo);

    // Create 1x1 dummy white texture
    {
        uint32_t whitePixel = 0xFFFFFFFF;
        
        vk::ImageCreateInfo imgInfo({}, vk::ImageType::e2D, vk::Format::eB8G8R8A8Unorm,
                                     vk::Extent3D(1, 1, 1), 1, 1,
                                     vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                     vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
        m_dummyTexture.image = m_device->createImageUnique(imgInfo);
        
        auto memReqs = m_device->getImageMemoryRequirements(m_dummyTexture.image.get());
        uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
        m_dummyTexture.memory = m_device->allocateMemoryUnique(allocInfo);
        m_device->bindImageMemory(m_dummyTexture.image.get(), m_dummyTexture.memory.get(), 0);
        
        // Create staging buffer for 4 bytes
        vk::BufferCreateInfo stagingInfo({}, 4, vk::BufferUsageFlagBits::eTransferSrc);
        auto stagingBuffer = m_device->createBufferUnique(stagingInfo);
        auto stagingReqs = m_device->getBufferMemoryRequirements(stagingBuffer.get());
        uint32_t stagingTypeIndex = FindMemoryType(stagingReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        vk::MemoryAllocateInfo stagingAlloc(stagingReqs.size, stagingTypeIndex);
        auto stagingMemory = m_device->allocateMemoryUnique(stagingAlloc);
        m_device->bindBufferMemory(stagingBuffer.get(), stagingMemory.get(), 0);
        
        void* mapped = m_device->mapMemory(stagingMemory.get(), 0, 4);
        std::memcpy(mapped, &whitePixel, 4);
        m_device->unmapMemory(stagingMemory.get());
        
        // Command buffer for one-time upload
        vk::CommandBufferAllocateInfo cmdAlloc(m_commandPools[0].get(), vk::CommandBufferLevel::ePrimary, 1);
        auto cmds = m_device->allocateCommandBuffersUnique(cmdAlloc);
        auto cmd = std::move(cmds[0]);
        
        cmd->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        TransitionImageLayout(cmd.get(), m_dummyTexture.image.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, range);
        
        vk::BufferImageCopy region;
        region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        region.imageExtent = vk::Extent3D(1, 1, 1);
        cmd->copyBufferToImage(stagingBuffer.get(), m_dummyTexture.image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &region);
        
        TransitionImageLayout(cmd.get(), m_dummyTexture.image.get(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, range);
        
        cmd->end();
        
        vk::SubmitInfo submit(0, nullptr, nullptr, 1, &cmd.get());
        m_graphicsQueue.submit(submit, {});
        m_device->waitIdle();
        
        // Image View
        vk::ImageViewCreateInfo viewInfo({}, m_dummyTexture.image.get(), vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm,
                                        {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        m_dummyTexture.imageView = m_device->createImageViewUnique(viewInfo);
        
        // Sampler
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        m_dummyTexture.sampler = m_device->createSamplerUnique(samplerInfo);
        
        // Descriptor Set
        vk::DescriptorSetAllocateInfo setAlloc(m_descriptorPool.get(), 1, &m_descriptorSetLayout.get());
        auto sets = m_device->allocateDescriptorSets(setAlloc);
        m_dummyTexture.descriptorSet = sets[0];
        
        vk::DescriptorImageInfo imageInfo(m_dummyTexture.sampler.get(), m_dummyTexture.imageView.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet descriptorWrite(m_dummyTexture.descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr);
        m_device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
        
        GLIDE_LOG(INFO, "Vulkan", "Created 1x1 dummy white texture.");
    }

    if (!CreateRenderPass()) return false;

    m_initialized = true;
    GLIDE_LOG(INFO, "Vulkan", "Vulkan GPU context established successfully.");
    return true;
}

void VulkanBackend::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    GLIDE_LOG(INFO, "Vulkan", "Shutting down Hybrid Vulkan context.");
    
    // 1. Force-detach window resources first (handles active frame command buffer, SDL, etc.)
    DetachWindow();

    // 2. Explicitly destroy all device-owned pipelines, shaders, and caches
    m_pipelineCache.clear();
    m_activePointPipeline = nullptr;
    m_activeLinePipeline = nullptr;
    m_activeTrianglePipeline = nullptr;
    m_danglingPipelines.clear(); // Clear recompiled mid-frame pipelines
    m_pipelineLayout.reset();
    m_vertShaderModule.reset();
    m_fragShaderModule.reset();

    // Reset descriptor layout, pool, and textures
    m_vulkanTextures.clear();
    m_dummyTexture.imageView.reset();
    m_dummyTexture.sampler.reset();
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

    // 2.5. Save persistent Vulkan pipeline cache back to disk!
    if (m_device && m_vkPipelineCache) {
        std::string cachePath = "";
        const char* envCache = std::getenv("GLIDE_WRAPPER_CACHE_PATH");
        if (envCache && envCache[0] != '\0') {
            cachePath = envCache;
        } else {
            cachePath = ".glide-vulkan-cache.bin";
        }

        try {
            auto data = m_device->getPipelineCacheData(m_vkPipelineCache.get());
            if (!data.empty()) {
                std::ofstream cacheFile(cachePath, std::ios::binary | std::ios::trunc);
                if (cacheFile.is_open()) {
                    cacheFile.write(reinterpret_cast<const char*>(data.data()), data.size());
                    cacheFile.close();
                    GLIDE_LOG(INFO, "Vulkan", "Successfully serialized persistent pipeline cache binary (" << data.size() << " bytes) to " << cachePath);
                } else {
                    GLIDE_LOG(WARN, "Vulkan", "Failed to open pipeline cache file for writing: " << cachePath);
                }
            }
        } catch (const std::exception& e) {
            GLIDE_LOG(WARN, "Vulkan", "Failed to retrieve or save pipeline cache data: " << e.what());
        }
    }

    // 5. Reset command submission and render pass infrastructure (Double-Buffered)
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

bool VulkanBackend::AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return false;
    if (m_windowAttached) DetachWindow();

    GLIDE_LOG(INFO, "Vulkan", "Attaching Hybrid Vulkan window (" << width << "x" << height << "), Windowed=" << (windowed ? "Yes" : "No"));

    m_headlessWidth = width;
    m_headlessHeight = height;
    m_headlessMode = true; // Headless-driven for visual regression

    if (!CreateHeadlessRenderTarget(width, height)) return false;

    // Phase 2: Compile Shaders & Graphics Pipelines
    if (!CreateShaderModules()) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to compile Vulkan shader modules!");
        return false;
    }
    if (!CreateGraphicsPipelines()) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to compile Vulkan graphics pipelines!");
        return false;
    }

    // Phase 2: Allocate dynamic ring Vertex Buffer (1MB)
    try {
        vk::BufferCreateInfo bufferInfo({}, m_vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
        m_vertexBuffer = m_device->createBufferUnique(bufferInfo);
        
        auto memReqs = m_device->getBufferMemoryRequirements(m_vertexBuffer.get());
        uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
        
        m_vertexBufferMemory = m_device->allocateMemoryUnique(allocInfo);
        m_device->bindBufferMemory(m_vertexBuffer.get(), m_vertexBufferMemory.get(), 0);
        
        m_vertexBufferMap = m_device->mapMemory(m_vertexBufferMemory.get(), 0, m_vertexBufferSize);
        m_vertexBufferOffset = 0;
        m_batchedPipeline = nullptr;
        m_batchedDescSet = nullptr;
        m_batchedVertexCount = 0;
        m_batchedFirstVertex = 0;
        GLIDE_LOG(INFO, "Vulkan", "Allocated 1MB host-visible dynamic ring vertex buffer and reset batching state.");
    } catch (const std::exception& e) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to allocate Vulkan vertex buffer: " << e.what());
        return false;
    }

    // Query GPU details
    auto props = m_physicalDevice.getProperties();
    std::string devName(props.deviceName);

    std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle << ", res=" << width << "x" << height << ")\r\n";
    std::cout << "Info: Host Vulkan Adapter: " << devName << " (Vulkan API " 
              << (props.apiVersion >> 22) << "." << ((props.apiVersion >> 12) & 0x3FF) << "." << (props.apiVersion & 0xFFF) << ")\r\n";
    std::cout << "Info: Pixel Format RGBA8888 D24S0 nAux 0 nSamples 0 0\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n" << std::flush;

    SetClipWindow(0, 0, width, height);

    if (!m_config.forceNoWindow) {
        GLIDE_LOG(INFO, "Vulkan", "Instantiating interactive SDL2 presentation console...");
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            GLIDE_LOG(WARN, "Vulkan", "SDL_Init failed: " << SDL_GetError() << ". Running headless offscreen.");
        } else {
            auto* win = SDL_CreateWindow("3dfx glide-ng Presentation Console (Vulkan Swapchain)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
            if (win) {
                // Create Vulkan Surface associated with the SDL window
                VkSurfaceKHR rawSurface;
                if (SDL_Vulkan_CreateSurface(win, m_instance.get(), &rawSurface)) {
                    m_surface = vk::UniqueSurfaceKHR(rawSurface, m_instance.get());
                    GLIDE_LOG(INFO, "Vulkan", "Vulkan presentation surface created successfully!");
                } else {
                    GLIDE_LOG(WARN, "Vulkan", "Failed to create Vulkan surface: " << SDL_GetError());
                }

                auto* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
                
                if (ren) {
                    auto* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
                    if (tex) {
                        m_sdlWindow   = win;
                        m_sdlRenderer = ren;
                        m_sdlTexture  = tex;
                        GLIDE_LOG(INFO, "Vulkan", "SDL2 presentation console structured beautifully.");
                    } else {
                        SDL_DestroyRenderer(ren);
                        SDL_DestroyWindow(win);
                    }
                } else {
                    SDL_DestroyWindow(win);
                }
            }
        }
    }

    // Begin recording our persistent frame command buffer!
    m_commandBuffer->reset({});
    m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // One-time layout transition of color and depth images from eUndefined to their optimal layouts
    GLIDE_LOG(INFO, "Vulkan", "Performing one-time startup layout transitions for render targets...");
    vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    for (int i = 0; i < 2; ++i) {
        TransitionImageLayout(*m_commandBuffer, m_headlessImages[i].get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, colorRange);
    }

    vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
    TransitionImageLayout(*m_commandBuffer, m_depthImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, depthRange);

    m_commandBuffer->end();
    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
    m_graphicsQueue.submit(submitInfo, {});
    m_device->waitIdle(); // Block until transitions complete

    // Restart command buffer for frame recording
    m_commandBuffer->reset({});
    m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    m_windowAttached = true;
    return true;
}

void VulkanBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "Vulkan", "Detaching Hybrid Vulkan window resources.");
    
    if (m_device) {
        m_device->waitIdle();
    }

    if (m_sdlTexture)  { SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture)); m_sdlTexture = nullptr; }
    if (m_sdlRenderer) { SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer)); m_sdlRenderer = nullptr; }
    if (m_sdlWindow)   { SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow)); m_sdlWindow = nullptr; }
    if (!m_config.forceNoWindow) SDL_QuitSubSystem(SDL_INIT_VIDEO);

    if (m_headlessMode) {
        // Phase 2: Unmap and reset vertex buffer and pipeline resources
        if (m_vertexBufferMap) {
            m_device->unmapMemory(m_vertexBufferMemory.get());
            m_vertexBufferMap = nullptr;
        }
        m_vertexBufferMemory.reset();
        m_vertexBuffer.reset();
        m_pipelineCache.clear();
        m_activePointPipeline = nullptr;
        m_activeLinePipeline = nullptr;
        m_activeTrianglePipeline = nullptr;
        m_pipelineLayout.reset();
        m_vertShaderModule.reset();
        m_fragShaderModule.reset();

        for (uint32_t i = 0; i < 2; ++i) {
            if (m_gpuStagingMaps[i]) {
                m_device->unmapMemory(m_headlessPixelMemories[i].get());
                m_gpuStagingMaps[i] = nullptr;
            }
            m_headlessPixelBuffers[i].reset();
            m_headlessPixelMemories[i].reset();
        }
        FreeCpuBuffers();
        for (int i = 0; i < 2; ++i) {
            m_headlessFramebuffers[i].reset();
            m_headlessImageViews[i].reset();
            m_headlessImageMemories[i].reset();
            m_headlessImages[i].reset();
        }

        // Explicitly reset depth buffer resources to prevent leaks and shutdown crashes!
        m_depthImageView.reset();
        m_depthImageMemory.reset();
        m_depthImage.reset();
    }

    m_windowAttached = false;
    m_headlessMode = false;
}

void VulkanBackend::ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) {

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    // Terminate active render pass if any (so next draw starts a new render pass that triggers LoadOp Clear!)
    if (m_inRenderPass) {
        FlushBatch();
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
    }

    GLIDE_LOG(DEBUG, "Vulkan", "ClearBuffer: color=" << std::hex << color << ", clearMask=" << clearMask);

    // 1. Call SoftwareBackendBase::ClearBuffer to clear the CPU-side framebuffer!
    SoftwareBackendBase::ClearBuffer(color, alpha, z, clearMask);

    // 1.5. Force CPU framebuffer alpha to 0 (fully transparent) so background is not cached by SwapBuffers!
    if (clearMask & 1) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        if (pixels) {
            for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
                pixels[i] &= 0x00FFFFFF;
            }
        }
    }

    // 2. Save clear state and mark pending for GPU LoadOp clears when Render Pass starts!
    uint32_t activeIdx = GetActiveGpuBufferIdx();
    if (clearMask & 1) { // Color clear
        float a = ((color >> 24) & 0xFF) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8)  & 0xFF) / 255.0f;
        float b = (color         & 0xFF) / 255.0f;

        // Respect the active pixel format from SoftwareBackendBase!
        if (m_pixelFormatOverride == 1) { // ABGR format override
            std::swap(r, b);
        }

        m_clearColorValue[activeIdx] = vk::ClearColorValue(std::array<float, 4>{r, g, b, a});
        m_clearColorPending[activeIdx] = true;
    }

    if (clearMask & 2) { // Depth clear
        m_clearDepthValue[activeIdx] = z;
        m_clearDepthPending[activeIdx] = true;
    }
}

void VulkanBackend::SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (depthMode != m_depthMode || compareOp != m_depthCompareOp || depthMask != m_depthMask || biasLevel != m_depthBiasLevel) {
        FlushBatch();
    }
    SoftwareBackendBase::SetDepthState(depthMode, compareOp, depthMask, biasLevel);
}

void VulkanBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (rgbSrcFactor != m_rgbSrcBlend || rgbDstFactor != m_rgbDstBlend || alphaSrcFactor != m_alphaSrcBlend || alphaDstFactor != m_alphaDstBlend) {
        FlushBatch();
    }
    SoftwareBackendBase::SetBlendState(rgbSrcFactor, rgbDstFactor, alphaSrcFactor, alphaDstFactor);
}

void VulkanBackend::SetCullState(uint32_t cullMode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (cullMode != m_cullMode) {
        FlushBatch();
    }
    SoftwareBackendBase::SetCullState(cullMode);
}

void VulkanBackend::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (compareOp != m_alphaTestOp || refVal != m_alphaTestRefVal) {
        FlushBatch();
    }
    SoftwareBackendBase::SetAlphaTestState(compareOp, refVal);
    // Note: Emulated in fragment shader via push constants. No pipeline recompilation needed!
}

void VulkanBackend::SetSstOrigin(uint32_t origin) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (origin != m_sstOrigin) {
        FlushBatch();
    }
    SoftwareBackendBase::SetSstOrigin(origin);
}

void VulkanBackend::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (minX != m_clipMinX || minY != m_clipMinY || maxX != m_clipMaxX || maxY != m_clipMaxY) {
        FlushBatch();
    }
    SoftwareBackendBase::SetClipWindow(minX, minY, maxX, maxY);

    if (m_initialized && m_device && m_inRenderPass) {
        vk::Rect2D scissor = GetScissorRectVulkan();
        m_commandBuffer->setScissor(0, 1, &scissor);
    }
}

void VulkanBackend::SetConstantColor(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (color != m_constantColor) {
        FlushBatch();
    }
    SoftwareBackendBase::SetConstantColor(color);
}

void VulkanBackend::SetRenderBuffer(uint32_t target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (target != m_activeRenderBuffer) {
        FlushBatch();
    }
    SoftwareBackendBase::SetRenderBuffer(target);
}

void VulkanBackend::SetPixelFormat(uint32_t format) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (format != m_pixelFormatOverride) {
        FlushBatch();
    }
    SoftwareBackendBase::SetPixelFormat(format);
}

void VulkanBackend::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal, uint32_t colorOther, bool colorInvert,
                                    uint32_t alphaFunc, uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther, bool alphaInvert) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (colorFunc != m_colorCombinerFunc || colorFactor != m_colorCombinerFactor || colorLocal != m_colorCombinerLocal ||
        colorOther != m_colorCombinerOther || colorInvert != m_colorCombinerInvert ||
        alphaFunc != m_alphaCombinerFunc || alphaFactor != m_alphaCombinerFactor || alphaLocal != m_alphaCombinerLocal ||
        alphaOther != m_alphaCombinerOther || alphaInvert != m_alphaCombinerInvert) {
        FlushBatch();
    }
    SoftwareBackendBase::SetCombinerMode(colorFunc, colorFactor, colorLocal, colorOther, colorInvert,
                                         alphaFunc, alphaFactor, alphaLocal, alphaOther, alphaInvert);
}

void VulkanBackend::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu < 2 && (rgbFunc != m_texCombinerRgbFunc[tmu] || rgbFactor != m_texCombinerRgbFactor[tmu] ||
                    alphaFunc != m_texCombinerAlphaFunc[tmu] || alphaFactor != m_texCombinerAlphaFactor[tmu])) {
        FlushBatch();
    }
    SoftwareBackendBase::SetTexCombinerMode(tmu, rgbFunc, rgbFactor, alphaFunc, alphaFactor);
}

void VulkanBackend::SetSTWHintState(uint32_t hintMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (hintMask != m_stwHintMask) {
        FlushBatch();
    }
    SoftwareBackendBase::SetSTWHintState(hintMask);
}

void VulkanBackend::SetChromakeyMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (mode != m_chromakeyMode) {
        FlushBatch();
    }
    SoftwareBackendBase::SetChromakeyMode(mode);
}

void VulkanBackend::SetChromakeyValue(uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (value != m_chromakeyValue) {
        FlushBatch();
    }
    SoftwareBackendBase::SetChromakeyValue(value);
}

void VulkanBackend::SetFogMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (mode != m_fogMode) {
        FlushBatch();
    }
    SoftwareBackendBase::SetFogMode(mode);
}

void VulkanBackend::SetFogColor(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (color != m_fogColor) {
        FlushBatch();
    }
    SoftwareBackendBase::SetFogColor(color);
}

void VulkanBackend::SetFogTable(const uint8_t* table) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (table && std::memcmp(table, m_fogTable, 64) != 0) {
        FlushBatch();
    }
    SoftwareBackendBase::SetFogTable(table);
}

bool VulkanBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // Flush any pending CPU LFB writes to the GPU active image before we swap/present!
    FlushLFBToGPU();

    // Enforce frame pacing and track frame timing using unified FrameTracker
    auto& tracker = TelemetryManager::GetInstance().GetFrameTracker();
    tracker.MarkFrameEnd(m_config.maxFps);
    tracker.MarkFrameStart();

    GLIDE_PROFILE_SCOPE("Vulkan::SwapBuffersSubmit");

    // 1. Flush any pending batch draws and terminate the active render pass
    if (m_batchedVertexCount > 0) {
        FlushBatch();
    }
    
    // Check if we have any pending LoadOp clears. If yes, execute them now by beginning and ending a render pass.
    uint32_t activeIdx = GetActiveGpuBufferIdx();
    if (!m_inRenderPass && (m_clearColorPending[activeIdx] || m_clearDepthPending[activeIdx])) {
        EnsureRenderPassActive();
    }
    
    if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
    }

    // 2. Mode-specific presentation / execution dispatch
    //
    // ARCHITECTURAL DESIGN NOTE (SYNC VS ASYNC PRESENTATION):
    // * WINDOWED/INTERACTIVE MODE (Synchronous):
    //   Because windowed presentation blits the CPU-side double-buffer (m_headlessPixelMap) to the SDL window,
    //   we must ensure that the CPU buffer is in perfect sync with the GPU before the frame ends.
    //   Pipelining this asynchronously with a 1-frame delay caused a startup blink (displaying unitialized 
    //   staging memory on frame 0) and out-of-sync CPU readbacks (causing interactive tests to fail initial frames).
    //   To guarantee 0-frame latency, zero blinking, and immediate correctness of interactive readbacks, 
    //   windowed mode executes SYNCHRONOUSLY by immediately awaiting the current frame's GPU fence.
    // * HEADLESS MODE (Asynchronous):
    //   Headless mode preserves 100% asynchronous, pipelined double-buffered queue execution. This guarantees 
    //   maximum performance and zero CPU stalls during automated visual regression runs, where rendering speed is 
    //   the primary metric and readbacks only occur via explicit, synchronous ReadLFB calls.
    if (!m_config.forceNoWindow && m_sdlWindow && m_headlessPixelMap) {
        // --- WINDOWED MODE PRESENTATION (Synchronous Correct Presentation) ---

        // 2.1. Cache CPU-rendered text/geometry pixels (e.g. from LFB)
        std::vector<uint32_t> cpuRenderingCache(m_headlessWidth * m_headlessHeight, 0);
        const auto* cpuPixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
        bool hasCpuRendering = false;
        size_t cpuPixelCount = 0;

        if (cpuPixels) {
            for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
                if ((cpuPixels[i] >> 24) != 0) {
                    cpuRenderingCache[i] = cpuPixels[i];
                    hasCpuRendering = true;
                    cpuPixelCount++;
                }
            }
        }
        GLIDE_LOG(DEBUG, "Vulkan", "SwapBuffers: Cache CPU pixels phase. Found " << cpuPixelCount << " non-transparent CPU pixels.");

        // 2.2. Execute GPU presentation copy for the CURRENT frame slot
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        uint32_t activeIdx = GetActiveGpuBufferIdx();
        TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, range);

        vk::BufferImageCopy region;
        region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
        
        m_commandBuffer->copyImageToBuffer(m_headlessImages[activeIdx].get(), vk::ImageLayout::eTransferSrcOptimal, m_headlessPixelBuffers[m_currentFrameSlot].get(), 1, &region);

        TransitionImageLayout(*m_commandBuffer, m_headlessImages[activeIdx].get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal, range);

        // End and submit GPU commands for the CURRENT frame slot, associating it with its fence!
        m_commandBuffer->end();
        
        // Reset the fence for the current slot before submitting!
        m_device->resetFences(1, &m_fences[m_currentFrameSlot].get());
        
        vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
        m_graphicsQueue.submit(submitInfo, m_fences[m_currentFrameSlot].get());

        // Wait for the CURRENT frame's GPU copy to complete immediately!
        if (m_device->waitForFences(1, &m_fences[m_currentFrameSlot].get(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to wait for fence of current frame slot " << m_currentFrameSlot);
        }

        // Now it is 100% safe to destroy any pipelines that were recompiled mid-frame!
        m_danglingPipelines.clear();

        // Explicitly invalidate mapped memory range of the current slot to force CPU cache refresh!
        vk::MappedMemoryRange memRange(m_headlessPixelMemories[m_currentFrameSlot].get(), 0, VK_WHOLE_SIZE);
        m_device->invalidateMappedMemoryRanges(memRange);

        // Copy the GPU-rendered framebuffer from the current slot's mapped staging memory into our CPU-side active buffer
        if (m_gpuStagingMaps[m_currentFrameSlot]) {
            std::memcpy(m_headlessPixelMap, m_gpuStagingMaps[m_currentFrameSlot], m_headlessWidth * m_headlessHeight * 4);
        }

        // Overlay CPU-rendered text/geometry back
        if (hasCpuRendering) {
            auto* dest = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
            for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
                if (cpuRenderingCache[i] != 0) {
                    dest[i] = cpuRenderingCache[i];
                }
            }
        }

        // Blit the combined frame to SDL2 window
        if (m_sdlTexture && m_sdlRenderer) {
            SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_headlessPixelMap, m_headlessWidth * 4);
            SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
            SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
        }

        // Advance to the next slot (so we use the other staging slot for the next frame)
        m_currentFrameSlot = (m_currentFrameSlot + 1) % 2;

        // Reset the texture staging offset for the new active slot back to its base!
        m_texStagingOffsets[m_currentFrameSlot] = m_currentFrameSlot * m_texStagingSlotSize;

        // Reset and begin recording the next frame's commands immediately in the new active slot!
        m_commandBuffer = &m_commandBuffers[m_currentFrameSlot].get();
        m_commandBuffer->reset({});
        m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    } else {
        // --- HEADLESS MODE (Direct Asynchronous GPU Queue Submission) ---
        m_commandBuffer->end();
        
        // Reset the fence for the current slot before submitting!
        m_device->resetFences(1, &m_fences[m_currentFrameSlot].get());
        
        vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
        m_graphicsQueue.submit(submitInfo, m_fences[m_currentFrameSlot].get());

        // Wait for the next slot (the previous frame's slot) to be free before we start recording into it!
        uint32_t nextSlot = (m_currentFrameSlot + 1) % 2;
        if (m_device->waitForFences(1, &m_fences[nextSlot].get(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to wait for fence of next frame slot " << nextSlot);
        }

        // Advance the frame slot
        m_currentFrameSlot = nextSlot;

        // Reset the texture staging offset for the new active slot back to its base!
        m_texStagingOffsets[m_currentFrameSlot] = m_currentFrameSlot * m_texStagingSlotSize;

        // Reset and begin recording the next frame's commands immediately in the new active slot!
        m_commandBuffer = &m_commandBuffers[m_currentFrameSlot].get();
        m_commandBuffer->reset({});
        m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    }

    // 3. Swap the CPU buffer indices for double-buffering
    if (m_headlessPixelMap) {
        std::swap(m_frontBufferIdx, m_backBufferIdx);
        std::swap(m_lfbBufferDirty[0], m_lfbBufferDirty[1]); // Swap dirty flags alongside buffer indices!
        if (m_activeRenderBuffer == 0) { // FRONTBUFFER
            m_headlessPixelMap = m_cpuBuffers[m_frontBufferIdx].data();
        } else { // BACKBUFFER
            m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
        }
    }

    // 4. Swap the GPU color targets for physical double-buffering!
    std::swap(m_headlessImages[0], m_headlessImages[1]);
    std::swap(m_headlessImageMemories[0], m_headlessImageMemories[1]);
    std::swap(m_headlessImageViews[0], m_headlessImageViews[1]);
    std::swap(m_headlessFramebuffers[0], m_headlessFramebuffers[1]);

    return true;
}

bool VulkanBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // --- STEP 1: Cache CPU-rendered text/geometry pixels ---
    std::vector<uint32_t> cpuRenderingCache(m_headlessWidth * m_headlessHeight, 0);
    
    // Resolve the CPU source buffer based on the requested buffer (0 = FRONT, 1 = BACK)
    void* readDst = (buffer == 0) ? m_cpuBuffers[m_frontBufferIdx].data() : m_cpuBuffers[m_backBufferIdx].data();
    const auto* cpuPixels = reinterpret_cast<const uint32_t*>(readDst);
    
    bool hasCpuRendering = false;
    size_t cpuPixelCount = 0;

    if (cpuPixels) {
        for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
            // If the pixel is not transparent background (alpha != 0)
            if ((cpuPixels[i] >> 24) != 0) {
                cpuRenderingCache[i] = cpuPixels[i];
                hasCpuRendering = true;
                cpuPixelCount++;
            }
        }
    }
    GLIDE_LOG(DEBUG, "Vulkan", "ReadLFB: Cache CPU pixels phase. Found " << cpuPixelCount << " non-transparent CPU pixels.");

    // 1. Flush pending batch draws
    if (m_batchedVertexCount > 0) {
        FlushBatch();
    }

    // Resolve the GPU image index based on the requested buffer (0 = FRONT, 1 = BACK)
    uint32_t readIdx = (buffer == m_activeRenderBuffer) ? GetActiveGpuBufferIdx() : (1u - GetActiveGpuBufferIdx());

    // If the target read FBO has pending clears, we must execute them first!
    if (!m_inRenderPass && (m_clearColorPending[readIdx] || m_clearDepthPending[readIdx])) {
        GLIDE_LOG(DEBUG, "Vulkan", "ReadLFB: Executing pending clears on read target FBO " << readIdx << "...");
        uint32_t origActive = m_activeRenderBuffer;
        m_activeRenderBuffer = buffer; // Temporarily set active buffer to target buffer so EnsureRenderPassActive targets readIdx!
        EnsureRenderPassActive();
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
        m_activeRenderBuffer = origActive; // Restore
    }

    if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
    }

    // 2. Transition layout from ColorAttachmentOptimal to TransferSrcOptimal
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, range);

    // 3. Record copy from GPU image to staging buffer
    vk::BufferImageCopy region;
    region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
    m_commandBuffer->copyImageToBuffer(m_headlessImages[readIdx].get(), vk::ImageLayout::eTransferSrcOptimal, m_headlessPixelBuffers[m_currentFrameSlot].get(), 1, &region);

    // 4. End and submit the command buffer immediately
    m_commandBuffer->end();
    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
    m_graphicsQueue.submit(submitInfo, {});
    m_device->waitIdle(); // Block CPU until GPU finishes!

    // Now it is 100% safe to destroy any pipelines that were recompiled mid-frame!
    m_danglingPipelines.clear();

    // Copy the GPU-rendered framebuffer from mapped staging memory into our CPU-side active buffer
    if (m_gpuStagingMaps[m_currentFrameSlot] && readDst) {
        std::memcpy(readDst, m_gpuStagingMaps[m_currentFrameSlot], m_headlessWidth * m_headlessHeight * 4);
    }

    // --- STEP 3: Overlay CPU-rendered text/geometry back ---
    if (hasCpuRendering && readDst) {
        auto* dest = reinterpret_cast<uint32_t*>(readDst);
        for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
            if (cpuRenderingCache[i] != 0) {
                dest[i] = cpuRenderingCache[i];
            }
        }
    }

    // 6. Reset and begin a new command buffer for subsequent commands in the frame!
    m_commandBuffer->reset({});
    m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // 7. Transition image layout back to ColorAttachmentOptimal so it is ready for any subsequent draw calls!
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[readIdx].get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal, range);

    // 8. Now call the base class ReadLFB to copy the synchronized pixels to the client buffer!
    bool result = SoftwareBackendBase::ReadLFB(buffer, srcX, srcY, srcWidth, srcHeight, dstStride, dstData);

    // 9. Restore the CPU-only framebuffer state from our cache, completely wiping out GPU pollution while preserving CPU LFB writes!
    if (readDst) {
        auto* dest = reinterpret_cast<uint32_t*>(readDst);
        std::fill_n(dest, m_headlessWidth * m_headlessHeight, m_lastCpuClearColor);
        for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
            if (cpuRenderingCache[i] != 0) {
                dest[i] = cpuRenderingCache[i];
            }
        }
    }
    return result;
}

void VulkanBackend::PollEvents() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            GLIDE_LOG(DEBUG, "Vulkan", "SDL window quit request.");
        } else if (event.type == SDL_KEYDOWN) {
            GLIDE_LOG(DEBUG, "Vulkan", "SDL window keydown captured. Setting key hit hook flag.");
            g_glideWrapperSdlKeyHit = 1;
        }
    }
}

void VulkanBackend::SstIdle() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_device) {
        // Blocks the host CPU until all submitted GPU commands in all queues have finished execution!
        m_device->waitIdle();
    }
}


// Private Vulkan Setup Helpers (Identical to before)
bool VulkanBackend::CreateInstance() {
    vk::ApplicationInfo appInfo("glide-ng", VK_MAKE_VERSION(1, 0, 0), "No Engine", VK_MAKE_VERSION(1, 0, 0), VK_API_VERSION_1_1);
    vk::InstanceCreateInfo createInfo({}, &appInfo);

    std::vector<const char*> requiredExtensions;

    // Load Vulkan instance extensions required by SDL2 for surface creation!
    const char* display = std::getenv("DISPLAY");
    if (!m_config.forceNoWindow && display && display[0] != '\0') {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            GLIDE_LOG(WARN, "Vulkan", "SDL_Init video failed in CreateInstance: " << SDL_GetError());
        } else {
            // Explicitly load the Vulkan driver library via SDL to initialize its internal function pointers!
            if (SDL_Vulkan_LoadLibrary(nullptr) < 0) {
                GLIDE_LOG(WARN, "Vulkan", "SDL_Vulkan_LoadLibrary failed in CreateInstance: " << SDL_GetError());
            } else {
                unsigned int extensionCount = 0;
                if (SDL_Vulkan_GetInstanceExtensions(nullptr, &extensionCount, nullptr)) {
                    std::vector<const char*> sdlExtensions(extensionCount);
                    if (SDL_Vulkan_GetInstanceExtensions(nullptr, &extensionCount, sdlExtensions.data())) {
                        for (const auto& ext : sdlExtensions) {
                            requiredExtensions.push_back(ext);
                            GLIDE_LOG(INFO, "Vulkan", "Loaded SDL Vulkan extension: " << ext);
                        }
                    }
                } else {
                    GLIDE_LOG(WARN, "Vulkan", "SDL_Vulkan_GetInstanceExtensions count query failed: " << SDL_GetError());
                }
            }
        }
    } else {
        GLIDE_LOG(INFO, "Vulkan", "No active DISPLAY environment variable or forceNoWindow is set. Skipping SDL Vulkan instance extensions.");
    }

    createInfo.setEnabledExtensionCount(static_cast<uint32_t>(requiredExtensions.size()));
    createInfo.setPpEnabledExtensionNames(requiredExtensions.data());

    if (enableValidationLayers) {
        createInfo.setEnabledLayerCount(static_cast<uint32_t>(validationLayers.size()));
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
        GLIDE_LOG(CRITICAL, "Vulkan", "Zero Vulkan compatible hardware/software devices found on host.");
        return false;
    }
    m_physicalDevice = devices[0];
    vk::PhysicalDeviceProperties props = m_physicalDevice.getProperties();

    std::ostringstream logStream;
    logStream << "\n--- Active Vulkan Execution Adapter ---\n"
              << "  Adapter     : " << props.deviceName.data() << "\n"
              << "    API Ver   : " << VK_VERSION_MAJOR(props.apiVersion) << "."
              << VK_VERSION_MINOR(props.apiVersion) << "." << VK_VERSION_PATCH(props.apiVersion) << "\n"
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
        GLIDE_LOG(CRITICAL, "Vulkan", "Vulkan adapter lacks graphics command submission queue.");
        return false;
    }

    float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, m_graphicsQueueFamilyIndex, 1, &priority);

    // Query and enable dynamic depth clamping to prevent out-of-bounds Z clipping
    vk::PhysicalDeviceFeatures deviceFeatures;
    auto supportedFeatures = m_physicalDevice.getFeatures();
    if (supportedFeatures.depthClamp) {
        deviceFeatures.depthClamp = true;
        m_depthClampEnabled = true;
        GLIDE_LOG(INFO, "Vulkan", "Vulkan Depth Clamping physical device feature enabled.");
    } else {
        m_depthClampEnabled = false;
        GLIDE_LOG(WARN, "Vulkan", "Vulkan Depth Clamping NOT supported on host device!");
    }

    vk::DeviceCreateInfo deviceInfo({}, 1, &queueInfo);
    deviceInfo.setPEnabledFeatures(&deviceFeatures);

    if (enableValidationLayers) {
        deviceInfo.setEnabledLayerCount(static_cast<uint32_t>(validationLayers.size()));
        deviceInfo.setPpEnabledLayerNames(validationLayers.data());
    } else {
        deviceInfo.setEnabledLayerCount(0);
    }

    m_device = m_physicalDevice.createDeviceUnique(deviceInfo);
    m_graphicsQueue = m_device->getQueue(m_graphicsQueueFamilyIndex, 0);

    // 2.5. Initialize persistent Vulkan pipeline cache from disk!
    std::string cachePath = "";
    const char* envCache = std::getenv("GLIDE_WRAPPER_CACHE_PATH");
    if (envCache && envCache[0] != '\0') {
        cachePath = envCache;
    } else {
        cachePath = ".glide-vulkan-cache.bin";
    }

    std::vector<char> cacheData;
    std::ifstream cacheFile(cachePath, std::ios::binary | std::ios::ate);
    if (cacheFile.is_open()) {
        std::streamsize size = cacheFile.tellg();
        cacheFile.seekg(0, std::ios::beg);
        cacheData.resize(size);
        if (cacheFile.read(cacheData.data(), size)) {
            GLIDE_LOG(INFO, "Vulkan", "Successfully loaded persistent pipeline cache binary (" << size << " bytes) from " << cachePath);
        } else {
            cacheData.clear();
            GLIDE_LOG(WARN, "Vulkan", "Failed to read pipeline cache file: " << cachePath);
        }
        cacheFile.close();
    } else {
        GLIDE_LOG(INFO, "Vulkan", "No pre-existing pipeline cache file found at " << cachePath << ". Starting with a fresh cache.");
    }

    vk::PipelineCacheCreateInfo cacheInfo;
    if (!cacheData.empty()) {
        cacheInfo.initialDataSize = cacheData.size();
        cacheInfo.pInitialData = cacheData.data();
    }
    m_vkPipelineCache = m_device->createPipelineCacheUnique(cacheInfo);

    vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_graphicsQueueFamilyIndex);
    vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled); // Start signaled so first wait succeeds immediately!

    for (uint32_t i = 0; i < 2; ++i) {
        m_commandPools[i] = m_device->createCommandPoolUnique(poolInfo);
        
        vk::CommandBufferAllocateInfo allocInfo(m_commandPools[i].get(), vk::CommandBufferLevel::ePrimary, 1);
        auto bufs = m_device->allocateCommandBuffersUnique(allocInfo);
        m_commandBuffers[i] = std::move(bufs[0]);
        
        m_fences[i] = m_device->createFenceUnique(fenceInfo);
    }

    m_currentFrameSlot = 0;
    m_commandBuffer = &m_commandBuffers[0].get();

    // 2.6. Pre-allocate and permanently map persistent Texture Staging Buffer (16MB)
    const uint32_t stagingSize = 16 * 1024 * 1024; // 16MB
    vk::BufferCreateInfo texStagingInfo({}, stagingSize, vk::BufferUsageFlagBits::eTransferSrc);
    m_texStagingBuffer = m_device->createBufferUnique(texStagingInfo);
    
    auto texStagingReqs = m_device->getBufferMemoryRequirements(m_texStagingBuffer.get());
    uint32_t texStagingTypeIndex = FindMemoryType(texStagingReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo texStagingAlloc(texStagingReqs.size, texStagingTypeIndex);
    m_texStagingMemory = m_device->allocateMemoryUnique(texStagingAlloc);
    m_device->bindBufferMemory(m_texStagingBuffer.get(), m_texStagingMemory.get(), 0);
    
    m_texStagingMap = m_device->mapMemory(m_texStagingMemory.get(), 0, stagingSize);
    
    // Initialize double-buffered offsets
    m_texStagingOffsets[0] = 0;
    m_texStagingOffsets[1] = 8 * 1024 * 1024; // Halfway

    return true;
}

bool VulkanBackend::CreateRenderPass() {
    // 1. Color Attachment Description
    vk::AttachmentDescription colorAttachment({}, m_colorFormat, vk::SampleCountFlagBits::e1,
                                            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal);

    vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);

    // 2. Depth Attachment Description
    vk::AttachmentDescription depthAttachment({}, m_depthFormat, vk::SampleCountFlagBits::e1,
                                            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    // 3. Subpass Description (binds both color reference and depth stencil reference)
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorRef, nullptr, &depthRef);

    // Setup subpass dependencies to enforce strict GPU synchronization between clear, draw, and copy stages
    std::array<vk::SubpassDependency, 2> dependencies;

    // Dependency 1: Sync Clear/Transfer writes to Color/Depth Attachments
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;
    dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    // Dependency 2: Sync Color/Depth Attachment writes to Transfer/Copy reads (in SwapBuffers)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eTransfer;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    vk::RenderPassCreateInfo passInfo({}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1, &subpass, static_cast<uint32_t>(dependencies.size()), dependencies.data());
    m_renderPass = m_device->createRenderPassUnique(passInfo);
    return true;
}
bool VulkanBackend::CreateHeadlessRenderTarget(uint32_t width, uint32_t height) {
    // 1. Allocate depth image for hardware depth-testing support (Shared!)
    vk::ImageCreateInfo depthImageInfo({}, vk::ImageType::e2D, m_depthFormat,
                                      vk::Extent3D(width, height, 1), 1, 1,
                                      vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                      vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst);
    m_depthImage = m_device->createImageUnique(depthImageInfo);

    auto depthMemReqs = m_device->getImageMemoryRequirements(m_depthImage.get());
    uint32_t depthTypeIndex = FindMemoryType(depthMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo depthAllocInfo(depthMemReqs.size, depthTypeIndex);
    m_depthImageMemory = m_device->allocateMemoryUnique(depthAllocInfo);
    m_device->bindImageMemory(m_depthImage.get(), m_depthImageMemory.get(), 0);

    vk::ImageViewCreateInfo depthViewInfo({}, m_depthImage.get(), vk::ImageViewType::e2D, m_depthFormat,
                                         {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    m_depthImageView = m_device->createImageViewUnique(depthViewInfo);

    // 2. Allocate Double-Buffered Color Images and Framebuffers (i=0 for BACK, i=1 for FRONT)
    for (int i = 0; i < 2; ++i) {
        vk::ImageCreateInfo imageInfo({}, vk::ImageType::e2D, m_colorFormat,
                                     vk::Extent3D(width, height, 1), 1, 1,
                                     vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                     vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

        m_headlessImages[i] = m_device->createImageUnique(imageInfo);

        auto memReqs = m_device->getImageMemoryRequirements(m_headlessImages[i].get());
        uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);

        m_headlessImageMemories[i] = m_device->allocateMemoryUnique(allocInfo);
        m_device->bindImageMemory(m_headlessImages[i].get(), m_headlessImageMemories[i].get(), 0);

        vk::ImageViewCreateInfo viewInfo({}, m_headlessImages[i].get(), vk::ImageViewType::e2D, m_colorFormat,
                                        {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        m_headlessImageViews[i] = m_device->createImageViewUnique(viewInfo);

        // Create framebuffer with both color and depth attachments
        std::array<vk::ImageView, 2> attachments = {
            m_headlessImageViews[i].get(),
            m_depthImageView.get()
        };
        vk::FramebufferCreateInfo fbInfo({}, m_renderPass.get(), static_cast<uint32_t>(attachments.size()), attachments.data(), width, height, 1);
        m_headlessFramebuffers[i] = m_device->createFramebufferUnique(fbInfo);
    }

    // Create the CPU staging buffers (Double-Buffered)
    vk::BufferCreateInfo bufInfo({}, width * height * 4, vk::BufferUsageFlagBits::eTransferDst);
    
    for (uint32_t i = 0; i < 2; ++i) {
        m_headlessPixelBuffers[i] = m_device->createBufferUnique(bufInfo);

        auto bufReqs = m_device->getBufferMemoryRequirements(m_headlessPixelBuffers[i].get());
        uint32_t bufTypeIndex = FindMemoryType(bufReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        vk::MemoryAllocateInfo bufAlloc(bufReqs.size, bufTypeIndex);

        m_headlessPixelMemories[i] = m_device->allocateMemoryUnique(bufAlloc);
        m_device->bindBufferMemory(m_headlessPixelBuffers[i].get(), m_headlessPixelMemories[i].get(), 0);

        m_gpuStagingMaps[i] = m_device->mapMemory(m_headlessPixelMemories[i].get(), 0, width * height * 4);
        if (!m_gpuStagingMaps[i]) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to map headless host memory map for slot " << i << "!");
            return false;
        }
        
        // Pre-initialize GPU staging buffer to opaque black
        std::memset(m_gpuStagingMaps[i], 0xFF, width * height * 4);
    }

    // Allocate CPU framebuffers for double-buffering
    AllocateCpuBuffers(width, height);

    return true;
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    auto memProps = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    GLIDE_LOG(CRITICAL, "Vulkan", "No matching physical memory heap type filter found!");
    return 0;
}

void VulkanBackend::TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range) {
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eTransferSrcOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else {
        barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        sourceStage = vk::PipelineStageFlagBits::eAllCommands;
        destinationStage = vk::PipelineStageFlagBits::eAllCommands;
    }

    cmd.pipelineBarrier(sourceStage, destinationStage, {}, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanBackend::FlushLFBToGPU() {
    if (!m_lfbDirty) return;

    GLIDE_LOG(DEBUG, "Vulkan", "FlushLFBToGPU: Checking dirty buffers...");

    // Resolve the GPU image indices for FRONT and BACK buffers
    uint32_t gpuFrontIdx = (0 == m_activeRenderBuffer) ? GetActiveGpuBufferIdx() : (1u - GetActiveGpuBufferIdx());
    uint32_t gpuBackIdx = (1 == m_activeRenderBuffer) ? GetActiveGpuBufferIdx() : (1u - GetActiveGpuBufferIdx());

    // We will flush the dirty buffer. In practice, only one is dirty at a time.
    // If both are dirty, we warn and flush the back buffer.
    uint32_t bufferToFlush = 2; // 2 = none
    uint32_t targetGpuIdx = 0;
    void* srcCpuData = nullptr;

    if (m_lfbBufferDirty[0] && m_lfbBufferDirty[1]) {
        GLIDE_LOG(WARN, "Vulkan", "FlushLFBToGPU: Both FRONT and BACK LFB buffers are dirty in the same flush. Flushing BACK.");
        bufferToFlush = 1;
        targetGpuIdx = gpuBackIdx;
        srcCpuData = m_cpuBuffers[m_backBufferIdx].data();
    } else if (m_lfbBufferDirty[0]) {
        bufferToFlush = 0;
        targetGpuIdx = gpuFrontIdx;
        srcCpuData = m_cpuBuffers[m_frontBufferIdx].data();
    } else if (m_lfbBufferDirty[1]) {
        bufferToFlush = 1;
        targetGpuIdx = gpuBackIdx;
        srcCpuData = m_cpuBuffers[m_backBufferIdx].data();
    }

    if (bufferToFlush == 2 || !srcCpuData) {
        m_lfbDirty = false;
        m_lfbBufferDirty[0] = false;
        m_lfbBufferDirty[1] = false;
        return;
    }

    // If there are pending clears on the target FBO, we MUST execute them on the GPU first!
    // Otherwise, the subsequent copyBufferToImage will be overwritten when the clears are eventually executed!
    if (!m_inRenderPass && (m_clearColorPending[bufferToFlush] || m_clearDepthPending[bufferToFlush])) {
        GLIDE_LOG(DEBUG, "Vulkan", "FlushLFBToGPU: Executing pending clears on target buffer " << bufferToFlush << " before LFB upload...");
        uint32_t origActive = m_activeRenderBuffer;
        m_activeRenderBuffer = bufferToFlush; // Temporarily set active buffer so EnsureRenderPassActive targets bufferToFlush!
        EnsureRenderPassActive();
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
        m_activeRenderBuffer = origActive; // Restore
    }

    // 1. If we are inside an active render pass, we must end it first!
    if (m_inRenderPass) {
        FlushBatch();
        GLIDE_LOG(DEBUG, "Vulkan", "FlushLFBToGPU: Ending active render pass to perform CPU->GPU transfer...");
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
    }

    // Copy from CPU-side active buffer to GPU staging memory
    if (m_gpuStagingMaps[m_currentFrameSlot]) {
        std::memcpy(m_gpuStagingMaps[m_currentFrameSlot], srcCpuData, m_headlessWidth * m_headlessHeight * 4);
    }

    // 2. Transition image to TransferDstOptimal
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[targetGpuIdx].get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferDstOptimal, range);

    // 3. Record copy from buffer to image
    vk::BufferImageCopy region;
    region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);

    // Flush mapped memory to ensure host writes are visible to GPU
    vk::MappedMemoryRange memRange(m_headlessPixelMemories[m_currentFrameSlot].get(), 0, VK_WHOLE_SIZE);
    m_device->flushMappedMemoryRanges(memRange);

    m_commandBuffer->copyBufferToImage(m_headlessPixelBuffers[m_currentFrameSlot].get(), m_headlessImages[targetGpuIdx].get(), vk::ImageLayout::eTransferDstOptimal, 1, &region);

    // 4. Transition image back to ColorAttachmentOptimal
    TransitionImageLayout(*m_commandBuffer, m_headlessImages[targetGpuIdx].get(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal, range);

    // 5. Clear the dirty flags
    m_lfbDirty = false;
    m_lfbBufferDirty[0] = false;
    m_lfbBufferDirty[1] = false;
}

// --- Phase 2: Dynamic Shader Modules & Pipeline State Creation ---
bool VulkanBackend::CreateShaderModules() {
    try {
        vk::ShaderModuleCreateInfo vertInfo({}, color_vert_spv_size, color_vert_spv);
        m_vertShaderModule = m_device->createShaderModuleUnique(vertInfo);

        vk::ShaderModuleCreateInfo fragInfo({}, color_frag_spv_size, color_frag_spv);
        m_fragShaderModule = m_device->createShaderModuleUnique(fragInfo);

        GLIDE_LOG(INFO, "Vulkan", "Loaded build-time compiled SPIR-V vertex and fragment shader modules.");
        return true;
    } catch (const std::exception& e) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan Shader Modules: " << e.what());
        return false;
    }
}

PipelineGroup VulkanBackend::CompileGraphicsPipelines(const PipelineStateKey& key) {
    PipelineGroup group;
    try {
        // Shader Stages
        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, m_vertShaderModule.get(), "main"),
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, m_fragShaderModule.get(), "main")
        };

        // Vertex Input Bindings (matches ModernVertex exactly)
        vk::VertexInputBindingDescription bindingDesc(0, sizeof(ModernVertex), vk::VertexInputRate::eVertex);
        std::array<vk::VertexInputAttributeDescription, 4> attrDescs = {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, tex)),
            vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(ModernVertex, tmu_oow))
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 1, &bindingDesc, static_cast<uint32_t>(attrDescs.size()), attrDescs.data());

        // Input Assembly Topology (Primitive = PointList for test01!)
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::ePointList, false);

        // Dynamic Viewport and Scissor States (prevents pipeline rebuilds on resolution change)
        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());
        vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);

        // Rasterization Configuration (Dynamic Depth Clamping prevents out-of-bounds Z clipping!)
        vk::CullModeFlags vkCullMode = vk::CullModeFlagBits::eNone;
        switch (key.cullMode) {
            case 0: vkCullMode = vk::CullModeFlagBits::eNone; break;  // GR_CULL_DISABLE
            case 1: vkCullMode = vk::CullModeFlagBits::eBack; break;   // GR_CULL_NEGATIVE (cull CCW / Back face)
            case 2: vkCullMode = vk::CullModeFlagBits::eFront; break;  // GR_CULL_POSITIVE (cull CW / Front face)
            default: break;
        }

        vk::FrontFace frontFace = (key.sstOrigin == 1) ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise;
        vk::PipelineRasterizationStateCreateInfo rasterizer({}, m_depthClampEnabled, false, vk::PolygonMode::eFill, vkCullMode,
                                                           frontFace, false, 0.0f, 0.0f, 0.0f, 1.0f);

        // Multisample Configuration (none)
        vk::PipelineMultisampleStateCreateInfo multisampling({}, vk::SampleCountFlagBits::e1, false, 1.0f, nullptr, false, false);

        // Depth Stencil Configuration based on Glide's active states
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        if (key.depthMode != 0) {
            depthStencil.depthTestEnable = true;
            depthStencil.depthWriteEnable = key.depthMask ? true : false;

            vk::CompareOp op = vk::CompareOp::eLess;
            switch (key.depthCompareOp) {
                case 0: op = vk::CompareOp::eNever; break;
                case 1: op = vk::CompareOp::eLess; break;
                case 2: op = vk::CompareOp::eEqual; break;
                case 3: op = vk::CompareOp::eLessOrEqual; break;
                case 4: op = vk::CompareOp::eGreater; break;
                case 5: op = vk::CompareOp::eNotEqual; break;
                case 6: op = vk::CompareOp::eGreaterOrEqual; break;
                case 7: op = vk::CompareOp::eAlways; break;
                default: break;
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
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        
        colorBlendAttachment.blendEnable = true;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcColorBlendFactor = MapBlendFactorVulkan(key.rgbSrcBlend, false);
        colorBlendAttachment.dstColorBlendFactor = MapBlendFactorVulkan(key.rgbDstBlend, true);
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = MapBlendFactorVulkan(key.alphaSrcBlend, false);
        colorBlendAttachment.dstAlphaBlendFactor = MapBlendFactorVulkan(key.alphaDstBlend, true);

        vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment, {0.0f, 0.0f, 0.0f, 0.0f});

        // Push Constants Range (passes viewport and constant color configuration to both shader stages)
        if (!m_pipelineLayout) {
            vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstants));
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 1, &m_descriptorSetLayout.get(), 1, &pushRange);
            m_pipelineLayout = m_device->createPipelineLayoutUnique(pipelineLayoutInfo);
        }

        // Assemble Graphics Pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo({}, 2, shaderStages, &vertexInputInfo, &inputAssembly, nullptr, &viewportState,
                                                   &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState,
                                                   m_pipelineLayout.get(), m_renderPass.get(), 0, {}, -1);

        // 1. Compile Point Pipeline using persistent Vulkan pipeline cache!
        auto pointResult = m_device->createGraphicsPipelineUnique(m_vkPipelineCache.get(), pipelineInfo);
        if (pointResult.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to compile Vulkan Point pipeline!");
        }

        // 2. Compile Line Pipeline using persistent Vulkan pipeline cache!
        inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        auto lineResult = m_device->createGraphicsPipelineUnique(m_vkPipelineCache.get(), pipelineInfo);
        if (lineResult.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to compile Vulkan Line pipeline!");
        }

        // 3. Compile Triangle Pipeline using persistent Vulkan pipeline cache!
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        auto triangleResult = m_device->createGraphicsPipelineUnique(m_vkPipelineCache.get(), pipelineInfo);
        if (triangleResult.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to compile Vulkan Triangle pipeline!");
        }

        group.point = std::move(pointResult.value);
        group.line = std::move(lineResult.value);
        group.triangle = std::move(triangleResult.value);

        GLIDE_LOG(INFO, "Vulkan", "Successfully compiled graphics pipeline triplet.");
    } catch (const std::exception& e) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to compile Vulkan graphics pipelines: " << e.what());
    }
    return group;
}

void VulkanBackend::UpdateActivePipelines() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_device) return;

    PipelineStateKey key;
    key.cullMode = m_cullMode;
    key.sstOrigin = m_sstOrigin;
    key.depthMode = m_depthMode;
    key.depthMask = m_depthMask;
    key.depthCompareOp = m_depthCompareOp;
    key.rgbSrcBlend = m_rgbSrcBlend;
    key.rgbDstBlend = m_rgbDstBlend;
    key.alphaSrcBlend = m_alphaSrcBlend;
    key.alphaDstBlend = m_alphaDstBlend;

    auto it = m_pipelineCache.find(key);
    if (it == m_pipelineCache.end()) {
        GLIDE_LOG(INFO, "Vulkan", "Pipeline Cache Miss! Compiling new pipeline triplet for state combination: "
                  "Cull=" << key.cullMode << ", Origin=" << key.sstOrigin << ", DepthMode=" << key.depthMode
                  << ", DepthMask=" << key.depthMask << ", DepthOp=" << key.depthCompareOp
                  << ", RGBBlend(" << key.rgbSrcBlend << "," << key.rgbDstBlend << ")"
                  << ", AlphaBlend(" << key.alphaSrcBlend << "," << key.alphaDstBlend << ")");

        PipelineGroup group = CompileGraphicsPipelines(key);
        if (!group.point || !group.line || !group.triangle) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to compile new pipelines in UpdateActivePipelines!");
            return;
        }

        auto insertResult = m_pipelineCache.emplace(key, std::move(group));
        it = insertResult.first;
    }

    m_activePointPipeline = it->second.point.get();
    m_activeLinePipeline = it->second.line.get();
    m_activeTrianglePipeline = it->second.triangle.get();
}

bool VulkanBackend::CreateGraphicsPipelines() {
    UpdateActivePipelines();
    return m_activeTrianglePipeline ? true : false;
}

void VulkanBackend::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "Vulkan", "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1] << ", " << pt.pos[2] << ")");

    // 1. Determine active pipeline & descriptor set
    UpdateActivePipelines();
    vk::Pipeline reqPipeline = m_activePointPipeline;

    vk::DescriptorSet reqDescSet = m_dummyTexture.descriptorSet;
    if (m_boundTexAddress[0] != 0xFFFFFFFF) {
        auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
        if (it != m_vulkanTextures.end()) {
            reqDescSet = it->second.descriptorSet;
        }
    }

    // 2. Check if we need to flush the active batch
    bool stateChanged = (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
    bool batchEmpty = (m_batchedVertexCount == 0);
    bool bufferFull = (m_vertexBufferOffset + sizeof(ModernVertex) > m_vertexBufferSize);

    if (stateChanged || batchEmpty || bufferFull) {
        if (stateChanged || bufferFull) {
            FlushBatch();
        }
        if (bufferFull) {
            GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer wrap-around during DrawPoint!");
            m_vertexBufferOffset = 0;
        }
        
        m_batchedPipeline = reqPipeline;
        m_batchedDescSet = reqDescSet;
        m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }

    // 3. Append vertex to mapped buffer
    if (!m_vertexBufferMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawPoint!");
        return;
    }
    auto* dest = reinterpret_cast<ModernVertex*>(reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
        *dest = pt;
        dest->pos[1] = (float)m_headlessHeight - pt.pos[1];
    } else {
        *dest = pt;
    }

    // 4. Update batch & offset tracking
    m_batchedVertexCount += 1;
    m_vertexBufferOffset += sizeof(ModernVertex);
}

void VulkanBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "Vulkan", "DrawLine GPU draw. V1: (" << v1.pos[0] << ", " << v1.pos[1] << "), V2: (" << v2.pos[0] << ", " << v2.pos[1] << ")");

    // 1. Determine active pipeline & descriptor set
    UpdateActivePipelines();
    vk::Pipeline reqPipeline = m_activeLinePipeline;

    vk::DescriptorSet reqDescSet = m_dummyTexture.descriptorSet;
    if (m_boundTexAddress[0] != 0xFFFFFFFF) {
        auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
        if (it != m_vulkanTextures.end()) {
            reqDescSet = it->second.descriptorSet;
        }
    }

    // 2. Check if we need to flush the active batch
    bool stateChanged = (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
    bool batchEmpty = (m_batchedVertexCount == 0);
    bool bufferFull = (m_vertexBufferOffset + 2 * sizeof(ModernVertex) > m_vertexBufferSize);

    if (stateChanged || batchEmpty || bufferFull) {
        if (stateChanged || bufferFull) {
            FlushBatch();
        }
        if (bufferFull) {
            GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer wrap-around during DrawLine!");
            m_vertexBufferOffset = 0;
        }
        
        m_batchedPipeline = reqPipeline;
        m_batchedDescSet = reqDescSet;
        m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }

    // 3. Append vertices to mapped buffer
    if (!m_vertexBufferMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawLine!");
        return;
    }
    auto* dest = reinterpret_cast<ModernVertex*>(reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        dest[0] = v1; dest[0].pos[1] = (float)m_headlessHeight - v1.pos[1];
        dest[1] = v2; dest[1].pos[1] = (float)m_headlessHeight - v2.pos[1];
    } else {
        dest[0] = v1;
        dest[1] = v2;
    }

    // 4. Update batch & offset tracking
    m_batchedVertexCount += 2;
    m_vertexBufferOffset += 2 * sizeof(ModernVertex);
}

void VulkanBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "Vulkan", "DrawTriangle GPU draw. A: (" << a.pos[0] << ", " << a.pos[1] << "), B: (" << b.pos[0] << ", " << b.pos[1] << "), C: (" << c.pos[0] << ", " << c.pos[1] << ")");

    // 1. Determine active pipeline & descriptor set
    UpdateActivePipelines();
    vk::Pipeline reqPipeline = m_activeTrianglePipeline;

    vk::DescriptorSet reqDescSet = m_dummyTexture.descriptorSet;
    if (m_boundTexAddress[0] != 0xFFFFFFFF) {
        auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
        if (it != m_vulkanTextures.end()) {
            reqDescSet = it->second.descriptorSet;
        }
    }

    // 2. Check if we need to flush the active batch
    bool stateChanged = (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
    bool batchEmpty = (m_batchedVertexCount == 0);
    bool bufferFull = (m_vertexBufferOffset + 3 * sizeof(ModernVertex) > m_vertexBufferSize);

    if (stateChanged || batchEmpty || bufferFull) {
        if (stateChanged || bufferFull) {
            FlushBatch();
        }
        if (bufferFull) {
            GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer wrap-around during DrawTriangle!");
            m_vertexBufferOffset = 0;
        }
        
        m_batchedPipeline = reqPipeline;
        m_batchedDescSet = reqDescSet;
        m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }

    // 3. Append vertices to mapped buffer
    if (!m_vertexBufferMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawTriangle!");
        return;
    }
    auto* dest = reinterpret_cast<ModernVertex*>(reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        dest[0] = a; dest[0].pos[1] = (float)m_headlessHeight - a.pos[1];
        dest[1] = b; dest[1].pos[1] = (float)m_headlessHeight - b.pos[1];
        dest[2] = c; dest[2].pos[1] = (float)m_headlessHeight - c.pos[1];
    } else {
        dest[0] = a;
        dest[1] = b;
        dest[2] = c;
    }

    // 4. Update batch & offset tracking
    m_batchedVertexCount += 3;
    m_vertexBufferOffset += 3 * sizeof(ModernVertex);
}

void VulkanBackend::FlushBatch() {
    if (m_batchedVertexCount == 0) return;

    GLIDE_PROFILE_SCOPE("Vulkan::FlushBatch");
    TelemetryManager::GetInstance().IncrementTrianglesDrawn(m_batchedVertexCount / 3);

    GLIDE_LOG(DEBUG, "Vulkan", "FlushBatch: drawing " << m_batchedVertexCount << " vertices. FirstVertex=" << m_batchedFirstVertex);

    EnsureRenderPassActive();

    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, m_batchedPipeline);
    m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout.get(), 0, 1, &m_batchedDescSet, 0, nullptr);

    PushConstants pcs;
    PopulatePushConstants(pcs);
    m_commandBuffer->pushConstants<PushConstants>(m_pipelineLayout.get(), 
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pcs);

    m_commandBuffer->draw(m_batchedVertexCount, 1, m_batchedFirstVertex, 0);

    m_batchedVertexCount = 0;
}

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

void VulkanBackend::UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_device) return;

    // Only TMU 0 is currently supported in this backend
    if (tmu != 0) {
        GLIDE_LOG(WARN, "Vulkan", "UploadTexture ignored for unsupported TMU " << tmu);
        return;
    }

    // 1. Calculate incoming texture hash
    uint64_t incomingHash = CalculateTextureHash(tex);

    // 2. Check if texture already exists at this address and has matching data
    auto it = m_vulkanTextures.find(startAddress);
    if (it != m_vulkanTextures.end()) {
        if (it->second.dataHash == incomingHash) {
            GLIDE_LOG(DEBUG, "Vulkan", "UploadTexture: Address=0x" << std::hex << startAddress << std::dec 
                      << " data unmodified (hash match: 0x" << std::hex << incomingHash << std::dec << "). Bypassing upload!");
            return;
        }
    }

    GLIDE_PROFILE_SCOPE("Vulkan::UploadTexture");

    GLIDE_LOG(DEBUG, "Vulkan", "UploadTexture TMU" << tmu << ": Address=0x" << std::hex << startAddress << std::dec 
              << " Size: " << tex.baseWidth << "x" << tex.baseHeight 
              << " Mipmap levels: " << tex.swizzledMipLevels.size()
              << " (Hash: 0x" << std::hex << incomingHash << std::dec << ")");

    // Erase existing texture at the address if it exists to clean up its resources
    m_vulkanTextures.erase(startAddress);

    // Calculate size of this texture
    size_t totalBytes = 0;
    for (const auto& level : tex.swizzledMipLevels) {
        totalBytes += level.size() * sizeof(uint32_t);
    }

    // Check if we have enough space in the active slot's staging buffer segment
    uint32_t slotBase = m_currentFrameSlot * m_texStagingSlotSize;
    uint32_t slotEnd = (m_currentFrameSlot + 1) * m_texStagingSlotSize;
    
    if (m_texStagingOffsets[m_currentFrameSlot] + totalBytes > slotEnd) {
        GLIDE_LOG(WARN, "Vulkan", "Texture staging slot size exceeded! Stalling GPU on waitIdle() and wrapping around...");
        m_device->waitIdle();
        m_texStagingOffsets[m_currentFrameSlot] = slotBase;
    }

    uint32_t currentOffset = m_texStagingOffsets[m_currentFrameSlot];

    try {
        // 1. Create vk::Image
        vk::ImageCreateInfo imageInfo({}, vk::ImageType::e2D, vk::Format::eB8G8R8A8Unorm,
                                     vk::Extent3D(tex.baseWidth, tex.baseHeight, 1),
                                     static_cast<uint32_t>(tex.swizzledMipLevels.size()), 1,
                                     vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                     vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
        auto image = m_device->createImageUnique(imageInfo);

        // 2. Allocate and bind memory
        auto memReqs = m_device->getImageMemoryRequirements(image.get());
        uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
        auto memory = m_device->allocateMemoryUnique(allocInfo);
        m_device->bindImageMemory(image.get(), memory.get(), 0);

        // 3. Copy all mipmap levels data into our persistent staging memory map
        uint8_t* mappedData = static_cast<uint8_t*>(m_texStagingMap) + currentOffset;
        size_t offset = 0;
        std::vector<size_t> levelOffsets;
        for (const auto& level : tex.swizzledMipLevels) {
            levelOffsets.push_back(currentOffset + offset);
            size_t levelBytes = level.size() * sizeof(uint32_t);
            std::memcpy(mappedData + offset, level.data(), levelBytes);
            offset += levelBytes;
        }

        // Flush mapped memory range to ensure visibility to GPU
        vk::MappedMemoryRange memRange(m_texStagingMemory.get(), currentOffset, totalBytes);
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
        vk::ImageSubresourceRange entireImageRange(vk::ImageAspectFlagBits::eColor, 0, static_cast<uint32_t>(tex.swizzledMipLevels.size()), 0, 1);
        TransitionImageLayout(*m_commandBuffer, image.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, entireImageRange);

        // Copy each mipmap level from staging buffer to image
        std::vector<vk::BufferImageCopy> copyRegions;
        uint32_t width = tex.baseWidth;
        uint32_t height = tex.baseHeight;
        for (uint32_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
            vk::BufferImageCopy region;
            region.bufferOffset = levelOffsets[i];
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
            region.imageOffset = vk::Offset3D(0, 0, 0);
            region.imageExtent = vk::Extent3D(width, height, 1);
            copyRegions.push_back(region);

            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }

        m_commandBuffer->copyBufferToImage(m_texStagingBuffer.get(), image.get(), vk::ImageLayout::eTransferDstOptimal, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

        // Transition entire image to ShaderReadOnlyOptimal
        TransitionImageLayout(*m_commandBuffer, image.get(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, entireImageRange);

        // 6. Create Image View
        vk::ImageViewCreateInfo viewInfo({}, image.get(), vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm,
                                        {}, {vk::ImageAspectFlagBits::eColor, 0, static_cast<uint32_t>(tex.swizzledMipLevels.size()), 0, 1});
        auto imageView = m_device->createImageViewUnique(viewInfo);

        // 7. Create Sampler based on currently tracked states
        uint32_t clampS = m_texClampS[0];
        uint32_t clampT = m_texClampT[0];
        uint32_t minFilter = m_texMinFilter[0];
        uint32_t magFilter = m_texMagFilter[0];

        vk::SamplerAddressMode addressModeS = (clampS == 1) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eRepeat;
        vk::SamplerAddressMode addressModeT = (clampT == 1) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eRepeat;
        vk::Filter minF = (minFilter == 1) ? vk::Filter::eLinear : vk::Filter::eNearest;
        vk::Filter magF = (magFilter == 1) ? vk::Filter::eLinear : vk::Filter::eNearest;

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
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        auto sampler = m_device->createSamplerUnique(samplerInfo);

        // 8. Allocate Descriptor Set
        vk::DescriptorSetAllocateInfo setAlloc(m_descriptorPool.get(), 1, &m_descriptorSetLayout.get());
        auto sets = m_device->allocateDescriptorSets(setAlloc);
        auto descriptorSet = sets[0];

        // 9. Update Descriptor Set
        vk::DescriptorImageInfo descriptorImageInfo(sampler.get(), imageView.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet descriptorWrite(descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &descriptorImageInfo, nullptr, nullptr);
        m_device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);

        // 10. Save in texture cache map
        VulkanTexture vulkanTex;
        vulkanTex.image = std::move(image);
        vulkanTex.memory = std::move(memory);
        vulkanTex.imageView = std::move(imageView);
        vulkanTex.sampler = std::move(sampler);
        vulkanTex.descriptorSet = descriptorSet;
        vulkanTex.clampS = clampS;
        vulkanTex.clampT = clampT;
        vulkanTex.minFilter = minFilter;
        vulkanTex.magFilter = magFilter;
        vulkanTex.dataHash = incomingHash;

        m_vulkanTextures[startAddress] = std::move(vulkanTex);

        GLIDE_LOG(DEBUG, "Vulkan", "UploadTexture: Completed successfully for address 0x" << std::hex << startAddress << std::dec << " (No GPU block!)");
    } catch (const std::exception& e) {
        GLIDE_LOG(CRITICAL, "Vulkan", "UploadTexture failed for Address=0x" << std::hex << startAddress << std::dec << ": " << e.what());
    }
}

void VulkanBackend::BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    FlushBatch();
    
    // 1. Call base class SoftwareBackendBase::BindTexture to update software states
    SoftwareBackendBase::BindTexture(tmu, startAddress, clampS, clampT, minFilter, magFilter);

    if (!m_initialized || !m_device) return;

    // We only support TMU 0 for now (tmu == 0)
    if (tmu != 0) return;

    if (startAddress == 0xFFFFFFFF) return;

    auto it = m_vulkanTextures.find(startAddress);
    if (it != m_vulkanTextures.end()) {
        auto& tex = it->second;
        // Check if wrap or filter modes have changed
        if (tex.clampS != clampS || tex.clampT != clampT || tex.minFilter != minFilter || tex.magFilter != magFilter) {
            GLIDE_LOG(DEBUG, "Vulkan", "BindTexture: Sampler properties changed for 0x" << std::hex << startAddress << std::dec 
                      << ". Recreating VkSampler. WrapS=" << clampS << " WrapT=" << clampT 
                      << " MinFilter=" << minFilter << " MagFilter=" << magFilter);

            tex.clampS = clampS;
            tex.clampT = clampT;
            tex.minFilter = minFilter;
            tex.magFilter = magFilter;

            // Destroy old sampler
            tex.sampler.reset();

            // Recreate sampler
            vk::SamplerAddressMode addressModeS = (clampS == 1) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eRepeat;
            vk::SamplerAddressMode addressModeT = (clampT == 1) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eRepeat;
            vk::Filter minF = (minFilter == 1) ? vk::Filter::eLinear : vk::Filter::eNearest;
            vk::Filter magF = (magFilter == 1) ? vk::Filter::eLinear : vk::Filter::eNearest;

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
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 0.0f;

            try {
                tex.sampler = m_device->createSamplerUnique(samplerInfo);

                // Update the descriptor set
                vk::DescriptorImageInfo imageInfo(tex.sampler.get(), tex.imageView.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet descriptorWrite(tex.descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr);
                m_device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
            } catch (const std::exception& e) {
                GLIDE_LOG(CRITICAL, "Vulkan", "BindTexture failed to update sampler for Address=0x" << std::hex << startAddress << std::dec << ": " << e.what());
            }
        }
    }
}

vk::Rect2D VulkanBackend::GetScissorRectVulkan() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint32_t vkScissorY = m_clipMinY;
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT is 1 (bottom-left)
        // Flip Y to top-left coordinate system of Vulkan
        vkScissorY = m_headlessHeight - m_clipMaxY;
    }
    uint32_t vkScissorW = (m_clipMaxX > m_clipMinX) ? (m_clipMaxX - m_clipMinX) : 0;
    uint32_t vkScissorH = (m_clipMaxY > m_clipMinY) ? (m_clipMaxY - m_clipMinY) : 0;

    vk::Offset2D offset(static_cast<int32_t>(m_clipMinX), static_cast<int32_t>(vkScissorY));
    vk::Extent2D extent(vkScissorW, vkScissorH);
    return vk::Rect2D(offset, extent);
}

void VulkanBackend::EnsureRenderPassActive() {
    if (m_inRenderPass) return;

    // Begin render pass with eLoad (no automatic clear)
    vk::RenderPassBeginInfo renderPassInfo(m_renderPass.get(), m_headlessFramebuffers[GetActiveGpuBufferIdx()].get(),
                                         vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}),
                                         0, nullptr); // 0 clear values since we use eLoad!
    m_commandBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Configure Dynamic Viewport & Scissor
    vk::Viewport viewport(0.0f, 0.0f, (float)m_headlessWidth, (float)m_headlessHeight, 0.0f, 1.0f);
    m_commandBuffer->setViewport(0, 1, &viewport);

    vk::Rect2D scissor = GetScissorRectVulkan();
    m_commandBuffer->setScissor(0, 1, &scissor);

    // Bind vertex buffer
    vk::DeviceSize zeroOffset = 0;
    m_commandBuffer->bindVertexBuffers(0, 1, &m_vertexBuffer.get(), &zeroOffset);

    m_inRenderPass = true;

    // Perform explicit clears using vkCmdClearAttachments if pending on the active FBO!
    std::vector<vk::ClearAttachment> clears;
    uint32_t activeIdx = GetActiveGpuBufferIdx();
    if (m_clearColorPending[activeIdx]) {
        clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0, m_clearColorValue[activeIdx]));
        m_clearColorPending[activeIdx] = false;
    }
    if (m_clearDepthPending[activeIdx]) {
        clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eDepth, 0, vk::ClearValue(vk::ClearDepthStencilValue(m_clearDepthValue[activeIdx], 0))));
        m_clearDepthPending[activeIdx] = false;
    }

    if (!clears.empty()) {
        vk::ClearRect clearRect(vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
        m_commandBuffer->clearAttachments(static_cast<uint32_t>(clears.size()), clears.data(), 1, &clearRect);
        GLIDE_LOG(DEBUG, "Vulkan", "Explicitly cleared " << clears.size() << " attachments inside render pass.");
    }
}

void VulkanBackend::PopulatePushConstants(PushConstants& pcs) {
    pcs.viewportWidth = (float)m_headlessWidth;
    pcs.viewportHeight = (float)m_headlessHeight;
    pcs.depthBufferMode = m_depthMode;
    
    // Extract and normalize constant color components
    pcs.constantColor[0] = ((m_constantColor >> 16) & 0xFF) / 255.0f; // R
    pcs.constantColor[1] = ((m_constantColor >> 8) & 0xFF) / 255.0f;  // G
    pcs.constantColor[2] = (m_constantColor & 0xFF) / 255.0f;         // B
    pcs.constantColor[3] = ((m_constantColor >> 24) & 0xFF) / 255.0f; // A
    if (m_pixelFormatOverride == 1) {
        std::swap(pcs.constantColor[0], pcs.constantColor[2]);
    }
    if (pcs.constantColor[3] < 0.5f) {
        pcs.constantColor[3] = 1.0f; // Force opaque if alpha is uninitialized/transparent
    }

    pcs.alphaTestOp = m_alphaTestOp;
    pcs.alphaTestRef = (float)m_alphaTestRefVal / 255.0f;
    pcs.depthBias = static_cast<float>(m_depthBiasLevel) / 65535.0f;

    // Milestone 8: Fogging State (packed 128-byte layout)
    pcs.fogMode = m_fogMode;
    
    // Swizzle fog color if format override is active
    uint32_t packedFogColor = m_fogColor;
    if (m_pixelFormatOverride == 1) {
        uint32_t r = (m_fogColor >> 16) & 0xFF;
        uint32_t g = (m_fogColor >> 8) & 0xFF;
        uint32_t b = m_fogColor & 0xFF;
        uint32_t a = (m_fogColor >> 24) & 0xFF;
        packedFogColor = (a << 24) | (b << 16) | (g << 8) | r;
    }
    pcs.fogColor = packedFogColor;
    pcs.useTmuOow = (m_stwHintMask & 0x2) ? 1u : 0u;

    // Pack 64-entry 8-bit fog table into 16 uint32_t values
    for (int i = 0; i < 16; ++i) {
        uint32_t word = 0;
        word |= static_cast<uint32_t>(m_fogTable[i * 4 + 0]) << 0;
        word |= static_cast<uint32_t>(m_fogTable[i * 4 + 1]) << 8;
        word |= static_cast<uint32_t>(m_fogTable[i * 4 + 2]) << 16;
        word |= static_cast<uint32_t>(m_fogTable[i * 4 + 3]) << 24;
        pcs.fogTable[i] = word;
    }

    // Milestone 9: Combiner & Texture State
    pcs.colorFunc = m_colorCombinerFunc;
    pcs.colorFactor = m_colorCombinerFactor;
    pcs.colorLocal = m_colorCombinerLocal;
    pcs.colorOther = m_colorCombinerOther;
    pcs.colorInvert = m_colorCombinerInvert ? 1u : 0u;
    
    pcs.alphaFunc = m_alphaCombinerFunc;
    pcs.alphaFactor = m_alphaCombinerFactor;
    pcs.alphaLocal = m_alphaCombinerLocal;
    pcs.alphaOther = m_alphaCombinerOther;
    pcs.alphaInvert = m_alphaCombinerInvert ? 1u : 0u;

    pcs.textureEnabled = (m_boundTexAddress[0] != 0xFFFFFFFF) ? 1u : 0u;

    // Milestone 10: Chromakey State
    pcs.chromakeyEnabled = m_chromakeyMode;
    pcs.chromakeyValue[0] = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f; // R
    pcs.chromakeyValue[1] = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;  // G
    pcs.chromakeyValue[2] = (m_chromakeyValue & 0xFF) / 255.0f;         // B
    pcs.chromakeyValue[3] = ((m_chromakeyValue >> 24) & 0xFF) / 255.0f; // A
    if (m_pixelFormatOverride == 1) {
        std::swap(pcs.chromakeyValue[0], pcs.chromakeyValue[2]);
    }
}

} // namespace GlideWrapper
