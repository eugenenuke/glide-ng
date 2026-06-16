#include "VulkanBackend.h"
#include "core/Logger.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <SDL2/SDL.h>

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

    GLIDE_LOG(INFO, "Vulkan", "Initializing Vulkan staging context...");
    m_config = config;

    if (!CreateInstance()) return false;
    if (!SelectPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateRenderPass()) return false;

    m_initialized = true;
    GLIDE_LOG(INFO, "Vulkan", "Vulkan staging context established successfully.");
    return true;
}

void VulkanBackend::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    GLIDE_LOG(INFO, "Vulkan", "Shutting down Vulkan staging context.");
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

    GLIDE_LOG(INFO, "Vulkan", "Attaching execution window surface (" << width << "x" << height << "), Windowed=" << (windowed ? "Yes" : "No"));

    m_headlessWidth = width;
    m_headlessHeight = height;
    m_headlessMode = true;
    
    if (!CreateHeadlessRenderTarget(width, height)) return false;

    // Dynamically query Vulkan GPU details for our dynamic diagnostics!
    auto props = m_physicalDevice.getProperties();
    std::string devName(props.deviceName);

    // Format a clean driver details line
    std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle << ", res=" << width << "x" << height << ")\r\n";
    std::cout << "Info: Host Vulkan Adapter: " << devName << " (Vulkan API " 
              << (props.apiVersion >> 22) << "." << ((props.apiVersion >> 12) & 0x3FF) << "." << (props.apiVersion & 0xFFF) << ")\r\n";
    std::cout << "Info: Pixel Format RGBA8888 D24S0 nAux 0 nSamples 0 0\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n" << std::flush;

    m_headlessDepthBuffer.resize(width * height, 0.0f);
    SetClipWindow(0, 0, width, height);

    if (!m_config.forceNoWindow) {
        GLIDE_LOG(INFO, "Vulkan", "Instantiating interactive SDL2 desktop presentation console...");
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            GLIDE_LOG(WARN, "Vulkan", "SDL_Init failed: " << SDL_GetError() << ". Running pure offscreen testing mode.");
        } else {
            auto* win = SDL_CreateWindow("3dfx glide-ng Presentation Console (Vulkan)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
            if (win) {
                auto* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
                
                if (ren) {
                    auto* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
                    if (tex) {
                        m_sdlWindow   = win;
                        m_sdlRenderer = ren;
                        m_sdlTexture  = tex;
                        GLIDE_LOG(INFO, "Vulkan", "SDL2 live visual console structured beautifully.");
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

    m_windowAttached = true;
    return true;
}

void VulkanBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "Vulkan", "Detaching active display attachment resources.");
    
    if (m_device) m_device->waitIdle();

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

bool VulkanBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    GLIDE_LOG(DEBUG, "Vulkan", "Executing SwapBuffers presentation dispatch.");

    // Enforce artificial retro game frame pacing (e.g., 60 FPS presentation capping)
    if (m_config.maxFps > 0.0f) {
        auto delay = std::chrono::microseconds(static_cast<int>(1000000.0f / m_config.maxFps));
        std::this_thread::sleep_for(delay);
    }

    if (m_sdlTexture && m_sdlRenderer && m_headlessPixelMap) {
        SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_headlessPixelMap, m_headlessWidth * 4);
        SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
        SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
    }

    return true;
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

    // Default to the first physical device
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

} // namespace GlideWrapper
