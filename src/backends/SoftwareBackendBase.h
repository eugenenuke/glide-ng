#pragma once

#include "core/IGraphicsBackend.h"
#include "core/WrapperConfig.h"
#include <mutex>
#include <vector>

namespace GlideWrapper {

class SoftwareBackendBase : public IGraphicsBackend {
public:
    SoftwareBackendBase() = default;
    virtual ~SoftwareBackendBase() = default;

    // Derived classes MUST implement these context/lifecycle routines
    bool Initialize(const WrapperConfig& config) override = 0;
    void Shutdown() override = 0;
    void ResetState() override;
    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override = 0;
    void DetachWindow() override = 0;
    uint32_t GetWidth() const override { return m_headlessWidth; }
    uint32_t GetHeight() const override { return m_headlessHeight; }
    uint32_t GetMsaaSamples() const override { return 1; }

    // State Getters for Testing
    uint32_t GetColorCombinerFunc() const { return m_colorCombinerFunc; }
    uint32_t GetColorCombinerFactor() const { return m_colorCombinerFactor; }
    uint32_t GetColorCombinerLocal() const { return m_colorCombinerLocal; }
    uint32_t GetColorCombinerOther() const { return m_colorCombinerOther; }
    uint32_t GetConstantColor() const { return m_constantColor; }
    uint32_t GetSTWHintMask() const { return m_stwHintMask; }
    uint32_t GetCullMode() const { return m_cullMode; }
    uint32_t GetSstOrigin() const { return m_sstOrigin; }
    float GetDepthBufferPixel(uint32_t x, uint32_t y) const {
        uint32_t pIdx = y * m_headlessWidth + x;
        if (pIdx < m_headlessDepthBuffer.size()) return m_headlessDepthBuffer[pIdx];
        return 0.0f;
    }

    bool SwapBuffers() override = 0;
    void PollEvents() override;
    void SstIdle() override;

    // Concrete shared implementations of clearing & state mutations
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    void SetConstantColor(uint32_t color) override;
    void SetPixelFormat(uint32_t format) override;
    void SetRenderBuffer(uint32_t target) override;

    // Phase 3: Immediate Primitive Rasterization (left as pure virtual from IGraphicsBackend)

    // Phase 4: Depth Comparison, Transparency Blending & Culling States
    void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) override;
    void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
    void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
    void SetCullState(uint32_t cullMode) override;
    void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) override;
    void SetSstOrigin(uint32_t origin) override;
    void SetColorMask(bool rgb, bool alpha) override;
    void SetGamma(float gamma) override;
    void LoadGammaTable(uint32_t nentries, const uint32_t* rTable, const uint32_t* gTable, const uint32_t* bTable) override;
    
    // Milestone 8: Fogging State
    void SetFogMode(uint32_t mode) override;
    void SetFogColor(uint32_t color) override;
    void SetFogTable(const uint8_t* table) override;

    // Phase 5: Virtual TMU & Combiner Configurations
    void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) override;
    void UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) override;
    void SetTexLodBias(uint32_t tmu, float bias) override;
    void UploadTexturePartial(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex, uint32_t lodLevel, uint32_t startRow, uint32_t endRow) override;
    void PurgeTextures() override;
    void SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal, uint32_t colorOther, bool colorInvert,
                         uint32_t alphaFunc, uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther, bool alphaInvert) override;
    void SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) override;
    void SetSTWHintState(uint32_t hintMask) override;
    void SetChromakeyMode(uint32_t mode) override;
    void SetChromakeyValue(uint32_t value) override;
    void SetChromakeyRange(uint32_t minColor, uint32_t maxColor, uint32_t mode) override;
    void SetTexChromakeyMode(uint32_t tmu, uint32_t mode) override;
    void SetTexChromakeyRange(uint32_t tmu, uint32_t minColor, uint32_t maxColor, uint32_t mode) override;

    // Getters for per-TMU texture chromakeying diagnostics/testing
    uint32_t GetTexChromaMode(uint32_t tmu) const { return tmu < 2 ? m_texChromaMode[tmu] : 0; }
    uint32_t GetTexChromaMin(uint32_t tmu) const { return tmu < 2 ? m_texChromaMin[tmu] : 0; }
    uint32_t GetTexChromaMax(uint32_t tmu) const { return tmu < 2 ? m_texChromaMax[tmu] : 0; }
    uint32_t GetTexChromaRangeMode(uint32_t tmu) const { return tmu < 2 ? m_texChromaRangeMode[tmu] : 0; }

    // Linear Framebuffer (LFB) read/write utilities
    void* GetLfbWritePtr() override;
    bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    bool WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData, const void* refData = nullptr, const LfbPipelineConfig& config = {}) override;

protected:

    std::recursive_mutex m_mutex;
    bool m_initialized{false};
    bool m_windowAttached{false};
    bool m_headlessMode{false};
    WrapperConfig m_config;

    // Derived backends are responsible for allocating/mapping this CPU staging buffer
    void* m_headlessPixelMap{nullptr};
    uint32_t m_headlessWidth{800};
    uint32_t m_headlessHeight{600};
    bool m_lfbDirty{false};
    bool m_lfbBufferDirty[2]{false, false}; // 0 = FRONTBUFFER dirty, 1 = BACKBUFFER dirty

    // Double buffering CPU emulation
    void AllocateCpuBuffers(uint32_t width, uint32_t height);
    void FreeCpuBuffers();
    
    std::vector<uint8_t> m_cpuBuffers[2];
    std::vector<uint8_t> m_resolvedBuffer;
    uint32_t m_frontBufferIdx{0};
    uint32_t m_backBufferIdx{1};
    uint32_t m_activeRenderBuffer{1}; // 1 = GR_BUFFER_BACKBUFFER (default)

    // Phase 4 State Trackers & Headless Depth Buffer Attachment
    uint32_t m_depthMode{0};      // 0 = Z-buffer, 1 = W-buffer
    uint32_t m_depthCompareOp{1}; // 1 = LESS (GR_CMP_LESS)
    bool m_depthMask{true};
    int32_t m_depthBiasLevel{0};

    uint32_t m_rgbSrcBlend{4};    // 4 = ONE (GR_BLEND_ONE)
    uint32_t m_rgbDstBlend{0};    // 0 = ZERO (GR_BLEND_ZERO)
    uint32_t m_alphaSrcBlend{4};
    uint32_t m_alphaDstBlend{0};

    uint32_t m_alphaTestOp{7};    // 7 = ALWAYS (GR_CMP_ALWAYS)
    uint32_t m_alphaTestRefVal{0};

    uint32_t m_cullMode{0};       // 0 = DISABLE (GR_CULL_DISABLE)

    uint32_t m_clipMinX{0};
    uint32_t m_clipMinY{0};
    uint32_t m_clipMaxX{800};
    uint32_t m_clipMaxY{600};
    uint32_t m_sstOrigin{0};      // 0 = GR_ORIGIN_UPPER_LEFT
    
    uint32_t m_fogMode{0};
    uint32_t m_fogColor{0xff000000};
    uint8_t m_fogTable[64] = {0};

    bool m_colorMaskRgb{true};
    bool m_colorMaskAlpha{true};

    std::vector<float> m_headlessDepthBuffer;

    // Phase 5 Software Texture Sampler Variables
    float m_texLodBias[2] = {0.0f, 0.0f};
    uint32_t m_boundTexAddress[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t m_texClampS[2] = {0, 0};
    uint32_t m_texClampT[2] = {0, 0};
    uint32_t m_texMinFilter[2] = {0, 0};
    uint32_t m_texMagFilter[2] = {0, 0};

    uint32_t m_colorCombinerFunc   = 0;
    uint32_t m_colorCombinerFactor = 0;
    uint32_t m_colorCombinerLocal  = 0;
    uint32_t m_colorCombinerOther  = 0;
    bool     m_colorCombinerInvert = false;
    uint32_t m_alphaCombinerFunc   = 0;
    uint32_t m_alphaCombinerFactor = 0;
    uint32_t m_alphaCombinerLocal  = 0;
    uint32_t m_alphaCombinerOther  = 0;
    bool     m_alphaCombinerInvert = false;

    uint32_t m_texCombinerRgbFunc[2]    = {0, 0};
    uint32_t m_texCombinerRgbFactor[2]  = {0, 0};
    uint32_t m_texCombinerAlphaFunc[2]  = {0, 0};
    uint32_t m_texCombinerAlphaFactor[2]= {0, 0};

    uint32_t m_constantColor{0xFFFFFFFF};
    uint32_t m_stwHintMask{0}; // Default: 0 (GR_STWHINT_W_OK)
    uint32_t m_pixelFormatOverride{1}; // 1 = ABGR
    uint32_t m_chromakeyMode{0};       // 0 = GR_CHROMAKEY_DISABLE
    uint32_t m_chromakeyValue{0};
    uint32_t m_chromakeyRangeMin{0};
    uint32_t m_chromakeyRangeMax{0};
    uint32_t m_chromakeyRangeMode{0};
    uint32_t m_texChromaMode[2]{0, 0};
    uint32_t m_texChromaMin[2]{0, 0};
    uint32_t m_texChromaMax[2]{0, 0};
    uint32_t m_texChromaRangeMode[2]{0, 0};
    uint32_t m_lastCpuClearColor{0};
    
    // Gamma correction software LUT state
    bool m_useGammaLut{false};
    uint8_t m_lutR[256];
    uint8_t m_lutG[256];
    uint8_t m_lutB[256];

    // Software Anti-Aliasing (AA) States
    std::string m_softwareAaMode{"analytical"};
    uint32_t m_ssaaScale{1};
    bool m_aaEnabled{false};
    void ResolveAAMode();
    void SetAAState(bool enabled) override;
};

} // namespace GlideWrapper
