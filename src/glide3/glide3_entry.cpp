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

// Phase 4 states
uint32_t s_stippleMode = 0;
uint32_t s_stipplePattern = 0xFFFFFFFF;
bool s_windowOpen = false;

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
  s_sstOrigin = 0;
  g_inSplash = false;
  s_stippleMode = 0;
  s_stipplePattern = 0xFFFFFFFF;
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

FX_ENTRY GrContext_t FX_CALL grSstWinOpen(AnyPtr hWnd,
                                          GrScreenResolution_t resolution,
                                          GrScreenRefresh_t refresh,
                                          GrColorFormat_t format,
                                          GrOriginLocation_t origin,
                                          int nColBuffers, int nAuxBuffers) {
  EnsureEngineInitialized();
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
            "grSstWinOpen (Glide 3.x) invoked. Res="
                << resolution << ", Buffers=" << nColBuffers
                << ", Aux=" << nAuxBuffers);

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

  GlideWrapper::VertexLayout::GetInstance().ResetToGlide3Canonical();
  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  if (!backend) return 0;

  bool attached = backend->AttachWindow(
      reinterpret_cast<void*>(static_cast<uintptr_t>(hWnd)), width, height,
      hWnd != 0);
  backend->SetPixelFormat(format);
  s_sstOrigin = origin;
  backend->SetSstOrigin(origin);

  if (attached) {
    s_windowOpen = true;
    EntryCommon::ConfigureTmuMemoryLimits();
    EntryCommon::ApplyPostWinOpenOverrides(s_state, format, nColBuffers,
                                           nAuxBuffers);
  }

  return attached ? 1 : 0;
}

FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context) {
  EnsureEngineInitialized();
  (void)context;
  s_windowOpen = false;
  EntryCommon::CloseWindow("grSstWinClose (Glide 3.x) invoked.");
  return FXTRUE;
}

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha,
                                    FxU32 depth) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Draw);
  EnsureEngineInitialized();
  EntryCommon::ClearBuffer(color, alpha, depth);
}

FX_ENTRY void FX_CALL grBufferSwap(FxU32 swapInterval) {
  EnsureEngineInitialized();
  (void)swapInterval;
  EntryCommon::FlushActiveLfbWriteLock(s_state);
  if (EntryCommon::ShouldRenderShamelessPlug(s_shamelessPlug)) {
    grShamelessPlug();
  }
  EntryCommon::SwapBuffers();
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
  EntryCommon::SetRenderBuffer(buffer);
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
  backend->DrawPoint(mv);
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
    case GR_NUM_BOARDS:
      *params = 1;
      return 4;
    case GR_NUM_TMU:
      *params = static_cast<FxI32>(config.tmuCount);
      return 4;
    case GR_MAX_TEXTURE_SIZE:
      *params = (config.model >= GlideWrapper::CardModel::Voodoo3) ? 2048 : 256;
      return 4;
    case GR_MAX_TEXTURE_ASPECT_RATIO:
      *params = 3;  // 8:1
      return 4;
    case GR_BITS_DEPTH:
      *params = 16;
      return 4;
    case GR_BITS_RGBA:
      *params = 32;
      return 4;
    case GR_FOG_TABLE_ENTRIES:
      *params = 64;
      return 4;
    case GR_GAMMA_TABLE_ENTRIES:
      *params = 256;
      return 4;
    case GR_VIEWPORT:
      if (plength < 16) return 0;
      params[0] = static_cast<FxI32>(s_viewportX);
      params[1] = static_cast<FxI32>(s_viewportY);
      params[2] = static_cast<FxI32>(s_viewportWidth);
      params[3] = static_cast<FxI32>(s_viewportHeight);
      return 16;
    case GR_IS_BUSY:
      *params = 0;
      return 4;
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
    case 0x06:  // GR_GLIDE_STATE_SIZE
      *params = 1024;
      return 4;
    case 0x07:  // GR_GLIDE_VERTEXLAYOUT_SIZE
      *params = 256;
      return 4;
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grGet (Glide 3.x) unknown or unsupported pname=" << pname);
      return 0;
  }
}

static const GrResolution s_supportedResolutions[] = {
    {GR_RESOLUTION_512x384, GR_REFRESH_60Hz, 2, 1},
    {GR_RESOLUTION_640x480, GR_REFRESH_60Hz, 2, 1},
    {GR_RESOLUTION_800x600, GR_REFRESH_60Hz, 2, 1},
    {GR_RESOLUTION_1024x768, GR_REFRESH_60Hz, 2, 1}};

FX_ENTRY FxI32 FX_CALL grQueryResolutions(const GrResolution* resTemplate,
                                          GrResolution* output) {
  GLIDE_LOG(DEBUG, "Frontend", "grQueryResolutions (Glide 3.x) invoked.");

  FxI32 matchCount = 0;
  constexpr int numResolutions =
      sizeof(s_supportedResolutions) / sizeof(s_supportedResolutions[0]);

  for (int i = 0; i < numResolutions; ++i) {
    const auto& res = s_supportedResolutions[i];
    bool match = true;

    if (resTemplate) {
      if (resTemplate->resolution !=
              static_cast<GrScreenResolution_t>(GR_QUERY_ANY) &&
          resTemplate->resolution != res.resolution) {
        match = false;
      }
      if (resTemplate->refresh !=
              static_cast<GrScreenRefresh_t>(GR_QUERY_ANY) &&
          resTemplate->refresh != res.refresh) {
        match = false;
      }
      if (resTemplate->numColorBuffers != GR_QUERY_ANY &&
          resTemplate->numColorBuffers != res.numColorBuffers) {
        match = false;
      }
      if (resTemplate->numAuxBuffers != GR_QUERY_ANY &&
          resTemplate->numAuxBuffers != res.numAuxBuffers) {
        match = false;
      }
    }

    if (match) {
      if (output) {
        output[matchCount] = res;
      }
      matchCount++;
    }
  }

  return matchCount;
}

FX_ENTRY FxBool FX_CALL grReset(FxU32 what) {
  GLIDE_LOG(DEBUG, "Frontend", "grReset (Glide 3.x) invoked: what=" << what);
  return FXTRUE;
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname) {
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grGetString (Glide 3.x) invoked for pname: " << pname);
  auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
  switch (pname) {
    case GR_VERSION:
      return "3.10 (glide-ng)";
    case GR_HARDWARE: {
      switch (reg.GetConfig().model) {
        case GlideWrapper::CardModel::VoodooGraphics:
          return "Voodoo Graphics (SST-1) (Modernized Wrapper Emulation)";
        case GlideWrapper::CardModel::VoodooRush:
          return "Voodoo Rush (SST-96) (Modernized Wrapper Emulation)";
        case GlideWrapper::CardModel::Voodoo2:
          return "Voodoo2 (CVG) (Modernized Wrapper Emulation)";
        case GlideWrapper::CardModel::Voodoo3:
          return "Voodoo3 (H3) (Modernized Wrapper Emulation)";
        case GlideWrapper::CardModel::Voodoo5:
          return "Voodoo5 (H5) (Modernized Wrapper Emulation)";
        default:
          return "3dfx Voodoo Accelerator (Modernized Wrapper Emulation)";
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
        case GlideWrapper::CardModel::Voodoo3:
          return "CHROMARANGE FOGCOORD TEXCHROMA TEXMIRROR PALETTE6666 "
                 "RESOLUTION SURFACE COMMAND_TRANSPORT";
        case GlideWrapper::CardModel::Voodoo5:
          return "CHROMARANGE FOGCOORD TEXCHROMA TEXMIRROR PALETTE6666 "
                 "RESOLUTION SURFACE COMMAND_TRANSPORT PIXEXT COMBINE TEXFMT "
                 "TEXTUREBUFFER TEXUMA";
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
  const uint32_t tmuBytes = std::max(1u, config.tmuMemoryMb) * 1024u * 1024u;
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
  GLIDE_LOG(
      DEBUG, "Frontend",
      "grTexDetailControl (Glide 3.x) invoked for TMU=" << tmu << " (Stubbed)");
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
      (ws->rgbSrcBlend > 10) ? 1 : ws->rgbSrcBlend;  // GR_BLEND_ONE
  s_state.rgbDstBlend =
      (ws->rgbDstBlend > 10) ? 0 : ws->rgbDstBlend;  // GR_BLEND_ZERO
  s_state.alphaSrcBlend = (ws->alphaSrcBlend > 10) ? 1 : ws->alphaSrcBlend;
  s_state.alphaDstBlend = (ws->alphaDstBlend > 10) ? 0 : ws->alphaDstBlend;

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
    winWidth = b->GetWidth();
    winHeight = b->GetHeight();
  }
  s_state.clipMinX = std::max(0u, std::min(ws->clipMinX, winWidth));
  s_state.clipMinY = std::max(0u, std::min(ws->clipMinY, winHeight));
  s_state.clipMaxX = std::max(s_state.clipMinX, std::min(ws->clipMaxX, winWidth));
  s_state.clipMaxY =
      std::max(s_state.clipMinY, std::min(ws->clipMaxY, winHeight));

  s_state.colorMaskRgb = ws->colorMaskRgb != 0;
  s_state.colorMaskAlpha = ws->colorMaskAlpha != 0;
  for (int i = 0; i < 2; ++i) {
    s_state.texRgbInvert[i] = ws->texRgbInvert[i] != 0;
    s_state.texAlphaInvert[i] = ws->texAlphaInvert[i] != 0;
  }

  // Apply states to backend
  EntryCommon::ApplySharedStateToBackend(s_state, s_sstOrigin,
                                         /*includeTexInvert=*/true);
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetAAState(s_aaOrdered);
    b->SetDepthRange(s_depthRangeNear, s_depthRangeFar);
    b->SetStippleState(s_stippleMode, s_stipplePattern);
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
  GLIDE_LOG(DEBUG, "Frontend",
            "grGetProcAddress (Glide 3.x): " << (procName ? procName : "NULL"));
  if (!procName) return nullptr;

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
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexSource (Glide 3.x) invoked for TMU=" << tmu);
  EntryCommon::BindTexSource(s_state, tmu, startAddress);
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
  return EntryCommon::LfbReadRegion(srcBuf, srcX, srcY, srcWidth, srcHeight,
                                    dstStride, dstData)
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
  return EntryCommon::LfbWriteRegion(dstBuf, dstX, dstY, srcFmt, srcWidth,
                                     srcHeight, srcStride, srcData)
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
  return GlideWrapper::LfbManager::GetInstance().Lock(
             type, buffer, writeMode, origin, pixelPipeline != 0, info)
             ? FXTRUE
             : FXFALSE;
}

FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbUnlock (Glide 3.x) invoked.");
  return EntryCommon::LfbUnlockWithState(s_state, type, buffer) ? FXTRUE
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
    default:
      GLIDE_LOG(WARN, "Frontend",
                "grDisable (Glide 3.x) unsupported mode=" << mode);
      break;
  }
}

FX_ENTRY void FX_CALL grTexMultibase(GrChipID_t tmu, FxBool enable) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMultibase (Glide 3.x) stub invoked for TMU="
                << tmu << ", enable=" << enable);
}

FX_ENTRY void FX_CALL grTexMultibaseAddress(GrChipID_t tmu,
                                            GrTexBaseRange_t level,
                                            FxU32 startAddress, FxU32 evenOdd,
                                            GrTexInfo* info) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  GLIDE_LOG(DEBUG, "Frontend",
            "grTexMultibaseAddress (Glide 3.x) stub: tmu=" << tmu << ", level="
                                                           << level);
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

  // Save current Glide 3.x vertex layout state
  GlideWrapper::Glide3VertexLayout savedLayout;
  GlideWrapper::VertexLayout::GetInstance().GetLayoutState(&savedLayout);

  // Reset to Glide 2 canonical layout since GlideSplashAnimator uses standard
  // Glide 2 GrVertex structures
  GlideWrapper::VertexLayout::GetInstance().ResetToGlide2Canonical();

  EntryCommon::RenderSplash(x, y, w, h, frame, nullptr);

  // Restore the game's active Glide 3.x vertex layout state
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
