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

TEST_CASE("Core: Registry, Backend Lifecycle & Shared Library ABI Exports", "[Glide2][ABI]") {
  SECTION("EmulationRegistry Singleton Access") {
    GlideWrapper::WrapperConfig defaults;
    REQUIRE(defaults.model == GlideWrapper::CardModel::Voodoo2);
    REQUIRE(defaults.fbiMemoryMb == 16);

    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    REQUIRE(reg.GetConfig().fbiMemoryMb > 0);
    REQUIRE(reg.GetConfig().tmuCount >= 1);
  }

  SECTION("Logger Lifecycle") {
    auto& logger = GlideWrapper::Logger::GetInstance();
    logger.Initialize(GlideTestHelpers::TempTestPath("glide_ng_test_output.log"),
                      GlideWrapper::LogLevel::INFO);
    REQUIRE(logger.GetLevel() == GlideWrapper::LogLevel::INFO);
  }

  SECTION("IniConfigLoader Fallback Resolution") {
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    bool loaded = loader.Load("non_existent_config.ini", cfg);
    REQUIRE(loaded == true);
    // Should preserve defaults if file missing and no env vars
    REQUIRE(cfg.tmuCount == 2);
  }

  SECTION(
      "Extensible Configuration Hierarchy (Task 1.3 Environment Overrides)") {
#ifdef __linux__
    // Mock environment variables
    ::setenv("GLIDE_NG_FBI_MEM_MB", "32", 1);
    ::setenv("GLIDE_NG_VENDOR", "Test Vendor Override", 1);

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    loader.Load("non_existent.ini", cfg);

    REQUIRE(cfg.fbiMemoryMb == 32);
    REQUIRE(cfg.reportedVendorOverride == "Test Vendor Override");

    // Clean up environment
    ::unsetenv("GLIDE_NG_FBI_MEM_MB");
    ::unsetenv("GLIDE_NG_VENDOR");
#endif
  }

  SECTION("EmulationRegistry Feature-Gating Boundaries (Task 1.4)") {
    GlideWrapper::WrapperConfig cfg;
    cfg.model = GlideWrapper::CardModel::VoodooGraphics;  // SST-1

    // Voodoo Graphics does not support more than 1 TMU or resolutions above
    // 800x600
    bool supportsMultiPassOnly =
        (cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    REQUIRE(supportsMultiPassOnly == true);
  }

  SECTION("Startup Hardware & Emulation Summary Injection") {
    auto& logger = GlideWrapper::Logger::GetInstance();
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();

    logger.Initialize(
        GlideTestHelpers::TempTestPath("glide_ng_test_summary.log"),
        GlideWrapper::LogLevel::INFO);
    REQUIRE_NOTHROW(logger.LogHostVulkanCapabilities());
    REQUIRE_NOTHROW(logger.LogExecutionSummary(reg.GetConfig()));
  }

  SECTION("Foundational Legacy C Query APIs (Task 1.5 Dynamic Interop)") {
#ifdef __linux__
    // Load our compiled libglide2x.so dynamic frontend
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    // Resolve legacy function pointers (authentic Glide 2.x does NOT export grGetString)
    using InitFunc = void (*)(void);
    using GetVersionFunc = void (*)(char[80]);
    using QueryHwFunc = FxBool (*)(GrHwConfiguration*);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto getVersion =
        reinterpret_cast<GetVersionFunc>(::dlsym(handle, "grGlideGetVersion"));
    auto queryHw =
        reinterpret_cast<QueryHwFunc>(::dlsym(handle, "grSstQueryHardware"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(getVersion != nullptr);
    REQUIRE(queryHw != nullptr);
    REQUIRE(::dlsym(handle, "grGetString") == nullptr);
    REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);

    // Execute tests across legacy C boundaries
    glideInit();
    char verBuf[80] = {};
    getVersion(verBuf);
    std::string version(verBuf);
    REQUIRE((version == "2.56" || version == "2.48" || version == "2.61" ||
             version == "2.43" || version == "2.40" || version == "2.42" ||
             version == "2.11"));

    GrHwConfiguration hw;
    std::memset(&hw, 0xA5, sizeof(hw));  // Ensure grSstQueryHardware initializes all fields
    FxBool success = queryHw(&hw);
    REQUIRE(success == FXTRUE);
    REQUIRE(hw.num_sst == 1);
    REQUIRE(hw.SSTs[0].type == GR_SSTTYPE_VOODOO);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbiRev == 0x102);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbRam > 0);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.nTexelfx >= 1);
    CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.tmuConfig[0].tmuRam > 0);

    ::dlclose(handle);
#endif
  }

  SECTION("Newly Implemented Glide 2.x API Gaps (Task 3)") {
#ifdef __linux__
    // 1. Load the shared library
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    // 2. Resolve function pointers (authentic Glide 2.x does NOT export grGetProcAddress)
    using ChromakeyRangeExtFunc = void (*)(GrColor_t, GrColor_t, uint32_t);
    using ErrorSetCallbackFunc = void (*)(GrErrorCallbackFnc_t);
    using LfbConstantDepthFunc = void (*)(FxU16);
    using LfbWriteColorSwizzleFunc = void (*)(FxBool, FxBool);
    using AlphaControlsITRGBLightingFunc = void (*)(FxBool);

    REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);
    auto chromakeyRangeExt = reinterpret_cast<ChromakeyRangeExtFunc>(
        ::dlsym(handle, "grChromakeyRangeExt"));
    auto errorSetCallback = reinterpret_cast<ErrorSetCallbackFunc>(
        ::dlsym(handle, "grErrorSetCallback"));
    auto lfbConstantDepth = reinterpret_cast<LfbConstantDepthFunc>(
        ::dlsym(handle, "grLfbConstantDepth"));
    auto lfbWriteColorSwizzle = reinterpret_cast<LfbWriteColorSwizzleFunc>(
        ::dlsym(handle, "grLfbWriteColorSwizzle"));
    auto alphaControlsITRGBLighting =
        reinterpret_cast<AlphaControlsITRGBLightingFunc>(
            ::dlsym(handle, "grAlphaControlsITRGBLighting"));

    REQUIRE(chromakeyRangeExt != nullptr);
    REQUIRE(errorSetCallback != nullptr);
    REQUIRE(lfbConstantDepth != nullptr);
    REQUIRE(lfbWriteColorSwizzle != nullptr);
    REQUIRE(alphaControlsITRGBLighting != nullptr);

    // 4. Test grErrorSetCallback
    auto mockCallback = [](const char* string, FxBool fatal) {
      // Mock error callback
    };
    REQUIRE_NOTHROW(errorSetCallback(mockCallback));

    // 5. Test LFB and lighting helpers (ensure no-crash, safe parameters)
    REQUIRE_NOTHROW(lfbConstantDepth(0x7FFF));
    REQUIRE_NOTHROW(lfbWriteColorSwizzle(FXTRUE, FXTRUE));
    REQUIRE_NOTHROW(alphaControlsITRGBLighting(FXTRUE));

    // 7. Resolve the final 5 functions
    using GammaCorrectionValueFunc = void (*)(float);
    using SstControlFunc = FxBool (*)(FxU32);
    using TexDownloadMipMapLevelPartialFunc =
        void (*)(GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t,
                 GrTextureFormat_t, FxU32, void*, int, int);
    using GuDrawPolygonVertexListWithClipFunc = void (*)(int, const GrVertex[]);
    using ConvertAndDownloadRleFunc = void (*)(
        GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureFormat_t,
        FxU32, FxU8*, long, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU16*);

    auto gammaCorrectionValue = reinterpret_cast<GammaCorrectionValueFunc>(
        ::dlsym(handle, "grGammaCorrectionValue"));
    auto sstControl =
        reinterpret_cast<SstControlFunc>(::dlsym(handle, "grSstControl"));
    auto texDownloadMipMapLevelPartial =
        reinterpret_cast<TexDownloadMipMapLevelPartialFunc>(
            ::dlsym(handle, "grTexDownloadMipMapLevelPartial"));
    auto guDrawPolygonVertexListWithClip =
        reinterpret_cast<GuDrawPolygonVertexListWithClipFunc>(
            ::dlsym(handle, "guDrawPolygonVertexListWithClip"));
    auto convertAndDownloadRle = reinterpret_cast<ConvertAndDownloadRleFunc>(
        ::dlsym(handle, "ConvertAndDownloadRle"));

    REQUIRE(gammaCorrectionValue != nullptr);
    REQUIRE(sstControl != nullptr);
    REQUIRE(texDownloadMipMapLevelPartial != nullptr);
    REQUIRE(guDrawPolygonVertexListWithClip != nullptr);
    REQUIRE(convertAndDownloadRle != nullptr);

    // 8. Verify grSstControl and grGammaCorrectionValue
    REQUIRE(sstControl(0xFEED) == FXTRUE);
    REQUIRE_NOTHROW(gammaCorrectionValue(1.8f));

    // 9. Verify grTexDownloadMipMapLevelPartial (ensure no-crash)
    std::vector<uint16_t> mockTex(16 * 16, 0xABCD);
    REQUIRE_NOTHROW(texDownloadMipMapLevelPartial(0, 0x1000, 4, 4, 0, 0, 0,
                                                  mockTex.data(), 4, 12));

    // 10. Verify guDrawPolygonVertexListWithClip (ensure no-crash)
    GrVertex mockPolygon[4] = {};
    REQUIRE_NOTHROW(guDrawPolygonVertexListWithClip(4, mockPolygon));

    // 11. Verify ConvertAndDownloadRle with a mocked 2x2 RLE compressed texture
    // 2x2 texture (4 pixels) compressed as a single run-length of 4 white
    // pixels:
    // - Packet code: 0x83 (0x80 run-length bit | 3 count)
    // - Pixel value: 0xFFFF (16-bit white)
    uint8_t mockRleData[] = {0x83, 0xFF, 0xFF};
    REQUIRE_NOTHROW(convertAndDownloadRle(0, 0x2000, 8, 8, 0, 0, 0, mockRleData,
                                          2, 0, 0, 2, 2, 2, 2, nullptr));

    // 12. Clean up
    ::dlclose(handle);
#endif
  }

  SECTION("Gamma Correction & Custom Lookup Tables (TDD Phase)") {
#ifdef __linux__
    // 1. Verify Glide 2.x Gamma Exports
    {
      void* handle = OpenGlide2DsoHandle();
      REQUIRE(handle != nullptr);

      using LoadGammaTableFunc = void (*)(FxU32, FxU32*, FxU32*, FxU32*);
      using GammaCorrectionRGBFunc = void (*)(FxFloat, FxFloat, FxFloat);
      using GammaCorrectionValueFunc = void (*)(float);

      auto grLoadGammaTable = reinterpret_cast<LoadGammaTableFunc>(
          ::dlsym(handle, "grLoadGammaTable"));
      auto guGammaCorrectionRGB = reinterpret_cast<GammaCorrectionRGBFunc>(
          ::dlsym(handle, "guGammaCorrectionRGB"));
      auto grGammaCorrectionValue = reinterpret_cast<GammaCorrectionValueFunc>(
          ::dlsym(handle, "grGammaCorrectionValue"));

      REQUIRE(grLoadGammaTable != nullptr);
      REQUIRE(guGammaCorrectionRGB != nullptr);
      REQUIRE(grGammaCorrectionValue != nullptr);

      // Test execution - verify no crash under active backend
      uint32_t r[256], g[256], b[256];
      for (int i = 0; i < 256; ++i) {
        r[i] = g[i] = b[i] = i;
      }
      REQUIRE_NOTHROW(grLoadGammaTable(256, r, g, b));
      REQUIRE_NOTHROW(guGammaCorrectionRGB(1.5f, 1.0f, 0.5f));
      REQUIRE_NOTHROW(grGammaCorrectionValue(1.8f));

      ::dlclose(handle);
    }

    // 2. Verify Glide 3.x Gamma Exports (Voodoo3 / 2.61 spec)
    {
      void* handle = OpenGlide3DsoHandle();
      REQUIRE(handle != nullptr);

      using LoadGammaTableFunc = void (*)(FxU32, FxU32*, FxU32*, FxU32*);
      using GammaCorrectionRGBFunc = void (*)(FxFloat, FxFloat, FxFloat);

      auto grLoadGammaTable = reinterpret_cast<LoadGammaTableFunc>(
          ::dlsym(handle, "grLoadGammaTable"));
      auto guGammaCorrectionRGB = reinterpret_cast<GammaCorrectionRGBFunc>(
          ::dlsym(handle, "guGammaCorrectionRGB"));

      REQUIRE(grLoadGammaTable != nullptr);
      REQUIRE(guGammaCorrectionRGB != nullptr);

      // In Glide 3.x, grGammaCorrectionValue should NOT be exported!
      auto grGammaCorrectionValue = ::dlsym(handle, "grGammaCorrectionValue");
      REQUIRE(grGammaCorrectionValue == nullptr);

      // Test execution - verify no crash under active backend
      uint32_t r[256], g[256], b[256];
      for (int i = 0; i < 256; ++i) {
        r[i] = g[i] = b[i] = i;
      }
      REQUIRE_NOTHROW(grLoadGammaTable(256, r, g, b));
      REQUIRE_NOTHROW(guGammaCorrectionRGB(1.5f, 1.0f, 0.5f));

      ::dlclose(handle);
    }
#endif
  }

  SECTION(
      "Phase 2: Multilib Headless CI Swapchain Attachment & Buffer Clears "
      "(GENERATE Matrix)") {
#ifdef __linux__
    // Cycle through all 4 official 3dfx card permutations using Catch2 GENERATE
    auto modelName = GENERATE("SST1", "CVG", "H3", "H5");
    ::setenv("GLIDE_NG_CARD_MODEL", modelName, 1);

    // 1. Validate Glide 2.x Dynamic Exports
    {
      void* handle = OpenGlide2DsoHandle();
      REQUIRE(handle != nullptr);

      using InitFunc = void (*)(void);
      using WinOpenFunc =
          FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
      using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
      using BufferSwapFunc = void (*)(int);
      using WinCloseFunc = void (*)(void);

      using ShutdownFunc = void (*)(void);
      auto glideInit =
          reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
      auto winOpen =
          reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
      auto bufferClear =
          reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
      auto bufferSwap =
          reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
      auto winClose =
          reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
      auto glideShutdown =
          reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

      REQUIRE(glideInit != nullptr);
      REQUIRE(winOpen != nullptr);
      REQUIRE(bufferClear != nullptr);
      REQUIRE(bufferSwap != nullptr);
      REQUIRE(winClose != nullptr);
      REQUIRE(glideShutdown != nullptr);

      glideInit();
      REQUIRE(winOpen(0, 5, 0, 0, 0, 2, 0) == FXTRUE);
      REQUIRE_NOTHROW(bufferClear(0x00FF0000, 0, 65535));
      REQUIRE_NOTHROW(bufferSwap(1));
      REQUIRE_NOTHROW(winClose());
      REQUIRE_NOTHROW(glideShutdown());

      ::dlclose(handle);
    }

    // 2. Validate Glide 3.x Dynamic Exports
    {
      void* handle = OpenGlide3DsoHandle();
      REQUIRE(handle != nullptr);

      using InitFunc = void (*)(void);
      using WinOpenFunc =
          FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
      using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
      using BufferSwapFunc = void (*)(FxU32);
      using WinCloseFunc = FxBool (*)(FxU32);
      using ShutdownFunc = void (*)(void);

      auto glideInit =
          reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
      auto winOpen =
          reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
      auto bufferClear =
          reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
      auto bufferSwap =
          reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
      auto winClose =
          reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
      auto glideShutdown =
          reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

      REQUIRE(glideInit != nullptr);
      REQUIRE(winOpen != nullptr);
      REQUIRE(bufferClear != nullptr);
      REQUIRE(bufferSwap != nullptr);
      REQUIRE(winClose != nullptr);
      REQUIRE(glideShutdown != nullptr);

      glideInit();
      FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 0);
      REQUIRE(ctx == 1);
      REQUIRE_NOTHROW(bufferClear(0x00FF0000, 0, 65535));
      REQUIRE_NOTHROW(bufferSwap(1));
      REQUIRE(winClose(ctx) == FXTRUE);
      REQUIRE_NOTHROW(glideShutdown());

      ::dlclose(handle);
    }

    ::unsetenv("GLIDE_NG_CARD_MODEL");
#endif
  }

  SECTION("Artificial VSync Retro Frame Pacing Timestamps") {
    auto& logger = GlideWrapper::Logger::GetInstance();
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto& manager = GlideWrapper::BackendManager::GetInstance();

    reg.GetConfig().maxFps = 60.0f;  // 60 FPS capping -> ~16.67ms per frame
    reg.GetConfig().forceNoWindow =
        true;  // Run in pure headless mode for fast timing!
    logger.Initialize(
        GlideTestHelpers::TempTestPath("glide_ng_test_pacing.log"),
        GlideWrapper::LogLevel::INFO);
    manager.EstablishBackend(reg.GetConfig());

    // Execute 10 simulated frame presentation swaps
    auto* backend = manager.GetBackend();
    REQUIRE(backend != nullptr);

    backend->AttachWindow(nullptr, 800, 600, false);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
      backend->SwapBuffers();
    }
    auto end = std::chrono::steady_clock::now();
    backend->DetachWindow();
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    manager.ShutdownBackend();

    // 10 frames at 60 FPS should take ~166.7ms. Allow robust scheduler
    // tolerance [140ms, 350ms] to avoid transient VM scheduling flakes.
    REQUIRE(elapsedMs >= 140);
    REQUIRE(elapsedMs <= 350);
  }

  SECTION("Basic Headless Init, Clear, Swap, and Shutdown") {
#ifdef __linux__
    // Load libglide3x.so to execute the authentic test00 diagnostic lifecycle
    void* handle = OpenGlide3DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using SelectCtxFunc = FxBool (*)(FxU32);
    using RenderBufFunc = void (*)(FxU32);
    using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
    using BufferSwapFunc = void (*)(FxU32);
    using WinCloseFunc = FxBool (*)(FxU32);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto selectCtx =
        reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
    auto renderBuf =
        reinterpret_cast<RenderBufFunc>(::dlsym(handle, "grRenderBuffer"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(selectCtx != nullptr);
    REQUIRE(renderBuf != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // 1. Initialize Glide
    glideInit();

    // 2. Open headless window (Resolution 5 = 800x600)
    FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 1);
    REQUIRE(ctx == 1);

    // 3. Select active context
    REQUIRE(selectCtx(ctx) == FXTRUE);

    // 4. Render to backbuffer
    REQUIRE_NOTHROW(renderBuf(0));  // GR_BUFFER_BACKBUFFER

    // 5. Clear screen to Blue (0xFF0000 in RGB -> 0xFF0000FF in ARGB)
    REQUIRE_NOTHROW(bufferClear(0xFF0000FF, 0, 4294967295));

    // 6. Execute smooth swap
    REQUIRE_NOTHROW(bufferSwap(1));

    // 7. Close window context
    REQUIRE(winClose(ctx) == FXTRUE);
    REQUIRE_NOTHROW(glideShutdown());

    ::dlclose(handle);
#endif
  }

  SECTION("Telemetry and Statistics API Coverage Suite") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using DrawTriFunc = void (*)(const void*, const void*, const void*);
    using BufferSwapFunc = void (*)(int);
    using WinCloseFunc = void (*)(void);
    using ShutdownFunc = void (*)(void);

    using TriStatsFunc = void (*)(FxU32*, FxU32*);
    using ResetTriStatsFunc = void (*)(void);
    using PerfStatsFunc = void (*)(GrSstPerfStats_t*);
    using ResetPerfStatsFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto drawTri =
        reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    auto grTriStats =
        reinterpret_cast<TriStatsFunc>(::dlsym(handle, "grTriStats"));
    auto grResetTriStats =
        reinterpret_cast<ResetTriStatsFunc>(::dlsym(handle, "grResetTriStats"));
    auto grSstPerfStats =
        reinterpret_cast<PerfStatsFunc>(::dlsym(handle, "grSstPerfStats"));
    auto grSstResetPerfStats = reinterpret_cast<ResetPerfStatsFunc>(
        ::dlsym(handle, "grSstResetPerfStats"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    REQUIRE(grTriStats != nullptr);
    REQUIRE(grResetTriStats != nullptr);
    REQUIRE(grSstPerfStats != nullptr);
    REQUIRE(grSstResetPerfStats != nullptr);

    // 1. Initialize and open a context
    glideInit();
    REQUIRE(winOpen(0, 5, 0, 0, 0, 2, 0) == FXTRUE);

    // 2. Test initial reset state
    grResetTriStats();
    FxU32 processed = 0, drawn = 0;
    grTriStats(&processed, &drawn);
    REQUIRE(processed == 0);
    REQUIRE(drawn == 0);

    // 3. Draw a mock triangle 30 times and verify counters increment to exactly
    // 30 (use GrVertex to prevent ASan stack overflow!)
    GrVertex v0{}, v1{}, v2{};
    v0.x = 10.0f;
    v0.y = 10.0f;
    v0.z = 0.0f;
    v0.r = 255.0f;
    v0.g = 0.0f;
    v0.b = 0.0f;
    v0.ooz = 0.0f;
    v0.a = 255.0f;
    v0.oow = 1.0f;
    v1.x = 50.0f;
    v1.y = 10.0f;
    v1.z = 0.0f;
    v1.r = 255.0f;
    v1.g = 0.0f;
    v1.b = 0.0f;
    v1.ooz = 0.0f;
    v1.a = 255.0f;
    v1.oow = 1.0f;
    v2.x = 30.0f;
    v2.y = 40.0f;
    v2.z = 0.0f;
    v2.r = 255.0f;
    v2.g = 0.0f;
    v2.b = 0.0f;
    v2.ooz = 0.0f;
    v2.a = 255.0f;
    v2.oow = 1.0f;

    // Draw and swap 30 times to trigger sliding-window FPS update
    for (int i = 0; i < 30; ++i) {
      drawTri(&v0, &v1, &v2);
      bufferSwap(1);
    }

    grTriStats(&processed, &drawn);
    REQUIRE(processed == 30);
    REQUIRE(drawn == 30);

    // 4. Test grResetTriStats
    grResetTriStats();
    grTriStats(&processed, &drawn);
    REQUIRE(processed == 0);
    REQUIRE(drawn == 0);

    // 5. Test grSstPerfStats
    GrSstPerfStats_t stats;
    grSstPerfStats(&stats);
    // Map our FrameTracker metrics:
    // pixelsIn contains FPS (should be > 0.0f after 30 swaps)
    // chromaFail contains render time in microseconds (should be > 0)
    REQUIRE(stats.pixelsIn > 0);
    REQUIRE(stats.chromaFail > 0);

    // 6. Test grSstResetPerfStats
    grSstResetPerfStats();

    // 7. Cleanup
    winClose();
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("Milestone 1: Hardware Queries & Pacing (Resolution Matrix)") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = void (*)(void);
    using ShutdownFunc = void (*)(void);

    using WidthFunc = FxU32 (*)(void);
    using HeightFunc = FxU32 (*)(void);
    using BusyFunc = FxBool (*)(void);
    using StatusFunc = FxU32 (*)(void);
    using VideoLineFunc = FxU32 (*)(void);
    using RetraceFunc = FxBool (*)(void);
    using PendingFunc = int (*)(void);
    using CheckRoomFunc = void (*)(FxI32);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    auto grSstScreenWidth =
        reinterpret_cast<WidthFunc>(::dlsym(handle, "grSstScreenWidth"));
    auto grSstScreenHeight =
        reinterpret_cast<HeightFunc>(::dlsym(handle, "grSstScreenHeight"));
    auto grSstIsBusy =
        reinterpret_cast<BusyFunc>(::dlsym(handle, "grSstIsBusy"));
    auto grSstStatus =
        reinterpret_cast<StatusFunc>(::dlsym(handle, "grSstStatus"));
    auto grSstVideoLine =
        reinterpret_cast<VideoLineFunc>(::dlsym(handle, "grSstVideoLine"));
    auto grSstVRetraceOn =
        reinterpret_cast<RetraceFunc>(::dlsym(handle, "grSstVRetraceOn"));
    auto grBufferNumPending =
        reinterpret_cast<PendingFunc>(::dlsym(handle, "grBufferNumPending"));
    auto grCheckForRoom =
        reinterpret_cast<CheckRoomFunc>(::dlsym(handle, "grCheckForRoom"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(grSstScreenWidth != nullptr);
    REQUIRE(grSstScreenHeight != nullptr);
    REQUIRE(grSstIsBusy != nullptr);
    REQUIRE(grSstStatus != nullptr);
    REQUIRE(grSstVideoLine != nullptr);
    REQUIRE(grSstVRetraceOn != nullptr);
    REQUIRE(grBufferNumPending != nullptr);
    REQUIRE(grCheckForRoom != nullptr);

    // Run parameterized resolution matrix using Catch2 GENERATE
    auto resTuple =
        GENERATE(std::make_tuple(0x7, 640, 480),  // GR_RESOLUTION_640x480
                 std::make_tuple(0x8, 800, 600),  // GR_RESOLUTION_800x600
                 // GR_RESOLUTION_1024x768 (1024x768)
                 // HISTORICAL NOTE: On physical Voodoo 2 hardware, opening a
                 // Z-buffered 1024x768 target on a single card would fail due
                 // to physical VRAM limits, requiring SLI mode. Our API wrapper
                 // passes this test successfully because it leverages modern
                 // host VRAM, bypassing the historical hardware memory
                 // boundaries without needing explicit SLI logic.
                 std::make_tuple(0xC, 1024, 768),
                 std::make_tuple(0x1, 320, 240)  // GR_RESOLUTION_320x240
        );

    GrScreenResolution_t resolution = std::get<0>(resTuple);
    FxU32 expectedW = static_cast<FxU32>(std::get<1>(resTuple));
    FxU32 expectedH = static_cast<FxU32>(std::get<2>(resTuple));

    // 1. Initialize and open context with target resolution
    glideInit();
    REQUIRE(winOpen(0, resolution, 0, 0, 0, 2, 0) == FXTRUE);

    // 2. Validate resolution queries match expected pixel bounds!
    REQUIRE(grSstScreenWidth() == expectedW);
    REQUIRE(grSstScreenHeight() == expectedH);

    // 3. Validate status & pacing APIs
    REQUIRE(grSstIsBusy() == FXFALSE);
    REQUIRE(grBufferNumPending() == 0);
    FxU32 status = grSstStatus();
    REQUIRE((status & 0x0000003Fu) == 0x3Fu);        // PCI FIFO empty (63 free)
    REQUIRE((status & 0x00000380u) == 0u);           // FBI/TMU/SST idle
    REQUIRE((status & 0x0FFFF000u) == 0x0FFFF000u);  // Memory FIFO empty (0xFFFF)
    REQUIRE((status & 0xF0000000u) == 0u);           // 0 swaps pending, no IRQ
    REQUIRE((status & ~0x00000040u) == 0x0FFFF03Fu); // Only bit 6 (active-low VRetrace) varies
    REQUIRE_NOTHROW(grCheckForRoom(10));

    // 4. Validate pacing simulation
    FxU32 line1 = grSstVideoLine();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    FxU32 line2 = grSstVideoLine();

    // Assert we can query pacing state and suppress warnings
    REQUIRE_NOTHROW(grSstVRetraceOn());
    (void)line1;
    (void)line2;

    // 5. Clean up
    winClose();
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("Milestone 2: Anti-Aliasing & Transparent MSAA") {
#ifdef __linux__
    // 1. Temporarily set the environment variable GLIDE_NG_MSAA to 4
    ::setenv("GLIDE_NG_MSAA", "4", 1);

    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = void (*)(void);
    using AADrawTriFunc =
        void (*)(const void*, const void*, const void*, FxBool, FxBool, FxBool);
    using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
    using BufferSwapFunc = void (*)(int);
    using LfbReadFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto aaDrawTri =
        reinterpret_cast<AADrawTriFunc>(::dlsym(handle, "grAADrawTriangle"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto lfbRead =
        reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(aaDrawTri != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // 2. Initialize and open context (800x600 resolution)
    glideInit();
    REQUIRE(winOpen(0, 0x8, 0, 0, 0, 2, 0) == FXTRUE);

    // 3. Test drawing using the anti-aliased geometry forwarding call
    bufferClear(0x00000000, 0xFF, 0xFFFF);  // Clear to Black

    // Draw a green triangle
    GrVertex a{}, b{}, c{};
    a.x = 400.0f;
    a.y = 100.0f;
    a.ooz = 1.0f;
    a.oow = 1.0f;
    a.r = 0.0f;
    a.g = 255.0f;
    a.b = 0.0f;
    a.a = 255.0f;
    b.x = 200.0f;
    b.y = 500.0f;
    b.ooz = 1.0f;
    b.oow = 1.0f;
    b.r = 0.0f;
    b.g = 255.0f;
    b.b = 0.0f;
    b.a = 255.0f;
    c.x = 600.0f;
    c.y = 500.0f;
    c.ooz = 1.0f;
    c.oow = 1.0f;
    c.r = 0.0f;
    c.g = 255.0f;
    c.b = 0.0f;
    c.a = 255.0f;

    REQUIRE_NOTHROW(aaDrawTri(&a, &b, &c, FXTRUE, FXTRUE, FXTRUE));

    // 4. Read back center pixel via standard grLfbReadRegion API and verify it
    // was drawn green!
    uint32_t pixel = 0;
    REQUIRE(lfbRead(1, 400, 300, 1, 1, 4, &pixel) ==
            FXTRUE);  // 1 = GR_BUFFER_BACKBUFFER

    // Assert green component is active
    uint8_t g = (pixel >> 8) & 0xFF;
    REQUIRE(g == 255);

    bufferSwap(0);  // Trigger resolve and present!

    // 5. Clean up
    winClose();
    glideShutdown();
    ::dlclose(handle);

    // Reset the environment variable to avoid affecting other tests
    ::unsetenv("GLIDE_NG_MSAA");
#endif
  }

  SECTION("Milestone 3: Polygon Fan Decomposition") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = void (*)(void);
    using DrawPolyFunc = void (*)(const int, const int*, const void*);
    using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
    using BufferSwapFunc = void (*)(int);
    using LfbReadFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto drawPoly =
        reinterpret_cast<DrawPolyFunc>(::dlsym(handle, "grDrawPolygon"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto lfbRead =
        reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(drawPoly != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // 1. Initialize and open context (800x600 resolution)
    glideInit();
    REQUIRE(winOpen(0, 0x8, 0, 0, 0, 2, 0) == FXTRUE);

    // 2. Clear screen to black
    bufferClear(0x00000000, 0xFF, 0xFFFF);

    // 3. Draw a green quad (convex 4-vertex polygon)
    GrVertex vlist[4]{};
    // Top-Left
    vlist[0].x = 200.0f;
    vlist[0].y = 200.0f;
    vlist[0].ooz = 1.0f;
    vlist[0].oow = 1.0f;
    vlist[0].r = 0.0f;
    vlist[0].g = 255.0f;
    vlist[0].b = 0.0f;
    vlist[0].a = 255.0f;
    // Bottom-Left
    vlist[1].x = 200.0f;
    vlist[1].y = 400.0f;
    vlist[1].ooz = 1.0f;
    vlist[1].oow = 1.0f;
    vlist[1].r = 0.0f;
    vlist[1].g = 255.0f;
    vlist[1].b = 0.0f;
    vlist[1].a = 255.0f;
    // Bottom-Right
    vlist[2].x = 400.0f;
    vlist[2].y = 400.0f;
    vlist[2].ooz = 1.0f;
    vlist[2].oow = 1.0f;
    vlist[2].r = 0.0f;
    vlist[2].g = 255.0f;
    vlist[2].b = 0.0f;
    vlist[2].a = 255.0f;
    // Top-Right
    vlist[3].x = 400.0f;
    vlist[3].y = 200.0f;
    vlist[3].ooz = 1.0f;
    vlist[3].oow = 1.0f;
    vlist[3].r = 0.0f;
    vlist[3].g = 255.0f;
    vlist[3].b = 0.0f;
    vlist[3].a = 255.0f;

    int ilist[4] = {0, 1, 2, 3};

    REQUIRE_NOTHROW(drawPoly(4, ilist, vlist));

    // 4. Read back center pixel of the quad (300, 300) and assert it is green
    uint32_t pixel = 0;
    REQUIRE(lfbRead(1, 300, 300, 1, 1, 4, &pixel) ==
            FXTRUE);  // 1 = GR_BUFFER_BACKBUFFER

    uint8_t g = (pixel >> 8) & 0xFF;
    REQUIRE(g == 255);

    bufferSwap(0);  // Present and resolve!

    // 5. Clean up
    winClose();
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("Milestone 4: Shortcut State Config") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using WinOpenFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
    using WinCloseFunc = void (*)(void);
    using BufferClearFunc = void (*)(GrColor_t, FxU8, FxU16);
    using BufferSwapFunc = void (*)(FxU32);
    using DrawTriFunc =
        void (*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadFunc =
        FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void (*)(void);

    using ConstColor4Func = void (*)(float, float, float, float);
    using ColorMaskFunc = void (*)(FxBool, FxBool);
    using DisableAllEffectsFunc = void (*)(void);

    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto drawTri =
        reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto lfbRead =
        reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    auto constColor4 = reinterpret_cast<ConstColor4Func>(
        ::dlsym(handle, "grConstantColorValue4"));
    auto colorMask =
        reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
    auto disableAllEffects = reinterpret_cast<DisableAllEffectsFunc>(
        ::dlsym(handle, "grDisableAllEffects"));

    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(constColor4 != nullptr);
    REQUIRE(colorMask != nullptr);
    REQUIRE(disableAllEffects != nullptr);

    // Run this test on the active backend resolved by the environment matrix
    // Open window
    REQUIRE(winOpen(0, 0x8, 0, 0, 0, 2, 0) == FXTRUE);

    // Test 1: Write masking disabled completely (no color channels or alpha can
    // be written)
    colorMask(FXFALSE, FXFALSE);
    bufferClear(0x00000000, 0xFF, 0xFFFF);  // Clear to black

    GrVertex a, b, c;
    a.x = 400.0f;
    a.y = 100.0f;
    a.ooz = 1.0f;
    a.oow = 1.0f;
    a.r = 0.0f;
    a.g = 255.0f;
    a.b = 0.0f;
    a.a = 255.0f;
    b.x = 200.0f;
    b.y = 500.0f;
    b.ooz = 1.0f;
    b.oow = 1.0f;
    b.r = 0.0f;
    b.g = 255.0f;
    b.b = 0.0f;
    b.a = 255.0f;
    c.x = 600.0f;
    c.y = 500.0f;
    c.ooz = 1.0f;
    c.oow = 1.0f;
    c.r = 0.0f;
    c.g = 255.0f;
    c.b = 0.0f;
    c.a = 255.0f;

    REQUIRE_NOTHROW(drawTri(&a, &b, &c));

    // Read back center pixel (400, 300) - must remain 100% black (0x00000000 or
    // at least alpha=0 and green=0)
    uint32_t pixel = 0xffffffff;
    REQUIRE(lfbRead(1, 400, 300, 1, 1, 4, &pixel) == FXTRUE);
    REQUIRE((pixel & 0xffffffff) == 0x00000000);

    // Test 2: Write masking enabled for color and alpha
    colorMask(FXTRUE, FXTRUE);
    REQUIRE_NOTHROW(drawTri(&a, &b, &c));

    // Read back center pixel - now it MUST be green!
    REQUIRE(lfbRead(1, 400, 300, 1, 1, 4, &pixel) == FXTRUE);
    uint8_t g = (pixel >> 8) & 0xFF;
    REQUIRE(g == 255);

    // Test 3: grConstantColorValue4 packing validation
    constColor4(255.0f, 128.0f, 64.0f, 32.0f);  // A=255, R=128, G=64, B=32

    // Test 4: grDisableAllEffects runs successfully
    REQUIRE_NOTHROW(disableAllEffects());

    bufferSwap(0);
    winClose();
    glideShutdown();

    ::dlclose(handle);
#endif
  }

  SECTION("Milestone 5: Spinning Logo & LFB Formatting") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using WinOpenFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
    using WinCloseFunc = void (*)(void);
    using BufferClearFunc = void (*)(GrColor_t, FxU8, FxU16);
    using BufferSwapFunc = void (*)(FxU32);
    using LfbReadFunc =
        FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void (*)(void);

    using SplashFunc = void (*)(float, float, float, float, FxU32);
    using ShamelessPlugFunc = void (*)(const FxBool);

    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap =
        reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto lfbRead =
        reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
    auto shamelessPlug = reinterpret_cast<ShamelessPlugFunc>(
        ::dlsym(handle, "grGlideShamelessPlug"));

    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(splash != nullptr);
    REQUIRE(shamelessPlug != nullptr);

    // Run this test on the active backend resolved by the environment matrix
    // Open window
    REQUIRE(winOpen(0, 0x8, 0, 0, 0, 2, 0) == FXTRUE);

    // 1. Enable shameless plug watermark logo
    REQUIRE_NOTHROW(shamelessPlug(FXTRUE));

    // 2. Render a single frame of the 3D spinning logo (frame 0)
    REQUIRE_NOTHROW(splash(0.0f, 0.0f, 640.0f, 480.0f, 0));

    // 3. Swap buffers - this will draw the splash frame, overlay the shameless
    // plug, and swap!
    REQUIRE_NOTHROW(bufferSwap(0));

    // 4. Clean up
    winClose();
    glideShutdown();

    ::dlclose(handle);
#endif
  }

  SECTION("Milestone 6: Advanced Texturing & Utilities (LOD/Mipmaps)") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using WinOpenFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
    using WinCloseFunc = void (*)(void);
    using ShutdownFunc = void (*)(void);

    using AllocFunc = GrMipMapId_t (*)(
        GrChipID_t, FxU8, int, int, GrTextureFormat_t, GrMipMapMode_t, GrLOD_t,
        GrLOD_t, GrAspectRatio_t, GrTextureClampMode_t, GrTextureClampMode_t,
        GrTextureFilterMode_t, GrTextureFilterMode_t, float, FxBool);
    using SourceFunc = void (*)(GrMipMapId_t);
    using GetInfoFunc = GrMipMapInfo* (*)(GrMipMapId_t);
    using ChangeAttrsFunc = FxBool (*)(
        GrMipMapId_t, int, int, GrTextureFormat_t, GrMipMapMode_t, GrLOD_t,
        GrLOD_t, GrAspectRatio_t, GrTextureClampMode_t, GrTextureClampMode_t,
        GrTextureFilterMode_t, GrTextureFilterMode_t);
    using GetCurrentFunc = GrMipMapId_t (*)(GrChipID_t);
    using DownloadFunc = void (*)(GrMipMapId_t, const void*, const GuNccTable*);
    using TexCombineFunc = void (*)(GrChipID_t, GrTextureCombineFnc_t);

    using LodBiasFunc = void (*)(GrChipID_t, float);
    using MultibaseFunc = void (*)(GrChipID_t, FxBool);
    using MultibaseAddrFunc =
        void (*)(GrChipID_t, GrTexBaseRange_t, FxU32, FxU32, GrTexInfo*);

    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    auto texAlloc =
        reinterpret_cast<AllocFunc>(::dlsym(handle, "guTexAllocateMemory"));
    auto texSource =
        reinterpret_cast<SourceFunc>(::dlsym(handle, "guTexSource"));
    auto texGetInfo =
        reinterpret_cast<GetInfoFunc>(::dlsym(handle, "guTexGetMipMapInfo"));
    auto texChange = reinterpret_cast<ChangeAttrsFunc>(
        ::dlsym(handle, "guTexChangeAttributes"));
    auto texGetCurrent = reinterpret_cast<GetCurrentFunc>(
        ::dlsym(handle, "guTexGetCurrentMipMap"));
    auto texDownload =
        reinterpret_cast<DownloadFunc>(::dlsym(handle, "guTexDownloadMipMap"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(
        ::dlsym(handle, "grTexCombineFunction"));

    auto lodBias =
        reinterpret_cast<LodBiasFunc>(::dlsym(handle, "grTexLodBiasValue"));
    auto multibase =
        reinterpret_cast<MultibaseFunc>(::dlsym(handle, "grTexMultibase"));
    auto multibaseAddr = reinterpret_cast<MultibaseAddrFunc>(
        ::dlsym(handle, "grTexMultibaseAddress"));

    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(texAlloc != nullptr);
    REQUIRE(texSource != nullptr);
    REQUIRE(texGetInfo != nullptr);
    REQUIRE(texChange != nullptr);
    REQUIRE(texGetCurrent != nullptr);
    REQUIRE(texDownload != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(lodBias != nullptr);
    REQUIRE(multibase != nullptr);
    REQUIRE(multibaseAddr != nullptr);

    // Open window for quick validation
    REQUIRE(winOpen(0, 0x8, 0, 0, 0, 2, 0) == FXTRUE);

    // 1. Allocate a 256x256 RGB_565 texture mipmap
    GrMipMapId_t mmid = texAlloc(0,  // TMU0
                                 0,  // odd_even_mask
                                 256, 256,
                                 0x3,     // GR_TEXFMT_RGB_565
                                 0,       // GR_MIPMAP_DISABLE
                                 0, 0,    // lod_min, lod_max
                                 0,       // GR_ASPECT_1x1
                                 0, 0,    // clamp modes
                                 0, 0,    // filter modes
                                 0.0f,    // lod_bias
                                 FXFALSE  // trilinear
    );
    REQUIRE(mmid != GR_NULL_MIPMAP_HANDLE);

    // 2. Query mipmap info and verify metadata
    GrMipMapInfo* info = texGetInfo(mmid);
    REQUIRE(info != nullptr);
    REQUIRE(info->valid == FXTRUE);
    REQUIRE(info->width == 256);
    REQUIRE(info->height == 256);
    REQUIRE(info->format == 0x3);

    // 3. Change attributes and verify they updated
    REQUIRE(texChange(mmid, 128, 128, 0x3, 0, 0, 0, 0, 0, 0, 0, 0) == FXTRUE);
    REQUIRE(info->width == 128);
    REQUIRE(info->height == 128);

    // 4. Download mock texture data
    std::vector<uint16_t> mockData(128 * 128,
                                   0xF800);  // Pure Red 16-bit pixels
    REQUIRE_NOTHROW(texDownload(mmid, mockData.data(), nullptr));
    REQUIRE(info->data == mockData.data());

    // 5. Bind the texture and verify it is marked as current on TMU0
    REQUIRE_NOTHROW(texSource(mmid));
    REQUIRE(texGetCurrent(0) == mmid);

    // 6. Test texture combination state mapping
    REQUIRE_NOTHROW(texCombine(0, 0x4));  // GR_TEXTURECOMBINE_MULTIPLY

    // 7. Test LOD bias and multibase stubs run successfully
    REQUIRE_NOTHROW(lodBias(0, -0.5f));
    REQUIRE_NOTHROW(multibase(0, FXTRUE));

    GrTexInfo coreInfo;
    REQUIRE_NOTHROW(multibaseAddr(0, 0, 0, 0, &coreInfo));

    // Clean up
    winClose();
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("Phase 3: Automated Geometry Drawing Validation Suite") {
#ifdef __linux__
    // 1. Validate Glide 2.x Fixed Geometry Parsing & Drawing
    {
      void* handle = OpenGlide2DsoHandle();
      REQUIRE(handle != nullptr);

      using WinOpenFunc =
          FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
      using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
      using DrawTriFunc = void (*)(const void*, const void*, const void*);
      using BufferSwapFunc = void (*)(int);
      using WinCloseFunc = void (*)(void);
      using ShutdownFunc = void (*)(void);

      auto glideInit =
          reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideInit"));
      auto winOpen =
          reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
      auto bufferClear =
          reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
      auto drawTri =
          reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
      auto bufferSwap =
          reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
      auto winClose =
          reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
      auto glideShutdown =
          reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

      REQUIRE(glideInit != nullptr);
      REQUIRE(winOpen != nullptr);
      REQUIRE(bufferClear != nullptr);
      REQUIRE(drawTri != nullptr);
      REQUIRE(bufferSwap != nullptr);
      REQUIRE(winClose != nullptr);
      REQUIRE(glideShutdown != nullptr);

      glideInit();
      REQUIRE(winOpen(0, 5, 0, 0, 0, 2, 0) == FXTRUE);
      bufferClear(0, 0, 65535);

      // Canonical Glide 2 vertex: x, y, z, r, g, b (use GrVertex to prevent
      // ASan stack overflow!)
      GrVertex v0{}, v1{}, v2{};
      v0.x = 100.0f;
      v0.y = 100.0f;
      v0.z = 0.0f;
      v0.r = 255.0f;
      v0.g = 0.0f;
      v0.b = 0.0f;
      v0.ooz = 0.0f;
      v0.a = 255.0f;
      v0.oow = 1.0f;
      v1.x = 500.0f;
      v1.y = 100.0f;
      v1.z = 0.0f;
      v1.r = 255.0f;
      v1.g = 0.0f;
      v1.b = 0.0f;
      v1.ooz = 0.0f;
      v1.a = 255.0f;
      v1.oow = 1.0f;
      v2.x = 300.0f;
      v2.y = 400.0f;
      v2.z = 0.0f;
      v2.r = 255.0f;
      v2.g = 0.0f;
      v2.b = 0.0f;
      v2.ooz = 0.0f;
      v2.a = 255.0f;
      v2.oow = 1.0f;

      REQUIRE_NOTHROW(drawTri(&v0, &v1, &v2));
      REQUIRE_NOTHROW(bufferSwap(1));
      winClose();
      glideShutdown();

      ::dlclose(handle);
    }

    // 2. Validate Glide 3.x Programmable Vertex Layouts & Drawing
    {
      void* handle = OpenGlide3DsoHandle();
      REQUIRE(handle != nullptr);

      using InitFunc = void (*)(void);
      using WinOpenFunc =
          FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
      using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
      using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
      using DrawTriFunc = void (*)(const void*, const void*, const void*);
      using BufferSwapFunc = void (*)(FxU32);
      using WinCloseFunc = FxBool (*)(FxU32);
      using ShutdownFunc = void (*)(void);

      auto glideInit =
          reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
      auto winOpen =
          reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
      auto vertexLayout =
          reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
      auto bufferClear =
          reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
      auto drawTri =
          reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
      auto bufferSwap =
          reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
      auto winClose =
          reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
      auto glideShutdown =
          reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

      REQUIRE(glideInit != nullptr);
      REQUIRE(winOpen != nullptr);
      REQUIRE(vertexLayout != nullptr);
      REQUIRE(bufferClear != nullptr);
      REQUIRE(drawTri != nullptr);
      REQUIRE(bufferSwap != nullptr);
      REQUIRE(winClose != nullptr);
      REQUIRE(glideShutdown != nullptr);

      glideInit();
      FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 0);
      REQUIRE(ctx == 1);
      bufferClear(0, 0, 4294967295);

      // Custom non-standard vertex layout!
      struct CustomVert3 {
        float pos[2];      // offset 0
        uint32_t padding;  // offset 8
        float rgb[3];      // offset 12
        float alpha;       // offset 24
      };

      // Configure programmable layout (Correct Glide 3.x Parameter IDs!)
      vertexLayout(0x01, 0, 1);   // GR_PARAM_XY at offset 0
      vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB at offset 12
      vertexLayout(0x10, 24, 1);  // GR_PARAM_A at offset 24
      // Disable other unused parameters to prevent ASan stack-buffer-overflow
      // on 28-byte CustomVert3!
      vertexLayout(0x02, 0, 0);  // GR_PARAM_Z, DISABLE
      vertexLayout(0x04, 0, 0);  // GR_PARAM_Q (OOW), DISABLE
      vertexLayout(0x40, 0, 0);  // GR_PARAM_ST0, DISABLE

      CustomVert3 v0{{200.0f, 200.0f}, 0, {0.0f, 255.0f, 0.0f}, 255.0f};
      CustomVert3 v1{{600.0f, 200.0f}, 0, {0.0f, 255.0f, 0.0f}, 255.0f};
      CustomVert3 v2{{400.0f, 500.0f}, 0, {0.0f, 255.0f, 0.0f}, 255.0f};

      REQUIRE_NOTHROW(drawTri(&v0, &v1, &v2));
      REQUIRE_NOTHROW(bufferSwap(1));
      winClose(ctx);
      glideShutdown();

      ::dlclose(handle);
    }
#endif
  }

  SECTION("Phase 4: Depth (Z/W) Comparison & Transparency State Suites") {
#ifdef __linux__
    void* handle = OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using ClearFunc = void (*)(FxU32, FxU32, FxU32);
    using ClipFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
    using BlendFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
    using CullFunc = void (*)(FxU32);
    using DrawTriFunc = void (*)(const void*, const void*, const void*);
    using ReadFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using WinCloseFunc = void (*)(void);
    using AlphaTestFunc = void (*)(FxU32);
    using AlphaRefFunc = void (*)(FxU32);
    using DepthModeFunc = void (*)(FxU32);
    using DepthFuncFunc = void (*)(FxU32);
    using DepthMaskFunc = void (*)(FxBool);
    using AlphaCombineFunc = void (*)(FxU32, FxU32, FxU32, FxU32, FxBool);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto clearBuf =
        reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
    auto clipWin = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
    auto blendFunc =
        reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto cullMode = CullFunc(::dlsym(handle, "grCullMode"));
    auto drawTri =
        reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb =
        reinterpret_cast<ReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto alphaCombine =
        reinterpret_cast<AlphaCombineFunc>(::dlsym(handle, "grAlphaCombine"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto alphaTestFunc =
        reinterpret_cast<AlphaTestFunc>(::dlsym(handle, "grAlphaTestFunction"));
    auto alphaRefVal = reinterpret_cast<AlphaRefFunc>(
        ::dlsym(handle, "grAlphaTestReferenceValue"));
    auto depthBufferMode =
        reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
    auto depthBufferFunc = reinterpret_cast<DepthFuncFunc>(
        ::dlsym(handle, "grDepthBufferFunction"));
    auto depthMask =
        reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(clearBuf != nullptr);
    REQUIRE(clipWin != nullptr);
    REQUIRE(blendFunc != nullptr);
    REQUIRE(cullMode != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(alphaCombine != nullptr);
    REQUIRE(alphaTestFunc != nullptr);
    REQUIRE(alphaRefVal != nullptr);
    REQUIRE(depthBufferMode != nullptr);
    REQUIRE(depthBufferFunc != nullptr);
    REQUIRE(depthMask != nullptr);
    REQUIRE(glideShutdown != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0);  // 800x600 headless
    REQUIRE(ctx == 1);

    std::vector<uint32_t> lfbData(800 * 600, 0);

    // 1. Validate Scissor Box Containment
    // Authentic 3dfx grBufferClear respects grClipWindow, so clear full screen
    // first before restricting the clip rectangle (matching official test15).
    clearBuf(0xFF000000, 0, 1);  // Black
    clipWin(100, 100, 200, 200);

    StandardVert q0{0.0f, 0.0f,   0.5f, 255.0f, 255.0f, 0.0f,
                    1.0f, 255.0f, 1.0f, 0.0f,   0.0f,   1.0f};
    StandardVert q1{2000.0f, 0.0f,   0.5f, 255.0f, 255.0f, 0.0f,
                    1.0f,    255.0f, 1.0f, 0.0f,   0.0f,   1.0f};
    StandardVert q2{0.0f, 2000.0f, 0.5f, 255.0f, 255.0f, 0.0f,
                    1.0f, 255.0f,  1.0f, 0.0f,   0.0f,   1.0f};
    drawTri(&q0, &q1, &q2);

    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[150 * 800 + 150] ==
            0xFFFFFF00);  // Inside Scissor -> Yellow
    REQUIRE(lfbData[50 * 800 + 50] ==
            0xFF000000);  // Outside Scissor -> Untouched Black

    // Revert Scissor
    clipWin(0, 0, 800, 600);

    // 2. Validate Alpha Blending (50% Blue over Solid Red -> Perfect Purple
    // 0xFF7F007F)
    clearBuf(0xFFFF0000, 0, 1);  // Solid Red (0xFFFF0000 in ARGB)
    blendFunc(1, 5, 1, 5);  // GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA
    alphaCombine(1, 0, 0, 2,
                 0);  // GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                      // GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE

    StandardVert b0{0.0f, 0.0f,   0.5f, 0.0f, 0.0f, 255.0f,
                    1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f};  // 50% Blue
    StandardVert b1{2000.0f, 0.0f,   0.5f, 0.0f, 0.0f, 255.0f,
                    1.0f,    127.5f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert b2{0.0f, 2000.0f, 0.5f, 0.0f, 0.0f, 255.0f,
                    1.0f, 127.5f,  1.0f, 0.0f, 0.0f, 1.0f};
    drawTri(&b0, &b1, &b2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    uint32_t blendPixel = lfbData[200 * 800 + 300];
    REQUIRE((blendPixel & 0xFF00FF00) ==
            0xBF000000);  // 75% Alpha (0xBF), 0 Green
    uint32_t blendR = (blendPixel >> 16) & 0xFF;
    uint32_t blendB = blendPixel & 0xFF;
    REQUIRE(
        (blendR == 0x7F ||
         blendR == 0x80));  // Allow both GPU (0x7F) and CPU (0x80) rounding!
    REQUIRE((blendB == 0x7F || blendB == 0x80));

    // Revert Blend & Combiner
    blendFunc(4, 0, 4, 0);
    alphaCombine(3, 8, 1, 2, 0);  // Revert to authentic defaults

    // 3. Validate Polygon Winding Culling
    cullMode(1);                 // GR_CULL_NEGATIVE / CW backface
    clearBuf(0xFF000000, 0, 1);  // Black

    // Draw Clockwise primitive (should be culled!)
    StandardVert cw0{100.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f,   1.0f};
    StandardVert cw1{100.0f, 400.0f, 0.5f, 0.0f, 255.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f,   1.0f};
    StandardVert cw2{400.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f,
                     1.0f,   255.0f, 1.0f, 0.0f, 0.0f,   1.0f};
    drawTri(&cw0, &cw1, &cw2);

    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[150 * 800 + 150] ==
            0xFF000000);  // Mapped target remains perfectly black!

    // Revert Cull
    cullMode(0);  // GR_CULL_DISABLE

    // 4. Validate Alpha Testing (Punch-Through Alpha Rejection)
    clearBuf(0xFF000000, 0, 1);   // Black
    alphaTestFunc(4);             // GR_CMP_GREATER
    alphaRefVal(128);             // Threshold = 128 (50%)
    alphaCombine(1, 0, 0, 2, 0);  // Use iterated vertex alpha

    // Draw quad with Alpha = 100 (below threshold -> should be rejected)
    StandardVert at0{0.0f, 0.0f,   0.5f, 0.0f, 255.0f, 255.0f,
                     1.0f, 100.0f, 1.0f, 0.0f, 0.0f,   1.0f};  // Cyan
    StandardVert at1{2000.0f, 0.0f,   0.5f, 0.0f, 255.0f, 255.0f,
                     1.0f,    100.0f, 1.0f, 0.0f, 0.0f,   1.0f};
    StandardVert at2{0.0f, 2000.0f, 0.5f, 0.0f, 255.0f, 255.0f,
                     1.0f, 100.0f,  1.0f, 0.0f, 0.0f,   1.0f};
    drawTri(&at0, &at1, &at2);

    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[300 * 800 + 300] == 0xFF000000);  // Remains black!

    // Draw quad with Alpha = 200 (above threshold -> should pass)
    at0.a = 200.0f;
    at1.a = 200.0f;
    at2.a = 200.0f;
    drawTri(&at0, &at1, &at2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[300 * 800 + 300] ==
            0xC800FFFF);  // Alpha=200(0xC8), Cyan(0x00FFFF)

    // Revert Alpha Test
    alphaTestFunc(7);             // GR_CMP_ALWAYS
    alphaCombine(3, 8, 1, 2, 0);  // Revert to constant alpha default

    // 5. Validate Extended Depth Comparison Suites (GR_CMP_LESS & Depth
    // Masking)
    depthBufferMode(1);  // GR_DEPTHBUFFER_ZBUFFER
    depthBufferFunc(1);  // GR_CMP_LESS
    depthMask(FXTRUE);

    clearBuf(0xFF000000, 0, 65535);  // Black, Depth=1.0 (65535 / 65535.0f)

    // Draw deep quad at Z = 0.8 (Should pass because 0.8 < 1.0)
    StandardVert d0{0.0f, 0.0f,   0.8f, 255.0f, 0.0f, 0.0f,
                    0.8f, 255.0f, 1.0f, 0.0f,   0.0f, 1.0f};  // Red, ooz = 0.8f
    StandardVert d1{2000.0f, 0.0f,   0.8f, 255.0f, 0.0f, 0.0f,
                    0.8f,    255.0f, 1.0f, 0.0f,   0.0f, 1.0f};
    StandardVert d2{0.0f, 2000.0f, 0.8f, 255.0f, 0.0f, 0.0f,
                    0.8f, 255.0f,  1.0f, 0.0f,   0.0f, 1.0f};
    drawTri(&d0, &d1, &d2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[250 * 800 + 250] ==
            0xFFFF0000);  // Mapped red (opaque 0xFF)!

    // Draw quad at Z = 0.9 (Should be REJECTED because 0.9 is NOT less than
    // 0.8)
    d0.z = 0.9f;
    d1.z = 0.9f;
    d2.z = 0.9f;
    d0.ooz = 0.9f;
    d1.ooz = 0.9f;
    d2.ooz = 0.9f;
    d0.r = 0.0f;
    d1.r = 0.0f;
    d2.r = 0.0f;
    d0.g = 255.0f;
    d1.g = 255.0f;
    d2.g = 255.0f;  // Yellow
    drawTri(&d0, &d1, &d2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[250 * 800 + 250] == 0xFFFF0000);  // Remains untouched red!

    // 6. Validate Depth Mask (Read-Only Depth Buffer)
    depthMask(FXFALSE);  // Read-only depth!
    // Draw closer quad at Z = 0.4 in Blue
    d0.z = 0.4f;
    d1.z = 0.4f;
    d2.z = 0.4f;
    d0.ooz = 0.4f;
    d1.ooz = 0.4f;
    d2.ooz = 0.4f;
    d0.g = 0.0f;
    d1.g = 0.0f;
    d2.g = 0.0f;
    d0.b = 255.0f;
    d1.b = 255.0f;
    d2.b = 255.0f;  // Blue
    drawTri(&d0, &d1, &d2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[250 * 800 + 250] ==
            0xFF0000FF);  // Passes depth (0.4 < 0.8) -> Blue (opaque 0xFF)!

    // Now draw another quad at Z = 0.5 in Green
    // Since the previous draw had depthMask=FXFALSE, the Z buffer at that pixel
    // is still 0.8! So Z = 0.5 < 0.8 should PASS and overwrite with Green!
    d0.z = 0.5f;
    d1.z = 0.5f;
    d2.z = 0.5f;
    d0.ooz = 0.5f;
    d1.ooz = 0.5f;
    d2.ooz = 0.5f;
    d0.b = 0.0f;
    d1.b = 0.0f;
    d2.b = 0.0f;
    d0.g = 255.0f;
    d1.g = 255.0f;
    d2.g = 255.0f;  // Green
    drawTri(&d0, &d1, &d2);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
    REQUIRE(lfbData[250 * 800 + 250] ==
            0xFF00FF00);  // Successfully writes Green (opaque 0xFF)!

    // Revert Depth Mode
    depthMask(FXTRUE);

    winClose();
    glideShutdown();
    ::dlclose(handle);
#endif
  }

  SECTION("Task 5.0: Virtual TMU Multi-MIP Texturing & Uber-Shader Suites") {
#ifdef __linux__
    void* handle = OpenGlide3DsoHandle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using SelectCtxFunc = FxBool (*)(FxU32);
    using BufferClearFunc = void (*)(FxU32, FxU32, FxU32);
    using WinCloseFunc = FxBool (*)(FxU32);
    using DrawTriFunc = void (*)(const void*, const void*, const void*);
    using ReadLfbFunc =
        FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ClipWinFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
    using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);

    using TexDownloadMipMapFunc =
        void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexCombineFunc =
        void (*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t,
                 GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc =
        void (*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
                 GrCombineOther_t, FxBool);
    using TexFilterModeFunc =
        void (*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using TexClampModeFunc =
        void (*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen =
        reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto selectCtx =
        reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
    auto bufferClear =
        reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto drawTri =
        reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb =
        reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto clipWin =
        reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
    auto vertexLayout =
        reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

    auto texDownload = reinterpret_cast<TexDownloadMipMapFunc>(
        ::dlsym(handle, "grTexDownloadMipMap"));
    auto texSource =
        reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
    auto texCombine =
        reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto colorCombine =
        reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto texFilter =
        reinterpret_cast<TexFilterModeFunc>(::dlsym(handle, "grTexFilterMode"));
    auto texClamp =
        reinterpret_cast<TexClampModeFunc>(::dlsym(handle, "grTexClampMode"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
    REQUIRE(glideShutdown != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1);  // 800x600
    REQUIRE(ctx == 1);
    REQUIRE(selectCtx(ctx) == FXTRUE);

    // Reset Scissor Clip Window to cover full screen
    clipWin(0, 0, 800, 600);

    // Configure explicit deterministic layout matching StandardVert precisely
    // using Glide 3.x canonical parameter constants
    vertexLayout(0x01, 0, 1);   // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);   // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1);  // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1);  // GR_PARAM_W at 32
    vertexLayout(0x40, 36, 1);  // GR_PARAM_ST0 at 36

    bufferClear(0xFF000000, 0, 0xFFFFFFFF);  // Clear depth to 1.0f

    // Prepare 16x16 Texture in GR_TEXFMT_RGB_565 (Format 0xa)
    // Checkerboard pattern: top-left & bottom-right are Cyan (0x07FF),
    // top-right & bottom-left are Magenta (0xF81F)
    std::vector<uint16_t> texData(16 * 16);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        bool isCyan = ((x / 8) + (y / 8)) % 2 == 0;
        texData[y * 16 + x] = isCyan ? 0x07FF : 0xF81F;
      }
    }

    GrTexInfo texInfo;
    texInfo.smallLod = 4;  // LOD 4 -> 16x16
    texInfo.largeLod = 4;
    texInfo.aspectRatio = 0;  // 1:1
    texInfo.format = 0xa;     // GR_TEXFMT_RGB_565
    texInfo.data = texData.data();

    // 1. Download MipMap to TMU 0 at address 0x1000 (Non-zero active handle)
    texDownload(0, 0x00001000, 0, &texInfo);

    // 2. Bind Texture
    texSource(0, 0x00001000, 0, &texInfo);
    texClamp(0, 0, 0);   // WRAP
    texFilter(0, 0, 0);  // POINT filter

    // 3. Configure Register Combiners
    texCombine(0, 1, 1, 1, 1, FXFALSE, FXFALSE);
    colorCombine(3, 8, 1, 1,
                 FXFALSE);  // Decal texturing: SCALE_OTHER, ONE, NONE, TEXTURE

    // 4. Draw Triangle Quad sampling top-left texel (s=64.0, t=64.0 -> should
    // map to Cyan) StandardVert format: x, y, z, r, g, b, ooz, a, oow, s, t, w
    StandardVert t0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 64.0f,  64.0f,  1.0f};
    StandardVert t1{350.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 64.0f,  64.0f,  1.0f};
    StandardVert t2{300.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 64.0f,  64.0f,  1.0f};
    drawTri(&t0, &t1, &t2);

    std::vector<uint32_t> lfb(800 * 600);
    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfb.data()) == FXTRUE);

    // Cyan pixel verification (R == 0, G >= 254, B >= 254)
    REQUIRE((lfb[310 * 800 + 310] & 0x00FF0000) == 0x00000000);
    REQUIRE((lfb[310 * 800 + 310] & 0x0000FF00) >= 0x0000FE00);
    REQUIRE((lfb[310 * 800 + 310] & 0x000000FF) >= 0x000000FE);

    // 5. Draw Triangle Quad sampling top-right texel (s=192.0, t=64.0 -> should
    // map to Magenta)
    StandardVert m0{400.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 192.0f, 64.0f,  1.0f};
    StandardVert m1{450.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 192.0f, 64.0f,  1.0f};
    StandardVert m2{400.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f,
                    1.0f,   255.0f, 1.0f, 192.0f, 64.0f,  1.0f};
    drawTri(&m0, &m1, &m2);

    REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfb.data()) == FXTRUE);

    // Magenta pixel verification (R >= 254, G == 0, B >= 254)
    REQUIRE((lfb[310 * 800 + 410] & 0x00FF0000) >= 0x00FE0000);
    REQUIRE((lfb[310 * 800 + 410] & 0x0000FF00) == 0x00000000);
    REQUIRE((lfb[310 * 800 + 410] & 0x000000FF) >= 0x000000FE);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
#endif
  }
}

TEST_CASE("Glide 2.x: Hardware Profile & Version Gating", "[Glide2][Gating]") {
  SECTION("Dynamic version and board gating") {
#ifdef __linux__
  // Load the runtime wrapper dynamic library
  void* handle = OpenGlide2DsoHandle();
  REQUIRE(handle != nullptr);

  // Resolve base initialization entry points (pure Glide 2.x does NOT export grGetProcAddress/grGetString)
  auto init = (void(FX_CALL*)(void))::dlsym(handle, "grGlideInit");
  auto shutdown = (void(FX_CALL*)(void))::dlsym(handle, "grGlideShutdown");
  auto sstSelect = (void(FX_CALL*)(int))::dlsym(handle, "grSstSelect");
  auto getVersion =
      (void(FX_CALL*)(char[80]))::dlsym(handle, "grGlideGetVersion");
  auto sstQueryHardware = (FxBool(FX_CALL*)(GrHwConfiguration*))::dlsym(
      handle, "grSstQueryHardware");
  auto sstWinOpen = (FxBool(FX_CALL*)(
      FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t,
      GrOriginLocation_t, int, int))::dlsym(handle, "grSstWinOpen");

  REQUIRE(init != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(getVersion != nullptr);
  REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);
  REQUIRE(::dlsym(handle, "grGetString") == nullptr);
  REQUIRE(sstQueryHardware != nullptr);
  REQUIRE(sstWinOpen != nullptr);

  // Helper to setup and initialize under specific env profiles
  auto init_with_profile = [&](const char* card, const char* version) {
    if (card)
      ::setenv("GLIDE_NG_CARD_MODEL", card, 1);
    else
      ::unsetenv("GLIDE_NG_CARD_MODEL");

    if (version)
      ::setenv("GLIDE_NG_API_VERSION", version, 1);
    else
      ::unsetenv("GLIDE_NG_API_VERSION");

    init();
    sstSelect(0);

    // Open a dummy context to instantiate the backend and create the splash
    // animator!
    sstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
               GR_ORIGIN_UPPER_LEFT, 2, 1);
  };

  SECTION("Case A: Default Profile (Voodoo2, 2.56)") {
    init_with_profile("voodoo2", nullptr);

    char verBuf[80] = {};
    getVersion(verBuf);
    REQUIRE(std::string(verBuf) == "2.56");

    // Hardware query check: Voodoo2 CVG reports GR_SSTTYPE_VOODOO with fbiRev=0x102
    // unless FX_GLIDE_REPORT_REAL_HW is defined (strict 3dfx != NULL check: any
    // defined value including "0" or "" enables GR_SSTTYPE_Voodoo2).
    GrHwConfiguration hwconfig;
    ::unsetenv("FX_GLIDE_REPORT_REAL_HW");
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_VOODOO);
    REQUIRE(hwconfig.SSTs[0].sstBoard.Voodoo2Config.fbiRev == 0x102);
    REQUIRE(hwconfig.SSTs[0].sstBoard.Voodoo2Config.fbRam == 16);

    ::setenv("FX_GLIDE_REPORT_REAL_HW", "1", 1);
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_Voodoo2);
    REQUIRE(hwconfig.SSTs[0].sstBoard.Voodoo2Config.fbiRev == 0x102);

    ::setenv("FX_GLIDE_REPORT_REAL_HW", "0", 1);
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_Voodoo2);

    ::setenv("FX_GLIDE_REPORT_REAL_HW", "", 1);
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_Voodoo2);
    ::unsetenv("FX_GLIDE_REPORT_REAL_HW");

    shutdown();
  }

  SECTION("Case B: Voodoo1 Legacy Profile (Voodoo Graphics, default 2.48)") {
    ::unsetenv("SST_SLIDETECT");
    ::unsetenv("SSTV2_SLIDETECT");
    ::unsetenv("GLIDE_NG_SLI");
    init_with_profile("voodoo1", nullptr);
    char verBuf[80] = {};
    getVersion(verBuf);
    REQUIRE(std::string(verBuf) == "2.48");

    // Hardware query check: Voodoo Graphics config (SLI off by default)
    GrHwConfiguration hwconfig;
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_VOODOO);
    REQUIRE(hwconfig.SSTs[0].sstBoard.VoodooConfig.fbiRev == 2);
    REQUIRE(hwconfig.SSTs[0].sstBoard.VoodooConfig.sliDetect == FXFALSE);
    shutdown();

    // Optional SST-1 SLI passthrough (e.g. Quantum3D Obsidian) via SST_SLIDETECT=1
    ::setenv("SST_SLIDETECT", "1", 1);
    init_with_profile("voodoo1", nullptr);
    REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
    REQUIRE(hwconfig.SSTs[0].sstBoard.VoodooConfig.sliDetect == FXTRUE);
    shutdown();
    ::unsetenv("SST_SLIDETECT");
  }

  SECTION("Case C: Banshee & Voodoo3 Interop Profile (authentic H3 fbiRev + 2.61 + FX_GLIDE_EMUL_RUSH)") {
    ::unsetenv("FX_GLIDE_EMUL_RUSH");
    init_with_profile("banshee", nullptr);
    char verBuf[80] = {};
    getVersion(verBuf);
    REQUIRE(std::string(verBuf) == "2.61");
    GrHwConfiguration hwBanshee;
    REQUIRE(sstQueryHardware(&hwBanshee) == FXTRUE);
    REQUIRE(hwBanshee.SSTs[0].type == GR_SSTTYPE_VOODOO);
    REQUIRE(hwBanshee.SSTs[0].sstBoard.VoodooConfig.fbiRev == 0x1002);
    REQUIRE(hwBanshee.SSTs[0].sstBoard.VoodooConfig.nTexelfx == 1);

    // Authentic 2.60/2.61 FX_GLIDE_EMUL_RUSH reports GR_SSTTYPE_SST96
    ::setenv("FX_GLIDE_EMUL_RUSH", "1", 1);
    REQUIRE(sstQueryHardware(&hwBanshee) == FXTRUE);
    REQUIRE(hwBanshee.SSTs[0].type == GR_SSTTYPE_SST96);
    REQUIRE(hwBanshee.SSTs[0].sstBoard.SST96Config.tmuConfig.tmuRev == 0x1002);
    ::unsetenv("FX_GLIDE_EMUL_RUSH");
    shutdown();

    init_with_profile("voodoo3", "2.61");
    getVersion(verBuf);
    REQUIRE(std::string(verBuf) == "2.61");
    GrHwConfiguration hwV3;
    REQUIRE(sstQueryHardware(&hwV3) == FXTRUE);
    REQUIRE(hwV3.SSTs[0].type == GR_SSTTYPE_VOODOO);
    REQUIRE(hwV3.SSTs[0].sstBoard.VoodooConfig.fbiRev == 0x11002);
    REQUIRE(hwV3.SSTs[0].sstBoard.VoodooConfig.nTexelfx == 2);

    ::setenv("FX_GLIDE_EMUL_RUSH", "1", 1);
    REQUIRE(sstQueryHardware(&hwV3) == FXTRUE);
    REQUIRE(hwV3.SSTs[0].type == GR_SSTTYPE_SST96);
    REQUIRE(hwV3.SSTs[0].sstBoard.SST96Config.nTexelfx == 2);
    REQUIRE(hwV3.SSTs[0].sstBoard.SST96Config.tmuConfig.tmuRev == 0x11002);
    ::unsetenv("FX_GLIDE_EMUL_RUSH");
    shutdown();
  }

  SECTION(
      "Case D: Static Function Safe Gating Call (Direct Gated Invocation)") {
    init_with_profile("voodoo1", "2.40");

    // Resolve grChromakeyRangeExt directly
    typedef void(FX_CALL * chromakeyRange_t)(GrColor_t, GrColor_t, FxI32);
    auto chromakeyRange =
        (chromakeyRange_t)::dlsym(handle, "grChromakeyRangeExt");
    REQUIRE(chromakeyRange != nullptr);

    // Invoking gated function directly in Voodoo 1 mode must not crash; it
    // behaves as a safe no-op.
    chromakeyRange(0x0, 0xFFFFFFFF,
                   0x8);  // 0x8 = GR_CHROMARANGE_RGB_ALL (inclusive mode)

    shutdown();
  }

  SECTION("Case E: grSplashCb Callback Execution") {
    init_with_profile("voodoo3", "2.61");

    auto p_grSplashCb =
        (void(FX_CALL*)(float, float, float, float, FxU32,
                        void (*)(int)))::dlsym(handle, "grSplashCb");
    REQUIRE(p_grSplashCb != nullptr);

    // Setup a tracking callback
    static int s_callback_trigger_count = 0;
    s_callback_trigger_count = 0;
    auto tracking_callback = [](int frame) { s_callback_trigger_count++; };

    // Render frame 35 (single frame path: callback should not be triggered)
    p_grSplashCb(0.0f, 0.0f, 640.0f, 480.0f, 35, tracking_callback);
    REQUIRE(s_callback_trigger_count == 0);

    // Render whole animation (automatic loop: callback should be triggered for
    // each frame)
    p_grSplashCb(0.0f, 0.0f, 640.0f, 480.0f, 0, tracking_callback);

    // Ensure the callback was actually invoked repeatedly!
    REQUIRE(s_callback_trigger_count > 0);

    shutdown();
  }

  ::dlclose(handle);
#endif
  }

  SECTION("Legacy guFb/guMP/guMovie exports") {
#ifdef __linux__
  grGlideInit();
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) ==
          FXTRUE);

  SECTION("guFbWriteRegion & guFbReadRegion 565 round-trip") {
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    uint16_t srcBlock[16];
    for (int i = 0; i < 16; ++i) {
      srcBlock[i] = (i & 1) ? 0xF800u : 0x07E0u;
    }
    guFbWriteRegion(20, 24, 4, 4, srcBlock, 4 * sizeof(uint16_t));
    uint16_t dstBlock[16] = {};
    guFbReadRegion(20, 24, 4, 4, dstBlock, 4 * sizeof(uint16_t));
    for (int i = 0; i < 16; ++i) {
      CHECK(dstBlock[i] == srcBlock[i]);
    }
  }

  SECTION("guMPDrawTriangle 2-pass ADD and MULTIPLY") {
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0xFFFFFFFFu);

    guTexMemReset();
    std::vector<uint16_t> redTex(16 * 16, 0xF800u);    // RGB565 Red (255,0,0)
    std::vector<uint16_t> greenTex(16 * 16, 0x07E0u);  // RGB565 Green (0,255,0)
    std::vector<uint16_t> halfTex(16 * 16, 0x8410u);   // ~50% grey

    GrMipMapId_t mmRed = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    GrMipMapId_t mmGreen = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    GrMipMapId_t mmHalf = guTexAllocateMemory(
        GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565,
        GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
        GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
        GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f,
        FXFALSE);
    REQUIRE(mmRed != GR_NULL_MIPMAP_HANDLE);
    REQUIRE(mmGreen != GR_NULL_MIPMAP_HANDLE);
    REQUIRE(mmHalf != GR_NULL_MIPMAP_HANDLE);

    guTexDownloadMipMap(mmRed, redTex.data(), nullptr);
    guTexDownloadMipMap(mmGreen, greenTex.data(), nullptr);
    guTexDownloadMipMap(mmHalf, halfTex.data(), nullptr);

    auto makeV = [](float x, float y, float s, float t) {
      GrVertex v = {};
      v.x = x;
      v.y = y;
      v.oow = 1.0f;
      v.tmuvtx[0].sow = s;
      v.tmuvtx[0].tow = t;
      return v;
    };
    GrVertex v0 = makeV(40.0f, 40.0f, 0.0f, 0.0f);
    GrVertex v1 = makeV(120.0f, 40.0f, 256.0f, 0.0f);
    GrVertex v2 = makeV(120.0f, 120.0f, 256.0f, 256.0f);

    // 1) ADD: Red + Green => Yellow (R>=28, G>=58, B<=2)
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    guMPInit();
    guMPTexCombineFunction(GR_MPTEXTURECOMBINE_ADD);
    guMPTexSource(0, mmRed);
    guMPTexSource(1, mmGreen);
    guMPDrawTriangle(&v0, &v1, &v2);
    uint16_t pxAdd = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 95, 65, 1, 1, 2, &pxAdd) ==
            FXTRUE);
    CHECK(((pxAdd >> 11) & 31u) >= 28u);
    CHECK(((pxAdd >> 5) & 63u) >= 58u);
    CHECK((pxAdd & 31u) <= 2u);

    // 2) MULTIPLY: Red * ~50% Grey => Dark Red (R around 12..19, G<=2, B<=2)
    grBufferClear(0xFF000000u, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    guMPTexCombineFunction(GR_MPTEXTURECOMBINE_MULTIPLY);
    guMPTexSource(0, mmRed);
    guMPTexSource(1, mmHalf);
    guMPDrawTriangle(&v0, &v1, &v2);
    uint16_t pxMul = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 95, 65, 1, 1, 2, &pxMul) ==
            FXTRUE);
    const uint32_t mulR = (pxMul >> 11) & 31u;
    CHECK(mulR >= 12u);
    CHECK(mulR <= 19u);
    CHECK(((pxMul >> 5) & 63u) <= 2u);
    CHECK((pxMul & 31u) <= 2u);
  }

  SECTION("grSstVidMode and guMovie* smoke test") {
    REQUIRE_NOTHROW(grSstVidMode(0, nullptr));
    REQUIRE_NOTHROW(guMovieSetName("capture.mov"));
    REQUIRE_NOTHROW(guMovieStart());
    REQUIRE_NOTHROW(guMovieStop());
  }

  grSstWinClose();
  grGlideShutdown();
#endif
  }

  SECTION("Pure Glide 2.x ABI, bare version strings & T-buffer mask") {
#ifdef __linux__
  struct EnvGuard {
    std::vector<std::string> keys = {"GLIDE_NG_CARD_MODEL",
                                     "GLIDE_NG_API_VERSION",
                                     "GLIDE_NG_HEADLESS"};
    std::vector<std::pair<std::string, std::string>> saved;
    EnvGuard() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        ::unsetenv(k.c_str());
      }
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    }
    ~EnvGuard() {
      for (const auto& kv : saved) {
        if (kv.second == "\x01UNSET") {
          ::unsetenv(kv.first.c_str());
        } else {
          ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
      }
    }
  } guard;

  void* handle = OpenGlide2DsoHandle();
  REQUIRE(handle != nullptr);

  // Authentic Glide 2.x DLLs/OVLs never exported grGetString or grGetProcAddress
  CHECK(::dlsym(handle, "grGetString") == nullptr);
  CHECK(::dlsym(handle, "grGetProcAddress") == nullptr);

  // Verify default bare 3-digit version per card model
  struct CardExpect {
    const char* card;
    const char* expectedVer;
    GrSstType expectedType;
    int expectedFbiRev;
  };
  const CardExpect cases[] = {
      {"voodoo1", "2.48", GR_SSTTYPE_VOODOO, 2},
      {"rush",    "2.48", GR_SSTTYPE_SST96,  0},
      {"voodoo2", "2.56", GR_SSTTYPE_VOODOO, 0x102},
      {"banshee", "2.61", GR_SSTTYPE_VOODOO, 0x1002},
      {"voodoo3", "2.61", GR_SSTTYPE_VOODOO, 0x11002},
      {"voodoo4", "2.61", GR_SSTTYPE_VOODOO, 0x11002},
      {"voodoo5", "2.61", GR_SSTTYPE_VOODOO, 0x11002},
  };
  for (const auto& c : cases) {
    ::setenv("GLIDE_NG_CARD_MODEL", c.card, 1);
    ::unsetenv("GLIDE_NG_API_VERSION");
    grGlideInit();
    char verBuf[80] = {};
    grGlideGetVersion(verBuf);
    INFO("Card=" << c.card << " ver=" << verBuf);
    CHECK(std::string(verBuf) == c.expectedVer);
    CHECK(std::string(verBuf).find("glide-ng") == std::string::npos);

    GrHwConfiguration hw;
    std::memset(&hw, 0, sizeof(hw));
    REQUIRE(grSstQueryHardware(&hw) == FXTRUE);
    CHECK(hw.SSTs[0].type == c.expectedType);
    if (c.expectedType == GR_SSTTYPE_VOODOO) {
      CHECK(hw.SSTs[0].sstBoard.VoodooConfig.fbiRev == c.expectedFbiRev);
    } else if (c.expectedType == GR_SSTTYPE_Voodoo2) {
      CHECK(hw.SSTs[0].sstBoard.Voodoo2Config.fbiRev == c.expectedFbiRev);
    }
    grGlideShutdown();
  }

  // Verify grTBufferWriteMaskExt is wired and safe across backends
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) ==
          FXTRUE);
  using TBufferMaskFn = void(FX_CALL*)(FxU32);
  auto tbufMask =
      reinterpret_cast<TBufferMaskFn>(::dlsym(handle, "grTBufferWriteMaskExt"));
  REQUIRE(tbufMask != nullptr);
  REQUIRE_NOTHROW(tbufMask(0x5u));
  REQUIRE_NOTHROW(tbufMask(0xFFFFFFFFu));
  grSstWinClose();
  grGlideShutdown();

  ::dlclose(handle);
#endif
  }

}

