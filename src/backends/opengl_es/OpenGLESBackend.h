#pragma once

#include "backends/SoftwareBackendBase.h"
#include <vector>
#include <unordered_map>

namespace GlideWrapper {

/**
 * @brief Concrete OpenGL ES 3.2 translation engine.
 * Inherits from SoftwareBackendBase for 100% deterministic pixel-perfect software rasterization.
 * Employs SDL2-managed EGL context creation for cross-platform portability.
 */
class OpenGLESBackend : public SoftwareBackendBase {
public:
    OpenGLESBackend() = default;
    ~OpenGLESBackend() override { Shutdown(); }

    // Overridden Context & Presentation Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;
    void ResetState() override;

    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;
    uint32_t GetMsaaSamples() const override { return m_msaaSamples; }

    bool SwapBuffers() override;
    void SstIdle() override;
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    void DrawPoint(const ModernVertex& pt) override;
    void DrawLine(const ModernVertex& v0, const ModernVertex& v1) override;
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;
    void UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) override;
    void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
    void SetColorMask(bool rgb, bool alpha) override;
    void SetGamma(float gamma) override;
    void LoadGammaTable(uint32_t nentries, const uint32_t* rTable, const uint32_t* gTable, const uint32_t* bTable) override;

private:
    bool CreateGLContext(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed);
    void DestroyGLContext();
    void SyncDepthState();
    void ApplyBlendingGLES();
    void ApplyColorMaskGLES();
    void ApplyScissorGLES();
    void SyncCullStateGLES();
    void PopulateUniforms();

    // SDL2 & OpenGL ES Context Handles
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_glContext{nullptr};   // SDL_GLContext

    // GLES 3.2 presentation resources for streaming our software pixel map
    uint32_t m_glTexture{0};      // GLuint
    uint32_t m_glVAO{0};          // GLuint
    uint32_t m_glVBO{0};          // GLuint
    uint32_t m_glProgram{0};      // GLuint

    // Double-buffered offscreen GPU rendering targets
    uint32_t m_headlessFBOs[2]{0, 0};              // GLuint
    uint32_t m_fboTextures[2]{0, 0};             // GLuint
    uint32_t m_headlessDepthRenderbuffers[2]{0, 0}; // GLuint

    // Milestone 3: GPU-Native Geometry Resources
    bool CreateGeomShaders();
    bool CreateGeomBuffers();

    uint32_t m_geomProgram{0};              // GLuint
    uint32_t m_geomVAO{0};                  // GLuint
    uint32_t m_geomVBO{0};                  // GLuint
    uint32_t m_geomVBOOffset{0};
    const uint32_t m_geomVBOSize{1024 * 1024}; // 1MB dynamic ring buffer
    std::unordered_map<uint32_t, uint32_t> m_glesTextures;

    // Unified GPU Framebuffer LFB Flushing
    void FlushLFBToGPU();
    std::vector<uint32_t> m_swizzleBuffer;
    uint32_t m_msaaSamples{1};
    bool m_is2DGeometry{true};
    float m_gammaCorrectionValue{1.0f};
    
    // GLES Gamma LUT Texture Resources
    uint32_t m_glGammaLutTex{0};
    bool m_useGammaLut{false};

    // Cached Uniform Locations for geomProgram
    int m_uSstOriginLoc{-1};
    int m_uViewportSizeLoc{-1};
    int m_uDepthClipOverrideLoc{-1};
    int m_uDepthBufferModeLoc{-1};
    int m_uConstantColorLoc{-1};
    int m_uChromakeyEnabledLoc{-1};
    int m_uChromakeyValueLoc{-1};
    int m_uChromakeyRangeEnabledLoc{-1};
    int m_uChromakeyRangeMinLoc{-1};
    int m_uChromakeyRangeMaxLoc{-1};
    int m_uChromakeyRangeExclusiveLoc{-1};
    int m_uChromakeyRangeUnionLoc{-1};
    int m_uColorFuncLoc{-1};
    int m_uColorFactorLoc{-1};
    int m_uColorLocalLoc{-1};
    int m_uColorOtherLoc{-1};
    int m_uColorInvertLoc{-1};
    int m_uAlphaFuncLoc{-1};
    int m_uAlphaFactorLoc{-1};
    int m_uAlphaLocalLoc{-1};
    int m_uAlphaOtherLoc{-1};
    int m_uAlphaInvertLoc{-1};
    int m_uTextureEnabledLoc{-1};
    int m_uTexture0Loc{-1};
    int m_uTexture1Loc{-1};
    int m_uUseTmuOowLoc{-1};
    int m_uAlphaTestOpLoc{-1};
    int m_uAlphaTestRefLoc{-1};
    int m_uDepthBiasLoc{-1};
    int m_uFogModeLoc{-1};
    int m_uFogColorLoc{-1};
    int m_uFogTableLoc{-1};
    int m_uTexChromaEnabledLoc{-1};
    int m_uTexChromaRangeModeLoc{-1};
    int m_uTexChromaMinLoc{-1};
    int m_uTexChromaMaxLoc{-1};
};

} // namespace GlideWrapper
