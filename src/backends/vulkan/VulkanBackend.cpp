#include "VulkanBackend.h"
#include "core/Logger.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <SDL2/SDL.h>

// Include build-time generated shader SPIR-V bytecode headers!
#include "color_vert.spv.h"
#include "color_frag.spv.h"

extern "C" {
    extern volatile int g_glideWrapperSdlKeyHit;
}

namespace GlideWrapper {

// Push Constants definition shared across pipeline layout and draw pipelines
struct PushConstants {
    float viewportWidth;
    float viewportHeight;
    float padding[2];          // Explicitly align constantColor to 16-byte boundary (std140)
    float constantColor[4];    // RGBA normalized floats
    uint32_t useConstantColor;  // 1 = true, 0 = false
    uint32_t useConstantAlpha;  // 1 = true, 0 = false
    uint32_t depthBufferMode;   // 0 = None, 1 = Z, 2 = W
    uint32_t alphaTestOp;       // 0..7 (Glide compare ops, 7 = ALWAYS/disabled)
    float alphaTestRef;         // 0.0 .. 1.0 (normalized reference value)
    float depthBias;            // Normalized depth bias
};

// Authoritative Vulkan validation layer declarations
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

static vk::BlendFactor MapBlendFactorVulkan(uint32_t factor) {
    switch (factor) {
        case 0: return vk::BlendFactor::eZero;                  // GR_BLEND_ZERO
        case 1: return vk::BlendFactor::eSrcAlpha;              // GR_BLEND_SRC_ALPHA
        case 2: return vk::BlendFactor::eSrcColor;              // GR_BLEND_SRC_COLOR
        case 3: return vk::BlendFactor::eDstAlpha;              // GR_BLEND_DST_ALPHA
        case 4: return vk::BlendFactor::eOne;                   // GR_BLEND_ONE
        case 5: return vk::BlendFactor::eOneMinusSrcAlpha;      // GR_BLEND_ONE_MINUS_SRC_ALPHA
        case 6: return vk::BlendFactor::eOneMinusSrcColor;      // GR_BLEND_ONE_MINUS_SRC_COLOR
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
    m_pointPipeline.reset();
    m_linePipeline.reset();
    m_trianglePipeline.reset();
    m_danglingPipelines.clear(); // Clear recompiled mid-frame pipelines
    m_pipelineLayout.reset();
    m_vertShaderModule.reset();
    m_fragShaderModule.reset();

    // 3. Explicitly destroy all device-owned buffers and memories
    m_vertexBuffer.reset();
    m_vertexBufferMemory.reset();
    m_headlessPixelBuffer.reset();
    m_headlessPixelMemory.reset();

    // 4. Explicitly destroy all device-owned image resources
    m_headlessFramebuffer.reset();
    m_headlessImageView.reset();
    m_headlessImageMemory.reset();
    m_headlessImage.reset();

    m_depthImageView.reset();
    m_depthImageMemory.reset();
    m_depthImage.reset();

    // 5. Reset command submission and render pass infrastructure
    m_commandBuffer.reset();
    m_commandPool.reset();
    m_renderPass.reset();

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
        GLIDE_LOG(INFO, "Vulkan", "Allocated 1MB host-visible dynamic ring vertex buffer.");
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
            auto* win = SDL_CreateWindow("3dfx glide-ng Presentation Console (Hybrid Vulkan)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
            if (win) {
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
    TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, colorRange);

    vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
    TransitionImageLayout(m_commandBuffer.get(), m_depthImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, depthRange);

    m_commandBuffer->end();
    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_commandBuffer.get());
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
        m_pointPipeline.reset();
        m_linePipeline.reset();
        m_trianglePipeline.reset();
        m_pipelineLayout.reset();
        m_vertShaderModule.reset();
        m_fragShaderModule.reset();

        if (m_headlessPixelMap) {
            m_device->unmapMemory(m_headlessPixelMemory.get());
            m_headlessPixelMap = nullptr;
        }
        m_headlessPixelBuffer.reset();
        m_headlessPixelMemory.reset();
        m_headlessFramebuffer.reset();
        m_headlessImageView.reset();
        m_headlessImageMemory.reset();
        m_headlessImage.reset();

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
    if (clearMask & 1) { // Color clear
        float a = ((color >> 24) & 0xFF) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8)  & 0xFF) / 255.0f;
        float b = (color         & 0xFF) / 255.0f;

        // Respect the active pixel format from SoftwareBackendBase!
        if (m_pixelFormatOverride == 1) { // ABGR format override
            std::swap(r, b);
        }

        m_clearColorValue = vk::ClearColorValue(std::array<float, 4>{r, g, b, a});
        m_clearColorPending = true;
    }

    if (clearMask & 2) { // Depth clear
        m_clearDepthValue = z;
        m_clearDepthPending = true;
    }
}

void VulkanBackend::SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool stateChanged = (depthMode != m_depthMode) || (compareOp != m_depthCompareOp) || (depthMask != m_depthMask);
    
    SoftwareBackendBase::SetDepthState(depthMode, compareOp, depthMask, biasLevel);

    if (stateChanged && m_initialized && m_device) {
        GLIDE_LOG(INFO, "Vulkan", "SetDepthState: Recompiling Vulkan pipelines with new depth config...");
        CreateGraphicsPipelines();
    }
}

void VulkanBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool stateChanged = (rgbSrcFactor != m_rgbSrcBlend) || (rgbDstFactor != m_rgbDstBlend) ||
                        (alphaSrcFactor != m_alphaSrcBlend) || (alphaDstFactor != m_alphaDstBlend);

    SoftwareBackendBase::SetBlendState(rgbSrcFactor, rgbDstFactor, alphaSrcFactor, alphaDstFactor);

    if (stateChanged && m_initialized && m_device) {
        GLIDE_LOG(INFO, "Vulkan", "SetBlendState: Recompiling Vulkan pipelines with new blending config...");
        CreateGraphicsPipelines();
    }
}

void VulkanBackend::SetCullState(uint32_t cullMode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool stateChanged = (cullMode != m_cullMode);
    
    SoftwareBackendBase::SetCullState(cullMode);

    if (stateChanged && m_initialized && m_device) {
        GLIDE_LOG(INFO, "Vulkan", "SetCullState: Recompiling Vulkan pipelines with new culling config...");
        CreateGraphicsPipelines();
    }
}

void VulkanBackend::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    SoftwareBackendBase::SetAlphaTestState(compareOp, refVal);
    // Note: Emulated in fragment shader via push constants. No pipeline recompilation needed!
}

void VulkanBackend::SetSstOrigin(uint32_t origin) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool stateChanged = (origin != m_sstOrigin);
    
    SoftwareBackendBase::SetSstOrigin(origin);

    if (stateChanged && m_initialized && m_device) {
        GLIDE_LOG(INFO, "Vulkan", "SetSstOrigin: Recompiling Vulkan pipelines with new origin config...");
        CreateGraphicsPipelines();
    }
}

bool VulkanBackend::SwapBuffers() {

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    GLIDE_LOG(DEBUG, "Vulkan", "SwapBuffers Hybrid Presentation dispatch.");

    // Enforce frame pacing
    if (m_config.maxFps > 0.0f) {
        auto delay = std::chrono::microseconds(static_cast<int>(1000000.0f / m_config.maxFps));
        std::this_thread::sleep_for(delay);
    }

    if (m_headlessMode) {
        // Terminate active render pass before copying GPU image to host buffer.
        // If no draw calls were executed (clear-only frame), we must explicitly begin/end a render pass to execute the pending clears!
        if (!m_inRenderPass) {
            if (m_clearColorPending || m_clearDepthPending) {
                vk::RenderPassBeginInfo renderPassInfo(m_renderPass.get(), m_headlessFramebuffer.get(),
                                                     vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}),
                                                     0, nullptr);
                m_commandBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

                std::vector<vk::ClearAttachment> clears;
                if (m_clearColorPending) {
                    clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0, m_clearColorValue));
                }
                if (m_clearDepthPending) {
                    clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eDepth, 0, vk::ClearValue(vk::ClearDepthStencilValue(m_clearDepthValue, 0))));
                }
                if (!clears.empty()) {
                    vk::ClearRect clearRect(vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
                    m_commandBuffer->clearAttachments(static_cast<uint32_t>(clears.size()), clears.data(), 1, &clearRect);
                    GLIDE_LOG(DEBUG, "Vulkan", "Explicitly cleared " << clears.size() << " attachments in SwapBuffers manual clear.");
                }

                m_commandBuffer->endRenderPass();
                m_clearColorPending = false;
                m_clearDepthPending = false;
            }
        } else {
            m_commandBuffer->endRenderPass();
            m_inRenderPass = false;
        }

        // --- STEP 1: Cache CPU-rendered text/geometry pixels ---
        // Any pixel in m_headlessPixelMap that has alpha > 0 (drawn by CPU rasterizer) is cached.
        // The background pixels cleared by SoftwareBackendBase::ClearBuffer have alpha = 0.
        std::vector<uint32_t> cpuRenderingCache(m_headlessWidth * m_headlessHeight, 0);
        const auto* cpuPixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
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
        GLIDE_LOG(INFO, "Vulkan", "SwapBuffers: Cache CPU pixels phase. Found " << cpuPixelCount << " non-transparent CPU pixels.");

        // --- STEP 2: Execute GPU presentation copy ---
        // Manually transition layout from ColorAttachmentOptimal to TransferSrcOptimal before copy!
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, range);

        // Record copy from GPU image (which has the GPU-cleared blue background) to host staging buffer.
        // This will overwrite m_headlessPixelMap with the GPU-cleared pixels.
        vk::BufferImageCopy region;
        region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
        
        m_commandBuffer->copyImageToBuffer(m_headlessImage.get(), vk::ImageLayout::eTransferSrcOptimal, m_headlessPixelBuffer.get(), 1, &region);

        // Transition layout back to ColorAttachmentOptimal so the image state is clean for the next frame!
        TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal, range);

        // End and submit GPU commands
        m_commandBuffer->end();
        vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_commandBuffer.get());
        m_graphicsQueue.submit(submitInfo, {});
        m_device->waitIdle(); // Wait for GPU copy to complete

        // Now it is 100% safe to destroy any pipelines that were recompiled mid-frame!
        m_danglingPipelines.clear();

        // Explicitly invalidate mapped memory range to force CPU cache refresh of GPU copy!
        vk::MappedMemoryRange memRange(m_headlessPixelMemory.get(), 0, VK_WHOLE_SIZE);
        m_device->invalidateMappedMemoryRanges(memRange);

        // --- STEP 3: Overlay CPU-rendered text/geometry back ---
        // Overwrite the staging memory (m_headlessPixelMap) with our cached CPU text pixels!
        if (hasCpuRendering && m_headlessPixelMap) {
            auto* dest = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
            for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
                if (cpuRenderingCache[i] != 0) {
                    dest[i] = cpuRenderingCache[i];
                }
            }
        }

        // Blit the combined frame to SDL2 window if active
        if (m_sdlTexture && m_sdlRenderer && m_headlessPixelMap) {
            SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_headlessPixelMap, m_headlessWidth * 4);
            SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
            SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
        }

        // Reset and begin recording the next frame's commands immediately!
        m_commandBuffer->reset({});
        m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // Force alpha to 0 in staging buffer so GPU-rendered pixels are not cached as CPU text in the next frame!
        if (m_headlessPixelMap) {
            auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
            for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
                pixels[i] &= 0x00FFFFFF;
            }
        }
    }

    return true;
}

bool VulkanBackend::ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;



    // --- STEP 1: Cache CPU-rendered text/geometry pixels ---
    std::vector<uint32_t> cpuRenderingCache(m_headlessWidth * m_headlessHeight, 0);
    const auto* cpuPixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
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
    GLIDE_LOG(INFO, "Vulkan", "ReadLFB: Cache CPU pixels phase. Found " << cpuPixelCount << " non-transparent CPU pixels.");

    // 1. Terminate active render pass if any
    if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
    }

    // 2. Transition layout from ColorAttachmentOptimal to TransferSrcOptimal
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, range);

    // 3. Record copy from GPU image to staging buffer
    vk::BufferImageCopy region;
    region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
    m_commandBuffer->copyImageToBuffer(m_headlessImage.get(), vk::ImageLayout::eTransferSrcOptimal, m_headlessPixelBuffer.get(), 1, &region);

    // 4. End and submit the command buffer immediately
    m_commandBuffer->end();
    vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_commandBuffer.get());
    m_graphicsQueue.submit(submitInfo, {});
    m_device->waitIdle(); // Block CPU until GPU finishes!

    // 5. Invalidate host cache
    vk::MappedMemoryRange memRange(m_headlessPixelMemory.get(), 0, VK_WHOLE_SIZE);
    m_device->invalidateMappedMemoryRanges(memRange);

    // --- STEP 3: Overlay CPU-rendered text/geometry back ---
    if (hasCpuRendering && m_headlessPixelMap) {
        auto* dest = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
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
    TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal, range);

    // 8. Now call the base class ReadLFB to copy the synchronized pixels to the client buffer!
    bool result = SoftwareBackendBase::ReadLFB(srcX, srcY, srcWidth, srcHeight, dstStride, dstData);

    // 9. Force alpha to 0 in the staging buffer so GPU-rendered pixels are not cached as CPU text in the next ReadLFB!
    if (m_headlessPixelMap) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
            pixels[i] &= 0x00FFFFFF;
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
            GLIDE_LOG(INFO, "Vulkan", "SDL window quit request.");
        } else if (event.type == SDL_KEYDOWN) {
            GLIDE_LOG(INFO, "Vulkan", "SDL window keydown captured. Setting key hit hook flag.");
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
    GLIDE_LOG(INFO, "Vulkan", logStream.str());

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

    vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_graphicsQueueFamilyIndex);
    m_commandPool = m_device->createCommandPoolUnique(poolInfo);

    vk::CommandBufferAllocateInfo allocInfo(m_commandPool.get(), vk::CommandBufferLevel::ePrimary, 1);
    auto bufs = m_device->allocateCommandBuffersUnique(allocInfo);
    m_commandBuffer = std::move(bufs[0]);

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
    vk::ImageCreateInfo imageInfo({}, vk::ImageType::e2D, m_colorFormat,
                                 vk::Extent3D(width, height, 1), 1, 1,
                                 vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);

    m_headlessImage = m_device->createImageUnique(imageInfo);

    auto memReqs = m_device->getImageMemoryRequirements(m_headlessImage.get());
    // Use device local memory for optimal GPU rendering performance and compatibility
    uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);

    m_headlessImageMemory = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindImageMemory(m_headlessImage.get(), m_headlessImageMemory.get(), 0);

    vk::ImageViewCreateInfo viewInfo({}, m_headlessImage.get(), vk::ImageViewType::e2D, m_colorFormat,
                                    {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_headlessImageView = m_device->createImageViewUnique(viewInfo);

    // 2. Allocate depth image for hardware depth-testing support
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

    // 3. Create framebuffer with both color and depth attachments
    std::array<vk::ImageView, 2> attachments = {
        m_headlessImageView.get(),
        m_depthImageView.get()
    };
    vk::FramebufferCreateInfo fbInfo({}, m_renderPass.get(), static_cast<uint32_t>(attachments.size()), attachments.data(), width, height, 1);
    m_headlessFramebuffer = m_device->createFramebufferUnique(fbInfo);

    // Create the CPU staging buffer map
    vk::BufferCreateInfo bufInfo({}, width * height * 4, vk::BufferUsageFlagBits::eTransferDst);
    m_headlessPixelBuffer = m_device->createBufferUnique(bufInfo);

    auto bufReqs = m_device->getBufferMemoryRequirements(m_headlessPixelBuffer.get());
    uint32_t bufTypeIndex = FindMemoryType(bufReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo bufAlloc(bufReqs.size, bufTypeIndex);

    m_headlessPixelMemory = m_device->allocateMemoryUnique(bufAlloc);
    m_device->bindBufferMemory(m_headlessPixelBuffer.get(), m_headlessPixelMemory.get(), 0);

    m_headlessPixelMap = m_device->mapMemory(m_headlessPixelMemory.get(), 0, width * height * 4);
    if (!m_headlessPixelMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to map headless host memory map!");
        return false;
    }

    // Pre-initialize buffer to opaque black
    std::memset(m_headlessPixelMap, 0xFF, width * height * 4);
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

bool VulkanBackend::CreateGraphicsPipelines() {
    try {
        // Shader Stages
        vk::PipelineShaderStageCreateInfo shaderStages[] = {
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, m_vertShaderModule.get(), "main"),
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, m_fragShaderModule.get(), "main")
        };

        // Vertex Input Bindings (matches ModernVertex exactly)
        vk::VertexInputBindingDescription bindingDesc(0, sizeof(ModernVertex), vk::VertexInputRate::eVertex);
        std::array<vk::VertexInputAttributeDescription, 3> attrDescs = {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(ModernVertex, tex))
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
        switch (m_cullMode) {
            case 0: vkCullMode = vk::CullModeFlagBits::eNone; break;  // GR_CULL_DISABLE
            case 1: vkCullMode = vk::CullModeFlagBits::eBack; break;   // GR_CULL_NEGATIVE (cull CCW / Back face)
            case 2: vkCullMode = vk::CullModeFlagBits::eFront; break;  // GR_CULL_POSITIVE (cull CW / Front face)
            default: break;
        }

        vk::FrontFace frontFace = (m_sstOrigin == 1) ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise;
        vk::PipelineRasterizationStateCreateInfo rasterizer({}, m_depthClampEnabled, false, vk::PolygonMode::eFill, vkCullMode,
                                                           frontFace, false, 0.0f, 0.0f, 0.0f, 1.0f);

        // Multisample Configuration (none)
        vk::PipelineMultisampleStateCreateInfo multisampling({}, vk::SampleCountFlagBits::e1, false, 1.0f, nullptr, false, false);

        // Depth Stencil Configuration based on Glide's active states
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        if (m_depthMode != 0) {
            depthStencil.depthTestEnable = true;
            depthStencil.depthWriteEnable = m_depthMask ? true : false;

            vk::CompareOp op = vk::CompareOp::eLess;
            switch (m_depthCompareOp) {
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
        colorBlendAttachment.srcColorBlendFactor = MapBlendFactorVulkan(m_rgbSrcBlend);
        colorBlendAttachment.dstColorBlendFactor = MapBlendFactorVulkan(m_rgbDstBlend);
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = MapBlendFactorVulkan(m_alphaSrcBlend);
        colorBlendAttachment.dstAlphaBlendFactor = MapBlendFactorVulkan(m_alphaDstBlend);

        vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment, {0.0f, 0.0f, 0.0f, 0.0f});

        // Push Constants Range (passes viewport and constant color configuration to both shader stages)
        vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstants));
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 0, nullptr, 1, &pushRange);
        m_pipelineLayout = m_device->createPipelineLayoutUnique(pipelineLayoutInfo);

        // Assemble Graphics Pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo({}, 2, shaderStages, &vertexInputInfo, &inputAssembly, nullptr, &viewportState,
                                                   &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState,
                                                   m_pipelineLayout.get(), m_renderPass.get(), 0, {}, -1);

        // 1. Compile Point Pipeline
        auto pointResult = m_device->createGraphicsPipelineUnique({}, pipelineInfo);
        if (pointResult.result != vk::Result::eSuccess) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan Point graphics pipeline!");
            return false;
        }

        // 2. Compile Line Pipeline
        inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        auto lineResult = m_device->createGraphicsPipelineUnique({}, pipelineInfo);
        if (lineResult.result != vk::Result::eSuccess) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan Line graphics pipeline!");
            return false;
        }

        // 3. Compile Triangle Pipeline
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        auto triangleResult = m_device->createGraphicsPipelineUnique({}, pipelineInfo);
        if (triangleResult.result != vk::Result::eSuccess) {
            GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan Triangle graphics pipeline!");
            return false;
        }

        // Defer destruction of old pipelines to the end of the frame to prevent command buffer invalidation!
        if (m_pointPipeline) m_danglingPipelines.push_back(std::move(m_pointPipeline));
        if (m_linePipeline) m_danglingPipelines.push_back(std::move(m_linePipeline));
        if (m_trianglePipeline) m_danglingPipelines.push_back(std::move(m_trianglePipeline));

        m_pointPipeline = std::move(pointResult.value);
        m_linePipeline = std::move(lineResult.value);
        m_trianglePipeline = std::move(triangleResult.value);

        GLIDE_LOG(INFO, "Vulkan", "Vulkan Point, Line & Triangle Graphics Pipelines compiled successfully.");
        return true;
    } catch (const std::exception& e) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Failed to create Vulkan Graphics Pipelines: " << e.what());
        return false;
    }
}

void VulkanBackend::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    GLIDE_LOG(DEBUG, "Vulkan", "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1] << ", " << pt.pos[2] << ")");

    // 1. Upload vertex to dynamic ring vertex buffer
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

    // 2. Ensure Render Pass is active (lazy initialization)
    EnsureRenderPassActive();

    // Always bind the correct pipeline for this draw call!
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, m_pointPipeline.get());

    // Determine whether to use constant color or vertex color based on Glide combiner state
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);

    PushConstants pcs;
    pcs.viewportWidth = (float)m_headlessWidth;
    pcs.viewportHeight = (float)m_headlessHeight;
    pcs.useConstantColor = useConstColor ? 1 : 0;
    pcs.useConstantAlpha = useConstAlpha ? 1 : 0;
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

    m_commandBuffer->pushConstants<PushConstants>(m_pipelineLayout.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pcs);

    // 3. Issue GPU point draw call using firstVertex to read the correct slice!
    uint32_t firstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    m_commandBuffer->draw(1, 1, firstVertex, 0);

    // 4. Advance ring buffer offset (wrap around if buffer is full)
    m_vertexBufferOffset += sizeof(ModernVertex);
    if (m_vertexBufferOffset + sizeof(ModernVertex) > m_vertexBufferSize) {
        GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer ring buffer wrap-around!");
        m_vertexBufferOffset = 0;
    }
}

void VulkanBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    GLIDE_LOG(DEBUG, "Vulkan", "DrawLine GPU draw. V1: (" << v1.pos[0] << ", " << v1.pos[1] << "), V2: (" << v2.pos[0] << ", " << v2.pos[1] << ")");

    // 1. Upload vertices to dynamic ring vertex buffer
    if (!m_vertexBufferMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawLine!");
        return;
    }
    
    // Check if we need to wrap the ring buffer before writing two vertices!
    if (m_vertexBufferOffset + 2 * sizeof(ModernVertex) > m_vertexBufferSize) {
        GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer ring buffer wrap-around during DrawLine!");
        m_vertexBufferOffset = 0;
    }

    auto* dest = reinterpret_cast<ModernVertex*>(reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        dest[0] = v1; dest[0].pos[1] = (float)m_headlessHeight - v1.pos[1];
        dest[1] = v2; dest[1].pos[1] = (float)m_headlessHeight - v2.pos[1];
    } else {
        dest[0] = v1;
        dest[1] = v2;
    }

    // 2. Ensure Render Pass is active (lazy initialization)
    EnsureRenderPassActive();

    // Always bind the correct pipeline for this draw call!
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, m_linePipeline.get());

    // Determine whether to use constant color or vertex color based on Glide combiner state
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);

    PushConstants pcs;
    pcs.viewportWidth = (float)m_headlessWidth;
    pcs.viewportHeight = (float)m_headlessHeight;
    pcs.useConstantColor = useConstColor ? 1 : 0;
    pcs.useConstantAlpha = useConstAlpha ? 1 : 0;
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

    m_commandBuffer->pushConstants<PushConstants>(m_pipelineLayout.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pcs);

    // 3. Issue GPU line draw call (vertexCount = 2)
    uint32_t firstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    m_commandBuffer->draw(2, 1, firstVertex, 0);

    // 4. Advance ring buffer offset
    m_vertexBufferOffset += 2 * sizeof(ModernVertex);
}

void VulkanBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    // TODO(Phase4): Remove this temporary CPU software rasterizer fallback once
    // GPU-native texture mapping is fully implemented in Phase 4.
    // This fallback ensures that textured console text characters (drawn as textured quads)
    // are correctly rasterized on the CPU and composited, while flat/Gouraud shaded
    // triangles (like test04) continue to run 100% natively on the GPU.
    if (m_boundTexAddress[0] != 0) {
        SoftwareBackendBase::DrawTriangle(a, b, c);
        return;
    }

    GLIDE_LOG(DEBUG, "Vulkan", "DrawTriangle GPU draw. A: (" << a.pos[0] << ", " << a.pos[1] << "), B: (" << b.pos[0] << ", " << b.pos[1] << "), C: (" << c.pos[0] << ", " << c.pos[1] << ")");
    GLIDE_LOG(INFO, "Vulkan", "DrawTriangle Alpha Debug: Vertex 0 Alpha = " << a.color[3]
              << ", AlphaTestOp = " << m_alphaTestOp
              << ", RefVal = " << m_alphaTestRefVal);

    // 1. Upload vertices to dynamic ring vertex buffer
    if (!m_vertexBufferMap) {
        GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawTriangle!");
        return;
    }
    
    // Check if we need to wrap the ring buffer before writing three vertices!
    if (m_vertexBufferOffset + 3 * sizeof(ModernVertex) > m_vertexBufferSize) {
        GLIDE_LOG(WARN, "Vulkan", "Dynamic Vertex Buffer ring buffer wrap-around during DrawTriangle!");
        m_vertexBufferOffset = 0;
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

    // 2. Ensure Render Pass is active (lazy initialization)
    EnsureRenderPassActive();

    // Always bind the correct pipeline for this draw call!
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, m_trianglePipeline.get());

    // Determine whether to use constant color or vertex color based on Glide combiner state
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);

    PushConstants pcs;
    pcs.viewportWidth = (float)m_headlessWidth;
    pcs.viewportHeight = (float)m_headlessHeight;
    pcs.useConstantColor = useConstColor ? 1 : 0;
    pcs.useConstantAlpha = useConstAlpha ? 1 : 0;
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

    m_commandBuffer->pushConstants<PushConstants>(m_pipelineLayout.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pcs);

    // 3. Issue GPU triangle draw call (vertexCount = 3)
    uint32_t firstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    m_commandBuffer->draw(3, 1, firstVertex, 0);

    // 4. Advance ring buffer offset
    m_vertexBufferOffset += 3 * sizeof(ModernVertex);
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
    vk::RenderPassBeginInfo renderPassInfo(m_renderPass.get(), m_headlessFramebuffer.get(),
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

    // Perform explicit clears using vkCmdClearAttachments if pending!
    std::vector<vk::ClearAttachment> clears;
    if (m_clearColorPending) {
        clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0, m_clearColorValue));
        m_clearColorPending = false;
    }
    if (m_clearDepthPending) {
        clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eDepth, 0, vk::ClearValue(vk::ClearDepthStencilValue(m_clearDepthValue, 0))));
        m_clearDepthPending = false;
    }

    if (!clears.empty()) {
        vk::ClearRect clearRect(vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
        m_commandBuffer->clearAttachments(static_cast<uint32_t>(clears.size()), clears.data(), 1, &clearRect);
        GLIDE_LOG(DEBUG, "Vulkan", "Explicitly cleared " << clears.size() << " attachments inside render pass.");
    }
}

} // namespace GlideWrapper
