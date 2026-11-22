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

TEST_CASE("Glide 2.x: Points, Lines & Gouraud Shading", "[Glide2][Geometry]") {
  SECTION("Points and lines") {
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
  using DrawPointFunc = void (*)(const GrVertex*);
  using DrawLineFunc = void (*)(const GrVertex*, const GrVertex*);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
  auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
  auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
  auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
  auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
  auto constantColorValue =
      (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
  auto drawPoint = (DrawPointFunc)dlsym(handle, "grDrawPoint");
  auto drawLine = (DrawLineFunc)dlsym(handle, "grDrawLine");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constantColorValue != nullptr);
  REQUIRE(drawPoint != nullptr);
  REQUIRE(drawLine != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  glideInit();
  sstSelect(0);

  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Setup flat constant color shading
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

  SECTION("SDK parity (01): Flat Diagonal Points") {
    constantColorValue(0xFFFFFFFF);  // White
    bufferClear(0x00, 0, 0);         // Clear to Black

    // Draw points along the diagonal (from i=15 to i=95 to stay below console)
    for (int i = 15; i < 95; i++) {
      GrVertex vtx;
      vtx.x = (float)i * 6.4f;
      vtx.y = (float)i * 4.8f;
      vtx.z = 0.0f;
      vtx.ooz = 1.0f;
      vtx.oow = 1.0f;
      drawPoint(&vtx);
    }

    // Read the entire backbuffer into RAM once (640x480)
    std::vector<uint32_t> fb(640 * 480);
    FxBool readOk =
        readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
    REQUIRE(readOk == FXTRUE);

    // DIAGNOSTIC: Count total non-black pixels and scan first point
    // neighborhood
    int nonBlackCount = 0;
    int firstPx = (int)std::round(15.0f * 6.4f);
    int firstPy = (int)std::round(15.0f * 4.8f);

    for (int y = 0; y < 480; y++) {
      for (int x = 0; x < 640; x++) {
        uint32_t color = fb[y * 640 + x];
        if ((color & 0x00FFFFFF) != 0) {
          nonBlackCount++;
        }
      }
    }

    UNSCOPED_INFO("--- DIAGNOSTICS FOR test01 ---");
    UNSCOPED_INFO("Total non-black pixels in backbuffer: " << nonBlackCount);
    UNSCOPED_INFO("5x5 Neighborhood around first point (96, 72):");
    for (int row = 0; row < 5; row++) {
      int ny = firstPy - 2 + row;
      UNSCOPED_INFO("  y=" << ny << ": " << std::hex << " [x=" << (firstPx - 2)
                           << "]: 0x" << fb[ny * 640 + (firstPx - 2)] << " | 0x"
                           << fb[ny * 640 + (firstPx - 1)] << " | 0x"
                           << fb[ny * 640 + firstPx] << " | 0x"
                           << fb[ny * 640 + (firstPx + 1)] << " | 0x"
                           << fb[ny * 640 + (firstPx + 2)] << std::dec);
    }

    // Verify points along diagonal are White, and verify point size is exactly
    // 1x1 (neighbors are Black)
    for (int i = 15; i < 95; i++) {
      int px = (int)std::round((float)i * 6.4f);
      int py = (int)std::round((float)i * 4.8f);

      // Find the White pixel in the 3x3 neighborhood around (px, py)
      int whiteCount = 0;
      int foundX = -1;
      int foundY = -1;

      for (int ny = py - 1; ny <= py + 1; ny++) {
        if (ny < 0 || ny >= 480) continue;
        for (int nx = px - 1; nx <= px + 1; nx++) {
          if (nx < 0 || nx >= 640) continue;
          uint32_t color = fb[ny * 640 + nx];
          if ((color & 0x00FFFFFF) == 0x00FFFFFF) {
            whiteCount++;
            foundX = nx;
            foundY = ny;
          }
        }
      }

      INFO(
          "Failed at point index "
          << i << " (px=" << px << ", py=" << py
          << "). Expected exactly 1 white pixel in 3x3 neighborhood, but found "
          << whiteCount);
      REQUIRE(whiteCount == 1);

      // For the found white pixel, verify its own 8 immediate neighbors are
      // Black
      for (int ny = foundY - 1; ny <= foundY + 1; ny++) {
        if (ny < 0 || ny >= 480) continue;
        for (int nx = foundX - 1; nx <= foundX + 1; nx++) {
          if (nx < 0 || nx >= 640) continue;
          if (nx == foundX && ny == foundY) continue;
          uint32_t color = fb[ny * 640 + nx];
          REQUIRE((color & 0x00FFFFFF) == 0);
        }
      }
    }
  }

  SECTION("SDK parity (02): Flat Line Fan") {
    constantColorValue(0xFFFFFFFF);  // White
    bufferClear(0x00, 0, 0);         // Clear to Black

    // Draw line fan from top edge (i * 6.4, 0) to right edge (640, i * 4.8)
    for (int i = 0; i < 100; i++) {
      GrVertex vtxA, vtxB;
      vtxA.z = vtxB.z = 0.0f;
      vtxA.ooz = vtxB.ooz = 1.0f;
      vtxA.oow = vtxB.oow = 1.0f;

      vtxA.x = (float)i * 6.4f;
      vtxA.y = 0.0f;
      vtxB.x = 640.0f;
      vtxB.y = (float)i * 4.8f;

      drawLine(&vtxA, &vtxB);
    }

    // Read the entire backbuffer into RAM once (640x480)
    std::vector<uint32_t> fb(640 * 480);
    FxBool readOk =
        readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
    REQUIRE(readOk == FXTRUE);

    // DIAGNOSTIC: Count total non-black pixels in test02
    int nonBlackCount = 0;
    for (int y = 0; y < 480; y++) {
      for (int x = 0; x < 640; x++) {
        uint32_t color = fb[y * 640 + x];
        if ((color & 0x00FFFFFF) != 0) {
          nonBlackCount++;
        }
      }
    }
    UNSCOPED_INFO("--- DIAGNOSTICS FOR test02 ---");
    UNSCOPED_INFO(
        "Total non-black pixels in backbuffer for test02: " << nonBlackCount);

    // Verify that pixels inside the fan are White, and outside are Black
    // Since all lines start at the top edge and end at the right edge,
    // the top-right region is heavily covered, while the bottom-left region is
    // untouched.

    // (600, 100) is near the top-right corner and sits deep inside the line fan
    uint32_t colorInside = fb[100 * 640 + 600];
    INFO("Expected (600, 100) to be White, but got 0x"
         << std::hex << colorInside << std::dec);
    REQUIRE((colorInside & 0x00FFFFFF) == 0x00FFFFFF);

    // (100, 400) is in the bottom-left region and sits far outside the line fan
    uint32_t colorOutside = fb[400 * 640 + 100];
    REQUIRE((colorOutside & 0x00FFFFFF) == 0x00000000);
  }

  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("Gouraud color interpolation") {
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
  using DrawLineFunc = void (*)(const GrVertex*, const GrVertex*);
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
  auto drawLine = (DrawLineFunc)dlsym(handle, "grDrawLine");
  auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
  auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
  auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(drawLine != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  glideInit();
  sstSelect(0);

  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Setup Gouraud shading combiner (iterated local)
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

  SECTION("SDK parity (03): Gouraud Shaded Line") {
    bufferClear(0x00, 0, 0);  // Clear to Black

    GrVertex vtxA, vtxB;
    vtxA.z = vtxB.z = 0.0f;
    vtxA.ooz = vtxB.ooz = 1.0f;
    vtxA.oow = vtxB.oow = 1.0f;

    // Diagonal line from (0,0) to (640,480)
    vtxA.x = 0.0f;
    vtxA.y = 0.0f;
    vtxA.r = 255.0f;
    vtxA.g = 0.0f;
    vtxA.b = 0.0f;
    vtxA.a = 255.0f;  // Red

    vtxB.x = 640.0f;
    vtxB.y = 480.0f;
    vtxB.r = 0.0f;
    vtxB.g = 255.0f;
    vtxB.b = 0.0f;
    vtxB.a = 255.0f;  // Green

    drawLine(&vtxA, &vtxB);

    // Read the entire backbuffer into RAM once (640x480)
    std::vector<uint32_t> fb(640 * 480);
    FxBool readOk =
        readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
    REQUIRE(readOk == FXTRUE);

    // DIAGNOSTIC: Count total non-black pixels in test03
    int nonBlackCount = 0;
    for (int y = 0; y < 480; y++) {
      for (int x = 0; x < 640; x++) {
        uint32_t color = fb[y * 640 + x];
        if ((color & 0x00FFFFFF) != 0) {
          nonBlackCount++;
        }
      }
    }
    UNSCOPED_INFO("--- DIAGNOSTICS FOR test03 ---");
    UNSCOPED_INFO(
        "Total non-black pixels in backbuffer for test03: " << nonBlackCount);

    // Read and verify color gradient along the line using exact integer steps
    // 1. Near the start (80, 60) -> should be heavily Red
    uint32_t startPixel = fb[60 * 640 + 80];
    FxU8 r_start = startPixel & 0xFF;
    FxU8 g_start = (startPixel >> 8) & 0xFF;

    INFO("Failed at (80, 60) with color 0x"
         << std::hex << startPixel << std::dec << " (R=" << (int)r_start
         << ", G=" << (int)g_start << ")");
    REQUIRE(r_start > 200);
    REQUIRE(g_start < 50);

    // 2. Midpoint (320, 240) -> should be Yellow (balanced Red and Green)
    uint32_t midPixel = fb[240 * 640 + 320];
    FxU8 r_mid = midPixel & 0xFF;
    FxU8 g_mid = (midPixel >> 8) & 0xFF;

    INFO("Failed at midpoint (320, 240) with color 0x"
         << std::hex << midPixel << std::dec << " (R=" << (int)r_mid
         << ", G=" << (int)g_mid << ")");
    REQUIRE(r_mid > 100);
    REQUIRE(g_mid > 100);
    REQUIRE(abs((int)r_mid - (int)g_mid) < 25);  // roughly equal

    // 3. Near the end (560, 420) -> should be heavily Green
    uint32_t endPixel = fb[420 * 640 + 560];
    FxU8 r_end = endPixel & 0xFF;
    FxU8 g_end = (endPixel >> 8) & 0xFF;

    INFO("Failed at (560, 420) with color 0x"
         << std::hex << endPixel << std::dec << " (R=" << (int)r_end
         << ", G=" << (int)g_end << ")");
    REQUIRE(g_end > 200);
    REQUIRE(r_end < 50);
  }

  SECTION("SDK parity (04): Gouraud Shaded Triangle") {
    bufferClear(0x00, 0, 0);  // Clear to Black

    GrVertex vtxA, vtxB, vtxC;
    vtxA.z = vtxB.z = vtxC.z = 0.0f;
    vtxA.ooz = vtxB.ooz = vtxC.ooz = 1.0f;
    vtxA.oow = vtxB.oow = vtxC.oow = 1.0f;

    // Vertex A (Top-Center) - Red
    vtxA.x = 192.0f;
    vtxA.y = 144.0f;
    vtxA.r = 255.0f;
    vtxA.g = 0.0f;
    vtxA.b = 0.0f;
    vtxA.a = 255.0f;

    // Vertex B (Bottom-Right) - Green
    vtxB.x = 512.0f;
    vtxB.y = 192.0f;
    vtxB.r = 0.0f;
    vtxB.g = 255.0f;
    vtxB.b = 0.0f;
    vtxB.a = 255.0f;

    // Vertex C (Bottom-Left) - Blue
    vtxC.x = 320.0f;
    vtxC.y = 384.0f;
    vtxC.r = 0.0f;
    vtxC.g = 0.0f;
    vtxC.b = 255.0f;
    vtxC.a = 255.0f;

    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Read the entire backbuffer into RAM once (640x480)
    std::vector<uint32_t> fb(640 * 480);
    FxBool readOk =
        readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
    REQUIRE(readOk == FXTRUE);

    // 1. Verify center (340, 240) is neutral Gray
    uint32_t centerPixel = fb[240 * 640 + 340];
    FxU8 r_c = centerPixel & 0xFF;
    FxU8 g_c = (centerPixel >> 8) & 0xFF;
    FxU8 b_c = (centerPixel >> 16) & 0xFF;

    INFO("Failed at centroid (340, 240) with color 0x"
         << std::hex << centerPixel << std::dec << " (R=" << (int)r_c
         << ", G=" << (int)g_c << ", B=" << (int)b_c << ")");
    REQUIRE(r_c > 60);
    REQUIRE(r_c < 100);
    REQUIRE(abs((int)r_c - (int)g_c) < 10);
    REQUIRE(abs((int)g_c - (int)b_c) < 10);

    // 2. Verify near Green vertex (500, 195) is dominant Green
    uint32_t greenPixel = fb[195 * 640 + 500];
    FxU8 r_g = greenPixel & 0xFF;
    FxU8 g_g = (greenPixel >> 8) & 0xFF;
    FxU8 b_g = (greenPixel >> 16) & 0xFF;

    INFO("Failed at (500, 195) with color 0x"
         << std::hex << greenPixel << std::dec << " (R=" << (int)r_g
         << ", G=" << (int)g_g << ", B=" << (int)b_g << ")");
    REQUIRE(g_g > 200);
    REQUIRE(r_g < 50);
    REQUIRE(b_g < 50);

    // 3. Verify near Blue vertex (320, 375) is dominant Blue
    uint32_t bluePixel = fb[375 * 640 + 320];
    FxU8 r_b = bluePixel & 0xFF;
    FxU8 g_b = (bluePixel >> 8) & 0xFF;
    FxU8 b_b = (bluePixel >> 16) & 0xFF;

    INFO("Failed at (320, 375) with color 0x"
         << std::hex << bluePixel << std::dec << " (R=" << (int)r_b
         << ", G=" << (int)g_b << ", B=" << (int)b_b << ")");
    REQUIRE(b_b > 200);
    REQUIRE(r_b < 50);
    REQUIRE(g_b < 50);
  }

  shutdown();
  ::dlclose(handle);
#endif
  }

}

TEST_CASE("Glide 2.x: Framebuffer Controls (Depth, Clip, Scissor, Cull)", "[Glide2][Framebuffer]") {
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

  using DepthBufferModeFunc = void (*)(GrDepthBufferMode_t);
  using DepthBufferFunctionFunc = void (*)(GrCmpFnc_t);
  using DepthMaskFunc = void (*)(FxBool);
  using CullModeFunc = void (*)(GrCullMode_t);
  using SstOriginFunc = void (*)(GrOriginLocation_t);
  using DepthBiasLevelFunc = void (*)(FxI16);
  using ClipWindowFunc = void (*)(FxU32, FxU32, FxU32, FxU32);

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

  auto depthBufferMode =
      (DepthBufferModeFunc)dlsym(handle, "grDepthBufferMode");
  auto depthBufferFunction =
      (DepthBufferFunctionFunc)dlsym(handle, "grDepthBufferFunction");
  auto depthMask = (DepthMaskFunc)dlsym(handle, "grDepthMask");
  auto cullMode = (CullModeFunc)dlsym(handle, "grCullMode");
  auto sstOrigin = (SstOriginFunc)dlsym(handle, "grSstOrigin");
  auto depthBiasLevel = (DepthBiasLevelFunc)dlsym(handle, "grDepthBiasLevel");
  auto clipWindow = (ClipWindowFunc)dlsym(handle, "grClipWindow");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constantColorValue != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(depthBufferMode != nullptr);
  REQUIRE(depthBufferFunction != nullptr);
  REQUIRE(depthMask != nullptr);
  REQUIRE(cullMode != nullptr);
  REQUIRE(sstOrigin != nullptr);
  REQUIRE(depthBiasLevel != nullptr);
  REQUIRE(clipWindow != nullptr);

  glideInit();
  sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Common setup: flat constant color combiners
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

  SECTION("SDK parity (05): Z-buffer Depth Buffering") {
    depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    depthBufferFunction(GR_CMP_GREATER);
    depthMask(FXTRUE);

    bufferClear(0x00, 0, 0);  // Clear color to Black, depth to 0 (farthest ooz)

    GrVertex vtxA, vtxB, vtxC;

    // Triangle 1: Gray (0x00808080) at constant depth 10.0f (ooz = 6553.5)
    vtxA.x = 160.0f;
    vtxA.y = 100.8f;
    vtxA.ooz = 6553.5f;
    vtxA.oow = 1.0f;
    vtxB.x = 480.0f;
    vtxB.y = 100.8f;
    vtxB.ooz = 6553.5f;
    vtxB.oow = 1.0f;
    vtxC.x = 320.0f;
    vtxC.y = 379.2f;
    vtxC.ooz = 6553.5f;
    vtxC.oow = 1.0f;

    constantColorValue(0x00808080);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Triangle 2: Green (0x0000FF00) interpenetrating (depth 12.5f to 7.5f)
    vtxA.x = 550.4f;
    vtxA.y = 100.8f;
    vtxA.ooz = 5242.8f;
    vtxA.oow = 1.0f;
    vtxB.x = 550.4f;
    vtxB.y = 379.2f;
    vtxB.ooz = 5242.8f;
    vtxB.oow = 1.0f;
    vtxC.x = 89.6f;
    vtxC.y = 240.0f;
    vtxC.ooz = 8738.0f;
    vtxC.oow = 1.0f;

    constantColorValue(0x0000FF00);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Read back the framebuffer
    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Verify mathematical Z-buffer parity
    // 1. Pixel at x=320, y=240 (x < 377.6) -> must be Green (0x0000FF00)
    uint32_t p_close = fb[240 * 640 + 320];
    REQUIRE((p_close & 0x00FFFFFF) == 0x0000FF00);

    // 2. Pixel at x=390, y=240 (377.6 < x < 400) -> must be Gray (0x00808080)
    uint32_t p_far = fb[240 * 640 + 390];
    REQUIRE((p_far & 0x00FFFFFF) == 0x00808080);
  }

  SECTION("SDK parity (06): W-buffer Depth Buffering") {
    depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    depthBufferFunction(GR_CMP_LESS);
    depthMask(FXTRUE);

    bufferClear(0x00, 0, 0xFFFF);  // Clear depth to farthest (W_FARTHEST)

    GrVertex vtxA, vtxB, vtxC;

    // Triangle 1: Gray (0x00808080) at constant depth W = 10.0 (oow = 0.1)
    vtxA.x = 160.0f;
    vtxA.y = 100.8f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 0.1f;
    vtxB.x = 480.0f;
    vtxB.y = 100.8f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 0.1f;
    vtxC.x = 320.0f;
    vtxC.y = 379.2f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 0.1f;

    constantColorValue(0x00808080);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Triangle 2: Green (0x0000FF00) interpenetrating (depth W = 12.5 to 7.5)
    vtxA.x = 550.4f;
    vtxA.y = 100.8f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 0.08f;
    vtxB.x = 550.4f;
    vtxB.y = 379.2f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 0.08f;
    vtxC.x = 89.6f;
    vtxC.y = 240.0f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 0.1333333f;

    constantColorValue(0x0000FF00);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Read back
    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Verify W-buffer parity (should cross at exactly the same place x = 377.6)
    uint32_t p_close = fb[240 * 640 + 320];
    REQUIRE((p_close & 0x00FFFFFF) == 0x0000FF00);

    uint32_t p_far = fb[240 * 640 + 390];
    REQUIRE((p_far & 0x00FFFFFF) == 0x00808080);
  }

  SECTION("SDK parity (10): Polygon Backface Culling") {
    depthBufferMode(GR_DEPTHBUFFER_DISABLE);

    GrVertex vtxA, vtxB, vtxC;
    // Vertex A (Top-Center)
    vtxA.x = 320.0f;
    vtxA.y = 72.0f;
    vtxA.ooz = 1.0f;
    vtxA.oow = 1.0f;
    // Vertex B (Bottom-Right)
    vtxB.x = 512.0f;
    vtxB.y = 408.0f;
    vtxB.ooz = 1.0f;
    vtxB.oow = 1.0f;
    // Vertex C (Bottom-Left)
    vtxC.x = 128.0f;
    vtxC.y = 408.0f;
    vtxC.ooz = 1.0f;
    vtxC.oow = 1.0f;

    // Sub-test 1: Cull Positive (CW) -> Only CCW (Red) is drawn
    cullMode(GR_CULL_POSITIVE);
    bufferClear(0x00, 0, 0);

    // Draw CW in Blue
    constantColorValue(0x000000FF);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Draw CCW in Red (Lower Left Origin)
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    constantColorValue(0x00FF0000);
    drawTriangle(&vtxA, &vtxC, &vtxB);
    sstOrigin(GR_ORIGIN_UPPER_LEFT);  // Restore

    // Read back
    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Under Cull Positive (CW culled, only CCW/Red is drawn):
    // (200, 130) is inside Red triangle, outside Blue -> must be Red
    uint32_t p_red_side = fb[130 * 640 + 200];
    REQUIRE((p_red_side & 0x00FFFFFF) == 0x00FF0000);

    // (200, 350) is inside Blue triangle, outside Red -> must be Black (since
    // Blue is culled)
    uint32_t p_blue_side = fb[350 * 640 + 200];
    REQUIRE((p_blue_side & 0x00FFFFFF) == 0x00000000);

    // Sub-test 2: Cull Negative (CCW) -> Only CW (Blue) is drawn
    cullMode(GR_CULL_NEGATIVE);
    bufferClear(0x00, 0, 0);

    // Draw CW in Blue
    constantColorValue(0x000000FF);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Draw CCW in Red (Lower Left Origin)
    sstOrigin(GR_ORIGIN_LOWER_LEFT);
    constantColorValue(0x00FF0000);
    drawTriangle(&vtxA, &vtxC, &vtxB);
    sstOrigin(GR_ORIGIN_UPPER_LEFT);  // Restore

    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Under Cull Negative (CCW culled, only CW/Blue is drawn):
    // (200, 130) is inside Red triangle, outside Blue -> must be Black (since
    // Red is culled)
    p_red_side = fb[130 * 640 + 200];
    REQUIRE((p_red_side & 0x00FFFFFF) == 0x00000000);

    // (200, 350) is inside Blue triangle, outside Red -> must be Blue
    p_blue_side = fb[350 * 640 + 200];
    REQUIRE((p_blue_side & 0x00FFFFFF) == 0x000000FF);

    cullMode(GR_CULL_DISABLE);  // Restore
  }

  SECTION("SDK parity (14): Dynamic Depth Bias") {
    depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    depthBufferFunction(GR_CMP_GREATER);
    depthMask(FXTRUE);

    GrVertex vtxA, vtxB, vtxC;

    // Sub-test 1: Positive Bias (+300) -> Gray triangle is biased closer
    bufferClear(0x00, 0, 0);

    // 1. Draw GRAY triangle (biased)
    vtxA.x = 160.0f;
    vtxA.y = 100.8f;
    vtxA.ooz = 6553.5f;
    vtxA.oow = 1.0f;
    vtxB.x = 480.0f;
    vtxB.y = 100.8f;
    vtxB.ooz = 6553.5f;
    vtxB.oow = 1.0f;
    vtxC.x = 320.0f;
    vtxC.y = 379.2f;
    vtxC.ooz = 6553.5f;
    vtxC.oow = 1.0f;

    constantColorValue(0x00808080);
    depthBiasLevel(300);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // 2. Draw GREEN triangle (static zero bias)
    vtxA.x = 550.4f;
    vtxA.y = 100.8f;
    vtxA.ooz = 5242.8f;
    vtxA.oow = 1.0f;
    vtxB.x = 550.4f;
    vtxB.y = 379.2f;
    vtxB.ooz = 5242.8f;
    vtxB.oow = 1.0f;
    vtxC.x = 89.6f;
    vtxC.y = 240.0f;
    vtxC.ooz = 8738.0f;
    vtxC.oow = 1.0f;

    constantColorValue(0x0000FF00);
    depthBiasLevel(0);
    drawTriangle(&vtxA, &vtxB, &vtxC);

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Under +300 bias, Gray triangle's ooz (6553.5) becomes effectively 6853.5.
    // At x=360, Green's ooz is 6687.1. Since 6853.5 > 6687.1, Gray now
    // OVERWRITES Green!
    uint32_t p_biased = fb[240 * 640 + 360];
    REQUIRE((p_biased & 0x00FFFFFF) == 0x00808080);

    // Sub-test 2: Negative Bias (-300) -> Gray triangle is biased further
    bufferClear(0x00, 0, 0);

    // Draw GRAY with -300 bias
    constantColorValue(0x00808080);
    depthBiasLevel(-300);
    vtxA.x = 160.0f;
    vtxA.y = 100.8f;
    vtxA.ooz = 6553.5f;
    vtxA.oow = 1.0f;
    vtxB.x = 480.0f;
    vtxB.y = 100.8f;
    vtxB.ooz = 6553.5f;
    vtxB.oow = 1.0f;
    vtxC.x = 320.0f;
    vtxC.y = 379.2f;
    vtxC.ooz = 6553.5f;
    vtxC.oow = 1.0f;
    drawTriangle(&vtxA, &vtxB, &vtxC);

    // Draw GREEN with 0 bias
    constantColorValue(0x0000FF00);
    depthBiasLevel(0);
    vtxA.x = 550.4f;
    vtxA.y = 100.8f;
    vtxA.ooz = 5242.8f;
    vtxA.oow = 1.0f;
    vtxB.x = 550.4f;
    vtxB.y = 379.2f;
    vtxB.ooz = 5242.8f;
    vtxB.oow = 1.0f;
    vtxC.x = 89.6f;
    vtxC.y = 240.0f;
    vtxC.ooz = 8738.0f;
    vtxC.oow = 1.0f;
    drawTriangle(&vtxA, &vtxB, &vtxC);

    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Under -300 bias, Gray's ooz (6553.5) becomes effectively 6253.5.
    // At x=390, Green's ooz is 6459.1. Since 6253.5 < 6459.1, Gray is REJECTED
    // (Green wins)!
    uint32_t p_unbiased = fb[240 * 640 + 390];
    REQUIRE((p_unbiased & 0x00FFFFFF) == 0x0000FF00);

    depthBiasLevel(0);  // Restore
  }

  SECTION("SDK parity (15): Screen Clipping") {
    depthBufferMode(GR_DEPTHBUFFER_DISABLE);

    // Clear full screen before narrowing clip window (matching official test15)
    clipWindow(0, 0, 640, 480);
    bufferClear(0x00, 0, 0);

    // Mode 0: Centered large clip window: x=[128, 512], y=[96, 384]
    clipWindow(128, 96, 512, 384);

    // Draw a grid of gray triangles covering the entire screen
    GrVertex vtxA, vtxB, vtxC;
    for (int y = 0; y < 10; y++) {
      for (int x = 0; x < 10; x++) {
        vtxA.x = vtxB.x = 64.0f * (float)x;
        vtxA.y = 48.0f * (float)y;
        vtxB.y = vtxC.y = 48.0f * ((float)y + 1.0f);
        vtxC.x = 64.0f * ((float)x + 1.0f);

        vtxA.ooz = vtxB.ooz = vtxC.ooz = 1.0f;
        vtxA.oow = vtxB.oow = vtxC.oow = 1.0f;
        vtxA.r = vtxB.r = vtxC.r = 128.0f;
        vtxA.g = vtxB.g = vtxC.g = 128.0f;
        vtxA.b = vtxB.b = vtxC.b = 128.0f;
        vtxA.a = 255.0f;
        vtxB.a = 255.0f;
        vtxC.a = 255.0f;

        constantColorValue(0x00808080);
        drawTriangle(&vtxA, &vtxB, &vtxC);
      }
    }

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Verify clipping:
    // Inside (200, 200) -> must be Gray
    uint32_t p_in = fb[200 * 640 + 200];
    REQUIRE((p_in & 0x00FFFFFF) == 0x00808080);

    // Outside (50, 50) -> must be Black
    uint32_t p_out = fb[50 * 640 + 50];
    REQUIRE((p_out & 0x00FFFFFF) == 0x00000000);

    // Restore clip window
    clipWindow(0, 0, 640, 480);
  }

  shutdown();
  ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x: Coordinate Unsnapping & 12.4 Fixed-Point Wrapping", "[Glide2][Geometry][Unsnap]") {
  SECTION("IEEE-754 snap bias unsnapping") {
#ifdef __linux__
    // 1. Load Glide 2 shared library
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using DrawLineFunc = void(*)(const void*, const void*);
    using ReadLfbFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using WinCloseFunc = FxBool(*)(void);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto drawLine = reinterpret_cast<DrawLineFunc>(::dlsym(handle, "grDrawLine"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(drawLine != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(shutdown != nullptr);

    // 3. Initialize Glide 2 and open a headless window (Software backend for speed & sanity)
    ::setenv("GLIDE_NG_BACKEND", "SOFTWARE", 1);
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    glideInit();
    FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ok == FXTRUE);

    // Configure Glide to draw using vertex colors (iterated)
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    // Clear buffer to black
    bufferClear(0, 0, 0);

    // 4. Draw a biased white line using SNAP_BIAS
    // SNAP_BIAS = (3 << 18) = 786432.0f
    const float SNAP_BIAS = 786432.0f;
    
    struct GrVertex2 {
        float x, y, z;
        float r, g, b;
        float ooz;
        float a;
        float oow;
        float sow, tow, oow_tmu;
    };

    GrVertex2 v1{};
    v1.x = SNAP_BIAS + 100.0f;
    v1.y = SNAP_BIAS + 100.0f;
    v1.z = 0.0f;
    v1.r = v1.g = v1.b = 255.0f;
    v1.a = 255.0f;
    v1.oow = 1.0f;

    GrVertex2 v2{};
    v2.x = SNAP_BIAS + 200.0f;
    v2.y = SNAP_BIAS + 100.0f; // Horizontal line from (100, 100) to (200, 100)
    v2.z = 0.0f;
    v2.r = v2.g = v2.b = 255.0f;
    v2.a = 255.0f;
    v2.oow = 1.0f;

    // Issue the draw call!
    drawLine(&v1, &v2);

    // Read back a pixel on the line (e.g., at X=150, Y=100)
    uint32_t pixel = 0;
    FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 150, 100, 1, 1, 4, &pixel);
    REQUIRE(readOk == FXTRUE);

    // Unpack color (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixel & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = (pixel >> 16) & 0xFF;
    
    INFO("Readback pixel at (150, 100): R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // The pixel color must be white (R, G, B > 240) if coordinate unsnapping succeeded.
    // If unsnapping failed, the line would be clipped off-screen and the pixel would be black.
    CHECK(r > 240);
    CHECK(g > 240);
    CHECK(b > 240);

    winClose();
    shutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
#endif
  }

  SECTION("12.4 fixed-point wrap, guTexChangeAttributes & DELTA0 combine") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                  GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t,
                                     GrCombineLocal_t, GrCombineOther_t, FxBool);
    using GuColorCombineFunc = void(*)(GrColorCombineFnc_t);
    using ConstColorFunc = void(*)(GrColor_t);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using TexAllocFunc = FxI32(*)(int, FxU8, int, int, FxU32, FxI32, FxI32, FxI32,
                                  FxI32, FxI32, FxI32, FxI32, FxI32, float, FxBool);
    using TexChangeFunc = FxBool(*)(FxI32, int, int, FxU32, FxI32, FxI32, FxI32,
                                    FxI32, FxI32, FxI32, FxI32, FxI32);
    using TexInfoFunc = void*(*)(FxI32);
    using WinCloseFunc = FxBool(*)(void);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto guColorCombine = reinterpret_cast<GuColorCombineFunc>(::dlsym(handle, "guColorCombineFunction"));
    auto constColor = reinterpret_cast<ConstColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto texAlloc = reinterpret_cast<TexAllocFunc>(::dlsym(handle, "guTexAllocateMemory"));
    auto texChange = reinterpret_cast<TexChangeFunc>(::dlsym(handle, "guTexChangeAttributes"));
    auto texInfo = reinterpret_cast<TexInfoFunc>(::dlsym(handle, "guTexGetMipMapInfo"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    REQUIRE((glideInit != nullptr && winOpen != nullptr &&
             bufferClear != nullptr && colorCombine != nullptr &&
             guColorCombine != nullptr && constColor != nullptr &&
             drawTri != nullptr && readLfb != nullptr &&
             texAlloc != nullptr && texChange != nullptr &&
             texInfo != nullptr && winClose != nullptr &&
             shutdown != nullptr));

    ::setenv("GLIDE_NG_BACKEND", "SOFTWARE", 1);
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    glideInit();
    REQUIRE(winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                    GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);

    struct GrVertex2 {
        float x, y, z, r, g, b, ooz, a, oow, sow, tow, oow_tmu;
    };

    // 1. Coherent signed 12.4 fixed-point wrap: triangle shifted by (-5*4096, +5*4096)
    //    = (-20480, +20480) wraps cleanly back into screen coords [100..200, 100..200].
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    bufferClear(0, 0, 0);
    GrVertex2 a{}, b{}, c{};
    a.x = -20480.0f + 100.0f; a.y = 20480.0f + 100.0f; a.oow = 1.0f; a.r = 255.0f; a.a = 255.0f;
    b.x = -20480.0f + 200.0f; b.y = 20480.0f + 100.0f; b.oow = 1.0f; b.r = 255.0f; b.a = 255.0f;
    c.x = -20480.0f + 100.0f; c.y = 20480.0f + 200.0f; c.oow = 1.0f; c.r = 255.0f; c.a = 255.0f;
    drawTri(&a, &b, &c);
    uint32_t px = 0;
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 120, 120, 1, 1, 4, &px) == FXTRUE);
    CHECK((px & 0xFFu) > 240u);

    // 2. Wide unclipped frustum triangle crossing X=-2500..+320 must NOT wrap or invert.
    bufferClear(0, 0, 0);
    GrVertex2 w0{}, w1{}, w2{};
    w0.x = -2500.0f; w0.y = 100.0f; w0.oow = 1.0f; w0.g = 255.0f; w0.a = 255.0f;
    w1.x = 320.0f;   w1.y = 100.0f; w1.oow = 1.0f; w1.g = 255.0f; w1.a = 255.0f;
    w2.x = 320.0f;   w2.y = 300.0f; w2.oow = 1.0f; w2.g = 255.0f; w2.a = 255.0f;
    drawTri(&w0, &w1, &w2);
    uint32_t pxInside = 0, pxRight = 0;
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 280, 140, 1, 1, 4, &pxInside) == FXTRUE);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 400, 140, 1, 1, 4, &pxRight) == FXTRUE);
    CHECK(((pxInside >> 8) & 0xFFu) > 240u);
    CHECK(((pxRight >> 8) & 0xFFu) == 0u);

    // 3. guColorCombineFunction(GR_COLORCOMBINE_ITRGB_DELTA0) matches authentic
    //    3dfx cc_delta0mode: uses grConstantColorValue (ignoring vertex RGB).
    bufferClear(0, 0, 0);
    constColor(0xFF0000FFu);  // ABGR/RGB blue: 0xFF0000FF -> readback checks R/B
    guColorCombine(GR_COLORCOMBINE_ITRGB_DELTA0);
    GrVertex2 d0{}, d1{}, d2{};
    d0.x = 10.0f; d0.y = 10.0f; d0.oow = 1.0f; d0.r = 255.0f; d0.g = 255.0f; d0.a = 255.0f;
    d1.x = 90.0f; d1.y = 10.0f; d1.oow = 1.0f; d1.r = 255.0f; d1.g = 255.0f; d1.a = 255.0f;
    d2.x = 10.0f; d2.y = 90.0f; d2.oow = 1.0f; d2.r = 255.0f; d2.g = 255.0f; d2.a = 255.0f;
    drawTri(&d0, &d1, &d2);
    uint32_t pxConst = 0;
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 30, 30, 1, 1, 4, &pxConst) == FXTRUE);
    CHECK(((pxConst >> 8) & 0xFFu) == 0u);  // Green from vertex must be ignored
    const bool hasConstChannel =
        ((pxConst & 0xFFu) > 240u) || (((pxConst >> 16) & 0xFFu) > 240u);
    CHECK(hasConstChannel);

    // 4. guTexChangeAttributes(-1) sentinel must preserve existing attributes.
    const FxI32 mmid = texAlloc(0, 0x3, 256, 256, 0xB /*ARGB_1555*/, 1, 0, 0, 3,
                                0, 0, 0, 0, 0.0f, FXFALSE);
    REQUIRE(mmid >= 0);
    REQUIRE(texChange(mmid, -1, -1, static_cast<FxU32>(-1), -1, -1, -1, -1,
                      -1, -1, 1 /*BILINEAR*/, 1 /*BILINEAR*/) == FXTRUE);
    struct MinimalMipInfo {
        int sst; FxBool valid; int width; int height;
        int aspect_ratio; void* data; FxU32 format; int mipmap_mode;
        int magfilter_mode; int minfilter_mode; int s_clamp_mode; int t_clamp_mode;
    };
    auto* info = static_cast<MinimalMipInfo*>(texInfo(mmid));
    REQUIRE(info != nullptr);
    CHECK(info->width == 256);
    CHECK(info->height == 256);
    CHECK(info->format == 0xBu);
    CHECK(info->aspect_ratio == 3);
    CHECK(info->mipmap_mode == 1);
    CHECK(info->minfilter_mode == 1);
    CHECK(info->magfilter_mode == 1);

    winClose();
    shutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
#endif
  }

}

