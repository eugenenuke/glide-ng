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

TEST_CASE("Glide 2.x: Texture & Combiner Pipeline", "[Glide2][Texture]") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  // Resolve Glide APIs
  auto glideInit = (void (*)(void))dlsym(handle, "grGlideInit");
  auto sstSelect = (void (*)(int))dlsym(handle, "grSstSelect");
  auto winOpen = (FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                             GrColorFormat_t, GrOriginLocation_t, int,
                             int))dlsym(handle, "grSstWinOpen");
  auto bufferClear =
      (void (*)(GrColor_t, GrAlpha_t, FxU16))dlsym(handle, "grBufferClear");
  auto readLfb = (FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                             void*))dlsym(handle, "grLfbReadRegion");
  auto shutdown = (void (*)(void))dlsym(handle, "grGlideShutdown");

  auto colorCombine =
      (void (*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
                GrCombineOther_t, FxBool))dlsym(handle, "grColorCombine");
  auto alphaCombine =
      (void (*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
                GrCombineOther_t, FxBool))dlsym(handle, "grAlphaCombine");
  auto constantColorValue =
      (void (*)(GrColor_t))dlsym(handle, "grConstantColorValue");
  auto texDownload = (void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*))dlsym(
      handle, "grTexDownloadMipMap");
  auto texDownloadTable = (void (*)(GrChipID_t, GrTexTable_t, void*))dlsym(
      handle, "grTexDownloadTable");
  auto texSource = (void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*))dlsym(
      handle, "grTexSource");
  auto texClamp =
      (void (*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t))dlsym(
          handle, "grTexClampMode");
  auto texFilter =
      (void (*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t))dlsym(
          handle, "grTexFilterMode");
  auto drawTriangle = (void (*)(const void*, const void*, const void*))dlsym(
      handle, "grDrawTriangle");
  auto texCombine = (void (*)(
      GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
      GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(alphaCombine != nullptr);
  REQUIRE(constantColorValue != nullptr);
  REQUIRE(texDownload != nullptr);
  REQUIRE(texDownloadTable != nullptr);
  REQUIRE(texSource != nullptr);
  REQUIRE(texClamp != nullptr);
  REQUIRE(texFilter != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(texCombine != nullptr);

  // Initialize
  glideInit();
  sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  SECTION("SDK parity (17): Multi-mode Register Combiners") {
    std::vector<uint16_t> texPixels(256u * 256u, 0xFFFFu);
    GrTexInfo texInfo{};
    texInfo.smallLod = GR_LOD_256;
    texInfo.largeLod = GR_LOD_256;
    texInfo.aspectRatio = GR_ASPECT_1x1;
    texInfo.format = GR_TEXFMT_RGB_565;
    texInfo.data = texPixels.data();

    // Download to TMU0
    texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);

    // Bind TMU0
    texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texClamp(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
              GR_TEXTUREFILTER_POINT_SAMPLED);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_NONE, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);

    // Set green constant color (FLATLIT mode uses it)
    constantColorValue(0x00ff0000);

    // Centered quad corners: A(128, 96), B(512, 96), C(128, 384), D(512, 384)
    GrVertex vtxA, vtxB, vtxC, vtxD;
    vtxA.oow = 1.0f;
    vtxB = vtxC = vtxD = vtxA;

    vtxA.x = vtxC.x = 128.0f;
    vtxB.x = vtxD.x = 512.0f;
    vtxA.y = vtxB.y = 96.0f;
    vtxC.y = vtxD.y = 384.0f;

    vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
    vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
    vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
    vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

    // Iterated vertex colors
    vtxA.r = 255.0f;
    vtxA.g = 0.0f;
    vtxA.b = 0.0f;
    vtxA.a = 255.0f;
    vtxB.r = 0.0f;
    vtxB.g = 255.0f;
    vtxB.b = 0.0f;
    vtxB.a = 128.0f;
    vtxC.r = 0.0f;
    vtxC.g = 0.0f;
    vtxC.b = 255.0f;
    vtxC.a = 128.0f;
    vtxD.r = 0.0f;
    vtxD.g = 0.0f;
    vtxD.b = 0.0f;
    vtxD.a = 0.0f;

    std::vector<uint32_t> fb(640 * 480);

    // Sub-section: Mode 0 (DECAL)
    {
      bufferClear(0, 0, 0);
      colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
      drawTriangle(&vtxA, &vtxD, &vtxC);
      drawTriangle(&vtxA, &vtxB, &vtxD);

      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);

      uint32_t p_decal = fb[240 * 640 + 320];
      uint8_t r = p_decal & 0xFF;
      uint8_t g = (p_decal >> 8) & 0xFF;
      uint8_t b = (p_decal >> 16) & 0xFF;
      INFO("Decal Mode center pixel (320, 240): color = 0x" << std::hex
                                                            << p_decal);
      REQUIRE((r > 0 || g > 0 || b > 0));
    }

    // Sub-section: Mode 1 (FLATLIT - modulated by constant color Red)
    {
      bufferClear(0, 0, 0);
      colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE,
                   FXFALSE);
      drawTriangle(&vtxA, &vtxD, &vtxC);
      drawTriangle(&vtxA, &vtxB, &vtxD);

      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);

      uint32_t p_flat = fb[240 * 640 + 320];
      uint8_t r = p_flat & 0xFF;
      uint8_t g = (p_flat >> 8) & 0xFF;
      uint8_t b = (p_flat >> 16) & 0xFF;
      INFO("Flatlit Mode center pixel (320, 240): color = 0x"
           << std::hex << p_flat << " (r=" << std::dec << (int)r
           << ", g=" << (int)g << ", b=" << (int)b << ")");
      // Since constant color is 0x00ff0000 (Blue in client's ABGR layout),
      // the output is modulated by Blue: Red and Green must be near 0.
      REQUIRE(r <= 4);
      REQUIRE(g <= 4);
      REQUIRE(b > 0);
    }

    // Sub-section: Mode 2 (RGBLIT - modulated by iterated color)
    {
      bufferClear(0, 0, 0);
      colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE,
                   FXFALSE);
      drawTriangle(&vtxA, &vtxD, &vtxC);
      drawTriangle(&vtxA, &vtxB, &vtxD);

      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);

      uint32_t p_cornerA = fb[100 * 640 + 132];
      uint32_t p_cornerD = fb[380 * 640 + 508];

      REQUIRE((p_cornerA & 0xFFFFFF) != 0);
      // (508, 380) is 4px inside corner D=(512,384) where RGB=(0,0,0), so
      // interpolated Gouraud RGB modulated against white is near-black (<= 8).
      REQUIRE((p_cornerD & 0xFF) <= 8);
      REQUIRE(((p_cornerD >> 8) & 0xFF) <= 8);
      REQUIRE(((p_cornerD >> 16) & 0xFF) <= 8);
    }
  }

  SECTION("SDK parity (18): Texture Alpha Blending") {
    tlSetScreen(640.0f, 480.0f);
    using AlphaBlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                    GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    auto alphaBlendFunction =
        reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));
    auto texMipMapMode = (void (*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(
        handle, "grTexMipMapMode");
    REQUIRE(alphaBlendFunction != nullptr);
    REQUIRE(texMipMapMode != nullptr);

    // Self-contained ARGB4444 texture: solid green with alpha ramping 0 -> 255
    // left-to-right across the quad to verify transparent, 50% blend, and opaque.
    std::vector<uint16_t> alphaPixels(256u * 256u);
    for (int y = 0; y < 256; ++y) {
      for (int x = 0; x < 256; ++x) {
        alphaPixels[static_cast<size_t>(y) * 256u + static_cast<size_t>(x)] =
            GlideTestHelpers::PackArgb4444(static_cast<uint8_t>(x), 0, 255, 0);
      }
    }
    GrTexInfo texInfo{};
    texInfo.smallLod = GR_LOD_256;
    texInfo.largeLod = GR_LOD_256;
    texInfo.aspectRatio = GR_ASPECT_1x1;
    texInfo.format = GR_TEXFMT_ARGB_4444;
    texInfo.data = alphaPixels.data();

    // Download and bind TMU0
    texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    texMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);

    bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

    // --- Stage 1: Draw 10x10 grid of background squares ---
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                       GR_BLEND_ZERO);

#define RED 0x000000ff
#define BLUE 0x00ff0000

    for (int y = 0; y < 10; y++) {
      for (int x = 0; x < 10; x++) {
        GrVertex vtxA{}, vtxB{}, vtxC{}, vtxD{};
        vtxA.oow = vtxB.oow = vtxC.oow = vtxD.oow = 1.0f;

        vtxA.x = vtxB.x = tlScaleX(((float)x) / 10.0f);
        vtxA.y = vtxD.y = tlScaleY(((float)y) / 10.0f);
        vtxB.y = vtxC.y = tlScaleY((((float)y) / 10.0f) + 0.1f);
        vtxC.x = vtxD.x = tlScaleX((((float)x) / 10.0f) + 0.1f);

        constantColorValue(RED);
        drawTriangle(&vtxA, &vtxB, &vtxC);
        constantColorValue(BLUE);
        drawTriangle(&vtxA, &vtxC, &vtxD);
      }
    }

    // --- Stage 2: Draw the large central transparent textured quad ---
    GrVertex vtxA, vtxB, vtxC, vtxD;
    vtxA.oow = 1.0f;
    vtxB = vtxC = vtxD = vtxA;

    vtxA.x = vtxC.x = tlScaleX(0.2f);
    vtxB.x = vtxD.x = tlScaleX(0.8f);
    vtxA.y = vtxB.y = tlScaleY(0.2f);
    vtxC.y = vtxD.y = tlScaleY(0.8f);

    vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
    vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
    vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
    vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ZERO, GR_BLEND_ZERO);

    drawTriangle(&vtxA, &vtxD, &vtxC);
    drawTriangle(&vtxA, &vtxB, &vtxD);

    std::vector<uint32_t> fb(640 * 480);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
            FXTRUE);

    // Outside quad: unchanged pure Blue background tile
    REQUIRE((fb[50 * 640 + 50] & 0xFFFFFF) == 0x00FF0000);
    REQUIRE((fb[400 * 640 + 50] & 0xFFFFFF) == 0x00FF0000);

    // Left edge inside quad (u ~ 0.03, alpha ~ 0): background shows through, G ~ 0
    {
      const uint32_t px = fb[240 * 640 + 140];
      const int g = static_cast<int>((px >> 8) & 0xFFu);
      const int r = static_cast<int>(px & 0xFFu);
      const int b = static_cast<int>((px >> 16) & 0xFFu);
      REQUIRE(g <= 25);
      REQUIRE((r >= 220 || b >= 220));
    }
    // Center of quad (u = 0.5, alpha ~ 128): ~50% green blended over red/blue bg
    {
      const uint32_t px = fb[240 * 640 + 320];
      const int g = static_cast<int>((px >> 8) & 0xFFu);
      const int r = static_cast<int>(px & 0xFFu);
      const int b = static_cast<int>((px >> 16) & 0xFFu);
      REQUIRE(g >= 100);
      REQUIRE(g <= 165);
      REQUIRE((r >= 90 || b >= 90));
    }
    // Right edge inside quad (u ~ 0.97, alpha ~ 255): nearly opaque green
    {
      const uint32_t px = fb[240 * 640 + 500];
      const int g = static_cast<int>((px >> 8) & 0xFFu);
      const int r = static_cast<int>(px & 0xFFu);
      const int b = static_cast<int>((px >> 16) & 0xFFu);
      REQUIRE(g >= 230);
      REQUIRE(r <= 25);
      REQUIRE(b <= 25);
    }
  }

  SECTION("SDK parity (19): Texture Filtering") {
    tlSetScreen(640.0f, 480.0f);
    auto texMipMapMode = (void (*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(
        handle, "grTexMipMapMode");
    auto texCombine = (void (*)(
        GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
        GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
    REQUIRE(texMipMapMode != nullptr);
    REQUIRE(texCombine != nullptr);

    // 256x256 RGB565 alternating 1-texel Red/Blue vertical stripes:
    // POINT_SAMPLED always yields pure Red or pure Blue (0 purple pixels),
    // while BILINEAR blends adjacent texels into purple across the quad.
    std::vector<uint16_t> stripePixels(256u * 256u);
    for (int y = 0; y < 256; ++y) {
      for (int x = 0; x < 256; ++x) {
        stripePixels[static_cast<size_t>(y) * 256u + static_cast<size_t>(x)] =
            (x & 1) ? 0xF800u : 0x001Fu;
      }
    }
    GrTexInfo texInfo{};
    texInfo.smallLod = GR_LOD_256;
    texInfo.largeLod = GR_LOD_256;
    texInfo.aspectRatio = GR_ASPECT_1x1;
    texInfo.format = GR_TEXFMT_RGB_565;
    texInfo.data = stripePixels.data();

    texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);

    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    texMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

    std::vector<uint32_t> fb(640 * 480);
    auto renderFilterQuad = [&](float minN, float maxN,
                                GrTextureFilterMode_t mode) {
      texFilter(GR_TMU0, mode, mode);
      bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

      GrVertex vtxA{}, vtxB{}, vtxC{}, vtxD{};
      vtxA.oow = 1.0f;
      vtxB = vtxC = vtxD = vtxA;
      vtxA.x = vtxC.x = tlScaleX(minN);
      vtxB.x = vtxD.x = tlScaleX(maxN);
      vtxA.y = vtxB.y = tlScaleY(minN);
      vtxC.y = vtxD.y = tlScaleY(maxN);
      vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
      vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
      vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
      vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

      drawTriangle(&vtxA, &vtxD, &vtxC);
      drawTriangle(&vtxA, &vtxB, &vtxD);
      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);
    };

    auto countBlendedPixels = [&](int x0, int y0, int x1, int y1) {
      int blended = 0;
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          const uint32_t px = fb[static_cast<size_t>(y) * 640u + static_cast<size_t>(x)];
          const int r = static_cast<int>(px & 0xFFu);
          const int b = static_cast<int>((px >> 16) & 0xFFu);
          if (r > 35 && b > 35) ++blended;
        }
      }
      return blended;
    };

    // Minify (64x48 quad): Point vs Bilinear
    renderFilterQuad(0.45f, 0.55f, GR_TEXTUREFILTER_POINT_SAMPLED);
    REQUIRE(countBlendedPixels(295, 222, 345, 258) == 0);
    REQUIRE((fb[100 * 640 + 100] & 0xFFFFFF) == 0);

    renderFilterQuad(0.45f, 0.55f, GR_TEXTUREFILTER_BILINEAR);
    REQUIRE(countBlendedPixels(295, 222, 345, 258) > 100);
    REQUIRE((fb[100 * 640 + 100] & 0xFFFFFF) == 0);

    // Magnify (640x480 quad): Point vs Bilinear
    renderFilterQuad(0.0f, 1.0f, GR_TEXTUREFILTER_POINT_SAMPLED);
    REQUIRE(countBlendedPixels(100, 100, 540, 380) == 0);

    renderFilterQuad(0.0f, 1.0f, GR_TEXTUREFILTER_BILINEAR);
    REQUIRE(countBlendedPixels(100, 100, 540, 380) > 10000);
  }

  SECTION("SDK parity (21): Texture Compositing / Multi-pass") {
    tlSetScreen(640.0f, 480.0f);
    auto texMipMapMode = (void (*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(
        handle, "grTexMipMapMode");
    auto texCombine = (void (*)(
        GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
        GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
    auto texDetailControl = (void (*)(GrChipID_t, int, int, float))dlsym(
        handle, "grTexDetailControl");
    using AlphaBlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                    GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    auto alphaBlendFunction =
        reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));

    REQUIRE(texMipMapMode != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(texDetailControl != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);

    // Self-contained RGB565 textures for lightmap, specular & detail compositing:
    //   base   = solid Blue  (R=0,   G=0,   B=255)
    //   light  = warm light  (R=200, G=160, B=224)
    //   detail = Orange-Red  (R=240, G=64,  B=0)
    std::vector<uint16_t> basePixels(256u * 256u, 0x001Fu);
    std::vector<uint16_t> lightPixels(
        256u * 256u, GlideTestHelpers::PackRgb565(200, 160, 224));
    std::vector<uint16_t> detailPixels(
        256u * 256u, GlideTestHelpers::PackRgb565(240, 64, 0));

    auto makeTexInfo = [](std::vector<uint16_t>& buf) {
      GrTexInfo ti{};
      ti.smallLod = GR_LOD_256;
      ti.largeLod = GR_LOD_256;
      ti.aspectRatio = GR_ASPECT_1x1;
      ti.format = GR_TEXFMT_RGB_565;
      ti.data = buf.data();
      return ti;
    };
    GrTexInfo baseInfo = makeTexInfo(basePixels);
    GrTexInfo lightInfo = makeTexInfo(lightPixels);
    GrTexInfo detailInfo = makeTexInfo(detailPixels);

    // Download to TMU0
    unsigned long baseTextureAddr = grTexMinAddress(GR_TMU0);
    texDownload(GR_TMU0, baseTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &baseInfo);

    unsigned long lightTextureAddr =
        baseTextureAddr +
        grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &baseInfo);
    texDownload(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &lightInfo);

    unsigned long detailTextureAddr =
        lightTextureAddr +
        grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &lightInfo);
    texDownload(GR_TMU0, detailTextureAddr, GR_MIPMAPLEVELMASK_BOTH,
                &detailInfo);

    // Vertices setup (Centered about Y axis)
    TlVertex3D srcVerts[4] = {
        {-0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        {0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0, 0, 0, 0},
        {-0.5f, 0.0f, -0.5f, 1.0f, 0.0f, 1.0f, 0, 0, 0, 0},
        {0.5f, 0.0f, -0.5f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0}};

    std::vector<uint32_t> fb(640 * 480);
    auto assertColorClose = [](uint32_t actual, uint32_t expected) {
      int b_act = (actual >> 16) & 0xFF;
      int g_act = (actual >> 8) & 0xFF;
      int r_act = actual & 0xFF;
      int b_exp = (expected >> 16) & 0xFF;
      int g_exp = (expected >> 8) & 0xFF;
      int r_exp = expected & 0xFF;
      INFO("actual=0x" << std::hex << actual << " expected=0x" << expected
                       << std::dec);
      REQUIRE(std::abs(b_act - b_exp) <= 15);
      REQUIRE(std::abs(g_act - g_exp) <= 15);
      REQUIRE(std::abs(r_act - r_exp) <= 15);
    };

    auto renderCompositingFrame = [&](int modeIndex, int detailBias = 2) {
      bufferClear(0x00404040, 0, GR_ZDEPTHVALUE_FARTHEST);

      texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
      colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

      GrVertex vtxA{}, vtxB{}, vtxC{}, vtxD{};
      TlVertex3D xfVerts[4];
      TlVertex3D prjVerts[4];

      vtxA.oow = 1.0f;
      vtxB = vtxC = vtxD = vtxA;

      tlSetMatrix(tlIdentity());
      tlMultMatrix(tlXRotation(-20.0f));
      tlMultMatrix(tlTranslation(0.0f, -0.3f, 1.01f));

      tlTransformVertices(xfVerts, srcVerts, 4);
      tlProjectVertices(prjVerts, xfVerts, 4);

      vtxA.x = tlScaleX(prjVerts[0].x);
      vtxA.y = tlScaleY(1.0f - prjVerts[0].y);
      vtxA.oow = 1.0f / prjVerts[0].w;

      vtxB.x = tlScaleX(prjVerts[1].x);
      vtxB.y = tlScaleY(1.0f - prjVerts[1].y);
      vtxB.oow = 1.0f / prjVerts[1].w;

      vtxC.x = tlScaleX(prjVerts[2].x);
      vtxC.y = tlScaleY(1.0f - prjVerts[2].y);
      vtxC.oow = 1.0f / prjVerts[2].w;

      vtxD.x = tlScaleX(prjVerts[3].x);
      vtxD.y = tlScaleY(1.0f - prjVerts[3].y);
      vtxD.oow = 1.0f / prjVerts[3].w;

      vtxA.tmuvtx[0].sow = prjVerts[0].s * 255.0f * vtxA.oow;
      vtxA.tmuvtx[0].tow = prjVerts[0].t * 255.0f * vtxA.oow;
      vtxB.tmuvtx[0].sow = prjVerts[1].s * 255.0f * vtxB.oow;
      vtxB.tmuvtx[0].tow = prjVerts[1].t * 255.0f * vtxB.oow;
      vtxC.tmuvtx[0].sow = prjVerts[2].s * 255.0f * vtxC.oow;
      vtxC.tmuvtx[0].tow = prjVerts[2].t * 255.0f * vtxC.oow;
      vtxD.tmuvtx[0].sow = prjVerts[3].s * 255.0f * vtxD.oow;
      vtxD.tmuvtx[0].tow = prjVerts[3].t * 255.0f * vtxD.oow;

      // Pass 1
      if (modeIndex == 0 || modeIndex == 1) {
        texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                   FXFALSE);
      } else if (modeIndex == 2) {
        texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL,
                   GR_COMBINE_FACTOR_DETAIL_FACTOR, GR_COMBINE_FUNCTION_LOCAL,
                   GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        texDetailControl(GR_TMU0, detailBias, 7, 1.0f);
      }
      alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
      texSource(GR_TMU0, baseTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &baseInfo);

      drawTriangle(&vtxA, &vtxB, &vtxD);
      drawTriangle(&vtxA, &vtxD, &vtxC);

      // Pass 2
      if (modeIndex == 0) {  // LIGHTMAP
        alphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ZERO,
                           GR_BLEND_ZERO);
        texSource(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH,
                  &lightInfo);
      } else if (modeIndex == 1) {  // SPECULAR
        alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO,
                           GR_BLEND_ZERO);
        texSource(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH,
                  &lightInfo);
      } else if (modeIndex == 2) {  // DETAIL
        texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL,
                   GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR,
                   GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                   FXFALSE);
        alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO,
                           GR_BLEND_ZERO);
        texSource(GR_TMU0, detailTextureAddr, GR_MIPMAPLEVELMASK_BOTH,
                  &detailInfo);
      }

      drawTriangle(&vtxA, &vtxB, &vtxD);
      drawTriangle(&vtxA, &vtxD, &vtxC);

      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);
    };

    // Mode 0: LIGHTMAP (Blue * light B=222 -> 0xFFDE0000)
    renderCompositingFrame(0);
    assertColorClose(fb[300 * 640 + 320], 0xFFDE0000);
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);

    // Mode 1: SPECULAR (Blue + warm light (200,160,224) clamped -> (200,160,255))
    renderCompositingFrame(1);
    assertColorClose(fb[300 * 640 + 320], 0xFFFFA0C8);
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);

    // Mode 2: DETAIL
    // Authentic 3dfx detail texturing (grTexDetailControl(TMU0, 2, 7, 1.0f)):
    //   Pass 1 = base * (1 - b), Pass 2 = detail * b (additive blend)
    //   b = clamp((detailBias - rawLod) * (1 << detailScale) / 255.0, 0, max)
    // At distance=1.01f with detailBias=2, rawLod (~0.96) < 2 so detail blends ~52%.
    renderCompositingFrame(2, 2);
    {
      const uint32_t px = fb[300 * 640 + 320];
      const int r = static_cast<int>(px & 0xFFu);
      const int b = static_cast<int>((px >> 16) & 0xFFu);
      REQUIRE(r >= 80);
      REQUIRE(b >= 80);
      REQUIRE(b <= 175);
    }
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);

    // With detailBias=0, rawLod (~0.96) >= 0 so b = 0.0 -> 100% baseTexture
    // (solid Blue = ABGR 0x00FF0000).
    renderCompositingFrame(2, 0);
    assertColorClose(fb[300 * 640 + 320], 0x00FF0000);
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);
  }

  SECTION("SDK parity (29): Perspective vs. Affine") {
    tlSetScreen(640.0f, 480.0f);
    auto depthBufferMode =
        (void (*)(GrDepthBufferMode_t))dlsym(handle, "grDepthBufferMode");
    auto depthBufferFunction =
        (void (*)(GrCmpFnc_t))dlsym(handle, "grDepthBufferFunction");
    auto depthMask = (void (*)(FxBool))dlsym(handle, "grDepthMask");
    auto hints = (void (*)(GrHint_t, FxU32))dlsym(handle, "grHints");
    auto texCombine = (void (*)(
        GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
        GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
    using AlphaBlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                    GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    auto alphaBlendFunction =
        reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));

    REQUIRE(depthBufferMode != nullptr);
    REQUIRE(depthBufferFunction != nullptr);
    REQUIRE(depthMask != nullptr);
    REQUIRE(hints != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);

    auto assertColorClose = [](uint32_t actual, uint32_t expected) {
      int b_act = (actual >> 16) & 0xFF;
      int g_act = (actual >> 8) & 0xFF;
      int r_act = actual & 0xFF;
      int b_exp = (expected >> 16) & 0xFF;
      int g_exp = (expected >> 8) & 0xFF;
      int r_exp = expected & 0xFF;
      INFO("actual=0x" << std::hex << actual << " expected=0x" << expected
                       << std::dec);
      REQUIRE(std::abs(b_act - b_exp) <= 15);
      REQUIRE(std::abs(g_act - g_exp) <= 15);
      REQUIRE(std::abs(r_act - r_exp) <= 15);
    };

    // Self-contained 256x256 RGB565 horizontal band texture:
    //   y < 32   : Green (0x07E0) -> sampled at (320,270) in Affine mode (t_y ~ 21)
    //   32..139  : White (0xFFFF) -> sampled at (320,270) in Persp (t_y ~ 44) & (280,320) in Affine (t_y ~ 115)
    //   140..255 : Red   (0xF800) -> sampled at (280,320) in Persp (t_y ~ 166)
    std::vector<uint16_t> bandPixels(256u * 256u);
    for (int y = 0; y < 256; ++y) {
      const uint16_t c = (y < 32) ? 0x07E0u : ((y < 140) ? 0xFFFFu : 0xF800u);
      for (int x = 0; x < 256; ++x) {
        bandPixels[static_cast<size_t>(y) * 256u + static_cast<size_t>(x)] = c;
      }
    }
    GrTexInfo texInfo{};
    texInfo.smallLod = GR_LOD_256;
    texInfo.largeLod = GR_LOD_256;
    texInfo.aspectRatio = GR_ASPECT_1x1;
    texInfo.format = GR_TEXFMT_RGB_565;
    texInfo.data = bandPixels.data();

    texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
              GR_TEXTUREFILTER_POINT_SAMPLED);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    depthBufferFunction(GR_CMP_LESS);
    depthMask(FXTRUE);

    struct LocalVertex3D {
      float x, y, z, w;
      float s, t;
    };

    LocalVertex3D srcVerts[4] = {{-0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f},
                                 {0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f},
                                 {0.5f, 0.0f, -0.5f, 1.0f, 1.0f, 1.0f},
                                 {-0.5f, 0.0f, -0.5f, 1.0f, 0.0f, 1.0f}};

    std::vector<uint32_t> fb(640 * 480);
    auto renderFrame29 = [&](bool separateW) {
      if (separateW) {
        hints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);
      } else {
        hints(GR_HINT_STWHINT, 0);
      }

      bufferClear(0x00404040, 0, GR_WDEPTHVALUE_FARTHEST);
      alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);

      GrVertex v[4]{};
      float distance = 1.2f;  // Frame 0 distance
      float angle = -20.0f * 3.14159265f / 180.0f;
      float cos_t = std::cos(angle);
      float sin_t = std::sin(angle);

      for (int i = 0; i < 4; ++i) {
        float rx = srcVerts[i].x;
        float ry = srcVerts[i].y * cos_t - srcVerts[i].z * sin_t;
        float rz = srcVerts[i].y * sin_t + srcVerts[i].z * cos_t;
        float tx = rx;
        float ty = ry - 0.3f;
        float tz = rz + distance;

        float px = tx / tz;
        float py = ty / tz;

        v[i].x = tlScaleX((px + 1.0f) * 0.5f);
        v[i].y = tlScaleY((1.0f - py) * 0.5f);
        v[i].oow = 1.0f / tz;

        if (separateW) {
          v[i].tmuvtx[0].oow = 1.0f;
          v[i].tmuvtx[0].sow = srcVerts[i].s * 255.0f * v[i].tmuvtx[0].oow;
          v[i].tmuvtx[0].tow = srcVerts[i].t * 255.0f * v[i].tmuvtx[0].oow;
        } else {
          v[i].tmuvtx[0].oow = v[i].oow;
          v[i].tmuvtx[0].sow = srcVerts[i].s * 255.0f * v[i].tmuvtx[0].oow;
          v[i].tmuvtx[0].tow = srcVerts[i].t * 255.0f * v[i].tmuvtx[0].oow;
        }
      }

      drawTriangle(&v[0], &v[1], &v[2]);
      drawTriangle(&v[0], &v[2], &v[3]);

      REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                      fb.data()) == FXTRUE);
    };

    // Mode 0: Separate W (Affine)
    renderFrame29(true);
    assertColorClose(fb[270 * 640 + 320], 0xFF00FF00);  // Green (t_y ~ 21)
    assertColorClose(fb[320 * 640 + 280], 0xFFFFFFFF);  // White (t_y ~ 115)
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);

    // Mode 1: Default W (Perspective)
    renderFrame29(false);
    assertColorClose(fb[270 * 640 + 320], 0xFFFFFFFF);  // White (t_y ~ 44)
    assertColorClose(fb[320 * 640 + 280], 0xFF0000FF);  // Red   (t_y ~ 166)
    assertColorClose(fb[50 * 640 + 50], 0xFF414041);
  }

  SECTION("SDK parity (16): Watermark & Logo") {
    tlSetScreen(640.0f, 480.0f);
    auto glideShamelessPlug =
        (void (*)(FxBool))dlsym(handle, "grGlideShamelessPlug");
    auto splash =
        (void (*)(float, float, float, float, FxU32))dlsym(handle, "grSplash");
    auto constantColorValue =
        (void (*)(GrColor_t))dlsym(handle, "grConstantColorValue");
    auto colorCombine =
        (void (*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
                  GrCombineFactor_t, FxBool))dlsym(handle, "grColorCombine");
    auto drawTriangle =
        (void (*)(const GrVertex*, const GrVertex*, const GrVertex*))dlsym(
            handle, "grDrawTriangle");
    auto bufferClear =
        (void (*)(GrColor_t, GrAlpha_t, FxU16))dlsym(handle, "grBufferClear");
    auto bufferSwap = (void (*)(int))dlsym(handle, "grBufferSwap");

    REQUIRE(glideShamelessPlug != nullptr);
    REQUIRE(splash != nullptr);
    REQUIRE(constantColorValue != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);

    auto assertColorClose = [](uint32_t actual, uint32_t expected) {
      int r_act = (actual >> 16) & 0xFF;
      int g_act = (actual >> 8) & 0xFF;
      int b_act = actual & 0xFF;
      int r_exp = (expected >> 16) & 0xFF;
      int g_exp = (expected >> 8) & 0xFF;
      int b_exp = expected & 0xFF;
      REQUIRE(std::abs(r_act - r_exp) <= 15);
      REQUIRE(std::abs(g_act - g_exp) <= 15);
      REQUIRE(std::abs(b_act - b_exp) <= 15);
    };

    auto renderMode0Frame = [&](int frameIndex, bool enablePlug) {
#define BLUE_COLOR 0x00ff0000
#define RED_COLOR 0x000000ff
#define BLACK_COLOR 0x00000000
      bufferClear(BLACK_COLOR, 0, GR_ZDEPTHVALUE_FARTHEST);

      GrVertex vtxA{}, vtxB{}, vtxC{}, vtxD{};
      vtxA.oow = vtxB.oow = vtxC.oow = vtxD.oow = 1.0f;
      for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
          vtxA.x = vtxB.x = tlScaleX(static_cast<float>(x) / 10.0f);
          vtxA.y = vtxD.y = tlScaleY(static_cast<float>(y) / 10.0f);
          vtxB.y = vtxC.y = tlScaleY((static_cast<float>(y) / 10.0f) + 0.1f);
          vtxC.x = vtxD.x = tlScaleX((static_cast<float>(x) / 10.0f) + 0.1f);

          constantColorValue(RED_COLOR);
          drawTriangle(&vtxA, &vtxB, &vtxC);
          constantColorValue(BLUE_COLOR);
          drawTriangle(&vtxA, &vtxC, &vtxD);
        }
      }

      glideShamelessPlug(enablePlug ? FXTRUE : FXFALSE);
      bufferSwap(1);
    };

    auto renderMode1Frame = [&](int frameIndex) {
#define BLACK_COLOR 0x00000000
      bufferClear(BLACK_COLOR, 0, GR_ZDEPTHVALUE_FARTHEST);
      glideShamelessPlug(FXFALSE);
      splash(0.0f, 0.0f, 640.0f, 480.0f, frameIndex + 1);
      bufferSwap(1);
    };

    std::vector<uint32_t> fb(640 * 480);

    // Mode 0: Watermark
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    renderMode0Frame(14, true);
    REQUIRE(readLfb(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 4,
                    fb.data()) == FXTRUE);

    assertColorClose(fb[50 * 640 + 50], 0x00FF0000);    // Red background
    assertColorClose(fb[240 * 640 + 320], 0x000000FF);  // Blue background
    assertColorClose(fb[430 * 640 + 50], 0x000000FF);   // Blue background
    // Clean-room 180x90 Chrome & Amber Shield badge (dstX=460, dstY=389) with
    // black (0x0000) chromakey and GR_BLEND_SRC_ALPHA (90/255) translucency:
    // 1. Chromakey-transparent margin inside 180x90 box stays unblended.
    {
      uint32_t marginPx = fb[392 * 640 + 462];  // local (2, 3): pure chromakey
      int mr = (marginPx >> 16) & 0xFF;
      int mg = (marginPx >> 8) & 0xFF;
      int mb = marginPx & 0xFF;
      REQUIRE(mg <= 5);
      REQUIRE((mr >= 245 || mb >= 245));
    }
    // 2. Amber pill region introduces green channel (~50) over pure red/blue
    //    checkerboard (whose green channel is 0), proving alpha=90 blending.
    {
      uint32_t amberPx = fb[414 * 640 + 598];  // local (138, 25) in upper amber pill
      int ag = (amberPx >> 8) & 0xFF;
      REQUIRE(ag >= 25);
    }
    // 3. Verify substantial badge coverage (>4000 blended pixels in 180x90 box).
    {
      int blendedCount = 0;
      for (int by = 389; by < 479; ++by) {
        for (int bx = 460; bx < 640; ++bx) {
          uint32_t px = fb[by * 640 + bx];
          int pr = (px >> 16) & 0xFF;
          int pg = (px >> 8) & 0xFF;
          int pb = px & 0xFF;
          bool pureRed = (pr > 240 && pg < 10 && pb < 10);
          bool pureBlue = (pb > 240 && pg < 10 && pr < 10);
          if (!pureRed && !pureBlue) {
            ++blendedCount;
          }
        }
      }
      REQUIRE(blendedCount > 4000);
    }

    // Mode 1: Clean-Room 3D Chrome & Amber Shield Splash Screen
    renderMode1Frame(35);
    REQUIRE(readLfb(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 4,
                    fb.data()) == FXTRUE);

    assertColorClose(fb[50 * 640 + 50], 0xFF000000);   // Black top-left sky
    assertColorClose(fb[50 * 640 + 590], 0xFF000000);  // Black top-right sky
    // Center of the spinning 3D shield around (320, 211) must be visibly lit
    {
      uint32_t centerPx = fb[211 * 640 + 320];
      int cr = (centerPx >> 16) & 0xFF;
      int cg = (centerPx >> 8) & 0xFF;
      int cb = centerPx & 0xFF;
      REQUIRE((cr + cg + cb) > 120);
    }
    // Whole splash (3D shield + perspective floor grid + "GLIDE NG" wordmark)
    {
      int litPixels = 0;
      for (uint32_t px : fb) {
        if ((px & 0x00FFFFFFu) != 0u) {
          ++litPixels;
        }
      }
      REQUIRE(litPixels > 10000);
    }

    // Cleanup state
    glideShamelessPlug(FXFALSE);
  }

  shutdown();
  ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x: Per-TMU Chromakey & Palette Deduplication", "[Glide2][Texture][Chroma]") {
  SECTION("Per-TMU chromakey & state serialization") {
#ifdef __linux__
  SECTION("Glide 2.x Per-TMU Texture Chromakey Caching & State Serialization") {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using ShutdownFunc = void (*)(void);
    using SstSelectFunc = void (*)(int);
    using SstWinOpenFunc =
        FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                   GrColorFormat_t, GrOriginLocation_t, int, int);
    using GlideGetStateFunc = void (*)(GrState*);
    using GlideSetStateFunc = void (*)(const GrState*);
    using WinCloseFunc = void (*)(void);

    auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto shutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect =
        reinterpret_cast<SstSelectFunc>(::dlsym(handle, "grSstSelect"));
    auto sstWinOpen =
        reinterpret_cast<SstWinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    using TexChromaModeGlide2Func = void (*)(int, uint32_t);
    using TexChromaRangeGlide2Func =
        void (*)(int, uint32_t, uint32_t, uint32_t);

    auto texChromaMode = reinterpret_cast<TexChromaModeGlide2Func>(
        ::dlsym(handle, "grTexChromaModeExt"));
    auto texChromaRange = reinterpret_cast<TexChromaRangeGlide2Func>(
        ::dlsym(handle, "grTexChromaRangeExt"));
    auto glideGetState =
        reinterpret_cast<GlideGetStateFunc>(::dlsym(handle, "grGlideGetState"));
    auto glideSetState =
        reinterpret_cast<GlideSetStateFunc>(::dlsym(handle, "grGlideSetState"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

    REQUIRE(init != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(sstWinOpen != nullptr);
    REQUIRE(texChromaMode != nullptr);
    REQUIRE(texChromaRange != nullptr);
    REQUIRE(glideGetState != nullptr);
    REQUIRE(glideSetState != nullptr);
    REQUIRE(winClose != nullptr);

    ::setenv("GLIDE_NG_BACKEND", "software", 1);

    init();
    sstSelect(0);
    FxBool open_ok =
        sstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                   GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(open_ok == FXTRUE);

    // Configure per-TMU chromakey
    texChromaMode(0, 1);
    texChromaRange(0, 0x00AABBCC, 0x00DDEEFF, 1);
    texChromaMode(1, 0);

    auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend());
    REQUIRE(backend != nullptr);
    CHECK(backend->GetTexChromaMode(0) == 1);
    CHECK(backend->GetTexChromaMin(0) == 0x00AABBCC);
    CHECK(backend->GetTexChromaMax(0) == 0x00DDEEFF);

    // Serialize
    std::vector<char> stateBuf(2048, 0);
    GrState* statePtr = reinterpret_cast<GrState*>(stateBuf.data());
    glideGetState(statePtr);

    // Modify
    texChromaMode(0, 0);
    CHECK(backend->GetTexChromaMode(0) == 0);

    // Restore
    glideSetState(statePtr);

    // Verify
    CHECK(backend->GetTexChromaMode(0) == 1);
    CHECK(backend->GetTexChromaMin(0) == 0x00AABBCC);
    CHECK(backend->GetTexChromaMax(0) == 0x00DDEEFF);

    winClose();
    shutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::dlclose(handle);
  }
#endif
  }

  SECTION("Lazy palette switching & state-setter deduplication") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadSplash;
    std::string splashVal;
    ~EnvRestorer() {
      if (hadSplash)
        ::setenv("FX_GLIDE_NO_SPLASH", splashVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_NO_SPLASH");
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXTRUE);
  grDitherMode(GR_DITHER_DISABLE);

  SECTION("20 resident P_8 textures + 10 palette swaps only upload bound tex") {
    // Upload initial palette
    GuTexPalette pal{};
    for (int i = 0; i < 256; ++i) pal.data[i] = 0x00FF0000u;  // Red
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    // Download 20 distinct 16x16 P_8 textures into TMU0
    std::vector<uint8_t> texData(16 * 16, 42);
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = texData.data();

    const uint32_t stride =
        grTexCalcMemRequired(GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
                             GR_TEXFMT_P_8);
    std::vector<uint32_t> addrs(20);
    for (int i = 0; i < 20; ++i) {
      addrs[i] = grTexMinAddress(GR_TMU0) + static_cast<uint32_t>(i) * stride;
      grTexDownloadMipMap(GR_TMU0, addrs[i], GR_MIPMAPLEVELMASK_BOTH, &ti);
    }

    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);

    // Flush initial upload frame
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grBufferSwap(0);

    // Frame A: swap palette 10 times, only ever binding/drawing texture #0!
    // Eager re-upload would trigger 20 * 10 = 200 uploads; lazy re-upload
    // triggers at most 10 (only texture #0 when drawn with each new palette).
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grTexSource(GR_TMU0, addrs[0], GR_MIPMAPLEVELMASK_BOTH, &ti);
    GrVertex a{}, b{}, c{};
    a.x = 100.0f;
    a.y = 100.0f;
    a.oow = 1.0f;
    a.tmuvtx[0].sow = 0.0f;
    a.tmuvtx[0].tow = 0.0f;
    b.x = 300.0f;
    b.y = 100.0f;
    b.oow = 1.0f;
    b.tmuvtx[0].sow = 255.0f;
    b.tmuvtx[0].tow = 0.0f;
    c.x = 200.0f;
    c.y = 300.0f;
    c.oow = 1.0f;
    c.tmuvtx[0].sow = 128.0f;
    c.tmuvtx[0].tow = 255.0f;

    for (int p = 0; p < 10; ++p) {
      // Make last palette pure GREEN so we also verify visual correctness!
      uint32_t col = (p == 9) ? 0x0000FF00u : static_cast<uint32_t>(p * 20 + 5);
      pal.data[42] = col;
      grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
      grDrawTriangle(&a, &b, &c);
    }
    grBufferSwap(0);

    const auto mA =
        GlideWrapper::TelemetryManager::GetInstance().GetLastFrameMetrics();
    REQUIRE(mA.texUploads <= 10);

    // Verify rendered pixel reflects the final GREEN palette!
    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 200, 180, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 16) & 0xFFu) <= 10);
    REQUIRE(((px >> 8) & 0xFFu) >= 240);
    REQUIRE((px & 0xFFu) <= 10);

    // Frame B: re-submitting the EXACT same palette and drawing again -> 0
    // uploads!
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    grTexSource(GR_TMU0, addrs[0], GR_MIPMAPLEVELMASK_BOTH, &ti);
    grDrawTriangle(&a, &b, &c);
    grBufferSwap(0);

    const auto mB =
        GlideWrapper::TelemetryManager::GetInstance().GetLastFrameMetrics();
    REQUIRE(mB.texUploads == 0);
  }

  SECTION("Redundant state setters do not cause extra GPU state flushes") {
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grConstantColorValue(0xFFFF0000);

    // Warm one frame so baseline state is established
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grBufferSwap(0);

    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    GrVertex a{}, b{}, c{};
    a.oow = b.oow = c.oow = 1.0f;
    for (int i = 0; i < 16; ++i) {
      // Repeat identical state calls before every triangle (matches splash /
      // real game loops).
      grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                           GR_BLEND_ZERO);
      grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
      grConstantColorValue(0xFFFF0000);
      a.x = 10.0f + i * 20.0f;
      a.y = 10.0f;
      b.x = 25.0f + i * 20.0f;
      b.y = 10.0f;
      c.x = 18.0f + i * 20.0f;
      c.y = 40.0f;
      grDrawTriangle(&a, &b, &c);
    }
    grBufferSwap(0);

    const auto m =
        GlideWrapper::TelemetryManager::GetInstance().GetLastFrameMetrics();
    REQUIRE(m.stateFlushes == 0);
    // On Vulkan & OpenGL ES, all 16 triangles batch into 1 draw call.
    const char* envBackend = std::getenv("GLIDE_NG_BACKEND");
    if (envBackend && (std::string(envBackend) == "vulkan" ||
                       std::string(envBackend) == "opengl_es")) {
      REQUIRE(m.drawCalls == 1);
    }
  }

  grSstWinClose();
  grGlideShutdown();
#endif
  }

  SECTION("Texture replacement, mid-frame snapshots & exact chromakey") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadSplash;
    std::string splashVal;
    ~EnvRestorer() {
      if (hadSplash)
        ::setenv("FX_GLIDE_NO_SPLASH", splashVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_NO_SPLASH");
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXTRUE);
  grDitherMode(GR_DITHER_DISABLE);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                       GR_BLEND_ZERO);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                  GR_TEXTUREFILTER_POINT_SAMPLED);
  grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);

  auto drawTexturedQuad = [](float x0, float y0, float x1, float y1) {
    GrVertex v0{}, v1{}, v2{}, v3{};
    v0.x = x0;
    v0.y = y0;
    v0.oow = 1.0f;
    v0.tmuvtx[0].sow = 0.0f;
    v0.tmuvtx[0].tow = 0.0f;

    v1.x = x1;
    v1.y = y0;
    v1.oow = 1.0f;
    v1.tmuvtx[0].sow = 255.0f;
    v1.tmuvtx[0].tow = 0.0f;

    v2.x = x1;
    v2.y = y1;
    v2.oow = 1.0f;
    v2.tmuvtx[0].sow = 255.0f;
    v2.tmuvtx[0].tow = 255.0f;

    v3.x = x0;
    v3.y = y1;
    v3.oow = 1.0f;
    v3.tmuvtx[0].sow = 0.0f;
    v3.tmuvtx[0].tow = 255.0f;

    grDrawTriangle(&v0, &v1, &v2);
    grDrawTriangle(&v0, &v2, &v3);
  };

  SECTION("Replacing 256x256 with 64x64 at same address does not merge/crash") {
    GuTexPalette pal{};
    pal.data[1] = 0xFFFF0000u;  // Red
    pal.data[2] = 0xFF00FF00u;  // Green
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    const FxU32 addr = grTexMinAddress(GR_TMU0);
    std::vector<uint8_t> red256(256 * 256, 1);
    GrTexInfo ti256{};
    ti256.smallLod = GR_LOD_256;
    ti256.largeLod = GR_LOD_256;
    ti256.aspectRatio = GR_ASPECT_1x1;
    ti256.format = GR_TEXFMT_P_8;
    ti256.data = red256.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti256);

    // Replace with a smaller 64x64 green texture at the exact same address.
    // Previously this merged LODs 0..2 with an empty LOD 1 vector, causing
    // SIGSEGV on GLES and keeping red at mip 0 across all backends.
    std::vector<uint8_t> green64(64 * 64, 2);
    GrTexInfo ti64{};
    ti64.smallLod = GR_LOD_64;
    ti64.largeLod = GR_LOD_64;
    ti64.aspectRatio = GR_ASPECT_1x1;
    ti64.format = GR_TEXFMT_P_8;
    ti64.data = green64.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti64);

    auto* tex =
        GlideWrapper::TextureManager::GetInstance().GetTexture(GR_TMU0, addr);
    REQUIRE(tex != nullptr);
    REQUIRE(tex->baseWidth == 64);
    REQUIRE(tex->baseHeight == 64);
    REQUIRE(tex->swizzledMipLevels.size() == 1);

    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti64);
    drawTexturedQuad(100.0f, 100.0f, 300.0f, 300.0f);
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 200, 200, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 16) & 0xFFu) <= 10);
    REQUIRE(((px >> 8) & 0xFFu) >= 240);
    REQUIRE((px & 0xFFu) <= 10);
  }

  SECTION("Overlapping TMU byte ranges evict stale textures cleanly") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    const FxU32 base = grTexMinAddress(GR_TMU0);
    std::vector<uint8_t> buf256(256 * 256, 1);
    std::vector<uint8_t> buf64(64 * 64, 2);

    GrTexInfo ti256{};
    ti256.smallLod = GR_LOD_256;
    ti256.largeLod = GR_LOD_256;
    ti256.aspectRatio = GR_ASPECT_1x1;
    ti256.format = GR_TEXFMT_P_8;
    ti256.data = buf256.data();

    GrTexInfo ti64{};
    ti64.smallLod = GR_LOD_64;
    ti64.largeLod = GR_LOD_64;
    ti64.aspectRatio = GR_ASPECT_1x1;
    ti64.format = GR_TEXFMT_P_8;
    ti64.data = buf64.data();

    grTexDownloadMipMap(GR_TMU0, base, GR_MIPMAPLEVELMASK_BOTH, &ti256);
    REQUIRE(tm.GetTexture(GR_TMU0, base) != nullptr);

    // Downloading inside [base, base + 65536) evicts the enclosing 256x256 tex.
    const FxU32 innerAddr = base + 0x4000u;
    grTexDownloadMipMap(GR_TMU0, innerAddr, GR_MIPMAPLEVELMASK_BOTH, &ti64);
    REQUIRE(tm.GetTexture(GR_TMU0, base) == nullptr);
    REQUIRE(tm.GetTexture(GR_TMU0, innerAddr) != nullptr);

    // Downloading back at base evicts the overlapping inner 64x64 texture.
    grTexDownloadMipMap(GR_TMU0, base, GR_MIPMAPLEVELMASK_BOTH, &ti256);
    REQUIRE(tm.GetTexture(GR_TMU0, innerAddr) == nullptr);
    REQUIRE(tm.GetTexture(GR_TMU0, base) != nullptr);
  }

  SECTION("Mid-frame palette swap and TMU overwrite preserve earlier draws") {
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    GuTexPalette pal{};
    pal.data[1] = 0xFFFF0000u;  // Red
    pal.data[2] = 0xFF0000FFu;  // Blue
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    std::vector<uint8_t> texIdx1(16 * 16, 1);
    std::vector<uint8_t> texIdx2(16 * 16, 2);
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = texIdx1.data();

    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);

    // 1. Left quad: Red (idx 1, palette 1)
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    drawTexturedQuad(40.0f, 100.0f, 140.0f, 200.0f);

    // 2. Center quad: Green (same address, mid-frame palette swap)
    pal.data[1] = 0xFF00FF00u;
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    drawTexturedQuad(240.0f, 100.0f, 340.0f, 200.0f);

    // 3. Right quad: Blue (same address, mid-frame texture overwrite)
    ti.data = texIdx2.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    drawTexturedQuad(440.0f, 100.0f, 540.0f, 200.0f);

    grBufferSwap(0);

    uint32_t leftPx = 0, centerPx = 0, rightPx = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 90, 150, 1, 1, 4, &leftPx) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 290, 150, 1, 1, 4,
                            &centerPx) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 490, 150, 1, 1, 4,
                            &rightPx) == FXTRUE);

    // Left = Red
    REQUIRE(((leftPx >> 16) & 0xFFu) >= 240);
    REQUIRE(((leftPx >> 8) & 0xFFu) <= 10);
    REQUIRE((leftPx & 0xFFu) <= 10);
    // Center = Green
    REQUIRE(((centerPx >> 16) & 0xFFu) <= 10);
    REQUIRE(((centerPx >> 8) & 0xFFu) >= 240);
    REQUIRE((centerPx & 0xFFu) <= 10);
    // Right = Blue
    REQUIRE(((rightPx >> 16) & 0xFFu) <= 10);
    REQUIRE(((rightPx >> 8) & 0xFFu) <= 10);
    REQUIRE((rightPx & 0xFFu) >= 240);
  }

  SECTION(
      "Exact 8-bit chromakey discards RGB(0,0,0) but keeps "
      "RGB(1,1,1)/(2,2,2)") {
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    GuTexPalette pal{};
    pal.data[0] = 0xFF000000u;  // Exact black (chromakey target)
    pal.data[1] = 0xFF010101u;  // Near-black RGB(1,1,1) — must survive!
    pal.data[2] = 0xFF020202u;  // Near-black RGB(2,2,2) — must survive!
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    // 64x64 texture split into 3 vertical stripes: [0..20]=0, [21..42]=1,
    // [43..63]=2
    std::vector<uint8_t> texBuf(64 * 64, 0);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        uint8_t idx = (x < 21) ? 0 : ((x < 43) ? 1 : 2);
        texBuf[static_cast<size_t>(y) * 64 + x] = idx;
      }
    }

    GrTexInfo ti{};
    ti.smallLod = GR_LOD_64;
    ti.largeLod = GR_LOD_64;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = texBuf.data();

    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);

    // Bright magenta background to clearly detect chromakey punch-through.
    grBufferClear(0x00FF00FFu, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grChromakeyValue(0x00000000u);
    grChromakeyMode(GR_CHROMAKEY_ENABLE);
    drawTexturedQuad(100.0f, 100.0f, 400.0f, 200.0f);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grBufferSwap(0);

    uint32_t pxKey = 0, pxNear1 = 0, pxNear2 = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 150, 150, 1, 1, 4, &pxKey) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 250, 150, 1, 1, 4,
                            &pxNear1) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 350, 150, 1, 1, 4,
                            &pxNear2) == FXTRUE);

    // Stripe 0: exact black discarded -> Magenta background visible
    REQUIRE(((pxKey >> 16) & 0xFFu) >= 240);
    REQUIRE(((pxKey >> 8) & 0xFFu) <= 10);
    REQUIRE((pxKey & 0xFFu) >= 240);

    // Stripe 1: RGB(1,1,1) NOT discarded -> solid dark pixel
    REQUIRE(((pxNear1 >> 16) & 0xFFu) <= 5);
    REQUIRE(((pxNear1 >> 8) & 0xFFu) <= 5);
    REQUIRE((pxNear1 & 0xFFu) <= 5);

    // Stripe 2: RGB(2,2,2) NOT discarded -> solid dark pixel
    REQUIRE(((pxNear2 >> 16) & 0xFFu) <= 5);
    REQUIRE(((pxNear2 >> 8) & 0xFFu) <= 5);
    REQUIRE((pxNear2 & 0xFFu) <= 5);
  }

  grSstWinClose();
  grGlideShutdown();
#endif
  }

}

TEST_CASE("Glide 2.x: Partial Mipmaps, Coplanar W-Buffer & Combiner Clamping", "[Glide2][Texture][Combiner]") {
#ifdef __linux__
  ::setenv("GLIDE_NG_HEADLESS", "1", 1);
  ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
  ::setenv("GLIDE_NG_VSYNC", "0", 1);

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);

  auto makeV = [](float x, float y, float s, float t, float r, float g,
                  float b) {
    GrVertex v{};
    v.x = x;
    v.y = y;
    v.oow = 1.0f;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = 255.0f;
    v.tmuvtx[0].oow = 1.0f;
    v.tmuvtx[0].sow = s;
    v.tmuvtx[0].tow = t;
    v.tmuvtx[1].oow = 1.0f;
    v.tmuvtx[1].sow = s;
    v.tmuvtx[1].tow = t;
    return v;
  };

  SECTION("grTexDownloadMipMapLevelPartial updates inclusive [start..end] rows only") {
    constexpr uint32_t kDim = 16;
    std::vector<uint16_t> redTex(kDim * kDim, 0xF800u);   // Pure red RGB565
    std::vector<uint16_t> greenBand(4 * kDim, 0x07E0u);   // Rows 4..7 inclusive
    std::vector<uint16_t> blueRow(1 * kDim, 0x001Fu);     // Row 12..12 single row

    GrTexInfo info{};
    info.smallLod = GR_LOD_16;
    info.largeLod = GR_LOD_16;
    info.aspectRatio = GR_ASPECT_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = redTex.data();

    const FxU32 addr = grTexMinAddress(GR_TMU0);
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexDownloadMipMapLevelPartial(GR_TMU0, addr, GR_LOD_16, GR_LOD_16,
                                    GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                    GR_MIPMAPLEVELMASK_BOTH, greenBand.data(),
                                    4, 7);
    grTexDownloadMipMapLevelPartial(GR_TMU0, addr, GR_LOD_16, GR_LOD_16,
                                    GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                    GR_MIPMAPLEVELMASK_BOTH, blueRow.data(),
                                    12, 12);

    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDitherMode(GR_DITHER_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Map 16x16 texels onto 160x160 pixels (10 screen px per texel row)
    GrVertex tl = makeV(0.0f, 0.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex tr = makeV(160.0f, 0.0f, 256.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex br = makeV(160.0f, 160.0f, 256.0f, 256.0f, 255.0f, 255.0f, 255.0f);
    GrVertex bl = makeV(0.0f, 160.0f, 0.0f, 256.0f, 255.0f, 255.0f, 255.0f);
    grDrawTriangle(&tl, &tr, &br);
    grDrawTriangle(&tl, &br, &bl);

    auto readPx = [](uint32_t x, uint32_t y) -> uint16_t {
      uint16_t px = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, x, y, 1, 1, 2, &px) ==
              FXTRUE);
      return px;
    };

    // Row 2 -> untouched RED; Row 5 & Row 7 -> GREEN; Row 10 -> RED; Row 12 -> BLUE
    const uint16_t pRow2 = readPx(80, 25);
    const uint16_t pRow5 = readPx(80, 55);
    const uint16_t pRow7 = readPx(80, 75);
    const uint16_t pRow10 = readPx(80, 105);
    const uint16_t pRow12 = readPx(80, 125);
    const uint16_t pRow14 = readPx(80, 145);

    CHECK(((pRow2 >> 11) & 31) >= 28);
    CHECK(((pRow2 >> 5) & 63) <= 4);
    CHECK(((pRow5 >> 5) & 63) >= 58);
    CHECK(((pRow5 >> 11) & 31) <= 2);
    CHECK(((pRow7 >> 5) & 63) >= 58);
    CHECK(((pRow10 >> 11) & 31) >= 28);
    CHECK(((pRow10 >> 5) & 63) <= 4);
    CHECK((pRow12 & 31) >= 28);
    CHECK(((pRow12 >> 11) & 31) <= 2);
    CHECK(((pRow14 >> 11) & 31) >= 28);
  }

  SECTION("Coplanar W-buffer LEQUAL passes & negative combiner differences clamp cleanly to 0") {
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grDitherMode(GR_DITHER_DISABLE);
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LEQUAL);
    grDepthMask(FXTRUE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    // Pass 1: Large quad at oow=1.0 (green)
    GrVertex a0 = makeV(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f, 0.0f);
    GrVertex a1 = makeV(320.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f, 0.0f);
    GrVertex a2 = makeV(320.0f, 240.0f, 0.0f, 0.0f, 0.0f, 240.0f, 0.0f);
    GrVertex a3 = makeV(0.0f, 240.0f, 0.0f, 0.0f, 0.0f, 240.0f, 0.0f);
    grDrawTriangle(&a0, &a1, &a2);
    grDrawTriangle(&a0, &a2, &a3);

    // Pass 2: Offset coplanar quad at identical oow=1.0 (blue) with different diagonal
    GrVertex b0 = makeV(17.0f, 13.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f);
    GrVertex b1 = makeV(299.0f, 13.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f);
    GrVertex b2 = makeV(299.0f, 211.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f);
    GrVertex b3 = makeV(17.0f, 211.0f, 0.0f, 0.0f, 0.0f, 0.0f, 240.0f);
    grDrawTriangle(&b0, &b1, &b3);
    grDrawTriangle(&b1, &b2, &b3);

    // Every interior sample must be BLUE (zero coplanar W-buffer Z-fighting)
    for (uint32_t sx : {50u, 110u, 160u, 220u, 270u}) {
      for (uint32_t sy : {40u, 110u, 180u}) {
        uint16_t px = 0;
        REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, sx, sy, 1, 1, 2, &px) ==
                FXTRUE);
        CHECK((px & 31) >= 26);
        CHECK(((px >> 5) & 63) <= 4);
      }
    }

    // Negative combiner result: (other=0 - local=1) * 1 -> must clamp to 0 (never wrap)
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grConstantColorValue(0x00000000u);  // other = 0
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
                   GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
                   GR_COMBINE_OTHER_CONSTANT, FXFALSE);
    GrVertex n0 = makeV(360.0f, 40.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex n1 = makeV(440.0f, 40.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex n2 = makeV(440.0f, 120.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex n3 = makeV(360.0f, 120.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    grDrawTriangle(&n0, &n1, &n2);
    grDrawTriangle(&n0, &n2, &n3);

    uint16_t negPx = 0xFFFFu;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 400, 80, 1, 1, 2, &negPx) ==
            FXTRUE);
    CHECK(negPx == 0x0000u);

    // GR_DITHER_2x2 with alpha=0 transparent quad must NEVER turn pixels opaque
    grBufferClear(0xFF00FF00u, 0xFF, GR_WDEPTHVALUE_FARTHEST);  // bright green bg
    grDitherMode(GR_DITHER_2x2);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ZERO, GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0x00000000u);  // black RGB, alpha = 0 (fully transparent)
    GrVertex t0 = makeV(20.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    GrVertex t1 = makeV(100.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    GrVertex t2 = makeV(100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    GrVertex t3 = makeV(20.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    grDrawTriangle(&t0, &t1, &t2);
    grDrawTriangle(&t0, &t2, &t3);
    // Check an entire 2x2 dither tile: all 4 pixels must remain green
    for (uint32_t dx = 0; dx < 2; ++dx) {
      for (uint32_t dy = 0; dy < 2; ++dy) {
        uint16_t px = 0;
        REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 40 + dx, 40 + dy, 1, 1, 2,
                                &px) == FXTRUE);
        CHECK(((px >> 5) & 63) >= 56);
      }
    }

    // Opaque draw (GR_BLEND_ONE, GR_BLEND_ZERO) with GR_DITHER_2x2 and
    // alpha=0 (e.g. untextured Gouraud mesh with TMU0 ZERO + AlphaCombine
    // reading TEXTURE, as in 3dfx Demo Room's 3D light-direction arrow) must
    // NOT be discarded by alpha dithering.
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grDitherMode(GR_DITHER_2x2);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO,
                         GR_BLEND_ZERO);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
                   GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
                   GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE,
                   FXFALSE);
    GrVertex o0 = makeV(20.0f, 20.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex o1 = makeV(100.0f, 20.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex o2 = makeV(100.0f, 100.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    GrVertex o3 = makeV(20.0f, 100.0f, 0.0f, 0.0f, 255.0f, 255.0f, 255.0f);
    grDrawTriangle(&o0, &o1, &o2);
    grDrawTriangle(&o0, &o2, &o3);
    for (uint32_t dx = 0; dx < 2; ++dx) {
      for (uint32_t dy = 0; dy < 2; ++dy) {
        uint16_t px = 0;
        REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 40 + dx, 40 + dy, 1, 1, 2,
                                &px) == FXTRUE);
        CHECK(((px >> 11) & 31) >= 28);
        CHECK(((px >> 5) & 63) >= 56);
        CHECK((px & 31) >= 28);
      }
    }

    // Opaque draw with GR_DITHER_4x4 and GR_CMP_LESS alpha test must pass
    // when combiner alpha < ref (40 < 100), not get culled by alpha dithering.
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grDitherMode(GR_DITHER_4x4);
    grAlphaTestFunction(GR_CMP_LESS);
    grAlphaTestReferenceValue(100);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0x28FF0000u);  // alpha = 40 (< 100), bright red/blue
    grDrawTriangle(&o0, &o1, &o2);
    grDrawTriangle(&o0, &o2, &o3);
    uint16_t ltPx = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 40, 40, 1, 1, 2, &ltPx) ==
            FXTRUE);
    CHECK(ltPx != 0x0000u);
    grAlphaTestFunction(GR_CMP_ALWAYS);

    // Translucent 50% blend (alpha=128) with GR_DITHER_4x4 + SRC_ALPHA /
    // ONE_MINUS_SRC_ALPHA must smoothly preserve 8-bit blend alpha (e.g.
    // 3dfx Museum's reflective marble floor), never quantizing alpha to 1-bit.
    grBufferClear(0xFF00FF00u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grDitherMode(GR_DITHER_4x4);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0x80000000u);  // black RGB, alpha = 128 (~50%)
    grDrawTriangle(&o0, &o1, &o2);
    grDrawTriangle(&o0, &o2, &o3);
    for (uint32_t dx = 0; dx < 4; ++dx) {
      for (uint32_t dy = 0; dy < 4; ++dy) {
        uint16_t px = 0;
        REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 40 + dx, 40 + dy, 1, 1, 2,
                                &px) == FXTRUE);
        const uint32_t g6 = (px >> 5) & 63u;
        CHECK(g6 >= 24u);
        CHECK(g6 <= 40u);
      }
    }
  }

  SECTION("RAMDAC Gamma LUT switches cleanly across backends while keeping backbuffer VRAM linear") {
    // 1. Mid-grey linear clear + gamma=1.7 presentation swap
    grGammaCorrectionValue(1.7f);
    grBufferClear(0xFF808080u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    uint16_t backPx = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 80, 80, 1, 1, 2, &backPx) ==
            FXTRUE);
    // Backbuffer VRAM must remain linear mid-grey (~16/31 R, ~32/63 G, ~16/31 B)
    CHECK(((backPx >> 11) & 31) >= 14);
    CHECK(((backPx >> 11) & 31) <= 18);
    CHECK(((backPx >> 5) & 63) >= 29);
    CHECK(((backPx >> 5) & 63) <= 35);
    REQUIRE_NOTHROW(grBufferSwap(0));

    // 2. Dynamic custom LUT update (grLoadGammaTable) + second swap
    using LoadGammaTableFunc = void (*)(FxU32, FxU32*, FxU32*, FxU32*);
    auto loadGammaTable = reinterpret_cast<LoadGammaTableFunc>(
        ::dlsym(RTLD_DEFAULT, "grLoadGammaTable"));
    REQUIRE(loadGammaTable != nullptr);
    FxU32 rTab[256], gTab[256], bTab[256];
    for (int i = 0; i < 256; ++i) {
      rTab[i] = static_cast<FxU32>(std::min(255, i * 2));
      gTab[i] = static_cast<FxU32>(i);
      bTab[i] = static_cast<FxU32>(i / 2);
    }
    REQUIRE_NOTHROW(loadGammaTable(256, rTab, gTab, bTab));
    grBufferClear(0xFF808080u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    REQUIRE_NOTHROW(grBufferSwap(0));

    // 3. Reset back to linear 1.0 (restores fast direct-blit path on Vulkan)
    REQUIRE_NOTHROW(grGammaCorrectionValue(1.0f));
    grBufferClear(0xFF808080u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    REQUIRE_NOTHROW(grBufferSwap(0));
  }

  grSstWinClose();
  grGlideShutdown();
#endif
}

TEST_CASE("Glide 2.x: grTexDownloadMipMapLevel Telemetry Attribution",
          "[Glide2][Texture][Perf]") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = reinterpret_cast<void (*)(void)>(dlsym(handle, "grGlideInit"));
  auto sstSelect = reinterpret_cast<void (*)(int)>(dlsym(handle, "grSstSelect"));
  auto winOpen = reinterpret_cast<FxBool (*)(
      FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t,
      GrOriginLocation_t, int, int)>(dlsym(handle, "grSstWinOpen"));
  auto texDownloadLevel = reinterpret_cast<void (*)(
      GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureFormat_t,
      FxU32, void*)>(dlsym(handle, "grTexDownloadMipMapLevel"));
  auto bufferSwap =
      reinterpret_cast<void (*)(int)>(dlsym(handle, "grBufferSwap"));
  auto winClose =
      reinterpret_cast<void (*)(void)>(dlsym(handle, "grSstWinClose"));
  auto shutdown =
      reinterpret_cast<void (*)(void)>(dlsym(handle, "grGlideShutdown"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(texDownloadLevel != nullptr);
  REQUIRE(bufferSwap != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(shutdown != nullptr);

  glideInit();
  sstSelect(0);
  REQUIRE(winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                  GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);

  auto& tm = GlideWrapper::TelemetryManager::GetInstance();
  tm.ConfigurePerfLog(true, "");

  std::vector<uint16_t> texels(16u * 16u, 0xF800u);
  texDownloadLevel(GR_TMU0, 0u, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
                   GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH, texels.data());
  bufferSwap(0);

  const auto metrics = tm.GetLastFrameMetrics();
  CHECK(metrics.texUploads >= 1u);
  CHECK(metrics.texCpuMs > 0.0f);

  tm.ShutdownPerfLog();
  winClose();
  shutdown();
#endif
}

TEST_CASE(
    "Glide 2.x: Zero-decode backend cache hit skips CPU swizzle and preserves "
    "lazy CPU fallback",
    "[Glide2][Texture][Perf]") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  struct EnvRestorer {
    bool hadSplash;
    std::string splashVal;
    ~EnvRestorer() {
      if (hadSplash)
        ::setenv("FX_GLIDE_NO_SPLASH", splashVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_NO_SPLASH");
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash};

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXTRUE);
  grDitherMode(GR_DITHER_DISABLE);

  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  REQUIRE(backend != nullptr);
  const char* envBackend = std::getenv("GLIDE_NG_BACKEND");
  const std::string activeBackend =
      (envBackend && *envBackend)
          ? std::string(envBackend)
          : GlideWrapper::EmulationRegistry::GetInstance().GetConfig().backend;
  const bool hasBackendCache =
      (activeBackend == "vulkan" || activeBackend == "opengl_es" ||
       activeBackend == "software");

  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);

  auto drawRectTex = [&](uint32_t addr, const GrTexInfo& info, float x0,
                         float x1) {
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH,
                const_cast<GrTexInfo*>(&info));
    GrVertex a{}, b{}, c{}, d{};
    a.x = x0;
    a.y = 0.0f;
    a.oow = 1.0f;
    a.tmuvtx[0].sow = 0.0f;
    a.tmuvtx[0].tow = 0.0f;
    b.x = x1;
    b.y = 0.0f;
    b.oow = 1.0f;
    b.tmuvtx[0].sow = 255.0f;
    b.tmuvtx[0].tow = 0.0f;
    c.x = x1;
    c.y = 480.0f;
    c.oow = 1.0f;
    c.tmuvtx[0].sow = 255.0f;
    c.tmuvtx[0].tow = 255.0f;
    d.x = x0;
    d.y = 480.0f;
    d.oow = 1.0f;
    d.tmuvtx[0].sow = 0.0f;
    d.tmuvtx[0].tow = 255.0f;
    grDrawTriangle(&a, &b, &c);
    grDrawTriangle(&a, &c, &d);
  };

  auto readPixel565 = [](uint32_t x, uint32_t y) -> uint16_t {
    uint16_t px565 = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, x, y, 1u, 1u, 2u, &px565) ==
            FXTRUE);
    return px565;
  };
  auto checkRed565 = [](uint16_t px) {
    CHECK(((px >> 11) & 31u) >= 28u);
    CHECK(((px >> 5) & 63u) <= 4u);
    CHECK((px & 31u) <= 4u);
  };
  auto checkGreen565 = [](uint16_t px) {
    CHECK(((px >> 11) & 31u) <= 4u);
    CHECK(((px >> 5) & 63u) >= 58u);
    CHECK((px & 31u) <= 4u);
  };
  auto checkBlue565 = [](uint16_t px) {
    CHECK(((px >> 11) & 31u) <= 4u);
    CHECK(((px >> 5) & 63u) <= 4u);
    CHECK((px & 31u) >= 28u);
  };
  auto checkWhite565 = [](uint16_t px) {
    CHECK(((px >> 11) & 31u) >= 28u);
    CHECK(((px >> 5) & 63u) >= 58u);
    CHECK((px & 31u) >= 28u);
  };

  auto& texMgr = GlideWrapper::TextureManager::GetInstance();
  auto& telMgr = GlideWrapper::TelemetryManager::GetInstance();
  const uint32_t addr0 = grTexMinAddress(GR_TMU0);
  const uint32_t addr1 = addr0 + 0x2000u;

  GuTexPalette pal1{};
  GuTexPalette pal2{};
  for (int i = 0; i < 256; ++i) {
    pal1.data[i] = 0x00000000u;
    pal2.data[i] = 0x00000000u;
  }
  pal1.data[42] = 0x00FF0000u;  // Red
  pal1.data[77] = 0x00FFFFFFu;  // White
  pal2.data[42] = 0x0000FF00u;  // Green

  std::vector<uint8_t> p8Data(32u * 32u, 42u);
  GrTexInfo p8Info{};
  p8Info.smallLod = GR_LOD_32;
  p8Info.largeLod = GR_LOD_32;
  p8Info.aspectRatio = GR_ASPECT_1x1;
  p8Info.format = GR_TEXFMT_P_8;
  p8Info.data = p8Data.data();

  // Part A: Palette cycling P1 -> P2 -> P1 at addr0 skips CPU reswizzle on hit
  grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal1);
  grTexDownloadMipMap(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &p8Info);
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, p8Info, 0.0f, 640.0f);
  grBufferSwap(0);
  checkRed565(readPixel565(320u, 240u));

  grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal2);
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, p8Info, 0.0f, 640.0f);
  grBufferSwap(0);
  checkGreen565(readPixel565(320u, 240u));

  const uint64_t skipsBeforeA = texMgr.GetSkippedCpuDecodeCount();
  grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal1);
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, p8Info, 0.0f, 640.0f);
  grBufferSwap(0);
  const auto metricsA = telMgr.GetLastFrameMetrics();
  if (hasBackendCache) {
    CHECK(texMgr.GetSkippedCpuDecodeCount() >= skipsBeforeA + 1u);
    CHECK(metricsA.texUploads == 0u);
  } else {
    CHECK(texMgr.GetSkippedCpuDecodeCount() == skipsBeforeA);
  }
  checkRed565(readPixel565(320u, 240u));

  // Lazy CPU snapshot fallback must materialize P1 red pixels cleanly.
  auto snapA = texMgr.GetTextureSnapshot(0u, addr0);
  REQUIRE(snapA != nullptr);
  REQUIRE(snapA->swizzledMipLevels.size() == 1u);
  REQUIRE(snapA->swizzledMipLevels[0].size() == 32u * 32u);
  CHECK(snapA->swizzledMipLevels[0][0] == 0xFFFF0000u);

  // Part B: Cross-address DownloadMipMap cache hit for both P_8 and RGB_565
  std::vector<uint16_t> rgb565Blue(32u * 32u, 0x001Fu);
  GrTexInfo rgbInfo{};
  rgbInfo.smallLod = GR_LOD_32;
  rgbInfo.largeLod = GR_LOD_32;
  rgbInfo.aspectRatio = GR_ASPECT_1x1;
  rgbInfo.format = GR_TEXFMT_RGB_565;
  rgbInfo.data = rgb565Blue.data();

  grTexDownloadMipMap(GR_TMU0, addr1, GR_MIPMAPLEVELMASK_BOTH, &rgbInfo);
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, p8Info, 0.0f, 320.0f);
  drawRectTex(addr1, rgbInfo, 320.0f, 640.0f);
  grBufferSwap(0);
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, p8Info, 0.0f, 320.0f);
  drawRectTex(addr1, rgbInfo, 320.0f, 640.0f);
  grBufferSwap(0);

  const uint64_t skipsBeforeB = texMgr.GetSkippedCpuDecodeCount();
  grTexDownloadMipMap(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &rgbInfo);
  grTexDownloadMipMap(GR_TMU0, addr1, GR_MIPMAPLEVELMASK_BOTH, &p8Info);
  if (hasBackendCache) {
    CHECK(texMgr.GetSkippedCpuDecodeCount() >= skipsBeforeB + 2u);
  }
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, rgbInfo, 0.0f, 320.0f);
  drawRectTex(addr1, p8Info, 320.0f, 640.0f);
  grBufferSwap(0);
  const auto metricsB = telMgr.GetLastFrameMetrics();
  if (hasBackendCache) {
    CHECK(metricsB.texUploads == 0u);
  }
  checkBlue565(readPixel565(160u, 240u));
  checkRed565(readPixel565(480u, 240u));

  // Part C: Partial updates on lazy-cached RGB_565 & P_8 textures
  std::vector<uint16_t> rgb565Green(32u * 32u, 0x07E0u);
  std::vector<uint8_t> p8White(32u * 32u, 77u);
  REQUIRE(texMgr.DownloadMipMapPartial(0u, addr0, GR_LOD_32, GR_LOD_32,
                                       GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                       rgb565Green.data(), 0, 32));
  REQUIRE(texMgr.DownloadMipMapPartial(0u, addr1, GR_LOD_32, GR_LOD_32,
                                       GR_ASPECT_1x1, GR_TEXFMT_P_8,
                                       p8White.data(), 0, 32));
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  drawRectTex(addr0, rgbInfo, 0.0f, 320.0f);
  drawRectTex(addr1, p8Info, 320.0f, 640.0f);
  grBufferSwap(0);
  checkGreen565(readPixel565(160u, 240u));
  checkWhite565(readPixel565(480u, 240u));

  // LOD merge on a lazy-cached RGB_565 mip chain (GR_LOD_32=32x32, GR_LOD_16=16x16)
  const uint32_t addr2 = addr0 + 0x8000u;
  const uint32_t addr3 = addr0 + 0x10000u;
  std::vector<uint16_t> lod16Red(16u * 16u, 0xF800u);
  std::vector<uint16_t> lod32Green(32u * 32u, 0x07E0u);
  REQUIRE(texMgr.DownloadMipMap(0u, addr2, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
                                GR_TEXFMT_RGB_565, lod16Red.data(), false,
                                GR_LOD_32));
  grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
  GrTexInfo lod16Info{};
  lod16Info.smallLod = GR_LOD_16;
  lod16Info.largeLod = GR_LOD_16;
  lod16Info.aspectRatio = GR_ASPECT_1x1;
  lod16Info.format = GR_TEXFMT_RGB_565;
  lod16Info.data = lod16Red.data();
  drawRectTex(addr2, lod16Info, 0.0f, 640.0f);
  grBufferSwap(0);

  REQUIRE(texMgr.DownloadMipMap(0u, addr3, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
                                GR_TEXFMT_RGB_565, lod16Red.data(), false,
                                GR_LOD_32));
  REQUIRE(texMgr.DownloadMipMap(0u, addr3, GR_LOD_32, GR_LOD_32, GR_ASPECT_1x1,
                                GR_TEXFMT_RGB_565, lod32Green.data(), false,
                                GR_LOD_32));
  auto mergedSnap = texMgr.GetTextureSnapshot(0u, addr3);
  REQUIRE(mergedSnap != nullptr);
  REQUIRE(mergedSnap->swizzledMipLevels.size() == 2u);
  REQUIRE(mergedSnap->swizzledMipLevels[0].size() == 32u * 32u);
  REQUIRE(mergedSnap->swizzledMipLevels[1].size() == 16u * 16u);
  CHECK(mergedSnap->swizzledMipLevels[0][0] == 0xFF00FF00u);
  CHECK(mergedSnap->swizzledMipLevels[1][0] == 0xFFFF0000u);

  grSstWinClose();
  grGlideShutdown();
#endif
}

TEST_CASE(
    "Glide 2.x: Live sampler/LOD bias after address switching and headless "
    "multi-frame swap sync",
    "[Glide2][Texture][SamplerSync]") {
#ifdef __linux__
  struct EnvGuard {
    bool hadBe;
    std::string beVal;
    bool hadHl;
    std::string hlVal;
    bool hadSplash;
    std::string splashVal;
    ~EnvGuard() {
      if (hadBe)
        ::setenv("GLIDE_NG_BACKEND", beVal.c_str(), 1);
      else
        ::unsetenv("GLIDE_NG_BACKEND");
      if (hadHl)
        ::setenv("GLIDE_NG_HEADLESS", hlVal.c_str(), 1);
      else
        ::unsetenv("GLIDE_NG_HEADLESS");
      if (hadSplash)
        ::setenv("FX_GLIDE_NO_SPLASH", splashVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_NO_SPLASH");
    }
  } envGuard{std::getenv("GLIDE_NG_BACKEND") != nullptr,
             std::getenv("GLIDE_NG_BACKEND") ? std::getenv("GLIDE_NG_BACKEND")
                                             : "",
             std::getenv("GLIDE_NG_HEADLESS") != nullptr,
             std::getenv("GLIDE_NG_HEADLESS") ? std::getenv("GLIDE_NG_HEADLESS")
                                              : "",
             std::getenv("FX_GLIDE_NO_SPLASH") != nullptr,
             std::getenv("FX_GLIDE_NO_SPLASH")
                 ? std::getenv("FX_GLIDE_NO_SPLASH")
                 : ""};

  ::setenv("GLIDE_NG_HEADLESS", "1", 1);
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);

  for (const char* be : {"software", "vulkan", "opengl_es"}) {
    INFO("Backend: " << be);
    ::setenv("GLIDE_NG_BACKEND", be, 1);

    grGlideInit();
    grSstSelect(0);
    if (grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                     GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) !=
        FXTRUE) {
      grGlideShutdown();
      continue;
    }

    grDitherMode(GR_DITHER_DISABLE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);

    // 1. Alternating 1-texel Red/Blue stripes at addr0 + solid Blue at addr1
    std::vector<uint16_t> stripePixels(32u * 32u);
    for (size_t i = 0; i < stripePixels.size(); ++i) {
      stripePixels[i] = (i & 1u) ? 0xF800u : 0x001Fu;
    }
    std::vector<uint16_t> bluePixels(32u * 32u, 0x001Fu);

    GrTexInfo stripeInfo{};
    stripeInfo.smallLod = GR_LOD_32;
    stripeInfo.largeLod = GR_LOD_32;
    stripeInfo.aspectRatio = GR_ASPECT_1x1;
    stripeInfo.format = GR_TEXFMT_RGB_565;
    stripeInfo.data = stripePixels.data();

    GrTexInfo blueInfo = stripeInfo;
    blueInfo.data = bluePixels.data();

    const uint32_t addr0 = grTexMinAddress(GR_TMU0);
    const uint32_t addr1 = addr0 + 0x2000u;
    const uint32_t addr2 = addr0 + 0x4000u;
    grTexDownloadMipMap(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &stripeInfo);
    grTexDownloadMipMap(GR_TMU0, addr1, GR_MIPMAPLEVELMASK_BOTH, &blueInfo);

    auto drawQuad = [](float x0, float y0, float x1, float y1) {
      GrVertex a{}, b{}, c{}, d{};
      a.x = x0;
      a.y = y0;
      a.oow = 1.0f;
      a.tmuvtx[0].sow = 0.0f;
      a.tmuvtx[0].tow = 0.0f;
      b.x = x1;
      b.y = y0;
      b.oow = 1.0f;
      b.tmuvtx[0].sow = 255.0f;
      b.tmuvtx[0].tow = 0.0f;
      c.x = x1;
      c.y = y1;
      c.oow = 1.0f;
      c.tmuvtx[0].sow = 255.0f;
      c.tmuvtx[0].tow = 255.0f;
      d.x = x0;
      d.y = y1;
      d.oow = 1.0f;
      d.tmuvtx[0].sow = 0.0f;
      d.tmuvtx[0].tow = 255.0f;
      grDrawTriangle(&a, &b, &c);
      grDrawTriangle(&a, &c, &d);
    };

    // Rapid address switches under POINT_SAMPLED, then toggle live BILINEAR
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);

    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grTexSource(GR_TMU0, addr1, GR_MIPMAPLEVELMASK_BOTH, &blueInfo);
    drawQuad(0.0f, 0.0f, 640.0f, 480.0f);
    grTexSource(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &stripeInfo);
    drawQuad(0.0f, 0.0f, 640.0f, 480.0f);

    std::vector<uint16_t> fb(640u * 480u, 0u);
    auto countBlended = [&](int x0, int y0, int x1, int y1) {
      int blended = 0;
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          const uint16_t px = fb[static_cast<size_t>(y) * 640u + x];
          const int r = static_cast<int>(((px >> 11) & 31u) * 255u / 31u);
          const int b = static_cast<int>((px & 31u) * 255u / 31u);
          if (r > 35 && b > 35) ++blended;
        }
      }
      return blended;
    };

    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 2,
                            fb.data()) == FXTRUE);
    CHECK(countBlended(100, 100, 540, 380) == 0);

    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR,
                    GR_TEXTUREFILTER_BILINEAR);
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    grTexSource(GR_TMU0, addr1, GR_MIPMAPLEVELMASK_BOTH, &blueInfo);
    drawQuad(0.0f, 0.0f, 640.0f, 480.0f);
    grTexSource(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &stripeInfo);
    drawQuad(0.0f, 0.0f, 640.0f, 480.0f);
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 2,
                            fb.data()) == FXTRUE);
    CHECK(countBlended(100, 100, 540, 380) > 10000);

    // 2. Live grTexLodBiasValue switch after mipmapped texture binding
    // Build combined mip chain: LOD 32 (32x32 Red) + LOD 16 (16x16 Green)
    std::vector<uint16_t> mipChain(32u * 32u + 16u * 16u);
    std::fill_n(mipChain.data(), 32u * 32u, uint16_t{0xF800u});
    std::fill_n(mipChain.data() + 32u * 32u, 16u * 16u, uint16_t{0x07E0u});
    GrTexInfo mipInfo{};
    mipInfo.smallLod = GR_LOD_16;
    mipInfo.largeLod = GR_LOD_32;
    mipInfo.aspectRatio = GR_ASPECT_1x1;
    mipInfo.format = GR_TEXFMT_RGB_565;
    mipInfo.data = mipChain.data();
    grTexDownloadMipMap(GR_TMU0, addr2, GR_MIPMAPLEVELMASK_BOTH, &mipInfo);

    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexSource(GR_TMU0, addr2, GR_MIPMAPLEVELMASK_BOTH, &mipInfo);

    grTexLodBiasValue(GR_TMU0, -2.0f);
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    drawQuad(100.0f, 100.0f, 132.0f, 132.0f);  // 1:1 -> LOD 0 (Red)
    uint16_t pxLod = 0u;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 116, 116, 1, 1, 2, &pxLod) ==
            FXTRUE);
    CHECK(((pxLod >> 11) & 31u) >= 28u);
    CHECK(((pxLod >> 5) & 63u) <= 4u);

    grTexLodBiasValue(GR_TMU0, 2.0f);
    grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    drawQuad(100.0f, 100.0f, 132.0f, 132.0f);  // Biased +2 -> LOD 1 (Green)
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 116, 116, 1, 1, 2, &pxLod) ==
            FXTRUE);
    CHECK(((pxLod >> 11) & 31u) <= 4u);
    CHECK(((pxLod >> 5) & 63u) >= 58u);
    grTexLodBiasValue(GR_TMU0, 0.0f);

    // 3. 6-frame headless SwapBuffers + Frontbuffer readback (validates GLsync ring)
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    struct ExpectedFrame {
      uint32_t argb;
      uint32_t minR, maxR, minG, maxG, minB, maxB;
    };
    const ExpectedFrame frames[6] = {
        {0xFFFF0000u, 28, 31, 0, 4, 0, 4},   // Red
        {0xFF00FF00u, 0, 4, 58, 63, 0, 4},   // Green
        {0xFF0000FFu, 0, 4, 0, 4, 28, 31},   // Blue
        {0xFFFFFF00u, 28, 31, 58, 63, 0, 4}, // Yellow
        {0xFFFF00FFu, 28, 31, 0, 4, 28, 31}, // Magenta
        {0xFF00FFFFu, 0, 4, 58, 63, 28, 31}, // Cyan
    };
    for (int f = 0; f < 6; ++f) {
      grBufferClear(0xFF000000u, 0, GR_WDEPTHVALUE_FARTHEST);
      grConstantColorValue(frames[f].argb);
      drawQuad(0.0f, 0.0f, 640.0f, 480.0f);
      grBufferSwap(0);
      uint16_t px = 0u;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 2, &px) ==
              FXTRUE);
      const uint32_t r = (px >> 11) & 31u;
      const uint32_t g = (px >> 5) & 63u;
      const uint32_t b = px & 31u;
      CHECK(r >= frames[f].minR);
      CHECK(r <= frames[f].maxR);
      CHECK(g >= frames[f].minG);
      CHECK(g <= frames[f].maxG);
      CHECK(b >= frames[f].minB);
      CHECK(b <= frames[f].maxB);
    }

    grSstWinClose();
    grGlideShutdown();
  }
#endif
}

