#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#endif
#include <vulkan/vulkan.hpp>

#include "backends/CpuLfbStaging.h"
#include "backends/FrontBufferFlushTimer.h"
#include "backends/GlideStateTracker.h"
#include "core/HashUtils.h"
#include "core/HudOverlay.h"
#include "core/NativeSurfaceHost.h"

namespace GlideWrapper {

// Push Constants definition shared across pipeline layout and draw pipelines
// (exactly 256 bytes)
struct PushConstants {
  // Block 0: Offset 0 (16 bytes)
  float viewportWidth;
  float viewportHeight;
  uint32_t depthBufferMode;  // 0 = None, 1 = Z, 2 = W
  uint32_t alphaTestOp;      // 0..7 (Glide compare ops, 7 = ALWAYS/disabled)

  // Block 1: Offset 16 (16 bytes)
  uint32_t constantColor;  // Packed RGBA
  float alphaTestRef;      // 0.0 .. 1.0 (normalized reference value)
  float depthBias;         // Normalized depth bias
  uint32_t fogMode;        // 0 = disabled, otherwise enabled

  // Block 2: Offset 32 (16 bytes)
  uint32_t fogColor;  // Packed 32-bit ARGB/ABGR color
  uint32_t chromakeyRangeMode;
  float depthNear;  // Phase 4 depth range near (normalized)
  float depthFar;   // Phase 4 depth range far (normalized)

  // Block 3: Offset 48 (16 bytes)
  uint32_t chromakeyValue;     // Packed RGBA
  uint32_t chromakeyRangeMin;  // Packed RGBA
  uint32_t chromakeyRangeMax;  // Packed RGBA
  uint32_t tmuCombinerModes1;

  // Block 4: Offset 64 (64 bytes)
  uint32_t
      fogTable[16];  // 64-entry 8-bit fog table packed into 16 uint32_t values

  // Block 5: Offset 128 (16 bytes)
  uint32_t colorFunc;
  uint32_t colorFactor;
  uint32_t colorLocal;
  uint32_t colorOther;

  // Block 6: Offset 144 (16 bytes)
  uint32_t alphaFunc;
  uint32_t alphaFactor;
  uint32_t alphaLocal;
  uint32_t alphaOther;

  // Block 7: Offset 160 (16 bytes)
  uint32_t textureEnabled;
  uint32_t tmuCombinerModes0;
  uint32_t stipplePattern;  // Phase 4 32-bit stipple pattern
  uint32_t flags;           // Phase 4 packed flags

  // Block 8: Offset 176 (16 bytes)
  uint32_t texChromaMin0;  // TMU0 packed RGBA min (0xAABBGGRR)
  uint32_t texChromaMin1;  // TMU1 packed RGBA min (0xAABBGGRR)
  uint32_t texChromaMax0;  // TMU0 packed RGBA max (0xAABBGGRR)
  uint32_t texChromaMax1;  // TMU1 packed RGBA max (0xAABBGGRR)

  // Block 9: Offset 192 (16 bytes)
  uint32_t texDetail0;   // Packed TMU0 detail control (bias/scale/max)
  uint32_t texDetail1;   // Packed TMU1 detail control (bias/scale/max)
  float texLodBias0;     // TMU0 LOD bias for LOD_FRACTION combiner factor
  float texLodBias1;     // TMU1 LOD bias for LOD_FRACTION combiner factor

  // Block 10: Offset 208 (16 bytes) - VSA-100 COMBINE extension state
  uint32_t extColorCombine;
  uint32_t extAlphaCombine;
  uint32_t extTexColorCombine0;
  uint32_t extTexAlphaCombine0;

  // Block 11: Offset 224 (16 bytes) - VSA-100 TMU1 COMBINE + TMU constant color
  uint32_t extTexColorCombine1;
  uint32_t extTexAlphaCombine1;
  uint32_t tmuConstantColor0;  // Packed RGBA (0xAABBGGRR)
  uint32_t tmuConstantColor1;  // Packed RGBA (0xAABBGGRR)
};

static_assert(sizeof(PushConstants) == 240,
              "Vulkan PushConstants layout size mismatch!");
static_assert(sizeof(PushConstants) <= 256,
              "Vulkan PushConstants exceed 256-byte hardware limit!");

struct VulkanTexture {
  vk::UniqueImage image;
  vk::UniqueDeviceMemory memory;
  vk::UniqueImageView imageView;
  vk::Format format{vk::Format::eB8G8R8A8Unorm};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t mipLevels{0};
  size_t gpuByteSize{0};
  uint32_t clampS{0};
  uint32_t clampT{0};
  uint32_t minFilter{0};
  uint32_t magFilter{0};
  uint64_t dataHash{0};
};

struct TexturePoolKey {
  vk::Format format{vk::Format::eUndefined};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t mipLevels{0};
  bool operator==(const TexturePoolKey& other) const {
    return format == other.format && width == other.width &&
           height == other.height && mipLevels == other.mipLevels;
  }
};

struct TexturePoolKeyHash {
  std::size_t operator()(const TexturePoolKey& k) const {
    std::size_t h = 0;
    Hash::HashCombine(h, static_cast<std::size_t>(static_cast<uint32_t>(k.format)));
    Hash::HashCombine(h, static_cast<std::size_t>(k.width));
    Hash::HashCombine(h, static_cast<std::size_t>(k.height));
    Hash::HashCombine(h, static_cast<std::size_t>(k.mipLevels));
    return h;
  }
};

struct TextureContentKey {
  uint64_t dataHash{0};
  vk::Format format{vk::Format::eUndefined};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t mipLevels{0};
  bool operator==(const TextureContentKey& other) const {
    return dataHash == other.dataHash && format == other.format &&
           width == other.width && height == other.height &&
           mipLevels == other.mipLevels;
  }
};

struct TextureContentKeyHash {
  std::size_t operator()(const TextureContentKey& k) const {
    std::size_t h = std::hash<uint64_t>{}(k.dataHash);
    Hash::HashCombine(h, static_cast<std::size_t>(static_cast<uint32_t>(k.format)));
    Hash::HashCombine(h, static_cast<std::size_t>(k.width));
    Hash::HashCombine(h, static_cast<std::size_t>(k.height));
    Hash::HashCombine(h, static_cast<std::size_t>(k.mipLevels));
    return h;
  }
};

struct SamplerKey {
  uint32_t clampS;
  uint32_t clampT;
  uint32_t minFilter;
  uint32_t magFilter;
  float lodBias;
  uint32_t mipmapMode;
  bool lodBlend;
  bool operator==(const SamplerKey& other) const {
    return clampS == other.clampS && clampT == other.clampT &&
           minFilter == other.minFilter && magFilter == other.magFilter &&
           lodBias == other.lodBias && mipmapMode == other.mipmapMode &&
           lodBlend == other.lodBlend;
  }
};

struct SamplerKeyHash {
  std::size_t operator()(const SamplerKey& k) const {
    std::size_t h = 0;
    Hash::HashCombine(h, k.clampS);
    Hash::HashCombine(h, k.clampT);
    Hash::HashCombine(h, k.minFilter);
    Hash::HashCombine(h, k.magFilter);
    union {
      float f;
      uint32_t u;
    } u;
    u.f = k.lodBias;
    Hash::HashCombine(h, u.u);
    Hash::HashCombine(h, k.mipmapMode);
    Hash::HashCombine(h, k.lodBlend ? 1u : 0u);
    return h;
  }
};

struct DescriptorKey {
  vk::ImageView imageView0;
  vk::Sampler sampler0;
  vk::ImageView imageView1;
  vk::Sampler sampler1;
  bool operator==(const DescriptorKey& other) const {
    return imageView0 == other.imageView0 && sampler0 == other.sampler0 &&
           imageView1 == other.imageView1 && sampler1 == other.sampler1;
  }
};

template <typename VkHandle>
inline uint64_t VkHandleToU64(VkHandle h) {
  if constexpr (std::is_pointer_v<VkHandle>) {
    return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(h));
  } else {
    return static_cast<uint64_t>(h);
  }
}

struct DescriptorKeyHash {
  std::size_t operator()(const DescriptorKey& k) const {
    std::size_t h = 0;
    Hash::HashCombine(
        h, std::hash<uint64_t>{}(
               VkHandleToU64(static_cast<VkImageView>(k.imageView0))));
    Hash::HashCombine(
        h, std::hash<uint64_t>{}(
               VkHandleToU64(static_cast<VkSampler>(k.sampler0))));
    Hash::HashCombine(
        h, std::hash<uint64_t>{}(
               VkHandleToU64(static_cast<VkImageView>(k.imageView1))));
    Hash::HashCombine(
        h, std::hash<uint64_t>{}(
               VkHandleToU64(static_cast<VkSampler>(k.sampler1))));
    return h;
  }
};

struct PipelineStateKey {
  uint32_t cullMode{0};
  uint32_t sstOrigin{0};
  uint32_t depthMode{0};
  bool depthMask{false};
  uint32_t depthCompareOp{0};
  uint32_t rgbSrcBlend{0};
  uint32_t rgbDstBlend{0};
  uint32_t rgbBlendOp{0};
  uint32_t alphaSrcBlend{0};
  uint32_t alphaDstBlend{0};
  uint32_t alphaBlendOp{0};
  uint8_t colorWriteMask{0xFu};  // bit0=R, bit1=G, bit2=B, bit3=A
  bool singleSample{false};
  uint32_t sampleMask{0xFFFFFFFFu};
  bool stencilEnabled{false};
  uint32_t stencilCompareOp{7u};
  uint32_t stencilFailOp{0u};
  uint32_t stencilZFailOp{0u};
  uint32_t stencilZPassOp{0u};
  uint8_t compareToBiasPass{0u};  // 0=normal, 1=stencil mark, 2=stencil-gated write

  bool operator==(const PipelineStateKey& other) const {
    return cullMode == other.cullMode && sstOrigin == other.sstOrigin &&
           depthMode == other.depthMode && depthMask == other.depthMask &&
           depthCompareOp == other.depthCompareOp &&
           rgbSrcBlend == other.rgbSrcBlend &&
           rgbDstBlend == other.rgbDstBlend &&
           rgbBlendOp == other.rgbBlendOp &&
           alphaSrcBlend == other.alphaSrcBlend &&
           alphaDstBlend == other.alphaDstBlend &&
           alphaBlendOp == other.alphaBlendOp &&
           colorWriteMask == other.colorWriteMask &&
           singleSample == other.singleSample &&
           sampleMask == other.sampleMask &&
           stencilEnabled == other.stencilEnabled &&
           stencilCompareOp == other.stencilCompareOp &&
           stencilFailOp == other.stencilFailOp &&
           stencilZFailOp == other.stencilZFailOp &&
           stencilZPassOp == other.stencilZPassOp &&
           compareToBiasPass == other.compareToBiasPass;
  }
};

struct PipelineStateKeyHash {
  std::size_t operator()(const PipelineStateKey& k) const {
    std::size_t h = 0;
    Hash::HashCombine(h, k.cullMode);
    Hash::HashCombine(h, k.sstOrigin);
    Hash::HashCombine(h, k.depthMode);
    Hash::HashCombine(h, k.depthMask ? 1u : 0u);
    Hash::HashCombine(h, k.depthCompareOp);
    Hash::HashCombine(h, k.rgbSrcBlend);
    Hash::HashCombine(h, k.rgbDstBlend);
    Hash::HashCombine(h, k.rgbBlendOp);
    Hash::HashCombine(h, k.alphaSrcBlend);
    Hash::HashCombine(h, k.alphaDstBlend);
    Hash::HashCombine(h, k.alphaBlendOp);
    Hash::HashCombine(h, k.colorWriteMask);
    Hash::HashCombine(h, k.singleSample ? 1u : 0u);
    Hash::HashCombine(h, k.sampleMask);
    Hash::HashCombine(h, k.stencilEnabled ? 1u : 0u);
    Hash::HashCombine(h, k.stencilCompareOp);
    Hash::HashCombine(h, k.stencilFailOp);
    Hash::HashCombine(h, k.stencilZFailOp);
    Hash::HashCombine(h, k.stencilZPassOp);
    Hash::HashCombine(h, k.compareToBiasPass);
    return h;
  }
};

struct PipelineCacheKey {
  PipelineStateKey state;
  vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};
  bool operator==(const PipelineCacheKey& other) const {
    return state == other.state && topology == other.topology;
  }
};

struct PipelineCacheKeyHash {
  std::size_t operator()(const PipelineCacheKey& k) const {
    std::size_t h = PipelineStateKeyHash{}(k.state);
    Hash::HashCombine(h, static_cast<std::size_t>(k.topology));
    return h;
  }
};

/**
 * @brief Concrete production Vulkan translation engine.
 * Inherits pure pipeline state tracking from GlideStateTracker and composes
 * lazily-allocated CpuLfbStaging for Linear Framebuffer access.
 */
class VulkanBackend : public GlideStateTracker {
 public:
  VulkanBackend() = default;
  ~VulkanBackend() override { Shutdown(); }

  // Foundational Core Lifecycle
  bool Initialize(const WrapperConfig& config) override;
  void Shutdown() override;
  void ResetState() override;

  // Window Surface & Headless Swapchain Attachment
  bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height,
                    bool windowed) override;
  void DetachWindow() override;
  void SetPassthruMode(bool show3dfx) override;
  uint32_t GetMsaaSamples() const override { return m_msaaSamples; }
  bool HasAllocatedCpuLfbStaging() const { return m_lfbStaging.IsAllocated(); }
  uint64_t GetTexturePoolReuseCount() const { return m_texturePoolReuses; }
  uint64_t GetTextureContentCacheHitCount() const {
    return m_textureContentCacheHits;
  }
  size_t GetDescriptorPoolCount() const { return m_descriptorPools.size(); }

  // Selects an optimal Vulkan swapchain present mode.
  // On Wayland, prefers Mailbox even when VSync is enabled because Wayland
  // compositors present tear-free at VBlank and Mailbox avoids Mesa WSI
  // wp_fifo_v1 / wp_commit_timer_v1 stalls under RDP/Mutter.
  static vk::PresentModeKHR SelectPresentMode(
      const std::vector<vk::PresentModeKHR>& availableModes, bool vsyncEnabled,
      bool isWaylandSurface, bool forceStrictFifo = false);

  // Descriptor for a candidate X11 visual used to select a Vulkan-presentable
  // TrueColor visual when an attached X11Child inherits a non-TrueColor visual
  // (e.g. DirectColor from sdl12-compat / sdl2-compat) or a visual rejected by
  // vkGetPhysicalDeviceXlibPresentationSupportKHR.
  struct X11VisualCandidate {
    unsigned long visualId{0};
    int depth{0};
    bool isTrueColor{false};
    bool isDefault{false};
    bool presentSupported{false};
  };
  static unsigned long SelectPreferredX11Visual(
      const std::vector<X11VisualCandidate>& candidates);

  // GPU-Driven Clearing & Swapchain Presentation
  void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                   uint32_t clearMask) override;
  void ClearBufferExt(uint32_t color, uint32_t alpha, float z, uint8_t stencil,
                      uint32_t clearMask) override;
  bool SwapBuffers() override;
  void FlushBins() override {}
  void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask,
                     int32_t biasLevel) override;
  void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                     uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
  void SetBlendStateExt(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                        uint32_t rgbOp, uint32_t alphaSrcFactor,
                        uint32_t alphaDstFactor, uint32_t alphaOp) override;
  void SetColorCombineExt(uint32_t packed) override;
  void SetAlphaCombineExt(uint32_t packed) override;
  void SetTexColorCombineExt(uint32_t tmu, uint32_t packed) override;
  void SetTexAlphaCombineExt(uint32_t tmu, uint32_t packed) override;
  void SetTmuConstantColor(uint32_t tmu, uint32_t color) override;
  void SetCullState(uint32_t cullMode) override;
  void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
  void SetSstOrigin(uint32_t origin) override;
  void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX,
                     uint32_t maxY) override;
  void SetColorMask(bool rgb, bool alpha) override;
  void SetColorMaskExt(bool r, bool g, bool b, bool a) override;
  void SetAAMultiSample(bool enabled) override;
  void SetTBufferWriteMask(uint32_t mask) override;
  void SetStencilMode(bool enabled) override;
  void SetStencilFunc(uint32_t compareOp, uint8_t refVal,
                      uint8_t readMask) override;
  void SetStencilMask(uint8_t writeMask) override;
  void SetStencilOp(uint32_t failOp, uint32_t zFailOp,
                    uint32_t zPassOp) override;
  void SetDepthRange(float nearVal, float farVal) override;
  void SetDitherMode(uint32_t mode) override;
  void SetStippleState(uint32_t mode, uint32_t pattern) override;
  void SetGamma(float gamma) override;
  void LoadGammaTable(uint32_t nentries, const uint32_t* rTable,
                      const uint32_t* gTable, const uint32_t* bTable) override;
  void SetConstantColor(uint32_t color) override;
  void SetRenderBuffer(uint32_t target) override;
  void SetPixelFormat(uint32_t format) override;
  void* GetLfbWritePtr() override;
  bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth,
               uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
  bool WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                uint32_t srcFmt, const void* srcData,
                const void* refData = nullptr,
                const LfbPipelineConfig& config = {}) override;
  void PollEvents() override;
  void SstIdle() override;

  // Phase 2: Host-GPU Vertex Buffers & State Pipelines
  void DrawPoint(const ModernVertex& pt) override;
  void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
  void DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                    const ModernVertex& c) override;
  void DrawTriangleFan(const ModernVertex* verts, uint32_t count) override;
  void DrawTriangleList(const ModernVertex* verts, uint32_t count) override;
  void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS,
                   uint32_t clampT, uint32_t minFilter,
                   uint32_t magFilter) override;
  void UploadTexture(uint32_t tmu, uint32_t startAddress,
                     const struct VirtualTexture& tex) override;
  bool TryBindCachedTexture(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex) override;
  bool SupportsCompressedBcnTextures() const override {
    return m_textureCompressionBcEnabled;
  }
  bool BeginGpuTextureBufferMode() override;
  bool CommitGpuTextureBuffer(uint32_t tmu, uint32_t startAddress,
                              uint32_t baseWidth, uint32_t baseHeight,
                              uint32_t mipLevel, uint32_t totalMipLevels,
                              uint32_t copyWidth, uint32_t copyHeight,
                              uint32_t origin) override;
  void EndGpuTextureBufferMode() override;
  void SetTexLodBias(uint32_t tmu, float bias) override;
  void SetTexDetailControl(uint32_t tmu, int32_t lodBias, uint8_t detailScale,
                           float detailMax) override;
  void SetTexMipMapMode(uint32_t tmu, uint32_t mode, bool lodBlend) override;
  void UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex, uint32_t lodLevel,
                            uint32_t startRow, uint32_t endRow) override;
  void PurgeTextures() override;
  void SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor,
                       uint32_t colorLocal, uint32_t colorOther,
                       bool colorInvert, uint32_t alphaFunc,
                       uint32_t alphaFactor, uint32_t alphaLocal,
                       uint32_t alphaOther, bool alphaInvert) override;
  void SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor,
                          uint32_t alphaFunc, uint32_t alphaFactor,
                          bool rgbInvert, bool alphaInvert) override;
  void SetSTWHintState(uint32_t hintMask) override;
  void SetChromakeyMode(uint32_t mode) override;
  void SetChromakeyValue(uint32_t value) override;
  void SetChromakeyRange(uint32_t minColor, uint32_t maxColor,
                         uint32_t mode) override;
  void SetFogMode(uint32_t mode) override;
  void SetFogColor(uint32_t color) override;
  void SetFogTable(const uint8_t* table) override;

 protected:
  void ResetState_Locked() override;

 private:
  void DetachWindow_Locked(std::unique_lock<std::mutex>& lock);
  void UploadTexture_Locked(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex);
  bool CreateInstance();
  bool SelectPhysicalDevice();
  bool CreateLogicalDevice();
  bool CreateRenderPass();
  bool CreateHeadlessRenderTarget(uint32_t width, uint32_t height);
  bool CreateSwapchain(uint32_t width, uint32_t height);
  void DestroySwapchain_Locked();
  void RecreateSwapchain(uint32_t width, uint32_t height);
  uint32_t FindMemoryType(uint32_t typeFilter,
                          vk::MemoryPropertyFlags properties);
  void TransitionImageLayout(vk::CommandBuffer cmd, vk::Image image,
                             vk::ImageLayout oldLayout,
                             vk::ImageLayout newLayout,
                             vk::ImageSubresourceRange range);
  void FlushLFBToGPU_Locked();
  void FlushBatch(bool fromStateFlush = false);
  uint32_t ResolveEffectiveMsaaSamples_Locked() const;
  uint32_t GetEffectiveSampleMask_Locked() const;
  bool ShouldUseSingleSamplePass_Locked() const;
  void ResolveMsaaToSingle_Locked(uint32_t gpuIdx);
  bool InitMsaaCopyResources_Locked();
  void DestroyMsaaCopyResources_Locked();
  void CopySingleToMsaa_Locked(uint32_t gpuIdx);
  void DrawMaskedClearQuad_Locked(float r, float g, float b, float a, float z,
                                  uint8_t stencil, bool doColorClear,
                                  bool doDepthClear, bool doStencilClear,
                                  uint32_t sampleMask,
                                  const vk::Rect2D& clipRect,
                                  bool useSingleSample);
  bool HasStencilAspect() const {
    return m_depthFormat == vk::Format::eD24UnormS8Uint ||
           m_depthFormat == vk::Format::eD32SfloatS8Uint;
  }
  vk::ImageAspectFlags DepthStencilAspectMask() const {
    return HasStencilAspect() ? (vk::ImageAspectFlagBits::eDepth |
                                 vk::ImageAspectFlagBits::eStencil)
                              : vk::ImageAspectFlagBits::eDepth;
  }
  uint32_t GetActiveGpuBufferIdx() const {
    return (m_activeRenderBuffer == 0) ? 1u : 0u;
  }

  // Vulkan Context Handles
  vk::UniqueInstance m_instance;
  vk::PhysicalDevice m_physicalDevice;
  vk::UniqueDevice m_device;
  vk::Queue m_graphicsQueue;
  uint32_t m_graphicsQueueFamilyIndex{0};
  bool m_depthClampEnabled{false};
  bool m_textureCompressionBcEnabled{false};
  bool m_isCpuVulkanDevice{false};

  // Command submission infrastructure (Double-Buffered)
  vk::UniqueCommandPool m_commandPools[2];
  vk::UniqueCommandBuffer m_commandBuffers[2];
  vk::UniqueCommandBuffer m_uploadCommandBuffers[2];
  bool m_uploadCmdHasWork[2]{false, false};
  vk::CommandBuffer* m_commandBuffer{
      nullptr};  // Non-owning pointer pointing to active command buffer
  vk::UniqueFence
      m_fences[2];  // Fences to track CPU-GPU synchronization per frame slot
  uint32_t m_currentFrameSlot{0};  // Active frame slot (0 or 1)

  vk::CommandBuffer& BeginUploadCommandBuffer_Locked();
  void SubmitPendingUploadsAndWaitIdle_Locked();
  void SubmitMidFrameAndWaitIdle_Locked(bool reopenDrawCmd = true);

  // Render Pass abstraction
  vk::UniqueRenderPass m_renderPass;
  vk::UniqueRenderPass m_singleSampleRenderPass;
  vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};

  // Headless automated CI testing execution handles (Double-Buffered)
  vk::UniqueImage m_headlessImages[2];
  vk::UniqueDeviceMemory m_headlessImageMemories[2];
  vk::UniqueImageView m_headlessImageViews[2];
  vk::UniqueFramebuffer m_headlessFramebuffers[2];
  vk::UniqueFramebuffer m_singleSampleFramebuffers[2];

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
  vk::UniqueImageView m_msaaDepthSampleView;  // Depth-only aspect view for MSAA resolve
  bool m_msaaDepthSampleable{false};
  bool m_msaaDepthDirty{false};

  // VSA-100 TEXTUREBUFFER zero-readback GPU RTT backbuffer scratch image
  vk::UniqueImage m_rttSaveImage;
  vk::UniqueDeviceMemory m_rttSaveImageMemory;
  uint32_t m_rttSaveWidth{0};
  uint32_t m_rttSaveHeight{0};
  bool m_rttSaveImageInitialized{false};

  // Staging buffers to copy GPU image to CPU (Double-Buffered)
  vk::UniqueBuffer m_headlessPixelBuffers[2];
  vk::UniqueDeviceMemory m_headlessPixelMemories[2];

  // Native surface / child window host (Headless, StandaloneSDL2, X11Child,
  // WaylandSubsurface)
  NativeSurfaceHost m_surfaceHost;
  bool CreatePresentationSurface();
  void RecreateSurfaceAndSwapchain_Locked(uint32_t width, uint32_t height);
  bool m_passthruSuspended{false};
  bool m_detachedDeadWaylandWsi{false};

  vk::UniqueSurfaceKHR
      m_surface;  // Vulkan presentation surface backed by m_surfaceHost

  // --- Phase 9: Vulkan Swapchain Resources (Milestone 2) ---
  vk::UniqueSwapchainKHR m_swapchain;
  std::vector<vk::Image> m_swapchainImages;
  std::vector<vk::UniqueImageView> m_swapchainImageViews;
  vk::Format m_swapchainImageFormat{vk::Format::eB8G8R8A8Unorm};
  vk::Extent2D m_swapchainExtent;

  // Synchronization semaphores for swapchain presentation:
  // - imageAvailableSemaphores: indexed per frame-in-flight slot (0..1)
  // - renderFinishedSemaphores: indexed per swapchain image (imageIdx) so a
  //   semaphore handed to vkQueuePresentKHR is never reused until that image
  //   is re-acquired (fixes VUID-vkQueueSubmit-pSignalSemaphores-00067).
  vk::UniqueSemaphore m_imageAvailableSemaphores[2];
  std::vector<vk::UniqueSemaphore> m_renderFinishedSemaphores;

  // --- Phase 2: Dynamic Vertex Buffers & State Pipelines ---
  bool CreateShaderModules();
  PipelineStateKey GetCurrentStateKey() const;
  vk::UniquePipeline CompileSinglePipeline(const PipelineStateKey& key,
                                           vk::PrimitiveTopology topology);
  vk::Pipeline GetOrCreatePipeline(const PipelineStateKey& key,
                                   vk::PrimitiveTopology topology);

  vk::UniqueShaderModule m_vertShaderModule;
  vk::UniqueShaderModule m_fragShaderModule;
  vk::UniqueShaderModule m_fragShaderModuleDualSrc;
  vk::UniqueShaderModule m_fragShaderModuleWBuf;
  vk::UniqueShaderModule m_fragShaderModuleDualSrcWBuf;
  vk::UniquePipelineLayout m_pipelineLayout;

  // Pipeline cache storing owned UniquePipeline individually by cache key
  std::unordered_map<PipelineCacheKey, vk::UniquePipeline, PipelineCacheKeyHash>
      m_pipelineCache;
  vk::UniquePipelineCache
      m_vkPipelineCache;  // Persistent Vulkan driver-level pipeline cache

  // Dynamic Ring Vertex Buffer (partitioned into two per-frame slots aligned to
  // sizeof(ModernVertex) so active GPU draws never collide with CPU writes and
  // normal frames never stall on waitIdle()). Keep 32-bit processes lean to
  // preserve Win32/Wine virtual address space (especially under CPU adapters).
  static constexpr uint32_t kVertexSlotBudgetBytes =
      (sizeof(void*) <= 4) ? (2u * 1024u * 1024u) : (8u * 1024u * 1024u);
  static constexpr uint32_t kVertexSlotCapacityVerts =
      kVertexSlotBudgetBytes / sizeof(ModernVertex);
  static constexpr uint32_t kVertexSlotBytes =
      kVertexSlotCapacityVerts * sizeof(ModernVertex);
  vk::UniqueBuffer m_vertexBuffer;
  vk::UniqueDeviceMemory m_vertexBufferMemory;
  void* m_vertexBufferMap{nullptr};
  uint32_t m_vertexBufferOffset{0};
  const uint32_t m_vertexBufferSize{kVertexSlotBytes * 2};

  // Unified Frame Render Pass State
  bool m_inRenderPass{false};
  bool m_activePassIsSingleSample{false};
  uint32_t m_activePassGpuIdx{0};
  bool m_msaaColorDirty[2]{false, false};
  bool m_singleSampleColorDirty[2]{false, false};

  // 1x -> MSAA color seed pipeline (used when transitioning from aliased 1x
  // draws to MSAA draws within the same frame, e.g. split-screen AA toggle)
  vk::UniqueSampler m_msaaCopySampler;
  vk::UniqueDescriptorSetLayout m_msaaCopyDescSetLayout;
  vk::UniqueDescriptorPool m_msaaCopyDescPool;
  vk::DescriptorSet m_msaaCopyDescSets[2];
  vk::ImageView m_msaaCopyBoundView[2];
  vk::UniquePipelineLayout m_msaaCopyPipelineLayout;
  vk::UniquePipeline m_msaaCopyPipeline;

  // Draw Batching State & Cached Draw Pipeline/Descriptor Handles
  uint32_t m_batchedVertexCount{0};
  uint32_t m_batchedFirstVertex{0};
  vk::Pipeline m_batchedPipeline;
  vk::DescriptorSet m_batchedDescSet;
  vk::PrimitiveTopology m_batchedTopology{vk::PrimitiveTopology::eTriangleList};
  vk::Pipeline m_boundPipelineInPass;
  vk::DescriptorSet m_boundDescSetInPass;
  bool m_pipelineStateDirty{true};
  bool m_descriptorStateDirty{true};
  vk::Pipeline m_cachedPipeline;
  vk::PrimitiveTopology m_cachedTopology{vk::PrimitiveTopology::eTriangleList};
  vk::DescriptorSet m_cachedDescSet;
  void ResolveDrawState_Locked(vk::PrimitiveTopology topology,
                               vk::Pipeline& outPipeline,
                               vk::DescriptorSet& outDescSet);

  // Clear state tracking for Render Pass LoadOp clears (tracked per physical
  // GPU framebuffer FBO 0 and 1)
  vk::ClearColorValue m_clearColorValue[2] = {
      vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}),
      vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f})};
  float m_clearDepthValue[2]{1.0f, 1.0f};
  uint32_t m_clearStencilValue[2]{0u, 0u};
  bool m_clearColorPending[2]{false, false};
  bool m_clearDepthPending[2]{false, false};
  bool m_clearDepthPendingSingle[2]{false, false};
  bool m_clearStencilPending[2]{false, false};
  bool m_clearStencilPendingSingle[2]{false, false};
  // Dynamic Scissor Helper (caller must hold m_mutex)
  vk::Rect2D GetScissorRectVulkan_Locked();
  void EnsureRenderPassActive(bool useSingleSample);
  void EnsureRenderPassActive() {
    EnsureRenderPassActive(ShouldUseSingleSamplePass_Locked());
  }
  void PopulatePushConstants(PushConstants& pcs);

  vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
  static constexpr uint32_t kDescriptorPoolChunkSets = 4096u;
  struct DescriptorPoolChunk {
    vk::UniqueDescriptorPool pool;
    uint32_t allocatedSets{0};
  };
  struct TrackedDescriptorSet {
    vk::UniqueDescriptorSet set;
    size_t poolIdx{0};
  };
  size_t CreateTextureDescriptorPool_Locked();
  void ReleaseDescriptorSet_Locked(TrackedDescriptorSet& entry);
  std::vector<DescriptorPoolChunk> m_descriptorPools;
  size_t m_activeDescriptorPoolIdx{0};
  std::unordered_map<uint32_t, std::shared_ptr<VulkanTexture>>
      m_vulkanTextures[2];
  VulkanTexture m_dummyTexture;
  vk::UniqueSampler m_dummySampler;
  vk::DescriptorSet m_dummyDescriptorSet;
  void* m_gpuStagingMaps[2]{nullptr,
                            nullptr};  // Mapped staging memory addresses

  vk::Sampler GetOrCreateSampler(uint32_t clampS, uint32_t clampT,
                                 uint32_t minFilter, uint32_t magFilter,
                                 float lodBias, uint32_t mipmapMode,
                                 bool lodBlend);
  vk::DescriptorSet GetOrCreateDescriptorSet(vk::ImageView imageView0,
                                             vk::Sampler sampler0,
                                             vk::ImageView imageView1,
                                             vk::Sampler sampler1);

  std::unordered_map<SamplerKey, vk::UniqueSampler, SamplerKeyHash>
      m_samplerCache;
  std::unordered_map<DescriptorKey, TrackedDescriptorSet, DescriptorKeyHash>
      m_descriptorCache;
  // Reverse index from non-dummy VkImageView handle -> DescriptorKeys for O(1)
  // bidirectional descriptor set retirement in RetireTextureAtAddress_Locked.
  std::unordered_map<uint64_t,
                     std::unordered_set<DescriptorKey, DescriptorKeyHash>>
      m_viewToDescriptorKeys;

  // Helpers to keep command buffers and per-slot fences valid after errors so
  // subsequent frames never append to an ended command buffer or deadlock in
  // waitForFences(..., UINT64_MAX).
  void ResetFrameFenceSignaled_Locked(uint32_t slot);
  void ReopenCommandBuffer_Locked();

  // Deferred per-frame-slot retirement queues so overwritten/purged textures
  // and descriptor sets stay alive until the GPU fence for that slot signals.
  void EvictDescriptorsForView_Locked(vk::ImageView view,
                                      bool deferDestruction);
  void RetireTextureAtAddress_Locked(uint32_t tmu, uint32_t startAddress);
  void ReclaimRetiredResources_Locked(uint32_t slot);
  static constexpr size_t kDescriptorCacheHighWater = 3072;
  std::vector<std::shared_ptr<VulkanTexture>> m_retiredTextures[2];
  std::vector<TrackedDescriptorSet> m_retiredDescriptorSets[2];

  // Bounded O(1) bucketed free-list + content-addressed cache for zero-alloc
  // VkImage reuse across high-churn paletted workloads and TMU evictions.
  static constexpr size_t kMaxPooledTextures = 1024;
  static constexpr size_t kMaxPooledTextureBytes =
      (sizeof(void*) <= 4) ? (64ull * 1024ull * 1024ull)
                           : (128ull * 1024ull * 1024ull);

  struct PoolNode {
    std::shared_ptr<VulkanTexture> tex;
    TexturePoolKey poolKey{};
    TextureContentKey contentKey{};
    bool hasContentKey{false};
    std::list<uint64_t>::iterator lruIt;
    std::list<uint64_t>::iterator bucketIt;
  };

  bool TryBindContentCachedTexture_Locked(uint32_t tmu, uint32_t startAddress,
                                          vk::Format targetFormat,
                                          uint32_t mipCount, uint32_t width,
                                          uint32_t height,
                                          uint64_t incomingHash);
  std::shared_ptr<VulkanTexture> TryAcquireContentCachedTexture_Locked(
      const TextureContentKey& key);
  std::shared_ptr<VulkanTexture> TryAcquirePooledTexture_Locked(
      const TexturePoolKey& key, size_t estimatedBytes);
  void InsertIntoPool_Locked(std::shared_ptr<VulkanTexture> tex);
  std::shared_ptr<VulkanTexture> TakeFromPoolById_Locked(uint64_t id);
  void ClearTexturePool_Locked(bool evictDescriptors);
  void EraseFromLiveContentMap_Locked(const VulkanTexture& tex);

  // Non-owning index of live/in-flight textures (owned by m_vulkanTextures or
  // m_retiredTextures). Weak refs keep use_count() exact for GPU-idle checks.
  std::unordered_map<TextureContentKey, std::weak_ptr<VulkanTexture>,
                     TextureContentKeyHash>
      m_liveContentMap;
  // Idle (fence-completed) texture pool: O(1) lookup by content key or
  // (format, width, height, mipLevels) bucket, bounded by count & byte LRU.
  std::unordered_map<uint64_t, PoolNode> m_poolNodes;
  std::list<uint64_t> m_poolLru;  // front = MRU, back = LRU eviction victim
  std::unordered_map<TexturePoolKey, std::list<uint64_t>, TexturePoolKeyHash>
      m_poolBuckets;  // front = oldest reusable in bucket, back = newest
  std::unordered_map<TextureContentKey, uint64_t, TextureContentKeyHash>
      m_idleContentMap;
  uint64_t m_nextPoolNodeId{1};
  size_t m_texturePoolBytes{0};
  uint64_t m_texturePoolReuses{0};
  uint64_t m_textureContentCacheHits{0};

  // Persistent Texture Staging Buffer (Asynchronous Uploads)
  vk::UniqueBuffer m_texStagingBuffer;
  vk::UniqueDeviceMemory m_texStagingMemory;
  void* m_texStagingMap{nullptr};
  uint32_t m_texStagingOffsets[2]{0, 0};
  // 16 MB/slot (32 MB total) on 32-bit builds fits streaming lightmap/procedural
  // texture bursts (e.g. Unreal Engine 1) without per-frame mid-frame stalls
  // while staying well inside the Win32/Wine32 2-4 GB VA limit; 32 MB/slot on
  // 64-bit.
  const uint32_t m_texStagingSlotSize{
      (sizeof(void*) <= 4) ? (16u * 1024u * 1024u) : (32u * 1024u * 1024u)};
  uint32_t m_msaaSamples{1};

  // Lazily-allocated CPU LFB staging (0 bytes until first LFB operation)
  CpuLfbStaging m_lfbStaging;

  // --- M5: Sparse LFB Overlay Compositing Resources (lazily allocated) ---
  bool InitLfbOverlayPipeline_Locked();
  bool EnsureLfbOverlayResources_Locked(uint32_t width, uint32_t height,
                                        vk::DeviceSize requiredBytes);
  void DestroyLfbOverlayResources_Locked();
  void ReadbackGpuToStaging_Locked(uint32_t buffer);
  void CompositeLfbOverlay_Locked(uint32_t buffer, uint32_t targetGpuIdx,
                                  const uint32_t* pixels, uint32_t width,
                                  uint32_t height,
                                  const CpuLfbStaging::DirtyBand* bands,
                                  size_t bandCount);
  vk::UniqueSampler m_lfbOverlaySampler;
  vk::UniqueDescriptorSetLayout m_lfbOverlayDescSetLayout;
  vk::UniqueDescriptorPool m_lfbOverlayDescPool;
  vk::DescriptorSet m_lfbOverlayDescSet;
  vk::UniquePipelineLayout m_lfbOverlayPipelineLayout;
  vk::UniquePipeline m_lfbOverlayPipeline;
  vk::UniquePipeline m_lfbDepthPipeline;
  vk::UniquePipeline m_lfbDepthPipelineMsaa;
  vk::UniqueDescriptorPool m_msaaDepthResolveDescPool;
  vk::DescriptorSet m_msaaDepthResolveDescSet;
  vk::UniquePipeline m_msaaDepthResolvePipeline;
  bool InitLfbDepthPipeline_Locked(bool msaa);
  bool InitMsaaDepthResolvePipeline_Locked();
  bool ResolveMsaaDepthToSingle_Locked();
  bool ReadbackAuxDepthToShadow_Locked();
  bool WriteAuxDepthLFB_Locked(uint32_t dstX, uint32_t dstY, uint32_t srcWidth,
                               uint32_t srcHeight, int32_t srcStride,
                               const void* srcData, const void* refData);
  void PresentFrontBuffer_Locked();
  void NoteFrontBufferMutation_Locked(bool forceImmediate = false);
  void OnFrontBufferFlushTimer();
  bool m_lfbReadCacheValid[2]{false, false};
  bool m_frontBufferNeedsPresent{false};
  std::chrono::steady_clock::time_point m_lastFrontPresentTime{};
  std::vector<uint32_t> m_auxDepthScratch;
  std::vector<uint16_t> m_auxDepthShadow16;
  bool m_auxDepthShadowValid{false};
  vk::UniqueImage m_lfbOverlayImage;
  vk::UniqueDeviceMemory m_lfbOverlayImageMemory;
  vk::UniqueImageView m_lfbOverlayImageView;
  vk::ImageLayout m_lfbOverlayImageLayout{vk::ImageLayout::eUndefined};
  uint32_t m_lfbOverlayTexW{0};
  uint32_t m_lfbOverlayTexH{0};
  vk::UniqueBuffer m_lfbOverlayStagingBuffers[2];
  vk::UniqueDeviceMemory m_lfbOverlayStagingMemories[2];
  void* m_lfbOverlayStagingMaps[2]{nullptr, nullptr};
  vk::DeviceSize m_lfbOverlayStagingSizes[2]{0, 0};
  vk::DeviceSize m_lfbOverlayStagingOffsets[2]{0, 0};

  // Vulkan Gamma LUT State variables & guest-resolution RAMDAC LUT pass
  bool InitGammaPresentPipeline_Locked();
  bool EnsureGammaResolvedTargets_Locked();
  void DestroyGammaPresentResources_Locked();
  bool EnsureGammaLutTexture_Locked();
  bool ApplyGammaToGuestImage_Locked(uint32_t activeIdx,
                                     vk::Image& outResolvedImage);

  float m_gammaCorrectionValue{1.0f};
  bool m_gammaLutDirty{true};
  // m_useGammaLut and m_lutR/G/B are inherited from GlideStateTracker.
  vk::Format m_gammaPresentPipelineFormat{vk::Format::eUndefined};
  vk::UniqueRenderPass m_gammaPresentRenderPass;
  vk::UniqueImage m_gammaResolvedImages[2];
  vk::UniqueDeviceMemory m_gammaResolvedImageMemories[2];
  vk::UniqueImageView m_gammaResolvedImageViews[2];
  vk::UniqueFramebuffer m_gammaResolvedFramebuffers[2];
  uint32_t m_gammaResolvedWidth{0};
  uint32_t m_gammaResolvedHeight{0};
  vk::UniqueDescriptorSetLayout m_gammaPresentDescSetLayout;
  vk::UniqueDescriptorPool m_gammaPresentDescPool;
  vk::DescriptorSet m_gammaPresentDescSets[2];
  vk::ImageView m_gammaPresentBoundSrcView[2];
  vk::UniquePipelineLayout m_gammaPresentPipelineLayout;
  vk::UniquePipeline m_gammaPresentPipeline;
  vk::UniqueSampler m_gammaSrcSampler;
  vk::UniqueSampler m_gammaLutSampler;
  vk::UniqueImage m_gammaLutImage;
  vk::UniqueDeviceMemory m_gammaLutImageMemory;
  vk::UniqueImageView m_gammaLutImageView;
  vk::ImageLayout m_gammaLutImageLayout{vk::ImageLayout::eUndefined};
  vk::UniqueBuffer m_gammaLutStagingBuffers[2];
  vk::UniqueDeviceMemory m_gammaLutStagingMemories[2];
  void* m_gammaLutStagingMaps[2]{nullptr, nullptr};

  // --- M3: Post-Scene On-Screen HUD Overlay (zero Glide state mutation) ---
  bool InitHudPipeline_Locked();
  bool CreateHudSwapchainFramebuffers_Locked();
  void DestroyHudSwapchainFramebuffers_Locked();
  void DestroyHudResources_Locked();
  bool EnsureHudTexture_Locked(uint32_t width, uint32_t height);
  bool RenderHudToSwapchain_Locked(uint32_t imageIdx, const PresentRect& dst);

  HudOverlay m_hudOverlay;
  vk::Format m_hudPipelineFormat{vk::Format::eUndefined};
  vk::UniqueRenderPass m_hudRenderPass;
  std::vector<vk::UniqueFramebuffer> m_hudFramebuffers;
  vk::UniqueDescriptorSetLayout m_hudDescSetLayout;
  vk::UniqueDescriptorPool m_hudDescPool;
  vk::DescriptorSet m_hudDescSet;
  vk::UniquePipelineLayout m_hudPipelineLayout;
  vk::UniquePipeline m_hudPipeline;
  vk::UniqueSampler m_hudSampler;
  vk::UniqueImage m_hudImage;
  vk::UniqueDeviceMemory m_hudImageMemory;
  vk::UniqueImageView m_hudImageView;
  vk::ImageLayout m_hudImageLayout{vk::ImageLayout::eUndefined};
  uint32_t m_hudTexW{0};
  uint32_t m_hudTexH{0};
  uint64_t m_hudUploadedRev{0};
  vk::UniqueBuffer m_hudStagingBuffers[2];
  vk::UniqueDeviceMemory m_hudStagingMemories[2];
  void* m_hudStagingMaps[2]{nullptr, nullptr};
  vk::DeviceSize m_hudStagingSizes[2]{0, 0};

  FrontBufferFlushTimer m_frontFlushTimer{
      [this]() { OnFrontBufferFlushTimer(); }};
};

}  // namespace GlideWrapper
