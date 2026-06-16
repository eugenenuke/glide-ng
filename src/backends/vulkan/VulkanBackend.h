#pragma once

#include "backends/SoftwareBackendBase.h"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <mutex>

namespace GlideWrapper {

/**
 * @brief Concrete production Vulkan translation engine.
 * Inherits from SoftwareBackendBase to utilize the CPU rasterizer as a progressive migration bridge.
 * Rewrites core presentation, clearing, and swapchain presentation on the GPU.
 */
class VulkanBackend : public SoftwareBackendBase {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override { Shutdown(); }

    // Foundational Core Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;

    // Window Surface & Headless Swapchain Attachment
    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;

    // GPU-Driven Clearing & Swapchain Presentation
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    bool SwapBuffers() override;
    void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) override;
    bool ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    void PollEvents() override;

    // Phase 2: Host-GPU Vertex Buffers & State Pipelines
    void DrawPoint(const ModernVertex& pt) override;
    void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateRenderPass();
    bool CreateHeadlessRenderTarget(uint32_t width, uint32_t height);
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    void TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range);

    // Vulkan Context Handles
    vk::UniqueInstance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::UniqueDevice m_device;
    vk::Queue m_graphicsQueue;
    uint32_t m_graphicsQueueFamilyIndex{0};
    bool m_depthClampEnabled{false};

    // Command submission infrastructure
    vk::UniqueCommandPool m_commandPool;
    vk::UniqueCommandBuffer m_commandBuffer;

    // Render Pass abstraction
    vk::UniqueRenderPass m_renderPass;
    vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};

    // Headless automated CI testing execution handles
    vk::UniqueImage m_headlessImage;
    vk::UniqueDeviceMemory m_headlessImageMemory;
    vk::UniqueImageView m_headlessImageView;
    vk::UniqueFramebuffer m_headlessFramebuffer;

    // Headless depth attachment resources
    vk::UniqueImage m_depthImage;
    vk::UniqueDeviceMemory m_depthImageMemory;
    vk::UniqueImageView m_depthImageView;
    vk::Format m_depthFormat{vk::Format::eD32Sfloat};

    // Staging buffer to copy GPU image to CPU
    vk::UniqueBuffer m_headlessPixelBuffer;
    vk::UniqueDeviceMemory m_headlessPixelMemory;

    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_sdlRenderer{nullptr}; // SDL_Renderer*
    void* m_sdlTexture{nullptr};  // SDL_Texture*

    // --- Phase 2: Dynamic Vertex Buffers & State Pipelines ---
    bool CreateShaderModules();
    bool CreateGraphicsPipelines();

    vk::UniqueShaderModule m_vertShaderModule;
    vk::UniqueShaderModule m_fragShaderModule;
    vk::UniquePipelineLayout m_pipelineLayout;
    vk::UniquePipeline m_pointPipeline;
    vk::UniquePipeline m_linePipeline;
    vk::UniquePipeline m_trianglePipeline;
    std::vector<vk::UniquePipeline> m_danglingPipelines;

    // Dynamic Ring Vertex Buffer
    vk::UniqueBuffer m_vertexBuffer;
    vk::UniqueDeviceMemory m_vertexBufferMemory;
    void* m_vertexBufferMap{nullptr};
    uint32_t m_vertexBufferOffset{0};
    const uint32_t m_vertexBufferSize{1024 * 1024}; // 1MB
    
    // Unified Frame Render Pass State
    bool m_inRenderPass{false};

    // Clear state tracking for Render Pass LoadOp clears
    vk::ClearColorValue m_clearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    float m_clearDepthValue{1.0f};
    bool m_clearColorPending{false};
    bool m_clearDepthPending{false};
};


} // namespace GlideWrapper
