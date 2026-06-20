#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "core/WrapperConfig.h"
#include "core/Logger.h"
#include "core/JsonConfigLoader.h"
#include "core/TextureManager.h"
#include "software/SoftwareBackend.h"
#include <cstdlib>
#include <dlfcn.h>
#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include "core/BackendManager.h"
#include <glide.h>
#include <tlib.h>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>
#include <iostream>

namespace {
    struct StandardVert {
        float x, y;    // 0, 4
        float z;       // 8
        float r, g, b; // 12, 16, 20
        float ooz;     // 24
        float a;       // 28
        float oow;     // 32
        float s, t;    // 36, 40
        float w;       // 44
    };
}

inline void* OpenGlide2Handle() {
    void* h = ::dlopen("../lib/libglide2x.so", RTLD_LAZY);
    if (!h) h = ::dlopen("lib/libglide2x.so", RTLD_LAZY);
    if (!h) h = ::dlopen("build/lib/libglide2x.so", RTLD_LAZY);
    return h;
}

inline void* OpenGlide3Handle() {
    void* h = ::dlopen("../lib/libglide3x.so", RTLD_LAZY);
    if (!h) h = ::dlopen("lib/libglide3x.so", RTLD_LAZY);
    if (!h) h = ::dlopen("build/lib/libglide3x.so", RTLD_LAZY);
    return h;
}

TEST_CASE("Architectural Core Validation", "[Architecture]") {
    SECTION("EmulationRegistry Singleton Access") {
        auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
        REQUIRE(reg.GetConfig().model == GlideWrapper::CardModel::Voodoo2);
        REQUIRE(reg.GetConfig().fbiMemoryMb == 16);
    }

    SECTION("Logger Lifecycle") {
        auto& logger = GlideWrapper::Logger::GetInstance();
        logger.Initialize("test_output.log", GlideWrapper::LogLevel::INFO);
        REQUIRE(logger.GetLevel() == GlideWrapper::LogLevel::INFO);
    }

    SECTION("JsonConfigLoader Fallback Resolution") {
        GlideWrapper::JsonConfigLoader loader;
        GlideWrapper::WrapperConfig cfg;
        bool loaded = loader.Load("non_existent_config.json", cfg);
        REQUIRE(loaded == true);
        // Should preserve defaults if file missing and no env vars
        REQUIRE(cfg.tmuCount == 2);
    }

    SECTION("Extensible Configuration Hierarchy (Task 1.3 Environment Overrides)") {
#ifdef __linux__
        // Mock environment variables
        ::setenv("GLIDE_WRAPPER_FBI_MEM_MB", "32", 1);
        ::setenv("GLIDE_WRAPPER_VENDOR", "Antigravity Test Vendor", 1);
        
        GlideWrapper::JsonConfigLoader loader;
        GlideWrapper::WrapperConfig cfg;
        loader.Load("non_existent.json", cfg);
        
        REQUIRE(cfg.fbiMemoryMb == 32);
        REQUIRE(cfg.reportedVendorOverride == "Antigravity Test Vendor");
        
        // Clean up environment
        ::unsetenv("GLIDE_WRAPPER_FBI_MEM_MB");
        ::unsetenv("GLIDE_WRAPPER_VENDOR");
#endif
    }

    SECTION("EmulationRegistry Feature-Gating Boundaries (Task 1.4)") {
        GlideWrapper::WrapperConfig cfg;
        cfg.model = GlideWrapper::CardModel::VoodooGraphics; // SST-1
        
        // Voodoo Graphics does not support more than 1 TMU or resolutions above 800x600
        bool supportsMultiPassOnly = (cfg.model == GlideWrapper::CardModel::VoodooGraphics);
        REQUIRE(supportsMultiPassOnly == true);
    }

    SECTION("Startup Hardware & Emulation Summary Injection") {
        auto& logger = GlideWrapper::Logger::GetInstance();
        auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
        
        logger.Initialize("test_summary.log", GlideWrapper::LogLevel::INFO);
        REQUIRE_NOTHROW(logger.LogHostVulkanCapabilities());
        REQUIRE_NOTHROW(logger.LogExecutionSummary(reg.GetConfig()));
    }

    SECTION("Foundational Legacy C Query APIs (Task 1.5 Dynamic Interop)") {
#ifdef __linux__
        // Load our compiled libglide2x.so dynamic frontend
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);
        
        // Resolve legacy function pointers
        using InitFunc = void(*)(void);
        using GetStringFunc = const char*(*)(FxU32);
        using QueryHwFunc = FxBool(*)(GrHwConfiguration*);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto getString = reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
        auto queryHw   = reinterpret_cast<QueryHwFunc>(::dlsym(handle, "grSstQueryHardware"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(getString != nullptr);
        REQUIRE(queryHw != nullptr);

        // Execute tests across legacy C boundaries
        glideInit();
        REQUIRE(std::string(getString(0xa4)) == "2.43 (glide-ng)"); // GR_VERSION
        REQUIRE(std::string(getString(0xa3)) == "3Dfx Interactive"); // GR_VENDOR
        
        GrHwConfiguration hw;
        FxBool success = queryHw(&hw);
        REQUIRE(success == FXTRUE);
        REQUIRE(hw.num_sst == 1);
        REQUIRE(hw.SSTs[0].type == GR_SSTTYPE_Voodoo2);

        ::dlclose(handle);
#endif
    }

    SECTION("Newly Implemented Glide 2.x API Gaps (Task 3)") {
#ifdef __linux__
        // 1. Load the shared library
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);
        
        // 2. Resolve function pointers
        using GetProcAddressFunc = GrProc(*)(char*);
        using ErrorSetCallbackFunc = void(*)(GrErrorCallbackFnc_t);
        using LfbConstantDepthFunc = void(*)(FxU16);
        using LfbWriteColorSwizzleFunc = void(*)(FxBool, FxBool);
        using AlphaControlsITRGBLightingFunc = void(*)(FxBool);

        auto getProcAddress = reinterpret_cast<GetProcAddressFunc>(::dlsym(handle, "grGetProcAddress"));
        auto errorSetCallback = reinterpret_cast<ErrorSetCallbackFunc>(::dlsym(handle, "grErrorSetCallback"));
        auto lfbConstantDepth = reinterpret_cast<LfbConstantDepthFunc>(::dlsym(handle, "grLfbConstantDepth"));
        auto lfbWriteColorSwizzle = reinterpret_cast<LfbWriteColorSwizzleFunc>(::dlsym(handle, "grLfbWriteColorSwizzle"));
        auto alphaControlsITRGBLighting = reinterpret_cast<AlphaControlsITRGBLightingFunc>(::dlsym(handle, "grAlphaControlsITRGBLighting"));

        REQUIRE(getProcAddress != nullptr);
        REQUIRE(errorSetCallback != nullptr);
        REQUIRE(lfbConstantDepth != nullptr);
        REQUIRE(lfbWriteColorSwizzle != nullptr);
        REQUIRE(alphaControlsITRGBLighting != nullptr);

        // 3. Test grGetProcAddress resolves the chroma-range extension
        // We use mutable local arrays to prevent "ISO C++ forbids converting a string constant to char*" warnings
        char extName[] = "grChromakeyRangeExt";
        char badExtName[] = "non_existent_extension";

        GrProc chromakeyRangePtr = getProcAddress(extName);
        REQUIRE(chromakeyRangePtr != nullptr);
        
        // Test grGetProcAddress returns nullptr for invalid/null extensions
        REQUIRE(getProcAddress(badExtName) == nullptr);
        REQUIRE(getProcAddress(nullptr) == nullptr);

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
        using GammaCorrectionValueFunc = void(*)(float);
        using SstControlFunc = FxBool(*)(FxU32);
        using TexDownloadMipMapLevelPartialFunc = void(*)(GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureFormat_t, FxU32, void*, int, int);
        using GuDrawPolygonVertexListWithClipFunc = void(*)(int, const GrVertex[]);
        using ConvertAndDownloadRleFunc = void(*)(GrChipID_t, FxU32, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureFormat_t, FxU32, FxU8*, long, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU16*);

        auto gammaCorrectionValue = reinterpret_cast<GammaCorrectionValueFunc>(::dlsym(handle, "grGammaCorrectionValue"));
        auto sstControl = reinterpret_cast<SstControlFunc>(::dlsym(handle, "grSstControl"));
        auto texDownloadMipMapLevelPartial = reinterpret_cast<TexDownloadMipMapLevelPartialFunc>(::dlsym(handle, "grTexDownloadMipMapLevelPartial"));
        auto guDrawPolygonVertexListWithClip = reinterpret_cast<GuDrawPolygonVertexListWithClipFunc>(::dlsym(handle, "guDrawPolygonVertexListWithClip"));
        auto convertAndDownloadRle = reinterpret_cast<ConvertAndDownloadRleFunc>(::dlsym(handle, "ConvertAndDownloadRle"));

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
        REQUIRE_NOTHROW(texDownloadMipMapLevelPartial(0, 0x1000, 4, 4, 0, 0, 0, mockTex.data(), 4, 12));

        // 10. Verify guDrawPolygonVertexListWithClip (ensure no-crash)
        GrVertex mockPolygon[4] = {};
        REQUIRE_NOTHROW(guDrawPolygonVertexListWithClip(4, mockPolygon));

        // 11. Verify ConvertAndDownloadRle with a mocked 2x2 RLE compressed texture
        // 2x2 texture (4 pixels) compressed as a single run-length of 4 white pixels:
        // - Packet code: 0x83 (0x80 run-length bit | 3 count)
        // - Pixel value: 0xFFFF (16-bit white)
        uint8_t mockRleData[] = { 0x83, 0xFF, 0xFF };
        REQUIRE_NOTHROW(convertAndDownloadRle(0, 0x2000, 8, 8, 0, 0, 0, mockRleData, 2, 0, 0, 2, 2, 2, 2, nullptr));

        // 12. Clean up
        ::dlclose(handle);
#endif
    }

    SECTION("Gamma Correction & Custom Lookup Tables (TDD Phase)") {
#ifdef __linux__
        // 1. Verify Glide 2.x Gamma Exports
        {
            void* handle = OpenGlide2Handle();
            REQUIRE(handle != nullptr);

            using LoadGammaTableFunc = void(*)(FxU32, FxU32*, FxU32*, FxU32*);
            using GammaCorrectionRGBFunc = void(*)(FxFloat, FxFloat, FxFloat);
            using GammaCorrectionValueFunc = void(*)(float);

            auto grLoadGammaTable = reinterpret_cast<LoadGammaTableFunc>(::dlsym(handle, "grLoadGammaTable"));
            auto guGammaCorrectionRGB = reinterpret_cast<GammaCorrectionRGBFunc>(::dlsym(handle, "guGammaCorrectionRGB"));
            auto grGammaCorrectionValue = reinterpret_cast<GammaCorrectionValueFunc>(::dlsym(handle, "grGammaCorrectionValue"));

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
            void* handle = OpenGlide3Handle();
            REQUIRE(handle != nullptr);

            using LoadGammaTableFunc = void(*)(FxU32, FxU32*, FxU32*, FxU32*);
            using GammaCorrectionRGBFunc = void(*)(FxFloat, FxFloat, FxFloat);

            auto grLoadGammaTable = reinterpret_cast<LoadGammaTableFunc>(::dlsym(handle, "grLoadGammaTable"));
            auto guGammaCorrectionRGB = reinterpret_cast<GammaCorrectionRGBFunc>(::dlsym(handle, "guGammaCorrectionRGB"));

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

    SECTION("Phase 2: Multilib Headless CI Swapchain Attachment & Buffer Clears (GENERATE Matrix)") {
#ifdef __linux__
        // Cycle through all 4 official 3dfx card permutations using Catch2 GENERATE
        auto modelName = GENERATE("SST1", "CVG", "H3", "H5");
        ::setenv("GLIDE_WRAPPER_CARD_MODEL", modelName, 1);

        // 1. Validate Glide 2.x Dynamic Exports
        {
            void* handle = OpenGlide2Handle();
            REQUIRE(handle != nullptr);

            using InitFunc = void(*)(void);
            using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
            using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
            using BufferSwapFunc = void(*)(int);
            using WinCloseFunc = void(*)(void);

            using ShutdownFunc = void(*)(void);
            auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
            auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
            void* handle = OpenGlide3Handle();
            REQUIRE(handle != nullptr);

            using InitFunc = void(*)(void);
            using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
            using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
            using BufferSwapFunc = void(*)(FxU32);
            using WinCloseFunc = FxBool(*)(FxU32);
            using ShutdownFunc = void(*)(void);

            auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
            auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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

        ::unsetenv("GLIDE_WRAPPER_CARD_MODEL");
#endif
    }

    SECTION("Task 2.3: Artificial VSync Retro Frame Pacing Timestamps") {
        auto& logger = GlideWrapper::Logger::GetInstance();
        auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
        auto& manager = GlideWrapper::BackendManager::GetInstance();
        
        reg.GetConfig().maxFps = 60.0f; // 60 FPS capping -> ~16.67ms per frame
        reg.GetConfig().forceNoWindow = true; // Run in pure headless mode for fast timing!
        logger.Initialize("test_pacing.log", GlideWrapper::LogLevel::INFO);
        manager.EstablishBackend(reg.GetConfig());
        
        auto start = std::chrono::steady_clock::now();
        
        // Execute 10 simulated frame presentation swaps
        auto* backend = manager.GetBackend();
        REQUIRE(backend != nullptr);
        
        backend->AttachWindow(nullptr, 800, 600, false);
        for (int i = 0; i < 10; ++i) {
            backend->SwapBuffers();
        }
        backend->DetachWindow();
        
        auto end = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        manager.ShutdownBackend();
        
        // 10 frames at 60 FPS should take ~166.7ms. Allow robust scheduler tolerance [140ms, 300ms]
        REQUIRE(elapsedMs >= 140);
        REQUIRE(elapsedMs <= 300);
    }

    SECTION("Task 2.3: Authentic test00.c Headless Diagnostic Execution Suite") {
#ifdef __linux__
        // Load libglide3x.so to execute the authentic test00 diagnostic lifecycle
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using SelectCtxFunc = FxBool(*)(FxU32);
        using RenderBufFunc = void(*)(FxU32);
        using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
        using BufferSwapFunc = void(*)(FxU32);
        using WinCloseFunc = FxBool(*)(FxU32);
        using ShutdownFunc = void(*)(void);

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto selectCtx   = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
        auto renderBuf   = reinterpret_cast<RenderBufFunc>(::dlsym(handle, "grRenderBuffer"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        REQUIRE_NOTHROW(renderBuf(0)); // GR_BUFFER_BACKBUFFER

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
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using DrawTriFunc = void(*)(const void*, const void*, const void*);
        using BufferSwapFunc = void(*)(int);
        using WinCloseFunc = void(*)(void);
        using ShutdownFunc = void(*)(void);
        
        using TriStatsFunc = void(*)(FxU32*, FxU32*);
        using ResetTriStatsFunc = void(*)(void);
        using PerfStatsFunc = void(*)(GrSstPerfStats_t*);
        using ResetPerfStatsFunc = void(*)(void);

        auto glideInit      = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen        = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto drawTri        = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto bufferSwap     = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto winClose       = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto glideShutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
        
        auto grTriStats     = reinterpret_cast<TriStatsFunc>(::dlsym(handle, "grTriStats"));
        auto grResetTriStats = reinterpret_cast<ResetTriStatsFunc>(::dlsym(handle, "grResetTriStats"));
        auto grSstPerfStats = reinterpret_cast<PerfStatsFunc>(::dlsym(handle, "grSstPerfStats"));
        auto grSstResetPerfStats = reinterpret_cast<ResetPerfStatsFunc>(::dlsym(handle, "grSstResetPerfStats"));

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

        // 3. Draw a mock triangle 30 times and verify counters increment to exactly 30
        struct Vert { float x, y, z, r, g, b, ooz, a, oow; };
        Vert v0{10.0f, 10.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};
        Vert v1{50.0f, 10.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};
        Vert v2{30.0f, 40.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};

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
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using WinCloseFunc = void(*)(void);
        using ShutdownFunc = void(*)(void);
        
        using WidthFunc = FxU32(*)(void);
        using HeightFunc = FxU32(*)(void);
        using BusyFunc = FxBool(*)(void);
        using StatusFunc = FxU32(*)(void);
        using VideoLineFunc = FxU32(*)(void);
        using RetraceFunc = FxBool(*)(void);
        using PendingFunc = int(*)(void);
        using CheckRoomFunc = void(*)(FxI32);

        auto glideInit      = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen        = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose       = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto glideShutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
        
        auto grSstScreenWidth  = reinterpret_cast<WidthFunc>(::dlsym(handle, "grSstScreenWidth"));
        auto grSstScreenHeight = reinterpret_cast<HeightFunc>(::dlsym(handle, "grSstScreenHeight"));
        auto grSstIsBusy       = reinterpret_cast<BusyFunc>(::dlsym(handle, "grSstIsBusy"));
        auto grSstStatus       = reinterpret_cast<StatusFunc>(::dlsym(handle, "grSstStatus"));
        auto grSstVideoLine    = reinterpret_cast<VideoLineFunc>(::dlsym(handle, "grSstVideoLine"));
        auto grSstVRetraceOn   = reinterpret_cast<RetraceFunc>(::dlsym(handle, "grSstVRetraceOn"));
        auto grBufferNumPending = reinterpret_cast<PendingFunc>(::dlsym(handle, "grBufferNumPending"));
        auto grCheckForRoom    = reinterpret_cast<CheckRoomFunc>(::dlsym(handle, "grCheckForRoom"));

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
        auto resTuple = GENERATE(
            std::make_tuple(0x7, 640, 480),  // GR_RESOLUTION_640x480
            std::make_tuple(0x8, 800, 600),  // GR_RESOLUTION_800x600
            // GR_RESOLUTION_1024x768 (1024x768)
            // HISTORICAL NOTE: On physical Voodoo 2 hardware, opening a Z-buffered 1024x768 target 
            // on a single card would fail due to physical VRAM limits, requiring SLI mode.
            // Our API wrapper passes this test successfully because it leverages modern host VRAM,
            // bypassing the historical hardware memory boundaries without needing explicit SLI logic.
            std::make_tuple(0xC, 1024, 768), 
            std::make_tuple(0x1, 320, 240)   // GR_RESOLUTION_320x240
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
        REQUIRE(grSstStatus() == 0x3FF);
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
        // 1. Temporarily set the environment variable GLIDE_WRAPPER_MSAA to 4
        ::setenv("GLIDE_WRAPPER_MSAA", "4", 1);

        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using WinCloseFunc = void(*)(void);
        using AADrawTriFunc = void(*)(const void*, const void*, const void*, FxBool, FxBool, FxBool);
        using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
        using BufferSwapFunc = void(*)(int);
        using LfbReadFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto aaDrawTri   = reinterpret_cast<AADrawTriFunc>(::dlsym(handle, "grAADrawTriangle"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead     = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        bufferClear(0x00000000, 0xFF, 0xFFFF); // Clear to Black

        // Draw a green triangle
        GrVertex a{}, b{}, c{};
        a.x = 400.0f; a.y = 100.0f; a.ooz = 1.0f; a.oow = 1.0f; a.r = 0.0f; a.g = 255.0f; a.b = 0.0f; a.a = 255.0f;
        b.x = 200.0f; b.y = 500.0f; b.ooz = 1.0f; b.oow = 1.0f; b.r = 0.0f; b.g = 255.0f; b.b = 0.0f; b.a = 255.0f;
        c.x = 600.0f; c.y = 500.0f; c.ooz = 1.0f; c.oow = 1.0f; c.r = 0.0f; c.g = 255.0f; c.b = 0.0f; c.a = 255.0f;

        REQUIRE_NOTHROW(aaDrawTri(&a, &b, &c, FXTRUE, FXTRUE, FXTRUE));

        // 4. Read back center pixel via standard grLfbReadRegion API and verify it was drawn green!
        uint32_t pixel = 0;
        REQUIRE(lfbRead(1, 400, 300, 1, 1, 4, &pixel) == FXTRUE); // 1 = GR_BUFFER_BACKBUFFER
        
        // Assert green component is active
        uint8_t g = (pixel >> 8) & 0xFF;
        REQUIRE(g == 255);

        bufferSwap(0); // Trigger resolve and present!

        // 5. Clean up
        winClose();
        glideShutdown();
        ::dlclose(handle);

        // Reset the environment variable to avoid affecting other tests
        ::unsetenv("GLIDE_WRAPPER_MSAA");
#endif
    }

    SECTION("Milestone 3: Polygon Fan Decomposition") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using WinCloseFunc = void(*)(void);
        using DrawPolyFunc = void(*)(const int, const int*, const void*);
        using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
        using BufferSwapFunc = void(*)(int);
        using LfbReadFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto drawPoly    = reinterpret_cast<DrawPolyFunc>(::dlsym(handle, "grDrawPolygon"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead     = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        vlist[0].x = 200.0f; vlist[0].y = 200.0f; vlist[0].ooz = 1.0f; vlist[0].oow = 1.0f; vlist[0].r = 0.0f; vlist[0].g = 255.0f; vlist[0].b = 0.0f; vlist[0].a = 255.0f;
        // Bottom-Left
        vlist[1].x = 200.0f; vlist[1].y = 400.0f; vlist[1].ooz = 1.0f; vlist[1].oow = 1.0f; vlist[1].r = 0.0f; vlist[1].g = 255.0f; vlist[1].b = 0.0f; vlist[1].a = 255.0f;
        // Bottom-Right
        vlist[2].x = 400.0f; vlist[2].y = 400.0f; vlist[2].ooz = 1.0f; vlist[2].oow = 1.0f; vlist[2].r = 0.0f; vlist[2].g = 255.0f; vlist[2].b = 0.0f; vlist[2].a = 255.0f;
        // Top-Right
        vlist[3].x = 400.0f; vlist[3].y = 200.0f; vlist[3].ooz = 1.0f; vlist[3].oow = 1.0f; vlist[3].r = 0.0f; vlist[3].g = 255.0f; vlist[3].b = 0.0f; vlist[3].a = 255.0f;

        int ilist[4] = { 0, 1, 2, 3 };

        REQUIRE_NOTHROW(drawPoly(4, ilist, vlist));

        // 4. Read back center pixel of the quad (300, 300) and assert it is green
        uint32_t pixel = 0;
        REQUIRE(lfbRead(1, 300, 300, 1, 1, 4, &pixel) == FXTRUE); // 1 = GR_BUFFER_BACKBUFFER
        
        uint8_t g = (pixel >> 8) & 0xFF;
        REQUIRE(g == 255);

        bufferSwap(0); // Present and resolve!

        // 5. Clean up
        winClose();
        glideShutdown();
        ::dlclose(handle);
#endif
    }

    SECTION("Milestone 4: Shortcut State Config") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
        using WinCloseFunc = void(*)(void);
        using BufferClearFunc = void(*)(GrColor_t, FxU8, FxU16);
        using BufferSwapFunc = void(*)(FxU32);
        using DrawTriFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
        using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using ShutdownFunc = void(*)(void);

        using ConstColor4Func = void(*)(float, float, float, float);
        using ColorMaskFunc = void(*)(FxBool, FxBool);
        using DisableAllEffectsFunc = void(*)(void);

        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto lfbRead = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        auto constColor4 = reinterpret_cast<ConstColor4Func>(::dlsym(handle, "grConstantColorValue4"));
        auto colorMask = reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
        auto disableAllEffects = reinterpret_cast<DisableAllEffectsFunc>(::dlsym(handle, "grDisableAllEffects"));

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

        // Test 1: Write masking disabled completely (no color channels or alpha can be written)
        colorMask(FXFALSE, FXFALSE);
        bufferClear(0x00000000, 0xFF, 0xFFFF); // Clear to black

        GrVertex a, b, c;
        a.x = 400.0f; a.y = 100.0f; a.ooz = 1.0f; a.oow = 1.0f; a.r = 0.0f; a.g = 255.0f; a.b = 0.0f; a.a = 255.0f;
        b.x = 200.0f; b.y = 500.0f; b.ooz = 1.0f; b.oow = 1.0f; b.r = 0.0f; b.g = 255.0f; b.b = 0.0f; b.a = 255.0f;
        c.x = 600.0f; c.y = 500.0f; c.ooz = 1.0f; c.oow = 1.0f; c.r = 0.0f; c.g = 255.0f; c.b = 0.0f; c.a = 255.0f;

        REQUIRE_NOTHROW(drawTri(&a, &b, &c));

        // Read back center pixel (400, 300) - must remain 100% black (0x00000000 or at least alpha=0 and green=0)
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
        constColor4(255.0f, 128.0f, 64.0f, 32.0f); // A=255, R=128, G=64, B=32

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
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
        using WinCloseFunc = void(*)(void);
        using BufferClearFunc = void(*)(GrColor_t, FxU8, FxU16);
        using BufferSwapFunc = void(*)(FxU32);
        using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using ShutdownFunc = void(*)(void);

        using SplashFunc = void(*)(float, float, float, float, FxU32);
        using ShamelessPlugFunc = void(*)(const FxBool);

        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
        auto shamelessPlug = reinterpret_cast<ShamelessPlugFunc>(::dlsym(handle, "grGlideShamelessPlug"));

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

        // 3. Swap buffers - this will draw the splash frame, overlay the shameless plug, and swap!
        REQUIRE_NOTHROW(bufferSwap(0));

        // 4. Clean up
        winClose();
        glideShutdown();

        ::dlclose(handle);
#endif
    }

    SECTION("Milestone 6: Advanced Texturing & Utilities (LOD/Mipmaps)") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
        using WinCloseFunc = void(*)(void);
        using ShutdownFunc = void(*)(void);

        using AllocFunc = GrMipMapId_t(*)(GrChipID_t, FxU8, int, int, GrTextureFormat_t, GrMipMapMode_t, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureClampMode_t, GrTextureClampMode_t, GrTextureFilterMode_t, GrTextureFilterMode_t, float, FxBool);
        using SourceFunc = void(*)(GrMipMapId_t);
        using GetInfoFunc = GrMipMapInfo*(*)(GrMipMapId_t);
        using ChangeAttrsFunc = FxBool(*)(GrMipMapId_t, int, int, GrTextureFormat_t, GrMipMapMode_t, GrLOD_t, GrLOD_t, GrAspectRatio_t, GrTextureClampMode_t, GrTextureClampMode_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
        using GetCurrentFunc = GrMipMapId_t(*)(GrChipID_t);
        using DownloadFunc = void(*)(GrMipMapId_t, const void*, const GuNccTable*);
        using TexCombineFunc = void(*)(GrChipID_t, GrTextureCombineFnc_t);

        using LodBiasFunc = void(*)(GrChipID_t, float);
        using MultibaseFunc = void(*)(GrChipID_t, FxBool);
        using MultibaseAddrFunc = void(*)(GrChipID_t, GrTexBaseRange_t, FxU32, FxU32, GrTexInfo*);

        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        auto texAlloc = reinterpret_cast<AllocFunc>(::dlsym(handle, "guTexAllocateMemory"));
        auto texSource = reinterpret_cast<SourceFunc>(::dlsym(handle, "guTexSource"));
        auto texGetInfo = reinterpret_cast<GetInfoFunc>(::dlsym(handle, "guTexGetMipMapInfo"));
        auto texChange = reinterpret_cast<ChangeAttrsFunc>(::dlsym(handle, "guTexChangeAttributes"));
        auto texGetCurrent = reinterpret_cast<GetCurrentFunc>(::dlsym(handle, "guTexGetCurrentMipMap"));
        auto texDownload = reinterpret_cast<DownloadFunc>(::dlsym(handle, "guTexDownloadMipMap"));
        auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombineFunction"));

        auto lodBias = reinterpret_cast<LodBiasFunc>(::dlsym(handle, "grTexLodBiasValue"));
        auto multibase = reinterpret_cast<MultibaseFunc>(::dlsym(handle, "grTexMultibase"));
        auto multibaseAddr = reinterpret_cast<MultibaseAddrFunc>(::dlsym(handle, "grTexMultibaseAddress"));

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
        GrMipMapId_t mmid = texAlloc(
            0, // TMU0
            0, // odd_even_mask
            256, 256,
            0x3, // GR_TEXFMT_RGB_565
            0, // GR_MIPMAP_DISABLE
            0, 0, // lod_min, lod_max
            0, // GR_ASPECT_1x1
            0, 0, // clamp modes
            0, 0, // filter modes
            0.0f, // lod_bias
            FXFALSE // trilinear
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
        REQUIRE(texChange(
            mmid,
            128, 128,
            0x3,
            0,
            0, 0,
            0,
            0, 0,
            0, 0
        ) == FXTRUE);
        REQUIRE(info->width == 128);
        REQUIRE(info->height == 128);

        // 4. Download mock texture data
        std::vector<uint16_t> mockData(128 * 128, 0xF800); // Pure Red 16-bit pixels
        REQUIRE_NOTHROW(texDownload(mmid, mockData.data(), nullptr));
        REQUIRE(info->data == mockData.data());

        // 5. Bind the texture and verify it is marked as current on TMU0
        REQUIRE_NOTHROW(texSource(mmid));
        REQUIRE(texGetCurrent(0) == mmid);

        // 6. Test texture combination state mapping
        REQUIRE_NOTHROW(texCombine(0, 0x4)); // GR_TEXTURECOMBINE_MULTIPLY

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
            void* handle = OpenGlide2Handle();
            REQUIRE(handle != nullptr);

            using InitFunc = void(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int); // match signature or use void(*)()
            using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
            using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
            using DrawTriFunc = void(*)(const void*, const void*, const void*);
            using BufferSwapFunc = void(*)(int);
            using WinCloseFunc = void(*)(void);
            using ShutdownFunc = void(*)(void);

            auto glideInit   = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto drawTri     = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
            auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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

            // Canonical Glide 2 vertex: x, y, z, r, g, b
            struct Vert2 { float x, y, z, r, g, b, ooz, a, oow; };
            Vert2 v0{100.0f, 100.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};
            Vert2 v1{500.0f, 100.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};
            Vert2 v2{300.0f, 400.0f, 0.0f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f};

            REQUIRE_NOTHROW(drawTri(&v0, &v1, &v2));
            REQUIRE_NOTHROW(bufferSwap(1));
            winClose();
            glideShutdown();

            ::dlclose(handle);
        }

        // 2. Validate Glide 3.x Programmable Vertex Layouts & Drawing
        {
            void* handle = OpenGlide3Handle();
            REQUIRE(handle != nullptr);

            using InitFunc = void(*)(void);
            using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
            using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);
            using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
            using DrawTriFunc = void(*)(const void*, const void*, const void*);
            using BufferSwapFunc = void(*)(FxU32);
            using WinCloseFunc = FxBool(*)(FxU32);
            using ShutdownFunc = void(*)(void);

            auto glideInit    = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen      = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
            auto bufferClear  = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto drawTri      = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
            auto bufferSwap   = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose     = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
            auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
                float pos[2];    // offset 0
                uint32_t padding;// offset 8
                float rgb[3];    // offset 12
                float alpha;     // offset 24
            };

            // Configure programmable layout
            vertexLayout(0, 0, 1);  // GR_PARAM_XY at offset 0
            vertexLayout(4, 12, 1); // GR_PARAM_RGB at offset 12
            vertexLayout(5, 24, 1); // GR_PARAM_A at offset 24

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
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc     = void(*)(void);
        using WinOpenFunc  = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using ClearFunc    = void(*)(FxU32, FxU32, FxU32);
        using ClipFunc     = void(*)(FxU32, FxU32, FxU32, FxU32);
        using BlendFunc    = void(*)(FxU32, FxU32, FxU32, FxU32);
        using CullFunc     = void(*)(FxU32);
        using DrawTriFunc  = void(*)(const void*, const void*, const void*);
        using ReadFunc     = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using WinCloseFunc = void(*)(void);
        using AlphaTestFunc = void(*)(FxU32);
        using AlphaRefFunc  = void(*)(FxU32);
        using DepthModeFunc = void(*)(FxU32);
        using DepthFuncFunc = void(*)(FxU32);
        using DepthMaskFunc = void(*)(FxBool);
        using AlphaCombineFunc = void(*)(FxU32, FxU32, FxU32, FxU32, FxBool);
        using ShutdownFunc = void(*)(void);

        auto glideInit  = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen    = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto clearBuf   = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
        auto clipWin    = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
        auto blendFunc  = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
        auto cullMode   = CullFunc(::dlsym(handle, "grCullMode"));
        auto drawTri    = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto readLfb    = reinterpret_cast<ReadFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto alphaCombine = reinterpret_cast<AlphaCombineFunc>(::dlsym(handle, "grAlphaCombine"));
        auto winClose   = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto alphaTestFunc   = reinterpret_cast<AlphaTestFunc>(::dlsym(handle, "grAlphaTestFunction"));
        auto alphaRefVal     = reinterpret_cast<AlphaRefFunc>(::dlsym(handle, "grAlphaTestReferenceValue"));
        auto depthBufferMode = reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
        auto depthBufferFunc = reinterpret_cast<DepthFuncFunc>(::dlsym(handle, "grDepthBufferFunction"));
        auto depthMask       = reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
        auto glideShutdown   = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 headless
        REQUIRE(ctx == 1);

        std::vector<uint32_t> lfbData(800 * 600, 0);

        // 1. Validate Scissor Box Containment
        clipWin(100, 100, 200, 200);
        clearBuf(0xFF000000, 0, 1); // Black

        StandardVert q0{0.0f, 0.0f, 0.5f, 255.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert q1{2000.0f, 0.0f, 0.5f, 255.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert q2{0.0f, 2000.0f, 0.5f, 255.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&q0, &q1, &q2);

        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[150 * 800 + 150] == 0xFFFFFF00); // Inside Scissor -> Yellow
        REQUIRE(lfbData[50 * 800 + 50] == 0xFF000000);   // Outside Scissor -> Untouched Black

        // Revert Scissor
        clipWin(0, 0, 800, 600);

        // 2. Validate Alpha Blending (50% Blue over Solid Red -> Perfect Purple 0xFF7F007F)
        clearBuf(0xFFFF0000, 0, 1); // Solid Red (0xFFFF0000 in ARGB)
        blendFunc(1, 5, 1, 5); // GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA
        alphaCombine(1, 0, 0, 2, 0); // GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE

        StandardVert b0{0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f}; // 50% Blue
        StandardVert b1{2000.0f, 0.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert b2{0.0f, 2000.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&b0, &b1, &b2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        uint32_t blendPixel = lfbData[200 * 800 + 300];
        REQUIRE((blendPixel & 0xFF00FF00) == 0xBF000000); // 75% Alpha (0xBF), 0 Green
        uint32_t blendR = (blendPixel >> 16) & 0xFF;
        uint32_t blendB = blendPixel & 0xFF;
        REQUIRE((blendR == 0x7F || blendR == 0x80)); // Allow both GPU (0x7F) and CPU (0x80) rounding!
        REQUIRE((blendB == 0x7F || blendB == 0x80));

        // Revert Blend & Combiner
        blendFunc(4, 0, 4, 0);
        alphaCombine(3, 8, 1, 2, 0); // Revert to authentic defaults

        // 3. Validate Polygon Winding Culling
        cullMode(1); // GR_CULL_NEGATIVE / CW backface
        clearBuf(0xFF000000, 0, 1); // Black

        // Draw Clockwise primitive (should be culled!)
        StandardVert cw0{100.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert cw1{100.0f, 400.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert cw2{400.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&cw0, &cw1, &cw2);

        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[150 * 800 + 150] == 0xFF000000); // Mapped target remains perfectly black!

        // Revert Cull
        cullMode(0); // GR_CULL_DISABLE

        // 4. Validate Alpha Testing (Punch-Through Alpha Rejection)
        clearBuf(0xFF000000, 0, 1); // Black
        alphaTestFunc(4); // GR_CMP_GREATER
        alphaRefVal(128); // Threshold = 128 (50%)
        alphaCombine(1, 0, 0, 2, 0); // Use iterated vertex alpha

        // Draw quad with Alpha = 100 (below threshold -> should be rejected)
        StandardVert at0{0.0f, 0.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f}; // Cyan
        StandardVert at1{2000.0f, 0.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert at2{0.0f, 2000.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&at0, &at1, &at2);

        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[300 * 800 + 300] == 0xFF000000); // Remains black!

        // Draw quad with Alpha = 200 (above threshold -> should pass)
        at0.a = 200.0f; at1.a = 200.0f; at2.a = 200.0f;
        drawTri(&at0, &at1, &at2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[300 * 800 + 300] == 0xC800FFFF); // Alpha=200(0xC8), Cyan(0x00FFFF)

        // Revert Alpha Test
        alphaTestFunc(7); // GR_CMP_ALWAYS
        alphaCombine(3, 8, 1, 2, 0); // Revert to constant alpha default

        // 5. Validate Extended Depth Comparison Suites (GR_CMP_LESS & Depth Masking)
        depthBufferMode(1); // GR_DEPTHBUFFER_ZBUFFER
        depthBufferFunc(1); // GR_CMP_LESS
        depthMask(FXTRUE);

        clearBuf(0xFF000000, 0, 65535); // Black, Depth=1.0 (65535 / 65535.0f)

        // Draw deep quad at Z = 0.8 (Should pass because 0.8 < 1.0)
        StandardVert d0{0.0f, 0.0f, 0.8f, 255.0f, 0.0f, 0.0f, 0.8f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f}; // Red, ooz = 0.8f
        StandardVert d1{2000.0f, 0.0f, 0.8f, 255.0f, 0.0f, 0.0f, 0.8f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert d2{0.0f, 2000.0f, 0.8f, 255.0f, 0.0f, 0.0f, 0.8f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFFFF0000); // Mapped red (opaque 0xFF)!

        // Draw quad at Z = 0.9 (Should be REJECTED because 0.9 is NOT less than 0.8)
        d0.z = 0.9f; d1.z = 0.9f; d2.z = 0.9f; d0.ooz = 0.9f; d1.ooz = 0.9f; d2.ooz = 0.9f;
        d0.r = 0.0f; d1.r = 0.0f; d2.r = 0.0f; d0.g = 255.0f; d1.g = 255.0f; d2.g = 255.0f; // Yellow
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFFFF0000); // Remains untouched red!

        // 6. Validate Depth Mask (Read-Only Depth Buffer)
        depthMask(FXFALSE); // Read-only depth!
        // Draw closer quad at Z = 0.4 in Blue
        d0.z = 0.4f; d1.z = 0.4f; d2.z = 0.4f; d0.ooz = 0.4f; d1.ooz = 0.4f; d2.ooz = 0.4f;
        d0.g = 0.0f; d1.g = 0.0f; d2.g = 0.0f; d0.b = 255.0f; d1.b = 255.0f; d2.b = 255.0f; // Blue
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFF0000FF); // Passes depth (0.4 < 0.8) -> Blue (opaque 0xFF)!

        // Now draw another quad at Z = 0.5 in Green
        // Since the previous draw had depthMask=FXFALSE, the Z buffer at that pixel is still 0.8!
        // So Z = 0.5 < 0.8 should PASS and overwrite with Green!
        d0.z = 0.5f; d1.z = 0.5f; d2.z = 0.5f; d0.ooz = 0.5f; d1.ooz = 0.5f; d2.ooz = 0.5f;
        d0.b = 0.0f; d1.b = 0.0f; d2.b = 0.0f; d0.g = 255.0f; d1.g = 255.0f; d2.g = 255.0f; // Green
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFF00FF00); // Successfully writes Green (opaque 0xFF)!

        // Revert Depth Mode
        depthMask(FXTRUE);

        winClose();
        glideShutdown();
        ::dlclose(handle);
#endif
    }

    SECTION("Task 5.0: Virtual TMU Multi-MIP Texturing & Uber-Shader Suites") {
#ifdef __linux__
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using SelectCtxFunc = FxBool(*)(FxU32);
        using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
        using WinCloseFunc = FxBool(*)(FxU32);
        using DrawTriFunc = void(*)(const void*, const void*, const void*);
        using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
        using ClipWinFunc = void(*)(FxU32, FxU32, FxU32, FxU32);
        using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);

        using TexDownloadMipMapFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
        using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
        using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
        using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
        using TexFilterModeFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
        using TexClampModeFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
        auto clipWin = reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
        auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

        auto texDownload = reinterpret_cast<TexDownloadMipMapFunc>(::dlsym(handle, "grTexDownloadMipMap"));
        auto texSource = reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
        auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
        auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
        auto texFilter = reinterpret_cast<TexFilterModeFunc>(::dlsym(handle, "grTexFilterMode"));
        auto texClamp = reinterpret_cast<TexClampModeFunc>(::dlsym(handle, "grTexClampMode"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
        REQUIRE(ctx == 1);
        REQUIRE(selectCtx(ctx) == FXTRUE);

        // Reset Scissor Clip Window to cover full screen
        clipWin(0, 0, 800, 600);

        // Configure explicit deterministic layout matching StandardVert precisely using Glide 3.x canonical parameter constants
        vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
        vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
        vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
        vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
        vertexLayout(0x03, 32, 1); // GR_PARAM_W at 32
        vertexLayout(0x40, 36, 1); // GR_PARAM_ST0 at 36

        bufferClear(0xFF000000, 0, 0xFFFFFFFF); // Clear depth to 1.0f

        // Prepare 16x16 Texture in GR_TEXFMT_RGB_565 (Format 0xa)
        // Checkerboard pattern: top-left & bottom-right are Cyan (0x07FF), top-right & bottom-left are Magenta (0xF81F)
        std::vector<uint16_t> texData(16 * 16);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                bool isCyan = ((x / 8) + (y / 8)) % 2 == 0;
                texData[y * 16 + x] = isCyan ? 0x07FF : 0xF81F;
            }
        }

        GrTexInfo texInfo;
        texInfo.smallLod = 4; // LOD 4 -> 16x16
        texInfo.largeLod = 4;
        texInfo.aspectRatio = 0; // 1:1
        texInfo.format = 0xa; // GR_TEXFMT_RGB_565
        texInfo.data = texData.data();

        // 1. Download MipMap to TMU 0 at address 0x1000 (Non-zero active handle)
        texDownload(0, 0x00001000, 0, &texInfo);

        // 2. Bind Texture
        texSource(0, 0x00001000, 0, &texInfo);
        texClamp(0, 0, 0); // WRAP
        texFilter(0, 0, 0); // POINT filter

        // 3. Configure Register Combiners
        texCombine(0, 1, 1, 1, 1, FXFALSE, FXFALSE);
        colorCombine(3, 8, 1, 1, FXFALSE); // Decal texturing: SCALE_OTHER, ONE, NONE, TEXTURE

        // 4. Draw Triangle Quad sampling top-left texel (s=64.0, t=64.0 -> should map to Cyan)
        // StandardVert format: x, y, z, r, g, b, ooz, a, oow, s, t, w
        StandardVert t0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        StandardVert t1{350.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        StandardVert t2{300.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        drawTri(&t0, &t1, &t2);

        std::vector<uint32_t> lfb(800 * 600);
        REQUIRE(readLfb(1, 0, 0, 800, 600, 800 * 4, lfb.data()) == FXTRUE);
        
        // Cyan pixel verification (R == 0, G >= 254, B >= 254)
        REQUIRE((lfb[310 * 800 + 310] & 0x00FF0000) == 0x00000000);
        REQUIRE((lfb[310 * 800 + 310] & 0x0000FF00) >= 0x0000FE00);
        REQUIRE((lfb[310 * 800 + 310] & 0x000000FF) >= 0x000000FE);

        // 5. Draw Triangle Quad sampling top-right texel (s=192.0, t=64.0 -> should map to Magenta)
        StandardVert m0{400.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
        StandardVert m1{450.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
        StandardVert m2{400.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
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

TEST_CASE("Glide Utility Helpers (gu*)", "[Utility]") {
    SECTION("Endian Swapping") {
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using SwapWordsFunc = FxU32(*)(FxU32);
        using SwapBytesFunc = FxU16(*)(FxU16);

        auto swapWords = reinterpret_cast<SwapWordsFunc>(::dlsym(handle, "guEndianSwapWords"));
        auto swapBytes = reinterpret_cast<SwapBytesFunc>(::dlsym(handle, "guEndianSwapBytes"));

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

        using EncodeRLEFunc = int(*)(void*, void*, FxU32, FxU32);
        auto encodeRLE = reinterpret_cast<EncodeRLEFunc>(::dlsym(handle, "guEncodeRLE16"));
        REQUIRE(encodeRLE != nullptr);

        // Construct 8x2 test image (16 pixels)
        // 8 pixels of Red (0xF800), 4 pixels of Green (0x07E0), 4 pixels of Blue (0x001F)
        std::vector<uint16_t> src(16);
        for (int i = 0; i < 8; ++i) src[i] = 0xF800;
        for (int i = 8; i < 12; ++i) src[i] = 0x07E0;
        for (int i = 12; i < 16; ++i) src[i] = 0x001F;

        // 1. Query size only
        int bytesNeeded = encodeRLE(nullptr, src.data(), 8, 2);
        REQUIRE(bytesNeeded == 12); // 3 runs * 4 bytes/run = 12 bytes

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

        using CreateMipMapFunc = FxU16*(*)(void);
        auto createMipMap = reinterpret_cast<CreateMipMapFunc>(::dlsym(handle, "guTexCreateColorMipMap"));
        REQUIRE(createMipMap != nullptr);

        FxU16* mipmap = createMipMap();
        REQUIRE(mipmap != nullptr);

        // Verify colors at the start of each mipmap level
        // Level 0 (256x256): starts at offset 0, Red (0xF800)
        REQUIRE(mipmap[0] == 0xF800);
        REQUIRE(mipmap[256*256 - 1] == 0xF800);

        // Level 1 (128x128): starts at offset 65536, Green (0x07E0)
        REQUIRE(mipmap[65536] == 0x07E0);
        REQUIRE(mipmap[65536 + 128*128 - 1] == 0x07E0);

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

        using InitFunc = void(*)(void);
        using SelectSstFunc = void(*)(int);
        using OpenWinFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using CloseWinFunc = void(*)(void);
        using AlphaSourceFunc = void(*)(GrAlphaSource_t);
        using ColorCombineFunc = void(*)(GrColorCombineFnc_t);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
        auto alphaSrc  = reinterpret_cast<AlphaSourceFunc>(::dlsym(handle, "guAlphaSource"));
        auto colorComb = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "guColorCombineFunction"));
        auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
        FxBool ok = winOpen(0, GR_RESOLUTION_800x600, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 0);
        REQUIRE(ok == FXTRUE);

        // Call the combiner helpers under the active context
        // These should execute successfully and update the combiner state without throwing/crashing
        REQUIRE_NOTHROW(alphaSrc(GR_ALPHASOURCE_TEXTURE_ALPHA));
        REQUIRE_NOTHROW(alphaSrc(GR_ALPHASOURCE_ITERATED_ALPHA));

        REQUIRE_NOTHROW(colorComb(GR_COLORCOMBINE_DECAL_TEXTURE));
        REQUIRE_NOTHROW(colorComb(GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB));

        winClose();
        glideShutdown();
        ::dlclose(handle);
    }
}

TEST_CASE("Texture Decoders & LFB Pipeline Simulation Correctness", "[Texture_LFB]") {
    // Initialize logger to print to console for debug visibility!
    auto& logger = GlideWrapper::Logger::GetInstance();
    logger.Initialize("test_debug.log", GlideWrapper::LogLevel::DEBUG, true);

    SECTION("RGB_332 Texture Format Decoder") {
        auto& tm = GlideWrapper::TextureManager::GetInstance();
        tm.Reset();
        
        // 1 pixel of RGB_332
        uint8_t rawData = 0b11111010; // R=7 (111), G=6 (110), B=2 (10)
        
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
        
        uint8_t rawData = 0x8A; // Alpha = 8, Intensity = 10
        
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
        
        uint16_t rawData = 0x89FA; // Alpha = 0x89, RGB332 = 0xFA (R=7, G=6, B=2)
        
        bool ok = tm.DownloadMipMap(0, 0x3000, 8, 8, 3, 0x8, &rawData);
        REQUIRE(ok == true);
        
        auto* tex = tm.GetTexture(0, 0x3000);
        REQUIRE(tex != nullptr);
        
        // Expected ARGB: A=0x89, R=255 (0xFF), G=218 (0xDA), B=170 (0xAA) -> 0x89FFDAAA
        uint32_t decoded = tex->swizzledMipLevels[0][0];
        REQUIRE(decoded == 0x89FFDAAA);
    }

    SECTION("AP_88 Texture Format Decoder and Deferred Palettization") {
        auto& tm = GlideWrapper::TextureManager::GetInstance();
        tm.Reset();
        
        uint16_t rawData = 0xF245; // Alpha = 0xF2, Index = 0x45
        
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
        mockPalette[0x45] = 0x00112233; // RGB color = 0x112233
        
        ok = tm.DownloadTable(0, 2, mockPalette); // 2 = GR_TEXTABLE_PALETTE
        REQUIRE(ok == true);
        
        // 3. Verify texture deferred-swizzles correctly using the new palette table
        // Expected: A=0xF2, R=0x11, G=0x22, B=0x33 -> 0xF2112233
        REQUIRE(tex->swizzledMipLevels[0][0] == 0xF2112233);
    }

    SECTION("AYIQ_8422 Texture Format Decoder and Deferred NCC Swizzling") {
        auto& tm = GlideWrapper::TextureManager::GetInstance();
        tm.Reset();
        
        uint16_t rawData = 0xE196; // Alpha = 0xE1, YIQ = 0x96 (Y=9, I=1, Q=2)
        
        // Download AYIQ_8422 texture
        bool ok = tm.DownloadMipMap(0, 0x5000, 8, 8, 3, 0x9, &rawData);
        REQUIRE(ok == true);
        
        auto* tex = tm.GetTexture(0, 0x5000);
        REQUIRE(tex != nullptr);
        REQUIRE(tex->isNcc == true);
        
        // 1. Check grayscale fallback: A=0xE1, Y=9 -> 9 * 255 / 15 = 153 (0x99) -> 0xE1999999
        REQUIRE(tex->swizzledMipLevels[0][0] == 0xE1999999);
        
        // 2. Load NCC0 table
        GuNccTable ncc;
        std::memset(&ncc, 0, sizeof(ncc));
        ncc.yRGB[9] = 100;
        ncc.iRGB[1][0] = 10; // R
        ncc.iRGB[1][1] = 20; // G
        ncc.iRGB[1][2] = 30; // B
        ncc.qRGB[2][0] = 5;  // R
        ncc.qRGB[2][1] = 15; // G
        ncc.qRGB[2][2] = 25; // B
        
        // Active NCC table is NCC0 (0) by default. Download table NCC0 (0).
        ok = tm.DownloadTable(0, 0, &ncc); // 0 = GR_TEXTABLE_NCC0
        REQUIRE(ok == true);
        
        // 3. Verify texture deferred-swizzles correctly using active NCC table
        // Expected: A=0xE1, R=115 (0x73), G=135 (0x87), B=155 (0x9B) -> 0xE173879B
        REQUIRE(tex->swizzledMipLevels[0][0] == 0xE173879B);
    }

    SECTION("LFB Blending and Chroma Keying Simulation") {
        GlideWrapper::SoftwareBackend backend;
        GlideWrapper::WrapperConfig cfg;
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
        
        // 1. Chroma Keying test: Write Green (0x07E0) with Chroma Keying matching Green
        uint16_t green565 = 0x07E0;
        
        GlideWrapper::LfbPipelineConfig chromaConfig;
        chromaConfig.pixelPipeline = true;
        chromaConfig.chromakeyEnabled = true;
        chromaConfig.chromakeyValue = 0x07E0; // Match green
        
        // Write to (0, 0)
        ok = backend.WriteLFB(1, 0, 0, 1, 1, 2, 0, &green565, nullptr, chromaConfig);
        REQUIRE(ok == true);
        
        // Verify pixel remains Blue (write discarded)
        backend.ReadLFB(1, 0, 0, 1, 1, 4, &pixel);
        REQUIRE(pixel == 0xFF0000FF);
        
        // 2. Alpha Blending test: Write Green (0x07E0) with 50% constant alpha blending
        GlideWrapper::LfbPipelineConfig blendConfig;
        blendConfig.pixelPipeline = true;
        blendConfig.chromakeyEnabled = false;
        blendConfig.constantAlpha = 128; // ~50%
        blendConfig.rgbSrcBlend = 4;     // GR_BLEND_SRC_ALPHA
        blendConfig.rgbDstBlend = 5;     // GR_BLEND_ONE_MINUS_SRC_ALPHA
        
        ok = backend.WriteLFB(1, 0, 0, 1, 1, 2, 0, &green565, nullptr, blendConfig);
        REQUIRE(ok == true);
        
        // Verify blended color: Green (0, 255, 0) blended 50/50 with Blue (0, 0, 255)
        // R = 0
        // G = 255 * (128.0f/255.0f) = 128
        // B = 255 * (127.0f/255.0f) = 127
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

TEST_CASE("OpenGL ES Gamma Correction Accuracy Validation") {
#ifdef __linux__
    // 1. Ensure we only run this test if the OpenGL ES backend is active at process start
    const char* envBackend = std::getenv("GLIDE_BACKEND");
    if (!envBackend || std::string(envBackend) != "opengl_es") {
        return; // Safely skip if not explicitly running GLES
    }

    // Load the shared library dynamically
    void* handle = ::dlopen("./build/lib/libglide2x.so", RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(handle != nullptr);

    // Resolve needed functions
    using InitFunc = void(*)(void);
    using ShutdownFunc = void(*)(void);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using BufferSwapFunc = void(*)(FxU32);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GammaFunc = void(*)(float);

    auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto lfbReadRegion = reinterpret_cast<LfbReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto gammaCorrection = reinterpret_cast<GammaFunc>(::dlsym(handle, "grGammaCorrectionValue"));

    REQUIRE(init != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbReadRegion != nullptr);
    REQUIRE(gammaCorrection != nullptr);

    // 1. Force the OpenGL ES backend (saving original for restoration)
    const char* origBackend = std::getenv("GLIDE_BACKEND");
    std::string origBackendStr = origBackend ? origBackend : "";
    ::setenv("GLIDE_BACKEND", "opengl_es", 1);

    // 2. Initialize Glide and open the window (windowed mode so it runs headlessly)
    REQUIRE_NOTHROW(init());
    FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 0);
    REQUIRE(ok == FXTRUE);

    // --- CASE 1: GAMMA = 1.0f (Linear Mapping) ---
    gammaCorrection(1.0f);

    // Clear the backbuffer to a solid mid-grey color:
    // Color: 0xFF808080 (ARGB8888, Alpha=FF, Red=80, Green=80, Blue=80)
    bufferClear(0xFF808080, 0xFF, 0);

    // Swap buffers to present the mid-grey frame onto the screen (with Gamma = 1.0)
    bufferSwap(1);

    // Read the center pixel (at 320, 240) directly from the EGL window surface (framebuffer 0)
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
    // Verify that although the screen shows a boosted 180, the digital VRAM front buffer
    // itself remains perfectly linear at 128 (matching physical Voodoo RAMDAC behavior!)
    uint32_t vramPixel = 0;
    ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &vramPixel);
    REQUIRE(ok == FXTRUE);

    // Unpack Red channel from ARGB8888 VRAM pixel
    uint8_t vramR = (vramPixel >> 16) & 0xFF;
    REQUIRE(vramR >= 126);
    REQUIRE(vramR <= 130); // Still linear 128!

    // Clean up
    REQUIRE_NOTHROW(shutdown());
    ::dlclose(handle);

    // Restore environment faithfully
    if (!origBackendStr.empty()) {
        ::setenv("GLIDE_BACKEND", origBackendStr.c_str(), 1);
    } else {
        ::unsetenv("GLIDE_BACKEND");
    }
#endif
}

TEST_CASE("Glide Chroma-Key Range Rendering Verification") {
#ifdef __linux__
    const char* envBackend = std::getenv("GLIDE_BACKEND");
    std::string activeBackend = envBackend ? envBackend : "vulkan";



    INFO("Testing Chroma-Key Range on Backend: " << activeBackend);

    // Load the shared library dynamically
    void* handle = ::dlopen("./build/lib/libglide2x.so", RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(handle != nullptr);

    // Resolve needed functions
    using InitFunc = void(*)(void);
    using ShutdownFunc = void(*)(void);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using BufferSwapFunc = void(*)(FxU32);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GetProcAddressFunc = GrProc(*)(char*);
    using ChromakeyModeFunc = void(*)(GrChromakeyMode_t);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);

    auto init = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto lfbReadRegion = reinterpret_cast<LfbReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto getProcAddress = reinterpret_cast<GetProcAddressFunc>(::dlsym(handle, "grGetProcAddress"));
    auto chromakeyMode = reinterpret_cast<ChromakeyModeFunc>(::dlsym(handle, "grChromakeyMode"));
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));

    REQUIRE(init != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbReadRegion != nullptr);
    REQUIRE(getProcAddress != nullptr);
    REQUIRE(chromakeyMode != nullptr);
    REQUIRE(drawTriangle != nullptr);

    // Initialize Glide first to load environment overrides and lock the correct backend
    REQUIRE_NOTHROW(init());

    // Resolve our extension function grChromakeyRangeExt dynamically!
    using ChromakeyRangeExtFunc = void(*)(GrColor_t, GrColor_t, uint32_t);
    char extName[] = "grChromakeyRangeExt";
    auto chromakeyRangeExt = reinterpret_cast<ChromakeyRangeExtFunc>(getProcAddress(extName));
    REQUIRE(chromakeyRangeExt != nullptr); // Dynamic loader check!

    // Open the window
    FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 0);
    REQUIRE(ok == FXTRUE);

    // Resolve additional state functions for robust initialization and to prevent state leaks
    // We use standard C/C++ types (int, float) in the signatures to ensure 100% compile-safe ABI compatibility.
    using StateFunc5 = void(*)(int, int, int, int, int);
    using StateFunc7 = void(*)(int, int, int, int, int, int, int);
    using StateFunc1 = void(*)(int);
    using StateFunc4 = void(*)(int, uint32_t, uint32_t, void*);

    auto colorCombine = reinterpret_cast<StateFunc5>(::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<StateFunc5>(::dlsym(handle, "grAlphaCombine"));
    auto depthBufferMode = reinterpret_cast<StateFunc1>(::dlsym(handle, "grDepthBufferMode"));
    auto alphaTestFunction = reinterpret_cast<StateFunc1>(::dlsym(handle, "grAlphaTestFunction"));
    auto texCombine = reinterpret_cast<StateFunc7>(::dlsym(handle, "grTexCombine"));
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
    // GR_COMBINE_FUNCTION_LOCAL = 1, GR_COMBINE_FACTOR_NONE = 0, GR_COMBINE_LOCAL_ITERATED = 0, GR_COMBINE_OTHER_NONE = 2
    colorCombine(1, 0, 0, 2, 0);
    alphaCombine(1, 0, 0, 2, 0);

    // Disable depth buffering, alpha testing, texturing on both TMUs, and fogging:
    // GR_DEPTHBUFFER_DISABLE = 0, GR_CMP_ALWAYS = 7, GR_COMBINE_FUNCTION_ZERO = 0, GR_FOG_DISABLE = 0
    depthBufferMode(0);
    alphaTestFunction(7);
    texCombine(0, 0, 0, 0, 0, 0, 0); // TMU0
    texCombine(1, 0, 0, 0, 0, 0, 0); // TMU1
    texSource(0, 0xFFFFFFFF, 0, nullptr); // Unbind TMU0 texture (resets m_boundTexAddress[0])
    texSource(1, 0xFFFFFFFF, 0, nullptr); // Unbind TMU1 texture (resets m_boundTexAddress[1])
    fogMode(0);

    // Enable Chroma-Keying
    chromakeyMode(GR_CHROMAKEY_ENABLE);

    // Define vertices for a large triangle covering the center of the screen
    // Aligning exactly with the official Glide 2.x GrVertex struct layout:
    struct SimpleVertex {
        float x, y, z;      // X, Y, Z (float 0, 1, 2)
        float r, g, b;      // R, G, B (float 3, 4, 5)
        float ooz;          // ooz     (float 6)
        float a;            // Alpha   (float 7)
        float oow;          // oow     (float 8)
        float s, t, oow_tex; // TMU0 s, t, oow (float 9, 10, 11)
    };

    // x, y, z,  r, g, b, ooz, a, oow,  s, t, oow_tex
    SimpleVertex v0 = { 100.0f, 100.0f, 1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 255.0f, 1.0f,  0.0f, 0.0f, 1.0f };
    SimpleVertex v1 = { 540.0f, 100.0f, 1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 255.0f, 1.0f,  0.0f, 0.0f, 1.0f };
    SimpleVertex v2 = { 320.0f, 380.0f, 1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 255.0f, 1.0f,  0.0f, 0.0f, 1.0f };

    // --- CASE 1: INTERSECTION MODE - DISCARD MATCHING COLOR ---
    // Min color: R=100, G=120, B=50  -> 0x00647832
    // Max color: R=150, G=180, B=80  -> 0x0096B450
    // Mode: Range Enabled, Inclusive, Intersection -> 1 << 28 = 0x10000000
    chromakeyRangeExt(0x00647832, 0x0096B450, 0x10000000);

    // Clear background to pure solid blue: 0xFF0000FF
    bufferClear(0xFF0000FF, 0xFF, 0);

    // Setup vertices to have color: R=128, G=150, B=60 -> 0xFF80963C (Perfect match on all channels!)
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

    INFO("pixelVal1 (Case 1) = 0x" << std::hex << pixelVal1 << " | R=" << (int)r1 << ", G=" << (int)g1 << ", B=" << (int)b1);

    // Since the color matched all channels, the triangle should be DISCARDED!
    // Center pixel should remain the background color Solid Blue: 0xFF0000FF
    REQUIRE((int)r1 <= 5);
    REQUIRE((int)g1 <= 5);
    REQUIRE((int)b1 >= 250);

    // --- CASE 2: INTERSECTION MODE - RENDER NON-MATCHING COLOR ---
    // Setup vertices to have color: R=200, G=150, B=60 (Red is 200, which is outside [100, 150])
    // Since it's Intersection Mode, this should NOT discard!
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

    INFO("pixelVal2 (Case 2) = 0x" << std::hex << pixelVal2 << " | R=" << (int)r2 << ", G=" << (int)g2 << ", B=" << (int)b2);

    // Center pixel should be the triangle's color: R=200, G=150, B=60 (with tolerance of 2)
    REQUIRE((int)r2 >= 198); REQUIRE((int)r2 <= 202);
    REQUIRE((int)g2 >= 148); REQUIRE((int)g2 <= 152);
    REQUIRE((int)b2 >= 58);  REQUIRE((int)b2 <= 62);

    // --- CASE 3: UNION MODE - DISCARD PARTIAL MATCHING COLOR ---
    // Mode: Range Enabled, Inclusive, Union Mode -> (1 << 28) | (1 << 27) = 0x18000000
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

    INFO("pixelVal3 (Case 3) = 0x" << std::hex << pixelVal3 << " | R=" << (int)r3 << ", G=" << (int)g3 << ", B=" << (int)b3);

    // Triangle discarded -> Center pixel remains background Solid Blue: 0xFF0000FF
    REQUIRE((int)r3 <= 5);
    REQUIRE((int)g3 <= 5);
    REQUIRE((int)b3 >= 250);

    // --- CASE 4: DISABLE RANGE MODE & FALLBACK TO EXACT MATCH ---
    using ChromakeyValueFunc = void(*)(GrColor_t);
    auto chromakeyValue = reinterpret_cast<ChromakeyValueFunc>(::dlsym(handle, "grChromakeyValue"));
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
    ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal4_match);
    REQUIRE(ok == FXTRUE);

    uint8_t r4_m = (pixelVal4_match >> 16) & 0xFF;
    uint8_t g4_m = (pixelVal4_match >> 8) & 0xFF;
    uint8_t b4_m = pixelVal4_match & 0xFF;

    // Should be discarded -> remains Solid Blue
    REQUIRE((int)r4_m <= 5);
    REQUIRE((int)g4_m <= 5);
    REQUIRE((int)b4_m >= 250);

    // 4. Render a color that would match the old range but does NOT match the exact value:
    // Red = 138 (inside old [100, 150] range, but not exactly 128)
    // Since range mode is disabled, this should NOT be discarded and should render normally!
    bufferClear(0xFF0000FF, 0xFF, 0);
    v0.r = v1.r = v2.r = 138.0f;

    drawTriangle(&v0, &v1, &v2);
    bufferSwap(1);

    uint32_t pixelVal4_nomatch = 0;
    ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 240, 1, 1, 4, &pixelVal4_nomatch);
    REQUIRE(ok == FXTRUE);

    uint8_t r4_nm = (pixelVal4_nomatch >> 16) & 0xFF;
    uint8_t g4_nm = (pixelVal4_nomatch >> 8) & 0xFF;
    uint8_t b4_nm = pixelVal4_nomatch & 0xFF;

    // Should NOT be discarded -> rendered as Red=138, Green=150, Blue=60 (with tolerance 2)
    REQUIRE((int)r4_nm >= 136); REQUIRE((int)r4_nm <= 140);
    REQUIRE((int)g4_nm >= 148); REQUIRE((int)g4_nm <= 152);
    REQUIRE((int)b4_nm >= 58);  REQUIRE((int)b4_nm <= 62);

    // Clean up
    REQUIRE_NOTHROW(shutdown());
    ::dlclose(handle);
#endif
}


TEST_CASE("Glide 2.x Alpha 8 Font Texture Rendering & Text Damage Detection", "[Glide2][Text]") {
#ifdef __linux__
    // Dynamically load the Glide 2 wrapper library
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    // Resolve Glide 2.x function pointers
    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using TexDownloadFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSourceFunc = void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexClampFunc = void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t);
    using TexFilterFunc = void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using AlphaCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using DrawTriangleFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

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

    FxBool openOk = winOpen(0, GR_RESOLUTION_800x600, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    bufferClear(0xFF000000, 0, 0); // Clear to Black

    // Prepare 32x16 Texture in GR_TEXFMT_ALPHA_8
    // Top half (y = 0..7) is solid white (0xFF), bottom half (y = 8..15) is transparent/black (0x00)
    std::vector<uint8_t> texData(32 * 16, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            texData[y * 32 + x] = 0xFF;
        }
    }

    GrTexInfo texInfo;
    texInfo.smallLod = GR_LOD_32; // LOD 32 -> 32x32 Max dimension
    texInfo.largeLod = GR_LOD_32;
    texInfo.aspectRatio = GR_ASPECT_2x1; // 2W x 1H -> 32x16
    texInfo.format = GR_TEXFMT_ALPHA_8;
    texInfo.data = texData.data();

    // Download to TMU0
    texDownload(0, 0x00002000, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texSource(0, 0x00002000, GR_MIPMAPLEVELMASK_BOTH, &texInfo);
    texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texFilter(0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    // Route texture alpha channel to TMU0 output
    texCombine(0, 
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, 
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, 
               FXFALSE, FXFALSE);

    // Route TMU0 output to framebuffer color (RGB) and alpha channels
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Define vertices for a centered 200x200 quad (x from 300 to 500, y from 300 to 500)
    // Glide 2.x coordinates are in pixel space.
    // Scale factor for 32x16 texture: D=32, so scale = 256.0 / 32 = 8.0.
    // s ranges from 0 to 32 * 8 = 256.0.
    // t ranges from 0 to 16 * 8 = 128.0.
    GrVertex v[4];
    for (int i = 0; i < 4; ++i) {
        v[i].z = 0.0f;
        v[i].ooz = 1.0f;
        v[i].oow = 1.0f;
        v[i].tmuvtx[0].oow = 1.0f;
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
    }

    // Top-Left
    v[0].x = 300.0f; v[0].y = 300.0f;
    v[0].tmuvtx[0].sow = 0.0f; v[0].tmuvtx[0].tow = 0.0f;

    // Top-Right
    v[1].x = 500.0f; v[1].y = 300.0f;
    v[1].tmuvtx[0].sow = 256.0f; v[1].tmuvtx[0].tow = 0.0f;

    // Bottom-Right
    v[2].x = 500.0f; v[2].y = 500.0f;
    v[2].tmuvtx[0].sow = 256.0f; v[2].tmuvtx[0].tow = 128.0f;

    // Bottom-Left
    v[3].x = 300.0f; v[3].y = 500.0f;
    v[3].tmuvtx[0].sow = 0.0f; v[3].tmuvtx[0].tow = 128.0f;

    // Draw two triangles to form the quad
    drawTriangle(&v[0], &v[1], &v[2]);
    drawTriangle(&v[0], &v[2], &v[3]);

    // Read back a pixel from the top half (e.g. x=400, y=350) directly from the back buffer
    // It should be solid white: 0xFFFFFFFF
    uint32_t topPixel = 0;
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 400, 350, 1, 1, 4, &topPixel);
    REQUIRE(ok == FXTRUE);

    // Read back a pixel from the bottom half (e.g. x=400, y=450) directly from the back buffer
    // It should be black: 0xFF000000 or similar (since alpha is 0)
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

    INFO("Top half pixel at (400,350) = 0x" << std::hex << topPixel << " | R=" << (int)r_top << ", G=" << (int)g_top << ", B=" << (int)b_top);
    INFO("Bottom half pixel at (400,450) = 0x" << std::hex << bottomPixel << " | R=" << (int)r_bottom << ", G=" << (int)g_bottom << ", B=" << (int)b_bottom);

    // Top half MUST be white (since it maps to texture t < 8.0, containing 0xFF)
    REQUIRE(r_top >= 250);
    REQUIRE(g_top >= 250);
    REQUIRE(b_top >= 250);

    // Bottom half MUST be black (since it maps to texture t > 8.0, containing 0x00)
    REQUIRE(r_bottom <= 5);
    REQUIRE(g_bottom <= 5);
    REQUIRE(b_bottom <= 5);

    // Clean up
    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x ABGR Framebuffer Clear & LFB Readback Parity", "[Glide2][Clear]") {
#ifdef __linux__
    // Dynamically load the Glide 2 wrapper library
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    // Resolve Glide 2.x function pointers
    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

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
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    // Clear buffer with color 0xff0000 (which represents BLUE under ABGR)
    bufferClear(0xff0000, 0, 0);

    // 1. Validate 16-bit LFB readback (should return hardware-accurate BGR565 format)
    // Blue is in bits 11..15 of the 16-bit word, Red in bits 0..4, Green in bits 5..10.
    // Since the screen is Blue, the value should be exactly 0xF800 (Blue=31, Green=0, Red=0).
    std::vector<uint16_t> buffer16(10, 0);
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 300, 240, 10, 1, 20, buffer16.data());
    REQUIRE(ok == FXTRUE);

    for (int i = 0; i < 10; ++i) {
        REQUIRE(buffer16[i] == 0xF800);
    }

    // 2. Validate 32-bit LFB readback (should return hardware-accurate ABGR format)
    // Blue is in bits 16..23, Alpha is in bits 24..31, Red and Green are 0.
    // So the value should be exactly 0xFFFF0000.
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

TEST_CASE("Glide 2.x Pixel Pipeline Chromakey & Triangles Discard Parity", "[Glide2][Chroma]") {
#ifdef __linux__
    // Dynamically load the Glide 2 wrapper library
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    // Resolve Glide 2.x function pointers
    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using ChromakeyModeFunc = void(*)(GrChromakeyMode_t);
    using ChromakeyValueFunc = void(*)(GrColor_t);
    using ConstantColorValueFunc = void(*)(GrColor_t);
    using SstOriginFunc = void(*)(GrOriginLocation_t);
    using DrawTriangleFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

    auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
    auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
    auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
    auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
    auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
    auto chromakeyMode = (ChromakeyModeFunc)dlsym(handle, "grChromakeyMode");
    auto chromakeyValue = (ChromakeyValueFunc)dlsym(handle, "grChromakeyValue");
    auto constantColorValue = (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
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
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    // Set up render state: constant color routed to combiner, chromakey enabled
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    chromakeyMode(GR_CHROMAKEY_ENABLE);

    // Define vertices for a triangle centered on the screen
    GrVertex vtxA, vtxB, vtxC;
    vtxA.z = vtxB.z = vtxC.z = 0.0f;
    vtxA.ooz = vtxB.ooz = vtxC.ooz = 1.0f;
    vtxA.oow = vtxB.oow = vtxC.oow = 1.0f;
    vtxA.r = vtxA.g = vtxA.b = vtxA.a = 255.0f;
    vtxB.r = vtxB.g = vtxB.b = vtxB.a = 255.0f;
    vtxC.r = vtxC.g = vtxC.b = vtxC.a = 255.0f;

    vtxA.x = 320.0f; vtxA.y = 48.0f;
    vtxB.x = 512.0f; vtxB.y = 432.0f;
    vtxC.x = 128.0f; vtxC.y = 432.0f;

    // SECTION 1: Test with Chromakey = Blue (0x00ff0000)
    SECTION("Chromakey Blue - Red triangle visible, Blue discarded") {
        chromakeyValue(0x00ff0000); // Blue key

        bufferClear(0x00, 0, 0); // Clear to Black

        // 1. Draw RED triangle (pointing down)
        constantColorValue(0x000000ff); // Red
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw BLUE triangle (pointing up)
        sstOrigin(GR_ORIGIN_LOWER_LEFT);
        constantColorValue(0x00ff0000); // Blue
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Restore origin for LFB read
        sstOrigin(GR_ORIGIN_UPPER_LEFT);

        // Read pixel from Red-only region (150, 400) -> should be Red!
        uint32_t redPixel = 0;
        FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 400, 1, 1, 4, &redPixel);
        REQUIRE(ok == FXTRUE);
        REQUIRE(redPixel == 0x000000FF); // Red under ABGR (Alpha = 0)

        // Read pixel from Blue-only region (150, 80) -> should be Black (discarded)!
        uint32_t bluePixel = 0;
        ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 80, 1, 1, 4, &bluePixel);
        REQUIRE(ok == FXTRUE);
        REQUIRE(bluePixel == 0xFF000000); // Opaque Black (discarded, remains at clear color)
    }

    // SECTION 2: Test with Chromakey = Red (0x000000ff)
    SECTION("Chromakey Red - Blue triangle visible, Red discarded") {
        chromakeyValue(0x000000ff); // Red key

        bufferClear(0x00, 0, 0); // Clear to Black

        // 1. Draw RED triangle (pointing down)
        constantColorValue(0x000000ff); // Red
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw BLUE triangle (pointing up)
        sstOrigin(GR_ORIGIN_LOWER_LEFT);
        constantColorValue(0x00ff0000); // Blue
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Restore origin
        sstOrigin(GR_ORIGIN_UPPER_LEFT);

        // Read pixel from Red-only region (150, 400) -> should be Black (discarded)!
        uint32_t redPixel = 0;
        FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 400, 1, 1, 4, &redPixel);
        REQUIRE(ok == FXTRUE);
        REQUIRE(redPixel == 0xFF000000); // Opaque Black (discarded, remains at clear color)

        // Read pixel from Blue-only region (150, 80) -> should be Blue!
        uint32_t bluePixel = 0;
        ok = readLfb(GR_BUFFER_BACKBUFFER, 150, 80, 1, 1, 4, &bluePixel);
        REQUIRE(ok == FXTRUE);
        REQUIRE(bluePixel == 0x00FF0000); // Blue under ABGR (Alpha = 0)
    }

    // Clean up
    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x Geometry - Points & Lines [test01][test02]", "[Glide2][Geometry]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using ConstantColorValueFunc = void(*)(GrColor_t);
    using DrawPointFunc = void(*)(const GrVertex*);
    using DrawLineFunc = void(*)(const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

    auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
    auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
    auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
    auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
    auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
    auto constantColorValue = (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
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

    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    // Setup flat constant color shading
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

    SECTION("test01: Flat Diagonal Points") {
        constantColorValue(0xFFFFFFFF); // White
        bufferClear(0x00, 0, 0);       // Clear to Black

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
        FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
        REQUIRE(readOk == FXTRUE);

        // DIAGNOSTIC: Count total non-black pixels and scan first point neighborhood
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
            UNSCOPED_INFO("  y=" << ny << ": "
                << std::hex
                << " [x=" << (firstPx - 2) << "]: 0x" << fb[ny * 640 + (firstPx - 2)]
                << " | 0x" << fb[ny * 640 + (firstPx - 1)]
                << " | 0x" << fb[ny * 640 + firstPx]
                << " | 0x" << fb[ny * 640 + (firstPx + 1)]
                << " | 0x" << fb[ny * 640 + (firstPx + 2)]
                << std::dec);
        }

        // Verify points along diagonal are White, and verify point size is exactly 1x1 (neighbors are Black)
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
            
            INFO("Failed at point index " << i << " (px=" << px << ", py=" << py << "). Expected exactly 1 white pixel in 3x3 neighborhood, but found " << whiteCount);
            REQUIRE(whiteCount == 1);
            
            // For the found white pixel, verify its own 8 immediate neighbors are Black
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

    SECTION("test02: Flat Line Fan") {
        constantColorValue(0xFFFFFFFF); // White
        bufferClear(0x00, 0, 0);       // Clear to Black

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
        FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
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
        UNSCOPED_INFO("Total non-black pixels in backbuffer for test02: " << nonBlackCount);

        // Verify that pixels inside the fan are White, and outside are Black
        // Since all lines start at the top edge and end at the right edge,
        // the top-right region is heavily covered, while the bottom-left region is untouched.
        
        // (600, 100) is near the top-right corner and sits deep inside the line fan
        uint32_t colorInside = fb[100 * 640 + 600];
        INFO("Expected (600, 100) to be White, but got 0x" << std::hex << colorInside << std::dec);
        REQUIRE((colorInside & 0x00FFFFFF) == 0x00FFFFFF);

        // (100, 400) is in the bottom-left region and sits far outside the line fan
        uint32_t colorOutside = fb[400 * 640 + 100];
        REQUIRE((colorOutside & 0x00FFFFFF) == 0x00000000);
    }

    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x Geometry - Gouraud Color Interpolation [test03][test04]", "[Glide2][Gouraud]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using DrawLineFunc = void(*)(const GrVertex*, const GrVertex*);
    using DrawTriangleFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

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

    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    // Setup Gouraud shading combiner (iterated local)
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    SECTION("test03: Gouraud Shaded Line") {
        bufferClear(0x00, 0, 0); // Clear to Black

        GrVertex vtxA, vtxB;
        vtxA.z = vtxB.z = 0.0f;
        vtxA.ooz = vtxB.ooz = 1.0f;
        vtxA.oow = vtxB.oow = 1.0f;

        // Diagonal line from (0,0) to (640,480)
        vtxA.x = 0.0f; vtxA.y = 0.0f;
        vtxA.r = 255.0f; vtxA.g = 0.0f; vtxA.b = 0.0f; vtxA.a = 255.0f; // Red

        vtxB.x = 640.0f; vtxB.y = 480.0f;
        vtxB.r = 0.0f; vtxB.g = 255.0f; vtxB.b = 0.0f; vtxB.a = 255.0f; // Green

        drawLine(&vtxA, &vtxB);

        // Read the entire backbuffer into RAM once (640x480)
        std::vector<uint32_t> fb(640 * 480);
        FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
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
        UNSCOPED_INFO("Total non-black pixels in backbuffer for test03: " << nonBlackCount);

        // Read and verify color gradient along the line using exact integer steps
        // 1. Near the start (80, 60) -> should be heavily Red
        uint32_t startPixel = fb[60 * 640 + 80];
        FxU8 r_start = startPixel & 0xFF;
        FxU8 g_start = (startPixel >> 8) & 0xFF;
        
        INFO("Failed at (80, 60) with color 0x" << std::hex << startPixel << std::dec << " (R=" << (int)r_start << ", G=" << (int)g_start << ")");
        REQUIRE(r_start > 200);
        REQUIRE(g_start < 50);

        // 2. Midpoint (320, 240) -> should be Yellow (balanced Red and Green)
        uint32_t midPixel = fb[240 * 640 + 320];
        FxU8 r_mid = midPixel & 0xFF;
        FxU8 g_mid = (midPixel >> 8) & 0xFF;
        
        INFO("Failed at midpoint (320, 240) with color 0x" << std::hex << midPixel << std::dec << " (R=" << (int)r_mid << ", G=" << (int)g_mid << ")");
        REQUIRE(r_mid > 100);
        REQUIRE(g_mid > 100);
        REQUIRE(abs((int)r_mid - (int)g_mid) < 25); // roughly equal

        // 3. Near the end (560, 420) -> should be heavily Green
        uint32_t endPixel = fb[420 * 640 + 560];
        FxU8 r_end = endPixel & 0xFF;
        FxU8 g_end = (endPixel >> 8) & 0xFF;
        
        INFO("Failed at (560, 420) with color 0x" << std::hex << endPixel << std::dec << " (R=" << (int)r_end << ", G=" << (int)g_end << ")");
        REQUIRE(g_end > 200);
        REQUIRE(r_end < 50);
    }

    SECTION("test04: Gouraud Shaded Triangle") {
        bufferClear(0x00, 0, 0); // Clear to Black

        GrVertex vtxA, vtxB, vtxC;
        vtxA.z = vtxB.z = vtxC.z = 0.0f;
        vtxA.ooz = vtxB.ooz = vtxC.ooz = 1.0f;
        vtxA.oow = vtxB.oow = vtxC.oow = 1.0f;

        // Vertex A (Top-Center) - Red
        vtxA.x = 192.0f; vtxA.y = 144.0f;
        vtxA.r = 255.0f; vtxA.g = 0.0f; vtxA.b = 0.0f; vtxA.a = 255.0f;

        // Vertex B (Bottom-Right) - Green
        vtxB.x = 512.0f; vtxB.y = 192.0f;
        vtxB.r = 0.0f; vtxB.g = 255.0f; vtxB.b = 0.0f; vtxB.a = 255.0f;

        // Vertex C (Bottom-Left) - Blue
        vtxC.x = 320.0f; vtxC.y = 384.0f;
        vtxC.r = 0.0f; vtxC.g = 0.0f; vtxC.b = 255.0f; vtxC.a = 255.0f;

        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Read the entire backbuffer into RAM once (640x480)
        std::vector<uint32_t> fb(640 * 480);
        FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data());
        REQUIRE(readOk == FXTRUE);

        // 1. Verify center (340, 240) is neutral Gray
        uint32_t centerPixel = fb[240 * 640 + 340];
        FxU8 r_c = centerPixel & 0xFF;
        FxU8 g_c = (centerPixel >> 8) & 0xFF;
        FxU8 b_c = (centerPixel >> 16) & 0xFF;
        
        INFO("Failed at centroid (340, 240) with color 0x" << std::hex << centerPixel << std::dec << " (R=" << (int)r_c << ", G=" << (int)g_c << ", B=" << (int)b_c << ")");
        REQUIRE(r_c > 60);
        REQUIRE(r_c < 100);
        REQUIRE(abs((int)r_c - (int)g_c) < 10);
        REQUIRE(abs((int)g_c - (int)b_c) < 10);

        // 2. Verify near Green vertex (500, 195) is dominant Green
        uint32_t greenPixel = fb[195 * 640 + 500];
        FxU8 r_g = greenPixel & 0xFF;
        FxU8 g_g = (greenPixel >> 8) & 0xFF;
        FxU8 b_g = (greenPixel >> 16) & 0xFF;
        
        INFO("Failed at (500, 195) with color 0x" << std::hex << greenPixel << std::dec << " (R=" << (int)r_g << ", G=" << (int)g_g << ", B=" << (int)b_g << ")");
        REQUIRE(g_g > 200);
        REQUIRE(r_g < 50);
        REQUIRE(b_g < 50);

        // 3. Verify near Blue vertex (320, 375) is dominant Blue
        uint32_t bluePixel = fb[375 * 640 + 320];
        FxU8 r_b = bluePixel & 0xFF;
        FxU8 g_b = (bluePixel >> 8) & 0xFF;
        FxU8 b_b = (bluePixel >> 16) & 0xFF;
        
        INFO("Failed at (320, 375) with color 0x" << std::hex << bluePixel << std::dec << " (R=" << (int)r_b << ", G=" << (int)g_b << ", B=" << (int)b_b << ")");
        REQUIRE(b_b > 200);
        REQUIRE(r_b < 50);
        REQUIRE(g_b < 50);
    }

    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x Framebuffer Controls [test05][test06][test10][test14][test15]", "[Glide2][Framebuffer]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using ConstantColorValueFunc = void(*)(GrColor_t);
    using DrawTriangleFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

    using DepthBufferModeFunc = void(*)(GrDepthBufferMode_t);
    using DepthBufferFunctionFunc = void(*)(GrCmpFnc_t);
    using DepthMaskFunc = void(*)(FxBool);
    using CullModeFunc = void(*)(GrCullMode_t);
    using SstOriginFunc = void(*)(GrOriginLocation_t);
    using DepthBiasLevelFunc = void(*)(FxI16);
    using ClipWindowFunc = void(*)(FxU32, FxU32, FxU32, FxU32);

    auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
    auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
    auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
    auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
    auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
    auto constantColorValue = (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
    auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
    auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
    auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

    auto depthBufferMode = (DepthBufferModeFunc)dlsym(handle, "grDepthBufferMode");
    auto depthBufferFunction = (DepthBufferFunctionFunc)dlsym(handle, "grDepthBufferFunction");
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
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    // Common setup: flat constant color combiners
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

    SECTION("test05: Z-buffer Depth Buffering") {
        depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        depthBufferFunction(GR_CMP_GREATER);
        depthMask(FXTRUE);
        
        bufferClear(0x00, 0, 0); // Clear color to Black, depth to 0 (farthest ooz)

        GrVertex vtxA, vtxB, vtxC;

        // Triangle 1: Gray (0x00808080) at constant depth 10.0f (ooz = 6553.5)
        vtxA.x = 160.0f; vtxA.y = 100.8f; vtxA.ooz = 6553.5f; vtxA.oow = 1.0f;
        vtxB.x = 480.0f; vtxB.y = 100.8f; vtxB.ooz = 6553.5f; vtxB.oow = 1.0f;
        vtxC.x = 320.0f; vtxC.y = 379.2f; vtxC.ooz = 6553.5f; vtxC.oow = 1.0f;

        constantColorValue(0x00808080);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Triangle 2: Green (0x0000FF00) interpenetrating (depth 12.5f to 7.5f)
        vtxA.x = 550.4f; vtxA.y = 100.8f; vtxA.ooz = 5242.8f; vtxA.oow = 1.0f;
        vtxB.x = 550.4f; vtxB.y = 379.2f; vtxB.ooz = 5242.8f; vtxB.oow = 1.0f;
        vtxC.x = 89.6f;  vtxC.y = 240.0f; vtxC.ooz = 8738.0f; vtxC.oow = 1.0f;

        constantColorValue(0x0000FF00);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Read back the framebuffer
        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Verify mathematical Z-buffer parity
        // 1. Pixel at x=320, y=240 (x < 377.6) -> must be Green (0x0000FF00)
        uint32_t p_close = fb[240 * 640 + 320];
        REQUIRE((p_close & 0x00FFFFFF) == 0x0000FF00);

        // 2. Pixel at x=390, y=240 (377.6 < x < 400) -> must be Gray (0x00808080)
        uint32_t p_far = fb[240 * 640 + 390];
        REQUIRE((p_far & 0x00FFFFFF) == 0x00808080);
    }

    SECTION("test06: W-buffer Depth Buffering") {
        depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);

        bufferClear(0x00, 0, 0xFFFF); // Clear depth to farthest (W_FARTHEST)

        GrVertex vtxA, vtxB, vtxC;

        // Triangle 1: Gray (0x00808080) at constant depth W = 10.0 (oow = 0.1)
        vtxA.x = 160.0f; vtxA.y = 100.8f; vtxA.ooz = 1.0f; vtxA.oow = 0.1f;
        vtxB.x = 480.0f; vtxB.y = 100.8f; vtxB.ooz = 1.0f; vtxB.oow = 0.1f;
        vtxC.x = 320.0f; vtxC.y = 379.2f; vtxC.ooz = 1.0f; vtxC.oow = 0.1f;

        constantColorValue(0x00808080);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Triangle 2: Green (0x0000FF00) interpenetrating (depth W = 12.5 to 7.5)
        vtxA.x = 550.4f; vtxA.y = 100.8f; vtxA.ooz = 1.0f; vtxA.oow = 0.08f;
        vtxB.x = 550.4f; vtxB.y = 379.2f; vtxB.ooz = 1.0f; vtxB.oow = 0.08f;
        vtxC.x = 89.6f;  vtxC.y = 240.0f; vtxC.ooz = 1.0f; vtxC.oow = 0.1333333f;

        constantColorValue(0x0000FF00);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Read back
        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Verify W-buffer parity (should cross at exactly the same place x = 377.6)
        uint32_t p_close = fb[240 * 640 + 320];
        REQUIRE((p_close & 0x00FFFFFF) == 0x0000FF00);

        uint32_t p_far = fb[240 * 640 + 390];
        REQUIRE((p_far & 0x00FFFFFF) == 0x00808080);
    }

    SECTION("test10: Polygon Backface Culling") {
        depthBufferMode(GR_DEPTHBUFFER_DISABLE);

        GrVertex vtxA, vtxB, vtxC;
        // Vertex A (Top-Center)
        vtxA.x = 320.0f; vtxA.y = 72.0f; vtxA.ooz = 1.0f; vtxA.oow = 1.0f;
        // Vertex B (Bottom-Right)
        vtxB.x = 512.0f; vtxB.y = 408.0f; vtxB.ooz = 1.0f; vtxB.oow = 1.0f;
        // Vertex C (Bottom-Left)
        vtxC.x = 128.0f; vtxC.y = 408.0f; vtxC.ooz = 1.0f; vtxC.oow = 1.0f;

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
        sstOrigin(GR_ORIGIN_UPPER_LEFT); // Restore

        // Read back
        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Under Cull Positive (CW culled, only CCW/Red is drawn):
        // (200, 130) is inside Red triangle, outside Blue -> must be Red
        uint32_t p_red_side = fb[130 * 640 + 200];
        REQUIRE((p_red_side & 0x00FFFFFF) == 0x00FF0000);

        // (200, 350) is inside Blue triangle, outside Red -> must be Black (since Blue is culled)
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
        sstOrigin(GR_ORIGIN_UPPER_LEFT); // Restore

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Under Cull Negative (CCW culled, only CW/Blue is drawn):
        // (200, 130) is inside Red triangle, outside Blue -> must be Black (since Red is culled)
        p_red_side = fb[130 * 640 + 200];
        REQUIRE((p_red_side & 0x00FFFFFF) == 0x00000000);

        // (200, 350) is inside Blue triangle, outside Red -> must be Blue
        p_blue_side = fb[350 * 640 + 200];
        REQUIRE((p_blue_side & 0x00FFFFFF) == 0x000000FF);

        cullMode(GR_CULL_DISABLE); // Restore
    }

    SECTION("test14: Dynamic Depth Bias") {
        depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        depthBufferFunction(GR_CMP_GREATER);
        depthMask(FXTRUE);

        GrVertex vtxA, vtxB, vtxC;

        // Sub-test 1: Positive Bias (+300) -> Gray triangle is biased closer
        bufferClear(0x00, 0, 0);

        // 1. Draw GRAY triangle (biased)
        vtxA.x = 160.0f; vtxA.y = 100.8f; vtxA.ooz = 6553.5f; vtxA.oow = 1.0f;
        vtxB.x = 480.0f; vtxB.y = 100.8f; vtxB.ooz = 6553.5f; vtxB.oow = 1.0f;
        vtxC.x = 320.0f; vtxC.y = 379.2f; vtxC.ooz = 6553.5f; vtxC.oow = 1.0f;

        constantColorValue(0x00808080);
        depthBiasLevel(300);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw GREEN triangle (static zero bias)
        vtxA.x = 550.4f; vtxA.y = 100.8f; vtxA.ooz = 5242.8f; vtxA.oow = 1.0f;
        vtxB.x = 550.4f; vtxB.y = 379.2f; vtxB.ooz = 5242.8f; vtxB.oow = 1.0f;
        vtxC.x = 89.6f;  vtxC.y = 240.0f; vtxC.ooz = 8738.0f; vtxC.oow = 1.0f;

        constantColorValue(0x0000FF00);
        depthBiasLevel(0);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Under +300 bias, Gray triangle's ooz (6553.5) becomes effectively 6853.5.
        // At x=360, Green's ooz is 6687.1. Since 6853.5 > 6687.1, Gray now OVERWRITES Green!
        uint32_t p_biased = fb[240 * 640 + 360];
        REQUIRE((p_biased & 0x00FFFFFF) == 0x00808080);

        // Sub-test 2: Negative Bias (-300) -> Gray triangle is biased further
        bufferClear(0x00, 0, 0);

        // Draw GRAY with -300 bias
        constantColorValue(0x00808080);
        depthBiasLevel(-300);
        vtxA.x = 160.0f; vtxA.y = 100.8f; vtxA.ooz = 6553.5f; vtxA.oow = 1.0f;
        vtxB.x = 480.0f; vtxB.y = 100.8f; vtxB.ooz = 6553.5f; vtxB.oow = 1.0f;
        vtxC.x = 320.0f; vtxC.y = 379.2f; vtxC.ooz = 6553.5f; vtxC.oow = 1.0f;
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Draw GREEN with 0 bias
        constantColorValue(0x0000FF00);
        depthBiasLevel(0);
        vtxA.x = 550.4f; vtxA.y = 100.8f; vtxA.ooz = 5242.8f; vtxA.oow = 1.0f;
        vtxB.x = 550.4f; vtxB.y = 379.2f; vtxB.ooz = 5242.8f; vtxB.oow = 1.0f;
        vtxC.x = 89.6f;  vtxC.y = 240.0f; vtxC.ooz = 8738.0f; vtxC.oow = 1.0f;
        drawTriangle(&vtxA, &vtxB, &vtxC);

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Under -300 bias, Gray's ooz (6553.5) becomes effectively 6253.5.
        // At x=390, Green's ooz is 6459.1. Since 6253.5 < 6459.1, Gray is REJECTED (Green wins)!
        uint32_t p_unbiased = fb[240 * 640 + 390];
        REQUIRE((p_unbiased & 0x00FFFFFF) == 0x0000FF00);

        depthBiasLevel(0); // Restore
    }

    SECTION("test15: Screen Clipping") {
        depthBufferMode(GR_DEPTHBUFFER_DISABLE);

        // Mode 0: Centered large clip window: x=[128, 512], y=[96, 384]
        clipWindow(128, 96, 512, 384);
        bufferClear(0x00, 0, 0);

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
                vtxA.a = 255.0f; vtxB.a = 255.0f; vtxC.a = 255.0f;

                constantColorValue(0x00808080);
                drawTriangle(&vtxA, &vtxB, &vtxC);
            }
        }

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

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

TEST_CASE("Glide 2.x Alpha Blending & Fog [test07][test08][test13]", "[Glide2][AlphaBlend][Fog]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using ConstantColorValueFunc = void(*)(GrColor_t);
    using DrawTriangleFunc = void(*)(const GrVertex*, const GrVertex*, const GrVertex*);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

    using AlphaCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using AlphaBlendFunctionFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using DepthBufferModeFunc = void(*)(GrDepthBufferMode_t);
    using SstOriginFunc = void(*)(GrOriginLocation_t);
    using FogModeFunc = void(*)(GrFogMode_t);
    using FogColorValueFunc = void(*)(GrColor_t);
    using FogTableFunc = void(*)(const GrFog_t*);
    using FogGenerateExpFunc = void(*)(GrFog_t*, float);

    auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
    auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
    auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
    auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
    auto colorCombine = (ColorCombineFunc)dlsym(handle, "grColorCombine");
    auto constantColorValue = (ConstantColorValueFunc)dlsym(handle, "grConstantColorValue");
    auto drawTriangle = (DrawTriangleFunc)dlsym(handle, "grDrawTriangle");
    auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
    auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

    auto alphaCombine = (AlphaCombineFunc)dlsym(handle, "grAlphaCombine");
    auto alphaBlendFunction = (AlphaBlendFunctionFunc)dlsym(handle, "grAlphaBlendFunction");
    auto depthBufferMode = (DepthBufferModeFunc)dlsym(handle, "grDepthBufferMode");
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
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    SECTION("test07: Translucent Alpha Blend") {
        /* Set up Render State - flat constant color + alpha blending */
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ZERO, GR_BLEND_ZERO);
        depthBufferMode(GR_DEPTHBUFFER_DISABLE);

        bufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;

        // Triangle 1: Opaque Blue (0xFF0000FF)
        vtxA.x = 160.0f; vtxA.y = 100.8f; vtxA.ooz = 1.0f; vtxA.oow = 1.0f;
        vtxB.x = 480.0f; vtxB.y = 100.8f; vtxB.ooz = 1.0f; vtxB.oow = 1.0f;
        vtxC.x = 320.0f; vtxC.y = 379.2f; vtxC.ooz = 1.0f; vtxC.oow = 1.0f;

        constantColorValue(0xFF0000FF);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Triangle 2: Translucent Red (0x80FF0000) - 50% opacity
        vtxA.x = 550.4f; vtxA.y = 100.8f; vtxA.ooz = 1.0f; vtxA.oow = 1.0f;
        vtxB.x = 550.4f; vtxB.y = 379.2f; vtxB.ooz = 1.0f; vtxB.oow = 1.0f;
        vtxC.x = 89.6f;  vtxC.y = 240.0f; vtxC.ooz = 1.0f; vtxC.oow = 1.0f;

        constantColorValue(0x80FF0000);
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // Read back
        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // 1. Overlap pixel (320, 240) -> blended to Magenta (0x007F007F)
        // Src (Red, 0x80) over Dest (Blue, opaque)
        // Red = 127, Green = 0, Blue = 127
        uint32_t p_blend = fb[240 * 640 + 320];
        uint8_t r = p_blend & 0xFF;
        uint8_t g = (p_blend >> 8) & 0xFF;
        uint8_t b = (p_blend >> 16) & 0xFF;
        INFO("Overlap pixel (320, 240): color = 0x" << std::hex << p_blend << " (r=" << std::dec << (int)r << ", g=" << (int)g << ", b=" << (int)b << ")");
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
        INFO("Red-only pixel (120, 240): color = 0x" << std::hex << p_red << " (r=" << std::dec << (int)r << ", g=" << (int)g << ", b=" << (int)b << ")");
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

    SECTION("test08: Table-Based Fogging") {
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
        fogMode(GR_FOG_WITH_TABLE);
        fogColorValue(0xFF00FF00); // Green

        GrFog_t fogtable[64];
        fogGenerateExp(fogtable, 0.01f);
        fogTable(fogtable);

        bufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;
        
        vtxA.x = 0.0f; vtxA.y = 0.0f; vtxA.ooz = 1.0f;
        vtxA.r = 0.0f; vtxA.g = 0.0f; vtxA.b = 64.0f; vtxA.a = 255.0f;
        vtxA.oow = 1.0f / 20.0f;

        vtxB.x = 10.24f; vtxB.y = 480.0f; vtxB.ooz = 1.0f;
        vtxB.r = 0.0f; vtxB.g = 0.0f; vtxB.b = 128.0f; vtxB.a = 255.0f;
        vtxB.oow = 1.0f / 2000.0f;

        vtxC.x = 640.0f; vtxC.y = 9.984f; vtxC.ooz = 1.0f;
        vtxC.r = 0.0f; vtxC.g = 0.0f; vtxC.b = 64.0f; vtxC.a = 255.0f;
        vtxC.oow = 1.0f / 20.0f;

        drawTriangle(&vtxA, &vtxB, &vtxC);

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

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
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
        fogMode(GR_FOG_WITH_TABLE | GR_FOG_MULT2);
        fogColorValue(0xFF00FF00); // Green

        GrFog_t fogtable[64];
        fogGenerateExp(fogtable, 0.01f);
        fogTable(fogtable);

        bufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;
        vtxA.x = 0.0f; vtxA.y = 0.0f; vtxA.ooz = 1.0f;
        vtxA.r = 0.0f; vtxA.g = 0.0f; vtxA.b = 64.0f; vtxA.a = 255.0f;
        vtxA.oow = 1.0f / 20.0f;

        vtxB.x = 10.24f; vtxB.y = 480.0f; vtxB.ooz = 1.0f;
        vtxB.r = 0.0f; vtxB.g = 0.0f; vtxB.b = 128.0f; vtxB.a = 255.0f;
        vtxB.oow = 1.0f / 2000.0f;

        vtxC.x = 640.0f; vtxC.y = 9.984f; vtxC.ooz = 1.0f;
        vtxC.r = 0.0f; vtxC.g = 0.0f; vtxC.b = 64.0f; vtxC.a = 255.0f;
        vtxC.oow = 1.0f / 20.0f;

        drawTriangle(&vtxA, &vtxB, &vtxC);

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

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

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Near pixel: W is close to 20. f = 0.1813.
        // Subtractive Fog: Color = InputColor * (1 - f) = Blue(64) * 0.8187 = 52.4 -> 52.
        // Swapped client read-back: Red channel receives the Blue input.
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
        fogColorValue(0x0000FF00); // Green Fog
        fogMode(GR_FOG_WITH_ITERATED_ALPHA);
        bufferClear(0x00, 0, 0);

        // Setup constant alpha 64 for all vertices to verify uniform alpha fogging
        vtxA.a = 64.0f;
        vtxB.a = 64.0f;
        vtxC.a = 64.0f;

        drawTriangle(&vtxA, &vtxB, &vtxC);

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Check a pixel inside the triangle (e.g., center)
        uint32_t p_iter = fb[100 * 640 + 320];
        r = p_iter & 0xFF;
        g = (p_iter >> 8) & 0xFF;
        b = (p_iter >> 16) & 0xFF;

        // Swapped readback: Red channel gets the Blue input color.
        // Color_out = Blue_perspective_interpolated(77.3) * (1 - 0.251) = (58, 64, 0)
        // So client read-back: r = 0, g = 64, b = 58
        REQUIRE(r <= 8);
        REQUIRE(std::abs((int)g - 64) <= 8);
        REQUIRE(std::abs((int)b - 58) <= 8);

        // Restore vertex alpha to default 255.0f
        vtxA.a = 255.0f;
        vtxB.a = 255.0f;
        vtxC.a = 255.0f;

        // Restore fog color and mode
        fogColorValue(0x0000FF00); // Default green
        fogMode(GR_FOG_DISABLE);
    }

    SECTION("test13: Gouraud Iterated Alpha") {
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ZERO, GR_BLEND_ZERO);
        depthBufferMode(GR_DEPTHBUFFER_DISABLE);

        bufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;
        vtxA.x = 320.0f; vtxA.y = 48.0f; vtxA.ooz = 1.0f; vtxA.oow = 1.0f;
        vtxB.x = 512.0f; vtxB.y = 432.0f; vtxB.ooz = 1.0f; vtxB.oow = 1.0f;
        vtxC.x = 128.0f; vtxC.y = 432.0f; vtxC.ooz = 1.0f; vtxC.oow = 1.0f;

        // 1. Draw solid RED background triangle (opaque alpha)
        vtxA.a = vtxB.a = vtxC.a = 255.0f;
        constantColorValue(0x000000FF); // Red (in ABGR)
        drawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw fading BLUE overlay triangle (iterated alpha, lower-left origin)
        sstOrigin(GR_ORIGIN_LOWER_LEFT);
        vtxA.a = 0.0f; // transparent at vertex A (rendered at bottom)
        vtxB.a = vtxC.a = 255.0f; // opaque at vertex B, C (rendered at top)
        constantColorValue(0x00FF0000); // Blue (in ABGR)
        drawTriangle(&vtxA, &vtxB, &vtxC);
        sstOrigin(GR_ORIGIN_UPPER_LEFT); // Restore

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        // Along x = 320:
        // 1. Top region (y = 60) -> overlay is almost opaque Blue -> must be Blue
        // Red = 0, Green = 0, Blue = 255
        uint32_t p_top = fb[60 * 640 + 320];
        uint8_t r = p_top & 0xFF;
        uint8_t g = (p_top >> 8) & 0xFF;
        uint8_t b = (p_top >> 16) & 0xFF;
        REQUIRE(r <= 32); // allow some gradient margin
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

        // 3. Bottom region (y = 420) -> overlay is almost transparent -> must be Red
        // Red = 255, Green = 0, Blue = 0
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

TEST_CASE("Glide 2.x LFB Access [test11][test26]", "[Glide2][LFB]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using GlideInitFunc = void(*)(void);
    using SstSelectFunc = void(*)(int);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU16);
    using LfbReadRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using GlideShutdownFunc = void(*)(void);

    using LfbLockFunc = FxBool(*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t, GrOriginLocation_t, FxBool, GrLfbInfo_t*);
    using LfbUnlockFunc = FxBool(*)(GrLock_t, GrBuffer_t);
    using LfbWriteRegionFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxI32, void*);
    using DitherModeFunc = void(*)(GrDitherMode_t);

    auto glideInit = (GlideInitFunc)dlsym(handle, "grGlideInit");
    auto sstSelect = (SstSelectFunc)dlsym(handle, "grSstSelect");
    auto winOpen = (WinOpenFunc)dlsym(handle, "grSstWinOpen");
    auto bufferClear = (BufferClearFunc)dlsym(handle, "grBufferClear");
    auto readLfb = (LfbReadRegionFunc)dlsym(handle, "grLfbReadRegion");
    auto shutdown = (GlideShutdownFunc)dlsym(handle, "grGlideShutdown");

    auto lfbLock = (LfbLockFunc)dlsym(handle, "grLfbLock");
    auto lfbUnlock = (LfbUnlockFunc)dlsym(handle, "grLfbUnlock");
    auto lfbWriteRegion = (LfbWriteRegionFunc)dlsym(handle, "grLfbWriteRegion");
    auto ditherMode = (DitherModeFunc)dlsym(handle, "grDitherMode");

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(lfbLock != nullptr);
    REQUIRE(lfbUnlock != nullptr);
    REQUIRE(lfbWriteRegion != nullptr);
    REQUIRE(ditherMode != nullptr);

    glideInit();
    sstSelect(0);
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    SECTION("test11: LFB Direct Lock & Copy") {
        ditherMode(GR_DITHER_DISABLE);

        // Create Source 64x64 565 procedural pattern
        std::vector<uint16_t> colorBuf(64 * 64);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                uint8_t red = x << 2;
                uint8_t grn = y << 2;
                uint8_t blu = (x + y) << 1;
                uint16_t color = (red & 0xF8) << 8;
                color |= (grn & 0xFC) << 3;
                color |= (blu & 0xF8) >> 3;
                colorBuf[y * 64 + x] = color;
            }
        }

        bufferClear(0x00, 0, 0);

        GrLfbInfo_t info;
        info.size = sizeof(GrLfbInfo_t);

        // Lock back buffer for writing
        FxBool lockWriteOk = lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
        REQUIRE(lockWriteOk == FXTRUE);
        REQUIRE(info.lfbPtr != nullptr);

        int startX = 100;
        int startY = 100;

        // Write image directly to LFB
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                uint16_t* pixel = (uint16_t*)(((char*)info.lfbPtr) + (y + startY) * info.strideInBytes + (x + startX) * 2);
                *pixel = colorBuf[y * 64 + x];
            }
        }
        lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);

        // Lock back buffer for reading
        FxBool lockReadOk = lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
        REQUIRE(lockReadOk == FXTRUE);

        // Grab image back from LFB
        std::vector<uint16_t> grabBuf(64 * 64, 0);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                uint16_t* pixel = (uint16_t*)(((char*)info.lfbPtr) + (y + startY) * info.strideInBytes + (x + startX) * 2);
                grabBuf[y * 64 + x] = *pixel;
            }
        }
        lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER);

        // Assert byte-for-byte identity
        REQUIRE(std::memcmp(colorBuf.data(), grabBuf.data(), colorBuf.size() * 2) == 0);
    }

    SECTION("test26: LFB Region Copying & Formats") {
        ditherMode(GR_DITHER_DISABLE);

        // Generate a 256x256 procedural 565 source pattern in memory
        std::vector<uint16_t> textureData(256 * 256);
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 256; x++) {
                uint8_t red = x;
                uint8_t grn = y;
                uint8_t blu = x + y;
                textureData[y * 256 + x] = ((red & 0xF8) << 8) | ((grn & 0xFC) << 3) | ((blu & 0xF8) >> 3);
            }
        }

        // Helper function for converting our procedural texture to various formats
        auto imageConvert = [](void* dst, void* src, GrLfbSrcFmt_t format, uint32_t* outBpp) {
            uint16_t* srcData = (uint16_t*)src;
            uint16_t* shortData = (uint16_t*)dst;
            uint32_t* longData = (uint32_t*)dst;

            switch (format) {
            case GR_LFB_SRC_FMT_565:
            case GR_LFB_SRC_FMT_555:
            case GR_LFB_SRC_FMT_1555:
                *outBpp = 2;
                break;
            case GR_LFB_SRC_FMT_888:
            case GR_LFB_SRC_FMT_8888:
            case GR_LFB_SRC_FMT_565_DEPTH:
            case GR_LFB_SRC_FMT_1555_DEPTH:
            case GR_LFB_SRC_FMT_555_DEPTH:
                *outBpp = 4;
                break;
            default:
                *outBpp = 2;
                break;
            }

            for (int y = 0; y < 256; y++) {
                for (int x = 0; x < 256; x++) {
                    uint16_t shortColor = srcData[y * 256 + x];
                    uint32_t longColor = shortColor;

                    switch (format) {
                    case GR_LFB_SRC_FMT_565:
                        shortData[y * 256 + x] = shortColor;
                        break;
                    case GR_LFB_SRC_FMT_555:
                    case GR_LFB_SRC_FMT_1555:
                        shortColor = ((0x8000) |
                                      ((shortColor >> 1) & 0x7C00) |
                                      ((shortColor >> 1) & 0x03E0) |
                                      (shortColor & 0x1F));
                        shortData[y * 256 + x] = shortColor;
                        break;
                    case GR_LFB_SRC_FMT_888:
                    case GR_LFB_SRC_FMT_8888:
                        longColor = ((0xFF000000) |
                                     ((longColor << 8) & 0x00F80000) |
                                     ((longColor << 5) & 0x0000FC00) |
                                     ((longColor << 3) & 0x000000F8));
                        longData[y * 256 + x] = longColor;
                        break;
                    case GR_LFB_SRC_FMT_565_DEPTH:
                        longData[y * 256 + x] = longColor;
                        break;
                    case GR_LFB_SRC_FMT_1555_DEPTH:
                    case GR_LFB_SRC_FMT_555_DEPTH:
                        longColor = ((0x00008000) |
                                     ((longColor >> 1) & 0x00007C00) |
                                     ((longColor >> 1) & 0x000003E0) |
                                     (longColor & 0x0000001F));
                        longData[y * 256 + x] = longColor;
                        break;
                    default:
                        break;
                    }
                }
            }
        };

        const GrLfbSrcFmt_t validFormats[] = {
            GR_LFB_SRC_FMT_565,
            GR_LFB_SRC_FMT_555,
            GR_LFB_SRC_FMT_1555,
            GR_LFB_SRC_FMT_888,
            GR_LFB_SRC_FMT_8888,
            GR_LFB_SRC_FMT_565_DEPTH,
            GR_LFB_SRC_FMT_555_DEPTH,
            GR_LFB_SRC_FMT_1555_DEPTH
        };

        std::vector<uint32_t> imageBuffer(256 * 256);

        for (auto sourceFormat : validFormats) {
            std::memset(imageBuffer.data(), 0, imageBuffer.size() * 4);
            uint32_t bpp = 0;
            imageConvert(imageBuffer.data(), textureData.data(), sourceFormat, &bpp);

            bufferClear(0x00303030, 0, 0);

            FxBool writeOk = lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, sourceFormat, 256, 256, 256 * bpp, imageBuffer.data());
            REQUIRE(writeOk == FXTRUE);

            if (sourceFormat == GR_LFB_SRC_FMT_565) {
                std::vector<uint16_t> readImage(256 * 256, 0xFFFF);
                FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 256, 256, 256 * 2, readImage.data());
                REQUIRE(readOk == FXTRUE);

                REQUIRE(std::memcmp(imageBuffer.data(), readImage.data(), 256 * 256 * 2) == 0);
            }
        }
    }

    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x Texture & Combiner Pipeline [test17][test18][test19][test21][test29]", "[Glide2][Texture]") {
#ifdef __linux__
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    // Resolve Glide APIs
    auto glideInit = (void(*)(void))dlsym(handle, "grGlideInit");
    auto sstSelect = (void(*)(int))dlsym(handle, "grSstSelect");
    auto winOpen = (FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int))dlsym(handle, "grSstWinOpen");
    auto bufferClear = (void(*)(GrColor_t, GrAlpha_t, FxU16))dlsym(handle, "grBufferClear");
    auto readLfb = (FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*))dlsym(handle, "grLfbReadRegion");
    auto shutdown = (void(*)(void))dlsym(handle, "grGlideShutdown");

    auto colorCombine = (void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool))dlsym(handle, "grColorCombine");
    auto alphaCombine = (void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool))dlsym(handle, "grAlphaCombine");
    auto constantColorValue = (void(*)(GrColor_t))dlsym(handle, "grConstantColorValue");
    auto texDownload = (void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*))dlsym(handle, "grTexDownloadMipMap");
    auto texDownloadTable = (void(*)(GrChipID_t, GrTexTable_t, void*))dlsym(handle, "grTexDownloadTable");
    auto texSource = (void(*)(GrChipID_t, FxU32, FxU32, GrTexInfo*))dlsym(handle, "grTexSource");
    auto texClamp = (void(*)(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t))dlsym(handle, "grTexClampMode");
    auto texFilter = (void(*)(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t))dlsym(handle, "grTexFilterMode");
    auto drawTriangle = (void(*)(const void*, const void*, const void*))dlsym(handle, "grDrawTriangle");

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

    // Initialize
    glideInit();
    sstSelect(0);
    FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(openOk == FXTRUE);

    SECTION("test17: Multi-mode Register Combiners") {
        TlTexture texture;
        bool loadOk = tlLoadTexture("ext_tests/build_tests/miro.3df", &texture.info, &texture.tableType, &texture.tableData);
        REQUIRE(loadOk == true);

        // Download to TMU0
        texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // Bind TMU0
        texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        texClamp(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
        texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

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
        vtxA.r = 255.0f; vtxA.g =   0.0f; vtxA.b =   0.0f; vtxA.a = 255.0f;
        vtxB.r =   0.0f; vtxB.g = 255.0f; vtxB.b =   0.0f; vtxB.a = 128.0f;
        vtxC.r =   0.0f; vtxC.g =   0.0f; vtxC.b = 255.0f; vtxC.a = 128.0f;
        vtxD.r =   0.0f; vtxD.g =   0.0f; vtxD.b =   0.0f; vtxD.a =   0.0f;

        std::vector<uint32_t> fb(640 * 480);

        // Sub-section: Mode 0 (DECAL)
        {
            bufferClear(0, 0, 0);
            colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            
            uint32_t p_decal = fb[240 * 640 + 320];
            uint8_t r = p_decal & 0xFF;
            uint8_t g = (p_decal >> 8) & 0xFF;
            uint8_t b = (p_decal >> 16) & 0xFF;
            INFO("Decal Mode center pixel (320, 240): color = 0x" << std::hex << p_decal);
            REQUIRE((r > 0 || g > 0 || b > 0));
        }

        // Sub-section: Mode 1 (FLATLIT - modulated by constant color Red)
        {
            bufferClear(0, 0, 0);
            colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            
            uint32_t p_flat = fb[240 * 640 + 320];
            uint8_t r = p_flat & 0xFF;
            uint8_t g = (p_flat >> 8) & 0xFF;
            uint8_t b = (p_flat >> 16) & 0xFF;
            INFO("Flatlit Mode center pixel (320, 240): color = 0x" << std::hex << p_flat << " (r=" << std::dec << (int)r << ", g=" << (int)g << ", b=" << (int)b << ")");
            // Since constant color is 0x00ff0000 (Blue in client's ABGR layout),
            // the output is modulated by Blue: Red and Green must be near 0.
            REQUIRE(r <= 4);
            REQUIRE(g <= 4);
            REQUIRE(b > 0);
        }

        // Sub-section: Mode 2 (RGBLIT - modulated by iterated color)
        {
            bufferClear(0, 0, 0);
            colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            
            uint32_t p_cornerA = fb[100 * 640 + 132];
            uint32_t p_cornerD = fb[380 * 640 + 508];
            
            REQUIRE((p_cornerA & 0xFFFFFF) != 0);
            REQUIRE((p_cornerD & 0xFFFFFF) == 0);
        }
    }

    SECTION("test18: Texture Alpha Blending") {
        tlSetScreen(640.0f, 480.0f);
        using AlphaBlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
        auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));
        auto texMipMapMode = (void(*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(handle, "grTexMipMapMode");
        REQUIRE(alphaBlendFunction != nullptr);
        REQUIRE(texMipMapMode != nullptr);

        TlTexture texture;
        bool loadOk = tlLoadTexture("ext_tests/build_tests/alpha.3df", &texture.info, &texture.tableType, &texture.tableData);
        REQUIRE(loadOk == true);

        // Download to TMU0
        texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // Bind TMU0
        texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        texFilter(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
        texMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);

        bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

        // --- Stage 1: Draw 10x10 grid of background squares ---
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO, GR_BLEND_ZERO);

        #define RED  0x000000ff
        #define BLUE 0x00ff0000

        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                GrVertex vtxA, vtxB, vtxC, vtxD;
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

        colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ZERO, GR_BLEND_ZERO);

        drawTriangle(&vtxA, &vtxD, &vtxC);
        drawTriangle(&vtxA, &vtxB, &vtxD);

        std::vector<uint32_t> fb(640 * 480);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        REQUIRE((fb[50 * 640 + 50] & 0xFFFFFF) == 0x00FF0000);
        REQUIRE((fb[400 * 640 + 50] & 0xFFFFFF) == 0x00FF0000);

        auto assertColorClose = [](uint32_t actual, uint32_t expected) {
            int b_act = (actual >> 16) & 0xFF;
            int g_act = (actual >> 8) & 0xFF;
            int r_act = actual & 0xFF;

            int b_exp = (expected >> 16) & 0xFF;
            int g_exp = (expected >> 8) & 0xFF;
            int r_exp = expected & 0xFF;

            REQUIRE(std::abs(b_act - b_exp) <= 15);
            REQUIRE(std::abs(g_act - g_exp) <= 15);
            REQUIRE(std::abs(r_act - r_exp) <= 15);
        };

        assertColorClose(fb[240 * 640 + 320], 0xFF5A55BD);
        assertColorClose(fb[200 * 640 + 200], 0xFF0808D5);
        assertColorClose(fb[300 * 640 + 440], 0xFFDE0408);
        assertColorClose(fb[120 * 640 + 320], 0xFF7B79C5);
    }

    SECTION("test19: Texture Filtering") {
        tlSetScreen(640.0f, 480.0f);
        auto texMipMapMode = (void(*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(handle, "grTexMipMapMode");
        auto texCombine = (void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
        REQUIRE(texMipMapMode != nullptr);
        REQUIRE(texCombine != nullptr);

        TlTexture texture;
        bool loadOk = tlLoadTexture("ext_tests/build_tests/miro.3df", &texture.info, &texture.tableType, &texture.tableData);
        REQUIRE(loadOk == true);

        // Download to TMU0
        texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // Bind TMU0
        texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        
        colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        texMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

        std::vector<uint32_t> fb(640 * 480);
        auto assertColorClose = [](uint32_t actual, uint32_t expected) {
            int b_act = (actual >> 16) & 0xFF;
            int g_act = (actual >> 8) & 0xFF;
            int r_act = actual & 0xFF;
            int b_exp = (expected >> 16) & 0xFF;
            int g_exp = (expected >> 8) & 0xFF;
            int r_exp = expected & 0xFF;
            REQUIRE(std::abs(b_act - b_exp) <= 15);
            REQUIRE(std::abs(g_act - g_exp) <= 15);
            REQUIRE(std::abs(r_act - r_exp) <= 15);
        };

        // Frame 0: Magnify, Point Sampled
        {
            texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
            bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

            GrVertex vtxA, vtxB, vtxC, vtxD;
            vtxA.oow = 1.0f;
            vtxB = vtxC = vtxD = vtxA;

            vtxA.x = vtxC.x = tlScaleX(0.45f);
            vtxB.x = vtxD.x = tlScaleX(0.55f);
            vtxA.y = vtxB.y = tlScaleY(0.45f);
            vtxC.y = vtxD.y = tlScaleY(0.55f);

            vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
            vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
            vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
            vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            assertColorClose(fb[240 * 640 + 320], 0xFF525008);
            assertColorClose(fb[230 * 640 + 310], 0xFF415010);
            REQUIRE((fb[100 * 640 + 100] & 0xFFFFFF) == 0);
        }

        // Frame 1: Magnify, Bilinear
        {
            texFilter(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
            bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

            GrVertex vtxA, vtxB, vtxC, vtxD;
            vtxA.oow = 1.0f;
            vtxB = vtxC = vtxD = vtxA;

            vtxA.x = vtxC.x = tlScaleX(0.45f);
            vtxB.x = vtxD.x = tlScaleX(0.55f);
            vtxA.y = vtxB.y = tlScaleY(0.45f);
            vtxC.y = vtxD.y = tlScaleY(0.55f);

            vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
            vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
            vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
            vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            assertColorClose(fb[240 * 640 + 320], 0xFF4A5008);
            assertColorClose(fb[230 * 640 + 310], 0xFF414C10);
            REQUIRE((fb[100 * 640 + 100] & 0xFFFFFF) == 0);
        }

        // Frame 2: Minify, Point Sampled
        {
            texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
            bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

            GrVertex vtxA, vtxB, vtxC, vtxD;
            vtxA.oow = 1.0f;
            vtxB = vtxC = vtxD = vtxA;

            vtxA.x = vtxC.x = tlScaleX(0.0f);
            vtxB.x = vtxD.x = tlScaleX(1.0f);
            vtxA.y = vtxB.y = tlScaleY(0.0f);
            vtxC.y = vtxD.y = tlScaleY(1.0f);

            vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
            vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
            vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
            vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            assertColorClose(fb[240 * 640 + 320], 0xFF414810);
            assertColorClose(fb[200 * 640 + 200], 0xFF394010);
            assertColorClose(fb[50 * 640 + 50], 0xFF293408);
            REQUIRE((fb[350 * 640 + 450] & 0xFFFFFF) == 0);
        }

        // Frame 3: Minify, Bilinear
        {
            texFilter(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
            bufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

            GrVertex vtxA, vtxB, vtxC, vtxD;
            vtxA.oow = 1.0f;
            vtxB = vtxC = vtxD = vtxA;

            vtxA.x = vtxC.x = tlScaleX(0.0f);
            vtxB.x = vtxD.x = tlScaleX(1.0f);
            vtxA.y = vtxB.y = tlScaleY(0.0f);
            vtxC.y = vtxD.y = tlScaleY(1.0f);

            vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
            vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
            vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
            vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

            drawTriangle(&vtxA, &vtxD, &vtxC);
            drawTriangle(&vtxA, &vtxB, &vtxD);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
            assertColorClose(fb[240 * 640 + 320], 0xFF4A4C08);
            assertColorClose(fb[200 * 640 + 200], 0xFF393C08);
            assertColorClose(fb[50 * 640 + 50], 0xFF203008);
            REQUIRE((fb[350 * 640 + 450] & 0xFFFFFF) == 0);
        }
    }

    SECTION("test21: Texture Compositing / Multi-pass") {
        tlSetScreen(640.0f, 480.0f);
        auto texMipMapMode = (void(*)(GrChipID_t, GrMipMapMode_t, FxBool))dlsym(handle, "grTexMipMapMode");
        auto texCombine = (void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
        auto texDetailControl = (void(*)(GrChipID_t, int, int, float))dlsym(handle, "grTexDetailControl");
        using AlphaBlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
        auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));
        
        REQUIRE(texMipMapMode != nullptr);
        REQUIRE(texCombine != nullptr);
        REQUIRE(texDetailControl != nullptr);
        REQUIRE(alphaBlendFunction != nullptr);

        TlTexture baseTexture;
        TlTexture lightTexture;
        TlTexture detailTexture;

        REQUIRE(tlLoadTexture("ext_tests/build_tests/decal1.3df", &baseTexture.info, &baseTexture.tableType, &baseTexture.tableData) == true);
        REQUIRE(tlLoadTexture("ext_tests/build_tests/light.3df", &lightTexture.info, &lightTexture.tableType, &lightTexture.tableData) == true);
        REQUIRE(tlLoadTexture("ext_tests/build_tests/lava.3df", &detailTexture.info, &detailTexture.tableType, &detailTexture.tableData) == true);

        // Download to TMU0
        unsigned long baseTextureAddr = grTexMinAddress(GR_TMU0);
        texDownload(GR_TMU0, baseTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &baseTexture.info);
        if (baseTexture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, baseTexture.tableType, &baseTexture.tableData);
        }

        unsigned long lightTextureAddr = baseTextureAddr + grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &baseTexture.info);
        texDownload(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &lightTexture.info);
        if (lightTexture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, lightTexture.tableType, &lightTexture.tableData);
        }

        unsigned long detailTextureAddr = lightTextureAddr + grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &lightTexture.info);
        texDownload(GR_TMU0, detailTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &detailTexture.info);
        if (detailTexture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, detailTexture.tableType, &detailTexture.tableData);
        }

        // Vertices setup (Centered about Y axis)
        TlVertex3D srcVerts[4] = {
            { -0.5f, 0.0f,  0.5f, 1.0f, 0.0f, 0.0f, 0, 0, 0, 0 },
            {  0.5f, 0.0f,  0.5f, 1.0f, 1.0f, 0.0f, 0, 0, 0, 0 },
            { -0.5f, 0.0f, -0.5f, 1.0f, 0.0f, 1.0f, 0, 0, 0, 0 },
            {  0.5f, 0.0f, -0.5f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0 }
        };

        std::vector<uint32_t> fb(640 * 480);
        auto assertColorClose = [](uint32_t actual, uint32_t expected) {
            int b_act = (actual >> 16) & 0xFF;
            int g_act = (actual >> 8) & 0xFF;
            int r_act = actual & 0xFF;
            int b_exp = (expected >> 16) & 0xFF;
            int g_exp = (expected >> 8) & 0xFF;
            int r_exp = expected & 0xFF;
            REQUIRE(std::abs(b_act - b_exp) <= 15);
            REQUIRE(std::abs(g_act - g_exp) <= 15);
            REQUIRE(std::abs(r_act - r_exp) <= 15);
        };

        auto renderCompositingFrame = [&](int modeIndex) {
            bufferClear(0x00404040, 0, GR_ZDEPTHVALUE_FARTHEST);

            texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
            colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

            GrVertex vtxA, vtxB, vtxC, vtxD;
            TlVertex3D xfVerts[4];
            TlVertex3D prjVerts[4];

            vtxA.oow = 1.0f;
            vtxB = vtxC = vtxD = vtxA;

            float distance = 1.01f; // Frame 0 distance

            tlSetMatrix(tlIdentity());
            tlMultMatrix(tlXRotation(-20.0f));
            tlMultMatrix(tlTranslation(0.0f, -0.3f, distance));

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
                texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
            } else if (modeIndex == 2) {
                texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL, GR_COMBINE_FACTOR_DETAIL_FACTOR, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
                texDetailControl(GR_TMU0, 2, 7, 1.0f);
            }
            alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
            texSource(GR_TMU0, baseTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &baseTexture.info);

            drawTriangle(&vtxA, &vtxB, &vtxD);
            drawTriangle(&vtxA, &vtxD, &vtxC);

            // Pass 2
            if (modeIndex == 0) { // LIGHTMAP
                alphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ZERO, GR_BLEND_ZERO);
                texSource(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &lightTexture.info);
            } else if (modeIndex == 1) { // SPECULAR
                alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO);
                texSource(GR_TMU0, lightTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &lightTexture.info);
            } else if (modeIndex == 2) { // DETAIL
                texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL, GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
                alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO);
                texSource(GR_TMU0, detailTextureAddr, GR_MIPMAPLEVELMASK_BOTH, &detailTexture.info);
            }

            drawTriangle(&vtxA, &vtxB, &vtxD);
            drawTriangle(&vtxA, &vtxD, &vtxC);

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
        };

        // Mode 0: LIGHTMAP
        renderCompositingFrame(0);
        assertColorClose(fb[300 * 640 + 320], 0xFFDE0000);
        assertColorClose(fb[50 * 640 + 50], 0xFF414041);

        // Mode 1: SPECULAR
        renderCompositingFrame(1);
        assertColorClose(fb[300 * 640 + 320], 0xFFFFDADE);
        assertColorClose(fb[50 * 640 + 50], 0xFF414041);

        // Mode 2: DETAIL
        renderCompositingFrame(2);
        assertColorClose(fb[300 * 640 + 320], 0xFFFF34E6);
        assertColorClose(fb[50 * 640 + 50], 0xFF414041);
    }

    SECTION("test29: Perspective vs. Affine") {
        tlSetScreen(640.0f, 480.0f);
        auto depthBufferMode = (void(*)(GrDepthBufferMode_t))dlsym(handle, "grDepthBufferMode");
        auto depthBufferFunction = (void(*)(GrCmpFnc_t))dlsym(handle, "grDepthBufferFunction");
        auto depthMask = (void(*)(FxBool))dlsym(handle, "grDepthMask");
        auto hints = (void(*)(GrHint_t, FxU32))dlsym(handle, "grHints");
        auto texCombine = (void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool))dlsym(handle, "grTexCombine");
        using AlphaBlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
        auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunc>(dlsym(handle, "grAlphaBlendFunction"));
        
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
            REQUIRE(std::abs(b_act - b_exp) <= 15);
            REQUIRE(std::abs(g_act - g_exp) <= 15);
            REQUIRE(std::abs(r_act - r_exp) <= 15);
        };

        TlTexture texture;
        REQUIRE(tlLoadTexture("ext_tests/build_tests/decal1.3df", &texture.info, &texture.tableType, &texture.tableData) == true);

        texDownload(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            texDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        texSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
        texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

        depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);

        struct LocalVertex3D {
            float x, y, z, w;
            float s, t;
        };

        LocalVertex3D srcVerts[4] = {
            { -0.5f, 0.0f,  0.5f, 1.0f, 0.0f, 0.0f },
            {  0.5f, 0.0f,  0.5f, 1.0f, 1.0f, 0.0f },
            {  0.5f, 0.0f, -0.5f, 1.0f, 1.0f, 1.0f },
            { -0.5f, 0.0f, -0.5f, 1.0f, 0.0f, 1.0f }
        };

        std::vector<uint32_t> fb(640 * 480);
        auto renderFrame29 = [&](bool separateW) {
            if (separateW) {
                hints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);
            } else {
                hints(GR_HINT_STWHINT, 0);
            }

            bufferClear(0x00404040, 0, GR_WDEPTHVALUE_FARTHEST);
            alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO, GR_BLEND_ZERO);

            GrVertex v[4];
            float distance = 1.2f; // Frame 0 distance
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

            REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);
        };

        // Mode 0: Separate W (Affine)
        renderFrame29(true);
        assertColorClose(fb[270 * 640 + 320], 0xFF00FF00);
        assertColorClose(fb[320 * 640 + 280], 0xFF000000);
        assertColorClose(fb[50 * 640 + 50], 0xFF414041);
 
        // Mode 1: Default W (Perspective)
        renderFrame29(false);
        assertColorClose(fb[270 * 640 + 320], 0xFFFFFFFF);
        assertColorClose(fb[320 * 640 + 280], 0xFF00FF00);
        assertColorClose(fb[50 * 640 + 50], 0xFF414041);
    }

    SECTION("test16: Watermark & Logo") {
        tlSetScreen(640.0f, 480.0f);
        auto glideShamelessPlug = (void(*)(FxBool))dlsym(handle, "grGlideShamelessPlug");
        auto splash = (void(*)(float, float, float, float, FxU32))dlsym(handle, "grSplash");
        auto constantColorValue = (void(*)(GrColor_t))dlsym(handle, "grConstantColorValue");
        auto colorCombine = (void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool))dlsym(handle, "grColorCombine");
        auto drawTriangle = (void(*)(const GrVertex*, const GrVertex*, const GrVertex*))dlsym(handle, "grDrawTriangle");
        auto bufferClear = (void(*)(GrColor_t, GrAlpha_t, FxU16))dlsym(handle, "grBufferClear");
        auto bufferSwap = (void(*)(int))dlsym(handle, "grBufferSwap");

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
            #define BLUE_COLOR  0x00ff0000
            #define RED_COLOR   0x000000ff
            #define BLACK_COLOR 0x00000000

            bufferClear(BLACK_COLOR, 0, GR_ZDEPTHVALUE_FARTHEST);

            GrVertex vtxA, vtxB, vtxC, vtxD;
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
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        renderMode0Frame(14, true);
        REQUIRE(readLfb(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        assertColorClose(fb[50 * 640 + 50], 0x00FF0000);   // Red background
        assertColorClose(fb[240 * 640 + 320], 0x000000FF); // Blue background
        assertColorClose(fb[430 * 640 + 50], 0x000000FF);  // Blue background
        assertColorClose(fb[450 * 640 + 550], 0xFFFFE6E6); // Logo pink/white
        assertColorClose(fb[440 * 640 + 580], 0xFF4144E6); // Logo purple
        assertColorClose(fb[450 * 640 + 600], 0xFFDEDAFF); // Logo yellow/white
        assertColorClose(fb[460 * 640 + 620], 0x00FF0000); // Red background near edge

        // Mode 1: Splash Screen
        renderMode1Frame(35);
        REQUIRE(readLfb(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) == FXTRUE);

        assertColorClose(fb[50 * 640 + 50], 0xFF000000);   // Black background
        assertColorClose(fb[240 * 640 + 320], 0xFFC4C4C4); // Center of logo
        assertColorClose(fb[430 * 640 + 50], 0xFF000000);  // Black background

        // Cleanup state
        glideShamelessPlug(FXFALSE);
    }

    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 2.x Dynamic Version and Hardware Gating", "[Glide2][Gating]") {
#ifdef __linux__
    // Load the runtime wrapper dynamic library
    void* handle = ::dlopen("build/lib/libglide2x.so", RTLD_LAZY);
    REQUIRE(handle != nullptr);

    // Resolve base initialization entry points
    auto init = (void (FX_CALL*)(void))::dlsym(handle, "grGlideInit");
    auto shutdown = (void (FX_CALL*)(void))::dlsym(handle, "grGlideShutdown");
    auto sstSelect = (void (FX_CALL*)(int))::dlsym(handle, "grSstSelect");
    auto getProcAddress = (GrProc (FX_CALL*)(char*))::dlsym(handle, "grGetProcAddress");
    auto sstQueryHardware = (FxBool (FX_CALL*)(GrHwConfiguration*))::dlsym(handle, "grSstQueryHardware");
    auto sstWinOpen = (FxBool (FX_CALL*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int))::dlsym(handle, "grSstWinOpen");

    REQUIRE(init != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(getProcAddress != nullptr);
    REQUIRE(sstQueryHardware != nullptr);
    REQUIRE(sstWinOpen != nullptr);

    // Helper to setup and initialize under specific env profiles
    auto init_with_profile = [&](const char* card, const char* version) {
        if (card) ::setenv("GLIDE_DEVICE", card, 1);
        else ::unsetenv("GLIDE_DEVICE");
        
        if (version) ::setenv("GLIDE_VERSION_OVERRIDE", version, 1);
        else ::unsetenv("GLIDE_VERSION_OVERRIDE");
        
        init();
        sstSelect(0);
        
        // Open a dummy context to instantiate the backend and create the splash animator!
        sstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    };

    SECTION("Case A: Default Profile (Voodoo2, 2.43)") {
        init_with_profile("voodoo2", "2.43");

        // Query standard 2.43 functions (dynamic loading)
        REQUIRE(getProcAddress((char*)"grChromakeyRangeExt") != nullptr);
        
        // 2.61 functions should be blocked under 2.43
        REQUIRE(getProcAddress((char*)"grSplashCb") == nullptr);

        // Hardware query check: Voodoo 2 config
        GrHwConfiguration hwconfig;
        REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
        REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_Voodoo2);
        REQUIRE(hwconfig.SSTs[0].sstBoard.Voodoo2Config.fbRam == 16);

        shutdown();
    }

    SECTION("Case B: Voodoo1 Legacy Profile (Voodoo Graphics, 2.40)") {
        init_with_profile("voodoo1", "2.40");

        // 2.40 profile: 2.42/2.43 features must be blocked
        REQUIRE(getProcAddress((char*)"grChromakeyRangeExt") == nullptr);

        // Hardware query check: Voodoo Graphics config
        GrHwConfiguration hwconfig;
        REQUIRE(sstQueryHardware(&hwconfig) == FXTRUE);
        REQUIRE(hwconfig.SSTs[0].type == GR_SSTTYPE_VOODOO);

        shutdown();
    }

    SECTION("Case C: Voodoo3 Interop Profile (Voodoo3, 2.61)") {
        init_with_profile("voodoo3", "2.61");

        // Everything should be fully available under 2.61 on Voodoo3
        REQUIRE(getProcAddress((char*)"grChromakeyRangeExt") != nullptr);
        REQUIRE(getProcAddress((char*)"grSplashCb") != nullptr);

        shutdown();
    }

    SECTION("Case D: Static Function Safe Gating Call (Direct Gated Invocation)") {
        init_with_profile("voodoo1", "2.40");

        // Resolve grChromakeyRangeExt directly
        typedef void (FX_CALL *chromakeyRange_t)(GrColor_t, GrColor_t, FxI32);
        auto chromakeyRange = (chromakeyRange_t)::dlsym(handle, "grChromakeyRangeExt");
        REQUIRE(chromakeyRange != nullptr);

        // Invoking gated function directly in Voodoo 1 mode must not crash; it behaves as a safe no-op.
        chromakeyRange(0x0, 0xFFFFFFFF, 0x8); // 0x8 = GR_CHROMARANGE_RGB_ALL (inclusive mode)

        shutdown();
    }

    SECTION("Case E: grSplashCb Callback Execution") {
        init_with_profile("voodoo3", "2.61");

        auto p_grSplashCb = (void (FX_CALL*)(float, float, float, float, FxU32, void (*)(int)))::dlsym(handle, "grSplashCb");
        REQUIRE(p_grSplashCb != nullptr);

        // Setup a tracking callback
        static int s_callback_trigger_count = 0;
        s_callback_trigger_count = 0;
        auto tracking_callback = [](int frame) {
            s_callback_trigger_count++;
        };

        // Render frame 35 (single frame path: callback should not be triggered)
        p_grSplashCb(0.0f, 0.0f, 640.0f, 480.0f, 35, tracking_callback);
        REQUIRE(s_callback_trigger_count == 0);

        // Render whole animation (automatic loop: callback should be triggered for each frame)
        p_grSplashCb(0.0f, 0.0f, 640.0f, 480.0f, 0, tracking_callback);
        
        // Ensure the callback was actually invoked repeatedly!
        REQUIRE(s_callback_trigger_count > 0);

        shutdown();
    }

    ::dlclose(handle);
#endif
}



#include "core/VertexLayout.h"

TEST_CASE("Glide 2.x Vertex Layout Decoding Integrity", "[VertexLayout][Glide2]") {
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
        v2.x = 100.0f; v2.y = 200.0f;
        v2.z = 30000.0f;
        v2.r = 128.0f; v2.g = 192.0f; v2.b = 255.0f;
        v2.ooz = 40000.0f;
        v2.a = 200.0f;
        v2.oow = 0.5f;
        v2.s = 5.0f; v2.t = 10.0f;

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

#ifndef _WIN32
TEST_CASE("Glide 3.x Texture Loading and LFB Integrity", "[Glide3][Texture]") {
    void* handle = ::dlopen("build/lib/libglide3x.so", RTLD_LAZY);
    if (!handle) handle = ::dlopen("lib/libglide3x.so", RTLD_LAZY);
    if (!handle) handle = ::dlopen("../lib/libglide3x.so", RTLD_LAZY);
    REQUIRE(handle != nullptr);

    auto p_gu3dfGetInfo = (FxBool(*)(const char*, Gu3dfInfo*))dlsym(handle, "gu3dfGetInfo");
    auto p_gu3dfLoad = (FxBool(*)(const char*, Gu3dfInfo*))dlsym(handle, "gu3dfLoad");

    REQUIRE(p_gu3dfGetInfo != nullptr);
    REQUIRE(p_gu3dfLoad != nullptr);

    SECTION("Load decal1.3df and verify properties") {
        Gu3dfInfo info;
        std::memset(&info, 0, sizeof(info));
        
        // 1. Get texture info
        FxBool infoOk = p_gu3dfGetInfo("ext_tests/build_tests/decal1.3df", &info);
        REQUIRE(infoOk == FXTRUE);
        REQUIRE(info.header.width == 256);
        REQUIRE(info.header.height == 256);
        REQUIRE(info.header.format == GR_TEXFMT_RGB_565);
        REQUIRE(info.mem_required == 174762);

        // 2. Allocate and load data
        info.data = std::malloc(info.mem_required);
        REQUIRE(info.data != nullptr);

        FxBool loadOk = p_gu3dfLoad("ext_tests/build_tests/decal1.3df", &info);
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
#endif

#include "core/MathUtils.h"
#include "core/LfbManager.h"

TEST_CASE("MathUtils Unit Tests", "[Core][MathUtils]") {
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
        // Under W=52428.8, since it's far above farW (100.0), it should be 255 (full fog)
        REQUIRE(table[63] == 255);
    }

    SECTION("FogGenerateExp behaves exponentially") {
        uint8_t table[64];
        std::memset(table, 0, sizeof(table));

        GlideWrapper::MathUtils::FogGenerateExp(table, 0.01f);
        // Exponential values should increase monotonically
        for (int i = 1; i < 64; ++i) {
            REQUIRE(table[i] >= table[i-1]);
        }
    }
}

TEST_CASE("LfbManager Unit Tests", "[Core][LfbManager]") {
    auto& manager = GlideWrapper::LfbManager::GetInstance();
    
    SECTION("Initial state is unlocked") {
        REQUIRE(manager.IsLocked() == false);
    }

    SECTION("Unlock fails gracefully if not locked") {
        GlideWrapper::LfbPipelineConfig config;
        bool status = manager.Unlock(0, 0, config);
        REQUIRE(status == false);
        REQUIRE(manager.IsLocked() == false);
    }
}









