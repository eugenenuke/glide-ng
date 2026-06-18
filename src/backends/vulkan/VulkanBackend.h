#pragma once

#include "backends/SoftwareBackendBase.h"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace GlideWrapper {

// Push Constants definition shared across pipeline layout and draw pipelines (exactly 128 bytes)
struct PushConstants {
    float viewportWidth;
    float viewportHeight;
    float padding[2];          // Explicitly align constantColor to 16-byte boundary (std140)
    float constantColor[4];    // RGBA normalized floats
    uint32_t depthBufferMode;   // 0 = None, 1 = Z, 2 = W
    uint32_t alphaTestOp;       // 0..7 (Glide compare ops, 7 = ALWAYS/disabled)
    float alphaTestRef;         // 0.0 .. 1.0 (normalized reference value)
    float depthBias;            // Normalized depth bias
    uint32_t fogMode;           // 0 = disabled, otherwise enabled
    uint32_t fogColor;          // Packed 32-bit ARGB/ABGR color
    uint32_t useTmuOow;         // 1 = use separate TMU OOW (grHints), 0 = use vertex OOW
    uint32_t fogTable[16];      // 64-entry 8-bit fog table packed into 16 uint32_t values

    // Milestone 9: Combiner & Texture State
    uint32_t colorFunc;
    uint32_t colorFactor;
    uint32_t colorLocal;
    uint32_t colorOther;
    uint32_t colorInvert;
    uint32_t alphaFunc;
    uint32_t alphaFactor;
    uint32_t alphaLocal;
    uint32_t alphaOther;
    uint32_t alphaInvert;
    uint32_t textureEnabled;
    uint32_t chromakeyEnabled;
    float    paddingPush;
    float    chromakeyValue[4];
};

struct VulkanTexture {
    vk::UniqueImage image;
    vk::UniqueDeviceMemory memory;
    vk::UniqueImageView imageView;
    vk::UniqueSampler sampler;
    vk::DescriptorSet descriptorSet; // Owned by m_descriptorPool
    uint32_t clampS{0};
    uint32_t clampT{0};
    uint32_t minFilter{0};
    uint32_t magFilter{0};
    uint64_t dataHash{0};
};

struct PipelineStateKey {
    uint32_t cullMode;
    uint32_t sstOrigin;
    uint32_t depthMode;
    bool depthMask;
    uint32_t depthCompareOp;
    uint32_t rgbSrcBlend;
    uint32_t rgbDstBlend;
    uint32_t alphaSrcBlend;
    uint32_t alphaDstBlend;

    bool operator==(const PipelineStateKey& other) const {
        return cullMode == other.cullMode &&
               sstOrigin == other.sstOrigin &&
               depthMode == other.depthMode &&
               depthMask == other.depthMask &&
               depthCompareOp == other.depthCompareOp &&
               rgbSrcBlend == other.rgbSrcBlend &&
               rgbDstBlend == other.rgbDstBlend &&
               alphaSrcBlend == other.alphaSrcBlend &&
               alphaDstBlend == other.alphaDstBlend;
    }
};

struct PipelineStateKeyHash {
    std::size_t operator()(const PipelineStateKey& k) const {
        std::size_t h = 0;
        auto combine = [&h](std::size_t val) {
            h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        combine(k.cullMode);
        combine(k.sstOrigin);
        combine(k.depthMode);
        combine(k.depthMask ? 1 : 0);
        combine(k.depthCompareOp);
        combine(k.rgbSrcBlend);
        combine(k.rgbDstBlend);
        combine(k.alphaSrcBlend);
        combine(k.alphaDstBlend);
        return h;
    }
};

struct PipelineGroup {
    vk::UniquePipeline point;
    vk::UniquePipeline line;
    vk::UniquePipeline triangle;
};


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
    uint32_t GetMsaaSamples() const override { return m_msaaSamples; }

    // GPU-Driven Clearing & Swapchain Presentation
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    bool SwapBuffers() override;
    void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) override;
    void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
    void SetCullState(uint32_t cullMode) override;
    void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
    void SetSstOrigin(uint32_t origin) override;
    void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) override;
    void SetConstantColor(uint32_t color) override;
    void SetRenderBuffer(uint32_t target) override;
    void SetPixelFormat(uint32_t format) override;
    bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    void PollEvents() override;
    void SstIdle() override;

    // Phase 2: Host-GPU Vertex Buffers & State Pipelines
    void DrawPoint(const ModernVertex& pt) override;
    void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;
    void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) override;
    void UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) override;
    void SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal, uint32_t colorOther, bool colorInvert,
                         uint32_t alphaFunc, uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther, bool alphaInvert) override;
    void SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) override;
    void SetSTWHintState(uint32_t hintMask) override;
    void SetChromakeyMode(uint32_t mode) override;
    void SetChromakeyValue(uint32_t value) override;
    void SetFogMode(uint32_t mode) override;
    void SetFogColor(uint32_t color) override;
    void SetFogTable(const uint8_t* table) override;

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateRenderPass();
    bool CreateHeadlessRenderTarget(uint32_t width, uint32_t height);
    bool CreateSwapchain(uint32_t width, uint32_t height);
    void DestroySwapchain();
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    void TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range);
    void FlushLFBToGPU();
    void FlushBatch();
    uint32_t GetActiveGpuBufferIdx() const { return (m_activeRenderBuffer == 0) ? 1u : 0u; }

    // Vulkan Context Handles
    vk::UniqueInstance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::UniqueDevice m_device;
    vk::Queue m_graphicsQueue;
    uint32_t m_graphicsQueueFamilyIndex{0};
    bool m_depthClampEnabled{false};

    // Command submission infrastructure (Double-Buffered)
    vk::UniqueCommandPool m_commandPools[2];
    vk::UniqueCommandBuffer m_commandBuffers[2];
    vk::CommandBuffer* m_commandBuffer{nullptr}; // Non-owning pointer pointing to active command buffer
    vk::UniqueFence m_fences[2];       // Fences to track CPU-GPU synchronization per frame slot
    uint32_t m_currentFrameSlot{0};     // Active frame slot (0 or 1)

    // Render Pass abstraction
    vk::UniqueRenderPass m_renderPass;
    vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};

    // Headless automated CI testing execution handles (Double-Buffered)
    vk::UniqueImage m_headlessImages[2];
    vk::UniqueDeviceMemory m_headlessImageMemories[2];
    vk::UniqueImageView m_headlessImageViews[2];
    vk::UniqueFramebuffer m_headlessFramebuffers[2];

    // Headless depth attachment resources
    vk::UniqueImage m_depthImage;
    vk::UniqueDeviceMemory m_depthImageMemory;
    vk::UniqueImageView m_depthImageView;
    vk::Format m_depthFormat{vk::Format::eD32Sfloat};

    // Multi-Sampled (MSAA) rendering resources
    vk::UniqueImage m_msaaColorImages[2];
    vk::UniqueDeviceMemory m_msaaColorImageMemories[2];
    vk::UniqueImageView m_msaaColorImageViews[2];
    vk::UniqueImage m_msaaDepthImage;
    vk::UniqueDeviceMemory m_msaaDepthImageMemory;
    vk::UniqueImageView m_msaaDepthImageView;


    // Staging buffers to copy GPU image to CPU (Double-Buffered)
    vk::UniqueBuffer m_headlessPixelBuffers[2];
    vk::UniqueDeviceMemory m_headlessPixelMemories[2];

    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    vk::UniqueSurfaceKHR m_surface; // Vulkan presentation surface associated with m_sdlWindow

    // --- Phase 9: Vulkan Swapchain Resources (Milestone 2) ---
    vk::UniqueSwapchainKHR m_swapchain;
    std::vector<vk::Image> m_swapchainImages;
    std::vector<vk::UniqueImageView> m_swapchainImageViews;
    vk::Format m_swapchainImageFormat{vk::Format::eB8G8R8A8Unorm};
    vk::Extent2D m_swapchainExtent;

    // Synchronization semaphores for swapchain presentation (Double-Buffered)
    vk::UniqueSemaphore m_imageAvailableSemaphores[2];
    vk::UniqueSemaphore m_renderFinishedSemaphores[2];

    // --- Phase 2: Dynamic Vertex Buffers & State Pipelines ---
    bool CreateShaderModules();
    bool CreateGraphicsPipelines();
    PipelineGroup CompileGraphicsPipelines(const PipelineStateKey& key);
    void UpdateActivePipelines();

    vk::UniqueShaderModule m_vertShaderModule;
    vk::UniqueShaderModule m_fragShaderModule;
    vk::UniquePipelineLayout m_pipelineLayout;
    
    // Active pipeline handles (non-owning pointers into the cache)
    vk::Pipeline m_activePointPipeline;
    vk::Pipeline m_activeLinePipeline;
    vk::Pipeline m_activeTrianglePipeline;

    // Pipeline cache storing owned UniquePipeline triplets
    std::unordered_map<PipelineStateKey, PipelineGroup, PipelineStateKeyHash> m_pipelineCache;
    std::vector<vk::UniquePipeline> m_danglingPipelines;
    vk::UniquePipelineCache m_vkPipelineCache; // Persistent Vulkan driver-level pipeline cache

    // Dynamic Ring Vertex Buffer
    vk::UniqueBuffer m_vertexBuffer;
    vk::UniqueDeviceMemory m_vertexBufferMemory;
    void* m_vertexBufferMap{nullptr};
    uint32_t m_vertexBufferOffset{0};
    const uint32_t m_vertexBufferSize{1024 * 1024}; // 1MB
    
    // Unified Frame Render Pass State
    bool m_inRenderPass{false};

    // Draw Batching State
    uint32_t m_batchedVertexCount{0};
    uint32_t m_batchedFirstVertex{0};
    vk::Pipeline m_batchedPipeline;
    vk::DescriptorSet m_batchedDescSet;

    // Clear state tracking for Render Pass LoadOp clears (tracked per physical GPU framebuffer FBO 0 and 1)
    vk::ClearColorValue m_clearColorValue[2] = {
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}),
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f})
    };
    float m_clearDepthValue[2]{1.0f, 1.0f};
    bool m_clearColorPending[2]{false, false};
    bool m_clearDepthPending[2]{false, false};
    // Dynamic Scissor Helper
    vk::Rect2D GetScissorRectVulkan();
    void EnsureRenderPassActive();
    void PopulatePushConstants(PushConstants& pcs);

    vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
    vk::UniqueDescriptorPool m_descriptorPool;
    std::unordered_map<uint32_t, VulkanTexture> m_vulkanTextures;
    VulkanTexture m_dummyTexture;
    void* m_gpuStagingMaps[2]{nullptr, nullptr}; // Mapped staging memory addresses

    // Persistent Texture Staging Buffer (Asynchronous Uploads)
    vk::UniqueBuffer m_texStagingBuffer;
    vk::UniqueDeviceMemory m_texStagingMemory;
    void* m_texStagingMap{nullptr};
    uint32_t m_texStagingOffsets[2]{0, 0};
    const uint32_t m_texStagingSlotSize{8 * 1024 * 1024}; // 8MB per frame slot
    uint32_t m_msaaSamples{1};
};


} // namespace GlideWrapper
