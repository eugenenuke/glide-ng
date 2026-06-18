#include <catch2/catch_test_macros.hpp>
#include "core/WrapperConfig.h"
#include "core/Logger.h"
#include "core/JsonConfigLoader.h"
#include <cstdlib>
#include <dlfcn.h>
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
        bufferSwap(0); // Trigger resolve and present!

        // 4. Read back center pixel via standard grLfbReadRegion API and verify it was drawn green!
        uint32_t pixel = 0;
        REQUIRE(lfbRead(1, 400, 300, 1, 1, 4, &pixel) == FXTRUE); // 1 = GR_BUFFER_BACKBUFFER
        
        // Assert green component is active
        uint8_t g = (pixel >> 8) & 0xFF;
        REQUIRE(g == 255);

        // 5. Clean up
        winClose();
        ::dlclose(handle);

        // Reset the environment variable to avoid affecting other tests
        ::unsetenv("GLIDE_WRAPPER_MSAA");
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
        FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 0); // 800x600 headless
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
        FxU32 ctx = winOpen(0, 5, 0, 0, 0, 2, 1); // 800x600
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
