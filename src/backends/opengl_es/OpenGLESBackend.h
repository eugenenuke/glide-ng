#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <vector>

#include "backends/CpuLfbStaging.h"
#include "backends/GlideStateTracker.h"
#include "core/HudOverlay.h"
#include "core/NativeSurfaceHost.h"

namespace GlideWrapper {

/**
 * @brief Concrete OpenGL ES 3.2 translation engine.
 * Employs NativeSurfaceHost + Direct EGL 1.5 (X11 child window, Wayland
 * subsurface, surfaceless headless) with Standalone SDL2 fallback for demos.
 */
class OpenGLESBackend : public GlideStateTracker {
 public:
  OpenGLESBackend() = default;
  ~OpenGLESBackend() override { Shutdown(); }

  // Overridden Context & Presentation Lifecycle
  bool Initialize(const WrapperConfig& config) override;
  void Shutdown() override;
  void ResetState() override;

  bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height,
                    bool windowed) override;
  void DetachWindow() override;
  void SetPassthruMode(bool show3dfx) override;
  void ReleaseThreadContext() override;
  uint32_t GetMsaaSamples() const override { return m_msaaSamples; }
  bool HasAllocatedCpuLfbStaging() const { return m_lfbStaging.IsAllocated(); }

  bool SwapBuffers() override;
  void PollEvents() override;
  void FlushBins() override {}
  void SstIdle() override;
  void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                   uint32_t clearMask) override;
  void ClearBufferExt(uint32_t color, uint32_t alpha, float z, uint8_t stencil,
                      uint32_t clearMask) override;
  void* GetLfbWritePtr() override;
  bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth,
               uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
  bool WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                uint32_t srcFmt, const void* srcData,
                const void* refData = nullptr,
                const LfbPipelineConfig& config = {}) override;
  void DrawPoint(const ModernVertex& pt) override;
  void DrawLine(const ModernVertex& v0, const ModernVertex& v1) override;
  void DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                    const ModernVertex& c) override;
  void UploadTexture(uint32_t tmu, uint32_t startAddress,
                     const struct VirtualTexture& tex) override;
  bool SupportsCompressedBcnTextures() const override { return m_supportsS3tc; }
  bool BeginGpuTextureBufferMode() override;
  bool CommitGpuTextureBuffer(uint32_t tmu, uint32_t startAddress,
                              uint32_t baseWidth, uint32_t baseHeight,
                              uint32_t mipLevel, uint32_t totalMipLevels,
                              uint32_t copyWidth, uint32_t copyHeight,
                              uint32_t origin) override;
  void EndGpuTextureBufferMode() override;
  void UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex, uint32_t lodLevel,
                            uint32_t startRow, uint32_t endRow) override;
  void PurgeTextures() override;
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
  void SetGamma(float gamma) override;
  void SetRenderBuffer(uint32_t target) override;
  void SetDepthRange(float nearVal, float farVal) override;
  void SetDitherMode(uint32_t mode) override;
  void SetStippleState(uint32_t mode, uint32_t pattern) override;
  void LoadGammaTable(uint32_t nentries, const uint32_t* rTable,
                      const uint32_t* gTable, const uint32_t* bTable) override;
  void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask,
                     int32_t biasLevel) override;
  void SetCullState(uint32_t cullMode) override;
  void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
  void SetSstOrigin(uint32_t origin) override;
  void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX,
                     uint32_t maxY) override;
  void SetConstantColor(uint32_t color) override;
  void SetPixelFormat(uint32_t format) override;
  void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS,
                   uint32_t clampT, uint32_t minFilter,
                   uint32_t magFilter) override;
  void SetTexLodBias(uint32_t tmu, float bias) override;
  void SetTexDetailControl(uint32_t tmu, int32_t lodBias, uint8_t detailScale,
                           float detailMax) override;
  void SetTexMipMapMode(uint32_t tmu, uint32_t mode, bool lodBlend) override;
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
  void SetTexChromakeyMode(uint32_t tmu, uint32_t mode) override;
  void SetTexChromakeyRange(uint32_t tmu, uint32_t minColor, uint32_t maxColor,
                            uint32_t mode) override;
  void SetFogMode(uint32_t mode) override;
  void SetFogColor(uint32_t color) override;
  void SetFogTable(const uint8_t* table) override;

 protected:
  void ResetState_Locked() override;

 private:
  void DetachWindow_Locked();
  void UploadTexture_Locked(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex);
  void LoadGammaTable_Locked(uint32_t nentries, const uint32_t* rTable,
                             const uint32_t* gTable, const uint32_t* bTable);
  bool CreateGLContext(void* nativeWindowHandle, uint32_t width,
                       uint32_t height, bool windowed);
  void DestroyGLContext();
  bool CreateDirectEglContext();
  bool CreateStandaloneSdlGlContext();
  bool CreateWin32WglContext();
  void ReleaseWglContextOnCurrentThread_Locked();
  void RebindWglHdc_Locked(bool rebindCurrent);
  void UploadGammaLutIfDirty_Locked();
  bool EnsureContextCurrent();
  void SaveCurrentHostEglContext();
  void RestoreHostContextIfSaved();
  void PresentToWindowSurface(uint32_t hostW, uint32_t hostH);
  void SyncDepthState();
  void SyncStencilStateGLES_Locked();
  void ApplyBlendingGLES();
  void ApplyColorMaskGLES();
  void ApplyScissorGLES_Locked();
  void SyncCullStateGLES_Locked();
  void ApplySampleMaskGLES_Locked();
  void PopulateUniforms(uint32_t dirtyMask);
  void InvalidateGlBindingCache();
  uint32_t GetActiveGpuSlot() const {
    return (m_activeRenderBuffer == 0) ? 1u : 0u;
  }
  uint32_t GetActiveGpuFbo() const;
  uint32_t ResolveDesiredMsaaSamples_Locked() const;
  uint32_t GetEffectiveSampleMask_Locked() const;
  bool ShouldUseSingleSampleFbo_Locked() const;
  void ResolveMsaaToSingle_Locked(uint32_t slot);
  void CopySingleToMsaa_Locked(uint32_t slot);
  void DrawMaskedClearQuadGLES_Locked(float r, float g, float b, float a,
                                      float z, uint8_t stencil,
                                      bool doColorClear, bool doDepthClear,
                                      bool doStencilClear,
                                      uint32_t sampleMask);

  enum GlesDirtyBits : uint32_t {
    kGlesDirtyViewportAndOrigin = 1u << 0,
    kGlesDirtyDepth             = 1u << 1,
    kGlesDirtyBlend             = 1u << 2,
    kGlesDirtyColorMask         = 1u << 3,
    kGlesDirtyScissor           = 1u << 4,
    kGlesDirtyCull              = 1u << 5,
    kGlesDirtyConstantColor     = 1u << 6,
    kGlesDirtyChromakey         = 1u << 7,
    kGlesDirtyColorCombiner     = 1u << 8,
    kGlesDirtyTexBinding        = 1u << 9,
    kGlesDirtyTexSampler0       = 1u << 10,
    kGlesDirtyTexSampler1       = 1u << 11,
    kGlesDirtyTexCombiner       = 1u << 12,
    kGlesDirtyStwHint           = 1u << 13,
    kGlesDirtyAlphaTest         = 1u << 14,
    kGlesDirtyFogModeAndColor   = 1u << 15,
    kGlesDirtyFogTable          = 1u << 16,
    kGlesDirtyTexChromakey      = 1u << 17,
    kGlesDirtyDepthRange        = 1u << 18,
    kGlesDirtyDitherStipple     = 1u << 19,
    kGlesDirtySamplerUnits      = 1u << 20,
    kGlesDirtySampleMask        = 1u << 21,
    kGlesDirtyTexDetail         = 1u << 22,
    kGlesDirtyStencil           = 1u << 23,
    kGlesDirtyExtCombine        = 1u << 24,
    kGlesDirtyAll               = 0xFFFFFFFFu,
  };
  uint32_t m_glesDirtyMask{kGlesDirtyAll};

  // Cached GL object bindings to avoid redundant bind/unbind churn across
  // consecutive FlushBatch calls within a frame.
  uint32_t m_cachedBoundFbo{0xFFFFFFFFu};
  uint32_t m_cachedBoundProgram{0xFFFFFFFFu};
  uint32_t m_cachedBoundVao{0xFFFFFFFFu};
  uint32_t m_cachedBoundVbo{0xFFFFFFFFu};
  uint32_t m_cachedBoundTex[2]{0xFFFFFFFFu, 0xFFFFFFFFu};
  uint32_t m_cachedBoundSampler[2]{0xFFFFFFFFu, 0xFFFFFFFFu};
  uint32_t m_cachedActiveTexUnit{0xFFFFFFFFu};
  bool m_cachedViewportValid{false};

  // Unified native OS surface host (Option A: Headless / StandaloneSDL2 /
  // X11Child / WaylandSubsurface).
  NativeSurfaceHost m_surfaceHost;

  // Direct EGL 1.5 handles (opaque to keep EGL/X11/Wayland headers out of .h)
  void* m_eglDisplay{nullptr};   // EGLDisplay
  void* m_eglContext{nullptr};   // EGLContext
  void* m_eglSurface{nullptr};   // EGLSurface
  void* m_wlEglWindow{nullptr};  // wl_egl_window* (WaylandSubsurface only)
  bool m_useDirectEgl{false};

  // Win32 WGL OpenGL 3.3 Core handles (opaque to keep <windows.h> out of .h)
  void* m_wglHdc{nullptr};        // HDC
  void* m_wglContext{nullptr};    // HGLRC
  void* m_wglDummyHwnd{nullptr};  // Hidden HWND for headless WGL
  void* m_wglTargetHwnd{nullptr}; // HWND associated with m_wglHdc
  bool m_useWgl{false};
  bool m_eglDisplayOwned{false};  // Never eglTerminate borrowed wl_display*

  // StandaloneSDL2 fallback context (hWnd == 0 demos only)
  void* m_sdlGlContext{nullptr};  // SDL_GLContext

  // Saved foreign host EGL or GLX context (preserves DOSBox-X output=opengl on
  // both Wayland EGL and X11 GLX when host and wrapper share the same thread).
  bool m_hasSavedHostEgl{false};
  uint32_t m_savedHostEglApi{0};
  void* m_savedHostEglDisplay{nullptr};
  void* m_savedHostEglDraw{nullptr};
  void* m_savedHostEglRead{nullptr};
  void* m_savedHostEglContext{nullptr};

  bool m_hasSavedHostGlx{false};
  void* m_savedHostGlxDisplay{nullptr};
  unsigned long m_savedHostGlxDraw{0};
  unsigned long m_savedHostGlxRead{0};
  void* m_savedHostGlxContext{nullptr};

  // GLES 3.2 presentation quad & Gamma LUT shader resources
  uint32_t m_glVAO{0};      // GLuint
  uint32_t m_glVBO{0};      // GLuint
  uint32_t m_glProgram{0};  // GLuint

  // Double-buffered offscreen GPU rendering targets (1x resolved/aliased)
  uint32_t m_headlessFBOs[2]{0, 0};       // GLuint
  uint32_t m_fboTextures[2]{0, 0};        // GLuint
  uint32_t m_sharedDepthRenderbuffer{0};  // GLuint (Win32 desktop GL RBO)
  uint32_t m_sharedDepthTexture{0};       // GLuint (GLES 3.2 sampleable D24S8)
  uint32_t m_depthReadFbo{0};             // GLuint (lazy RGBA8 depth pack FBO)
  uint32_t m_depthReadTex{0};             // GLuint (lazy RGBA8 depth pack tex)
  uint32_t m_depthReadWidth{0};
  uint32_t m_depthReadHeight{0};
  uint32_t m_glDepthReadProgram{0};       // GLuint
  int m_uDepthReadTexLoc{-1};
  int m_uDepthReadFlipYLoc{-1};

  // Double-buffered offscreen MSAA rendering targets (allocated when
  // m_msaaSamples > 1; supports live GR_AA_MULTI_SAMPLE + T-Buffer masks)
  uint32_t m_msaaFBOs[2]{0, 0};       // GLuint
  uint32_t m_msaaColorRBOs[2]{0, 0};  // GLuint
  uint32_t m_msaaDepthRBO{0};         // GLuint
  bool m_msaaColorDirty[2]{false, false};
  bool m_singleSampleColorDirty[2]{false, false};
  bool m_supportsSampleMask{false};
  bool m_supportsS3tc{false};

  // VSA-100 TEXTUREBUFFER zero-readback GPU RTT resources (lazily allocated)
  uint32_t m_rttSaveFbo{0};
  uint32_t m_rttSaveTexture{0};
  uint32_t m_rttCommitFbo{0};
  uint32_t m_rttSaveWidth{0};
  uint32_t m_rttSaveHeight{0};

  // Milestone 3: GPU-Native Geometry Resources
  bool CreateGeomShaders();
  bool CreateGeomBuffers();

  uint32_t m_geomProgram{0};  // GLuint
  uint32_t m_geomVAO{0};      // GLuint
  uint32_t m_geomVBO{0};      // GLuint
  uint32_t m_geomVBOOffset{0};
  uint32_t m_geomVBOSizeConfigured{
      8 * 1024 * 1024};  // Configurable dynamic ring buffer size in bytes
  struct GlesTexEntry {
    uint32_t id{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipLevels{0};
    uint32_t internalFormat{0};
  };
  std::unordered_map<uint32_t, GlesTexEntry> m_glesTextures[2];
  std::vector<uint32_t> m_texUploadScratch;

  // Geometry Batching State
  enum class BatchPrimitiveMode { None, Points, Lines, Triangles };
  BatchPrimitiveMode m_batchPrimitiveMode{BatchPrimitiveMode::None};
  std::vector<ModernVertex> m_batchVertices;
  bool m_batchIs2DGeometry{false};
  void FlushBatch(bool fromStateFlush = false);

  // Unified GPU Framebuffer LFB Flushing (lazily allocated; caller holds m_mutex)
  void FlushLFBToGPU_Locked();
  CpuLfbStaging m_lfbStaging;
  std::vector<uint32_t> m_swizzleBuffer;
  uint32_t m_msaaSamples{1};
  bool m_is2DGeometry{true};
  float m_gammaCorrectionValue{1.0f};

  // GLES Gamma LUT Texture Resources (m_useGammaLut and m_lutR/G/B inherited
  // from GlideStateTracker).
  uint32_t m_glGammaLutTex{0};
  bool m_gammaLutDirty{false};
  uint64_t m_makeCurrentFailCount{0};
  int m_uUseGammaLutLoc{-1};
  int m_uPreserveAlphaLoc{-1};
  int m_uFlipYLoc{-1};

  // On-screen HUD Overlay Resources
  HudOverlay m_hudOverlay;
  uint32_t m_glHudTex{0};
  uint64_t m_hudUploadedRev{0};

  // Sparse LFB Overlay Composite Texture (lazily allocated on first LFB write)
  uint32_t m_glLfbOverlayTex{0};
  uint32_t m_glLfbOverlayWidth{0};
  uint32_t m_glLfbOverlayHeight{0};
  uint32_t m_glLfbDepthProgram{0};
  int m_uDepthQuadTexLoc{-1};
  int m_uDepthQuadFlipYLoc{-1};
  bool m_lfbReadCacheValid[2]{false, false};
  std::vector<uint16_t> m_auxDepthShadow16;
  std::vector<uint32_t> m_auxDepthScratch;
  std::vector<float> m_auxDepthFloatScratch;
  bool m_auxDepthShadowValid{false};
  bool m_msaaDepthDirty{false};
  std::chrono::steady_clock::time_point m_lastFrontPresentTime{};
  bool InitLfbDepthProgram_Locked();
  bool InitDepthReadProgram_Locked();
  bool ReadbackAuxDepthToShadow_Locked();
  bool WriteAuxDepthLFB_Locked(uint32_t dstX, uint32_t dstY, uint32_t srcWidth,
                               uint32_t srcHeight, int32_t srcStride,
                               const void* srcData, const void* refData);
  void PresentFrontBufferGLES_Locked();

  // Cached Uniform Locations for geomProgram
  int m_uSstOriginLoc{-1};
  int m_uDepthNearLoc{-1};
  int m_uDepthFarLoc{-1};
  int m_uDitherModeLoc{-1};
  int m_uStippleModeLoc{-1};
  int m_uStipplePatternLoc{-1};
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
  int m_uTexLodBiasLoc{-1};
  int m_uTexSampleLodBiasLoc{-1};
  int m_uTexDetailBiasLoc{-1};
  int m_uTexDetailScaleLoc{-1};
  int m_uTexDetailMaxLoc{-1};
  int m_uTexRgbFuncLoc{-1};
  int m_uTexRgbFactorLoc{-1};
  int m_uTexAlphaFuncLoc{-1};
  int m_uTexAlphaFactorLoc{-1};
  int m_uTexRgbInvertLoc{-1};
  int m_uTexAlphaInvertLoc{-1};
  int m_uExtCombineLoc{-1};
  int m_uTmuConstantColorLoc{-1};
  uint32_t m_glesSamplers[2]{0, 0};  // GLuint sampler objects for TMU0/1
  std::atomic<bool> m_passthruSuspended{false};
};

}  // namespace GlideWrapper
