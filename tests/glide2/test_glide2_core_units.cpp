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

TEST_CASE("Core: Utility Math, Fog & Texture Helpers", "[Core][Utility]") {
  SECTION("Glide gu* helpers") {
  SECTION("Endian Swapping") {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using SwapWordsFunc = FxU32 (*)(FxU32);
    using SwapBytesFunc = FxU16 (*)(FxU16);

    auto swapWords =
        reinterpret_cast<SwapWordsFunc>(::dlsym(handle, "guEndianSwapWords"));
    auto swapBytes =
        reinterpret_cast<SwapBytesFunc>(::dlsym(handle, "guEndianSwapBytes"));

    REQUIRE(swapWords != nullptr);
    REQUIRE(swapBytes != nullptr);

    // Test word swap (swaps high and low 16-bit words of a 32-bit value)
    REQUIRE(swapWords(0x12345678) == 0x56781234);
    REQUIRE(swapWords(0x0000FFFF) == 0xFFFF0000);
    REQUIRE(swapWords(0xABCDEF01) == 0xEF01ABCD);

    // Test byte swap (swaps two bytes of a 16-bit value)
    REQUIRE(swapBytes(0x1234) == 0x3412);
    REQUIRE(swapBytes(0x00FF) == 0xFF00);
    REQUIRE(swapBytes(0xABCD) == 0xCDAB);

    ::dlclose(handle);
  }

  SECTION("RLE 16-bit Encoding") {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using EncodeRLEFunc = int (*)(void*, void*, FxU32, FxU32);
    auto encodeRLE =
        reinterpret_cast<EncodeRLEFunc>(::dlsym(handle, "guEncodeRLE16"));
    REQUIRE(encodeRLE != nullptr);

    // Construct 8x2 test image (16 pixels)
    // 8 pixels of Red (0xF800), 4 pixels of Green (0x07E0), 4 pixels of Blue
    // (0x001F)
    std::vector<uint16_t> src(16);
    for (int i = 0; i < 8; ++i) src[i] = 0xF800;
    for (int i = 8; i < 12; ++i) src[i] = 0x07E0;
    for (int i = 12; i < 16; ++i) src[i] = 0x001F;

    // 1. Query size only
    int bytesNeeded = encodeRLE(nullptr, src.data(), 8, 2);
    REQUIRE(bytesNeeded == 12);  // 3 runs * 4 bytes/run = 12 bytes

    // 2. Perform encoding
    std::vector<uint32_t> dst(3, 0);
    int bytesWritten = encodeRLE(dst.data(), src.data(), 8, 2);
    REQUIRE(bytesWritten == 12);

    // 3. Verify encoded runs
    // Run 0: 8 of Red (0xF800) -> 0x0008F800
    REQUIRE(dst[0] == 0x0008F800);
    // Run 1: 4 of Green (0x07E0) -> 0x000407E0
    REQUIRE(dst[1] == 0x000407E0);
    // Run 2: 4 of Blue (0x001F) -> 0x0004001F
    REQUIRE(dst[2] == 0x0004001F);

    ::dlclose(handle);
  }

  SECTION("Color MipMap Creation") {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using CreateMipMapFunc = FxU16* (*)(void);
    auto createMipMap = reinterpret_cast<CreateMipMapFunc>(
        ::dlsym(handle, "guTexCreateColorMipMap"));
    REQUIRE(createMipMap != nullptr);

    FxU16* mipmap = createMipMap();
    REQUIRE(mipmap != nullptr);

    // Verify colors at the start of each mipmap level
    // Level 0 (256x256): starts at offset 0, Red (0xF800)
    REQUIRE(mipmap[0] == 0xF800);
    REQUIRE(mipmap[256 * 256 - 1] == 0xF800);

    // Level 1 (128x128): starts at offset 65536, Green (0x07E0)
    REQUIRE(mipmap[65536] == 0x07E0);
    REQUIRE(mipmap[65536 + 128 * 128 - 1] == 0x07E0);

    // Level 2 (64x64): starts at offset 81920, Blue (0x001F)
    REQUIRE(mipmap[81920] == 0x001F);

    // Level 3 (32x32): starts at offset 86016, White (0xFFFF)
    REQUIRE(mipmap[86016] == 0xFFFF);

    // Level 4 (16x16): starts at offset 87040, Black (0x0000)
    REQUIRE(mipmap[87040] == 0x0000);

    // Level 5 (8x8): starts at offset 87296, Red (0xF800)
    REQUIRE(mipmap[87296] == 0xF800);

    // Level 6 (4x4): starts at offset 87360, Green (0x07E0)
    REQUIRE(mipmap[87360] == 0x07E0);

    // Level 7 (2x2): starts at offset 87376, Blue (0x001F)
    REQUIRE(mipmap[87376] == 0x001F);

    // Level 8 (1x1): starts at offset 87380, White (0xFFFF)
    REQUIRE(mipmap[87380] == 0xFFFF);

    std::free(mipmap);
    ::dlclose(handle);
  }

  SECTION("Combiner Preset State Routing") {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using SelectSstFunc = void (*)(int);
    using OpenWinFunc =
        FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                   GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void (*)(void);
    using AlphaSourceFunc = void (*)(GrAlphaSource_t);
    using ColorCombineFunc = void (*)(GrColorCombineFnc_t);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect =
        reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen =
        reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto alphaSrc =
        reinterpret_cast<AlphaSourceFunc>(::dlsym(handle, "guAlphaSource"));
    auto colorComb = reinterpret_cast<ColorCombineFunc>(
        ::dlsym(handle, "guColorCombineFunction"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(alphaSrc != nullptr);
    REQUIRE(colorComb != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Open a headless window context to instantiate the backend
    glideInit();
    sstSelect(0);
    FxBool ok = winOpen(0, GR_RESOLUTION_800x600, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 0);
    REQUIRE(ok == FXTRUE);

    // Call the combiner helpers under the active context
    // These should execute successfully and update the combiner state without
    // throwing/crashing
    REQUIRE_NOTHROW(alphaSrc(GR_ALPHASOURCE_TEXTURE_ALPHA));
    REQUIRE_NOTHROW(alphaSrc(GR_ALPHASOURCE_ITERATED_ALPHA));

    REQUIRE_NOTHROW(colorComb(GR_COLORCOMBINE_DECAL_TEXTURE));
    REQUIRE_NOTHROW(colorComb(GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB));

    winClose();
    glideShutdown();
    ::dlclose(handle);
  }
  }

  SECTION("MathUtils fog table calculations") {
  SECTION("FogTableIndexToW calculations") {
    REQUIRE(GlideWrapper::MathUtils::FogTableIndexToW(0) == 1.0f);
    REQUIRE(GlideWrapper::MathUtils::FogTableIndexToW(63) == 52428.800781f);
    // Clamp boundaries
    REQUIRE(GlideWrapper::MathUtils::FogTableIndexToW(-1) == 1.0f);
    REQUIRE(GlideWrapper::MathUtils::FogTableIndexToW(100) == 52428.800781f);
  }

  SECTION("FogGenerateLinear generates correct linear mapping") {
    uint8_t table[64];
    std::memset(table, 0, sizeof(table));

    // Define linear range between 1.0 and 100.0
    GlideWrapper::MathUtils::FogGenerateLinear(table, 1.0f, 100.0f);

    // Under W=1.0, fog value should be 0 (no fog)
    REQUIRE(table[0] == 0);
    // Under W=52428.8, since it's far above farW (100.0), it should be 255
    // (full fog)
    REQUIRE(table[63] == 255);
  }

  SECTION("FogGenerateExp behaves exponentially") {
    uint8_t table[64];
    std::memset(table, 0, sizeof(table));

    GlideWrapper::MathUtils::FogGenerateExp(table, 0.01f);
    // Exponential values should increase monotonically
    for (int i = 1; i < 64; ++i) {
      REQUIRE(table[i] >= table[i - 1]);
    }
  }
  }

  SECTION("LfbManager initial & unlock edge cases") {
  auto& manager = GlideWrapper::LfbManager::GetInstance();

  SECTION("Initial state is unlocked") { REQUIRE(manager.IsLocked() == false); }

  SECTION("Unlock fails gracefully if not locked") {
    GlideWrapper::LfbPipelineConfig config;
    bool status = manager.Unlock(0, 0, config);
    REQUIRE(status == false);
    REQUIRE(manager.IsLocked() == false);
  }
  }

}

TEST_CASE("Core: VertexLayout Decoding (Glide 2.x & Multi-TMU)", "[Core][VertexLayout]") {
  SECTION("Glide 2.x single-TMU vertex layout") {
  SECTION("Glide 2 Canonical Vertex Decoding") {
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    layout.ResetToGlide2Canonical();

    // Prepare mock raw Glide 2 vertex matching canonical offsets:
    // x,y at 0, z at 8, rgb at 12, ooz at 24, a at 28, oow at 32, st0 at 36
    struct MockGlide2Vertex {
      float x, y;
      float z;
      float r, g, b;
      float ooz;
      float a;
      float oow;
      float s, t;
    };

    MockGlide2Vertex v2;
    v2.x = 100.0f;
    v2.y = 200.0f;
    v2.z = 30000.0f;
    v2.r = 128.0f;
    v2.g = 192.0f;
    v2.b = 255.0f;
    v2.ooz = 40000.0f;
    v2.a = 200.0f;
    v2.oow = 0.5f;
    v2.s = 5.0f;
    v2.t = 10.0f;

    GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v2);

    REQUIRE(mv.pos[0] == 100.0f);
    REQUIRE(mv.pos[1] == 200.0f);
    REQUIRE(mv.pos[2] == 40000.0f / 65535.0f);
    REQUIRE(mv.color[0] == 128.0f / 255.0f);
    REQUIRE(mv.color[1] == 192.0f / 255.0f);
    REQUIRE(mv.color[2] == 255.0f / 255.0f);
    REQUIRE(mv.color[3] == 200.0f / 255.0f);
    REQUIRE(mv.pos[3] == 0.5f);
    REQUIRE(mv.tex[0] == 5.0f);
  }
  }

  SECTION("Dual-TMU (TMU1) vertex layout decoding") {
  auto& layout = GlideWrapper::VertexLayout::GetInstance();

  SECTION("Glide 2 TMU 1 Decoding (Canonical)") {
    layout.ResetToGlide2Canonical();

    struct MockGlide2TMU1Vertex {
      float x, y;
      float z;
      float r, g, b;
      float ooz;
      float a;
      float oow;
      float s0, t0;
      float padding[1];  // Pad to offset 48
      float s1, t1;
    };

    MockGlide2TMU1Vertex v;
    v.x = 100.0f;
    v.y = 200.0f;
    v.z = 0.0f;
    v.r = 255.0f;
    v.g = 255.0f;
    v.b = 255.0f;
    v.ooz = 0.0f;
    v.a = 255.0f;
    v.oow = 1.0f;
    v.s0 = 5.0f;
    v.t0 = 10.0f;
    v.s1 = 15.0f;
    v.t1 = 20.0f;

    GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

    REQUIRE(mv.pos[0] == 100.0f);
    REQUIRE(mv.pos[1] == 200.0f);
    REQUIRE(mv.tex[0] == 5.0f);
    REQUIRE(mv.tex[1] == 10.0f);
    REQUIRE(mv.tex[2] == 15.0f);
    REQUIRE(mv.tex[3] == 20.0f);
    REQUIRE(mv.tmu_oow[1] == 1.0f);  // Default when separate W disabled
  }

  SECTION("Glide 2 TMU 1 Decoding (Canonical with separate W)") {
    layout.ResetToGlide2Canonical();
    layout.SetSTWHintMask(0x8);  // GR_STWHINT_W_DIFF_TMU1

    struct MockGlide2TMU1Vertex {
      float x, y;
      float z;
      float r, g, b;
      float ooz;
      float a;
      float oow;
      float s0, t0;
      float padding[1];  // Pad to offset 48
      float s1, t1, w1;
    };

    MockGlide2TMU1Vertex v;
    v.x = 100.0f;
    v.y = 200.0f;
    v.z = 0.0f;
    v.r = 255.0f;
    v.g = 255.0f;
    v.b = 255.0f;
    v.ooz = 0.0f;
    v.a = 255.0f;
    v.oow = 0.5f;
    v.s0 = 5.0f;
    v.t0 = 10.0f;
    v.s1 = 15.0f;
    v.t1 = 20.0f;
    v.w1 = 0.25f;

    GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

    REQUIRE(mv.pos[0] == 100.0f);
    REQUIRE(mv.pos[1] == 200.0f);
    REQUIRE(mv.tex[0] == 5.0f);
    REQUIRE(mv.tex[1] == 10.0f);
    REQUIRE(mv.tex[2] == 15.0f);
    REQUIRE(mv.tex[3] == 20.0f);
    REQUIRE(mv.tmu_oow[1] == 0.25f);
  }

  SECTION("Glide 3 TMU 1 Decoding (Voodoo2 Canonical on Glide 3)") {
    layout.ResetToGlide2Canonical();

    struct MockVoodoo2Vertex {
      float x, y;     // 0, 4
      float z;        // 8
      float r, g, b;  // 12, 16, 20
      float ooz;      // 24
      float a;        // 28
      float oow;      // 32
      float s0, t0;   // 36, 40
      float oow0;     // 44
      float s1, t1;   // 48, 52 (TMU 1!)
      float oow1;     // 56
    };

    MockVoodoo2Vertex v;
    v.x = 120.0f;
    v.y = 240.0f;
    v.z = 0.5f;
    v.r = 255.0f;
    v.g = 128.0f;
    v.b = 0.0f;
    v.ooz = 32768.0f;
    v.a = 255.0f;
    v.oow = 0.5f;
    v.s0 = 10.0f;
    v.t0 = 20.0f;
    v.oow0 = 0.5f;
    v.s1 = 100.0f;
    v.t1 = 200.0f;
    v.oow1 = 0.5f;

    GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

    REQUIRE(mv.pos[0] == 120.0f);
    REQUIRE(mv.pos[1] == 240.0f);
    REQUIRE(mv.pos[3] == 0.5f);
    REQUIRE(mv.tex[0] == 10.0f);
    REQUIRE(mv.tex[1] == 20.0f);
    REQUIRE(mv.tex[2] == 100.0f);
    REQUIRE(mv.tex[3] == 200.0f);
  }

  SECTION("Glide 3 TMU 1 Decoding (Glide 3 Custom Layout)") {
    layout.ResetToGlide3Canonical();

    layout.SetParamOffsetGlide3(0x01, 0, 1);   // GR_PARAM_XY
    layout.SetParamOffsetGlide3(0x04, 12, 1);  // GR_PARAM_Q
    layout.SetParamOffsetGlide3(0x20, 16, 1);  // GR_PARAM_RGB
    layout.SetParamOffsetGlide3(0x10, 28, 1);  // GR_PARAM_A
    layout.SetParamOffsetGlide3(0x40, 32, 1);  // GR_PARAM_ST0
    layout.SetParamOffsetGlide3(0x41, 40, 1);  // GR_PARAM_ST1

    struct MockGlide3Vertex {
      float x, y;     // 0, 4
      float pad_ooz;  // 8
      float q;        // 12
      float r, g, b;  // 16, 20, 24
      float a;        // 28
      float s0, t0;   // 32, 36
      float s1, t1;   // 40, 44 (TMU 1!)
    };

    MockGlide3Vertex v;
    v.x = 300.0f;
    v.y = 150.0f;
    v.q = 0.2f;
    v.r = 0.0f;
    v.g = 255.0f;
    v.b = 255.0f;
    v.a = 128.0f;
    v.s0 = 5.0f;
    v.t0 = 8.0f;
    v.s1 = 50.0f;
    v.t1 = 80.0f;

    GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

    REQUIRE(mv.pos[0] == 300.0f);
    REQUIRE(mv.pos[1] == 150.0f);
    REQUIRE(mv.pos[3] == 0.2f);
    REQUIRE(mv.tex[0] == 5.0f);
    REQUIRE(mv.tex[1] == 8.0f);
    REQUIRE(mv.tex[2] == 50.0f);
    REQUIRE(mv.tex[3] == 80.0f);
  }
  }

}

TEST_CASE("Core: Texture Decoders, PALETTE6666 & Partial Mipmap Packing", "[Core][Texture]") {
  SECTION("Texture decoders & LFB simulation") {
  // Initialize logger to print to console for debug visibility!
  auto& logger = GlideWrapper::Logger::GetInstance();
  logger.Initialize(GlideTestHelpers::TempTestPath("glide_ng_test_debug.log"),
                    GlideWrapper::LogLevel::DEBUG, true);

  SECTION("RGB_332 Texture Format Decoder") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    tm.Reset();

    // 1 pixel of RGB_332
    uint8_t rawData = 0b11111010;  // R=7 (111), G=6 (110), B=2 (10)

    // Download as 1x1 mipmap
    bool ok = tm.DownloadMipMap(0, 0x1000, 8, 8, 3, 0x0, &rawData);
    REQUIRE(ok == true);

    auto* tex = tm.GetTexture(0, 0x1000);
    REQUIRE(tex != nullptr);
    REQUIRE(tex->swizzledMipLevels.size() == 1);
    REQUIRE(tex->swizzledMipLevels[0].size() == 1);

    // Expected ARGB: A=255, R=255, G=218, B=170 -> 0xFFFFDAAA
    uint32_t decoded = tex->swizzledMipLevels[0][0];
    REQUIRE(decoded == 0xFFFFDAAA);
  }

  SECTION("ALPHA_INTENSITY_44 Texture Format Decoder") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    tm.Reset();

    uint8_t rawData = 0x8A;  // Alpha = 8, Intensity = 10

    bool ok = tm.DownloadMipMap(0, 0x2000, 8, 8, 3, 0x4, &rawData);
    REQUIRE(ok == true);

    auto* tex = tm.GetTexture(0, 0x2000);
    REQUIRE(tex != nullptr);

    // Expected ARGB: A=136 (0x88), R=G=B=170 (0xAA) -> 0x88AAAAAA
    uint32_t decoded = tex->swizzledMipLevels[0][0];
    REQUIRE(decoded == 0x88AAAAAA);
  }

  SECTION("ARGB_8332 Texture Format Decoder") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    tm.Reset();

    uint16_t rawData = 0x89FA;  // Alpha = 0x89, RGB332 = 0xFA (R=7, G=6, B=2)

    bool ok = tm.DownloadMipMap(0, 0x3000, 8, 8, 3, 0x8, &rawData);
    REQUIRE(ok == true);

    auto* tex = tm.GetTexture(0, 0x3000);
    REQUIRE(tex != nullptr);

    // Expected ARGB: A=0x89, R=255 (0xFF), G=218 (0xDA), B=170 (0xAA) ->
    // 0x89FFDAAA
    uint32_t decoded = tex->swizzledMipLevels[0][0];
    REQUIRE(decoded == 0x89FFDAAA);
  }

  SECTION("AP_88 Texture Format Decoder and Deferred Palettization") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    tm.Reset();

    uint16_t rawData = 0xF245;  // Alpha = 0xF2, Index = 0x45

    // Download AP_88 texture before palette is loaded
    bool ok = tm.DownloadMipMap(0, 0x4000, 8, 8, 3, 0xe, &rawData);
    REQUIRE(ok == true);

    auto* tex = tm.GetTexture(0, 0x4000);
    REQUIRE(tex != nullptr);
    REQUIRE(tex->isPaletted == true);

    // 1. Check grayscale fallback: A=0xF2, R=G=B=0x45 -> 0xF2454545
    REQUIRE(tex->swizzledMipLevels[0][0] == 0xF2454545);

    // 2. Load palette table
    uint32_t mockPalette[256] = {0};
    mockPalette[0x45] = 0x00112233;  // RGB color = 0x112233

    ok = tm.DownloadTable(0, 2, mockPalette);  // 2 = GR_TEXTABLE_PALETTE
    REQUIRE(ok == true);

    // 3. Verify texture deferred-swizzles correctly using the new palette table
    // Expected: A=0xF2, R=0x11, G=0x22, B=0x33 -> 0xF2112233
    REQUIRE(tex->swizzledMipLevels[0][0] == 0xF2112233);
  }

  SECTION("AYIQ_8422 Texture Format Decoder and Deferred NCC Swizzling") {
    auto& tm = GlideWrapper::TextureManager::GetInstance();
    tm.Reset();

    uint16_t rawData = 0xE196;  // Alpha = 0xE1, YIQ = 0x96 (Y=9, I=1, Q=2)

    // Download AYIQ_8422 texture
    bool ok = tm.DownloadMipMap(0, 0x5000, 8, 8, 3, 0x9, &rawData);
    REQUIRE(ok == true);

    auto* tex = tm.GetTexture(0, 0x5000);
    REQUIRE(tex != nullptr);
    REQUIRE(tex->isNcc == true);

    // 1. Check grayscale fallback: A=0xE1, Y=9 -> 9 * 255 / 15 = 153 (0x99) ->
    // 0xE1999999
    REQUIRE(tex->swizzledMipLevels[0][0] == 0xE1999999);

    // 2. Load NCC0 table
    GuNccTable ncc;
    std::memset(&ncc, 0, sizeof(ncc));
    ncc.yRGB[9] = 100;
    ncc.iRGB[1][0] = 10;  // R
    ncc.iRGB[1][1] = 20;  // G
    ncc.iRGB[1][2] = 30;  // B
    ncc.qRGB[2][0] = 5;   // R
    ncc.qRGB[2][1] = 15;  // G
    ncc.qRGB[2][2] = 25;  // B

    // Active NCC table is NCC0 (0) by default. Download table NCC0 (0).
    ok = tm.DownloadTable(0, 0, &ncc);  // 0 = GR_TEXTABLE_NCC0
    REQUIRE(ok == true);

    // 3. Verify texture deferred-swizzles correctly using active NCC table
    // Expected: A=0xE1, R=115 (0x73), G=135 (0x87), B=155 (0x9B) -> 0xE173879B
    REQUIRE(tex->swizzledMipLevels[0][0] == 0xE173879B);
  }

  SECTION("LFB Blending and Chroma Keying Simulation") {
    GlideWrapper::SoftwareBackend backend;
    GlideWrapper::WrapperConfig cfg;
    cfg.forceNoWindow = true;
    bool ok = backend.Initialize(cfg);
    REQUIRE(ok == true);

    ok = backend.AttachWindow(nullptr, 800, 600, true);
    REQUIRE(ok == true);

    // Clear backbuffer (buffer 1) to solid Blue (0xFF0000FF)
    backend.ClearBuffer(0x000000FF, 0xFF, 1.0f, 1);

    // Verify it was cleared correctly
    uint32_t pixel = 0;
    backend.ReadLFB(1, 0, 0, 1, 1, 4, &pixel);
    REQUIRE(pixel == 0xFF0000FF);

    // 1. Chroma Keying test: Write Green (0x07E0 -> RGB 0x0000FF00) with
    // Chroma Keying matching 24-bit GrColor_t Green
    uint16_t green565 = 0x07E0;

    GlideWrapper::LfbPipelineConfig chromaConfig;
    chromaConfig.pixelPipeline = true;
    chromaConfig.chromakeyEnabled = true;
    chromaConfig.chromakeyValue = 0x0000FF00;  // 24-bit GrColor_t Green

    // Write to (0, 0)
    ok =
        backend.WriteLFB(1, 0, 0, 1, 1, 2, 0, &green565, nullptr, chromaConfig);
    REQUIRE(ok == true);

    // Verify pixel remains Blue (write discarded)
    backend.ReadLFB(1, 0, 0, 1, 1, 4, &pixel);
    REQUIRE(pixel == 0xFF0000FF);

    // 2. Alpha Blending test: Write Green (0x07E0) with 50% constant alpha
    // blending
    GlideWrapper::LfbPipelineConfig blendConfig;
    blendConfig.pixelPipeline = true;
    blendConfig.chromakeyEnabled = false;
    blendConfig.constantAlpha = 128;  // ~50%
    blendConfig.rgbSrcBlend = 1;      // GR_BLEND_SRC_ALPHA (1 in Glide enum)
    blendConfig.rgbDstBlend = 5;      // GR_BLEND_ONE_MINUS_SRC_ALPHA

    ok = backend.WriteLFB(1, 0, 0, 1, 1, 2, 0, &green565, nullptr, blendConfig);
    REQUIRE(ok == true);

    // Verify blended color: Green (0, 255, 0) blended 50/50 with Blue (0, 0,
    // 255) R = 0 G = 255 * (128.0f/255.0f) = 128 B = 255 * (127.0f/255.0f) =
    // 127
    backend.ReadLFB(1, 0, 0, 1, 1, 4, &pixel);
    uint8_t r = (pixel >> 16) & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = pixel & 0xFF;

    REQUIRE(r == 0);
    REQUIRE(g >= 127);
    REQUIRE(g <= 129);
    REQUIRE(b >= 126);
    REQUIRE(b <= 128);
  }
  }

  SECTION("PALETTE6666 unpacking & swizzling") {
  auto& tm = GlideWrapper::TextureManager::GetInstance();
  tm.Reset();

  SECTION("Authentic ARGB8888 -> A6R6G6B6 quantization with bit replication") {
    auto expand6666 = [](uint32_t val) -> uint32_t {
      const uint32_t a6 = (val >> 26) & 0x3Fu;
      const uint32_t r6 = (val >> 18) & 0x3Fu;
      const uint32_t g6 = (val >> 10) & 0x3Fu;
      const uint32_t b6 = (val >> 2) & 0x3Fu;
      return (((a6 << 2) | (a6 >> 4)) << 24) |
             (((r6 << 2) | (r6 >> 4)) << 16) |
             (((g6 << 2) | (g6 >> 4)) << 8) |
             ((b6 << 2) | (b6 >> 4));
    };
    CHECK(expand6666(0x00FFFFFFu) == 0x00FFFFFFu);  // Transparent white glyph bg
    CHECK(expand6666(0xFCFFFFFCu) == 0xFFFFFFFFu);  // Opaque white max 6-bit
    CHECK(expand6666(0xFFA8542Cu) == 0xFFAA552Cu);  // 6-bit LSB replication
    CHECK(expand6666(0x00000000u) == 0x00000000u);  // Transparent black
  }

  SECTION("Download PALETTE6666 table and swizzle GR_TEXFMT_P_8_6666 texture") {
    std::vector<uint8_t> rawIndices = {0, 1, 2, 1, 1, 0, 2, 0,
                                       0, 1, 0, 1, 1, 0, 1, 2};

    bool dl_ok =
        tm.DownloadMipMap(0, 0x00010000, 6, 6, 3, 6, rawIndices.data());
    REQUIRE(dl_ok == true);

    auto* tex = tm.GetTexture(0, 0x00010000);
    REQUIRE(tex != nullptr);
    CHECK(tex->isPaletted == true);
    CHECK(tex->format == 6);

    REQUIRE(tex->swizzledMipLevels.size() == 1);
    CHECK(tex->swizzledMipLevels[0][0] == 0xFF000000);
    CHECK(tex->swizzledMipLevels[0][1] == 0xFF010101);

    // 3dfx Glide 3.x GR_TEXTABLE_PALETTE_6666_EXT passes standard 32-bit
    // ARGB8888 (0xAARRGGBB) entries; hardware keeps top 6 bits per channel.
    std::vector<uint32_t> mockPalette(256, 0);
    mockPalette[0] = 0xFFFF0000u;  // Opaque red
    mockPalette[1] = 0xFF00FF00u;  // Opaque green
    mockPalette[2] = 0x00FFFFFFu;  // Fully transparent white (alpha=0!)

    bool tbl_ok = tm.DownloadTable(0, 3, mockPalette.data());
    REQUIRE(tbl_ok == true);

    CHECK(tex->swizzledMipLevels[0][0] == 0xFFFF0000u);
    CHECK(tex->swizzledMipLevels[0][1] == 0xFF00FF00u);
    CHECK(tex->swizzledMipLevels[0][2] == 0x00FFFFFFu);
    CHECK(tex->swizzledMipLevels[0][4] == 0xFF00FF00u);
  }
  }

  SECTION("DownloadMipMapPartial auto-allocation & tight VRAM packing") {
  auto& tm = GlideWrapper::TextureManager::GetInstance();
  tm.Reset();

  // Internal LOD 2 = 64x64, aspect 3 = 1x1, format 0xa = GR_TEXFMT_RGB_565 (2 bpp).
  constexpr uint32_t kWidth = 64u;
  constexpr int32_t kLod64 = 2;
  constexpr int32_t kLod32 = 3;
  constexpr uint32_t kAspect1x1 = 3u;
  constexpr uint32_t kFmtRgb565 = 0xau;
  constexpr uint32_t kRows = 4u;
  constexpr uint32_t kAddrA = 0x5560u;
  constexpr uint32_t kCompactBytes = kWidth * kRows * 2u;  // 512 bytes
  constexpr uint32_t kAddrB = kAddrA + kCompactBytes;      // 0x5760

  SECTION("Auto-allocates without prior DownloadMipMap and keeps tight neighbors") {
    std::vector<uint16_t> redRows(kWidth * kRows, 0xF800u);   // Pure red
    std::vector<uint16_t> blueRows(kWidth * kRows, 0x001Fu);  // Pure blue

    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod64, kLod64, kAspect1x1,
                                     kFmtRgb565, redRows.data(), 0,
                                     static_cast<int>(kRows)));
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrB, kLod64, kLod64, kAspect1x1,
                                     kFmtRgb565, blueRows.data(), 0,
                                     static_cast<int>(kRows)));

    // Both tightly-packed neighbors must stay resident (not falsely evicted by
    // inflating allocatedByteSize to full 64x64 = 8192 bytes).
    auto* texA = tm.GetTexture(0, kAddrA);
    auto* texB = tm.GetTexture(0, kAddrB);
    REQUIRE(texA != nullptr);
    REQUIRE(texB != nullptr);
    CHECK(texA->allocatedByteSize == kCompactBytes);
    CHECK(texB->allocatedByteSize == kCompactBytes);
    CHECK((texA->swizzledMipLevels[0][0] & 0x00FF0000u) == 0x00FF0000u);
    CHECK((texA->swizzledMipLevels[0][0] & 0x000000FFu) == 0x00000000u);
    CHECK((texB->swizzledMipLevels[0][0] & 0x000000FFu) == 0x000000FFu);
    CHECK((texB->swizzledMipLevels[0][0] & 0x00FF0000u) == 0x00000000u);

    // Extending texA to rows [4..8) grows footprint from 512 -> 1024 bytes,
    // which overlaps kAddrB and must evict texB cleanly while keeping rows 0..3.
    std::vector<uint16_t> greenRows(kWidth * kRows, 0x07E0u);  // Pure green
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod64, kLod64, kAspect1x1,
                                     kFmtRgb565, greenRows.data(),
                                     static_cast<int>(kRows),
                                     static_cast<int>(kRows * 2u)));
    CHECK(tm.GetTexture(0, kAddrB) == nullptr);
    texA = tm.GetTexture(0, kAddrA);
    REQUIRE(texA != nullptr);
    CHECK(texA->allocatedByteSize == kCompactBytes * 2u);
    CHECK((texA->swizzledMipLevels[0][0] & 0x00FF0000u) == 0x00FF0000u);
    CHECK((texA->swizzledMipLevels[0][kWidth * kRows] & 0x0000FF00u) ==
          0x0000FF00u);
  }

  SECTION("Expands mip chain when coarser LOD streams in via partial download") {
    std::vector<uint16_t> redRows(kWidth * kRows, 0xF800u);
    std::vector<uint16_t> blueMip(32u * 2u, 0x001Fu);
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod64, kLod64, kAspect1x1,
                                     kFmtRgb565, redRows.data(), 0,
                                     static_cast<int>(kRows)));
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod32, kLod64, kAspect1x1,
                                     kFmtRgb565, blueMip.data(), 0, 2));
    auto* tex = tm.GetTexture(0, kAddrA);
    REQUIRE(tex != nullptr);
    REQUIRE(tex->swizzledMipLevels.size() == 2u);
    CHECK(tex->largeLod == kLod64);
    CHECK(tex->smallLod == kLod32);
    CHECK((tex->swizzledMipLevels[0][0] & 0x00FF0000u) == 0x00FF0000u);
    CHECK((tex->swizzledMipLevels[1][0] & 0x000000FFu) == 0x000000FFu);
  }

  SECTION("Replaces stale entry when format changes at same startAddress") {
    std::vector<uint16_t> red565(kWidth * kRows, 0xF800u);
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod64, kLod64, kAspect1x1,
                                     kFmtRgb565, red565.data(), 0,
                                     static_cast<int>(kRows)));
    // Format 0xc = GR_TEXFMT_ARGB_4444
    std::vector<uint16_t> cyan4444(kWidth * kRows, 0xF0FFu);
    REQUIRE(tm.DownloadMipMapPartial(0, kAddrA, kLod64, kLod64, kAspect1x1,
                                     0xcu, cyan4444.data(), 0,
                                     static_cast<int>(kRows)));
    auto* tex = tm.GetTexture(0, kAddrA);
    REQUIRE(tex != nullptr);
    CHECK(tex->format == 0xcu);
    CHECK(tex->swizzledMipLevels[0][0] == 0xFF00FFFFu);
  }
  }

  SECTION("HashUtils: FNV-1a, MixBytes64 & HashCombine") {
    using namespace GlideWrapper::Hash;
    // Standard FNV-1a 64-bit empty & "foobar" test vectors
    CHECK(kFnv1aOffsetBasis64 == 0xCBF29CE484222325ULL);
    CHECK(kFnv1aPrime64 == 0x00000100000001B3ULL);
    CHECK(Fnv1aBytes64("", 0) == kFnv1aOffsetBasis64);
    const char* sample = "foobar";
    CHECK(Fnv1aBytes64(sample, 6) == 0x85944171F73967E8ULL);
    CHECK(NonZeroHash64(0ULL) == 1ULL);
    CHECK(NonZeroHash64(42ULL) == 42ULL);

    // MixBytes64 handles aligned 8-byte blocks + unaligned tails deterministically
    const uint8_t bufA[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    uint8_t bufB[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14};
    const uint64_t hA = MixBytes64(kFnv1aOffsetBasis64, bufA, sizeof(bufA));
    const uint64_t hA2 = MixBytes64(kFnv1aOffsetBasis64, bufA, sizeof(bufA));
    const uint64_t hB = MixBytes64(kFnv1aOffsetBasis64, bufB, sizeof(bufB));
    CHECK(hA == hA2);
    CHECK(hA != hB);

    // HashCombine is deterministic and order-sensitive
    std::size_t s1 = 0, s2 = 0;
    HashCombine(s1, 10u);
    HashCombine(s1, 20u);
    HashCombine(s2, 20u);
    HashCombine(s2, 10u);
    CHECK(s1 != 0u);
    CHECK(s1 != s2);

    // GlideConstants ABI/hardware invariants
    CHECK(GlideWrapper::kUnboundTexAddress == 0xFFFFFFFFu);
    CHECK(GlideWrapper::kExtCombineActiveBit == 0x80000000u);
    CHECK(GlideWrapper::kLfbBufferFront == 0u);
    CHECK(GlideWrapper::kLfbBufferBack == 1u);
    CHECK(GlideWrapper::kLfbBufferAux == 2u);
    CHECK(GlideWrapper::kLfbSrcFmtZa16 == 0x0Fu);
    CHECK(GlideWrapper::kDepth24MaxU32 == 16777215u);
    CHECK(GlideWrapper::kRgbByteBroadcast * 0xABu == 0x00ABABABu);
    CHECK(GlideWrapper::kRgbaByteBroadcast * 0xCDu == 0xCDCDCDCDu);
  }

}

