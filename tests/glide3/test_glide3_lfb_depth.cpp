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
#include "software/SoftwareBackend.h"
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

TEST_CASE("LFB: Clear, Readback & Write Modes (888/8888/DEPTH/ZA16)", "[Glide3][LFB]") {
  SECTION("Vulkan grBufferClear & readback verification") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless Vulkan backend for testing
    ::setenv("GLIDE_NG_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    // 1. Verify basic color clearing works
    bufferClear(0xFF00FF00, 0xFF, 0xFFFF);
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 100, 100, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pixels[i] != 0);
    }

    // 2. Clear color buffer to black (0) before rendering
    bufferClear(0x00000000, 0, 0xFFFF);

    // 3. Configure Glide state for flat shading using iterated color
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool);
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    REQUIRE(colorCombine != nullptr);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    // 4. Configure vertex layout using local Glide 3.x parameter definitions:
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01

    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    REQUIRE(vertexLayout != nullptr);
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // 5. Define 3 vertices for a red triangle in the center of the screen
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red
    TestVert v2{ 400.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red

    // 6. Draw the triangle
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    REQUIRE(drawTriangle != nullptr);
    drawTriangle(&v0, &v1, &v2);

    // 7. Read back a 4x4 region from the center of the triangle (400, 250) on the BACKBUFFER (1)
    std::vector<uint32_t> drawPixels(16, 0);
    ok = lfbRead(1, 400, 250, 4, 4, 4 * 4, drawPixels.data());
    REQUIRE(ok == FXTRUE);

    // 8. Verify that the pixels inside the triangle are RED (not black/zero!)
    for (int i = 0; i < 16; ++i) {
        REQUIRE(drawPixels[i] != 0);
    }

    winClose();
    glideShutdown();
    ::dlclose(handle);
  }

  SECTION("Lazy CPU ClearBuffer defers fills until LFB access") {
  class LazyClearProbeBackend : public GlideWrapper::SoftwareBackend {
   public:
    void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                     uint32_t clearMask) override {
      ScheduleLazyCpuClear(color, alpha, z, clearMask);
    }
    uint32_t PeekRawBackPixel(uint32_t x, uint32_t y) const {
      const auto* px = reinterpret_cast<const uint32_t*>(
          m_cpuBuffers[m_backBufferIdx].data());
      return px[y * m_headlessWidth + x];
    }
  };

  GlideWrapper::WrapperConfig config;
  config.backend = "software";
  config.forceNoWindow = true;
  config.maxFps = 0.0;

  LazyClearProbeBackend backend;
  REQUIRE(backend.Initialize(config));
  REQUIRE(backend.AttachWindow(nullptr, 64, 64, true));

  // Clear backbuffer lazily to bright green (0x0000FF00 in ABGR/ARGB).
  backend.ClearBuffer(0x0000FF00, 0xFF, 0.5f, 0x3);
  CHECK(backend.HasPendingLazyCpuClear(1));
  // Raw CPU backbuffer must remain untouched (0) until an LFB operation occurs.
  CHECK(backend.PeekRawBackPixel(0, 0) == 0u);

  // Perform a partial 1x1 LFB write at (10, 10) with pure white (RGB565 0xFFFF).
  uint16_t white565 = 0xFFFF;
  REQUIRE(backend.WriteLFB(1, 10, 10, 1, 1, 2, 0 /* GR_LFB_SRC_FMT_565 */,
                           &white565));
  CHECK_FALSE(backend.HasPendingLazyCpuClear(1));

  // Read back both the written pixel and an untouched background pixel.
  uint16_t writtenPx = 0;
  uint16_t bgPx = 0;
  REQUIRE(backend.ReadLFB(1, 10, 10, 1, 1, 2, &writtenPx));
  REQUIRE(backend.ReadLFB(1, 0, 0, 1, 1, 2, &bgPx));
  CHECK(writtenPx == 0xFFFF);
  CHECK(bgPx == 0x07E0);  // Green in RGB565!

  // Subsequent lazy clear followed by ReadLFB materializes cleanly too.
  backend.ClearBuffer(0x00FF0000, 0xFF, 1.0f, 0x1);
  CHECK(backend.HasPendingLazyCpuClear(1));
  uint16_t afterClearPx = 0;
  REQUIRE(backend.ReadLFB(1, 10, 10, 1, 1, 2, &afterClearPx));
  CHECK_FALSE(backend.HasPendingLazyCpuClear(1));
  CHECK(afterClearPx != 0xFFFF);

  backend.DetachWindow();
  backend.Shutdown();
  }

  SECTION("LfbManager stride & color mapping across write modes") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void(FX_CALL*)())::dlsym(handle, "grGlideInit");
  auto glideShutdown = (void(FX_CALL*)())::dlsym(handle, "grGlideShutdown");
  auto sstSelect = (void(FX_CALL*)(int))::dlsym(handle, "grSstSelect");
  auto winOpen = (FxU32(FX_CALL*)(FxU32, GrScreenResolution_t,
                                  GrScreenRefresh_t, GrColorFormat_t,
                                  GrOriginLocation_t, int, int))::dlsym(handle, "grSstWinOpen");
  auto winClose = (FxBool(FX_CALL*)(FxU32))::dlsym(handle, "grSstWinClose");
  auto bufferClear = (void(FX_CALL*)(GrColor_t, GrAlpha_t, FxU32))::dlsym(handle, "grBufferClear");
  auto lfbLock = (FxBool(FX_CALL*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t,
                                   GrOriginLocation_t, FxBool, GrLfbInfo_t*))::dlsym(handle, "grLfbLock");
  auto lfbUnlock = (FxBool(FX_CALL*)(GrLock_t, GrBuffer_t))::dlsym(handle, "grLfbUnlock");

  REQUIRE(glideInit != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(lfbLock != nullptr);
  REQUIRE(lfbUnlock != nullptr);

  glideInit();
  sstSelect(0);
  // Open with GR_COLORFORMAT_ABGR (same as 3dfx SDK test12) to ensure LFB
  // 888/8888 pixel data still unpacks as 0x00RRGGBB / 0xAARRGGBB.
  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  for (GrLfbWriteMode_t mode : {GR_LFBWRITEMODE_888, GR_LFBWRITEMODE_8888}) {
    bufferClear(0, 0, 0);

    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, mode,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXTRUE);
    REQUIRE(info.lfbPtr != nullptr);
    REQUIRE(info.strideInBytes == 640 * 4);

    auto* row = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(info.lfbPtr) + 10 * info.strideInBytes);
    row[10] = 0xFFFF0000;  // Pure Red (ARGB)
    row[11] = 0xFF00FF00;  // Pure Green (ARGB)
    row[12] = 0xFF0000FF;  // Pure Blue (ARGB)
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    GrLfbInfo_t readInfo{};
    readInfo.size = sizeof(readInfo);
    REQUIRE(lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &readInfo) == FXTRUE);
    REQUIRE(readInfo.strideInBytes == 640 * 2);
    const auto* readRow = reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(readInfo.lfbPtr) + 10 * readInfo.strideInBytes);
    CHECK(readRow[10] == 0xF800);  // Red in RGB565
    CHECK(readRow[11] == 0x07E0);  // Green in RGB565
    CHECK(readRow[12] == 0x001F);  // Blue in RGB565
    REQUIRE(lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // 565_DEPTH (0xc) uses 4 BPP with RGB565 in the low 16 bits.
  {
    bufferClear(0, 0, 0);
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565_DEPTH, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &info) == FXTRUE);
    REQUIRE(info.strideInBytes == 640 * 4);
    auto* row = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(info.lfbPtr) + 12 * info.strideInBytes);
    row[15] = 0x1234F800;  // Depth 0x1234 + Red RGB565
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    GrLfbInfo_t readInfo{};
    readInfo.size = sizeof(readInfo);
    REQUIRE(lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &readInfo) == FXTRUE);
    const auto* readRow = reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(readInfo.lfbPtr) + 12 * readInfo.strideInBytes);
    CHECK(readRow[15] == 0xF800);
    REQUIRE(lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // ZA16 (0xf) uses 2 BPP.
  {
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_ZA16, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &info) == FXTRUE);
    CHECK(info.strideInBytes == 640 * 2);
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // Reserved write modes must be rejected cleanly.
  for (GrLfbWriteMode_t reserved :
       {GR_LFBWRITEMODE_RESERVED1, GR_LFBWRITEMODE_RESERVED3,
        GR_LFBWRITEMODE_RESERVED4}) {
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    CHECK(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, reserved,
                  GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXFALSE);
  }

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
#endif
  }

}

TEST_CASE("LFB: AUXBUFFER ZA16 Depth Mask, Nested Lock & Read Cache", "[Glide3][LFB][Depth]") {
#ifdef __linux__
  auto openLib = []() -> void* { return OpenGlide3Handle(); };
  void* handle = openLib();
  REQUIRE(handle != nullptr);

  using InitFn = void (*)(void);
  using ShutdownFn = void (*)(void);
  using WinOpenFn = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinCloseFn = FxBool (*)(FxU32);
  using BufferClearFn = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using BufferSwapFn = void (*)(FxU32);
  using VertexLayoutFn = void (*)(FxU32, FxI32, FxU32);
  using DepthModeFn = void (*)(GrDepthBufferMode_t);
  using DepthFuncFn = void (*)(GrCmpFnc_t);
  using DepthMaskFn = void (*)(FxBool);
  using DepthBiasFn = void (*)(FxI32);
  using ColorCombineFn = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                  GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ConstColorFn = void (*)(GrColor_t);
  using DrawTriFn = void (*)(const void*, const void*, const void*);
  using LfbLockFn = FxBool (*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t,
                               GrOriginLocation_t, FxBool, GrLfbInfo_t*);
  using LfbUnlockFn = FxBool (*)(GrLock_t, GrBuffer_t);
  using LfbWriteRegionFn = FxBool (*)(GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t,
                                      FxU32, FxU32, FxBool, FxI32, void*);
  using LfbReadRegionFn = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32,
                                     FxU32, void*);

  auto glideInit = reinterpret_cast<InitFn>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFn>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFn>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFn>(::dlsym(handle, "grSstWinClose"));
  auto bufferClear =
      reinterpret_cast<BufferClearFn>(::dlsym(handle, "grBufferClear"));
  auto bufferSwap =
      reinterpret_cast<BufferSwapFn>(::dlsym(handle, "grBufferSwap"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFn>(::dlsym(handle, "grVertexLayout"));
  auto depthMode =
      reinterpret_cast<DepthModeFn>(::dlsym(handle, "grDepthBufferMode"));
  auto depthFunc =
      reinterpret_cast<DepthFuncFn>(::dlsym(handle, "grDepthBufferFunction"));
  auto depthMask = reinterpret_cast<DepthMaskFn>(::dlsym(handle, "grDepthMask"));
  auto depthBias =
      reinterpret_cast<DepthBiasFn>(::dlsym(handle, "grDepthBiasLevel"));
  auto colorCombine =
      reinterpret_cast<ColorCombineFn>(::dlsym(handle, "grColorCombine"));
  auto constColor =
      reinterpret_cast<ConstColorFn>(::dlsym(handle, "grConstantColorValue"));
  auto drawTri = reinterpret_cast<DrawTriFn>(::dlsym(handle, "grDrawTriangle"));
  auto lfbLock = reinterpret_cast<LfbLockFn>(::dlsym(handle, "grLfbLock"));
  auto lfbUnlock =
      reinterpret_cast<LfbUnlockFn>(::dlsym(handle, "grLfbUnlock"));
  auto writeRegion =
      reinterpret_cast<LfbWriteRegionFn>(::dlsym(handle, "grLfbWriteRegion"));
  auto readRegion =
      reinterpret_cast<LfbReadRegionFn>(::dlsym(handle, "grLfbReadRegion"));

  const bool allLoaded =
      (glideInit && glideShutdown && winOpen && winClose && bufferClear &&
       bufferSwap && vertexLayout && depthMode && depthFunc && depthMask &&
       depthBias && colorCombine && constColor && drawTri && lfbLock &&
       lfbUnlock && writeRegion && readRegion);
  REQUIRE(allLoaded);

  struct Vtx {
    float x, y, z, oow;
  };
  auto rgb565R = [](uint16_t p) { return ((p >> 11) & 0x1Fu) * 255u / 31u; };
  auto rgb565G = [](uint16_t p) { return ((p >> 5) & 0x3Fu) * 255u / 63u; };
  auto rgb565B = [](uint16_t p) { return (p & 0x1Fu) * 255u / 31u; };

  const char* backends[] = {"software", "vulkan", "opengl_es"};
  for (const char* be : backends) {
    ::setenv("GLIDE_NG_BACKEND", be, 1);
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);

    glideInit();
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (ctx == 0) {
      glideShutdown();
      continue;
    }

    vertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    vertexLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
    vertexLayout(GR_PARAM_Q, 12, GR_PARAM_ENABLE);

    // 1. Seed full color backdrop (pure bright GREEN 0x00FF00 -> RGB565 0x07E0)
    //    and depth clear to 0xFFFF.
    bufferClear(0xFF00FF00u, 255, 0xFFFFu);
    depthMode(GR_DEPTHBUFFER_WBUFFER);
    depthFunc(GR_CMP_LESS);
    depthMask(FXTRUE);

    // 2. Write AUXBUFFER ZA16 cockpit mask via grLfbLock:
    //    left half [0..319]   = 0x0000 (masked strut - MUST survive clears!)
    //    right half [320..639] = 0xFFFF (open windshield - MUST be cleared!)
    GrLfbInfo_t auxInfo{};
    auxInfo.size = sizeof(auxInfo);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_AUXBUFFER,
                    GR_LFBWRITEMODE_ZA16, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &auxInfo) == FXTRUE);
    REQUIRE(auxInfo.lfbPtr != nullptr);
    auto* auxBytes = static_cast<uint8_t*>(auxInfo.lfbPtr);
    for (uint32_t y = 0; y < 480; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(auxBytes + y * auxInfo.strideInBytes);
      for (uint32_t x = 0; x < 320; ++x) row[x] = 0x0000u;
      for (uint32_t x = 320; x < 640; ++x) row[x] = 0xFFFFu;
    }
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_AUXBUFFER) == FXTRUE);

    // Verify AUXBUFFER write did NOT corrupt the green color backbuffer!
    uint16_t preLeft = 0, preRight = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 100, 240, 1, 1, 2, &preLeft) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 500, 240, 1, 1, 2, &preRight) == FXTRUE);
    INFO("backend=" << be << " pre-clear color check");
    CHECK(rgb565G(preLeft) > 220u);
    CHECK(rgb565R(preLeft) < 20u);
    CHECK(rgb565G(preRight) > 220u);

    // 3. Draw fullscreen BLUE quad using GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS
    //    + GR_CMP_NOTEQUAL + bias=0 (mirrors Archimedean Dynasty combat clear).
    depthMode(GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS);
    depthFunc(GR_CMP_NOTEQUAL);
    depthBias(0);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    constColor(0xFF0000FFu);  // Pure BLUE
    Vtx q0{0.0f, 0.0f, 1.0f, 1.0f / 65535.0f};
    Vtx q1{640.0f, 0.0f, 1.0f, 1.0f / 65535.0f};
    Vtx q2{640.0f, 480.0f, 1.0f, 1.0f / 65535.0f};
    Vtx q3{0.0f, 480.0f, 1.0f, 1.0f / 65535.0f};
    drawTri(&q0, &q1, &q2);
    drawTri(&q0, &q2, &q3);

    uint16_t strutPx = 0, winPx = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 100, 240, 1, 1, 2, &strutPx) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 500, 240, 1, 1, 2, &winPx) == FXTRUE);
    INFO("backend=" << be << " WBUFFER_COMPARE_TO_BIAS NOTEQUAL 0");
    // Left strut (depth==0x0000) MUST remain GREEN (rejected!)
    CHECK(rgb565G(strutPx) > 220u);
    CHECK(rgb565B(strutPx) < 20u);
    // Right windshield (depth==0xFFFF) MUST be cleared to BLUE!
    CHECK(rgb565B(winPx) > 220u);
    CHECK(rgb565G(winPx) < 20u);

    // 4. Subsequent 3D W-buffer RED triangle spanning BOTH halves MUST clip
    //    behind the left 0x0000 strut while passing over the right 0xFFFF!
    depthMode(GR_DEPTHBUFFER_WBUFFER);
    depthFunc(GR_CMP_LESS);
    constColor(0xFFFF0000u);  // Pure RED
    Vtx r0{200.0f, 200.0f, 0.5f, 1.0f / 100.0f};
    Vtx r1{440.0f, 200.0f, 0.5f, 1.0f / 100.0f};
    Vtx r2{440.0f, 300.0f, 0.5f, 1.0f / 100.0f};
    Vtx r3{200.0f, 300.0f, 0.5f, 1.0f / 100.0f};
    drawTri(&r0, &r1, &r2);
    drawTri(&r0, &r2, &r3);

    uint16_t maskedRed = 0, openRed = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 250, 250, 1, 1, 2, &maskedRed) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 390, 250, 1, 1, 2, &openRed) == FXTRUE);
    CHECK(rgb565G(maskedRed) > 220u);  // Still GREEN behind strut!
    CHECK(rgb565R(maskedRed) < 20u);
    CHECK(rgb565R(openRed) > 220u);    // Visible RED in open window!
    CHECK(rgb565B(openRed) < 20u);

    // 5. Nested re-entrant grLfbLock(WRITE_ONLY, BACK) preserves outer writes!
    GrLfbInfo_t outerInfo{}, innerInfo{};
    outerInfo.size = sizeof(outerInfo);
    innerInfo.size = sizeof(innerInfo);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &outerInfo) == FXTRUE);
    auto* pOuter = static_cast<uint8_t*>(outerInfo.lfbPtr);
    // Write YELLOW (0xFFE0) under outer lock at (50, 50)
    reinterpret_cast<uint16_t*>(pOuter + 50 * outerInfo.strideInBytes)[50] = 0xFFE0u;
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &innerInfo) == FXTRUE);
    auto* pInner = static_cast<uint8_t*>(innerInfo.lfbPtr);
    // Write CYAN (0x07FF) under inner lock at (60, 50)
    reinterpret_cast<uint16_t*>(pInner + 50 * innerInfo.strideInBytes)[60] = 0x07FFu;
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
    // Write WHITE (0xFFFF) back in outer lock at (70, 50) before final unlock
    reinterpret_cast<uint16_t*>(pOuter + 50 * outerInfo.strideInBytes)[70] = 0xFFFFu;
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    uint16_t outY = 0, outC = 0, outW = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 50, 50, 1, 1, 2, &outY) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 60, 50, 1, 1, 2, &outC) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 70, 50, 1, 1, 2, &outW) == FXTRUE);
    CHECK(rgb565R(outY) > 220u);
    CHECK(rgb565G(outY) > 220u);
    CHECK(rgb565B(outY) < 20u);
    CHECK(rgb565G(outC) > 220u);
    CHECK(rgb565B(outC) > 220u);
    CHECK(rgb565R(outC) < 20u);
    CHECK(rgb565R(outW) > 220u);
    CHECK(rgb565G(outW) > 220u);
    CHECK(rgb565B(outW) > 220u);

    // 6. Read/write LFB region scanline cache coherence check
    uint16_t strip[8] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u,
                         0x07FFu, 0xF81Fu, 0xFFFFu, 0x0000u};
    REQUIRE(writeRegion(GR_BUFFER_BACKBUFFER, 10, 10, GR_LFB_SRC_FMT_565,
                        8, 1, FXFALSE, 16, strip) == FXTRUE);
    uint16_t got[8]{};
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 10, 10, 8, 1, 16, got) == FXTRUE);
    CHECK(rgb565R(got[0]) > 220u);
    CHECK(rgb565G(got[1]) > 220u);
    CHECK(rgb565B(got[2]) > 220u);

    winClose(ctx);
    glideShutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
  }
  ::dlclose(handle);
#endif
}

TEST_CASE("Software: ThreadPool & Headless Resource Lifecycle", "[Software][Lifecycle]") {
  SECTION("forceNoWindow skips SDL allocation") {
  GlideWrapper::WrapperConfig config;
  config.backend = "software";
  config.forceNoWindow = true;
  config.maxFps = 0.0;

  GlideWrapper::SoftwareBackend backend;
  REQUIRE(backend.Initialize(config));
  REQUIRE(backend.AttachWindow(nullptr, 320, 240, true));
  REQUIRE(backend.IsHeadlessMode());
  REQUIRE(backend.GetWidth() == 320);
  REQUIRE(backend.GetHeight() == 240);

  backend.ClearBuffer(0xFF112233, 0xFF, 1.0f, 0x7);
  REQUIRE(backend.SwapBuffers());

  uint16_t pixel = 0;
  REQUIRE(backend.ReadLFB(0, 160, 120, 1, 1, 2, &pixel));
  CHECK(pixel != 0);

  backend.DetachWindow();
  CHECK_FALSE(backend.IsHeadlessMode());
  backend.Shutdown();
  }

  SECTION("Lazy ThreadPool lifecycle across probe and reopen") {
  using namespace GlideWrapper;
  auto& reg = EmulationRegistry::GetInstance();
  const auto savedCfg = reg.GetConfig();
  auto& bm = BackendManager::GetInstance();

  WrapperConfig cfg = savedCfg;
  cfg.backend = "software";
  cfg.forceNoWindow = true;
  cfg.model = CardModel::Voodoo2;
  cfg.swThreads = 4;
  reg.GetConfig() = cfg;

  // 1. Probe-only cycle: Initialize -> Shutdown with NO AttachWindow() (mirrors
  //    Win32 plugin LoadLibrary -> grSstQueryHardware -> FreeLibrary).
  REQUIRE(bm.EstablishBackend(cfg));
  REQUIRE(bm.GetBackend() != nullptr);
  bm.ShutdownBackend();

  // 2. Repeated AttachWindow -> DrawTriangle -> DetachWindow reopen cycles must
  //    spawn, run, and cleanly join the lazy ThreadPool on each cycle.
  REQUIRE(bm.EstablishBackend(cfg));
  auto* be = bm.GetBackend();
  REQUIRE(be != nullptr);
  for (int cycle = 0; cycle < 2; ++cycle) {
    REQUIRE(be->AttachWindow(nullptr, 128, 128, false));
    be->ClearBuffer(0xFF000000u, 0u, 0.0f, 1u);
    be->SetDepthState(0u, 7u, false, 0);
    be->SetCombinerMode(1u, 0u, 0u, 0u, false, 1u, 0u, 0u, 0u, false);
    ModernVertex v0{}, v1{}, v2{};
    v0.pos = {16.0f, 16.0f, 0.5f, 1.0f};
    v1.pos = {112.0f, 16.0f, 0.5f, 1.0f};
    v2.pos = {16.0f, 112.0f, 0.5f, 1.0f};
    v0.color = v1.color = v2.color = {0.0f, 1.0f, 0.0f, 1.0f};
    be->DrawTriangle(v0, v1, v2);
    be->FlushBins();
    std::vector<uint32_t> fb(128u * 128u, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, 128u, 128u, 128u * 4u, fb.data()));
    const uint32_t px = fb[32u * 128u + 32u];
    CHECK(((px >> 8) & 0xFFu) > 200u);
    CHECK((px & 0xFFu) < 25u);
    be->DetachWindow();
  }
  bm.ShutdownBackend();
  reg.GetConfig() = savedCfg;
  }

}

