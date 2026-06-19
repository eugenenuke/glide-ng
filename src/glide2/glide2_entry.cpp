#include "core/Logger.h"
#include "core/WrapperConfig.h"
#include "core/IConfigLoader.h"
#include "core/JsonConfigLoader.h"
#include "core/BackendManager.h"
#include "core/GlideSplashAnimator.h"
#include "core/VertexLayout.h"
#include "core/TextureManager.h"
#include "core/Telemetry.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <chrono>

#ifndef __linux__
#define __linux__
#endif

#pragma GCC diagnostic ignored "-Wwrite-strings"

#include <glide.h>

// Fallback legacy defines if internal headers omitted them
#ifndef GR_VERSION
#define GR_EXTENSION                    0xa0
#define GR_HARDWARE                     0xa1
#define GR_RENDERER                     0xa2
#define GR_VENDOR                       0xa3
#define GR_VERSION                      0xa4
#endif

extern "C" {

namespace {
    std::chrono::steady_clock::time_point s_lastSwapTime = std::chrono::steady_clock::now();

    uint32_t s_depthMode = 0;
    uint32_t s_depthCompare = 1;
    bool s_depthMask = true;
    int32_t s_depthBias = 0;

    uint32_t s_alphaTestOp = 7;
    uint32_t s_alphaTestRef = 0;

    uint32_t s_clampS[2] = {0, 0}; // 0=WRAP, 1=CLAMP
    uint32_t s_clampT[2] = {0, 0};
    uint32_t s_minFilter[2] = {0, 0}; // 0=POINT, 1=BILINEAR
    uint32_t s_magFilter[2] = {0, 0};
    uint32_t s_colorFunc   = 0x3; // GR_COMBINE_FUNCTION_SCALE_OTHER
    uint32_t s_colorFactor = 0x8; // GR_COMBINE_FACTOR_ONE
    uint32_t s_colorLocal  = 0x0; // GR_COMBINE_LOCAL_ITERATED
    uint32_t s_colorOther  = 0x0; // GR_COMBINE_OTHER_ITERATED
    bool     s_colorInvert = false;
    uint32_t s_alphaFunc   = 0x3; // GR_COMBINE_FUNCTION_SCALE_OTHER
    uint32_t s_alphaFactor = 0x8; // GR_COMBINE_FACTOR_ONE
    uint32_t s_alphaLocal  = 0x1; // GR_COMBINE_LOCAL_NONE (constant)
    uint32_t s_alphaOther  = 0x2; // GR_COMBINE_OTHER_CONSTANT
    bool     s_alphaInvert = false;
    uint32_t s_texRgbFunc[2]    = {0, 0};
    uint32_t s_texRgbFactor[2]  = {0, 0};
    uint32_t s_texAlphaFunc[2]  = {0, 0};
    uint32_t s_texAlphaFactor[2]= {0, 0};
    uint32_t s_boundTex[2]      = {0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t s_stwHintMask      = 0; // Default: 0 (GR_STWHINT_W_OK)

    uint32_t s_constantColor = 0xFFFFFFFF;
    uint32_t s_rgbSrcBlend = 4;   // GR_BLEND_ONE
    uint32_t s_rgbDstBlend = 0;   // GR_BLEND_ZERO
    typedef FxI32 GrChromaRangeMode_t;
    uint32_t s_alphaSrcBlend = 4;
    uint32_t s_alphaDstBlend = 0;
    uint32_t s_cullMode = 0;      // GR_CULL_DISABLE
    uint32_t s_clipMinX = 0;
    uint32_t s_clipMinY = 0;
    uint32_t s_clipMaxX = 640;
    uint32_t s_clipMaxY = 480;
    uint32_t s_fogMode = 0;
    uint32_t s_fogColor = 0xff000000;
    uint8_t s_fogTable[64] = {0};

    bool s_colorMaskRgb = true;
    bool s_colorMaskAlpha = true;
    bool s_shamelessPlugEnabled = false;
    uint8_t s_lfbConstantAlpha = 255;
    uint32_t s_lfbWriteColorFormat = 0; // GR_COLORFORMAT_ARGB
    uint32_t s_chromakeyMode = 0;
    uint32_t s_chromakeyValue = 0;
    uint32_t s_chromakeyRangeMin = 0;
    uint32_t s_chromakeyRangeMax = 0;
    GrChromaRangeMode_t s_chromakeyRangeMode = 0;

    std::vector<GrMipMapInfo> s_guTextures;
    uint32_t s_guTexNextAddress = 0;
    GrMipMapId_t s_currentMipMap[2] = { GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE };
    float s_texLodBias = 0.0f;

    uint16_t s_lfbConstantDepth = 0;
    bool s_lfbWriteColorSwizzle = false;
    bool s_lfbWriteColorSwizzleSwap = false;
    bool s_alphaControlsITRGBLighting = false;
    GrErrorCallbackFnc_t s_errorCallback = nullptr;
    float s_gammaCorrectionValue = 1.0f;

    // Optimized 16-bit 3dfx RLE Decompression Helper
    static void Decompress3dfxRle(int width, int height, const uint8_t* src, uint16_t* dst) {
        int count = width * height;
        while (count > 0) {
            uint8_t code = *src++;
            int run = (code & 0x7F) + 1;
            if (code & 0x80) { // Run-Length Packet
                uint16_t val = *reinterpret_cast<const uint16_t*>(src);
                src += 2;
                for (int i = 0; i < run && count > 0; ++i) {
                    *dst++ = val;
                    count--;
                }
            } else { // Literal Packet
                for (int i = 0; i < run && count > 0; ++i) {
                    *dst++ = *reinterpret_cast<const uint16_t*>(src);
                    src += 2;
                    count--;
                }
            }
        }
    }
}

uint32_t s_sstOrigin = 0;     // GR_ORIGIN_UPPER_LEFT (Global for grSplash)
void grShamelessPlug(void);   // Forward declaration of internal watermark renderer

// Opaque state container matching GrState's char padding
struct GlideWrapperState {
    uint32_t depthMode;
    uint32_t depthCompare;
    uint32_t depthMask;
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
    uint32_t colorInvert;
    uint32_t alphaFunc;
    uint32_t alphaFactor;
    uint32_t alphaLocal;
    uint32_t alphaOther;
    uint32_t alphaInvert;

    uint32_t texRgbFunc[2];
    uint32_t texRgbFactor[2];
    uint32_t texAlphaFunc[2];
    uint32_t texAlphaFactor[2];
    uint32_t boundTex[2];
    uint32_t stwHintMask;

    uint32_t constantColor;
    uint32_t rgbSrcBlend;
    uint32_t rgbDstBlend;
    uint32_t alphaSrcBlend;
    uint32_t alphaDstBlend;
    uint32_t cullMode;
    uint32_t sstOrigin;
    uint32_t clipMinX;
    uint32_t clipMinY;
    uint32_t clipMaxX;
    uint32_t clipMaxY;
    uint32_t fogMode;
    uint32_t fogColor;
    uint8_t fogTable[64];

    uint32_t colorMaskRgb;
    uint32_t colorMaskAlpha;
};

static_assert(sizeof(GlideWrapperState) <= sizeof(GrState), "GlideWrapperState exceeds GrState footprint!");

FX_ENTRY void FX_CALL grGlideInit(void) {
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    GlideWrapper::JsonConfigLoader loader;
    loader.Load("glide_config.json", reg.GetConfig());
    auto& logger = GlideWrapper::Logger::GetInstance();
    logger.Initialize(std::string(GlideWrapper::WRAPPER_PROJECT_NAME) + ".log", static_cast<GlideWrapper::LogLevel>(reg.GetConfig().logLevel), reg.GetConfig().logToConsole);
    GLIDE_LOG(INFO, "Frontend", "grGlideInit (Glide 2.x) invoked.");
    
    // Dump dynamic startup summaries
    logger.LogExecutionSummary(reg.GetConfig());

    // Establish multi-target Vulkan rendering execution pipeline
    GlideWrapper::BackendManager::GetInstance().EstablishBackend(reg.GetConfig());
}

inline void EnsureEngineInitialized() {
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto& bm  = GlideWrapper::BackendManager::GetInstance();
    if (!bm.GetBackend()) {
        auto& logger = GlideWrapper::Logger::GetInstance();
        logger.Initialize("glide-ng.log", static_cast<GlideWrapper::LogLevel>(reg.GetConfig().logLevel), reg.GetConfig().logToConsole);
        bm.EstablishBackend(reg.GetConfig());
        GLIDE_LOG(INFO, "Frontend", "Implicit grGlideInit executed seamlessly.");
    } else {
        bm.GetBackend()->PollEvents();
    }
}

FX_ENTRY void FX_CALL grGlideShutdown(void) {
    GLIDE_LOG(INFO, "Frontend", "grGlideShutdown (Glide 2.x) invoked.");
    GlideWrapper::TelemetryManager::GetInstance().PrintReport();
    GlideWrapper::BackendManager::GetInstance().ShutdownBackend();
}

FX_ENTRY FxBool FX_CALL grSstWinOpen(
          FxU32                hWnd,
          GrScreenResolution_t resolution,
          GrScreenRefresh_t    refresh,
          GrColorFormat_t      format,
          GrOriginLocation_t   origin,
          int                  nColBuffers,
          int                  nAuxBuffers) {
    EnsureEngineInitialized();
    GlideWrapper::BackendManager::GetInstance().SetSplashAnimator(std::make_unique<GlideWrapper::GlideSplashAnimator>());
    GLIDE_LOG(INFO, "Frontend", "grSstWinOpen (Glide 2.x) invoked. Res=" << resolution << ", Buffers=" << nColBuffers << ", Origin=" << origin);
    s_sstOrigin = origin;
    GlideWrapper::VertexLayout::GetInstance().ResetToGlide2Canonical();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend) return FXFALSE;
    
    // Normalize classic resolution defines to pixel dimensions
    uint32_t width = 640;
    uint32_t height = 480;
    if (resolution == GR_RESOLUTION_320x200) { width = 320; height = 200; }
    else if (resolution == GR_RESOLUTION_320x240) { width = 320; height = 240; }
    else if (resolution == GR_RESOLUTION_400x256) { width = 400; height = 256; }
    else if (resolution == GR_RESOLUTION_400x300) { width = 400; height = 300; }
    else if (resolution == GR_RESOLUTION_512x384) { width = 512; height = 384; }
    else if (resolution == GR_RESOLUTION_640x200) { width = 640; height = 200; }
    else if (resolution == GR_RESOLUTION_640x350) { width = 640; height = 350; }
    else if (resolution == GR_RESOLUTION_640x400) { width = 640; height = 400; }
    else if (resolution == GR_RESOLUTION_640x480) { width = 640; height = 480; }
    else if (resolution == GR_RESOLUTION_800x600) { width = 800; height = 600; }
    else if (resolution == GR_RESOLUTION_856x480) { width = 856; height = 480; }
    else if (resolution == GR_RESOLUTION_960x720) { width = 960; height = 720; }
    else if (resolution == GR_RESOLUTION_1024x768) { width = 1024; height = 768; }
    else if (resolution == GR_RESOLUTION_1280x1024) { width = 1280; height = 1024; }
    else if (resolution == GR_RESOLUTION_1600x1200) { width = 1600; height = 1200; }
 
    // Reset all frontend static global states to canonical Glide defaults
    s_depthMode = 0;
    s_depthCompare = 1;
    s_depthMask = true;
    s_depthBias = 0;
    s_alphaTestOp = 7;
    s_alphaTestRef = 0;
    s_clampS[0] = s_clampS[1] = 0;
    s_clampT[0] = s_clampT[1] = 0;
    s_minFilter[0] = s_minFilter[1] = 0;
    s_magFilter[0] = s_magFilter[1] = 0;
    s_colorFunc   = 0x3; // GR_COMBINE_FUNCTION_SCALE_OTHER
    s_colorFactor = 0x8; // GR_COMBINE_FACTOR_ONE
    s_colorLocal  = 0x0; // GR_COMBINE_LOCAL_ITERATED
    s_colorOther  = 0x0; // GR_COMBINE_OTHER_ITERATED
    s_colorInvert = false;
    s_alphaFunc   = 0x3; // GR_COMBINE_FUNCTION_SCALE_OTHER
    s_alphaFactor = 0x8; // GR_COMBINE_FACTOR_ONE
    s_alphaLocal  = 0x1; // GR_COMBINE_LOCAL_NONE
    s_alphaOther  = 0x2; // GR_COMBINE_OTHER_CONSTANT
    s_alphaInvert = false;
    s_texRgbFunc[0] = s_texRgbFunc[1] = 0;
    s_texRgbFactor[0] = s_texRgbFactor[1] = 0;
    s_texAlphaFunc[0] = s_texAlphaFunc[1] = 0;
    s_texAlphaFactor[0] = s_texAlphaFactor[1] = 0;
    s_boundTex[0] = s_boundTex[1] = 0xFFFFFFFF;
    s_stwHintMask = 0;
    s_constantColor = 0xFFFFFFFF;
    s_rgbSrcBlend = 4; // GR_BLEND_ONE
    s_rgbDstBlend = 0; // GR_BLEND_ZERO
    s_alphaSrcBlend = 4;
    s_alphaDstBlend = 0;
    s_cullMode = 0;
    s_clipMinX = 0;
    s_clipMinY = 0;
    s_clipMaxX = width;
    s_clipMaxY = height;
    s_fogMode = 0;
    s_fogColor = 0xff000000;
    std::memset(s_fogTable, 0, sizeof(s_fogTable));
    s_colorMaskRgb = true;
    s_colorMaskAlpha = true;
    s_shamelessPlugEnabled = false;
    s_lfbConstantAlpha = 255;
    s_lfbWriteColorFormat = 0;
    s_chromakeyMode = 0;
    s_chromakeyValue = 0;
    s_chromakeyRangeMin = 0;
    s_chromakeyRangeMax = 0;
    s_chromakeyRangeMode = 0;
    s_guTextures.clear();
    s_guTexNextAddress = 0;
    s_currentMipMap[0] = s_currentMipMap[1] = GR_NULL_MIPMAP_HANDLE;
    s_texLodBias = 0.0f;
    s_lfbConstantDepth = 0;
    s_lfbWriteColorSwizzle = false;
    s_lfbWriteColorSwizzleSwap = false;
    s_alphaControlsITRGBLighting = false;
    s_gammaCorrectionValue = 1.0f;
 
    bool attached = backend->AttachWindow(reinterpret_cast<void*>(static_cast<uintptr_t>(hWnd)), width, height, hWnd != 0);
    backend->SetPixelFormat(format);
    if (attached) {
        backend->SetCombinerMode(s_colorFunc, s_colorFactor, s_colorLocal, s_colorOther, s_colorInvert,
                                 s_alphaFunc, s_alphaFactor, s_alphaLocal, s_alphaOther, s_alphaInvert);
        backend->SetConstantColor(s_constantColor);
        backend->SetBlendState(s_rgbSrcBlend, s_rgbDstBlend, s_alphaSrcBlend, s_alphaDstBlend);
        backend->SetCullState(s_cullMode);
        backend->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
        backend->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
        backend->SetClipWindow(s_clipMinX, s_clipMinY, s_clipMaxX, s_clipMaxY);
        backend->SetSstOrigin(s_sstOrigin);
        backend->SetFogMode(s_fogMode);
        backend->SetFogColor(s_fogColor);
        backend->SetSTWHintState(s_stwHintMask);
        backend->SetFogTable(s_fogTable);
        for (uint32_t tmu = 0; tmu < 2; ++tmu) {
            backend->SetTexCombinerMode(tmu, s_texRgbFunc[tmu], s_texRgbFactor[tmu], s_texAlphaFunc[tmu], s_texAlphaFactor[tmu]);
            backend->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
    return attached ? FXTRUE : FXFALSE;
}

FX_ENTRY void FX_CALL grSstWinClose(void) {
    EnsureEngineInitialized();
    GlideWrapper::BackendManager::GetInstance().SetSplashAnimator(nullptr);
    GLIDE_LOG(INFO, "Frontend", "grSstWinClose invoked.");
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->DetachWindow();
}

FX_ENTRY void FX_CALL grHints(GrHint_t hintType, FxU32 hintMask) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grHints invoked: Type=" << hintType << ", Mask=" << hintMask);
    if (hintType == GR_HINT_STWHINT) {
        s_stwHintMask = hintMask;
        GlideWrapper::VertexLayout::GetInstance().SetSTWHintMask(hintMask);
        auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
        if (backend) {
            backend->SetSTWHintState(hintMask);
        }
    }
}

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->ClearBuffer(color, alpha, static_cast<float>(depth) / 65535.0f, 0x3);
}

FX_ENTRY void FX_CALL grBufferSwap(int swap_interval) {
    EnsureEngineInitialized();
    if (s_shamelessPlugEnabled) {
        grShamelessPlug();
    }
    s_lastSwapTime = std::chrono::steady_clock::now();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->SwapBuffers();
}

FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grRenderBuffer invoked for Buffer=" << buffer);
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->SetRenderBuffer(buffer);
}

FX_ENTRY void FX_CALL grDrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !a || !b || !c) return;

    GLIDE_PROFILE_SCOPE("API::grDrawTriangle");
    GlideWrapper::TelemetryManager::GetInstance().IncrementTrianglesProcessed(1);

    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawTriangle(layout.DecodeVertex(a), layout.DecodeVertex(b), layout.DecodeVertex(c));
}

FX_ENTRY void FX_CALL grTriStats(FxU32* processed, FxU32* drawn) {
    if (processed) {
        *processed = static_cast<FxU32>(GlideWrapper::TelemetryManager::GetInstance().GetTrianglesProcessed());
    }
    if (drawn) {
        *drawn = static_cast<FxU32>(GlideWrapper::TelemetryManager::GetInstance().GetTrianglesDrawn());
    }
}

FX_ENTRY void FX_CALL grResetTriStats(void) {
    GlideWrapper::TelemetryManager::GetInstance().ResetTriangleStats();
}

FX_ENTRY void FX_CALL grSstPerfStats(GrSstPerfStats_t* stats) {
    if (stats) {
        auto& tracker = GlideWrapper::TelemetryManager::GetInstance().GetFrameTracker();
        std::memset(stats, 0, sizeof(GrSstPerfStats_t));
        stats->pixelsIn = static_cast<FxU32>(tracker.GetCurrentFps());
        stats->chromaFail = static_cast<FxU32>(tracker.GetRenderTimeMs() * 1000.0f);
    }
}

FX_ENTRY void FX_CALL grSstResetPerfStats(void) {
    GlideWrapper::TelemetryManager::GetInstance().Reset();
}

FX_ENTRY void FX_CALL grDrawLine(const GrVertex* a, const GrVertex* b) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !a || !b) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawLine(layout.DecodeVertex(a), layout.DecodeVertex(b));
}

FX_ENTRY void FX_CALL grDrawPoint(const GrVertex* pt) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grDrawPoint invoked!");
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !pt) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawPoint(layout.DecodeVertex(pt));
}

// =============================================================================
// Polygon Fan Decomposition & Forwarding Helpers
// =============================================================================
static void DrawPolygonHelper(const int nverts, const GrVertex vlist[]) {
    if (nverts < 3 || !vlist) return;
    for (int i = 1; i < nverts - 1; ++i) {
        grDrawTriangle(&vlist[0], &vlist[i], &vlist[i + 1]);
    }
}

static void DrawPolygonIndexedHelper(const int nverts, const int ilist[], const GrVertex vlist[]) {
    if (nverts < 3 || !ilist || !vlist) return;
    for (int i = 1; i < nverts - 1; ++i) {
        grDrawTriangle(&vlist[ilist[0]], &vlist[ilist[i]], &vlist[ilist[i + 1]]);
    }
}

// =============================================================================
// Standard Polygon Drawing APIs (Milestone 3)
// =============================================================================
FX_ENTRY void FX_CALL grDrawPolygon(const int nverts, const int ilist[], const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonIndexedHelper(nverts, ilist, vlist);
}

FX_ENTRY void FX_CALL grDrawPolygonVertexList(const int nverts, const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonHelper(nverts, vlist);
}

FX_ENTRY void FX_CALL grDrawPlanarPolygon(const int nverts, const int ilist[], const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonIndexedHelper(nverts, ilist, vlist);
}

FX_ENTRY void FX_CALL grDrawPlanarPolygonVertexList(const int nverts, const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonHelper(nverts, vlist);
}

// =============================================================================
// Anti-Aliased Geometry Forwarding APIs (Milestone 2)
// =============================================================================
FX_ENTRY void FX_CALL grAADrawPoint(const GrVertex* pt) {
    grDrawPoint(pt);
}

FX_ENTRY void FX_CALL grAADrawLine(const GrVertex* v1, const GrVertex* v2) {
    grDrawLine(v1, v2);
}

FX_ENTRY void FX_CALL grAADrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c, FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias) {
    (void)ab_antialias;
    (void)bc_antialias;
    (void)ca_antialias;
    grDrawTriangle(a, b, c);
}

FX_ENTRY void FX_CALL grAADrawPolygon(const int nverts, const int ilist[], const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonIndexedHelper(nverts, ilist, vlist);
}

FX_ENTRY void FX_CALL grAADrawPolygonVertexList(const int nverts, const GrVertex vlist[]) {
    EnsureEngineInitialized();
    DrawPolygonHelper(nverts, vlist);
}

// =============================================================================
// Clipped Utility Forwarding APIs (Milestone 2)
// =============================================================================
FX_ENTRY void FX_CALL guDrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    grDrawTriangle(a, b, c);
}

FX_ENTRY void FX_CALL guAADrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    grDrawTriangle(a, b, c);
}

FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration* hwconfig) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grSstQueryHardware invoked.");
    if (!hwconfig) return FXFALSE;

    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto& config = reg.GetConfig();

    hwconfig->num_sst = 1;
    if (config.model == GlideWrapper::CardModel::VoodooGraphics) {
        hwconfig->SSTs[0].type = GR_SSTTYPE_VOODOO;
        hwconfig->SSTs[0].sstBoard.VoodooConfig.fbRam = config.fbiMemoryMb;
    } else {
        // Voodoo 2 or Voodoo 3
        hwconfig->SSTs[0].type = GR_SSTTYPE_Voodoo2;
        hwconfig->SSTs[0].sstBoard.Voodoo2Config.fbRam = config.fbiMemoryMb;
        hwconfig->SSTs[0].sstBoard.Voodoo2Config.nTexelfx = config.tmuCount;
        hwconfig->SSTs[0].sstBoard.Voodoo2Config.sliDetect = (config.model == GlideWrapper::CardModel::Voodoo2) ? FXTRUE : FXFALSE;
    }
    
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grSstQueryBoards(GrHwConfiguration* hwconfig) {
    return grSstQueryHardware(hwconfig);
}

FX_ENTRY void FX_CALL grGlideGetVersion(char version[80]) {
    GLIDE_LOG(DEBUG, "Frontend", "grGlideGetVersion invoked.");
    if (version) {
        std::strncpy(version, "2.43 (glide-ng)", 80);
        version[79] = '\0';
    }
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst) {
    GLIDE_LOG(DEBUG, "Frontend", "grSstSelect invoked for SST=" << which_sst);
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname) {
    GLIDE_LOG(DEBUG, "Frontend", "grGetString invoked for pname: " << pname);
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    switch (pname) {
        case GR_VERSION:  return "2.43 (glide-ng)";
        case GR_HARDWARE: return "Voodoo2 (Modernized Wrapper Emulation)";
        case GR_VENDOR:   return reg.GetConfig().reportedVendorOverride.c_str();
        case GR_RENDERER: {
            if (reg.GetConfig().backend == "opengl_es") {
                return "3dfx glide-ng OpenGL ES Wrapper";
            } else if (reg.GetConfig().backend == "software") {
                return "3dfx glide-ng Software Wrapper";
            }
            return "3dfx glide-ng Vulkan Wrapper";
        }
        case GR_EXTENSION:return "";
        default: return "Unknown";
    }
}

FX_ENTRY void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) {
    GLIDE_LOG(DEBUG, "Frontend", "grClipWindow invoked: " << minx << "," << miny << " to " << maxx << "," << maxy);
    s_clipMinX = minx;
    s_clipMinY = miny;
    s_clipMaxX = maxx;
    s_clipMaxY = maxy;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetClipWindow(minx, miny, maxx, maxy);
    }
}

FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexMinAddress invoked for TMU=" << tmu);
    return 0;
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexMaxAddress invoked for TMU=" << tmu);
    return 0x200000;
}

FX_ENTRY FxU32 FX_CALL grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexCalcMemRequired invoked.");
    return GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(lodmin, lodmax, aspect, fmt);
}

FX_ENTRY void FX_CALL grGlideGetState(GrState* state) {
    GLIDE_LOG(DEBUG, "Frontend", "grGlideGetState invoked.");
    if (!state) return;
    auto* ws = reinterpret_cast<GlideWrapperState*>(state);
    ws->depthMode = s_depthMode;
    ws->depthCompare = s_depthCompare;
    ws->depthMask = s_depthMask ? 1 : 0;
    ws->depthBias = s_depthBias;
    ws->alphaTestOp = s_alphaTestOp;
    ws->alphaTestRef = s_alphaTestRef;
    std::memcpy(ws->clampS, s_clampS, sizeof(s_clampS));
    std::memcpy(ws->clampT, s_clampT, sizeof(s_clampT));
    std::memcpy(ws->minFilter, s_minFilter, sizeof(s_minFilter));
    std::memcpy(ws->magFilter, s_magFilter, sizeof(s_magFilter));
    ws->colorFunc = s_colorFunc;
    ws->colorFactor = s_colorFactor;
    ws->colorLocal = s_colorLocal;
    ws->colorOther = s_colorOther;
    ws->colorInvert = s_colorInvert ? 1 : 0;
    ws->alphaFunc = s_alphaFunc;
    ws->alphaFactor = s_alphaFactor;
    ws->alphaLocal = s_alphaLocal;
    ws->alphaOther = s_alphaOther;
    ws->alphaInvert = s_alphaInvert ? 1 : 0;
    std::memcpy(ws->texRgbFunc, s_texRgbFunc, sizeof(s_texRgbFunc));
    std::memcpy(ws->texRgbFactor, s_texRgbFactor, sizeof(s_texRgbFactor));
    std::memcpy(ws->texAlphaFunc, s_texAlphaFunc, sizeof(s_texAlphaFunc));
    std::memcpy(ws->texAlphaFactor, s_texAlphaFactor, sizeof(s_texAlphaFactor));
    std::memcpy(ws->boundTex, s_boundTex, sizeof(s_boundTex));
    ws->stwHintMask = s_stwHintMask;
    ws->constantColor = s_constantColor;
    ws->rgbSrcBlend = s_rgbSrcBlend;
    ws->rgbDstBlend = s_rgbDstBlend;
    ws->alphaSrcBlend = s_alphaSrcBlend;
    ws->alphaDstBlend = s_alphaDstBlend;
    ws->cullMode = s_cullMode;
    ws->sstOrigin = s_sstOrigin;
    ws->clipMinX = s_clipMinX;
    ws->clipMinY = s_clipMinY;
    ws->clipMaxX = s_clipMaxX;
    ws->clipMaxY = s_clipMaxY;
    ws->fogMode = s_fogMode;
    ws->fogColor = s_fogColor;
    std::memcpy(ws->fogTable, s_fogTable, sizeof(s_fogTable));
    ws->colorMaskRgb = s_colorMaskRgb ? 1 : 0;
    ws->colorMaskAlpha = s_colorMaskAlpha ? 1 : 0;
}

FX_ENTRY void FX_CALL grGlideSetState(const GrState* state) {
    GLIDE_LOG(DEBUG, "Frontend", "grGlideSetState invoked.");
    if (!state) return;
    const auto* ws = reinterpret_cast<const GlideWrapperState*>(state);
    
    s_depthMode = ws->depthMode;
    s_depthCompare = ws->depthCompare;
    s_depthMask = ws->depthMask != 0;
    s_depthBias = ws->depthBias;
    s_alphaTestOp = ws->alphaTestOp;
    s_alphaTestRef = ws->alphaTestRef;
    std::memcpy(s_clampS, ws->clampS, sizeof(s_clampS));
    std::memcpy(s_clampT, ws->clampT, sizeof(s_clampT));
    std::memcpy(s_minFilter, ws->minFilter, sizeof(s_minFilter));
    std::memcpy(s_magFilter, ws->magFilter, sizeof(s_magFilter));
    s_colorFunc = ws->colorFunc;
    s_colorFactor = ws->colorFactor;
    s_colorLocal = ws->colorLocal;
    s_colorOther = ws->colorOther;
    s_colorInvert = ws->colorInvert != 0;
    s_alphaFunc = ws->alphaFunc;
    s_alphaFactor = ws->alphaFactor;
    s_alphaLocal = ws->alphaLocal;
    s_alphaOther = ws->alphaOther;
    s_alphaInvert = ws->alphaInvert != 0;
    std::memcpy(s_texRgbFunc, ws->texRgbFunc, sizeof(s_texRgbFunc));
    std::memcpy(s_texRgbFactor, ws->texRgbFactor, sizeof(s_texRgbFactor));
    std::memcpy(s_texAlphaFunc, ws->texAlphaFunc, sizeof(s_texAlphaFunc));
    std::memcpy(s_texAlphaFactor, ws->texAlphaFactor, sizeof(s_texAlphaFactor));
    std::memcpy(s_boundTex, ws->boundTex, sizeof(s_boundTex));
    s_stwHintMask = ws->stwHintMask;
    GlideWrapper::VertexLayout::GetInstance().SetSTWHintMask(s_stwHintMask);
    s_constantColor = ws->constantColor;
    s_rgbSrcBlend = ws->rgbSrcBlend;
    s_rgbDstBlend = ws->rgbDstBlend;
    s_alphaSrcBlend = ws->alphaSrcBlend;
    s_alphaDstBlend = ws->alphaDstBlend;
    s_cullMode = ws->cullMode;
    s_sstOrigin = ws->sstOrigin;
    s_clipMinX = ws->clipMinX;
    s_clipMinY = ws->clipMinY;
    s_clipMaxX = ws->clipMaxX;
    s_clipMaxY = ws->clipMaxY;
    s_fogMode = ws->fogMode;
    s_fogColor = ws->fogColor;
    std::memcpy(s_fogTable, ws->fogTable, sizeof(s_fogTable));
    s_colorMaskRgb = ws->colorMaskRgb != 0;
    s_colorMaskAlpha = ws->colorMaskAlpha != 0;

    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_colorLocal, s_colorOther, s_colorInvert,
                           s_alphaFunc, s_alphaFactor, s_alphaLocal, s_alphaOther, s_alphaInvert);
        b->SetBlendState(s_rgbSrcBlend, s_rgbDstBlend, s_alphaSrcBlend, s_alphaDstBlend);
        b->SetCullState(s_cullMode);
        b->SetSstOrigin(s_sstOrigin);
        b->SetClipWindow(s_clipMinX, s_clipMinY, s_clipMaxX, s_clipMaxY);
        b->SetColorMask(s_colorMaskRgb, s_colorMaskAlpha);
        b->SetConstantColor(s_constantColor);
        b->SetFogMode(s_fogMode);
        b->SetFogColor(s_fogColor);
        b->SetFogTable(s_fogTable);
        b->SetSTWHintState(s_stwHintMask);

        for (uint32_t tmu = 0; tmu < 2; ++tmu) {
            b->SetTexCombinerMode(tmu, s_texRgbFunc[tmu], s_texRgbFactor[tmu], s_texAlphaFunc[tmu], s_texAlphaFactor[tmu]);
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t fnc, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert) {
    GLIDE_LOG(DEBUG, "Frontend", "grColorCombine invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local << ", Other=" << other << ", Invert=" << invert);
    s_colorFunc   = fnc;
    s_colorFactor = factor;
    s_colorLocal  = local;
    s_colorOther  = other;
    s_colorInvert = invert != 0;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_colorLocal, s_colorOther, s_colorInvert,
                           s_alphaFunc, s_alphaFactor, s_alphaLocal, s_alphaOther, s_alphaInvert);
    }
}

FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t fnc, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert) {
    GLIDE_LOG(DEBUG, "Frontend", "grAlphaCombine invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local << ", Other=" << other << ", Invert=" << invert);
    s_alphaFunc   = fnc;
    s_alphaFactor = factor;
    s_alphaLocal  = local;
    s_alphaOther  = other;
    s_alphaInvert = invert != 0;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_colorLocal, s_colorOther, s_colorInvert,
                           s_alphaFunc, s_alphaFactor, s_alphaLocal, s_alphaOther, s_alphaInvert);
    }
}

FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgbFnc, GrCombineFactor_t rgbFactor, GrCombineFunction_t alphaFnc, GrCombineFactor_t alphaFactor, FxBool rgbInvert, FxBool alphaInvert) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexCombine invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_texRgbFunc[tmu]    = rgbFnc;
        s_texRgbFactor[tmu]  = rgbFactor;
        s_texAlphaFunc[tmu]  = alphaFnc;
        s_texAlphaFactor[tmu]= alphaFactor;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->SetTexCombinerMode(tmu, rgbFnc, rgbFactor, alphaFnc, alphaFactor);
        }
    }
}

FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgbSf, GrAlphaBlendFnc_t rgbDf, GrAlphaBlendFnc_t alphaSf, GrAlphaBlendFnc_t alphaDf) {
    GLIDE_LOG(DEBUG, "Frontend", "grAlphaBlendFunction invoked.");
    s_rgbSrcBlend = rgbSf;
    s_rgbDstBlend = rgbDf;
    s_alphaSrcBlend = alphaSf;
    s_alphaDstBlend = alphaDf;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetBlendState(rgbSf, rgbDf, alphaSf, alphaDf);
    }
}

FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(DEBUG, "Frontend", "grAlphaTestFunction invoked.");
    s_alphaTestOp = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t refVal) {
    GLIDE_LOG(DEBUG, "Frontend", "grAlphaTestReferenceValue invoked: " << static_cast<int>(refVal));
    s_alphaTestRef = refVal;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter, GrTextureFilterMode_t magfilter) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexFilterMode invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_minFilter[tmu] = minfilter;
        s_magFilter[tmu] = magfilter;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexClampMode invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_clampS[tmu] = s_clampmode;
        s_clampT[tmu] = t_clampmode;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexMipMapMode invoked for TMU=" << tmu);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode) {
    GLIDE_LOG(DEBUG, "Frontend", "grDepthBufferMode invoked: " << mode);
    s_depthMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(DEBUG, "Frontend", "grDepthBufferFunction invoked.");
    s_depthCompare = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthMask(FxBool mask) {
    GLIDE_LOG(DEBUG, "Frontend", "grDepthMask invoked: " << mask);
    s_depthMask = mask;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBiasLevel(FxI16 level) {
    GLIDE_LOG(DEBUG, "Frontend", "grDepthBiasLevel invoked: " << level);
    s_depthBias = level;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin) {
    GLIDE_LOG(DEBUG, "Frontend", "grSstOrigin invoked: " << origin);
    s_sstOrigin = origin;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetSstOrigin(origin);
    }
}

FX_ENTRY void FX_CALL grSstIdle(void) {
    GLIDE_LOG(DEBUG, "Frontend", "grSstIdle invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SstIdle();
    }
}

FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode) {
    GLIDE_LOG(DEBUG, "Frontend", "grCullMode invoked: " << mode);
    s_cullMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCullState(mode);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexDownloadMipMap invoked for TMU=" << tmu);
    if (info && info->data) {
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, info->smallLod, info->largeLod, info->aspectRatio, info->format, info->data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void* data) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexDownloadMipMapLevel invoked for TMU=" << tmu << ", LOD=" << thisLod);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, thisLod, thisLod, aspectRatio, format, data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void* data) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexDownloadTable invoked for TMU=" << tmu << ", Type=" << type);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadTable(tmu, type, data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadTablePartial(GrChipID_t tmu, GrTexTable_t type, void* data, int start, int end) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexDownloadTablePartial invoked for TMU=" << tmu << ", Type=" << type << ", range " << start << ".." << end);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadTable(tmu, type, data);
    }
}

FX_ENTRY void FX_CALL grTexNCCTable(GrChipID_t tmu, GrNCCTable_t table) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexNCCTable invoked for TMU=" << tmu << ", Table=" << table);
    GlideWrapper::TextureManager::GetInstance().SetActiveNccTable(tmu, table);
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexSource invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_boundTex[tmu] = startAddress;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, startAddress, s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexTextureMemRequired invoked");
    if (!info) return 0;
    return grTexCalcMemRequired(info->smallLod, info->largeLod, info->aspectRatio, info->format);
}

FX_ENTRY void FX_CALL grTexDetailControl(GrChipID_t tmu, int lod_bias, FxU8 detail_scale, float detail_max) {
    GLIDE_LOG(DEBUG, "Frontend", "grTexDetailControl invoked for TMU=" << tmu << ", lod_bias=" << lod_bias << ", detail_scale=" << (int)detail_scale << ", detail_max=" << detail_max << " (Stubbed)");
}

FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t tmu, float bias) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexLodBiasValue: tmu=" << tmu << ", bias=" << bias);
    s_texLodBias = bias;
}

FX_ENTRY void FX_CALL grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t func) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexCombineFunction: tmu=" << tmu << ", func=" << func);
    switch (func) {
        case GR_TEXTURECOMBINE_ZERO:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO,
                         GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_DECAL:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                         GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_OTHER:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_ADD:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_MULTIPLY:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_SUBTRACT:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
            break;
        case GR_TEXTURECOMBINE_ONE:
            grTexCombine(tmu, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO,
                         GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, FXTRUE, FXTRUE);
            break;
        default:
            GLIDE_LOG(WARN, "Frontend", "Unsupported grTexCombineFunction enum: " << func);
            break;
    }
}

FX_ENTRY void FX_CALL grTexMultibase(GrChipID_t tmu, FxBool enable) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexMultibase: tmu=" << tmu << ", enable=" << enable << " (Stubbed)");
}

FX_ENTRY void FX_CALL grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexMultibaseAddress: tmu=" << tmu << ", range=" << range << ", startAddress=" << startAddress << " (Stubbed)");
}

FX_ENTRY GrMipMapId_t FX_CALL guTexAllocateMemory(
    GrChipID_t tmu,
    FxU8 odd_even_mask,
    int width, int height,
    GrTextureFormat_t fmt,
    GrMipMapMode_t mm_mode,
    GrLOD_t smallest_lod, GrLOD_t largest_lod,
    GrAspectRatio_t aspect,
    GrTextureClampMode_t s_clamp_mode,
    GrTextureClampMode_t t_clamp_mode,
    GrTextureFilterMode_t minfilter_mode,
    GrTextureFilterMode_t magfilter_mode,
    float lod_bias,
    FxBool trilinear)
{
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexAllocateMemory: tmu=" << tmu << ", dim=" << width << "x" << height << ", format=" << fmt);
    
    GrMipMapInfo tex;
    std::memset(&tex, 0, sizeof(GrMipMapInfo));
    tex.valid = FXTRUE;
    tex.width = width;
    tex.height = height;
    tex.aspect_ratio = aspect;
    tex.format = fmt;
    tex.mipmap_mode = mm_mode;
    tex.magfilter_mode = magfilter_mode;
    tex.minfilter_mode = minfilter_mode;
    tex.s_clamp_mode = s_clamp_mode;
    tex.t_clamp_mode = t_clamp_mode;
    tex.lod_bias = static_cast<FxU32>(lod_bias);
    tex.lod_min = smallest_lod;
    tex.lod_max = largest_lod;
    tex.tmu = tmu;
    tex.odd_even_mask = odd_even_mask;
    tex.trilinear = trilinear;

    // Calculate VRAM size required and allocate
    uint32_t bytesRequired = grTexCalcMemRequired(smallest_lod, largest_lod, aspect, fmt);
    tex.tmu_base_address = s_guTexNextAddress;
    s_guTexNextAddress += bytesRequired;

    s_guTextures.push_back(tex);
    GrMipMapId_t id = static_cast<GrMipMapId_t>(s_guTextures.size() - 1);
    return id;
}

FX_ENTRY void FX_CALL guTexSource(GrMipMapId_t id) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexSource: id=" << id);
    if (id == GR_NULL_MIPMAP_HANDLE || id >= (GrMipMapId_t)s_guTextures.size() || !s_guTextures[id].valid) {
        return;
    }
    
    auto& tex = s_guTextures[id];
    GrTexInfo info;
    info.smallLod = tex.lod_min;
    info.largeLod = tex.lod_max;
    info.aspectRatio = tex.aspect_ratio;
    info.format = tex.format;
    info.data = tex.data;

    grTexSource(tex.tmu, tex.tmu_base_address, tex.odd_even_mask, &info);
    grTexClampMode(tex.tmu, tex.s_clamp_mode, tex.t_clamp_mode);
    grTexFilterMode(tex.tmu, tex.minfilter_mode, tex.magfilter_mode);
    grTexMipMapMode(tex.tmu, tex.mipmap_mode, tex.trilinear);

    s_currentMipMap[tex.tmu] = id;
}

FX_ENTRY void FX_CALL guTexDownloadMipMap(GrMipMapId_t id, const void *src, const GuNccTable *table) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexDownloadMipMap: id=" << id);
    if (id == GR_NULL_MIPMAP_HANDLE || id >= (GrMipMapId_t)s_guTextures.size() || !s_guTextures[id].valid) {
        return;
    }
    
    auto& tex = s_guTextures[id];
    tex.data = const_cast<void*>(src);
    if (table) {
        tex.ncc_table = *table;
    }

    GrTexInfo info;
    info.smallLod = tex.lod_min;
    info.largeLod = tex.lod_max;
    info.aspectRatio = tex.aspect_ratio;
    info.format = tex.format;
    info.data = tex.data;

    grTexDownloadMipMap(tex.tmu, tex.tmu_base_address, tex.odd_even_mask, &info);
}

FX_ENTRY FxBool FX_CALL guTexChangeAttributes(
    GrMipMapId_t id,
    int width, int height,
    GrTextureFormat_t fmt,
    GrMipMapMode_t mm_mode,
    GrLOD_t smallest_lod, GrLOD_t largest_lod,
    GrAspectRatio_t aspect,
    GrTextureClampMode_t s_clamp_mode,
    GrTextureClampMode_t t_clamp_mode,
    GrTextureFilterMode_t minfilter_mode,
    GrTextureFilterMode_t magfilter_mode)
{
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexChangeAttributes: id=" << id);
    if (id == GR_NULL_MIPMAP_HANDLE || id >= (GrMipMapId_t)s_guTextures.size() || !s_guTextures[id].valid) {
        return FXFALSE;
    }
    
    auto& tex = s_guTextures[id];
    tex.width = width;
    tex.height = height;
    tex.format = fmt;
    tex.mipmap_mode = mm_mode;
    tex.lod_min = smallest_lod;
    tex.lod_max = largest_lod;
    tex.aspect_ratio = aspect;
    tex.s_clamp_mode = s_clamp_mode;
    tex.t_clamp_mode = t_clamp_mode;
    tex.minfilter_mode = minfilter_mode;
    tex.magfilter_mode = magfilter_mode;

    return FXTRUE;
}

FX_ENTRY GrMipMapId_t FX_CALL guTexGetCurrentMipMap(GrChipID_t tmu) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexGetCurrentMipMap: tmu=" << tmu);
    if (tmu < 0 || tmu > 1) return GR_NULL_MIPMAP_HANDLE;
    return s_currentMipMap[tmu];
}

FX_ENTRY GrMipMapInfo* FX_CALL guTexGetMipMapInfo(GrMipMapId_t id) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "guTexGetMipMapInfo: id=" << id);
    if (id == GR_NULL_MIPMAP_HANDLE || id >= (GrMipMapId_t)s_guTextures.size() || !s_guTextures[id].valid) {
        return nullptr;
    }
    return &s_guTextures[id];
}

FX_ENTRY void FX_CALL guTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t func) {
    grTexCombineFunction(tmu, func);
}

FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grConstantColorValue invoked: " << std::hex << value << std::dec);
    s_constantColor = value;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetConstantColor(value);
    }
}

FX_ENTRY void FX_CALL grConstantColorValue4(float a, float r, float g, float b) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grConstantColorValue4 invoked: a=" << a << ", r=" << r << ", g=" << g << ", b=" << b);
    uint32_t aInt = static_cast<uint32_t>(a + 0.5f);
    uint32_t rInt = static_cast<uint32_t>(r + 0.5f);
    uint32_t gInt = static_cast<uint32_t>(g + 0.5f);
    uint32_t bInt = static_cast<uint32_t>(b + 0.5f);
    aInt = std::max(0u, std::min(255u, aInt));
    rInt = std::max(0u, std::min(255u, rInt));
    gInt = std::max(0u, std::min(255u, gInt));
    bInt = std::max(0u, std::min(255u, bInt));
    uint32_t packed = (aInt << 24) | (rInt << 16) | (gInt << 8) | bInt;
    grConstantColorValue(packed);
}

FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grColorMask invoked: rgb=" << rgb << ", a=" << a);
    s_colorMaskRgb = (rgb != 0);
    s_colorMaskAlpha = (a != 0);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetColorMask(s_colorMaskRgb, s_colorMaskAlpha);
    }
}

FX_ENTRY void FX_CALL grDisableAllEffects(void) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grDisableAllEffects invoked");
    s_rgbSrcBlend = 4;   // GR_BLEND_ONE
    s_rgbDstBlend = 0;   // GR_BLEND_ZERO
    s_alphaSrcBlend = 4; // GR_BLEND_ONE
    s_alphaDstBlend = 0; // GR_BLEND_ZERO
    s_depthCompare = 1;  // GR_CMP_ALWAYS
    s_depthMask = false;
    s_alphaTestOp = 7;   // GR_CMP_ALWAYS
    s_fogMode = 0;       // GR_FOG_DISABLE
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetBlendState(s_rgbSrcBlend, s_rgbDstBlend, s_alphaSrcBlend, s_alphaDstBlend);
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
        b->SetFogMode(s_fogMode);
        b->SetChromakeyMode(0); // Disable chromakey
    }
}

FX_ENTRY void FX_CALL grGlideShamelessPlug(const FxBool on) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grGlideShamelessPlug invoked: on=" << on);
    s_shamelessPlugEnabled = (on != 0);
}

FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t srcBuf, FxU32 srcX, FxU32 srcY, FxU32 srcWidth, FxU32 srcHeight, FxU32 dstStride, void* dstData) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbReadRegion invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->ReadLFB(srcBuf, srcX, srcY, srcWidth, srcHeight, dstStride, dstData) ? FXTRUE : FXFALSE;
    }
    if (dstData) std::memset(dstData, 0, srcHeight * dstStride);
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dstBuf, FxU32 dstX, FxU32 dstY, GrLfbSrcFmt_t srcFmt, FxU32 srcWidth, FxU32 srcHeight, FxI32 srcStride, void* srcData) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbWriteRegion invoked: " << srcWidth << "x" << srcHeight);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->WriteLFB(dstBuf, dstX, dstY, srcWidth, srcHeight, srcStride, srcFmt, srcData) ? FXTRUE : FXFALSE;
    }
    return FXTRUE;
}

struct LfbLockState {
    bool isLocked{false};
    GrLock_t type;
    GrBuffer_t buffer;
    GrLfbWriteMode_t writeMode;
    GrOriginLocation_t origin;
    bool pixelPipeline{false};
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> stagingBuffer;
    std::vector<uint8_t> originalBuffer;
    uint32_t stride{0};
    uint32_t pixelSize{0};
};

static LfbLockState g_lfbLock;

static void FlipRows(uint8_t* data, uint32_t height, uint32_t stride) {
    std::vector<uint8_t> tempRow(stride);
    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* rowTop = data + y * stride;
        uint8_t* rowBottom = data + (height - 1 - y) * stride;
        std::memcpy(tempRow.data(), rowTop, stride);
        std::memcpy(rowTop, rowBottom, stride);
        std::memcpy(rowBottom, tempRow.data(), stride);
    }
}

FX_ENTRY void FX_CALL grLfbConstantAlpha(FxU8 alpha) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbConstantAlpha: " << (int)alpha);
    s_lfbConstantAlpha = alpha;
}

FX_ENTRY void FX_CALL grLfbWriteColorFormat(GrColorFormat_t format) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbWriteColorFormat: " << format);
    s_lfbWriteColorFormat = format;
}

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t* info) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbLock invoked: buffer=" << buffer << ", mode=" << writeMode << ", origin=" << origin);
    
    if (!info) return FXFALSE;
    
    auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!b) return FXFALSE;

    g_lfbLock.width = b->GetWidth();
    g_lfbLock.height = b->GetHeight();
    
    // Resolve writeMode. Glide 2.x applications using ANY expect 16-bit 565 natively
    GrLfbWriteMode_t resolvedMode = writeMode;
    if (resolvedMode == GR_LFBWRITEMODE_ANY) {
        resolvedMode = GR_LFBWRITEMODE_565;
    }

    // Resolve pixel size
    if (resolvedMode == GR_LFBWRITEMODE_565 || 
        resolvedMode == GR_LFBWRITEMODE_555 || 
        resolvedMode == GR_LFBWRITEMODE_1555 || 
        resolvedMode == GR_LFBWRITEMODE_565_DEPTH || 
        resolvedMode == GR_LFBWRITEMODE_555_DEPTH || 
        resolvedMode == GR_LFBWRITEMODE_1555_DEPTH || 
        resolvedMode == GR_LFBWRITEMODE_ZA16) {
        g_lfbLock.pixelSize = 2;
    } else if (resolvedMode == GR_LFBWRITEMODE_888) {
        g_lfbLock.pixelSize = 3;
    } else if (resolvedMode == GR_LFBWRITEMODE_8888) {
        g_lfbLock.pixelSize = 4;
    } else {
        GLIDE_LOG(CRITICAL, "Frontend", "grLfbLock: Unsupported writeMode=" << writeMode);
        return FXFALSE;
    }

    g_lfbLock.stride = g_lfbLock.width * g_lfbLock.pixelSize;
    g_lfbLock.stagingBuffer.resize(g_lfbLock.stride * g_lfbLock.height);
    
    // Always pre-populate the staging buffer with the current pixels from the backend to preserve unwritten pixels!
    // Read from backend (ReadLFB always reads top-down)
    b->ReadLFB(buffer, 0, 0, g_lfbLock.width, g_lfbLock.height, g_lfbLock.stride, g_lfbLock.stagingBuffer.data());
    
    // Capture the original top-down buffer for dirty pixel tracking!
    g_lfbLock.originalBuffer = g_lfbLock.stagingBuffer;
    
    // If the application expects a LOWER_LEFT origin, we must vertically flip the rows!
    if (origin == GR_ORIGIN_LOWER_LEFT) {
        FlipRows(g_lfbLock.stagingBuffer.data(), g_lfbLock.height, g_lfbLock.stride);
    }

    info->size = sizeof(GrLfbInfo_t);
    info->writeMode = resolvedMode;
    info->origin = origin;
    info->strideInBytes = g_lfbLock.stride;
    info->lfbPtr = g_lfbLock.stagingBuffer.data();

    g_lfbLock.isLocked = true;
    g_lfbLock.type = type;
    g_lfbLock.buffer = buffer;
    g_lfbLock.writeMode = resolvedMode;
    g_lfbLock.origin = origin;
    g_lfbLock.pixelPipeline = (pixelPipeline != 0);

    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbUnlock invoked.");
    
    if (!g_lfbLock.isLocked) {
        GLIDE_LOG(WARN, "Frontend", "grLfbUnlock: No active LFB lock found!");
        return FXFALSE;
    }

    auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!b) return FXFALSE;

    // If the application was writing, we must flush the staging buffer back to the backend!
    if (g_lfbLock.type == GR_LFB_WRITE_ONLY) {
        // If the application wrote using LOWER_LEFT origin, we must vertically flip the rows before flushing!
        if (g_lfbLock.origin == GR_ORIGIN_LOWER_LEFT) {
            FlipRows(g_lfbLock.stagingBuffer.data(), g_lfbLock.height, g_lfbLock.stride);
        }

        // Map writeMode to GrLfbSrcFmt_t
        uint32_t srcFmt = 0; // Default to 565
        if (g_lfbLock.writeMode == GR_LFBWRITEMODE_565) srcFmt = 0;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_555) srcFmt = 1;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_1555) srcFmt = 2;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_888) srcFmt = 4;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_8888) srcFmt = 5;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_565_DEPTH) srcFmt = 12;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_555_DEPTH) srcFmt = 13;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_1555_DEPTH) srcFmt = 14;
        else if (g_lfbLock.writeMode == GR_LFBWRITEMODE_ZA16) srcFmt = 15;

        // Construct pipeline configuration for the backend to simulate blending & chroma keying
        GlideWrapper::LfbPipelineConfig config;
        config.pixelPipeline = g_lfbLock.pixelPipeline;
        config.chromakeyEnabled = (s_chromakeyMode != 0);
        config.chromakeyValue = s_chromakeyValue;
        config.constantAlpha = s_lfbConstantAlpha;
        config.rgbSrcBlend = s_rgbSrcBlend;
        config.rgbDstBlend = s_rgbDstBlend;

        // Flush to backend with dirty tracking comparison and pipeline configuration!
        b->WriteLFB(g_lfbLock.buffer, 0, 0, g_lfbLock.width, g_lfbLock.height, g_lfbLock.stride, srcFmt, g_lfbLock.stagingBuffer.data(), g_lfbLock.originalBuffer.data(), config);
    }

    // Free staging buffer memory
    g_lfbLock.stagingBuffer.clear();
    g_lfbLock.stagingBuffer.shrink_to_fit();
    g_lfbLock.isLocked = false;

    return FXTRUE;
}

static bool Read3dfHeader(FILE* fp, char* buffer, size_t bufSize, char* version, char* formatStr, int* smallLod, int* largeLod, int* aspectW, int* aspectH) {
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        if (!fgets(buffer + pos, bufSize - pos, fp)) return false;
        pos += std::strlen(buffer + pos);
    }
    
    int parsed = std::sscanf(buffer, "3df v%15s\n%31s\nlod range: %d %d\naspect ratio: %d %d\n",
                             version, formatStr, smallLod, largeLod, aspectW, aspectH);
    if (parsed != 6) {
        parsed = std::sscanf(buffer, "3df v%15s %31s lod range: %d %d aspect ratio: %d %d\n",
                             version, formatStr, smallLod, largeLod, aspectW, aspectH);
    }
    return (parsed == 6);
}

static uint16_t ReadBE16(FILE* fp) {
    int b1 = std::getc(fp);
    int b2 = std::getc(fp);
    if (b1 == EOF || b2 == EOF) return 0;
    return (static_cast<uint16_t>(b1) << 8) | b2;
}

static uint32_t ReadBE32(FILE* fp) {
    uint8_t bytes[4];
    if (std::fread(bytes, 1, 4, fp) != 4) return 0;
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8)  |
           bytes[3];
}

FX_ENTRY FxBool FX_CALL gu3dfGetInfo(const char* filename, Gu3dfInfo* info) {
    GLIDE_LOG(DEBUG, "Frontend", "gu3dfGetInfo invoked for: " << (filename ? filename : "unknown"));
    if (!filename || !info) return FXFALSE;

    FILE* fp = std::fopen(filename, "rb");
    if (!fp) {
        GLIDE_LOG(WARN, "Frontend", "gu3dfGetInfo: Failed to open " << filename);
        return FXFALSE;
    }

    char buffer[256] = {0};
    char version[16] = {0};
    char formatStr[32] = {0};
    int smallLod = 0, largeLod = 0, aspectW = 0, aspectH = 0;

    if (!Read3dfHeader(fp, buffer, sizeof(buffer), version, formatStr, &smallLod, &largeLod, &aspectW, &aspectH)) {
        GLIDE_LOG(WARN, "Frontend", "gu3dfGetInfo: Failed to parse header of " << filename);
        std::fclose(fp);
        return FXFALSE;
    }
    std::fclose(fp);

    for (int i = 0; formatStr[i]; ++i) {
        formatStr[i] = (char)std::toupper((unsigned char)formatStr[i]);
    }

    GrTextureFormat_t format = GR_TEXFMT_RGB_565;
    bool formatFound = false;
    struct FormatMap { const char* name; GrTextureFormat_t fmt; };
    FormatMap maps[] = {
        { "I8",       GR_TEXFMT_INTENSITY_8 },
        { "A8",       GR_TEXFMT_ALPHA_8 },
        { "RGB565",   GR_TEXFMT_RGB_565 },
        { "ARGB1555", GR_TEXFMT_ARGB_1555 },
        { "ARGB4444", GR_TEXFMT_ARGB_4444 },
        { "AI88",     GR_TEXFMT_ALPHA_INTENSITY_88 },
        { "P8",       GR_TEXFMT_P_8 },
        { "AP88",     GR_TEXFMT_AP_88 }
    };
    for (const auto& m : maps) {
        if (std::strcmp(formatStr, m.name) == 0) {
            format = m.fmt;
            formatFound = true;
            break;
        }
    }
    if (!formatFound) {
        GLIDE_LOG(WARN, "Frontend", "gu3dfGetInfo: Unsupported format " << formatStr);
        return FXFALSE;
    }

    GrAspectRatio_t aspect = GR_ASPECT_1x1;
    if (aspectW == 1 && aspectH == 1) aspect = GR_ASPECT_1x1;
    else if (aspectW == 1 && aspectH == 2) aspect = GR_ASPECT_1x2;
    else if (aspectW == 1 && aspectH == 4) aspect = GR_ASPECT_1x4;
    else if (aspectW == 1 && aspectH == 8) aspect = GR_ASPECT_1x8;
    else if (aspectW == 2 && aspectH == 1) aspect = GR_ASPECT_2x1;
    else if (aspectW == 4 && aspectH == 1) aspect = GR_ASPECT_4x1;
    else if (aspectW == 8 && aspectH == 1) aspect = GR_ASPECT_8x1;

    auto mapLod = [](int dim) -> GrLOD_t {
        if (dim <= 1) return GR_LOD_1;
        if (dim <= 2) return GR_LOD_2;
        if (dim <= 4) return GR_LOD_4;
        if (dim <= 8) return GR_LOD_8;
        if (dim <= 16) return GR_LOD_16;
        if (dim <= 32) return GR_LOD_32;
        if (dim <= 64) return GR_LOD_64;
        if (dim <= 128) return GR_LOD_128;
        return GR_LOD_256;
    };

    info->header.format = format;
    info->header.aspect_ratio = aspect;
    info->header.small_lod = mapLod(smallLod);
    info->header.large_lod = mapLod(largeLod);
    
    if (aspectW >= aspectH) {
        info->header.width = largeLod;
        info->header.height = largeLod / aspectW;
    } else {
        info->header.height = largeLod;
        info->header.width = largeLod / aspectH;
    }

    uint32_t totalBytes = 0;
    int bytesPerTexel = (format >= GR_TEXFMT_16BIT) ? 2 : 1;
    for (int lod = info->header.large_lod; lod <= info->header.small_lod; ++lod) {
        int lDim = 256 >> lod;
        int w = lDim, h = lDim;
        if (aspectW >= aspectH) {
            h = std::max(1, lDim / aspectW);
        } else {
            w = std::max(1, lDim / aspectH);
        }
        totalBytes += w * h * bytesPerTexel;
    }
    info->mem_required = totalBytes;

    GLIDE_LOG(DEBUG, "Frontend", "gu3dfGetInfo: parsed " << filename << " successfully. Format=" << formatStr 
                                << " Aspect=" << aspectW << "x" << aspectH << " LargeLOD=" << largeLod << " MemRequired=" << totalBytes);
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL gu3dfLoad(const char* filename, Gu3dfInfo* info) {
    GLIDE_LOG(DEBUG, "Frontend", "gu3dfLoad invoked for: " << (filename ? filename : "unknown"));
    if (!filename || !info) return FXFALSE;

    if (!gu3dfGetInfo(filename, info)) return FXFALSE;

    FILE* fp = std::fopen(filename, "rb");
    if (!fp) return FXFALSE;

    char buffer[256] = {0};
    char version[16] = {0};
    char formatStr[32] = {0};
    int smallLod = 0, largeLod = 0, aspectW = 0, aspectH = 0;
    Read3dfHeader(fp, buffer, sizeof(buffer), version, formatStr, &smallLod, &largeLod, &aspectW, &aspectH);

    if (info->header.format == GR_TEXFMT_P_8) {
        for (int i = 0; i < 256; ++i) {
            info->table.palette.data[i] = ReadBE32(fp);
        }
        GLIDE_LOG(DEBUG, "Frontend", "gu3dfLoad: Loaded 256 palette entries for P8 texture.");
    }

    if (!info->data) {
        GLIDE_LOG(WARN, "Frontend", "gu3dfLoad: Destination info->data is NULL! Caller must allocate memory.");
        std::fclose(fp);
        return FXFALSE;
    }

    uint8_t* dstBytes = reinterpret_cast<uint8_t*>(info->data);
    int bytesPerTexel = (info->header.format >= GR_TEXFMT_16BIT) ? 2 : 1;

    for (int lod = info->header.large_lod; lod <= info->header.small_lod; ++lod) {
        int lDim = 256 >> lod;
        int w = lDim, h = lDim;
        if (aspectW >= aspectH) {
            h = std::max(1, lDim / aspectW);
        } else {
            w = std::max(1, lDim / aspectH);
        }
        size_t pixelCount = w * h;

        if (bytesPerTexel == 1) {
            size_t readCount = std::fread(dstBytes, 1, pixelCount, fp);
            if (readCount != pixelCount) {
                GLIDE_LOG(WARN, "Frontend", "gu3dfLoad: Premature EOF reading 8-bit LOD level " << lod 
                                            << " (read " << readCount << "/" << pixelCount << "). Filling remainder with zero.");
                std::memset(dstBytes + readCount, 0, pixelCount - readCount);
            }
            dstBytes += pixelCount;
        } else {
            uint16_t* dstShorts = reinterpret_cast<uint16_t*>(dstBytes);
            bool hitEof = false;
            for (size_t p = 0; p < pixelCount; ++p) {
                if (hitEof) {
                    dstShorts[p] = 0;
                } else {
                    int b1 = std::getc(fp);
                    int b2 = std::getc(fp);
                    if (b1 == EOF || b2 == EOF) {
                        GLIDE_LOG(WARN, "Frontend", "gu3dfLoad: Premature EOF reading 16-bit LOD level " << lod 
                                                    << " at pixel " << p << "/" << pixelCount << ". Filling remainder with zero.");
                        dstShorts[p] = 0;
                        hitEof = true;
                    } else {
                        dstShorts[p] = (static_cast<uint16_t>(b1) << 8) | b2;
                    }
                }
            }
            dstBytes += pixelCount * 2;
        }
    }

    std::fclose(fp);
    GLIDE_LOG(DEBUG, "Frontend", "gu3dfLoad: Loaded " << filename << " successfully into memory.");
    return FXTRUE;
}

// ==========================================
// Phase 1: Fogging Entry Points & Utilities
// ==========================================

static const float tableIndexToW[64] = {
    1.000000f,      1.142857f,      1.333333f,      1.600000f, 
    2.000000f,      2.285714f,      2.666667f,      3.200000f, 
    4.000000f,      4.571429f,      5.333333f,      6.400000f, 
    8.000000f,      9.142858f,     10.666667f,     12.800000f, 
    16.000000f,     18.285715f,     21.333334f,     25.600000f, 
    32.000000f,     36.571430f,     42.666668f,     51.200001f, 
    64.000000f,     73.142860f,     85.333336f,    102.400002f, 
    128.000000f,    146.285721f,    170.666672f,    204.800003f, 
    256.000000f,    292.571442f,    341.333344f,    409.600006f, 
    512.000000f,    585.142883f,    682.666687f,    819.200012f, 
    1024.000000f,   1170.285767f,   1365.333374f,   1638.400024f, 
    2048.000000f,   2340.571533f,   2730.666748f,   3276.800049f, 
    4096.000000f,   4681.143066f,   5461.333496f,   6553.600098f, 
    8192.000000f,   9362.286133f,  10922.666992f,  13107.200195f, 
    16384.000000f,  18724.572266f,  21845.333984f,  26214.400391f, 
    32768.000000f,  37449.144531f,  43690.667969f,  52428.800781f
};

FX_ENTRY float FX_CALL guFogTableIndexToW(int i) {
    if (i < 0) i = 0;
    if (i >= 64) i = 63;
    return tableIndexToW[i];
}

FX_ENTRY void FX_CALL guFogGenerateExp(GrFog_t *fogtable, float density) {
    if (!fogtable) return;
    float dp = density * guFogTableIndexToW(63);
    float scale = 1.0f / (1.0f - std::exp(-dp));

    for (int i = 0; i < 64; ++i) {
        dp = density * guFogTableIndexToW(i);
        float f = (1.0f - std::exp(-dp)) * scale;
        f = std::clamp(f, 0.0f, 1.0f) * 255.0f;
        fogtable[i] = static_cast<GrFog_t>(f);
    }
}

FX_ENTRY void FX_CALL guFogGenerateExp2(GrFog_t *fogtable, float density) {
    if (!fogtable) return;
    float dp = density * guFogTableIndexToW(63);
    float scale = 1.0f / (1.0f - std::exp(-(dp * dp)));

    for (int i = 0; i < 64; ++i) {
        dp = density * guFogTableIndexToW(i);
        float f = (1.0f - std::exp(-(dp * dp))) * scale;
        f = std::clamp(f, 0.0f, 1.0f) * 255.0f;
        fogtable[i] = static_cast<GrFog_t>(f);
    }
}

FX_ENTRY void FX_CALL guFogGenerateLinear(GrFog_t *fogtable, float nearZ, float farZ) {
    if (!fogtable) return;
    for (int i = 0; i < 64; ++i) {
        float world_w = guFogTableIndexToW(i);
        world_w = std::clamp(world_w, nearZ, farZ);
        float f = (world_w - nearZ) / (farZ - nearZ);
        f = std::clamp(f, 0.0f, 1.0f) * 255.0f;
        fogtable[i] = static_cast<GrFog_t>(f);
    }
}

FX_ENTRY void FX_CALL grFogMode(GrFogMode_t mode) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grFogMode invoked: " << mode);
    s_fogMode = mode & (GR_FOG_WITH_TABLE | GR_FOG_WITH_ITERATED_ALPHA);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetFogMode(s_fogMode);
    }
}

FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fogcolor) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grFogColorValue invoked: " << std::hex << fogcolor << std::dec);
    s_fogColor = fogcolor;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetFogColor(s_fogColor);
    }
}

FX_ENTRY void FX_CALL grFogTable(const GrFog_t ft[]) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grFogTable invoked.");
    if (ft) {
        std::memcpy(s_fogTable, ft, sizeof(s_fogTable));
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->SetFogTable(s_fogTable);
        }
    }
}

FX_ENTRY FxU32 FX_CALL guEndianSwapWords( FxU32 value ) {
    return ( ( value & 0xFFFF0000 ) >> 16 ) | ( value << 16 );
}

FX_ENTRY FxU16 FX_CALL guEndianSwapBytes( FxU16 value ) {
    return ( ( value & 0xFF00 ) >> 8 ) | ( value << 8 );
}

FX_ENTRY void FX_CALL guAlphaSource( GrAlphaSource_t mode ) {
    GLIDE_LOG(DEBUG, "Frontend", "guAlphaSource invoked: " << mode);
    switch ( mode ) {
    case GR_ALPHASOURCE_CC_ALPHA:
        grAlphaCombine( GR_COMBINE_FUNCTION_LOCAL, 
                        GR_COMBINE_FACTOR_NONE, 
                        GR_COMBINE_LOCAL_CONSTANT, 
                        GR_COMBINE_OTHER_NONE, 
                        FXFALSE );
        break;

    case GR_ALPHASOURCE_ITERATED_ALPHA:
        grAlphaCombine( GR_COMBINE_FUNCTION_LOCAL, 
                        GR_COMBINE_FACTOR_NONE, 
                        GR_COMBINE_LOCAL_ITERATED, 
                        GR_COMBINE_OTHER_NONE, 
                        FXFALSE );
        break;

    case GR_ALPHASOURCE_TEXTURE_ALPHA:
        grAlphaCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, 
                        GR_COMBINE_FACTOR_ONE, 
                        GR_COMBINE_LOCAL_NONE, 
                        GR_COMBINE_OTHER_TEXTURE, 
                        FXFALSE );
        break;

    case GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA:
        grAlphaCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, 
                        GR_COMBINE_FACTOR_LOCAL, 
                        GR_COMBINE_LOCAL_ITERATED, 
                        GR_COMBINE_OTHER_TEXTURE, 
                        FXFALSE );
        break;

    default:
        GLIDE_LOG(WARN, "Frontend", "guAlphaSource: unknown alpha source mode: " << mode);
        break;
    }
}

FX_ENTRY void FX_CALL guColorCombineFunction( GrColorCombineFnc_t fnc ) {
    GLIDE_LOG(DEBUG, "Frontend", "guColorCombineFunction invoked: " << fnc);
    switch ( fnc )
    {
    case GR_COLORCOMBINE_ZERO:
        grColorCombine( GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, FXFALSE );
        break;

    case GR_COLORCOMBINE_CCRGB:
        grColorCombine( GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE );
        break;

    case GR_COLORCOMBINE_ITRGB_DELTA0:
    case GR_COLORCOMBINE_ITRGB:
        grColorCombine( GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE );
        break;

    case GR_COLORCOMBINE_DECAL_TEXTURE:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0:
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_ADD_ITRGB:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_TEXTURE_SUB_ITRGB:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA:
        grColorCombine( GR_COMBINE_FUNCTION_BLEND, GR_COMBINE_FACTOR_TEXTURE_ALPHA, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_ITERATED, FXFALSE );
        break;

    case GR_COLORCOMBINE_DIFF_SPEC_A:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_DIFF_SPEC_B:
        grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE );
        break;

    case GR_COLORCOMBINE_ONE:
        grColorCombine( GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, FXTRUE );
        break;
        
    default:
        GLIDE_LOG(WARN, "Frontend", "guColorCombineFunction: unsupported color combine function: " << fnc);
        break;
    }
}

FX_ENTRY int FX_CALL guEncodeRLE16( void *dst, void *src, FxU32 width, FxU32 height ) {
    int byteCount = 0;
    int sourceImageSizeInWords = width * height;
    FxU16 *srcPixels = reinterpret_cast<FxU16*>(src);
    FxU32 *dstPixels = reinterpret_cast<FxU32*>(dst);

    if ( dstPixels ) {
        while( sourceImageSizeInWords > 0 ) {
            FxU16 length    = 1;
            FxU16 color     = *srcPixels;
            int   lookAhead = 1;

            while( (sourceImageSizeInWords - length > 0) &&
                   (color == srcPixels[lookAhead]) ) {
                length++;
                lookAhead++;
            }

            *dstPixels = ((((FxU32)length)<<16) | ((FxU32)color));
            dstPixels++;
            byteCount += 4;
            srcPixels += length;
            sourceImageSizeInWords -= length;            
        }
    } else {
        while( sourceImageSizeInWords > 0 ) {
            FxU16 length    = 1;
            FxU16 color     = *srcPixels;
            int   lookAhead = 1;

            while( (sourceImageSizeInWords - length > 0) &&
                   (color == srcPixels[lookAhead]) ) {
                length++;
                lookAhead++;
            }

            byteCount += 4;
            srcPixels += length;
            sourceImageSizeInWords -= length;            
        }
    }
    return byteCount;
}

static void setlevel( FxU16 *data, FxU16 color, int width, int height ) {
   int s, t;
   for ( t = 0; t < height; t++ ) {
      for ( s = 0; s < width; s++ ) {
         *data = color;
          data++;
      }
   }
}

FX_ENTRY FxU16 * FX_CALL guTexCreateColorMipMap( void ) {
   FxU32 memrequired;
   FxU16 *data;
   FxU16 *start;

   GLIDE_LOG(DEBUG, "Frontend", "guTexCreateColorMipMap invoked.");
   memrequired = 2 * ( 256 * 256 + 128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1 * 1 );
   start = data = reinterpret_cast<FxU16*>(std::malloc( memrequired ));
   if ( !data )
      return nullptr;

   setlevel( data,            0xF800, 256, 256 ); // Red
   setlevel( data += 256*256, 0x07e0, 128, 128 ); // Green
   setlevel( data += 128*128, 0x001F, 64, 64 );   // Blue
   setlevel( data += 64*64,   0xFFFF, 32, 32);    // White
   setlevel( data += 32*32,   0x0000, 16, 16 );   // Black
   setlevel( data += 16*16,   0xF800, 8, 8);      // Red
   setlevel( data += 8*8,     0x07e0, 4, 4 );     // Green
   setlevel( data += 4*4,     0x001f, 2, 2 );     // Blue
   setlevel( data += 2*2,     0xFFFF, 1, 1 );     // White

   return start;
}

FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grChromakeyMode invoked: mode=" << mode);
    s_chromakeyMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetChromakeyMode(mode);
    }
}

FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grChromakeyValue invoked: value=0x" << std::hex << value << std::dec);
    s_chromakeyValue = value;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetChromakeyValue(value);
    }
}

FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t mode) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grDitherMode invoked: mode=" << mode);
}

FX_ENTRY FxU32 FX_CALL grSstScreenWidth(void) {
    EnsureEngineInitialized();
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->GetWidth();
    }
    return 0;
}

FX_ENTRY FxU32 FX_CALL grSstScreenHeight(void) {
    EnsureEngineInitialized();
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->GetHeight();
    }
    return 0;
}

FX_ENTRY FxBool FX_CALL grSstIsBusy(void) {
    EnsureEngineInitialized();
    return FXFALSE;
}

FX_ENTRY FxU32 FX_CALL grSstStatus(void) {
    EnsureEngineInitialized();
    // Return status register bits indicating:
    // - PCI FIFO has plenty of room (0x3FF free slots)
    // - Card is not busy / rasterizer is idle
    return 0x3FF;
}

FX_ENTRY FxU32 FX_CALL grSstVideoLine(void) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    uint32_t height = backend ? backend->GetHeight() : 480;
    if (height == 0) height = 480;
    
    uint32_t totalLines = height + 30; // height + 30 VBLANK lines
    auto now = std::chrono::steady_clock::now();
    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - s_lastSwapTime).count();
    
    const uint64_t frameTimeUs = 16666; // 1/60 sec
    uint64_t currentFrameTimeUs = elapsedUs % frameTimeUs;
    
    return static_cast<uint32_t>((currentFrameTimeUs * totalLines) / frameTimeUs);
}

FX_ENTRY FxBool FX_CALL grSstVRetraceOn(void) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    uint32_t height = backend ? backend->GetHeight() : 480;
    if (height == 0) height = 480;
    
    return (grSstVideoLine() >= height) ? FXTRUE : FXFALSE;
}

FX_ENTRY int FX_CALL grBufferNumPending(void) {
    EnsureEngineInitialized();
    return 0;
}

FX_ENTRY void FX_CALL grCheckForRoom(FxI32 n) {
    EnsureEngineInitialized();
}

// Define the local typedef for GrChromaRangeMode_t which is guarded in glide2.h

// Forward declaration of our Chroma-Key Range Extension
FX_ENTRY void FX_CALL grChromakeyRangeExt(GrColor_t minColor, GrColor_t maxColor, GrChromaRangeMode_t mode);

FX_ENTRY GrProc FX_CALL grGetProcAddress(char *procName) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grGetProcAddress: " << (procName ? procName : "NULL"));
    if (!procName) return nullptr;

    if (std::strcmp(procName, "grChromakeyRangeExt") == 0) {
        return (GrProc)grChromakeyRangeExt;
    }

    GLIDE_LOG(WARN, "Frontend", "grGetProcAddress: Unknown extension " << procName);
    return nullptr;
}

FX_ENTRY void FX_CALL grChromakeyRangeExt(GrColor_t minColor, GrColor_t maxColor, GrChromaRangeMode_t mode) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grChromakeyRangeExt: minColor=0x" << std::hex << minColor 
              << ", maxColor=0x" << maxColor << ", mode=" << std::dec << mode);
    s_chromakeyRangeMin = minColor;
    s_chromakeyRangeMax = maxColor;
    s_chromakeyRangeMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetChromakeyRange(minColor, maxColor, mode);
    }
}

FX_ENTRY void FX_CALL grErrorSetCallback(GrErrorCallbackFnc_t fnc) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grErrorSetCallback invoked");
    s_errorCallback = fnc;
}

FX_ENTRY void FX_CALL grLfbConstantDepth(FxU16 depth) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbConstantDepth: " << depth);
    s_lfbConstantDepth = depth;
}

FX_ENTRY void FX_CALL grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grLfbWriteColorSwizzle: swizzleBytes=" << swizzleBytes << ", swapWords=" << swapWords);
    s_lfbWriteColorSwizzle = (swizzleBytes != 0);
    s_lfbWriteColorSwizzleSwap = (swapWords != 0);
}

FX_ENTRY void FX_CALL grAlphaControlsITRGBLighting(FxBool enable) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grAlphaControlsITRGBLighting: " << enable);
    s_alphaControlsITRGBLighting = (enable != 0);
}

FX_ENTRY void FX_CALL grGammaCorrectionValue(float value) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grGammaCorrectionValue: " << value);
    s_gammaCorrectionValue = value;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetGamma(value);
    }
}

FX_ENTRY FxBool FX_CALL grSstControl(FxU32 code) {
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grSstControl invoked: code=" << code);
    return FXTRUE;
}

FX_ENTRY void FX_CALL grTexDownloadMipMapLevelPartial(
    GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
    GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
    void *data, int start, int end)
{
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "grTexDownloadMipMapLevelPartial invoked for TMU=" << tmu 
              << ", LOD=" << thisLod << ", rows=" << start << ".." << end << " (Promoting to full download)");
    // Promote to full level download for simplicity and visual correctness!
    grTexDownloadMipMapLevel(tmu, startAddress, thisLod, largeLod, aspectRatio, format, evenOdd, data);
}

FX_ENTRY void FX_CALL guDrawPolygonVertexListWithClip(int nverts, const GrVertex vlist[]) {
    // Delegate to GPU-native polygon drawer (GPU hardware handles viewport clipping automatically!)
    grDrawPolygonVertexList(nverts, vlist);
}

FX_ENTRY void FX_CALL ConvertAndDownloadRle(
    GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
    GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
    FxU8 *bm_data, long bm_h, FxU32 u0, FxU32 v0, FxU32 width, FxU32 height,
    FxU32 dest_width, FxU32 dest_height, FxU16 *tlut)
{
    EnsureEngineInitialized();
    GLIDE_LOG(DEBUG, "Frontend", "ConvertAndDownloadRle: TMU=" << tmu << ", Address=0x" << std::hex << startAddress);
    if (!bm_data) return;

    // Decompress 16-bit RLE texture pixels in RAM
    std::vector<uint16_t> decompressed(dest_width * dest_height, 0);
    Decompress3dfxRle(dest_width, dest_height, bm_data, decompressed.data());

    // Download the decompressed texture to GPU TMU memory
    grTexDownloadMipMapLevel(tmu, startAddress, thisLod, largeLod, aspectRatio, format, evenOdd, decompressed.data());
}

} // extern "C"
