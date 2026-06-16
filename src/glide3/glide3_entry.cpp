#include "core/Logger.h"
#include "core/WrapperConfig.h"
#include "core/IConfigLoader.h"
#include "core/JsonConfigLoader.h"
#include "core/BackendManager.h"
#include "core/VertexLayout.h"
#include "core/TextureManager.h"
#include <iostream>

#ifndef __linux__
#define __linux__
#endif

#pragma GCC diagnostic ignored "-Wwrite-strings"

#include <cstring>
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
    uint32_t s_alphaFunc   = 0;
    uint32_t s_alphaFactor = 0;
    uint32_t s_texRgbFunc[2]    = {0, 0};
    uint32_t s_texRgbFactor[2]  = {0, 0};
    uint32_t s_texAlphaFunc[2]  = {0, 0};
    uint32_t s_texAlphaFactor[2]= {0, 0};
    uint32_t s_boundTex[2]      = {0, 0};
}

FX_ENTRY void FX_CALL grGlideInit(void) {
    auto& logger = GlideWrapper::Logger::GetInstance();
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    
    // Parse configuration profile to ensure accurate LogLevel exists
    GlideWrapper::JsonConfigLoader configLoader;
    configLoader.Load("glide_config.json", reg.GetConfig());

    logger.Initialize(std::string(GlideWrapper::WRAPPER_PROJECT_NAME) + ".log", static_cast<GlideWrapper::LogLevel>(reg.GetConfig().logLevel), reg.GetConfig().logToConsole);
    GLIDE_LOG(INFO, "Frontend", "grGlideInit (Glide 3.x) invoked.");
    
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
    GLIDE_LOG(INFO, "Frontend", "grGlideShutdown (Glide 3.x) invoked.");
    GlideWrapper::BackendManager::GetInstance().ShutdownBackend();
}

FX_ENTRY GrContext_t FX_CALL grSstWinOpen(FxU32 hWnd, GrScreenResolution_t resolution, GrScreenRefresh_t refresh, GrColorFormat_t format, GrOriginLocation_t origin, int nColBuffers, int nAuxBuffers) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSstWinOpen (Glide 3.x) invoked. Res=" << resolution << ", Buffers=" << nColBuffers);
    GlideWrapper::VertexLayout::GetInstance().ResetToGlide2Canonical();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend) return 0;
    
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
    return attached ? 1 : 0;
}

FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSstWinClose invoked for Context=" << context);
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->DetachWindow();
    return FXTRUE;
}

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->ClearBuffer(color, alpha, static_cast<float>(depth) / 4294967295.0f, 0x3);
}

FX_ENTRY void FX_CALL grBufferSwap(FxU32 swapInterval) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (backend) backend->SwapBuffers();
}

FX_ENTRY FxBool FX_CALL grSelectContext(GrContext_t context) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSelectContext (Glide 3.x) invoked for Context=" << context);
    return FXTRUE;
}

FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grRenderBuffer (Glide 3.x) invoked for Buffer=" << buffer);
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grSstSelect (Glide 3.x) invoked for SST=" << which_sst);
}

FX_ENTRY void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grVertexLayout (Glide 3.x) invoked. Param=" << param << ", Offset=" << offset << ", Mode=" << mode);
    GlideWrapper::VertexLayout::GetInstance().SetParamOffsetGlide3(param, offset, mode);
}

FX_ENTRY void FX_CALL grDrawTriangle(const void* a, const void* b, const void* c) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grDrawTriangle (Glide 3.x) invoked with a=" << a << ", b=" << b << ", c=" << c);
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !a || !b || !c) {
        GLIDE_LOG(WARN, "Frontend", "grDrawTriangle early exit: backend=" << backend << ", a=" << a << ", b=" << b << ", c=" << c);
        return;
    }
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawTriangle(layout.DecodeVertex(a), layout.DecodeVertex(b), layout.DecodeVertex(c));
}

FX_ENTRY void FX_CALL grDrawLine(const void* a, const void* b) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !a || !b) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawLine(layout.DecodeVertex(a), layout.DecodeVertex(b));
}

FX_ENTRY void FX_CALL grDrawPoint(const void* pt) {
    EnsureEngineInitialized();
    auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
    if (!backend || !pt) return;
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    backend->DrawPoint(layout.DecodeVertex(pt));
}

FX_ENTRY FxU32 FX_CALL grGet(FxU32 pname, FxU32 plength, FxI32* params) {
    GLIDE_LOG(INFO, "Frontend", "grGet (Glide 3.x) invoked for pname: " << pname);
    if (!params || plength < 4) return 0;

    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    
    switch (pname) {
        case GR_MEMORY_FB:              *params = reg.GetConfig().fbiMemoryMb * 1024 * 1024; return 4;
        case GR_MEMORY_TMU:             *params = reg.GetConfig().tmuMemoryMb * 1024 * 1024; return 4;
        case GR_NUM_BOARDS:             *params = 1; return 4;
        case GR_NUM_TMU:                *params = reg.GetConfig().tmuCount; return 4;
        case GR_MAX_TEXTURE_SIZE:       *params = 2048; return 4;
        case GR_MAX_TEXTURE_ASPECT_RATIO:*params = 3; return 4; // 8:1
        default: return 0;
    }
}

FX_ENTRY const char* FX_CALL grGetString(FxU32 pname) {
    GLIDE_LOG(INFO, "Frontend", "grGetString (Glide 3.x) invoked for pname: " << pname);
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    switch (pname) {
        case GR_VERSION:  return "3.10 (glide-ng)";
        case GR_HARDWARE: return "Voodoo5 (Modernized Wrapper Emulation)";
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
    GLIDE_LOG(INFO, "Frontend", "grClipWindow (Glide 3.x) invoked: " << minx << "," << miny << " to " << maxx << "," << maxy);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetClipWindow(minx, miny, maxx, maxy);
    }
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu) {
    GLIDE_LOG(INFO, "Frontend", "grTexMaxAddress (Glide 3.x) invoked for TMU=" << tmu);
    return 0x200000;
}

FX_ENTRY FxU32 FX_CALL grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt) {
    GLIDE_LOG(INFO, "Frontend", "grTexCalcMemRequired (Glide 3.x) invoked.");
    return GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(lodmin, lodmax, aspect, fmt);
}

FX_ENTRY void FX_CALL grGlideGetState(void* state) {
    GLIDE_LOG(INFO, "Frontend", "grGlideGetState (Glide 3.x) invoked.");
}

FX_ENTRY void FX_CALL grGlideSetState(const void* state) {
    GLIDE_LOG(INFO, "Frontend", "grGlideSetState (Glide 3.x) invoked.");
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t fnc, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert) {
    GLIDE_LOG(INFO, "Frontend", "grColorCombine (Glide 3.x) invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local);
    s_colorFunc   = fnc;
    s_colorFactor = (static_cast<uint32_t>(factor) << 16) | static_cast<uint32_t>(local);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_alphaFunc, s_alphaFactor);
    }
}

FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t fnc, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert) {
    GLIDE_LOG(INFO, "Frontend", "grAlphaCombine (Glide 3.x) invoked: Function=" << fnc << ", Factor=" << factor << ", Local=" << local);
    s_alphaFunc   = fnc;
    s_alphaFactor = (static_cast<uint32_t>(factor) << 16) | static_cast<uint32_t>(local);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCombinerMode(s_colorFunc, s_colorFactor, s_alphaFunc, s_alphaFactor);
    }
}

FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgbFnc, GrCombineFactor_t rgbFactor, GrCombineFunction_t alphaFnc, GrCombineFactor_t alphaFactor, FxBool rgbInvert, FxBool alphaInvert) {
    GLIDE_LOG(INFO, "Frontend", "grTexCombine (Glide 3.x) invoked for TMU=" << tmu);
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
    GLIDE_LOG(INFO, "Frontend", "grAlphaBlendFunction (Glide 3.x) invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetBlendState(rgbSf, rgbDf, alphaSf, alphaDf);
    }
}

FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(INFO, "Frontend", "grAlphaTestFunction (Glide 3.x) invoked.");
    s_alphaTestOp = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t refVal) {
    GLIDE_LOG(INFO, "Frontend", "grAlphaTestReferenceValue (Glide 3.x) invoked: " << static_cast<int>(refVal));
    s_alphaTestRef = refVal;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetAlphaTestState(s_alphaTestOp, s_alphaTestRef);
    }
}

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter, GrTextureFilterMode_t magfilter) {
    GLIDE_LOG(INFO, "Frontend", "grTexFilterMode (Glide 3.x) invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_minFilter[tmu] = minfilter;
        s_magFilter[tmu] = magfilter;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode) {
    GLIDE_LOG(INFO, "Frontend", "grTexClampMode (Glide 3.x) invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_clampS[tmu] = s_clampmode;
        s_clampT[tmu] = t_clampmode;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, s_boundTex[tmu], s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend) {
    GLIDE_LOG(INFO, "Frontend", "grTexMipMapMode (Glide 3.x) invoked for TMU=" << tmu);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBufferMode (Glide 3.x) invoked: " << mode);
    s_depthMode = mode;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t fnc) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBufferFunction (Glide 3.x) invoked.");
    s_depthCompare = fnc;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthMask(FxBool mask) {
    GLIDE_LOG(INFO, "Frontend", "grDepthMask (Glide 3.x) invoked: " << mask);
    s_depthMask = mask;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grDepthBiasLevel(FxI32 level) {
    GLIDE_LOG(INFO, "Frontend", "grDepthBiasLevel (Glide 3.x) invoked: " << level);
    s_depthBias = level;
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetDepthState(s_depthMode, s_depthCompare, s_depthMask, s_depthBias);
    }
}

FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin) {
    GLIDE_LOG(INFO, "Frontend", "grSstOrigin (Glide 3.x) invoked: " << origin);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetSstOrigin(origin);
    }
}

FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode) {
    GLIDE_LOG(INFO, "Frontend", "grCullMode (Glide 3.x) invoked: " << mode);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetCullState(mode);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadMipMap (Glide 3.x) invoked for TMU=" << tmu);
    if (info && info->data) {
        // Translate Glide 3.x signed log2 aspect ratio to Glide 2.x core format (CoreAspect = 3 - Glide3Aspect)
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, info->smallLodLog2, info->largeLodLog2, 3 - info->aspectRatioLog2, info->format, info->data);
    }
}

FX_ENTRY void FX_CALL grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void* data) {
    GLIDE_LOG(INFO, "Frontend", "grTexDownloadMipMapLevel (Glide 3.x) invoked for TMU=" << tmu << ", LOD=" << thisLod);
    if (data) {
        // Translate Glide 3.x signed log2 aspect ratio to Glide 2.x core format
        GlideWrapper::TextureManager::GetInstance().DownloadMipMap(tmu, startAddress, thisLod, thisLod, 3 - aspectRatio, format, data);
    }
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "grTexSource (Glide 3.x) invoked for TMU=" << tmu);
    if (tmu < 2) {
        s_boundTex[tmu] = startAddress;
        if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
            b->BindTexture(tmu, startAddress, s_clampS[tmu], s_clampT[tmu], s_minFilter[tmu], s_magFilter[tmu]);
        }
    }
}

FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grConstantColorValue (Glide 3.x) invoked: " << std::hex << value << std::dec);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        b->SetConstantColor(value);
    }
}

FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t srcBuf, FxU32 srcX, FxU32 srcY, FxU32 srcWidth, FxU32 srcHeight, FxU32 dstStride, void* dstData) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbReadRegion (Glide 3.x) invoked.");
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->ReadLFB(srcX, srcY, srcWidth, srcHeight, dstStride, dstData) ? FXTRUE : FXFALSE;
    }
    if (dstData) std::memset(dstData, 0, srcHeight * dstStride);
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dstBuf, FxU32 dstX, FxU32 dstY, GrLfbSrcFmt_t srcFmt, FxU32 srcWidth, FxU32 srcHeight, FxBool pixelPipeline, FxI32 srcStride, void* srcData) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbWriteRegion (Glide 3.x) invoked: " << srcWidth << "x" << srcHeight);
    if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
        return b->WriteLFB(dstX, dstY, srcWidth, srcHeight, srcStride, srcFmt, srcData) ? FXTRUE : FXFALSE;
    }
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t* info) {
    EnsureEngineInitialized();
    GLIDE_LOG(INFO, "Frontend", "grLfbLock (Glide 3.x) invoked: buffer=" << buffer << ", mode=" << writeMode);
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
    GLIDE_LOG(INFO, "Frontend", "grLfbUnlock (Glide 3.x) invoked.");
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL gu3dfGetInfo(const char* filename, Gu3dfInfo* info) {
    GLIDE_LOG(INFO, "Frontend", "gu3dfGetInfo (Glide 3.x) invoked for: " << (filename ? filename : "unknown"));
    if (info) {
        info->header.width = 256;
        info->header.height = 256;
        info->header.small_lod = 0;
        info->header.large_lod = 8;
        info->header.aspect_ratio = 0;
        info->header.format = 0;
        info->mem_required = 65536;
    }
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL gu3dfLoad(const char* filename, Gu3dfInfo* data) {
    GLIDE_LOG(INFO, "Frontend", "gu3dfLoad (Glide 3.x) invoked for: " << (filename ? filename : "unknown"));
    return FXTRUE;
}

} // extern "C"
