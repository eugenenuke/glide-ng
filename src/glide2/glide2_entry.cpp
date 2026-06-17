#include "core/Logger.h"
#include "core/WrapperConfig.h"
#include "core/IConfigLoader.h"
#include "core/JsonConfigLoader.h"
#include "core/BackendManager.h"
#include "core/VertexLayout.h"
#include "core/TextureManager.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>

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
    uint32_t s_colorFunc   = 0;
    uint32_t s_colorFactor = 0;
    uint32_t s_colorLocal  = 0;
    uint32_t s_colorOther  = 0;
    bool     s_colorInvert = false;
    uint32_t s_alphaFunc   = 0;
    uint32_t s_alphaFactor = 0;
    uint32_t s_alphaLocal  = 0;
    uint32_t s_alphaOther  = 0;
    bool     s_alphaInvert = false;
    uint32_t s_texRgbFunc[2]    = {0, 0};
    uint32_t s_texRgbFactor[2]  = {0, 0};
    uint32_t s_texAlphaFunc[2]  = {0, 0};
    uint32_t s_texAlphaFactor[2]= {0, 0};
    uint32_t s_boundTex[2]      = {0, 0};

    uint32_t s_constantColor = 0xFFFFFFFF;
    uint32_t s_rgbSrcBlend = 4;   // GR_BLEND_ONE
    uint32_t s_rgbDstBlend = 0;   // GR_BLEND_ZERO
    uint32_t s_alphaSrcBlend = 4;
    uint32_t s_alphaDstBlend = 0;
    uint32_t s_cullMode = 0;      // GR_CULL_DISABLE
    uint32_t s_sstOrigin = 0;     // GR_ORIGIN_UPPER_LEFT
    uint32_t s_clipMinX = 0;
    uint32_t s_clipMinY = 0;
    uint32_t s_clipMaxX = 640;
    uint32_t s_clipMaxY = 480;
    uint32_t s_fogMode = 0;
    uint32_t s_fogColor = 0xff000000;
    uint8_t s_fogTable[64] = {0};
}

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
        bm.EstablishBackend(reg.GetConfig());
        auto& logger = GlideWrapper::Logger::GetInstance();
        logger.Initialize("glide-ng.log", static_cast<GlideWrapper::LogLevel>(reg.GetConfig().logLevel), reg.GetConfig().logToConsole);
        GLIDE_LOG(INFO, "Frontend", "Implicit grGlideInit executed seamlessly.");
    } else {
        bm.GetBackend()->PollEvents();
    }
}

FX_ENTRY void FX_CALL grGlideShutdown(void) {
    GLIDE_LOG(INFO, "Frontend", "grGlideShutdown (Glide 2.x) invoked.");
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
    GLIDE_LOG(INFO, "Frontend", "grSstWinOpen (Glide 2.x) invoked. Res=" << resolution << ", Buffers=" << nColBuffers);
    GlideWrapper::VertexLayout::GetInstance().ResetToGlide2Canonical();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend) return FXFALSE;
    
    // Normalize classic resolution defines to pixel dimensions
    uint32_t width = 800;
    uint32_t height = 600;
    if (resolution == GR_RESOLUTION_320x200) { width = 320; height = 200; }
    if (resolution == GR_RESOLUTION_320x240) { width = 320; height = 240; }
    if (resolution == GR_RESOLUTION_512x384) { width = 512; height = 384; }
    if (resolution == GR_RESOLUTION_640x400) { width = 640; height = 400; }
    if (resolution == GR_RESOLUTION_640x480) { width = 640; height = 480; }
    if (resolution == GR_RESOLUTION_800x600) { width = 800; height = 600; }
    if (resolution == GR_RESOLUTION_960x720) { width = 960; height = 720; }
    if (resolution == GR_RESOLUTION_1024x768) { width = 1024; height = 768; }
    
    backend->SetPixelFormat(format);
    bool attached = backend->AttachWindow(reinterpret_cast<void*>(static_cast<uintptr_t>(hWnd)), width, height, hWnd != 0);
    return attached ? FXTRUE : FXFALSE;
}

FX_ENTRY void FX_CALL grSstWinClose(void) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSstWinClose invoked.");
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->DetachWindow();
}

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->ClearBuffer(color, alpha, static_cast<float>(depth) / 65535.0f, 0x3);
}

FX_ENTRY void FX_CALL grBufferSwap(int swap_interval) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->SwapBuffers();
}

FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grRenderBuffer invoked for Buffer=" << buffer);
}

FX_ENTRY void FX_CALL grDrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !a || !b || !c) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawTriangle(layout.DecodeVertex(a), layout.DecodeVertex(b), layout.DecodeVertex(c));
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
    GLIDE_LOG(INFO, "Frontend", "grDrawPoint invoked!");
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !pt) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawPoint(layout.DecodeVertex(pt));
}

FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration* hwconfig) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSstQueryHardware invoked.");
    if (!hwconfig) return FXFALSE;

    hwconfig->num_sst = 1;
    hwconfig->SSTs[0].type = GR_SSTTYPE_Voodoo2;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.fbRam = 16;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.nTexelfx = 2;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.sliDetect = FXTRUE;
    
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grSstQueryBoards(GrHwConfiguration* hwconfig) {
    return grSstQueryHardware(hwconfig);
}

FX_ENTRY void FX_CALL grGlideGetVersion(char version[80]) {
    GLIDE_LOG(INFO, "Frontend", "grGlideGetVersion invoked.");
    if (version) {
        std::strncpy(version, "2.43 (glide-ng)", 80);
        version[79] = '\0';
    }
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst) {
    GLIDE_LOG(INFO, "Frontend", "grSstSelect invoked for SST=" << which_sst);
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname) {
    GLIDE_LOG(INFO, "Frontend", "grGetString invoked for pname: " << pname);
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
    GLIDE_LOG(INFO, "Frontend", "grClipWindow invoked: " << minx << "," << miny << " to " << maxx << "," << maxy);
    s_clipMinX = minx;
    s_clipMinY = miny;
    s_clipMaxX = maxx;
    s_clipMaxY = maxy;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetClipWindow(minx, miny, maxx, maxy);
    }
}

FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu) {
    GLIDE_LOG(INFO, "Frontend", "grTexMinAddress invoked for TMU=" << tmu);
    return 0;
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu) {
    GLIDE_LOG(INFO, "Frontend", "grTexMaxAddress invoked for TMU=" << tmu);
    return 0x200000;
}

FX_ENTRY FxU32 FX_CALL grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt) {
    GLIDE_LOG(INFO, "Frontend", "grTexCalcMemRequired invoked.");
    return GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(lodmin, lodmax, aspect, fmt);
}

FX_ENTRY void FX_CALL grGlideGetState(GrState* state) {
    GLIDE_LOG(INFO, "Frontend", "grGlideGetState invoked.");
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
}

FX_ENTRY void FX_CALL grGlideSetState(const GrState* state) {
    GLIDE_LOG(INFO, "Frontend", "grGlideSetState invoked.");
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

    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_colorLocal, s_colorOther, s_colorInvert,
                           s_alphaFunc, s_alphaFactor, s_alphaLocal, s_alphaOther, s_alphaInvert);
        b->SetBlendState(s_rgbSrcBlend, s_rgbDstBlend, s_alphaSrcBlend, s_alphaDstBlend);
        b->SetCullState(s_cullMode);
        b->SetClipWindow(s_clipMinX, s_clipMinY, s_clipMaxX, s_clipMaxY);
        b->SetConstantColor(s_constantColor);
        b->SetFogMode(s_fogMode);
        b->SetFogColor(s_fogColor);
        b->SetFogTable(s_fogTable);

        for (uint32_t tmu = 0; tmu < 2; ++tmu) {
            b->SetTexCombinerMode(tmu, s_texRgbFunc[tmu], s_texRgbFactor[tmu], s_texAlphaFunc[tmu], s_texAlphaFactor[tmu]);
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t fnc, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert) {
    GLIDE_LOG(INFO, "Frontend", "grColorCombine invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local << ", Other=" << other << ", Invert=" << invert);
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
    GLIDE_LOG(INFO, "Frontend", "grAlphaCombine invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local << ", Other=" << other << ", Invert=" << invert);
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
    GLIDE_LOG(INFO, "Frontend", "grTexCombine invoked for TMU=" << tmu);
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
    GLIDE_LOG(INFO, "Frontend", "grAlphaBlendFunction invoked.");
    s_rgbSrcBlend = rgbSf;
    s_rgbDstBlend = rgbDf;
    s_alphaSrcBlend = alphaSf;
    s_alphaDstBlend = alphaDf;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetBlendState(rgbSf, rgbDf, alphaSf, alphaDf);
    }
}

FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(INFO, "Frontend", "grAlphaTestFunction invoked.");
    s_alphaTestOp = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t refVal) {
    GLIDE_LOG(INFO, "Frontend", "grAlphaTestReferenceValue invoked: " << static_cast<int>(refVal));
    s_alphaTestRef = refVal;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter, GrTextureFilterMode_t magfilter) {
    GLIDE_LOG(INFO, "Frontend", "grTexFilterMode invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_minFilter[tmu] = minfilter;
        s_magFilter[tmu] = magfilter;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode) {
    GLIDE_LOG(INFO, "Frontend", "grTexClampMode invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_clampS[tmu] = s_clampmode;
        s_clampT[tmu] = t_clampmode;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend) {
    GLIDE_LOG(INFO, "Frontend", "grTexMipMapMode invoked for TMU=" << tmu);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBufferMode invoked: " << mode);
    s_depthMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBufferFunction invoked.");
    s_depthCompare = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthMask(FxBool mask) {
    GLIDE_LOG(INFO, "Frontend", "grDepthMask invoked: " << mask);
    s_depthMask = mask;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBiasLevel(FxI16 level) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBiasLevel invoked: " << level);
    s_depthBias = level;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin) {
    GLIDE_LOG(INFO, "Frontend", "grSstOrigin invoked: " << origin);
    s_sstOrigin = origin;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetSstOrigin(origin);
    }
}

FX_ENTRY void FX_CALL grSstIdle(void) {
    GLIDE_LOG(INFO, "Frontend", "grSstIdle invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SstIdle();
    }
}

FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode) {
    GLIDE_LOG(INFO, "Frontend", "grCullMode invoked: " << mode);
    s_cullMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCullState(mode);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadMipMap invoked for TMU=" << tmu);
    if (info && info->data) {
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, info->smallLod, info->largeLod, info->aspectRatio, info->format, info->data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void* data) {
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadMipMapLevel invoked for TMU=" << tmu << ", LOD=" << thisLod);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, thisLod, thisLod, aspectRatio, format, data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void* data) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadTable invoked for TMU=" << tmu << ", Type=" << type);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadTable(tmu, type, data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadTablePartial(GrChipID_t tmu, GrTexTable_t type, void* data, int start, int end) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadTablePartial invoked for TMU=" << tmu << ", Type=" << type << ", range " << start << ".." << end);
    if (data) {
        GlideWrapper::TextureManager::GetInstance().DownloadTable(tmu, type, data);
    }
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "grTexSource invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_boundTex[tmu] = startAddress;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, startAddress, s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grConstantColorValue invoked: " << std::hex << value << std::dec);
    s_constantColor = value;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetConstantColor(value);
    }
}

FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t srcBuf, FxU32 srcX, FxU32 srcY, FxU32 srcWidth, FxU32 srcHeight, FxU32 dstStride, void* dstData) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbReadRegion invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->ReadLFB(srcX, srcY, srcWidth, srcHeight, dstStride, dstData) ? FXTRUE : FXFALSE;
    }
    if (dstData) std::memset(dstData, 0, srcHeight * dstStride);
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dstBuf, FxU32 dstX, FxU32 dstY, GrLfbSrcFmt_t srcFmt, FxU32 srcWidth, FxU32 srcHeight, FxI32 srcStride, void* srcData) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbWriteRegion invoked: " << srcWidth << "x" << srcHeight);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->WriteLFB(dstX, dstY, srcWidth, srcHeight, srcStride, srcFmt, srcData) ? FXTRUE : FXFALSE;
    }
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t* info) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbLock invoked: buffer=" << buffer << ", mode=" << writeMode);
    if (info) {
        info->size = sizeof(GrLfbInfo_t);
        info->writeMode = writeMode;
        info->origin = origin;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            info->lfbPtr = b->GetLfbWritePtr();
            info->strideInBytes = 800 * 4;
            return FXTRUE;
        }
    }
    return FXFALSE;
}

FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbUnlock invoked.");
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
    GLIDE_LOG(INFO, "Frontend", "gu3dfGetInfo invoked for: " << (filename ? filename : "unknown"));
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
        int lDim = 1 << lod;
        int w = lDim, h = lDim;
        if (aspectW >= aspectH) {
            h = std::max(1, lDim / aspectW);
        } else {
            w = std::max(1, lDim / aspectH);
        }
        totalBytes += w * h * bytesPerTexel;
    }
    info->mem_required = totalBytes;

    GLIDE_LOG(INFO, "Frontend", "gu3dfGetInfo: parsed " << filename << " successfully. Format=" << formatStr 
                                << " Aspect=" << aspectW << "x" << aspectH << " LargeLOD=" << largeLod << " MemRequired=" << totalBytes);
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL gu3dfLoad(const char* filename, Gu3dfInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "gu3dfLoad invoked for: " << (filename ? filename : "unknown"));
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
        GLIDE_LOG(INFO, "Frontend", "gu3dfLoad: Loaded 256 palette entries for P8 texture.");
    }

    if (!info->data) {
        GLIDE_LOG(WARN, "Frontend", "gu3dfLoad: Destination info->data is NULL! Caller must allocate memory.");
        std::fclose(fp);
        return FXFALSE;
    }

    uint8_t* dstBytes = reinterpret_cast<uint8_t*>(info->data);
    int bytesPerTexel = (info->header.format >= GR_TEXFMT_16BIT) ? 2 : 1;

    for (int lod = info->header.large_lod; lod <= info->header.small_lod; ++lod) {
        int lDim = 1 << lod;
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
                GLIDE_LOG(WARN, "Frontend", "gu3dfLoad: Premature EOF reading 8-bit LOD level " << lod);
                std::fclose(fp);
                return FXFALSE;
            }
            dstBytes += pixelCount;
        } else {
            uint16_t* dstShorts = reinterpret_cast<uint16_t*>(dstBytes);
            for (size_t p = 0; p < pixelCount; ++p) {
                dstShorts[p] = ReadBE16(fp);
            }
            dstBytes += pixelCount * 2;
        }
    }

    std::fclose(fp);
    GLIDE_LOG(INFO, "Frontend", "gu3dfLoad: Loaded " << filename << " successfully into memory.");
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
    GLIDE_LOG(INFO, "Frontend", "grFogMode invoked: " << mode);
    s_fogMode = mode & (GR_FOG_WITH_TABLE | GR_FOG_WITH_ITERATED_ALPHA);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetFogMode(s_fogMode);
    }
}

FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fogcolor) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grFogColorValue invoked: " << std::hex << fogcolor << std::dec);
    s_fogColor = fogcolor;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetFogColor(s_fogColor);
    }
}

FX_ENTRY void FX_CALL grFogTable(const GrFog_t ft[]) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grFogTable invoked.");
    if (ft) {
        std::memcpy(s_fogTable, ft, sizeof(s_fogTable));
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->SetFogTable(s_fogTable);
        }
    }
}

} // extern "C"
