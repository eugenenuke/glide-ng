#pragma once

#include "core/IGraphicsBackend.h"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <mutex>

namespace GlideWrapper {

/**
 * @brief Concrete production Vulkan translation engine.
 * Inherits directly from IGraphicsBackend for a true hardware-accelerated GPU pipeline.
 * Implements GPU-driven clearing, swapchain presentation, and dynamic state management.
 */
class VulkanBackend : public IGraphicsBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override { Shutdown(); }

    // Foundational Core Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;

    // Phase 2: Window Surface & Headless Swapchain Attachment
    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;

    // Phase 2: Execution Buffer Clearing & Swapchain Presentation
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    bool SwapBuffers() override;
    void SetConstantColor(uint32_t color) override;
    void SetPixelFormat(uint32_t format) override;
    void PollEvents() override;

    // Phase 3: Immediate Primitive Rasterization (Stubs for Phase 1)
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;
    void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
    void DrawPoint(const ModernVertex& pt) override;

    // Phase 4: Depth Comparison, Transparency Blending & Culling States (Stubs for Phase 1)
    void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) override;
    void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
    void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
    void SetCullState(uint32_t cullMode) override;
    void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) override;

    // Linear Framebuffer (LFB) cpu CPU inspection and modification (Stubs for Phase 1)
    void* GetLfbWritePtr() override;
    bool ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    bool WriteLFB(uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData) override;

    // Phase 5: Virtual TMU & Programmable Uber-Shader Entry Declarations (Stubs for Phase 1)
    void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) override;
    void SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t alphaFunc, uint32_t alphaFactor) override;
    void SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) override;

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateRenderPass();
    bool CreateHeadlessRenderTarget(uint32_t width, uint32_t height);
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    void TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range);

    // State Synchronization Mutex
    std::recursive_mutex m_mutex;
    bool m_initialized{false};
    bool m_windowAttached{false};
    bool m_headlessMode{false};
    WrapperConfig m_config;
    uint32_t m_pixelFormat{0};

    // Headless CPU-accessible memory buffer
    void* m_headlessPixelMap{nullptr};
    uint32_t m_headlessWidth{800};
    uint32_t m_headlessHeight{600};

    // Phase 4 State Trackers
    uint32_t m_clipMinX{0};
    uint32_t m_clipMinY{0};
    uint32_t m_clipMaxX{800};
    uint32_t m_clipMaxY{600};

    // Vulkan Context Handles
    vk::UniqueInstance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::UniqueDevice m_device;
    vk::Queue m_graphicsQueue;
    uint32_t m_graphicsQueueFamilyIndex{0};

    // Command submission infrastructure
    vk::UniqueCommandPool m_commandPool;
    vk::UniqueCommandBuffer m_commandBuffer;

    // Render Pass abstraction (Unused in Phase 1, but kept for future phases)
    vk::UniqueRenderPass m_renderPass;
    vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};

    // Headless automated CI testing execution handles
    vk::UniqueImage m_headlessImage;
    vk::UniqueDeviceMemory m_headlessImageMemory;
    vk::UniqueImageView m_headlessImageView;
    vk::UniqueFramebuffer m_headlessFramebuffer;

    // Staging buffer to copy GPU image to CPU
    vk::UniqueBuffer m_headlessPixelBuffer;
    vk::UniqueDeviceMemory m_headlessPixelMemory;

    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_sdlRenderer{nullptr}; // SDL_Renderer*
    void* m_sdlTexture{nullptr};  // SDL_Texture*
};

} // namespace GlideWrapper
