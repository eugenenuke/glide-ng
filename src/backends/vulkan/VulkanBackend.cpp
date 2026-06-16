#include "VulkanBackend.h"
#include "core/Logger.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <SDL2/SDL.h>

extern "C" {
    extern volatile int g_glideWrapperSdlKeyHit;
}

namespace GlideWrapper {

// Authoritative Vulkan validation layer declarations
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

bool VulkanBackend::Initialize(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    GLIDE_LOG(INFO, "Vulkan", "Initializing hardware Vulkan GPU context...");
    m_config = config;

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

    GLIDE_LOG(INFO, "Vulkan", "Shutting down hardware Vulkan context.");
    DetachWindow();

    m_commandBuffer.reset();
    m_commandPool.reset();
    m_renderPass.reset();
    m_device.reset();
    m_instance.reset();

    m_initialized = false;
}

bool VulkanBackend::AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return false;
    if (m_windowAttached) DetachWindow();

    GLIDE_LOG(INFO, "Vulkan", "Attaching hardware Vulkan window (" << width << "x" << height << "), Windowed=" << (windowed ? "Yes" : "No"));

    m_headlessWidth = width;
    m_headlessHeight = height;
    m_headlessMode = true; // Phase 1 is purely headless-driven for visual regression

    if (!CreateHeadlessRenderTarget(width, height)) return false;

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
            auto* win = SDL_CreateWindow("3dfx glide-ng Presentation Console (Vulkan GPU)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
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

    m_windowAttached = true;
    return true;
}

void VulkanBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "Vulkan", "Detaching active Vulkan window resources.");
    
    if (m_device) {
        m_device->waitIdle();
    }

    if (m_sdlTexture)  { SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture)); m_sdlTexture = nullptr; }
    if (m_sdlRenderer) { SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer)); m_sdlRenderer = nullptr; }
    if (m_sdlWindow)   { SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow)); m_sdlWindow = nullptr; }
    if (!m_config.forceNoWindow) SDL_QuitSubSystem(SDL_INIT_VIDEO);

    if (m_headlessMode) {
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
    }

    m_windowAttached = false;
    m_headlessMode = false;
}

void VulkanBackend::ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    GLIDE_LOG(DEBUG, "Vulkan", "ClearBuffer invoked: color=" << std::hex << color << ", clearMask=" << clearMask);

    if (clearMask & 1) { // Clear color
        // Unpack ARGB8888 to float array (Vulkan expects RGBA floats)
        float a = ((color >> 24) & 0xFF) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8)  & 0xFF) / 255.0f;
        float b = (color         & 0xFF) / 255.0f;

        if (m_pixelFormat == 1) { // ABGR format override
            std::swap(r, b);
        }

        vk::ClearColorValue clearColor(std::array<float, 4>{r, g, b, a});
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        // Transition layout to TransferDst so we can clear it
        TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, range);

        // Clear the image on the GPU!
        m_commandBuffer->clearColorImage(m_headlessImage.get(), vk::ImageLayout::eTransferDstOptimal, &clearColor, 1, &range);

        // Transition layout to TransferSrc so we can copy it in SwapBuffers
        TransitionImageLayout(m_commandBuffer.get(), m_headlessImage.get(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, range);
    }
}

bool VulkanBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    GLIDE_LOG(DEBUG, "Vulkan", "SwapBuffers presentation dispatch.");

    // Enforce frame pacing
    if (m_config.maxFps > 0.0f) {
        auto delay = std::chrono::microseconds(static_cast<int>(1000000.0f / m_config.maxFps));
        std::this_thread::sleep_for(delay);
    }

    if (m_headlessMode) {
        // Record copy from GPU image to host staging buffer
        vk::BufferImageCopy region;
        region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        region.imageExtent = vk::Extent3D(m_headlessWidth, m_headlessHeight, 1);
        
        m_commandBuffer->copyImageToBuffer(m_headlessImage.get(), vk::ImageLayout::eTransferSrcOptimal, m_headlessPixelBuffer.get(), 1, &region);

        // End recording
        m_commandBuffer->end();

        // Submit command buffer and wait synchronously
        vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_commandBuffer.get());
        m_graphicsQueue.submit(submitInfo, {});
        m_device->waitIdle();

        // Blit to SDL2 window if active
        if (m_sdlTexture && m_sdlRenderer && m_headlessPixelMap) {
            SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_headlessPixelMap, m_headlessWidth * 4);
            SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
            SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
        }

        // Reset and begin recording the next frame's commands immediately!
        m_commandBuffer->reset({});
        m_commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    }

    return true;
}

void VulkanBackend::SetConstantColor(uint32_t color) {
    // Stub for Phase 1
}

void VulkanBackend::SetPixelFormat(uint32_t format) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_pixelFormat = format;
    GLIDE_LOG(INFO, "Vulkan", "SetPixelFormat: " << format);
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

// Phase 3: Drawing Stubs
void VulkanBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    GLIDE_LOG(WARN, "Vulkan", "DrawTriangle stub called (GPU rendering not yet active).");
}

void VulkanBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    GLIDE_LOG(WARN, "Vulkan", "DrawLine stub called.");
}

void VulkanBackend::DrawPoint(const ModernVertex& pt) {
    GLIDE_LOG(WARN, "Vulkan", "DrawPoint stub called.");
}

// Phase 4: State Stubs
void VulkanBackend::SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) {
    // Stub for Phase 1
}

void VulkanBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    // Stub for Phase 1
}

void VulkanBackend::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
    // Stub for Phase 1
}

void VulkanBackend::SetCullState(uint32_t cullMode) {
    // Stub for Phase 1
}

void VulkanBackend::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_clipMinX = minX;
    m_clipMinY = minY;
    m_clipMaxX = maxX;
    m_clipMaxY = maxY;
}

// LFB Access Stubs
void* VulkanBackend::GetLfbWritePtr() {
    return nullptr;
}

bool VulkanBackend::ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!dstData || !m_headlessPixelMap) return false;

    GLIDE_LOG(INFO, "Vulkan", "ReadLFB invoked: srcX=" << srcX << ", srcY=" << srcY << ", srcWidth=" << srcWidth << ", srcHeight=" << srcHeight);

    const auto* pixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);

    bool is32Bit = (dstStride >= srcWidth * 4);

    for (uint32_t y = 0; y < srcHeight; ++y) {
        uint32_t readY = srcY + y;
        if (readY >= m_headlessHeight) break;

        if (is32Bit) {
            uint32_t* destRow32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
            for (uint32_t x = 0; x < srcWidth; ++x) {
                uint32_t readX = srcX + x;
                if (readX >= m_headlessWidth) break;
                destRow32[x] = pixels[readY * m_headlessWidth + readX];
            }
        } else {
            uint16_t* destRow16 = reinterpret_cast<uint16_t*>(outBytes + y * dstStride);
            for (uint32_t x = 0; x < srcWidth; ++x) {
                uint32_t readX = srcX + x;
                if (readX >= m_headlessWidth) break;

                uint32_t pWord = pixels[readY * m_headlessWidth + readX];
                uint16_t r = ((pWord >> 16) & 0xFF) * 31 / 255;
                uint16_t g = ((pWord >> 8)  & 0xFF) * 63 / 255;
                uint16_t b = (pWord         & 0xFF) * 31 / 255;
                uint16_t c16 = (r << 11) | (g << 5) | b;
                destRow16[x] = c16;
            }
        }
    }
    return true;
}

bool VulkanBackend::WriteLFB(uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData) {
    return false;
}

// Phase 5: Texture & Combiner Stubs
void VulkanBackend::BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) {
    // Stub for Phase 1
}

void VulkanBackend::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    // Stub for Phase 1
}

void VulkanBackend::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    // Stub for Phase 1
}


// Private Vulkan Setup Helpers
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
    vk::DeviceCreateInfo deviceInfo({}, 1, &queueInfo);

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
    vk::AttachmentDescription colorAttachment({}, m_colorFormat, vk::SampleCountFlagBits::e1,
                                            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);

    vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorRef);
    vk::RenderPassCreateInfo passInfo({}, 1, &colorAttachment, 1, &subpass);
    m_renderPass = m_device->createRenderPassUnique(passInfo);
    return true;
}

bool VulkanBackend::CreateHeadlessRenderTarget(uint32_t width, uint32_t height) {
    vk::ImageCreateInfo imageInfo({}, vk::ImageType::e2D, m_colorFormat,
                                 vk::Extent3D(width, height, 1), 1, 1,
                                 vk::SampleCountFlagBits::e1, vk::ImageTiling::eLinear,
                                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);

    m_headlessImage = m_device->createImageUnique(imageInfo);

    auto memReqs = m_device->getImageMemoryRequirements(m_headlessImage.get());
    uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);

    m_headlessImageMemory = m_device->allocateMemoryUnique(allocInfo);
    m_device->bindImageMemory(m_headlessImage.get(), m_headlessImageMemory.get(), 0);

    vk::ImageViewCreateInfo viewInfo({}, m_headlessImage.get(), vk::ImageViewType::e2D, m_colorFormat,
                                    {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    m_headlessImageView = m_device->createImageViewUnique(viewInfo);

    vk::FramebufferCreateInfo fbInfo({}, m_renderPass.get(), 1, &m_headlessImageView.get(), width, height, 1);
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

} // namespace GlideWrapper
