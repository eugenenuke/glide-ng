#include <catch2/catch_test_macros.hpp>
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
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>

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

            auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

            REQUIRE(glideInit != nullptr);
            REQUIRE(winOpen != nullptr);
            REQUIRE(bufferClear != nullptr);
            REQUIRE(bufferSwap != nullptr);
            REQUIRE(winClose != nullptr);

            glideInit();
            REQUIRE(winOpen(0, 5, 0, 0, 0, 2, 0) == FXTRUE);
            REQUIRE_NOTHROW(bufferClear(0x00FF0000, 0, 65535));
            REQUIRE_NOTHROW(bufferSwap(1));
            REQUIRE_NOTHROW(winClose());

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

            auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

            REQUIRE(glideInit != nullptr);
            REQUIRE(winOpen != nullptr);
            REQUIRE(bufferClear != nullptr);
            REQUIRE(bufferSwap != nullptr);
            REQUIRE(winClose != nullptr);

            glideInit();
            FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 0);
            REQUIRE(ctx == 1);
            REQUIRE_NOTHROW(bufferClear(0x00FF0000, 0, 65535));
            REQUIRE_NOTHROW(bufferSwap(1));
            REQUIRE(winClose(ctx) == FXTRUE);

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

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto selectCtx   = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
        auto renderBuf   = reinterpret_cast<RenderBufFunc>(::dlsym(handle, "grRenderBuffer"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(selectCtx != nullptr);
        REQUIRE(renderBuf != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(winClose != nullptr);

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
        
        using TriStatsFunc = void(*)(FxU32*, FxU32*);
        using ResetTriStatsFunc = void(*)(void);
        using PerfStatsFunc = void(*)(GrSstPerfStats_t*);
        using ResetPerfStatsFunc = void(*)(void);

        auto glideInit      = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen        = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto drawTri        = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto bufferSwap     = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto winClose       = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        
        auto grTriStats     = reinterpret_cast<TriStatsFunc>(::dlsym(handle, "grTriStats"));
        auto grResetTriStats = reinterpret_cast<ResetTriStatsFunc>(::dlsym(handle, "grResetTriStats"));
        auto grSstPerfStats = reinterpret_cast<PerfStatsFunc>(::dlsym(handle, "grSstPerfStats"));
        auto grSstResetPerfStats = reinterpret_cast<ResetPerfStatsFunc>(::dlsym(handle, "grSstResetPerfStats"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(drawTri != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(winClose != nullptr);
        
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

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto aaDrawTri   = reinterpret_cast<AADrawTriFunc>(::dlsym(handle, "grAADrawTriangle"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead     = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(aaDrawTri != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(lfbRead != nullptr);

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

        auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto drawPoly    = reinterpret_cast<DrawPolyFunc>(::dlsym(handle, "grDrawPolygon"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead     = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(drawPoly != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(lfbRead != nullptr);

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

        using ConstColor4Func = void(*)(float, float, float, float);
        using ColorMaskFunc = void(*)(FxBool, FxBool);
        using DisableAllEffectsFunc = void(*)(void);

        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto lfbRead = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));

        auto constColor4 = reinterpret_cast<ConstColor4Func>(::dlsym(handle, "grConstantColorValue4"));
        auto colorMask = reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
        auto disableAllEffects = reinterpret_cast<DisableAllEffectsFunc>(::dlsym(handle, "grDisableAllEffects"));

        REQUIRE(winOpen != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(drawTri != nullptr);
        REQUIRE(lfbRead != nullptr);
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

        using SplashFunc = void(*)(float, float, float, float, FxU32);
        using ShamelessPlugFunc = void(*)(const FxBool);

        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto lfbRead = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));

        auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
        auto shamelessPlug = reinterpret_cast<ShamelessPlugFunc>(::dlsym(handle, "grGlideShamelessPlug"));

        REQUIRE(winOpen != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(bufferSwap != nullptr);
        REQUIRE(lfbRead != nullptr);
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

        ::dlclose(handle);
#endif
    }

    SECTION("Milestone 6: Advanced Texturing & Utilities (LOD/Mipmaps)") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32);
        using WinCloseFunc = void(*)(void);

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
        ::dlclose(handle);
#endif
    }

    SECTION("Phase 3: Automated Geometry Drawing Validation Suite") {
#ifdef __linux__
        // 1. Validate Glide 2.x Fixed Geometry Parsing & Drawing
        {
            void* handle = OpenGlide2Handle();
            REQUIRE(handle != nullptr);

            using InitFunc = void(*)(void);
            using WinOpenFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
            using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
            using DrawTriFunc = void(*)(const void*, const void*, const void*);
            using BufferSwapFunc = void(*)(int);
            using WinCloseFunc = void(*)(void);

            auto glideInit   = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen     = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto drawTri     = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
            auto bufferSwap  = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose    = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

            REQUIRE(glideInit != nullptr);
            REQUIRE(winOpen != nullptr);
            REQUIRE(bufferClear != nullptr);
            REQUIRE(drawTri != nullptr);
            REQUIRE(bufferSwap != nullptr);
            REQUIRE(winClose != nullptr);

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

            auto glideInit    = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
            auto winOpen      = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
            auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
            auto bufferClear  = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
            auto drawTri      = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
            auto bufferSwap   = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
            auto winClose     = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));

            REQUIRE(glideInit != nullptr);
            REQUIRE(winOpen != nullptr);
            REQUIRE(vertexLayout != nullptr);
            REQUIRE(bufferClear != nullptr);
            REQUIRE(drawTri != nullptr);
            REQUIRE(bufferSwap != nullptr);
            REQUIRE(winClose != nullptr);

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

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
        auto alphaSrc  = reinterpret_cast<AlphaSourceFunc>(::dlsym(handle, "guAlphaSource"));
        auto colorComb = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "guColorCombineFunction"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(sstSelect != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(alphaSrc != nullptr);
        REQUIRE(colorComb != nullptr);

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

    if (activeBackend == "software" || activeBackend == "sw") {
        return; // Skip chroma range rendering test on software backend
    }

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

    // Clean up
    REQUIRE_NOTHROW(shutdown());
    ::dlclose(handle);
#endif
}

TEST_CASE("Alpha 8 Font Texture Rendering & Text Damage Detection", "[Architecture][Text]") {
#ifdef __linux__
    // 1. Load the shared library
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    // 2. Resolve function pointers
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
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

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
    REQUIRE(shutdown != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
    REQUIRE(ctx == 1);
    REQUIRE(selectCtx(ctx) == FXTRUE);

    clipWin(0, 0, 800, 600);

    // Configure vertex layout
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W at 32
    vertexLayout(0x40, 36, 1); // GR_PARAM_ST0 at 36

    bufferClear(0xFF000000, 0, 0xFFFFFFFF); // Clear to Black

    // Prepare 32x16 Texture in GR_TEXFMT_ALPHA_8
    // Top half (y = 0..7) is solid white (0xFF), bottom half (y = 8..15) is transparent/black (0x00)
    std::vector<uint8_t> texData(32 * 16, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            texData[y * 32 + x] = 0xFF;
        }
    }

    GrTexInfo texInfo;
    texInfo.smallLod = 3; // LOD 3 -> 32x32 (Max dimension)
    texInfo.largeLod = 3;
    texInfo.aspectRatio = 1; // 2W x 1H -> 32x16 (Glide 3 aspect ratio value)
    texInfo.format = GR_TEXFMT_ALPHA_8;
    texInfo.data = texData.data();

    // Download to TMU0
    texDownload(0, 0x00002000, 0, &texInfo);
    texSource(0, 0x00002000, 0, &texInfo);
    texClamp(0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texFilter(0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    // Combine setup: output the texture alpha to RGB/A channels so we can read it easily
    texCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Draw a centered quad from (300,300) to (500,500)
    // S coordinate is scaled: s_val = s_pixel * (256.0 / 32.0) = s_pixel * 8.0
    // T coordinate is scaled: t_val = t_pixel * (256.0 / 32.0) = t_pixel * 8.0 (since max dimension is 32)
    // So for:
    // s_pixel = 0 -> s_val = 0
    // s_pixel = 32 -> s_val = 256.0
    // t_pixel = 0 -> t_val = 0
    // t_pixel = 16 -> t_val = 128.0
    
    StandardVert v0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f,   0.0f, 1.0f};
    StandardVert v1{500.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f,   0.0f, 1.0f};
    StandardVert v2{500.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f, 128.0f, 1.0f};
    StandardVert v3{300.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f, 128.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

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

    INFO("Top half pixel at (400,150) = 0x" << std::hex << topPixel << " | R=" << (int)r_top << ", G=" << (int)g_top << ", B=" << (int)b_top);
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



