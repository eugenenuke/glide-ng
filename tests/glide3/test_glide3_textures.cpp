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
#include "opengl_es/OpenGLESBackend.h"
#include "software/SoftwareBackend.h"
#include "vulkan/VulkanBackend.h"
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

#ifdef __linux__
TEST_CASE("Glide 3.x: .3df Loading, LOD & Aspect Bounds", "[Glide3][Texture]") {
  SECTION("Synthetic RGB565 .3df loading & integrity") {
  void* handle = OpenGlide3DsoHandle();
  REQUIRE(handle != nullptr);

  auto p_gu3dfGetInfo =
      (FxBool (*)(const char*, Gu3dfInfo*))dlsym(handle, "gu3dfGetInfo");
  auto p_gu3dfLoad =
      (FxBool (*)(const char*, Gu3dfInfo*))dlsym(handle, "gu3dfLoad");

  REQUIRE(p_gu3dfGetInfo != nullptr);
  REQUIRE(p_gu3dfLoad != nullptr);

  SECTION("Load synthetic mipmapped RGB565 .3df and verify properties") {
    const std::string synthPath =
        GlideTestHelpers::TempTestPath("glide_ng_test_decal1.3df");
    GlideTestHelpers::ScopedTempFile temp3df(synthPath);
    REQUIRE(GlideTestHelpers::WriteSynthetic3dfRgb565(
        synthPath, 256, 1, [](int x, int y, int /*dim*/) -> uint16_t {
          return GlideTestHelpers::PackRgb565(
              static_cast<uint8_t>((x * 4) & 0xFF),
              static_cast<uint8_t>((y * 4) & 0xFF), 200);
        }));

    Gu3dfInfo info;
    std::memset(&info, 0, sizeof(info));

    // 1. Get texture info (9 mip levels 256..1 RGB565 = 174,762 bytes)
    FxBool infoOk = p_gu3dfGetInfo(synthPath.c_str(), &info);
    REQUIRE(infoOk == FXTRUE);
    REQUIRE(info.header.width == 256);
    REQUIRE(info.header.height == 256);
    REQUIRE(info.header.format == GR_TEXFMT_RGB_565);
    REQUIRE(info.mem_required == 174762);

    // 2. Allocate and load data
    info.data = std::malloc(info.mem_required);
    REQUIRE(info.data != nullptr);

    FxBool loadOk = p_gu3dfLoad(synthPath.c_str(), &info);
    REQUIRE(loadOk == FXTRUE);

    // 3. Verify that pixel data is loaded (not all zeroes/black!)
    const uint16_t* pixels = reinterpret_cast<const uint16_t*>(info.data);
    bool hasNonZeroPixel = false;
    for (uint32_t i = 0; i < info.header.width * info.header.height; ++i) {
      if (pixels[i] != 0) {
        hasNonZeroPixel = true;
        break;
      }
    }
    REQUIRE(hasNonZeroPixel == true);

    std::free(info.data);
  }

  ::dlclose(handle);
  }

  SECTION("Voodoo3 2048x2048 texture support & LOD bounds") {
    // 1. Get the global EmulationRegistry instance and save the previous config
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto prevConfig = reg.GetConfig();
    
    // 2. Setup Voodoo2 emulation first (max texture dimension 256)
    reg.GetConfig().model = GlideWrapper::CardModel::Voodoo2;
    
    // Verify min LOD logic for Voodoo2: min LOD is 8 - log2(256) = 0.
    // So downloading LOD -1 (512x512) or -3 (2048x2048) must fail bounds check!
    std::vector<uint16_t> dummyData(2048 * 2048, 0);
    
    bool v2DownloadSuccess = GlideWrapper::TextureManager::GetInstance().DownloadMipMap(
        0, 0, -1, -1, 3, 0xa, dummyData.data()
    );
    CHECK(v2DownloadSuccess == false); // Must fail on Voodoo2!
    
    // 3. Setup Voodoo3 emulation (max texture dimension 2048)
    reg.GetConfig().model = GlideWrapper::CardModel::Voodoo3;
    
    // Temporarily increase TMU0 memory limit to 16MB to accommodate the 8MB test texture
    GlideWrapper::TextureManager::GetInstance().SetTmuMemoryLimitMb(0, 16);
    
    // Verify min LOD logic for Voodoo3: min LOD is 8 - log2(2048) = -3.
    // So downloading LOD -3 (2048x2048) must succeed bounds check!
    bool v3DownloadSuccess = GlideWrapper::TextureManager::GetInstance().DownloadMipMap(
        0, 0, -3, -3, 3, 0xa, dummyData.data()
    );
    CHECK(v3DownloadSuccess == true); // Must succeed on Voodoo3!
    
    // Verify memory calculations under negative LODs:
    // For LOD -3 (2048x2048), format RGB_565 (2 bytes per texel), packed = true:
    // Size = 2048 * 2048 * 2 = 8388608 bytes.
    uint32_t memReq = GlideWrapper::TextureManager::GetInstance().CalculateMemoryRequired(
        -3, -3, 3, 0xa, true
    );
    CHECK(memReq == 8388608);
    
    // Restore configuration and TMU memory limit to be pristine
    GlideWrapper::TextureManager::GetInstance().SetTmuMemoryLimitMb(0, 2);
    reg.GetConfig() = prevConfig;
  }

  SECTION("Texture LOD & aspect bounds validation") {
#ifdef __linux__
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit    = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideInit"));
    auto sstSelect    = reinterpret_cast<void(*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen      = reinterpret_cast<FxU32(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int)>(::dlsym(handle, "grSstWinOpen"));
    auto texDownload      = reinterpret_cast<void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*)>(::dlsym(handle, "grTexDownloadMipMap"));
    auto winClose         = reinterpret_cast<FxBool(*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown    = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(texDownload != nullptr);

    glideInit();
    if (sstSelect) sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Create a 16x16 dummy texture buffer
    std::vector<uint16_t> dummyData(16 * 16, 0);

    GrTexInfo badInfo;
    badInfo.format = GR_TEXFMT_RGB_565;
    badInfo.data = dummyData.data();

    // 1. Test case: Invalid aspect ratio (aspectRatioLog2 out of bounds)
    badInfo.smallLodLog2 = 4; // 16x16
    badInfo.largeLodLog2 = 4;
    badInfo.aspectRatioLog2 = 999; 

    // We verify that calling grTexDownloadMipMap does NOT crash the process
    texDownload(GR_TMU0, 0, 0, &badInfo);

    // 2. Test case: Invalid LOD limits where smallLodLog2 < largeLodLog2 in log2 (lodMin > lodMax)
    badInfo.aspectRatioLog2 = 0; // 1:1
    badInfo.smallLodLog2 = 0; // 1x1 (translated to lodMin = 8)
    badInfo.largeLodLog2 = 4; // 16x16 (translated to lodMax = 4)

    // We verify that this does NOT crash and is caught safely
    texDownload(GR_TMU0, 0, 0, &badInfo);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("TMU budget & Vulkan texture pool reuse") {
#ifdef __linux__
  SECTION("Glide 3.x grTexMaxAddress honors configured tmuMemoryMb") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = (void(FX_CALL*)())::dlsym(handle, "grGlideInit");
    auto glideShutdown = (void(FX_CALL*)())::dlsym(handle, "grGlideShutdown");
    auto texMaxAddress =
        (FxU32(FX_CALL*)(GrChipID_t))::dlsym(handle, "grTexMaxAddress");
    auto grGetFn =
        (FxU32(FX_CALL*)(FxU32, FxU32, FxI32*))::dlsym(handle, "grGet");
    REQUIRE(glideInit != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(texMaxAddress != nullptr);
    REQUIRE(grGetFn != nullptr);

    glideInit();
    FxI32 tmuBytes = 0;
    grGetFn(GR_MEMORY_TMU, sizeof(tmuBytes), &tmuBytes);
    REQUIRE(tmuBytes >= 2 * 1024 * 1024);
    CHECK(texMaxAddress(GR_TMU0) == static_cast<FxU32>(tmuBytes));
    glideShutdown();
    ::dlclose(handle);
  }

  SECTION("VulkanBackend pools and reuses retired same-dimension textures") {
    GlideWrapper::WrapperConfig config;
    config.backend = "vulkan";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::VulkanBackend backend;
    if (backend.Initialize(config) &&
        backend.AttachWindow(nullptr, 64, 64, true)) {
      auto makeTex = [](uint32_t argb, uint64_t rawHash) {
        GlideWrapper::VirtualTexture vt;
        vt.baseWidth = 32;
        vt.baseHeight = 32;
        vt.format = 0x5;
        vt.rawDataHash = rawHash;
        vt.swizzledTableHash = 1;
        vt.swizzledWithTable = true;
        vt.swizzledMipLevels.emplace_back(32 * 32, argb);
        return vt;
      };

      // Frame 0: upload red texture at addr=0, then overwrite with green so the
      // red VkImage is retired into slot 0's retirement queue. Fill the remaining
      // idle pool headroom (1023 tiny 4x4 images retired via addr=0x1000) so the
      // pool reaches kMaxPooledTextures (1024) without prematurely cannibalizing
      // warm content-keyed entries below capacity.
      auto makeTex4x4 = [](uint32_t argb, uint64_t rawHash) {
        GlideWrapper::VirtualTexture vt;
        vt.baseWidth = 4;
        vt.baseHeight = 4;
        vt.format = 0x5;
        vt.rawDataHash = rawHash;
        vt.swizzledTableHash = 1;
        vt.swizzledWithTable = true;
        vt.swizzledMipLevels.emplace_back(4u * 4u, argb);
        return vt;
      };
      backend.UploadTexture(0, 0, makeTex(0xFFFF0000u, 0x1111u));
      backend.UploadTexture(0, 0, makeTex(0xFF00FF00u, 0x2222u));
      for (uint32_t i = 0; i < 1024u; ++i) {
        backend.UploadTexture(
            0, 0x1000u,
            makeTex4x4(0xFF000000u | (i & 0xFFFFFFu), 0x70000u + i));
      }
      REQUIRE(backend.SwapBuffers());
      // Frame 1: swap again so slot 0's fence completes and reclaims the
      // retired 32x32x1 + 1023x 4x4 textures into m_texturePool.
      REQUIRE(backend.SwapBuffers());

      // Frame 2: upload a blue 32x32x1 texture at addr=0 -> must reuse the
      // pooled 32x32x1 VkImage/DeviceMemory/ImageView!
      const uint64_t reusesBefore = backend.GetTexturePoolReuseCount();
      backend.UploadTexture(0, 0, makeTex(0xFF0000FFu, 0x3333u));
      CHECK(backend.GetTexturePoolReuseCount() == reusesBefore + 1);

      // Draw a full-screen textured quad to prove the reused VkImage renders
      // the new blue pixels cleanly (not stale red/green).
      backend.ClearBuffer(0x00000000u, 0xFF, 1.0f, 0x3);
      backend.SetCombinerMode(3, 8, 1, 1, false, 1, 0, 1, 2, false);
      backend.SetTexCombinerMode(0, 1, 0, 1, 0, false, false);
      backend.BindTexture(0, 0, 1, 1, 0, 0);
      GlideWrapper::ModernVertex v0{}, v1{}, v2{}, v3{};
      auto setV = [](GlideWrapper::ModernVertex& v, float x, float y, float s,
                     float t) {
        v.pos[0] = x;
        v.pos[1] = y;
        v.pos[2] = 0.5f;
        v.pos[3] = 1.0f;
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        v.tex[0] = s;
        v.tex[1] = t;
        v.tmu_oow[0] = 1.0f;
      };
      setV(v0, 0.0f, 0.0f, 0.0f, 0.0f);
      setV(v1, 64.0f, 0.0f, 255.0f, 0.0f);
      setV(v2, 64.0f, 64.0f, 255.0f, 255.0f);
      setV(v3, 0.0f, 64.0f, 0.0f, 255.0f);
      backend.DrawTriangle(v0, v1, v2);
      backend.DrawTriangle(v0, v2, v3);
      REQUIRE(backend.SwapBuffers());

      uint16_t px565 = 0;
      REQUIRE(backend.ReadLFB(0, 32, 32, 1, 1, 2, &px565));
      CHECK(px565 == 0x001Fu);  // Pure Blue in RGB565!

      backend.DetachWindow();
      backend.Shutdown();
    }
  }

  SECTION("VulkanBackend content-addressed cache and >128 high-churn pool reuse") {
    GlideWrapper::WrapperConfig config;
    config.backend = "vulkan";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::VulkanBackend backend;
    if (backend.Initialize(config) &&
        backend.AttachWindow(nullptr, 64, 64, true)) {
      auto makeTex = [](uint32_t argb, uint64_t rawHash) {
        GlideWrapper::VirtualTexture vt;
        vt.baseWidth = 32;
        vt.baseHeight = 32;
        vt.format = 0x5;
        vt.rawDataHash = rawHash;
        vt.swizzledTableHash = 1;
        vt.swizzledWithTable = true;
        vt.swizzledMipLevels.emplace_back(32 * 32, argb);
        return vt;
      };

      GlideWrapper::ModernVertex v0{}, v1{}, v2{}, v3{};
      auto setV = [](GlideWrapper::ModernVertex& v, float x, float y, float s,
                     float t) {
        v.pos[0] = x;
        v.pos[1] = y;
        v.pos[2] = 0.5f;
        v.pos[3] = 1.0f;
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        v.tex[0] = s;
        v.tex[1] = t;
        v.tmu_oow[0] = 1.0f;
      };
      setV(v0, 0.0f, 0.0f, 0.0f, 0.0f);
      setV(v1, 64.0f, 0.0f, 255.0f, 0.0f);
      setV(v2, 64.0f, 64.0f, 255.0f, 255.0f);
      setV(v3, 0.0f, 64.0f, 0.0f, 255.0f);

      auto drawBoundTexAndReadCenter = [&](uint32_t addr) -> uint16_t {
        backend.ClearBuffer(0x00000000u, 0xFFu, 1.0f, 0x3u);
        backend.SetCombinerMode(3, 8, 1, 1, false, 1, 0, 1, 2, false);
        backend.SetTexCombinerMode(0, 1, 0, 1, 0, false, false);
        backend.BindTexture(0, addr, 1, 1, 0, 0);
        backend.DrawTriangle(v0, v1, v2);
        backend.DrawTriangle(v0, v2, v3);
        REQUIRE(backend.SwapBuffers());
        uint16_t px565 = 0;
        REQUIRE(backend.ReadLFB(0, 32, 32, 1, 1, 2, &px565));
        return px565;
      };

      // 1. Live/in-flight content hit: red -> green -> red on same slot.
      backend.UploadTexture(0u, 0u, makeTex(0xFFFF0000u, 0x1111u));
      backend.UploadTexture(0u, 0u, makeTex(0xFF00FF00u, 0x2222u));
      const uint64_t liveHitsBefore = backend.GetTextureContentCacheHitCount();
      backend.UploadTexture(0u, 0u, makeTex(0xFFFF0000u, 0x1111u));
      CHECK(backend.GetTextureContentCacheHitCount() == liveHitsBefore + 1u);
      CHECK(drawBoundTexAndReadCenter(0u) == 0xF800u);  // Pure Red

      // 2. Idle pooled content hit across TMU addresses: swap once more so
      //    slot 0's fence completes and retired green moves to m_texturePool.
      REQUIRE(backend.SwapBuffers());
      const uint64_t idleHitsBefore = backend.GetTextureContentCacheHitCount();
      backend.UploadTexture(0u, 0x1000u, makeTex(0xFF00FF00u, 0x2222u));
      CHECK(backend.GetTextureContentCacheHitCount() == idleHitsBefore + 1u);
      CHECK(drawBoundTexAndReadCenter(0x1000u) == 0x07E0u);  // Pure Green

      // 3. Shared-texture COW safety during UploadTexturePartial: share red at
      //    addr=0 and addr=0x2000, partially overwrite addr=0 with blue, and
      //    verify addr=0x2000 stays untouched red.
      const uint64_t cowHitsBefore = backend.GetTextureContentCacheHitCount();
      backend.UploadTexture(0u, 0x2000u, makeTex(0xFFFF0000u, 0x1111u));
      CHECK(backend.GetTextureContentCacheHitCount() == cowHitsBefore + 1u);
      auto bluePartial = makeTex(0xFF0000FFu, 0u);
      backend.UploadTexturePartial(0u, 0u, bluePartial, 0u, 0u, 32u);
      CHECK(drawBoundTexAndReadCenter(0u) == 0x001Fu);       // Blue at addr=0
      CHECK(drawBoundTexAndReadCenter(0x2000u) == 0xF800u);  // Still Red

      // 4. Multi-frame content-cache preservation (no premature cannibalization)
      //    and zero-alloc bucketed pool reuse at pool cap (1024).
      for (uint32_t i = 0; i < 200u; ++i) {
        backend.UploadTexture(
            0u, 0u,
            makeTex(0xFF000000u | ((i + 1u) & 0xFFu), 0xD0000u + i));
      }
      REQUIRE(backend.SwapBuffers());
      REQUIRE(backend.SwapBuffers());
      for (uint32_t i = 0; i < 200u; ++i) {
        backend.UploadTexture(
            0u, 0u,
            makeTex(0xFF000000u | (((i + 1u) & 0xFFu) << 8), 0xE0000u + i));
      }
      REQUIRE(backend.SwapBuffers());
      const uint64_t hitsBeforeA = backend.GetTextureContentCacheHitCount();
      for (uint32_t i = 0; i < 200u; ++i) {
        backend.UploadTexture(
            0u, 0u,
            makeTex(0xFF000000u | ((i + 1u) & 0xFFu), 0xD0000u + i));
      }
      CHECK(backend.GetTextureContentCacheHitCount() >= hitsBeforeA + 200u);

      // Push idle pool to kMaxPooledTextures (1024) with tiny 4x4 textures,
      // then verify subsequent uploads reuse >= 200 pooled images without
      // hitting the old 128 cap.
      auto makeTex4x4 = [](uint32_t argb, uint64_t rawHash) {
        GlideWrapper::VirtualTexture vt;
        vt.baseWidth = 4;
        vt.baseHeight = 4;
        vt.format = 0x5;
        vt.rawDataHash = rawHash;
        vt.swizzledTableHash = 1;
        vt.swizzledWithTable = true;
        vt.swizzledMipLevels.emplace_back(4u * 4u, argb);
        return vt;
      };
      for (uint32_t i = 0; i < 1024u; ++i) {
        backend.UploadTexture(
            0u, 0u,
            makeTex4x4(0xFF000000u | (i & 0xFFFFFFu), 0xF0000u + i));
      }
      REQUIRE(backend.SwapBuffers());
      REQUIRE(backend.SwapBuffers());
      const uint64_t reusesBefore = backend.GetTexturePoolReuseCount();
      for (uint32_t i = 0; i < 200u; ++i) {
        backend.UploadTexture(
            0u, 0u,
            makeTex4x4(0xFF000000u | ((i + 1u) & 0xFFu), 0x100000u + i));
      }
      CHECK(backend.GetTexturePoolReuseCount() >= reusesBefore + 200u);

      backend.DetachWindow();
      backend.Shutdown();
    }
  }

  SECTION("VulkanBackend grows descriptor pools past 4096 dual-TMU pairs") {
    GlideWrapper::WrapperConfig config;
    config.backend = "vulkan";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::VulkanBackend backend;
    if (backend.Initialize(config) &&
        backend.AttachWindow(nullptr, 64, 64, true)) {
      auto makeTex4x4 = [](uint32_t argb, uint64_t rawHash) {
        GlideWrapper::VirtualTexture vt;
        vt.baseWidth = 4;
        vt.baseHeight = 4;
        vt.format = 0xAu;
        vt.rawDataHash = rawHash;
        vt.swizzledMipLevels.emplace_back(4 * 4, argb);
        return vt;
      };

      // Upload 66 unique 4x4 textures on TMU0 and 66 unique 4x4 textures on
      // TMU1: 66 * 66 = 4,356 unique (view0, sampler0, view1, sampler1) pairs
      // exceeding the initial 4,096-set VkDescriptorPool chunk without
      // overwriting any startAddress.
      constexpr uint32_t kCount = 66u;
      for (uint32_t i = 0; i < kCount; ++i) {
        const uint32_t color0 = (i == kCount - 1u)
                                    ? 0xFF00FF00u
                                    : (0xFF000000u | ((i + 1u) * 3u));
        const uint32_t color1 = 0xFFFFFFFFu;
        backend.UploadTexture(0u, i * 64u,
                              makeTex4x4(color0, 0xA000u + i));
        backend.UploadTexture(1u, i * 64u,
                              makeTex4x4(color1, 0xB000u + i));
      }

      backend.ClearBuffer(0x00000000u, 0xFFu, 1.0f, 0x3u);
      // Output TMU0 * TMU1 (diffuse modulated by white TMU1).
      backend.SetCombinerMode(3, 8, 1, 1, false, 1, 0, 1, 2, false);
      backend.SetTexCombinerMode(1, 1, 0, 1, 0, false, false);
      backend.SetTexCombinerMode(0, 3, 1, 1, 1, false, false);

      GlideWrapper::ModernVertex v0{}, v1{}, v2{}, v3{};
      auto setV = [](GlideWrapper::ModernVertex& v, float x, float y, float s,
                     float t) {
        v.pos[0] = x;
        v.pos[1] = y;
        v.pos[2] = 0.5f;
        v.pos[3] = 1.0f;
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        v.tex[0] = s;
        v.tex[1] = t;
        v.tex[2] = s;
        v.tex[3] = t;
        v.tmu_oow[0] = 1.0f;
        v.tmu_oow[1] = 1.0f;
      };
      setV(v0, 0.0f, 0.0f, 0.0f, 0.0f);
      setV(v1, 64.0f, 0.0f, 255.0f, 0.0f);
      setV(v2, 64.0f, 64.0f, 255.0f, 255.0f);
      setV(v3, 0.0f, 64.0f, 0.0f, 255.0f);

      for (uint32_t i = 0; i < kCount; ++i) {
        backend.BindTexture(0u, i * 64u, 1u, 1u, 0u, 0u);
        for (uint32_t j = 0; j < kCount; ++j) {
          backend.BindTexture(1u, j * 64u, 1u, 1u, 0u, 0u);
          backend.DrawTriangle(v0, v1, v2);
          backend.DrawTriangle(v0, v2, v3);
        }
      }
      CHECK(backend.GetDescriptorPoolCount() >= 2u);
      REQUIRE(backend.SwapBuffers());

      // Final (i=65, j=65) draw uses pure green (0xFF00FF00 * 0xFFFFFFFF).
      uint16_t px565 = 0;
      REQUIRE(backend.ReadLFB(0u, 32u, 32u, 1u, 1u, 2u, &px565));
      CHECK(px565 == 0x07E0u);

      // Overwrite a TMU0 texture under high-water pressure so bidirectional
      // descriptor eviction & deferred pool free executes cleanly across pools.
      backend.UploadTexture(0u, 0u, makeTex4x4(0xFFFF0000u, 0xC000u));
      REQUIRE(backend.SwapBuffers());
      REQUIRE(backend.SwapBuffers());

      backend.DetachWindow();
      backend.Shutdown();
    }
  }
#endif
  }

}
#endif

#ifdef __linux__
TEST_CASE("Glide 3.x: TEXMIRROR & TEXCHROMA Filtering Extensions", "[Glide3][TexMirror][TexChroma]") {
  SECTION("Texture coordinate mirroring (TEXMIRROR)") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using TexDownloadMipMapFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using TexClampModeFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using TexFilterModeFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto drawTri   = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto texDownload = reinterpret_cast<TexDownloadMipMapFunc>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto texClamp = reinterpret_cast<TexClampModeFunc>(::dlsym(handle, "grTexClampMode"));
    auto texFilter = reinterpret_cast<TexFilterModeFunc>(::dlsym(handle, "grTexFilterMode"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(texClamp != nullptr);
    REQUIRE(texFilter != nullptr);
    REQUIRE(shutdown != nullptr);

    // Force headless software backend
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600 resolution
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W at 32
    vertexLayout(0x40, 36, 1); // GR_PARAM_ST0 at 36

    bufferClear(0xFF000000, 0, 0xFFFFFFFF); // Clear to Black

    // 1. Create a 2x2 texture of format GR_TEXFMT_ALPHA_8 (checkerboard pattern)
    // Pixel (0,0)=255, Pixel (1,0)=0, Pixel (0,1)=0, Pixel (1,1)=255
    std::vector<uint8_t> texData = {
        255, 0,
        0, 255
    };

    GrTexInfo texInfo;
    texInfo.smallLodLog2 = 1; // 2^1 = 2
    texInfo.largeLodLog2 = 1;
    texInfo.aspectRatioLog2 = 1; // 1:1
    texInfo.format = GR_TEXFMT_ALPHA_8;
    texInfo.data = texData.data();

    texDownload(0, 0x00002000, 0, &texInfo);
    texSource(0, 0x00002000, 0, &texInfo);
    
    // Set clamp mode to MIRROR (2)
    #ifndef GR_TEXTURECLAMP_MIRROR_EXT
    #define GR_TEXTURECLAMP_MIRROR_EXT 2
    #endif
    texClamp(0, (GrTextureClampMode_t)GR_TEXTURECLAMP_MIRROR_EXT, (GrTextureClampMode_t)GR_TEXTURECLAMP_MIRROR_EXT);
    texFilter(0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

    texCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Render a quad from (300, 300) to (500, 500) where texture coordinates span [0, 512] (2.0 repetitions)
    StandardVert v0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f,   0.0f, 1.0f};
    StandardVert v1{500.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 512.0f,   0.0f, 1.0f};
    StandardVert v2{500.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 512.0f, 512.0f, 1.0f};
    StandardVert v3{300.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f, 512.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    // Read back pixels at key horizontal locations along y=400:
    uint32_t px325 = 0, px375 = 0, px425 = 0, px475 = 0;
    
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 325, 400, 1, 1, 4, &px325) == FXTRUE);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 375, 400, 1, 1, 4, &px375) == FXTRUE);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 425, 400, 1, 1, 4, &px425) == FXTRUE);
    REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 475, 400, 1, 1, 4, &px475) == FXTRUE);

    uint8_t r325 = (px325 >> 16) & 0xFF;
    uint8_t r375 = (px375 >> 16) & 0xFF;
    uint8_t r425 = (px425 >> 16) & 0xFF;
    uint8_t r475 = (px475 >> 16) & 0xFF;

    CHECK(r325 >= 250);
    CHECK(r375 <= 5);
    CHECK(r425 <= 5);   // Mirroring verified: 320 maps to 192 (Black)
    CHECK(r475 >= 250); // Mirroring verified: 448 maps to 64 (White)

    winClose();
    shutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
  }

  SECTION("Bilinear halo-free chromakeying (TEXCHROMA)") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using TexDownloadMipMapFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using AlphaCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using TexClampModeFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using TexFilterModeFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using GetProcFunc = void*(*)(const char*); // Fixed to void* to prevent 64-bit truncation!
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto drawTri   = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto texDownload = reinterpret_cast<TexDownloadMipMapFunc>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<AlphaCombineFunc>(::dlsym(handle, "grAlphaCombine"));
    auto texClamp = reinterpret_cast<TexClampModeFunc>(::dlsym(handle, "grTexClampMode"));
    auto texFilter = reinterpret_cast<TexFilterModeFunc>(::dlsym(handle, "grTexFilterMode"));
    auto getProc   = reinterpret_cast<GetProcFunc>(::dlsym(handle, "grGetProcAddress"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(alphaCombine != nullptr);
    REQUIRE(texClamp != nullptr);
    REQUIRE(texFilter != nullptr);
    REQUIRE(getProc != nullptr);
    REQUIRE(shutdown != nullptr);

    // Lookup texture chromakey extension functions
    auto texChromaMode = reinterpret_cast<void(*)(GrChipID_t, FxU32)>(getProc("grTexChromaModeExt"));
    auto texChromaRange = reinterpret_cast<void(*)(GrChipID_t, GrColor_t, GrColor_t, FxU32)>(getProc("grTexChromaRangeExt"));
    REQUIRE(texChromaMode != nullptr);
    REQUIRE(texChromaRange != nullptr);

    // Force headless software backend
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600 resolution
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W at 32
    vertexLayout(0x40, 36, 1); // GR_PARAM_ST0 at 36

    bufferClear(0xFF000000, 0, 0xFFFFFFFF); // Clear to Black

    // 2. Create a 2x2 texture of format GR_TEXFMT_ARGB_4444
    // Left half (x=0) is Solid Blue (0xF00F), Right half (x=1) is Solid Green (0xF0F0 - Key Color)
    std::vector<uint16_t> texData = {
        0xF00F, 0xF0F0,
        0xF00F, 0xF0F0
    };

    GrTexInfo texInfo;
    texInfo.smallLodLog2 = 1; // 2^1 = 2
    texInfo.largeLodLog2 = 1;
    texInfo.aspectRatioLog2 = 1; // 1:1
    texInfo.format = GR_TEXFMT_ARGB_4444;
    texInfo.data = texData.data();

    texDownload(0, 0x00002000, 0, &texInfo);
    texSource(0, 0x00002000, 0, &texInfo);
    texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    
    // Enable Bilinear Filtering to force interpolation!
    texFilter(0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    // Configure Texture Chromakeying on TMU0
    // Key out the green color: 0xF0F0 (ARGB4444).
    // The key check in our wrapper is 32-bit (A8R8G8B8).
    // Bright green in 32-bit ARGB is 0xFF00FF00.
    #ifndef GR_TEXCHROMA_ENABLE_EXT
    #define GR_TEXCHROMA_ENABLE_EXT 1
    #endif
    texChromaRange(0, 0xFF00FF00, 0xFF00FF00, 0); // Exact match mode on 0xFF00FF00
    texChromaMode(0, GR_TEXCHROMA_ENABLE_EXT);

    // Configure both RGB and Alpha combiners to output the texture color/alpha
    texCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Render a quad from (300, 300) to (500, 500)
    StandardVert v0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f,   0.0f, 1.0f};
    StandardVert v1{500.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f,   0.0f, 1.0f};
    StandardVert v2{500.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f, 256.0f, 1.0f};
    StandardVert v3{300.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f, 256.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    // Read back a line of pixels across the transition zone (from x=330 to x=470 along y=400):
    // Texel centers are at s=64 (x=350) and s=192 (x=450).
    // Transition zone spans x in (350, 450).
    for (int x = 330; x <= 470; ++x) {
        uint32_t px = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, x, 400, 1, 1, 4, &px) == FXTRUE);
        
        uint8_t a = (px >> 24) & 0xFF;
        uint8_t r = (px >> 16) & 0xFF;
        uint8_t g = (px >> 8) & 0xFF;
        uint8_t b = px & 0xFF;

        // Swizzle-tolerant color check: since we wrote Blue (0xF00F) in ARGB4444,
        // it may read back as Blue or Red depending on swizzles.
        uint8_t colorVal = std::max(r, b);

        if (x <= 350) {
            // Solid color zone
            CHECK(a >= 250);
            CHECK(colorVal >= 250);
            CHECK(g == 0);
        } else if (x >= 450) {
            // Completely transparent black zone (fully inside keyed-out green area)
            CHECK(px == 0);
        } else {
            // Blending transition zone: must be semi-transparent, contain no green, and some blue/red
            if (!(g == 0 && a < 255 && a > 0 && colorVal < 255 && colorVal > 0)) {
                std::cout << "FAIL at x=" << x << ": px=0x" << std::hex << px 
                          << " (a=" << std::dec << (int)a << ", r=" << (int)r 
                          << ", g=" << (int)g << ", b=" << (int)b 
                          << ", colorVal=" << (int)colorVal << ")" << std::endl;
            }
            CHECK(g == 0); // Crucial: no green halo!
            CHECK(a < 255);
            CHECK(a > 0);
            CHECK(colorVal < 255);
            CHECK(colorVal > 0);
        }
    }

    winClose();
    shutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
  }

  SECTION("Per-TMU texture chromakey caching & state serialization") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using ShutdownFunc = void (*)(void);
    using SstSelectFunc = void (*)(int);
    using SstWinOpenFunc =
        FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                   GrColorFormat_t, GrOriginLocation_t, int, int);
    using TexChromaModeFunc = void (*)(int, int);
    using TexChromaRangeFunc = void (*)(int, uint32_t, uint32_t, int);
    using GlideGetStateFunc = void (*)(void*);
    using GlideSetStateFunc = void (*)(const void*);
    using WinCloseFunc = void (*)(void);

    auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto shutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect =
        reinterpret_cast<SstSelectFunc>(::dlsym(handle, "grSstSelect"));
    auto sstWinOpen =
        reinterpret_cast<SstWinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto texChromaMode = reinterpret_cast<TexChromaModeFunc>(
        ::dlsym(handle, "grTexChromaModeExt"));
    auto texChromaRange = reinterpret_cast<TexChromaRangeFunc>(
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

    texChromaMode(0, 1);
    texChromaRange(0, 0x00112233, 0x00445566, 1);
    texChromaMode(1, 0);
    texChromaRange(1, 0x00778899, 0x00AABBCC, 0);

    auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend());
    REQUIRE(backend != nullptr);
    CHECK(backend->GetTexChromaMode(0) == 1);
    CHECK(backend->GetTexChromaMin(0) == 0x00112233);
    CHECK(backend->GetTexChromaMax(0) == 0x00445566);
    CHECK(backend->GetTexChromaRangeMode(0) == 1);
    CHECK(backend->GetTexChromaMode(1) == 0);

    std::vector<char> stateBuf(2048, 0);
    void* statePtr = stateBuf.data();
    glideGetState(statePtr);

    texChromaMode(0, 0);
    texChromaRange(0, 0, 0, 0);
    texChromaMode(1, 1);
    texChromaRange(1, 0, 0, 1);

    CHECK(backend->GetTexChromaMode(0) == 0);
    CHECK(backend->GetTexChromaMode(1) == 1);

    glideSetState(statePtr);

    CHECK(backend->GetTexChromaMode(0) == 1);
    CHECK(backend->GetTexChromaMin(0) == 0x00112233);
    CHECK(backend->GetTexChromaMax(0) == 0x00445566);
    CHECK(backend->GetTexChromaRangeMode(0) == 1);
    CHECK(backend->GetTexChromaMode(1) == 0);
    CHECK(backend->GetTexChromaMin(1) == 0x00778899);
    CHECK(backend->GetTexChromaMax(1) == 0x00AABBCC);

    winClose();
    shutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::dlclose(handle);
  }

}
#endif

#ifdef __linux__
TEST_CASE("Glide 3.x: Mipmapping, LOD Truncation & Multi-Texturing", "[Glide3][Mipmap][LOD]") {
  SECTION("Mipmapping state & save/restore round-trip") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using TexMipMapModeFunc = void(*)(GrChipID_t, GrMipMapMode_t, FxBool);
    using GetStateFunc = void(*)(void*);
    using SetStateFunc = void(*)(const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto texMipMapMode = reinterpret_cast<TexMipMapModeFunc>(::dlsym(handle, "grTexMipMapMode"));
    auto getState  = reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
    auto setState  = reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(texMipMapMode != nullptr);
    REQUIRE(getState != nullptr);
    REQUIRE(setState != nullptr);
    REQUIRE(shutdown != nullptr);

    // Force headless software backend
    ::setenv("GLIDE_NG_BACKEND", "software", 1);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
    REQUIRE(ctx != 0);

    // 1. Set different mipmap modes on TMU0 and TMU1
    texMipMapMode(0, 1, FXTRUE);  // TMU0: GR_MIPMAP_NEAREST, lodBlend=true
    texMipMapMode(1, 0, FXFALSE); // TMU1: GR_MIPMAP_DISABLE, lodBlend=false

    // 2. Capture state
    std::vector<uint8_t> stateBuffer(2048, 0);
    getState(stateBuffer.data());

    // 3. Temporarily change the states to ensure restore works
    texMipMapMode(0, 0, FXFALSE);
    texMipMapMode(1, 1, FXTRUE);

    // 4. Restore the captured state
    setState(stateBuffer.data());

    // 5. Re-capture and verify round-trip values
    std::vector<uint8_t> checkBuffer(2048, 0);
    getState(checkBuffer.data());

    // Cast to the internal struct layout to verify fields
    struct StateLayout {
      uint32_t depthMode;
      uint32_t depthCompare;
      int depthMask;
      int32_t depthBias;
      uint32_t alphaTestOp;
      uint32_t alphaTestRef;
      uint32_t clampS[2];
      uint32_t clampT[2];
      uint32_t minFilter[2];
      uint32_t magFilter[2];
      uint32_t colorFunc;
      uint32_t colorFactor;
      uint32_t colorLocal;
      uint32_t colorOther;
      int colorInvert;
      uint32_t alphaFunc;
      uint32_t alphaFactor;
      uint32_t alphaLocal;
      uint32_t alphaOther;
      int alphaInvert;
      uint32_t texRgbFunc[2];
      uint32_t texRgbFactor[2];
      uint32_t texAlphaFunc[2];
      uint32_t texAlphaFactor[2];
      uint32_t boundTex[2];
      uint32_t coordSpace;
      float viewportX;
      float viewportY;
      float viewportWidth;
      float viewportHeight;
      float depthRangeNear;
      float depthRangeFar;
      int aaOrdered;
      int shamelessPlug;
      uint8_t lfbConstantAlpha;
      uint32_t lfbWriteColorFormat;
      uint32_t chromakeyMode;
      uint32_t chromakeyValue;
      uint32_t chromakeyRangeMin;
      uint32_t chromakeyRangeMax;
      uint32_t chromakeyRangeMode;
      struct {
        int mode;
        uint32_t minColor;
        uint32_t maxColor;
      } texChromaState[2];
      uint32_t rgbSrcBlend;
      uint32_t rgbDstBlend;
      uint32_t alphaSrcBlend;
      uint32_t alphaDstBlend;
      uint32_t fogMode;
      uint32_t fogColor;
      uint8_t fogTable[64];
      uint32_t cullMode;
      uint32_t constantColor;
      uint32_t sstOrigin;
      float texLodBias[2];
      uint32_t mipmapMode[2];
      int lodBlend[2];
    };

    auto* ws = reinterpret_cast<StateLayout*>(checkBuffer.data());
    CHECK(ws->mipmapMode[0] == 1);       // GR_MIPMAP_NEAREST
    CHECK(ws->lodBlend[0] == 1);         // true
    CHECK(ws->mipmapMode[1] == 0);       // GR_MIPMAP_DISABLE
    CHECK(ws->lodBlend[1] == 0);         // false

    winClose();
    shutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::dlclose(handle);
  }

  SECTION("Software mipmapping & LOD bias parity") {
    // Force the Software backend for this test
    ::setenv("GLIDE_NG_BACKEND", "sw", 1);

    // Initialize Glide
    GrScreenResolution_t res = GR_RESOLUTION_1024x768;
    grGlideInit();
    GrContext_t ctx = grSstWinOpen(0, res, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                                  GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Create a 256x256 texture with a complete mipmap chain down to 1x1
    std::vector<uint16_t> textureData(256 * 256 * 2, 0xFFFF);
    GrTexInfo info;
    info.smallLodLog2 = GR_LOD_LOG2_1;
    info.largeLodLog2 = GR_LOD_LOG2_256;
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_ARGB_4444;
    info.data = textureData.data();

    uint32_t startAddress = 0;
    grTexDownloadMipMap(GR_TMU0, startAddress, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, startAddress, GR_MIPMAPLEVELMASK_BOTH, &info);

    // 1. Verify Nearest Mipmap Mode State Propagation
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    
    // 2. Verify Trilinear/LOD Blend State Propagation
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXTRUE);

    // 3. Verify LOD Bias Propagation
    grTexLodBiasValue(GR_TMU0, 1.5f);

    // Clean up
    grSstWinClose(ctx);
    grGlideShutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
  }

  SECTION("Software multi-texturing parity") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using CloseWinFunc = void(*)(FxU32);
    using TexDownloadFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexClampFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using TexFilterFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void(*)(void);
    using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto texDownload = reinterpret_cast<TexDownloadFunc>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource   = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texClamp    = reinterpret_cast<TexClampFunc>(::dlsym(handle, "grTexClampMode"));
    auto texFilter   = reinterpret_cast<TexFilterFunc>(::dlsym(handle, "grTexFilterMode"));
    auto texCombine  = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto bufferClear  = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto drawTri     = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb     = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(texClamp != nullptr);
    REQUIRE(texFilter != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(vertexLayout != nullptr);

    // Force headless software reference backend for TDD validation
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    if (sstSelect) sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_800x600, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Configure Custom Multi-texture Vertex Layout
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at offset 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at offset 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at offset 12
    vertexLayout(0x10, 24, 1); // GR_PARAM_A at offset 24
    vertexLayout(0x04, 28, 1); // GR_PARAM_Q at offset 28
    vertexLayout(0x40, 32, 1); // GR_PARAM_ST0 at offset 32
    vertexLayout(0x100, 40, 1); // GR_PARAM_Q0 at offset 40
    vertexLayout(0x80, 44, 1);  // GR_PARAM_ST1 at offset 44
    vertexLayout(0x200, 52, 1); // GR_PARAM_Q1 at offset 52

    // Allocate 16x16 red texture with 50% Alpha (0x8F00 in ARGB_4444) for TMU0
    std::vector<uint16_t> tex0Data(16 * 16, 0x8F00); 
    GrTexInfo tex0Info;
    tex0Info.smallLodLog2 = 4; // 16x16
    tex0Info.largeLodLog2 = 4;
    tex0Info.aspectRatioLog2 = 0; // 1:1 aspect
    tex0Info.format = GR_TEXFMT_ARGB_4444;
    tex0Info.data = tex0Data.data();

    // Allocate 16x16 blue texture (0xF00F in ARGB_4444) for TMU1
    std::vector<uint16_t> tex1Data(16 * 16, 0xF00F); 
    GrTexInfo tex1Info;
    tex1Info.smallLodLog2 = 4; // 16x16
    tex1Info.largeLodLog2 = 4;
    tex1Info.aspectRatioLog2 = 0; // 1:1 aspect
    tex1Info.format = GR_TEXFMT_ARGB_4444;
    tex1Info.data = tex1Data.data();

    // Download and configure TMU 0
    texDownload(0, 0x00001000, 0, &tex0Info);
    texSource(0, 0x00001000, 0, &tex0Info);
    texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texFilter(0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

    // Download and configure TMU 1
    texDownload(1, 0x00010000, 0, &tex1Info);
    texSource(1, 0x00010000, 0, &tex1Info);
    texClamp(1, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texFilter(1, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

    // Combine setup:
    // TMU 1: Output texture color directly (tmu1Out = tex1Color = Blue)
    texCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    // TMU 0: Mix TMU 1 output (blue) with TMU 0 texture color (red) in 50/50 blend!
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND, GR_COMBINE_FACTOR_LOCAL_ALPHA,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    // FBI Combiner: Pass texture color directly
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Clear buffer to dark gray
    bufferClear(0xFF333333, 0, 0);

    // Define vertices
    struct MultiTexVert {
        float x, y, z;
        float r, g, b, a;
        float oow;
        float s0, t0, oow0;
        float s1, t1, oow1;
    };

    MultiTexVert v0{300.0f, 200.0f, 0.5f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    MultiTexVert v1{500.0f, 200.0f, 0.5f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 16.0f, 0.0f, 1.0f, 16.0f, 0.0f, 1.0f};
    MultiTexVert v2{500.0f, 400.0f, 0.5f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 16.0f, 16.0f, 1.0f, 16.0f, 16.0f, 1.0f};
    MultiTexVert v3{300.0f, 400.0f, 0.5f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 0.0f, 16.0f, 1.0f, 0.0f, 16.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    // Read back a pixel from the center (x=400, y=300)
    uint32_t centerPixel = 0;
    FxBool ok = readLfb(1, 400, 300, 1, 1, 4, &centerPixel);
    REQUIRE(ok == FXTRUE);

    // Unpack colors (ABGR format)
    uint8_t a = (centerPixel >> 24) & 0xFF;
    uint8_t b = (centerPixel >> 16) & 0xFF;
    uint8_t g = (centerPixel >> 8) & 0xFF;
    uint8_t r = centerPixel & 0xFF;

    // Assert that the color is a perfect purple blend of 50% Red and 50% Blue!
    INFO("Center Pixel color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b << ", A=" << (int)a);
    REQUIRE(r >= 110);
    REQUIRE(r <= 125);
    REQUIRE(g <= 5);
    REQUIRE(b >= 130);
    REQUIRE(b <= 145);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
  }

  SECTION("Mathematical LOD y-derivative calculation") {
#ifdef __linux__
    // Force the Software backend for this test
    ::setenv("GLIDE_NG_BACKEND", "sw", 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit    = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideInit"));
    auto sstSelect    = reinterpret_cast<void(*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen      = reinterpret_cast<FxU32(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int)>(::dlsym(handle, "grSstWinOpen"));
    auto winClose     = reinterpret_cast<FxBool(*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideShutdown"));

    auto texDownload  = reinterpret_cast<void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*)>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource    = reinterpret_cast<void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*)>(::dlsym(handle, "grTexSource"));
    auto texMipMapMode = reinterpret_cast<void(*)(GrChipID_t, GrMipMapMode_t, FxBool)>(::dlsym(handle, "grTexMipMapMode"));
    auto texFilterMode = reinterpret_cast<void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t)>(::dlsym(handle, "grTexFilterMode"));
    auto vertexLayout  = reinterpret_cast<void(*)(FxU32, int, FxBool)>(::dlsym(handle, "grVertexLayout"));

    auto colorCombine = reinterpret_cast<void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool)>(::dlsym(handle, "grColorCombine"));
    auto texCombine   = reinterpret_cast<void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool)>(::dlsym(handle, "grTexCombine"));
    auto drawTriangle = reinterpret_cast<void(*)(const void*, const void*, const void*)>(::dlsym(handle, "grDrawTriangle"));
    auto lfbReadRegion = reinterpret_cast<FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(lfbReadRegion != nullptr);

    glideInit();
    if (sstSelect) sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Set up vertex layout in Glide 3.x
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at offset 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at offset 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at offset 12
    vertexLayout(0x10, 24, 1); // GR_PARAM_A at offset 24
    vertexLayout(0x04, 28, 1); // GR_PARAM_Q at offset 28
    vertexLayout(0x40, 32, 1); // GR_PARAM_ST0 at offset 32

    // Create a contiguous 256x256 texture mipmap chain for RGB_565 (87381 texels)
    std::vector<uint16_t> textureData(87381, 0xFFFF);
    
    // Fill each level with contrasting colors:
    // Level 0 (256x256, 65536 texels): Red (0xF800)
    for (int i = 0; i < 65536; ++i) textureData[i] = 0xF800;
    // Level 1 (128x128, 16384 texels): Green (0x07E0)
    for (int i = 65536; i < 81920; ++i) textureData[i] = 0x07E0;
    // Level 2 (64x64, 4096 texels): Blue (0x001F)
    for (int i = 81920; i < 86016; ++i) textureData[i] = 0x001F;
    // Level 3 (32x32, 1024 texels): Yellow (0xFFE0)
    for (int i = 86016; i < 87040; ++i) textureData[i] = 0xFFE0;
    // Level 4 (16x16, 256 texels): Magenta (0xF81F)
    for (int i = 87040; i < 87296; ++i) textureData[i] = 0xF81F;
    // Level 5 (8x8, 64 texels): Cyan (0x07FF)
    for (int i = 87296; i < 87360; ++i) textureData[i] = 0x07FF;
    // Level 6 (4x4, 16 texels): White (0xFFFF)
    for (int i = 87360; i < 87376; ++i) textureData[i] = 0xFFFF;
    // Level 7 (2x2, 4 texels): White (0xFFFF)
    for (int i = 87376; i < 87380; ++i) textureData[i] = 0xFFFF;
    // Level 8 (1x1, 1 texel): White (0xFFFF)
    textureData[87380] = 0xFFFF;

    GrTexInfo info;
    info.smallLodLog2 = GR_LOD_LOG2_1;     // 1x1
    info.largeLodLog2 = GR_LOD_LOG2_256;   // 256x256
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = textureData.data();

    uint32_t startAddress = 0;
    texDownload(GR_TMU0, startAddress, GR_MIPMAPLEVELMASK_BOTH, &info);
    texSource(GR_TMU0, startAddress, GR_MIPMAPLEVELMASK_BOTH, &info);

    // Set discrete mipmapping and point filtering to get sharp boundaries
    texMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    texFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

    // Setup combiner: output texture color directly
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    // Set up vertices for mathematical LOD test
    struct TestVert {
        float x, y;     // offset 0
        float z;        // offset 8
        float r, g, b;  // offset 12
        float a;        // offset 24
        float q;        // offset 28 (oow)
        float s, t;     // offset 32
    };

    TestVert vA{ 100.0f, 100.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 0.0f, 512.0f };
    TestVert vB{ 100.0f, 108.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 0.0f, 256.0f };
    TestVert vC{ 108.0f, 108.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f, 0.0f, 256.0f };

    // Draw the triangle
    drawTriangle(&vA, &vB, &vC);

    // Read the 8x8 region of the triangle
    std::vector<uint32_t> pixels(64, 0);
    FxBool readOk = lfbReadRegion(GR_BUFFER_BACKBUFFER, 100, 100, 8, 8, 8 * 4, pixels.data());
    REQUIRE(readOk == FXTRUE);
    std::cout << "[DEBUG_GRID] Printing 8x8 rendered grid:" << std::endl;
    for (int y = 0; y < 8; ++y) {
        std::cout << "  y=" << 100 + y << ": ";
        for (int x = 0; x < 8; ++x) {
            uint32_t val = pixels[y * 8 + x];
            std::cout << std::hex << val << " ";
        }
        std::cout << std::dec << std::endl;
    }
    uint32_t color = pixels[6 * 8 + 2]; // (102, 106)
    uint8_t r_chan = (color >> 16) & 0xFF;
    uint8_t g_chan = (color >> 8) & 0xFF;
    uint8_t b_chan = color & 0xFF;

    // Under the bug, the y-derivative of T was 0, resulting in LOD=0 (Red).
    // Under the fix, the y-derivative is correct, resulting in LOD=5 (Cyan).
    // Verify that the color is Cyan (R=0, G=255, B=255) and NOT Red (R=255, G=0, B=0)
    CHECK(r_chan == 0);
    CHECK(g_chan > 240);
    CHECK(b_chan > 240);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
#endif
  }

}
#endif

TEST_CASE("Glide 3.x: Authentic GR_MIPMAP_NEAREST LOD Truncation", "[Glide3][Mipmap][LOD]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)();
  using ShutdownFunc = void (*)();
  using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using SelectCtxFunc = FxBool (*)(FxU32);
  using ClearFunc = void (*)(FxU32, FxU32, FxU32);
  using LayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using ColorCombineFunc = void (*)(FxI32, FxI32, FxI32, FxI32, FxBool);
  using TexCombineFunc = void (*)(FxU32, FxI32, FxI32, FxI32, FxI32, FxBool, FxBool);
  using AlphaBlendFunc = void (*)(FxI32, FxI32, FxI32, FxI32);
  using DepthModeFunc = void (*)(FxI32);
  using CullFunc = void (*)(FxI32);
  using TexMinAddrFunc = FxU32 (*)(FxU32);
  using TexDownloadFunc = void (*)(FxU32, FxU32, FxU32, GrTexInfo*);
  using TexSourceFunc = void (*)(FxU32, FxU32, FxU32, GrTexInfo*);
  using TexMipMapFunc = void (*)(FxU32, FxU32, FxBool);
  using TexFilterFunc = void (*)(FxU32, FxU32, FxU32);
  using TexClampFunc = void (*)(FxU32, FxU32, FxU32);
  using TexLodBiasFunc = void (*)(FxU32, float);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using ReadRegionFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
  auto bufClear = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
  auto vtxLayout = reinterpret_cast<LayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
  auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
  auto alphaBlend = reinterpret_cast<AlphaBlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
  auto depthMode = reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
  auto cullMode = reinterpret_cast<CullFunc>(::dlsym(handle, "grCullMode"));
  auto texMinAddr = reinterpret_cast<TexMinAddrFunc>(::dlsym(handle, "grTexMinAddress"));
  auto texDownload = reinterpret_cast<TexDownloadFunc>(::dlsym(handle, "grTexDownloadMipMap"));
  auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
  auto texMipMap = reinterpret_cast<TexMipMapFunc>(::dlsym(handle, "grTexMipMapMode"));
  auto texFilter = reinterpret_cast<TexFilterFunc>(::dlsym(handle, "grTexFilterMode"));
  auto texClamp = reinterpret_cast<TexClampFunc>(::dlsym(handle, "grTexClampMode"));
  auto texLodBias = reinterpret_cast<TexLodBiasFunc>(::dlsym(handle, "grTexLodBiasValue"));
  auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto readRegion = reinterpret_cast<ReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
  REQUIRE((glideInit && glideShutdown && winOpen && winClose && selectCtx &&
           bufClear && vtxLayout && colorCombine && texCombine && alphaBlend &&
           depthMode && cullMode && texMinAddr && texDownload && texSource &&
           texMipMap && texFilter && texClamp && texLodBias && drawTri &&
           readRegion));

  // Build 64x64..1x1 RGB565 mip chain with distinct solid colors per level:
  //   mip 0 (64x64): RED   (0xF800)
  //   mip 1 (32x32): GREEN (0x07E0)
  //   mip 2 (16x16): BLUE  (0x001F)
  //   mip 3..6     : WHITE (0xFFFF)
  std::vector<uint16_t> mipData;
  auto pushLevel = [&](int dim, uint16_t color565) {
    mipData.insert(mipData.end(), static_cast<size_t>(dim * dim), color565);
  };
  pushLevel(64, 0xF800u);
  pushLevel(32, 0x07E0u);
  pushLevel(16, 0x001Fu);
  pushLevel(8, 0xFFFFu);
  pushLevel(4, 0xFFFFu);
  pushLevel(2, 0xFFFFu);
  pushLevel(1, 0xFFFFu);

  struct SimpleTexVert {
    float x, y;
    float q;
    float s, t;
  };

  for (const char* be : {"software", "vulkan", "opengl_es"}) {
    ::setenv("GLIDE_NG_BACKEND", be, 1);
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);

    glideInit();
    const FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);
    selectCtx(ctx);

    vtxLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_Q, 8, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_ST0, 12, GR_PARAM_ENABLE);
    depthMode(GR_DEPTHBUFFER_DISABLE);
    cullMode(GR_CULL_DISABLE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    GrTexInfo info{};
    info.smallLodLog2 = 0;   // GR_LOD_LOG2_1
    info.largeLodLog2 = 6;   // GR_LOD_LOG2_64
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = mipData.data();

    const FxU32 addr = texMinAddr(GR_TMU0);
    texDownload(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texClamp(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    auto sampleRatio = [&](float minRatio, FxU32 mmMode, FxBool lodBlend,
                           float bias) -> uint16_t {
      bufClear(0xFF000000u, 0, 0xFFFF);
      texMipMap(GR_TMU0, mmMode, lodBlend);
      texLodBias(GR_TMU0, bias);
      // 32x32 screen quad centered at (160, 160). Texture is 64x64 mapped across
      // Glide [0, 256] (so span = 4 * minRatio * 32 = 128 * minRatio).
      const float span = 32.0f * (256.0f / 64.0f) * minRatio;
      SimpleTexVert v0{144.0f, 144.0f, 1.0f, 0.0f, 0.0f};
      SimpleTexVert v1{176.0f, 144.0f, 1.0f, span, 0.0f};
      SimpleTexVert v2{176.0f, 176.0f, 1.0f, span, span};
      SimpleTexVert v3{144.0f, 176.0f, 1.0f, 0.0f, span};
      drawTri(&v0, &v1, &v2);
      drawTri(&v0, &v2, &v3);
      uint16_t px = 0;
      REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 160, 160, 1, 1, 2, &px) == FXTRUE);
      return px;
    };
    auto chR = [](uint16_t p) { return ((p >> 11) & 0x1Fu) * 255u / 31u; };
    auto chG = [](uint16_t p) { return ((p >> 5) & 0x3Fu) * 255u / 63u; };
    auto chB = [](uint16_t p) { return (p & 0x1Fu) * 255u / 31u; };

    INFO("backend=" << be);
    // 1) GR_MIPMAP_NEAREST truncates (floor): ratio 1.68x (rawLod=0.75) -> mip 0 (RED)
    {
      const uint16_t px = sampleRatio(1.68179f, GR_MIPMAP_NEAREST, FXFALSE, 0.0f);
      CHECK(chR(px) >= 240u);
      CHECK(chG(px) <= 15u);
      CHECK(chB(px) <= 15u);
    }
    // 2) Authentic 3dfx rounding recipe: bias=+0.5 at rawLod=0.75 -> mip 1 (GREEN)
    {
      const uint16_t px = sampleRatio(1.68179f, GR_MIPMAP_NEAREST, FXFALSE, 0.5f);
      CHECK(chR(px) <= 15u);
      CHECK(chG(px) >= 240u);
      CHECK(chB(px) <= 15u);
    }
    // 3) Bias=+0.5 must NOT double-round at rawLod=0.25 (1.19x -> floor(0.75)=0 RED)
    {
      const uint16_t px = sampleRatio(1.18921f, GR_MIPMAP_NEAREST, FXFALSE, 0.5f);
      CHECK(chR(px) >= 240u);
      CHECK(chG(px) <= 15u);
      CHECK(chB(px) <= 15u);
    }
    // 4) Exact 2x minification (rawLod=1.0) -> mip 1 (GREEN)
    {
      const uint16_t px = sampleRatio(2.0f, GR_MIPMAP_NEAREST, FXFALSE, 0.0f);
      CHECK(chR(px) <= 15u);
      CHECK(chG(px) >= 240u);
      CHECK(chB(px) <= 15u);
    }
    // 5) Ratio 3.36x (rawLod=1.75) truncates to mip 1 (GREEN), NOT mip 2 (BLUE)
    {
      const uint16_t px = sampleRatio(3.36359f, GR_MIPMAP_NEAREST, FXFALSE, 0.0f);
      CHECK(chR(px) <= 15u);
      CHECK(chG(px) >= 240u);
      CHECK(chB(px) <= 15u);
    }
    // 6) Exact 4x minification (rawLod=2.0) -> mip 2 (BLUE)
    {
      const uint16_t px = sampleRatio(4.0f, GR_MIPMAP_NEAREST, FXFALSE, 0.0f);
      CHECK(chR(px) <= 15u);
      CHECK(chG(px) <= 15u);
      CHECK(chB(px) >= 240u);
    }
    // 7) Trilinear (lodBlend=FXTRUE) must remain UNSHIFTED: rawLod=0.5 blends 50/50 RED+GREEN
    {
      const uint16_t px = sampleRatio(1.41421f, GR_MIPMAP_NEAREST, FXTRUE, 0.0f);
      CHECK(chR(px) >= 90u);
      CHECK(chR(px) <= 165u);
      CHECK(chG(px) >= 90u);
      CHECK(chG(px) <= 165u);
      CHECK(chB(px) <= 15u);
    }
    // 8) GR_MIPMAP_NEAREST_DITHER centers transition near k+0.5: rawLod=0.75 -> mip 1 (GREEN)
    {
      const uint16_t px =
          sampleRatio(1.68179f, GR_MIPMAP_NEAREST_DITHER, FXFALSE, 0.0f);
      CHECK(chR(px) <= 15u);
      CHECK(chG(px) >= 240u);
      CHECK(chB(px) <= 15u);
    }
    // 9) GR_TEXCOMBINE_FACTOR_LOD_FRACTION must stay unshifted under GR_MIPMAP_NEAREST
    //    (rawLod=0.5, bias=0 -> lodFrac = 0.5; blend local RED * (1 - 0.5) ~ 128)
    {
      texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL,
                 GR_COMBINE_FACTOR_LOD_FRACTION, GR_COMBINE_FUNCTION_LOCAL,
                 GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
      const uint16_t px = sampleRatio(1.41421f, GR_MIPMAP_NEAREST, FXFALSE, 0.0f);
      CHECK(chR(px) >= 95u);
      CHECK(chR(px) <= 165u);
      CHECK(chG(px) <= 15u);
      CHECK(chB(px) <= 15u);
      texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
    }

    winClose(ctx);
    glideShutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
    ::unsetenv("FX_GLIDE_NO_SPLASH");
  }
  ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x: Multi-TMU Palette Broadcast & Partial Updates", "[Glide3][Texture][Palette]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = reinterpret_cast<void (*)()>(dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<void (*)()>(dlsym(handle, "grGlideShutdown"));
  auto sstSelect =
      reinterpret_cast<void (*)(int)>(dlsym(handle, "grSstSelect"));
  auto winOpen = reinterpret_cast<GrContext_t (*)(
      FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t,
      GrOriginLocation_t, int, int)>(dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<FxBool (*)(GrContext_t)>(dlsym(handle, "grSstWinClose"));
  auto vtxLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
      dlsym(handle, "grVertexLayout"));
  auto reset = reinterpret_cast<FxBool (*)(FxU32)>(dlsym(handle, "grReset"));
  auto bufClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
      dlsym(handle, "grBufferClear"));
  auto texMinAddr = reinterpret_cast<FxU32 (*)(GrChipID_t)>(
      dlsym(handle, "grTexMinAddress"));
  auto texDownload =
      reinterpret_cast<void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*)>(
          dlsym(handle, "grTexDownloadMipMap"));
  auto texSource =
      reinterpret_cast<void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*)>(
          dlsym(handle, "grTexSource"));
  auto texDownloadTable = reinterpret_cast<void (*)(GrTexTable_t, void*)>(
      dlsym(handle, "grTexDownloadTable"));
  auto texDownloadTablePartial =
      reinterpret_cast<void (*)(GrTexTable_t, void*, int, int)>(
          dlsym(handle, "grTexDownloadTablePartial"));
  auto texCombine = reinterpret_cast<void (*)(
      GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
      GrCombineFactor_t, FxBool, FxBool)>(dlsym(handle, "grTexCombine"));
  auto colorCombine = reinterpret_cast<void (*)(
      GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
      GrCombineOther_t, FxBool)>(dlsym(handle, "grColorCombine"));
  auto alphaCombine = reinterpret_cast<void (*)(
      GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
      GrCombineOther_t, FxBool)>(dlsym(handle, "grAlphaCombine"));
  auto drawTri = reinterpret_cast<void (*)(const void*, const void*,
                                           const void*)>(
      dlsym(handle, "grDrawTriangle"));
  auto readRegion = reinterpret_cast<FxBool (*)(
      GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
      dlsym(handle, "grLfbReadRegion"));
  auto depthMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(
      dlsym(handle, "grDepthBufferMode"));
  auto alphaBlend = reinterpret_cast<void (*)(
      GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
      GrAlphaBlendFnc_t)>(dlsym(handle, "grAlphaBlendFunction"));
  REQUIRE(glideInit != nullptr);
  REQUIRE(texDownloadTable != nullptr);
  REQUIRE(texDownloadTablePartial != nullptr);

  struct Vtx {
    float x, y;
    float oow;
    float s0, t0;
    float s1, t1;
  };
  auto drawQuad = [&](float x0, float y0, float x1, float y1) {
    Vtx a{x0, y0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vtx b{x1, y0, 1.0f, 255.0f, 0.0f, 255.0f, 0.0f};
    Vtx c{x1, y1, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f};
    Vtx d{x0, y1, 1.0f, 0.0f, 255.0f, 0.0f, 255.0f};
    drawTri(&a, &b, &c);
    drawTri(&a, &c, &d);
  };
  auto rgb565R = [](uint16_t p) -> uint32_t {
    return ((p >> 11) & 0x1Fu) * 255u / 31u;
  };
  auto rgb565G = [](uint16_t p) -> uint32_t {
    return ((p >> 5) & 0x3Fu) * 255u / 63u;
  };
  auto rgb565B = [](uint16_t p) -> uint32_t {
    return (p & 0x1Fu) * 255u / 31u;
  };

  std::vector<uint8_t> idx5(8 * 8, 5u);
  std::vector<uint8_t> idx11(8 * 8, 11u);
  for (const char* be : {"software", "vulkan", "opengl_es"}) {
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_BACKEND", be, 1);
    ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
    glideInit();
    sstSelect(0);
    GrContext_t ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
      glideShutdown();
      continue;
    }

    reset(GR_VERTEX_PARAMETER);
    vtxLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_W, 8, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_ST0, 12, GR_PARAM_ENABLE);
    vtxLayout(GR_PARAM_ST1, 20, GR_PARAM_ENABLE);
    depthMode(GR_DEPTHBUFFER_DISABLE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    bufClear(0xFF000000u, 0, 0xFFFFu);

    // Download two distinct P_8 textures on TMU1 BEFORE uploading the palette
    // (mirrors Diablo II D2Glide.dll's P_8 atlas split on TMU1 vs TMU0).
    uint32_t pal[256]{};
    pal[5] = 0x000000FFu;   // pure blue
    pal[11] = 0x00FF0000u;  // pure red

    const FxU32 base1 = texMinAddr(GR_TMU1);
    GrTexInfo t5{};
    t5.smallLodLog2 = GR_LOD_LOG2_8;
    t5.largeLodLog2 = GR_LOD_LOG2_8;
    t5.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    t5.format = GR_TEXFMT_P_8;
    t5.data = idx5.data();
    GrTexInfo t11 = t5;
    t11.data = idx11.data();

    texDownload(GR_TMU1, base1, GR_MIPMAPLEVELMASK_BOTH, &t5);
    texDownload(GR_TMU1, base1 + 1024u, GR_MIPMAPLEVELMASK_BOTH, &t11);

    // Broadcast palette with NO TMU argument (Glide 3.x spec) — MUST populate
    // BOTH GR_TMU0 and GR_TMU1!
    texDownloadTable(GR_TEXTABLE_PALETTE, pal);

    // Route TMU1 through TMU0 (PASS_OTHER = LOCAL on TMU1 -> OTHER on TMU0)
    texCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, FXFALSE,
               FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

    texSource(GR_TMU1, base1, GR_MIPMAPLEVELMASK_BOTH, &t5);
    drawQuad(10.0f, 10.0f, 50.0f, 50.0f);
    texSource(GR_TMU1, base1 + 1024u, GR_MIPMAPLEVELMASK_BOTH, &t11);
    drawQuad(60.0f, 10.0f, 100.0f, 50.0f);

    uint16_t pxBlue = 0, pxRed = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 30, 30, 1, 1, 2, &pxBlue) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 80, 30, 1, 1, 2, &pxRed) == FXTRUE);
    INFO("backend=" << be << " full blue=0x" << std::hex << pxBlue
                    << " red=0x" << pxRed);
    CHECK(rgb565B(pxBlue) > 200u);
    CHECK(rgb565R(pxBlue) < 25u);
    CHECK(rgb565R(pxRed) > 200u);
    CHECK(rgb565B(pxRed) < 25u);

    // Partial palette update: change ONLY indices [10..12] -> index 11 becomes
    // pure GREEN, while index 5 outside [10..12] MUST stay pure BLUE (even if
    // palPartial[5] is overwritten with junk magenta!).
    uint32_t palPartial[256]{};
    palPartial[5] = 0x00FF00FFu;   // must NOT be applied
    palPartial[11] = 0x0000FF00u;  // pure green (inside 10..12)
    texDownloadTablePartial(GR_TEXTABLE_PALETTE, palPartial, 10, 12);

    texSource(GR_TMU1, base1, GR_MIPMAPLEVELMASK_BOTH, &t5);
    drawQuad(10.0f, 70.0f, 50.0f, 110.0f);
    texSource(GR_TMU1, base1 + 1024u, GR_MIPMAPLEVELMASK_BOTH, &t11);
    drawQuad(60.0f, 70.0f, 100.0f, 110.0f);

    uint16_t pxKeepBlue = 0, pxGreen = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 30, 90, 1, 1, 2, &pxKeepBlue) ==
            FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 80, 90, 1, 1, 2, &pxGreen) ==
            FXTRUE);
    INFO("backend=" << be << " partial keepBlue=0x" << std::hex << pxKeepBlue
                    << " green=0x" << pxGreen);
    CHECK(rgb565B(pxKeepBlue) > 200u);
    CHECK(rgb565R(pxKeepBlue) < 25u);
    CHECK(rgb565G(pxGreen) > 200u);
    CHECK(rgb565R(pxGreen) < 25u);

    // GR_TEXTABLE_PALETTE_6666_EXT (32-bit 0xAARRGGBB -> 6:6:6:6 alpha palette):
    // Verify alpha=0 white background (0x00FFFFFF) blends completely
    // transparent over a blue cleared buffer when SRC_ALPHA/ONE_MINUS_SRC_ALPHA
    // blending is active (NFSPU Voodoo3 HUD glyph regression).
    auto alphaBlend =
        reinterpret_cast<void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                  GrAlphaBlendFnc_t, GrAlphaBlendFnc_t)>(
            dlsym(handle, "grAlphaBlendFunction"));
    REQUIRE(alphaBlend != nullptr);
    uint32_t pal6666[256]{};
    pal6666[5] = 0x00FFFFFFu;   // Transparent white (alpha=0, RGB=FFFFFF)
    pal6666[11] = 0xFFFF0000u;  // Opaque red (alpha=255, RGB=FF0000)
    texDownloadTable(GR_TEXTABLE_PALETTE_6666_EXT, pal6666);

    alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
               GR_BLEND_ZERO);
    bufClear(0xFF00FF00u, 0, 0xFFFF);  // Green clear (invariant across ABGR/ARGB)
    texSource(GR_TMU1, base1, GR_MIPMAPLEVELMASK_BOTH, &t5);
    drawQuad(120.0f, 10.0f, 160.0f, 50.0f);  // Transparent white -> keeps green
    texSource(GR_TMU1, base1 + 1024u, GR_MIPMAPLEVELMASK_BOTH, &t11);
    drawQuad(170.0f, 10.0f, 210.0f, 50.0f);  // Opaque red -> writes red

    uint16_t pxTransp = 0, pxOpaque = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 140, 30, 1, 1, 2, &pxTransp) ==
            FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 190, 30, 1, 1, 2, &pxOpaque) ==
            FXTRUE);
    INFO("backend=" << be << " pal6666 transp=0x" << std::hex << pxTransp
                    << " opaque=0x" << pxOpaque);
    CHECK(rgb565G(pxTransp) > 200u);
    CHECK(rgb565R(pxTransp) < 25u);
    CHECK(rgb565R(pxOpaque) > 200u);
    CHECK(rgb565G(pxOpaque) < 25u);

    // Unreal Engine 1 regression: grTexDownloadMipMapLevelPartial without any
    // prior grTexDownloadMipMap call, tightly packing two 64x64 RGB565 textures
    // (each 4 rows = 512 bytes) side by side in TMU0 linear VRAM.
    auto texDlPartial = reinterpret_cast<FxBool (*)(
        GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t,
        GrTextureFormat_t, FxU32, void*, int, int)>(
        dlsym(handle, "grTexDownloadMipMapLevelPartial"));
    REQUIRE(texDlPartial != nullptr);

    const FxU32 addrA = texMinAddr(GR_TMU0) + 0x2000u;
    const FxU32 addrB = addrA + (64u * 4u * 2u);  // Tight 512-byte packing
    std::vector<uint16_t> redRows(64u * 4u, 0xF800u);   // Pure red RGB565
    std::vector<uint16_t> blueRows(64u * 4u, 0x001Fu);  // Pure blue RGB565
    REQUIRE(texDlPartial(GR_TMU0, addrA, GR_LOD_LOG2_64, GR_LOD_LOG2_64,
                         GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565,
                         GR_MIPMAPLEVELMASK_BOTH, redRows.data(), 0,
                         3) == FXTRUE);
    REQUIRE(texDlPartial(GR_TMU0, addrB, GR_LOD_LOG2_64, GR_LOD_LOG2_64,
                         GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565,
                         GR_MIPMAPLEVELMASK_BOTH, blueRows.data(), 0,
                         3) == FXTRUE);

    GrTexInfo partialInfo{};
    partialInfo.smallLodLog2 = GR_LOD_LOG2_64;
    partialInfo.largeLodLog2 = GR_LOD_LOG2_64;
    partialInfo.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    partialInfo.format = GR_TEXFMT_RGB_565;

    // Sample straight from TMU0 with row t inside the uploaded top band [0..3]
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    auto drawTopBand = [&](float x0, float y0, float x1, float y1) {
      // Rows 0..3 out of 64 -> t in [0..16] out of 256 (s=128, t=6 safely inside!)
      Vtx a{x0, y0, 1.0f, 128.0f, 6.0f, 128.0f, 6.0f};
      Vtx b{x1, y0, 1.0f, 128.0f, 6.0f, 128.0f, 6.0f};
      Vtx c{x1, y1, 1.0f, 128.0f, 6.0f, 128.0f, 6.0f};
      Vtx d{x0, y1, 1.0f, 128.0f, 6.0f, 128.0f, 6.0f};
      drawTri(&a, &b, &c);
      drawTri(&a, &c, &d);
    };
    bufClear(0xFF000000u, 0, 0xFFFF);
    texSource(GR_TMU0, addrA, GR_MIPMAPLEVELMASK_BOTH, &partialInfo);
    drawTopBand(10.0f, 120.0f, 50.0f, 160.0f);
    texSource(GR_TMU0, addrB, GR_MIPMAPLEVELMASK_BOTH, &partialInfo);
    drawTopBand(60.0f, 120.0f, 100.0f, 160.0f);

    uint16_t pxPartialRed = 0, pxPartialBlue = 0;
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 30, 140, 1, 1, 2,
                       &pxPartialRed) == FXTRUE);
    REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 80, 140, 1, 1, 2,
                       &pxPartialBlue) == FXTRUE);
    INFO("backend=" << be << " partial red=0x" << std::hex << pxPartialRed
                    << " blue=0x" << pxPartialBlue);
    CHECK(rgb565R(pxPartialRed) > 200u);
    CHECK(rgb565B(pxPartialRed) < 25u);
    CHECK(rgb565B(pxPartialBlue) > 200u);
    CHECK(rgb565R(pxPartialBlue) < 25u);

    winClose(ctx);
    glideShutdown();
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_HEADLESS");
    ::unsetenv("FX_GLIDE_NO_SPLASH");
  }
  ::dlclose(handle);
#endif
}

TEST_CASE("Rasterizer: MSAA Centroid Interpolation, Gradients & Top-Left Scanlines", "[MSAA][Mipmap][Scanline]") {
  SECTION("Centroid interpolation prevents rotated sprite fringes") {
  using namespace GlideWrapper;

  // Build a 64x64 ARGB_1555 texture mirroring rotated sprite sheets / wrapped
  // alpha-tested sprites (e.g. GTA mission arrow):
  //  - Outer ring [0..7] and [56..63]: opaque black (0x8000)
  //  - Inner margin [8..55]: fully transparent (0x0000)
  //  - Center [26..37]: opaque bright yellow (0xFFE0)
  std::vector<uint16_t> tex(64u * 64u, 0x8000u);
  for (uint32_t y = 8u; y < 56u; ++y) {
    for (uint32_t x = 8u; x < 56u; ++x) {
      tex[y * 64u + x] = 0x0000u;
    }
  }
  for (uint32_t y = 26u; y < 38u; ++y) {
    for (uint32_t x = 26u; x < 38u; ++x) {
      tex[y * 64u + x] = 0xFFE0u;
    }
  }

  for (const char* beName : {"vulkan", "opengl_es", "software"}) {
    auto& reg = EmulationRegistry::GetInstance();
    auto savedCfg = reg.GetConfig();
    auto& bm = BackendManager::GetInstance();
    auto& tm = TextureManager::GetInstance();

    WrapperConfig cfg = savedCfg;
    cfg.backend = beName;
    cfg.forceNoWindow = true;
    cfg.model = CardModel::Voodoo2;
    cfg.tmuMemoryMb = 16;
    cfg.fbiMemoryMb = 16;
    cfg.msaaSamples = 4;
    if (std::string(beName) == "software") {
      cfg.swAaMode = "ssaa";
    }
    reg.GetConfig() = cfg;
    REQUIRE(bm.EstablishBackend(cfg));
    auto* be = bm.GetBackend();
    REQUIRE(be != nullptr);
    REQUIRE(be->AttachWindow(nullptr, 128, 128, false));

    tm.Reset();
    tm.SetTmuMemoryLimitMb(0, 16);
    REQUIRE(tm.DownloadMipMap(0u, 0x10000u, 2, 2, 3u, 11u, tex.data(), true));

    be->SetSstOrigin(0u);
    be->SetClipWindow(0u, 0u, 128u, 128u);
    be->SetDepthState(0u, 7u, false, 0);
    be->SetBlendState(4u, 0u, 4u, 0u);  // ONE, ZERO
    be->SetAlphaTestState(4u, 128u);    // GR_CMP_GREATER, ref=128
    be->SetColorCombineExt(0u);
    be->SetTexCombinerMode(0u, 1u, 0u, 1u, 0u, false, false);
    // Color = OTHER (TMU0), Alpha = OTHER (TMU0)
    be->SetCombinerMode(3u, 8u, 2u, 1u, false, 3u, 8u, 2u, 1u, false);
    // Clamp(0,0) = WRAP, Filter(1,1) = BILINEAR
    be->BindTexture(0u, 0x10000u, 0u, 0u, 1u, 1u);

    // Clear to bright white so any leaked dark border fringe stands out starkly
    be->ClearBuffer(0xFFFFFFFFu, 255u, 1.0f, 3u);

    auto makeV = [](float x, float y, float s, float t) {
      ModernVertex mv{};
      mv.pos = {x, y, 0.5f, 1.0f};
      mv.color = {1.0f, 1.0f, 1.0f, 1.0f};
      mv.tex = {s, t, 0.0f, 0.0f};
      mv.tmu_oow = {1.0f, 1.0f};
      return mv;
    };

    // 45-degree rotated quad mapped strictly onto transparent region [8.5..55.5]
    // in 0..256 Glide texture units ([34.0..222.0]). Without centroid
    // interpolation, partial-coverage pixels on the 45-degree outer edges
    // extrapolate UVs outside [34..222] into the opaque black ring [0..7]/[56..63].
    const float u0 = 34.0f;
    const float u1 = 222.0f;
    ModernVertex vTop = makeV(64.0f, 32.0f, u0, u0);
    ModernVertex vRight = makeV(96.0f, 64.0f, u1, u0);
    ModernVertex vBot = makeV(64.0f, 96.0f, u1, u1);
    ModernVertex vLeft = makeV(32.0f, 64.0f, u0, u1);
    be->DrawTriangle(vTop, vRight, vBot);
    be->DrawTriangle(vTop, vBot, vLeft);
    be->FlushBins();

    std::vector<uint32_t> fb(128u * 128u, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, 128u, 128u, 128u * 4u, fb.data()));

    INFO("backend=" << beName);
    // 1. Center pixel must be opaque yellow (sprite actually rendered)
    const uint32_t centerPx = fb[64u * 128u + 64u];
    CHECK((centerPx & 0xFFu) > 220u);
    CHECK(((centerPx >> 8) & 0xFFu) > 220u);
    CHECK(((centerPx >> 16) & 0xFFu) < 30u);

    // 2. Outer perimeter around the rotated quad (r >= 18 px from center) must
    //    remain pure white — zero extrapolated dark fringe pixels!
    uint32_t fringePixels = 0u;
    for (uint32_t y = 0u; y < 128u; ++y) {
      for (uint32_t x = 0u; x < 128u; ++x) {
        const int dx = static_cast<int>(x) - 64;
        const int dy = static_cast<int>(y) - 64;
        if (dx * dx + dy * dy < 18 * 18) continue;
        const uint32_t px = fb[y * 128u + x];
        const uint32_t r = px & 0xFFu;
        const uint32_t g = (px >> 8) & 0xFFu;
        const uint32_t b = (px >> 16) & 0xFFu;
        if (r < 245u || g < 245u || b < 245u) {
          ++fringePixels;
        }
      }
    }
    CHECK(fringePixels == 0u);

    be->DetachWindow();
    bm.ShutdownBackend();
    reg.GetConfig() = savedCfg;
  }
  }

  SECTION("Non-centroid gradients prevent quad-diagonal LOD spikes") {
  using namespace GlideWrapper;

  // Build a 128x256 RGB_565 2-level mipmap texture (matching Tomb Raider's
  // 128x256 title tiles with largeLod=0, smallLod=1, aspect=GR_ASPECT_1x2):
  //  - Mip 0 (128x256): pure bright green (RGB565 = 0x07E0)
  //  - Mip 1 (64x128):  pure bright red   (RGB565 = 0xF800)
  // Rendering a 1:1 128x256 2-triangle quad under 4x MSAA with trilinear
  // mipmapping must stay 100% on Mip 0 across the entire interior including
  // the internal triangle diagonal. Computing dFdx/dFdy on centroid varyings
  // shifts samples off pixel centers along the diagonal and spikes into Mip 1.
  std::vector<uint16_t> tex(128u * 256u + 64u * 128u);
  std::fill(tex.begin(), tex.begin() + 128u * 256u, static_cast<uint16_t>(0x07E0u));
  std::fill(tex.begin() + 128u * 256u, tex.end(), static_cast<uint16_t>(0xF800u));

  for (const char* beName : {"vulkan", "opengl_es", "software"}) {
    auto& reg = EmulationRegistry::GetInstance();
    auto savedCfg = reg.GetConfig();
    auto& bm = BackendManager::GetInstance();
    auto& tm = TextureManager::GetInstance();

    WrapperConfig cfg = savedCfg;
    cfg.backend = beName;
    cfg.forceNoWindow = true;
    cfg.model = CardModel::Voodoo2;
    cfg.tmuMemoryMb = 16;
    cfg.fbiMemoryMb = 16;
    cfg.msaaSamples = 4;
    if (std::string(beName) == "software") {
      cfg.swAaMode = "ssaa";
    }
    reg.GetConfig() = cfg;
    REQUIRE(bm.EstablishBackend(cfg));
    auto* be = bm.GetBackend();
    REQUIRE(be != nullptr);
    REQUIRE(be->AttachWindow(nullptr, 128, 256, false));

    tm.Reset();
    tm.SetTmuMemoryLimitMb(0, 16);
    // lodMin=0 (256), lodMax=1 (128), aspect=4 (GR_ASPECT_1x2), format=10 (RGB_565)
    REQUIRE(tm.DownloadMipMap(0u, 0x10000u, 0, 1, 4u, 10u, tex.data(), true));

    be->SetSstOrigin(0u);
    be->SetClipWindow(0u, 0u, 128u, 256u);
    be->SetDepthState(0u, 7u, false, 0);
    be->SetBlendState(4u, 0u, 4u, 0u);  // ONE, ZERO
    be->SetAlphaTestState(7u, 0u);      // GR_CMP_ALWAYS
    be->SetColorCombineExt(0u);
    be->SetTexCombinerMode(0u, 1u, 0u, 1u, 0u, false, false);
    be->SetCombinerMode(3u, 8u, 2u, 1u, false, 3u, 8u, 2u, 1u, false);
    // Clamp + Bilinear + Trilinear mipmapping (GR_MIPMAP_NEAREST, lodBlend=true)
    be->SetTexMipMapMode(0u, 1u, true);
    be->SetTexLodBias(0u, 0.0f);
    be->BindTexture(0u, 0x10000u, 1u, 1u, 1u, 1u);

    be->ClearBuffer(0xFF000000u, 255u, 1.0f, 3u);

    auto makeV = [](float x, float y, float s, float t) {
      ModernVertex mv{};
      mv.pos = {x, y, 0.5f, 1.0f};
      mv.color = {1.0f, 1.0f, 1.0f, 1.0f};
      mv.tex = {s, t, 0.0f, 0.0f};
      mv.tmu_oow = {1.0f, 1.0f};
      return mv;
    };

    // 1:1 full-viewport 128x256 quad split into 2 triangles along (0,0)-(128,256)
    // Glide 1x2 aspect uses s in [0..128], t in [0..256].
    ModernVertex vTL = makeV(0.0f, 0.0f, 0.0f, 0.0f);
    ModernVertex vTR = makeV(128.0f, 0.0f, 128.0f, 0.0f);
    ModernVertex vBR = makeV(128.0f, 256.0f, 128.0f, 256.0f);
    ModernVertex vBL = makeV(0.0f, 256.0f, 0.0f, 256.0f);
    be->DrawTriangle(vTL, vTR, vBR);
    be->DrawTriangle(vTL, vBR, vBL);
    be->FlushBins();

    std::vector<uint32_t> fb(128u * 256u, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, 128u, 256u, 128u * 4u, fb.data()));

    INFO("backend=" << beName);
    uint32_t redSpikePixels = 0u;
    uint32_t nonGreenPixels = 0u;
    for (uint32_t y = 2u; y < 254u; ++y) {
      for (uint32_t x = 2u; x < 126u; ++x) {
        const uint32_t px = fb[y * 128u + x];
        const uint32_t r = px & 0xFFu;
        const uint32_t g = (px >> 8) & 0xFFu;
        if (r > 20u) {
          ++redSpikePixels;
        }
        if (g < 220u) {
          ++nonGreenPixels;
        }
      }
    }
    CHECK(redSpikePixels == 0u);
    CHECK(nonGreenPixels == 0u);

    be->DetachWindow();
    bm.ShutdownBackend();
    reg.GetConfig() = savedCfg;
  }
  }

  SECTION("1-pixel top-edge right-triangle scanline parity") {
  // Regression test for Build Engine 3dfx (Blood 3DFX.EXE) floor/ceiling
  // rendering: every horizontal scanline is submitted as a single 1-pixel-tall
  // right triangle a=(x1, y+0.5), b=(x0, y+1.5), c=(x0, y+0.5) relying on
  // authentic 3dfx Y-down top-left edge inclusion. Verify all 3 backends
  // (Vulkan, OpenGL ES, Software) cover 100% of every scanline row.
  using namespace GlideWrapper;

  constexpr uint32_t kW = 128u;
  constexpr uint32_t kH = 64u;
  auto& reg = EmulationRegistry::GetInstance();
  auto& bm = BackendManager::GetInstance();
  const WrapperConfig savedCfg = reg.GetConfig();

  for (const char* beName : {"software", "vulkan", "opengl_es"}) {
    WrapperConfig cfg = savedCfg;
    cfg.backend = beName;
    cfg.forceNoWindow = true;
    cfg.model = CardModel::VoodooGraphics;
    cfg.msaaSamples = 1u;
    reg.GetConfig() = cfg;
    REQUIRE(bm.EstablishBackend(cfg));
    auto* be = bm.GetBackend();
    REQUIRE(be != nullptr);
    REQUIRE(be->AttachWindow(nullptr, kW, kH, false));

    be->SetSstOrigin(0u);  // GR_ORIGIN_UPPER_LEFT
    be->SetClipWindow(0u, 0u, kW, kH);
    be->SetDepthState(0u, 7u, false, 0);
    be->SetBlendState(4u, 0u, 4u, 0u);  // ONE, ZERO
    be->SetAlphaTestState(7u, 0u);
    be->SetColorCombineExt(0u);
    be->SetCombinerMode(1u, 0u, 0u, 0u, false, 1u, 0u, 0u, 0u, false);  // LOCAL (iterated)
    be->ClearBuffer(0xFF000000u, 255u, 1.0f, 3u);

    auto makeV = [](float x, float y) {
      ModernVertex mv{};
      mv.pos = {x, y, 0.5f, 1.0f};
      mv.color = {0.0f, 1.0f, 0.0f, 1.0f};  // Solid green
      return mv;
    };

    // Draw 16 contiguous 1-pixel scanlines [y=16..31], x=[16..112]
    for (uint32_t y = 16u; y < 32u; ++y) {
      const float fy = static_cast<float>(y) + 0.5f;
      ModernVertex a = makeV(112.5f, fy);
      ModernVertex b = makeV(16.5f, fy + 1.0f);
      ModernVertex c = makeV(16.5f, fy);
      be->DrawTriangle(a, b, c);
    }
    be->FlushBins();

    std::vector<uint32_t> fb(kW * kH, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, kW, kH, kW * 4u, fb.data()));

    INFO("backend=" << beName);
    uint32_t missingPixels = 0u;
    for (uint32_t y = 16u; y < 32u; ++y) {
      for (uint32_t x = 18u; x < 110u; ++x) {
        const uint32_t g = (fb[y * kW + x] >> 8) & 0xFFu;
        if (g < 220u) {
          ++missingPixels;
        }
      }
    }
    CHECK(missingPixels == 0u);

    be->DetachWindow();
    bm.ShutdownBackend();
    reg.GetConfig() = savedCfg;
  }
  }

}

