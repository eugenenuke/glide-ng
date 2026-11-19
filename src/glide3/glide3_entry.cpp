#include <cmath>
#include <iostream>
#include <vector>

#include "core/3dfParser.h"
#include "core/BackendManager.h"
#include "core/GlideEntryCommon.h"
#include "core/GlideSplashAnimator.h"
#include "core/LfbManager.h"
#include "core/Logger.h"
#include "core/MathUtils.h"
#include "core/SimdKernels.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/VertexLayout.h"
#include "core/WrapperConfig.h"

#if !defined(_WIN32) && !defined(__linux__)
#define __linux__
#endif

#pragma GCC diagnostic ignored "-Wwrite-strings"

#include <glide.h>

#include <cstring>

// Fallback legacy defines if internal headers omitted them
#ifndef GR_VERSION
#define GR_EXTENSION 0xa0
#define GR_HARDWARE 0xa1
#define GR_RENDERER 0xa2
#define GR_VENDOR 0xa3
#define GR_VERSION 0xa4
#endif

extern "C" {
uint32_t s_sstOrigin = 0;
bool g_inSplash = false;  // Execution context flag for internally loaded
                          // Glide 2.x splash textures
FX_ENTRY void FX_CALL grShamelessPlug(void);
}  // Close extern "C"

namespace {
namespace EntryCommon = GlideWrapper::EntryCommon;

EntryCommon::SharedFrontendState s_state;

// Glide 3.x State Control cache
uint32_t s_coordSpace = 0;  // 0 = GR_WINDOW_COORDS, 1 = GR_CLIP_COORDS
float s_viewportX = 0.0f;
float s_viewportY = 0.0f;
float s_viewportWidth = 640.0f;
float s_viewportHeight = 480.0f;
float s_depthRangeNear = 1.0f;
float s_depthRangeFar = 65535.0f;

// Enable states
bool s_aaOrdered = false;
bool s_shamelessPlug = false;
bool s_aaMultiSample = true;
uint32_t s_tbufferWriteMask = 0xFFFFFFFFu;

// Phase 4 states
uint32_t s_stippleMode = 0;
uint32_t s_stipplePattern = 0xFFFFFFFF;
bool s_windowOpen = false;

// Legacy / extended Glide 3.x state
GrErrorCallbackFnc_t s_errorCallback = nullptr;
FxU32 s_lfbConstantDepth = 0;
bool s_lfbWriteColorSwizzle = false;
bool s_lfbWriteColorSwizzleSwap = false;
bool s_alphaControlsITRGBLighting = false;
GrPixelFormat_t s_activePixelFormat = GR_PIXFMT_RGB_565;
FxI32 s_numPendingBuffers = 2;
uint32_t s_pointsDrawn = 0;
uint32_t s_linesDrawn = 0;

// VSA-100 / H3 TEXTUREBUFFER dynamic render-to-texture state.
struct TextureBufferState {
  bool active{false};
  bool configured{false};
  uint32_t tmu{0};
  uint32_t startAddress{0};
  int32_t thisLodG2{0};
  int32_t largeLodG2{0};
  uint32_t aspectG2{3};
  uint32_t format{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t auxTmu{0};
  uint32_t auxAddress{0};
  uint32_t activeAuxBuffer{GR_BUFFER_AUXBUFFER};
  std::vector<uint32_t> savedBackbuffer;
  uint32_t savedW{0};
  uint32_t savedH{0};
  uint32_t savedClipMinX{0};
  uint32_t savedClipMinY{0};
  uint32_t savedClipMaxX{640};
  uint32_t savedClipMaxY{480};
  bool savedAaMultiSample{true};
  bool gpuModeActive{false};

  void Reset() { *this = TextureBufferState{}; }
};
TextureBufferState s_texBuf;

void NormalizeLfbPixelsToCanonicalArgb(std::vector<uint32_t>& pixels) {
  if (s_state.colorFormat != GR_COLORFORMAT_ABGR || pixels.empty()) return;
  GlideWrapper::GetSimdKernels().SwapRB32(pixels.data(), pixels.data(),
                                          pixels.size());
}

void ApplyTexBufClipWindow() {
  if (!s_texBuf.active || !s_texBuf.configured) return;
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend) return;
  const uint32_t clipW = std::min(s_texBuf.width, backend->GetGuestWidth());
  const uint32_t clipH = std::min(s_texBuf.height, backend->GetGuestHeight());
  EntryCommon::SetClipWindow(s_state, 0u, 0u, clipW, clipH);
}

void CommitActiveTextureBuffer() {
  if (!s_texBuf.active || !s_texBuf.configured || s_texBuf.width == 0 ||
      s_texBuf.height == 0) {
    return;
  }
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend) return;

  EntryCommon::FlushActiveLfbWriteLock(s_state);
  const uint32_t screenW = backend->GetGuestWidth();
  const uint32_t screenH = backend->GetGuestHeight();
  const uint32_t readW = std::min(s_texBuf.width, screenW);
  const uint32_t readH = std::min(s_texBuf.height, screenH);
  if (readW == 0 || readH == 0) return;

  // Fast path: zero-readback GPU-to-TMU commit on Vulkan & OpenGL ES.
  if (s_texBuf.gpuModeActive &&
      GlideWrapper::TextureManager::GetInstance().CommitGpuRenderedTexture(
          s_texBuf.tmu, s_texBuf.startAddress, s_texBuf.thisLodG2,
          s_texBuf.largeLodG2, s_texBuf.aspectG2, s_texBuf.format, readW, readH,
          s_sstOrigin)) {
    return;
  }

  const uint32_t srcY =
      (s_sstOrigin == GR_ORIGIN_LOWER_LEFT) ? (screenH - readH) : 0u;
  std::vector<uint32_t> readPixels(static_cast<size_t>(readW) * readH, 0u);
  if (!backend->ReadLFB(GR_BUFFER_BACKBUFFER, 0u, srcY, readW, readH,
                        readW * 4u, readPixels.data())) {
    return;
  }
  NormalizeLfbPixelsToCanonicalArgb(readPixels);

  std::vector<uint32_t> canonical;
  const std::vector<uint32_t>* commitPixels = &readPixels;
  if (readW != s_texBuf.width || readH != s_texBuf.height) {
    canonical.assign(static_cast<size_t>(s_texBuf.width) * s_texBuf.height, 0u);
    for (uint32_t y = 0; y < readH; ++y) {
      std::memcpy(canonical.data() + static_cast<size_t>(y) * s_texBuf.width,
                  readPixels.data() + static_cast<size_t>(y) * readW,
                  static_cast<size_t>(readW) * sizeof(uint32_t));
    }
    commitPixels = &canonical;
  }

  GlideWrapper::TextureManager::GetInstance().CommitRenderedTexture(
      s_texBuf.tmu, s_texBuf.startAddress, s_texBuf.thisLodG2,
      s_texBuf.largeLodG2, s_texBuf.aspectG2, s_texBuf.format, *commitPixels);
}

void EnterTextureBufferMode() {
  if (s_texBuf.active) return;
  EntryCommon::FlushActiveLfbWriteLock(s_state);
  s_texBuf.savedClipMinX = s_state.clipMinX;
  s_texBuf.savedClipMinY = s_state.clipMinY;
  s_texBuf.savedClipMaxX = s_state.clipMaxX;
  s_texBuf.savedClipMaxY = s_state.clipMaxY;
  s_texBuf.savedAaMultiSample = s_aaMultiSample;
  s_texBuf.gpuModeActive = false;

  if (auto* backend =
          GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    // Try zero-readback GPU backbuffer save first (Vulkan / OpenGL ES).
    if (backend->BeginGpuTextureBufferMode()) {
      s_texBuf.gpuModeActive = true;
    } else {
      const uint32_t w = backend->GetGuestWidth();
      const uint32_t h = backend->GetGuestHeight();
      if (w > 0 && h > 0) {
        s_texBuf.savedBackbuffer.assign(static_cast<size_t>(w) * h, 0u);
        s_texBuf.savedW = w;
        s_texBuf.savedH = h;
        if (backend->ReadLFB(GR_BUFFER_BACKBUFFER, 0u, 0u, w, h, w * 4u,
                             s_texBuf.savedBackbuffer.data())) {
          NormalizeLfbPixelsToCanonicalArgb(s_texBuf.savedBackbuffer);
        } else {
          s_texBuf.savedBackbuffer.clear();
          s_texBuf.savedW = 0;
          s_texBuf.savedH = 0;
        }
      }
    }
    backend->SetAAMultiSample(false);
    backend->SetRenderBuffer(GR_BUFFER_BACKBUFFER);
  }
  s_texBuf.active = true;
  ApplyTexBufClipWindow();
}

void ExitTextureBufferMode(GrBuffer_t targetBuffer) {
  if (!s_texBuf.active) return;
  CommitActiveTextureBuffer();
  s_texBuf.active = false;
  const bool wasGpuMode = s_texBuf.gpuModeActive;
  s_texBuf.gpuModeActive = false;

  if (auto* backend =
          GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    if (wasGpuMode) {
      backend->EndGpuTextureBufferMode();
    } else if (!s_texBuf.savedBackbuffer.empty() && s_texBuf.savedW > 0 &&
               s_texBuf.savedH > 0) {
      backend->WriteLFB(GR_BUFFER_BACKBUFFER, 0u, 0u, s_texBuf.savedW,
                        s_texBuf.savedH,
                        static_cast<int32_t>(s_texBuf.savedW * 4u), 0x100u,
                        s_texBuf.savedBackbuffer.data());
    }
    backend->SetAAMultiSample(s_texBuf.savedAaMultiSample);
  }
  s_texBuf.savedBackbuffer.clear();
  s_texBuf.savedW = 0;
  s_texBuf.savedH = 0;
  EntryCommon::SetClipWindow(s_state, s_texBuf.savedClipMinX,
                             s_texBuf.savedClipMinY, s_texBuf.savedClipMaxX,
                             s_texBuf.savedClipMaxY);
  EntryCommon::SetRenderBuffer(targetBuffer);
}

inline GrBuffer_t ResolveLfbTargetBuffer(GrBuffer_t buf) {
  return (buf == GR_BUFFER_TEXTUREBUFFER_EXT)
             ? static_cast<GrBuffer_t>(GR_BUFFER_BACKBUFFER)
             : buf;
}

inline void TransformVertexIfNecessary(GlideWrapper::ModernVertex& mv) {
  if (s_coordSpace == 1) {  // GR_CLIP_COORDS
    const bool depthActive = (s_state.depthMode != 0);
    mv.pos[0] = (mv.pos[0] + 1.0f) * 0.5f * s_viewportWidth + s_viewportX;
    mv.pos[1] = (mv.pos[1] + 1.0f) * 0.5f * s_viewportHeight + s_viewportY;
    if (depthActive) {
      mv.pos[2] =
          (mv.pos[2] + 1.0f) * 0.5f * (s_depthRangeFar - s_depthRangeNear) +
          s_depthRangeNear;
    }
  }
  EntryCommon::SanitizeVertex(s_state, mv);
}

void ResetFrontendState() {
  s_state.Reset(/*defaultLfbConstantAlpha=*/0, /*defaultTexCombineFunc=*/1);
  s_windowOpen = false;
  s_coordSpace = 0;
  s_viewportX = 0.0f;
  s_viewportY = 0.0f;
  s_viewportWidth = 640.0f;
  s_viewportHeight = 480.0f;
  s_depthRangeNear = 1.0f;
  s_depthRangeFar = 65535.0f;
  s_aaOrdered = false;
  s_shamelessPlug = false;
  s_aaMultiSample = true;
  s_tbufferWriteMask = 0xFFFFFFFFu;
  s_sstOrigin = 0;
  g_inSplash = false;
  EntryCommon::ResetAutoSplashState();
  s_stippleMode = 0;
  s_stipplePattern = 0xFFFFFFFF;
  s_errorCallback = nullptr;
  s_lfbConstantDepth = 0;
  s_lfbWriteColorSwizzle = false;
  s_lfbWriteColorSwizzleSwap = false;
  s_alphaControlsITRGBLighting = false;
  s_activePixelFormat = GR_PIXFMT_RGB_565;
  s_numPendingBuffers = 2;
  s_pointsDrawn = 0;
  s_linesDrawn = 0;
  s_texBuf.Reset();
}
}  // namespace

extern "C" {
struct GlideWrapperState {
  uint32_t depthMode;
  uint32_t depthCompare;
  int depthMask;
  int32_t depthBias;
  uint32_t alphaTestOp;
  uint32_t alphaTestRef;
  uint32_t clampS[2];
  uint32_t clampT[2];
  uint32_t minFilter[2];
  uint32_t magFilter[2];
  uint32_t colorFunc;
  uint32_t colorFactor;
  uint32_t colorLocal;
  uint32_t colorOther;
  int colorInvert;
  uint32_t alphaFunc;
  uint32_t alphaFactor;
  uint32_t alphaLocal;
  uint32_t alphaOther;
  int alphaInvert;
  uint32_t texRgbFunc[2];
  uint32_t texRgbFactor[2];
  uint32_t texAlphaFunc[2];
  uint32_t texAlphaFactor[2];
  uint32_t boundTex[2];
  uint32_t coordSpace;
  float viewportX;
  float viewportY;
  float viewportWidth;
  float viewportHeight;
  float depthRangeNear;
  float depthRangeFar;
  int aaOrdered;
  int shamelessPlug;
  uint8_t lfbConstantAlpha;
  uint32_t lfbWriteColorFormat;
  uint32_t chromakeyMode;
  uint32_t chromakeyValue;
  uint32_t chromakeyRangeMin;
  uint32_t chromakeyRangeMax;
  uint32_t chromakeyRangeMode;
  struct {
    GrTexChromakeyMode_t mode;
    GrColor_t minColor;
    GrColor_t maxColor;
  } texChromaState[2];
  uint32_t rgbSrcBlend;
  uint32_t rgbDstBlend;
  uint32_t alphaSrcBlend;
  uint32_t alphaDstBlend;
  uint32_t fogMode;
  uint32_t fogColor;
  uint8_t fogTable[64];
  uint32_t cullMode;
  uint32_t constantColor;
  uint32_t sstOrigin;
  float texLodBias[2];
  uint32_t mipmapMode[2];
  uint32_t lodBlend[2];
  uint32_t clipMinX;
  uint32_t clipMinY;
  uint32_t clipMaxX;
  uint32_t clipMaxY;
  int colorMaskRgb;
  int colorMaskAlpha;
  int texRgbInvert[2];
  int texAlphaInvert[2];
  uint32_t ditherMode;
  uint32_t stippleMode;
  uint32_t stipplePattern;
  uint32_t hasTbufferState;
  int aaMultiSample;
  uint32_t tbufferWriteMask;
  uint32_t hasColorMaskExt;
  uint8_t colorMaskR;
  uint8_t colorMaskG;
  uint8_t colorMaskB;
  int32_t texDetailBias[2];
  uint8_t texDetailScale[2];
  float texDetailMax[2];
  uint32_t hasStencilState;
  uint8_t stencilEnabled;
  uint8_t stencilRef;
  uint8_t stencilReadMask;
  uint8_t stencilWriteMask;
  uint8_t lfbConstantStencil;
  uint32_t stencilCompareOp;
  uint32_t stencilFailOp;
  uint32_t stencilZFailOp;
  uint32_t stencilZPassOp;
  uint32_t hasCombineExtState;
  uint32_t rgbBlendOp;
  uint32_t alphaBlendOp;
  uint32_t extColorCombine;
  uint32_t extAlphaCombine;
  uint32_t extTexColorCombine[2];
  uint32_t extTexAlphaCombine[2];
  uint32_t tmuConstantColor[2];
};

static_assert(sizeof(GlideWrapperState) <= 1024,
              "GlideWrapperState footprint exceeds the 1024-byte client "
              "allocation limit!");

FX_ENTRY void FX_CALL grGlideInit(void) {
  EntryCommon::InitGlide("Glide 3.x", ResetFrontendState);
}

inline void EnsureEngineInitialized() {
  EntryCommon::EnsureEngineInitialized(ResetFrontendState);
}

FX_ENTRY void FX_CALL grGlideShutdown(void) {
  s_windowOpen = false;
  EntryCommon::ShutdownGlide("Glide 3.x");
}

static uint32_t DecodePixelFormatSamples(GrPixelFormat_t pixelFormat) {
  switch (pixelFormat) {
    case GR_PIXFMT_AA_2_RGB_565:
    case GR_PIXFMT_AA_2_ARGB_1555:
    case GR_PIXFMT_AA_2_ARGB_8888:
      return 2u;
    case GR_PIXFMT_AA_4_RGB_565:
    case GR_PIXFMT_AA_4_ARGB_1555:
    case GR_PIXFMT_AA_4_ARGB_8888:
      return 4u;
    case GR_PIXFMT_AA_8_RGB_565:
    case GR_PIXFMT_AA_8_ARGB_1555:
    case GR_PIXFMT_AA_8_ARGB_8888:
      return 8u;
    default:
      return 1u;
  }
}

static GrContext_t OpenWindowGlide3(AnyPtr hWnd,
                                    GrScreenResolution_t resolution,
                                    GrScreenRefresh_t refresh,
                                    GrColorFormat_t format,
                                    GrOriginLocation_t origin,
                                    GrPixelFormat_t pixelFormat,
                                    uint32_t requestedSamples,
                                    int nColBuffers, int nAuxBuffers) {
  EnsureEngineInitialized();
  (void)refresh;
  const auto& cfg = GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  if (s_windowOpen && cfg.ignoreReopen) {
    GLIDE_LOG(INFO, "Frontend",
              "grSstWinOpen (Glide 3.x) ignored while window is active "
              "(FX_GLIDE_IGNORE_REOPEN=1).");
    return 1;
  }
  GlideWrapper::BackendManager::GetInstance().SetSplashAnimator(
      std::make_unique<GlideWrapper::GlideSplashAnimator>());
  GLIDE_LOG(INFO, "Frontend",
            "grSstWinOpen" << (requestedSamples > 0 ? "Ext" : "")
                           << " (Glide 3.x) invoked. Res=" << resolution
                           << ", Buffers=" << nColBuffers
                           << ", Aux=" << nAuxBuffers
                           << ", ReqSamples=" << requestedSamples);

  const auto dims = EntryCommon::ResolveResolution(resolution);
  const uint32_t width = dims.width;
  const uint32_t height = dims.height;

  // Reset all Glide 3.x frontend static global states to canonical Glide
  // defaults
  s_state.Reset(/*defaultLfbConstantAlpha=*/0, /*defaultTexCombineFunc=*/1,
                width, height);
  s_coordSpace = 0;
  s_viewportX = 0.0f;
  s_viewportY = 0.0f;
  s_viewportWidth = static_cast<float>(width);
  s_viewportHeight = static_cast<float>(height);
  s_depthRangeNear = 1.0f;
  s_depthRangeFar = 65535.0f;
  s_aaOrdered = false;
  s_shamelessPlug = false;
  s_aaMultiSample = true;
  s_tbufferWriteMask = 0xFFFFFFFFu;
  s_activePixelFormat = pixelFormat;
  s_texBuf.Reset();

  GlideWrapper::VertexLayout::GetInstance().ResetToGlide3Canonical();
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend) return 0;

  backend->SetRequestedMsaaSamples(requestedSamples);
  bool attached = backend->AttachWindow(
      reinterpret_cast<void*>(static_cast<uintptr_t>(hWnd)), width, height,
      hWnd != 0);
  backend->SetPixelFormat(format);
  s_sstOrigin = origin;
  backend->SetSstOrigin(origin);
  backend->SetAAMultiSample(true);
  backend->SetTBufferWriteMask(0xFFFFFFFFu);

  if (attached) {
    s_windowOpen = true;
    EntryCommon::ConfigureTmuMemoryLimits();
    EntryCommon::ApplyPostWinOpenOverrides(s_state, format, nColBuffers,
                                           nAuxBuffers);
    EntryCommon::MaybeRunAutoStartupSplash(
        width, height, [](uint32_t w, uint32_t h) {
          grSplash(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h),
                   0u);
        });
  }

  return attached ? 1 : 0;
}

FX_ENTRY GrContext_t FX_CALL grSstWinOpen(AnyPtr hWnd,
                                          GrScreenResolution_t resolution,
                                          GrScreenRefresh_t refresh,
                                          GrColorFormat_t format,
                                          GrOriginLocation_t origin,
                                          int nColBuffers, int nAuxBuffers) {
  return OpenWindowGlide3(hWnd, resolution, refresh, format, origin,
                          GR_PIXFMT_RGB_565, /*requestedSamples=*/0u,
                          nColBuffers, nAuxBuffers);
}

FX_ENTRY GrContext_t FX_CALL grSstWinOpenExt(AnyPtr hWnd,
                                             GrScreenResolution_t resolution,
                                             GrScreenRefresh_t refresh,
                                             GrColorFormat_t format,
                                             GrOriginLocation_t origin,
                                             GrPixelFormat_t pixelFormat,
                                             int nColBuffers, int nAuxBuffers) {
  EnsureEngineInitialized();
  uint32_t reqSamples = DecodePixelFormatSamples(pixelFormat);
  const auto& cfg = GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  if (cfg.model == GlideWrapper::CardModel::Voodoo4 && reqSamples > 2u) {
    GLIDE_LOG(WARN, "Frontend",
              "grSstWinOpenExt (Glide 3.x): Voodoo4 (single VSA-100) supports "
              "up to 2-sample Spatial AA; clamping requested "
                  << reqSamples << "x to 2x.");
    reqSamples = 2u;
  } else if (cfg.model < GlideWrapper::CardModel::Voodoo4 && reqSamples > 1u) {
    GLIDE_LOG(WARN, "Frontend",
              "grSstWinOpenExt (Glide 3.x): Active card profile does not "
              "support VSA-100 Spatial AA; clamping requested "
                  << reqSamples << "x to 1x.");
    reqSamples = 1u;
  }
  GLIDE_LOG(INFO, "Frontend",
            "grSstWinOpenExt (Glide 3.x): pixelFormat=0x"
                << std::hex << pixelFormat << std::dec
                << " -> requestedSamples=" << reqSamples);
  return OpenWindowGlide3(hWnd, resolution, refresh, format, origin,
                          pixelFormat, reqSamples, nColBuffers, nAuxBuffers);
}

FX_ENTRY void FX_CALL grTBufferWriteMaskExt(FxU32 tmask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  s_tbufferWriteMask = static_cast<uint32_t>(tmask);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTBufferWriteMaskExt (Glide 3.x): mask=0x" << std::hex << tmask
                                                         << std::dec);
  if (auto* backend =
          GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    backend->SetTBufferWriteMask(s_tbufferWriteMask);
  }
}

FX_ENTRY void FX_CALL grColorMaskExt(FxBool r, FxBool g, FxBool b, FxBool a) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grColorMaskExt (Glide 3.x): R=" << r << ", G=" << g
                                             << ", B=" << b << ", A=" << a);
  EntryCommon::SetColorMaskExt(s_state, r != 0, g != 0, b != 0, a != 0);
}

FX_ENTRY void FX_CALL grStencilFuncExt(GrCmpFnc_t fnc, GrStencil_t ref,
                                       GrStencil_t mask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grStencilFuncExt (Glide 3.x): fnc=" << fnc
                                                 << ", ref=" << static_cast<int>(ref)
                                                 << ", mask=" << static_cast<int>(mask));
  EntryCommon::SetStencilFunc(s_state, static_cast<uint32_t>(fnc),
                              static_cast<uint8_t>(ref),
                              static_cast<uint8_t>(mask));
}

FX_ENTRY void FX_CALL grStencilMaskExt(GrStencil_t mask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grStencilMaskExt (Glide 3.x): mask=" << static_cast<int>(mask));
  EntryCommon::SetStencilMask(s_state, static_cast<uint8_t>(mask));
}

FX_ENTRY void FX_CALL grStencilOpExt(GrStencilOp_t fail, GrStencilOp_t zfail,
                                     GrStencilOp_t zpass) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grStencilOpExt (Glide 3.x): fail=" << fail << ", zfail=" << zfail
                                                << ", zpass=" << zpass);
  EntryCommon::SetStencilOp(s_state, static_cast<uint32_t>(fail),
                            static_cast<uint32_t>(zfail),
                            static_cast<uint32_t>(zpass));
}

FX_ENTRY void FX_CALL grBufferClearExt(GrColor_t color, GrAlpha_t alpha,
                                       FxU32 depth, GrStencil_t stencil) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grBufferClearExt (Glide 3.x): color=0x"
                << std::hex << color << std::dec
                << ", alpha=" << static_cast<int>(alpha) << ", depth=" << depth
                << ", stencil=" << static_cast<int>(stencil));
  EntryCommon::ClearBufferExt(color, static_cast<uint8_t>(alpha), depth,
                              static_cast<uint8_t>(stencil));
}

FX_ENTRY void FX_CALL grLfbConstantStencilExt(GrStencil_t stencil) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbConstantStencilExt (Glide 3.x): stencil="
                << static_cast<int>(stencil));
  s_state.lfbConstantStencil = static_cast<uint8_t>(stencil);
}

FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context) {
  EnsureEngineInitialized();
  (void)context;
  s_texBuf.Reset();
  s_windowOpen = false;
  EntryCommon::CloseWindow("grSstWinClose (Glide 3.x) invoked.");
  return FXTRUE;
}

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha,
                                    FxU32 depth) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  EntryCommon::FlushActiveLfbWriteLock(s_state);
  EntryCommon::ClearBuffer(color, alpha, depth);
}

FX_ENTRY void FX_CALL grBufferSwap(FxU32 swapInterval) {
  EnsureEngineInitialized();
  (void)swapInterval;
  if (s_texBuf.active) {
    CommitActiveTextureBuffer();
  }
  EntryCommon::FlushActiveLfbWriteLock(s_state);
  if (EntryCommon::ShouldRenderShamelessPlug(s_shamelessPlug)) {
    grShamelessPlug();
  }
  EntryCommon::SwapBuffers();
}

FX_ENTRY void FX_CALL grSetNumPendingBuffers(FxI32 NumPendingBuffers) {
  EnsureEngineInitialized();
  if (NumPendingBuffers < 1) NumPendingBuffers = 1;
  if (NumPendingBuffers > 8) NumPendingBuffers = 8;
  s_numPendingBuffers = NumPendingBuffers;
  GLIDE_LOG(DEBUG, "Frontend",
            "grSetNumPendingBuffers (Glide 3.x): " << s_numPendingBuffers);
}

FX_ENTRY FxBool FX_CALL grSelectContext(GrContext_t context) {
  EnsureEngineInitialized();
  GLIDE_LOG(INFO, "Frontend",
            "grSelectContext (Glide 3.x) invoked for Context=" << context);
  return FXTRUE;
}

FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grRenderBuffer (Glide 3.x) invoked for Buffer=" << buffer);
  if (buffer == GR_BUFFER_TEXTUREBUFFER_EXT) {
    EnterTextureBufferMode();
  } else if (s_texBuf.active) {
    ExitTextureBufferMode(buffer);
  } else {
    EntryCommon::SetRenderBuffer(buffer);
  }
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grSstSelect (Glide 3.x) invoked for SST=" << which_sst);
}

FX_ENTRY void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grVertexLayout (Glide 3.x) invoked. Param="
                << param << ", Offset=" << offset << ", Mode=" << mode);
  GlideWrapper::VertexLayout::GetInstance().SetParamOffsetGlide3(param, offset,
                                                                 mode);
}

FX_ENTRY void FX_CALL grDrawTriangle(const void* a, const void* b,
                                     const void* c) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(TRACE, "Frontend",
            "grDrawTriangle (Glide 3.x) invoked with a=" << a << ", b=" << b
                                                         << ", c=" << c);
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend || !a || !b || !c) {
    GLIDE_LOG(WARN, "Frontend",
              "grDrawTriangle early exit: backend="
                  << backend << ", a=" << a << ", b=" << b << ", c=" << c);
    return;
  }

  GLIDE_PROFILE_SCOPE("API::grDrawTriangle");
  GLIDE_INCREMENT_TRIANGLES_PROCESSED(1);

  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  GlideWrapper::ModernVertex va;
  GlideWrapper::ModernVertex vb;
  GlideWrapper::ModernVertex vc;
  layout.DecodeVertexInto(a, va);
  layout.DecodeVertexInto(b, vb);
  layout.DecodeVertexInto(c, vc);

  TransformVertexIfNecessary(va);
  TransformVertexIfNecessary(vb);
  TransformVertexIfNecessary(vc);

  EntryCommon::SyncTexturesBeforeDraw();
  backend->DrawTriangle(va, vb, vc);
}

FX_ENTRY void FX_CALL grAADrawTriangle(const void* a, const void* b,
                                       const void* c, FxBool ab_strong,
                                       FxBool bc_strong, FxBool ca_strong) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  GLIDE_LOG(TRACE, "Frontend", "grAADrawTriangle (Glide 3.x) invoked.");
  grDrawTriangle(a, b, c);
}

FX_ENTRY void FX_CALL grDrawVertexArray(FxU32 mode, FxU32 count,
                                        void* pointers) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(TRACE, "Frontend",
            "grDrawVertexArray (Glide 3.x) invoked: mode=" << mode << ", count="
                                                           << count);
  if (!pointers || count < 3) return;

  GLIDE_PROFILE_SCOPE("API::grDrawVertexArray");
  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  const void* const* ptrs = static_cast<const void* const*>(pointers);

  constexpr FxU32 kStackCap = 64;
  GlideWrapper::ModernVertex stackVerts[kStackCap];
  std::vector<GlideWrapper::ModernVertex> heapVerts;
  GlideWrapper::ModernVertex* verts = stackVerts;
  if (count > kStackCap) {
    heapVerts.resize(count);
    verts = heapVerts.data();
  }
  for (FxU32 i = 0; i < count; ++i) {
    layout.DecodeVertexInto(ptrs[i], verts[i]);
    TransformVertexIfNecessary(verts[i]);
  }

  EntryCommon::DrawVertexList(mode, count, verts,
                              /*allowPointsAndLines=*/false);
}

FX_ENTRY void FX_CALL grDrawVertexArrayContiguous(FxU32 mode, FxU32 count,
                                                  void* pointers,
                                                  FxU32 stride) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(TRACE, "Frontend",
            "grDrawVertexArrayContiguous (Glide 3.x) invoked: mode="
                << mode << ", count=" << count << ", stride=" << stride);
  if (!pointers || count < 3 || stride == 0) return;

  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  int32_t maxOffset = layout.GetMaxActiveOffset();
  if (static_cast<int32_t>(stride) < maxOffset) {
    GLIDE_LOG(WARN, "Frontend",
              "grDrawVertexArrayContiguous: stride ("
                  << stride
                  << ") is smaller than maximum active layout offset ("
                  << maxOffset
                  << "). Rejecting draw call to prevent buffer overflow.");
    return;
  }

  GLIDE_PROFILE_SCOPE("API::grDrawVertexArrayContiguous");
  const char* rawBytes = static_cast<const char*>(pointers);

  constexpr FxU32 kStackCap = 64;
  GlideWrapper::ModernVertex stackVerts[kStackCap];
  std::vector<GlideWrapper::ModernVertex> heapVerts;
  GlideWrapper::ModernVertex* verts = stackVerts;
  if (count > kStackCap) {
    heapVerts.resize(count);
    verts = heapVerts.data();
  }
  for (FxU32 i = 0; i < count; ++i) {
    layout.DecodeVertexInto(rawBytes + i * stride, verts[i]);
    TransformVertexIfNecessary(verts[i]);
  }

  EntryCommon::DrawVertexList(mode, count, verts,
                              /*allowPointsAndLines=*/false);
}

FX_ENTRY void FX_CALL grTriStats(FxU32* processed, FxU32* drawn) {
  uint32_t p = 0;
  uint32_t d = 0;
  EntryCommon::GetTriStats(processed ? &p : nullptr, drawn ? &d : nullptr);
  if (processed) *processed = static_cast<FxU32>(p);
  if (drawn) *drawn = static_cast<FxU32>(d);
}

FX_ENTRY void FX_CALL grResetTriStats(void) { EntryCommon::ResetTriStats(); }

FX_ENTRY void FX_CALL grSstPerfStats(GrSstPerfStats_t* stats) {
  if (stats) {
    std::memset(stats, 0, sizeof(GrSstPerfStats_t));
    uint32_t pixelsIn = 0;
    uint32_t chromaFail = 0;
    EntryCommon::GetPerfStats(pixelsIn, chromaFail);
    stats->pixelsIn = static_cast<FxU32>(pixelsIn);
    stats->chromaFail = static_cast<FxU32>(chromaFail);
  }
}

FX_ENTRY void FX_CALL grSstResetPerfStats(void) {
  EntryCommon::ResetPerfStats();
}

FX_ENTRY void FX_CALL grDrawLine(const void* a, const void* b) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(TRACE, "Frontend", "grDrawLine (Glide 3.x) invoked.");
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend || !a || !b) return;
  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  GlideWrapper::ModernVertex va = layout.DecodeVertex(a);
  GlideWrapper::ModernVertex vb = layout.DecodeVertex(b);

  TransformVertexIfNecessary(va);
  TransformVertexIfNecessary(vb);

  EntryCommon::SyncTexturesBeforeDraw();
  ++s_linesDrawn;
  backend->DrawLine(va, vb);
}

FX_ENTRY void FX_CALL grDrawPoint(const void* pt) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  GLIDE_LOG(TRACE, "Frontend", "grDrawPoint (Glide 3.x) invoked.");
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend || !pt) return;
  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  GlideWrapper::ModernVertex mv = layout.DecodeVertex(pt);

  TransformVertexIfNecessary(mv);

  EntryCommon::SyncTexturesBeforeDraw();
  ++s_pointsDrawn;
  backend->DrawPoint(mv);
}

static bool Is32BitPixelFormat(GrPixelFormat_t fmt) {
  return fmt == GR_PIXFMT_ARGB_8888 || fmt == GR_PIXFMT_AA_2_ARGB_8888 ||
         fmt == GR_PIXFMT_AA_4_ARGB_8888 || fmt == GR_PIXFMT_AA_8_ARGB_8888;
}

static bool Is1555PixelFormat(GrPixelFormat_t fmt) {
  return fmt == GR_PIXFMT_ARGB_1555 || fmt == GR_PIXFMT_AA_2_ARGB_1555 ||
         fmt == GR_PIXFMT_AA_4_ARGB_1555 || fmt == GR_PIXFMT_AA_8_ARGB_1555;
}

FX_ENTRY FxU32 FX_CALL grGet(FxU32 pname, FxU32 plength, FxI32* params) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grGet (Glide 3.x) invoked for pname: " << pname);
  if (!params || plength < 4) return 0;

  auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
  const auto& config = reg.GetConfig();

  switch (pname) {
    case GR_MEMORY_FB:
      *params = static_cast<FxI32>(std::max(1u, config.fbiMemoryMb) * 1024u *
                                   1024u);
      return 4;
    case GR_MEMORY_TMU:
      *params = static_cast<FxI32>(std::max(1u, config.tmuMemoryMb) * 1024u *
                                   1024u);
      return 4;
    case GR_MEMORY_UMA:
      *params = (config.model >= GlideWrapper::CardModel::VoodooBanshee)
                    ? static_cast<FxI32>(std::max(1u, config.fbiMemoryMb) *
                                         1024u * 1024u)
                    : 0;
      return 4;
    case GR_NUM_BOARDS:
      *params = 1;
      return 4;
    case GR_NUM_FB:
      *params = (config.model == GlideWrapper::CardModel::Voodoo5 ||
                 (config.model == GlideWrapper::CardModel::Voodoo2 &&
                  config.enableSli))
                    ? 2
                    : 1;
      return 4;
    case GR_NUM_TMU:
      *params = static_cast<FxI32>(config.tmuCount);
      return 4;
    case GR_MAX_TEXTURE_SIZE:
      // Authentic 3dfx hardware limit (h3/h5 diget.c): 2048x2048 is VSA-100
      // (Voodoo4/Voodoo5) only; SST-1, Rush, Voodoo2, Banshee, and Voodoo3
      // all cap at 256x256.
      *params = (config.model >= GlideWrapper::CardModel::Voodoo4) ? 2048 : 256;
      return 4;
    case GR_MAX_TEXTURE_ASPECT_RATIO:
      *params = 3;  // 8:1
      return 4;
    case GR_NON_POWER_OF_TWO_TEXTURES:
      *params = FXFALSE;
      return 4;
    case GR_BITS_DEPTH:
      *params = Is32BitPixelFormat(s_activePixelFormat) ? 32 : 16;
      return 4;
    case GR_BITS_RGBA:
      if (plength < 16) return 0;
      if (Is32BitPixelFormat(s_activePixelFormat)) {
        params[0] = 8;
        params[1] = 8;
        params[2] = 8;
        params[3] = 8;
      } else if (Is1555PixelFormat(s_activePixelFormat)) {
        params[0] = 5;
        params[1] = 5;
        params[2] = 5;
        params[3] = 1;
      } else {
        params[0] = 5;
        params[1] = 6;
        params[2] = 5;
        params[3] = 0;
      }
      return 16;
    case GR_BITS_GAMMA:
      *params = 8;
      return 4;
    case GR_FOG_TABLE_ENTRIES:
      *params = 64;
      return 4;
    case GR_GAMMA_TABLE_ENTRIES:
      *params = 256;
      return 4;
    case GR_GLIDE_STATE_SIZE:
      *params = 1024;
      return 4;
    case GR_GLIDE_VERTEXLAYOUT_SIZE:
      *params = 256;
      return 4;
    case GR_FIFO_FULLNESS:
      *params = 0;
      return 4;
    case GR_IS_BUSY:
      *params = 0;
      return 4;
    case GR_LFB_PIXEL_PIPE:
      *params = FXTRUE;
      return 4;
    case GR_NUM_SWAP_HISTORY_BUFFER:
      *params = 8;
      return 4;
    case GR_PENDING_BUFFERSWAPS:
      *params = 0;
      return 4;
    case GR_REVISION_FB:
    case GR_REVISION_TMU:
      switch (config.model) {
        case GlideWrapper::CardModel::VoodooGraphics:
          *params = (pname == GR_REVISION_FB) ? 2 : 1;
          break;
        case GlideWrapper::CardModel::VoodooRush:
          *params = 1;
          break;
        case GlideWrapper::CardModel::Voodoo2:
          *params = (pname == GR_REVISION_FB) ? 2 : 1;
          break;
        case GlideWrapper::CardModel::VoodooBanshee:
          *params = 0x1000;
          break;
        case GlideWrapper::CardModel::Voodoo3:
          *params = 0x11100;
          break;
        case GlideWrapper::CardModel::Voodoo4:
          *params = 0x11104;
          break;
        case GlideWrapper::CardModel::Voodoo5:
          *params = 0x11105;
          break;
      }
      return 4;
    case GR_SUPPORTS_PASSTHRU:
      *params = (config.model == GlideWrapper::CardModel::VoodooGraphics ||
                 config.model == GlideWrapper::CardModel::Voodoo2)
                    ? FXTRUE
                    : FXFALSE;
      return 4;
    case GR_TEXTURE_ALIGN:
      *params = (config.model >= GlideWrapper::CardModel::VoodooBanshee) ? 16
                                                                         : 8;
      return 4;
    case GR_VIEWPORT:
      if (plength < 16) return 0;
      params[0] = static_cast<FxI32>(s_viewportX);
      params[1] = static_cast<FxI32>(s_viewportY);
      params[2] = static_cast<FxI32>(s_viewportWidth);
      params[3] = static_cast<FxI32>(s_viewportHeight);
      return 16;
    case GR_WDEPTH_MIN_MAX:
      if (plength < 8) return 0;
      params[0] = 0;
      params[1] = 65535;
      return 8;
    case GR_ZDEPTH_MIN_MAX:
      if (plength < 8) return 0;
      params[0] = 65535;
      params[1] = 0;
      return 8;
    case GR_STATS_POINTS:
      *params = static_cast<FxI32>(s_pointsDrawn);
      return 4;
    case GR_STATS_LINES:
      *params = static_cast<FxI32>(s_linesDrawn);
      return 4;
    case GR_STATS_TRIANGLES_IN:
    case GR_STATS_TRIANGLES_OUT: {
      uint32_t proc = 0;
      uint32_t drawn = 0;
      EntryCommon::GetTriStats(&proc, &drawn);
      *params = static_cast<FxI32>(
          (pname == GR_STATS_TRIANGLES_IN) ? proc : drawn);
      return 4;
    }
    case GR_STATS_PIXELS_IN:
    case GR_STATS_PIXELS_CHROMA_FAIL:
    case GR_STATS_PIXELS_OUT:
    case GR_STATS_PIXELS_AFUNC_FAIL:
    case GR_STATS_PIXELS_DEPTHFUNC_FAIL: {
      uint32_t pixelsIn = 0;
      uint32_t chromaFail = 0;
      EntryCommon::GetPerfStats(pixelsIn, chromaFail);
      if (pname == GR_STATS_PIXELS_IN) {
        *params = static_cast<FxI32>(pixelsIn);
      } else if (pname == GR_STATS_PIXELS_CHROMA_FAIL) {
        *params = static_cast<FxI32>(chromaFail);
      } else if (pname == GR_STATS_PIXELS_OUT) {
        *params = static_cast<FxI32>(pixelsIn >= chromaFail
                                         ? (pixelsIn - chromaFail)
                                         : 0u);
      } else {
        *params = 0;
      }
      return 4;
    }
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grGet (Glide 3.x) unknown or unsupported pname=" << pname);
      return 0;
  }
}

namespace {
struct ResolutionCatalogEntry {
  GrScreenResolution_t resEnum;
  uint32_t width;
  uint32_t height;
  GlideWrapper::CardModel minCard;
};

// Authentic 3dfx resolution tiers across SST-1/Rush, CVG, H3, and VSA-100.
static constexpr ResolutionCatalogEntry kResolutionCatalog[] = {
    {GR_RESOLUTION_320x200,   320,  200,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_320x240,   320,  240,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_400x256,   400,  256,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_400x300,   400,  300,  GlideWrapper::CardModel::Voodoo2},
    {GR_RESOLUTION_512x256,   512,  256,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_512x384,   512,  384,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_640x200,   640,  200,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_640x350,   640,  350,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_640x400,   640,  400,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_640x480,   640,  480,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_800x600,   800,  600,  GlideWrapper::CardModel::VoodooGraphics},
    {GR_RESOLUTION_856x480,   856,  480,  GlideWrapper::CardModel::Voodoo2},
    {GR_RESOLUTION_960x720,   960,  720,  GlideWrapper::CardModel::Voodoo2},
    {GR_RESOLUTION_1024x768,  1024, 768,  GlideWrapper::CardModel::Voodoo2},
    {GR_RESOLUTION_1152x864,  1152, 864,  GlideWrapper::CardModel::VoodooBanshee},
    {GR_RESOLUTION_1280x960,  1280, 960,  GlideWrapper::CardModel::VoodooBanshee},
    {GR_RESOLUTION_1280x1024, 1280, 1024, GlideWrapper::CardModel::VoodooBanshee},
    {GR_RESOLUTION_1600x1200, 1600, 1200, GlideWrapper::CardModel::VoodooBanshee},
    {GR_RESOLUTION_1600x1024, 1600, 1024, GlideWrapper::CardModel::Voodoo4},
    {GR_RESOLUTION_1792x1344, 1792, 1344, GlideWrapper::CardModel::Voodoo4},
    {GR_RESOLUTION_1856x1392, 1856, 1392, GlideWrapper::CardModel::Voodoo4},
    {GR_RESOLUTION_1920x1440, 1920, 1440, GlideWrapper::CardModel::Voodoo4},
    {GR_RESOLUTION_2048x1536, 2048, 1536, GlideWrapper::CardModel::Voodoo4},
};
}  // namespace

FX_ENTRY FxI32 FX_CALL grQueryResolutions(const GrResolution* resTemplate,
                                          GrResolution* output) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grQueryResolutions (Glide 3.x) invoked.");

  const auto& config =
      GlideWrapper::EmulationRegistry::GetInstance().GetConfig();

  // Authentic constrained-template validation (matches 3dfx h5/diget.c)
  GrScreenRefresh_t targetRefresh = GR_REFRESH_60Hz;
  int targetColBufs = 2;
  int targetAuxBufs = 1;
  bool filterRes = false;
  GrScreenResolution_t targetRes = GR_RESOLUTION_640x480;

  if (resTemplate) {
    if (resTemplate->resolution !=
        static_cast<GrScreenResolution_t>(GR_QUERY_ANY)) {
      if (resTemplate->resolution < GR_RESOLUTION_MIN ||
          resTemplate->resolution > GR_RESOLUTION_MAX) {
        return 0;
      }
      filterRes = true;
      targetRes = resTemplate->resolution;
    }
    if (resTemplate->refresh != static_cast<GrScreenRefresh_t>(GR_QUERY_ANY)) {
      if (resTemplate->refresh != GR_REFRESH_NONE &&
          (resTemplate->refresh < GR_REFRESH_60Hz ||
           resTemplate->refresh > GR_REFRESH_120Hz)) {
        return 0;
      }
      targetRefresh = (resTemplate->refresh == GR_REFRESH_NONE)
                          ? GR_REFRESH_60Hz
                          : resTemplate->refresh;
    }
    if (resTemplate->numColorBuffers != GR_QUERY_ANY) {
      if (resTemplate->numColorBuffers < 2 ||
          resTemplate->numColorBuffers > 3) {
        return 0;
      }
      targetColBufs = resTemplate->numColorBuffers;
    }
    if (resTemplate->numAuxBuffers != GR_QUERY_ANY) {
      if (resTemplate->numAuxBuffers < 0 || resTemplate->numAuxBuffers > 1) {
        return 0;
      }
      targetAuxBufs = resTemplate->numAuxBuffers;
    }
  }

  // Authentic VRAM capacity check (16-bit buffers = 2 bytes/pixel per buffer;
  // Voodoo2 SLI doubles effective framebuffer; UMA H3/H5 reserves 2 MB for TMU)
  uint64_t availFbBytes =
      static_cast<uint64_t>(std::max(1u, config.fbiMemoryMb)) * 1024ull *
      1024ull;
  if (config.model == GlideWrapper::CardModel::Voodoo2 && config.enableSli) {
    availFbBytes *= 2ull;
  } else if (config.model >= GlideWrapper::CardModel::VoodooBanshee &&
             availFbBytes > (2ull * 1024ull * 1024ull)) {
    availFbBytes -= (2ull * 1024ull * 1024ull);
  }

  FxI32 matchCount = 0;
  for (const auto& entry : kResolutionCatalog) {
    if (static_cast<int>(config.model) < static_cast<int>(entry.minCard)) {
      continue;
    }
    if (filterRes && entry.resEnum != targetRes) {
      continue;
    }
    const uint64_t reqBytes =
        static_cast<uint64_t>(entry.width) *
        static_cast<uint64_t>(entry.height) * 2ull *
        static_cast<uint64_t>(targetColBufs + targetAuxBufs);
    if (reqBytes > availFbBytes) {
      continue;
    }
    if (output) {
      output[matchCount].resolution = entry.resEnum;
      output[matchCount].refresh = targetRefresh;
      output[matchCount].numColorBuffers = targetColBufs;
      output[matchCount].numAuxBuffers = targetAuxBufs;
    }
    ++matchCount;
  }

  // Per 3dfx Glide 3.x specification (sst1/cvg/h3/h5 diget.c),
  // grQueryResolutions returns byte size = matchCount * sizeof(GrResolution).
  return matchCount * static_cast<FxI32>(sizeof(GrResolution));
}

FX_ENTRY FxBool FX_CALL grReset(FxU32 what) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grReset (Glide 3.x) invoked: what=" << what);
  switch (what) {
    case GR_STATS_POINTS:
      s_pointsDrawn = 0;
      return FXTRUE;
    case GR_STATS_LINES:
      s_linesDrawn = 0;
      return FXTRUE;
    case GR_STATS_PIXELS:
      EntryCommon::ResetPerfStats();
      return FXTRUE;
    case GR_STATS_TRIANGLES:
      EntryCommon::ResetTriStats();
      return FXTRUE;
    case GR_VERTEX_PARAMETER:
      GlideWrapper::VertexLayout::GetInstance().ResetAllParamsDisabled();
      return FXTRUE;
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grReset (Glide 3.x): unknown what=" << what);
      return FXFALSE;
  }
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grGetString (Glide 3.x) invoked for pname: " << pname);
  auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
  switch (pname) {
    case GR_VERSION:
      return GlideWrapper::FormatApiVersionString(
          reg.GetConfig().glide3ApiVersion);
    case GR_HARDWARE: {
      switch (reg.GetConfig().model) {
        case GlideWrapper::CardModel::VoodooGraphics:
          return "Voodoo Graphics";
        case GlideWrapper::CardModel::VoodooRush:
          return "Voodoo Rush";
        case GlideWrapper::CardModel::Voodoo2:
          return "Voodoo2";
        case GlideWrapper::CardModel::VoodooBanshee:
          return "Voodoo Banshee";
        case GlideWrapper::CardModel::Voodoo3:
          return "Voodoo3";
        case GlideWrapper::CardModel::Voodoo4:
          return "Voodoo4";
        case GlideWrapper::CardModel::Voodoo5:
          return "Voodoo5";
        default:
          return "Voodoo2";
      }
    }
    case GR_VENDOR:
      return reg.GetConfig().reportedVendorOverride.c_str();
    case GR_RENDERER:
      return EntryCommon::GetRendererString();
    case GR_EXTENSION: {
      switch (reg.GetConfig().model) {
        case GlideWrapper::CardModel::VoodooGraphics:
        case GlideWrapper::CardModel::VoodooRush:
          return "CHROMARANGE FOGCOORD";
        case GlideWrapper::CardModel::Voodoo2:
          return "CHROMARANGE FOGCOORD TEXCHROMA RESOLUTION SURFACE "
                 "COMMAND_TRANSPORT";
        case GlideWrapper::CardModel::VoodooBanshee:
        case GlideWrapper::CardModel::Voodoo3:
          return "CHROMARANGE FOGCOORD GETGAMMA TEXCHROMA TEXMIRROR "
                 "PALETTE6666 RESOLUTION SURFACE COMMAND_TRANSPORT "
                 "TEXTUREBUFFER TEXUMA";
        case GlideWrapper::CardModel::Voodoo4:
        case GlideWrapper::CardModel::Voodoo5:
          return "CHROMARANGE FOGCOORD GETGAMMA TEXCHROMA TEXMIRROR "
                 "PALETTE6666 RESOLUTION SURFACE COMMAND_TRANSPORT PIXEXT "
                 "COMBINE TEXFMT TEXTUREBUFFER TEXUMA";
        default:
          return "CHROMARANGE FOGCOORD";
      }
    }
    default:
      return "Unknown";
  }
}

FX_ENTRY void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx,
                                   FxU32 maxy) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grClipWindow (Glide 3.x) invoked: "
                << minx << "," << miny << " to " << maxx << "," << maxy);
  EntryCommon::SetClipWindow(s_state, minx, miny, maxx, maxy);
}

FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu) {
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMinAddress (Glide 3.x) invoked for TMU=" << tmu);
  return 0;
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu) {
  EnsureEngineInitialized();
  const auto& config = GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  if (tmu < 0 || static_cast<uint32_t>(tmu) >= config.tmuCount) {
    GLIDE_LOG(DEBUG, "Frontend",
              "grTexMaxAddress (Glide 3.x) invoked for unavailable TMU="
                  << tmu << " -> 0");
    return 0;
  }
  uint32_t tmuBytes = std::max(1u, config.tmuMemoryMb) * 1024u * 1024u;
  if (GlideWrapper::TextureManager::GetInstance().IsUmaMode() &&
      config.tmuCount >= 2) {
    tmuBytes *= config.tmuCount;
  }
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMaxAddress (Glide 3.x) invoked for TMU="
                << tmu << " -> 0x" << std::hex << tmuBytes << std::dec);
  return tmuBytes;
}

FX_ENTRY FxU32 FX_CALL grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax,
                                            GrAspectRatio_t aspect,
                                            GrTextureFormat_t fmt) {
  GLIDE_LOG(DEBUG, "Frontend", "grTexCalcMemRequired (Glide 3.x) invoked.");
  if (g_inSplash) {
    // Internally loaded Glide 2.x splash texture: already has Glide 2.x
    // canonical values!
    return GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(
        lodmin, lodmax, aspect, fmt);
  }
  return GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(
      8 - lodmin, 8 - lodmax, 3 - aspect, fmt);
}

FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo* info) {
  GLIDE_LOG(DEBUG, "Frontend", "grTexTextureMemRequired (Glide 3.x) invoked");
  if (!info) return 0;
  return grTexCalcMemRequired(info->smallLodLog2, info->largeLodLog2,
                              info->aspectRatioLog2, info->format);
}

FX_ENTRY void FX_CALL grTexDownloadTable(GrTexTable_t type, void* data) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDownloadTable (Glide 3.x) invoked for Type=" << type);
  if (data) {
    GlideWrapper::TextureManager::GetInstance().DownloadTable(GR_TMU0, type,
                                                              data);
  }
}

FX_ENTRY void FX_CALL grTexDownloadTablePartial(GrTexTable_t type, void* data,
                                                int start, int end) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDownloadTablePartial (Glide 3.x) invoked for Type="
                << type << ", range " << start << ".." << end);
  if (data) {
    GlideWrapper::TextureManager::GetInstance().DownloadTable(GR_TMU0, type,
                                                              data);
  }
}

FX_ENTRY void FX_CALL grTexDetailControl(GrChipID_t tmu, int lod_bias,
                                         FxU8 detail_scale, float detail_max) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDetailControl (Glide 3.x): tmu="
                << tmu << ", lod_bias=" << lod_bias
                << ", detail_scale=" << static_cast<int>(detail_scale)
                << ", detail_max=" << detail_max);
  EntryCommon::SetTexDetailControl(s_state, tmu, lod_bias, detail_scale,
                                   detail_max);
}

FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t tmu, float bias) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexLodBiasValue (Glide 3.x): tmu=" << tmu << ", bias=" << bias);
  EntryCommon::SetTexLodBias(s_state, tmu, bias);
}

FX_ENTRY void FX_CALL grGlideGetState(void* state) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grGlideGetState (Glide 3.x) invoked.");
  if (!state) return;
  auto* ws = reinterpret_cast<GlideWrapperState*>(state);
  std::memset(ws, 0, sizeof(GlideWrapperState));
  ws->depthMode = s_state.depthMode;
  ws->depthCompare = s_state.depthCompare;
  ws->depthMask = s_state.depthMask ? 1 : 0;
  ws->depthBias = s_state.depthBias;
  ws->alphaTestOp = s_state.alphaTestOp;
  ws->alphaTestRef = s_state.alphaTestRef;
  std::memcpy(ws->clampS, s_state.clampS, sizeof(s_state.clampS));
  std::memcpy(ws->clampT, s_state.clampT, sizeof(s_state.clampT));
  std::memcpy(ws->minFilter, s_state.minFilter, sizeof(s_state.minFilter));
  std::memcpy(ws->magFilter, s_state.magFilter, sizeof(s_state.magFilter));
  ws->colorFunc = s_state.colorFunc;
  ws->colorFactor = s_state.colorFactor;
  ws->colorLocal = s_state.colorLocal;
  ws->colorOther = s_state.colorOther;
  ws->colorInvert = s_state.colorInvert ? 1 : 0;
  ws->alphaFunc = s_state.alphaFunc;
  ws->alphaFactor = s_state.alphaFactor;
  ws->alphaLocal = s_state.alphaLocal;
  ws->alphaOther = s_state.alphaOther;
  ws->alphaInvert = s_state.alphaInvert ? 1 : 0;
  std::memcpy(ws->texRgbFunc, s_state.texRgbFunc, sizeof(s_state.texRgbFunc));
  std::memcpy(ws->texRgbFactor, s_state.texRgbFactor,
              sizeof(s_state.texRgbFactor));
  std::memcpy(ws->texAlphaFunc, s_state.texAlphaFunc,
              sizeof(s_state.texAlphaFunc));
  std::memcpy(ws->texAlphaFactor, s_state.texAlphaFactor,
              sizeof(s_state.texAlphaFactor));
  std::memcpy(ws->boundTex, s_state.boundTex, sizeof(s_state.boundTex));
  ws->coordSpace = s_coordSpace;
  ws->viewportX = s_viewportX;
  ws->viewportY = s_viewportY;
  ws->viewportWidth = s_viewportWidth;
  ws->viewportHeight = s_viewportHeight;
  ws->depthRangeNear = s_depthRangeNear;
  ws->depthRangeFar = s_depthRangeFar;
  ws->aaOrdered = s_aaOrdered ? 1 : 0;
  ws->shamelessPlug = s_shamelessPlug ? 1 : 0;
  ws->lfbConstantAlpha = s_state.lfbConstantAlpha;
  ws->lfbWriteColorFormat = s_state.lfbWriteColorFormat;
  ws->chromakeyMode = s_state.chromakeyMode;
  ws->chromakeyValue = s_state.chromakeyValue;
  ws->chromakeyRangeMin = s_state.chromakeyRangeMin;
  ws->chromakeyRangeMax = s_state.chromakeyRangeMax;
  ws->chromakeyRangeMode = s_state.chromakeyRangeMode;
  for (int i = 0; i < 2; ++i) {
    ws->texChromaState[i].mode = s_state.texChromaState[i].mode;
    ws->texChromaState[i].minColor = s_state.texChromaState[i].minColor;
    ws->texChromaState[i].maxColor = s_state.texChromaState[i].maxColor;
  }
  ws->rgbSrcBlend = s_state.rgbSrcBlend;
  ws->rgbDstBlend = s_state.rgbDstBlend;
  ws->alphaSrcBlend = s_state.alphaSrcBlend;
  ws->alphaDstBlend = s_state.alphaDstBlend;
  ws->fogMode = s_state.fogMode;
  ws->fogColor = s_state.fogColor;
  std::memcpy(ws->fogTable, s_state.fogTable, sizeof(s_state.fogTable));
  ws->cullMode = s_state.cullMode;
  ws->constantColor = s_state.constantColor;
  ws->sstOrigin = s_sstOrigin;
  std::memcpy(ws->texLodBias, s_state.texLodBias, sizeof(s_state.texLodBias));
  std::memcpy(ws->mipmapMode, s_state.mipmapMode, sizeof(s_state.mipmapMode));
  for (int i = 0; i < 2; ++i) {
    ws->lodBlend[i] = s_state.lodBlend[i] ? 1 : 0;
  }
  ws->clipMinX = s_state.clipMinX;
  ws->clipMinY = s_state.clipMinY;
  ws->clipMaxX = s_state.clipMaxX;
  ws->clipMaxY = s_state.clipMaxY;
  ws->colorMaskRgb = s_state.colorMaskRgb ? 1 : 0;
  ws->colorMaskAlpha = s_state.colorMaskAlpha ? 1 : 0;
  for (int i = 0; i < 2; ++i) {
    ws->texRgbInvert[i] = s_state.texRgbInvert[i] ? 1 : 0;
    ws->texAlphaInvert[i] = s_state.texAlphaInvert[i] ? 1 : 0;
  }
  ws->ditherMode = s_state.ditherMode;
  ws->stippleMode = s_stippleMode;
  ws->stipplePattern = s_stipplePattern;
  ws->hasTbufferState = 1u;
  ws->aaMultiSample = s_aaMultiSample ? 1 : 0;
  ws->tbufferWriteMask = s_tbufferWriteMask;
  ws->hasColorMaskExt = 1u;
  ws->colorMaskR = s_state.colorMaskR ? 1u : 0u;
  ws->colorMaskG = s_state.colorMaskG ? 1u : 0u;
  ws->colorMaskB = s_state.colorMaskB ? 1u : 0u;
  for (int i = 0; i < 2; ++i) {
    ws->texDetailBias[i] = s_state.texDetailBias[i];
    ws->texDetailScale[i] = s_state.texDetailScale[i];
    ws->texDetailMax[i] = s_state.texDetailMax[i];
  }
  ws->hasStencilState = 1u;
  ws->stencilEnabled = s_state.stencilEnabled ? 1u : 0u;
  ws->stencilRef = s_state.stencilRef;
  ws->stencilReadMask = s_state.stencilReadMask;
  ws->stencilWriteMask = s_state.stencilWriteMask;
  ws->lfbConstantStencil = s_state.lfbConstantStencil;
  ws->stencilCompareOp = s_state.stencilCompareOp;
  ws->stencilFailOp = s_state.stencilFailOp;
  ws->stencilZFailOp = s_state.stencilZFailOp;
  ws->stencilZPassOp = s_state.stencilZPassOp;
  ws->hasCombineExtState = 1u;
  ws->rgbBlendOp = s_state.rgbBlendOp;
  ws->alphaBlendOp = s_state.alphaBlendOp;
  ws->extColorCombine = s_state.extColorCombine;
  ws->extAlphaCombine = s_state.extAlphaCombine;
  std::memcpy(ws->extTexColorCombine, s_state.extTexColorCombine,
              sizeof(s_state.extTexColorCombine));
  std::memcpy(ws->extTexAlphaCombine, s_state.extTexAlphaCombine,
              sizeof(s_state.extTexAlphaCombine));
  std::memcpy(ws->tmuConstantColor, s_state.tmuConstantColor,
              sizeof(s_state.tmuConstantColor));
}

FX_ENTRY void FX_CALL grGlideSetState(const void* state) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grGlideSetState (Glide 3.x) invoked.");
  if (!state) return;
  auto* ws = reinterpret_cast<const GlideWrapperState*>(state);

  // VULN-01: Sanitize all restored state enums and boundaries to prevent
  // out-of-range memory corruption
  s_state.depthMode = (ws->depthMode > 3) ? 0 : ws->depthMode;
  s_state.depthCompare =
      (ws->depthCompare > 7) ? 1 : ws->depthCompare;  // GR_CMP_LESS
  s_state.depthMask = ws->depthMask != 0;
  s_state.depthBias = ws->depthBias;
  s_state.alphaTestOp =
      (ws->alphaTestOp > 7) ? 7 : ws->alphaTestOp;  // GR_CMP_ALWAYS
  s_state.alphaTestRef = ws->alphaTestRef;
  s_state.ditherMode = (ws->ditherMode > 2) ? 0 : ws->ditherMode;
  s_stippleMode = (ws->stippleMode > 2) ? 0 : ws->stippleMode;
  s_stipplePattern = ws->stipplePattern;

  for (int i = 0; i < 2; ++i) {
    s_state.clampS[i] = (ws->clampS[i] > 2) ? 0 : ws->clampS[i];
    s_state.clampT[i] = (ws->clampT[i] > 2) ? 0 : ws->clampT[i];
  }
  std::memcpy(s_state.minFilter, ws->minFilter, sizeof(s_state.minFilter));
  std::memcpy(s_state.magFilter, ws->magFilter, sizeof(s_state.magFilter));

  s_state.colorFunc = ws->colorFunc;
  s_state.colorFactor = ws->colorFactor;
  s_state.colorLocal = ws->colorLocal;
  s_state.colorOther = ws->colorOther;
  s_state.colorInvert = ws->colorInvert != 0;
  s_state.alphaFunc = ws->alphaFunc;
  s_state.alphaFactor = ws->alphaFactor;
  s_state.alphaLocal = ws->alphaLocal;
  s_state.alphaOther = ws->alphaOther;
  s_state.alphaInvert = ws->alphaInvert != 0;
  std::memcpy(s_state.texRgbFunc, ws->texRgbFunc, sizeof(s_state.texRgbFunc));
  std::memcpy(s_state.texRgbFactor, ws->texRgbFactor,
              sizeof(s_state.texRgbFactor));
  std::memcpy(s_state.texAlphaFunc, ws->texAlphaFunc,
              sizeof(s_state.texAlphaFunc));
  std::memcpy(s_state.texAlphaFactor, ws->texAlphaFactor,
              sizeof(s_state.texAlphaFactor));
  std::memcpy(s_state.boundTex, ws->boundTex, sizeof(s_state.boundTex));

  s_coordSpace = ws->coordSpace;
  s_viewportX = ws->viewportX;
  s_viewportY = ws->viewportY;
  s_viewportWidth = ws->viewportWidth;
  s_viewportHeight = ws->viewportHeight;
  s_depthRangeNear = ws->depthRangeNear;
  s_depthRangeFar = ws->depthRangeFar;
  s_aaOrdered = ws->aaOrdered != 0;
  s_shamelessPlug = ws->shamelessPlug != 0;
  s_state.lfbConstantAlpha = ws->lfbConstantAlpha;
  s_state.lfbWriteColorFormat = ws->lfbWriteColorFormat;
  s_state.chromakeyMode = ws->chromakeyMode;
  s_state.chromakeyValue = ws->chromakeyValue;
  s_state.chromakeyRangeMin = ws->chromakeyRangeMin;
  s_state.chromakeyRangeMax = ws->chromakeyRangeMax;
  s_state.chromakeyRangeMode = ws->chromakeyRangeMode;
  for (int i = 0; i < 2; ++i) {
    s_state.texChromaState[i].mode = ws->texChromaState[i].mode;
    s_state.texChromaState[i].minColor = ws->texChromaState[i].minColor;
    s_state.texChromaState[i].maxColor = ws->texChromaState[i].maxColor;
  }

  s_state.rgbSrcBlend =
      (ws->rgbSrcBlend > 15) ? 4 : ws->rgbSrcBlend;  // GR_BLEND_ONE
  s_state.rgbDstBlend =
      (ws->rgbDstBlend > 15) ? 0 : ws->rgbDstBlend;  // GR_BLEND_ZERO
  s_state.alphaSrcBlend = (ws->alphaSrcBlend > 15) ? 4 : ws->alphaSrcBlend;
  s_state.alphaDstBlend = (ws->alphaDstBlend > 15) ? 0 : ws->alphaDstBlend;

  s_state.fogMode = ws->fogMode;
  s_state.fogColor = ws->fogColor;
  std::memcpy(s_state.fogTable, ws->fogTable, sizeof(s_state.fogTable));
  s_state.cullMode =
      (ws->cullMode > 2) ? 0 : ws->cullMode;  // GR_CULL_DISABLE
  s_state.constantColor = ws->constantColor;
  s_sstOrigin = (ws->sstOrigin > 1) ? 0 : ws->sstOrigin;
  std::memcpy(s_state.texLodBias, ws->texLodBias, sizeof(s_state.texLodBias));
  std::memcpy(s_state.mipmapMode, ws->mipmapMode, sizeof(s_state.mipmapMode));
  for (int i = 0; i < 2; ++i) {
    s_state.lodBlend[i] = ws->lodBlend[i] != 0;
  }

  // Get active window size to clamp clip boundaries safely
  uint32_t winWidth = 640;
  uint32_t winHeight = 480;
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    winWidth = b->GetGuestWidth();
    winHeight = b->GetGuestHeight();
  }
  s_state.clipMinX = std::max(0u, std::min(ws->clipMinX, winWidth));
  s_state.clipMinY = std::max(0u, std::min(ws->clipMinY, winHeight));
  s_state.clipMaxX = std::max(s_state.clipMinX, std::min(ws->clipMaxX, winWidth));
  s_state.clipMaxY =
      std::max(s_state.clipMinY, std::min(ws->clipMaxY, winHeight));

  s_state.colorMaskRgb = ws->colorMaskRgb != 0;
  if (ws->hasColorMaskExt != 0) {
    s_state.colorMaskR = ws->colorMaskR != 0;
    s_state.colorMaskG = ws->colorMaskG != 0;
    s_state.colorMaskB = ws->colorMaskB != 0;
    s_state.colorMaskRgb =
        (s_state.colorMaskR || s_state.colorMaskG || s_state.colorMaskB);
    for (int i = 0; i < 2; ++i) {
      s_state.texDetailBias[i] = std::clamp(ws->texDetailBias[i], -32, 31);
      s_state.texDetailScale[i] = std::min<uint8_t>(ws->texDetailScale[i], 7u);
      s_state.texDetailMax[i] = std::clamp(ws->texDetailMax[i], 0.0f, 1.0f);
    }
  } else {
    s_state.colorMaskR = s_state.colorMaskRgb;
    s_state.colorMaskG = s_state.colorMaskRgb;
    s_state.colorMaskB = s_state.colorMaskRgb;
  }
  s_state.colorMaskAlpha = ws->colorMaskAlpha != 0;
  for (int i = 0; i < 2; ++i) {
    s_state.texRgbInvert[i] = ws->texRgbInvert[i] != 0;
    s_state.texAlphaInvert[i] = ws->texAlphaInvert[i] != 0;
  }
  if (ws->hasTbufferState != 0) {
    s_aaMultiSample = ws->aaMultiSample != 0;
    s_tbufferWriteMask = ws->tbufferWriteMask;
  }
  if (ws->hasStencilState != 0) {
    s_state.stencilEnabled = ws->stencilEnabled != 0;
    s_state.stencilRef = ws->stencilRef;
    s_state.stencilReadMask = ws->stencilReadMask;
    s_state.stencilWriteMask = ws->stencilWriteMask;
    s_state.lfbConstantStencil = ws->lfbConstantStencil;
    s_state.stencilCompareOp =
        (ws->stencilCompareOp > 7u) ? 7u : ws->stencilCompareOp;
    s_state.stencilFailOp = (ws->stencilFailOp > 7u) ? 0u : ws->stencilFailOp;
    s_state.stencilZFailOp =
        (ws->stencilZFailOp > 7u) ? 0u : ws->stencilZFailOp;
    s_state.stencilZPassOp =
        (ws->stencilZPassOp > 7u) ? 0u : ws->stencilZPassOp;
  }
  if (ws->hasCombineExtState != 0) {
    s_state.rgbBlendOp = (ws->rgbBlendOp > 2u) ? 0u : ws->rgbBlendOp;
    s_state.alphaBlendOp = (ws->alphaBlendOp > 2u) ? 0u : ws->alphaBlendOp;
    s_state.extColorCombine = ws->extColorCombine;
    s_state.extAlphaCombine = ws->extAlphaCombine;
    std::memcpy(s_state.extTexColorCombine, ws->extTexColorCombine,
                sizeof(s_state.extTexColorCombine));
    std::memcpy(s_state.extTexAlphaCombine, ws->extTexAlphaCombine,
                sizeof(s_state.extTexAlphaCombine));
    std::memcpy(s_state.tmuConstantColor, ws->tmuConstantColor,
                sizeof(s_state.tmuConstantColor));
  } else {
    s_state.rgbBlendOp = 0u;
    s_state.alphaBlendOp = 0u;
    s_state.extColorCombine = 0u;
    s_state.extAlphaCombine = 0u;
    s_state.extTexColorCombine[0] = s_state.extTexColorCombine[1] = 0u;
    s_state.extTexAlphaCombine[0] = s_state.extTexAlphaCombine[1] = 0u;
  }

  // Apply states to backend
  EntryCommon::ApplySharedStateToBackend(s_state, s_sstOrigin,
                                         /*includeTexInvert=*/true);
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetAAState(s_aaOrdered);
    b->SetDepthRange(s_depthRangeNear, s_depthRangeFar);
    b->SetStippleState(s_stippleMode, s_stipplePattern);
    if (ws->hasTbufferState != 0) {
      b->SetAAMultiSample(s_aaMultiSample);
      b->SetTBufferWriteMask(s_tbufferWriteMask);
    }
  }
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t fnc,
                                     GrCombineFactor_t factor,
                                     GrCombineLocal_t local,
                                     GrCombineOther_t other, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grColorCombine (Glide 3.x) invoked: Function="
                << fnc << ", Factor=" << factor << ", Local=" << local
                << ", Other=" << other << ", Invert=" << invert);
  EntryCommon::SetColorCombine(s_state, fnc, factor, local, other, invert != 0);
}

FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t fnc,
                                     GrCombineFactor_t factor,
                                     GrCombineLocal_t local,
                                     GrCombineOther_t other, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grAlphaCombine (Glide 3.x) invoked: Function="
                << fnc << ", Factor=" << factor << ", Local=" << local
                << ", Other=" << other << ", Invert=" << invert);
  EntryCommon::SetAlphaCombine(s_state, fnc, factor, local, other, invert != 0);
}

FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgbFnc,
                                   GrCombineFactor_t rgbFactor,
                                   GrCombineFunction_t alphaFnc,
                                   GrCombineFactor_t alphaFactor,
                                   FxBool rgbInvert, FxBool alphaInvert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexCombine (Glide 3.x) invoked for TMU=" << tmu);
  EntryCommon::SetTexCombine(s_state, tmu, rgbFnc, rgbFactor, alphaFnc,
                             alphaFactor, rgbInvert != 0, alphaInvert != 0,
                             /*forwardInverts=*/true);
}

FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgbSf,
                                           GrAlphaBlendFnc_t rgbDf,
                                           GrAlphaBlendFnc_t alphaSf,
                                           GrAlphaBlendFnc_t alphaDf) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grAlphaBlendFunction (Glide 3.x) invoked.");
  EntryCommon::SetAlphaBlendFunction(s_state, rgbSf, rgbDf, alphaSf, alphaDf);
}

FX_ENTRY void FX_CALL grColorCombineExt(GrCCUColor_t a, GrCombineMode_t a_mode,
                                        GrCCUColor_t b, GrCombineMode_t b_mode,
                                        GrCCUColor_t c, FxBool c_invert,
                                        GrCCUColor_t d, FxBool d_invert,
                                        FxU32 shift, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grColorCombineExt (Glide 3.x): a=" << a << " b=" << b << " c=" << c
                                                << " d=" << d
                                                << " shift=" << shift);
  EntryCommon::SetColorCombineExt(s_state, a, a_mode, b, b_mode, c,
                                  c_invert != 0, d, d_invert != 0, shift,
                                  invert != 0);
}

FX_ENTRY void FX_CALL grAlphaCombineExt(GrACUColor_t a, GrCombineMode_t a_mode,
                                        GrACUColor_t b, GrCombineMode_t b_mode,
                                        GrACUColor_t c, FxBool c_invert,
                                        GrACUColor_t d, FxBool d_invert,
                                        FxU32 shift, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grAlphaCombineExt (Glide 3.x): a=" << a << " b=" << b << " c=" << c
                                                << " d=" << d
                                                << " shift=" << shift);
  EntryCommon::SetAlphaCombineExt(s_state, a, a_mode, b, b_mode, c,
                                  c_invert != 0, d, d_invert != 0, shift,
                                  invert != 0);
}

FX_ENTRY void FX_CALL grTexColorCombineExt(
    GrChipID_t tmu, GrTCCUColor_t a, GrCombineMode_t a_mode, GrTCCUColor_t b,
    GrCombineMode_t b_mode, GrTCCUColor_t c, FxBool c_invert, GrTCCUColor_t d,
    FxBool d_invert, FxU32 shift, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexColorCombineExt (Glide 3.x): tmu="
                << tmu << " a=" << a << " b=" << b << " c=" << c << " d=" << d);
  EntryCommon::SetTexColorCombineExt(s_state, tmu, a, a_mode, b, b_mode, c,
                                     c_invert != 0, d, d_invert != 0, shift,
                                     invert != 0);
}

FX_ENTRY void FX_CALL grTexAlphaCombineExt(
    GrChipID_t tmu, GrTACUColor_t a, GrCombineMode_t a_mode, GrTACUColor_t b,
    GrCombineMode_t b_mode, GrTACUColor_t c, FxBool c_invert, GrTACUColor_t d,
    FxBool d_invert, FxU32 shift, FxBool invert) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexAlphaCombineExt (Glide 3.x): tmu="
                << tmu << " a=" << a << " b=" << b << " c=" << c << " d=" << d);
  EntryCommon::SetTexAlphaCombineExt(s_state, tmu, a, a_mode, b, b_mode, c,
                                     c_invert != 0, d, d_invert != 0, shift,
                                     invert != 0);
}

FX_ENTRY void FX_CALL grConstantColorValueExt(GrChipID_t tmu, GrColor_t value) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grConstantColorValueExt (Glide 3.x): tmu="
                << tmu << " value=0x" << std::hex << value << std::dec);
  EntryCommon::SetTmuConstantColor(s_state, tmu, value);
}

FX_ENTRY void FX_CALL grAlphaBlendFunctionExt(
    GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendOp_t rgb_op,
    GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df,
    GrAlphaBlendOp_t alpha_op) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grAlphaBlendFunctionExt (Glide 3.x): rgb=("
                << rgb_sf << "," << rgb_df << ",op=" << rgb_op << ") alpha=("
                << alpha_sf << "," << alpha_df << ",op=" << alpha_op << ")");
  EntryCommon::SetAlphaBlendFunctionExt(s_state, rgb_sf, rgb_df, rgb_op,
                                        alpha_sf, alpha_df, alpha_op);
}

FX_ENTRY void FX_CALL grTextureBufferExt(
    GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLOD, GrLOD_t largeLOD,
    GrAspectRatio_t aspectRatio, GrTextureFormat_t format,
    FxU32 odd_even_mask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  (void)odd_even_mask;

  // Switching texture-buffer targets mid-mode (e.g. SDK test41.c) must commit
  // the previous target before retargeting.
  if (s_texBuf.active && s_texBuf.configured) {
    CommitActiveTextureBuffer();
  }

  const int32_t g2ThisLod = 8 - static_cast<int32_t>(thisLOD);
  const int32_t g2LargeLod = 8 - static_cast<int32_t>(largeLOD);
  const uint32_t g2Aspect =
      static_cast<uint32_t>(3 - static_cast<int32_t>(aspectRatio));
  const uint32_t maxDim = (g2ThisLod >= 0)
                              ? (256u >> std::min(g2ThisLod, 8))
                              : (256u << std::min(-g2ThisLod, 3));
  uint32_t texW = maxDim;
  uint32_t texH = maxDim;
  switch (g2Aspect) {
    case 0: texH = std::max(1u, texH >> 3); break;
    case 1: texH = std::max(1u, texH >> 2); break;
    case 2: texH = std::max(1u, texH >> 1); break;
    case 3: break;
    case 4: texW = std::max(1u, texW >> 1); break;
    case 5: texW = std::max(1u, texW >> 2); break;
    case 6: texW = std::max(1u, texW >> 3); break;
    default: break;
  }

  s_texBuf.configured = true;
  s_texBuf.tmu = static_cast<uint32_t>(tmu);
  s_texBuf.startAddress = static_cast<uint32_t>(startAddress);
  s_texBuf.thisLodG2 = g2ThisLod;
  s_texBuf.largeLodG2 = g2LargeLod;
  s_texBuf.aspectG2 = g2Aspect;
  s_texBuf.format = static_cast<uint32_t>(format);
  s_texBuf.width = texW;
  s_texBuf.height = texH;

  GLIDE_LOG(DEBUG, "Frontend",
            "grTextureBufferExt (Glide 3.x): tmu="
                << tmu << ", addr=0x" << std::hex << startAddress << std::dec
                << ", size=" << texW << "x" << texH << ", fmt=" << format);

  if (s_texBuf.active) {
    ApplyTexBufClipWindow();
  }
}

FX_ENTRY void FX_CALL grTextureAuxBufferExt(
    GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLOD, GrLOD_t largeLOD,
    GrAspectRatio_t aspectRatio, GrTextureFormat_t format,
    FxU32 odd_even_mask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  (void)thisLOD;
  (void)largeLOD;
  (void)aspectRatio;
  (void)format;
  (void)odd_even_mask;
  s_texBuf.auxTmu = static_cast<uint32_t>(tmu);
  s_texBuf.auxAddress = static_cast<uint32_t>(startAddress);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTextureAuxBufferExt (Glide 3.x): tmu="
                << tmu << ", addr=0x" << std::hex << startAddress << std::dec);
}

FX_ENTRY void FX_CALL grAuxBufferExt(GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  s_texBuf.activeAuxBuffer = static_cast<uint32_t>(buffer);
  GLIDE_LOG(DEBUG, "Frontend",
            "grAuxBufferExt (Glide 3.x): buffer=" << buffer);
}

FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t fnc) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grAlphaTestFunction (Glide 3.x) invoked.");
  EntryCommon::SetAlphaTestFunction(s_state, fnc);
}

FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t refVal) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grAlphaTestReferenceValue (Glide 3.x) invoked: "
                << static_cast<int>(refVal));
  EntryCommon::SetAlphaTestReferenceValue(s_state, refVal);
}

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu,
                                      GrTextureFilterMode_t minfilter,
                                      GrTextureFilterMode_t magfilter) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexFilterMode (Glide 3.x) invoked for TMU=" << tmu);
  EntryCommon::SetTexFilterMode(s_state, tmu, minfilter, magfilter);
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu,
                                     GrTextureClampMode_t s_clampmode,
                                     GrTextureClampMode_t t_clampmode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexClampMode (Glide 3.x) invoked for TMU=" << tmu);
  EntryCommon::SetTexClampMode(s_state, tmu, s_clampmode, t_clampmode);
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode,
                                      FxBool lodBlend) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMipMapMode (Glide 3.x) invoked for TMU="
                << tmu << ", mode=" << mode << ", lodBlend=" << lodBlend);
  EntryCommon::SetTexMipMapMode(s_state, tmu, mode, lodBlend != 0);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grDepthBufferMode (Glide 3.x) invoked: " << mode);
  EntryCommon::SetDepthMode(s_state, mode);
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t fnc) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grDepthBufferFunction (Glide 3.x) invoked.");
  EntryCommon::SetDepthFunction(s_state, fnc);
}

FX_ENTRY void FX_CALL grDepthMask(FxBool mask) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grDepthMask (Glide 3.x) invoked: " << mask);
  EntryCommon::SetDepthMask(s_state, mask != 0);
}

FX_ENTRY void FX_CALL grDepthBiasLevel(FxI32 level) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grDepthBiasLevel (Glide 3.x) invoked: " << level);
  EntryCommon::SetDepthBias(s_state, level);
}

FX_ENTRY float FX_CALL guFogTableIndexToW(int i) {
  return GlideWrapper::MathUtils::FogTableIndexToW(i);
}

FX_ENTRY void FX_CALL guFogGenerateExp(GrFog_t* fogtable, float density) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GlideWrapper::MathUtils::FogGenerateExp(fogtable, density);
}

FX_ENTRY void FX_CALL guFogGenerateExp2(GrFog_t* fogtable, float density) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GlideWrapper::MathUtils::FogGenerateExp2(fogtable, density);
}

FX_ENTRY void FX_CALL guFogGenerateLinear(GrFog_t* fogtable, float nearZ,
                                          float farZ) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GlideWrapper::MathUtils::FogGenerateLinear(fogtable, nearZ, farZ);
}

FX_ENTRY void FX_CALL grFogMode(GrFogMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grFogMode (Glide 3.x) invoked: " << mode);
  EntryCommon::SetFogMode(s_state, mode);
}

FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fogcolor) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grFogColorValue (Glide 3.x) invoked: " << std::hex << fogcolor
                                                    << std::dec);
  EntryCommon::SetFogColor(s_state, fogcolor);
}

FX_ENTRY void FX_CALL grFogTable(const GrFog_t ft[]) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grFogTable (Glide 3.x) invoked.");
  EntryCommon::SetFogTable(s_state, ft);
}

FX_ENTRY void FX_CALL grFinish(void) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  GLIDE_LOG(DEBUG, "Frontend", "grFinish (Glide 3.x) invoked.");
  EntryCommon::SstIdle();
}

FX_ENTRY void FX_CALL grFlush(void) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  GLIDE_LOG(DEBUG, "Frontend", "grFlush (Glide 3.x) invoked.");
}

FX_ENTRY void FX_CALL grChromaRangeModeExt(GrChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grChromaRangeModeExt (Glide 3.x): mode=" << mode);
  s_state.chromakeyRangeMode = mode;
  EntryCommon::SetChromakeyMode(s_state, mode);
}

FX_ENTRY void FX_CALL grChromaRangeExt(GrColor_t minColor, GrColor_t maxColor,
                                       GrChromaRangeMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grChromaRangeExt (Glide 3.x): min=0x"
                << std::hex << minColor << ", max=0x" << maxColor
                << ", mode=" << std::dec << mode);
  s_state.chromakeyValue = minColor;
  EntryCommon::SetChromakeyRange(s_state, minColor, maxColor, mode);
}

FX_ENTRY void FX_CALL grTexChromaMode(GrChipID_t tmu,
                                      GrTexChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexChromaMode (Glide 3.x): tmu=" << tmu << ", mode=" << mode);
  EntryCommon::SetTexChromakeyMode(s_state, tmu, mode);
}

FX_ENTRY void FX_CALL grTexChromaModeExt(GrChipID_t tmu,
                                         GrTexChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  grTexChromaMode(tmu, mode);
}

FX_ENTRY void FX_CALL grTexChromaRange(GrChipID_t tmu, GrColor_t minColor,
                                       GrColor_t maxColor,
                                       GrTexChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexChromaRange (Glide 3.x): tmu="
                << tmu << ", min=0x" << std::hex << minColor << ", max=0x"
                << maxColor << ", mode=" << std::dec << mode);
  EntryCommon::SetTexChromakeyRange(s_state, tmu, minColor, maxColor, mode);
}

FX_ENTRY void FX_CALL grTexChromaRangeExt(GrChipID_t tmu, GrColor_t minColor,
                                          GrColor_t maxColor,
                                          GrTexChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  grTexChromaRange(tmu, minColor, maxColor, mode);
}

FX_ENTRY GrProc FX_CALL grGetProcAddress(char* procName) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grGetProcAddress (Glide 3.x): " << (procName ? procName : "NULL"));
  if (!procName) return nullptr;

  if (!GlideWrapper::EmulationRegistry::GetInstance().IsCardFeatureAvailable(
          procName)) {
    GLIDE_LOG(WARN, "Frontend",
              "grGetProcAddress (Glide 3.x): Gating out '"
                  << procName << "' under active card profile");
    return nullptr;
  }

  auto toGrProc = [](auto fn) -> GrProc {
    // Cast through void(*)() to satisfy -Wcast-function-type under -Wpedantic.
    return reinterpret_cast<GrProc>(reinterpret_cast<void (*)()>(fn));
  };

  if (std::strcmp(procName, "grChromaRangeModeExt") == 0) {
    return toGrProc(grChromaRangeModeExt);
  }
  if (std::strcmp(procName, "grChromaRangeExt") == 0) {
    return toGrProc(grChromaRangeExt);
  }
  if (std::strcmp(procName, "grTexChromaModeExt") == 0) {
    return toGrProc(grTexChromaModeExt);
  }
  if (std::strcmp(procName, "grTexChromaRangeExt") == 0) {
    return toGrProc(grTexChromaRangeExt);
  }
  if (std::strcmp(procName, "grSstWinOpenExt") == 0) {
    return toGrProc(grSstWinOpenExt);
  }
  if (std::strcmp(procName, "grTBufferWriteMaskExt") == 0) {
    return toGrProc(grTBufferWriteMaskExt);
  }
  if (std::strcmp(procName, "grColorMaskExt") == 0) {
    return toGrProc(grColorMaskExt);
  }
  if (std::strcmp(procName, "grStencilFuncExt") == 0) {
    return toGrProc(grStencilFuncExt);
  }
  if (std::strcmp(procName, "grStencilMaskExt") == 0) {
    return toGrProc(grStencilMaskExt);
  }
  if (std::strcmp(procName, "grStencilOpExt") == 0) {
    return toGrProc(grStencilOpExt);
  }
  if (std::strcmp(procName, "grBufferClearExt") == 0) {
    return toGrProc(grBufferClearExt);
  }
  if (std::strcmp(procName, "grLfbConstantStencilExt") == 0) {
    return toGrProc(grLfbConstantStencilExt);
  }
  if (std::strcmp(procName, "grGetGammaTableExt") == 0) {
    return toGrProc(grGetGammaTableExt);
  }
  if (std::strcmp(procName, "grColorCombineExt") == 0) {
    return toGrProc(grColorCombineExt);
  }
  if (std::strcmp(procName, "grAlphaCombineExt") == 0) {
    return toGrProc(grAlphaCombineExt);
  }
  if (std::strcmp(procName, "grTexColorCombineExt") == 0) {
    return toGrProc(grTexColorCombineExt);
  }
  if (std::strcmp(procName, "grTexAlphaCombineExt") == 0) {
    return toGrProc(grTexAlphaCombineExt);
  }
  if (std::strcmp(procName, "grConstantColorValueExt") == 0) {
    return toGrProc(grConstantColorValueExt);
  }
  if (std::strcmp(procName, "grAlphaBlendFunctionExt") == 0) {
    return toGrProc(grAlphaBlendFunctionExt);
  }
  if (std::strcmp(procName, "grTextureBufferExt") == 0) {
    return toGrProc(grTextureBufferExt);
  }
  if (std::strcmp(procName, "grTextureAuxBufferExt") == 0) {
    return toGrProc(grTextureAuxBufferExt);
  }
  if (std::strcmp(procName, "grAuxBufferExt") == 0) {
    return toGrProc(grAuxBufferExt);
  }
  if (std::strcmp(procName, "grSetNumPendingBuffers") == 0) {
    return toGrProc(grSetNumPendingBuffers);
  }

  GLIDE_LOG(WARN, "Frontend",
            "grGetProcAddress (Glide 3.x): Unknown extension " << procName);
  return nullptr;
}

FX_ENTRY void FX_CALL grSstVidMode(FxU32 whichSst, void* vidTimings) {
  GLIDE_LOG(DEBUG, "Frontend",
            "grSstVidMode (Glide 3.x) stub: whichSst=" << whichSst);
}

FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grSstOrigin (Glide 3.x) invoked: " << origin);
  EntryCommon::SetSstOrigin(s_sstOrigin, origin);
}

FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grCullMode (Glide 3.x) invoked: " << mode);
  EntryCommon::SetCullMode(s_state, mode);
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress,
                                          FxU32 evenOdd, GrTexInfo* info) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDownloadMipMap (Glide 3.x) invoked for TMU=" << tmu);
  if (info && info->data) {
    if (g_inSplash) {
      // Internally loaded Glide 2.x splash texture: its GrTexInfo fields
      // contain Glide 2.x canonical values. Note that although the header
      // declaration in glide3/glide.h names them smallLodLog2, largeLodLog2,
      // aspectRatioLog2, they physically map to the exact same offsets/sizes as
      // Glide 2.x smallLod, largeLod, aspectRatio.
      GlideWrapper::TextureManager::GetInstance().DownloadMipMap(
          tmu, startAddress, info->largeLodLog2, info->smallLodLog2,
          info->aspectRatioLog2, info->format, info->data,
          /*replaceExisting=*/true);
    } else {
      // Translate Glide 3.x signed log2 aspect ratio and LOD to Glide 2.x core
      // format
      GlideWrapper::TextureManager::GetInstance().DownloadMipMap(
          tmu, startAddress, 8 - info->largeLodLog2, 8 - info->smallLodLog2,
          3 - info->aspectRatioLog2, info->format, info->data,
          /*replaceExisting=*/true);
    }
  }
}

FX_ENTRY void FX_CALL
grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                         GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                         GrTextureFormat_t format, FxU32 evenOdd, void* data) {
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDownloadMipMapLevel (Glide 3.x) invoked for TMU="
                << tmu << ", LOD=" << thisLod);
  if (data) {
    // Translate Glide 3.x signed log2 aspect ratio and LOD to Glide 2.x core
    // format
    GlideWrapper::TextureManager::GetInstance().DownloadMipMap(
        tmu, startAddress, 8 - thisLod, 8 - thisLod, 3 - aspectRatio, format,
        data, /*replaceExisting=*/false, /*baseLargeLod=*/8 - largeLod);
  }
}

FX_ENTRY FxBool FX_CALL grTexDownloadMipMapLevelPartial(
    GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
    GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
    void* data, int startRow, int endRow) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexDownloadMipMapLevelPartial (Glide 3.x) invoked for TMU="
                << tmu << ", LOD=" << thisLod << " rows=" << startRow << ".."
                << endRow);
  if (data && tmu < 2) {
    const int32_t g2ThisLod = 8 - thisLod;
    if (evenOdd != 0) {
      const FxU32 levelMask =
          (g2ThisLod & 1) ? GR_MIPMAPLEVELMASK_ODD : GR_MIPMAPLEVELMASK_EVEN;
      if ((evenOdd & levelMask) == 0) return FXTRUE;
    }
    // Authentic Glide 3.x row bounds [startRow..endRow] are inclusive;
    // TextureManager::DownloadMipMapPartial uses half-open [startRow, endRow).
    GlideWrapper::TextureManager::GetInstance().DownloadMipMapPartial(
        tmu, startAddress, g2ThisLod, 8 - largeLod, 3 - aspectRatio, format,
        data, startRow, endRow + 1);
    return FXTRUE;
  }
  return FXFALSE;
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress,
                                  FxU32 evenOdd, GrTexInfo* info) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  (void)evenOdd;
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexSource (Glide 3.x) invoked for TMU=" << tmu);
  if (s_texBuf.active && s_texBuf.configured &&
      static_cast<uint32_t>(tmu) == s_texBuf.tmu &&
      static_cast<uint32_t>(startAddress) == s_texBuf.startAddress) {
    CommitActiveTextureBuffer();
  }
  uint32_t bindAddr = startAddress;
  auto& tm = GlideWrapper::TextureManager::GetInstance();
  if (info && tm.IsMultibaseEnabled(static_cast<uint32_t>(tmu))) {
    const int32_t g2Large =
        g_inSplash ? info->largeLodLog2 : (8 - info->largeLodLog2);
    const int32_t g2Small =
        g_inSplash ? info->smallLodLog2 : (8 - info->smallLodLog2);
    const uint32_t g2Aspect =
        g_inSplash ? static_cast<uint32_t>(info->aspectRatioLog2)
                   : static_cast<uint32_t>(3 - info->aspectRatioLog2);
    bindAddr = tm.ResolveMultibaseSource(static_cast<uint32_t>(tmu),
                                         startAddress, g2Large, g2Small,
                                         g2Aspect, info->format);
  }
  EntryCommon::BindTexSource(s_state, tmu, bindAddr);
}

FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grConstantColorValue (Glide 3.x) invoked: " << std::hex << value
                                                         << std::dec);
  EntryCommon::SetConstantColor(s_state, value);
}

FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t srcBuf, FxU32 srcX,
                                        FxU32 srcY, FxU32 srcWidth,
                                        FxU32 srcHeight, FxU32 dstStride,
                                        void* dstData) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbReadRegion (Glide 3.x) invoked.");
  EntryCommon::FlushActiveLfbWriteLock(s_state);
  return EntryCommon::LfbReadRegion(ResolveLfbTargetBuffer(srcBuf), srcX, srcY,
                                    srcWidth, srcHeight, dstStride, dstData)
             ? FXTRUE
             : FXFALSE;
}

FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dstBuf, FxU32 dstX,
                                         FxU32 dstY, GrLfbSrcFmt_t srcFmt,
                                         FxU32 srcWidth, FxU32 srcHeight,
                                         FxBool pixelPipeline, FxI32 srcStride,
                                         void* srcData) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  (void)pixelPipeline;
  GLIDE_LOG(
      DEBUG, "Frontend",
      "grLfbWriteRegion (Glide 3.x) invoked: " << srcWidth << "x" << srcHeight);
  return EntryCommon::LfbWriteRegion(ResolveLfbTargetBuffer(dstBuf), dstX, dstY,
                                     srcFmt, srcWidth, srcHeight, srcStride,
                                     srcData)
             ? FXTRUE
             : FXFALSE;
}

// LFB locking, unlocking, and row-flipping are managed by the unified
// GlideWrapper::LfbManager.

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer,
                                  GrLfbWriteMode_t writeMode,
                                  GrOriginLocation_t origin,
                                  FxBool pixelPipeline, GrLfbInfo_t* info) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbLock (Glide 3.x) invoked: buffer="
                << buffer << ", mode=" << writeMode << ", origin=" << origin);
  if (!GlideWrapper::LfbManager::IsWriteLockType(type)) {
    EntryCommon::FlushActiveLfbWriteLock(s_state);
  }
  return GlideWrapper::LfbManager::GetInstance().Lock(
             type, ResolveLfbTargetBuffer(buffer), writeMode, origin,
             pixelPipeline != 0, info)
             ? FXTRUE
             : FXFALSE;
}

FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbUnlock (Glide 3.x) invoked.");
  return EntryCommon::LfbUnlockWithState(s_state, type,
                                         ResolveLfbTargetBuffer(buffer))
             ? FXTRUE
             : FXFALSE;
}

FX_ENTRY FxBool FX_CALL gu3dfGetInfo(const char* filename, Gu3dfInfo* info) {
  GLIDE_LOG(DEBUG, "Frontend",
            "gu3dfGetInfo (Glide 3.x) invoked for: " << (filename ? filename
                                                                  : "unknown"));
  if (!filename || !info) return FXFALSE;

  GlideWrapper::Texture::Shared3dfHeader sharedHeader;
  uint32_t memRequired = 0;
  if (!GlideWrapper::Texture::Parse3dfHeader(filename, sharedHeader,
                                             memRequired)) {
    return FXFALSE;
  }

  info->header.format = static_cast<GrTextureFormat_t>(sharedHeader.format);
  info->header.aspect_ratio = static_cast<GrAspectRatio_t>(
      EntryCommon::Map3dfAspectToGlide3(sharedHeader.aspectW,
                                        sharedHeader.aspectH));
  info->header.small_lod =
      EntryCommon::Map3dfLodToGlide3(sharedHeader.smallLod);
  info->header.large_lod =
      EntryCommon::Map3dfLodToGlide3(sharedHeader.largeLod);
  info->header.width = sharedHeader.width;
  info->header.height = sharedHeader.height;
  info->mem_required = memRequired;

  return FXTRUE;
}

FX_ENTRY FxBool FX_CALL gu3dfLoad(const char* filename, Gu3dfInfo* info) {
  GLIDE_LOG(DEBUG, "Frontend",
            "gu3dfLoad (Glide 3.x) invoked for: " << (filename ? filename
                                                               : "unknown"));
  if (!filename || !info) return FXFALSE;

  if (!gu3dfGetInfo(filename, info)) return FXFALSE;

  if (!info->data) {
    GLIDE_LOG(WARN, "Frontend",
              "gu3dfLoad (Glide 3.x): Destination info->data is NULL! Caller "
              "must allocate memory.");
    return FXFALSE;
  }

  const bool isPalettized = (info->header.format == GR_TEXFMT_P_8 ||
                             info->header.format == GR_TEXFMT_AP_88);
  uint32_t paletteTmp[256] = {};
  const bool ok = EntryCommon::Load3dfPixelsAndPalette(
      filename, info->header.format, isPalettized ? paletteTmp : nullptr,
      info->data);
  if (ok && isPalettized) {
    for (int i = 0; i < 256; ++i) {
      info->table.palette.data[i] = static_cast<FxU32>(paletteTmp[i]);
    }
  }
  return ok ? FXTRUE : FXFALSE;
}

FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grDitherMode (Glide 3.x) invoked: mode=" << mode);
  EntryCommon::SetDitherMode(s_state, mode);
}

FX_ENTRY void FX_CALL grStippleMode(GrStippleMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grStippleMode (Glide 3.x) invoked: mode=" << mode);
  s_stippleMode = mode;
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetStippleState(s_stippleMode, s_stipplePattern);
  }
}

FX_ENTRY void FX_CALL grStipplePattern(GrStipplePattern_t pattern) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grStipplePattern (Glide 3.x) invoked: pattern=" << pattern);
  s_stipplePattern = pattern;
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetStippleState(s_stippleMode, s_stipplePattern);
  }
}

FX_ENTRY void FX_CALL grCoordinateSpace(GrCoordinateSpaceMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grCoordinateSpace (Glide 3.x) set to mode=" << mode);
  s_coordSpace = (mode == GR_CLIP_COORDS) ? 1 : 0;
}

FX_ENTRY void FX_CALL grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grViewport (Glide 3.x) set to x=" << x << ", y=" << y << ", w="
                                               << width << ", h=" << height);
  s_viewportX = static_cast<float>(x);
  s_viewportY = static_cast<float>(y);
  s_viewportWidth = static_cast<float>(width);
  s_viewportHeight = static_cast<float>(height);
}

FX_ENTRY void FX_CALL grDepthRange(FxFloat n, FxFloat f) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grDepthRange (Glide 3.x) set to near=" << n << ", far=" << f);
  s_depthRangeNear = n;
  s_depthRangeFar = f;
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetDepthRange(n, f);
  }
}

FX_ENTRY void FX_CALL grEnable(GrEnableMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grEnable (Glide 3.x) mode=" << mode);
  switch (mode) {
    case GR_AA_ORDERED:
      s_aaOrdered = true;
      if (auto* backend =
              GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        backend->SetAAState(true);
      }
      break;
    case GR_SHAMELESS_PLUG:
      s_shamelessPlug = true;
      break;
    case GR_STENCIL_MODE_EXT:
      EntryCommon::SetStencilMode(s_state, true);
      break;
    case GR_ALLOW_MIPMAP_DITHER:
    case GR_PASSTHRU:
    case GR_VIDEO_SMOOTHING:
    case GR_OPENGL_MODE_EXT:
    case 0x09:        // GR_MULTIRENDERING_EXT
    case 0x80000001u: // GR_COMBINEEXT_MODE (VSA-100 / Napalm)
      break;
    case GR_TEXTURE_UMA_EXT: {
      const auto model =
          GlideWrapper::EmulationRegistry::GetInstance().GetConfig().model;
      if (model == GlideWrapper::CardModel::VoodooBanshee ||
          model == GlideWrapper::CardModel::Voodoo3 ||
          model == GlideWrapper::CardModel::Voodoo4 ||
          model == GlideWrapper::CardModel::Voodoo5) {
        GlideWrapper::TextureManager::GetInstance().SetUmaMode(true);
      }
      break;
    }
    case GR_AA_MULTI_SAMPLE:
      s_aaMultiSample = true;
      if (auto* backend =
              GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        backend->SetAAMultiSample(true);
      }
      break;
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grEnable (Glide 3.x) unsupported mode=" << mode);
      break;
  }
}

FX_ENTRY void FX_CALL grDisable(GrEnableMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend", "grDisable (Glide 3.x) mode=" << mode);
  switch (mode) {
    case GR_AA_ORDERED:
      s_aaOrdered = false;
      if (auto* backend =
              GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        backend->SetAAState(false);
      }
      break;
    case GR_SHAMELESS_PLUG:
      s_shamelessPlug = false;
      break;
    case GR_STENCIL_MODE_EXT:
      EntryCommon::SetStencilMode(s_state, false);
      break;
    case GR_ALLOW_MIPMAP_DITHER:
    case GR_PASSTHRU:
    case GR_VIDEO_SMOOTHING:
    case GR_OPENGL_MODE_EXT:
    case 0x09:        // GR_MULTIRENDERING_EXT
    case 0x80000001u: // GR_COMBINEEXT_MODE (VSA-100 / Napalm)
      break;
    case GR_TEXTURE_UMA_EXT:
      GlideWrapper::TextureManager::GetInstance().SetUmaMode(false);
      break;
    case GR_AA_MULTI_SAMPLE:
      s_aaMultiSample = false;
      if (auto* backend =
              GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        backend->SetAAMultiSample(false);
      }
      break;
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grDisable (Glide 3.x) unsupported mode=" << mode);
      break;
  }
}

FX_ENTRY void FX_CALL grTexMultibase(GrChipID_t tmu, FxBool enable) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMultibase (Glide 3.x): tmu=" << tmu
                                               << ", enable=" << enable);
  GlideWrapper::TextureManager::GetInstance().SetMultibase(
      static_cast<uint32_t>(tmu), enable != 0);
}

FX_ENTRY void FX_CALL grTexMultibaseAddress(GrChipID_t tmu,
                                            GrTexBaseRange_t level,
                                            FxU32 startAddress, FxU32 evenOdd,
                                            GrTexInfo* info) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  (void)evenOdd;
  (void)info;
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMultibaseAddress (Glide 3.x): tmu="
                << tmu << ", level=" << level << ", addr=0x" << std::hex
                << startAddress << std::dec);
  GlideWrapper::TextureManager::GetInstance().SetMultibaseAddress(
      static_cast<uint32_t>(tmu), static_cast<uint32_t>(level), startAddress);
}

FX_ENTRY void FX_CALL grGlideGetVertexLayout(void* layout) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grGlideGetVertexLayout (Glide 3.x) invoked.");
  GlideWrapper::VertexLayout::GetInstance().GetLayoutState(layout);
}

FX_ENTRY void FX_CALL grGlideSetVertexLayout(const void* layout) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grGlideSetVertexLayout (Glide 3.x) invoked.");
  GlideWrapper::VertexLayout::GetInstance().SetLayoutState(layout);
}

FX_ENTRY void FX_CALL grLoadGammaTable(FxU32 nentries, FxU32* red, FxU32* green,
                                       FxU32* blue) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLoadGammaTable (Glide 3.x): nentries=" << nentries);
  std::vector<uint32_t> rBuf;
  std::vector<uint32_t> gBuf;
  std::vector<uint32_t> bBuf;
  if (red && nentries > 0) {
    rBuf.assign(red, red + nentries);
  }
  if (green && nentries > 0) {
    gBuf.assign(green, green + nentries);
  }
  if (blue && nentries > 0) {
    bBuf.assign(blue, blue + nentries);
  }
  EntryCommon::LoadGammaTable(
      static_cast<uint32_t>(nentries), rBuf.empty() ? nullptr : rBuf.data(),
      gBuf.empty() ? nullptr : gBuf.data(),
      bBuf.empty() ? nullptr : bBuf.data());
}

FX_ENTRY void FX_CALL grGetGammaTableExt(FxU32 nentries, FxU32* red,
                                         FxU32* green, FxU32* blue) {
  EnsureEngineInitialized();
  if (!GlideWrapper::EmulationRegistry::GetInstance().IsCardFeatureAvailable(
          "grGetGammaTableExt")) {
    GLIDE_LOG(WARN, "Frontend",
              "GATED CALL: grGetGammaTableExt is not available under active "
              "card profile.");
    return;
  }
  GLIDE_LOG(DEBUG, "Frontend",
            "grGetGammaTableExt (Glide 3.x): nentries=" << nentries);
  if (nentries == 0 || (!red && !green && !blue)) return;
  const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(nentries), 256u);
  std::vector<uint32_t> rBuf(red ? count : 0u);
  std::vector<uint32_t> gBuf(green ? count : 0u);
  std::vector<uint32_t> bBuf(blue ? count : 0u);
  EntryCommon::GetGammaTable(count, red ? rBuf.data() : nullptr,
                             green ? gBuf.data() : nullptr,
                             blue ? bBuf.data() : nullptr);
  for (uint32_t i = 0; i < count; ++i) {
    if (red) red[i] = static_cast<FxU32>(rBuf[i]);
    if (green) green[i] = static_cast<FxU32>(gBuf[i]);
    if (blue) blue[i] = static_cast<FxU32>(bBuf[i]);
  }
}

FX_ENTRY void FX_CALL grAlphaControlsITRGBLighting(FxBool enable) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  if (!GlideWrapper::EmulationRegistry::GetInstance().IsFunctionAvailable(
          "grAlphaControlsITRGBLighting")) {
    GLIDE_LOG(WARN, "Frontend",
              "GATED CALL: grAlphaControlsITRGBLighting (Glide 3.x) gated out "
              "under active version/device profile.");
    return;
  }
  GLIDE_LOG(DEBUG, "Frontend",
            "grAlphaControlsITRGBLighting (Glide 3.x): " << enable);
  s_alphaControlsITRGBLighting = (enable != 0);
}

FX_ENTRY void FX_CALL grCheckForRoom(FxI32 n) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grCheckForRoom (Glide 3.x): n=" << n);
}

FX_ENTRY void FX_CALL grErrorSetCallback(GrErrorCallbackFnc_t fnc) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grErrorSetCallback (Glide 3.x) invoked");
  s_errorCallback = fnc;
}

FX_ENTRY void FX_CALL grLfbConstantDepth(FxU32 depth) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbConstantDepth (Glide 3.x): " << depth);
  s_lfbConstantDepth = depth;
}

FX_ENTRY void FX_CALL grLfbWriteColorSwizzle(FxBool swizzleBytes,
                                             FxBool swapWords) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbWriteColorSwizzle (Glide 3.x): swizzleBytes="
                << swizzleBytes << ", swapWords=" << swapWords);
  s_lfbWriteColorSwizzle = (swizzleBytes != 0);
  s_lfbWriteColorSwizzleSwap = (swapWords != 0);
}

FX_ENTRY void FX_CALL grSstConfigPipeline(GrChipID_t chip, FxU32 reg,
                                          FxU32 value) {
  EnsureEngineInitialized();
  GLIDE_LOG(INFO, "Frontend",
            "grSstConfigPipeline (Glide 3.x): chip=" << chip << ", reg=" << reg
                                                     << ", value=" << value);
}

FX_ENTRY void FX_CALL grTexNCCTable(GrNCCTable_t table) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Tex);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grTexNCCTable (Glide 3.x): Table=" << table);
  auto& tm = GlideWrapper::TextureManager::GetInstance();
  tm.SetActiveNccTable(GR_TMU0, table);
  tm.SetActiveNccTable(GR_TMU1, table);
}

FX_ENTRY int FX_CALL guEncodeRLE16(void* dst, void* src, FxU32 width,
                                   FxU32 height) {
  return EntryCommon::EncodeRLE16(dst, src, static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height));
}

FX_ENTRY void FX_CALL guGammaCorrectionRGB(FxFloat red, FxFloat green,
                                           FxFloat blue) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "guGammaCorrectionRGB (Glide 3.x): red="
                << red << ", green=" << green << ", blue=" << blue);
  GlideWrapper::MathUtils::GammaCorrectionRGB(red, green, blue);
}

FX_ENTRY void FX_CALL grDisableAllEffects(void) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grDisableAllEffects (Glide 3.x) invoked");
  EntryCommon::DisableAllEffects(s_state, /*clearCachedFogMode=*/false);
}

FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grChromakeyMode (Glide 3.x) invoked: mode=" << mode);
  EntryCommon::SetChromakeyMode(s_state, mode);
}

FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grChromakeyValue (Glide 3.x) invoked: value=0x"
                << std::hex << value << std::dec);
  EntryCommon::SetChromakeyValue(s_state, value);
}

FX_ENTRY void FX_CALL grLfbConstantAlpha(GrAlpha_t alpha) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbConstantAlpha (Glide 3.x) invoked: alpha="
                << static_cast<int>(alpha));
  s_state.lfbConstantAlpha = alpha;
}

FX_ENTRY void FX_CALL grLfbWriteColorFormat(GrColorFormat_t colorFormat) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(
      DEBUG, "Frontend",
      "grLfbWriteColorFormat (Glide 3.x) invoked: format=" << colorFormat);
  s_state.lfbWriteColorFormat = colorFormat;
}

FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grColorMask (Glide 3.x) invoked: rgb=" << rgb << ", a=" << a);
  EntryCommon::SetColorMask(s_state, rgb != 0, a != 0);
}

FX_ENTRY void FX_CALL grShamelessPlug(void) {
  EnsureEngineInitialized();
  EntryCommon::RenderShamelessPlug();
}

FX_ENTRY void FX_CALL grSplash(float x, float y, float w, float h,
                               FxU32 frame) {
  EnsureEngineInitialized();
  g_inSplash = true;

  // Save current Glide 3.x vertex layout and coordinate space state
  GlideWrapper::Glide3VertexLayout savedLayout;
  GlideWrapper::VertexLayout::GetInstance().GetLayoutState(&savedLayout);
  const uint32_t savedCoordSpace = s_coordSpace;

  // Reset to Glide 2 canonical window-space layout for GlideSplashAnimator
  GlideWrapper::VertexLayout::GetInstance().ResetToGlide2Canonical();
  s_coordSpace = 0;

  EntryCommon::RenderSplash(x, y, w, h, frame, nullptr);

  // Restore the game's active Glide 3.x vertex layout and coordinate space
  s_coordSpace = savedCoordSpace;
  GlideWrapper::VertexLayout::GetInstance().SetLayoutState(&savedLayout);

  g_inSplash = false;
}

/**
 * @brief DOSBox-specific configuration extension.
 * Performs a handshake verification by setting the signature pointer to 'SDL2'.
 */
FX_ENTRY void FX_CALL setConfig(FxU32 flags, void* magic) {
  EntryCommon::HandleDosboxSetConfig("Glide 3.x", flags, magic);
}

/**
 * @brief DOSBox-specific resolution configuration extension.
 */
FX_ENTRY void FX_CALL setConfigRes(int res, void (*swap12)()) {
  EntryCommon::HandleDosboxSetConfigRes("Glide 3.x", res, swap12);
}

}  // extern "C"
