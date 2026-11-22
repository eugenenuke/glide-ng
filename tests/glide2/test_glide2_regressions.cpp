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

TEST_CASE("Glide 2.x: Legacy Texture Budget & Palette Churn Regressions", "[Glide2][Regression]") {
#ifdef __linux__
  setenv("GLIDE_NG_FORCE_NO_WINDOW", "1", 1);
  setenv("GLIDE_NG_SPLASH", "0", 1);
  setenv("GLIDE_NG_SHAMELESS_PLUG", "0", 1);

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXTRUE);

  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grCullMode(GR_CULL_DISABLE);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  grAlphaTestFunction(GR_CMP_ALWAYS);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grFogMode(GR_FOG_DISABLE);
  grColorMask(FXTRUE, FXTRUE);
  grDepthMask(FXTRUE);

  auto drawColoredQuad = [](float x0, float y0, float x1, float y1, float r,
                            float g, float b, float oow, float tmu0Oow,
                            float ooz = 0.5f) {
    GrVertex v0{}, v1{}, v2{}, v3{};
    auto initV = [&](GrVertex& v, float x, float y, float s, float t) {
      v.x = x;
      v.y = y;
      v.z = ooz;
      v.ooz = ooz;
      v.r = r;
      v.g = g;
      v.b = b;
      v.a = 255.0f;
      v.oow = oow;
      v.tmuvtx[0].sow = s;
      v.tmuvtx[0].tow = t;
      v.tmuvtx[0].oow = tmu0Oow;
    };
    initV(v0, x0, y0, 0.0f, 0.0f);
    initV(v1, x1, y0, 255.0f, 0.0f);
    initV(v2, x1, y1, 255.0f, 255.0f);
    initV(v3, x0, y1, 0.0f, 255.0f);
    grDrawTriangle(&v0, &v1, &v2);
    grDrawTriangle(&v0, &v2, &v3);
  };

  SECTION(
      "ITRGB_DELTA0 and TEXTURE_TIMES_ITRGB_DELTA0 use constant color, not "
      "iterated RGB") {
    grBufferClear(0x00000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grConstantColorValue(0xFF0080FFu);  // R=0, G=128, B=255
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB_DELTA0);
    // Deliberately pass bright magenta vertex RGB (255, 0, 255) — must be
    // ignored in favor of constantColor (0, 128, 255).
    drawColoredQuad(40.0f, 40.0f, 160.0f, 160.0f, 255.0f, 0.0f, 255.0f, 1.0f,
                    1.0f);

    // Also verify GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0 with a white
    // texture multiplied by constantColor (0, 255, 0), ignoring magenta vertex
    // RGB.
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    GuTexPalette pal{};
    pal.data[1] = 0x00FFFFFFu;  // White (no top-byte alpha!)
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    std::vector<uint8_t> white16(16 * 16, 1);
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = white16.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    grConstantColorValue(0xFF00FF00u);  // Pure green
    guColorCombineFunction(GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0);
    drawColoredQuad(240.0f, 40.0f, 360.0f, 160.0f, 255.0f, 0.0f, 255.0f, 1.0f,
                    1.0f);

    grBufferSwap(0);

    uint32_t pxFlat = 0, pxTex = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 100, 100, 1, 1, 4,
                            &pxFlat) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 300, 100, 1, 1, 4, &pxTex) ==
            FXTRUE);

    // Flat ITRGB_DELTA0 -> constantColor (0, 128, 255)
    CHECK(((pxFlat >> 16) & 0xFFu) <= 10);
    CHECK(((pxFlat >> 8) & 0xFFu) >= 118);
    CHECK(((pxFlat >> 8) & 0xFFu) <= 138);
    CHECK((pxFlat & 0xFFu) >= 240);

    // TEXTURE_TIMES_ITRGB_DELTA0 -> white * green = (0, 255, 0)
    CHECK(((pxTex >> 16) & 0xFFu) <= 10);
    CHECK(((pxTex >> 8) & 0xFFu) >= 240);
    CHECK((pxTex & 0xFFu) <= 10);
  }

  SECTION(
      "guTexSource(GR_NULL_MIPMAP_HANDLE) unbinds TMU0 so untextured oow=0 "
      "quads render cleanly") {
    // First bind a real texture on TMU0 (simulating previous frame's state).
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    GuTexPalette pal{};
    pal.data[1] = 0x00FF0000u;
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    std::vector<uint8_t> red16(16 * 16, 1);
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = red16.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);

    // Clear to bright yellow so any discarded sky quad is immediately visible.
    grBufferClear(0x00FFFF00u, 0xFF, GR_WDEPTHVALUE_FARTHEST);

    // Replicate Carmageddon's sky-quad setup: unbind TMU0 via guTexSource(NULL)
    // and draw a dark-blue untextured quad with v->oow == 0.0f.
    guTexSource(GR_NULL_MIPMAP_HANDLE);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_ZERO);
    grConstantColorValue(0xFF000040u);  // Dark blue
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB_DELTA0);
    drawColoredQuad(50.0f, 50.0f, 300.0f, 200.0f, 255.0f, 255.0f, 0.0f,
                    /*oow=*/0.0f, /*tmu0Oow=*/0.0f);

    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 150, 120, 1, 1, 4, &px) ==
            FXTRUE);
    CHECK(((px >> 16) & 0xFFu) <= 10);
    CHECK(((px >> 8) & 0xFFu) <= 10);
    CHECK((px & 0xFFu) >= 54);
    CHECK((px & 0xFFu) <= 74);
  }

  SECTION(
      "GR_TEXTABLE_PALETTE forces opaque alpha for P_8 and STW_HINT_W_DIFF_TMU0 "
      "renders with v->oow == 0") {
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    GuTexPalette pal{};
    // Top byte intentionally 0x00 (standard Glide 24-bit 0x00RRGGBB palette).
    pal.data[1] = 0x0000FF80u;  // R=0, G=255, B=128
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    std::vector<uint8_t> tex16(16 * 16, 1);
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;
    ti.data = tex16.data();
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    guColorCombineFunction(GR_COLORCOMBINE_DECAL_TEXTURE);

    // Use texture alpha for standard alpha blending — if P_8 palette didn't
    // force 0xFF alpha, this quad would blend to 0% (invisible).
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);

    // Also enable table fog with an all-zero fog table and pass v->oow == 0.0f
    // with GR_STWHINT_W_DIFF_TMU0 (tmuvtx[0].oow == 1.0f) to verify neither
    // near-plane clipping nor fog 1/oow discards or NaN-corrupts the quad.
    GrFog_t fogTbl[GR_FOG_TABLE_SIZE] = {};
    grFogTable(fogTbl);
    grFogColorValue(0x00FF0000u);
    grFogMode(GR_FOG_WITH_TABLE);
    grHints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);

    grBufferClear(0x00FF0000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    drawColoredQuad(100.0f, 100.0f, 260.0f, 260.0f, 0.0f, 0.0f, 0.0f,
                    /*oow=*/0.0f, /*tmu0Oow=*/1.0f);

    grHints(GR_HINT_STWHINT, 0);
    grFogMode(GR_FOG_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 180, 180, 1, 1, 4, &px) ==
            FXTRUE);
    CHECK(((px >> 16) & 0xFFu) <= 10);
    CHECK(((px >> 8) & 0xFFu) >= 240);
    CHECK((px & 0xFFu) >= 118);
    CHECK((px & 0xFFu) <= 138);
  }

  SECTION("grBufferClear obeys grDepthMask(FXFALSE) and grColorMask(FXFALSE)") {
    guTexSource(GR_NULL_MIPMAP_HANDLE);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_ZERO);
    guColorCombineFunction(GR_COLORCOMBINE_CCRGB);
    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);
    grColorMask(FXTRUE, FXTRUE);

    // Clear color to black and depth to far (1.0), then draw a near red quad at
    // Z=0.2 so depth buffer holds 0.2.
    grBufferClear(0x00000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    grConstantColorValue(0xFFFF0000u);  // Red
    drawColoredQuad(100.0f, 100.0f, 260.0f, 260.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                    1.0f, /*ooz=*/0.2f);

    // Disable BOTH color and depth write masks, then call grBufferClear to blue
    // + far depth. Neither color (red) nor depth (0.2) should be overwritten!
    grColorMask(FXFALSE, FXFALSE);
    grDepthMask(FXFALSE);
    grBufferClear(0x000000FFu, 0xFF, GR_WDEPTHVALUE_FARTHEST);

    // Re-enable color writes and draw a farther green quad at Z=0.6.
    // Because depth (0.2) was NOT cleared, 0.6 < 0.2 is FALSE -> green must be
    // rejected and red must remain visible!
    grColorMask(FXTRUE, FXTRUE);
    grConstantColorValue(0xFF00FF00u);  // Green
    drawColoredQuad(100.0f, 100.0f, 260.0f, 260.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                    1.0f, /*ooz=*/0.6f);

    grDepthMask(FXTRUE);
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 180, 180, 1, 1, 4, &px) ==
            FXTRUE);
    CHECK(((px >> 16) & 0xFFu) >= 240);  // Red preserved!
    CHECK(((px >> 8) & 0xFFu) <= 10);
    CHECK((px & 0xFFu) <= 10);
  }

  SECTION("Glide 2.x TMU memory queries honor configured tmuMemoryMb") {
    const auto& cfg =
        GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
    const FxU32 expectedBytes =
        std::max(2u, cfg.tmuMemoryMb) * 1024u * 1024u;
    CHECK(grTexMaxAddress(GR_TMU0) == expectedBytes);
    CHECK(guTexMemQueryAvail(GR_TMU0) == expectedBytes);
    FxI32 reportedMem = 0;
    grGet(GR_MEMORY_TMU, sizeof(reportedMem), &reportedMem);
    CHECK(static_cast<FxU32>(reportedMem) == expectedBytes);
  }

  grSstWinClose();
  grGlideShutdown();
#endif
}

TEST_CASE("Glide 2.x: Palette Persistence, W-Buffer & Depth Bias Regressions", "[Glide2][Regression][WBuffer]") {
#ifdef __linux__
  ::setenv("GLIDE_NG_FORCE_NO_WINDOW", "1", 1);
  ::setenv("GLIDE_NG_SPLASH", "0", 1);
  ::setenv("GLIDE_NG_SHAMELESS_PLUG", "0", 1);

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) ==
          FXTRUE);

  auto makeQuadVert = [](float x, float y, float oow, float s, float t,
                         float r, float g, float b) {
    GrVertex v{};
    v.x = x;
    v.y = y;
    v.ooz = 0.0f;
    v.oow = oow;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = 255.0f;
    v.tmuvtx[0].sow = s * oow;
    v.tmuvtx[0].tow = t * oow;
    v.tmuvtx[0].oow = oow;
    return v;
  };

  auto drawPolyQuad = [&](float x0, float y0, float x1, float y1, float oow,
                          float r = 255.0f, float g = 255.0f,
                          float b = 255.0f) {
    GrVertex q[4] = {
        makeQuadVert(x0, y0, oow, 0.0f, 0.0f, r, g, b),
        makeQuadVert(x1, y0, oow, 255.0f, 0.0f, r, g, b),
        makeQuadVert(x1, y1, oow, 255.0f, 255.0f, r, g, b),
        makeQuadVert(x0, y1, oow, 0.0f, 255.0f, r, g, b),
    };
    grDrawPolygonVertexList(4, q);
  };

  SECTION("guTexMemReset preserves TMU palette for subsequent P_8 textures") {
    // Replicate Tomb Raider 1 startup / level load:
    // grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE) -> guTexMemReset() ->
    // guTexAllocateMemory / guTexDownloadMipMap(P_8).
    GuTexPalette pal{};
    pal.data[1] = 0x00FF8020u;  // Bright orange (R=255, G=128, B=32)
    pal.data[2] = 0x002080FFu;  // Bright blue   (R=32,  G=128, B=255)
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    guTexMemReset();
    CHECK(guTexGetCurrentMipMap(GR_TMU0) == GR_NULL_MIPMAP_HANDLE);

    // 16x16 P_8 texture: left half index 1 (orange), right half index 2 (blue)
    std::vector<uint8_t> texBuf(16 * 16, 1);
    for (int y = 0; y < 16; ++y) {
      for (int x = 8; x < 16; ++x) {
        texBuf[static_cast<size_t>(y) * 16 + x] = 2;
      }
    }

    GrMipMapId_t mmid = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_P_8,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    REQUIRE(mmid != GR_NULL_MIPMAP_HANDLE);
    guTexDownloadMipMap(mmid, texBuf.data(), nullptr);
    guTexSource(mmid);

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    guColorCombineFunction(GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB);
    grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    drawPolyQuad(100.0f, 100.0f, 300.0f, 200.0f, 1.0f);
    grBufferSwap(0);

    uint32_t pxLeft = 0, pxRight = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 150, 150, 1, 1, 4,
                            &pxLeft) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 250, 150, 1, 1, 4,
                            &pxRight) == FXTRUE);

    // Left must be bright orange (NOT grayscale 1,1,1)
    CHECK(((pxLeft >> 16) & 0xFFu) >= 240);
    CHECK(((pxLeft >> 8) & 0xFFu) >= 118);
    CHECK(((pxLeft >> 8) & 0xFFu) <= 138);
    CHECK((pxLeft & 0xFFu) >= 20);
    CHECK((pxLeft & 0xFFu) <= 45);

    // Right must be bright blue (NOT grayscale 2,2,2)
    CHECK(((pxRight >> 16) & 0xFFu) >= 20);
    CHECK(((pxRight >> 16) & 0xFFu) <= 45);
    CHECK(((pxRight >> 8) & 0xFFu) >= 118);
    CHECK(((pxRight >> 8) & 0xFFu) <= 138);
    CHECK((pxRight & 0xFFu) >= 240);
  }

  SECTION(
      "W-buffer oow > 1.0 renders background, 3D ring item, and text overlay "
      "with accurate perspective UV division") {
    guTexMemReset();
    // 16x16 RGB565 texture: left half green (0x07E0), right half red (0xF800)
    std::vector<uint16_t> bgTex(16 * 16, 0x07E0u);
    for (int y = 0; y < 16; ++y) {
      for (int x = 8; x < 16; ++x) {
        bgTex[static_cast<size_t>(y) * 16 + x] = 0xF800u;
      }
    }
    GrMipMapId_t mmBg = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    REQUIRE(mmBg != GR_NULL_MIPMAP_HANDLE);
    guTexDownloadMipMap(mmBg, bgTex.data(), nullptr);

    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthBiasLevel(0);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    // Pass 1: 2D background tile with grDepthMask(FXFALSE) and oow = 16.0 > 1.0
    grDepthMask(FXFALSE);
    guColorCombineFunction(GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB);
    guTexSource(mmBg);
    drawPolyQuad(40.0f, 40.0f, 600.0f, 440.0f, 16.0f);

    // Pass 2: 3D ring object at W = 20.0 (oow = 0.05) with grDepthMask(FXTRUE)
    grDepthMask(FXTRUE);
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
    drawPolyQuad(180.0f, 140.0f, 460.0f, 340.0f, 0.05f, 0.0f, 0.0f, 255.0f);

    // Pass 3: 2D menu text label overlay at oow = 65535.0 (> 1.0 -> depth 0.0)
    // overlapping the 3D object; must pass GR_CMP_LESS against W=20.0!
    drawPolyQuad(260.0f, 200.0f, 380.0f, 280.0f, 65535.0f, 255.0f, 255.0f,
                 0.0f);

    grBufferSwap(0);

    uint32_t pxBgLeft = 0, pxBgRight = 0, pxObj = 0, pxText = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 100, 240, 1, 1, 4,
                            &pxBgLeft) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 540, 240, 1, 1, 4,
                            &pxBgRight) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 210, 240, 1, 1, 4, &pxObj) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4,
                            &pxText) == FXTRUE);

    // Background left half = Green, right half = Red (proves sow/oow exact!)
    CHECK(((pxBgLeft >> 16) & 0xFFu) <= 10);
    CHECK(((pxBgLeft >> 8) & 0xFFu) >= 240);
    CHECK((pxBgLeft & 0xFFu) <= 10);

    CHECK(((pxBgRight >> 16) & 0xFFu) >= 240);
    CHECK(((pxBgRight >> 8) & 0xFFu) <= 10);
    CHECK((pxBgRight & 0xFFu) <= 10);

    // 3D ring object = Blue (proves Pass 1 grDepthMask(0) didn't block 3D!)
    CHECK(((pxObj >> 16) & 0xFFu) <= 10);
    CHECK(((pxObj >> 8) & 0xFFu) <= 10);
    CHECK((pxObj & 0xFFu) >= 240);

    // 2D text overlay = Yellow (proves oow = 65535 renders in front of W = 20!)
    CHECK(((pxText >> 16) & 0xFFu) >= 240);
    CHECK(((pxText >> 8) & 0xFFu) >= 240);
    CHECK((pxText & 0xFFu) <= 10);
  }

  SECTION("W-buffer grDepthBiasLevel pulls coplanar decals/shadows closer") {
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);
    grDepthBiasLevel(0);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
    grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    // Base floor at W = 100.0 (oow = 0.01) in Green
    drawPolyQuad(100.0f, 100.0f, 500.0f, 380.0f, 0.01f, 0.0f, 255.0f, 0.0f);

    // Coplanar shadow/decal at W = 100.0 with negative depthBias (-32) in Red
    grDepthBiasLevel(-32);
    drawPolyQuad(150.0f, 150.0f, 300.0f, 300.0f, 0.01f, 255.0f, 0.0f, 0.0f);

    // Coplanar quad at W = 100.0 with positive depthBias (+32) in Blue -> must
    // fail GR_CMP_LESS against the floor
    grDepthBiasLevel(32);
    drawPolyQuad(320.0f, 150.0f, 450.0f, 300.0f, 0.01f, 0.0f, 0.0f, 255.0f);
    grDepthBiasLevel(0);

    grBufferSwap(0);

    uint32_t pxNegBias = 0, pxPosBias = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 225, 225, 1, 1, 4,
                            &pxNegBias) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 385, 225, 1, 1, 4,
                            &pxPosBias) == FXTRUE);

    // Negative bias wins -> Red
    CHECK(((pxNegBias >> 16) & 0xFFu) >= 240);
    CHECK(((pxNegBias >> 8) & 0xFFu) <= 10);
    CHECK((pxNegBias & 0xFFu) <= 10);

    // Positive bias loses -> remains Green floor
    CHECK(((pxPosBias >> 16) & 0xFFu) <= 10);
    CHECK(((pxPosBias >> 8) & 0xFFu) >= 240);
    CHECK((pxPosBias & 0xFFu) <= 10);
  }

  SECTION(
      "Chromakey with bound TMU texture does not punch holes in untextured "
      "GR_COLORCOMBINE_ITRGB polygons") {
    guTexMemReset();
    GuTexPalette pal{};
    pal.data[0] = 0x00604020u;  // Tomb Raider 1 transparent key color
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);

    std::vector<uint8_t> keyTex(16 * 16, 0);  // 100% chromakey color!
    GrMipMapId_t mmKey = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_P_8,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    REQUIRE(mmKey != GR_NULL_MIPMAP_HANDLE);
    guTexDownloadMipMap(mmKey, keyTex.data(), nullptr);
    guTexSource(mmKey);  // Keep TMU0 bound!

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grConstantColorValue(0x7F000000u);  // Black RGB, half alpha (TR1 default)
    grChromakeyValue(0x00604020u);
    grChromakeyMode(GR_CHROMAKEY_ENABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    // Switch to untextured Gouraud (Function=LOCAL, Local=ITERATED,
    // Other=CONSTANT) while TMU0 still holds the 0x604020 texture.
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
    grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    drawPolyQuad(150.0f, 150.0f, 350.0f, 300.0f, 1.0f, 240.0f, 160.0f, 80.0f);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 250, 225, 1, 1, 4, &px) ==
            FXTRUE);
    CHECK(((px >> 16) & 0xFFu) >= 225);
    CHECK(((px >> 8) & 0xFFu) >= 145);
    CHECK(((px >> 8) & 0xFFu) <= 175);
    CHECK((px & 0xFFu) >= 65);
    CHECK((px & 0xFFu) <= 95);
  }

  SECTION(
      "grDrawLine W-buffer depth test and write preserves TR1 health bar "
      "against later black background lines") {
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);
    grDepthBiasLevel(0);
    grChromakeyMode(GR_CHROMAKEY_ENABLE);
    grChromakeyValue(0x00604020u);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_CONSTANT,
                   FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_CONSTANT,
                   FXFALSE);
    grBufferClear(0xFF202080u, 0, GR_WDEPTHVALUE_FARTHEST);

    auto drawHLine = [](float x0, float x1, float y, FxU32 color) {
      grConstantColorValue(color);
      GrVertex a{}, b{};
      a.x = x0;
      a.y = y;
      a.z = 0.0f;
      a.oow = 81.92f;
      b.x = x1;
      b.y = y;
      b.z = 0.0f;
      b.oow = 81.92f;
      grDrawLine(&a, &b);
    };

    // Exact Tomb Raider 1 sequence: yellow-gold filled health lines first,
    // then full-width black background lines at identical oow = 81.92.
    for (int y = 8; y <= 12; ++y) {
      drawHLine(8.0f, 68.0f, static_cast<float>(y), 0x00A47848u);
    }
    for (int y = 7; y <= 13; ++y) {
      drawHLine(7.0f, 109.0f, static_cast<float>(y), 0x00000000u);
    }
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grBufferSwap(0);

    // Interior of filled health bar must remain yellowish-gold (NOT overwritten
    // by the later black lines since 0.0 < 0.0 fails GR_CMP_LESS).
    uint32_t pxFill = 0;
    uint32_t pxEmpty = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 30, 10, 1, 1, 4, &pxFill) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 90, 10, 1, 1, 4, &pxEmpty) ==
            FXTRUE);

    // GR_COLORFORMAT_ARGB -> R=0xA4(164), G=0x78(120), B=0x48(72)
    CHECK(((pxFill >> 16) & 0xFFu) >= 145);
    CHECK(((pxFill >> 8) & 0xFFu) >= 100);
    CHECK(((pxFill >> 8) & 0xFFu) <= 140);
    CHECK((pxFill & 0xFFu) >= 55);
    CHECK((pxFill & 0xFFu) <= 90);

    // Unfilled right portion of health bar must be black.
    CHECK(((pxEmpty >> 16) & 0xFFu) <= 10);
    CHECK(((pxEmpty >> 8) & 0xFFu) <= 10);
    CHECK((pxEmpty & 0xFFu) <= 10);
  }

  SECTION(
      "Perspective mipmap edge block never spikes to 1x1 mip via "
      "out-of-triangle block-center W extrapolation") {
    guTexMemReset();
    // Build a 256x256 RGB565 mip chain (9 levels: 256..1):
    //   mip 0..2: bright GREEN (0x07E0)
    //   mip 3..8: pure RED (0xF800) — must NOT be sampled at 1:1 edge pixel!
    std::vector<uint16_t> mipChain;
    for (int lvl = 0; lvl <= 8; ++lvl) {
      const size_t dim = static_cast<size_t>(256u >> lvl);
      const uint16_t color = (lvl <= 2) ? 0x07E0u : 0xF800u;
      mipChain.insert(mipChain.end(), dim * dim, color);
    }
    GrTexInfo info{};
    info.smallLod = GR_LOD_1;
    info.largeLod = GR_LOD_256;
    info.aspectRatio = GR_ASPECT_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = mipChain.data();

    const FxU32 texAddr = grTexMinAddress(GR_TMU0);
    grTexDownloadMipMap(GR_TMU0, texAddr, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, texAddr, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR,
                    GR_TEXTUREFILTER_BILINEAR);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    grTexLodBiasValue(GR_TMU0, 0.0f);

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grBufferClear(0xFF000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    // Pixel (104, 100) lies strictly inside this steep perspective triangle
    // (1 texel/pixel -> LOD ~0, GREEN), while the 2x4 SIMD block center
    // (105.5, 100.5) extrapolates outside the triangle to divW ~ 3.5e-6.
    GrVertex a{}, b{}, c{};
    a.x = 100.0f;
    a.y = 100.0f;
    a.oow = 0.002f;
    a.tmuvtx[0].sow = 0.0f;
    a.tmuvtx[0].tow = 0.0f;

    b.x = 105.0f;
    b.y = 100.0f;
    b.oow = 0.000185f;
    b.tmuvtx[0].sow = 5.0f * b.oow;
    b.tmuvtx[0].tow = 0.0f;

    c.x = 100.0f;
    c.y = 120.0f;
    c.oow = 0.002f;
    c.tmuvtx[0].sow = 0.0f;
    c.tmuvtx[0].tow = 20.0f * c.oow;

    grDrawTriangle(&a, &b, &c);
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 104, 100, 1, 1, 4, &px) ==
            FXTRUE);
    CHECK(((px >> 8) & 0xFFu) >= 240);   // GREEN (low mip)
    CHECK(((px >> 16) & 0xFFu) <= 15);   // NOT RED (no 1x1 mip spike!)
  }

  grSstWinClose();
  grGlideShutdown();
#endif
}

TEST_CASE("Glide 2.x: Combiner-Gated TMU Activation & Subnormal OOW", "[Glide2][Regression][Combiner]") {
#ifdef __linux__
  const char* prevBackend = std::getenv("GLIDE_NG_BACKEND");
  std::string savedBackend = prevBackend ? prevBackend : "";
  const bool hadPrevBackend = (prevBackend != nullptr);

  ::setenv("GLIDE_NG_FORCE_NO_WINDOW", "1", 1);
  ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);

  int testedBackends = 0;
  for (const char* backendName : {"software", "vulkan", "opengl_es"}) {
    ::setenv("GLIDE_NG_BACKEND", backendName, 1);
    grGlideInit();
    grSstSelect(0);
    if (grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                     GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                     1) == FXFALSE) {
      grGlideShutdown();
      continue;
    }
    ++testedBackends;

    // Upload a uniform 16x16 ALPHA_8 texture with alpha = 0x80 (~50%).
    std::vector<uint8_t> alphaTex(16 * 16, 0x80u);
    GrTexInfo tInfo{};
    tInfo.smallLod = GR_LOD_16;
    tInfo.largeLod = GR_LOD_16;
    tInfo.aspectRatio = GR_ASPECT_1x1;
    tInfo.format = GR_TEXFMT_ALPHA_8;
    tInfo.data = alphaTex.data();
    const FxU32 addr = grTexMinAddress(GR_TMU0);
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &tInfo);
    grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &tInfo);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grCullMode(GR_CULL_DISABLE);
    grFogMode(GR_FOG_DISABLE);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    grColorMask(FXTRUE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE,
                   FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_CONSTANT,
                   FXFALSE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grBufferClear(0xFF000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    auto drawQuad = [](float x0, float y0, float x1, float y1, float r, float g,
                       float b, float a, float oow) {
      auto mk = [&](float x, float y, float s, float t) {
        GrVertex v{};
        v.x = x;
        v.y = y;
        v.oow = oow;
        v.r = r;
        v.g = g;
        v.b = b;
        v.a = a;
        v.tmuvtx[0].sow = s * oow;
        v.tmuvtx[0].tow = t * oow;
        return v;
      };
      GrVertex q[4] = {
          mk(x0, y0, 0.0f, 0.0f),
          mk(x1, y0, 255.0f, 0.0f),
          mk(x1, y1, 255.0f, 255.0f),
          mk(x0, y1, 0.0f, 255.0f),
      };
      grDrawPolygonVertexList(4, q);
    };

    // Untextured sky-gradient style strips with stale grTexSource + stale
    // grAlphaCombine(..., TEXTURE) and subnormal / zero / negative vertex oow.
    drawQuad(0.0f, 0.0f, 300.0f, 100.0f, 20.0f, 60.0f, 200.0f, 255.0f,
             6.63809e-39f);
    drawQuad(0.0f, 120.0f, 100.0f, 220.0f, 20.0f, 60.0f, 200.0f, 255.0f, 0.0f);
    drawQuad(0.0f, 240.0f, 100.0f, 340.0f, 20.0f, 60.0f, 200.0f, 255.0f, -1.0f);

    // Without re-binding grTexSource, enable src-alpha blending (e.g. ALPHA_8
    // cloud sheets). TMU0 must resume sampling and blend ~50% white over blue.
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    drawQuad(100.0f, 0.0f, 200.0f, 100.0f, 255.0f, 255.0f, 255.0f, 255.0f,
             1.0f);

    // Without re-binding grTexSource, switch to opaque blend + alpha test
    // (> 0xC0). The ALPHA_8 texel (0x80) must fail and discard the red quad.
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grAlphaTestFunction(GR_CMP_GREATER);
    grAlphaTestReferenceValue(0xC0);
    drawQuad(200.0f, 0.0f, 300.0f, 100.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f);

    // Disable alpha test again and verify a subnormal-oow green quad renders.
    grAlphaTestFunction(GR_CMP_ALWAYS);
    drawQuad(200.0f, 120.0f, 300.0f, 220.0f, 10.0f, 220.0f, 30.0f, 255.0f,
             2.94736e-39f);

    grBufferSwap(0);

    auto readPx = [](uint32_t x, uint32_t y) {
      uint32_t px = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, x, y, 1, 1, 4, &px) ==
              FXTRUE);
      return px;
    };
    const uint32_t pSub = readPx(50, 50);
    const uint32_t pZero = readPx(50, 170);
    const uint32_t pNeg = readPx(50, 290);
    const uint32_t pCloud = readPx(150, 50);
    const uint32_t pAlphaTest = readPx(250, 50);
    const uint32_t pGreen = readPx(250, 170);

    INFO("Backend=" << backendName << " pSub=0x" << std::hex << pSub
                    << " pZero=0x" << pZero << " pNeg=0x" << pNeg
                    << " pCloud=0x" << pCloud << " pAlphaTest=0x" << pAlphaTest
                    << " pGreen=0x" << pGreen << std::dec);

    // Pure blue strips: none may be discarded to cleared black!
    for (uint32_t p : {pSub, pZero, pNeg}) {
      CHECK(((p >> 16) & 0xFFu) <= 35);
      CHECK(((p >> 8) & 0xFFu) >= 45);
      CHECK((p & 0xFFu) >= 185);
    }
    // ~50% blended white over (20, 60, 200) -> approx (138, 158, 228).
    CHECK(((pCloud >> 16) & 0xFFu) >= 115);
    CHECK(((pCloud >> 16) & 0xFFu) <= 165);
    CHECK(((pCloud >> 8) & 0xFFu) >= 135);
    CHECK(((pCloud >> 8) & 0xFFu) <= 185);
    // Alpha-tested red quad discarded -> underlying blue preserved.
    CHECK(((pAlphaTest >> 16) & 0xFFu) <= 35);
    CHECK((pAlphaTest & 0xFFu) >= 185);
    // Restored untextured green quad with subnormal oow rendered solidly.
    CHECK(((pGreen >> 16) & 0xFFu) <= 30);
    CHECK(((pGreen >> 8) & 0xFFu) >= 200);
    CHECK((pGreen & 0xFFu) <= 50);

    grSstWinClose();
    grGlideShutdown();
  }
  CHECK(testedBackends >= 1);

  if (hadPrevBackend) {
    ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
}

