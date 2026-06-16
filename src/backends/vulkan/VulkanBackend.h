#pragma once

#include "backends/SoftwareBackendBase.h"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace GlideWrapper {

/**
 * @brief Concrete production Vulkan translation engine.
 * Inherits from SoftwareBackendBase for 100% deterministic pixel-perfect software rasterization.
 * Implements Vulkan context initialization, memory allocation, and swapchain/SDL2 presentation.
 */
class VulkanBackend : public SoftwareBackendBase {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override { Shutdown(); }

    // Overridden Context & Presentation Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;

    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;

    bool SwapBuffers() override;

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateRenderPass();
    bool CreateHeadlessRenderTarget(uint32_t width, uint32_t height);
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    // Vulkan Context Handles
    vk::UniqueInstance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::UniqueDevice m_device;
    vk::Queue m_graphicsQueue;
    uint32_t m_graphicsQueueFamilyIndex{0};

    // Command submission infrastructure
    vk::UniqueCommandPool m_commandPool;
    vk::UniqueCommandBuffer m_commandBuffer;

    // Render Pass abstraction
    vk::UniqueRenderPass m_renderPass;
    vk::Format m_colorFormat{vk::Format::eR8G8B8A8Unorm};

    // Windowed execution handles (Vulkan Swapchain - if real GPU used)
    vk::UniqueSurfaceKHR m_surface;
    vk::UniqueSwapchainKHR m_swapchain;
    std::vector<vk::Image> m_swapchainImages;
    std::vector<vk::UniqueImageView> m_swapchainImageViews;
    std::vector<vk::UniqueFramebuffer> m_swapchainFramebuffers;
    vk::Extent2D m_swapchainExtent;

    // Headless automated CI testing execution handles
    vk::UniqueImage m_headlessImage;
    vk::UniqueDeviceMemory m_headlessImageMemory;
    vk::UniqueImageView m_headlessImageView;
    vk::UniqueFramebuffer m_headlessFramebuffer;

    // Headless CPU-accessible memory buffer
    vk::UniqueBuffer m_headlessPixelBuffer;
    vk::UniqueDeviceMemory m_headlessPixelMemory;

    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_sdlRenderer{nullptr}; // SDL_Renderer*
    void* m_sdlTexture{nullptr};  // SDL_Texture*
};

} // namespace GlideWrapper
