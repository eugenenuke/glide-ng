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
#include "core/GpuSelection.h"
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

TEST_CASE("Glide 2.x: Hardware/Version & Environment Overrides", "[Glide2][Config][EnvVars]") {
  SECTION("Hardware & version override consistency") {
#ifdef __linux__
  struct ScopedEnv {
    std::string key;
    bool hadPrev = false;
    std::string prevVal;
    ScopedEnv(const char* k, const char* v) : key(k) {
      if (const char* cur = std::getenv(k)) {
        hadPrev = true;
        prevVal = cur;
      }
      if (v) {
        ::setenv(k, v, 1);
      } else {
        ::unsetenv(k);
      }
    }
    ~ScopedEnv() {
      if (hadPrev) {
        ::setenv(key.c_str(), prevVal.c_str(), 1);
      } else {
        ::unsetenv(key.c_str());
      }
    }
  };

  SECTION("Voodoo Graphics (1-TMU) + custom API version/memory overrides") {
    ScopedEnv eCard("GLIDE_NG_CARD_MODEL", "VOODOO1");
    ScopedEnv eVer("GLIDE_NG_API_VERSION", "2.61");
    ScopedEnv eFb("GLIDE_NG_FBI_MEM_MB", "8");
    ScopedEnv eTmuCnt("GLIDE_NG_TMU_COUNT", "1");
    ScopedEnv eTmuMem("GLIDE_NG_TMU_MEM_MB", "4");

    grGlideInit();

    char verBuf[80] = {};
    grGlideGetVersion(verBuf);
    CHECK(std::string(verBuf) == "2.61");

    FxI32 fbBytes = 0;
    FxI32 tmuBytes = 0;
    FxI32 numTmu = 0;
    grGet(GR_MEMORY_FB, sizeof(fbBytes), &fbBytes);
    grGet(GR_MEMORY_TMU, sizeof(tmuBytes), &tmuBytes);
    grGet(GR_NUM_TMU, sizeof(numTmu), &numTmu);
    CHECK(fbBytes == 8 * 1024 * 1024);
    CHECK(tmuBytes == 4 * 1024 * 1024);
    CHECK(numTmu == 1);

    GrHwConfiguration hw;
    std::memset(&hw, 0, sizeof(hw));
    REQUIRE(grSstQueryHardware(&hw) == FXTRUE);
    CHECK(hw.num_sst == 1);
    CHECK(hw.SSTs[0].type == GR_SSTTYPE_VOODOO);
    CHECK(hw.SSTs[0].sstBoard.VoodooConfig.fbRam == 8);
    CHECK(hw.SSTs[0].sstBoard.VoodooConfig.nTexelfx == 1);
    CHECK(hw.SSTs[0].sstBoard.VoodooConfig.tmuConfig[0].tmuRam == 4);

    CHECK(grTexMinAddress(GR_TMU0) == 0u);
    CHECK(grTexMaxAddress(GR_TMU0) == 4u * 1024u * 1024u);
    CHECK(guTexMemQueryAvail(GR_TMU0) == 4u * 1024u * 1024u);
    // TMU1 does not exist on a 1-TMU configuration -> must report 0 bytes
    CHECK(grTexMaxAddress(GR_TMU1) == 0u);
    CHECK(guTexMemQueryAvail(GR_TMU1) == 0u);

    grGlideShutdown();
  }

  SECTION("Voodoo2 SLI + 2-TMU + custom FBI/TMU memory overrides") {
    ScopedEnv eCard("GLIDE_NG_CARD_MODEL", "VOODOO2");
    ScopedEnv eVer("GLIDE_NG_API_VERSION", "2.42");
    ScopedEnv eFb("GLIDE_NG_FBI_MEM_MB", "12");
    ScopedEnv eTmuCnt("GLIDE_NG_TMU_COUNT", "2");
    ScopedEnv eTmuMem("GLIDE_NG_TMU_MEM_MB", "8");
    ScopedEnv eSli("GLIDE_NG_SLI", "1");

    grGlideInit();

    char verBuf[80] = {};
    grGlideGetVersion(verBuf);
    CHECK(std::string(verBuf) == "2.42");

    FxI32 fbBytes = 0;
    FxI32 tmuBytes = 0;
    FxI32 numTmu = 0;
    grGet(GR_MEMORY_FB, sizeof(fbBytes), &fbBytes);
    grGet(GR_MEMORY_TMU, sizeof(tmuBytes), &tmuBytes);
    grGet(GR_NUM_TMU, sizeof(numTmu), &numTmu);
    CHECK(fbBytes == 12 * 1024 * 1024);
    CHECK(tmuBytes == 8 * 1024 * 1024);
    CHECK(numTmu == 2);

    GrHwConfiguration hw;
    std::memset(&hw, 0, sizeof(hw));
    REQUIRE(grSstQueryHardware(&hw) == FXTRUE);
    CHECK(hw.SSTs[0].type == GR_SSTTYPE_VOODOO);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbiRev == 0x102);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbRam == 12);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.nTexelfx == 2);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.sliDetect == FXTRUE);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.tmuConfig[0].tmuRam == 8);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.tmuConfig[1].tmuRam == 8);

    CHECK(grTexMaxAddress(GR_TMU0) == 8u * 1024u * 1024u);
    CHECK(grTexMaxAddress(GR_TMU1) == 8u * 1024u * 1024u);
    CHECK(guTexMemQueryAvail(GR_TMU1) == 8u * 1024u * 1024u);

    grGlideShutdown();
  }
#endif
  }

  SECTION("Authentic 3dfx environment variables") {
#ifdef __linux__
  struct ScopedEnvM8G2 {
    std::vector<std::string> keys = {
        "GLIDE_NG_HEADLESS", "GLIDE_NG_MAX_FPS", "GLIDE_NG_VSYNC",
        "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_LOG_FILE",
        "FX_GLIDE_FBRAM", "FX_GLIDE_TMU_MEMSIZE", "SSTV2_NUM_TMUS",
        "SST_GAMMA", "FX_GLIDE_IGNORE_REOPEN", "FX_GLIDE_NO_DITHER_SUB",
        "SSTV2_FORCE_BILINEAR", "SST_TEXMAP_DISABLE", "FX_GLIDE_ALLOC_AUX",
        "FX_GLIDE_BOUNDS_CHECK", "FX_GLIDE_SNAPSHOT"};
    std::vector<std::pair<std::string, std::string>> saved;
    ScopedEnvM8G2() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        ::unsetenv(k.c_str());
      }
    }
    ~ScopedEnvM8G2() {
      for (const auto& kv : saved) {
        if (kv.second == "\x01UNSET") {
          ::unsetenv(kv.first.c_str());
        } else {
          ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
      }
    }
  } guard;

  SECTION("Authentic hardware sizing + SSTV2_FORCE_BILINEAR seam blending") {
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("FX_GLIDE_FBRAM", "4", 1);
    ::setenv("FX_GLIDE_TMU_MEMSIZE", "8", 1);
    ::setenv("SSTV2_NUM_TMUS", "1", 1);
    ::setenv("SSTV2_FORCE_BILINEAR", "1", 1);

    grGlideInit();
    GrHwConfiguration hw;
    std::memset(&hw, 0, sizeof(hw));
    REQUIRE(grSstQueryHardware(&hw) == FXTRUE);
    CHECK(hw.SSTs[0].type == GR_SSTTYPE_VOODOO);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbiRev == 0x82);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbRam == 4);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.nTexelfx == 1);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.tmuConfig[0].tmuRam == 8);
    CHECK(grTexMaxAddress(GR_TMU0) == 8u * 1024u * 1024u);
    CHECK(grTexMaxAddress(GR_TMU1) == 0u);

    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);

    // 16x16 RGB565 texture: left half black, right half white
    uint16_t texData[16 * 16];
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        texData[y * 16 + x] = (x < 8) ? 0x0000u : 0xFFFFu;
      }
    }
    GrTexInfo ti{};
    ti.smallLod = GR_LOD_16;
    ti.largeLod = GR_LOD_16;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_RGB_565;
    ti.data = texData;
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    // App requests POINT_SAMPLED -> SSTV2_FORCE_BILINEAR=1 must force linear blend at seam
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                    GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grBufferClear(0xFF000000u, 0xFF, 0xFFFF);

    auto makeV = [](float x, float y, float s, float t) {
      GrVertex v{};
      v.x = x; v.y = y; v.oow = 1.0f;
      v.r = 255.0f; v.g = 255.0f; v.b = 255.0f; v.a = 255.0f;
      v.tmuvtx[0].sow = s; v.tmuvtx[0].tow = t; v.tmuvtx[0].oow = 1.0f;
      return v;
    };
    GrVertex tl = makeV(0.0f, 0.0f, 0.0f, 0.0f);
    GrVertex tr = makeV(320.0f, 0.0f, 256.0f, 0.0f);
    GrVertex br = makeV(320.0f, 240.0f, 256.0f, 256.0f);
    GrVertex bl = makeV(0.0f, 240.0f, 0.0f, 256.0f);
    grDrawTriangle(&tl, &tr, &br);
    grDrawTriangle(&tl, &br, &bl);

    uint16_t seamRow[32] = {};
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 144, 120, 32, 1, 32 * 2, seamRow) == FXTRUE);
    int blendedCount = 0;
    for (uint16_t px : seamRow) {
      int r = ((px >> 11) & 31) << 3;
      if (r >= 24 && r <= 230) blendedCount++;
    }
    CHECK(blendedCount >= 4);

    grSstWinClose();
    grGlideShutdown();
  }

  SECTION("FX_GLIDE_IGNORE_REOPEN, SST_GAMMA, bounds check, and PPM snapshot") {
    const std::string logPath = "/tmp/glide_ng_m82_g2_runtime.log";
    const std::string snapPath = "/tmp/glide_ng_m82_g2_snap.ppm";
    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("SST_GAMMA", "1.8", 1);
    ::setenv("FX_GLIDE_IGNORE_REOPEN", "1", 1);
    ::setenv("FX_GLIDE_BOUNDS_CHECK", "1", 1);
    const std::string snapArg = snapPath + "@0";
    ::setenv("FX_GLIDE_SNAPSHOT", snapArg.c_str(), 1);

    grGlideInit();
    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);
    grGammaCorrectionValue(1.0f);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grBufferClear(0xFF000000u, 0xFF, 0xFFFF);

    auto makeV = [](float x, float y, float r, float g, float b) {
      GrVertex v{};
      v.x = x; v.y = y; v.oow = 1.0f;
      v.r = r; v.g = g; v.b = b; v.a = 255.0f;
      return v;
    };
    // Green patch to verify reopen preservation & PPM snapshot RGB channel accuracy
    GrVertex tl = makeV(40.0f, 40.0f, 16.0f, 240.0f, 32.0f);
    GrVertex tr = makeV(140.0f, 40.0f, 16.0f, 240.0f, 32.0f);
    GrVertex br = makeV(140.0f, 140.0f, 16.0f, 240.0f, 32.0f);
    GrVertex bl = makeV(40.0f, 140.0f, 16.0f, 240.0f, 32.0f);
    grDrawTriangle(&tl, &tr, &br);
    grDrawTriangle(&tl, &br, &bl);

    // Off-screen triangle triggers FX_GLIDE_BOUNDS_CHECK
    GrVertex oa = makeV(700.0f, 520.0f, 255.0f, 0.0f, 0.0f);
    GrVertex ob = makeV(800.0f, 520.0f, 255.0f, 0.0f, 0.0f);
    GrVertex oc = makeV(750.0f, 620.0f, 255.0f, 0.0f, 0.0f);
    grDrawTriangle(&oa, &ob, &oc);

    // Redundant grSstWinOpen must be ignored, preserving the green patch
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);
    uint16_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 90, 90, 1, 1, 2, &px) == FXTRUE);
    CHECK((((px >> 5) & 63) << 2) > 200);

    grBufferSwap(0);
    grSstWinClose();
    grGlideShutdown();

    std::ifstream snapIn(snapPath, std::ios::binary);
    REQUIRE(snapIn.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(snapIn)),
                               std::istreambuf_iterator<char>());
    const std::string hdr = "P6\n640 480\n255\n";
    REQUIRE(bytes.size() == hdr.size() + 640u * 480u * 3u);
    const size_t off = hdr.size() + (90u * 640u + 90u) * 3u;
    CHECK(bytes[off + 0] < 40);   // R
    CHECK(bytes[off + 1] > 200);  // G
    CHECK(bytes[off + 2] < 60);   // B

    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    const std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                 std::istreambuf_iterator<char>());
    CHECK(logContent.find("[BoundsCheck]") != std::string::npos);
    CHECK(logContent.find("Applying authentic gamma override: 1.8") != std::string::npos);

    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());
  }
#endif
  }

}

TEST_CASE("Software: SIMD Tier Parsing, CPUID Fallback & Non-Destructive Gamma", "[Software][SIMD][Gamma]") {
  SECTION("Runtime SIMD tier selection & CPUID fallback") {
  using namespace GlideWrapper;

  SwSimdLevel lvl = SwSimdLevel::Scalar;
  REQUIRE(ParseSwSimdLevel("scalar", lvl));
  CHECK(lvl == SwSimdLevel::Scalar);
  REQUIRE(ParseSwSimdLevel("0", lvl));
  CHECK(lvl == SwSimdLevel::Scalar);
  REQUIRE(ParseSwSimdLevel("avx2", lvl));
  CHECK(lvl == SwSimdLevel::Avx2);
  REQUIRE(ParseSwSimdLevel("1", lvl));
  CHECK(lvl == SwSimdLevel::Avx2);
  REQUIRE(ParseSwSimdLevel("avx512", lvl));
  CHECK(lvl == SwSimdLevel::Avx512);
  REQUIRE(ParseSwSimdLevel("AVX-512", lvl));
  CHECK(lvl == SwSimdLevel::Avx512);
  REQUIRE(ParseSwSimdLevel("auto", lvl));
  CHECK(lvl == SwSimdLevel::Auto);
  CHECK_FALSE(ParseSwSimdLevel("neon", lvl));

  // Default is AVX2
  {
    const char* prev = std::getenv("GLIDE_NG_SW_SIMD");
    std::string savedPrev = prev ? prev : "";
    bool hadPrev = (prev != nullptr);
    ::unsetenv("GLIDE_NG_SW_SIMD");

    WrapperConfig cfg;
    IniConfigLoader loader;
    loader.Load("/nonexistent_glide_ng_simd_test.ini", cfg);
    CHECK(cfg.swSimdLevel == SwSimdLevel::Avx2);
    CHECK(cfg.swUseSimd == true);

    ::setenv("GLIDE_NG_SW_SIMD", "avx512", 1);
    WrapperConfig cfg512;
    loader.Load("/nonexistent_glide_ng_simd_test.ini", cfg512);
    CHECK(cfg512.swSimdLevel == SwSimdLevel::Avx512);
    CHECK(cfg512.swUseSimd == true);

    std::string warn;
    SwSimdLevel eff = ResolveEffectiveSimdLevel(SwSimdLevel::Avx512, &warn);
    SwSimdLevel hostMax = DetectHostMaxSimdLevel();
    if (hostMax >= SwSimdLevel::Avx512) {
      CHECK(eff == SwSimdLevel::Avx512);
      CHECK(warn.empty());
    } else {
      CHECK(eff == hostMax);
      CHECK_FALSE(warn.empty());
    }

    ::setenv("GLIDE_NG_SW_SIMD", "bogus_isa", 1);
    WrapperConfig cfgBad;
    loader.Load("/nonexistent_glide_ng_simd_test.ini", cfgBad);
    CHECK_FALSE(cfgBad.invalidConfigValues.empty());

    if (hadPrev) {
      ::setenv("GLIDE_NG_SW_SIMD", savedPrev.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_SW_SIMD");
    }
    SetActiveSimdLevel(ResolveEffectiveSimdLevel(SwSimdLevel::Avx2));
  }
  }

  SECTION("Non-destructive RAMDAC gamma LUT across swaps") {
#ifdef __linux__
  const char* prevBackend = std::getenv("GLIDE_NG_BACKEND");
  const bool hadPrevBackend = (prevBackend != nullptr);
  const std::string savedBackend = hadPrevBackend ? prevBackend : "";
  ::setenv("GLIDE_NG_BACKEND", "software", 1);

  grGlideInit();
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) ==
          FXTRUE);

  // Fill both double buffers once with neutral mid-grey (simulating a static
  // cockpit bezel drawn once without per-frame grBufferClear, e.g. Longbow 2).
  grGammaCorrectionValue(1.0f);
  grBufferClear(0xFF808080u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grBufferSwap(0);
  grBufferClear(0xFF808080u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grBufferSwap(0);

  // Darkening gamma (< 1.0): 12 consecutive swaps must NEVER decay retained VRAM
  // toward black.
  grGammaCorrectionValue(0.5f);
  for (int i = 0; i < 12; ++i) {
    grBufferSwap(0);
  }
  uint32_t frontPx = 0;
  uint32_t backPx = 0;
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4,
                          &frontPx) == FXTRUE);
  REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4,
                          &backPx) == FXTRUE);
  CHECK(((frontPx >> 16) & 0xFFu) == 0x80u);
  CHECK(((frontPx >> 8) & 0xFFu) == 0x80u);
  CHECK((frontPx & 0xFFu) == 0x80u);
  CHECK(((backPx >> 16) & 0xFFu) == 0x80u);
  CHECK(((backPx >> 8) & 0xFFu) == 0x80u);
  CHECK((backPx & 0xFFu) == 0x80u);

  // Brightening gamma (> 1.0): 12 consecutive swaps must NEVER wash retained
  // VRAM toward white.
  grGammaCorrectionValue(2.0f);
  for (int i = 0; i < 12; ++i) {
    grBufferSwap(0);
  }
  frontPx = 0;
  backPx = 0;
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4,
                          &frontPx) == FXTRUE);
  REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4,
                          &backPx) == FXTRUE);
  CHECK(((frontPx >> 16) & 0xFFu) == 0x80u);
  CHECK(((frontPx >> 8) & 0xFFu) == 0x80u);
  CHECK((frontPx & 0xFFu) == 0x80u);
  CHECK(((backPx >> 16) & 0xFFu) == 0x80u);
  CHECK(((backPx >> 8) & 0xFFu) == 0x80u);
  CHECK((backPx & 0xFFu) == 0x80u);

  grGammaCorrectionValue(1.0f);
  grSstWinClose();
  grGlideShutdown();

  if (hadPrevBackend) {
    ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
  }

}

TEST_CASE("Software: Multi-Threaded Tile Scaling", "[Software][Scaling]") {
#ifdef __linux__
  struct EnvRestorer {
    std::string name;
    std::string oldValue;
    bool hadValue;
    EnvRestorer(const std::string& n) : name(n) {
      char* val = std::getenv(n.c_str());
      hadValue = (val != nullptr);
      if (hadValue) oldValue = val;
    }
    ~EnvRestorer() {
      if (hadValue) {
        ::setenv(name.c_str(), oldValue.c_str(), 1);
      } else {
        ::unsetenv(name.c_str());
      }
    }
  };

  // Save and restore environment variables to prevent test contamination
  EnvRestorer restorerBackend("GLIDE_NG_BACKEND");
  EnvRestorer restorerLogLevel("GLIDE_NG_LOG_LEVEL");
  EnvRestorer restorerThreads("GLIDE_NUM_THREADS");

  // Force software backend and disable logs during sweep
  ::setenv("GLIDE_NG_BACKEND", "software", 1);
  ::setenv("GLIDE_NG_LOG_LEVEL", "0", 1);

  void* handle = OpenGlide3DsoHandle();
  REQUIRE(handle != nullptr);

  auto glideInit =
      reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideInit"));
  auto sstSelect =
      reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
  auto winOpen = reinterpret_cast<unsigned long (*)(
      FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t,
      GrOriginLocation_t, int, int)>(::dlsym(handle, "grSstWinOpen"));
  auto winClose = reinterpret_cast<void (*)(unsigned long)>(
      ::dlsym(handle, "grSstWinClose"));
  auto shutdown =
      reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideShutdown"));
  auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
      ::dlsym(handle, "grBufferClear"));
  auto drawTriangle =
      reinterpret_cast<void (*)(const void*, const void*, const void*)>(
          ::dlsym(handle, "grDrawTriangle"));
  auto readLfb =
      reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                  void*)>(::dlsym(handle, "grLfbReadRegion"));
  auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxU32, FxBool)>(
      ::dlsym(handle, "grVertexLayout"));
  auto colorCombine =
      reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                GrCombineLocal_t, GrCombineOther_t, FxBool)>(
          ::dlsym(handle, "grColorCombine"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(drawTriangle != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(vertexLayout != nullptr);
  REQUIRE(colorCombine != nullptr);

  std::vector<uint32_t> baselinePixels;
  double baselineTime = 0.0;

  // Sweep threads: 1, 2, 4, 8, 12, 16, 24
  std::vector<int> threadCounts = {1, 2, 4, 8, 12, 16, 24};

  for (int threads : threadCounts) {
    ::setenv("GLIDE_NUM_THREADS", std::to_string(threads).c_str(), 1);

    glideInit();
    sstSelect(0);
    unsigned long ctx =
        winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Setup basic rendering state
    vertexLayout(0x01, 0, 1);   // GR_PARAM_XY at offset 0
    vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB at offset 12
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    bufferClear(0xFF000000, 0, 0);  // Clear to Black

    // Draw 10,000 shaded triangles to create a substantial workload
    struct GridVertex {
      float x, y, z;
      float r, g, b, a;
    };

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; ++i) {
      for (int j = 0; j < 100; ++j) {
        GridVertex v0{i * 6.4f, j * 4.8f, 0.5f, 255.0f, 0.0f, 0.0f, 255.0f};
        GridVertex v1{(i + 1) * 6.4f, j * 4.8f, 0.5f,  0.0f,
                      255.0f,         0.0f,     255.0f};
        GridVertex v2{i * 6.4f, (j + 1) * 4.8f, 0.5f,  0.0f,
                      0.0f,     255.0f,         255.0f};
        drawTriangle(&v0, &v1, &v2);
      }
    }

    // Force rendering to finish by reading the framebuffer
    std::vector<uint32_t> fb(640 * 480);
    FxBool ok =
        readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
    REQUIRE(ok == FXTRUE);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    printf("Threads: %d -> Time: %.3f ms\n", threads, elapsed.count());
    fflush(stdout);

    if (threads == 1) {
      baselinePixels = fb;
      baselineTime = elapsed.count();
    } else {
      // 1. Verify bit-identical rendering output!
      REQUIRE(fb == baselinePixels);

      // 2. Assert no performance collapse!
      // Multi-threaded execution time must not exceed 1.5x the baseline.
      REQUIRE(elapsed.count() < baselineTime * 1.5);
    }

    winClose(ctx);
    shutdown();
  }

  ::dlclose(handle);
#endif
}

TEST_CASE("Core: GPU Preference Selection & Safe Software Fallback",
          "[Glide2][Config][EnvVars][GPU]") {
#ifdef __linux__
  using namespace GlideWrapper;

  SECTION("ParseGpuPreference and FormatGpuPreferenceFull cover all aliases") {
    GpuPreference pref = GpuPreference::Default;
    std::string nameMatch;

    CHECK(ParseGpuPreference("default", pref, nameMatch));
    CHECK(pref == GpuPreference::Default);
    CHECK(FormatGpuPreferenceFull(pref, nameMatch) == "default");

    CHECK(ParseGpuPreference(" auto ", pref, nameMatch));
    CHECK(pref == GpuPreference::Default);

    CHECK(ParseGpuPreference("dgpu", pref, nameMatch));
    CHECK(pref == GpuPreference::Discrete);
    CHECK(FormatGpuPreferenceFull(pref, nameMatch) == "discrete");

    CHECK(ParseGpuPreference("IGPU", pref, nameMatch));
    CHECK(pref == GpuPreference::Integrated);
    CHECK(FormatGpuPreferenceFull(pref, nameMatch) == "integrated");

    CHECK(ParseGpuPreference("llvm", pref, nameMatch));
    CHECK(pref == GpuPreference::LlvmPipe);
    CHECK(FormatGpuPreferenceFull(pref, nameMatch) == "llvmpipe");

    CHECK(ParseGpuPreference("lavapipe", pref, nameMatch));
    CHECK(pref == GpuPreference::LlvmPipe);

    CHECK(ParseGpuPreference("name:Radeon 610M", pref, nameMatch));
    CHECK(pref == GpuPreference::NameMatch);
    CHECK(nameMatch == "Radeon 610M");
    CHECK(FormatGpuPreferenceFull(pref, nameMatch) == "name:Radeon 610M");

    CHECK_FALSE(ParseGpuPreference("", pref, nameMatch));
    CHECK_FALSE(ParseGpuPreference("name:   ", pref, nameMatch));
    CHECK_FALSE(ParseGpuPreference("bogus_gpu", pref, nameMatch));
  }

  SECTION("IniConfigLoader resolves default < INI [core] gpu < GLIDE_NG_GPU") {
    struct ScopedGpuEnv {
      bool had = false;
      std::string prev;
      ScopedGpuEnv() {
        if (const char* v = std::getenv("GLIDE_NG_GPU")) {
          had = true;
          prev = v;
        }
        ::unsetenv("GLIDE_NG_GPU");
      }
      ~ScopedGpuEnv() {
        if (had) {
          ::setenv("GLIDE_NG_GPU", prev.c_str(), 1);
        } else {
          ::unsetenv("GLIDE_NG_GPU");
        }
      }
    } guard;

    const std::string iniPath = "/tmp/glide_ng_gpu_pref_test.ini";
    {
      FILE* fp = std::fopen(iniPath.c_str(), "w");
      REQUIRE(fp != nullptr);
      std::fputs("[core]\nbackend = vulkan\ngpu = discrete\n", fp);
      std::fclose(fp);
    }

    IniConfigLoader loader;
    {
      WrapperConfig cfg;
      REQUIRE(loader.Load(iniPath, cfg));
      CHECK(cfg.gpuPreference == GpuPreference::Discrete);
      CHECK(cfg.settingSources["gpu"] == "ini");
    }

    ::setenv("GLIDE_NG_GPU", "integrated", 1);
    {
      WrapperConfig cfg;
      REQUIRE(loader.Load(iniPath, cfg));
      CHECK(cfg.gpuPreference == GpuPreference::Integrated);
      CHECK(cfg.settingSources["gpu"] == "GLIDE_NG_GPU");
    }

    std::remove(iniPath.c_str());
  }

  SECTION("Vulkan candidate selection honors OS loader order and explicit policies") {
    const std::vector<VulkanDeviceCandidate> hybrid{
        {"AMD Radeon 610M (RADV)", VulkanDeviceKind::IntegratedGpu, true},
        {"NVIDIA GeForce RTX 4070", VulkanDeviceKind::DiscreteGpu, true},
        {"llvmpipe (LLVM 23)", VulkanDeviceKind::Cpu, true},
    };

    // Default preserves loader order (OS default GPU0)
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::Default, "") == 0);
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::Integrated, "") == 0);
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::Discrete, "") == 1);
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::LlvmPipe, "") == 2);
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::NameMatch, "4070") == 1);
    CHECK(SelectVulkanDeviceCandidate(hybrid, GpuPreference::NameMatch, "missing") == -1);

    // Non-graphics device at index 0 is skipped even under Default
    const std::vector<VulkanDeviceCandidate> computeFirst{
        {"Compute Only Accelerator", VulkanDeviceKind::DiscreteGpu, false},
        {"AMD Radeon 610M (RADV)", VulkanDeviceKind::IntegratedGpu, true},
    };
    CHECK(SelectVulkanDeviceCandidate(computeFirst, GpuPreference::Default, "") == 1);
    CHECK(SelectVulkanDeviceCandidate(computeFirst, GpuPreference::Discrete, "") == -1);
  }

  SECTION("GLES renderer validation enforces explicit GPU constraints") {
    std::string reason;
    CHECK(ValidateGlesRendererForGpuPreference(
        "AMD Radeon 610M (radeonsi)", "AMD", GpuPreference::Default, "", reason));
    CHECK(ValidateGlesRendererForGpuPreference(
        "llvmpipe (LLVM 23)", "Mesa", GpuPreference::LlvmPipe, "", reason));
    CHECK_FALSE(ValidateGlesRendererForGpuPreference(
        "NVIDIA GeForce RTX 4070/PCIe/SSE2", "NVIDIA Corporation",
        GpuPreference::LlvmPipe, "", reason));
    CHECK_FALSE(ValidateGlesRendererForGpuPreference(
        "llvmpipe (LLVM 23)", "Mesa", GpuPreference::Discrete, "", reason));
    CHECK_FALSE(ValidateGlesRendererForGpuPreference(
        "AMD Radeon 610M (radeonsi, raphael_mendocino)", "AMD",
        GpuPreference::Discrete, "", reason));
    CHECK_FALSE(ValidateGlesRendererForGpuPreference(
        "NVIDIA GeForce RTX 4070/PCIe/SSE2", "NVIDIA Corporation",
        GpuPreference::Integrated, "", reason));
    CHECK(ValidateGlesRendererForGpuPreference(
        "AMD Radeon 610M (radeonsi, raphael_mendocino)", "AMD",
        GpuPreference::NameMatch, "610M", reason));
    CHECK_FALSE(ValidateGlesRendererForGpuPreference(
        "AMD Radeon 610M (radeonsi)", "AMD",
        GpuPreference::NameMatch, "GeForce", reason));
  }

  SECTION("Vulkan and GLES fall back cleanly to Software when requested GPU is absent") {
    struct ScopedFallbackEnv {
      std::vector<std::string> keys{"GLIDE_NG_HEADLESS", "GLIDE_NG_BACKEND",
                                    "GLIDE_NG_GPU", "GLIDE_NG_MAX_FPS",
                                    "GLIDE_NG_NO_SPLASH"};
      std::vector<std::pair<std::string, std::string>> saved;
      ScopedFallbackEnv() {
        for (const auto& k : keys) {
          const char* v = std::getenv(k.c_str());
          saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        }
      }
      ~ScopedFallbackEnv() {
        for (const auto& kv : saved) {
          if (kv.second == "\x01UNSET") {
            ::unsetenv(kv.first.c_str());
          } else {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
          }
        }
      }
    } guard;

    for (const char* requestedBackend : {"vulkan", "opengl_es"}) {
      INFO("Testing fallback from " << requestedBackend);
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
      ::setenv("GLIDE_NG_BACKEND", requestedBackend, 1);
      ::setenv("GLIDE_NG_GPU", "name:__nonexistent_gpu_adapter__", 1);
      ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
      ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);

      grGlideInit();
      grSstSelect(0);
      REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                           1) == FXTRUE);
      CHECK(BackendManager::GetInstance().GetActiveBackendName() == "software");

      grBufferClear(0xFF123456u, 0xFF, 0xFFFF);
      std::vector<uint32_t> px(640u * 480u, 0u);
      REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                              px.data()) == FXTRUE);
      CHECK((px[0] & 0x00FFFFFFu) == 0x00123456u);

      grSstWinClose();
      grGlideShutdown();
    }
  }
#endif
}

