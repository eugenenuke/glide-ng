/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless Catch2 test suite for the Glide 2.11 (libglide.so / glide.dll)
 * pre-2.3 SST-1 frontend:
 *  - Default Voodoo Graphics (SST-1, 4 MB FB, 1x 2 MB TMU) + API 2.11 profile
 *  - grSstOpen / grSstPassthruMode lifecycle
 *  - Authentic TMU0 VRAM budget enforcement in guTexAllocateMemory
 *  - Fixed-1024-pixel-stride legacy LFB (grLfbBegin/End/WriteMode/Origin/
 *    BypassMode/GetReadPtr/GetWritePtr, cached write-pointer reuse)
 *  - guFbWriteRegion / guFbReadRegion (including wide caller strides >= w*4)
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <glide.h>

#include "core/WrapperConfig.h"
#include "test_helpers.h"

namespace {

struct ScopedEnvOverride {
  std::vector<std::pair<std::string, std::pair<bool, std::string>>> saved;

  void Set(const char* name, const char* value) {
    SaveIfNew(name);
    ::setenv(name, value, 1);
  }

  void Unset(const char* name) {
    SaveIfNew(name);
    ::unsetenv(name);
  }

  ~ScopedEnvOverride() {
    for (const auto& kv : saved) {
      if (kv.second.first) {
        ::setenv(kv.first.c_str(), kv.second.second.c_str(), 1);
      } else {
        ::unsetenv(kv.first.c_str());
      }
    }
  }

 private:
  void SaveIfNew(const char* name) {
    for (const auto& kv : saved) {
      if (kv.first == name) return;
    }
    const char* prev = std::getenv(name);
    if (prev) {
      saved.push_back({name, {true, std::string(prev)}});
    } else {
      saved.push_back({name, {false, ""}});
    }
  }
};

void UnpackRgb565(uint16_t p, int& r, int& g, int& b) {
  r = ((p >> 11) & 0x1F) * 255 / 31;
  g = ((p >> 5) & 0x3F) * 255 / 63;
  b = (p & 0x1F) * 255 / 31;
}

}  // namespace

TEST_CASE("M14 Glide 2.11 Default SST-1 Profile & Hardware Identity",
          "[M14][Glide211][Profile]") {
  ScopedEnvOverride env;
  env.Set("GLIDE_NG_CONFIG", "/dev/null");
  env.Set("GLIDE_NG_HEADLESS", "1");
  env.Set("GLIDE_NG_NO_SPLASH", "1");
  env.Set("GLIDE_NG_BACKEND", "software");
  env.Unset("GLIDE_NG_CARD_MODEL");
  env.Unset("GLIDE_NG_API_VERSION");
  env.Unset("GLIDE_NG_FBI_MEM_MB");
  env.Unset("GLIDE_NG_TMU_COUNT");
  env.Unset("GLIDE_NG_TMU_MEM_MB");
  env.Unset("FX_GLIDE_FBRAM");
  env.Unset("FX_GLIDE_TMU_MEMSIZE");
  env.Unset("SSTV2_NUM_TMUS");

  grGlideInit();

  char ver[128] = {};
  grGlideGetVersion(ver);
  CHECK(std::string(ver).find("2.11") != std::string::npos);

  const auto& cfg = GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
  CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
  CHECK(cfg.fbiMemoryMb == 4u);
  CHECK(cfg.tmuCount == 1u);
  CHECK(cfg.tmuMemoryMb == 2u);

  GrHwConfiguration hw{};
  REQUIRE(grSstQueryHardware(&hw) == FXTRUE);
  REQUIRE(hw.num_sst == 1);
  CHECK(hw.SSTs[0].type == GR_SSTTYPE_VOODOO);
  CHECK(hw.SSTs[0].sstBoard.VoodooConfig.fbRam == 4);
  CHECK(hw.SSTs[0].sstBoard.VoodooConfig.nTexelfx == 1);
  CHECK(hw.SSTs[0].sstBoard.VoodooConfig.tmuConfig[0].tmuRam == 2);

  grGlideShutdown();

  // Verify explicit user overrides still take precedence over the 2.11 default.
  env.Set("GLIDE_NG_CARD_MODEL", "Voodoo2");
  env.Set("GLIDE_NG_API_VERSION", "2.56");
  grGlideInit();
  const auto& overrideCfg =
      GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  CHECK(overrideCfg.model == GlideWrapper::CardModel::Voodoo2);
  CHECK(overrideCfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
  grGlideShutdown();
}

TEST_CASE("M14 Glide 2.11 guTexAllocateMemory Enforces TMU0 VRAM Budget",
          "[M14][Glide211][Textures]") {
  ScopedEnvOverride env;
  env.Set("GLIDE_NG_CONFIG", "/dev/null");
  env.Set("GLIDE_NG_HEADLESS", "1");
  env.Set("GLIDE_NG_NO_SPLASH", "1");
  env.Set("GLIDE_NG_BACKEND", "software");
  env.Unset("GLIDE_NG_CARD_MODEL");
  env.Unset("GLIDE_NG_TMU_MEM_MB");
  env.Unset("FX_GLIDE_TMU_MEMSIZE");

  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstOpen(GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                    GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                    GR_SMOOTHING_ENABLE, 2) == FXTRUE);

  guTexMemReset();

  // Reproduces CyberGladiators' startup texture LRU pool probe:
  // 15 x 256x256 ARGB_1555 (15 * 128 KB = 1920 KB), followed by 128x128
  // ARGB_1555 (32 KB each) until TMU0 2 MB (2048 KB) is exhausted.
  for (int i = 0; i < 15; ++i) {
    GrMipMapId_t id = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 256, 256, GR_TEXFMT_ARGB_1555,
        GR_MIPMAP_DISABLE, GR_LOD_256, GR_LOD_256, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_BILINEAR,
        GR_TEXTUREFILTER_BILINEAR, 0.0f, FXFALSE);
    REQUIRE(id != GR_NULL_MIPMAP_HANDLE);
  }

  int smallCount = 0;
  for (int i = 0; i < 64; ++i) {
    GrMipMapId_t id = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 128, 128, GR_TEXFMT_ARGB_1555,
        GR_MIPMAP_DISABLE, GR_LOD_128, GR_LOD_128, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_BILINEAR,
        GR_TEXTUREFILTER_BILINEAR, 0.0f, FXFALSE);
    if (id == GR_NULL_MIPMAP_HANDLE) {
      break;
    }
    ++smallCount;
  }
  // Remaining 128 KB fits exactly 4 x 32 KB (128x128x16bpp) textures.
  CHECK(smallCount == 4);

  // Resetting TMU memory must reclaim the full 2 MB budget.
  guTexMemReset();
  GrMipMapId_t afterReset = guTexAllocateMemory(
      GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 256, 256, GR_TEXFMT_ARGB_1555,
      GR_MIPMAP_DISABLE, GR_LOD_256, GR_LOD_256, GR_ASPECT_1x1,
      GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_BILINEAR,
      GR_TEXTUREFILTER_BILINEAR, 0.0f, FXFALSE);
  CHECK(afterReset != GR_NULL_MIPMAP_HANDLE);

  grSstWinClose();
  grGlideShutdown();
}

TEST_CASE("M14 Glide 2.11 Legacy LFB (1024-px Stride) & guFbRead/WriteRegion",
          "[M14][Glide211][LFB]") {
  const std::string backend = GENERATE(std::string("vulkan"),
                                       std::string("opengl_es"),
                                       std::string("software"));
  INFO("Testing Glide 2.11 legacy LFB on backend: " << backend);

  ScopedEnvOverride env;
  env.Set("GLIDE_NG_CONFIG", "/dev/null");
  env.Set("GLIDE_NG_HEADLESS", "1");
  env.Set("GLIDE_NG_NO_SPLASH", "1");
  env.Set("GLIDE_NG_BACKEND", backend.c_str());
  env.Unset("GLIDE_NG_CARD_MODEL");
  env.Unset("GLIDE_NG_API_VERSION");

  grGlideInit();
  grSstSelect(0);

  // Intentionally use 512x384: 1024-pixel 16-bit stride is 2048 B == 512 * 4,
  // exercising the regression guard against misclassifying 16-bit reads as BGRA.
  constexpr int kHeight = 384;
  REQUIRE(grSstOpen(GR_RESOLUTION_512x384, GR_REFRESH_60Hz,
                    GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                    GR_SMOOTHING_ENABLE, 2) == FXTRUE);

  // Exercise VGA / SST-1 pass-through switching.
  grSstPassthruMode(GR_PASSTHRU_SHOW_VGA);
  grSstPassthruMode(GR_PASSTHRU_SHOW_SST1);

  // Clear backbuffer to solid blue (GR_COLORFORMAT_ARGB: 0x000000FF = blue).
  grBufferClear(0x000000FFu, 0, GR_WDEPTHVALUE_FARTHEST);

  // 1. Fixed 1024-pixel stride RGB565 write + readback (UPPER_LEFT)
  grLfbWriteMode(GR_LFBWRITEMODE_565);
  grLfbOrigin(GR_ORIGIN_UPPER_LEFT);
  grLfbBypassMode(GR_LFBBYPASS_ENABLE);

  auto* writePtr16 =
      static_cast<uint16_t*>(grLfbGetWritePtr(GR_BUFFER_BACKBUFFER));
  REQUIRE(writePtr16 != nullptr);

  grLfbBegin();
  writePtr16[30 * GR_LFB_STRIDE_PIXELS_211 + 40] = 0xF800u;  // Red
  grLfbEnd();

  const auto* readPtr16 =
      static_cast<const uint16_t*>(grLfbGetReadPtr(GR_BUFFER_BACKBUFFER));
  REQUIRE(readPtr16 != nullptr);

  int r = 0, g = 0, b = 0;
  UnpackRgb565(readPtr16[30 * GR_LFB_STRIDE_PIXELS_211 + 40], r, g, b);
  CHECK(r > 220);
  CHECK(g < 30);
  CHECK(b < 30);

  UnpackRgb565(readPtr16[100 * GR_LFB_STRIDE_PIXELS_211 + 100], r, g, b);
  CHECK(r < 30);
  CHECK(g < 30);
  CHECK(b > 220);

  // 2. Reuse the cached writePtr16 across a second grLfbBegin/End pair without
  //    calling grLfbGetWritePtr() again (authentic SST-1 fixed BAR idiom).
  grLfbBegin();
  writePtr16[60 * GR_LFB_STRIDE_PIXELS_211 + 80] = 0x07E0u;  // Green
  grLfbEnd();

  readPtr16 =
      static_cast<const uint16_t*>(grLfbGetReadPtr(GR_BUFFER_BACKBUFFER));
  REQUIRE(readPtr16 != nullptr);

  UnpackRgb565(readPtr16[60 * GR_LFB_STRIDE_PIXELS_211 + 80], r, g, b);
  CHECK(r < 30);
  CHECK(g > 220);
  CHECK(b < 30);

  // Previous red pixel must remain intact.
  UnpackRgb565(readPtr16[30 * GR_LFB_STRIDE_PIXELS_211 + 40], r, g, b);
  CHECK(r > 220);
  CHECK(g < 30);
  CHECK(b < 30);

  // 3. 32-bit ARGB8888 write with GR_ORIGIN_LOWER_LEFT at (20, 10) -> upper-left
  //    (20, kHeight - 1 - 10) == (20, 373).
  grLfbWriteMode(GR_LFBWRITEMODE_8888);
  grLfbOrigin(GR_ORIGIN_LOWER_LEFT);
  grLfbBegin();
  auto* writePtr32 =
      static_cast<uint32_t*>(grLfbGetWritePtr(GR_BUFFER_BACKBUFFER));
  REQUIRE(writePtr32 != nullptr);
  writePtr32[10 * GR_LFB_STRIDE_PIXELS_211 + 20] = 0xFFFFFF00u;  // Yellow
  grLfbEnd();

  grLfbOrigin(GR_ORIGIN_UPPER_LEFT);
  readPtr16 =
      static_cast<const uint16_t*>(grLfbGetReadPtr(GR_BUFFER_BACKBUFFER));
  REQUIRE(readPtr16 != nullptr);

  UnpackRgb565(readPtr16[(kHeight - 1 - 10) * GR_LFB_STRIDE_PIXELS_211 + 20], r,
               g, b);
  CHECK(r > 220);
  CHECK(g > 220);
  CHECK(b < 30);

  // 4. guFbWriteRegion + guFbReadRegion with wide caller stride (>= w * 4).
  constexpr int kBoxW = 16;
  constexpr int kBoxH = 16;
  std::vector<uint16_t> magentaBlock(kBoxW * kBoxH, 0xF81Fu);  // Magenta
  grLfbWriteMode(GR_LFBWRITEMODE_565);
  grLfbOrigin(GR_ORIGIN_UPPER_LEFT);
  guFbWriteRegion(200, 120, kBoxW, kBoxH, magentaBlock.data(), kBoxW * 2);

  constexpr int kWideStrideBytes = kBoxW * 8;  // 128 B (>= kBoxW * 4)
  std::vector<uint8_t> wideReadback(kWideStrideBytes * kBoxH, 0u);
  guFbReadRegion(200, 120, kBoxW, kBoxH, wideReadback.data(), kWideStrideBytes);

  uint16_t centerSample = 0;
  std::memcpy(&centerSample,
              wideReadback.data() + 8 * kWideStrideBytes + 8 * sizeof(uint16_t),
              sizeof(uint16_t));
  UnpackRgb565(centerSample, r, g, b);
  CHECK(r > 220);
  CHECK(g < 30);
  CHECK(b > 220);

  // Verify 3D draw + buffer swap also succeeds on the opened Glide 2.11 context.
  GrVertex v0{}, v1{}, v2{};
  v0.x = 260.0f; v0.y = 40.0f; v0.oow = 1.0f; v0.r = 255.0f; v0.g = 255.0f; v0.b = 255.0f; v0.a = 255.0f;
  v1.x = 340.0f; v1.y = 40.0f; v1.oow = 1.0f; v1.r = 255.0f; v1.g = 255.0f; v1.b = 255.0f; v1.a = 255.0f;
  v2.x = 300.0f; v2.y = 110.0f; v2.oow = 1.0f; v2.r = 255.0f; v2.g = 255.0f; v2.b = 255.0f; v2.a = 255.0f;
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  guDrawTriangleWithClip(&v0, &v1, &v2);
  grBufferSwap(0);

  grSstWinClose();
  grGlideShutdown();
}
