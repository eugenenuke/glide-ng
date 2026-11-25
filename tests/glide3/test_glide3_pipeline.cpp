// Auto-modularized Glide 3.x Catch2 test suite unit.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <type_traits>
#include <vector>

#include <SDL2/SDL.h>

#include "backends/GlideStateTracker.h"
#include "backends/SoftwareBackendBase.h"
#include "core/BackendManager.h"
#include "core/GlideEntryCommon.h"
#include "core/HudOverlay.h"
#include "core/IniConfigLoader.h"
#include "core/Logger.h"
#include "core/NativeSurfaceHost.h"
#include "core/SimdDispatch.h"
#include "core/SimdKernels.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/VertexLayout.h"
#include "core/WrapperConfig.h"
#include "common/test_helpers.h"

#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include <glide.h>

namespace {
struct StandardVert {
  float x, y;     // 0, 4
  float z;        // 8
  float r, g, b;  // 12, 16, 20
  float ooz;      // 24
  float a;        // 28
  float oow;      // 32
  float s, t;     // 36, 40
  float w;        // 44
};

[[maybe_unused]] inline void* OpenGlide3Handle() {
  return GlideTestHelpers::OpenGlideSelfHandle();
}
[[maybe_unused]] inline void* OpenGlide3DsoHandle() {
  return GlideTestHelpers::OpenGlideLibrary("libglide3x.so");
}
[[maybe_unused]] inline void* OpenGlide2DsoHandle() {
  return GlideTestHelpers::OpenGlideLibrary("libglide2x.so");
}
}  // namespace

TEST_CASE("Glide 3.x: Vertex Layout & Parameter Decoding", "[Glide3][VertexLayout]") {
  SECTION("Vertex layout decoding integrity") {
    SECTION("Glide 3 Canonical Vertex Decoding") {
        auto& layout = GlideWrapper::VertexLayout::GetInstance();
        layout.ResetToGlide3Canonical();

        // Prepare mock raw Glide 3 vertex matching standard GrVertex offsets:
        // x,y at 0, ooz at 8, oow at 12, r,g,b at 16, a at 28, z at 32, st0 at 36
        struct MockGlide3Vertex {
            float x, y;
            float ooz;
            float oow;
            float r, g, b;
            float a;
            float z;
            float sow, tow;
            float oow_tmu;
        };

        MockGlide3Vertex v3;
        v3.x = 10.0f; v3.y = 20.0f;
        v3.ooz = 50000.0f;
        v3.oow = 0.25f;
        v3.r = 64.0f; v3.g = 128.0f; v3.b = 192.0f;
        v3.a = 100.0f;
        v3.z = 99.0f;
        v3.sow = 15.0f; v3.tow = 25.0f;
        v3.oow_tmu = 0.25f;

        GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v3);

        REQUIRE(mv.pos[0] == 10.0f);
        REQUIRE(mv.pos[1] == 20.0f);
        REQUIRE(mv.pos[2] == 50000.0f / 65535.0f);
        REQUIRE(mv.pos[3] == 0.25f);
        REQUIRE(mv.color[0] == 64.0f / 255.0f);
        REQUIRE(mv.color[1] == 128.0f / 255.0f);
        REQUIRE(mv.color[2] == 192.0f / 255.0f);
        REQUIRE(mv.color[3] == 100.0f / 255.0f);
        REQUIRE(mv.tex[0] == 15.0f);
        REQUIRE(mv.tex[1] == 25.0f);
    }
  }

  SECTION("FOGCOORD parameter decoding") {
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    layout.ResetToGlide3Canonical();

    // 1. Enable custom fog coordinate offset
    // GR_PARAM_FOG_EXT = 0x05, offset = 28, mode = 1 (ENABLE)
    layout.SetParamOffsetGlide3(0x05, 28, 1);

    // 2. Create a mock vertex byte buffer matching this layout
    struct MockVertex {
        float x, y;    // 0, 4
        float z;       // 8
        float r, g, b; // 12, 16, 20
        float a;       // 24
        float fog;     // 28  <-- Custom Fog Coordinate!
        float pad[3];  // Pad to 44 bytes to prevent ASan stack-buffer-overflow on canonical ST0 at offset 36!
    };

    MockVertex mockV;
    mockV.x = 100.0f;
    mockV.y = 200.0f;
    mockV.z = 0.5f;
    mockV.r = 255.0f;
    mockV.g = 128.0f;
    mockV.b = 64.0f;
    mockV.a = 255.0f;
    mockV.fog = 12.34f; // Custom fog coord!

    // 3. Decode the vertex
    GlideWrapper::ModernVertex decoded = layout.DecodeVertex(&mockV);

    // 4. Assert decoded values
    CHECK(decoded.pos[0] == 100.0f);
    CHECK(decoded.pos[1] == 200.0f);
    CHECK(decoded.fog == 12.34f);

    // Clean up layout
    layout.ResetToGlide3Canonical();
  }

  SECTION("GR_PARAM_PARGB packed color & enable/disable modes") {
  // Exact layout used by official 3dfx Voodoo5/Napalm Glide 3.x demos
  // (Kitty Hawk, Museum, Rocket Burger): XY@0, Q@12, ST0@16, PARGB@32.
  // Bytes 16..31 overlap Glide 3.x canonical float RGB@16 and A@28.
  struct PargbDemoVert {
    float x, y;      // 0..7   (GR_PARAM_XY)
    float padZ;      // 8..11
    float q;         // 12..15 (GR_PARAM_Q)
    float s0, t0;    // 16..23 (GR_PARAM_ST0 — overlaps canonical RGB!)
    float pad1, pad2;// 24..31 (overlaps canonical RGB.b and A!)
    uint32_t pargb;  // 32..35 (GR_PARAM_PARGB: 0xAARRGGBB)
    float q0;        // 36..39 (optional GR_PARAM_Q0)
    float q1;        // 40..43 (optional GR_PARAM_Q1)
  };
  static_assert(sizeof(PargbDemoVert) == 44, "PargbDemoVert layout mismatch");

  SECTION("VertexLayout decodes GR_PARAM_PARGB (0xAARRGGBB), GR_PARAM_Q0/Q1, and round-trips state") {
    auto& vl = GlideWrapper::VertexLayout::GetInstance();
    vl.ResetToGlide3Canonical();

    // Enable PARGB without explicitly disabling canonical RGB/A
    vl.SetParamOffsetGlide3(0x01, 0, 1);   // GR_PARAM_XY
    vl.SetParamOffsetGlide3(0x04, 12, 1);  // GR_PARAM_Q
    vl.SetParamOffsetGlide3(0x40, 16, 1);  // GR_PARAM_ST0
    vl.SetParamOffsetGlide3(0x41, 16, 1);  // GR_PARAM_ST1
    vl.SetParamOffsetGlide3(0x30, 32, 1);  // GR_PARAM_PARGB
    vl.SetParamOffsetGlide3(0x50, 36, 1);  // GR_PARAM_Q0
    vl.SetParamOffsetGlide3(0x51, 40, 1);  // GR_PARAM_Q1

    PargbDemoVert v{};
    v.x = 123.5f;
    v.y = 45.25f;
    v.q = 0.5f;
    v.s0 = 0.25f;
    v.t0 = 0.75f;
    v.pargb = 0x80F04010u;  // A=128, R=240, G=64, B=16
    v.q0 = 0.25f;
    v.q1 = 0.125f;

    GlideWrapper::ModernVertex mv = vl.DecodeVertex(&v);
    CHECK(mv.pos[0] == 123.5f);
    CHECK(mv.pos[1] == 45.25f);
    CHECK(mv.pos[3] == 0.5f);
    CHECK(std::fabs(mv.color[0] - (240.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[1] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[2] - (16.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[3] - (128.0f / 255.0f)) < 1e-4f);
    CHECK(mv.tex[0] == 0.25f);
    CHECK(mv.tex[1] == 0.75f);
    CHECK(mv.tmu_oow[0] == 0.25f);
    CHECK(mv.tmu_oow[1] == 0.125f);
    CHECK(vl.GetMaxActiveOffset() >= 44);

    // Save layout, reset to float canonical, restore layout, verify identical decode
    uint8_t savedLayout[256] = {};
    vl.GetLayoutState(savedLayout);
    vl.ResetToGlide3Canonical();
    vl.SetLayoutState(savedLayout);

    GlideWrapper::ModernVertex mvRestored = vl.DecodeVertex(&v);
    CHECK(std::fabs(mvRestored.color[0] - (240.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[1] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[2] - (16.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[3] - (128.0f / 255.0f)) < 1e-4f);
    CHECK(mvRestored.pos[3] == 0.5f);
    CHECK(mvRestored.tmu_oow[0] == 0.25f);

    // Re-enabling float GR_PARAM_RGB / GR_PARAM_A switches colorType back to GR_FLOAT
    vl.SetParamOffsetGlide3(0x20, 12, 1);  // GR_PARAM_RGB
    vl.SetParamOffsetGlide3(0x10, 28, 1);  // GR_PARAM_A
    StandardVert fv = {10.0f, 20.0f, 0.0f, 32.0f, 224.0f, 64.0f, 0.0f, 192.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    GlideWrapper::ModernVertex mvf = vl.DecodeVertex(&fv);
    CHECK(std::fabs(mvf.color[0] - (32.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[1] - (224.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[2] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[3] - (192.0f / 255.0f)) < 1e-4f);
  }

#ifdef __linux__
  SECTION("End-to-end Glide 3.x PARGB rendering, alpha blending, layout save/restore & VSA-100 grEnable/grDisable") {
    struct ScopedPargbEnv {
      std::vector<std::string> keys = {
          "GLIDE_NG_HEADLESS", "GLIDE_NG_MAX_FPS", "GLIDE_NG_VSYNC",
          "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_LOG_FILE"};
      std::vector<std::pair<std::string, std::string>> saved;
      ScopedPargbEnv() {
        for (const auto& k : keys) {
          const char* v = std::getenv(k.c_str());
          saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        }
      }
      ~ScopedPargbEnv() {
        for (const auto& kv : saved) {
          if (kv.second == "\x01UNSET") {
            ::unsetenv(kv.first.c_str());
          } else {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
          }
        }
      }
    } envGuard;

    const std::string logPath = "/tmp/glide_ng_g3_pargb_test.log";
    std::remove(logPath.c_str());
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "warn", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(::dlsym(handle, "grVertexLayout"));
    auto getVertexLayout = reinterpret_cast<void (*)(void*)>(::dlsym(handle, "grGlideGetVertexLayout"));
    auto setVertexLayout = reinterpret_cast<void (*)(const void*)>(::dlsym(handle, "grGlideSetVertexLayout"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(::dlsym(handle, "grBufferClear"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grAlphaCombine"));
    auto alphaBlend = reinterpret_cast<void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                                GrAlphaBlendFnc_t, GrAlphaBlendFnc_t)>(
        ::dlsym(handle, "grAlphaBlendFunction"));
    auto depthMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(::dlsym(handle, "grDepthBufferMode"));
    auto ditherMode = reinterpret_cast<void (*)(GrDitherMode_t)>(::dlsym(handle, "grDitherMode"));
    auto grEnableFn = reinterpret_cast<void (*)(GrEnableMode_t)>(::dlsym(handle, "grEnable"));
    auto grDisableFn = reinterpret_cast<void (*)(GrEnableMode_t)>(::dlsym(handle, "grDisable"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbRead = reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(getVertexLayout != nullptr);
    REQUIRE(setVertexLayout != nullptr);
    REQUIRE(grEnableFn != nullptr);
    REQUIRE(grDisableFn != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Standard Glide 3.x & VSA-100 enable/disable modes must not emit WARN spam
    const GrEnableMode_t v5Modes[] = {
        0x02u,        // GR_ALLOW_MIPMAP_DITHER
        0x03u,        // GR_PASSTHRU
        0x05u,        // GR_VIDEO_SMOOTHING
        0x80000001u,  // GR_COMBINEEXT_MODE
        0x80000002u,  // GR_AA_MULTI_SAMPLE
    };
    for (GrEnableMode_t m : v5Modes) {
      grEnableFn(m);
      grDisableFn(m);
    }

    depthMode(GR_DEPTHBUFFER_DISABLE);
    ditherMode(GR_DITHER_DISABLE);
    bufferClear(0xFF000000u, 0xFF, 0xFFFF);

    // Configure Voodoo5 demo layout (XY@0, Q@12, ST0@16, PARGB@32)
    vertexLayout(0x01, 0, 1);   // GR_PARAM_XY
    vertexLayout(0x04, 12, 1);  // GR_PARAM_Q
    vertexLayout(0x40, 16, 1);  // GR_PARAM_ST0
    vertexLayout(0x30, 32, 1);  // GR_PARAM_PARGB

    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    auto drawPargbQuad = [&](float x0, float y0, float x1, float y1, uint32_t pargb) {
      PargbDemoVert tl{x0, y0, 0.0f, 1.0f, 0.1f, 0.2f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert tr{x1, y0, 0.0f, 1.0f, 0.9f, 0.2f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert br{x1, y1, 0.0f, 1.0f, 0.9f, 0.8f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert bl{x0, y1, 0.0f, 1.0f, 0.1f, 0.8f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    };

    // 1. Opaque orange-red PARGB (0xFFF05010: A=255, R=240, G=80, B=16)
    drawPargbQuad(20.0f, 20.0f, 120.0f, 120.0f, 0xFFF05010u);

    // 2. Alpha-blended 50% green (0x8010F010) over opaque blue (0xFF1020F0)
    drawPargbQuad(150.0f, 20.0f, 250.0f, 120.0f, 0xFF1020F0u);
    alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    drawPargbQuad(150.0f, 20.0f, 250.0f, 120.0f, 0x8010F010u);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    // 3. Save PARGB layout -> switch to float RGB -> draw green -> restore PARGB -> draw red
    uint8_t pargbLayoutBuf[256] = {};
    getVertexLayout(pargbLayoutBuf);

    vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB (GR_FLOAT)
    vertexLayout(0x10, 28, 1);  // GR_PARAM_A (GR_FLOAT)
    vertexLayout(0x03, 32, 1);  // GR_PARAM_W
    {
      StandardVert tl = {280.0f, 20.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert tr = {360.0f, 20.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert br = {360.0f, 120.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert bl = {280.0f, 120.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    }

    setVertexLayout(pargbLayoutBuf);
    drawPargbQuad(380.0f, 20.0f, 460.0f, 120.0f, 0xFFF01010u);

    auto readRGB = [&](FxU32 x, FxU32 y, int& r, int& g, int& b) {
      uint16_t px = 0;
      REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, x, y, 1, 1, 2, &px) == FXTRUE);
      r = ((px >> 11) & 31) << 3;
      g = ((px >> 5) & 63) << 2;
      b = (px & 31) << 3;
    };

    int r = 0, g = 0, b = 0;
    // Quad 1: orange-red PARGB
    readRGB(70, 70, r, g, b);
    CHECK(r > 210);
    CHECK(g > 50);
    CHECK(g < 110);
    CHECK(b < 40);

    // Quad 2: 50% green blended over blue -> both G and B ~115..140
    readRGB(200, 70, r, g, b);
    CHECK(r < 40);
    CHECK(g > 90);
    CHECK(g < 165);
    CHECK(b > 90);
    CHECK(b < 165);

    // Quad 3: float green after switching from PARGB -> RGB
    readRGB(320, 70, r, g, b);
    CHECK(r < 40);
    CHECK(g > 210);
    CHECK(b < 40);

    // Quad 4: red PARGB after grGlideSetVertexLayout restore
    readRGB(420, 70, r, g, b);
    CHECK(r > 210);
    CHECK(g < 40);
    CHECK(b < 40);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Confirm zero unsupported mode warnings were emitted
    std::ifstream logIn(logPath);
    if (logIn.is_open()) {
      const std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                   std::istreambuf_iterator<char>());
      CHECK(logContent.find("unsupported mode=") == std::string::npos);
    }
    std::remove(logPath.c_str());
  }
#endif
  }

}

TEST_CASE("Glide 3.x: Alpha-8 Font Rendering & W-Buffering", "[Glide3][Text][WBuffer]") {
  SECTION("Alpha-8 font texture rendering") {
#ifdef __linux__
    // 1. Load the shared library
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    // 2. Resolve function pointers
    using InitFunc = void(*)(void);
    using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using SelectCtxFunc = FxBool(*)(FxU32);
    using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
    using WinCloseFunc = FxBool(*)(FxU32);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ClipWinFunc = void(*)(FxU32, FxU32, FxU32, FxU32);
    using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);

    using TexDownloadMipMapFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using TexFilterModeFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using TexClampModeFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto clipWin = reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

    auto texDownload = reinterpret_cast<TexDownloadMipMapFunc>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto texFilter = reinterpret_cast<TexFilterModeFunc>(::dlsym(handle, "grTexFilterMode"));
    auto texClamp = reinterpret_cast<TexClampModeFunc>(::dlsym(handle, "grTexClampMode"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(selectCtx != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(clipWin != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(texFilter != nullptr);
    REQUIRE(texClamp != nullptr);
    REQUIRE(shutdown != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
    REQUIRE(ctx == 1);
    REQUIRE(selectCtx(ctx) == FXTRUE);

    clipWin(0, 0, 800, 600);

    // Configure vertex layout
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W at 32
    vertexLayout(0x40, 36, 1); // GR_PARAM_ST0 at 36

    bufferClear(0xFF000000, 0, 0xFFFFFFFF); // Clear to Black

    // Prepare 32x16 Texture in GR_TEXFMT_ALPHA_8
    // Top half (y = 0..7) is solid white (0xFF), bottom half (y = 8..15) is transparent/black (0x00)
    std::vector<uint8_t> texData(32 * 16, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            texData[y * 32 + x] = 0xFF;
        }
    }

    GrTexInfo texInfo;
    texInfo.smallLodLog2 = 5; // Glide 3.x GR_LOD_LOG2_32 = 5 (corresponds to Glide 2.x GR_LOD_32 = 3)
    texInfo.largeLodLog2 = 5;
    texInfo.aspectRatioLog2 = 1; // 2W x 1H -> 32x16 (Glide 3 aspect ratio value)
    texInfo.format = GR_TEXFMT_ALPHA_8;
    texInfo.data = texData.data();

    // Download to TMU0
    texDownload(0, 0x00002000, 0, &texInfo);
    texSource(0, 0x00002000, 0, &texInfo);
    texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texFilter(0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    // Combine setup: output the texture alpha to RGB/A channels so we can read it easily
    texCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Draw a centered quad from (300,300) to (500,500)
    StandardVert v0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f,   0.0f, 1.0f};
    StandardVert v1{500.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f,   0.0f, 1.0f};
    StandardVert v2{500.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f, 128.0f, 1.0f};
    StandardVert v3{300.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f, 128.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    // Read back a pixel from the top half (e.g. x=400, y=350) directly from the back buffer
    uint32_t topPixel = 0;
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 400, 350, 1, 1, 4, &topPixel);
    REQUIRE(ok == FXTRUE);

    // Read back a pixel from the bottom half (e.g. x=400, y=450) directly from the back buffer
    uint32_t bottomPixel = 0;
    ok = readLfb(GR_BUFFER_BACKBUFFER, 400, 450, 1, 1, 4, &bottomPixel);
    REQUIRE(ok == FXTRUE);

    // Unpack colors
    uint8_t r_top = (topPixel >> 16) & 0xFF;
    uint8_t g_top = (topPixel >> 8) & 0xFF;
    uint8_t b_top = topPixel & 0xFF;

    uint8_t r_bottom = (bottomPixel >> 16) & 0xFF;
    uint8_t g_bottom = (bottomPixel >> 8) & 0xFF;
    uint8_t b_bottom = bottomPixel & 0xFF;

    INFO("Top half pixel at (400,150) = 0x" << std::hex << topPixel << " | R=" << (int)r_top << ", G=" << (int)g_top << ", B=" << (int)b_top);
    INFO("Bottom half pixel at (400,450) = 0x" << std::hex << bottomPixel << " | R=" << (int)r_bottom << ", G=" << (int)g_bottom << ", B=" << (int)b_bottom);

    REQUIRE(r_top >= 250);
    REQUIRE(g_top >= 250);
    REQUIRE(b_top >= 250);

    REQUIRE(r_bottom <= 5);
    REQUIRE(g_bottom <= 5);
    REQUIRE(b_bottom <= 5);

    shutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("W-buffering and layout Q-param compatibility") {
    #ifndef GR_WDEPTH_MIN_MAX
    #define GR_WDEPTH_MIN_MAX 0x27
    #endif
    #ifndef GR_ZDEPTH_MIN_MAX
    #define GR_ZDEPTH_MIN_MAX 0x28
    #endif

    // 1. Direct unit test of VertexLayout decoding for Q-Parameter (GR_PARAM_Q)
    SECTION("Direct VertexLayout Decode with GR_PARAM_Q") {
        auto& layout = GlideWrapper::VertexLayout::GetInstance();
        layout.ResetToGlide3Canonical();

        // Configure layout: XY at 0, Q (0x04) at 8
        layout.SetParamOffsetGlide3(0x01, 0, 1); // GR_PARAM_XY
        layout.SetParamOffsetGlide3(0x04, 8, 1); // GR_PARAM_Q
        // Disable other parameters to prevent stack-buffer-overflow when decoding MockQVertex!
        layout.SetParamOffsetGlide3(0x20, 0, 0); // GR_PARAM_RGB
        layout.SetParamOffsetGlide3(0x10, 0, 0); // GR_PARAM_A
        layout.SetParamOffsetGlide3(0x40, 0, 0); // GR_PARAM_ST0

        struct MockQVertex {
            float x, y;
            float q; // oow (1/W)
        };

        MockQVertex v;
        v.x = 100.0f; v.y = 200.0f;
        v.q = 0.125f; // W = 8.0f

        GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

        REQUIRE(mv.pos[0] == 100.0f);
        REQUIRE(mv.pos[1] == 200.0f);
        REQUIRE(mv.pos[3] == 0.125f);
    }

#ifdef __linux__
    // 2. Integration test via dynamic shared library loading
    #if 0
    SECTION("Dynamic API Verification for grGet Depth Ranges and State Sizes") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using GetFunc = FxU32(*)(FxU32, FxU32, FxI32*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto grGetVal = reinterpret_cast<GetFunc>(::dlsym(handle, "grGet"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(grGetVal != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();

        // Test GR_WDEPTH_MIN_MAX
        FxI32 wRange[2] = { -1, -1 };
        FxU32 bytes = grGetVal(GR_WDEPTH_MIN_MAX, 8, wRange);
        REQUIRE(bytes == 8);
        REQUIRE(wRange[0] == 0);
        REQUIRE(wRange[1] == 65535);

        // Test GR_ZDEPTH_MIN_MAX
        FxI32 zRange[2] = { -1, -1 };
        bytes = grGetVal(GR_ZDEPTH_MIN_MAX, 8, zRange);
        REQUIRE(bytes == 8);
        REQUIRE(zRange[0] == 65535);
        REQUIRE(zRange[1] == 0);

        // Test GR_GLIDE_STATE_SIZE (0x06)
        FxI32 stateSize = 0;
        bytes = grGetVal(0x06, 4, &stateSize);
        REQUIRE(bytes == 4);
        REQUIRE(stateSize > 0);

        // Test GR_GLIDE_VERTEXLAYOUT_SIZE (0x07)
        FxI32 layoutSize = 0;
        bytes = grGetVal(0x07, 4, &layoutSize);
        REQUIRE(bytes == 4);
        REQUIRE(layoutSize > 0);

        shutdown();
        ::dlclose(handle);
    }
    #endif

    #if 0
    SECTION("Dynamic API Verification for LFB Lock, Write and Origin Swapping") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using LockFunc = FxBool(*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t, GrOriginLocation_t, FxBool, GrLfbInfo_t*);
        using UnlockFunc = FxBool(*)(GrLock_t, GrBuffer_t);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto lfbLock = reinterpret_cast<LockFunc>(::dlsym(handle, "grLfbLock"));
        auto lfbUnlock = reinterpret_cast<UnlockFunc>(::dlsym(handle, "grLfbUnlock"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(lfbLock != nullptr);
        REQUIRE(lfbUnlock != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
        REQUIRE(b != nullptr);

        // 1. Clear backbuffer to solid black
        b->ClearBuffer(0, 0, 0.0f, 0x3);

        // 2. Lock backbuffer with UPPER_LEFT origin, write a red pixel at (0, 0)
        GrLfbInfo_t info{};
        info.size = sizeof(info);
        FxBool locked = lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
        REQUIRE(locked == FXTRUE);
        REQUIRE(info.lfbPtr != nullptr);
        REQUIRE(info.strideInBytes >= 640 * 2);

        auto* shortData = reinterpret_cast<FxU16*>(info.lfbPtr);
        shortData[0] = 0xF800; // Red in RGB565

        FxBool unlocked = lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        REQUIRE(unlocked == FXTRUE);

        // 3. Read back from backend and verify it is red (0xFFFF0000 in RGBA)
        std::vector<uint32_t> readPixels(640 * 480, 0);
        b->ReadLFB(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, readPixels.data());
        REQUIRE(readPixels[0] == 0xFFFF0000);

        // 4. Clear backbuffer again
        b->ClearBuffer(0, 0, 0.0f, 0x3);

        // 5. Lock with LOWER_LEFT origin, write red pixel at (0, 0) (logical)
        locked = lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_LOWER_LEFT, FXFALSE, &info);
        REQUIRE(locked == FXTRUE);
        shortData = reinterpret_cast<FxU16*>(info.lfbPtr);
        shortData[0] = 0xF800; // Red

        unlocked = lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        REQUIRE(unlocked == FXTRUE);

        // 6. Read back: row-flipping must have mapped it to physical (0, height - 1)!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        b->ReadLFB(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, readPixels.data());
        
        // Logical (0,0) under LOWER_LEFT maps to physical bottom-left (0, 479)
        REQUIRE(readPixels[479 * 640] == 0xFFFF0000);
        REQUIRE(readPixels[0] == 0xFF000000); // Physical (0,0) must remain black (alpha=255)!

        shutdown();
        ::dlclose(handle);
    }
    #endif

    SECTION("Dynamic API Verification for Splash Screen and Shameless Plug") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;
        using G3EnableMode = FxU32;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using SplashFunc = void(*)(float, float, float, float, FxU32);
        using EnableFunc = void(*)(G3EnableMode);
        using DisableFunc = void(*)(G3EnableMode);
        using SwapFunc = void(*)(FxU32);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
        auto enable = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
        auto disable = reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
        auto swap = reinterpret_cast<SwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(splash != nullptr);
        REQUIRE(enable != nullptr);
        REQUIRE(disable != nullptr);
        REQUIRE(swap != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Render a splash frame (tests core animator lifecycle)
        splash(0.0f, 0.0f, 640.0f, 480.0f, 35);

        // Enable shameless plug and swap (tests plug drawing)
        enable(static_cast<G3EnableMode>(0x04)); // GR_SHAMELESS_PLUG
        swap(1);

        disable(static_cast<G3EnableMode>(0x04));

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Vertex Layout Save/Restore and TMU Chromakey Extensions") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using GetProcFunc = GrProc(*)(char*);
        using LayoutFunc = void(*)(FxU32, FxI32, FxU32);
        using GetLayoutFunc = void(*)(void*);
        using SetLayoutFunc = void(*)(const void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto getProc = reinterpret_cast<GetProcFunc>(::dlsym(handle, "grGetProcAddress"));
        auto vertexLayout = reinterpret_cast<LayoutFunc>(::dlsym(handle, "grVertexLayout"));
        auto getLayout = reinterpret_cast<GetLayoutFunc>(::dlsym(handle, "grGlideGetVertexLayout"));
        auto setLayout = reinterpret_cast<SetLayoutFunc>(::dlsym(handle, "grGlideSetVertexLayout"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(getProc != nullptr);
        REQUIRE(vertexLayout != nullptr);
        REQUIRE(getLayout != nullptr);
        REQUIRE(setLayout != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // 1. Modify active layout
        vertexLayout(0x01, 8, 1);  // GR_PARAM_XY at offset 8, mode ENABLE
        vertexLayout(0x20, 24, 1); // GR_PARAM_RGB at offset 24, mode ENABLE

        // 2. Save active layout
        uint8_t savedLayout[256];
        std::memset(savedLayout, 0, sizeof(savedLayout));
        getLayout(savedLayout);

        // 3. Clobber/reset layout to canonical
        vertexLayout(0x01, 0, 1);
        vertexLayout(0x20, 12, 1);

        // 4. Restore saved layout
        setLayout(savedLayout);

        // 5. Verify restored offsets via Backend/Core DecodeVertex
        auto& vlayout = GlideWrapper::VertexLayout::GetInstance();
        
        // Let's decode a dummy vertex to verify offsets were restored!
        struct DummyRestoredVertex {
            float pad1[2];
            float x, y;
            float pad2[2];
            float r, g, b;
            float pad3[2]; // Add padding to cover ST0 at offset 36 (prevent ASan stack overflow)
        } vertex;
        vertex.x = 10.0f; vertex.y = 20.0f;
        vertex.r = 255.0f; vertex.g = 128.0f; vertex.b = 0.0f;

        GlideWrapper::ModernVertex decoded = vlayout.DecodeVertex(&vertex);
        REQUIRE(decoded.pos[0] == 10.0f);
        REQUIRE(decoded.pos[1] == 20.0f);
        REQUIRE(decoded.color[0] == 1.0f);
        REQUIRE(std::abs(decoded.color[1] - 128.0f / 255.0f) < 0.001f);
        REQUIRE(decoded.color[2] == 0.0f);

        // 6. Query and verify TMU Chromakey extensions via grGetProcAddress
        auto texChromaMode = GlideTestHelpers::CastProc<void(*)(GrChipID_t, FxU32)>(getProc(const_cast<char*>("grTexChromaModeExt")));
        auto texChromaRange = GlideTestHelpers::CastProc<void(*)(GrChipID_t, GrColor_t, GrColor_t, FxU32)>(getProc(const_cast<char*>("grTexChromaRangeExt")));
        
        REQUIRE(texChromaMode != nullptr);
        REQUIRE(texChromaRange != nullptr);

        // Invoke them to ensure no crash and correct routing
        texChromaMode(0, 1); // Enable chromakey on TMU 0
        texChromaRange(0, 0xFF000000, 0xFF0000FF, 1); // Range-based on TMU 0

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Backface Culling and Winding") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
        using ConstantColorFunc = void(*)(GrColor_t);
        using CullModeFunc = void(*)(GrCullMode_t);
        using DrawTriFunc = void(*)(const void*, const void*, const void*);
        using BufferClearFunc = void(*)(GrColor_t, FxU16, FxU32);
        using SwapFunc = void(*)(FxU32);
        using ShutdownFunc = void(*)(void);
        using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
        auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
        auto cullMode = reinterpret_cast<CullModeFunc>(::dlsym(handle, "grCullMode"));
        auto drawTriangle = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto swap = reinterpret_cast<SwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
        auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(colorCombine != nullptr);
        REQUIRE(constantColor != nullptr);
        REQUIRE(cullMode != nullptr);
        REQUIRE(drawTriangle != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(swap != nullptr);
        REQUIRE(shutdown != nullptr);
        REQUIRE(vertexLayout != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Explicitly define the layout for G3Vertex { float x, y, z, oow; } to prevent ASan stack-buffer-overflow!
        vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at offset 0, ENABLE
        vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at offset 8, ENABLE
        vertexLayout(0x04, 12, 1); // GR_PARAM_Q at offset 12, ENABLE
        vertexLayout(0x20, 0, 0);  // GR_PARAM_RGB, DISABLE
        vertexLayout(0x10, 0, 0);  // GR_PARAM_A, DISABLE
        vertexLayout(0x40, 0, 0);  // GR_PARAM_ST0 at offset 0, DISABLE (Correct ID: 0x40!)

        // Set up flat constant color rendering
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

        // Define three vertices for Clockwise triangle (Triangle 1)
        struct G3Vertex { float x, y, z, oow; } vA, vB, vC;
        vA.x = 320.0f; vA.y = 48.0f;  vA.z = 0.0f; vA.oow = 1.0f;
        vB.x = 512.0f; vB.y = 432.0f; vB.z = 0.0f; vB.oow = 1.0f;
        vC.x = 128.0f; vC.y = 432.0f; vC.z = 0.0f; vC.oow = 1.0f;

        auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
        REQUIRE(backend != nullptr);

        std::vector<uint32_t> readPixels(640 * 480, 0);

        // ==========================================
        // TEST CASE A: CULL_POSITIVE (Cull Clockwise, Render Counter-Clockwise)
        // ==========================================
        cullMode(GR_CULL_POSITIVE);
        bufferClear(0xFF000000, 0, 0); // Clear to black (alpha=255)

        // 1. Draw Clockwise triangle (vA -> vB -> vC) in BLUE (0x000000FF in unpacked shader)
        constantColor(0x000000FF); 
        drawTriangle(&vA, &vB, &vC);

        // Read back center: must remain black!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00000000); // Culled! (ignore alpha)

        // 2. Draw Counter-Clockwise triangle (vA -> vC -> vB) in RED (0x00FF0000 in unpacked shader)
        constantColor(0x00FF0000);
        drawTriangle(&vA, &vC, &vB);

        // Read back center: must be Red (0x00FF0000)
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00FF0000); // Rendered! (ignore alpha)

        // ==========================================
        // TEST CASE B: CULL_NEGATIVE (Cull Counter-Clockwise, Render Clockwise)
        // ==========================================
        cullMode(GR_CULL_NEGATIVE);
        bufferClear(0xFF000000, 0, 0);

        // 1. Draw Counter-Clockwise triangle (vA -> vC -> vB) in RED (0x00FF0000)
        constantColor(0x00FF0000);
        drawTriangle(&vA, &vC, &vB);

        // Read back center: must remain black!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00000000); // Culled! (ignore alpha)

        // 2. Draw Clockwise triangle (vA -> vB -> vC) in BLUE (0x000000FF)
        constantColor(0x000000FF);
        drawTriangle(&vA, &vB, &vC);

        // Read back center: must be Blue (0x000000FF)
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x000000FF); // Rendered! (ignore alpha)

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Coordinate Origin Save/Restore") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using CloseWinFunc = void(*)(void);
        using OriginFunc = void(*)(GrOriginLocation_t);
        using GetStateFunc = void(*)(void*);
        using SetStateFunc = void(*)(const void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
        auto origin = reinterpret_cast<OriginFunc>(::dlsym(handle, "grSstOrigin"));
        auto getState = reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
        auto setState = reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(origin != nullptr);
        REQUIRE(getState != nullptr);
        REQUIRE(setState != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        // Force headless software reference backend for testing
        ::setenv("GLIDE_NG_BACKEND", "software", 1);

        glideInit();
        if (sstSelect) sstSelect(0);

        // Open window with LOWER_LEFT origin (1)
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
            GlideWrapper::BackendManager::GetInstance().GetBackend()
        );
        REQUIRE(backend != nullptr);
        REQUIRE(backend->GetSstOrigin() == 1); // Verify initial origin is indeed LOWER_LEFT (1)

        // 1. Save state (should capture origin=1)
        char savedState[1024];
        getState(savedState);

        // 2. Clobber origin to UPPER_LEFT (0) (simulating console overlay text render)
        origin(GR_ORIGIN_UPPER_LEFT);
        REQUIRE(backend->GetSstOrigin() == 0);

        // 3. Restore state: should restore origin back to 1!
        setState(savedState);
        REQUIRE(backend->GetSstOrigin() == 1);

        winClose();
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
    }
#endif // matches line 678
  }

}

#ifdef __linux__
TEST_CASE("Glide 3.x: Multi-Pass Pre-Fog Blending & FOGCOORD Extension", "[Glide3][Fog][PreFog]") {
  SECTION("Vulkan multi-pass pre-fog blending") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using FogModeFunc = void(*)(GrFogMode_t);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto blendFunc = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(blendFunc != nullptr);
    REQUIRE(fogMode != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless Vulkan backend for testing
    ::setenv("GLIDE_NG_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    if (sstSelect) sstSelect(0);
    unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 0);
    REQUIRE(ctx != 0);

    // Set vertex layout
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // Define vertices for a full-screen or centered quad
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    // Solid orange color: (1.0, 0.5, 0.0) -> R=255, G=128, B=0
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v2{ 700.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v3{ 100.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };

    // Clear buffer to Black
    bufferClear(0xFF000000, 0, 0xFFFF);

    // ==========================================
    // PASS 1: Render base color (Solid Orange)
    // ==========================================
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    blendFunc(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // Src * 1 + Dst * 0
    fogMode(0); // Disable fog

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // ==========================================
    // PASS 2: Render lightmap with PREFOG_COLOR blending
    // ==========================================
    // C_src = f * C_fog. We want C_src to be Black (0, 0, 0).
    // So we enable fog with MULT2 and Black fog color.
    // Pre-fog color should be Green (0.5, 1.0, 0.0) -> R=128, G=255, B=0.
    v0.r = 128.0f; v0.g = 255.0f; v0.b = 0.0f;
    v1.r = 128.0f; v1.g = 255.0f; v1.b = 0.0f;
    v2.r = 128.0f; v2.g = 255.0f; v2.b = 0.0f;
    v3.r = 128.0f; v3.g = 255.0f; v3.b = 0.0f;

    // Set constant fog color to Black
    using FogColorFunc = void(*)(GrColor_t);
    auto fogColorVal = reinterpret_cast<FogColorFunc>(::dlsym(handle, "grFogColorValue"));
    REQUIRE(fogColorVal != nullptr);
    fogColorVal(0x00000000); // Black

    // Configure fog: GR_FOG_MULT2 (0x100) | GR_FOG_WITH_ITERATED_ALPHA (0x1) -> f = alpha = 1.0f (so C_src = C_fog = Black)
    #define G3_FOG_MULT2 0x100
    fogMode(static_cast<GrFogMode_t>(G3_FOG_MULT2 | 0x1)); 

    // Configure blending: Src * 1 + Dst * Src_prefog (GR_BLEND_PREFOG_COLOR = 15)
    blendFunc(GR_BLEND_ONE, static_cast<GrAlphaBlendFnc_t>(15), GR_BLEND_ONE, GR_BLEND_ZERO);

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // Read back center pixel (400, 300)
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 400, 300, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);

    // Expected blended color:
    // Red = C_src.r + C_dest.r * C_prefog.r = 0.0 + 1.0 * 0.5 = 0.5 (128)
    // Green = C_src.g + C_dest.g * C_prefog.g = 0.0 + 0.5 * 1.0 = 0.5 (128)
    // Blue = C_src.b + C_dest.b * C_prefog.b = 0.0 + 0.0 * 0.0 = 0.0 (0)
    // Unpack colors (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixels[0] & 0xFF;
    uint8_t g = (pixels[0] >> 8) & 0xFF;
    uint8_t b = (pixels[0] >> 16) & 0xFF;

    INFO("Vulkan Blended Pixel Color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // Assert that color matches Orange/Yellow (128, 128, 0) with a tolerance of +-5
    REQUIRE(std::abs((int)r - 128) <= 5);
    REQUIRE(std::abs((int)g - 128) <= 5);
    REQUIRE(b <= 5);

    winClose();
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
  }

  SECTION("OpenGL ES multi-pass pre-fog blending") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using FogModeFunc = void(*)(GrFogMode_t);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto blendFunc = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(blendFunc != nullptr);
    REQUIRE(fogMode != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless OpenGL ES backend for testing
    ::setenv("GLIDE_NG_BACKEND", "opengl_es", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    if (sstSelect) sstSelect(0);
    unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Set vertex layout
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // Define vertices for a full-screen or centered quad
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    // Solid orange color: (1.0, 0.5, 0.0) -> R=255, G=128, B=0
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v2{ 700.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v3{ 100.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };

    // Clear buffer to Black
    bufferClear(0xFF000000, 0, 0xFFFF);

    // ==========================================
    // PASS 1: Render base color (Solid Orange)
    // ==========================================
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    blendFunc(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // Src * 1 + Dst * 0
    fogMode(0); // Disable fog

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // ==========================================
    // PASS 2: Render lightmap with PREFOG_COLOR blending
    // ==========================================
    // C_src = f * C_fog. We want C_src to be Black (0, 0, 0).
    // So we enable fog with MULT2 and Black fog color.
    // Pre-fog color should be Green (0.5, 1.0, 0.0) -> R=128, G=255, B=0.
    v0.r = 128.0f; v0.g = 255.0f; v0.b = 0.0f;
    v1.r = 128.0f; v1.g = 255.0f; v1.b = 0.0f;
    v2.r = 128.0f; v2.g = 255.0f; v2.b = 0.0f;
    v3.r = 128.0f; v3.g = 255.0f; v3.b = 0.0f;

    // Set constant fog color to Black
    using FogColorFunc = void(*)(GrColor_t);
    auto fogColorVal = reinterpret_cast<FogColorFunc>(::dlsym(handle, "grFogColorValue"));
    REQUIRE(fogColorVal != nullptr);
    fogColorVal(0x00000000); // Black

    // Configure fog: GR_FOG_MULT2 (0x100) | GR_FOG_WITH_ITERATED_ALPHA (0x1) -> f = alpha = 1.0f (so C_src = C_fog = Black)
    #define G3_FOG_MULT2 0x100
    fogMode(static_cast<GrFogMode_t>(G3_FOG_MULT2 | 0x1)); 

    // Configure blending: Src * 1 + Dst * Src_prefog (GR_BLEND_PREFOG_COLOR = 15)
    blendFunc(GR_BLEND_ONE, static_cast<GrAlphaBlendFnc_t>(15), GR_BLEND_ONE, GR_BLEND_ZERO);

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // Read back center pixel (400, 300)
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 400, 300, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);

    // Expected blended color:
    // Red = C_src.r + C_dest.r * C_prefog.r = 0.0 + 1.0 * 0.5 = 0.5 (128)
    // Green = C_src.g + C_dest.g * C_prefog.g = 0.0 + 0.5 * 1.0 = 0.5 (128)
    // Blue = C_src.b + C_dest.b * C_prefog.b = 0.0 + 0.0 * 0.0 = 0.0 (0)
    // Unpack colors (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixels[0] & 0xFF;
    uint8_t g = (pixels[0] >> 8) & 0xFF;
    uint8_t b = (pixels[0] >> 16) & 0xFF;

    INFO("GLES Blended Pixel Color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // Assert that color matches Orange/Yellow (128, 128, 0) with a tolerance of +-5
    REQUIRE(std::abs((int)r - 128) <= 5);
    REQUIRE(std::abs((int)g - 128) <= 5);
    REQUIRE(b <= 5);

    winClose();
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
  }

  SECTION("FOGCOORD extension & fog mode mapping") {
    // 1. Glide 3.x Verification
    SECTION("Glide 3.x FOGCOORD Support") {
#ifdef __linux__
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using SelectCtxFunc = FxBool(*)(FxU32);
        using GetStringFunc = const char*(*)(FxU32);
        using FogModeFunc = void(*)(FxU32);
        using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);
        using WinCloseFunc = FxBool(*)(FxU32);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
        auto getString = reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
        auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
        auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(selectCtx != nullptr);
        REQUIRE(getString != nullptr);
        REQUIRE(fogMode != nullptr);
        REQUIRE(vertexLayout != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        // Run with software backend to isolate
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
        REQUIRE(ctx != 0);
        REQUIRE(selectCtx(ctx) == FXTRUE);

        // A. Verify that FOGCOORD is advertised in the extension string
        const char* extensions = getString(0xa0); // GR_EXTENSION
        REQUIRE(extensions != nullptr);
        std::string extStr(extensions);
        CHECK(extStr.find("FOGCOORD") != std::string::npos);

        // B. Verify that grFogMode with Glide 3 table-on-fogcoord compiles/runs without crash
        // GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT = 0x1
        fogMode(0x1); 

        // C. Verify that grVertexLayout with GR_PARAM_FOG_EXT works
        // GR_PARAM_FOG_EXT = 0x05, offset = 28, mode = 1 (ENABLE)
        vertexLayout(0x05, 28, 1);

        winClose(ctx);
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
#endif
    }

    // 2. Glide 2.x Verification
    SECTION("Glide 2.x Pure ABI (No grGetString / No FOGCOORD)") {
#ifdef __linux__
        void* handle = OpenGlide2DsoHandle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using WinCloseFunc = FxBool(*)(void);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(::dlsym(handle, "grGetString") == nullptr);
        REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        // Resolution GR_RESOLUTION_640x480 = 0x4
        FxBool ok = winOpen(0, 0x4, 0, 0, 0, 2, 1);
        REQUIRE(ok == FXTRUE);

        winClose();
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
#endif
    }
  }

}
#endif

#ifdef __linux__
TEST_CASE("Glide 3.x: Retro Rasterization, Depth Blend & AA Modes", "[Glide3][Software][AA]") {
  SECTION("Software anti-aliasing (AA) modes") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using DrawPointFunc = void(*)(const void*);
    using DrawLineFunc = void(*)(const void*, const void*);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using EnableFunc = void(*)(FxU32);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void(*)(void);
    using AlphaBlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto drawPoint = reinterpret_cast<DrawPointFunc>(::dlsym(handle, "grDrawPoint"));
    auto drawLine  = reinterpret_cast<DrawLineFunc>(::dlsym(handle, "grDrawLine"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto enable    = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(drawPoint != nullptr);
    REQUIRE(drawLine != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(enable != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);

    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_A        0x10
    #define G3_PARAM_ENABLE   0x01
    #define GR_AA_ORDERED     0x01

    struct GlideVertex {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };

    SECTION("Option A: Analytical Point AA Coverage") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        ::setenv("GLIDE_NG_SW_AA", "analytical", 1);

        glideInit();
        if (sstSelect) sstSelect(0);
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
        vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
        // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
        vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
        vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
        vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
        vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        constantColor(0xFFFFFFFF); // Solid white
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                           GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);

        bufferClear(0x00000000, 0, 0);

        enable(GR_AA_ORDERED);

        GlideVertex v;
        v.x = 320.9f; v.y = 240.9f; v.z = 0.5f;
        v.ooz = 1.0f; v.a = 255.0f; v.oow = 1.0f;
        v.r = 255.0f; v.g = 255.0f; v.b = 255.0f;

        drawPoint(&v);

        uint32_t pixels[9];
        FxBool ok = lfbRead(GR_BUFFER_BACKBUFFER, 319, 239, 3, 3, 12, pixels);
        REQUIRE(ok == FXTRUE);

        uint32_t centerPixel = pixels[4] & 0x00FFFFFF;
        uint32_t rightPixel = pixels[5] & 0x00FFFFFF;
        uint32_t bottomPixel = pixels[7] & 0x00FFFFFF;

        CHECK(centerPixel > 0);
        CHECK(rightPixel > 0);
        CHECK(bottomPixel > 0);
        CHECK(rightPixel < 0x00FFFFFF);
        CHECK(bottomPixel < 0x00FFFFFF);

        winClose();
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
        ::unsetenv("GLIDE_NG_SW_AA");
    }

    SECTION("Option B: SSAA Resolution Scaling & Downsampling") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        ::setenv("GLIDE_NG_SW_AA", "ssaa", 1);

        glideInit();
        if (sstSelect) sstSelect(0);
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
            GlideWrapper::BackendManager::GetInstance().GetBackend()
        );
        REQUIRE(backend != nullptr);

        // Framebuffer dimensions must be scaled by 2x
        CHECK(backend->GetWidth() == 1280);
        CHECK(backend->GetHeight() == 960);

        winClose();
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
        ::unsetenv("GLIDE_NG_SW_AA");
    }
  }

  SECTION("Dithering, stipple & retro state features") {
#ifdef __linux__
    // Force headless software reference backend for testing
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using ShutdownFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);
    using DepthBufferModeFunc = void(*)(GrDepthBufferMode_t);
    using DepthBufferFunctionFunc = void(*)(GrCmpFnc_t);
    using DepthMaskFunc = void(*)(FxBool);
    using AlphaBlendFunctionFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using AlphaTestFunctionFunc = void(*)(GrCmpFnc_t);
    using AlphaTestReferenceValueFunc = void(*)(GrAlpha_t);
    using DitherModeFunc = void(*)(GrDitherMode_t);
    using StippleModeFunc = void(*)(GrStippleMode_t);
    using StipplePatternFunc = void(*)(GrStipplePattern_t);
    using DepthRangeFunc = void(*)(float, float);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using AlphaCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto drawTri   = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb   = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto depthBufferMode = reinterpret_cast<DepthBufferModeFunc>(::dlsym(handle, "grDepthBufferMode"));
    auto depthBufferFunction = reinterpret_cast<DepthBufferFunctionFunc>(::dlsym(handle, "grDepthBufferFunction"));
    auto depthMask = reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
    auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunctionFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto alphaTestFunction = reinterpret_cast<AlphaTestFunctionFunc>(::dlsym(handle, "grAlphaTestFunction"));
    auto alphaTestRef = reinterpret_cast<AlphaTestReferenceValueFunc>(::dlsym(handle, "grAlphaTestReferenceValue"));
    auto ditherMode = reinterpret_cast<DitherModeFunc>(::dlsym(handle, "grDitherMode"));
    auto stippleMode = reinterpret_cast<StippleModeFunc>(::dlsym(handle, "grStippleMode"));
    auto stipplePattern = reinterpret_cast<StipplePatternFunc>(::dlsym(handle, "grStipplePattern"));
    auto depthRange = reinterpret_cast<DepthRangeFunc>(::dlsym(handle, "grDepthRange"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<AlphaCombineFunc>(::dlsym(handle, "grAlphaCombine"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(depthBufferMode != nullptr);
    REQUIRE(depthBufferFunction != nullptr);
    REQUIRE(depthMask != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);
    REQUIRE(alphaTestFunction != nullptr);
    REQUIRE(alphaTestRef != nullptr);
    REQUIRE(ditherMode != nullptr);
    REQUIRE(stippleMode != nullptr);
    REQUIRE(stipplePattern != nullptr);
    REQUIRE(depthRange != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(alphaCombine != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend()
    );
    REQUIRE(backend != nullptr);

    // Canonical vertex layout for StandardVert
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W (oow) at 32

    // Set simple color combiner: output vertex iterated color
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    SECTION("Depth Buffering (Z vs W, compare ops, depth mask)") {
        // 1. Z-buffering test
        depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);

        bufferClear(0x00000000, 0, 0xFFFF); // Clear depth to 1.0f

        // Draw green triangle at Z = 0.5f (close)
        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        // Draw red triangle at Z = 0.75f (farther, should be occluded)
        StandardVert v3 = { 100.0f, 100.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v4 = { 200.0f, 100.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v5 = { 150.0f, 200.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        drawTri(&v3, &v4, &v5);

        // Verify pixel at center (150, 130) is Green (not Red)
        uint32_t px = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        uint8_t gChan = (px >> 8) & 0xFF;
        CHECK(gChan > 240);
        
        // 2. Depth mask test
        // Authentic Glide grBufferClear obeys grDepthMask (SST_ZAWRMASK), so
        // clear depth to 1.0f while depthMask is enabled, then disable writes.
        bufferClear(0x00000000, 0, 0xFFFF);
        depthMask(FXFALSE); // disable depth writes

        // Draw green triangle at Z = 0.5f again
        drawTri(&v0, &v1, &v2);
        // Verify depth buffer pixel was NOT written (should still be 1.0f)
        float dbPixel = backend->GetDepthBufferPixel(150, 130);
        CHECK(dbPixel == 1.0f);

        // 3. W-buffering test
        depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);
        depthRange(2.0f, 10.0f);
        bufferClear(0x00000000, 0, 0xFFFF); // Clears depth to 1.0f

        // Draw green triangle with W = 4.0f (oow = 0.25f) -> TargetDepth = (4-2)/8 = 0.25f
        v0.oow = 0.25f; v1.oow = 0.25f; v2.oow = 0.25f;
        drawTri(&v0, &v1, &v2);

        float dbWVal = backend->GetDepthBufferPixel(150, 130);
        CHECK(std::abs(dbWVal - 0.25f) < 0.001f);
    }

    SECTION("Alpha Blending and Testing") {
        bufferClear(0x00000000, 0, 0xFFFF);
        
        // 1. Alpha blending test: Src = Src Alpha, Dst = One Minus Src Alpha
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                           GR_BLEND_ONE, GR_BLEND_ZERO);

        // Draw flat blue triangle with Alpha = 0.5f (128)
        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        uint32_t px = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        uint8_t b1 = px & 0xFF;
        uint8_t b3 = (px >> 16) & 0xFF;
        bool blendOk = (b1 > 110 && b1 < 140) || (b3 > 110 && b3 < 140);
        CHECK(blendOk);

        // 2. Alpha testing test
        bufferClear(0x00000000, 0, 0xFFFF);
        alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // no blend

        // Enable alpha test: pass if Alpha > 150
        alphaTestFunction(GR_CMP_GREATER);
        alphaTestRef(150);

        // Draw flat red triangle with Alpha = 100 (should be discarded)
        v0.r = 255.0f; v0.g = 0.0f; v0.b = 0.0f; v0.a = 100.0f;
        v1.r = 255.0f; v1.g = 0.0f; v1.b = 0.0f; v1.a = 100.0f;
        v2.r = 255.0f; v2.g = 0.0f; v2.b = 0.0f; v2.a = 100.0f;
        drawTri(&v0, &v1, &v2);

        px = 0xFFFFFFFF;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        CHECK((px & 0x00FFFFFF) == 0); // Color is black

        // Draw flat red triangle with Alpha = 200 (should pass)
        v0.a = 200.0f; v1.a = 200.0f; v2.a = 200.0f;
        drawTri(&v0, &v1, &v2);

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        b1 = px & 0xFF;
        b3 = (px >> 16) & 0xFF;
        bool redHigh = (b1 > 240) || (b3 > 240);
        CHECK(redHigh); // Red is high
        
        alphaTestFunction(GR_CMP_ALWAYS);
    }

    SECTION("Dithering (16-bit retro dithering)") {
        bufferClear(0x00000000, 0, 0xFFFF);
        ditherMode(GR_DITHER_4x4);

        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        std::vector<uint32_t> block(16);
        for (int yOffset = 0; yOffset < 4; ++yOffset) {
            for (int xOffset = 0; xOffset < 4; ++xOffset) {
                readLfb(GR_BUFFER_BACKBUFFER, 148 + xOffset, 128 + yOffset, 1, 1, 4, &block[yOffset * 4 + xOffset]);
            }
        }

        bool foundDifference = false;
        uint32_t firstPixel = block[0] & 0x00FFFFFF;
        for (int i = 1; i < 16; ++i) {
            if ((block[i] & 0x00FFFFFF) != firstPixel) {
                foundDifference = true;
                break;
            }
        }
        if (!foundDifference) {
            std::cout << "[DITHER_DEBUG] All 16 pixels are identical: 0x" << std::hex << firstPixel << std::dec << std::endl;
            for (int i = 0; i < 16; ++i) {
                std::cout << "  Pixel " << i << ": 0x" << std::hex << block[i] << std::dec << std::endl;
            }
        }
        CHECK(foundDifference == true);

        ditherMode(GR_DITHER_DISABLE);
    }

    SECTION("Stipple Transparency") {
        bufferClear(0x00000000, 0, 0xFFFF);
        stippleMode(GR_STIPPLE_PATTERN);
        stipplePattern(0x55AA55AA);

        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        uint32_t px150 = 0;
        uint32_t px151 = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px150) == FXTRUE);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 151, 130, 1, 1, 4, &px151) == FXTRUE);

        if ((px150 & 0x00FFFFFF) == 0 || (px151 & 0x00FFFFFF) != 0) {
            std::cout << "[STIPPLE_DEBUG] Stipple mismatch!" << std::endl;
            std::cout << "  Pixel (150, 130): 0x" << std::hex << px150 << std::dec << std::endl;
            std::cout << "  Pixel (151, 130): 0x" << std::hex << px151 << std::dec << std::endl;
            std::cout << "  Backend Stipple Mode: " << backend->GetStippleMode() << std::endl;
            std::cout << "  Backend Stipple Pattern: 0x" << std::hex << backend->GetStipplePattern() << std::dec << std::endl;
        }

        CHECK((px150 & 0x00FFFFFF) != 0);
        CHECK((px151 & 0x00FFFFFF) == 0);

        stippleMode(0);
    }

    winClose();
    shutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
#endif
  }

  SECTION("Z-buffered blended triangle stack (software rasterizer check)") {

  const char* prevHeadless = std::getenv("GLIDE_NG_HEADLESS");
  const char* prevBackend = std::getenv("GLIDE_NG_BACKEND");
  const std::string savedHeadless = prevHeadless ? prevHeadless : "";
  const std::string savedBackend = prevBackend ? prevBackend : "";
  ::setenv("GLIDE_NG_HEADLESS", "1", 1);
  ::setenv("GLIDE_NG_BACKEND", "software", 1);

  struct SwValVert {
    float x, y, z;
    float r, g, b, a;
  };

  grGlideInit();
  GrContext_t ctx = grSstWinOpen(
      0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
      GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_RGB, 12, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A, 24, GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  grDitherMode(GR_DITHER_DISABLE);
  grStippleMode(GR_STIPPLE_DISABLE);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grBufferClear(0xFF111111u, 0xFFu, 0xFFFFu);

  // Foreground RGB triangle at Z=0.5, background CMY triangle at Z=0.7,
  // semi-transparent white overlay quad at Z=0.3 (formerly sw_validate.cpp).
  SwValVert v0{100.0f, 100.0f, 0.5f, 255.0f, 0.0f, 0.0f, 255.0f};
  SwValVert v1{300.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 255.0f};
  SwValVert v2{200.0f, 300.0f, 0.5f, 0.0f, 0.0f, 255.0f, 255.0f};
  grDrawTriangle(&v0, &v1, &v2);

  SwValVert v3{150.0f, 150.0f, 0.7f, 0.0f, 255.0f, 255.0f, 255.0f};
  SwValVert v4{350.0f, 150.0f, 0.7f, 255.0f, 0.0f, 255.0f, 255.0f};
  SwValVert v5{250.0f, 350.0f, 0.7f, 255.0f, 255.0f, 0.0f, 255.0f};
  grDrawTriangle(&v3, &v4, &v5);

  SwValVert q0{50.0f, 200.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f};
  SwValVert q1{250.0f, 200.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f};
  SwValVert q2{250.0f, 400.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f};
  SwValVert q3{50.0f, 400.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f};
  grDrawTriangle(&q0, &q1, &q2);
  grDrawTriangle(&q0, &q2, &q3);

  uint32_t clearPx = 0, fgPx = 0, blendPx = 0;
  REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 10, 10, 1, 1, 4, &clearPx) == FXTRUE);
  REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 200, 140, 1, 1, 4, &fgPx) == FXTRUE);
  REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 100, 300, 1, 1, 4, &blendPx) == FXTRUE);
  CHECK((clearPx & 0x00FFFFFFu) != (fgPx & 0x00FFFFFFu));
  CHECK((blendPx & 0xFFu) > (clearPx & 0xFFu));

  grSstWinClose(ctx);
  grGlideShutdown();

  if (prevHeadless) ::setenv("GLIDE_NG_HEADLESS", savedHeadless.c_str(), 1);
  else ::unsetenv("GLIDE_NG_HEADLESS");
  if (prevBackend) ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  else ::unsetenv("GLIDE_NG_BACKEND");

  }

}
#endif

TEST_CASE("Glide 3.x: GR_CLIP_COORDS Homogeneous Clipping & Projection", "[Glide3][ClipCoords]") {
  using namespace GlideWrapper;

  SECTION("VertexLayout::DecodeClipVertex separates W, Q, Q0, Q1 and normalizes [0..1] colors") {
    auto& vl = VertexLayout::GetInstance();
    vl.ResetAllParamsDisabled();

    struct CustomClipVert {
      float x, y;
      float garbageAtOffset8;  // Unbound GR_PARAM_Z must ignore this!
      float w;
      float q;
      float r, g, b, a;
      float s0, t0, q0;
      float s1, t1, q1;
    };

    vl.SetParamOffsetGlide3(GR_PARAM_XY, offsetof(CustomClipVert, x), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_W, offsetof(CustomClipVert, w), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_Q, offsetof(CustomClipVert, q), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_RGB, offsetof(CustomClipVert, r), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_A, offsetof(CustomClipVert, a), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_ST0, offsetof(CustomClipVert, s0), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_Q0, offsetof(CustomClipVert, q0), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_ST1, offsetof(CustomClipVert, s1), GR_PARAM_ENABLE);
    vl.SetParamOffsetGlide3(GR_PARAM_Q1, offsetof(CustomClipVert, q1), GR_PARAM_ENABLE);

    CHECK_FALSE(vl.HasExplicitZ());
    CHECK(vl.HasParamQ());
    CHECK(vl.HasParamQ0());
    CHECK(vl.HasParamQ1());
    CHECK((vl.GetEffectiveStwHintMask() & 0xAu) == 0xAu);

    CustomClipVert src{};
    src.x = -1.5f;
    src.y = 0.75f;
    src.garbageAtOffset8 = 9999.0f;
    src.w = 4.0f;
    src.q = 0.25f;
    src.r = 0.2f;
    src.g = 0.6f;
    src.b = 1.5f;   // Clamped to 1.0f
    src.a = -0.2f;  // Clamped to 0.0f
    src.s0 = 0.5f;
    src.t0 = 0.75f;
    src.q0 = 2.0f;
    src.s1 = 0.25f;
    src.t1 = 0.125f;
    src.q1 = 3.0f;

    auto near = [](float x, float y) { return std::fabs(x - y) < 1e-5f; };
    const ClipVertex cv = vl.DecodeClipVertex(&src);
    CHECK(near(cv.x, -1.5f));
    CHECK(near(cv.y, 0.75f));
    CHECK(near(cv.z, 0.0f));
    CHECK(near(cv.w, 4.0f));
    CHECK(near(cv.q, 0.25f));
    CHECK(near(cv.r, 0.2f));
    CHECK(near(cv.g, 0.6f));
    CHECK(near(cv.b, 1.0f));
    CHECK(near(cv.a, 0.0f));
    CHECK(near(cv.s0, 0.5f));
    CHECK(near(cv.t0, 0.75f));
    CHECK(near(cv.q0, 2.0f));
    CHECK(near(cv.s1, 0.25f));
    CHECK(near(cv.t1, 0.125f));
    CHECK(near(cv.q1, 3.0f));

    // Verify GetLayoutState / SetLayoutState preserves independent W and Q
    uint8_t savedLayout[256] = {};
    vl.GetLayoutState(savedLayout);
    vl.ResetAllParamsDisabled();
    CHECK_FALSE(vl.HasParamQ());
    vl.SetLayoutState(savedLayout);
    CHECK(vl.HasParamQ());
    CHECK(vl.HasParamQ0());
    const ClipVertex cv2 = vl.DecodeClipVertex(&src);
    CHECK(near(cv2.w, 4.0f));
    CHECK(near(cv2.q, 0.25f));
    CHECK(near(cv2.q0, 2.0f));

    vl.ResetToGlide2Canonical();
  }

#ifdef __linux__
  SECTION("End-to-End GR_CLIP_COORDS Frustum Clipping, Aspect UV & Orientation Parity") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)();
    using ShutdownFunc = void (*)();
    using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = FxBool (*)(FxU32);
    using ClearFunc = void (*)(FxU32, FxU32, FxU32);
    using CoordSpaceFunc = void (*)(FxU32);
    using ViewportFunc = void (*)(FxI32, FxI32, FxI32, FxI32);
    using DepthRangeFunc = void (*)(FxFloat, FxFloat);
    using ResetFunc = void (*)(FxU32);
    using LayoutFunc = void (*)(FxU32, FxI32, FxU32);
    using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                      GrCombineLocal_t, GrCombineOther_t, FxBool);
    using TexCombineFunc = void (*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using TexDownloadFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexFilterFunc = void (*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using TexClampFunc = void (*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using DrawContigFunc = void (*)(FxU32, FxU32, void*, FxU32);
    using ReadLfbFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
    auto coordSpace = reinterpret_cast<CoordSpaceFunc>(::dlsym(handle, "grCoordinateSpace"));
    auto viewport = reinterpret_cast<ViewportFunc>(::dlsym(handle, "grViewport"));
    auto depthRange = reinterpret_cast<DepthRangeFunc>(::dlsym(handle, "grDepthRange"));
    auto resetFn = reinterpret_cast<ResetFunc>(::dlsym(handle, "grReset"));
    auto vertexLayout = reinterpret_cast<LayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto texDownload = reinterpret_cast<TexDownloadFunc>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texFilter = reinterpret_cast<TexFilterFunc>(::dlsym(handle, "grTexFilterMode"));
    auto texClamp = reinterpret_cast<TexClampFunc>(::dlsym(handle, "grTexClampMode"));
    auto drawContig = reinterpret_cast<DrawContigFunc>(::dlsym(handle, "grDrawVertexArrayContiguous"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(drawContig != nullptr);
    REQUIRE(resetFn != nullptr);
    REQUIRE(vertexLayout != nullptr);

    struct HitmanVert {
      float x, y, w, q;
      float r, g, b, a;
      float s0, t0, q0;
    };

    // Non-square 64x32 RGB565 texture (GR_ASPECT_LOG2_2x1):
    // left half (x < 32) = pure red (0xF800), right half (x >= 32) = pure blue (0x001F).
    std::vector<uint16_t> tex64x32(64u * 32u, 0u);
    for (uint32_t y = 0u; y < 32u; ++y) {
      for (uint32_t x = 0u; x < 64u; ++x) {
        tex64x32[y * 64u + x] = (x < 32u) ? 0xF800u : 0x001Fu;
      }
    }

    for (const char* beName : {"software", "vulkan", "opengl_es"}) {
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
      ::setenv("GLIDE_NG_BACKEND", beName, 1);
      ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);

      glideInit();
      FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
      REQUIRE(ctx != 0u);

      coordSpace(GR_CLIP_COORDS);
      viewport(0, 0, 640, 480);
      depthRange(0.0f, 1.0f);

      resetFn(GR_VERTEX_PARAMETER);
      vertexLayout(GR_PARAM_XY, offsetof(HitmanVert, x), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_W, offsetof(HitmanVert, w), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_Q, offsetof(HitmanVert, q), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_RGB, offsetof(HitmanVert, r), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_A, offsetof(HitmanVert, a), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_ST0, offsetof(HitmanVert, s0), GR_PARAM_ENABLE);
      vertexLayout(GR_PARAM_Q0, offsetof(HitmanVert, q0), GR_PARAM_ENABLE);

      // 1) Behind-camera near-plane Sutherland-Hodgman clipping + normalized [0..1] RGB:
      //    Triangle spans from w = -1.0 behind camera to w = 2.0 on upper half of screen
      //    (y_ndc in [0.0..1.0], which under GR_ORIGIN_UPPER_LEFT maps to rows [240..480]).
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
      bufferClear(0xFF000000u, 255u, 0xFFFFu);

      HitmanVert clipTri[3] = {
          // Behind camera (w = -1.0f) — crosses left frustum plane x+w=0 at t=0.5 (w=+0.5, x_ndc=-1)
          {-2.0f, 0.0f, -1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
          // Visible right-lower vertex (x_ndc = +0.5 -> x=480, y_ndc = -0.25 -> y=180)
          { 1.0f, -0.5f, 2.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
          // Visible right-upper vertex (x_ndc = +0.5 -> x=480, y_ndc = +0.75 -> y=420)
          { 1.0f,  1.5f, 2.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
      };
      drawContig(GR_TRIANGLES, 3u, clipTri, sizeof(HitmanVert));

      std::vector<uint16_t> fb16(640u * 480u, 0u);
      REQUIRE(readLfb(1u, 0u, 0u, 640u, 480u, 640u * 2u, fb16.data()) == FXTRUE);

      auto rgb565R = [](uint16_t p) { return ((p >> 11) & 0x1Fu) * 255u / 31u; };
      auto rgb565G = [](uint16_t p) { return ((p >> 5) & 0x3Fu) * 255u / 63u; };
      auto rgb565B = [](uint16_t p) { return (p & 0x1Fu) * 255u / 31u; };

      INFO("backend=" << beName);
      // Sample inside the visible clipped portion at center (x=320, y=260) -> bright green
      const uint16_t clippedPx = fb16[260u * 640u + 320u];
      CHECK(rgb565G(clippedPx) > 220u);
      CHECK(rgb565R(clippedPx) < 20u);
      CHECK(rgb565B(clippedPx) < 20u);
      // Right margin (x=580, y=40) must remain cleared black (no inverted wrap from w < 0)
      const uint16_t bgPx = fb16[40u * 640u + 580u];
      CHECK(rgb565G(bgPx) < 20u);

      // 2) Aspect-adjusted [0..1] UV scaling on 64x32 (GR_ASPECT_LOG2_2x1) texture
      //    with homogeneous w=2.0 and independent q=0.25, q0=1.0.
      GrTexInfo ti{};
      ti.smallLodLog2 = GR_LOD_LOG2_64;
      ti.largeLodLog2 = GR_LOD_LOG2_64;
      ti.aspectRatioLog2 = GR_ASPECT_LOG2_2x1;
      ti.format = GR_TEXFMT_RGB_565;
      ti.data = tex64x32.data();
      texDownload(GR_TMU0, 0x10000u, GR_MIPMAPLEVELMASK_BOTH, &ti);
      texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
      texClamp(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
      texSource(GR_TMU0, 0x10000u, GR_MIPMAPLEVELMASK_BOTH, &ti);
      texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
      colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
      bufferClear(0xFF000000u, 255u, 0xFFFFu);

      // Full-screen clip-space quad at w=2.0 (x,y in [-2..+2]), s0,t0 in [0..1], q=0.25, q0=1.0
      HitmanVert quad[6] = {
          {-2.0f, -2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
          { 2.0f, -2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
          { 2.0f,  2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
          {-2.0f, -2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
          { 2.0f,  2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
          {-2.0f,  2.0f, 2.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
      };
      drawContig(GR_TRIANGLES, 6u, quad, sizeof(HitmanVert));

      REQUIRE(readLfb(1u, 0u, 0u, 640u, 480u, 640u * 2u, fb16.data()) == FXTRUE);
      // Left quarter (u = 0.25) must be pure red; right quarter (u = 0.75) must be pure blue
      const uint16_t leftPx = fb16[240u * 640u + 160u];
      const uint16_t rightPx = fb16[240u * 640u + 480u];
      CHECK(rgb565R(leftPx) > 220u);
      CHECK(rgb565B(leftPx) < 20u);
      CHECK(rgb565B(rightPx) > 220u);
      CHECK(rgb565R(rightPx) < 20u);

      winClose(ctx);
      glideShutdown();
      ::unsetenv("GLIDE_NG_BACKEND");
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }

    ::dlclose(handle);
  }
#endif
}

TEST_CASE("Glide 3.x: Packed GrColorFormat RGBA & BGRA Parity", "[Glide3][ColorFormat]") {
  using namespace GlideWrapper;

  SECTION("GrColorToCanonicalArgb / CanonicalArgbToGrColor / UnpackGrColorNorm") {
    // Canonical ARGB with distinct bytes: A=0x78, R=0x12, G=0x34, B=0x56
    constexpr uint32_t kArgb = 0x78123456u;
    constexpr uint32_t kAbgr = 0x78563412u;
    constexpr uint32_t kRgba = 0x12345678u;
    constexpr uint32_t kBgra = 0x56341278u;

    CHECK(GrColorToCanonicalArgb(kArgb, 0u) == kArgb);
    CHECK(GrColorToCanonicalArgb(kAbgr, 1u) == kArgb);
    CHECK(GrColorToCanonicalArgb(kRgba, 2u) == kArgb);
    CHECK(GrColorToCanonicalArgb(kBgra, 3u) == kArgb);

    CHECK(CanonicalArgbToGrColor(kArgb, 0u) == kArgb);
    CHECK(CanonicalArgbToGrColor(kArgb, 1u) == kAbgr);
    CHECK(CanonicalArgbToGrColor(kArgb, 2u) == kRgba);
    CHECK(CanonicalArgbToGrColor(kArgb, 3u) == kBgra);

    // Hitman: Codename 47 Main Menu white fog (GR_COLORFORMAT_RGBA: 0xFFFFFF00)
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    UnpackGrColorNorm(0xFFFFFF00u, 2u, r, g, b, a);
    CHECK(std::fabs(r - 1.0f) < 1e-5f);
    CHECK(std::fabs(g - 1.0f) < 1e-5f);
    CHECK(std::fabs(b - 1.0f) < 1e-5f);
    CHECK(std::fabs(a - 0.0f) < 1e-5f);
  }

#ifdef __linux__
  SECTION("End-to-End GR_COLORFORMAT_RGBA and GR_COLORFORMAT_BGRA Rendering Parity") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)();
    using ShutdownFunc = void (*)();
    using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = FxBool (*)(FxU32);
    using ClearFunc = void (*)(FxU32, FxU32, FxU32);
    using ConstColorFunc = void (*)(GrColor_t);
    using FogColorFunc = void (*)(GrColor_t);
    using FogModeFunc = void (*)(GrFogMode_t);
    using FogTableFunc = void (*)(const GrFog_t*);
    using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                      GrCombineLocal_t, GrCombineOther_t, FxBool);
    using DrawTriFunc = void (*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
    auto constColor = reinterpret_cast<ConstColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto fogColor = reinterpret_cast<FogColorFunc>(::dlsym(handle, "grFogColorValue"));
    auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
    auto fogTable = reinterpret_cast<FogTableFunc>(::dlsym(handle, "grFogTable"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(fogColor != nullptr);
    REQUIRE(fogTable != nullptr);

    auto rgb565R = [](uint16_t p) { return ((p >> 11) & 0x1Fu) * 255u / 31u; };
    auto rgb565G = [](uint16_t p) { return ((p >> 5) & 0x3Fu) * 255u / 63u; };
    auto rgb565B = [](uint16_t p) { return (p & 0x1Fu) * 255u / 31u; };

    GrFog_t fullFog[64];
    std::fill(std::begin(fullFog), std::end(fullFog), static_cast<GrFog_t>(255u));

    for (const char* beName : {"software", "vulkan", "opengl_es"}) {
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
      ::setenv("GLIDE_NG_BACKEND", beName, 1);

      // 1) GR_COLORFORMAT_RGBA (2): Clear=Red (0xFF0000FF), Const=Blue (0x0000FFFF),
      //    Fog=White (0xFFFFFF00 — Hitman Main Menu regression!)
      glideInit();
      FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_RGBA, GR_ORIGIN_UPPER_LEFT, 2, 1);
      REQUIRE(ctx != 0u);

      bufferClear(0xFF0000FFu, 255u, 0xFFFFu);
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

      // Draw constant-color blue quad in middle third [x=200..400, y=100..300]
      fogMode(GR_FOG_DISABLE);
      constColor(0x0000FFFFu);  // RGBA pure blue
      StandardVert b0 = {200.0f, 100.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert b1 = {400.0f, 100.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert b2 = {400.0f, 300.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert b3 = {200.0f, 300.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      drawTri(&b0, &b1, &b2);
      drawTri(&b0, &b2, &b3);

      // Draw 100%-fogged quad in right third [x=440..600, y=100..300] with RGBA white 0xFFFFFF00
      fogColor(0xFFFFFF00u);
      fogTable(fullFog);
      fogMode(GR_FOG_WITH_TABLE_ON_Q);
      constColor(0x000000FFu);  // Black base -> 100% fog color output
      StandardVert f0 = {440.0f, 100.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 0.1f, 0.0f, 0.0f, 1.0f};
      StandardVert f1 = {600.0f, 100.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 0.1f, 0.0f, 0.0f, 1.0f};
      StandardVert f2 = {600.0f, 300.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 0.1f, 0.0f, 0.0f, 1.0f};
      StandardVert f3 = {440.0f, 300.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 255.0f, 0.1f, 0.0f, 0.0f, 1.0f};
      drawTri(&f0, &f1, &f2);
      drawTri(&f0, &f2, &f3);

      std::vector<uint16_t> fb16(640u * 480u, 0u);
      std::vector<uint32_t> fb32(640u * 480u, 0u);
      REQUIRE(readLfb(1u, 0u, 0u, 640u, 480u, 640u * 2u, fb16.data()) == FXTRUE);
      REQUIRE(readLfb(1u, 0u, 0u, 640u, 480u, 640u * 4u, fb32.data()) == FXTRUE);

      INFO("backend=" << beName << " format=RGBA");
      // Clear pixel (x=80, y=200): pure red
      const uint16_t clear16 = fb16[200u * 640u + 80u];
      CHECK(rgb565R(clear16) > 220u);
      CHECK(rgb565G(clear16) < 20u);
      CHECK(rgb565B(clear16) < 20u);
      // 32-bit RGBA swizzle check: R in bits 24..31, B in bits 8..15
      const uint32_t clear32 = fb32[200u * 640u + 80u];
      CHECK(((clear32 >> 24) & 0xFFu) > 220u);
      CHECK(((clear32 >> 8) & 0xFFu) < 20u);

      // Constant-color pixel (x=300, y=200): pure blue
      const uint16_t blue16 = fb16[200u * 640u + 300u];
      CHECK(rgb565B(blue16) > 220u);
      CHECK(rgb565R(blue16) < 20u);
      CHECK(rgb565G(blue16) < 20u);

      // Fogged pixel (x=520, y=200): pure WHITE (not yellow!)
      const uint16_t fog16 = fb16[200u * 640u + 520u];
      CHECK(rgb565R(fog16) > 220u);
      CHECK(rgb565G(fog16) > 220u);
      CHECK(rgb565B(fog16) > 220u);

      winClose(ctx);
      glideShutdown();

      // 2) GR_COLORFORMAT_BGRA (3): Clear=Red (0x0000FFFF), Const=Blue (0xFF0000FF)
      glideInit();
      ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                    GR_COLORFORMAT_BGRA, GR_ORIGIN_UPPER_LEFT, 2, 1);
      REQUIRE(ctx != 0u);

      bufferClear(0x0000FFFFu, 255u, 0xFFFFu);  // BGRA pure red
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
      constColor(0xFF0000FFu);  // BGRA pure blue
      drawTri(&b0, &b1, &b2);
      drawTri(&b0, &b2, &b3);

      REQUIRE(readLfb(1u, 0u, 0u, 640u, 480u, 640u * 2u, fb16.data()) == FXTRUE);
      INFO("backend=" << beName << " format=BGRA");
      const uint16_t bgraClear16 = fb16[200u * 640u + 80u];
      CHECK(rgb565R(bgraClear16) > 220u);
      CHECK(rgb565B(bgraClear16) < 20u);
      const uint16_t bgraBlue16 = fb16[200u * 640u + 300u];
      CHECK(rgb565B(bgraBlue16) > 220u);
      CHECK(rgb565R(bgraBlue16) < 20u);

      winClose(ctx);
      glideShutdown();
      ::unsetenv("GLIDE_NG_BACKEND");
    }

    ::dlclose(handle);
  }
#endif
}

TEST_CASE("Glide 3.x: Textured Lines & Points Combiner Parity", "[Glide3][Lines][Combiner]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using GlideInitFn = void (*)();
  using GlideShutdownFn = void (*)();
  using SstSelectFn = void (*)(int);
  using SstWinOpenFn = GrContext_t (*)(FxU32, GrScreenResolution_t,
                                       GrScreenRefresh_t, GrColorFormat_t,
                                       GrOriginLocation_t, int, int);
  using SstWinCloseFn = FxBool (*)(GrContext_t);
  using VertexLayoutFn = void (*)(FxU32, FxI32, FxU32);
  using ResetTriFn = void (*)();
  using BufferClearFn = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using DepthModeFn = void (*)(GrDepthBufferMode_t);
  using ColorCombineFn = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                  GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaCombineFn = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                  GrCombineLocal_t, GrCombineOther_t, FxBool);
  using TexCombineFn = void (*)(GrChipID_t, GrCombineFunction_t,
                                GrCombineFactor_t, GrCombineFunction_t,
                                GrCombineFactor_t, FxBool, FxBool);
  using TexMinAddrFn = FxU32 (*)(GrChipID_t);
  using TexDownloadFn = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexSourceFn = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using AlphaBlendFn = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using DrawLineFn = void (*)(const void*, const void*);
  using DrawPointFn = void (*)(const void*);
  using ReadRegionFn = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                  void*);

  auto glideInit = reinterpret_cast<GlideInitFn>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown = reinterpret_cast<GlideShutdownFn>(::dlsym(handle, "grGlideShutdown"));
  auto sstSelect = reinterpret_cast<SstSelectFn>(::dlsym(handle, "grSstSelect"));
  auto winOpen = reinterpret_cast<SstWinOpenFn>(::dlsym(handle, "grSstWinOpen"));
  auto winClose = reinterpret_cast<SstWinCloseFn>(::dlsym(handle, "grSstWinClose"));
  auto vtxLayout = reinterpret_cast<VertexLayoutFn>(::dlsym(handle, "grVertexLayout"));
  auto resetLayout = reinterpret_cast<ResetTriFn>(::dlsym(handle, "grReset"));
  auto bufClear = reinterpret_cast<BufferClearFn>(::dlsym(handle, "grBufferClear"));
  auto depthMode = reinterpret_cast<DepthModeFn>(::dlsym(handle, "grDepthBufferMode"));
  auto colorCombine = reinterpret_cast<ColorCombineFn>(::dlsym(handle, "grColorCombine"));
  auto alphaCombine = reinterpret_cast<AlphaCombineFn>(::dlsym(handle, "grAlphaCombine"));
  auto texCombine = reinterpret_cast<TexCombineFn>(::dlsym(handle, "grTexCombine"));
  auto texMinAddr = reinterpret_cast<TexMinAddrFn>(::dlsym(handle, "grTexMinAddress"));
  auto texDownload = reinterpret_cast<TexDownloadFn>(::dlsym(handle, "grTexDownloadMipMap"));
  auto texSource = reinterpret_cast<TexSourceFn>(::dlsym(handle, "grTexSource"));
  auto alphaBlend = reinterpret_cast<AlphaBlendFn>(::dlsym(handle, "grAlphaBlendFunction"));
  auto drawLine = reinterpret_cast<DrawLineFn>(::dlsym(handle, "grDrawLine"));
  auto drawPoint = reinterpret_cast<DrawPointFn>(::dlsym(handle, "grDrawPoint"));
  auto readRegion = reinterpret_cast<ReadRegionFn>(::dlsym(handle, "grLfbReadRegion"));

  struct TexVtx {
    float x, y, z, oow;
    float r, g, b, a;
    float sow, tow;
  };
  auto rgb565R = [](uint16_t p) -> uint32_t { return ((p >> 11) & 0x1Fu) * 255u / 31u; };
  auto rgb565G = [](uint16_t p) -> uint32_t { return ((p >> 5) & 0x3Fu) * 255u / 63u; };
  auto rgb565B = [](uint16_t p) -> uint32_t { return (p & 0x1Fu) * 255u / 31u; };

  std::vector<uint16_t> solidGreenTex(8 * 8, 0x07E0u);  // RGB565 pure green
  for (const char* be : {"software", "vulkan", "opengl_es"}) {
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_BACKEND", be, 1);
    glideInit();
    sstSelect(0);
    GrContext_t ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
      glideShutdown();
      continue;
    }

    (void)resetLayout;
    vtxLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_W, 12, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_RGB, 16, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_A, 28, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_ST0, 32, GR_PARAM_ENABLE);

    depthMode(GR_DEPTHBUFFER_DISABLE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    bufClear(0xFF000000u, 0, 0xFFFFu);

    GrTexInfo info{};
    info.smallLodLog2 = GR_LOD_LOG2_8;
    info.largeLodLog2 = GR_LOD_LOG2_8;
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = solidGreenTex.data();
    const FxU32 addr = texMinAddr(GR_TMU0);
    texDownload(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);

    // Modulate solid green texture by WHITE vertex color -> line/point MUST be
    // green (0, ~255, 0), NOT white (255, 255, 255)! (Lands of Lore 2 bug fix)
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

    TexVtx a{100.5f, 120.5f, 0.5f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 16.0f, 16.0f};
    TexVtx b{300.5f, 120.5f, 0.5f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 240.0f, 240.0f};
    drawLine(&a, &b);

    TexVtx pt{200.5f, 200.5f, 0.5f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 128.0f, 128.0f};
    drawPoint(&pt);

    uint16_t linePx = 0, ptPx = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 200, 120, 1, 1, 2, &linePx) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 200, 200, 1, 1, 2, &ptPx) == FXTRUE);
    INFO("backend=" << be << " linePx=0x" << std::hex << linePx << " ptPx=0x" << ptPx);
    CHECK(rgb565G(linePx) > 200u);
    CHECK(rgb565R(linePx) < 25u);
    CHECK(rgb565B(linePx) < 25u);
    CHECK(rgb565G(ptPx) > 200u);
    CHECK(rgb565R(ptPx) < 25u);
    CHECK(rgb565B(ptPx) < 25u);

    winClose(ctx);
    glideShutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
  }
  ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x: Batched VertexArray Triangle, Strip, Fan & Polygon Parity",
          "[Glide3][Geometry][VertexArray]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using ShutdownFunc = void (*)(void);
  using BufClearFunc = void (*)(FxU32, FxU32, FxU32);
  using ColorCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ConstColorFunc = void (*)(FxU32);
  using CullModeFunc = void (*)(GrCullMode_t);
  using DepthModeFunc = void (*)(GrDepthBufferMode_t);
  using AlphaCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaBlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                  GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using ResetFunc = FxBool (*)(FxU32);
  using VtxLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using DrawVertArrayContigFunc = void (*)(FxU32, FxU32, void*, FxU32);
  using ReadRegionFunc =
      FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using BufSwapFunc = void (*)(FxU32);
  using SstOriginFunc = void (*)(GrOriginLocation_t);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto sstSelect =
      reinterpret_cast<SstSelectFunc>(::dlsym(handle, "grSstSelect"));
  auto winOpen =
      reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto bufClear =
      reinterpret_cast<BufClearFunc>(::dlsym(handle, "grBufferClear"));
  auto colorCombine =
      reinterpret_cast<ColorCombFunc>(::dlsym(handle, "grColorCombine"));
  auto constColor =
      reinterpret_cast<ConstColorFunc>(::dlsym(handle, "grConstantColorValue"));
  auto cullMode =
      reinterpret_cast<CullModeFunc>(::dlsym(handle, "grCullMode"));
  auto depthMode =
      reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
  auto alphaCombine =
      reinterpret_cast<AlphaCombFunc>(::dlsym(handle, "grAlphaCombine"));
  auto alphaBlend =
      reinterpret_cast<AlphaBlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
  auto resetFn = reinterpret_cast<ResetFunc>(::dlsym(handle, "grReset"));
  auto vtxLayout =
      reinterpret_cast<VtxLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto drawVertArrayContig = reinterpret_cast<DrawVertArrayContigFunc>(
      ::dlsym(handle, "grDrawVertexArrayContiguous"));
  auto readRegion =
      reinterpret_cast<ReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto bufSwap = reinterpret_cast<BufSwapFunc>(::dlsym(handle, "grBufferSwap"));
  auto sstOrigin =
      reinterpret_cast<SstOriginFunc>(::dlsym(handle, "grSstOrigin"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(bufClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constColor != nullptr);
  REQUIRE(cullMode != nullptr);
  REQUIRE(depthMode != nullptr);
  REQUIRE(alphaCombine != nullptr);
  REQUIRE(alphaBlend != nullptr);
  REQUIRE(resetFn != nullptr);
  REQUIRE(vtxLayout != nullptr);
  REQUIRE(drawVertArrayContig != nullptr);
  REQUIRE(readRegion != nullptr);
  REQUIRE(bufSwap != nullptr);
  REQUIRE(sstOrigin != nullptr);

  struct SimpleVtx {
    float x, y;
  };
  struct EnvGuard {
    bool hadBe;
    std::string beVal;
    bool hadHl;
    std::string hlVal;
    ~EnvGuard() {
      if (hadBe)
        ::setenv("GLIDE_NG_BACKEND", beVal.c_str(), 1);
      else
        ::unsetenv("GLIDE_NG_BACKEND");
      if (hadHl)
        ::setenv("GLIDE_NG_HEADLESS", hlVal.c_str(), 1);
      else
        ::unsetenv("GLIDE_NG_HEADLESS");
    }
  } envGuard{std::getenv("GLIDE_NG_BACKEND") != nullptr,
             std::getenv("GLIDE_NG_BACKEND") ? std::getenv("GLIDE_NG_BACKEND")
                                             : "",
             std::getenv("GLIDE_NG_HEADLESS") != nullptr,
             std::getenv("GLIDE_NG_HEADLESS") ? std::getenv("GLIDE_NG_HEADLESS")
                                              : ""};

  for (const char* be : {"software", "vulkan", "opengl_es"}) {
    INFO("Backend: " << be);
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_BACKEND", be, 1);

    glideInit();
    sstSelect(0);
    GrContext_t ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
      glideShutdown();
      continue;
    }

    REQUIRE(resetFn(GR_VERTEX_PARAMETER) == FXTRUE);
    vtxLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    depthMode(GR_DEPTHBUFFER_DISABLE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    cullMode(GR_CULL_NEGATIVE);
    bufClear(0xFF000000u, 0, 0xFFFFu);

    // 1. GR_TRIANGLES (CW square at [40..120, 40..120]) -> Red
    SimpleVtx tris[6] = {
        {40.0f, 40.0f},   {120.0f, 40.0f},  {120.0f, 120.0f},
        {40.0f, 40.0f},   {120.0f, 120.0f}, {40.0f, 120.0f},
    };
    constColor(0xFFFF0000u);
    drawVertArrayContig(GR_TRIANGLES, 6, tris, sizeof(SimpleVtx));

    // 2. GR_TRIANGLE_STRIP across both even (i=0) and odd (i=1) triangles
    //    starting CW so all strip triangles have positive screen area under
    //    GR_CULL_NEGATIVE ([160..280, 40..120]) -> Green
    SimpleVtx strip[6] = {
        {160.0f, 120.0f}, {160.0f, 40.0f}, {200.0f, 120.0f},
        {200.0f, 40.0f},  {240.0f, 120.0f}, {240.0f, 40.0f},
    };
    constColor(0xFF00FF00u);
    drawVertArrayContig(GR_TRIANGLE_STRIP, 6, strip, sizeof(SimpleVtx));

    // 3. GR_TRIANGLE_FAN (CW hexagon at center (400, 120)) -> Blue
    SimpleVtx fan[8];
    fan[0] = {400.0f, 120.0f};
    constexpr float kTwoPi = 6.28318530718f;
    for (int i = 0; i <= 6; ++i) {
      const float a = kTwoPi * static_cast<float>(i) / 6.0f;
      fan[1 + i] = {400.0f + 50.0f * std::cos(a),
                    120.0f + 50.0f * std::sin(a)};
    }
    constColor(0xFF0000FFu);
    drawVertArrayContig(GR_TRIANGLE_FAN, 8, fan, sizeof(SimpleVtx));

    // 4. GR_POLYGON (CW 8-vert octagon at center (320, 320)) -> Yellow
    SimpleVtx poly[8];
    for (int i = 0; i < 8; ++i) {
      const float a = kTwoPi * static_cast<float>(i) / 8.0f;
      poly[i] = {320.0f + 60.0f * std::cos(a), 320.0f + 60.0f * std::sin(a)};
    }
    constColor(0xFFFFFF00u);
    drawVertArrayContig(GR_POLYGON, 8, poly, sizeof(SimpleVtx));

    auto readPx = [&](FxU32 x, FxU32 y) -> uint16_t {
      uint16_t px = 0u;
      REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, x, y, 1, 1, 2, &px) == FXTRUE);
      return px;
    };
    auto r565 = [](uint16_t p) { return (p >> 11) & 0x1Fu; };
    auto g565 = [](uint16_t p) { return (p >> 5) & 0x3Fu; };
    auto b565 = [](uint16_t p) { return p & 0x1Fu; };

    const uint16_t pxTri = readPx(80u, 80u);
    CHECK(r565(pxTri) >= 28u);
    CHECK(g565(pxTri) <= 4u);
    CHECK(b565(pxTri) <= 4u);

    const uint16_t pxStripEven = readPx(170u, 80u);
    const uint16_t pxStripOdd = readPx(190u, 70u);
    const uint16_t pxStripTail = readPx(225u, 70u);
    for (uint16_t spx : {pxStripEven, pxStripOdd, pxStripTail}) {
      CHECK(r565(spx) <= 4u);
      CHECK(g565(spx) >= 58u);
      CHECK(b565(spx) <= 4u);
    }

    const uint16_t pxFan = readPx(400u, 120u);
    CHECK(r565(pxFan) <= 4u);
    CHECK(g565(pxFan) <= 4u);
    CHECK(b565(pxFan) >= 28u);

    const uint16_t pxPoly = readPx(320u, 320u);
    CHECK(r565(pxPoly) >= 28u);
    CHECK(g565(pxPoly) >= 58u);
    CHECK(b565(pxPoly) <= 4u);

    // 5. Headless swap + on-demand GR_BUFFER_FRONTBUFFER readback parity
    auto readFrontPx = [&](FxU32 x, FxU32 y) -> uint16_t {
      uint16_t px = 0u;
      REQUIRE(readRegion(GR_BUFFER_FRONTBUFFER, x, y, 1, 1, 2, &px) == FXTRUE);
      return px;
    };
    bufSwap(0);
    const uint16_t frontTri = readFrontPx(80u, 80u);
    CHECK(r565(frontTri) >= 28u);
    CHECK(g565(frontTri) <= 4u);
    CHECK(b565(frontTri) <= 4u);
    const uint16_t frontFan = readFrontPx(400u, 120u);
    CHECK(r565(frontFan) <= 4u);
    CHECK(g565(frontFan) <= 4u);
    CHECK(b565(frontFan) >= 28u);

    // 6. GR_ORIGIN_LOWER_LEFT Y-flip + multi-frame headless swap across ring
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    cullMode(GR_CULL_DISABLE);
    SimpleVtx lowerPoly[8];
    for (int i = 0; i < 8; ++i) {
      const float a = kTwoPi * static_cast<float>(i) / 8.0f;
      lowerPoly[i] = {400.0f + 50.0f * std::cos(a),
                      100.0f + 50.0f * std::sin(a)};
    }
    for (int frame = 0; frame < 4; ++frame) {
      bufClear(0xFF000000u, 0, 0xFFFFu);
      constColor(0xFFFF0000u);
      drawVertArrayContig(GR_TRIANGLES, 6, tris, sizeof(SimpleVtx));
      constColor(0xFF0000FFu);
      drawVertArrayContig(GR_POLYGON, 8, lowerPoly, sizeof(SimpleVtx));
      bufSwap(0);
    }

    const uint16_t lowerTri = readFrontPx(80u, 400u);
    CHECK(r565(lowerTri) >= 28u);
    CHECK(g565(lowerTri) <= 4u);
    CHECK(b565(lowerTri) <= 4u);

    const uint16_t lowerPolyPx = readFrontPx(400u, 380u);
    CHECK(r565(lowerPolyPx) <= 4u);
    CHECK(g565(lowerPolyPx) <= 4u);
    CHECK(b565(lowerPolyPx) >= 28u);

    const uint16_t unflippedTri = readFrontPx(80u, 80u);
    CHECK(r565(unflippedTri) <= 4u);
    CHECK(g565(unflippedTri) <= 4u);
    CHECK(b565(unflippedTri) <= 4u);

    const uint16_t unflippedPoly = readFrontPx(400u, 100u);
    CHECK(r565(unflippedPoly) <= 4u);
    CHECK(g565(unflippedPoly) <= 4u);
    CHECK(b565(unflippedPoly) <= 4u);

    sstOrigin(GR_ORIGIN_UPPER_LEFT);
    winClose(ctx);
    glideShutdown();
  }
  ::dlclose(handle);
#endif
}

