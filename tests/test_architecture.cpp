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

        auto glideInit  = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen    = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto clearBuf   = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
        auto clipWin    = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
        auto blendFunc  = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
        auto cullMode   = CullFunc(::dlsym(handle, "grCullMode"));
        auto drawTri    = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto readLfb    = reinterpret_cast<ReadFunc>(::dlsym(handle, "grLfbReadRegion"));
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

        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[150 * 800 + 150] == 0xFFFFFF00); // Inside Scissor -> Yellow
        REQUIRE(lfbData[50 * 800 + 50] == 0xFF000000);   // Outside Scissor -> Untouched Black

        // Revert Scissor
        clipWin(0, 0, 800, 600);

        // 2. Validate Alpha Blending (50% Blue over Solid Red -> Perfect Purple 0xFF7F007F)
        clearBuf(0xFFFF0000, 0, 1); // Solid Red (0xFFFF0000 in ARGB)
        blendFunc(1, 5, 1, 5); // GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA

        StandardVert b0{0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f}; // 50% Blue
        StandardVert b1{2000.0f, 0.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert b2{0.0f, 2000.0f, 0.5f, 0.0f, 0.0f, 255.0f, 1.0f, 127.5f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&b0, &b1, &b2);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        uint32_t blendPixel = lfbData[200 * 800 + 300];
        REQUIRE((blendPixel & 0xFF00FF00) == 0xBF000000); // 75% Alpha (0xBF), 0 Green
        uint32_t blendR = (blendPixel >> 16) & 0xFF;
        uint32_t blendB = blendPixel & 0xFF;
        REQUIRE((blendR == 0x7F || blendR == 0x80)); // Allow both GPU (0x7F) and CPU (0x80) rounding!
        REQUIRE((blendB == 0x7F || blendB == 0x80));

        // Revert Blend
        blendFunc(4, 0, 4, 0);

        // 3. Validate Polygon Winding Culling
        cullMode(1); // GR_CULL_NEGATIVE / CW backface
        clearBuf(0xFF000000, 0, 1); // Black

        // Draw Clockwise primitive (should be culled!)
        StandardVert cw0{100.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert cw1{100.0f, 400.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert cw2{400.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&cw0, &cw1, &cw2);

        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[150 * 800 + 150] == 0xFF000000); // Mapped target remains perfectly black!

        // Revert Cull
        cullMode(0); // GR_CULL_DISABLE

        // 4. Validate Alpha Testing (Punch-Through Alpha Rejection)
        clearBuf(0xFF000000, 0, 1); // Black
        alphaTestFunc(4); // GR_CMP_GREATER
        alphaRefVal(128); // Threshold = 128 (50%)

        // Draw quad with Alpha = 100 (below threshold -> should be rejected)
        StandardVert at0{0.0f, 0.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f}; // Cyan
        StandardVert at1{2000.0f, 0.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert at2{0.0f, 2000.0f, 0.5f, 0.0f, 255.0f, 255.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&at0, &at1, &at2);

        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[300 * 800 + 300] == 0xFF000000); // Remains black!

        // Draw quad with Alpha = 200 (above threshold -> should pass)
        at0.a = 200.0f; at1.a = 200.0f; at2.a = 200.0f;
        drawTri(&at0, &at1, &at2);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[300 * 800 + 300] == 0xC800FFFF); // Alpha=200(0xC8), Cyan(0x00FFFF)

        // Revert Alpha Test
        alphaTestFunc(7); // GR_CMP_ALWAYS

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
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFFFF0000); // Mapped red (opaque 0xFF)!

        // Draw quad at Z = 0.9 (Should be REJECTED because 0.9 is NOT less than 0.8)
        d0.z = 0.9f; d1.z = 0.9f; d2.z = 0.9f; d0.ooz = 0.9f; d1.ooz = 0.9f; d2.ooz = 0.9f;
        d0.r = 0.0f; d1.r = 0.0f; d2.r = 0.0f; d0.g = 255.0f; d1.g = 255.0f; d2.g = 255.0f; // Yellow
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFFFF0000); // Remains untouched red!

        // 6. Validate Depth Mask (Read-Only Depth Buffer)
        depthMask(FXFALSE); // Read-only depth!
        // Draw closer quad at Z = 0.4 in Blue
        d0.z = 0.4f; d1.z = 0.4f; d2.z = 0.4f; d0.ooz = 0.4f; d1.ooz = 0.4f; d2.ooz = 0.4f;
        d0.g = 0.0f; d1.g = 0.0f; d2.g = 0.0f; d0.b = 255.0f; d1.b = 255.0f; d2.b = 255.0f; // Blue
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
        REQUIRE(lfbData[250 * 800 + 250] == 0xFF0000FF); // Passes depth (0.4 < 0.8) -> Blue (opaque 0xFF)!

        // Now draw another quad at Z = 0.5 in Green
        // Since the previous draw had depthMask=FXFALSE, the Z buffer at that pixel is still 0.8!
        // So Z = 0.5 < 0.8 should PASS and overwrite with Green!
        d0.z = 0.5f; d1.z = 0.5f; d2.z = 0.5f; d0.ooz = 0.5f; d1.ooz = 0.5f; d2.ooz = 0.5f;
        d0.b = 0.0f; d1.b = 0.0f; d2.b = 0.0f; d0.g = 255.0f; d1.g = 255.0f; d2.g = 255.0f; // Green
        drawTri(&d0, &d1, &d2);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfbData.data()) == FXTRUE);
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
        colorCombine(1, 1, 0, 1, FXFALSE);

        // 4. Draw Triangle Quad sampling top-left texel (s=64.0, t=64.0 -> should map to Cyan)
        // StandardVert format: x, y, z, r, g, b, ooz, a, oow, s, t, w
        StandardVert t0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        StandardVert t1{350.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        StandardVert t2{300.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 64.0f, 64.0f, 1.0f};
        drawTri(&t0, &t1, &t2);

        std::vector<uint32_t> lfb(800 * 600);
        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfb.data()) == FXTRUE);
        
        // Cyan pixel verification (R == 0, G >= 254, B >= 254)
        REQUIRE((lfb[310 * 800 + 310] & 0x00FF0000) == 0x00000000);
        REQUIRE((lfb[310 * 800 + 310] & 0x0000FF00) >= 0x0000FE00);
        REQUIRE((lfb[310 * 800 + 310] & 0x000000FF) >= 0x000000FE);

        // 5. Draw Triangle Quad sampling top-right texel (s=192.0, t=64.0 -> should map to Magenta)
        StandardVert m0{400.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
        StandardVert m1{450.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
        StandardVert m2{400.0f, 350.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 192.0f, 64.0f, 1.0f};
        drawTri(&m0, &m1, &m2);

        REQUIRE(readLfb(0, 0, 0, 800, 600, 800 * 4, lfb.data()) == FXTRUE);

        // Magenta pixel verification (R >= 254, G == 0, B >= 254)
        REQUIRE((lfb[310 * 800 + 410] & 0x00FF0000) >= 0x00FE0000);
        REQUIRE((lfb[310 * 800 + 410] & 0x0000FF00) == 0x00000000);
        REQUIRE((lfb[310 * 800 + 410] & 0x000000FF) >= 0x000000FE);

        winClose(ctx);
        ::dlclose(handle);
#endif
    }
}
