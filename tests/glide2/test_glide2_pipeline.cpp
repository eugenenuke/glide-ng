// Auto-modularized Glide 2.x Catch2 test suite unit.
#include <dlfcn.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "core/BackendManager.h"
#include "core/IniConfigLoader.h"
#include "core/LfbManager.h"
#include "core/Logger.h"
#include "core/MathUtils.h"
#include "core/SimdDispatch.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/VertexLayout.h"
#include "core/WrapperConfig.h"
#include "software/SoftwareBackend.h"
#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include <glide.h>
#include "common/test_helpers.h"

namespace {
[[maybe_unused]] inline void* OpenGlide2Handle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideSelfHandle(flags);
}
[[maybe_unused]] inline void* OpenGlide2DsoHandle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideLibrary("libglide2x.so", flags);
}
[[maybe_unused]] inline void* OpenGlide3DsoHandle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideLibrary("libglide3x.so", flags);
}
}  // namespace

TEST_CASE("Glide 2.x: Clear, Gamma & ABGR Framebuffer Parity", "[Glide2][Clear][Gamma]") {
  SECTION("ABGR clear & LFB readback parity") {
#ifdef __linux__
  // Dynamically load the Glide 2 wrapper library
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  // Resolve Glide 2.x function pointers
  using GlideInitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
  auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
  auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
  auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  // Initialize Glide
  glideInit();
  sstSelect(0);

  // Open window with ABGR format! (640x480 resolution to match test00)
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Clear buffer with color 0xff0000 (which represents BLUE under ABGR)
  bufferClear(0xff0000, 0, 0);

  // 1. Validate 16-bit LFB readback (should return symmetric RGB565 format)
  // Blue is in bits 0..4 of the 16-bit word, Red in bits 11..15, Green
  // in bits 5..10. Since the screen is Blue, the value should be exactly 31
  // (Blue=31, Green=0, Red=0).
  std::vector<uint16_t> buffer16(10, 0);
  FxBool ok =
      readLfb(GR_BUFFER_BACKBUFFER, 300, 240, 10, 1, 20, buffer16.data());
  REQUIRE(ok == FXTRUE);

  for (int i = 0; i < 10; ++i) {
    REQUIRE(buffer16[i] == 31);
  }

  // 2. Validate 32-bit LFB readback (should return hardware-accurate ABGR
  // format) Blue is in bits 16..23, Alpha is in bits 24..31, Red and Green are
  // 0. So the value should be exactly 0xFFFF0000.
  std::vector<uint32_t> buffer32(10, 0);
  ok = readLfb(GR_BUFFER_BACKBUFFER, 300, 240, 10, 1, 40, buffer32.data());
  REQUIRE(ok == FXTRUE);

  for (int i = 0; i < 10; ++i) {
    REQUIRE(buffer32[i] == 0xFFFF0000);
  }

  // Clean up
  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("OpenGL ES gamma correction accuracy") {
#ifdef __linux__
  // 1. Ensure we only run this test if the OpenGL ES backend is active at
  // process start
  const char* envBackend = std::getenv("GLIDE_NG_BACKEND");
  if (!envBackend || std::string(envBackend) != "opengl_es") {
    return;  // Safely skip if not explicitly running GLES
  }
  const char* envHeadless = std::getenv("GLIDE_NG_HEADLESS");
  if (envHeadless && std::string(envHeadless) == "1") {
    return;  // Default framebuffer (FBO 0) readback requires a non-headless EGL surface
  }

  // Load the shared library dynamically
  void* handle = OpenGlide2Handle(RTLD_NOW | RTLD_GLOBAL);
  REQUIRE(handle != nullptr);

  // Resolve needed functions
  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using BufferSwapFunc = void (*)(FxU32);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GammaFunc = void (*)(float);

  auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto shutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto bufferClear =
      reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
  auto bufferSwap =
      reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
  auto lfbReadRegion =
      reinterpret_cast<LfbReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto gammaCorrection =
      reinterpret_cast<GammaFunc>(::dlsym(handle, "grGammaCorrectionValue"));

  REQUIRE(init != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(bufferSwap != nullptr);
  REQUIRE(lfbReadRegion != nullptr);
  REQUIRE(gammaCorrection != nullptr);

  // 1. Force the OpenGL ES backend (saving original for restoration)
  const char* origBackend = std::getenv("GLIDE_NG_BACKEND");
  std::string origBackendStr = origBackend ? origBackend : "";
  ::setenv("GLIDE_NG_BACKEND", "opengl_es", 1);

  // 2. Initialize Glide and open the window (windowed mode so it runs
  // headlessly)
  REQUIRE_NOTHROW(init());
  FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 0);
  REQUIRE(ok == FXTRUE);

  // --- CASE 1: GAMMA = 1.0f (Linear Mapping) ---
  gammaCorrection(1.0f);

  // Clear the backbuffer to a solid mid-grey color:
  // Color: 0xFF808080 (ARGB8888, Alpha=FF, Red=80, Green=80, Blue=80)
  bufferClear(0xFF808080, 0xFF, 0);

  // Swap buffers to present the mid-grey frame onto the screen (with Gamma
  // = 1.0)
  bufferSwap(1);

  // Read the center pixel (at 320, 240) directly from the EGL window surface
  // (framebuffer 0)
  ::glBindFramebuffer(GL_FRAMEBUFFER, 0);
  uint8_t screenPixel1[4] = {0};
  ::glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screenPixel1);

  // The screen presented value should be exactly 128 (linear, no scaling)
  REQUIRE(screenPixel1[0] >= 126);
  REQUIRE(screenPixel1[0] <= 130);

  // --- CASE 2: GAMMA = 2.0f (Power Curve Boost) ---
  gammaCorrection(2.0f);

  // Clear the backbuffer to the exact same mid-grey color
  bufferClear(0xFF808080, 0xFF, 0);

  // Swap buffers to trigger our new presentation shader with Gamma = 2.0!
  bufferSwap(1);

  // Read the center pixel directly from the EGL window surface (framebuffer 0)
  ::glBindFramebuffer(GL_FRAMEBUFFER, 0);
  uint8_t screenPixel2[4] = {0};
  ::glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screenPixel2);

  // Mathematically, the presented output value should be:
  // Color_out = (128 / 255.0)^(1.0 / 2.0) * 255.0 = sqrt(0.502) * 255.0 = 180
  // The screen pixel should have boosted from 128 to around 180!
  REQUIRE(screenPixel2[0] >= 175);
  REQUIRE(screenPixel2[0] <= 185);

  // --- CASE 3: SILICON-FAITHFUL VRAM VERIFICATION ---
  // Verify that although the screen shows a boosted 180, the digital VRAM front
  // buffer itself remains perfectly linear at 128 (matching physical Voodoo
  // RAMDAC behavior!)
  uint32_t vramPixel = 0;
  ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &vramPixel);
  REQUIRE(ok == FXTRUE);

  // Unpack Red channel from ARGB8888 VRAM pixel
  uint8_t vramR = (vramPixel >> 16) & 0xFF;
  REQUIRE(vramR >= 126);
  REQUIRE(vramR <= 130);  // Still linear 128!

  // Clean up
  REQUIRE_NOTHROW(shutdown());
  ::dlclose(handle);

  // Restore environment faithfully
  if (!origBackendStr.empty()) {
    ::setenv("GLIDE_NG_BACKEND", origBackendStr.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
  }

}

TEST_CASE("Glide 2.x: Alpha Blending, Fog & Multi-Pass Pre-Fog", "[Glide2][AlphaBlend][Fog]") {
  SECTION("Alpha blending & table fog") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  using GlideInitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ConstantColorValueFunc = void (*)(GrColor_t);
  using DrawTriangleFunc =
      void (*)(const GrVertex*, const GrVertex*, const GrVertex*);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  using AlphaCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaBlendFunctionFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                          GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using DepthBufferModeFunc = void (*)(GrDepthBufferMode_t);
  using SstOriginFunc = void (*)(GrOriginLocation_t);
  using FogModeFunc = void (*)(GrFogMode_t);
  using FogColorValueFunc = void (*)(GrColor_t);
  using FogTableFunc = void (*)(const GrFog_t*);
  using FogGenerateExpFunc = void (*)(GrFog_t*, float);

  auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
  auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
  auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
  auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
  auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
  auto constantColorValue =
      (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
  auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  auto alphaCombine = (AlphaCombineFunc)dlsym(handle, "grAlphaCombine");
  auto alphaBlendFunction =
      (AlphaBlendFunctionFunc)dlsym(handle, "grAlphaBlendFunction");
  auto depthBufferMode =
      (DepthBufferModeFunc)dlsym(handle, "grDepthBufferMode");
  auto sstOrigin = (SstOriginFunc)dlsym(handle, "grSstOrigin");
  auto fogMode = (FogModeFunc)dlsym(handle, "grFogMode");
  auto fogColorValue = (FogColorValueFunc)dlsym(handle, "grFogColorValue");
  auto fogTable = (FogTableFunc)dlsym(handle, "grFogTable");
  auto fogGenerateExp = (FogGenerateExpFunc)dlsym(handle, "guFogGenerateExp");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constantColorValue != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(alphaCombine != nullptr);
  REQUIRE(alphaBlendFunction != nullptr);
  REQUIRE(depthBufferMode != nullptr);
  REQUIRE(sstOrigin != nullptr);
  REQUIRE(fogMode != nullptr);
  REQUIRE(fogColorValue != nullptr);
  REQUIRE(fogTable != nullptr);
  REQUIRE(fogGenerateExp != nullptr);

  glideInit();
  sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  SECTION("SDK parity (07): Translucent Alpha Blend") {
    /* Set up Render State - flat constant color + alpha blending */
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ZERO, GR_BLEND_ZERO);
    depthBufferMode(GR_DEPTHBUFFER_DISABLE);

    bufferClear(0x00, 0, 0);

    GrVertex vtxA, vtxB, vtxC;

    // Triangle 1: Opaque Blue (0xFF0000FF)
    vtxA.x = 160.0f;
    vtxA.y = 100.8f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 1.0f;
    vtxB.x = 480.0f;
    vtxB.y = 100.8f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 1.0f;
    vtxC.x = 320.0f;
    vtxC.y = 379.2f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 1.0f;

    constantColorValue(0xFF0000FF);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Triangle 2: Translucent Red (0x80FF0000) - 50% opacity
    vtxA.x = 550.4f;
    vtxA.y = 100.8f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 1.0f;
    vtxB.x = 550.4f;
    vtxB.y = 379.2f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 1.0f;
    vtxC.x = 89.6f;
    vtxC.y = 240.0f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 1.0f;

    constantColorValue(0x80FF0000);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Read back
    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // 1. Overlap pixel (320, 240) -> blended to Magenta (0x007F007F)
    // Src (Red, 0x80) over Dest (Blue, opaque)
    // Red = 127, Green = 0, Blue = 127
    uint32_t p_blend = fb[240 * 640 + 320];
    uint8_t r = p_blend & 0xFF;
    uint8_t g = (p_blend >> 8) & 0xFF;
    uint8_t b = (p_blend >> 16) & 0xFF;
    INFO("Overlap pixel (320, 240): color = 0x"
         << std::hex << p_blend << " (r=" << std::dec << (int)r
         << ", g=" << (int)g << ", b=" << (int)b << ")");
    // Accept slight precision deviation (e.g. 127 +/- 4)
    REQUIRE(std::abs((int)r - 127) <= 4);
    REQUIRE(g <= 4);
    REQUIRE(std::abs((int)b - 127) <= 4);

    // 2. Non-overlap side pixel (120, 240) -> Translucent Red over Black.
    // Due to the wrapper's consistent draw-swapped/read-swapped architecture,
    // Red and Blue channels are swapped for the client:
    // Expected: Red = 0, Green = 0, Blue = 127
    uint32_t p_red = fb[240 * 640 + 120];
    r = p_red & 0xFF;
    g = (p_red >> 8) & 0xFF;
    b = (p_red >> 16) & 0xFF;
    INFO("Red-only pixel (120, 240): color = 0x"
         << std::hex << p_red << " (r=" << std::dec << (int)r
         << ", g=" << (int)g << ", b=" << (int)b << ")");
    REQUIRE(r <= 4);
    REQUIRE(g <= 4);
    REQUIRE(std::abs((int)b - 127) <= 4);

    // 3. Blue only pixel at (320, 120) -> Opaque Blue.
    // Due to the wrapper's consistent draw-swapped/read-swapped architecture,
    // Red and Blue channels are swapped for the client:
    // Expected: Red = 255, Green = 0, Blue = 0
    uint32_t p_blue = fb[120 * 640 + 320];
    r = p_blue & 0xFF;
    g = (p_blue >> 8) & 0xFF;
    b = (p_blue >> 16) & 0xFF;
    REQUIRE(std::abs((int)r - 255) <= 4);
    REQUIRE(g <= 4);
    REQUIRE(b <= 4);
  }

  SECTION("SDK parity (08): Table-Based Fogging") {
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    fogMode(GR_FOG_WITH_TABLE);
    fogColorValue(0xFF00FF00);  // Green

    GrFog_t fogtable[64];
    fogGenerateExp(fogtable, 0.01f);
    fogTable(fogtable);

    bufferClear(0x00, 0, 0);

    GrVertex vtxA, vtxB, vtxC;

    vtxA.x = 0.0f;
    vtxA.y = 0.0f;
    vtxA.ooz = 1.0f;
    vtxA.r = 0.0f;
    vtxA.g = 0.0f;
    vtxA.b = 64.0f;
    vtxA.a = 255.0f;
    vtxA.oow = 1.0f / 20.0f;

    vtxB.x = 10.24f;
    vtxB.y = 480.0f;
    vtxB.ooz = 1.0f;
    vtxB.r = 0.0f;
    vtxB.g = 0.0f;
    vtxB.b = 128.0f;
    vtxB.a = 255.0f;
    vtxB.oow = 1.0f / 2000.0f;

    vtxC.x = 640.0f;
    vtxC.y = 9.984f;
    vtxC.ooz = 1.0f;
    vtxC.r = 0.0f;
    vtxC.g = 0.0f;
    vtxC.b = 64.0f;
    vtxC.a = 255.0f;
    vtxC.oow = 1.0f / 20.0f;

    drawTriangle(&vtxA, &vtxB, &vtxC);

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // 1. Close pixel: near (320, 10) -> W is close to 20
    // Fog factor f = e^(-0.01 * 20) = 0.8187.
    // Color should be mostly Blue, very little Green fog
    // Red = 0, Green = 255 * 0.1813 = 46, Blue = 64 * 0.8187 = 52
    uint32_t p_close = fb[10 * 640 + 320];
    uint8_t r = p_close & 0xFF;
    uint8_t g = (p_close >> 8) & 0xFF;
    uint8_t b = (p_close >> 16) & 0xFF;
    REQUIRE(r <= 4);
    REQUIRE(std::abs((int)g - 46) <= 8);
    REQUIRE(std::abs((int)b - 52) <= 8);

    // 2. Far pixel: near (10, 470) -> W is close to 2000
    // Fog factor f = e^(-0.01 * 2000) = 0.
    // Color should be 100% Green fog color (0x0000FF00)
    // Red = 0, Green = 255, Blue = 0
    uint32_t p_far = fb[470 * 640 + 10];
    r = p_far & 0xFF;
    g = (p_far >> 8) & 0xFF;
    b = (p_far >> 16) & 0xFF;
    REQUIRE(r <= 4);
    REQUIRE(std::abs((int)g - 255) <= 4);
    REQUIRE(b <= 4);

    // Restore fog mode to disabled
    fogMode(GR_FOG_DISABLE);
  }

  SECTION("test08_extra: Additive and Subtractive Fogging") {
    // --------------------------------------------------------
    // Part A: Additive Fog Mode (GR_FOG_WITH_TABLE | GR_FOG_MULT2)
    // --------------------------------------------------------
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    fogMode(GR_FOG_WITH_TABLE | GR_FOG_MULT2);
    fogColorValue(0xFF00FF00);  // Green

    GrFog_t fogtable[64];
    fogGenerateExp(fogtable, 0.01f);
    fogTable(fogtable);

    bufferClear(0x00, 0, 0);

    GrVertex vtxA, vtxB, vtxC;
    vtxA.x = 0.0f;
    vtxA.y = 0.0f;
    vtxA.ooz = 1.0f;
    vtxA.r = 0.0f;
    vtxA.g = 0.0f;
    vtxA.b = 64.0f;
    vtxA.a = 255.0f;
    vtxA.oow = 1.0f / 20.0f;

    vtxB.x = 10.24f;
    vtxB.y = 480.0f;
    vtxB.ooz = 1.0f;
    vtxB.r = 0.0f;
    vtxB.g = 0.0f;
    vtxB.b = 128.0f;
    vtxB.a = 255.0f;
    vtxB.oow = 1.0f / 2000.0f;

    vtxC.x = 640.0f;
    vtxC.y = 9.984f;
    vtxC.ooz = 1.0f;
    vtxC.r = 0.0f;
    vtxC.g = 0.0f;
    vtxC.b = 64.0f;
    vtxC.a = 255.0f;
    vtxC.oow = 1.0f / 20.0f;

    drawTriangle(&vtxA, &vtxB, &vtxC);

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Near pixel: W is close to 20. f = 0.1813.
    // Additive Fog: Color = FogColor * f = Green * 0.1813 = (0, 46, 0)
    uint32_t p_add_close = fb[10 * 640 + 320];
    uint8_t r = p_add_close & 0xFF;
    uint8_t g = (p_add_close >> 8) & 0xFF;
    uint8_t b = (p_add_close >> 16) & 0xFF;
    REQUIRE(r <= 8);
    REQUIRE(std::abs((int)g - 46) <= 8);
    REQUIRE(b <= 8);

    // Far pixel: W is close to 2000. f = 1.0.
    // Additive Fog: Color = FogColor * 1.0 = (0, 255, 0)
    uint32_t p_add_far = fb[470 * 640 + 10];
    r = p_add_far & 0xFF;
    g = (p_add_far >> 8) & 0xFF;
    b = (p_add_far >> 16) & 0xFF;
    REQUIRE(r <= 8);
    REQUIRE(std::abs((int)g - 255) <= 4);
    REQUIRE(b <= 8);

    // --------------------------------------------------------
    // Part B: Subtractive Fog Mode (GR_FOG_WITH_TABLE | GR_FOG_ADD2)
    // --------------------------------------------------------
    fogMode(GR_FOG_WITH_TABLE | GR_FOG_ADD2);
    bufferClear(0x00, 0, 0);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Near pixel: W is close to 20. f = 0.1813.
    // Subtractive Fog: Color = InputColor * (1 - f) = Blue(64) * 0.8187 = 52.4
    // -> 52. Swapped client read-back: Red channel receives the Blue input.
    uint32_t p_sub_close = fb[10 * 640 + 320];
    r = p_sub_close & 0xFF;
    g = (p_sub_close >> 8) & 0xFF;
    b = (p_sub_close >> 16) & 0xFF;
    REQUIRE(r <= 8);
    REQUIRE(g <= 8);
    REQUIRE(std::abs((int)b - 52) <= 8);

    // Far pixel: W is close to 2000. f = 1.0.
    // Subtractive Fog: Color = InputColor * (1 - 1.0) = 0.
    uint32_t p_sub_far = fb[470 * 640 + 10];
    r = p_sub_far & 0xFF;
    g = (p_sub_far >> 8) & 0xFF;
    b = (p_sub_far >> 16) & 0xFF;
    REQUIRE(r <= 8);
    REQUIRE(g <= 8);
    REQUIRE(b <= 8);

    // --------------------------------------------------------
    // Part C: Iterated Alpha Fog Mode (GR_FOG_WITH_ITERATED_ALPHA)
    // --------------------------------------------------------
    fogColorValue(0x0000FF00);  // Green Fog
    fogMode(GR_FOG_WITH_ITERATED_ALPHA);
    bufferClear(0x00, 0, 0);

    // Setup constant alpha 64 for all vertices to verify uniform alpha fogging
    vtxA.a = 64.0f;
    vtxB.a = 64.0f;
    vtxC.a = 64.0f;

    drawTriangle(&vtxA, &vtxB, &vtxC);

    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Check a pixel inside the triangle (e.g., center)
    uint32_t p_iter = fb[100 * 640 + 320];
    r = p_iter & 0xFF;
    g = (p_iter >> 8) & 0xFF;
    b = (p_iter >> 16) & 0xFF;

    // Swapped readback: Red channel gets the Blue input color.
    // Color_out = Blue_perspective_interpolated(77.3) * (1 - 0.251) = (58, 64,
    // 0) So client read-back: r = 0, g = 64, b = 58
    REQUIRE(r <= 8);
    REQUIRE(std::abs((int)g - 64) <= 8);
    REQUIRE(std::abs((int)b - 58) <= 8);

    // Restore vertex alpha to default 255.0f
    vtxA.a = 255.0f;
    vtxB.a = 255.0f;
    vtxC.a = 255.0f;

    // Restore fog color and mode
    fogColorValue(0x0000FF00);  // Default green
    fogMode(GR_FOG_DISABLE);
  }

  SECTION("SDK parity (13): Gouraud Iterated Alpha") {
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ZERO, GR_BLEND_ZERO);
    depthBufferMode(GR_DEPTHBUFFER_DISABLE);

    bufferClear(0x00, 0, 0);

    GrVertex vtxA, vtxB, vtxC;
    vtxA.x = 320.0f;
    vtxA.y = 48.0f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 1.0f;
    vtxB.x = 512.0f;
    vtxB.y = 432.0f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 1.0f;
    vtxC.x = 128.0f;
    vtxC.y = 432.0f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 1.0f;

    // 1. Draw solid RED background triangle (opaque alpha)
    vtxA.a = vtxB.a = vtxC.a = 255.0f;
    constantColorValue(0x000000FF);  // Red (in ABGR)
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // 2. Draw fading BLUE overlay triangle (iterated alpha, lower-left origin)
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    vtxA.a = 0.0f;             // transparent at vertex A (rendered at bottom)
    vtxB.a = vtxC.a = 255.0f;  // opaque at vertex B, C (rendered at top)
    constantColorValue(0x00FF0000);  // Blue (in ABGR)
    drawTriangle(&vtxA, &vtxB, &vtxC);
    sstOrigin(GR_ORIGIN_UPPER_LEFT);  // Restore

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Along x = 320:
    // 1. Top region (y = 60) -> overlay is almost opaque Blue -> must be Blue
    // Red = 0, Green = 0, Blue = 255
    uint32_t p_top = fb[60 * 640 + 320];
    uint8_t r = p_top & 0xFF;
    uint8_t g = (p_top >> 8) & 0xFF;
    uint8_t b = (p_top >> 16) & 0xFF;
    REQUIRE(r <= 32);  // allow some gradient margin
    REQUIRE(g <= 4);
    REQUIRE(std::abs((int)b - 255) <= 32);

    // 2. Mid region (y = 240) -> blended halfway -> Magenta
    // Red = 127, Green = 0, Blue = 127
    uint32_t p_mid = fb[240 * 640 + 320];
    r = p_mid & 0xFF;
    g = (p_mid >> 8) & 0xFF;
    b = (p_mid >> 16) & 0xFF;
    REQUIRE(std::abs((int)r - 127) <= 16);
    REQUIRE(g <= 4);
    REQUIRE(std::abs((int)b - 127) <= 16);

    // 3. Bottom region (y = 420) -> overlay is almost transparent -> must be
    // Red Red = 255, Green = 0, Blue = 0
    uint32_t p_bot = fb[420 * 640 + 320];
    r = p_bot & 0xFF;
    g = (p_bot >> 8) & 0xFF;
    b = (p_bot >> 16) & 0xFF;
    REQUIRE(std::abs((int)r - 255) <= 32);
    REQUIRE(g <= 4);
    REQUIRE(b <= 32);
  }

  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("Multi-pass fog (ADD2/MULT2 + PREFOG_COLOR) & mipmap LOD offset") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadPrev;
    std::string val;
    ~EnvRestorer() {
      if (hadPrev) {
        ::setenv("FX_GLIDE_NO_SPLASH", val.c_str(), 1);
      } else {
        ::unsetenv("FX_GLIDE_NO_SPLASH");
      }
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);
  FxBool openOk =
      grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                   GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);
  grDitherMode(GR_DITHER_DISABLE);

  SECTION("Two-pass GR_FOG_ADD2 + GR_FOG_MULT2 with GR_BLEND_PREFOG_COLOR") {
    GrFog_t fogTable[GR_FOG_TABLE_SIZE];
    std::memset(fogTable, 128, sizeof(fogTable));  // f = 128/255 (~0.502)
    grFogColorValue(0x00404040u);                  // Cf = (64, 64, 64)
    grFogTable(fogTable);

    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);

    auto drawFullQuad = [](float oow) {
      GrVertex a{}, b{}, c{}, d{};
      a.x = 100.0f;
      a.y = 100.0f;
      a.oow = oow;
      b.x = 300.0f;
      b.y = 100.0f;
      b.oow = oow;
      c.x = 300.0f;
      c.y = 300.0f;
      c.oow = oow;
      d.x = 100.0f;
      d.y = 300.0f;
      d.oow = oow;
      grDrawTriangle(&a, &b, &c);
      grDrawTriangle(&a, &c, &d);
    };

    // Pass 1: C1 = (R=200, G=160, B=80) in ABGR -> 0x0050A0C8
    grConstantColorValue(0x0050A0C8u);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);
    grFogMode(GR_FOG_WITH_TABLE | GR_FOG_ADD2);
    drawFullQuad(0.1f);

    // Pass 2: C2 = (R=128, G=255, B=192) in ABGR -> 0x00C0FF80
    // Expected: Cf * f + (1 - f) * (C1 * C2)
    //   R = 64*(128/255) + (127/255)*(200*(128/255)) = 82.1
    //   G = 64*(128/255) + (127/255)*(160*(255/255)) = 111.8
    //   B = 64*(128/255) + (127/255)*(80*(192/255))  = 62.1
    grConstantColorValue(0x00C0FF80u);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_PREFOG_COLOR, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);
    grFogMode(GR_FOG_WITH_TABLE | GR_FOG_MULT2);
    drawFullQuad(0.1f);

    // Sample center of quad (x=200, y=200 in screen coords; y=200 is inside
    // [100, 300] under both lower-left and upper-left interpretations).
    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 200, 200, 1, 1, 4, &px) ==
            FXTRUE);
    const int r = static_cast<int>(px & 0xFFu);
    const int g = static_cast<int>((px >> 8) & 0xFFu);
    const int b = static_cast<int>((px >> 16) & 0xFFu);
    REQUIRE(std::abs(r - 82) <= 4);
    REQUIRE(std::abs(g - 112) <= 4);
    REQUIRE(std::abs(b - 62) <= 4);

    // Boundary safety check: eyeW far beyond s_tableIndexToW[63] must not OOB
    // and must saturate cleanly to fogTable[63].
    fogTable[62] = 255;
    fogTable[63] = 255;
    grFogTable(fogTable);
    grFogMode(GR_FOG_WITH_TABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);
    grConstantColorValue(0x00FFFFFFu);
    drawFullQuad(1e-6f);
    uint32_t farPx = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 200, 200, 1, 1, 4, &farPx) ==
            FXTRUE);
    REQUIRE(std::abs(static_cast<int>(farPx & 0xFFu) - 64) <= 4);
    REQUIRE(std::abs(static_cast<int>((farPx >> 8) & 0xFFu) - 64) <= 4);
    REQUIRE(std::abs(static_cast<int>((farPx >> 16) & 0xFFu) - 64) <= 4);
  }

  SECTION("Mipmapped texture with largeLod=GR_LOD_64 selects correct mip") {
    // Build a 64x64..1x1 RGB565 mip chain (largeLod=GR_LOD_64=2, smallLod=8):
    //   mip 0 (64x64): pure RED   (0xF800)
    //   mip 1 (32x32): pure BLUE  (0x001F)
    //   mip 2 (16x16): pure GREEN (0x07E0)
    //   mips 3..6    : pure GREEN (0x07E0)
    std::vector<uint16_t> mipData;
    auto appendLevel = [&](int dim, uint16_t color565) {
      mipData.insert(mipData.end(), static_cast<size_t>(dim * dim), color565);
    };
    appendLevel(64, 0xF800u);  // mip 0 -> RED
    appendLevel(32, 0x001Fu);  // mip 1 -> BLUE
    appendLevel(16, 0x07E0u);  // mip 2 -> GREEN
    appendLevel(8, 0x07E0u);
    appendLevel(4, 0x07E0u);
    appendLevel(2, 0x07E0u);
    appendLevel(1, 0x07E0u);

    GrTexInfo texInfo{};
    texInfo.largeLod = GR_LOD_64;
    texInfo.smallLod = GR_LOD_1;
    texInfo.aspectRatio = GR_ASPECT_1x1;
    texInfo.format = GR_TEXFMT_RGB_565;
    texInfo.data = mipData.data();

    const FxU32 texAddr = grTexMinAddress(GR_TMU0);
    grTexDownloadMipMap(GR_TMU0, texAddr, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    grTexSource(GR_TMU0, texAddr, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    grTexLodBiasValue(GR_TMU0, 0.0f);
    grFogMode(GR_FOG_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);

    // Map full 64x64 texture onto a 16x16 screen quad -> minification ratio =
    // 4x -> relative LOD = log2(4) = 2.0 -> MUST sample mip 2 (GREEN), NOT mip
    // 0 (RED). (Using 256.0f spans the exact Glide [0, 256] normalized domain;
    // 255.0f would yield ratio 3.984x -> log2 = 1.994 which authentic SST-1
    // GR_MIPMAP_NEAREST truncation maps to mip 1.)
    GrVertex a{}, b{}, c{}, d{};
    a.x = 100.0f;
    a.y = 200.0f;
    a.oow = 1.0f;
    a.tmuvtx[0].sow = 0.0f;
    a.tmuvtx[0].tow = 0.0f;
    b.x = 116.0f;
    b.y = 200.0f;
    b.oow = 1.0f;
    b.tmuvtx[0].sow = 256.0f;
    b.tmuvtx[0].tow = 0.0f;
    c.x = 116.0f;
    c.y = 216.0f;
    c.oow = 1.0f;
    c.tmuvtx[0].sow = 256.0f;
    c.tmuvtx[0].tow = 256.0f;
    d.x = 100.0f;
    d.y = 216.0f;
    d.oow = 1.0f;
    d.tmuvtx[0].sow = 0.0f;
    d.tmuvtx[0].tow = 256.0f;
    grDrawTriangle(&a, &b, &c);
    grDrawTriangle(&a, &c, &d);

    // Under GR_ORIGIN_LOWER_LEFT, y in [200, 216] maps to LFB upper-left y in
    // [480 - 216, 480 - 200] = [264, 280]; center is (108, 272).
    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 108, 272, 1, 1, 4, &px) ==
            FXTRUE);
    const uint32_t r = px & 0xFFu;
    const uint32_t g = (px >> 8) & 0xFFu;
    const uint32_t bCh = (px >> 16) & 0xFFu;
    REQUIRE(g >= 240);
    REQUIRE(r <= 10);
    REQUIRE(bCh <= 10);
  }

  grSstWinClose();
  grGlideShutdown();
#endif
  }

}

TEST_CASE("Glide 2.x: Chromakey, Range Keying & Text Rendering", "[Glide2][Chroma][Text]") {
  SECTION("Chroma-key range rendering") {
#ifdef __linux__
  const char* envBackend = std::getenv("GLIDE_NG_BACKEND");
  std::string activeBackend = envBackend ? envBackend : "vulkan";

  INFO("Testing Chroma-Key Range on Backend: " << activeBackend);

  // Load the shared library dynamically
  void* handle = OpenGlide2Handle(RTLD_NOW | RTLD_GLOBAL);
  REQUIRE(handle != nullptr);

  // Resolve needed functions
  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using BufferSwapFunc = void (*)(FxU32);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using ChromakeyModeFunc = void (*)(GrChromakeyMode_t);
  using DrawTriangleFunc = void (*)(const void*, const void*, const void*);

  auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto shutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto bufferClear =
      reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
  auto bufferSwap =
      reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
  auto lfbReadRegion =
      reinterpret_cast<LfbReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
  using ChromakeyRangeExtFunc = void (*)(GrColor_t, GrColor_t, uint32_t);
  auto chromakeyRangeExt = reinterpret_cast<ChromakeyRangeExtFunc>(
      ::dlsym(handle, "grChromakeyRangeExt"));
  auto chromakeyMode =
      reinterpret_cast<ChromakeyModeFunc>(::dlsym(handle, "grChromakeyMode"));
  auto drawTriangle =
      reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));

  REQUIRE(init != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(bufferSwap != nullptr);
  REQUIRE(lfbReadRegion != nullptr);
  REQUIRE(chromakeyRangeExt != nullptr);
  REQUIRE(chromakeyMode != nullptr);
  REQUIRE(drawTriangle != nullptr);

  // Initialize Glide first to load environment overrides and lock the correct
  // backend
  REQUIRE_NOTHROW(init());

  // Open the window
  FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 0);
  REQUIRE(ok == FXTRUE);

  // Resolve additional state functions for robust initialization and to prevent
  // state leaks We use standard C/C++ types (int, float) in the signatures to
  // ensure 100% compile-safe ABI compatibility.
  using StateFunc5 = void (*)(int, int, int, int, int);
  using StateFunc7 = void (*)(int, int, int, int, int, int, int);
  using StateFunc1 = void (*)(int);
  using StateFunc4 = void (*)(int, uint32_t, uint32_t, void*);

  auto colorCombine =
      reinterpret_cast<StateFunc5>(::dlsym(handle, "grColorCombine"));
  auto alphaCombine =
      reinterpret_cast<StateFunc5>(::dlsym(handle, "grAlphaCombine"));
  auto depthBufferMode =
      reinterpret_cast<StateFunc1>(::dlsym(handle, "grDepthBufferMode"));
  auto alphaTestFunction =
      reinterpret_cast<StateFunc1>(::dlsym(handle, "grAlphaTestFunction"));
  auto texCombine =
      reinterpret_cast<StateFunc7>(::dlsym(handle, "grTexCombine"));
  auto fogMode = reinterpret_cast<StateFunc1>(::dlsym(handle, "grFogMode"));
  auto texSource = reinterpret_cast<StateFunc4>(::dlsym(handle, "grTexSource"));

  REQUIRE(colorCombine != nullptr);
  REQUIRE(alphaCombine != nullptr);
  REQUIRE(depthBufferMode != nullptr);
  REQUIRE(alphaTestFunction != nullptr);
  REQUIRE(texCombine != nullptr);
  REQUIRE(fogMode != nullptr);
  REQUIRE(texSource != nullptr);

  // Reset combiners to draw simple iterated vertex colors:
  // GR_COMBINE_FUNCTION_LOCAL = 1, GR_COMBINE_FACTOR_NONE = 0,
  // GR_COMBINE_LOCAL_ITERATED = 0, GR_COMBINE_OTHER_NONE = 2
  colorCombine(1, 0, 0, 2, 0);
  alphaCombine(1, 0, 0, 2, 0);

  // Disable depth buffering, alpha testing, texturing on both TMUs, and
  // fogging: GR_DEPTHBUFFER_DISABLE = 0, GR_CMP_ALWAYS = 7,
  // GR_COMBINE_FUNCTION_ZERO = 0, GR_FOG_DISABLE = 0
  depthBufferMode(0);
  alphaTestFunction(7);
  texCombine(0, 0, 0, 0, 0, 0, 0);  // TMU0
  texCombine(1, 0, 0, 0, 0, 0, 0);  // TMU1
  texSource(0, 0xFFFFFFFF, 0,
            nullptr);  // Unbind TMU0 texture (resets m_boundTexAddress[0])
  texSource(1, 0xFFFFFFFF, 0,
            nullptr);  // Unbind TMU1 texture (resets m_boundTexAddress[1])
  fogMode(0);

  // Enable Chroma-Keying
  chromakeyMode(GR_CHROMAKEY_ENABLE);

  // Define vertices for a large triangle covering the center of the screen
  // Aligning exactly with the official Glide 2.x GrVertex struct layout:
  struct SimpleVertex {
    float x, y, z;        // X, Y, Z (float 0, 1, 2)
    float r, g, b;        // R, G, B (float 3, 4, 5)
    float ooz;            // ooz     (float 6)
    float a;              // Alpha   (float 7)
    float oow;            // oow     (float 8)
    float s, t, oow_tex;  // TMU0 s, t, oow (float 9, 10, 11)
  };

  // x, y, z,  r, g, b, ooz, a, oow,  s, t, oow_tex
  SimpleVertex v0 = {100.0f, 100.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  SimpleVertex v1 = {540.0f, 100.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  SimpleVertex v2 = {320.0f, 380.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f, 1.0f};

  // --- CASE 1: INTERSECTION MODE - DISCARD MATCHING COLOR ---
  // Min color: R=100, G=120, B=50  -> 0x00647832
  // Max color: R=150, G=180, B=80  -> 0x0096B450
  // Mode: Range Enabled, Inclusive, Intersection -> 1 << 28 = 0x10000000
  chromakeyRangeExt(0x00647832, 0x0096B450, 0x10000000);

  // Clear background to pure solid blue: 0xFF0000FF
  bufferClear(0xFF0000FF, 0xFF, 0);

  // Setup vertices to have color: R=128, G=150, B=60 -> 0xFF80963C (Perfect
  // match on all channels!)
  v0.r = v1.r = v2.r = 128.0f;
  v0.g = v1.g = v2.g = 150.0f;
  v0.b = v1.b = v2.b = 60.0f;

  // Draw the triangle
  drawTriangle(&v0, &v1, &v2);
  bufferSwap(1);

  // Read the center pixel (at 320, 240) from the front buffer
  uint32_t pixelVal1 = 0;
  ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal1);
  REQUIRE(ok == FXTRUE);

  // Unpack channels
  uint8_t r1 = (pixelVal1 >> 16) & 0xFF;
  uint8_t g1 = (pixelVal1 >> 8) & 0xFF;
  uint8_t b1 = pixelVal1 & 0xFF;

  INFO("pixelVal1 (Case 1) = 0x" << std::hex << pixelVal1 << " | R=" << (int)r1
                                 << ", G=" << (int)g1 << ", B=" << (int)b1);

  // Since the color matched all channels, the triangle should be DISCARDED!
  // Center pixel should remain the background color Solid Blue: 0xFF0000FF
  REQUIRE((int)r1 <= 5);
  REQUIRE((int)g1 <= 5);
  REQUIRE((int)b1 >= 250);

  // --- CASE 2: INTERSECTION MODE - RENDER NON-MATCHING COLOR ---
  // Setup vertices to have color: R=200, G=150, B=60 (Red is 200, which is
  // outside [100, 150]) Since it's Intersection Mode, this should NOT discard!
  bufferClear(0xFF0000FF, 0xFF, 0);
  v0.r = v1.r = v2.r = 200.0f;
  v0.g = v1.g = v2.g = 150.0f;
  v0.b = v1.b = v2.b = 60.0f;

  drawTriangle(&v0, &v1, &v2);
  bufferSwap(1);

  uint32_t pixelVal2 = 0;
  ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal2);
  REQUIRE(ok == FXTRUE);

  uint8_t r2 = (pixelVal2 >> 16) & 0xFF;
  uint8_t g2 = (pixelVal2 >> 8) & 0xFF;
  uint8_t b2 = pixelVal2 & 0xFF;

  INFO("pixelVal2 (Case 2) = 0x" << std::hex << pixelVal2 << " | R=" << (int)r2
                                 << ", G=" << (int)g2 << ", B=" << (int)b2);

  // Center pixel should be the triangle's color: R=200, G=150, B=60 (with
  // tolerance of 2)
  REQUIRE((int)r2 >= 198);
  REQUIRE((int)r2 <= 202);
  REQUIRE((int)g2 >= 148);
  REQUIRE((int)g2 <= 152);
  REQUIRE((int)b2 >= 58);
  REQUIRE((int)b2 <= 62);

  // --- CASE 3: UNION MODE - DISCARD PARTIAL MATCHING COLOR ---
  // Mode: Range Enabled, Inclusive, Union Mode -> (1 << 28) | (1 << 27) =
  // 0x18000000
  chromakeyRangeExt(0x00647832, 0x0096B450, 0x18000000);

  bufferClear(0xFF0000FF, 0xFF, 0);
  // Draw the same color: R=200, G=150, B=60.
  // Green (150) and Blue (60) are inside. Red (200) is outside.
  // In Union Mode, since Green and Blue match, the pixel MUST BE DISCARDED!
  drawTriangle(&v0, &v1, &v2);
  bufferSwap(1);

  uint32_t pixelVal3 = 0;
  ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal3);
  REQUIRE(ok == FXTRUE);

  uint8_t r3 = (pixelVal3 >> 16) & 0xFF;
  uint8_t g3 = (pixelVal3 >> 8) & 0xFF;
  uint8_t b3 = pixelVal3 & 0xFF;

  INFO("pixelVal3 (Case 3) = 0x" << std::hex << pixelVal3 << " | R=" << (int)r3
                                 << ", G=" << (int)g3 << ", B=" << (int)b3);

  // Triangle discarded -> Center pixel remains background Solid Blue:
  // 0xFF0000FF
  REQUIRE((int)r3 <= 5);
  REQUIRE((int)g3 <= 5);
  REQUIRE((int)b3 >= 250);

  // --- CASE 4: DISABLE RANGE MODE & FALLBACK TO EXACT MATCH ---
  using ChromakeyValueFunc = void (*)(GrColor_t);
  auto chromakeyValue =
      reinterpret_cast<ChromakeyValueFunc>(::dlsym(handle, "grChromakeyValue"));
  REQUIRE(chromakeyValue != nullptr);

  // 1. Explicitly disable range mode by passing 0 in the mode parameter
  chromakeyRangeExt(0, 0, 0);

  // 2. Configure exact match chromakey color: R=128, G=150, B=60 -> 0x0080963C
  chromakeyValue(0x0080963C);

  // 3. Render exact match color (R=128, G=150, B=60) -> should be DISCARDED
  bufferClear(0xFF0000FF, 0xFF, 0);
  v0.r = v1.r = v2.r = 128.0f;
  v0.g = v1.g = v2.g = 150.0f;
  v0.b = v1.b = v2.b = 60.0f;

  drawTriangle(&v0, &v1, &v2);
  bufferSwap(1);

  uint32_t pixelVal4_match = 0;
  ok =
      lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal4_match);
  REQUIRE(ok == FXTRUE);

  uint8_t r4_m = (pixelVal4_match >> 16) & 0xFF;
  uint8_t g4_m = (pixelVal4_match >> 8) & 0xFF;
  uint8_t b4_m = pixelVal4_match & 0xFF;

  // Should be discarded -> remains Solid Blue
  REQUIRE((int)r4_m <= 5);
  REQUIRE((int)g4_m <= 5);
  REQUIRE((int)b4_m >= 250);

  // 4. Render a color that would match the old range but does NOT match the
  // exact value: Red = 138 (inside old [100, 150] range, but not exactly 128)
  // Since range mode is disabled, this should NOT be discarded and should
  // render normally!
  bufferClear(0xFF0000FF, 0xFF, 0);
  v0.r = v1.r = v2.r = 138.0f;

  drawTriangle(&v0, &v1, &v2);
  bufferSwap(1);

  uint32_t pixelVal4_nomatch = 0;
  ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4,
                     &pixelVal4_nomatch);
  REQUIRE(ok == FXTRUE);

  uint8_t r4_nm = (pixelVal4_nomatch >> 16) & 0xFF;
  uint8_t g4_nm = (pixelVal4_nomatch >> 8) & 0xFF;
  uint8_t b4_nm = pixelVal4_nomatch & 0xFF;

  // Should NOT be discarded -> rendered as Red=138, Green=150, Blue=60 (with
  // tolerance 2)
  REQUIRE((int)r4_nm >= 136);
  REQUIRE((int)r4_nm <= 140);
  REQUIRE((int)g4_nm >= 148);
  REQUIRE((int)g4_nm <= 152);
  REQUIRE((int)b4_nm >= 58);
  REQUIRE((int)b4_nm <= 62);

  // Clean up
  REQUIRE_NOTHROW(shutdown());
  ::dlclose(handle);
#endif
  }

  SECTION("Alpha 8 font texture rendering & damage detection") {
#ifdef __linux__
  // Dynamically load the Glide 2 wrapper library
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  // Resolve Glide 2.x function pointers
  using GlideInitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using TexDownloadFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexSourceFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexClampFunc =
      void (*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
  using TexFilterFunc =
      void (*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
  using TexCombineFunc =
      void (*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t,
               GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
  using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using DrawTriangleFunc =
      void (*)(const GrVertex*, const GrVertex*, const GrVertex*);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
  auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
  auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
  auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
  auto texDownload = (TexDownloadFunc)dlsym(handle, "grTexDownloadMipMap");
  auto texSource = (TexSourceFunc)dlsym(handle, "grTexSource");
  auto texClamp = (TexClampFunc)dlsym(handle, "grTexClampMode");
  auto texFilter = (TexFilterFunc)dlsym(handle, "grTexFilterMode");
  auto texCombine = (TexCombineFunc)dlsym(handle, "grTexCombine");
  auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
  auto alphaCombine = (AlphaCombineFunc)dlsym(handle, "grAlphaCombine");
  auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(texDownload != nullptr);
  REQUIRE(texSource != nullptr);
  REQUIRE(texClamp != nullptr);
  REQUIRE(texFilter != nullptr);
  REQUIRE(texCombine != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(alphaCombine != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  // Initialize Glide 2.x
  glideInit();
  sstSelect(0);

  FxBool openOk = winOpen(0, GR_RESOLUTION_800x600, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  bufferClear(0xFF000000, 0, 0);  // Clear to Black

  // Prepare 32x16 Texture in GR_TEXFMT_ALPHA_8
  // Top half (y = 0..7) is solid white (0xFF), bottom half (y = 8..15) is
  // transparent/black (0x00)
  std::vector<uint8_t> texData(32 * 16, 0);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 32; ++x) {
      texData[y * 32 + x] = 0xFF;
    }
  }

  GrTexInfo texInfo;
  texInfo.smallLod = GR_LOD_32;  // LOD 32 -> 32x32 Max dimension
  texInfo.largeLod = GR_LOD_32;
  texInfo.aspectRatio = GR_ASPECT_2x1;  // 2W x 1H -> 32x16
  texInfo.format = GR_TEXFMT_ALPHA_8;
  texInfo.data = texData.data();

  // Download to TMU0
  texDownload(0, 0x00002000, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
  texSource(0, 0x00002000, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
  texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
  texFilter(0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

  // Route texture alpha channel to TMU0 output
  texCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
             GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
             FXFALSE);

  // Route TMU0 output to framebuffer color (RGB) and alpha channels
  colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  // Define vertices for a centered 200x200 quad (x from 300 to 500, y from 300
  // to 500) Glide 2.x coordinates are in pixel space. Scale factor for 32x16
  // texture: D=32, so scale = 256.0 / 32 = 8.0. s ranges from 0 to 32 * 8 =
  // 256.0. t ranges from 0 to 16 * 8 = 128.0.
  GrVertex v[4];
  for (int i = 0; i < 4; ++i) {
    v[i].z = 0.0f;
    v[i].ooz = 1.0f;
    v[i].oow = 1.0f;
    v[i].tmuvtx[0].oow = 1.0f;
    v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
  }

  // Top-Left
  v[0].x = 300.0f;
  v[0].y = 300.0f;
  v[0].tmuvtx[0].sow = 0.0f;
  v[0].tmuvtx[0].tow = 0.0f;

  // Top-Right
  v[1].x = 500.0f;
  v[1].y = 300.0f;
  v[1].tmuvtx[0].sow = 256.0f;
  v[1].tmuvtx[0].tow = 0.0f;

  // Bottom-Right
  v[2].x = 500.0f;
  v[2].y = 500.0f;
  v[2].tmuvtx[0].sow = 256.0f;
  v[2].tmuvtx[0].tow = 128.0f;

  // Bottom-Left
  v[3].x = 300.0f;
  v[3].y = 500.0f;
  v[3].tmuvtx[0].sow = 0.0f;
  v[3].tmuvtx[0].tow = 128.0f;

  // Draw two triangles to form the quad
  drawTriangle(&v[0], &v[1], &v[2]);
  drawTriangle(&v[0], &v[2], &v[3]);

  // Read back a pixel from the top half (e.g. x=400, y=350) directly from the
  // back buffer It should be solid white: 0xFFFFFFFF
  uint32_t topPixel = 0;
  FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 400, 350, 1, 1, 4, &topPixel);
  REQUIRE(ok == FXTRUE);

  // Read back a pixel from the bottom half (e.g. x=400, y=450) directly from
  // the back buffer It should be black: 0xFF000000 or similar (since alpha is
  // 0)
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

  INFO("Top half pixel at (400,350) = 0x"
       << std::hex << topPixel << " | R=" << (int)r_top << ", G=" << (int)g_top
       << ", B=" << (int)b_top);
  INFO("Bottom half pixel at (400,450) = 0x"
       << std::hex << bottomPixel << " | R=" << (int)r_bottom
       << ", G=" << (int)g_bottom << ", B=" << (int)b_bottom);

  // Top half MUST be white (since it maps to texture t < 8.0, containing 0xFF)
  REQUIRE(r_top >= 250);
  REQUIRE(g_top >= 250);
  REQUIRE(b_top >= 250);

  // Bottom half MUST be black (since it maps to texture t > 8.0, containing
  // 0x00)
  REQUIRE(r_bottom <= 5);
  REQUIRE(g_bottom <= 5);
  REQUIRE(b_bottom <= 5);

  // Clean up
  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("Pixel pipeline chromakey & triangle discard parity") {
#ifdef __linux__
  // Dynamically load the Glide 2 wrapper library
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  // Resolve Glide 2.x function pointers
  using GlideInitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ChromakeyModeFunc = void (*)(GrChromakeyMode_t);
  using ChromakeyValueFunc = void (*)(GrColor_t);
  using ConstantColorValueFunc = void (*)(GrColor_t);
  using SstOriginFunc = void (*)(GrOriginLocation_t);
  using DrawTriangleFunc =
      void (*)(const GrVertex*, const GrVertex*, const GrVertex*);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
  auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
  auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
  auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
  auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
  auto chromakeyMode = (ChromakeyModeFunc)dlsym(handle, "grChromakeyMode");
  auto chromakeyValue = (ChromakeyValueFunc)dlsym(handle, "grChromakeyValue");
  auto constantColorValue =
      (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
  auto sstOrigin = (SstOriginFunc)dlsym(handle, "grSstOrigin");
  auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(chromakeyMode != nullptr);
  REQUIRE(chromakeyValue != nullptr);
  REQUIRE(constantColorValue != nullptr);
  REQUIRE(sstOrigin != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  // Initialize Glide
  glideInit();
  sstSelect(0);

  // Open window with ABGR format
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Set up render state: constant color routed to combiner, chromakey enabled
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  chromakeyMode(GR_CHROMAKEY_ENABLE);

  // Define vertices for a triangle centered on the screen
  GrVertex vtxA, vtxB, vtxC;
  vtxA.z = vtxB.z = vtxC.z = 0.0f;
  vtxA.ooz = vtxB.ooz = vtxC.ooz = 1.0f;
  vtxA.oow = vtxB.oow = vtxC.oow = 1.0f;
  vtxA.r = vtxA.g = vtxA.b = vtxA.a = 255.0f;
  vtxB.r = vtxB.g = vtxB.b = vtxB.a = 255.0f;
  vtxC.r = vtxC.g = vtxC.b = vtxC.a = 255.0f;

  vtxA.x = 320.0f;
  vtxA.y = 48.0f;
  vtxB.x = 512.0f;
  vtxB.y = 432.0f;
  vtxC.x = 128.0f;
  vtxC.y = 432.0f;

  // SECTION 1: Test with Chromakey = Blue (0x00ff0000)
  SECTION("Chromakey Blue - Red triangle visible, Blue discarded") {
    chromakeyValue(0x00ff0000);  // Blue key

    bufferClear(0x00, 0, 0);  // Clear to Black

    // 1. Draw RED triangle (pointing down)
    constantColorValue(0x000000ff);  // Red
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // 2. Draw BLUE triangle (pointing up)
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    constantColorValue(0x00ff0000);  // Blue
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Restore origin for LFB read
    sstOrigin(GR_ORIGIN_UPPER_LEFT);

    // Read pixel from Red-only region (150, 400) -> should be Red!
    uint32_t redPixel = 0;
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 400, 1, 1, 4, &redPixel);
    REQUIRE(ok == FXTRUE);
    REQUIRE(redPixel == 0x000000FF);  // Red under ABGR (Alpha = 0)

    // Read pixel from Blue-only region (150, 80) -> should be Black
    // (discarded)!
    uint32_t bluePixel = 0;
    ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 80, 1, 1, 4, &bluePixel);
    REQUIRE(ok == FXTRUE);
    REQUIRE(bluePixel ==
            0xFF000000);  // Opaque Black (discarded, remains at clear color)
  }

  // SECTION 2: Test with Chromakey = Red (0x000000ff)
  SECTION("Chromakey Red - Blue triangle visible, Red discarded") {
    chromakeyValue(0x000000ff);  // Red key

    bufferClear(0x00, 0, 0);  // Clear to Black

    // 1. Draw RED triangle (pointing down)
    constantColorValue(0x000000ff);  // Red
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // 2. Draw BLUE triangle (pointing up)
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    constantColorValue(0x00ff0000);  // Blue
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Restore origin
    sstOrigin(GR_ORIGIN_UPPER_LEFT);

    // Read pixel from Red-only region (150, 400) -> should be Black
    // (discarded)!
    uint32_t redPixel = 0;
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 400, 1, 1, 4, &redPixel);
    REQUIRE(ok == FXTRUE);
    REQUIRE(redPixel ==
            0xFF000000);  // Opaque Black (discarded, remains at clear color)

    // Read pixel from Blue-only region (150, 80) -> should be Blue!
    uint32_t bluePixel = 0;
    ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 80, 1, 1, 4, &bluePixel);
    REQUIRE(ok == FXTRUE);
    REQUIRE(bluePixel == 0x00FF0000);  // Blue under ABGR (Alpha = 0)
  }

  // Clean up
  shutdown();
  ::dlclose(handle);
#endif
  }

}

TEST_CASE("Glide 2.x: State Serialization, Anti-Aliasing & Shameless Plug", "[Glide2][State]") {
  SECTION("State serialization & fixed-point LOD bias roundtrip") {
#ifdef __linux__
  // Force the Software backend for this test
  ::setenv("GLIDE_NG_BACKEND", "sw", 1);

  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void (*)(void))dlsym(handle, "grGlideInit");
  auto sstSelect = (void (*)(int))dlsym(handle, "grSstSelect");
  auto winOpen = (FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                             GrColorFormat_t, GrOriginLocation_t, int,
                             int))dlsym(handle, "grSstWinOpen");
  auto winClose = (FxBool (*)(FxU32))dlsym(handle, "grSstWinClose");
  auto shutdown = (void (*)(void))dlsym(handle, "grGlideShutdown");

  auto texLodBiasValue =
      (void (*)(GrChipID_t, float))dlsym(handle, "grTexLodBiasValue");
  auto glideGetState = (void (*)(GrState*))dlsym(handle, "grGlideGetState");
  auto glideSetState =
      (void (*)(const GrState*))dlsym(handle, "grGlideSetState");
  auto ditherMode = (void (*)(GrDitherMode_t))dlsym(handle, "grDitherMode");

  auto texAllocateMemory = (GrMipMapId_t (*)(
      GrChipID_t, FxU8, int, int, GrTextureFormat_t, GrMipMapMode_t, GrLOD_t,
      GrLOD_t, GrAspectRatio_t, GrTextureClampMode_t, GrTextureClampMode_t,
      GrTextureFilterMode_t, GrTextureFilterMode_t, float,
      FxBool))dlsym(handle, "guTexAllocateMemory");
  auto texSource = (void (*)(GrMipMapId_t))dlsym(handle, "guTexSource");

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(texLodBiasValue != nullptr);
  REQUIRE(glideGetState != nullptr);
  REQUIRE(glideSetState != nullptr);
  REQUIRE(ditherMode != nullptr);
  REQUIRE(texAllocateMemory != nullptr);
  REQUIRE(texSource != nullptr);

  glideInit();
  if (sstSelect) sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
  auto* sw_b = dynamic_cast<GlideWrapper::GlideStateTracker*>(b);
  REQUIRE(sw_b != nullptr);

  // 1. Set distinct LOD biases and verify backend receives them
  texLodBiasValue(GR_TMU0, -1.5f);
  texLodBiasValue(GR_TMU1, 2.25f);
  CHECK(sw_b->GetTexLodBias(0) == -1.5f);
  CHECK(sw_b->GetTexLodBias(1) == 2.25f);
  ditherMode(GR_DITHER_2x2);
  CHECK(sw_b->GetDitherMode() == GR_DITHER_2x2);

  // 2. Serialize state
  char stateBuffer[512] = {0};
  glideGetState(reinterpret_cast<GrState*>(stateBuffer));

  // 3. Clobber active states
  texLodBiasValue(GR_TMU0, 0.0f);
  texLodBiasValue(GR_TMU1, 0.0f);
  CHECK(sw_b->GetTexLodBias(0) == 0.0f);
  CHECK(sw_b->GetTexLodBias(1) == 0.0f);
  ditherMode(GR_DITHER_DISABLE);
  CHECK(sw_b->GetDitherMode() == GR_DITHER_DISABLE);

  // 4. Restore state
  glideSetState(reinterpret_cast<const GrState*>(stateBuffer));

  // 5. Assert active LOD biases are restored
  CHECK(sw_b->GetTexLodBias(0) == -1.5f);
  CHECK(sw_b->GetTexLodBias(1) == 2.25f);
  CHECK(sw_b->GetDitherMode() == GR_DITHER_2x2);

  // 6. Verify 4.2 fixed-point roundtrip in guTexAllocateMemory & guTexSource
  // Negative fractional bias (-1.75f)
  GrMipMapId_t idNeg = texAllocateMemory(
      GR_TMU0, 0, 256, 256, GR_TEXFMT_RGB_565, GR_MIPMAP_NEAREST, GR_LOD_256,
      GR_LOD_1, GR_ASPECT_1x1, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
      GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, -1.75f,
      FXFALSE);

  texSource(idNeg);
  CHECK(sw_b->GetTexLodBias(0) == -1.75f);

  // Positive fractional bias (2.25f)
  GrMipMapId_t idPos = texAllocateMemory(
      GR_TMU0, 0, 256, 256, GR_TEXFMT_RGB_565, GR_MIPMAP_NEAREST, GR_LOD_256,
      GR_LOD_1, GR_ASPECT_1x1, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
      GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 2.25f,
      FXFALSE);

  texSource(idPos);
  CHECK(sw_b->GetTexLodBias(0) == 2.25f);

  if (winClose) winClose(0);
  shutdown();
  ::dlclose(handle);
  ::unsetenv("GLIDE_NG_BACKEND");
#endif
  }

  SECTION("Shameless plug ABGR readback & state preservation") {
#ifdef __linux__
  // Ensure splash does not run during automated headless test
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadPrev;
    std::string val;
    ~EnvRestorer() {
      if (hadPrev) {
        ::setenv("FX_GLIDE_NO_SPLASH", val.c_str(), 1);
      } else {
        ::unsetenv("FX_GLIDE_NO_SPLASH");
      }
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);
  FxBool openOk =
      grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                   GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Verify grGlideGetState / grGlideSetState preserves LFB constant alpha and
  // LFB write color format (used by RenderBanner / grShamelessPlug).
  grLfbConstantAlpha(0x7A);
  grLfbWriteColorFormat(GR_COLORFORMAT_ARGB);
  GrState savedState{};
  grGlideGetState(&savedState);

  grLfbConstantAlpha(0x11);
  grLfbWriteColorFormat(GR_COLORFORMAT_ABGR);
  grGlideSetState(&savedState);

  // Restore standard ABGR window state before drawing test16 pattern.
  grLfbConstantAlpha(0xFF);
  grLfbWriteColorFormat(GR_COLORFORMAT_ABGR);
  grDitherMode(GR_DITHER_DISABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

  // Draw RED (0x000000FF in ABGR) and BLUE (0x00FF0000 in ABGR) quads away
  // from the 180x90 lower-right shameless plug banner.
  grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);

  auto drawQuad = [](float x0, float y0, float x1, float y1, GrColor_t abgr) {
    grConstantColorValue(abgr);
    GrVertex a{}, b{}, c{}, d{};
    a.x = x0;
    a.y = y0;
    a.oow = 1.0f;
    b.x = x1;
    b.y = y0;
    b.oow = 1.0f;
    c.x = x1;
    c.y = y1;
    c.oow = 1.0f;
    d.x = x0;
    d.y = y1;
    d.oow = 1.0f;
    grDrawTriangle(&a, &b, &c);
    grDrawTriangle(&a, &c, &d);
  };

  drawQuad(20.0f, 20.0f, 220.0f, 220.0f, 0x000000FFu);   // Pure RED in ABGR
  drawQuad(260.0f, 20.0f, 440.0f, 220.0f, 0x00FF0000u);  // Pure BLUE in ABGR

  // Enable Shameless Plug and swap: RenderBanner() performs LFB readback +
  // banner write + FlushLFBToGPU before swapping back->front.
  grGlideShamelessPlug(FXTRUE);
  grBufferSwap(1);

  // Read back from FRONTBUFFER in 32-bit ABGR (R in bits 0..7, G in 8..15, B
  // in 16..23) and verify R/B channels were NOT swapped by LFB readback.
  uint32_t redPx = 0;
  uint32_t bluePx = 0;
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 100, 100, 1, 1, 4, &redPx) ==
          FXTRUE);
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 340, 100, 1, 1, 4, &bluePx) ==
          FXTRUE);

  const uint32_t redR = redPx & 0xFFu;
  const uint32_t redG = (redPx >> 8) & 0xFFu;
  const uint32_t redB = (redPx >> 16) & 0xFFu;
  REQUIRE(redR >= 245);
  REQUIRE(redG <= 10);
  REQUIRE(redB <= 10);

  const uint32_t blueR = bluePx & 0xFFu;
  const uint32_t blueG = (bluePx >> 8) & 0xFFu;
  const uint32_t blueB = (bluePx >> 16) & 0xFFu;
  REQUIRE(blueB >= 245);
  REQUIRE(blueG <= 10);
  REQUIRE(blueR <= 10);

  // Also verify the 180x90 shameless plug banner actually drew into the
  // lower-right corner.
  std::vector<uint32_t> bannerPixels(180 * 90, 0);
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 640 - 180, 480 - 90, 180, 90,
                          180 * 4, bannerPixels.data()) == FXTRUE);
  int nonBlackBannerPixels = 0;
  for (uint32_t px : bannerPixels) {
    if ((px & 0x00FFFFFFu) != 0) {
      ++nonBlackBannerPixels;
    }
  }
  REQUIRE(nonBlackBannerPixels > 1000);

  grGlideShamelessPlug(FXFALSE);
  grSstWinClose();
  grGlideShutdown();
#endif
  }

  SECTION("Anti-aliased lower-left rendering & reopen cycles") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadPrev;
    std::string val;
    ~EnvRestorer() {
      if (hadPrev) {
        ::setenv("FX_GLIDE_NO_SPLASH", val.c_str(), 1);
      } else {
        ::unsetenv("FX_GLIDE_NO_SPLASH");
      }
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);

  SECTION("SDK parity (25): guTex + grGlideGet/SetState + grAADrawTriangle") {
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2,
                         1) == FXTRUE);
    grDitherMode(GR_DITHER_DISABLE);

    std::vector<uint16_t> texBuf(32 * 32, 0x07E0u);  // 32x32 pure GREEN RGB565
    GrMipMapId_t mmid = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 32, 32, GR_TEXFMT_RGB_565,
        GR_MIPMAP_DISABLE, GR_LOD_32, GR_LOD_32, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_BILINEAR,
        GR_TEXTUREFILTER_BILINEAR, 0.0f, FXFALSE);
    REQUIRE(mmid != GR_NULL_MIPMAP_HANDLE);
    guTexDownloadMipMap(mmid, texBuf.data(), nullptr);
    guTexSource(mmid);

    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    guAlphaSource(GR_ALPHASOURCE_ITERATED_ALPHA);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);

    GrState st{};
    grGlideGetState(&st);
    grGlideSetState(&st);

    grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
    GrVertex a{}, b{}, c{};
    a.x = 200.0f;
    a.y = 140.0f;
    a.oow = 1.0f;
    a.a = 255.0f;
    a.tmuvtx[0].sow = 0.0f;
    a.tmuvtx[0].tow = 0.0f;
    b.x = 440.0f;
    b.y = 140.0f;
    b.oow = 1.0f;
    b.a = 255.0f;
    b.tmuvtx[0].sow = 255.0f;
    b.tmuvtx[0].tow = 0.0f;
    c.x = 320.0f;
    c.y = 340.0f;
    c.oow = 1.0f;
    c.a = 255.0f;
    c.tmuvtx[0].sow = 128.0f;
    c.tmuvtx[0].tow = 255.0f;
    grAADrawTriangle(&a, &b, &c, FXTRUE, FXTRUE, FXTRUE);

    grBufferSwap(1);
    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 8) & 0xFFu) >= 240);  // Green channel from AA triangle

    grSstWinClose();
  }

  SECTION("SDK parity (27): 5 consecutive grSstWinOpen / grSstWinClose cycles") {
    const GrColor_t cycleColors[5] = {
        0x000000FFu,  // RED
        0x0000FF00u,  // GREEN
        0x00FF0000u,  // BLUE
        0x0000FFFFu,  // YELLOW
        0x00FF00FFu,  // MAGENTA
    };
    for (int cycle = 0; cycle < 5; ++cycle) {
      REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2,
                           1) == FXTRUE);
      grDitherMode(GR_DITHER_DISABLE);
      grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
      grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
      grConstantColorValue(cycleColors[cycle]);

      GrVertex a{}, b{}, c{};
      a.x = 100.0f;
      a.y = 100.0f;
      a.oow = 1.0f;
      b.x = 540.0f;
      b.y = 100.0f;
      b.oow = 1.0f;
      c.x = 320.0f;
      c.y = 380.0f;
      c.oow = 1.0f;
      grDrawTriangle(&a, &b, &c);

      grBufferSwap(1);
      uint32_t px = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 200, 1, 1, 4, &px) ==
              FXTRUE);
      REQUIRE((px & 0x00FFFFFFu) == (cycleColors[cycle] & 0x00FFFFFFu));

      grSstWinClose();
    }
  }

  grGlideShutdown();
#endif
  }

}

