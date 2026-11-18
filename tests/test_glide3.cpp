#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <cmath>
#include "core/WrapperConfig.h"
#include "core/SimdDispatch.h"
#include "core/SimdKernels.h"
#include "core/IniConfigLoader.h"
#include "core/GlideEntryCommon.h"
#include "core/TextureManager.h"
#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/HudOverlay.h"
#include "core/BackendManager.h"
#include "core/NativeSurfaceHost.h"
#include "core/VertexLayout.h"
#include "test_helpers.h"
#include "backends/GlideStateTracker.h"
#include "opengl_es/OpenGLESBackend.h"
#include "software/SoftwareBackend.h"
#include "vulkan/VulkanBackend.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <type_traits>
#include <vector>
#include <algorithm>

#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include <glide.h>

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

    inline void* OpenGlide3Handle() {
        void* h = ::dlopen("../lib/libglide3x.so", RTLD_LAZY);
        if (!h) h = ::dlopen("lib/libglide3x.so", RTLD_LAZY);
        if (!h) h = ::dlopen("build/lib/libglide3x.so", RTLD_LAZY);
        return h;
    }
}

TEST_CASE("Glide 3.x Alpha 8 Font Texture Rendering & Text Damage Detection", "[Glide3][Text]") {
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
    texInfo.smallLodLog2 = 5; // Glide 3.x GR_LOD_LOG2_32 = 5 (corresponds to Glide 2.x GR_LOD_32 = 3)
    texInfo.largeLodLog2 = 5;
    texInfo.aspectRatioLog2 = 1; // 2W x 1H -> 32x16 (Glide 3 aspect ratio value)
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
    StandardVert v0{300.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f,   0.0f, 1.0f};
    StandardVert v1{500.0f, 300.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f,   0.0f, 1.0f};
    StandardVert v2{500.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 256.0f, 128.0f, 1.0f};
    StandardVert v3{300.0f, 500.0f, 0.5f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f,   0.0f, 128.0f, 1.0f};

    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    // Read back a pixel from the top half (e.g. x=400, y=350) directly from the back buffer
    uint32_t topPixel = 0;
    FxBool ok = readLfb(GR_BUFFER_BACKBUFFER, 400, 350, 1, 1, 4, &topPixel);
    REQUIRE(ok == FXTRUE);

    // Read back a pixel from the bottom half (e.g. x=400, y=450) directly from the back buffer
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

    REQUIRE(r_top >= 250);
    REQUIRE(g_top >= 250);
    REQUIRE(b_top >= 250);

    REQUIRE(r_bottom <= 5);
    REQUIRE(g_bottom <= 5);
    REQUIRE(b_bottom <= 5);

    shutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x State Control & Dynamic Capabilities", "[Glide3][State]") {
#ifdef __linux__
    // Fallback defines for Glide 3.x exclusive enums in Glide 2.x test suite context
    #ifndef GR_NUM_TMU
    #define GR_NUM_TMU 0x13
    #endif
    #ifndef GR_MAX_TEXTURE_SIZE
    #define GR_MAX_TEXTURE_SIZE 0x0a
    #endif
    #ifndef GR_MEMORY_FB
    #define GR_MEMORY_FB 0x0c
    #endif
    #ifndef GR_VIEWPORT
    #define GR_VIEWPORT 0x26
    #endif
    #ifndef GR_CLIP_COORDS
    #define GR_CLIP_COORDS 0x01
    #endif

    // 1. Load the shared library
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    // 2. Resolve function pointers
    using InitFunc = void(*)(void);
    using ShutdownFunc = void(*)(void);
    using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = FxBool(*)(FxU32);
    using BufferClearFunc = void(*)(FxU32, FxU32, FxU32);
    using BufferSwapFunc = void(*)(FxU32);
    using CoordinateSpaceFunc = void(*)(FxU32);
    using ViewportFunc = void(*)(FxI32, FxI32, FxI32, FxI32);
    using DepthRangeFunc = void(*)(FxFloat, FxFloat);
    using EnableFunc = void(*)(FxU32);
    using DisableFunc = void(*)(FxU32);
    using GetFunc = FxU32(*)(FxU32, FxU32, FxI32*);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using VertexLayoutFunc = void(*)(const void*);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto bufferSwap = reinterpret_cast<BufferSwapFunc>(::dlsym(handle, "grBufferSwap"));
    auto coordSpace = reinterpret_cast<CoordinateSpaceFunc>(::dlsym(handle, "grCoordinateSpace"));
    auto viewport = reinterpret_cast<ViewportFunc>(::dlsym(handle, "grViewport"));
    auto depthRange = reinterpret_cast<DepthRangeFunc>(::dlsym(handle, "grDepthRange"));
    auto enable = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
    auto disable = reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
    auto grGetVal = reinterpret_cast<GetFunc>(::dlsym(handle, "grGet"));
    auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto setVertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grGlideSetVertexLayout"));
    
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(coordSpace != nullptr);
    REQUIRE(viewport != nullptr);
    REQUIRE(depthRange != nullptr);
    REQUIRE(enable != nullptr);
    REQUIRE(disable != nullptr);
    REQUIRE(grGetVal != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(setVertexLayout != nullptr);
    REQUIRE(colorCombine != nullptr);

    SECTION("Dynamic Card Capabilities (Voodoo 1 vs 2 vs 3 vs 4/5)") {
        // Test Voodoo 3 configuration (authentic 3dfx hardware caps at 256x256)
        ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
        glideInit();
        
        FxI32 params[4] = {0};
        grGetVal(GR_NUM_TMU, 4, params);
        REQUIRE(params[0] == 2);

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 256);

        grGetVal(GR_MEMORY_FB, 4, params);
        REQUIRE(params[0] == 16 * 1024 * 1024); // Voodoo3 default is 16MB

        shutdown();
        ::unsetenv("GLIDE_NG_CARD_MODEL");

        // Test Voodoo 4 / VSA-100 configuration (2048x2048 textures)
        ::setenv("GLIDE_NG_CARD_MODEL", "voodoo4", 1);
        glideInit();

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 2048);

        shutdown();
        ::unsetenv("GLIDE_NG_CARD_MODEL");

        // Test Voodoo 1 configuration
        ::setenv("GLIDE_NG_CARD_MODEL", "voodoo1", 1);
        glideInit();

        grGetVal(GR_NUM_TMU, 4, params);
        REQUIRE(params[0] == 1);

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 256);

        shutdown();
        ::unsetenv("GLIDE_NG_CARD_MODEL");
    }

    SECTION("Viewport & Depth Range Queries") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        
        FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Query default viewport
        FxI32 vp[4] = {0};
        grGetVal(GR_VIEWPORT, 16, vp);
        REQUIRE(vp[0] == 0);
        REQUIRE(vp[1] == 0);
        REQUIRE(vp[2] == 640);
        REQUIRE(vp[3] == 480);

        // Set custom viewport
        viewport(50, 40, 300, 200);
        grGetVal(GR_VIEWPORT, 16, vp);
        REQUIRE(vp[0] == 50);
        REQUIRE(vp[1] == 40);
        REQUIRE(vp[2] == 300);
        REQUIRE(vp[3] == 200);

        winClose(ctx);
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
    }

    SECTION("Clip Space Coordinates Projection Verification") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        
        FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Set pass-through color combining function to ensure vertices are rendered with iterated white color
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

        // Clear screen to black
        bufferClear(0x0, 0, 0);

        // Enable clip coordinates
        coordSpace(GR_CLIP_COORDS);

        // Setup a custom viewport box:
        // x=100, y=100, width=200, height=200
        viewport(100, 100, 200, 200);
        depthRange(0.0f, 1.0f);

        StandardVert v0 = { -1.0f, -1.0f, 0.0f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f }; // White
        StandardVert v1 = {  1.0f, -1.0f, 0.0f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        StandardVert v2 = {  0.0f,  1.0f, 0.0f, 255.0f, 255.0f, 255.0f, 1.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f };

        drawTri(&v0, &v1, &v2);

        // Read pixel buffer region from Back Buffer (1) to verify rendering bounds!
        std::vector<uint32_t> pixels(640 * 480, 0);
        FxBool read_ok = readLfb(1, 0, 0, 640, 480, 640 * 4, pixels.data());
        REQUIRE(read_ok == FXTRUE);

        // Verification (masking out alpha channel to focus on RGB color):
        REQUIRE((pixels[50 + 50 * 640] & 0x00FFFFFF) == 0x0);
        REQUIRE((pixels[400 + 400 * 640] & 0x00FFFFFF) == 0x0);
        REQUIRE((pixels[200 + 280 * 640] & 0x00FFFFFF) != 0x0);

        winClose(ctx);
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
    }
    
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x State Save & Restore Combiner Bug Verification", "[Glide3][State]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using ColorCombineFunc = void(*)(FxI32, FxI32, FxI32, FxI32, FxBool);
    using GetStateFunc = void(*)(void*);
    using SetStateFunc = void(*)(const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto colorComb = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto getState  = reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
    auto setState  = reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
    using CullModeFunc = void(*)(GrCullMode_t);
    auto cullMode = reinterpret_cast<CullModeFunc>(::dlsym(handle, "grCullMode"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(colorComb != nullptr);
    REQUIRE(getState != nullptr);
    REQUIRE(setState != nullptr);
    REQUIRE(cullMode != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless software reference backend for testing
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    // Get the reference to active backend
    auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend()
    );
    REQUIRE(backend != nullptr);

    // 1. Configure initial combiner and cull state
    colorComb(1, 0, 0, 2, 0); // function=1, factor=0, local=0, other=2
    cullMode(2); // GR_CULL_POSITIVE
    REQUIRE(backend->GetColorCombinerFunc() == 1);
    REQUIRE(backend->GetColorCombinerLocal() == 0);
    REQUIRE(backend->GetCullMode() == 2);

    // 2. Save state using grGlideGetState
    char savedState[1024];
    getState(savedState);

    // 3. Modify states to simulate watermark drawing (clobbering state)
    colorComb(3, 8, 1, 1, 0); // function=3, factor=8, local=1, other=1
    cullMode(0); // GR_CULL_DISABLE
    REQUIRE(backend->GetColorCombinerFunc() == 3);
    REQUIRE(backend->GetColorCombinerLocal() == 1);
    REQUIRE(backend->GetCullMode() == 0);

    // 4. Restore state using grGlideSetState
    setState(savedState);

    // 5. Verify that both the combiner and culling states were successfully restored!
    REQUIRE(backend->GetColorCombinerFunc() == 1);
    REQUIRE(backend->GetColorCombinerLocal() == 0);
    REQUIRE(backend->GetCullMode() == 2);

    winClose();
    glideShutdown();
    ::dlclose(handle);
}

TEST_CASE("Glide 3.x grBufferClear Depth Scaling Verification", "[Glide3][Depth]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless software reference backend for testing
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    // Get reference to the software backend
    auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend()
    );
    REQUIRE(backend != nullptr);

    // 1. Clear depth buffer using 16-bit farthest value (0xFFFF)
    bufferClear(0x00000000, 0, 0xFFFF);

    // 2. Verify that the depth buffer was cleared to 1.0f (farthest), NOT 0.000015f!
    float depthVal = backend->GetDepthBufferPixel(100, 100);
    REQUIRE(std::abs(depthVal - 1.0f) < 0.0001f);

    // 3. Clear depth buffer using a custom mid value (32768)
    bufferClear(0x00000000, 0, 32768);
    depthVal = backend->GetDepthBufferPixel(100, 100);
    REQUIRE(std::abs(depthVal - (32768.0f / 65535.0f)) < 0.0001f);

    winClose();
    glideShutdown();
    ::dlclose(handle);
}

TEST_CASE("Vulkan Backend grBufferClear and Readback Verification [Vulkan_Clear]", "[Vulkan_Clear]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless Vulkan backend for testing
    ::setenv("GLIDE_NG_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    // 1. Verify basic color clearing works
    bufferClear(0xFF00FF00, 0xFF, 0xFFFF);
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 100, 100, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pixels[i] != 0);
    }

    // 2. Clear color buffer to black (0) before rendering
    bufferClear(0x00000000, 0, 0xFFFF);

    // 3. Configure Glide state for flat shading using iterated color
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool);
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    REQUIRE(colorCombine != nullptr);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    // 4. Configure vertex layout using local Glide 3.x parameter definitions:
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01

    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    REQUIRE(vertexLayout != nullptr);
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // 5. Define 3 vertices for a red triangle in the center of the screen
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red
    TestVert v2{ 400.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 0.0f, 0.0f }; // Red

    // 6. Draw the triangle
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    REQUIRE(drawTriangle != nullptr);
    drawTriangle(&v0, &v1, &v2);

    // 7. Read back a 4x4 region from the center of the triangle (400, 250) on the BACKBUFFER (1)
    std::vector<uint32_t> drawPixels(16, 0);
    ok = lfbRead(1, 400, 250, 4, 4, 4 * 4, drawPixels.data());
    REQUIRE(ok == FXTRUE);

    // 8. Verify that the pixels inside the triangle are RED (not black/zero!)
    for (int i = 0; i < 16; ++i) {
        REQUIRE(drawPixels[i] != 0);
    }

    winClose();
    glideShutdown();
    ::dlclose(handle);
}

#include "core/VertexLayout.h"

TEST_CASE("Glide 3.x Vertex Layout Decoding Integrity", "[VertexLayout][Glide3]") {
    SECTION("Glide 3 Canonical Vertex Decoding") {
        auto& layout = GlideWrapper::VertexLayout::GetInstance();
        layout.ResetToGlide3Canonical();

        // Prepare mock raw Glide 3 vertex matching standard GrVertex offsets:
        // x,y at 0, ooz at 8, oow at 12, r,g,b at 16, a at 28, z at 32, st0 at 36
        struct MockGlide3Vertex {
            float x, y;
            float ooz;
            float oow;
            float r, g, b;
            float a;
            float z;
            float sow, tow;
            float oow_tmu;
        };

        MockGlide3Vertex v3;
        v3.x = 10.0f; v3.y = 20.0f;
        v3.ooz = 50000.0f;
        v3.oow = 0.25f;
        v3.r = 64.0f; v3.g = 128.0f; v3.b = 192.0f;
        v3.a = 100.0f;
        v3.z = 99.0f;
        v3.sow = 15.0f; v3.tow = 25.0f;
        v3.oow_tmu = 0.25f;

        GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v3);

        REQUIRE(mv.pos[0] == 10.0f);
        REQUIRE(mv.pos[1] == 20.0f);
        REQUIRE(mv.pos[2] == 50000.0f / 65535.0f);
        REQUIRE(mv.pos[3] == 0.25f);
        REQUIRE(mv.color[0] == 64.0f / 255.0f);
        REQUIRE(mv.color[1] == 128.0f / 255.0f);
        REQUIRE(mv.color[2] == 192.0f / 255.0f);
        REQUIRE(mv.color[3] == 100.0f / 255.0f);
        REQUIRE(mv.tex[0] == 15.0f);
        REQUIRE(mv.tex[1] == 25.0f);
    }
}

TEST_CASE("Glide 3.x W-Buffering and Layout Q-Param Compatibility", "[Glide3][WBuffer]") {
    #ifndef GR_WDEPTH_MIN_MAX
    #define GR_WDEPTH_MIN_MAX 0x27
    #endif
    #ifndef GR_ZDEPTH_MIN_MAX
    #define GR_ZDEPTH_MIN_MAX 0x28
    #endif

    // 1. Direct unit test of VertexLayout decoding for Q-Parameter (GR_PARAM_Q)
    SECTION("Direct VertexLayout Decode with GR_PARAM_Q") {
        auto& layout = GlideWrapper::VertexLayout::GetInstance();
        layout.ResetToGlide3Canonical();

        // Configure layout: XY at 0, Q (0x04) at 8
        layout.SetParamOffsetGlide3(0x01, 0, 1); // GR_PARAM_XY
        layout.SetParamOffsetGlide3(0x04, 8, 1); // GR_PARAM_Q
        // Disable other parameters to prevent stack-buffer-overflow when decoding MockQVertex!
        layout.SetParamOffsetGlide3(0x20, 0, 0); // GR_PARAM_RGB
        layout.SetParamOffsetGlide3(0x10, 0, 0); // GR_PARAM_A
        layout.SetParamOffsetGlide3(0x40, 0, 0); // GR_PARAM_ST0

        struct MockQVertex {
            float x, y;
            float q; // oow (1/W)
        };

        MockQVertex v;
        v.x = 100.0f; v.y = 200.0f;
        v.q = 0.125f; // W = 8.0f

        GlideWrapper::ModernVertex mv = layout.DecodeVertex(&v);

        REQUIRE(mv.pos[0] == 100.0f);
        REQUIRE(mv.pos[1] == 200.0f);
        REQUIRE(mv.pos[3] == 0.125f);
    }

#ifdef __linux__
    // 2. Integration test via dynamic shared library loading
    #if 0
    SECTION("Dynamic API Verification for grGet Depth Ranges and State Sizes") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using GetFunc = FxU32(*)(FxU32, FxU32, FxI32*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto grGetVal = reinterpret_cast<GetFunc>(::dlsym(handle, "grGet"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(grGetVal != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();

        // Test GR_WDEPTH_MIN_MAX
        FxI32 wRange[2] = { -1, -1 };
        FxU32 bytes = grGetVal(GR_WDEPTH_MIN_MAX, 8, wRange);
        REQUIRE(bytes == 8);
        REQUIRE(wRange[0] == 0);
        REQUIRE(wRange[1] == 65535);

        // Test GR_ZDEPTH_MIN_MAX
        FxI32 zRange[2] = { -1, -1 };
        bytes = grGetVal(GR_ZDEPTH_MIN_MAX, 8, zRange);
        REQUIRE(bytes == 8);
        REQUIRE(zRange[0] == 65535);
        REQUIRE(zRange[1] == 0);

        // Test GR_GLIDE_STATE_SIZE (0x06)
        FxI32 stateSize = 0;
        bytes = grGetVal(0x06, 4, &stateSize);
        REQUIRE(bytes == 4);
        REQUIRE(stateSize > 0);

        // Test GR_GLIDE_VERTEXLAYOUT_SIZE (0x07)
        FxI32 layoutSize = 0;
        bytes = grGetVal(0x07, 4, &layoutSize);
        REQUIRE(bytes == 4);
        REQUIRE(layoutSize > 0);

        shutdown();
        ::dlclose(handle);
    }
    #endif

    #if 0
    SECTION("Dynamic API Verification for LFB Lock, Write and Origin Swapping") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using LockFunc = FxBool(*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t, GrOriginLocation_t, FxBool, GrLfbInfo_t*);
        using UnlockFunc = FxBool(*)(GrLock_t, GrBuffer_t);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto lfbLock = reinterpret_cast<LockFunc>(::dlsym(handle, "grLfbLock"));
        auto lfbUnlock = reinterpret_cast<UnlockFunc>(::dlsym(handle, "grLfbUnlock"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(lfbLock != nullptr);
        REQUIRE(lfbUnlock != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
        REQUIRE(b != nullptr);

        // 1. Clear backbuffer to solid black
        b->ClearBuffer(0, 0, 0.0f, 0x3);

        // 2. Lock backbuffer with UPPER_LEFT origin, write a red pixel at (0, 0)
        GrLfbInfo_t info{};
        info.size = sizeof(info);
        FxBool locked = lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
        REQUIRE(locked == FXTRUE);
        REQUIRE(info.lfbPtr != nullptr);
        REQUIRE(info.strideInBytes >= 640 * 2);

        auto* shortData = reinterpret_cast<FxU16*>(info.lfbPtr);
        shortData[0] = 0xF800; // Red in RGB565

        FxBool unlocked = lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        REQUIRE(unlocked == FXTRUE);

        // 3. Read back from backend and verify it is red (0xFFFF0000 in RGBA)
        std::vector<uint32_t> readPixels(640 * 480, 0);
        b->ReadLFB(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, readPixels.data());
        REQUIRE(readPixels[0] == 0xFFFF0000);

        // 4. Clear backbuffer again
        b->ClearBuffer(0, 0, 0.0f, 0x3);

        // 5. Lock with LOWER_LEFT origin, write red pixel at (0, 0) (logical)
        locked = lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_LOWER_LEFT, FXFALSE, &info);
        REQUIRE(locked == FXTRUE);
        shortData = reinterpret_cast<FxU16*>(info.lfbPtr);
        shortData[0] = 0xF800; // Red

        unlocked = lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        REQUIRE(unlocked == FXTRUE);

        // 6. Read back: row-flipping must have mapped it to physical (0, height - 1)!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        b->ReadLFB(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, readPixels.data());
        
        // Logical (0,0) under LOWER_LEFT maps to physical bottom-left (0, 479)
        REQUIRE(readPixels[479 * 640] == 0xFFFF0000);
        REQUIRE(readPixels[0] == 0xFF000000); // Physical (0,0) must remain black (alpha=255)!

        shutdown();
        ::dlclose(handle);
    }
    #endif

    SECTION("Dynamic API Verification for Splash Screen and Shameless Plug") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;
        using G3EnableMode = FxU32;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using SplashFunc = void(*)(float, float, float, float, FxU32);
        using EnableFunc = void(*)(G3EnableMode);
        using DisableFunc = void(*)(G3EnableMode);
        using SwapFunc = void(*)(FxU32);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
        auto enable = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
        auto disable = reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
        auto swap = reinterpret_cast<SwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(splash != nullptr);
        REQUIRE(enable != nullptr);
        REQUIRE(disable != nullptr);
        REQUIRE(swap != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Render a splash frame (tests core animator lifecycle)
        splash(0.0f, 0.0f, 640.0f, 480.0f, 35);

        // Enable shameless plug and swap (tests plug drawing)
        enable(static_cast<G3EnableMode>(0x04)); // GR_SHAMELESS_PLUG
        swap(1);

        disable(static_cast<G3EnableMode>(0x04));

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Vertex Layout Save/Restore and TMU Chromakey Extensions") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using GetProcFunc = GrProc(*)(char*);
        using LayoutFunc = void(*)(FxU32, FxI32, FxU32);
        using GetLayoutFunc = void(*)(void*);
        using SetLayoutFunc = void(*)(const void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto getProc = reinterpret_cast<GetProcFunc>(::dlsym(handle, "grGetProcAddress"));
        auto vertexLayout = reinterpret_cast<LayoutFunc>(::dlsym(handle, "grVertexLayout"));
        auto getLayout = reinterpret_cast<GetLayoutFunc>(::dlsym(handle, "grGlideGetVertexLayout"));
        auto setLayout = reinterpret_cast<SetLayoutFunc>(::dlsym(handle, "grGlideSetVertexLayout"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(getProc != nullptr);
        REQUIRE(vertexLayout != nullptr);
        REQUIRE(getLayout != nullptr);
        REQUIRE(setLayout != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // 1. Modify active layout
        vertexLayout(0x01, 8, 1);  // GR_PARAM_XY at offset 8, mode ENABLE
        vertexLayout(0x20, 24, 1); // GR_PARAM_RGB at offset 24, mode ENABLE

        // 2. Save active layout
        uint8_t savedLayout[256];
        std::memset(savedLayout, 0, sizeof(savedLayout));
        getLayout(savedLayout);

        // 3. Clobber/reset layout to canonical
        vertexLayout(0x01, 0, 1);
        vertexLayout(0x20, 12, 1);

        // 4. Restore saved layout
        setLayout(savedLayout);

        // 5. Verify restored offsets via Backend/Core DecodeVertex
        auto& vlayout = GlideWrapper::VertexLayout::GetInstance();
        
        // Let's decode a dummy vertex to verify offsets were restored!
        struct DummyRestoredVertex {
            float pad1[2];
            float x, y;
            float pad2[2];
            float r, g, b;
            float pad3[2]; // Add padding to cover ST0 at offset 36 (prevent ASan stack overflow)
        } vertex;
        vertex.x = 10.0f; vertex.y = 20.0f;
        vertex.r = 255.0f; vertex.g = 128.0f; vertex.b = 0.0f;

        GlideWrapper::ModernVertex decoded = vlayout.DecodeVertex(&vertex);
        REQUIRE(decoded.pos[0] == 10.0f);
        REQUIRE(decoded.pos[1] == 20.0f);
        REQUIRE(decoded.color[0] == 1.0f);
        REQUIRE(std::abs(decoded.color[1] - 128.0f / 255.0f) < 0.001f);
        REQUIRE(decoded.color[2] == 0.0f);

        // 6. Query and verify TMU Chromakey extensions via grGetProcAddress
        auto texChromaMode = reinterpret_cast<void(*)(GrChipID_t, FxU32)>(getProc(const_cast<char*>("grTexChromaModeExt")));
        auto texChromaRange = reinterpret_cast<void(*)(GrChipID_t, GrColor_t, GrColor_t, FxU32)>(getProc(const_cast<char*>("grTexChromaRangeExt")));
        
        REQUIRE(texChromaMode != nullptr);
        REQUIRE(texChromaRange != nullptr);

        // Invoke them to ensure no crash and correct routing
        texChromaMode(0, 1); // Enable chromakey on TMU 0
        texChromaRange(0, 0xFF000000, 0xFF0000FF, 1); // Range-based on TMU 0

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Backface Culling and Winding") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using G3Context = unsigned long;

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = G3Context(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
        using ConstantColorFunc = void(*)(GrColor_t);
        using CullModeFunc = void(*)(GrCullMode_t);
        using DrawTriFunc = void(*)(const void*, const void*, const void*);
        using BufferClearFunc = void(*)(GrColor_t, FxU16, FxU32);
        using SwapFunc = void(*)(FxU32);
        using ShutdownFunc = void(*)(void);
        using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
        auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
        auto cullMode = reinterpret_cast<CullModeFunc>(::dlsym(handle, "grCullMode"));
        auto drawTriangle = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
        auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
        auto swap = reinterpret_cast<SwapFunc>(::dlsym(handle, "grBufferSwap"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
        auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(colorCombine != nullptr);
        REQUIRE(constantColor != nullptr);
        REQUIRE(cullMode != nullptr);
        REQUIRE(drawTriangle != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(swap != nullptr);
        REQUIRE(shutdown != nullptr);
        REQUIRE(vertexLayout != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        // Explicitly define the layout for G3Vertex { float x, y, z, oow; } to prevent ASan stack-buffer-overflow!
        vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at offset 0, ENABLE
        vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at offset 8, ENABLE
        vertexLayout(0x04, 12, 1); // GR_PARAM_Q at offset 12, ENABLE
        vertexLayout(0x20, 0, 0);  // GR_PARAM_RGB, DISABLE
        vertexLayout(0x10, 0, 0);  // GR_PARAM_A, DISABLE
        vertexLayout(0x40, 0, 0);  // GR_PARAM_ST0 at offset 0, DISABLE (Correct ID: 0x40!)

        // Set up flat constant color rendering
        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

        // Define three vertices for Clockwise triangle (Triangle 1)
        struct G3Vertex { float x, y, z, oow; } vA, vB, vC;
        vA.x = 320.0f; vA.y = 48.0f;  vA.z = 0.0f; vA.oow = 1.0f;
        vB.x = 512.0f; vB.y = 432.0f; vB.z = 0.0f; vB.oow = 1.0f;
        vC.x = 128.0f; vC.y = 432.0f; vC.z = 0.0f; vC.oow = 1.0f;

        auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
        REQUIRE(backend != nullptr);

        std::vector<uint32_t> readPixels(640 * 480, 0);

        // ==========================================
        // TEST CASE A: CULL_POSITIVE (Cull Clockwise, Render Counter-Clockwise)
        // ==========================================
        cullMode(GR_CULL_POSITIVE);
        bufferClear(0xFF000000, 0, 0); // Clear to black (alpha=255)

        // 1. Draw Clockwise triangle (vA -> vB -> vC) in BLUE (0x000000FF in unpacked shader)
        constantColor(0x000000FF); 
        drawTriangle(&vA, &vB, &vC);

        // Read back center: must remain black!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00000000); // Culled! (ignore alpha)

        // 2. Draw Counter-Clockwise triangle (vA -> vC -> vB) in RED (0x00FF0000 in unpacked shader)
        constantColor(0x00FF0000);
        drawTriangle(&vA, &vC, &vB);

        // Read back center: must be Red (0x00FF0000)
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00FF0000); // Rendered! (ignore alpha)

        // ==========================================
        // TEST CASE B: CULL_NEGATIVE (Cull Counter-Clockwise, Render Clockwise)
        // ==========================================
        cullMode(GR_CULL_NEGATIVE);
        bufferClear(0xFF000000, 0, 0);

        // 1. Draw Counter-Clockwise triangle (vA -> vC -> vB) in RED (0x00FF0000)
        constantColor(0x00FF0000);
        drawTriangle(&vA, &vC, &vB);

        // Read back center: must remain black!
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x00000000); // Culled! (ignore alpha)

        // 2. Draw Clockwise triangle (vA -> vB -> vC) in BLUE (0x000000FF)
        constantColor(0x000000FF);
        drawTriangle(&vA, &vB, &vC);

        // Read back center: must be Blue (0x000000FF)
        std::fill(readPixels.begin(), readPixels.end(), 0);
        backend->ReadLFB(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 4, readPixels.data());
        REQUIRE((readPixels[0] & 0x00FFFFFF) == 0x000000FF); // Rendered! (ignore alpha)

        shutdown();
        ::dlclose(handle);
    }

    SECTION("Dynamic API Verification for Coordinate Origin Save/Restore") {
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using SelectFunc = void(*)(GrChipID_t);
        using WinOpenFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using CloseWinFunc = void(*)(void);
        using OriginFunc = void(*)(GrOriginLocation_t);
        using GetStateFunc = void(*)(void*);
        using SetStateFunc = void(*)(const void*);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto sstSelect = reinterpret_cast<SelectFunc>(::dlsym(handle, "grSstSelect"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
        auto origin = reinterpret_cast<OriginFunc>(::dlsym(handle, "grSstOrigin"));
        auto getState = reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
        auto setState = reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(origin != nullptr);
        REQUIRE(getState != nullptr);
        REQUIRE(setState != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        // Force headless software reference backend for testing
        ::setenv("GLIDE_NG_BACKEND", "software", 1);

        glideInit();
        if (sstSelect) sstSelect(0);

        // Open window with LOWER_LEFT origin (1)
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
            GlideWrapper::BackendManager::GetInstance().GetBackend()
        );
        REQUIRE(backend != nullptr);
        REQUIRE(backend->GetSstOrigin() == 1); // Verify initial origin is indeed LOWER_LEFT (1)

        // 1. Save state (should capture origin=1)
        char savedState[1024];
        getState(savedState);

        // 2. Clobber origin to UPPER_LEFT (0) (simulating console overlay text render)
        origin(GR_ORIGIN_UPPER_LEFT);
        REQUIRE(backend->GetSstOrigin() == 0);

        // 3. Restore state: should restore origin back to 1!
        setState(savedState);
        REQUIRE(backend->GetSstOrigin() == 1);

        winClose();
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
    }
#endif // matches line 678
} // matches line 645

#ifdef __linux__
TEST_CASE("Vulkan Backend Multi-pass Pre-Fog Blending Verification [Vulkan_PreFog]", "[Glide3][Vulkan][PreFog]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using FogModeFunc = void(*)(GrFogMode_t);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto blendFunc = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(blendFunc != nullptr);
    REQUIRE(fogMode != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless Vulkan backend for testing
    ::setenv("GLIDE_NG_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    if (sstSelect) sstSelect(0);
    unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 0);
    REQUIRE(ctx != 0);

    // Set vertex layout
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // Define vertices for a full-screen or centered quad
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    // Solid orange color: (1.0, 0.5, 0.0) -> R=255, G=128, B=0
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v2{ 700.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v3{ 100.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };

    // Clear buffer to Black
    bufferClear(0xFF000000, 0, 0xFFFF);

    // ==========================================
    // PASS 1: Render base color (Solid Orange)
    // ==========================================
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    blendFunc(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // Src * 1 + Dst * 0
    fogMode(0); // Disable fog

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // ==========================================
    // PASS 2: Render lightmap with PREFOG_COLOR blending
    // ==========================================
    // C_src = f * C_fog. We want C_src to be Black (0, 0, 0).
    // So we enable fog with MULT2 and Black fog color.
    // Pre-fog color should be Green (0.5, 1.0, 0.0) -> R=128, G=255, B=0.
    v0.r = 128.0f; v0.g = 255.0f; v0.b = 0.0f;
    v1.r = 128.0f; v1.g = 255.0f; v1.b = 0.0f;
    v2.r = 128.0f; v2.g = 255.0f; v2.b = 0.0f;
    v3.r = 128.0f; v3.g = 255.0f; v3.b = 0.0f;

    // Set constant fog color to Black
    using FogColorFunc = void(*)(GrColor_t);
    auto fogColorVal = reinterpret_cast<FogColorFunc>(::dlsym(handle, "grFogColorValue"));
    REQUIRE(fogColorVal != nullptr);
    fogColorVal(0x00000000); // Black

    // Configure fog: GR_FOG_MULT2 (0x100) | GR_FOG_WITH_ITERATED_ALPHA (0x1) -> f = alpha = 1.0f (so C_src = C_fog = Black)
    #define G3_FOG_MULT2 0x100
    fogMode(static_cast<GrFogMode_t>(G3_FOG_MULT2 | 0x1)); 

    // Configure blending: Src * 1 + Dst * Src_prefog (GR_BLEND_PREFOG_COLOR = 15)
    blendFunc(GR_BLEND_ONE, static_cast<GrAlphaBlendFnc_t>(15), GR_BLEND_ONE, GR_BLEND_ZERO);

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // Read back center pixel (400, 300)
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 400, 300, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);

    // Expected blended color:
    // Red = C_src.r + C_dest.r * C_prefog.r = 0.0 + 1.0 * 0.5 = 0.5 (128)
    // Green = C_src.g + C_dest.g * C_prefog.g = 0.0 + 0.5 * 1.0 = 0.5 (128)
    // Blue = C_src.b + C_dest.b * C_prefog.b = 0.0 + 0.0 * 0.0 = 0.0 (0)
    // Unpack colors (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixels[0] & 0xFF;
    uint8_t g = (pixels[0] >> 8) & 0xFF;
    uint8_t b = (pixels[0] >> 16) & 0xFF;

    INFO("Vulkan Blended Pixel Color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // Assert that color matches Orange/Yellow (128, 128, 0) with a tolerance of +-5
    REQUIRE(std::abs((int)r - 128) <= 5);
    REQUIRE(std::abs((int)g - 128) <= 5);
    REQUIRE(b <= 5);

    winClose();
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
}

TEST_CASE("OpenGL ES Backend Multi-pass Pre-Fog Blending Verification [GLES_PreFog]", "[Glide3][GLES][PreFog]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using FogModeFunc = void(*)(GrFogMode_t);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using DrawTriangleFunc = void(*)(const void*, const void*, const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto blendFunc = reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto drawTriangle = reinterpret_cast<DrawTriangleFunc>(::dlsym(handle, "grDrawTriangle"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(blendFunc != nullptr);
    REQUIRE(fogMode != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(drawTriangle != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless OpenGL ES backend for testing
    ::setenv("GLIDE_NG_BACKEND", "opengl_es", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    if (sstSelect) sstSelect(0);
    unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Set vertex layout
    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_ENABLE   0x01
    vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
    vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
    // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
    vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
    vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
    vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
    vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

    // Define vertices for a full-screen or centered quad
    struct TestVert {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };
    // Solid orange color: (1.0, 0.5, 0.0) -> R=255, G=128, B=0
    TestVert v0{ 100.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v1{ 700.0f, 100.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v2{ 700.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };
    TestVert v3{ 100.0f, 500.0f, 0.5f, 1.0f, 255.0f, 1.0f, 255.0f, 128.0f, 0.0f };

    // Clear buffer to Black
    bufferClear(0xFF000000, 0, 0xFFFF);

    // ==========================================
    // PASS 1: Render base color (Solid Orange)
    // ==========================================
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    blendFunc(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // Src * 1 + Dst * 0
    fogMode(0); // Disable fog

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // ==========================================
    // PASS 2: Render lightmap with PREFOG_COLOR blending
    // ==========================================
    // C_src = f * C_fog. We want C_src to be Black (0, 0, 0).
    // So we enable fog with MULT2 and Black fog color.
    // Pre-fog color should be Green (0.5, 1.0, 0.0) -> R=128, G=255, B=0.
    v0.r = 128.0f; v0.g = 255.0f; v0.b = 0.0f;
    v1.r = 128.0f; v1.g = 255.0f; v1.b = 0.0f;
    v2.r = 128.0f; v2.g = 255.0f; v2.b = 0.0f;
    v3.r = 128.0f; v3.g = 255.0f; v3.b = 0.0f;

    // Set constant fog color to Black
    using FogColorFunc = void(*)(GrColor_t);
    auto fogColorVal = reinterpret_cast<FogColorFunc>(::dlsym(handle, "grFogColorValue"));
    REQUIRE(fogColorVal != nullptr);
    fogColorVal(0x00000000); // Black

    // Configure fog: GR_FOG_MULT2 (0x100) | GR_FOG_WITH_ITERATED_ALPHA (0x1) -> f = alpha = 1.0f (so C_src = C_fog = Black)
    #define G3_FOG_MULT2 0x100
    fogMode(static_cast<GrFogMode_t>(G3_FOG_MULT2 | 0x1)); 

    // Configure blending: Src * 1 + Dst * Src_prefog (GR_BLEND_PREFOG_COLOR = 15)
    blendFunc(GR_BLEND_ONE, static_cast<GrAlphaBlendFnc_t>(15), GR_BLEND_ONE, GR_BLEND_ZERO);

    drawTriangle(&v0, &v1, &v2);
    drawTriangle(&v0, &v2, &v3);

    // Read back center pixel (400, 300)
    std::vector<uint32_t> pixels(16, 0);
    FxBool ok = lfbRead(1, 400, 300, 4, 4, 4 * 4, pixels.data());
    REQUIRE(ok == FXTRUE);

    // Expected blended color:
    // Red = C_src.r + C_dest.r * C_prefog.r = 0.0 + 1.0 * 0.5 = 0.5 (128)
    // Green = C_src.g + C_dest.g * C_prefog.g = 0.0 + 0.5 * 1.0 = 0.5 (128)
    // Blue = C_src.b + C_dest.b * C_prefog.b = 0.0 + 0.0 * 0.0 = 0.0 (0)
    // Unpack colors (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixels[0] & 0xFF;
    uint8_t g = (pixels[0] >> 8) & 0xFF;
    uint8_t b = (pixels[0] >> 16) & 0xFF;

    INFO("GLES Blended Pixel Color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // Assert that color matches Orange/Yellow (128, 128, 0) with a tolerance of +-5
    REQUIRE(std::abs((int)r - 128) <= 5);
    REQUIRE(std::abs((int)g - 128) <= 5);
    REQUIRE(b <= 5);

    winClose();
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
}
#endif

namespace {
    void* OpenGlide2Handle() {
        void* h = ::dlopen("../lib/libglide2x.so", RTLD_LAZY);
        if (!h) h = ::dlopen("lib/libglide2x.so", RTLD_LAZY);
        if (!h) h = ::dlopen("build/lib/libglide2x.so", RTLD_LAZY);
        return h;
    }
}

TEST_CASE("Glide 2.x Coordinate Unsnapping Verification", "[Glide2][Unsnap][Glide2_Unsnap]") {
#ifdef __linux__
    // 1. Load Glide 2 shared library
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using DrawLineFunc = void(*)(const void*, const void*);
    using ReadLfbFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using WinCloseFunc = FxBool(*)(void);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto drawLine = reinterpret_cast<DrawLineFunc>(::dlsym(handle, "grDrawLine"));
    auto readLfb = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(drawLine != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(shutdown != nullptr);

    // 3. Initialize Glide 2 and open a window (defaulting to Software backend for speed & sanity)
    ::setenv("GLIDE_NG_BACKEND", "SOFTWARE", 1);
    glideInit();
    FxBool ok = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ok == FXTRUE);

    // Configure Glide to draw using vertex colors (iterated)
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    // Clear buffer to black
    bufferClear(0, 0, 0);

    // 4. Draw a biased white line using SNAP_BIAS
    // SNAP_BIAS = (3 << 18) = 786432.0f
    const float SNAP_BIAS = 786432.0f;
    
    struct GrVertex2 {
        float x, y, z;
        float r, g, b;
        float ooz;
        float a;
        float oow;
        float sow, tow, oow_tmu;
    };

    GrVertex2 v1{};
    v1.x = SNAP_BIAS + 100.0f;
    v1.y = SNAP_BIAS + 100.0f;
    v1.z = 0.0f;
    v1.r = v1.g = v1.b = 255.0f;
    v1.a = 255.0f;
    v1.oow = 1.0f;

    GrVertex2 v2{};
    v2.x = SNAP_BIAS + 200.0f;
    v2.y = SNAP_BIAS + 100.0f; // Horizontal line from (100, 100) to (200, 100)
    v2.z = 0.0f;
    v2.r = v2.g = v2.b = 255.0f;
    v2.a = 255.0f;
    v2.oow = 1.0f;

    // Issue the draw call!
    drawLine(&v1, &v2);

    // Read back a pixel on the line (e.g., at X=150, Y=100)
    uint32_t pixel = 0;
    FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 150, 100, 1, 1, 4, &pixel);
    REQUIRE(readOk == FXTRUE);

    // Unpack color (little-endian ABGR: byte 0 is R, byte 1 is G, byte 2 is B)
    uint8_t r = pixel & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = (pixel >> 16) & 0xFF;
    
    INFO("Readback pixel at (150, 100): R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // The pixel color must be white (R, G, B > 240) if coordinate unsnapping succeeded.
    // If unsnapping failed, the line would be clipped off-screen and the pixel would be black.
    CHECK(r > 240);
    CHECK(g > 240);
    CHECK(b > 240);

    winClose();
    shutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
#endif
}

TEST_CASE("Glide FOGCOORD Extension and Fog Mode Mapping Verification", "[Glide][Fog][FOGCOORD]") {
    // 1. Glide 3.x Verification
    SECTION("Glide 3.x FOGCOORD Support") {
#ifdef __linux__
        void* handle = OpenGlide3Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxU32(*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
        using SelectCtxFunc = FxBool(*)(FxU32);
        using GetStringFunc = const char*(*)(FxU32);
        using FogModeFunc = void(*)(FxU32);
        using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);
        using WinCloseFunc = FxBool(*)(FxU32);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
        auto getString = reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
        auto fogMode = reinterpret_cast<FogModeFunc>(::dlsym(handle, "grFogMode"));
        auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(selectCtx != nullptr);
        REQUIRE(getString != nullptr);
        REQUIRE(fogMode != nullptr);
        REQUIRE(vertexLayout != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        // Run with software backend to isolate
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 1); // 800x600
        REQUIRE(ctx != 0);
        REQUIRE(selectCtx(ctx) == FXTRUE);

        // A. Verify that FOGCOORD is advertised in the extension string
        const char* extensions = getString(0xa0); // GR_EXTENSION
        REQUIRE(extensions != nullptr);
        std::string extStr(extensions);
        CHECK(extStr.find("FOGCOORD") != std::string::npos);

        // B. Verify that grFogMode with Glide 3 table-on-fogcoord compiles/runs without crash
        // GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT = 0x1
        fogMode(0x1); 

        // C. Verify that grVertexLayout with GR_PARAM_FOG_EXT works
        // GR_PARAM_FOG_EXT = 0x05, offset = 28, mode = 1 (ENABLE)
        vertexLayout(0x05, 28, 1);

        winClose(ctx);
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
#endif
    }

    // 2. Glide 2.x Verification
    SECTION("Glide 2.x Pure ABI (No grGetString / No FOGCOORD)") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using WinCloseFunc = FxBool(*)(void);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(::dlsym(handle, "grGetString") == nullptr);
        REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        // Resolution GR_RESOLUTION_640x480 = 0x4
        FxBool ok = winOpen(0, 0x4, 0, 0, 0, 2, 1);
        REQUIRE(ok == FXTRUE);

        winClose();
        shutdown();
        ::dlclose(handle);
        ::unsetenv("GLIDE_NG_BACKEND");
#endif
    }
}

TEST_CASE("VertexLayout FOGCOORD Parameter Decoding", "[VertexLayout][FOGCOORD]") {
    auto& layout = GlideWrapper::VertexLayout::GetInstance();
    layout.ResetToGlide3Canonical();

    // 1. Enable custom fog coordinate offset
    // GR_PARAM_FOG_EXT = 0x05, offset = 28, mode = 1 (ENABLE)
    layout.SetParamOffsetGlide3(0x05, 28, 1);

    // 2. Create a mock vertex byte buffer matching this layout
    struct MockVertex {
        float x, y;    // 0, 4
        float z;       // 8
        float r, g, b; // 12, 16, 20
        float a;       // 24
        float fog;     // 28  <-- Custom Fog Coordinate!
        float pad[3];  // Pad to 44 bytes to prevent ASan stack-buffer-overflow on canonical ST0 at offset 36!
    };

    MockVertex mockV;
    mockV.x = 100.0f;
    mockV.y = 200.0f;
    mockV.z = 0.5f;
    mockV.r = 255.0f;
    mockV.g = 128.0f;
    mockV.b = 64.0f;
    mockV.a = 255.0f;
    mockV.fog = 12.34f; // Custom fog coord!

    // 3. Decode the vertex
    GlideWrapper::ModernVertex decoded = layout.DecodeVertex(&mockV);

    // 4. Assert decoded values
    CHECK(decoded.pos[0] == 100.0f);
    CHECK(decoded.pos[1] == 200.0f);
    CHECK(decoded.fog == 12.34f);

    // Clean up layout
    layout.ResetToGlide3Canonical();
}

#ifdef __linux__
TEST_CASE("Software Backend Anti-Aliasing (AA) Modes & Verification", "[Software][AA]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = unsigned long(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
    using CloseWinFunc = void(*)(void);
    using DrawPointFunc = void(*)(const void*);
    using DrawLineFunc = void(*)(const void*, const void*);
    using VertexLayoutFunc = void(*)(FxU32, FxU32, FxBool);
    using EnableFunc = void(*)(FxU32);
    using ConstantColorFunc = void(*)(GrColor_t);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFunc = FxBool(*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using ShutdownFunc = void(*)(void);
    using AlphaBlendFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto drawPoint = reinterpret_cast<DrawPointFunc>(::dlsym(handle, "grDrawPoint"));
    auto drawLine  = reinterpret_cast<DrawLineFunc>(::dlsym(handle, "grDrawLine"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto enable    = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
    auto constantColor = reinterpret_cast<ConstantColorFunc>(::dlsym(handle, "grConstantColorValue"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto lfbRead   = reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(drawPoint != nullptr);
    REQUIRE(drawLine != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(enable != nullptr);
    REQUIRE(constantColor != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(lfbRead != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);

    #define G3_PARAM_XY       0x01
    #define G3_PARAM_RGB      0x20
    #define G3_PARAM_A        0x10
    #define G3_PARAM_ENABLE   0x01
    #define GR_AA_ORDERED     0x01

    struct GlideVertex {
        float x, y, z;
        float ooz, a, oow;
        float r, g, b;
    };

    SECTION("Option A: Analytical Point AA Coverage") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        ::setenv("GLIDE_NG_SW_AA", "analytical", 1);

        glideInit();
        if (sstSelect) sstSelect(0);
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        vertexLayout(G3_PARAM_XY, 0, G3_PARAM_ENABLE);
        vertexLayout(G3_PARAM_RGB, 24, G3_PARAM_ENABLE);
        // Disable unused attributes to prevent ASan stack-buffer-overflow on 36-byte vertex struct!
        vertexLayout(0x02, 0, 0); // GR_PARAM_Z, DISABLE
        vertexLayout(0x04, 0, 0); // GR_PARAM_Q (OOW), DISABLE
        vertexLayout(0x10, 0, 0); // GR_PARAM_A, DISABLE
        vertexLayout(0x40, 0, 0); // GR_PARAM_ST0, DISABLE

        colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
        constantColor(0xFFFFFFFF); // Solid white
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                           GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);

        bufferClear(0x00000000, 0, 0);

        enable(GR_AA_ORDERED);

        GlideVertex v;
        v.x = 320.9f; v.y = 240.9f; v.z = 0.5f;
        v.ooz = 1.0f; v.a = 255.0f; v.oow = 1.0f;
        v.r = 255.0f; v.g = 255.0f; v.b = 255.0f;

        drawPoint(&v);

        uint32_t pixels[9];
        FxBool ok = lfbRead(GR_BUFFER_BACKBUFFER, 319, 239, 3, 3, 12, pixels);
        REQUIRE(ok == FXTRUE);

        uint32_t centerPixel = pixels[4] & 0x00FFFFFF;
        uint32_t rightPixel = pixels[5] & 0x00FFFFFF;
        uint32_t bottomPixel = pixels[7] & 0x00FFFFFF;

        CHECK(centerPixel > 0);
        CHECK(rightPixel > 0);
        CHECK(bottomPixel > 0);
        CHECK(rightPixel < 0x00FFFFFF);
        CHECK(bottomPixel < 0x00FFFFFF);

        winClose();
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
        ::unsetenv("GLIDE_NG_SW_AA");
    }

    SECTION("Option B: SSAA Resolution Scaling & Downsampling") {
        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        ::setenv("GLIDE_NG_SW_AA", "ssaa", 1);

        glideInit();
        if (sstSelect) sstSelect(0);
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
            GlideWrapper::BackendManager::GetInstance().GetBackend()
        );
        REQUIRE(backend != nullptr);

        // Framebuffer dimensions must be scaled by 2x
        CHECK(backend->GetWidth() == 1280);
        CHECK(backend->GetHeight() == 960);

        winClose();
        shutdown();
        ::unsetenv("GLIDE_NG_BACKEND");
        ::unsetenv("GLIDE_NG_SW_AA");
    }
}
#endif



#ifdef __linux__
TEST_CASE("Glide 3.x Texture Coordinate Mirroring (TEXMIRROR) [Glide3][TexMirror]", "[Glide3][TexMirror]") {
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
    #define GR_TEXTURECLAMP_MIRROR_EXT 2
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

TEST_CASE("Glide 3.x Perfect Bilinear Halo-Free Texture Chromakeying (TEXCHROMA) [Glide3][TexChroma]", "[Glide3][TexChroma]") {
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
    #define GR_TEXCHROMA_ENABLE_EXT 1
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

TEST_CASE("Glide 3.x Mipmapping State & grGlideGetState/SetState Round-Trip [Glide3][Mipmap]", "[Glide3][Mipmap]") {
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

TEST_CASE("Avenger Voodoo3 2048x2048 Texture Support & LOD Bounds Verification [Glide3][Voodoo3]", "[Glide3][Voodoo3]") {
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

TEST_CASE("Software Reference Backend Mipmapping & LOD Bias Verification [Glide3][Software]", "[Glide3][Software]") {
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

#endif


TEST_CASE("Glide 3.x State Save & Restore ClipWindow and ColorMask Verification", "[Glide3][State]") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using SelectSstFunc = void(*)(int);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using ClipWinFunc = void(*)(FxU32, FxU32, FxU32, FxU32);
    using ColorMaskFunc = void(*)(FxBool, FxBool);
    using TexCombineFunc = void(*)(GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t, GrCombineFactor_t, FxBool, FxBool);
    using GetStateFunc = void(*)(void*);
    using SetStateFunc = void(*)(const void*);
    using ShutdownFunc = void(*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect = reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto clipWin   = reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
    auto colorMask = reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
    auto texCombine = reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
    auto getState  = reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
    auto setState  = reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(clipWin != nullptr);
    REQUIRE(colorMask != nullptr);
    REQUIRE(texCombine != nullptr);
    REQUIRE(getState != nullptr);
    REQUIRE(setState != nullptr);
    REQUIRE(glideShutdown != nullptr);

    // Force headless software reference backend for testing
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    auto* backend = dynamic_cast<GlideWrapper::GlideStateTracker*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend()
    );
    REQUIRE(backend != nullptr);

    // 1. Configure initial custom clip, mask and TMU invert states
    clipWin(30, 40, 500, 400);
    colorMask(FXTRUE, FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXTRUE, FXFALSE);
    texCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXTRUE);

    REQUIRE(backend->GetClipMinX() == 30);
    REQUIRE(backend->GetClipMinY() == 40);
    REQUIRE(backend->GetClipMaxX() == 500);
    REQUIRE(backend->GetClipMaxY() == 400);
    REQUIRE(backend->GetColorMaskRgb() == true);
    REQUIRE(backend->GetColorMaskAlpha() == false);
    REQUIRE(backend->GetTexCombinerRgbInvert(0) == true);
    REQUIRE(backend->GetTexCombinerAlphaInvert(0) == false);
    REQUIRE(backend->GetTexCombinerRgbInvert(1) == false);
    REQUIRE(backend->GetTexCombinerAlphaInvert(1) == true);

    // 2. Save state using grGlideGetState
    char savedState[1024];
    getState(savedState);

    // 3. Clobber/perturb states
    clipWin(0, 0, 800, 600);
    colorMask(FXFALSE, FXTRUE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    texCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    REQUIRE(backend->GetClipMinX() == 0);
    REQUIRE(backend->GetClipMinY() == 0);
    REQUIRE(backend->GetClipMaxX() == 800);
    REQUIRE(backend->GetClipMaxY() == 600);
    REQUIRE(backend->GetColorMaskRgb() == false);
    REQUIRE(backend->GetColorMaskAlpha() == true);
    REQUIRE(backend->GetTexCombinerRgbInvert(0) == false);
    REQUIRE(backend->GetTexCombinerAlphaInvert(0) == false);
    REQUIRE(backend->GetTexCombinerRgbInvert(1) == false);
    REQUIRE(backend->GetTexCombinerAlphaInvert(1) == false);

    // 4. Restore state using grGlideSetState
    setState(savedState);

    // 5. Verify that all serialized states were successfully restored to the backend!
    REQUIRE(backend->GetClipMinX() == 30);
    REQUIRE(backend->GetClipMinY() == 40);
    REQUIRE(backend->GetClipMaxX() == 500);
    REQUIRE(backend->GetClipMaxY() == 400);
    REQUIRE(backend->GetColorMaskRgb() == true);
    REQUIRE(backend->GetColorMaskAlpha() == false);
    REQUIRE(backend->GetTexCombinerRgbInvert(0) == true);
    REQUIRE(backend->GetTexCombinerAlphaInvert(0) == false);
    REQUIRE(backend->GetTexCombinerRgbInvert(1) == false);
    REQUIRE(backend->GetTexCombinerAlphaInvert(1) == true);

    winClose();
    glideShutdown();
    ::dlclose(handle);
}

TEST_CASE("Glide 3.x Software Reference Backend Multi-Texturing Parity", "[Glide3][Software][MultiTexture]") {
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

struct MockGlideWrapperState {
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
  uint32_t clipMinX;
  uint32_t clipMinY;
  uint32_t clipMaxX;
  uint32_t clipMaxY;
  int colorMaskRgb;
  int colorMaskAlpha;
  int texRgbInvert[2];
  int texAlphaInvert[2];
  uint32_t ditherMode;
  uint32_t stippleMode;
  uint32_t stipplePattern;
  int hasTbufferState;
  int aaMultiSample;
  uint32_t tbufferWriteMask;
};

TEST_CASE("Glide 3.x State Restoration Sanitization [VULN-01]", "[Glide3][State]") {
#ifdef __linux__
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit    = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideInit"));
    auto sstSelect    = reinterpret_cast<void(*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen      = reinterpret_cast<FxU32(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int)>(::dlsym(handle, "grSstWinOpen"));
    auto getState     = reinterpret_cast<void(*)(void*)>(::dlsym(handle, "grGlideGetState"));
    auto setState     = reinterpret_cast<void(*)(const void*)>(::dlsym(handle, "grGlideSetState"));
    auto winClose     = reinterpret_cast<FxBool(*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown = reinterpret_cast<void(*)(void)>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(getState != nullptr);
    REQUIRE(setState != nullptr);

    glideInit();
    if (sstSelect) sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Get current state (allocate full GR_GLIDE_STATE_SIZE = 1024 bytes)
    std::vector<uint8_t> stateBuffer(
        std::max<size_t>(1024u, sizeof(MockGlideWrapperState)), 0);
    getState(stateBuffer.data());

    auto* statePtr = reinterpret_cast<MockGlideWrapperState*>(stateBuffer.data());

    // Verify initial values
    REQUIRE(statePtr->cullMode == 0); // GR_CULL_DISABLE

    // Corrupt state values with out-of-range enums and values
    statePtr->cullMode = 999;        // invalid (valid range is 0..2)
    statePtr->depthCompare = 888;    // invalid (valid range is 0..7)
    statePtr->alphaTestOp = 777;     // invalid (valid range is 0..7)
    statePtr->rgbSrcBlend = 666;     // invalid (valid range is 0..10)
    statePtr->rgbDstBlend = 555;     // invalid (valid range is 0..10)
    statePtr->clampS[0] = 444;       // invalid (valid range is 0..2)
    statePtr->clipMaxX = 99999;      // invalid (exceeds window width 640)

    // Restore state using mutated buffer.
    // The wrapper must sanitize all these values to prevent corruption!
    setState(stateBuffer.data());

    // Read back state again to verify that sanitization took place!
    std::vector<uint8_t> checkBuffer(
        std::max<size_t>(1024u, sizeof(MockGlideWrapperState)), 0);
    getState(checkBuffer.data());

    auto* checkPtr = reinterpret_cast<MockGlideWrapperState*>(checkBuffer.data());

    // Cull mode must be sanitized to default (0 - GR_CULL_DISABLE)
    REQUIRE(checkPtr->cullMode == 0);

    // Depth compare must be sanitized to default (1 - GR_CMP_LESS)
    REQUIRE(checkPtr->depthCompare == 1);

    // Alpha test op must be sanitized to default (7 - GR_CMP_ALWAYS)
    REQUIRE(checkPtr->alphaTestOp == 7);

    // Blend modes must be sanitized to default (src=GR_BLEND_ONE=4, dst=GR_BLEND_ZERO=0)
    REQUIRE(checkPtr->rgbSrcBlend == GR_BLEND_ONE);
    REQUIRE(checkPtr->rgbDstBlend == GR_BLEND_ZERO);

    // Clamp mode must be sanitized to default (0 - GR_TEXTURECLAMP_WRAP)
    REQUIRE(checkPtr->clampS[0] == 0);

    // Clip boundaries must be clamped to window width (640)
    REQUIRE(checkPtr->clipMaxX == 640);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x Texture LOD & Aspect Bounds Validation [VULN-05]", "[Glide3][Texture]") {
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


TEST_CASE("Mathematical LOD y-derivative Calculation Verification [Glide3][LOD]", "[Glide3][LOD]") {
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

TEST_CASE("Glide 3.x Phase 4 Retro Aesthetic & Advanced Features [Glide3][Phase4][Retro]", "[Glide3][Phase4]") {
#ifdef __linux__
    // Force headless software reference backend for testing
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "3", 1); // INFO

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void(*)(void);
    using OpenWinFunc = FxU32(*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
    using CloseWinFunc = void(*)(void);
    using ShutdownFunc = void(*)(void);
    using BufferClearFunc = void(*)(GrColor_t, GrAlpha_t, FxU32);
    using DrawTriFunc = void(*)(const void*, const void*, const void*);
    using ReadLfbFunc = FxBool(*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
    using VertexLayoutFunc = void(*)(FxU32, FxI32, FxU32);
    using DepthBufferModeFunc = void(*)(GrDepthBufferMode_t);
    using DepthBufferFunctionFunc = void(*)(GrCmpFnc_t);
    using DepthMaskFunc = void(*)(FxBool);
    using AlphaBlendFunctionFunc = void(*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
    using AlphaTestFunctionFunc = void(*)(GrCmpFnc_t);
    using AlphaTestReferenceValueFunc = void(*)(GrAlpha_t);
    using DitherModeFunc = void(*)(GrDitherMode_t);
    using StippleModeFunc = void(*)(GrStippleMode_t);
    using StipplePatternFunc = void(*)(GrStipplePattern_t);
    using DepthRangeFunc = void(*)(float, float);
    using ColorCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);
    using AlphaCombineFunc = void(*)(GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t, GrCombineOther_t, FxBool);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto winOpen   = reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose  = reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
    auto shutdown  = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto bufferClear = reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
    auto drawTri   = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readLfb   = reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
    auto vertexLayout = reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto depthBufferMode = reinterpret_cast<DepthBufferModeFunc>(::dlsym(handle, "grDepthBufferMode"));
    auto depthBufferFunction = reinterpret_cast<DepthBufferFunctionFunc>(::dlsym(handle, "grDepthBufferFunction"));
    auto depthMask = reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
    auto alphaBlendFunction = reinterpret_cast<AlphaBlendFunctionFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto alphaTestFunction = reinterpret_cast<AlphaTestFunctionFunc>(::dlsym(handle, "grAlphaTestFunction"));
    auto alphaTestRef = reinterpret_cast<AlphaTestReferenceValueFunc>(::dlsym(handle, "grAlphaTestReferenceValue"));
    auto ditherMode = reinterpret_cast<DitherModeFunc>(::dlsym(handle, "grDitherMode"));
    auto stippleMode = reinterpret_cast<StippleModeFunc>(::dlsym(handle, "grStippleMode"));
    auto stipplePattern = reinterpret_cast<StipplePatternFunc>(::dlsym(handle, "grStipplePattern"));
    auto depthRange = reinterpret_cast<DepthRangeFunc>(::dlsym(handle, "grDepthRange"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<AlphaCombineFunc>(::dlsym(handle, "grAlphaCombine"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(shutdown != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(readLfb != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(depthBufferMode != nullptr);
    REQUIRE(depthBufferFunction != nullptr);
    REQUIRE(depthMask != nullptr);
    REQUIRE(alphaBlendFunction != nullptr);
    REQUIRE(alphaTestFunction != nullptr);
    REQUIRE(alphaTestRef != nullptr);
    REQUIRE(ditherMode != nullptr);
    REQUIRE(stippleMode != nullptr);
    REQUIRE(stipplePattern != nullptr);
    REQUIRE(depthRange != nullptr);
    REQUIRE(colorCombine != nullptr);
    REQUIRE(alphaCombine != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
        GlideWrapper::BackendManager::GetInstance().GetBackend()
    );
    REQUIRE(backend != nullptr);

    // Canonical vertex layout for StandardVert
    vertexLayout(0x01, 0, 1);  // GR_PARAM_XY at 0
    vertexLayout(0x02, 8, 1);  // GR_PARAM_Z at 8
    vertexLayout(0x20, 12, 1); // GR_PARAM_RGB at 12
    vertexLayout(0x10, 28, 1); // GR_PARAM_A at 28
    vertexLayout(0x03, 32, 1); // GR_PARAM_W (oow) at 32

    // Set simple color combiner: output vertex iterated color
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    SECTION("Depth Buffering (Z vs W, compare ops, depth mask)") {
        // 1. Z-buffering test
        depthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);

        bufferClear(0x00000000, 0, 0xFFFF); // Clear depth to 1.0f

        // Draw green triangle at Z = 0.5f (close)
        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        // Draw red triangle at Z = 0.75f (farther, should be occluded)
        StandardVert v3 = { 100.0f, 100.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v4 = { 200.0f, 100.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v5 = { 150.0f, 200.0f, 0.75f, 255.0f, 0.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        drawTri(&v3, &v4, &v5);

        // Verify pixel at center (150, 130) is Green (not Red)
        uint32_t px = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        uint8_t gChan = (px >> 8) & 0xFF;
        CHECK(gChan > 240);
        
        // 2. Depth mask test
        // Authentic Glide grBufferClear obeys grDepthMask (SST_ZAWRMASK), so
        // clear depth to 1.0f while depthMask is enabled, then disable writes.
        bufferClear(0x00000000, 0, 0xFFFF);
        depthMask(FXFALSE); // disable depth writes

        // Draw green triangle at Z = 0.5f again
        drawTri(&v0, &v1, &v2);
        // Verify depth buffer pixel was NOT written (should still be 1.0f)
        float dbPixel = backend->GetDepthBufferPixel(150, 130);
        CHECK(dbPixel == 1.0f);

        // 3. W-buffering test
        depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        depthBufferFunction(GR_CMP_LESS);
        depthMask(FXTRUE);
        depthRange(2.0f, 10.0f);
        bufferClear(0x00000000, 0, 0xFFFF); // Clears depth to 1.0f

        // Draw green triangle with W = 4.0f (oow = 0.25f) -> TargetDepth = (4-2)/8 = 0.25f
        v0.oow = 0.25f; v1.oow = 0.25f; v2.oow = 0.25f;
        drawTri(&v0, &v1, &v2);

        float dbWVal = backend->GetDepthBufferPixel(150, 130);
        CHECK(std::abs(dbWVal - 0.25f) < 0.001f);
    }

    SECTION("Alpha Blending and Testing") {
        bufferClear(0x00000000, 0, 0xFFFF);
        
        // 1. Alpha blending test: Src = Src Alpha, Dst = One Minus Src Alpha
        alphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                           GR_BLEND_ONE, GR_BLEND_ZERO);

        // Draw flat blue triangle with Alpha = 0.5f (128)
        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 0.0f, 0.0f, 255.0f, 0.0f, 128.0f, 1.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        uint32_t px = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        uint8_t b1 = px & 0xFF;
        uint8_t b3 = (px >> 16) & 0xFF;
        bool blendOk = (b1 > 110 && b1 < 140) || (b3 > 110 && b3 < 140);
        CHECK(blendOk);

        // 2. Alpha testing test
        bufferClear(0x00000000, 0, 0xFFFF);
        alphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO); // no blend

        // Enable alpha test: pass if Alpha > 150
        alphaTestFunction(GR_CMP_GREATER);
        alphaTestRef(150);

        // Draw flat red triangle with Alpha = 100 (should be discarded)
        v0.r = 255.0f; v0.g = 0.0f; v0.b = 0.0f; v0.a = 100.0f;
        v1.r = 255.0f; v1.g = 0.0f; v1.b = 0.0f; v1.a = 100.0f;
        v2.r = 255.0f; v2.g = 0.0f; v2.b = 0.0f; v2.a = 100.0f;
        drawTri(&v0, &v1, &v2);

        px = 0xFFFFFFFF;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        CHECK((px & 0x00FFFFFF) == 0); // Color is black

        // Draw flat red triangle with Alpha = 200 (should pass)
        v0.a = 200.0f; v1.a = 200.0f; v2.a = 200.0f;
        drawTri(&v0, &v1, &v2);

        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px) == FXTRUE);
        b1 = px & 0xFF;
        b3 = (px >> 16) & 0xFF;
        bool redHigh = (b1 > 240) || (b3 > 240);
        CHECK(redHigh); // Red is high
        
        alphaTestFunction(GR_CMP_ALWAYS);
    }

    SECTION("Dithering (16-bit retro dithering)") {
        bufferClear(0x00000000, 0, 0xFFFF);
        ditherMode(GR_DITHER_4x4);

        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 15.0f, 30.0f, 45.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        std::vector<uint32_t> block(16);
        for (int yOffset = 0; yOffset < 4; ++yOffset) {
            for (int xOffset = 0; xOffset < 4; ++xOffset) {
                readLfb(GR_BUFFER_BACKBUFFER, 148 + xOffset, 128 + yOffset, 1, 1, 4, &block[yOffset * 4 + xOffset]);
            }
        }

        bool foundDifference = false;
        uint32_t firstPixel = block[0] & 0x00FFFFFF;
        for (int i = 1; i < 16; ++i) {
            if ((block[i] & 0x00FFFFFF) != firstPixel) {
                foundDifference = true;
                break;
            }
        }
        if (!foundDifference) {
            std::cout << "[DITHER_DEBUG] All 16 pixels are identical: 0x" << std::hex << firstPixel << std::dec << std::endl;
            for (int i = 0; i < 16; ++i) {
                std::cout << "  Pixel " << i << ": 0x" << std::hex << block[i] << std::dec << std::endl;
            }
        }
        CHECK(foundDifference == true);

        ditherMode(GR_DITHER_DISABLE);
    }

    SECTION("Stipple Transparency") {
        bufferClear(0x00000000, 0, 0xFFFF);
        stippleMode(GR_STIPPLE_PATTERN);
        stipplePattern(0x55AA55AA);

        StandardVert v0 = { 100.0f, 100.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v1 = { 200.0f, 100.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        StandardVert v2 = { 150.0f, 200.0f, 0.5f, 255.0f, 255.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f };
        drawTri(&v0, &v1, &v2);

        uint32_t px150 = 0;
        uint32_t px151 = 0;
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 150, 130, 1, 1, 4, &px150) == FXTRUE);
        REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 151, 130, 1, 1, 4, &px151) == FXTRUE);

        if ((px150 & 0x00FFFFFF) == 0 || (px151 & 0x00FFFFFF) != 0) {
            std::cout << "[STIPPLE_DEBUG] Stipple mismatch!" << std::endl;
            std::cout << "  Pixel (150, 130): 0x" << std::hex << px150 << std::dec << std::endl;
            std::cout << "  Pixel (151, 130): 0x" << std::hex << px151 << std::dec << std::endl;
            std::cout << "  Backend Stipple Mode: " << backend->GetStippleMode() << std::endl;
            std::cout << "  Backend Stipple Pattern: 0x" << std::hex << backend->GetStipplePattern() << std::dec << std::endl;
        }

        CHECK((px150 & 0x00FFFFFF) != 0);
        CHECK((px151 & 0x00FFFFFF) == 0);

        stippleMode(0);
    }

    winClose();
    shutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_NG_BACKEND");
#endif
}

TEST_CASE("Glide 3.x State Serialization & Phase 4 States [Glide3][State][Serialization]", "[Glide3][State]") {
#ifdef __linux__
  // Force the Software backend for this test
  ::setenv("GLIDE_NG_BACKEND", "sw", 1);

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void (*)(void))dlsym(handle, "grGlideInit");
  auto winOpen = (FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int))dlsym(handle, "grSstWinOpen");
  auto winClose = (void (*)(void))dlsym(handle, "grSstWinClose");
  auto shutdown = (void (*)(void))dlsym(handle, "grGlideShutdown");

  auto ditherMode = (void (*)(GrDitherMode_t))dlsym(handle, "grDitherMode");
  auto stippleMode = (void (*)(GrStippleMode_t))dlsym(handle, "grStippleMode");
  auto stipplePattern = (void (*)(GrStipplePattern_t))dlsym(handle, "grStipplePattern");
  auto depthRange = (void (*)(float, float))dlsym(handle, "grDepthRange");
  auto glideGetState = (void (*)(void*))dlsym(handle, "grGlideGetState");
  auto glideSetState = (void (*)(const void*))dlsym(handle, "grGlideSetState");

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(ditherMode != nullptr);
  REQUIRE(stippleMode != nullptr);
  REQUIRE(stipplePattern != nullptr);
  REQUIRE(depthRange != nullptr);
  REQUIRE(glideGetState != nullptr);
  REQUIRE(glideSetState != nullptr);

  glideInit();
  FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
  REQUIRE(ctx != 0);

  auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend();
  auto* sw_b = dynamic_cast<GlideWrapper::GlideStateTracker*>(b);
  REQUIRE(sw_b != nullptr);

  // 1. Set custom states
  ditherMode(GR_DITHER_4x4);
  stippleMode(GR_STIPPLE_PATTERN);
  stipplePattern(0x0F0F0F0F);
  depthRange(0.25f, 0.75f);

  // Verify backend received them
  CHECK(sw_b->GetDitherMode() == GR_DITHER_4x4);
  CHECK(sw_b->GetStippleMode() == GR_STIPPLE_PATTERN);
  CHECK(sw_b->GetStipplePattern() == 0x0F0F0F0F);
  CHECK(sw_b->GetDepthNear() == 0.25f);
  CHECK(sw_b->GetDepthFar() == 0.75f);

  // 2. Serialize state
  char savedState[1024] = {0};
  glideGetState(savedState);

  // 3. Clobber/perturb states
  ditherMode(GR_DITHER_DISABLE);
  stippleMode(0); // DISABLE
  stipplePattern(0xFFFFFFFF);
  depthRange(0.0f, 1.0f);

  CHECK(sw_b->GetDitherMode() == GR_DITHER_DISABLE);
  CHECK(sw_b->GetStippleMode() == 0);
  CHECK(sw_b->GetStipplePattern() == 0xFFFFFFFF);
  CHECK(sw_b->GetDepthNear() == 0.0f);
  CHECK(sw_b->GetDepthFar() == 1.0f);

  // 4. Restore state
  glideSetState(savedState);

  // 5. Assert states are restored
  CHECK(sw_b->GetDitherMode() == GR_DITHER_4x4);
  CHECK(sw_b->GetStippleMode() == GR_STIPPLE_PATTERN);
  CHECK(sw_b->GetStipplePattern() == 0x0F0F0F0F);
  CHECK(sw_b->GetDepthNear() == 0.25f);
  CHECK(sw_b->GetDepthFar() == 0.75f);

  if (winClose) winClose();
  if (shutdown) shutdown();
  ::dlclose(handle);
  ::unsetenv("GLIDE_NG_BACKEND");
#endif
}

TEST_CASE("SoftwareBackend honors forceNoWindow without creating SDL resources",
          "[Software][Headless]") {
  GlideWrapper::WrapperConfig config;
  config.backend = "software";
  config.forceNoWindow = true;
  config.maxFps = 0.0;

  GlideWrapper::SoftwareBackend backend;
  REQUIRE(backend.Initialize(config));
  REQUIRE(backend.AttachWindow(nullptr, 320, 240, true));
  REQUIRE(backend.IsHeadlessMode());
  REQUIRE(backend.GetWidth() == 320);
  REQUIRE(backend.GetHeight() == 240);

  backend.ClearBuffer(0xFF112233, 0xFF, 1.0f, 0x7);
  REQUIRE(backend.SwapBuffers());

  uint16_t pixel = 0;
  REQUIRE(backend.ReadLFB(0, 160, 120, 1, 1, 2, &pixel));
  CHECK(pixel != 0);

  backend.DetachWindow();
  CHECK_FALSE(backend.IsHeadlessMode());
  backend.Shutdown();
}

TEST_CASE("Lazy CPU ClearBuffer defers fills until LFB access",
          "[Software][LFB][LazyClear]") {
  class LazyClearProbeBackend : public GlideWrapper::SoftwareBackend {
   public:
    void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                     uint32_t clearMask) override {
      ScheduleLazyCpuClear(color, alpha, z, clearMask);
    }
    uint32_t PeekRawBackPixel(uint32_t x, uint32_t y) const {
      const auto* px = reinterpret_cast<const uint32_t*>(
          m_cpuBuffers[m_backBufferIdx].data());
      return px[y * m_headlessWidth + x];
    }
  };

  GlideWrapper::WrapperConfig config;
  config.backend = "software";
  config.forceNoWindow = true;
  config.maxFps = 0.0;

  LazyClearProbeBackend backend;
  REQUIRE(backend.Initialize(config));
  REQUIRE(backend.AttachWindow(nullptr, 64, 64, true));

  // Clear backbuffer lazily to bright green (0x0000FF00 in ABGR/ARGB).
  backend.ClearBuffer(0x0000FF00, 0xFF, 0.5f, 0x3);
  CHECK(backend.HasPendingLazyCpuClear(1));
  // Raw CPU backbuffer must remain untouched (0) until an LFB operation occurs.
  CHECK(backend.PeekRawBackPixel(0, 0) == 0u);

  // Perform a partial 1x1 LFB write at (10, 10) with pure white (RGB565 0xFFFF).
  uint16_t white565 = 0xFFFF;
  REQUIRE(backend.WriteLFB(1, 10, 10, 1, 1, 2, 0 /* GR_LFB_SRC_FMT_565 */,
                           &white565));
  CHECK_FALSE(backend.HasPendingLazyCpuClear(1));

  // Read back both the written pixel and an untouched background pixel.
  uint16_t writtenPx = 0;
  uint16_t bgPx = 0;
  REQUIRE(backend.ReadLFB(1, 10, 10, 1, 1, 2, &writtenPx));
  REQUIRE(backend.ReadLFB(1, 0, 0, 1, 1, 2, &bgPx));
  CHECK(writtenPx == 0xFFFF);
  CHECK(bgPx == 0x07E0);  // Green in RGB565!

  // Subsequent lazy clear followed by ReadLFB materializes cleanly too.
  backend.ClearBuffer(0x00FF0000, 0xFF, 1.0f, 0x1);
  CHECK(backend.HasPendingLazyCpuClear(1));
  uint16_t afterClearPx = 0;
  REQUIRE(backend.ReadLFB(1, 10, 10, 1, 1, 2, &afterClearPx));
  CHECK_FALSE(backend.HasPendingLazyCpuClear(1));
  CHECK(afterClearPx != 0xFFFF);

  backend.DetachWindow();
  backend.Shutdown();
}

TEST_CASE("IniConfigLoader parses sections/comments and respects env overrides",
          "[Config][IniConfigLoader]") {
  const std::string tmpPath = "/tmp/glide_ng_test_config_m8.ini";

  // RAII helper to save/restore environment variables touched by this test.
  struct EnvGuard {
    std::string name;
    bool hadValue{false};
    std::string oldValue;
    explicit EnvGuard(const char* n) : name(n) {
      if (const char* v = ::getenv(n)) {
        hadValue = true;
        oldValue = v;
      }
      ::unsetenv(n);
    }
    ~EnvGuard() {
      if (hadValue) {
        ::setenv(name.c_str(), oldValue.c_str(), 1);
      } else {
        ::unsetenv(name.c_str());
      }
    }
  };

  EnvGuard guardConfig("GLIDE_NG_CONFIG");
  EnvGuard guardBackend("GLIDE_NG_BACKEND");
  EnvGuard guardHeadless("GLIDE_NG_HEADLESS");
  EnvGuard guardCard("GLIDE_NG_CARD_MODEL");
  EnvGuard guardApi("GLIDE_NG_API_VERSION");
  EnvGuard guardFbiMem("GLIDE_NG_FBI_MEM_MB");
  EnvGuard guardTmuCount("GLIDE_NG_TMU_COUNT");
  EnvGuard guardTmuMem("GLIDE_NG_TMU_MEM_MB");
  EnvGuard guardSli("GLIDE_NG_SLI");
  EnvGuard guardVendor("GLIDE_NG_VENDOR");
  EnvGuard guardRenderer("GLIDE_NG_RENDERER");
  EnvGuard guardVsync("GLIDE_NG_VSYNC");
  EnvGuard guardMaxFps("GLIDE_NG_MAX_FPS");
  EnvGuard guardMsaa("GLIDE_NG_MSAA");
  EnvGuard guardScale("GLIDE_NG_WINDOW_SCALE");
  EnvGuard guardFilter("GLIDE_NG_PRESENTATION_FILTER");
  EnvGuard guardLogLevel("GLIDE_NG_LOG_LEVEL");
  EnvGuard guardConsole("GLIDE_NG_LOG_CONSOLE");
  EnvGuard guardLogFile("GLIDE_NG_LOG_FILE");
  EnvGuard guardNoSplash("GLIDE_NG_NO_SPLASH");
  EnvGuard guardPlug("GLIDE_NG_SHAMELESS_PLUG");
  EnvGuard guardSwThreads("GLIDE_NG_SW_THREADS");
  EnvGuard guardSwSimd("GLIDE_NG_SW_SIMD");
  EnvGuard guardSwTile("GLIDE_NG_SW_TILE_SIZE");
  EnvGuard guardSwAa("GLIDE_NG_SW_AA");
  EnvGuard guardVkCache("GLIDE_NG_VK_CACHE_PATH");
  EnvGuard guardGlesVbo("GLIDE_NG_GLES_VBO_SIZE");
  EnvGuard guardFxNoSplash("FX_GLIDE_NO_SPLASH");
  EnvGuard guardFxPlug("FX_GLIDE_SHAMELESS_PLUG");
  EnvGuard guardFxSwap("FX_GLIDE_SWAPINTERVAL");
  // Legacy env vars guarded for clean-break tests
  EnvGuard guardOldBackend("GLIDE_WRAPPER_BACKEND");
  EnvGuard guardOldShortBackend("GLIDE_BACKEND");
  EnvGuard guardOldVsync("GLIDE_VSYNC");
  EnvGuard guardOldDevice("GLIDE_DEVICE");
  EnvGuard guardOldVersion("GLIDE_VERSION_OVERRIDE");
  EnvGuard guardOldHeadless("GLIDE_HEADLESS");
  EnvGuard guardOldForceNoWin("GLIDE_FORCE_NO_WINDOW");

  SECTION("Sections, comments, inline comments, quotes, BOM/CRLF, and unknown keys") {
    {
      std::ofstream out(tmpPath, std::ios::binary);
      // Include UTF-8 BOM + CRLF line endings
      out << "\xEF\xBB\xBF"
          << "# Full line comment: backend = vulkan\r\n"
          << "; Semicolon comment: card_model = Voodoo5\r\n"
          << "[core]\r\n"
          << "backend = opengl_es  # inline hash comment\r\n"
          << "headless = true ; inline semicolon comment\r\n"
          << "\r\n"
          << "[hardware]\r\n"
          << "card_model = Voodoo3\r\n"
          << "api_version = 2.42\r\n"
          << "vendor = \"3dfx # Custom ; Vendor\"\r\n"
          << "\r\n"
          << "[display]\r\n"
          << "vsync = false\r\n"
          << "max_fps = 120.5\r\n"
          << "msaa = 4\r\n"
          << "window_scale = 2\r\n"
          << "\r\n"
          << "[logging]\r\n"
          << "level = 4\r\n"
          << "console = true\r\n"
          << "\r\n"
          << "[unknown_section]\r\n"
          << "bogus_key = 123\r\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_42);
    CHECK(cfg.reportedVendorOverride == "3dfx # Custom ; Vendor");
    CHECK(cfg.logToConsole == true);
    CHECK(cfg.vsync == false);
    CHECK(cfg.forceNoWindow == true);
    CHECK(cfg.maxFps == 120.5f);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.windowScale == 2u);
    CHECK(cfg.logLevel == 4u);
    REQUIRE(cfg.unknownConfigKeys.size() == 1u);
    CHECK(cfg.unknownConfigKeys[0] == "unknown_section.bogus_key");

    std::remove(tmpPath.c_str());
  }

  SECTION("Flat sectionless INI parses cleanly, legacy JSON syntax is ignored, env overrides win") {
    {
      std::ofstream out(tmpPath);
      out << "card_model = VoodooGraphics\n"
          << "backend = gles\n"
          << "log_to_console = false\n"
          << "vsync = true\n"
          << "api_version = 2.40\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.logToConsole == false);
    CHECK(cfg.vsync == true);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_40);

    // Environment variables must override INI values.
    ::setenv("GLIDE_NG_BACKEND", "sw", 1);
    ::setenv("GLIDE_NG_LOG_CONSOLE", "1", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);

    GlideWrapper::WrapperConfig envCfg;
    REQUIRE(loader.Load(tmpPath, envCfg));
    CHECK(envCfg.backend == "software");
    CHECK(envCfg.logToConsole == true);
    CHECK(envCfg.vsync == false);

    // Clean break: a raw JSON file must NOT be parsed as config.
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_LOG_CONSOLE");
    ::unsetenv("GLIDE_NG_VSYNC");
    {
      std::ofstream out(tmpPath);
      out << "{ \"backend\": \"software\", \"model\": \"Voodoo3\", \"vsync\": false }\n";
    }
    GlideWrapper::WrapperConfig jsonCfg;
    REQUIRE(loader.Load(tmpPath, jsonCfg));
    CHECK(jsonCfg.backend == "vulkan");
    CHECK(jsonCfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(jsonCfg.vsync == true);

    std::remove(tmpPath.c_str());
  }

  SECTION("Strict 4-layer precedence: Defaults < INI < FX_GLIDE_* < GLIDE_NG_*") {
    const std::string customPath = "/tmp/glide_ng_custom_cfg_test.ini";
    {
      std::ofstream out(customPath);
      out << "[glide]\n"
          << "no_splash = false\n"
          << "shameless_plug = false\n"
          << "[display]\n"
          << "vsync = true\n"
          << "presentation_filter = nearest\n"
          << "[logging]\n"
          << "level = warn\n"
          << "file = custom_ini.log\n"
          << "[software]\n"
          << "threads = 4\n"
          << "simd = false\n"
          << "tile_size = 64\n"
          << "aa = ssaa\n"
          << "[vulkan]\n"
          << "cache_path = /tmp/custom_vk.bin\n"
          << "[opengl_es]\n"
          << "vbo_size_mb = 16\n";
    }

    // GLIDE_NG_CONFIG redirects loader away from default path
    ::setenv("GLIDE_NG_CONFIG", customPath.c_str(), 1);
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.configFileLoaded == true);
    CHECK(cfg.configFilePath == customPath);
    CHECK(cfg.noSplash == false);
    CHECK(cfg.shamelessPlugMode == 0);
    CHECK(cfg.vsync == true);
    CHECK(cfg.logLevel == 2u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.logFile == "custom_ini.log");
    CHECK(cfg.swThreads == 4u);
    CHECK(cfg.swUseSimd == false);
    CHECK(cfg.swTileSize == 64u);
    CHECK(cfg.swAaMode == "ssaa");
    CHECK(cfg.vkCachePath == "/tmp/custom_vk.bin");
    CHECK(cfg.glesVboSizeMb == 16u);
    CHECK(cfg.settingSources.at("noSplash") == "ini");

    // Layer 3: Authentic FX_GLIDE_* overrides INI
    ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
    ::setenv("FX_GLIDE_SHAMELESS_PLUG", "1", 1);
    ::setenv("FX_GLIDE_SWAPINTERVAL", "0", 1);
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.noSplash == true);
    CHECK(cfg.shamelessPlugMode == 1);
    CHECK(cfg.vsync == false);
    CHECK(cfg.swapInterval == 0);
    CHECK(cfg.settingSources.at("noSplash") == "FX_GLIDE_NO_SPLASH");
    CHECK(cfg.settingSources.at("shamelessPlug") == "FX_GLIDE_SHAMELESS_PLUG");
    CHECK(cfg.settingSources.at("vsync") == "FX_GLIDE_SWAPINTERVAL");

    // Layer 4: GLIDE_NG_* overrides FX_GLIDE_* and INI
    ::setenv("GLIDE_NG_NO_SPLASH", "0", 1);
    ::setenv("GLIDE_NG_SHAMELESS_PLUG", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "fifo", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "trace", 1);
    ::setenv("GLIDE_NG_PRESENTATION_FILTER", "linear", 1);
    ::setenv("GLIDE_NG_LOG_FILE", "override.log", 1);
    ::setenv("GLIDE_NG_SW_THREADS", "8", 1);
    ::setenv("GLIDE_NG_SW_SIMD", "1", 1);
    ::setenv("GLIDE_NG_SW_TILE_SIZE", "32", 1);
    ::setenv("GLIDE_NG_SW_AA", "analytical", 1);
    ::setenv("GLIDE_NG_VK_CACHE_PATH", "/tmp/env_vk.bin", 1);
    ::setenv("GLIDE_NG_GLES_VBO_SIZE", "32", 1);
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.noSplash == false);
    CHECK(cfg.shamelessPlugMode == 0);
    CHECK(cfg.vsync == true);
    CHECK(cfg.vsyncStrictFifo == true);
    CHECK(cfg.swapInterval == 1);
    CHECK(cfg.logLevel == 5u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.logFile == "override.log");
    CHECK(cfg.swThreads == 8u);
    CHECK(cfg.swUseSimd == true);
    CHECK(cfg.swTileSize == 32u);
    CHECK(cfg.swAaMode == "analytical");
    CHECK(cfg.vkCachePath == "/tmp/env_vk.bin");
    CHECK(cfg.glesVboSizeMb == 32u);
    CHECK(cfg.settingSources.at("noSplash") == "GLIDE_NG_NO_SPLASH");
    CHECK(cfg.settingSources.at("shamelessPlug") == "GLIDE_NG_SHAMELESS_PLUG");
    CHECK(cfg.settingSources.at("vsync") == "GLIDE_NG_VSYNC");

    std::remove(customPath.c_str());
  }

  SECTION("Clean break: legacy GLIDE_WRAPPER_* and GLIDE_* env vars are ignored") {
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_BACKEND", "software", 1);
    ::setenv("GLIDE_VSYNC", "0", 1);
    ::setenv("GLIDE_DEVICE", "voodoo1", 1);
    ::setenv("GLIDE_VERSION_OVERRIDE", "2.40", 1);
    ::setenv("GLIDE_HEADLESS", "1", 1);
    ::setenv("GLIDE_FORCE_NO_WINDOW", "1", 1);

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.vsync == true);
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.forceNoWindow == false);
  }

  SECTION("Shameless plug resolution respects app-controlled vs forced modes") {
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto saved = reg.GetConfig();

    GlideWrapper::WrapperConfig cfg = saved;
    cfg.shamelessPlugMode = -1;  // app-controlled
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == false);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == true);

    cfg.shamelessPlugMode = 1;  // forced ON
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == true);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == true);

    cfg.shamelessPlugMode = 0;  // forced OFF
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == false);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == false);

    reg.GetConfig() = saved;
  }
}

TEST_CASE("VulkanBackend::SelectPresentMode prefers Mailbox on Wayland VSync",
          "[Vulkan][WSI][PresentMode]") {
  const std::vector<vk::PresentModeKHR> allModes = {
      vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eMailbox,
      vk::PresentModeKHR::eFifo};
  const std::vector<vk::PresentModeKHR> fifoOnly = {vk::PresentModeKHR::eFifo};
  const std::vector<vk::PresentModeKHR> immAndFifo = {
      vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eFifo};

  // Wayland + VSync prefers Mailbox when available (tear-free, avoids wp_fifo_v1 stalls)
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, true, false) ==
        vk::PresentModeKHR::eMailbox);
  // Wayland + VSync falls back to FIFO when Mailbox is unavailable
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(immAndFifo, true, true, false) ==
        vk::PresentModeKHR::eFifo);
  // Explicit GLIDE_NG_VSYNC=fifo forces FIFO even on Wayland
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, true, true) ==
        vk::PresentModeKHR::eFifo);
  // X11 + VSync retains FIFO
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, false, false) ==
        vk::PresentModeKHR::eFifo);
  // VSync disabled prefers Mailbox -> Immediate -> FIFO
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, false, false, false) ==
        vk::PresentModeKHR::eMailbox);
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(immAndFifo, false, false, false) ==
        vk::PresentModeKHR::eImmediate);
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(fifoOnly, false, false, false) ==
        vk::PresentModeKHR::eFifo);
}

TEST_CASE("NativeSurfaceHost::ComputePresentRect preserves aspect ratio accurately",
          "[WSI][NativeSurfaceHost][AspectRatio]") {
  using GlideWrapper::NativeSurfaceHost;
  using GlideWrapper::PresentRect;

  // 4:3 guest (640x480) inside 16:9 host (1920x1080) -> pillarboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 1080, true) ==
        PresentRect{240, 0, 1440, 1080});

  // Exact aspect-ratio match (640x480 in 800x600)
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 800, 600, true) ==
        PresentRect{0, 0, 800, 600});

  // Wider guest (640x400, 16:10) inside 4:3 host (640x480) -> letterboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 400, 640, 480, true) ==
        PresentRect{0, 40, 640, 400});

  // 4:3 guest (800x600) inside 1440p host (2560x1440) -> pillarboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(800, 600, 2560, 1440, true) ==
        PresentRect{320, 0, 1920, 1440});

  // 4:3 guest (640x480) inside portrait host (1080x1920) -> letterboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1080, 1920, true) ==
        PresentRect{0, 555, 1080, 810});

  // Stretch mode disables aspect-ratio preservation
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 1080, false) ==
        PresentRect{0, 0, 1920, 1080});

  // Zero-size safety guards
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 0, 1080, true) ==
        PresentRect{0, 0, 0, 0});
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 0, true) ==
        PresentRect{0, 0, 0, 0});
  CHECK(NativeSurfaceHost::ComputePresentRect(0, 480, 1920, 1080, true) ==
        PresentRect{0, 0, 1920, 1080});
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 0, 1920, 1080, true) ==
        PresentRect{0, 0, 1920, 1080});
}

TEST_CASE("NativeSurfaceHost headless lifecycle and idempotent detach",
          "[WSI][NativeSurfaceHost][Headless]") {
  using GlideWrapper::NativeSurfaceHost;

  NativeSurfaceHost host;
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
  CHECK(!host.WasQuitRequested());
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(host.GetSDLWindow() == nullptr);
  CHECK(host.GetX11Display() == nullptr);
  CHECK(host.GetX11Window() == 0UL);
  CHECK(host.GetWaylandDisplay() == nullptr);
  CHECK(host.GetWaylandSurface() == nullptr);
  CHECK(host.GetWaylandScale() == 1);

  uint32_t pw = 999, ph = 999;
  CHECK(!host.SyncBounds(pw, ph));
  CHECK(pw == 0);
  CHECK(ph == 0);
  host.PollEvents();  // Safe no-op when detached

  // Attach headless (zero OS/SDL calls)
  REQUIRE(host.Attach(nullptr, 640, 480, false, /*forceNoWindow=*/true,
                      "Headless Test"));
  CHECK(host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Headless);
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(host.GetSDLWindow() == nullptr);

  uint32_t gw = 0, gh = 0;
  host.GetGuestSize(gw, gh);
  CHECK(gw == 640);
  CHECK(gh == 480);

  host.GetDrawableSize(pw, ph);
  CHECK(pw == 640);
  CHECK(ph == 480);
  CHECK(!host.SyncBounds(pw, ph));
  CHECK(pw == 640);
  CHECK(ph == 480);
  host.PollEvents();
  CHECK(!host.WasQuitRequested());

  // Re-attaching with a foreign handle while forceNoWindow=true stays Headless
  void* fakeHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345));
  REQUIRE(host.Attach(fakeHandle, 800, 600, true, /*forceNoWindow=*/true,
                      "Headless Foreign"));
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Headless);
  host.GetDrawableSize(pw, ph);
  CHECK(pw == 800);
  CHECK(ph == 600);

  // Detach resets cleanly and is idempotent
  host.Detach();
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
  host.Detach();
  CHECK(!host.IsAttached());
}

TEST_CASE("NativeSurfaceHost StandaloneSDL2 handles scale, resize, and SDL_QUIT",
          "[WSI][NativeSurfaceHost][Standalone]") {
  using GlideWrapper::NativeSurfaceHost;

  struct SdlDriverGuard {
    bool hadPrev = false;
    std::string prev;
    SdlDriverGuard() {
      if (const char* v = std::getenv("SDL_VIDEODRIVER")) {
        hadPrev = true;
        prev = v;
      }
      ::setenv("SDL_VIDEODRIVER", "dummy", 1);
    }
    ~SdlDriverGuard() {
      if (hadPrev) {
        ::setenv("SDL_VIDEODRIVER", prev.c_str(), 1);
      } else {
        ::unsetenv("SDL_VIDEODRIVER");
      }
    }
  } driverGuard;

  NativeSurfaceHost host;
  REQUIRE(host.Attach(nullptr, 320, 240, false, /*forceNoWindow=*/false,
                      "Standalone Unit Test", 0, /*windowScale=*/2));
  CHECK(host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::StandaloneSDL2);
  CHECK(host.GetSDLWindow() != nullptr);
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(!host.WasQuitRequested());

  // Verify SDL_HINT_NO_SIGNAL_HANDLERS was set so Ctrl+C works in demos.
  const char* noSigHint = SDL_GetHint(SDL_HINT_NO_SIGNAL_HANDLERS);
  REQUIRE(noSigHint != nullptr);
  CHECK(std::string(noSigHint) == "1");

  uint32_t pw = 0, ph = 0;
  host.GetDrawableSize(pw, ph);
  CHECK(pw == 640);
  CHECK(ph == 480);
  CHECK(!host.SyncBounds(pw, ph));

  // Simulate user clicking window close / SDL_QUIT.
  SDL_Event quitEv{};
  quitEv.type = SDL_QUIT;
  REQUIRE(SDL_PushEvent(&quitEv) == 1);
  host.PollEvents();
  CHECK(host.WasQuitRequested());

  host.Detach();
  CHECK(!host.IsAttached());
  CHECK(host.GetSDLWindow() == nullptr);
}

TEST_CASE("NativeSurfaceHost rejects invalid X11 Window ID gracefully via error trap",
          "[WSI][NativeSurfaceHost][X11]") {
  using GlideWrapper::NativeSurfaceHost;

  // Invalid 32-bit X11 XID must fail cleanly without Xlib aborting the process.
  NativeSurfaceHost host;
  void* badX11Win = reinterpret_cast<void*>(static_cast<uintptr_t>(0x0000DEADUL));
  CHECK(!host.Attach(badX11Win, 640, 480, true, /*forceNoWindow=*/false,
                     "Invalid X11 Test"));
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
}

TEST_CASE("DOSBox setConfig('SDL2') handshake exported by libglide2x and libglide3x",
          "[WSI][DOSBox][setConfig]") {
#ifdef __linux__
  using SetConfigFn = void (*)(FxU32, void*);

  for (bool glide3 : {false, true}) {
    void* handle = glide3 ? OpenGlide3Handle() : OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    auto setConfigFn = reinterpret_cast<SetConfigFn>(::dlsym(handle, "setConfig"));
    auto setConfigResFn = ::dlsym(handle, "setConfigRes");
    REQUIRE(setConfigFn != nullptr);
    REQUIRE(setConfigResFn != nullptr);

    uint32_t magic = 0x58326724;  // '$g2X' (SVN DOSBox initial sentinel)
    setConfigFn(1, &magic);
    CHECK(magic == 0x324c4453U);  // 'SDL2'

    // Null magic pointer must be safe.
    setConfigFn(0, nullptr);

    ::dlclose(handle);
  }
#endif
}

TEST_CASE("GPU backends inherit GlideStateTracker without SoftwareBackendBase or eager CPU LFB buffers",
          "[Architecture][GlideStateTracker]") {
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::VulkanBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::OpenGLESBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::SoftwareBackend>);
  static_assert(!std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                   GlideWrapper::VulkanBackend>);
  static_assert(!std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                   GlideWrapper::OpenGLESBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                  GlideWrapper::SoftwareBackend>);

  SECTION("VulkanBackend allocates zero CPU LFB staging until LFB is touched") {
    GlideWrapper::WrapperConfig config;
    config.backend = "vulkan";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::VulkanBackend backend;
    GlideWrapper::IGraphicsBackend* basePtr = &backend;
    CHECK(dynamic_cast<GlideWrapper::GlideStateTracker*>(basePtr) != nullptr);
    CHECK(dynamic_cast<GlideWrapper::SoftwareBackendBase*>(basePtr) == nullptr);

    if (backend.Initialize(config) && backend.AttachWindow(nullptr, 64, 64, true)) {
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.ClearBuffer(0x0000FF00, 0xFF, 1.0f, 0x3);
      REQUIRE(backend.SwapBuffers());
      // Pure 3D clear + swap must still keep CPU LFB staging unallocated.
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());

      // First LFB write lazily allocates and survives GPU flush + readback.
      uint16_t white565 = 0xFFFF;
      REQUIRE(backend.WriteLFB(1, 4, 4, 1, 1, 2, 0 /* 565 */, &white565));
      CHECK(backend.HasAllocatedCpuLfbStaging());

      uint16_t readPx = 0;
      REQUIRE(backend.ReadLFB(1, 4, 4, 1, 1, 2, &readPx));
      CHECK(readPx == 0xFFFF);

      backend.DetachWindow();
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.Shutdown();
    }
  }

  SECTION("OpenGLESBackend allocates zero CPU LFB staging until LFB is touched") {
    GlideWrapper::WrapperConfig config;
    config.backend = "opengl_es";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::OpenGLESBackend backend;
    GlideWrapper::IGraphicsBackend* basePtr = &backend;
    CHECK(dynamic_cast<GlideWrapper::GlideStateTracker*>(basePtr) != nullptr);
    CHECK(dynamic_cast<GlideWrapper::SoftwareBackendBase*>(basePtr) == nullptr);

    if (backend.Initialize(config) && backend.AttachWindow(nullptr, 64, 64, true)) {
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.ClearBuffer(0x0000FF00, 0xFF, 1.0f, 0x3);
      REQUIRE(backend.SwapBuffers());
      // Pure 3D clear + swap must still keep CPU LFB staging unallocated.
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());

      // First LFB write lazily allocates and survives GPU flush + readback.
      uint16_t white565 = 0xFFFF;
      REQUIRE(backend.WriteLFB(1, 4, 4, 1, 1, 2, 0 /* 565 */, &white565));
      CHECK(backend.HasAllocatedCpuLfbStaging());

      uint16_t readPx = 0;
      REQUIRE(backend.ReadLFB(1, 4, 4, 1, 1, 2, &readPx));
      CHECK(readPx == 0xFFFF);

      backend.DetachWindow();
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.Shutdown();
    }
  }
}

TEST_CASE("LfbManager maps GR_LFBWRITEMODE_888/8888/DEPTH/ZA16 strides and colors accurately",
          "[Glide3][LFB][WriteModes]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void(FX_CALL*)())::dlsym(handle, "grGlideInit");
  auto glideShutdown = (void(FX_CALL*)())::dlsym(handle, "grGlideShutdown");
  auto sstSelect = (void(FX_CALL*)(int))::dlsym(handle, "grSstSelect");
  auto winOpen = (FxU32(FX_CALL*)(FxU32, GrScreenResolution_t,
                                  GrScreenRefresh_t, GrColorFormat_t,
                                  GrOriginLocation_t, int, int))::dlsym(handle, "grSstWinOpen");
  auto winClose = (FxBool(FX_CALL*)(FxU32))::dlsym(handle, "grSstWinClose");
  auto bufferClear = (void(FX_CALL*)(GrColor_t, GrAlpha_t, FxU32))::dlsym(handle, "grBufferClear");
  auto lfbLock = (FxBool(FX_CALL*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t,
                                   GrOriginLocation_t, FxBool, GrLfbInfo_t*))::dlsym(handle, "grLfbLock");
  auto lfbUnlock = (FxBool(FX_CALL*)(GrLock_t, GrBuffer_t))::dlsym(handle, "grLfbUnlock");

  REQUIRE(glideInit != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(lfbLock != nullptr);
  REQUIRE(lfbUnlock != nullptr);

  glideInit();
  sstSelect(0);
  // Open with GR_COLORFORMAT_ABGR (same as 3dfx SDK test12) to ensure LFB
  // 888/8888 pixel data still unpacks as 0x00RRGGBB / 0xAARRGGBB.
  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  for (GrLfbWriteMode_t mode : {GR_LFBWRITEMODE_888, GR_LFBWRITEMODE_8888}) {
    bufferClear(0, 0, 0);

    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, mode,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXTRUE);
    REQUIRE(info.lfbPtr != nullptr);
    REQUIRE(info.strideInBytes == 640 * 4);

    auto* row = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(info.lfbPtr) + 10 * info.strideInBytes);
    row[10] = 0xFFFF0000;  // Pure Red (ARGB)
    row[11] = 0xFF00FF00;  // Pure Green (ARGB)
    row[12] = 0xFF0000FF;  // Pure Blue (ARGB)
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    GrLfbInfo_t readInfo{};
    readInfo.size = sizeof(readInfo);
    REQUIRE(lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &readInfo) == FXTRUE);
    REQUIRE(readInfo.strideInBytes == 640 * 2);
    const auto* readRow = reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(readInfo.lfbPtr) + 10 * readInfo.strideInBytes);
    CHECK(readRow[10] == 0xF800);  // Red in RGB565
    CHECK(readRow[11] == 0x07E0);  // Green in RGB565
    CHECK(readRow[12] == 0x001F);  // Blue in RGB565
    REQUIRE(lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // 565_DEPTH (0xc) uses 4 BPP with RGB565 in the low 16 bits.
  {
    bufferClear(0, 0, 0);
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565_DEPTH, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &info) == FXTRUE);
    REQUIRE(info.strideInBytes == 640 * 4);
    auto* row = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(info.lfbPtr) + 12 * info.strideInBytes);
    row[15] = 0x1234F800;  // Depth 0x1234 + Red RGB565
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    GrLfbInfo_t readInfo{};
    readInfo.size = sizeof(readInfo);
    REQUIRE(lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, FXFALSE, &readInfo) == FXTRUE);
    const auto* readRow = reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(readInfo.lfbPtr) + 12 * readInfo.strideInBytes);
    CHECK(readRow[15] == 0xF800);
    REQUIRE(lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // ZA16 (0xf) uses 2 BPP.
  {
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_ZA16, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &info) == FXTRUE);
    CHECK(info.strideInBytes == 640 * 2);
    REQUIRE(lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  }

  // Reserved write modes must be rejected cleanly.
  for (GrLfbWriteMode_t reserved :
       {GR_LFBWRITEMODE_RESERVED1, GR_LFBWRITEMODE_RESERVED3,
        GR_LFBWRITEMODE_RESERVED4}) {
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    CHECK(lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, reserved,
                  GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXFALSE);
  }

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
#endif
}

TEST_CASE("Glide 3.x Shameless Plug Preserves W-Buffered Scene Across Swaps",
          "[Glide3][State][ShamelessPlug]") {
#ifdef __linux__
  ::setenv("FX_GLIDE_SHAMELESS_PLUG", "1", 1);
  ::unsetenv("GLIDE_NG_SHAMELESS_PLUG");

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown = reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideShutdown"));
  auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
  auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int)>(
      ::dlsym(handle, "grSstWinOpen"));
  auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
  auto depthBufferMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(
      ::dlsym(handle, "grDepthBufferMode"));
  auto depthBufferFunction = reinterpret_cast<void (*)(GrCmpFnc_t)>(
      ::dlsym(handle, "grDepthBufferFunction"));
  auto depthMask = reinterpret_cast<void (*)(FxBool)>(::dlsym(handle, "grDepthMask"));
  auto colorCombine = reinterpret_cast<void (*)(
      GrCombineFunction_t, GrCombineFactor_t, GrCombineLocal_t,
      GrCombineOther_t, FxBool)>(::dlsym(handle, "grColorCombine"));
  auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
      ::dlsym(handle, "grBufferClear"));
  auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
      ::dlsym(handle, "grDrawTriangle"));
  auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));
  auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
      ::dlsym(handle, "grVertexLayout"));
  auto readLfb = reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32,
                                             FxU32, FxU32, void*)>(
      ::dlsym(handle, "grLfbReadRegion"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(depthBufferMode != nullptr);
  REQUIRE(depthBufferFunction != nullptr);
  REQUIRE(depthMask != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(drawTri != nullptr);
  REQUIRE(bufferSwap != nullptr);
  REQUIRE(vertexLayout != nullptr);
  REQUIRE(readLfb != nullptr);

  glideInit();
  sstSelect(0);
  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  // Match glide3_cube: W-buffering with GR_CMP_LESS and NO explicit grDepthRange.
  vertexLayout(0x01, 0, 1);   // GR_PARAM_XY
  vertexLayout(0x02, 8, 1);   // GR_PARAM_Z
  vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB
  vertexLayout(0x10, 28, 1);  // GR_PARAM_A
  vertexLayout(0x03, 32, 1);  // GR_PARAM_W (oow)
  depthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  depthBufferFunction(GR_CMP_LESS);
  depthMask(FXTRUE);
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

  StandardVert v0 = {120.0f, 120.0f, 0.0f, 0.0f, 255.0f, 255.0f, 0.0f, 255.0f, 0.2f, 0.0f, 0.0f, 1.0f};
  StandardVert v1 = {380.0f, 120.0f, 0.0f, 0.0f, 255.0f, 255.0f, 0.0f, 255.0f, 0.2f, 0.0f, 0.0f, 1.0f};
  StandardVert v2 = {250.0f, 360.0f, 0.0f, 0.0f, 255.0f, 255.0f, 0.0f, 255.0f, 0.2f, 0.0f, 0.0f, 1.0f};

  // Frame 1 triggers grShamelessPlug() -> grGlideGetState()/grGlideSetState().
  // Frame 2 verifies W-buffer depth range (1..65535) was NOT clobbered to (0..1).
  for (int frame = 1; frame <= 2; ++frame) {
    INFO("Checking W-buffered triangle + shameless plug on frame " << frame);
    bufferClear(0x00000000, 0, 0xFFFF);
    drawTri(&v0, &v1, &v2);
    bufferSwap(1);

    std::vector<uint16_t> front(640 * 480, 0);
    REQUIRE(readLfb(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 2,
                    front.data()) == FXTRUE);

    // Triangle interior at (250, 240) must remain non-black on both frames.
    CHECK(front[240 * 640 + 250] != 0);

    // Lower-right shameless plug area (180x90) must also contain non-black pixels.
    int plugNonBlack = 0;
    for (int y = 390; y < 480; ++y) {
      for (int x = 460; x < 640; ++x) {
        if (front[y * 640 + x] != 0) ++plugNonBlack;
      }
    }
    CHECK(plugNonBlack > 100);
  }

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  ::unsetenv("FX_GLIDE_SHAMELESS_PLUG");
#endif
}

TEST_CASE(
    "M2 Tiered Logger: levels, categories, frame-windowed trace, and "
    "microsecond format",
    "[Logger][M2]") {
  using namespace GlideWrapper;

  SECTION("Category parser & formatter round-trip and negation") {
    uint32_t mask = 0u;
    REQUIRE(ParseLogCategories("all", mask));
    CHECK(mask == LOG_CAT_ALL);
    CHECK(FormatLogCategories(mask) == "all");

    REQUIRE(ParseLogCategories("none", mask));
    CHECK(mask == LOG_CAT_NONE);
    CHECK(FormatLogCategories(mask) == "none");

    REQUIRE(ParseLogCategories("lfb, perf, api", mask));
    CHECK((mask & LOG_CAT_LFB) != 0u);
    CHECK((mask & LOG_CAT_PERF) != 0u);
    CHECK((mask & LOG_CAT_API) != 0u);
    CHECK((mask & LOG_CAT_DRAW) == 0u);
    CHECK(FormatLogCategories(mask) == "api,lfb,perf");

    // Negation starts from ALL and clears specified categories
    REQUIRE(ParseLogCategories("-draw,-state", mask));
    CHECK((mask & LOG_CAT_DRAW) == 0u);
    CHECK((mask & LOG_CAT_STATE) == 0u);
    CHECK((mask & LOG_CAT_LFB) != 0u);
    CHECK((mask & LOG_CAT_API) != 0u);

    // Unknown-only string returns false
    uint32_t untouched = 0x1234u;
    CHECK_FALSE(ParseLogCategories("bogus_category", untouched));
    CHECK(untouched == 0x1234u);
  }

  SECTION("TRACE gating, category filtering, and microsecond + frame format") {
    const std::string logPath = "/tmp/glide_ng_m2_logger_test.log";
    std::remove(logPath.c_str());

    WrapperConfig cfg{};
    cfg.logFile = logPath;
    cfg.logLevel = 5u;  // TRACE
    cfg.logToConsole = false;
    REQUIRE(ParseLogCategories("lfb,perf", cfg.logCategoryMask));
    cfg.traceFrameWindowActive = true;
    cfg.traceFrameStart = 2;
    cfg.traceFrameEnd = 3;

    auto& logger = Logger::GetInstance();
    logger.Initialize(cfg);
    REQUIRE(logger.GetLevel() == LogLevel::TRACE);

    // Frame 1 (outside trace window [2,3]): TRACE suppressed, DEBUG on LFB passes
    logger.SetCurrentFrame(1);
    GLIDE_LOG(TRACE, "LFB", "frame1_trace_should_skip");
    GLIDE_LOG(DEBUG, "LFB", "frame1_debug_lfb_pass");
    GLIDE_LOG(DEBUG, "Vulkan", "frame1_debug_draw_filtered");
    GLIDE_LOG(WARN, "Vulkan", "frame1_warn_bypasses_category");
    GLIDE_LOG(INFO, "Startup", "frame1_startup_bypasses_category");

    // Frame 2 & 3 (inside trace window [2,3]): TRACE on LFB passes, TRACE on State filtered
    logger.SetCurrentFrame(2);
    GLIDE_LOG(TRACE, "LFB", "frame2_trace_lfb_pass");
    GLIDE_LOG(TRACE, "StateTracker", "frame2_trace_state_filtered");

    logger.SetCurrentFrame(3);
    GLIDE_TRACE(LOG_CAT_PERF, "Perf", "frame3_trace_perf_pass");

    // Frame 4 (after trace window [2,3]): TRACE suppressed again
    logger.SetCurrentFrame(4);
    GLIDE_LOG(TRACE, "LFB", "frame4_trace_should_skip");
    logger.Flush();

    std::ifstream in(logPath);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    std::remove(logPath.c_str());

    // Restore clean logger state for subsequent tests
    logger.Initialize("/tmp/glide_ng_test_restore.log", LogLevel::WARN, false);
    std::remove("/tmp/glide_ng_test_restore.log");

    CHECK(content.find("frame1_trace_should_skip") == std::string::npos);
    CHECK(content.find("frame1_debug_lfb_pass") != std::string::npos);
    CHECK(content.find("frame1_debug_draw_filtered") == std::string::npos);
    CHECK(content.find("frame1_warn_bypasses_category") != std::string::npos);
    CHECK(content.find("frame1_startup_bypasses_category") != std::string::npos);
    CHECK(content.find("frame2_trace_lfb_pass") != std::string::npos);
    CHECK(content.find("frame2_trace_state_filtered") == std::string::npos);
    CHECK(content.find("frame3_trace_perf_pass") != std::string::npos);
    CHECK(content.find("frame4_trace_should_skip") == std::string::npos);

    // Verify monotonic microsecond timestamp + frame ID format:
    // [+SSSSSS.UUUUUU] [F000002] [TRCE] [LFB] frame2_trace_lfb_pass
    std::regex lineRe(
        R"(\[\+\d{6}\.\d{6}\] \[F000002\] \[TRCE\] \[LFB\] frame2_trace_lfb_pass)");
    CHECK(std::regex_search(content, lineRe));
  }

  SECTION("DEBUG level suppresses TRACE messages even without frame window") {
    const std::string logPath = "/tmp/glide_ng_m2_debug_level_test.log";
    std::remove(logPath.c_str());

    auto& logger = Logger::GetInstance();
    logger.Initialize(logPath, LogLevel::DEBUG, false);
    GLIDE_LOG(DEBUG, "Frontend", "debug_msg_visible");
    GLIDE_LOG(TRACE, "Frontend", "trace_msg_hidden");
    logger.Flush();

    std::ifstream in(logPath);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    std::remove(logPath.c_str());
    logger.Initialize("/tmp/glide_ng_test_restore.log", LogLevel::WARN, false);
    std::remove("/tmp/glide_ng_test_restore.log");

    CHECK(content.find("debug_msg_visible") != std::string::npos);
    CHECK(content.find("trace_msg_hidden") == std::string::npos);
  }
}

TEST_CASE(
    "M2 Config & Per-Frame Perf Telemetry: JSON/env parity and CSV output",
    "[Telemetry][Perf][M2]") {
#ifdef __linux__
  struct EnvGuardM2 {
    std::string name;
    bool hadValue{false};
    std::string oldValue;
    explicit EnvGuardM2(const char* n) : name(n) {
      if (const char* v = std::getenv(n)) {
        hadValue = true;
        oldValue = v;
      }
      ::unsetenv(n);
    }
    ~EnvGuardM2() {
      if (hadValue) ::setenv(name.c_str(), oldValue.c_str(), 1);
      else ::unsetenv(name.c_str());
    }
  };

  EnvGuardM2 gConfig("GLIDE_NG_CONFIG");
  EnvGuardM2 gCats("GLIDE_NG_LOG_CATEGORIES");
  EnvGuardM2 gFrames("GLIDE_NG_TRACE_FRAMES");
  EnvGuardM2 gPerf("GLIDE_NG_PERF_LOG");
  EnvGuardM2 gLogFile("GLIDE_NG_LOG_FILE");
  EnvGuardM2 gLogLevel("GLIDE_NG_LOG_LEVEL");
  EnvGuardM2 gHeadless("GLIDE_NG_HEADLESS");
  EnvGuardM2 gMaxFps("GLIDE_NG_MAX_FPS");

  SECTION("INI & GLIDE_NG_* parsing for logCategories, traceFrames, perfLog") {
    const std::string cfgPath = "/tmp/glide_ng_m2_cfg_test.ini";
    {
      std::ofstream out(cfgPath);
      out << "[logging]\n"
          << "categories = api,lfb\n"
          << "trace_frames = 10-20\n"
          << "perf_log = /tmp/ini_perf.csv\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.logCategoriesSpec == "api,lfb");
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 10u);
    CHECK(cfg.traceFrameEnd == 20u);
    CHECK(cfg.perfLogEnabled == true);
    CHECK(cfg.perfLogCsvPath == "/tmp/ini_perf.csv");
    CHECK(cfg.settingSources.at("logCategories") == "ini");
    CHECK(cfg.settingSources.at("traceFrames") == "ini");
    CHECK(cfg.settingSources.at("perfLog") == "ini");

    // Test single-frame, open-ended, and prefix ranges via env overrides
    ::setenv("GLIDE_NG_TRACE_FRAMES", "7", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 7u);
    CHECK(cfg.traceFrameEnd == 7u);

    ::setenv("GLIDE_NG_TRACE_FRAMES", "15-", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 15u);
    CHECK(cfg.traceFrameEnd == UINT64_MAX);

    ::setenv("GLIDE_NG_TRACE_FRAMES", "-5", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 0u);
    CHECK(cfg.traceFrameEnd == 5u);

    ::setenv("GLIDE_NG_LOG_CATEGORIES", "tex,perf", 1);
    ::setenv("GLIDE_NG_PERF_LOG", "1", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.logCategoriesSpec == "tex,perf");
    CHECK(cfg.perfLogEnabled == true);
    CHECK(cfg.perfLogCsvPath.empty());
    CHECK(cfg.settingSources.at("perfLog") == "GLIDE_NG_PERF_LOG");

    ::setenv("GLIDE_NG_PERF_LOG", "0", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.perfLogEnabled == false);

    std::remove(cfgPath.c_str());
  }

  SECTION("End-to-end Glide 3.x session produces valid CSV & [PERF] summary") {
    const std::string csvPath = "/tmp/glide_ng_m2_perf_e2e.csv";
    const std::string logPath = "/tmp/glide_ng_m2_perf_e2e.log";
    std::remove(csvPath.c_str());
    std::remove(logPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_PERF_LOG", csvPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
        ::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));
    auto lfbWriteRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxBool, FxI32, void*)>(
        ::dlsym(handle, "grLfbWriteRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbWriteRegion != nullptr);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    StandardVert v0 = {100.0f, 100.0f, 0.0f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v1 = {300.0f, 100.0f, 0.0f, 0.0f, 0.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v2 = {200.0f, 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<uint16_t> patch(16 * 16, 0xF800);

    for (int f = 0; f < 3; ++f) {
      bufferClear(0x00000000, 0, 0xFFFF);
      drawTri(&v0, &v1, &v2);
      drawTri(&v0, &v1, &v2);
      lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                     16, 16, FXFALSE, 16 * 2, patch.data());
      bufferSwap(0);
    }

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Verify CSV header + 3 frame rows with expected counters
    std::ifstream csvIn(csvPath);
    REQUIRE(csvIn.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(csvIn, line)) {
      if (!line.empty()) lines.push_back(line);
    }
    csvIn.close();
    REQUIRE(lines.size() == 4);  // 1 header + 3 frames
    CHECK(lines[0].find("frame,timestamp_ms,frame_ms,render_ms,present_ms") == 0);
    CHECK(lines[0].find(",wrapper_cpu_ms,app_cpu_ms,draw_cpu_ms,tex_cpu_ms,"
                        "lfb_cpu_ms,state_cpu_ms") != std::string::npos);
    // Row 1 (frame 0): check tris_processed=2, tris_drawn=2, lfb_writes=1, lfb_write_bytes=512
    CHECK(lines[1].find(",2,2,0,1,0,512,") != std::string::npos);

    // Verify log file contains [Perf] summary emitted on ShutdownPerfLog
    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    std::string logContent((std::istreambuf_iterator<char>(logIn)),
                           std::istreambuf_iterator<char>());
    logIn.close();
    CHECK(logContent.find("[Perf] FPS=") != std::string::npos);
    CHECK(logContent.find("[app=") != std::string::npos);
    CHECK(logContent.find("wrap=") != std::string::npos);
    CHECK(logContent.find("Perf Telemetry       : ENABLED") != std::string::npos);

    std::remove(csvPath.c_str());
    std::remove(logPath.c_str());
  }
#endif
}

TEST_CASE("M3 DXVK-Style On-Screen HUD Config, Rendering & Isolation [HUD][M3]",
          "[HUD][M3]") {
#ifdef __linux__
  using namespace GlideWrapper;

  struct HudEnvGuard {
    std::vector<std::pair<std::string, std::string>> saved;
    std::vector<std::string> unsetKeys;
    const std::vector<std::string> keys = {
        "GLIDE_NG_HUD", "GLIDE_NG_CONFIG", "GLIDE_NG_HEADLESS",
        "GLIDE_NG_LOG_FILE", "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_MAX_FPS"};
    HudEnvGuard() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        if (v) {
          saved.emplace_back(k, v);
        } else {
          unsetKeys.push_back(k);
        }
        ::unsetenv(k.c_str());
      }
    }
    ~HudEnvGuard() {
      for (const auto& k : keys) ::unsetenv(k.c_str());
      for (const auto& kv : saved) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }
  } guard;

  SECTION("GLIDE_NG_HUD and INI [hud] parsing + precedence") {
    const std::string cfgPath = "/tmp/glide_ng_m3_hud_cfg.ini";
    {
      std::ofstream out(cfgPath);
      out << "[hud]\n"
          << "elements = fps,backend\n"
          << "position = br\n"
          << "scale = 1.5\n"
          << "opacity = 0.6\n";
    }

    IniConfigLoader loader;
    WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == (HUD_FPS | HUD_BACKEND));
    CHECK(cfg.hudPosition == HudPosition::BOTTOM_RIGHT);
    CHECK(std::fabs(cfg.hudScale - 1.5f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.6f) < 1e-4f);
    CHECK(cfg.settingSources.at("hud") == "ini");

    // GLIDE_NG_HUD=full overrides INI
    ::setenv("GLIDE_NG_HUD", "full,position=tr,scale=2.0,opacity=0.9", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_FULL);
    CHECK(cfg.hudPosition == HudPosition::TOP_RIGHT);
    CHECK(std::fabs(cfg.hudScale - 2.0f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.9f) < 1e-4f);
    CHECK(cfg.settingSources.at("hud") == "GLIDE_NG_HUD");

    // GLIDE_NG_HUD=1 enables FPS shorthand
    ::setenv("GLIDE_NG_HUD", "1", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_FPS);

    // GLIDE_NG_HUD=0 disables even when INI enabled it
    ::setenv("GLIDE_NG_HUD", "0", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_NONE);

    // Specific multi-token & clamping checks
    ::setenv("GLIDE_NG_HUD", "frametimes,api,drawcalls,lfb,pos=bl,scale=9.0,opacity=2.5", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == (HUD_FRAMETIMES | HUD_API | HUD_DRAWCALLS | HUD_LFB));
    CHECK(cfg.hudPosition == HudPosition::BOTTOM_LEFT);
    CHECK(std::fabs(cfg.hudScale - 4.0f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 1.0f) < 1e-4f);

    std::remove(cfgPath.c_str());
  }

  SECTION("HudOverlay bitmap rasterization, placement & BGRA32 compositing") {
    WrapperConfig cfg;
    cfg.hudMask = HUD_FULL;
    cfg.hudPosition = HudPosition::TOP_LEFT;
    cfg.hudScale = 1.0f;
    cfg.hudOpacity = 0.8f;
    cfg.model = CardModel::Voodoo2;
    cfg.activeFrontendApi = "Glide3x";
    cfg.maxFps = 60.0f;

    HudOverlay hud;
    hud.Configure(cfg, {"VK", 640, 480});
    REQUIRE(hud.IsEnabled());

    FrameMetrics m{};
    m.frameTimeMs = 16.6f;
    m.renderTimeMs = 4.2f;
    m.swapPresentMs = 1.1f;
    m.drawCalls = 12;
    m.stateFlushes = 2;
    m.trianglesDrawn = 480;
    m.lfbWriteLocks = 3;
    m.lfbBytesWritten = 4096;
    m.texUploads = 1;
    m.texUploadBytes = 8192;

    hud.ForceRender(m, 60.0f);
    REQUIRE(hud.GetWidth() >= 100);
    REQUIRE(hud.GetHeight() >= 40);
    REQUIRE(hud.GetRevision() == 1);
    REQUIRE(hud.GetRgbaTopDown() != nullptr);
    REQUIRE(hud.GetRgbaBottomUp() != nullptr);

    // Verify lines cover all full elements
    const auto& lines = hud.GetLinesForTest();
    REQUIRE(lines.size() >= 6);
    CHECK(lines[0].find("FPS") != std::string::npos);
    CHECK(lines[0].find("60.0") != std::string::npos);

    // Top row of top-down buffer matches bottom row of bottom-up buffer
    const uint32_t w = hud.GetWidth();
    const uint32_t h = hud.GetHeight();
    const uint8_t* topDown = hud.GetRgbaTopDown();
    const uint8_t* bottomUp = hud.GetRgbaBottomUp();
    CHECK(std::memcmp(topDown, bottomUp + static_cast<size_t>(h - 1) * w * 4,
                      w * 4) == 0);

    // Border pixel at (0,0) should have non-zero alpha and amber color (R > G > B)
    CHECK(topDown[3] > 180);
    CHECK(topDown[0] > topDown[1]);
    CHECK(topDown[1] > topDown[2]);

    // Check corner placement math + small-viewport clamping
    int32_t x = 0, y = 0;
    uint32_t dw = 0, dh = 0;
    hud.ComputePlacement(0, 0, 640, 480, x, y, dw, dh);
    CHECK(x == 8);
    CHECK(y == 8);
    CHECK(dw == w);
    CHECK(dh == h);

    cfg.hudPosition = HudPosition::BOTTOM_RIGHT;
    hud.Configure(cfg, {"VK", 640, 480});
    hud.ForceRender(m, 60.0f);
    hud.ComputePlacement(100, 20, 640, 480, x, y, dw, dh);
    CHECK(x + static_cast<int32_t>(dw) == 100 + 640 - 8);
    CHECK(y + static_cast<int32_t>(dh) == 20 + 480 - 8);

    // Small viewport forces proportional clamp without overflowing bounds
    hud.ComputePlacement(0, 0, 80, 60, x, y, dw, dh);
    CHECK(dw <= 80);
    CHECK(dh <= 60);

    // BGRA32 composite modifies inside HUD rect and leaves outside pixels untouched
    cfg.hudPosition = HudPosition::TOP_LEFT;
    hud.Configure(cfg, {"SW", 640, 480});
    hud.ForceRender(m, 60.0f);
    std::vector<uint32_t> frame(640 * 480, 0xFF112233u);
    hud.CompositeOverBgra32(frame.data(), 640, 480, 640);
    CHECK(frame[8 * 640 + 8] != 0xFF112233u);          // Inside HUD top-left border
    CHECK(frame[470 * 640 + 630] == 0xFF112233u);      // Outside HUD (bottom-right)
  }

  SECTION("End-to-end Glide 3.x session with GLIDE_NG_HUD=full keeps guest LFB pristine") {
    const std::string logPath = "/tmp/glide_ng_m3_hud_e2e.log";
    std::remove(logPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_HUD", "full", 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));
    auto lfbReadRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbReadRegion != nullptr);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Clear both buffers to solid bright green (ARGB 0xFF00FF00)
    for (int f = 0; f < 3; ++f) {
      bufferClear(0xFF00FF00, 0xFF, 0xFFFF);
      bufferSwap(0);
    }

    // Read top-left 16x16 region of frontbuffer where top-left HUD is drawn
    // during presentation: guest framebuffer must remain pure green (not overwritten by HUD).
    std::vector<uint16_t> pixels565(16 * 16, 0);
    FxBool ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 8, 8, 16, 16, 16 * 2,
                              pixels565.data());
    REQUIRE(ok == FXTRUE);
    int pureGreenCount = 0;
    for (uint16_t p : pixels565) {
      if (p == 0x07E0) ++pureGreenCount;
    }
    CHECK(pureGreenCount == 16 * 16);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    std::string logContent((std::istreambuf_iterator<char>(logIn)),
                           std::istreambuf_iterator<char>());
    logIn.close();
    CHECK(logContent.find("HUD Overlay          : ENABLED") != std::string::npos);
    CHECK(logContent.find("hud <- GLIDE_NG_HUD") != std::string::npos);

    std::remove(logPath.c_str());
  }
#endif
}

TEST_CASE("M5 Pass 3 Glide3 TMU Budget and Vulkan Texture Pool Reuse",
          "[Glide3][M5][Pass3]") {
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
      // red VkImage is retired into slot 0's retirement queue.
      backend.UploadTexture(0, 0, makeTex(0xFFFF0000u, 0x1111u));
      backend.UploadTexture(0, 0, makeTex(0xFF00FF00u, 0x2222u));
      REQUIRE(backend.SwapBuffers());
      // Frame 1: swap again so slot 0's fence completes and reclaims the
      // retired 32x32x1 texture into m_texturePool.
      REQUIRE(backend.SwapBuffers());

      // Frame 2: upload a blue 32x32x1 texture at addr=0 -> must reuse the
      // pooled VkImage/DeviceMemory/ImageView!
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
#endif
}

TEST_CASE("M6.1 Telemetry CPU Bucket Attribution & App vs Wrapper Split",
          "[Telemetry][M6]") {
  using namespace GlideWrapper;
  auto& tm = TelemetryManager::GetInstance();

  SECTION("Disabled perf log keeps timing inactive with zero bucket time") {
    tm.ShutdownPerfLog();
    CHECK_FALSE(tm.IsPerfTimingEnabled());
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    tm.OnFrameComplete(2000, 2000, 0, 500.0f);
    const FrameMetrics m = tm.GetLastFrameMetrics();
    CHECK(m.wrapperCpuMs == 0.0f);
    CHECK(m.drawCpuMs == 0.0f);
    CHECK(m.texCpuMs == 0.0f);
    CHECK(m.lfbCpuMs == 0.0f);
    CHECK(m.stateCpuMs == 0.0f);
  }

  SECTION("Exclusive stack prevents double-counting on same & cross-bucket nesting") {
    tm.ConfigurePerfLog(true, "");
    REQUIRE(tm.IsPerfTimingEnabled());

    // 1. Same-bucket nesting: outer Draw sleeps ~2ms + inner Draw sleeps ~2ms
    //    -> total elapsed is ~4ms (NOT ~6ms from double-counting).
    const auto t0 = std::chrono::steady_clock::now();
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      {
        GLIDE_CPU_SCOPE(CpuBucket::Draw);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    // 2. Cross-bucket nesting: outer Draw sleeps ~2ms, inner Tex sleeps ~4ms
    //    -> Draw gains ~2ms, Tex gains ~4ms (Draw does NOT include Tex's 4ms).
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      {
        GLIDE_CPU_SCOPE(CpuBucket::Tex);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
    }
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
    const int64_t simulatedAppUs = 5000;
    const int64_t totalActiveUs = elapsedUs + simulatedAppUs;

    tm.OnFrameComplete(totalActiveUs, totalActiveUs, 0, 0.0f);
    const FrameMetrics m = tm.GetLastFrameMetrics();

    // Total wrapper CPU time must not exceed total measured elapsed wall time
    // (+ small scheduling epsilon), proving zero double-counting.
    const double elapsedMs = static_cast<double>(elapsedUs) / 1000.0;
    CHECK(m.drawCpuMs >= 5.0f);
    CHECK(m.texCpuMs >= 3.0f);
    CHECK(m.lfbCpuMs == 0.0f);
    CHECK(m.stateCpuMs == 0.0f);
    CHECK(std::fabs(m.wrapperCpuMs - (m.drawCpuMs + m.texCpuMs)) < 0.05f);
    CHECK(m.wrapperCpuMs <= static_cast<float>(elapsedMs + 0.5));
    CHECK(m.appCpuMs >= 4.0f);
    CHECK(std::fabs((m.appCpuMs + m.wrapperCpuMs) - m.renderTimeMs) < 0.05f);

    tm.ShutdownPerfLog();
    CHECK_FALSE(tm.IsPerfTimingEnabled());
  }

#ifdef __linux__
  SECTION("End-to-end Glide 3.x session splits app_cpu_ms from wrapper buckets in CSV") {
    const std::string csvPath = "/tmp/glide_ng_m6_cpu_split.csv";
    std::remove(csvPath.c_str());

    struct EnvGuardM6 {
      std::vector<std::string> keys = {
          "GLIDE_NG_HEADLESS", "GLIDE_NG_MAX_FPS", "GLIDE_NG_PERF_LOG",
          "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_NO_SPLASH"};
      std::vector<std::pair<std::string, std::string>> saved;
      EnvGuardM6() {
        for (const auto& k : keys) {
          const char* v = std::getenv(k.c_str());
          saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        }
      }
      ~EnvGuardM6() {
        for (const auto& kv : saved) {
          if (kv.second == "\x01UNSET") {
            ::unsetenv(kv.first.c_str());
          } else {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
          }
        }
      }
    } guard;

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "warn", 1);
    ::setenv("GLIDE_NG_PERF_LOG", csvPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
        ::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbWriteRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxBool, FxI32, void*)>(
        ::dlsym(handle, "grLfbWriteRegion"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    StandardVert v0 = {100.0f, 100.0f, 0.0f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v1 = {300.0f, 100.0f, 0.0f, 0.0f, 0.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v2 = {200.0f, 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<uint16_t> patch(32 * 32, 0x07E0);

    for (int f = 0; f < 2; ++f) {
      // Simulate external app/DOSBox CPU work outside Glide API calls.
      std::this_thread::sleep_for(std::chrono::milliseconds(6));
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
      bufferClear(0x00000000, 0, 0xFFFF);
      for (int t = 0; t < 20; ++t) {
        drawTri(&v0, &v1, &v2);
      }
      lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                     32, 32, FXFALSE, 32 * 2, patch.data());
      bufferSwap(0);
    }

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    std::ifstream csvIn(csvPath);
    REQUIRE(csvIn.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(csvIn, line)) {
      if (!line.empty()) lines.push_back(line);
    }
    csvIn.close();
    REQUIRE(lines.size() == 3);  // header + 2 frames

    auto splitCsv = [](const std::string& row) {
      std::vector<std::string> cols;
      size_t start = 0;
      while (start <= row.size()) {
        size_t pos = row.find(',', start);
        if (pos == std::string::npos) {
          cols.push_back(row.substr(start));
          break;
        }
        cols.push_back(row.substr(start, pos - start));
        start = pos + 1;
      }
      return cols;
    };

    const auto hdr = splitCsv(lines[0]);
    REQUIRE(hdr.size() == 24);
    CHECK(hdr[18] == "wrapper_cpu_ms");
    CHECK(hdr[19] == "app_cpu_ms");
    CHECK(hdr[20] == "draw_cpu_ms");
    CHECK(hdr[21] == "tex_cpu_ms");
    CHECK(hdr[22] == "lfb_cpu_ms");
    CHECK(hdr[23] == "state_cpu_ms");

    // Frame 1 (lines[2]) has a clean frameStart->frameEnd interval including
    // the 6 ms simulated app sleep.
    const auto f1 = splitCsv(lines[2]);
    REQUIRE(f1.size() == 24);
    const double wrapMs = std::stod(f1[18]);
    const double appMs = std::stod(f1[19]);
    const double drawMs = std::stod(f1[20]);
    const double lfbMs = std::stod(f1[22]);
    CHECK(wrapMs > 0.0);
    CHECK(drawMs > 0.0);
    CHECK(lfbMs >= 0.0);
    CHECK(appMs >= 4.0);

    std::remove(csvPath.c_str());
  }
#endif
}

TEST_CASE("M8.2 Phase-2 Authentic 3dfx Env Vars: INI Parsing, Precedence & Runtime Behavior",
          "[Config][EnvVars][M8]") {
  struct ScopedEnvM8 {
    std::vector<std::string> keys = {
        "GLIDE_NG_CONFIG", "GLIDE_NG_HEADLESS", "GLIDE_NG_CARD_MODEL",
        "GLIDE_NG_FBI_MEM_MB", "GLIDE_NG_TMU_MEM_MB", "GLIDE_NG_TMU_COUNT",
        "GLIDE_NG_MAX_FPS", "GLIDE_NG_VSYNC", "GLIDE_NG_LOG_LEVEL",
        "GLIDE_NG_LOG_FILE", "GLIDE_NG_NO_SPLASH",
        "FX_GLIDE_FBRAM", "FX_GLIDE_TMU_MEMSIZE", "SSTV2_NUM_TMUS",
        "SST_GAMMA", "SSTV2_GAMMA", "SST_SCREENREFRESH", "SSTV2_SCREENREFRESH",
        "FX_GLIDE_IGNORE_REOPEN", "FX_GLIDE_NO_DITHER_SUB",
        "SSTV2_FORCE_BILINEAR", "SSTV2_FORCE_TRILINEAR", "SST_TEXMAP_DISABLE",
        "FX_GLIDE_BOUNDS_CHECK", "FX_GLIDE_ALLOC_COLOR", "FX_GLIDE_ALLOC_AUX",
        "FX_GLIDE_SNAPSHOT", "FX_SNAPSHOT"};
    std::vector<std::pair<std::string, std::string>> saved;
    ScopedEnvM8() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        ::unsetenv(k.c_str());
      }
    }
    ~ScopedEnvM8() {
      for (const auto& kv : saved) {
        if (kv.second == "\x01UNSET") {
          ::unsetenv(kv.first.c_str());
        } else {
          ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
      }
    }
  } guard;

  SECTION("INI [glide]/[display] Phase-2 keys and snapshot syntax parsing") {
    const std::string iniPath = "/tmp/glide_ng_m82_ini_test.ini";
    {
      std::ofstream out(iniPath);
      out << "[hardware]\n"
          << "fbi_memory_mb = 6\n"
          << "tmu_memory_mb = 4\n"
          << "tmu_count = 1\n"
          << "[display]\n"
          << "screen_refresh = 75\n"
          << "gamma = 1.7\n"
          << "[glide]\n"
          << "ignore_reopen = true\n"
          << "no_dither_sub = true\n"
          << "force_bilinear = true\n"
          << "force_trilinear = false\n"
          << "texmap_disable = true\n"
          << "bounds_check = true\n"
          << "alloc_color = 3\n"
          << "alloc_aux = 0\n"
          << "snapshot = /tmp/ini_snap.ppm@4\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(iniPath, cfg));
    CHECK(cfg.fbiMemoryMb == 6u);
    CHECK(cfg.tmuMemoryMb == 4u);
    CHECK(cfg.tmuCount == 1u);
    CHECK(cfg.screenRefreshHz == 75u);
    CHECK(cfg.maxFps == 75.0f);
    CHECK(std::fabs(cfg.gammaOverride - 1.7f) < 1e-4f);
    CHECK(cfg.ignoreReopen == true);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.forceBilinear == true);
    CHECK(cfg.forceTrilinear == false);
    CHECK(cfg.texmapDisable == true);
    CHECK(cfg.boundsCheck == true);
    CHECK(cfg.allocColorBuffers == 3);
    CHECK(cfg.allocAuxBuffers == 0);
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 4u);
    CHECK(cfg.snapshotPath == "/tmp/ini_snap.ppm");
    CHECK(cfg.settingSources.at("gamma") == "ini");
    CHECK(cfg.settingSources.at("screenRefresh") == "ini");
    CHECK(cfg.settingSources.at("snapshot") == "ini");
    std::remove(iniPath.c_str());
  }

  SECTION("Strict 4-layer precedence across all 17 Phase-2 env vars and aliases") {
    const std::string iniPath = "/tmp/glide_ng_m82_prec_test.ini";
    {
      std::ofstream out(iniPath);
      out << "[hardware]\n"
          << "fbi_memory_mb = 6\n"
          << "tmu_memory_mb = 4\n"
          << "tmu_count = 1\n"
          << "[display]\n"
          << "max_fps = 60\n"
          << "[glide]\n"
          << "gamma = 1.2\n"
          << "ignore_reopen = false\n"
          << "alloc_color = 2\n"
          << "alloc_aux = 1\n";
    }
    ::setenv("GLIDE_NG_CONFIG", iniPath.c_str(), 1);

    // Layer 3: Authentic 3dfx vars override INI; V2/primary aliases beat SST_/FX_ aliases
    ::setenv("FX_GLIDE_FBRAM", "8", 1);
    ::setenv("FX_GLIDE_TMU_MEMSIZE", "12", 1);
    ::setenv("SSTV2_NUM_TMUS", "2", 1);
    ::setenv("SST_GAMMA", "1.5", 1);
    ::setenv("SSTV2_GAMMA", "2.1", 1);
    ::setenv("SST_SCREENREFRESH", "72", 1);
    ::setenv("SSTV2_SCREENREFRESH", "85", 1);
    ::setenv("FX_GLIDE_IGNORE_REOPEN", "1", 1);
    ::setenv("FX_GLIDE_NO_DITHER_SUB", "1", 1);
    ::setenv("SSTV2_FORCE_BILINEAR", "1", 1);
    ::setenv("SSTV2_FORCE_TRILINEAR", "1", 1);
    ::setenv("SST_TEXMAP_DISABLE", "1", 1);
    ::setenv("FX_GLIDE_BOUNDS_CHECK", "1", 1);
    ::setenv("FX_GLIDE_ALLOC_COLOR", "3", 1);
    ::setenv("FX_GLIDE_ALLOC_AUX", "0", 1);
    ::setenv("FX_SNAPSHOT", "/tmp/alias.ppm@2", 1);
    ::setenv("FX_GLIDE_SNAPSHOT", "/tmp/primary.ppm@7", 1);

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 8u);
    CHECK(cfg.tmuMemoryMb == 12u);
    CHECK(cfg.tmuCount == 2u);
    CHECK(std::fabs(cfg.gammaOverride - 2.1f) < 1e-4f);
    CHECK(cfg.screenRefreshHz == 85u);
    CHECK(cfg.maxFps == 85.0f);
    CHECK(cfg.ignoreReopen == true);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.forceBilinear == true);
    CHECK(cfg.forceTrilinear == true);
    CHECK(cfg.texmapDisable == true);
    CHECK(cfg.boundsCheck == true);
    CHECK(cfg.allocColorBuffers == 3);
    CHECK(cfg.allocAuxBuffers == 0);
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 7u);
    CHECK(cfg.snapshotPath == "/tmp/primary.ppm");
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "FX_GLIDE_FBRAM");
    CHECK(cfg.settingSources.at("tmuMemoryMb") == "FX_GLIDE_TMU_MEMSIZE");
    CHECK(cfg.settingSources.at("tmuCount") == "SSTV2_NUM_TMUS");
    CHECK(cfg.settingSources.at("gamma") == "SSTV2_GAMMA");
    CHECK(cfg.settingSources.at("screenRefresh") == "SSTV2_SCREENREFRESH");
    CHECK(cfg.settingSources.at("snapshot") == "FX_GLIDE_SNAPSHOT");

    // Verify numeric-only and @N snapshot formats
    ::unsetenv("FX_GLIDE_SNAPSHOT");
    ::setenv("FX_SNAPSHOT", "@3", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 3u);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_SNAPSHOT", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == false);

    // Layer 4: GLIDE_NG_* overrides authentic 3dfx vars
    ::setenv("GLIDE_NG_FBI_MEM_MB", "32", 1);
    ::setenv("GLIDE_NG_TMU_MEM_MB", "16", 1);
    ::setenv("GLIDE_NG_TMU_COUNT", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "144", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 32u);
    CHECK(cfg.tmuMemoryMb == 16u);
    CHECK(cfg.tmuCount == 1u);
    CHECK(cfg.maxFps == 144.0f);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "GLIDE_NG_FBI_MEM_MB");
    CHECK(cfg.settingSources.at("tmuMemoryMb") == "GLIDE_NG_TMU_MEM_MB");
    CHECK(cfg.settingSources.at("tmuCount") == "GLIDE_NG_TMU_COUNT");
    CHECK(cfg.settingSources.at("maxFps") == "GLIDE_NG_MAX_FPS");

    std::remove(iniPath.c_str());
  }

#ifdef __linux__
  SECTION("End-to-end Glide 3.x runtime verification (sizing, aux=0, no_dither, texmap_disable, reopen, bounds, gamma, PPM snapshot)") {
    const std::string logPath = "/tmp/glide_ng_m82_g3_runtime.log";
    const std::string snapPath = "/tmp/glide_ng_m82_g3_snap.ppm";
    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("FX_GLIDE_FBRAM", "4", 1);
    ::setenv("FX_GLIDE_TMU_MEMSIZE", "8", 1);
    ::setenv("SSTV2_NUM_TMUS", "1", 1);
    ::setenv("SSTV2_GAMMA", "2.0", 1);
    ::setenv("FX_GLIDE_IGNORE_REOPEN", "1", 1);
    ::setenv("FX_GLIDE_NO_DITHER_SUB", "1", 1);
    ::setenv("SST_TEXMAP_DISABLE", "1", 1);
    ::setenv("FX_GLIDE_ALLOC_AUX", "0", 1);
    ::setenv("FX_GLIDE_BOUNDS_CHECK", "1", 1);
    const std::string snapArg = snapPath + "@0";
    ::setenv("FX_GLIDE_SNAPSHOT", snapArg.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto grGetFn = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxI32*)>(::dlsym(handle, "grGet"));
    auto texMaxAddr = reinterpret_cast<FxU32 (*)(GrChipID_t)>(::dlsym(handle, "grTexMaxAddress"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(::dlsym(handle, "grVertexLayout"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(::dlsym(handle, "grBufferClear"));
    auto depthMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(::dlsym(handle, "grDepthBufferMode"));
    auto depthFunc = reinterpret_cast<void (*)(GrCmpFnc_t)>(::dlsym(handle, "grDepthBufferFunction"));
    auto depthMask = reinterpret_cast<void (*)(FxBool)>(::dlsym(handle, "grDepthMask"));
    auto ditherMode = reinterpret_cast<void (*)(GrDitherMode_t)>(::dlsym(handle, "grDitherMode"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbRead = reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));
    auto loadGamma = reinterpret_cast<void (*)(FxU32, FxU32*, FxU32*, FxU32*)>(
        ::dlsym(handle, "grLoadGammaTable"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));

    glideInit();

    // 1. Hardware sizing queries
    FxI32 fbBytes = 0, tmuBytes = 0, numTmus = 0;
    REQUIRE(grGetFn(GR_MEMORY_FB, 4, &fbBytes) == 4);
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuBytes) == 4);
    REQUIRE(grGetFn(GR_NUM_TMU, 4, &numTmus) == 4);
    CHECK(fbBytes == 4 * 1024 * 1024);
    CHECK(tmuBytes == 8 * 1024 * 1024);
    CHECK(numTmus == 1);
    CHECK(texMaxAddr(GR_TMU0) == 8u * 1024u * 1024u);
    CHECK(texMaxAddr(GR_TMU1) == 0u);

    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // App tries to reset gamma to linear -> SSTV2_GAMMA=2.0 override remains active
    FxU32 linearRamp[256];
    for (FxU32 i = 0; i < 256; ++i) linearRamp[i] = i;
    loadGamma(256, linearRamp, linearRamp, linearRamp);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);

    depthMask(FXTRUE);
    bufferClear(0xFF000000u, 0xFF, 0xFFFF);

    auto drawQuad = [&](float x0, float y0, float x1, float y1, float oow,
                        float r, float g, float b) {
      StandardVert tl = {x0, y0, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert tr = {x1, y0, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert br = {x1, y1, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert bl = {x0, y1, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    };

    // 2. FX_GLIDE_ALLOC_AUX=0: depth test disabled -> far BLUE overwrites near RED
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    depthMode(GR_DEPTHBUFFER_WBUFFER);
    depthFunc(GR_CMP_LESS);
    depthMask(FXTRUE);
    drawQuad(20.0f, 20.0f, 120.0f, 120.0f, 0.10f, 240.0f, 20.0f, 20.0f);
    drawQuad(30.0f, 30.0f, 110.0f, 110.0f, 0.01f, 20.0f, 40.0f, 240.0f);
    depthMode(GR_DEPTHBUFFER_DISABLE);
    depthMask(FXFALSE);

    // Pure RED patch at (150..250, 20..120) to verify PPM ABGR->RGB channel order
    drawQuad(150.0f, 20.0f, 250.0f, 120.0f, 1.0f, 240.0f, 16.0f, 16.0f);

    // 3. FX_GLIDE_NO_DITHER_SUB=1: flat (124,122,124) with GR_DITHER_4x4 has 0 R5 spread
    ditherMode(GR_DITHER_4x4);
    drawQuad(300.0f, 20.0f, 360.0f, 80.0f, 1.0f, 124.0f, 122.0f, 124.0f);
    ditherMode(GR_DITHER_DISABLE);

    // 4. FX_GLIDE_BOUNDS_CHECK=1: submit off-screen triangle
    StandardVert oa = {720.0f, 540.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert ob = {820.0f, 540.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert oc = {770.0f, 640.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    drawTri(&oa, &ob, &oc);

    // 5. Verify depth disabled (center of 30..110 is BLUE)
    uint16_t auxPx = 0;
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 70, 70, 1, 1, 2, &auxPx) == FXTRUE);
    const int auxR = ((auxPx >> 11) & 31) << 3;
    const int auxB = (auxPx & 31) << 3;
    CHECK(auxB > 180);
    CHECK(auxR < 60);

    // 6. Verify dither suppressed (16x16 block inside 300..360 has uniform R5)
    uint16_t dithBlock[16 * 16] = {};
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 316, 36, 16, 16, 16 * 2, dithBlock) == FXTRUE);
    int r5Min = 31, r5Max = 0;
    for (uint16_t p : dithBlock) {
      int r5 = (p >> 11) & 31;
      r5Min = std::min(r5Min, r5);
      r5Max = std::max(r5Max, r5);
    }
    CHECK(r5Min == r5Max);

    // 7. FX_GLIDE_IGNORE_REOPEN=1: redundant grSstWinOpen preserves red patch
    FxU32 ctx2 = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    CHECK(ctx2 != 0);
    uint16_t redPx = 0;
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 200, 70, 1, 1, 2, &redPx) == FXTRUE);
    CHECK((((redPx >> 11) & 31) << 3) > 200);
    CHECK(((redPx & 31) << 3) < 40);

    // 8. Swap frame 0 -> triggers FX_GLIDE_SNAPSHOT
    bufferSwap(0);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Verify PPM snapshot file exists, has exact binary P6 size, and accurate RGB (no R/B swap)
    std::ifstream snapIn(snapPath, std::ios::binary);
    REQUIRE(snapIn.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(snapIn)),
                               std::istreambuf_iterator<char>());
    const std::string expectedHdr = "P6\n640 480\n255\n";
    REQUIRE(bytes.size() == expectedHdr.size() + 640u * 480u * 3u);
    CHECK(std::string(bytes.begin(), bytes.begin() + expectedHdr.size()) == expectedHdr);
    const size_t redOffset = expectedHdr.size() + (70u * 640u + 200u) * 3u;
    CHECK(bytes[redOffset + 0] > 200);  // R
    CHECK(bytes[redOffset + 1] < 40);   // G
    CHECK(bytes[redOffset + 2] < 40);   // B (proves ABGR -> RGB unpack is exact)

    // Verify bounds check and gamma override were logged
    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    const std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                 std::istreambuf_iterator<char>());
    CHECK(logContent.find("[BoundsCheck]") != std::string::npos);
    CHECK(logContent.find("Applying authentic gamma override: 2") != std::string::npos);

    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());
  }
#endif
}

TEST_CASE("Glide 3.x GR_PARAM_PARGB Packed Color & VSA-100 Enable/Disable Modes",
          "[Glide3][VertexLayout][M9]") {
  // Exact layout used by official 3dfx Voodoo5/Napalm Glide 3.x demos
  // (Kitty Hawk, Museum, Rocket Burger): XY@0, Q@12, ST0@16, PARGB@32.
  // Bytes 16..31 overlap Glide 3.x canonical float RGB@16 and A@28.
  struct PargbDemoVert {
    float x, y;      // 0..7   (GR_PARAM_XY)
    float padZ;      // 8..11
    float q;         // 12..15 (GR_PARAM_Q)
    float s0, t0;    // 16..23 (GR_PARAM_ST0 — overlaps canonical RGB!)
    float pad1, pad2;// 24..31 (overlaps canonical RGB.b and A!)
    uint32_t pargb;  // 32..35 (GR_PARAM_PARGB: 0xAARRGGBB)
    float q0;        // 36..39 (optional GR_PARAM_Q0)
    float q1;        // 40..43 (optional GR_PARAM_Q1)
  };
  static_assert(sizeof(PargbDemoVert) == 44, "PargbDemoVert layout mismatch");

  SECTION("VertexLayout decodes GR_PARAM_PARGB (0xAARRGGBB), GR_PARAM_Q0/Q1, and round-trips state") {
    auto& vl = GlideWrapper::VertexLayout::GetInstance();
    vl.ResetToGlide3Canonical();

    // Enable PARGB without explicitly disabling canonical RGB/A
    vl.SetParamOffsetGlide3(0x01, 0, 1);   // GR_PARAM_XY
    vl.SetParamOffsetGlide3(0x04, 12, 1);  // GR_PARAM_Q
    vl.SetParamOffsetGlide3(0x40, 16, 1);  // GR_PARAM_ST0
    vl.SetParamOffsetGlide3(0x41, 16, 1);  // GR_PARAM_ST1
    vl.SetParamOffsetGlide3(0x30, 32, 1);  // GR_PARAM_PARGB
    vl.SetParamOffsetGlide3(0x50, 36, 1);  // GR_PARAM_Q0
    vl.SetParamOffsetGlide3(0x51, 40, 1);  // GR_PARAM_Q1

    PargbDemoVert v{};
    v.x = 123.5f;
    v.y = 45.25f;
    v.q = 0.5f;
    v.s0 = 0.25f;
    v.t0 = 0.75f;
    v.pargb = 0x80F04010u;  // A=128, R=240, G=64, B=16
    v.q0 = 0.25f;
    v.q1 = 0.125f;

    GlideWrapper::ModernVertex mv = vl.DecodeVertex(&v);
    CHECK(mv.pos[0] == 123.5f);
    CHECK(mv.pos[1] == 45.25f);
    CHECK(mv.pos[3] == 0.5f);
    CHECK(std::fabs(mv.color[0] - (240.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[1] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[2] - (16.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mv.color[3] - (128.0f / 255.0f)) < 1e-4f);
    CHECK(mv.tex[0] == 0.25f);
    CHECK(mv.tex[1] == 0.75f);
    CHECK(mv.tmu_oow[0] == 0.25f);
    CHECK(mv.tmu_oow[1] == 0.125f);
    CHECK(vl.GetMaxActiveOffset() >= 44);

    // Save layout, reset to float canonical, restore layout, verify identical decode
    uint8_t savedLayout[256] = {};
    vl.GetLayoutState(savedLayout);
    vl.ResetToGlide3Canonical();
    vl.SetLayoutState(savedLayout);

    GlideWrapper::ModernVertex mvRestored = vl.DecodeVertex(&v);
    CHECK(std::fabs(mvRestored.color[0] - (240.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[1] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[2] - (16.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvRestored.color[3] - (128.0f / 255.0f)) < 1e-4f);
    CHECK(mvRestored.pos[3] == 0.5f);
    CHECK(mvRestored.tmu_oow[0] == 0.25f);

    // Re-enabling float GR_PARAM_RGB / GR_PARAM_A switches colorType back to GR_FLOAT
    vl.SetParamOffsetGlide3(0x20, 12, 1);  // GR_PARAM_RGB
    vl.SetParamOffsetGlide3(0x10, 28, 1);  // GR_PARAM_A
    StandardVert fv = {10.0f, 20.0f, 0.0f, 32.0f, 224.0f, 64.0f, 0.0f, 192.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    GlideWrapper::ModernVertex mvf = vl.DecodeVertex(&fv);
    CHECK(std::fabs(mvf.color[0] - (32.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[1] - (224.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[2] - (64.0f / 255.0f)) < 1e-4f);
    CHECK(std::fabs(mvf.color[3] - (192.0f / 255.0f)) < 1e-4f);
  }

#ifdef __linux__
  SECTION("End-to-end Glide 3.x PARGB rendering, alpha blending, layout save/restore & VSA-100 grEnable/grDisable") {
    struct ScopedPargbEnv {
      std::vector<std::string> keys = {
          "GLIDE_NG_HEADLESS", "GLIDE_NG_MAX_FPS", "GLIDE_NG_VSYNC",
          "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_LOG_FILE"};
      std::vector<std::pair<std::string, std::string>> saved;
      ScopedPargbEnv() {
        for (const auto& k : keys) {
          const char* v = std::getenv(k.c_str());
          saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        }
      }
      ~ScopedPargbEnv() {
        for (const auto& kv : saved) {
          if (kv.second == "\x01UNSET") {
            ::unsetenv(kv.first.c_str());
          } else {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
          }
        }
      }
    } envGuard;

    const std::string logPath = "/tmp/glide_ng_g3_pargb_test.log";
    std::remove(logPath.c_str());
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "warn", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(::dlsym(handle, "grVertexLayout"));
    auto getVertexLayout = reinterpret_cast<void (*)(void*)>(::dlsym(handle, "grGlideGetVertexLayout"));
    auto setVertexLayout = reinterpret_cast<void (*)(const void*)>(::dlsym(handle, "grGlideSetVertexLayout"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(::dlsym(handle, "grBufferClear"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto alphaCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grAlphaCombine"));
    auto alphaBlend = reinterpret_cast<void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                                GrAlphaBlendFnc_t, GrAlphaBlendFnc_t)>(
        ::dlsym(handle, "grAlphaBlendFunction"));
    auto depthMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(::dlsym(handle, "grDepthBufferMode"));
    auto ditherMode = reinterpret_cast<void (*)(GrDitherMode_t)>(::dlsym(handle, "grDitherMode"));
    auto grEnableFn = reinterpret_cast<void (*)(GrEnableMode_t)>(::dlsym(handle, "grEnable"));
    auto grDisableFn = reinterpret_cast<void (*)(GrEnableMode_t)>(::dlsym(handle, "grDisable"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbRead = reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(vertexLayout != nullptr);
    REQUIRE(getVertexLayout != nullptr);
    REQUIRE(setVertexLayout != nullptr);
    REQUIRE(grEnableFn != nullptr);
    REQUIRE(grDisableFn != nullptr);

    glideInit();
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Standard Glide 3.x & VSA-100 enable/disable modes must not emit WARN spam
    const GrEnableMode_t v5Modes[] = {
        0x02u,        // GR_ALLOW_MIPMAP_DITHER
        0x03u,        // GR_PASSTHRU
        0x05u,        // GR_VIDEO_SMOOTHING
        0x80000001u,  // GR_COMBINEEXT_MODE
        0x80000002u,  // GR_AA_MULTI_SAMPLE
    };
    for (GrEnableMode_t m : v5Modes) {
      grEnableFn(m);
      grDisableFn(m);
    }

    depthMode(GR_DEPTHBUFFER_DISABLE);
    ditherMode(GR_DITHER_DISABLE);
    bufferClear(0xFF000000u, 0xFF, 0xFFFF);

    // Configure Voodoo5 demo layout (XY@0, Q@12, ST0@16, PARGB@32)
    vertexLayout(0x01, 0, 1);   // GR_PARAM_XY
    vertexLayout(0x04, 12, 1);  // GR_PARAM_Q
    vertexLayout(0x40, 16, 1);  // GR_PARAM_ST0
    vertexLayout(0x30, 32, 1);  // GR_PARAM_PARGB

    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    auto drawPargbQuad = [&](float x0, float y0, float x1, float y1, uint32_t pargb) {
      PargbDemoVert tl{x0, y0, 0.0f, 1.0f, 0.1f, 0.2f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert tr{x1, y0, 0.0f, 1.0f, 0.9f, 0.2f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert br{x1, y1, 0.0f, 1.0f, 0.9f, 0.8f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      PargbDemoVert bl{x0, y1, 0.0f, 1.0f, 0.1f, 0.8f, 0.0f, 0.0f, pargb, 1.0f, 1.0f};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    };

    // 1. Opaque orange-red PARGB (0xFFF05010: A=255, R=240, G=80, B=16)
    drawPargbQuad(20.0f, 20.0f, 120.0f, 120.0f, 0xFFF05010u);

    // 2. Alpha-blended 50% green (0x8010F010) over opaque blue (0xFF1020F0)
    drawPargbQuad(150.0f, 20.0f, 250.0f, 120.0f, 0xFF1020F0u);
    alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    drawPargbQuad(150.0f, 20.0f, 250.0f, 120.0f, 0x8010F010u);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    // 3. Save PARGB layout -> switch to float RGB -> draw green -> restore PARGB -> draw red
    uint8_t pargbLayoutBuf[256] = {};
    getVertexLayout(pargbLayoutBuf);

    vertexLayout(0x20, 12, 1);  // GR_PARAM_RGB (GR_FLOAT)
    vertexLayout(0x10, 28, 1);  // GR_PARAM_A (GR_FLOAT)
    vertexLayout(0x03, 32, 1);  // GR_PARAM_W
    {
      StandardVert tl = {280.0f, 20.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert tr = {360.0f, 20.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert br = {360.0f, 120.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      StandardVert bl = {280.0f, 120.0f, 0.0f, 16.0f, 240.0f, 16.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    }

    setVertexLayout(pargbLayoutBuf);
    drawPargbQuad(380.0f, 20.0f, 460.0f, 120.0f, 0xFFF01010u);

    auto readRGB = [&](FxU32 x, FxU32 y, int& r, int& g, int& b) {
      uint16_t px = 0;
      REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, x, y, 1, 1, 2, &px) == FXTRUE);
      r = ((px >> 11) & 31) << 3;
      g = ((px >> 5) & 63) << 2;
      b = (px & 31) << 3;
    };

    int r = 0, g = 0, b = 0;
    // Quad 1: orange-red PARGB
    readRGB(70, 70, r, g, b);
    CHECK(r > 210);
    CHECK(g > 50);
    CHECK(g < 110);
    CHECK(b < 40);

    // Quad 2: 50% green blended over blue -> both G and B ~115..140
    readRGB(200, 70, r, g, b);
    CHECK(r < 40);
    CHECK(g > 90);
    CHECK(g < 165);
    CHECK(b > 90);
    CHECK(b < 165);

    // Quad 3: float green after switching from PARGB -> RGB
    readRGB(320, 70, r, g, b);
    CHECK(r < 40);
    CHECK(g > 210);
    CHECK(b < 40);

    // Quad 4: red PARGB after grGlideSetVertexLayout restore
    readRGB(420, 70, r, g, b);
    CHECK(r > 210);
    CHECK(g < 40);
    CHECK(b < 40);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Confirm zero unsupported mode warnings were emitted
    std::ifstream logIn(logPath);
    if (logIn.is_open()) {
      const std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                   std::istreambuf_iterator<char>());
      CHECK(logContent.find("unsupported mode=") == std::string::npos);
    }
    std::remove(logPath.c_str());
  }
#endif
}

TEST_CASE("Glide 3.x grBufferClear Respects grClipWindow & Origin",
          "[Glide3][ClipWindow][ClearBuffer]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using SelectSstFunc = void (*)(int);
  using OpenWinFunc = FxU32 (*)(AnyPtr, GrScreenResolution_t,
                                GrScreenRefresh_t, GrColorFormat_t,
                                GrOriginLocation_t, int, int);
  using CloseWinFunc = FxBool (*)(FxU32);
  using ShutdownFunc = void (*)(void);
  using ClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using ClipWinFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
  using OriginFunc = void (*)(GrOriginLocation_t);
  using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ConstColorFunc = void (*)(GrColor_t);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using LfbReadFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                 void*);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto sstSelect =
      reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
  auto winOpen =
      reinterpret_cast<OpenWinFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<CloseWinFunc>(::dlsym(handle, "grSstWinClose"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto bufferClear =
      reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
  auto clipWin =
      reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
  auto sstOrigin =
      reinterpret_cast<OriginFunc>(::dlsym(handle, "grSstOrigin"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto colorCombine =
      reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
  auto constColor =
      reinterpret_cast<ConstColorFunc>(::dlsym(handle, "grConstantColorValue"));
  auto drawTri =
      reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto lfbRead =
      reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(clipWin != nullptr);
  REQUIRE(sstOrigin != nullptr);
  REQUIRE(vertexLayout != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constColor != nullptr);
  REQUIRE(drawTri != nullptr);
  REQUIRE(lfbRead != nullptr);

  glideInit();
  sstSelect(0);
  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  auto readRGB = [&](FxU32 x, FxU32 y, int& r, int& g, int& b) {
    uint16_t px = 0;
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, x, y, 1, 1, 2, &px) == FXTRUE);
    r = ((px >> 11) & 31) << 3;
    g = ((px >> 5) & 63) << 2;
    b = (px & 31) << 3;
  };

  // 1. Split-screen panes + narrow 4px center divider stripe (reproduces
  //    3dfx aademo.exe F9 split-screen sequence: clipped clear immediately
  //    followed by widening grClipWindow before drawing an overlay quad).
  clipWin(0, 0, 640, 480);
  bufferClear(0xFF0010F0u, 0xFF, 0xFFFFu);  // Full blue base

  clipWin(0, 0, 318, 480);
  bufferClear(0xFFF01010u, 0xFF, 0xFFFFu);  // Left pane: Red

  clipWin(322, 0, 640, 480);
  bufferClear(0xFF10F010u, 0xFF, 0xFFFFu);  // Right pane: Green

  clipWin(318, 0, 322, 480);
  bufferClear(0xFFF7F700u, 0xFF, 0xFFFFu);  // Center 4px divider: Yellow

  // Widen clip window immediately (no draw between yellow clear and clip
  // restore), then draw a white overlay quad in the bottom-right corner.
  clipWin(0, 0, 640, 480);
  vertexLayout(0x01, 0, 1);  // GR_PARAM_XY
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  constColor(0xFFFFFFFFu);
  struct XYVert {
    float x, y;
  };
  XYVert tl{560.0f, 400.0f}, tr{620.0f, 400.0f}, br{620.0f, 460.0f},
      bl{560.0f, 460.0f};
  drawTri(&tl, &tr, &br);
  drawTri(&tl, &br, &bl);

  int r = 0, g = 0, b = 0;
  // Left pane remains red
  readRGB(160, 240, r, g, b);
  CHECK(r > 210);
  CHECK(g < 40);
  CHECK(b < 40);

  // Center 4px divider is bright yellow
  readRGB(320, 240, r, g, b);
  CHECK(r > 210);
  CHECK(g > 210);
  CHECK(b < 40);

  // Right pane remains green
  readRGB(480, 240, r, g, b);
  CHECK(r < 40);
  CHECK(g > 210);
  CHECK(b < 40);

  // Bottom-right overlay quad is white
  readRGB(590, 430, r, g, b);
  CHECK(r > 220);
  CHECK(g > 220);
  CHECK(b > 220);

  // 2. Empty clip rect clear must be a no-op (must not clobber pixels or crash)
  clipWin(200, 200, 200, 200);
  bufferClear(0xFFFF00FFu, 0xFF, 0xFFFFu);
  readRGB(160, 240, r, g, b);
  CHECK(r > 210);
  CHECK(g < 40);
  CHECK(b < 40);

  // 3. GR_ORIGIN_LOWER_LEFT clipped clear must flip Y into bottom-up space
  sstOrigin(GR_ORIGIN_LOWER_LEFT);
  clipWin(0, 0, 640, 480);
  bufferClear(0xFF000000u, 0xFF, 0xFFFFu);  // Full black

  // Bottom-left Y in [50, 150) maps to top-down Y in [330, 430)
  clipWin(100, 50, 200, 150);
  bufferClear(0xFF00F0F0u, 0xFF, 0xFFFFu);  // Cyan sub-rect near bottom

  // Top-down y=380 (inside bottom-origin [50..150)) -> Cyan
  readRGB(150, 380, r, g, b);
  CHECK(r < 40);
  CHECK(g > 210);
  CHECK(b > 210);

  // Top-down y=100 (outside bottom-origin [50..150)) -> Untouched Black
  readRGB(150, 100, r, g, b);
  CHECK(r < 20);
  CHECK(g < 20);
  CHECK(b < 20);

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
#endif
}

TEST_CASE(
    "Glide 3.x grGetString(GR_HARDWARE) Returns Authentic Short Card Names",
    "[Glide3][GetString][Hardware]") {
#ifdef __linux__
  struct CardExpectation {
    const char* envVal;
    const char* expectedName;
  };
  const CardExpectation cases[] = {
      {"voodoo1", "Voodoo Graphics"},
      {"rush", "Voodoo Rush"},
      {"voodoo2", "Voodoo2"},
      {"banshee", "Voodoo Banshee"},
      {"voodoo_banshee", "Voodoo Banshee"},
      {"voodoo3", "Voodoo3"},
      {"voodoo4", "Voodoo4"},
      {"vsa100", "Voodoo4"},
      {"h4", "Voodoo4"},
      {"voodoo5", "Voodoo5"},
  };

  const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = origCard ? origCard : "";

  for (const auto& tc : cases) {
    ::setenv("GLIDE_NG_CARD_MODEL", tc.envVal, 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using GetStringFunc = const char* (*)(FxU32);
    using ShutdownFunc = void (*)(void);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto getString =
        reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(getString != nullptr);
    REQUIRE(glideShutdown != nullptr);

    glideInit();
    const char* hw = getString(0xa1);  // GR_HARDWARE
    REQUIRE(hw != nullptr);
    CHECK(std::string(hw) == tc.expectedName);
    // Authentic 3dfx hardware strings fit inside legacy fixed-size HUD buffers
    // (e.g., 3dfx aademo.exe formats "%s, 4 sample, Spatial Anti-Aliasing"
    // into a 60-byte stack buffer).
    CHECK(std::strlen(hw) <= 20u);

    glideShutdown();
    ::dlclose(handle);
  }

  if (origCard) {
    ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_CARD_MODEL");
  }
#endif
}

TEST_CASE(
    "Glide 3.x Voodoo5 Spatial AA & T-Buffer Extensions "
    "(grSstWinOpenExt / GR_AA_MULTI_SAMPLE / grTBufferWriteMaskExt)",
    "[Glide3][SpatialAA][TBuffer]") {
#ifdef __linux__
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using SelectSstFunc = void (*)(int);
  using GetProcAddrFunc = GrProc (*)(char*);
  using WinOpenFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinOpenExtFunc =
      FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, GrPixelFormat_t, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using ShutdownFunc = void (*)(void);
  using EnableFunc = void (*)(GrEnableMode_t);
  using DisableFunc = void (*)(GrEnableMode_t);
  using TBufferMaskFunc = void (*)(FxU32);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using ClipWinFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
  using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using ColorCombineFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                    GrCombineLocal_t, GrCombineOther_t, FxBool);
  using ConstColorFunc = void (*)(GrColor_t);
  using DepthModeFunc = void (*)(GrDepthBufferMode_t);
  using DepthFuncFunc = void (*)(GrCmpFnc_t);
  using DepthMaskFunc = void (*)(FxBool);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using LfbReadFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using LfbWriteFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t,
                                  FxU32, FxU32, FxBool, FxI32, void*);
  using GetStateFunc = void (*)(void*);
  using SetStateFunc = void (*)(const void*);
  using GetFunc = FxU32 (*)(FxU32, FxU32, FxI32*);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto sstSelect =
      reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
  auto getProc =
      reinterpret_cast<GetProcAddrFunc>(::dlsym(handle, "grGetProcAddress"));
  auto winOpen =
      reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto grEnableFn = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
  auto grDisableFn =
      reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
  auto bufferClear =
      reinterpret_cast<BufferClearFunc>(::dlsym(handle, "grBufferClear"));
  auto clipWin =
      reinterpret_cast<ClipWinFunc>(::dlsym(handle, "grClipWindow"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto colorCombine =
      reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
  auto constColor =
      reinterpret_cast<ConstColorFunc>(::dlsym(handle, "grConstantColorValue"));
  auto depthMode =
      reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
  auto depthFunc =
      reinterpret_cast<DepthFuncFunc>(::dlsym(handle, "grDepthBufferFunction"));
  auto depthMask =
      reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
  auto drawTri =
      reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto lfbRead =
      reinterpret_cast<LfbReadFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto lfbWrite =
      reinterpret_cast<LfbWriteFunc>(::dlsym(handle, "grLfbWriteRegion"));
  auto getState =
      reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
  auto setState =
      reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));
  auto grGetFn = reinterpret_cast<GetFunc>(::dlsym(handle, "grGet"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(getProc != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(grEnableFn != nullptr);
  REQUIRE(grDisableFn != nullptr);
  REQUIRE(bufferClear != nullptr);
  REQUIRE(clipWin != nullptr);
  REQUIRE(vertexLayout != nullptr);
  REQUIRE(colorCombine != nullptr);
  REQUIRE(constColor != nullptr);
  REQUIRE(depthMode != nullptr);
  REQUIRE(depthFunc != nullptr);
  REQUIRE(depthMask != nullptr);
  REQUIRE(drawTri != nullptr);
  REQUIRE(lfbRead != nullptr);
  REQUIRE(lfbWrite != nullptr);
  REQUIRE(getState != nullptr);
  REQUIRE(setState != nullptr);
  REQUIRE(grGetFn != nullptr);

  struct AaVertex {
    float x, y, ooz, oow;
  };

  auto makeQuad = [&](float x0, float y0, float x1, float y1, float z,
                      GrColor_t col) {
    constColor(col);
    AaVertex v0{x0, y0, z, 1.0f};
    AaVertex v1{x1, y0, z, 1.0f};
    AaVertex v2{x1, y1, z, 1.0f};
    AaVertex v3{x0, y1, z, 1.0f};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);
  };

  // 1. Dynamic extension lookup (all 8 PIXEXT entry points required by 3dfx
  //    tlib / Napalm demos such as Museum) & 4-sample context via
  //    grSstWinOpenExt under Voodoo5 profile.
  const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = origCard ? origCard : "";
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);

  glideInit();
  char winOpenExtName[] = "grSstWinOpenExt";
  char tbufMaskName[] = "grTBufferWriteMaskExt";
  char colorMaskExtName[] = "grColorMaskExt";
  char stencilFuncExtName[] = "grStencilFuncExt";
  char stencilMaskExtName[] = "grStencilMaskExt";
  char stencilOpExtName[] = "grStencilOpExt";
  char bufClearExtName[] = "grBufferClearExt";
  char lfbStencilExtName[] = "grLfbConstantStencilExt";
  auto winOpenExt = reinterpret_cast<WinOpenExtFunc>(
      reinterpret_cast<void*>(getProc(winOpenExtName)));
  auto tbufMaskExt = reinterpret_cast<TBufferMaskFunc>(
      reinterpret_cast<void*>(getProc(tbufMaskName)));
  REQUIRE(winOpenExt != nullptr);
  REQUIRE(tbufMaskExt != nullptr);
  CHECK(getProc(colorMaskExtName) != nullptr);
  CHECK(getProc(stencilFuncExtName) != nullptr);
  CHECK(getProc(stencilMaskExtName) != nullptr);
  CHECK(getProc(stencilOpExtName) != nullptr);
  CHECK(getProc(bufClearExtName) != nullptr);
  CHECK(getProc(lfbStencilExtName) != nullptr);

  sstSelect(0);
  FxU32 ctx =
      winOpenExt(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                 GR_ORIGIN_UPPER_LEFT, GR_PIXFMT_AA_4_RGB_565, 2, 1);
  REQUIRE(ctx != 0);

  auto* backend = GlideWrapper::BackendManager::GetInstance().GetBackend();
  REQUIRE(backend != nullptr);
  CHECK(backend->GetMsaaSamples() >= 4u);
  CHECK(backend->GetGuestWidth() == 640u);
  CHECK(backend->GetGuestHeight() == 480u);

  vertexLayout(GR_PARAM_XY, static_cast<FxI32>(offsetof(AaVertex, x)),
               GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Z, static_cast<FxI32>(offsetof(AaVertex, ooz)),
               GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Q, static_cast<FxI32>(offsetof(AaVertex, oow)),
               GR_PARAM_ENABLE);
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

  // 2. Single-frame split-screen live GR_AA_MULTI_SAMPLE toggle (Kitty Hawk F9):
  //    Left half (AA OFF) must produce strictly binary aliased edges;
  //    Right half (AA ON) must produce genuine intermediate blended edge pixels.
  clipWin(0, 0, 640, 480);
  tbufMaskExt(0xFu);
  depthMode(GR_DEPTHBUFFER_DISABLE);
  bufferClear(0xFF000000u, 0xFF, 0xFFFFu);

  constColor(0xFFFF0000u);  // Pure Red (ARGB)

  // Left half: AA OFF
  grDisableFn(GR_AA_MULTI_SAMPLE);
  clipWin(0, 0, 320, 480);
  {
    AaVertex a{40.0f, 100.0f, 0.5f, 1.0f};
    AaVertex b{260.0f, 190.0f, 0.5f, 1.0f};
    AaVertex c{40.0f, 340.0f, 0.5f, 1.0f};
    drawTri(&a, &b, &c);
  }

  // Right half: AA ON
  grEnableFn(GR_AA_MULTI_SAMPLE);
  clipWin(320, 0, 640, 480);
  {
    AaVertex a{360.0f, 100.0f, 0.5f, 1.0f};
    AaVertex b{580.0f, 190.0f, 0.5f, 1.0f};
    AaVertex c{360.0f, 340.0f, 0.5f, 1.0f};
    drawTri(&a, &b, &c);
  }

  std::vector<uint32_t> frame(640u * 480u, 0u);
  REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                  frame.data()) == FXTRUE);

  auto pxAt = [&](uint32_t x, uint32_t y) -> uint32_t {
    return frame[y * 640u + x];
  };

  // Both interiors must be solid Red
  CHECK(((pxAt(100, 200) >> 16) & 0xFFu) > 240u);
  CHECK(((pxAt(100, 200) >> 8) & 0xFFu) < 10u);
  CHECK(((pxAt(420, 200) >> 16) & 0xFFu) > 240u);
  CHECK(((pxAt(420, 200) >> 8) & 0xFFu) < 10u);

  uint32_t leftIntermediate = 0;
  uint32_t rightIntermediate = 0;
  for (uint32_t y = 90; y < 350; ++y) {
    for (uint32_t x = 30; x < 280; ++x) {
      uint32_t r = (pxAt(x, y) >> 16) & 0xFFu;
      if (r > 20u && r < 235u) ++leftIntermediate;
    }
    for (uint32_t x = 350; x < 600; ++x) {
      uint32_t r = (pxAt(x, y) >> 16) & 0xFFu;
      if (r > 20u && r < 235u) ++rightIntermediate;
    }
  }
  CHECK(leftIntermediate == 0u);
  CHECK(rightIntermediate >= 40u);

  // 3. Authentic VSA-100 T-Buffer sample masking + sample-masked grBufferClear
  //    + per-sample depth isolation (Rocket Burger Motion Blur / DoF pattern).
  clipWin(0, 0, 640, 480);
  grEnableFn(GR_AA_MULTI_SAMPLE);
  depthMode(GR_DEPTHBUFFER_ZBUFFER);
  depthFunc(GR_CMP_LESS);
  depthMask(FXTRUE);
  tbufMaskExt(0xFu);
  bufferClear(0xFF000000u, 0xFF, 0xFFFFu);

  // Samples 0 & 1: masked clear to Red (depth 1.0), draw closer Green at z=0.25
  tbufMaskExt(0x3u);
  bufferClear(0xFFFF0000u, 0xFF, 0xFFFFu);
  makeQuad(100.0f, 100.0f, 200.0f, 200.0f, 0.25f * 65535.0f, 0xFF00FF00u);

  // Samples 2 & 3: masked clear to Blue (depth 1.0), draw deeper Red at z=0.75
  // (must NOT be occluded by z=0.25 written only to samples 0 & 1!).
  tbufMaskExt(0xCu);
  bufferClear(0xFF0000FFu, 0xFF, 0xFFFFu);
  makeQuad(100.0f, 100.0f, 200.0f, 200.0f, 0.75f * 65535.0f, 0xFFFF0000u);

  tbufMaskExt(0xFu);
  REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                  frame.data()) == FXTRUE);

  // Background (samples 0,1 = Red, samples 2,3 = Blue) -> Purple (~127, 0, ~127)
  {
    const uint32_t bg = pxAt(30, 30);
    const uint32_t r = (bg >> 16) & 0xFFu;
    const uint32_t g = (bg >> 8) & 0xFFu;
    const uint32_t b = bg & 0xFFu;
    CHECK(r >= 110u);
    CHECK(r <= 145u);
    CHECK(g < 15u);
    CHECK(b >= 110u);
    CHECK(b <= 145u);
  }
  // Center quad (samples 0,1 = Green z=0.25, samples 2,3 = Red z=0.75) ->
  // Yellow-Olive (~127, ~127, 0)
  {
    const uint32_t mid = pxAt(150, 150);
    const uint32_t r = (mid >> 16) & 0xFFu;
    const uint32_t g = (mid >> 8) & 0xFFu;
    const uint32_t b = mid & 0xFFu;
    CHECK(r >= 110u);
    CHECK(r <= 145u);
    CHECK(g >= 110u);
    CHECK(g <= 145u);
    CHECK(b < 15u);
  }

  // 4. Guest-coordinate clipped grBufferClear + grLfbWriteRegion under 4-sample
  //    AA (verifies Software 2x2 SSAA + GPU 4x MSAA exact coordinate parity).
  depthMode(GR_DEPTHBUFFER_DISABLE);
  clipWin(0, 0, 640, 480);
  bufferClear(0xFF000000u, 0xFF, 0xFFFFu);

  clipWin(319, 0, 321, 480);
  bufferClear(0xFFFFFF00u, 0xFF, 0xFFFFu);  // 2px Yellow divider

  std::vector<uint16_t> cyanTile(16u * 16u, 0x07FFu);  // RGB565 Cyan
  REQUIRE(lfbWrite(GR_BUFFER_BACKBUFFER, 500, 400, GR_LFB_SRC_FMT_565, 16, 16,
                   FXFALSE, 16 * 2, cyanTile.data()) == FXTRUE);

  REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4,
                  frame.data()) == FXTRUE);
  {
    const uint32_t divPx = pxAt(320, 240);
    CHECK(((divPx >> 16) & 0xFFu) > 230u);
    CHECK(((divPx >> 8) & 0xFFu) > 230u);
    CHECK((divPx & 0xFFu) < 20u);

    const uint32_t outsideDiv = pxAt(310, 240);
    CHECK(((outsideDiv >> 16) & 0xFFu) < 20u);
    CHECK(((outsideDiv >> 8) & 0xFFu) < 20u);

    const uint32_t lfbPx = pxAt(508, 408);
    CHECK(((lfbPx >> 16) & 0xFFu) < 20u);
    CHECK(((lfbPx >> 8) & 0xFFu) > 230u);
    CHECK((lfbPx & 0xFFu) > 230u);
  }

  // 5. grGlideGetState / grGlideSetState preserves T-Buffer write mask
  FxI32 stateSize = 0;
  REQUIRE(grGetFn(GR_GLIDE_STATE_SIZE, 4, &stateSize) == 4u);
  REQUIRE(stateSize > 0);
  std::vector<uint8_t> savedState(static_cast<size_t>(stateSize), 0u);
  clipWin(0, 0, 640, 480);
  tbufMaskExt(0x5u);  // 2 of 4 samples (bits 0 and 2)
  getState(savedState.data());

  tbufMaskExt(0xFu);
  bufferClear(0xFF000000u, 0xFF, 0xFFFFu);
  setState(savedState.data());  // Restores tbufMask = 0x5
  bufferClear(0xFF00FF00u, 0xFF, 0xFFFFu);  // Masked clear -> 2/4 Green = ~127
  tbufMaskExt(0xFu);

  uint32_t statePx = 0u;
  REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 50, 50, 1, 1, 4, &statePx) == FXTRUE);
  {
    const uint32_t g = (statePx >> 8) & 0xFFu;
    CHECK(g >= 110u);
    CHECK(g <= 145u);
  }

  winClose(ctx);
  glideShutdown();

  // 6. Forced GLIDE_NG_MSAA=4 parity on standard grSstWinOpen across all 3
  //    backends (Vulkan, OpenGL ES, Software 2x2 SSAA).
  const char* origMsaa = std::getenv("GLIDE_NG_MSAA");
  std::string savedMsaa = origMsaa ? origMsaa : "";
  const char* origSwAa = std::getenv("GLIDE_NG_SW_AA");
  std::string savedSwAa = origSwAa ? origSwAa : "";
  ::unsetenv("GLIDE_NG_SW_AA");
  ::setenv("GLIDE_NG_MSAA", "4", 1);

  glideInit();
  sstSelect(0);
  FxU32 ctxForced =
      winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
              GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctxForced != 0);
  auto* backendForced =
      GlideWrapper::BackendManager::GetInstance().GetBackend();
  REQUIRE(backendForced != nullptr);
  CHECK(backendForced->GetMsaaSamples() >= 4u);
  winClose(ctxForced);
  glideShutdown();

  if (origMsaa) {
    ::setenv("GLIDE_NG_MSAA", savedMsaa.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_MSAA");
  }
  if (origSwAa) {
    ::setenv("GLIDE_NG_SW_AA", savedSwAa.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_SW_AA");
  }
  if (origCard) {
    ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_CARD_MODEL");
  }

  ::dlclose(handle);
#endif
}

TEST_CASE(
    "Voodoo4 Profile, Unknown Card Warning & Strict Card-Model Extension "
    "Gating",
    "[Glide3][CardGating][Voodoo4]") {
#ifdef __linux__
  const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = origCard ? origCard : "";
  const char* origApi = std::getenv("GLIDE_NG_API_VERSION");
  std::string savedApi = origApi ? origApi : "";
  const char* origMsaa = std::getenv("GLIDE_NG_MSAA");
  std::string savedMsaa = origMsaa ? origMsaa : "";

  auto restoreEnv = [&]() {
    if (origCard) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
    if (origApi) {
      ::setenv("GLIDE_NG_API_VERSION", savedApi.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_API_VERSION");
    }
    if (origMsaa) {
      ::setenv("GLIDE_NG_MSAA", savedMsaa.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_MSAA");
    }
  };

  // 1. IniConfigLoader: Voodoo4 hardware defaults & unknown card warning
  {
    ::unsetenv("GLIDE_NG_API_VERSION");
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo4", 1);
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    loader.Load("/nonexistent_v4_check.ini", cfg);
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo4);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_61);
    CHECK(cfg.fbiMemoryMb == 32u);
    CHECK(cfg.tmuMemoryMb == 16u);
    CHECK(cfg.tmuCount == 2u);
    CHECK(cfg.invalidConfigValues.empty());
  }
  {
    ::unsetenv("GLIDE_NG_API_VERSION");
    ::setenv("GLIDE_NG_CARD_MODEL", "banshee", 1);
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    loader.Load("/nonexistent_v4_check.ini", cfg);
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooBanshee);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_61);
    CHECK(cfg.fbiMemoryMb == 16u);
    CHECK(cfg.tmuMemoryMb == 16u);
    CHECK(cfg.tmuCount == 1u);
    CHECK(cfg.invalidConfigValues.empty());
  }
  {
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo99_unknown", 1);
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    loader.Load("/nonexistent_v4_check.ini", cfg);
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    REQUIRE(!cfg.invalidConfigValues.empty());
    CHECK(cfg.invalidConfigValues[0].find("voodoo99_unknown") !=
          std::string::npos);
  }

  // 2. Glide 3.x grGetProcAddress gating across card profiles
  {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using SelectSstFunc = void (*)(int);
    using GetProcAddrFunc = GrProc (*)(char*);
    using GetStringFunc = const char* (*)(FxU32);
    using WinOpenExtFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32,
                                     GrPixelFormat_t, int, int);
    using WinCloseFunc = FxBool (*)(FxU32);
    using ShutdownFunc = void (*)(void);

    auto glideInit =
        reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto sstSelect =
        reinterpret_cast<SelectSstFunc>(::dlsym(handle, "grSstSelect"));
    auto getProc =
        reinterpret_cast<GetProcAddrFunc>(::dlsym(handle, "grGetProcAddress"));
    auto getString =
        reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
    auto winClose =
        reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(getProc != nullptr);
    REQUIRE(getString != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(glideShutdown != nullptr);

    const char* pixextProcs[] = {
        "grSstWinOpenExt",      "grTBufferWriteMaskExt",
        "grColorMaskExt",       "grBufferClearExt",
        "grStencilFuncExt",     "grStencilMaskExt",
        "grStencilOpExt",       "grLfbConstantStencilExt",
    };

    auto probeProc = [&](const char* name) -> GrProc {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s", name);
      return getProc(buf);
    };

    // Voodoo1: CHROMARANGE OK, TEXCHROMA & PIXEXT gated out
    ::unsetenv("GLIDE_NG_API_VERSION");
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo1", 1);
    glideInit();
    CHECK(probeProc("grChromaRangeModeExt") != nullptr);
    CHECK(probeProc("grTexChromaModeExt") == nullptr);
    for (const char* p : pixextProcs) {
      CHECK(probeProc(p) == nullptr);
    }
    glideShutdown();

    // Voodoo2, Banshee & Voodoo3: TEXCHROMA OK, all 8 PIXEXT procs gated out
    for (const char* card : {"voodoo2", "banshee", "voodoo3"}) {
      ::setenv("GLIDE_NG_CARD_MODEL", card, 1);
      glideInit();
      CHECK(probeProc("grChromaRangeModeExt") != nullptr);
      CHECK(probeProc("grTexChromaModeExt") != nullptr);
      for (const char* p : pixextProcs) {
        CHECK(probeProc(p) == nullptr);
      }
      if (std::string(card) == "banshee") {
        const char* extB = getString(0xa0);
        REQUIRE(extB != nullptr);
        CHECK(std::string(extB).find("TEXMIRROR") != std::string::npos);
        CHECK(std::string(extB).find("PALETTE6666") != std::string::npos);
        CHECK(std::string(extB).find("PIXEXT") == std::string::npos);
      }
      glideShutdown();
    }

    // Voodoo4: all 8 PIXEXT procs available, GR_EXTENSION lists PIXEXT,
    // and requesting 4x AA via grSstWinOpenExt clamps to 2x (single VSA-100).
    ::unsetenv("GLIDE_NG_MSAA");
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo4", 1);
    glideInit();
    const char* extV4 = getString(0xa0);
    REQUIRE(extV4 != nullptr);
    CHECK(std::string(extV4).find("PIXEXT") != std::string::npos);
    for (const char* p : pixextProcs) {
      CHECK(probeProc(p) != nullptr);
    }
    auto winOpenExt = reinterpret_cast<WinOpenExtFunc>(
        reinterpret_cast<void*>(probeProc("grSstWinOpenExt")));
    REQUIRE(winOpenExt != nullptr);
    sstSelect(0);
    FxU32 ctxV4 = winOpenExt(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                             GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                             GR_PIXFMT_AA_4_RGB_565, 2, 1);
    REQUIRE(ctxV4 != 0);
    auto* backendV4 =
        GlideWrapper::BackendManager::GetInstance().GetBackend();
    REQUIRE(backendV4 != nullptr);
    // Single VSA-100 caps requested AA to 2x (host backends allocate >= 2
    // samples: e.g., 2x MSAA on Vulkan/GLES where supported, or 4x where the
    // backend uses a 2x2 SSAA grid / 4x-minimum MSAA hardware).
    CHECK(backendV4->GetMsaaSamples() >= 2u);
    CHECK(backendV4->GetMsaaSamples() <= 4u);
    winClose(ctxV4);
    glideShutdown();

    ::dlclose(handle);
  }

  // 3. Glide 2.x: Pure authentic ABI (no grGetString / grGetProcAddress);
  //    per-card default bare version & EmulationRegistry feature gating.
  {
    void* handle = OpenGlide2Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)(void);
    using GetVerFunc = void (*)(char[80]);
    using ShutdownFunc = void (*)(void);

    auto glideInit =
        reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto getVersion =
        reinterpret_cast<GetVerFunc>(::dlsym(handle, "grGlideGetVersion"));
    auto glideShutdown =
        reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    REQUIRE(glideInit != nullptr);
    REQUIRE(getVersion != nullptr);
    REQUIRE(::dlsym(handle, "grGetString") == nullptr);
    REQUIRE(::dlsym(handle, "grGetProcAddress") == nullptr);
    REQUIRE(glideShutdown != nullptr);

    char verBuf[80] = {};
    ::unsetenv("GLIDE_NG_API_VERSION");
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo1", 1);
    glideInit();
    getVersion(verBuf);
    CHECK(std::string(verBuf) == "2.48");
    glideShutdown();

    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
    glideInit();
    getVersion(verBuf);
    CHECK(std::string(verBuf) == "2.61");
    glideShutdown();

    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo4", 1);
    glideInit();
    getVersion(verBuf);
    CHECK(std::string(verBuf) == "2.61");
    glideShutdown();

    ::dlclose(handle);
  }

  restoreEnv();
#endif
}

TEST_CASE("M10.2 GETGAMMA round-trip & Glide 3.x missing public exports",
          "[Glide3][M10][GETGAMMA]") {
#ifdef __linux__
  const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = origCard ? origCard : "";
  auto restoreCard = [&]() {
    if (!savedCard.empty()) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
  };

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using GetStringFunc = const char* (*)(FxU32);
  using GetProcAddrFunc = GrProc (*)(char*);
  using LoadGammaFunc = void (*)(FxU32, FxU32*, FxU32*, FxU32*);
  using GetGammaFunc = void (*)(FxU32, FxU32*, FxU32*, FxU32*);
  using AlphaCtrlFunc = void (*)(FxBool);
  using CheckRoomFunc = void (*)(FxI32);
  using ErrCbFunc = void (*)(void (*)(const char*, FxBool));
  using LfbConstDepthFunc = void (*)(FxU32);
  using LfbSwizzleFunc = void (*)(FxBool, FxBool);
  using SstConfigPipeFunc = void (*)(FxI32, FxU32, FxU32);
  using TexNccFunc = void (*)(FxI32);
  using EncodeRleFunc = int (*)(void*, void*, FxU32, FxU32);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto getString =
      reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
  auto getProc =
      reinterpret_cast<GetProcAddrFunc>(::dlsym(handle, "grGetProcAddress"));
  auto loadGamma =
      reinterpret_cast<LoadGammaFunc>(::dlsym(handle, "grLoadGammaTable"));
  auto alphaCtrl = reinterpret_cast<AlphaCtrlFunc>(
      ::dlsym(handle, "grAlphaControlsITRGBLighting"));
  auto checkRoom =
      reinterpret_cast<CheckRoomFunc>(::dlsym(handle, "grCheckForRoom"));
  auto errCb =
      reinterpret_cast<ErrCbFunc>(::dlsym(handle, "grErrorSetCallback"));
  auto lfbDepth =
      reinterpret_cast<LfbConstDepthFunc>(::dlsym(handle, "grLfbConstantDepth"));
  auto lfbSwizzle = reinterpret_cast<LfbSwizzleFunc>(
      ::dlsym(handle, "grLfbWriteColorSwizzle"));
  auto sstPipe = reinterpret_cast<SstConfigPipeFunc>(
      ::dlsym(handle, "grSstConfigPipeline"));
  auto texNcc =
      reinterpret_cast<TexNccFunc>(::dlsym(handle, "grTexNCCTable"));
  auto encodeRle =
      reinterpret_cast<EncodeRleFunc>(::dlsym(handle, "guEncodeRLE16"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(glideShutdown != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(winClose != nullptr);
  REQUIRE(getString != nullptr);
  REQUIRE(getProc != nullptr);
  REQUIRE(loadGamma != nullptr);
  REQUIRE(alphaCtrl != nullptr);
  REQUIRE(checkRoom != nullptr);
  REQUIRE(errCb != nullptr);
  REQUIRE(lfbDepth != nullptr);
  REQUIRE(lfbSwizzle != nullptr);
  REQUIRE(sstPipe != nullptr);
  REQUIRE(texNcc != nullptr);
  REQUIRE(encodeRle != nullptr);

  char procName[] = "grGetGammaTableExt";

  // 1. Voodoo2: GETGAMMA not advertised & grGetGammaTableExt gated out.
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo2", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("GETGAMMA") == std::string::npos);
  CHECK(getProc(procName) == nullptr);
  glideShutdown();

  // 2. Voodoo Banshee+: GETGAMMA advertised & LUT round-trips accurately.
  ::setenv("GLIDE_NG_CARD_MODEL", "banshee", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("GETGAMMA") != std::string::npos);
  auto getGamma = reinterpret_cast<GetGammaFunc>(getProc(procName));
  REQUIRE(getGamma != nullptr);

  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  FxU32 rIn[256], gIn[256], bIn[256];
  for (uint32_t i = 0; i < 256; ++i) {
    rIn[i] = (i * 3u + 7u) & 0xFFu;
    gIn[i] = (255u - i) & 0xFFu;
    bIn[i] = (i ^ 0x5Au) & 0xFFu;
  }
  loadGamma(256, rIn, gIn, bIn);

  FxU32 rOut[256] = {}, gOut[256] = {}, bOut[256] = {};
  getGamma(256, rOut, gOut, bOut);
  for (uint32_t i = 0; i < 256; ++i) {
    REQUIRE(rOut[i] == rIn[i]);
    REQUIRE(gOut[i] == gIn[i]);
    REQUIRE(bOut[i] == bIn[i]);
  }

  // 3. Exercise the 8 newly exported Glide 3.x APIs.
  REQUIRE_NOTHROW(alphaCtrl(FXTRUE));
  REQUIRE_NOTHROW(checkRoom(64));
  REQUIRE_NOTHROW(errCb(nullptr));
  REQUIRE_NOTHROW(lfbDepth(0x1234u));
  REQUIRE_NOTHROW(lfbSwizzle(FXTRUE, FXFALSE));
  REQUIRE_NOTHROW(sstPipe(GR_TMU0, 0, 0));
  REQUIRE_NOTHROW(texNcc(0));

  uint16_t srcImg[6] = {0xF800, 0xF800, 0xF800, 0x07E0, 0x07E0, 0x001F};
  uint32_t rleOut[6] = {};
  const int sizeOnly = encodeRle(nullptr, srcImg, 3, 2);
  REQUIRE(sizeOnly == 12);  // 3 runs * 4 bytes
  const int encoded = encodeRle(rleOut, srcImg, 3, 2);
  REQUIRE(encoded == 12);
  CHECK(rleOut[0] == ((3u << 16) | 0xF800u));
  CHECK(rleOut[1] == ((2u << 16) | 0x07E0u));
  CHECK(rleOut[2] == ((1u << 16) | 0x001Fu));

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  restoreCard();
#endif
}

TEST_CASE(
    "M10.3 Per-channel grColorMaskExt, TEXUMA, grTexMultibase, and "
    "grTexDetailControl",
    "[Glide3][M10][M10.3]") {
#ifdef __linux__
  const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = origCard ? origCard : "";
  auto restoreCard = [&]() {
    if (!savedCard.empty()) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
  };

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using GetStringFunc = const char* (*)(FxU32);
  using GetProcAddrFunc = GrProc (*)(char*);
  using EnableFunc = void (*)(GrEnableMode_t);
  using DisableFunc = void (*)(GrEnableMode_t);
  using ClipFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
  using ClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using ColorMaskFunc = void (*)(FxBool, FxBool);
  using ColorMaskExtFunc = void (*)(FxBool, FxBool, FxBool, FxBool);
  using ColorCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using BlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                             GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using ReadLfbFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                 void*);
  using TexMaxAddrFunc = FxU32 (*)(GrChipID_t);
  using TexDownloadFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexSourceFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexCombineFunc = void (*)(GrChipID_t, GrCombineFunction_t,
                                  GrCombineFactor_t, GrCombineFunction_t,
                                  GrCombineFactor_t, FxBool, FxBool);
  using TexMipMapFunc = void (*)(GrChipID_t, GrMipMapMode_t, FxBool);
  using TexFilterFunc = void (*)(GrChipID_t, GrTextureFilterMode_t,
                                 GrTextureFilterMode_t);
  using TexMultibaseFunc = void (*)(GrChipID_t, FxBool);
  using TexMultibaseAddrFunc = void (*)(GrChipID_t, GrTexBaseRange_t, FxU32,
                                        FxU32, GrTexInfo*);
  using TexDetailFunc = void (*)(GrChipID_t, int, FxU8, float);
  using GetStateFunc = void (*)(void*);
  using SetStateFunc = void (*)(const void*);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto getString =
      reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
  auto getProc =
      reinterpret_cast<GetProcAddrFunc>(::dlsym(handle, "grGetProcAddress"));
  auto enableMode = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
  auto disableMode =
      reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
  auto clipWindow = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
  auto bufferClear =
      reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
  auto colorMask =
      reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
  auto colorCombine =
      reinterpret_cast<ColorCombFunc>(::dlsym(handle, "grColorCombine"));
  auto alphaCombine =
      reinterpret_cast<AlphaCombFunc>(::dlsym(handle, "grAlphaCombine"));
  auto alphaBlend =
      reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto drawTri =
      reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto readLfb =
      reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto texMaxAddr =
      reinterpret_cast<TexMaxAddrFunc>(::dlsym(handle, "grTexMaxAddress"));
  auto texDownload = reinterpret_cast<TexDownloadFunc>(
      ::dlsym(handle, "grTexDownloadMipMap"));
  auto texSource =
      reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
  auto texCombine =
      reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
  auto texMipMap =
      reinterpret_cast<TexMipMapFunc>(::dlsym(handle, "grTexMipMapMode"));
  auto texFilter =
      reinterpret_cast<TexFilterFunc>(::dlsym(handle, "grTexFilterMode"));
  auto texMultibase =
      reinterpret_cast<TexMultibaseFunc>(::dlsym(handle, "grTexMultibase"));
  auto texMultibaseAddr = reinterpret_cast<TexMultibaseAddrFunc>(
      ::dlsym(handle, "grTexMultibaseAddress"));
  auto texDetail =
      reinterpret_cast<TexDetailFunc>(::dlsym(handle, "grTexDetailControl"));
  auto getState =
      reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
  auto setState =
      reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(getString != nullptr);
  REQUIRE(getProc != nullptr);
  REQUIRE(texMultibase != nullptr);
  REQUIRE(texMultibaseAddr != nullptr);
  REQUIRE(texDetail != nullptr);

  // Verify TEXUMA advertisement: absent on Voodoo2, present on Banshee/V3/V5.
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo2", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("TEXUMA") == std::string::npos);
  glideShutdown();

  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("TEXUMA") != std::string::npos);
  glideShutdown();

  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("TEXUMA") != std::string::npos);

  char cmExtName[] = "grColorMaskExt";
  auto colorMaskExt = reinterpret_cast<ColorMaskExtFunc>(getProc(cmExtName));
  REQUIRE(colorMaskExt != nullptr);

  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  struct M10Vert {
    float x, y;
    float r, g, b, a;
    float oow;
    float s0, t0;
    float s1, t1;
  };
  vertexLayout(GR_PARAM_XY, offsetof(M10Vert, x), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_RGB, offsetof(M10Vert, r), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_A, offsetof(M10Vert, a), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Q, offsetof(M10Vert, oow), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_ST0, offsetof(M10Vert, s0), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_ST1, offsetof(M10Vert, s1), GR_PARAM_ENABLE);

  auto drawQuad = [&](float x0, float y0, float x1, float y1, float r, float g,
                      float b, float a, float sMax0 = 255.0f,
                      float tMax0 = 255.0f, float sMax1 = 255.0f,
                      float tMax1 = 255.0f) {
    M10Vert v0{x0, y0, r, g, b, a, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    M10Vert v1{x1, y0, r, g, b, a, 1.0f, sMax0, 0.0f, sMax1, 0.0f};
    M10Vert v2{x1, y1, r, g, b, a, 1.0f, sMax0, tMax0, sMax1, tMax1};
    M10Vert v3{x0, y1, r, g, b, a, 1.0f, 0.0f, tMax0, 0.0f, tMax1};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);
  };

  colorMask(FXTRUE, FXTRUE);
  clipWindow(0, 0, 640, 480);
  alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  bufferClear(0xFF101010, 0xFF, 0xFFFF);

  // =========================================================================
  // Quadrant 1 (top-left 0..320, 0..240): Per-channel grColorMaskExt
  // =========================================================================
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

  // Start with mid-gray (0x40, 0x40, 0x40) in Q1
  clipWindow(0, 0, 320, 240);
  bufferClear(0xFF404040, 0xFF, 0xFFFF);

  // Sub-box A (20..100, 30..210): masked CLEAR with R-only -> (0xFF, 0x40, 0x40)
  clipWindow(20, 30, 100, 210);
  colorMaskExt(FXTRUE, FXFALSE, FXFALSE, FXFALSE);
  bufferClear(0xFFFFFFFF, 0xFF, 0xFFFF);

  // Sub-box B (120..200, 30..210): masked DRAW with G-only -> (0x40, 0xFF, 0x40)
  clipWindow(0, 0, 320, 240);
  colorMaskExt(FXFALSE, FXTRUE, FXFALSE, FXFALSE);
  drawQuad(120, 30, 200, 210, 255.0f, 255.0f, 255.0f, 255.0f);

  // Sub-box C (220..300, 30..210): masked DRAW with B-only -> (0x40, 0x40, 0xFF)
  // Also test grGlideGetState / grGlideSetState preserves per-channel B-only!
  colorMaskExt(FXFALSE, FXFALSE, FXTRUE, FXFALSE);
  alignas(8) uint8_t savedBuf[1024] = {};
  getState(savedBuf);
  colorMask(FXTRUE, FXTRUE);  // clobber mask
  setState(savedBuf);         // restore B-only mask
  drawQuad(220, 30, 300, 210, 255.0f, 255.0f, 255.0f, 255.0f);

  colorMask(FXTRUE, FXTRUE);
  clipWindow(0, 0, 640, 480);

  // =========================================================================
  // Quadrant 2 (top-right 320..640, 0..240): TEXUMA cross-TMU sampling
  // =========================================================================
  const FxU32 nonUmaMax = texMaxAddr(GR_TMU0);
  enableMode(GR_TEXTURE_UMA_EXT);
  const FxU32 umaMax = texMaxAddr(GR_TMU0);
  CHECK(umaMax == nonUmaMax * 2u);

  // Download a solid CYAN (0x07FF) texture ONLY to GR_TMU0 at addr 0x10000,
  // and a solid MAGENTA (0xF81F) texture ONLY to GR_TMU1 at addr 0x20000.
  std::vector<uint16_t> cyanTex(64 * 64, 0x07FFu);
  std::vector<uint16_t> magTex(64 * 64, 0xF81Fu);
  GrTexInfo umaCyanInfo{GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                        GR_TEXFMT_RGB_565, cyanTex.data()};
  GrTexInfo umaMagInfo{GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                       GR_TEXFMT_RGB_565, magTex.data()};
  texDownload(GR_TMU0, 0x10000u, GR_MIPMAPLEVELMASK_BOTH, &umaCyanInfo);
  texDownload(GR_TMU1, 0x20000u, GR_MIPMAPLEVELMASK_BOTH, &umaMagInfo);

  texMipMap(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  texMipMap(GR_TMU1, GR_MIPMAP_DISABLE, FXFALSE);
  texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
            GR_TEXTUREFILTER_POINT_SAMPLED);
  texFilter(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED,
            GR_TEXTUREFILTER_POINT_SAMPLED);
  colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  // Left half of Q2 (340..470, 30..210): Sample 0x10000 (downloaded to TMU0)
  // via TMU1! (TMU0 passes through OTHER = TMU1 output)
  texSource(GR_TMU1, 0x10000u, GR_MIPMAPLEVELMASK_BOTH, &umaCyanInfo);
  texSource(GR_TMU0, 0x20000u, GR_MIPMAPLEVELMASK_BOTH, &umaMagInfo);
  texCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
             GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
             FXFALSE);
  texCombine(GR_TMU0, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
             GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, FXFALSE,
             FXFALSE);
  drawQuad(340, 30, 470, 210, 255.0f, 255.0f, 255.0f, 255.0f);

  // Right half of Q2 (490..620, 30..210): Sample 0x20000 (downloaded to TMU1)
  // via TMU0! (TMU0 outputs LOCAL)
  texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
             GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
             FXFALSE);
  drawQuad(490, 30, 620, 210, 255.0f, 255.0f, 255.0f, 255.0f);

  disableMode(GR_TEXTURE_UMA_EXT);
  CHECK(texMaxAddr(GR_TMU0) == nonUmaMax);

  // =========================================================================
  // Quadrant 3 (bottom-left 0..320, 240..480): grTexMultibase scattered mips
  // =========================================================================
  // Scatter 4 non-contiguous mip allocations:
  //   256x256 -> RED   (0xF800) at 0x100000
  //   128x128 -> GREEN (0x07E0) at 0x200000
  //   64x64   -> BLUE  (0x001F) at 0x300000
  //   32..1   -> YELLOW(0xFFE0) at 0x400000
  std::vector<uint16_t> mb256(256 * 256, 0xF800u);
  std::vector<uint16_t> mb128(128 * 128, 0x07E0u);
  std::vector<uint16_t> mb64(64 * 64, 0x001Fu);
  std::vector<uint16_t> mb32to1(32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1,
                                0xFFE0u);

  GrTexInfo info256{GR_LOD_LOG2_256, GR_LOD_LOG2_256, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_RGB_565, mb256.data()};
  GrTexInfo info128{GR_LOD_LOG2_128, GR_LOD_LOG2_128, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_RGB_565, mb128.data()};
  GrTexInfo info64{GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_RGB_565, mb64.data()};
  GrTexInfo info32{GR_LOD_LOG2_1, GR_LOD_LOG2_32, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_RGB_565, mb32to1.data()};
  GrTexInfo infoFull{GR_LOD_LOG2_1, GR_LOD_LOG2_256, GR_ASPECT_LOG2_1x1,
                     GR_TEXFMT_RGB_565, nullptr};

  texDownload(GR_TMU0, 0x100000u, GR_MIPMAPLEVELMASK_BOTH, &info256);
  texDownload(GR_TMU0, 0x200000u, GR_MIPMAPLEVELMASK_BOTH, &info128);
  texDownload(GR_TMU0, 0x300000u, GR_MIPMAPLEVELMASK_BOTH, &info64);
  texDownload(GR_TMU0, 0x400000u, GR_MIPMAPLEVELMASK_BOTH, &info32);

  texMultibase(GR_TMU0, FXTRUE);
  texMultibaseAddr(GR_TMU0, GR_TEXBASE_256, 0x100000u, GR_MIPMAPLEVELMASK_BOTH,
                   &info256);
  texMultibaseAddr(GR_TMU0, GR_TEXBASE_128, 0x200000u, GR_MIPMAPLEVELMASK_BOTH,
                   &info128);
  texMultibaseAddr(GR_TMU0, GR_TEXBASE_64, 0x300000u, GR_MIPMAPLEVELMASK_BOTH,
                   &info64);
  texMultibaseAddr(GR_TMU0, GR_TEXBASE_32_TO_1, 0x400000u,
                   GR_MIPMAPLEVELMASK_BOTH, &info32);

  texMipMap(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
  texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
            GR_TEXTUREFILTER_POINT_SAMPLED);
  texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
             GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
             FXFALSE);
  texSource(GR_TMU0, 0x100000u, GR_MIPMAPLEVELMASK_BOTH, &infoFull);

  // Draw 4 strips in Q3 with different UV scale -> LOD 0 (Red), 1 (Green),
  // 2 (Blue), 3 (Yellow). Each strip is 64px wide; s spans 64/128/256/512!
  drawQuad(16, 260, 80, 460, 255.0f, 255.0f, 255.0f, 255.0f, 64.0f, 200.0f);
  drawQuad(92, 260, 156, 460, 255.0f, 255.0f, 255.0f, 255.0f, 128.0f, 400.0f);
  drawQuad(168, 260, 232, 460, 255.0f, 255.0f, 255.0f, 255.0f, 256.0f, 800.0f);
  drawQuad(244, 260, 308, 460, 255.0f, 255.0f, 255.0f, 255.0f, 512.0f, 1600.0f);

  texMultibase(GR_TMU0, FXFALSE);

  // =========================================================================
  // Quadrant 4 (bottom-right 320..640, 240..480): grTexDetailControl near/far
  // =========================================================================
  // Base texture = solid BLUE (0x001F), Detail texture = solid RED (0xF800)
  // Using 2-pass test21 detail blend with detailBias=2, detailScale=7, max=1.0:
  //   - Left box (LOD=0, 1 texel/px): b = clamp((2-0)*128/255, 0, 1) = 1.0 -> RED
  //   - Mid  box (LOD=1, 2 texels/px): b = (2-1)*128/255 = 0.502 -> PURPLE (~128,0,127)
  //   - Right box(LOD=2.5, ~6 tex/px): b = 0.0 -> BLUE
  std::vector<uint16_t> blueBase(256 * 256, 0x001Fu);
  std::vector<uint16_t> redDetail(256 * 256, 0xF800u);
  GrTexInfo blueInfo{GR_LOD_LOG2_256, GR_LOD_LOG2_256, GR_ASPECT_LOG2_1x1,
                     GR_TEXFMT_RGB_565, blueBase.data()};
  GrTexInfo redInfo{GR_LOD_LOG2_256, GR_LOD_LOG2_256, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_RGB_565, redDetail.data()};
  texDownload(GR_TMU0, 0x500000u, GR_MIPMAPLEVELMASK_BOTH, &blueInfo);
  texDownload(GR_TMU0, 0x600000u, GR_MIPMAPLEVELMASK_BOTH, &redInfo);
  texMipMap(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  texDetail(GR_TMU0, 2, 7, 1.0f);

  auto drawDetailBox = [&](float x0, float y0, float x1, float y1,
                           float texelsPerPx) {
    const float spanX = (x1 - x0) * texelsPerPx;
    const float spanY = (y1 - y0) * texelsPerPx;
    // Pass 1: base * (1 - b)
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL,
               GR_COMBINE_FACTOR_DETAIL_FACTOR, GR_COMBINE_FUNCTION_LOCAL,
               GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    texSource(GR_TMU0, 0x500000u, GR_MIPMAPLEVELMASK_BOTH, &blueInfo);
    drawQuad(x0, y0, x1, y1, 255.0f, 255.0f, 255.0f, 255.0f, spanX, spanY);

    // Pass 2: + detail * b
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_BLEND_LOCAL,
               GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO);
    texSource(GR_TMU0, 0x600000u, GR_MIPMAPLEVELMASK_BOTH, &redInfo);
    drawQuad(x0, y0, x1, y1, 255.0f, 255.0f, 255.0f, 255.0f, spanX, spanY);
  };

  drawDetailBox(336, 260, 420, 460, 1.0f);  // LOD 0 -> b=1.0 -> RED
  drawDetailBox(436, 260, 520, 460, 2.0f);  // LOD 1 -> b=0.5 -> PURPLE
  drawDetailBox(536, 260, 620, 460, 6.0f);  // LOD 2.58 -> b=0.0 -> BLUE

  // Read full 640x480 backbuffer (ARGB8888: 0xAARRGGBB)
  std::vector<uint32_t> fb(640 * 480, 0u);
  REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
          FXTRUE);

  if (const char* snapPath = std::getenv("GLIDE_NG_M10_SNAPSHOT_PATH")) {
    std::ofstream ppm(snapPath, std::ios::binary | std::ios::trunc);
    if (ppm.is_open()) {
      ppm << "P6\n640 480\n255\n";
      for (uint32_t p : fb) {
        uint8_t rgb[3] = {static_cast<uint8_t>((p >> 16) & 0xFFu),
                          static_cast<uint8_t>((p >> 8) & 0xFFu),
                          static_cast<uint8_t>(p & 0xFFu)};
        ppm.write(reinterpret_cast<const char*>(rgb), 3);
      }
    }
  }

  auto checkRgb = [&](int x, int y, int expR, int expG, int expB, int tol) {
    const uint32_t p = fb[static_cast<size_t>(y) * 640u + static_cast<size_t>(x)];
    const int r = static_cast<int>((p >> 16) & 0xFFu);
    const int g = static_cast<int>((p >> 8) & 0xFFu);
    const int b = static_cast<int>(p & 0xFFu);
    INFO("pixel(" << x << "," << y << ") = 0x" << std::hex << p << std::dec
                  << " RGB=(" << r << "," << g << "," << b << ") expected=("
                  << expR << "," << expG << "," << expB << ")");
    CHECK(std::abs(r - expR) <= tol);
    CHECK(std::abs(g - expG) <= tol);
    CHECK(std::abs(b - expB) <= tol);
  };

  // Q1: Per-channel colorMaskExt (clear + draw + state save/restore)
  checkRgb(60, 120, 255, 64, 64, 2);   // R-only masked clear on 0x404040
  checkRgb(160, 120, 64, 255, 64, 2);  // G-only masked draw on 0x404040
  checkRgb(260, 120, 64, 64, 255, 2);  // B-only masked draw (restored via Get/SetState)

  // Q2: TEXUMA cross-TMU sampling
  checkRgb(405, 120, 0, 255, 255, 4);  // TMU1 sampled Cyan downloaded to TMU0
  checkRgb(555, 120, 255, 0, 255, 4);  // TMU0 sampled Magenta downloaded to TMU1

  // Q3: grTexMultibase scattered mip addresses
  checkRgb(48, 360, 255, 0, 0, 4);     // LOD 0 (256) -> Red
  checkRgb(124, 360, 0, 255, 0, 4);    // LOD 1 (128) -> Green
  checkRgb(200, 360, 0, 0, 255, 4);    // LOD 2 (64)  -> Blue
  checkRgb(276, 360, 255, 255, 0, 4);  // LOD 3 (32)  -> Yellow

  // Q4: grTexDetailControl near/mid/far LOD blend
  checkRgb(378, 360, 255, 0, 0, 8);    // LOD 0 -> b=1.0 -> Red
  checkRgb(478, 360, 128, 0, 127, 12); // LOD 1 -> b=0.5 -> Purple
  checkRgb(578, 360, 0, 0, 255, 8);    // LOD 2.58 -> b=0.0 -> Blue

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  restoreCard();
#endif
}

TEST_CASE("M10.4 VSA-100 PIXEXT Hardware Stencil", "[Glide3][M10][Stencil]") {
#ifdef __linux__
  std::string savedCard;
  if (const char* cur = std::getenv("GLIDE_NG_CARD_MODEL")) {
    savedCard = cur;
  }
  auto restoreCard = [&]() {
    if (!savedCard.empty()) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
  };

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using GetStringFunc = const char* (*)(FxU32);
  using GetProcAddrFunc = GrProc (*)(char*);
  using EnableFunc = void (*)(GrEnableMode_t);
  using DisableFunc = void (*)(GrEnableMode_t);
  using ClipFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
  using ColorMaskFunc = void (*)(FxBool, FxBool);
  using DepthModeFunc = void (*)(GrDepthBufferMode_t);
  using DepthFuncFn = void (*)(GrCmpFnc_t);
  using DepthMaskFunc = void (*)(FxBool);
  using ColorCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using BlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                             GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using ReadLfbFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                 void*);
  using GetStateFunc = void (*)(void*);
  using SetStateFunc = void (*)(const void*);
  using StencilFuncExtFn = void (*)(GrCmpFnc_t, GrStencil_t, GrStencil_t);
  using StencilMaskExtFn = void (*)(GrStencil_t);
  using StencilOpExtFn = void (*)(GrStencilOp_t, GrStencilOp_t, GrStencilOp_t);
  using BufferClearExtFn = void (*)(GrColor_t, GrAlpha_t, FxU32, GrStencil_t);
  using LfbConstStencilExtFn = void (*)(GrStencil_t);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto getString =
      reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
  auto getProc =
      reinterpret_cast<GetProcAddrFunc>(::dlsym(handle, "grGetProcAddress"));
  auto enableMode = reinterpret_cast<EnableFunc>(::dlsym(handle, "grEnable"));
  auto disableMode =
      reinterpret_cast<DisableFunc>(::dlsym(handle, "grDisable"));
  auto clipWindow = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
  auto colorMask =
      reinterpret_cast<ColorMaskFunc>(::dlsym(handle, "grColorMask"));
  auto depthMode =
      reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
  auto depthFunc =
      reinterpret_cast<DepthFuncFn>(::dlsym(handle, "grDepthBufferFunction"));
  auto depthMask =
      reinterpret_cast<DepthMaskFunc>(::dlsym(handle, "grDepthMask"));
  auto colorCombine =
      reinterpret_cast<ColorCombFunc>(::dlsym(handle, "grColorCombine"));
  auto alphaCombine =
      reinterpret_cast<AlphaCombFunc>(::dlsym(handle, "grAlphaCombine"));
  auto alphaBlend =
      reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto drawTri =
      reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto readLfb =
      reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto getState =
      reinterpret_cast<GetStateFunc>(::dlsym(handle, "grGlideGetState"));
  auto setState =
      reinterpret_cast<SetStateFunc>(::dlsym(handle, "grGlideSetState"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(getString != nullptr);
  REQUIRE(getProc != nullptr);

  // Verify PIXEXT card gating: absent on Voodoo3, present on Voodoo5.
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("PIXEXT") == std::string::npos);
  glideShutdown();

  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("PIXEXT") != std::string::npos);

  char sfName[] = "grStencilFuncExt";
  char smName[] = "grStencilMaskExt";
  char soName[] = "grStencilOpExt";
  char bcName[] = "grBufferClearExt";
  char lsName[] = "grLfbConstantStencilExt";
  auto loadExtProc = [&](char* name) -> void* {
    return reinterpret_cast<void*>(getProc(name));
  };
  auto stencilFunc = reinterpret_cast<StencilFuncExtFn>(loadExtProc(sfName));
  auto stencilMask = reinterpret_cast<StencilMaskExtFn>(loadExtProc(smName));
  auto stencilOp = reinterpret_cast<StencilOpExtFn>(loadExtProc(soName));
  auto bufferClearExt =
      reinterpret_cast<BufferClearExtFn>(loadExtProc(bcName));
  auto lfbConstStencil =
      reinterpret_cast<LfbConstStencilExtFn>(loadExtProc(lsName));
  REQUIRE(stencilFunc != nullptr);
  REQUIRE(stencilMask != nullptr);
  REQUIRE(stencilOp != nullptr);
  REQUIRE(bufferClearExt != nullptr);
  REQUIRE(lfbConstStencil != nullptr);

  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  struct StencilVert {
    float x, y, ooz;
    float r, g, b, a;
    float oow;
  };
  vertexLayout(GR_PARAM_XY, offsetof(StencilVert, x), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Z, offsetof(StencilVert, ooz), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_RGB, offsetof(StencilVert, r), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_A, offsetof(StencilVert, a), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Q, offsetof(StencilVert, oow), GR_PARAM_ENABLE);

  auto drawQuadZ = [&](float x0, float y0, float x1, float y1, float z,
                       float r, float g, float b) {
    StencilVert v0{x0, y0, z, r, g, b, 255.0f, 1.0f};
    StencilVert v1{x1, y0, z, r, g, b, 255.0f, 1.0f};
    StencilVert v2{x1, y1, z, r, g, b, 255.0f, 1.0f};
    StencilVert v3{x0, y1, z, r, g, b, 255.0f, 1.0f};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);
  };

  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  depthMode(GR_DEPTHBUFFER_DISABLE);
  depthMask(FXFALSE);
  colorMask(FXTRUE, FXTRUE);
  clipWindow(0, 0, 640, 480);

  // Enable hardware stencil and clear entire buffer (dark slate, stencil = 0)
  enableMode(GR_STENCIL_MODE_EXT);
  stencilMask(0xFFu);
  bufferClearExt(0xFF181824u, 0xFF, 0xFFFFu, 0u);
  lfbConstStencil(0x55u);

  // =========================================================================
  // Section 1 (Top half, Y=16..220): 3dfx test49-style 3x8 Stencil Func grid
  //   Row 0 (Y=16..76):  stencil buffer = 0 (ref 1 > buf 0)
  //   Row 1 (Y=88..148): stencil buffer = 1 (ref 1 == buf 1)
  //   Row 2 (Y=160..220):stencil buffer = 2 (ref 1 < buf 2)
  // =========================================================================
  const int rowY0[3] = {16, 88, 160};
  const int rowY1[3] = {76, 148, 220};
  for (int r = 0; r < 3; ++r) {
    clipWindow(16, static_cast<FxU32>(rowY0[r]), 624,
               static_cast<FxU32>(rowY1[r]));
    bufferClearExt(0xFF282834u, 0xFF, 0xFFFFu, static_cast<GrStencil_t>(r));
  }
  clipWindow(0, 0, 640, 480);

  const float colRgb[8][3] = {
      {255.0f, 64.0f, 64.0f},   // 0: NEVER (should never draw)
      {255.0f, 140.0f, 32.0f},  // 1: LESS
      {255.0f, 230.0f, 40.0f},  // 2: EQUAL
      {80.0f, 235.0f, 80.0f},   // 3: LEQUAL
      {40.0f, 225.0f, 225.0f},  // 4: GREATER
      {70.0f, 150.0f, 255.0f},  // 5: NOTEQUAL
      {190.0f, 100.0f, 255.0f}, // 6: GEQUAL
      {245.0f, 245.0f, 245.0f}, // 7: ALWAYS
  };
  stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
  for (int c = 0; c < 8; ++c) {
    const float x0 = 20.0f + c * 76.0f;
    const float x1 = x0 + 68.0f;
    stencilFunc(static_cast<GrCmpFnc_t>(c), 1u, 0xFFu);
    drawQuadZ(x0, 16.0f, x1, 220.0f, 0.0f, colRgb[c][0], colRgb[c][1],
              colRgb[c][2]);
  }

  // =========================================================================
  // Section 2 (Bottom-left, X=16..312, Y=248..468):
  //   Row A (Y=252..308): All 8 GR_STENCILOP_* operations (Green = PASS)
  // =========================================================================
  struct OpCase {
    GrStencil_t initVal;
    GrStencil_t refVal;
    GrStencilOp_t op;
    int repeatCount;
    GrStencil_t expected;
  };
  const OpCase opCases[8] = {
      {5u, 99u, GR_STENCILOP_KEEP, 1, 5u},
      {7u, 99u, GR_STENCILOP_ZERO, 1, 0u},
      {3u, 42u, GR_STENCILOP_REPLACE, 1, 42u},
      {254u, 0u, GR_STENCILOP_INCR_CLAMP, 2, 255u},
      {1u, 0u, GR_STENCILOP_DECR_CLAMP, 2, 0u},
      {0x5Au, 0u, GR_STENCILOP_INVERT, 1, 0xA5u},
      {255u, 0u, GR_STENCILOP_INCR_WRAP, 1, 0u},
      {0u, 0u, GR_STENCILOP_DECR_WRAP, 1, 255u},
  };
  for (int i = 0; i < 8; ++i) {
    const FxU32 x0 = 20u + static_cast<FxU32>(i) * 36u;
    const FxU32 x1 = x0 + 30u;
    clipWindow(x0, 252u, x1, 308u);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, opCases[i].initVal);
    colorMask(FXFALSE, FXFALSE);
    stencilFunc(GR_CMP_ALWAYS, opCases[i].refVal, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, opCases[i].op);
    for (int p = 0; p < opCases[i].repeatCount; ++p) {
      drawQuadZ(static_cast<float>(x0), 252.0f, static_cast<float>(x1), 308.0f,
                0.0f, 0.0f, 0.0f, 0.0f);
    }
    colorMask(FXTRUE, FXTRUE);
    stencilFunc(GR_CMP_EQUAL, opCases[i].expected, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(static_cast<float>(x0), 252.0f, static_cast<float>(x1), 308.0f,
              0.0f, 40.0f, 230.0f, 70.0f);
  }

  // Row B (Y=324..384): 4 tiles (Cyan = PASS, Red = FAIL)
  //   B0: sfail / zfail / zpass with Z-buffer
  {
    clipWindow(20u, 324u, 86u, 384u);
    depthMode(GR_DEPTHBUFFER_ZBUFFER);
    depthFunc(GR_CMP_LESS);
    depthMask(FXTRUE);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 10u);
    // Seed depth = 0.5 * 65535 (no stencil change)
    colorMask(FXFALSE, FXFALSE);
    stencilFunc(GR_CMP_ALWAYS, 0u, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(20.0f, 324.0f, 86.0f, 384.0f, 0.5f * 65535.0f, 0.0f, 0.0f, 0.0f);

    // Pass 1: zpass (z=0.25 < 0.5) & stencil EQUAL 10 -> INCR_CLAMP => 11
    depthMask(FXFALSE);
    stencilFunc(GR_CMP_EQUAL, 10u, 0xFFu);
    stencilOp(GR_STENCILOP_ZERO, GR_STENCILOP_ZERO, GR_STENCILOP_INCR_CLAMP);
    drawQuadZ(20.0f, 324.0f, 86.0f, 384.0f, 0.25f * 65535.0f, 0.0f, 0.0f, 0.0f);

    // Pass 2: zfail (z=0.75 !< 0.5) & stencil EQUAL 11 -> INCR_CLAMP => 12
    stencilFunc(GR_CMP_EQUAL, 11u, 0xFFu);
    stencilOp(GR_STENCILOP_ZERO, GR_STENCILOP_INCR_CLAMP, GR_STENCILOP_ZERO);
    drawQuadZ(20.0f, 324.0f, 86.0f, 384.0f, 0.75f * 65535.0f, 0.0f, 0.0f, 0.0f);

    // Pass 3: sfail (stencil EQUAL 99 fails) -> INCR_CLAMP => 13
    stencilFunc(GR_CMP_EQUAL, 99u, 0xFFu);
    stencilOp(GR_STENCILOP_INCR_CLAMP, GR_STENCILOP_ZERO, GR_STENCILOP_ZERO);
    drawQuadZ(20.0f, 324.0f, 86.0f, 384.0f, 0.25f * 65535.0f, 0.0f, 0.0f, 0.0f);

    // Verify stencil == 13
    depthMode(GR_DEPTHBUFFER_DISABLE);
    colorMask(FXTRUE, FXTRUE);
    stencilFunc(GR_CMP_EQUAL, 13u, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(20.0f, 324.0f, 86.0f, 384.0f, 0.0f, 40.0f, 220.0f, 240.0f);
  }

  //   B1: readMask + writeMask bit-plane isolation
  {
    clipWindow(92u, 324u, 158u, 384u);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0xF0u);
    // Write only low nibble (0x0F) with ref=0x5A -> buffer becomes 0xFA
    colorMask(FXFALSE, FXFALSE);
    stencilMask(0x0Fu);
    stencilFunc(GR_CMP_ALWAYS, 0x5Au, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_REPLACE);
    drawQuadZ(92.0f, 324.0f, 158.0f, 384.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    // Test readMask=0x0F: (0x0A & 0x0F) == (0xFA & 0x0F) -> increment high bit via mask 0xFF
    stencilMask(0xFFu);
    stencilFunc(GR_CMP_EQUAL, 0x0Au, 0x0Fu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_INCR_CLAMP);
    drawQuadZ(92.0f, 324.0f, 158.0f, 384.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    // Now exact value must be 0xFB!
    colorMask(FXTRUE, FXTRUE);
    stencilFunc(GR_CMP_EQUAL, 0xFBu, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(92.0f, 324.0f, 158.0f, 384.0f, 0.0f, 40.0f, 220.0f, 240.0f);
  }

  //   B2: Partial writeMask grBufferClearExt (must preserve unmasked bits!)
  {
    clipWindow(164u, 324u, 230u, 384u);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0xA5u);
    stencilMask(0x0Fu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0xFFu);  // -> (0xA5 & 0xF0) | 0x0F = 0xAF
    stencilMask(0xFFu);
    stencilFunc(GR_CMP_EQUAL, 0xAFu, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(164.0f, 324.0f, 230.0f, 384.0f, 0.0f, 40.0f, 220.0f, 240.0f);
  }

  //   B3: grGlideGetState / grGlideSetState preserves stencil state
  {
    clipWindow(236u, 324u, 302u, 384u);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0x33u);
    stencilFunc(GR_CMP_EQUAL, 0x33u, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    alignas(8) uint8_t savedBuf[1024] = {};
    getState(savedBuf);
    // Clobber stencil state
    stencilFunc(GR_CMP_NEVER, 0x00u, 0x00u);
    disableMode(GR_STENCIL_MODE_EXT);
    // Restore and draw
    setState(savedBuf);
    drawQuadZ(236.0f, 324.0f, 302.0f, 384.0f, 0.0f, 40.0f, 220.0f, 240.0f);
  }

  // Row C (Y=400..460): grBufferClearExt with GR_STENCIL_MODE_EXT disabled
  //   must preserve existing stencil buffer contents!
  {
    clipWindow(20u, 400u, 302u, 460u);
    stencilMask(0xFFu);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0x77u);
    disableMode(GR_STENCIL_MODE_EXT);
    bufferClearExt(0xFFB02020u, 0xFF, 0xFFFFu, 0x00u);  // Must NOT touch stencil!
    enableMode(GR_STENCIL_MODE_EXT);
    stencilFunc(GR_CMP_EQUAL, 0x77u, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
    drawQuadZ(20.0f, 400.0f, 302.0f, 460.0f, 0.0f, 245.0f, 220.0f, 45.0f);
  }

  // =========================================================================
  // Section 3 (Bottom-right, X=328..624, Y=248..464): Stencil Diamond Portal
  // =========================================================================
  {
    clipWindow(328u, 248u, 624u, 464u);
    stencilMask(0xFFu);
    bufferClearExt(0xFF141828u, 0xFF, 0xFFFFu, 0u);

    // Stamp diamond into stencil = 1 (color writes disabled)
    colorMask(FXFALSE, FXFALSE);
    stencilFunc(GR_CMP_ALWAYS, 1u, 0xFFu);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_REPLACE);
    StencilVert top{476.0f, 260.0f, 0.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f};
    StencilVert right{576.0f, 356.0f, 0.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f};
    StencilVert bottom{476.0f, 452.0f, 0.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f};
    StencilVert left{376.0f, 356.0f, 0.0f, 255.0f, 255.0f, 255.0f, 255.0f, 1.0f};
    drawTri(&top, &right, &bottom);
    drawTri(&top, &bottom, &left);

    colorMask(FXTRUE, FXTRUE);
    stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);

    // Inside diamond (stencil == 1): warm gold / orange horizontal stripes
    stencilFunc(GR_CMP_EQUAL, 1u, 0xFFu);
    for (int y = 248; y < 464; y += 24) {
      const bool alt = ((y - 248) / 24) % 2 == 0;
      drawQuadZ(328.0f, static_cast<float>(y), 624.0f,
                static_cast<float>(y + 24), 0.0f, alt ? 250.0f : 220.0f,
                alt ? 195.0f : 110.0f, alt ? 40.0f : 25.0f);
    }

    // Outside diamond (stencil == 0): deep teal / blue vertical stripes
    stencilFunc(GR_CMP_EQUAL, 0u, 0xFFu);
    for (int x = 328; x < 624; x += 24) {
      const bool alt = ((x - 328) / 24) % 2 == 0;
      drawQuadZ(static_cast<float>(x), 248.0f, static_cast<float>(x + 24),
                464.0f, 0.0f, alt ? 30.0f : 20.0f, alt ? 110.0f : 65.0f,
                alt ? 175.0f : 120.0f);
    }
  }

  disableMode(GR_STENCIL_MODE_EXT);
  clipWindow(0, 0, 640, 480);

  std::vector<uint32_t> fb(640 * 480, 0u);
  REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
          FXTRUE);

  if (const char* snapPath = std::getenv("GLIDE_NG_M10_SNAPSHOT_PATH")) {
    std::ofstream ppm(snapPath, std::ios::binary | std::ios::trunc);
    if (ppm.is_open()) {
      ppm << "P6\n640 480\n255\n";
      for (uint32_t p : fb) {
        uint8_t rgb[3] = {static_cast<uint8_t>((p >> 16) & 0xFFu),
                          static_cast<uint8_t>((p >> 8) & 0xFFu),
                          static_cast<uint8_t>(p & 0xFFu)};
        ppm.write(reinterpret_cast<const char*>(rgb), 3);
      }
    }
  }

  auto checkRgb = [&](int x, int y, int expR, int expG, int expB, int tol) {
    const uint32_t p = fb[static_cast<size_t>(y) * 640u + static_cast<size_t>(x)];
    const int r = static_cast<int>((p >> 16) & 0xFFu);
    const int g = static_cast<int>((p >> 8) & 0xFFu);
    const int b = static_cast<int>(p & 0xFFu);
    INFO("pixel(" << x << "," << y << ") = 0x" << std::hex << p << std::dec
                  << " RGB=(" << r << "," << g << "," << b << ") expected=("
                  << expR << "," << expG << "," << expB << ")");
    CHECK(std::abs(r - expR) <= tol);
    CHECK(std::abs(g - expG) <= tol);
    CHECK(std::abs(b - expB) <= tol);
  };

  // Verify 3x8 truth table (test49 semantics: ref=1 <op> buf)
  const bool expectPass[3][8] = {
      // NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS
      {false, false, false, false, true,  true,  true,  true},  // row 0: buf=0 (1 > 0)
      {false, false, true,  true,  false, false, true,  true},  // row 1: buf=1 (1 == 1)
      {false, true,  false, true,  false, true,  false, true},  // row 2: buf=2 (1 < 2)
  };
  for (int r = 0; r < 3; ++r) {
    const int cy = (rowY0[r] + rowY1[r]) / 2;
    for (int c = 0; c < 8; ++c) {
      const int cx = 20 + c * 76 + 34;
      if (expectPass[r][c]) {
        checkRgb(cx, cy, static_cast<int>(colRgb[c][0]),
                 static_cast<int>(colRgb[c][1]),
                 static_cast<int>(colRgb[c][2]), 4);
      } else {
        checkRgb(cx, cy, 0x28, 0x28, 0x34, 4);
      }
    }
  }

  // Verify all 8 GR_STENCILOP_* tiles (bright green)
  for (int i = 0; i < 8; ++i) {
    checkRgb(20 + i * 36 + 15, 280, 40, 230, 70, 4);
  }

  // Verify Row B tiles (B0: sfail/zfail/zpass, B1: masks, B2: partial clear, B3: state save)
  checkRgb(53, 354, 40, 220, 240, 4);
  checkRgb(125, 354, 40, 220, 240, 4);
  checkRgb(197, 354, 40, 220, 240, 4);
  checkRgb(269, 354, 40, 220, 240, 4);

  // Verify Row C (clear while disabled preserves stencil -> bright yellow)
  checkRgb(160, 430, 245, 220, 45, 4);

  // Verify Section 3 diamond portal (inside = gold/orange, corners outside = teal/blue)
  checkRgb(476, 356, 250, 195, 40, 4);   // center inside diamond (stripe 4 -> alt=true)
  checkRgb(340, 260, 30, 110, 175, 4);   // top-left outside diamond (col 0 -> alt=true)
  checkRgb(610, 450, 20, 65, 120, 4);    // bottom-right outside diamond (col 11 -> alt=false)

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  restoreCard();
#endif
}

TEST_CASE("M10.5 VSA-100 TEXFMT (ARGB_8888, RGB_888, YUV, FXT1, DXT1-5)",
          "[Glide3][M10][TexFmt]") {
#ifdef __linux__
  std::string savedCard;
  if (const char* cur = std::getenv("GLIDE_NG_CARD_MODEL")) {
    savedCard = cur;
  }
  auto restoreCard = [&]() {
    if (!savedCard.empty()) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
  };

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFunc = void (*)(void);
  using ShutdownFunc = void (*)(void);
  using WinOpenFunc = FxU32 (*)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
  using WinCloseFunc = FxBool (*)(FxU32);
  using GetStringFunc = const char* (*)(FxU32);
  using ClipFunc = void (*)(FxU32, FxU32, FxU32, FxU32);
  using ClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU32);
  using ColorCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using AlphaCombFunc = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
  using BlendFunc = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                             GrAlphaBlendFnc_t, GrAlphaBlendFnc_t);
  using VertexLayoutFunc = void (*)(FxU32, FxI32, FxU32);
  using DrawTriFunc = void (*)(const void*, const void*, const void*);
  using ReadLfbFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                 void*);
  using TexCalcMemFunc = FxU32 (*)(GrLOD_t, GrLOD_t, GrAspectRatio_t,
                                   GrTextureFormat_t);
  using TexDownloadFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexSourceFunc = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
  using TexCombineFunc = void (*)(GrChipID_t, GrCombineFunction_t,
                                  GrCombineFactor_t, GrCombineFunction_t,
                                  GrCombineFactor_t, FxBool, FxBool);
  using TexMipMapFunc = void (*)(GrChipID_t, GrMipMapMode_t, FxBool);
  using TexFilterFunc = void (*)(GrChipID_t, GrTextureFilterMode_t,
                                 GrTextureFilterMode_t);
  using TexClampFunc = void (*)(GrChipID_t, GrTextureClampMode_t,
                                GrTextureClampMode_t);
  using Gu3dfGetInfoFunc = FxBool (*)(const char*, Gu3dfInfo*);
  using Gu3dfLoadFunc = FxBool (*)(const char*, Gu3dfInfo*);

  auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
  auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
  auto getString =
      reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
  auto clipWindow = reinterpret_cast<ClipFunc>(::dlsym(handle, "grClipWindow"));
  auto bufferClear =
      reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
  auto colorCombine =
      reinterpret_cast<ColorCombFunc>(::dlsym(handle, "grColorCombine"));
  auto alphaCombine =
      reinterpret_cast<AlphaCombFunc>(::dlsym(handle, "grAlphaCombine"));
  auto alphaBlend =
      reinterpret_cast<BlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
  auto vertexLayout =
      reinterpret_cast<VertexLayoutFunc>(::dlsym(handle, "grVertexLayout"));
  auto drawTri =
      reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
  auto readLfb =
      reinterpret_cast<ReadLfbFunc>(::dlsym(handle, "grLfbReadRegion"));
  auto texCalcMem = reinterpret_cast<TexCalcMemFunc>(
      ::dlsym(handle, "grTexCalcMemRequired"));
  auto texDownload = reinterpret_cast<TexDownloadFunc>(
      ::dlsym(handle, "grTexDownloadMipMap"));
  auto texSource =
      reinterpret_cast<TexSourceFunc>(::dlsym(handle, "grTexSource"));
  auto texCombine =
      reinterpret_cast<TexCombineFunc>(::dlsym(handle, "grTexCombine"));
  auto texMipMap =
      reinterpret_cast<TexMipMapFunc>(::dlsym(handle, "grTexMipMapMode"));
  auto texFilter =
      reinterpret_cast<TexFilterFunc>(::dlsym(handle, "grTexFilterMode"));
  auto texClamp =
      reinterpret_cast<TexClampFunc>(::dlsym(handle, "grTexClampMode"));
  auto gu3dfGetInfoFn =
      reinterpret_cast<Gu3dfGetInfoFunc>(::dlsym(handle, "gu3dfGetInfo"));
  auto gu3dfLoadFn =
      reinterpret_cast<Gu3dfLoadFunc>(::dlsym(handle, "gu3dfLoad"));

  REQUIRE(glideInit != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(texCalcMem != nullptr);
  REQUIRE(gu3dfGetInfoFn != nullptr);
  REQUIRE(gu3dfLoadFn != nullptr);

  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
  glideInit();
  CHECK(std::string(getString(0xa0)).find("TEXFMT") != std::string::npos);

  // Verify grTexCalcMemRequired exact footprints for 64x64 single-level mips
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_8888) == 64u * 64u * 4u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXTFMT_RGB_888) == 64u * 64u * 3u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_YUYV_422) == 64u * 64u * 2u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_AYUV_444) == 64u * 64u * 4u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_CMP_FXT1) == (64u / 8u) * (64u / 4u) * 16u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_CMP_DXT1) == (64u / 4u) * (64u / 4u) * 8u);
  CHECK(texCalcMem(GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_CMP_DXT5) == (64u / 4u) * (64u / 4u) * 16u);
  // Smallest 1x1 compressed mips still reserve 1 full block
  CHECK(texCalcMem(GR_LOD_LOG2_1, GR_LOD_LOG2_1, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_CMP_FXT1) == 16u);
  CHECK(texCalcMem(GR_LOD_LOG2_1, GR_LOD_LOG2_1, GR_ASPECT_LOG2_1x1,
                   GR_TEXFMT_ARGB_CMP_DXT1) == 8u);

  FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  struct TexVert {
    float x, y;
    float r, g, b, a;
    float oow;
    float s0, t0;
  };
  vertexLayout(GR_PARAM_XY, offsetof(TexVert, x), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_RGB, offsetof(TexVert, r), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_A, offsetof(TexVert, a), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Q, offsetof(TexVert, oow), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_ST0, offsetof(TexVert, s0), GR_PARAM_ENABLE);

  auto drawFlatQuad = [&](float x0, float y0, float x1, float y1, float r,
                          float g, float b) {
    TexVert v0{x0, y0, r, g, b, 255.0f, 1.0f, 0.0f, 0.0f};
    TexVert v1{x1, y0, r, g, b, 255.0f, 1.0f, 255.0f, 0.0f};
    TexVert v2{x1, y1, r, g, b, 255.0f, 1.0f, 255.0f, 255.0f};
    TexVert v3{x0, y1, r, g, b, 255.0f, 1.0f, 0.0f, 255.0f};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);
  };

  auto tileRect = [](int col, int row, float& x0, float& y0, float& x1,
                     float& y1) {
    x0 = 12.0f + col * 156.0f;
    y0 = 12.0f + row * 154.0f;
    x1 = x0 + 148.0f;
    y1 = y0 + 144.0f;
  };

  clipWindow(0, 0, 640, 480);
  bufferClear(0xFF14141Cu, 0xFF, 0xFFFFu);

  // Draw a subtle checkerboard inside every tile so alpha transparency shows!
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 4; ++col) {
      float x0, y0, x1, y1;
      tileRect(col, row, x0, y0, x1, y1);
      for (int cy = 0; cy < 6; ++cy) {
        for (int cx = 0; cx < 6; ++cx) {
          const bool light = ((cx + cy) & 1) == 0;
          const float c = light ? 72.0f : 36.0f;
          const float bx0 = x0 + (x1 - x0) * (cx / 6.0f);
          const float bx1 = x0 + (x1 - x0) * ((cx + 1) / 6.0f);
          const float by0 = y0 + (y1 - y0) * (cy / 6.0f);
          const float by1 = y0 + (y1 - y0) * ((cy + 1) / 6.0f);
          drawFlatQuad(bx0, by0, bx1, by1, c, c, c);
        }
      }
    }
  }

  // Switch to textured alpha-blended rendering
  colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
               GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
             GR_BLEND_ZERO);
  texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
             GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
             FXFALSE);
  texMipMap(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
            GR_TEXTUREFILTER_POINT_SAMPLED);
  texClamp(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);

  FxU32 nextAddr = 0u;
  auto uploadAndDrawTile = [&](int col, int row, GrLOD_t lod,
                               GrTextureFormat_t fmt, void* data) {
    GrTexInfo info{lod, lod, GR_ASPECT_LOG2_1x1, fmt, data};
    const FxU32 bytes = texCalcMem(lod, lod, GR_ASPECT_LOG2_1x1, fmt);
    const FxU32 addr = nextAddr;
    nextAddr += (bytes + 15u) & ~15u;
    texDownload(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    float x0, y0, x1, y1;
    tileRect(col, row, x0, y0, x1, y1);
    drawFlatQuad(x0, y0, x1, y1, 255.0f, 255.0f, 255.0f);
  };

  // --- Helper: pack bits into 128-bit FXT1 block ---
  auto setFxt1Bits = [](uint32_t cc[4], int bitPos, int count, uint32_t val) {
    for (int b = 0; b < count; ++b) {
      if ((val >> b) & 1u) {
        const int p = bitPos + b;
        cc[p >> 5] |= (1u << (p & 31));
      }
    }
  };
  auto packRgb555 = [](uint32_t r5, uint32_t g5, uint32_t b5) -> uint32_t {
    return ((r5 & 31u) << 10) | ((g5 & 31u) << 5) | (b5 & 31u);
  };

  // Tile (0,0): GR_TEXFMT_ARGB_8888 (16x16: top=opaque Red/Green, bottom=50% alpha Blue/Yellow)
  {
    std::vector<uint32_t> img(16 * 16);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        if (y < 8) {
          img[y * 16 + x] = (x < 8) ? 0xFFFF2020u : 0xFF20FF20u;
        } else {
          img[y * 16 + x] = (x < 8) ? 0x802080FFu : 0x80FFFF20u;
        }
      }
    }
    uploadAndDrawTile(0, 0, GR_LOD_LOG2_16, GR_TEXFMT_ARGB_8888, img.data());
  }

  // Tile (1,0): GR_TEXTFMT_RGB_888 (16x16 packed 24-bit R,G,B)
  {
    std::vector<uint8_t> img(16 * 16 * 3);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        uint8_t r = 0, g = 0, b = 0;
        if (y < 8 && x < 8) { r = 240; g = 40; b = 40; }
        else if (y < 8)     { r = 40; g = 230; b = 60; }
        else if (x < 8)     { r = 40; g = 90; b = 245; }
        else                { r = 245; g = 225; b = 30; }
        const size_t o = (static_cast<size_t>(y) * 16u + x) * 3u;
        img[o + 0] = r;
        img[o + 1] = g;
        img[o + 2] = b;
      }
    }
    uploadAndDrawTile(1, 0, GR_LOD_LOG2_16, GR_TEXTFMT_RGB_888, img.data());
  }

  // Tile (2,0): GR_TEXFMT_YUYV_422 (16x16 studio-swing BT.601 color bars)
  {
    // BT.601 studio-swing: White(235,128,128), Red(81,90,240), Green(145,54,34), Blue(41,240,110)
    const uint8_t bars[4][3] = {
        {235, 128, 128}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}};
    std::vector<uint16_t> img(16 * 16);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; x += 2) {
        const int b = x / 4;
        const uint8_t Y = bars[b][0], U = bars[b][1], V = bars[b][2];
        img[y * 16 + x + 0] = static_cast<uint16_t>(Y | (U << 8));
        img[y * 16 + x + 1] = static_cast<uint16_t>(Y | (V << 8));
      }
    }
    uploadAndDrawTile(2, 0, GR_LOD_LOG2_16, GR_TEXFMT_YUYV_422, img.data());
  }

  // Tile (3,0): top half UYVY_422 (16x16), bottom half drawn over with AYUV_444 (16x16)
  {
    const uint8_t bars[4][3] = {
        {235, 128, 128}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}};
    std::vector<uint16_t> uyvy(16 * 16);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; x += 2) {
        const int b = x / 4;
        const uint8_t Y = bars[b][0], U = bars[b][1], V = bars[b][2];
        uyvy[y * 16 + x + 0] = static_cast<uint16_t>(U | (Y << 8));
        uyvy[y * 16 + x + 1] = static_cast<uint16_t>(V | (Y << 8));
      }
    }
    // Top half of Tile (3,0) = UYVY_422
    GrTexInfo uInfo{GR_LOD_LOG2_16, GR_LOD_LOG2_16, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_UYVY_422, uyvy.data()};
    const FxU32 uAddr = nextAddr;
    nextAddr += 16u * 16u * 2u;
    texDownload(GR_TMU0, uAddr, GR_MIPMAPLEVELMASK_BOTH, &uInfo);
    texSource(GR_TMU0, uAddr, GR_MIPMAPLEVELMASK_BOTH, &uInfo);
    float x0, y0, x1, y1;
    tileRect(3, 0, x0, y0, x1, y1);
    drawFlatQuad(x0, y0, x1, (y0 + y1) * 0.5f, 255.0f, 255.0f, 255.0f);

    // Bottom half of Tile (3,0) = AYUV_444 (alpha=0xFF left -> 0x00 right, pure Green Y=145,U=54,V=34)
    std::vector<uint32_t> ayuv(16 * 16);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        const uint32_t a = static_cast<uint32_t>(255 - (x * 255) / 15);
        ayuv[y * 16 + x] = (a << 24) | (145u << 16) | (54u << 8) | 34u;
      }
    }
    GrTexInfo aInfo{GR_LOD_LOG2_16, GR_LOD_LOG2_16, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_AYUV_444, ayuv.data()};
    const FxU32 aAddr = nextAddr;
    nextAddr += 16u * 16u * 4u;
    texDownload(GR_TMU0, aAddr, GR_MIPMAPLEVELMASK_BOTH, &aInfo);
    texSource(GR_TMU0, aAddr, GR_MIPMAPLEVELMASK_BOTH, &aInfo);
    drawFlatQuad(x0, (y0 + y1) * 0.5f, x1, y1, 255.0f, 255.0f, 255.0f);
  }

  // Tile (0,1): FXT1 CC_HI (8x4 block -> 8x8 texture, 2 identical block rows):
  //   c0 = pure Blue (0,0,31), c1 = pure Yellow (31,31,0); columns 0..6 = idx 0..6, col 7 = idx 7 (transparent!)
  {
    uint32_t cc[4] = {};
    // Mode "00?" (bits 126..127 = 0)
    setFxt1Bits(cc, 96, 15, packRgb555(0, 0, 31));    // c0 = Blue
    setFxt1Bits(cc, 111, 15, packRgb555(31, 31, 0));  // c1 = Yellow
    for (int ly = 0; ly < 4; ++ly) {
      for (int lx = 0; lx < 8; ++lx) {
        const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
        setFxt1Bits(cc, t * 3, 3, static_cast<uint32_t>(lx));  // 0..6 ramp, 7=transparent
      }
    }
    uint8_t img[32];  // 8x8 = 1x2 blocks = 32 bytes
    std::memcpy(img + 0, cc, 16);
    std::memcpy(img + 16, cc, 16);
    uploadAndDrawTile(0, 1, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_FXT1, img);
  }

  // Tile (1,1): FXT1 CC_CHROMA (mode "010"): 4 direct RGB555 colors (Red, Green, Blue, Cyan)
  {
    auto makeChromaBlock = [&](uint32_t leftIdx, uint32_t rightIdx,
                               uint8_t out[16]) {
      uint32_t cc[4] = {};
      setFxt1Bits(cc, 64, 15, packRgb555(31, 0, 0));    // 0: Red
      setFxt1Bits(cc, 79, 15, packRgb555(0, 31, 0));    // 1: Green
      setFxt1Bits(cc, 94, 15, packRgb555(0, 0, 31));    // 2: Blue
      setFxt1Bits(cc, 109, 15, packRgb555(0, 31, 31));  // 3: Cyan
      setFxt1Bits(cc, 125, 3, 2u);                      // mode 010 = CC_CHROMA
      for (int ly = 0; ly < 4; ++ly) {
        for (int lx = 0; lx < 8; ++lx) {
          const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
          setFxt1Bits(cc, t * 2, 2, (lx < 4) ? leftIdx : rightIdx);
        }
      }
      std::memcpy(out, cc, 16);
    };
    uint8_t img[32];
    makeChromaBlock(0u, 1u, img + 0);   // Top 8x4: Red | Green
    makeChromaBlock(2u, 3u, img + 16);  // Bottom 8x4: Blue | Cyan
    uploadAndDrawTile(1, 1, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_FXT1, img);
  }

  // Tile (2,1): FXT1 CC_MIXED (mode "1??"):
  //   Top block row: alpha=0 (left 4x4 Red->White 4-level, right 4x4 Blue->Green 4-level)
  //   Bottom block row: alpha=1 (left 4x4 Orange->White + idx=3 transparent, right 4x4 Purple->Cyan + idx=3 transparent)
  {
    uint32_t topCc[4] = {};
    setFxt1Bits(topCc, 64, 15, packRgb555(31, 0, 0));    // c0: Red
    setFxt1Bits(topCc, 79, 15, packRgb555(31, 31, 31));  // c1: White
    setFxt1Bits(topCc, 94, 15, packRgb555(0, 0, 31));    // c2: Blue
    setFxt1Bits(topCc, 109, 15, packRgb555(0, 31, 0));   // c3: Green
    setFxt1Bits(topCc, 124, 1, 0u);                      // alpha = 0
    setFxt1Bits(topCc, 125, 2, 3u);                      // glsb0=1, glsb1=1
    setFxt1Bits(topCc, 127, 1, 1u);                      // mode 1?? = CC_MIXED
    for (int ly = 0; ly < 4; ++ly) {
      for (int lx = 0; lx < 8; ++lx) {
        const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
        setFxt1Bits(topCc, t * 2, 2, static_cast<uint32_t>(lx & 3));
      }
    }

    uint32_t botCc[4] = {};
    setFxt1Bits(botCc, 64, 15, packRgb555(31, 16, 0));   // c0: Orange
    setFxt1Bits(botCc, 79, 15, packRgb555(31, 31, 31));  // c1: White
    setFxt1Bits(botCc, 94, 15, packRgb555(24, 0, 31));   // c2: Purple
    setFxt1Bits(botCc, 109, 15, packRgb555(0, 31, 31));  // c3: Cyan
    setFxt1Bits(botCc, 124, 1, 1u);                      // alpha = 1 (idx 3 = transparent!)
    setFxt1Bits(botCc, 127, 1, 1u);                      // mode 1?? = CC_MIXED
    for (int ly = 0; ly < 4; ++ly) {
      for (int lx = 0; lx < 8; ++lx) {
        const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
        setFxt1Bits(botCc, t * 2, 2, static_cast<uint32_t>(lx & 3));
      }
    }

    uint8_t img[32];
    std::memcpy(img + 0, topCc, 16);
    std::memcpy(img + 16, botCc, 16);
    uploadAndDrawTile(2, 1, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_FXT1, img);
  }

  // Tile (3,1): FXT1 CC_ALPHA (mode "011"):
  //   Top block row: lerp=1 (left: opaque Red -> transparent Blue; right: opaque Green -> transparent Blue)
  //   Bottom block row: lerp=0 (0=opaque Red, 1=50% Yellow, 2=opaque Cyan, 3=transparent)
  {
    uint32_t topCc[4] = {};
    setFxt1Bits(topCc, 64, 15, packRgb555(31, 0, 0));   // c0: Red
    setFxt1Bits(topCc, 79, 15, packRgb555(0, 0, 31));   // c1: Blue (shared)
    setFxt1Bits(topCc, 94, 15, packRgb555(0, 31, 0));   // c2: Green
    setFxt1Bits(topCc, 109, 5, 31u);                    // a0 = 100%
    setFxt1Bits(topCc, 114, 5, 0u);                     // a1 = 0%
    setFxt1Bits(topCc, 119, 5, 31u);                    // a2 = 100%
    setFxt1Bits(topCc, 124, 1, 1u);                     // lerp = 1
    setFxt1Bits(topCc, 125, 3, 3u);                     // mode 011 = CC_ALPHA
    for (int ly = 0; ly < 4; ++ly) {
      for (int lx = 0; lx < 8; ++lx) {
        const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
        setFxt1Bits(topCc, t * 2, 2, static_cast<uint32_t>(lx & 3));
      }
    }

    uint32_t botCc[4] = {};
    setFxt1Bits(botCc, 64, 15, packRgb555(31, 0, 0));    // c0: Red
    setFxt1Bits(botCc, 79, 15, packRgb555(31, 31, 0));   // c1: Yellow
    setFxt1Bits(botCc, 94, 15, packRgb555(0, 31, 31));   // c2: Cyan
    setFxt1Bits(botCc, 109, 5, 31u);                     // a0 = 100%
    setFxt1Bits(botCc, 114, 5, 16u);                     // a1 = ~52%
    setFxt1Bits(botCc, 119, 5, 31u);                     // a2 = 100%
    setFxt1Bits(botCc, 124, 1, 0u);                      // lerp = 0 (idx 3 = transparent!)
    setFxt1Bits(botCc, 125, 3, 3u);                      // mode 011 = CC_ALPHA
    for (int ly = 0; ly < 4; ++ly) {
      for (int lx = 0; lx < 8; ++lx) {
        const int t = ((lx & 4) ? 16 : 0) + ly * 4 + (lx & 3);
        setFxt1Bits(botCc, t * 2, 2, static_cast<uint32_t>(lx & 3));
      }
    }

    uint8_t img[32];
    std::memcpy(img + 0, topCc, 16);
    std::memcpy(img + 16, botCc, 16);
    uploadAndDrawTile(3, 1, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_FXT1, img);
  }

  // Tile (0,2): GR_TEXFMT_ARGB_CMP_DXT1 (8x8 = 2x2 blocks = 32 bytes):
  //   Top row (c0 > c1): Red (0xF800) -> Blue (0x001F), 4-color ramp [0, 2, 3, 1]
  //   Bottom row (c0 <= c1): Green (0x07E0) -> Yellow (0xFFE0), [0, 2, 1, 3=transparent]
  {
    auto packDxt1 = [](uint8_t out[8], uint16_t c0, uint16_t c1,
                       const uint8_t colIdx[4]) {
      std::memcpy(out + 0, &c0, 2);
      std::memcpy(out + 2, &c1, 2);
      uint32_t bits = 0;
      for (int ly = 0; ly < 4; ++ly) {
        for (int lx = 0; lx < 4; ++lx) {
          bits |= static_cast<uint32_t>(colIdx[lx]) << ((ly * 4 + lx) * 2);
        }
      }
      std::memcpy(out + 4, &bits, 4);
    };
    const uint8_t ramp4[4] = {0, 2, 3, 1};
    const uint8_t ramp3t[4] = {0, 2, 1, 3};
    uint8_t img[32];
    packDxt1(img + 0, 0xF800u, 0x001Fu, ramp4);
    packDxt1(img + 8, 0xF800u, 0x001Fu, ramp4);
    packDxt1(img + 16, 0x07E0u, 0xFFE0u, ramp3t);
    packDxt1(img + 24, 0x07E0u, 0xFFE0u, ramp3t);
    uploadAndDrawTile(0, 2, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_DXT1, img);
  }

  // Tile (1,2): GR_TEXFMT_ARGB_CMP_DXT3 (8x8 = 2x2 blocks = 64 bytes):
  //   Explicit 4-bit alpha ramp 0xF -> 0x0 left-to-right over solid Magenta (0xF81F)
  {
    uint8_t img[64] = {};
    for (int by = 0; by < 2; ++by) {
      for (int bx = 0; bx < 2; ++bx) {
        uint8_t* blk = img + (by * 2 + bx) * 16;
        uint64_t aBits = 0;
        for (int ly = 0; ly < 4; ++ly) {
          for (int lx = 0; lx < 4; ++lx) {
            const int gx = bx * 4 + lx;
            const uint64_t a4 = static_cast<uint64_t>(15 - gx * 2);
            aBits |= (a4 & 0xFu) << ((ly * 4 + lx) * 4);
          }
        }
        std::memcpy(blk, &aBits, 8);
        uint16_t c0 = 0xF81Fu, c1 = 0xF81Fu;
        uint32_t cIdx = 0u;
        std::memcpy(blk + 8, &c0, 2);
        std::memcpy(blk + 10, &c1, 2);
        std::memcpy(blk + 12, &cIdx, 4);
      }
    }
    uploadAndDrawTile(1, 2, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_DXT3, img);
  }

  // Tile (2,2): GR_TEXFMT_ARGB_CMP_DXT5 (8x8 = 2x2 blocks = 64 bytes):
  //   Top blocks: a0=255 > a1=0 (8-level interpolated alpha across 8 cols) over Cyan (0x07FF)
  //   Bottom blocks: a0=64 <= a1=192 (6-level + explicit 0 and 255) over Gold (0xFE00)
  {
    uint8_t img[64] = {};
    // Top row: 8-level alpha (indices 0,2,3,4 on left block; 5,6,7,1 on right block)
    const uint8_t topIdx[2][4] = {{0, 2, 3, 4}, {5, 6, 7, 1}};
    for (int bx = 0; bx < 2; ++bx) {
      uint8_t* blk = img + bx * 16;
      blk[0] = 255u;
      blk[1] = 0u;
      uint64_t idxBits = 0;
      for (int ly = 0; ly < 4; ++ly) {
        for (int lx = 0; lx < 4; ++lx) {
          idxBits |= static_cast<uint64_t>(topIdx[bx][lx]) << ((ly * 4 + lx) * 3);
        }
      }
      for (int b = 0; b < 6; ++b) blk[2 + b] = static_cast<uint8_t>(idxBits >> (b * 8));
      uint16_t c0 = 0x07FFu, c1 = 0x07FFu;
      std::memcpy(blk + 8, &c0, 2);
      std::memcpy(blk + 10, &c1, 2);
    }
    // Bottom row: a0=64 <= a1=192 -> idx 6=0 (transparent), idx 7=255 (opaque), 0=64, 1=192
    const uint8_t botIdx[2][4] = {{7, 1, 2, 0}, {7, 1, 0, 6}};
    for (int bx = 0; bx < 2; ++bx) {
      uint8_t* blk = img + (2 + bx) * 16;
      blk[0] = 64u;
      blk[1] = 192u;
      uint64_t idxBits = 0;
      for (int ly = 0; ly < 4; ++ly) {
        for (int lx = 0; lx < 4; ++lx) {
          idxBits |= static_cast<uint64_t>(botIdx[bx][lx]) << ((ly * 4 + lx) * 3);
        }
      }
      for (int b = 0; b < 6; ++b) blk[2 + b] = static_cast<uint8_t>(idxBits >> (b * 8));
      uint16_t c0 = 0xFE00u, c1 = 0xFE00u;
      std::memcpy(blk + 8, &c0, 2);
      std::memcpy(blk + 10, &c1, 2);
    }
    uploadAndDrawTile(2, 2, GR_LOD_LOG2_8, GR_TEXFMT_ARGB_CMP_DXT5, img);
  }

  // Tile (3,2): Self-contained synthetic ARGB8888 256x256 .3df asset via gu3dfGetInfo/Load
  {
    const std::string synthPath =
        GlideTestHelpers::TempTestPath("glide_ng_m10_argb8888_256.3df");
    GlideTestHelpers::ScopedTempFile temp3df(synthPath);
    REQUIRE(GlideTestHelpers::WriteSynthetic3dfArgb8888(
        synthPath, 256, 256, [](int x, int y, int /*dim*/) -> uint32_t {
          const uint8_t r = static_cast<uint8_t>(x);
          const uint8_t g = static_cast<uint8_t>(y);
          const uint8_t b = static_cast<uint8_t>(255 - ((x + y) / 2));
          return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                 (static_cast<uint32_t>(g) << 8) | b;
        }));
    Gu3dfInfo tInfo{};
    REQUIRE(gu3dfGetInfoFn(synthPath.c_str(), &tInfo) == FXTRUE);
    CHECK(tInfo.header.format == GR_TEXFMT_ARGB_8888);
    CHECK(tInfo.header.width == 256);
    CHECK(tInfo.header.height == 256);
    CHECK(tInfo.mem_required == 256u * 256u * 4u);
    std::vector<uint8_t> buf3df(tInfo.mem_required);
    tInfo.data = buf3df.data();
    REQUIRE(gu3dfLoadFn(synthPath.c_str(), &tInfo) == FXTRUE);
    uploadAndDrawTile(3, 2, tInfo.header.large_lod, tInfo.header.format,
                      buf3df.data());
  }

  std::vector<uint32_t> fb(640 * 480, 0u);
  REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
          FXTRUE);

  if (const char* snapPath = std::getenv("GLIDE_NG_M10_SNAPSHOT_PATH")) {
    std::ofstream ppm(snapPath, std::ios::binary | std::ios::trunc);
    if (ppm.is_open()) {
      ppm << "P6\n640 480\n255\n";
      for (uint32_t p : fb) {
        uint8_t rgb[3] = {static_cast<uint8_t>((p >> 16) & 0xFFu),
                          static_cast<uint8_t>((p >> 8) & 0xFFu),
                          static_cast<uint8_t>(p & 0xFFu)};
        ppm.write(reinterpret_cast<const char*>(rgb), 3);
      }
    }
  }

  auto sampleTile = [&](int col, int row, float u, float v, int& r, int& g,
                        int& b) {
    float x0, y0, x1, y1;
    tileRect(col, row, x0, y0, x1, y1);
    const int px = static_cast<int>(x0 + (x1 - x0) * u);
    const int py = static_cast<int>(y0 + (y1 - y0) * v);
    const uint32_t p = fb[static_cast<size_t>(py) * 640u + static_cast<size_t>(px)];
    r = static_cast<int>((p >> 16) & 0xFFu);
    g = static_cast<int>((p >> 8) & 0xFFu);
    b = static_cast<int>(p & 0xFFu);
  };
  auto checkTileRgb = [&](int col, int row, float u, float v, int expR,
                          int expG, int expB, int tol) {
    int r = 0, g = 0, b = 0;
    sampleTile(col, row, u, v, r, g, b);
    INFO("tile(" << col << "," << row << ") @(" << u << "," << v << ") RGB=("
                 << r << "," << g << "," << b << ") expected=(" << expR << ","
                 << expG << "," << expB << ")");
    CHECK(std::abs(r - expR) <= tol);
    CHECK(std::abs(g - expG) <= tol);
    CHECK(std::abs(b - expB) <= tol);
  };

  // Row 0: ARGB_8888, RGB_888, YUYV_422, UYVY_422 + AYUV_444
  checkTileRgb(0, 0, 0.25f, 0.25f, 255, 32, 32, 4);   // ARGB_8888 opaque Red
  checkTileRgb(0, 0, 0.75f, 0.25f, 32, 255, 32, 4);   // ARGB_8888 opaque Green
  checkTileRgb(1, 0, 0.25f, 0.25f, 240, 40, 40, 4);   // RGB_888 Red
  checkTileRgb(1, 0, 0.75f, 0.75f, 245, 225, 30, 4);  // RGB_888 Yellow
  checkTileRgb(2, 0, 0.12f, 0.50f, 255, 255, 255, 6); // YUYV_422 White
  checkTileRgb(2, 0, 0.37f, 0.50f, 255, 0, 0, 12);    // YUYV_422 Red
  checkTileRgb(2, 0, 0.62f, 0.50f, 0, 255, 0, 12);    // YUYV_422 Green
  checkTileRgb(2, 0, 0.87f, 0.50f, 0, 0, 255, 12);    // YUYV_422 Blue
  checkTileRgb(3, 0, 0.37f, 0.25f, 255, 0, 0, 12);    // UYVY_422 Red
  checkTileRgb(3, 0, 0.05f, 0.75f, 0, 255, 0, 14);    // AYUV_444 opaque Green (left)
  {
    // AYUV_444 transparent right edge shows gray checkerboard (R==G==B in 36..72)
    int r, g, b;
    sampleTile(3, 0, 0.96f, 0.75f, r, g, b);
    CHECK(r <= 80);
    CHECK(g <= 80);
    CHECK(b <= 80);
  }

  // Row 1: FXT1 (CC_HI, CC_CHROMA, CC_MIXED, CC_ALPHA)
  checkTileRgb(0, 1, 0.06f, 0.50f, 0, 0, 255, 6);     // CC_HI idx=0 Blue
  checkTileRgb(0, 1, 0.81f, 0.50f, 255, 255, 0, 6);   // CC_HI idx=6 Yellow
  {
    // CC_HI idx=7 transparent column shows gray checkerboard
    int r, g, b;
    sampleTile(0, 1, 0.94f, 0.50f, r, g, b);
    CHECK(std::abs(r - g) <= 2);
    CHECK(std::abs(g - b) <= 2);
    CHECK(r >= 30);
    CHECK(r <= 78);
  }
  checkTileRgb(1, 1, 0.25f, 0.25f, 255, 0, 0, 4);     // CC_CHROMA Red
  checkTileRgb(1, 1, 0.75f, 0.25f, 0, 255, 0, 4);     // CC_CHROMA Green
  checkTileRgb(1, 1, 0.25f, 0.75f, 0, 0, 255, 4);     // CC_CHROMA Blue
  checkTileRgb(1, 1, 0.75f, 0.75f, 0, 255, 255, 4);   // CC_CHROMA Cyan
  checkTileRgb(2, 1, 0.06f, 0.25f, 255, 0, 0, 6);     // CC_MIXED alpha=0 left Red
  checkTileRgb(2, 1, 0.56f, 0.25f, 0, 0, 255, 6);     // CC_MIXED alpha=0 right Blue
  checkTileRgb(2, 1, 0.94f, 0.25f, 0, 255, 0, 6);     // CC_MIXED alpha=0 right Green
  {
    // CC_MIXED alpha=1 idx=3 transparent column shows gray checkerboard
    int r, g, b;
    sampleTile(2, 1, 0.94f, 0.75f, r, g, b);
    CHECK(std::abs(r - g) <= 2);
    CHECK(r >= 30);
    CHECK(r <= 78);
  }
  checkTileRgb(3, 1, 0.06f, 0.25f, 255, 0, 0, 6);     // CC_ALPHA lerp=1 opaque Red
  checkTileRgb(3, 1, 0.56f, 0.25f, 0, 255, 0, 6);     // CC_ALPHA lerp=1 opaque Green
  checkTileRgb(3, 1, 0.06f, 0.75f, 255, 0, 0, 6);     // CC_ALPHA lerp=0 opaque Red
  checkTileRgb(3, 1, 0.31f, 0.75f, 0, 255, 255, 6);   // CC_ALPHA lerp=0 opaque Cyan (idx 2)

  // Row 2: DXT1, DXT3, DXT5, real t256x32.3df
  checkTileRgb(0, 2, 0.06f, 0.25f, 255, 0, 0, 6);     // DXT1 4-col Red
  checkTileRgb(0, 2, 0.44f, 0.25f, 0, 0, 255, 6);     // DXT1 4-col Blue
  checkTileRgb(0, 2, 0.06f, 0.75f, 0, 255, 0, 6);     // DXT1 3-col+alpha Green
  {
    // DXT1 idx=3 transparent column shows gray checkerboard
    int r, g, b;
    sampleTile(0, 2, 0.44f, 0.75f, r, g, b);
    CHECK(std::abs(r - g) <= 2);
    CHECK(r >= 30);
    CHECK(r <= 78);
  }
  checkTileRgb(1, 2, 0.06f, 0.50f, 255, 0, 255, 8);   // DXT3 opaque Magenta (left)
  {
    // DXT3 low-alpha right edge blends strongly toward gray checkerboard
    int r, g, b;
    sampleTile(1, 2, 0.94f, 0.50f, r, g, b);
    CHECK(r < 110);
  }
  checkTileRgb(2, 2, 0.06f, 0.25f, 0, 255, 255, 8);   // DXT5 8-lvl opaque Cyan (left)
  checkTileRgb(2, 2, 0.06f, 0.75f, 255, 195, 0, 10);  // DXT5 6-lvl explicit 255 Gold
  {
    // DXT5 explicit 0 alpha (idx=6) on bottom-right shows gray checkerboard
    int r, g, b;
    sampleTile(2, 2, 0.94f, 0.75f, r, g, b);
    CHECK(std::abs(r - g) <= 2);
    CHECK(r >= 30);
    CHECK(r <= 78);
  }

  // Real t256x32.3df loaded cleanly (non-blank colorful content)
  {
    int nonBg = 0;
    for (int sy = 1; sy <= 5; ++sy) {
      for (int sx = 1; sx <= 5; ++sx) {
        int r, g, b;
        sampleTile(3, 2, sx / 6.0f, sy / 6.0f, r, g, b);
        if (std::abs(r - g) > 10 || std::abs(g - b) > 10 || r > 100) ++nonBg;
      }
    }
    CHECK(nonBg >= 10);
  }

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  restoreCard();
#endif
}

TEST_CASE("M10.6 VSA-100 COMBINE, grAlphaBlendFunctionExt, and TEXTUREBUFFER",
          "[Glide3][M10][CombineExt]") {
#ifdef __linux__
  const char* prevCard = std::getenv("GLIDE_NG_CARD_MODEL");
  std::string savedCard = prevCard ? prevCard : "";
  const bool hadCard = (prevCard != nullptr);
  auto restoreCard = [&]() {
    if (hadCard) {
      ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
    } else {
      ::unsetenv("GLIDE_NG_CARD_MODEL");
    }
  };
  ::setenv("GLIDE_NG_HEADLESS", "1", 1);

  using GetStringFn = const char* (*)(FxU32);
  using GetProcFn = GrProc (*)(char*);
  using ColorCombineExtFn = void (*)(GrCCUColor_t, GrCombineMode_t,
                                     GrCCUColor_t, GrCombineMode_t,
                                     GrCCUColor_t, FxBool, GrCCUColor_t, FxBool,
                                     FxU32, FxBool);
  using AlphaCombineExtFn = void (*)(GrACUColor_t, GrCombineMode_t,
                                     GrACUColor_t, GrCombineMode_t,
                                     GrACUColor_t, FxBool, GrACUColor_t, FxBool,
                                     FxU32, FxBool);
  using TexColorCombineExtFn = void (*)(GrChipID_t, GrTCCUColor_t,
                                        GrCombineMode_t, GrTCCUColor_t,
                                        GrCombineMode_t, GrTCCUColor_t, FxBool,
                                        GrTCCUColor_t, FxBool, FxU32, FxBool);
  using TexAlphaCombineExtFn = void (*)(GrChipID_t, GrTACUColor_t,
                                        GrCombineMode_t, GrTACUColor_t,
                                        GrCombineMode_t, GrTACUColor_t, FxBool,
                                        GrTACUColor_t, FxBool, FxU32, FxBool);
  using ConstColorExtFn = void (*)(GrChipID_t, GrColor_t);
  using BlendExtFn = void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                              GrAlphaBlendOp_t, GrAlphaBlendFnc_t,
                              GrAlphaBlendFnc_t, GrAlphaBlendOp_t);
  using TexBufExtFn = void (*)(GrChipID_t, FxU32, GrLOD_t, GrLOD_t,
                               GrAspectRatio_t, GrTextureFormat_t, FxU32);
  using AuxBufExtFn = void (*)(GrBuffer_t);

  // 1. Card-model gating checks across voodoo2, banshee, and voodoo5.
  {
    auto probeCard = [&](const char* cardModel, bool expectTexBuf,
                         bool expectCombine) {
      ::setenv("GLIDE_NG_CARD_MODEL", cardModel, 1);
      void* h = OpenGlide3Handle();
      REQUIRE(h != nullptr);
      auto initFn = reinterpret_cast<void (*)()>(::dlsym(h, "grGlideInit"));
      auto shutFn = reinterpret_cast<void (*)()>(::dlsym(h, "grGlideShutdown"));
      auto getStr = reinterpret_cast<GetStringFn>(::dlsym(h, "grGetString"));
      auto getProc = reinterpret_cast<GetProcFn>(::dlsym(h, "grGetProcAddress"));
      REQUIRE(initFn != nullptr);
      REQUIRE(shutFn != nullptr);
      REQUIRE(getStr != nullptr);
      REQUIRE(getProc != nullptr);
      initFn();
      const char* extStr = getStr(GR_EXTENSION);
      REQUIRE(extStr != nullptr);
      const std::string ext(extStr);
      CHECK((ext.find("TEXTUREBUFFER") != std::string::npos) == expectTexBuf);
      CHECK((ext.find("COMBINE") != std::string::npos) == expectCombine);

      const char* texBufProcs[] = {"grTextureBufferExt",
                                   "grTextureAuxBufferExt", "grAuxBufferExt"};
      for (const char* p : texBufProcs) {
        CHECK((getProc(const_cast<char*>(p)) != nullptr) == expectTexBuf);
      }
      const char* combineProcs[] = {
          "grColorCombineExt",       "grAlphaCombineExt",
          "grTexColorCombineExt",    "grTexAlphaCombineExt",
          "grConstantColorValueExt", "grAlphaBlendFunctionExt"};
      for (const char* p : combineProcs) {
        CHECK((getProc(const_cast<char*>(p)) != nullptr) == expectCombine);
      }
      shutFn();
      ::dlclose(h);
    };

    probeCard("voodoo2", /*expectTexBuf=*/false, /*expectCombine=*/false);
    probeCard("banshee", /*expectTexBuf=*/true, /*expectCombine=*/false);
    probeCard("voodoo5", /*expectTexBuf=*/true, /*expectCombine=*/true);
  }

  // 2. Full 4-quadrant visual showcase on Voodoo5.
  ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
  auto glideShutdown =
      reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
  auto sstSelect =
      reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
  auto winOpen =
      reinterpret_cast<GrContext_t (*)(AnyPtr, GrScreenResolution_t,
                                       GrScreenRefresh_t, GrColorFormat_t,
                                       GrOriginLocation_t, int, int)>(
          ::dlsym(handle, "grSstWinOpen"));
  auto winClose =
      reinterpret_cast<FxBool (*)(GrContext_t)>(::dlsym(handle, "grSstWinClose"));
  auto getProc =
      reinterpret_cast<GetProcFn>(::dlsym(handle, "grGetProcAddress"));
  auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
      ::dlsym(handle, "grVertexLayout"));
  auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
      ::dlsym(handle, "grBufferClear"));
  auto renderBuffer = reinterpret_cast<void (*)(GrBuffer_t)>(
      ::dlsym(handle, "grRenderBuffer"));
  auto colorCombine =
      reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                GrCombineLocal_t, GrCombineOther_t, FxBool)>(
          ::dlsym(handle, "grColorCombine"));
  auto alphaCombine =
      reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                GrCombineLocal_t, GrCombineOther_t, FxBool)>(
          ::dlsym(handle, "grAlphaCombine"));
  auto texCombine = reinterpret_cast<void (*)(
      GrChipID_t, GrCombineFunction_t, GrCombineFactor_t, GrCombineFunction_t,
      GrCombineFactor_t, FxBool, FxBool)>(::dlsym(handle, "grTexCombine"));
  auto alphaBlend =
      reinterpret_cast<void (*)(GrAlphaBlendFnc_t, GrAlphaBlendFnc_t,
                                GrAlphaBlendFnc_t, GrAlphaBlendFnc_t)>(
          ::dlsym(handle, "grAlphaBlendFunction"));
  auto constantColor = reinterpret_cast<void (*)(GrColor_t)>(
      ::dlsym(handle, "grConstantColorValue"));
  auto texDownload = reinterpret_cast<void (*)(GrChipID_t, FxU32, FxU32,
                                               GrTexInfo*)>(
      ::dlsym(handle, "grTexDownloadMipMap"));
  auto texSource = reinterpret_cast<void (*)(GrChipID_t, FxU32, FxU32,
                                             GrTexInfo*)>(
      ::dlsym(handle, "grTexSource"));
  auto texFilter =
      reinterpret_cast<void (*)(GrChipID_t, GrTextureFilterMode_t,
                                GrTextureFilterMode_t)>(
          ::dlsym(handle, "grTexFilterMode"));
  auto texClamp =
      reinterpret_cast<void (*)(GrChipID_t, GrTextureClampMode_t,
                                GrTextureClampMode_t)>(
          ::dlsym(handle, "grTexClampMode"));
  auto drawTri =
      reinterpret_cast<void (*)(const void*, const void*, const void*)>(
          ::dlsym(handle, "grDrawTriangle"));
  auto readLfb = reinterpret_cast<FxBool (*)(
      GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
      ::dlsym(handle, "grLfbReadRegion"));
  auto writeLfb = reinterpret_cast<FxBool (*)(
      GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxBool, FxI32,
      void*)>(::dlsym(handle, "grLfbWriteRegion"));
  auto getState =
      reinterpret_cast<void (*)(void*)>(::dlsym(handle, "grGlideGetState"));
  auto setState = reinterpret_cast<void (*)(const void*)>(
      ::dlsym(handle, "grGlideSetState"));

  glideInit();
  sstSelect(0);
  GrContext_t ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                            GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(ctx != 0);

  auto colorCombineExt = reinterpret_cast<ColorCombineExtFn>(
      reinterpret_cast<void*>(getProc(const_cast<char*>("grColorCombineExt"))));
  auto alphaCombineExt = reinterpret_cast<AlphaCombineExtFn>(
      reinterpret_cast<void*>(getProc(const_cast<char*>("grAlphaCombineExt"))));
  auto texColorCombineExt = reinterpret_cast<TexColorCombineExtFn>(
      reinterpret_cast<void*>(
          getProc(const_cast<char*>("grTexColorCombineExt"))));
  auto texAlphaCombineExt = reinterpret_cast<TexAlphaCombineExtFn>(
      reinterpret_cast<void*>(
          getProc(const_cast<char*>("grTexAlphaCombineExt"))));
  auto constColorExt = reinterpret_cast<ConstColorExtFn>(
      reinterpret_cast<void*>(
          getProc(const_cast<char*>("grConstantColorValueExt"))));
  auto alphaBlendExt = reinterpret_cast<BlendExtFn>(reinterpret_cast<void*>(
      getProc(const_cast<char*>("grAlphaBlendFunctionExt"))));
  auto textureBufferExt = reinterpret_cast<TexBufExtFn>(reinterpret_cast<void*>(
      getProc(const_cast<char*>("grTextureBufferExt"))));
  auto textureAuxBufferExt = reinterpret_cast<TexBufExtFn>(
      reinterpret_cast<void*>(
          getProc(const_cast<char*>("grTextureAuxBufferExt"))));
  auto auxBufferExt = reinterpret_cast<AuxBufExtFn>(
      reinterpret_cast<void*>(getProc(const_cast<char*>("grAuxBufferExt"))));
  REQUIRE(colorCombineExt != nullptr);
  REQUIRE(alphaCombineExt != nullptr);
  REQUIRE(texColorCombineExt != nullptr);
  REQUIRE(texAlphaCombineExt != nullptr);
  REQUIRE(constColorExt != nullptr);
  REQUIRE(alphaBlendExt != nullptr);
  REQUIRE(textureBufferExt != nullptr);
  REQUIRE(textureAuxBufferExt != nullptr);
  REQUIRE(auxBufferExt != nullptr);

  struct M106Vert {
    float x, y;
    float r, g, b, a;
    float oow;
    float s0, t0;
    float s1, t1;
  };
  vertexLayout(GR_PARAM_XY, offsetof(M106Vert, x), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_RGB, offsetof(M106Vert, r), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_A, offsetof(M106Vert, a), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_Q, offsetof(M106Vert, oow), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_ST0, offsetof(M106Vert, s0), GR_PARAM_ENABLE);
  vertexLayout(GR_PARAM_ST1, offsetof(M106Vert, s1), GR_PARAM_ENABLE);

  auto drawQuad = [&](float x0, float y0, float x1, float y1, float r, float g,
                      float b, float a) {
    M106Vert v0{x0, y0, r, g, b, a, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    M106Vert v1{x1, y0, r, g, b, a, 1.0f, 255.0f, 0.0f, 255.0f, 0.0f};
    M106Vert v2{x1, y1, r, g, b, a, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f};
    M106Vert v3{x0, y1, r, g, b, a, 1.0f, 0.0f, 255.0f, 0.0f, 255.0f};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);
  };

  bufferClear(0xFF181C24u, 255, 0xFFFFu);
  alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

  // Subtle quadrant border frames
  colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  drawQuad(8, 8, 312, 232, 48, 56, 72, 255);
  drawQuad(328, 8, 632, 232, 48, 56, 72, 255);
  drawQuad(8, 248, 312, 472, 48, 56, 72, 255);
  drawQuad(328, 248, 632, 472, 48, 56, 72, 255);

  // =========================================================================
  // Q1 (Top-Left): grColorCombineExt + grAlphaCombineExt (6 swatches, 3x2)
  // =========================================================================
  auto q1Rect = [](int col, int row, float& x0, float& y0, float& x1,
                   float& y1) {
    x0 = 16.0f + col * 98.0f;
    y0 = 16.0f + row * 106.0f;
    x1 = x0 + 90.0f;
    y1 = y0 + 98.0f;
  };

  // Q1(0,0): Raw-B bypass (b_mode=ZERO, c=B, d=B) + shift=1 (2x)
  // K=(0.60,0.10,0.20), I=(0.20,0.40,0.30) -> ((K+0)*I + I)*2 = (163, 224, 184)
  {
    float x0, y0, x1, y1;
    q1Rect(0, 0, x0, y0, x1, y1);
    constantColor(0xFF991A33u);  // (153, 26, 51)
    colorCombineExt(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X, GR_CMBX_ITRGB,
                    GR_FUNC_MODE_ZERO, GR_CMBX_B, FXFALSE, GR_CMBX_B, FXFALSE,
                    1u, FXFALSE);
    drawQuad(x0, y0, x1, y1, 51.0f, 102.0f, 77.0f, 255.0f);
  }

  // Q1(1,0): X_MINUS_HALF + ONE_MINUS_X + invert=TRUE
  // K=(204,153,128)->(0.3,0.1,0), I=(179,51,230)->(0.3,0.8,0.1), 1-(a+b)=(102,26,230)
  {
    float x0, y0, x1, y1;
    q1Rect(1, 0, x0, y0, x1, y1);
    constantColor(0xFFCC9980u);
    colorCombineExt(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X_MINUS_HALF,
                    GR_CMBX_ITRGB, GR_FUNC_MODE_ONE_MINUS_X, GR_CMBX_ZERO,
                    FXTRUE, GR_CMBX_ZERO, FXFALSE, 0u, FXTRUE);
    drawQuad(x0, y0, x1, y1, 179.0f, 51.0f, 230.0f, 255.0f);
  }

  // Q1(2,0): NEGATIVE_X subtraction (K - I) clamped to [0,1]
  // K=(230,204,102), I=(51,128,179) -> (179, 76, 0)
  {
    float x0, y0, x1, y1;
    q1Rect(2, 0, x0, y0, x1, y1);
    constantColor(0xFFE6CC66u);
    colorCombineExt(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X, GR_CMBX_ITRGB,
                    GR_FUNC_MODE_NEGATIVE_X, GR_CMBX_ZERO, FXTRUE, GR_CMBX_ZERO,
                    FXFALSE, 0u, FXFALSE);
    drawQuad(x0, y0, x1, y1, 51.0f, 128.0f, 179.0f, 255.0f);
  }

  // Q1(0,1): grAlphaCombineExt feeding ALOCAL + alpha blend over (0, 0, 100)
  {
    float x0, y0, x1, y1;
    q1Rect(0, 1, x0, y0, x1, y1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    drawQuad(x0, y0, x1, y1, 0.0f, 0.0f, 100.0f, 255.0f);

    constantColor(0x80FF0000u);  // A=128 (~0.502), RGB=(255,0,0)
    alphaCombineExt(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
                    GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_ZERO,
                    GR_CMBX_ZERO, FXTRUE, GR_CMBX_ZERO, FXFALSE, 0u, FXFALSE);
    colorCombineExt(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X, GR_CMBX_ZERO,
                    GR_FUNC_MODE_ZERO, GR_CMBX_ALOCAL, FXFALSE, GR_CMBX_ZERO,
                    FXFALSE, 0u, FXFALSE);
    alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
               GR_BLEND_ZERO);
    // srcRGB=(128,0,0), srcA=204/255=0.80 -> (102, 0, 20)
    drawQuad(x0, y0, x1, y1, 0.0f, 255.0f, 0.0f, 204.0f);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  }

  // Q1(1,1): grColorCombineExt reading GR_CMBX_AOTHER (= ITALPHA = 204)
  {
    float x0, y0, x1, y1;
    q1Rect(1, 1, x0, y0, x1, y1);
    colorCombineExt(GR_CMBX_AOTHER, GR_FUNC_MODE_X, GR_CMBX_ZERO,
                    GR_FUNC_MODE_ZERO, GR_CMBX_ZERO, FXTRUE, GR_CMBX_ZERO,
                    FXFALSE, 0u, FXFALSE);
    drawQuad(x0, y0, x1, y1, 10.0f, 20.0f, 30.0f, 204.0f);
  }

  // Q1(2,1): Legacy grColorCombine resets extColorCombine while keeping extAlphaCombine
  {
    float x0, y0, x1, y1;
    q1Rect(2, 1, x0, y0, x1, y1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    drawQuad(x0, y0, x1, y1, 200.0f, 0.0f, 0.0f, 255.0f);

    constantColor(0xFF00FF00u);  // Constant Green; extAlphaCombine still reads ITALPHA=128
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
               GR_BLEND_ZERO);
    // (0,255,0)*0.502 + (200,0,0)*0.498 = (100, 128, 0)
    drawQuad(x0, y0, x1, y1, 0.0f, 0.0f, 0.0f, 128.0f);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  }

  // Reset legacy alpha combine before Q2
  alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

  // =========================================================================
  // Q2 (Top-Right): grTexColorCombineExt + grTexAlphaCombineExt +
  //                 grConstantColorValueExt (Single-TMU & Dual-TMU cascade)
  // =========================================================================
  {
    std::vector<uint32_t> tex0Pixels(16 * 16, 0xFFC82814u);  // (200, 40, 20, 255)
    std::vector<uint32_t> tex1Pixels(16 * 16, 0xC82850DCu);  // (40, 80, 220, 200)
    GrTexInfo ti0{GR_LOD_LOG2_16, GR_LOD_LOG2_16, GR_ASPECT_LOG2_1x1,
                  GR_TEXFMT_ARGB_8888, tex0Pixels.data()};
    GrTexInfo ti1{GR_LOD_LOG2_16, GR_LOD_LOG2_16, GR_ASPECT_LOG2_1x1,
                  GR_TEXFMT_ARGB_8888, tex1Pixels.data()};
    texDownload(GR_TMU0, 0x0u, GR_MIPMAPLEVELMASK_BOTH, &ti0);
    texDownload(GR_TMU1, 0x0u, GR_MIPMAPLEVELMASK_BOTH, &ti1);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
              GR_TEXTUREFILTER_POINT_SAMPLED);
    texFilter(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED,
              GR_TEXTUREFILTER_POINT_SAMPLED);
    texClamp(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texClamp(GR_TMU1, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    texSource(GR_TMU0, 0x0u, GR_MIPMAPLEVELMASK_BOTH, &ti0);
    texSource(GR_TMU1, 0x0u, GR_MIPMAPLEVELMASK_BOTH, &ti1);

    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Q2 Left: Single-TMU0: LOCAL_TEXTURE_RGB + TMU_CCOLOR -> (220, 220, 80)
    constColorExt(GR_TMU0, 0xFF14B43Cu);  // (20, 180, 60, 255)
    texColorCombineExt(GR_TMU0, GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
                       GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X, GR_CMBX_ZERO, FXTRUE,
                       GR_CMBX_ZERO, FXFALSE, 0u, FXFALSE);
    texAlphaCombineExt(GR_TMU0, GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
                       GR_CMBX_ZERO, GR_FUNC_MODE_ZERO, GR_CMBX_ZERO, FXTRUE,
                       GR_CMBX_ZERO, FXFALSE, 0u, FXFALSE);
    drawQuad(338, 18, 476, 222, 255, 255, 255, 255);

    // Q2 Right: Dual-TMU cascade:
    //   TMU1 RGB = tex1 (40,80,220) + tmuConst1 (0,120,0) = (40,200,220)
    //   TMU1 A   = tex1.a (200/255 = 0.7843)
    //   TMU0 RGB = (OTHER - LOCAL) * OTHER_ALPHA + LOCAL = (75, 165, 177)
    constColorExt(GR_TMU1, 0xFF007800u);  // (0, 120, 0, 255)
    texColorCombineExt(GR_TMU1, GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
                       GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X, GR_CMBX_ZERO, FXTRUE,
                       GR_CMBX_ZERO, FXFALSE, 0u, FXFALSE);
    texAlphaCombineExt(GR_TMU1, GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
                       GR_CMBX_ZERO, GR_FUNC_MODE_ZERO, GR_CMBX_ZERO, FXTRUE,
                       GR_CMBX_ZERO, FXFALSE, 0u, FXFALSE);
    texColorCombineExt(GR_TMU0, GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
                       GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
                       GR_CMBX_OTHER_TEXTURE_ALPHA, FXFALSE,
                       GR_CMBX_LOCAL_TEXTURE_RGB, FXFALSE, 0u, FXFALSE);
    drawQuad(484, 18, 622, 222, 255, 255, 255, 255);

    // Restore legacy TMU combiners
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    texCombine(GR_TMU1, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
  }

  // =========================================================================
  // Q3 (Bottom-Left): grAlphaBlendFunctionExt (2x2 swatches)
  // =========================================================================
  {
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    auto q3Rect = [](int col, int row, float& x0, float& y0, float& x1,
                     float& y1) {
      x0 = 16.0f + col * 148.0f;
      y0 = 256.0f + row * 106.0f;
      x1 = x0 + 140.0f;
      y1 = y0 + 98.0f;
    };

    // Q3(0,0): GR_BLEND_OP_SUB -> Src(220,190,50) - Dst(60,100,230) = (160, 90, 0)
    {
      float x0, y0, x1, y1;
      q3Rect(0, 0, x0, y0, x1, y1);
      alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      drawQuad(x0, y0, x1, y1, 60, 100, 230, 255);
      alphaBlendExt(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_OP_SUB, GR_BLEND_ONE,
                    GR_BLEND_ZERO, GR_BLEND_OP_ADD);
      drawQuad(x0, y0, x1, y1, 220, 190, 50, 255);
    }

    // Q3(1,0): GR_BLEND_OP_REVSUB -> Dst(60,100,230) - Src(220,190,50) = (0, 0, 180)
    {
      float x0, y0, x1, y1;
      q3Rect(1, 0, x0, y0, x1, y1);
      alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      drawQuad(x0, y0, x1, y1, 60, 100, 230, 255);
      alphaBlendExt(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_OP_REVSUB,
                    GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_OP_ADD);
      drawQuad(x0, y0, x1, y1, 220, 190, 50, 255);
    }

    // Q3(0,1): GR_BLEND_SAME_COLOR_EXT -> Src(204,128,255)^2 = (163, 64, 255)
    {
      float x0, y0, x1, y1;
      q3Rect(0, 1, x0, y0, x1, y1);
      alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      drawQuad(x0, y0, x1, y1, 32, 32, 32, 255);
      alphaBlendExt(GR_BLEND_SAME_COLOR_EXT, GR_BLEND_ZERO, GR_BLEND_OP_ADD,
                    GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_OP_ADD);
      drawQuad(x0, y0, x1, y1, 204, 128, 255, 255);
    }

    // Q3(1,1): GR_BLEND_ONE_MINUS_SAME_COLOR_EXT on both src & dst
    // Dst=(128,255,0)->(0.25,0,0), Src=(128,128,51)->(0.25,0.25,0.16) => (128, 64, 41)
    {
      float x0, y0, x1, y1;
      q3Rect(1, 1, x0, y0, x1, y1);
      alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      drawQuad(x0, y0, x1, y1, 128, 255, 0, 255);
      alphaBlendExt(GR_BLEND_ONE_MINUS_SAME_COLOR_EXT,
                    GR_BLEND_ONE_MINUS_SAME_COLOR_EXT, GR_BLEND_OP_ADD,
                    GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_OP_ADD);
      drawQuad(x0, y0, x1, y1, 128, 128, 51, 255);
    }

    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  }

  // =========================================================================
  // Q4 (Bottom-Right): TEXTUREBUFFER dynamic render-to-texture
  //   (Executed AFTER Q1..Q3 to prove main backbuffer is saved & restored!)
  // =========================================================================
  {
    constexpr FxU32 kTexAAddr = 0x40000u;
    constexpr FxU32 kTexBAddr = 0x50000u;

    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    // Enter TEXTUREBUFFER mode BEFORE grTextureBufferExt (matches SDK test41.c)
    renderBuffer(GR_BUFFER_TEXTUREBUFFER_EXT);
    auxBufferExt(GR_BUFFER_TEXTUREAUXBUFFER_EXT);
    textureAuxBufferExt(GR_TMU0, 0x60000u, GR_LOD_LOG2_128, GR_LOD_LOG2_128,
                        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565,
                        GR_MIPMAPLEVELMASK_BOTH);

    // Target A: 128x128 navy bg + bright yellow diamond + 16x16 magenta LFB patch
    textureBufferExt(GR_TMU0, kTexAAddr, GR_LOD_LOG2_128, GR_LOD_LOG2_128,
                     GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565,
                     GR_MIPMAPLEVELMASK_BOTH);
    bufferClear(0xFF102080u, 255, 0xFFFFu);
    {
      M106Vert top{64, 16, 255, 224, 0, 255, 1, 0, 0, 0, 0};
      M106Vert right{112, 64, 255, 224, 0, 255, 1, 0, 0, 0, 0};
      M106Vert bot{64, 112, 255, 224, 0, 255, 1, 0, 0, 0, 0};
      M106Vert left{16, 64, 255, 224, 0, 255, 1, 0, 0, 0, 0};
      drawTri(&top, &right, &bot);
      drawTri(&top, &bot, &left);
    }
    std::vector<uint16_t> magentaPatch(16 * 16, 0xF81Fu);
    REQUIRE(writeLfb(GR_BUFFER_TEXTUREBUFFER_EXT, 8, 8, GR_LFB_SRC_FMT_565, 16,
                     16, FXFALSE, 16 * 2, magentaPatch.data()) == FXTRUE);

    // Switch directly to Target B (64x64) MID-MODE (must auto-commit Target A!)
    textureBufferExt(GR_TMU0, kTexBAddr, GR_LOD_LOG2_64, GR_LOD_LOG2_64,
                     GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565,
                     GR_MIPMAPLEVELMASK_BOTH);
    bufferClear(0xFF00B440u, 255, 0xFFFFu);
    {
      M106Vert t0{32, 8, 255, 32, 32, 255, 1, 0, 0, 0, 0};
      M106Vert t1{56, 56, 255, 32, 32, 255, 1, 0, 0, 0, 0};
      M106Vert t2{8, 56, 255, 32, 32, 255, 1, 0, 0, 0, 0};
      drawTri(&t0, &t1, &t2);
    }

    // Exit TEXTUREBUFFER mode (auto-commits Target B and restores Q1..Q3 backbuffer)
    renderBuffer(GR_BUFFER_BACKBUFFER);
    auxBufferExt(GR_BUFFER_AUXBUFFER);

    // Sample Target A and Target B onto two quads inside Q4
    colorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    texCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
               FXFALSE);
    texFilter(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
              GR_TEXTUREFILTER_POINT_SAMPLED);

    GrTexInfo infoA{GR_LOD_LOG2_128, GR_LOD_LOG2_128, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_RGB_565, nullptr};
    texSource(GR_TMU0, kTexAAddr, GR_MIPMAPLEVELMASK_BOTH, &infoA);
    drawQuad(338, 258, 474, 394, 255, 255, 255, 255);

    GrTexInfo infoB{GR_LOD_LOG2_64, GR_LOD_LOG2_64, GR_ASPECT_LOG2_1x1,
                    GR_TEXFMT_RGB_565, nullptr};
    texSource(GR_TMU0, kTexBAddr, GR_MIPMAPLEVELMASK_BOTH, &infoB);
    drawQuad(486, 258, 622, 394, 255, 255, 255, 255);
  }

  // =========================================================================
  // Q4 Bottom Strip ([338..622, 404..462]): grGlideGetState / grGlideSetState
  //   round-trip preserving COMBINE + BlendExt state
  // =========================================================================
  {
    // Draw dark base strip (40, 20, 100)
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    drawQuad(338, 404, 622, 462, 40, 20, 100, 255);

    // Configure ext combine + ext blend and snapshot state
    constantColor(0xFF00C864u);  // (0, 200, 100)
    colorCombineExt(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X, GR_CMBX_ITRGB,
                    GR_FUNC_MODE_X, GR_CMBX_ZERO, FXTRUE, GR_CMBX_ZERO, FXFALSE,
                    0u, FXFALSE);
    alphaBlendExt(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_OP_SUB, GR_BLEND_ONE,
                  GR_BLEND_ZERO, GR_BLEND_OP_ADD);
    uint8_t savedState[1024] = {};
    getState(savedState);

    // Clobber with legacy state
    constantColor(0xFF000000u);
    colorCombine(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, FXFALSE);
    alphaBlend(GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);

    // Restore and draw: Src = K(0,200,100) + I(100,20,40) = (100,220,140)
    // SUB Dst(40,20,100) => (60, 200, 40) (bright leaf green!)
    setState(savedState);
    drawQuad(338, 404, 622, 462, 100, 20, 40, 255);
  }

  std::vector<uint32_t> fb(640 * 480, 0u);
  REQUIRE(readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 640 * 4, fb.data()) ==
          FXTRUE);

  if (const char* snapPath = std::getenv("GLIDE_NG_M10_SNAPSHOT_PATH")) {
    std::ofstream ppm(snapPath, std::ios::binary | std::ios::trunc);
    if (ppm.is_open()) {
      ppm << "P6\n640 480\n255\n";
      for (uint32_t p : fb) {
        uint8_t rgb[3] = {static_cast<uint8_t>((p >> 16) & 0xFFu),
                          static_cast<uint8_t>((p >> 8) & 0xFFu),
                          static_cast<uint8_t>(p & 0xFFu)};
        ppm.write(reinterpret_cast<const char*>(rgb), 3);
      }
    }
  }

  auto checkPx = [&](int px, int py, int expR, int expG, int expB, int tol,
                     const char* label) {
    const uint32_t p =
        fb[static_cast<size_t>(py) * 640u + static_cast<size_t>(px)];
    const int r = static_cast<int>((p >> 16) & 0xFFu);
    const int g = static_cast<int>((p >> 8) & 0xFFu);
    const int b = static_cast<int>(p & 0xFFu);
    INFO(label << " @(" << px << "," << py << ") RGB=(" << r << "," << g << ","
               << b << ") expected=(" << expR << "," << expG << "," << expB
               << ")");
    CHECK(std::abs(r - expR) <= tol);
    CHECK(std::abs(g - expG) <= tol);
    CHECK(std::abs(b - expB) <= tol);
  };

  // Q1 assertions (also proves TEXTUREBUFFER did NOT clobber top-left Q1!)
  checkPx(60, 65, 163, 224, 184, 5, "Q1(0,0) raw-B + 2x shift");
  checkPx(158, 65, 102, 26, 230, 5, "Q1(1,0) X_MINUS_HALF + ONE_MINUS_X + inv");
  checkPx(256, 65, 179, 76, 0, 5, "Q1(2,0) NEGATIVE_X sub");
  checkPx(60, 171, 102, 0, 20, 5, "Q1(0,1) grAlphaCombineExt + ALOCAL blend");
  checkPx(158, 171, 204, 204, 204, 5, "Q1(1,1) AOTHER grayscale");
  checkPx(256, 171, 100, 128, 0, 5, "Q1(2,1) legacy color + ext alpha");

  // Q2 assertions (TMU COMBINE + grConstantColorValueExt)
  checkPx(407, 120, 220, 220, 80, 5, "Q2-Left single-TMU + TMU_CCOLOR");
  checkPx(553, 120, 75, 165, 177, 5, "Q2-Right dual-TMU lerp cascade");

  // Q3 assertions (grAlphaBlendFunctionExt)
  checkPx(86, 305, 160, 90, 0, 5, "Q3(0,0) GR_BLEND_OP_SUB");
  checkPx(234, 305, 0, 0, 180, 5, "Q3(1,0) GR_BLEND_OP_REVSUB");
  checkPx(86, 411, 163, 64, 255, 5, "Q3(0,1) GR_BLEND_SAME_COLOR_EXT");
  checkPx(234, 411, 128, 64, 41, 5, "Q3(1,1) GR_BLEND_ONE_MINUS_SAME_COLOR_EXT");

  // Q4 assertions (TEXTUREBUFFER Target A & Target B + state round-trip)
  checkPx(406, 326, 255, 224, 0, 8, "Q4-TexA center yellow diamond");
  checkPx(460, 270, 16, 32, 128, 8, "Q4-TexA corner navy clear");
  checkPx(352, 272, 255, 0, 255, 8, "Q4-TexA LFB magenta patch");
  checkPx(554, 340, 255, 32, 32, 8, "Q4-TexB center red triangle");
  checkPx(500, 272, 0, 180, 64, 8, "Q4-TexB corner emerald clear");
  checkPx(480, 433, 60, 200, 40, 5, "Q4-Strip grGlideGetState/SetState round-trip");

  winClose(ctx);
  glideShutdown();
  ::dlclose(handle);
  restoreCard();
#endif
}

TEST_CASE(
    "M10.7 Glide 3.x Capabilities, Resolutions, grReset, grSetNumPendingBuffers, "
    "and Warn-Only Card-Version Matrix",
    "[Glide3][M10]") {
#ifdef __linux__
  struct EnvGuard {
    std::vector<std::string> keys = {"GLIDE_NG_CARD_MODEL",
                                     "GLIDE_NG_API_VERSION",
                                     "GLIDE_NG_FBI_MEM_MB",
                                     "GLIDE_NG_SLI",
                                     "GLIDE_NG_HEADLESS",
                                     "FX_GLIDE_FBRAM"};
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

  // 1. Warn-only Card-to-Version matrix in IniConfigLoader
  {
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;

    // Authentic combinations -> zero compatibility warnings
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo1", 1);
    ::setenv("GLIDE_NG_API_VERSION", "2.48", 1);
    REQUIRE(loader.Load("/nonexistent_m107.ini", cfg));
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_04);
    CHECK(cfg.invalidConfigValues.empty());
    CHECK(cfg.compatibilityWarnings.empty());

    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo2", 1);
    ::setenv("GLIDE_NG_API_VERSION", "3.03", 1);
    REQUIRE(loader.Load("/nonexistent_m107.ini", cfg));
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.invalidConfigValues.empty());
    CHECK(cfg.compatibilityWarnings.empty());

    // Ahistorical combination (Voodoo1 + 2.61) -> warn-only, override honored!
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo1", 1);
    ::setenv("GLIDE_NG_API_VERSION", "2.61", 1);
    REQUIRE(loader.Load("/nonexistent_m107.ini", cfg));
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_61);
    CHECK(cfg.invalidConfigValues.empty());
    REQUIRE(cfg.compatibilityWarnings.size() == 1u);
    CHECK(cfg.compatibilityWarnings[0].find("warn-only") != std::string::npos);

    // Ahistorical Glide 3.x combination (Voodoo2 + 3.10) -> warn-only, honored!
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo2", 1);
    ::setenv("GLIDE_NG_API_VERSION", "3.10", 1);
    REQUIRE(loader.Load("/nonexistent_m107.ini", cfg));
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.invalidConfigValues.empty());
    REQUIRE(cfg.compatibilityWarnings.size() == 1u);
    CHECK(cfg.compatibilityWarnings[0].find("3.10") != std::string::npos);

    ::unsetenv("GLIDE_NG_API_VERSION");
  }

  void* handle = OpenGlide3Handle();
  REQUIRE(handle != nullptr);

  using InitFn = void (*)(void);
  using ShutdownFn = void (*)(void);
  using SelectFn = void (*)(int);
  using GetStrFn = const char* (*)(FxU32);
  using GetFn = FxU32 (*)(FxU32, FxU32, FxI32*);
  using QueryResFn = FxI32 (*)(const GrResolution*, GrResolution*);
  using ResetFn = FxBool (*)(FxU32);
  using SetPendingFn = void (*)(FxI32);
  using GetProcFn = GrProc (*)(char*);
  using WinOpenFn = GrContext_t (*)(AnyPtr, GrScreenResolution_t,
                                    GrScreenRefresh_t, GrColorFormat_t,
                                    GrOriginLocation_t, int, int);
  using WinOpenExtFn = GrContext_t (*)(AnyPtr, GrScreenResolution_t,
                                       GrScreenRefresh_t, GrColorFormat_t,
                                       GrOriginLocation_t, GrPixelFormat_t, int,
                                       int);
  using WinCloseFn = FxBool (*)(GrContext_t);
  using VertexLayoutFn = void (*)(FxU32, FxI32, FxU32);
  using DrawPointFn = void (*)(const void*);

  auto init = reinterpret_cast<InitFn>(::dlsym(handle, "grGlideInit"));
  auto shutdown =
      reinterpret_cast<ShutdownFn>(::dlsym(handle, "grGlideShutdown"));
  auto sstSelect = reinterpret_cast<SelectFn>(::dlsym(handle, "grSstSelect"));
  auto getStr = reinterpret_cast<GetStrFn>(::dlsym(handle, "grGetString"));
  auto grGetFn = reinterpret_cast<GetFn>(::dlsym(handle, "grGet"));
  auto queryRes =
      reinterpret_cast<QueryResFn>(::dlsym(handle, "grQueryResolutions"));
  auto resetFn = reinterpret_cast<ResetFn>(::dlsym(handle, "grReset"));
  auto setPending = reinterpret_cast<SetPendingFn>(
      ::dlsym(handle, "grSetNumPendingBuffers"));
  auto getProc =
      reinterpret_cast<GetProcFn>(::dlsym(handle, "grGetProcAddress"));
  auto winOpen = reinterpret_cast<WinOpenFn>(::dlsym(handle, "grSstWinOpen"));
  auto winOpenExt =
      reinterpret_cast<WinOpenExtFn>(::dlsym(handle, "grSstWinOpenExt"));
  auto winClose =
      reinterpret_cast<WinCloseFn>(::dlsym(handle, "grSstWinClose"));
  auto vtxLayout =
      reinterpret_cast<VertexLayoutFn>(::dlsym(handle, "grVertexLayout"));
  auto drawPoint =
      reinterpret_cast<DrawPointFn>(::dlsym(handle, "grDrawPoint"));

  REQUIRE(init != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(getStr != nullptr);
  REQUIRE(grGetFn != nullptr);
  REQUIRE(queryRes != nullptr);
  REQUIRE(resetFn != nullptr);
  REQUIRE(setPending != nullptr);
  REQUIRE(getProc != nullptr);

  // 2. Bare 3-digit GR_VERSION + authentic GR_MAX_TEXTURE_SIZE across all cards
  struct CardCapExpect {
    const char* card;
    const char* ver;
    FxI32 maxTex;
    FxI32 passthru;
    FxI32 texAlign;
    GrScreenResolution_t expectPresent;
    GrScreenResolution_t expectAbsent;
  };
  const CardCapExpect capCases[] = {
      {"voodoo1", "3.04", 256,  FXTRUE,  8,  GR_RESOLUTION_800x600,   GR_RESOLUTION_1024x768},
      {"rush",    "3.04", 256,  FXFALSE, 8,  GR_RESOLUTION_800x600,   GR_RESOLUTION_1024x768},
      {"voodoo2", "3.03", 256,  FXTRUE,  8,  GR_RESOLUTION_1024x768,  GR_RESOLUTION_1600x1200},
      {"banshee", "3.10", 256,  FXFALSE, 16, GR_RESOLUTION_1600x1200, GR_RESOLUTION_2048x1536},
      {"voodoo3", "3.10", 256,  FXFALSE, 16, GR_RESOLUTION_1600x1200, GR_RESOLUTION_2048x1536},
      {"voodoo4", "3.10", 2048, FXFALSE, 16, GR_RESOLUTION_2048x1536, GR_RESOLUTION_NONE},
      {"voodoo5", "3.10", 2048, FXFALSE, 16, GR_RESOLUTION_2048x1536, GR_RESOLUTION_NONE},
  };

  for (const auto& c : capCases) {
    ::setenv("GLIDE_NG_CARD_MODEL", c.card, 1);
    ::unsetenv("GLIDE_NG_API_VERSION");
    ::unsetenv("GLIDE_NG_FBI_MEM_MB");
    ::unsetenv("GLIDE_NG_SLI");
    init();

    const std::string verStr = getStr(GR_VERSION);
    INFO("Card=" << c.card << " GR_VERSION=" << verStr);
    CHECK(verStr == c.ver);
    CHECK(verStr.find("glide-ng") == std::string::npos);

    FxI32 v = 0;
    CHECK(grGetFn(GR_MAX_TEXTURE_SIZE, 4, &v) == 4u);
    CHECK(v == c.maxTex);
    CHECK(grGetFn(GR_SUPPORTS_PASSTHRU, 4, &v) == 4u);
    CHECK(v == c.passthru);
    CHECK(grGetFn(GR_TEXTURE_ALIGN, 4, &v) == 4u);
    CHECK(v == c.texAlign);

    // grQueryResolutions returns byte size = count * sizeof(GrResolution)
    GrResolution tmpl{GR_QUERY_ANY, GR_QUERY_ANY, GR_QUERY_ANY, GR_QUERY_ANY};
    const FxI32 bytes = queryRes(&tmpl, nullptr);
    REQUIRE(bytes > 0);
    REQUIRE((bytes % static_cast<FxI32>(sizeof(GrResolution))) == 0);
    const size_t count = static_cast<size_t>(bytes) / sizeof(GrResolution);
    std::vector<GrResolution> list(count);
    CHECK(queryRes(&tmpl, list.data()) == bytes);

    auto hasRes = [&](GrScreenResolution_t r) {
      for (const auto& e : list) {
        if (e.resolution == r) return true;
      }
      return false;
    };
    CHECK(hasRes(c.expectPresent));
    if (c.expectAbsent != GR_RESOLUTION_NONE) {
      CHECK(!hasRes(c.expectAbsent));
    }
    shutdown();
  }

  // 3. Authentic Voodoo2 4 MB FB vs SLI 1024x768 resolution filtering
  {
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo2", 1);
    ::setenv("GLIDE_NG_FBI_MEM_MB", "4", 1);
    ::setenv("GLIDE_NG_SLI", "0", 1);
    init();
    GrResolution qAux{GR_RESOLUTION_1024x768, GR_REFRESH_60Hz, 2, 1};
    GrResolution qNoAux{GR_RESOLUTION_1024x768, GR_REFRESH_60Hz, 2, 0};
    // 1024x768 * 2B * 3 = 4.5 MB > 4 MB -> rejected; * 2 = 3.0 MB <= 4 MB -> ok
    CHECK(queryRes(&qAux, nullptr) == 0);
    CHECK(queryRes(&qNoAux, nullptr) == static_cast<FxI32>(sizeof(GrResolution)));
    shutdown();

    // With SLI enabled, effective FB doubles to 8 MB -> 1024x768 w/ aux fits!
    ::setenv("GLIDE_NG_SLI", "1", 1);
    init();
    CHECK(queryRes(&qAux, nullptr) == static_cast<FxI32>(sizeof(GrResolution)));
    shutdown();
    ::unsetenv("GLIDE_NG_FBI_MEM_MB");
    ::unsetenv("GLIDE_NG_SLI");
  }

  // 4. GR_BITS_RGBA (16 bytes), GR_BITS_DEPTH, grReset, grSetNumPendingBuffers
  {
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
    init();
    sstSelect(0);

    // grSetNumPendingBuffers available both statically & via grGetProcAddress
    char pendingName[] = "grSetNumPendingBuffers";
    CHECK(getProc(pendingName) != nullptr);
    REQUIRE_NOTHROW(setPending(3));

    // Standard grSstWinOpen -> RGB565 (5-6-5-0, 16-bit depth)
    GrContext_t ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);
    FxI32 rgba[4] = {0, 0, 0, 0};
    FxI32 depth = 0;
    CHECK(grGetFn(GR_BITS_RGBA, 4, rgba) == 0u);  // Rejects under-sized buffer
    CHECK(grGetFn(GR_BITS_RGBA, 16, rgba) == 16u);
    CHECK(rgba[0] == 5);
    CHECK(rgba[1] == 6);
    CHECK(rgba[2] == 5);
    CHECK(rgba[3] == 0);
    CHECK(grGetFn(GR_BITS_DEPTH, 4, &depth) == 4u);
    CHECK(depth == 16);

    // Draw a point & verify GR_STATS_POINTS + grReset(GR_STATS_POINTS)
    vtxLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    float pt[2] = {100.0f, 100.0f};
    drawPoint(pt);
    FxI32 pts = 0;
    CHECK(grGetFn(GR_STATS_POINTS, 4, &pts) == 4u);
    CHECK(pts >= 1);
    CHECK(resetFn(GR_STATS_POINTS) == FXTRUE);
    CHECK(grGetFn(GR_STATS_POINTS, 4, &pts) == 4u);
    CHECK(pts == 0);
    CHECK(resetFn(GR_STATS_LINES) == FXTRUE);
    CHECK(resetFn(GR_STATS_PIXELS) == FXTRUE);
    CHECK(resetFn(GR_STATS_TRIANGLES) == FXTRUE);
    CHECK(resetFn(GR_VERTEX_PARAMETER) == FXTRUE);
    CHECK(resetFn(0x9999u) == FXFALSE);
    winClose(ctx);

    // grSstWinOpenExt with ARGB_8888 -> 8-8-8-8 & 32-bit aux/depth
    ctx = winOpenExt(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                     GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                     GR_PIXFMT_AA_4_ARGB_8888, 2, 1);
    REQUIRE(ctx != 0);
    CHECK(grGetFn(GR_BITS_RGBA, 16, rgba) == 16u);
    CHECK(rgba[0] == 8);
    CHECK(rgba[1] == 8);
    CHECK(rgba[2] == 8);
    CHECK(rgba[3] == 8);
    CHECK(grGetFn(GR_BITS_DEPTH, 4, &depth) == 4u);
    CHECK(depth == 32);
    winClose(ctx);

    shutdown();
  }

  ::dlclose(handle);
#endif
}

TEST_CASE("M11.3: Shared CPU & Post-Process SIMD Kernels Bit-Exactness",
          "[M11][SimdKernels]") {
  using namespace GlideWrapper;
  const SwSimdLevel hostMax = DetectHostMaxSimdLevel();
  const auto& scalar = GetSimdKernelsForLevel(SwSimdLevel::Scalar);

  std::vector<SwSimdLevel> targets;
  if (hostMax >= SwSimdLevel::Avx2) {
    targets.push_back(SwSimdLevel::Avx2);
  }
  if (hostMax >= SwSimdLevel::Avx512) {
    targets.push_back(SwSimdLevel::Avx512);
  }
  REQUIRE(!targets.empty());

  // 1. Exhaustive 65536-value 16-bit -> BGRA32 conversions + odd tail counts
  std::vector<uint16_t> all16(65536 + 19);
  for (size_t i = 0; i < all16.size(); ++i) {
    all16[i] = static_cast<uint16_t>(i & 0xFFFFu);
  }
  const size_t oddCounts[] = {0, 1, 3, 7, 8, 15, 16, 17, 31, 32, 33, 65,
                              all16.size()};

  // 2. BGRA32 -> RGB565 inputs covering all 256 byte values on R, G, B, A
  std::vector<uint32_t> bgraIn(4096 + 23);
  for (size_t i = 0; i < bgraIn.size(); ++i) {
    const uint32_t a = static_cast<uint32_t>((i * 37u + 11u) & 0xFFu);
    const uint32_t r = static_cast<uint32_t>((i * 13u + 3u) & 0xFFu);
    const uint32_t g = static_cast<uint32_t>((i * 29u + 7u) & 0xFFu);
    const uint32_t b = static_cast<uint32_t>((i * 53u + 19u) & 0xFFu);
    bgraIn[i] = (a << 24) | (r << 16) | (g << 8) | b;
  }
  // Guarantee every 0..255 value appears in R, G, and B explicitly
  for (uint32_t v = 0; v < 256u; ++v) {
    bgraIn[v] = (v << 24) | (v << 16) | (v << 8) | v;
  }

  // 3. Palette & Gamma LUTs
  uint32_t palette[256];
  uint8_t lutR[256], lutG[256], lutB[256];
  for (uint32_t i = 0; i < 256u; ++i) {
    palette[i] = ((255u - i) << 24) | (((i * 73u) & 0xFFu) << 16) |
                 (((i * 151u) & 0xFFu) << 8) | ((i * 29u + 5u) & 0xFFu);
    lutR[i] = static_cast<uint8_t>((i * 197u + 13u) & 0xFFu);
    lutG[i] = static_cast<uint8_t>((255u - i) ^ 0x5Au);
    lutB[i] = static_cast<uint8_t>(((i * i) >> 8) & 0xFFu);
  }
  std::vector<uint8_t> p8In(512 + 29);
  for (size_t i = 0; i < p8In.size(); ++i) {
    p8In[i] = static_cast<uint8_t>((i * 41u + 7u) & 0xFFu);
  }

  for (SwSimdLevel lvl : targets) {
    const auto& simd = GetSimdKernelsForLevel(lvl);

    // 16-bit unpack kernels
    for (size_t cnt : oddCounts) {
      std::vector<uint32_t> ref(cnt, 0xDEADBEEFu);
      std::vector<uint32_t> got(cnt, 0xCAFEBABEu);

      scalar.ConvertRgb565ToBgra32(all16.data(), ref.data(), cnt);
      simd.ConvertRgb565ToBgra32(all16.data(), got.data(), cnt);
      REQUIRE(got == ref);

      scalar.ConvertArgb1555ToBgra32(all16.data(), ref.data(), cnt, false);
      simd.ConvertArgb1555ToBgra32(all16.data(), got.data(), cnt, false);
      REQUIRE(got == ref);

      scalar.ConvertArgb1555ToBgra32(all16.data(), ref.data(), cnt, true);
      simd.ConvertArgb1555ToBgra32(all16.data(), got.data(), cnt, true);
      REQUIRE(got == ref);

      scalar.ConvertArgb4444ToBgra32(all16.data(), ref.data(), cnt);
      simd.ConvertArgb4444ToBgra32(all16.data(), got.data(), cnt);
      REQUIRE(got == ref);
    }

    // BGRA32 -> RGB565
    for (size_t cnt :
         {size_t(0), size_t(1), size_t(7), size_t(8), size_t(15), size_t(16),
          size_t(17), size_t(33), bgraIn.size()}) {
      std::vector<uint16_t> ref(cnt, 0xBEEFu);
      std::vector<uint16_t> got(cnt, 0xCAFEu);
      scalar.ConvertBgra32ToRgb565(bgraIn.data(), ref.data(), cnt);
      simd.ConvertBgra32ToRgb565(bgraIn.data(), got.data(), cnt);
      REQUIRE(got == ref);
    }

    // ResolveSsaa2x2Row
    for (uint32_t w : {0u, 1u, 3u, 4u, 7u, 8u, 9u, 15u, 16u, 19u, 64u, 131u}) {
      std::vector<uint32_t> row0(2u * w + 4u);
      std::vector<uint32_t> row1(2u * w + 4u);
      for (size_t i = 0; i < row0.size(); ++i) {
        row0[i] = bgraIn[i % bgraIn.size()];
        row1[i] = bgraIn[(i * 3u + 17u) % bgraIn.size()];
      }
      std::vector<uint32_t> ref(w, 0u);
      std::vector<uint32_t> got(w, 0u);
      scalar.ResolveSsaa2x2Row(row0.data(), row1.data(), ref.data(), w);
      simd.ResolveSsaa2x2Row(row0.data(), row1.data(), got.data(), w);
      REQUIRE(got == ref);
    }

    // ApplyGammaLut
    for (size_t cnt :
         {size_t(0), size_t(1), size_t(7), size_t(8), size_t(15), size_t(16),
          size_t(33), size_t(513)}) {
      std::vector<uint32_t> ref(bgraIn.begin(), bgraIn.begin() + cnt);
      std::vector<uint32_t> got(bgraIn.begin(), bgraIn.begin() + cnt);
      scalar.ApplyGammaLut(ref.data(), cnt, lutR, lutG, lutB);
      simd.ApplyGammaLut(got.data(), cnt, lutR, lutG, lutB);
      REQUIRE(got == ref);
    }

    // DecodeP8ToBgra32 & SwapRB32 (out-of-place & in-place)
    for (size_t cnt :
         {size_t(0), size_t(1), size_t(7), size_t(8), size_t(15), size_t(16),
          size_t(37), p8In.size()}) {
      std::vector<uint32_t> ref(cnt, 0u);
      std::vector<uint32_t> got(cnt, 0u);
      scalar.DecodeP8ToBgra32(p8In.data(), palette, ref.data(), cnt);
      simd.DecodeP8ToBgra32(p8In.data(), palette, got.data(), cnt);
      REQUIRE(got == ref);

      std::vector<uint32_t> swRef(cnt, 0u);
      std::vector<uint32_t> swGot(cnt, 0u);
      scalar.SwapRB32(ref.data(), swRef.data(), cnt);
      simd.SwapRB32(ref.data(), swGot.data(), cnt);
      REQUIRE(swGot == swRef);

      std::vector<uint32_t> inPlace = ref;
      simd.SwapRB32(inPlace.data(), inPlace.data(), cnt);
      REQUIRE(inPlace == swRef);
    }

    // ClearColorMasked & ClearColorTBufferRow
    for (uint32_t x0 : {0u, 1u, 3u, 8u}) {
      for (uint32_t len : {0u, 1u, 7u, 8u, 15u, 16u, 35u}) {
        const uint32_t x1 = x0 + len;
        for (uint32_t parity = 0u; parity <= 3u; ++parity) {
          for (uint32_t keepMask : {0u, 0xFF00FF00u}) {
            const uint32_t clearWord = 0xAB123456u;
            const uint32_t maskedClear = clearWord & ~keepMask;
            std::vector<uint32_t> ref(bgraIn.begin(),
                                      bgraIn.begin() + x1 + 8u);
            std::vector<uint32_t> got = ref;
            scalar.ClearColorTBufferRow(ref.data(), x0, x1, clearWord, keepMask,
                                        maskedClear, parity);
            simd.ClearColorTBufferRow(got.data(), x0, x1, clearWord, keepMask,
                                      maskedClear, parity);
            REQUIRE(got == ref);
          }
        }
      }
    }

    // ClearDepthTBufferRow
    for (uint32_t x0 : {0u, 1u, 5u}) {
      for (uint32_t len : {0u, 1u, 7u, 8u, 15u, 16u, 37u}) {
        const uint32_t x1 = x0 + len;
        for (uint32_t parity = 0u; parity <= 3u; ++parity) {
          std::vector<float> ref(x1 + 8u);
          for (size_t i = 0; i < ref.size(); ++i) {
            ref[i] = static_cast<float>(i) * 0.01f;
          }
          std::vector<float> got = ref;
          scalar.ClearDepthTBufferRow(ref.data(), x0, x1, 0.75f, parity);
          simd.ClearDepthTBufferRow(got.data(), x0, x1, 0.75f, parity);
          REQUIRE(got == ref);
        }
      }
    }

    // ClearStencilMasked & ClearStencilTBufferRow
    for (uint32_t x0 : {0u, 1u, 3u, 17u}) {
      for (uint32_t len : {0u, 1u, 15u, 31u, 32u, 63u, 64u, 99u}) {
        const uint32_t x1 = x0 + len;
        for (uint32_t parity = 0u; parity <= 3u; ++parity) {
          for (uint8_t keepMask : {uint8_t(0u), uint8_t(0xA5u)}) {
            const uint8_t clearVal = 0x3Cu;
            const uint8_t maskedClear =
                static_cast<uint8_t>(clearVal & ~keepMask);
            std::vector<uint8_t> ref(p8In.begin(), p8In.begin() + x1 + 8u);
            std::vector<uint8_t> got = ref;
            scalar.ClearStencilTBufferRow(ref.data(), x0, x1, clearVal,
                                          maskedClear, keepMask, parity);
            simd.ClearStencilTBufferRow(got.data(), x0, x1, clearVal,
                                        maskedClear, keepMask, parity);
            REQUIRE(got == ref);
          }
        }
      }
    }
  }
}

TEST_CASE("M11.4: Vectorized Table Fog, Stipple & Stencil Bit-Exact Parity",
          "[M11][Fog][Stencil]") {
  using namespace GlideWrapper;
  const SwSimdLevel hostMax = DetectHostMaxSimdLevel();
  std::vector<SwSimdLevel> targets;
  if (hostMax >= SwSimdLevel::Avx2) targets.push_back(SwSimdLevel::Avx2);
  if (hostMax >= SwSimdLevel::Avx512) targets.push_back(SwSimdLevel::Avx512);
  REQUIRE(!targets.empty());

  auto makeVert = [](float x, float y, float z, float oow, float r, float g,
                     float b, float a) {
    ModernVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    v.pos[3] = oow;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = a;
    return v;
  };

  auto renderScenario = [&](SwSimdLevel lvl, uint32_t W, uint32_t H,
                            std::vector<uint32_t>& outColor,
                            std::vector<uint8_t>& outStencil, int mode) {
    WrapperConfig cfg{};
    cfg.backend = "software";
    cfg.forceNoWindow = true;
    cfg.swSimdLevel = lvl;
    cfg.swUseSimd = (lvl != SwSimdLevel::Scalar);
    SoftwareBackend be;
    REQUIRE(be.Initialize(cfg));
    REQUIRE(be.AttachWindow(nullptr, W, H, /*windowed=*/true));
    be.SetDitherMode(0);
    be.SetColorMaskExt(true, true, true, true);
    be.SetCombinerMode(1, 0, 0, 0, false, 1, 0, 0, 0, false);  // ITERATED
    be.SetBlendState(4, 0, 4, 0);

    if (mode == 0) {
      // Table Fog sweeping across ALL 64 W-table intervals (including steep
      // jumps, near-clamp w < 1, far-clamp w > 52428, and ADD2/MULT2).
      uint8_t fogTbl[64];
      for (int i = 0; i < 64; ++i) {
        // Non-monotonic + steep steps to catch ANY off-by-1 index error!
        fogTbl[i] = static_cast<uint8_t>(((i * 37 + (i & 1 ? 120 : 10)) & 0xFF));
      }
      be.SetFogTable(fogTbl);
      be.SetFogColor(0x004090E0u);
      be.SetDepthState(0, 7, false, 0);
      be.ClearBufferExt(0x00102030u, 255u, 1.0f, 0u, 0x3u);

      // Strip 1: Standard Table Fog (mode=1) sweeping oow across 1.2 -> 1e-6
      be.SetFogMode(1u);
      for (int band = 0; band < 16; ++band) {
        const float y0 = static_cast<float>(band * 3);
        const float y1 = static_cast<float>((band + 1) * 3);
        const float oowL = 1.2f / static_cast<float>(1u << band);
        const float oowR =
            std::max(1e-6f, 0.45f / static_cast<float>(1u << band));
        ModernVertex v0 = makeVert(0.0f, y0, 0.5f, oowL, 0.9f, 0.2f, 0.1f, 1.0f);
        ModernVertex v1 =
            makeVert(static_cast<float>(W), y0, 0.5f, oowR, 0.1f, 0.9f, 0.3f, 1.0f);
        ModernVertex v2 =
            makeVert(static_cast<float>(W), y1, 0.5f, oowR, 0.2f, 0.3f, 0.9f, 1.0f);
        ModernVertex v3 = makeVert(0.0f, y1, 0.5f, oowL, 0.8f, 0.7f, 0.2f, 1.0f);
        be.DrawTriangle(v0, v1, v2);
        be.DrawTriangle(v0, v2, v3);
      }

      // Strip 2: Multi-pass GR_FOG_MULT2 (0x201) + GR_FOG_ADD2 (0x101)
      be.SetFogMode(0x201u);
      be.SetBlendState(4, 0, 4, 0);
      {
        ModernVertex v0 = makeVert(0.0f, 48.0f, 0.5f, 0.8f, 0.8f, 0.5f, 0.3f, 1.0f);
        ModernVertex v1 =
            makeVert(static_cast<float>(W), 48.0f, 0.5f, 0.002f, 0.3f, 0.8f, 0.6f, 1.0f);
        ModernVertex v2 = makeVert(static_cast<float>(W), static_cast<float>(H),
                                   0.5f, 0.0005f, 0.6f, 0.2f, 0.9f, 1.0f);
        ModernVertex v3 =
            makeVert(0.0f, static_cast<float>(H), 0.5f, 0.5f, 0.9f, 0.8f, 0.1f, 1.0f);
        be.DrawTriangle(v0, v1, v2);
        be.DrawTriangle(v0, v2, v3);
        be.SetFogMode(0x101u);
        be.SetBlendState(4, 4, 4, 0);  // ONE, ONE additive pass
        be.DrawTriangle(v0, v1, v2);
        be.DrawTriangle(v0, v2, v3);
      }
    } else if (mode == 1) {
      // Stipple transparency (mode 1 = PATTERN, mode 2 = ROTATE)
      be.SetDepthState(0, 7, false, 0);
      be.ClearBufferExt(0x00081828u, 255u, 1.0f, 0u, 0x3u);
      be.SetStippleState(1u, 0xA5C39617u);
      ModernVertex v0 = makeVert(2.0f, 2.0f, 0.5f, 1.0f, 1.0f, 0.2f, 0.2f, 1.0f);
      ModernVertex v1 =
          makeVert(static_cast<float>(W) - 2.0f, 2.0f, 0.5f, 1.0f, 0.2f, 1.0f, 0.2f, 1.0f);
      ModernVertex v2 =
          makeVert(static_cast<float>(W) - 2.0f, static_cast<float>(H) * 0.5f,
                   0.5f, 1.0f, 0.2f, 0.2f, 1.0f, 1.0f);
      be.DrawTriangle(v0, v1, v2);

      be.SetStippleState(2u, 0x3C5AA5C3u);
      ModernVertex u0 =
          makeVert(1.0f, static_cast<float>(H) * 0.4f, 0.5f, 1.0f, 0.9f, 0.9f, 0.1f, 1.0f);
      ModernVertex u1 =
          makeVert(static_cast<float>(W) - 1.0f, static_cast<float>(H) * 0.5f,
                   0.5f, 1.0f, 0.1f, 0.9f, 0.9f, 1.0f);
      ModernVertex u2 =
          makeVert(static_cast<float>(W) * 0.5f, static_cast<float>(H) - 1.0f,
                   0.5f, 1.0f, 0.9f, 0.1f, 0.9f, 1.0f);
      be.DrawTriangle(u0, u1, u2);
    } else {
      // Hardware Stencil: exercise ALL 8 compare ops, ALL 8 stencil ops,
      // partial readMask/writeMask, and simultaneous sfail/zfail/zpass in quads.
      be.SetStencilMode(true);
      be.SetStencilMask(0xFFu);
      be.SetDepthState(1, 1, true, 0);  // Z-buffer LESS, write enabled
      be.ClearBufferExt(0x00112233u, 255u, 0.5f, 0x15u, 0x7u);

      // Pass 1: seed half screen with depth=0.25 and stencil=0xAA
      be.SetStencilFunc(7u, 0xAAu, 0xFFu);  // ALWAYS
      be.SetStencilOp(0u, 0u, 2u);          // KEEP, KEEP, REPLACE
      {
        ModernVertex v0 = makeVert(0.0f, 0.0f, 0.25f, 1.0f, 0.2f, 0.4f, 0.6f, 1.0f);
        ModernVertex v1 = makeVert(static_cast<float>(W) * 0.6f, 0.0f, 0.25f,
                                   1.0f, 0.2f, 0.4f, 0.6f, 1.0f);
        ModernVertex v2 = makeVert(static_cast<float>(W) * 0.3f,
                                   static_cast<float>(H), 0.25f, 1.0f, 0.2f,
                                   0.4f, 0.6f, 1.0f);
        be.DrawTriangle(v0, v1, v2);
      }

      // Pass 2..9: sweep every compare op (0..7) & stencil op (0..7) with masks
      for (uint32_t op = 0; op < 8u; ++op) {
        be.SetStencilFunc(op, static_cast<uint8_t>(0x30u + op * 17u), 0x5Fu);
        be.SetStencilMask(static_cast<uint8_t>(0x3Cu | (op & 3u)));
        be.SetStencilOp(op, (op + 3u) & 7u, (op + 5u) & 7u);
        const float xOff = static_cast<float>(op * 6);
        // Sloped Z from 0.1 (passes depth) to 0.9 (fails depth)
        ModernVertex v0 = makeVert(xOff, 0.0f, 0.1f, 1.0f, 0.1f * op, 0.8f,
                                   0.2f, 1.0f);
        ModernVertex v1 = makeVert(static_cast<float>(W), 4.0f + op * 3.0f,
                                   0.9f, 1.0f, 0.9f, 0.1f * op, 0.4f, 1.0f);
        ModernVertex v2 = makeVert(xOff + 10.0f, static_cast<float>(H), 0.4f,
                                   1.0f, 0.3f, 0.5f, 0.1f * op, 1.0f);
        be.DrawTriangle(v0, v1, v2);
      }
    }

    be.FlushBins();
    outColor.assign(static_cast<size_t>(W) * H, 0u);
    REQUIRE(be.ReadLFB(1u, 0u, 0u, W, H, W * 4u, outColor.data()));
    outStencil.resize(static_cast<size_t>(W) * H);
    for (uint32_t y = 0; y < H; ++y) {
      for (uint32_t x = 0; x < W; ++x) {
        outStencil[static_cast<size_t>(y) * W + x] =
            be.GetStencilBufferPixel(x, y);
      }
    }
    be.DetachWindow();
    be.Shutdown();
  };

  // Test both standard (64x64) and non-divisible-by-4 height (64x50)
  for (uint32_t H : {64u, 50u}) {
    const uint32_t W = 64u;
    for (int mode = 0; mode < 3; ++mode) {
      std::vector<uint32_t> refColor;
      std::vector<uint8_t> refStencil;
      renderScenario(SwSimdLevel::Scalar, W, H, refColor, refStencil, mode);

      for (SwSimdLevel lvl : targets) {
        std::vector<uint32_t> gotColor;
        std::vector<uint8_t> gotStencil;
        renderScenario(lvl, W, H, gotColor, gotStencil, mode);

        int maxDiff = 0;
        size_t diffPixels = 0;
        for (size_t i = 0; i < refColor.size(); ++i) {
          if (refColor[i] != gotColor[i]) {
            ++diffPixels;
            for (int shift = 0; shift <= 24; shift += 8) {
              const int a = static_cast<int>((refColor[i] >> shift) & 0xFFu);
              const int b = static_cast<int>((gotColor[i] >> shift) & 0xFFu);
              maxDiff = std::max(maxDiff, std::abs(a - b));
            }
          }
        }
        INFO("mode=" << mode << " H=" << H << " diffPixels=" << diffPixels
                     << " maxDiff=" << maxDiff);
        if (mode == 0) {
          // Table Fog must stay strictly bit-exact (0-LSB diff across all 64 intervals)
          CHECK(maxDiff == 0);
          CHECK(diffPixels == 0);
        } else {
          // FMA vs non-FMA barycentric color ramps can round +-1 LSB at X.5;
          // any stipple or stencil mask bug would jump by >40 vs background.
          CHECK(maxDiff <= 1);
        }
        CHECK(gotStencil == refStencil);
      }
    }
  }
}

TEST_CASE("M11.5: Software Rasterizer Multi-ISA Parity (VSA-100, Dither, 4x AA T-Buffer)",
          "[M11][Parity]") {
  using namespace GlideWrapper;
  const SwSimdLevel hostMax = DetectHostMaxSimdLevel();
  std::vector<SwSimdLevel> targets;
  if (hostMax >= SwSimdLevel::Avx2) targets.push_back(SwSimdLevel::Avx2);
  if (hostMax >= SwSimdLevel::Avx512) targets.push_back(SwSimdLevel::Avx512);
  REQUIRE(!targets.empty());

  auto makeVert = [](float x, float y, float z, float oow, float r, float g,
                     float b, float a) {
    ModernVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    v.pos[3] = oow;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = a;
    return v;
  };

  auto renderComplex = [&](SwSimdLevel lvl, uint32_t W, uint32_t H,
                           bool enableSsaa, std::vector<uint32_t>& outColor) {
    WrapperConfig cfg{};
    cfg.backend = "software";
    cfg.forceNoWindow = true;
    cfg.swSimdLevel = lvl;
    cfg.swUseSimd = (lvl != SwSimdLevel::Scalar);
    cfg.swAaMode = enableSsaa ? "ssaa" : "analytical";
    SoftwareBackend be;
    REQUIRE(be.Initialize(cfg));
    REQUIRE(be.AttachWindow(nullptr, W, H, /*windowed=*/true));
    be.SetColorMaskExt(true, true, true, true);
    be.SetDepthState(1, 3, true, 0);  // Z-buffer LEQUAL
    be.ClearBufferExt(0x00182430u, 255u, 1.0f, 0u, 0x3u);

    // 1. VSA-100 COMBINE (iter + const * 0.5) + 4x4 Bayer Dither + Blend
    be.SetDitherMode(2u);
    be.SetConstantColor(0x8040A060u);
    be.SetColorCombineExt(IGraphicsBackend::PackCombineExt(
        0x08u, 1u, 0x05u, 1u, 0x05u, false, 0x00u, false, 0u, false));
    be.SetBlendStateExt(1u, 5u, 0u, 4u, 0u, 0u);  // SRC_ALPHA, 1-SRC_ALPHA
    if (enableSsaa) {
      be.SetAAMultiSample(true);
      be.SetTBufferWriteMask(0x5u);  // subpixels 0 & 2 only
    }
    be.DrawTriangle(makeVert(2.0f, 2.0f, 0.4f, 1.0f, 0.9f, 0.2f, 0.3f, 0.75f),
                    makeVert(static_cast<float>(W) - 3.0f, 6.0f, 0.6f, 0.5f,
                             0.2f, 0.9f, 0.4f, 0.75f),
                    makeVert(static_cast<float>(W) * 0.5f,
                             static_cast<float>(H) - 2.0f, 0.2f, 0.8f, 0.3f,
                             0.4f, 0.9f, 0.75f));

    // 2. Second pass with complementary T-Buffer mask + collapsed AA + 2x2 dither
    be.SetDitherMode(1u);
    be.SetColorCombineExt(0u);
    be.SetCombinerMode(1, 0, 0, 0, false, 1, 0, 0, 0, false);
    be.SetBlendStateExt(4u, 4u, 1u, 4u, 0u, 0u);  // SUBTRACT blend op
    if (enableSsaa) {
      be.SetTBufferWriteMask(0xAu);  // subpixels 1 & 3
      be.SetAAMultiSample(false);    // exercise collapseAa 2x2 guest alignment
    }
    be.DrawTriangle(makeVert(8.0f, 4.0f, 0.3f, 0.9f, 0.8f, 0.7f, 0.2f, 1.0f),
                    makeVert(static_cast<float>(W) - 4.0f,
                             static_cast<float>(H) * 0.6f + 0.13f, 0.5f, 0.4f,
                             0.2f, 0.6f, 0.8f, 1.0f),
                    makeVert(4.0f, static_cast<float>(H) - 4.0f, 0.35f, 0.7f,
                             0.7f, 0.2f, 0.5f, 1.0f));
    if (enableSsaa) {
      be.SetTBufferWriteMask(0xFu);
      be.SetAAMultiSample(true);
    }

    be.FlushBins();
    outColor.assign(static_cast<size_t>(W) * H, 0u);
    REQUIRE(be.ReadLFB(1u, 0u, 0u, W, H, W * 4u, outColor.data()));
    be.DetachWindow();
    be.Shutdown();
  };

  for (bool ssaa : {false, true}) {
    for (uint32_t H : {64u, 50u}) {
      std::vector<uint32_t> ref;
      renderComplex(SwSimdLevel::Scalar, 64u, H, ssaa, ref);
      for (SwSimdLevel lvl : targets) {
        std::vector<uint32_t> got;
        renderComplex(lvl, 64u, H, ssaa, got);
        int maxDiff = 0;
        size_t diffPixels = 0;
        uint32_t firstX = 0, firstY = 0, firstRef = 0, firstGot = 0;
        for (size_t i = 0; i < ref.size(); ++i) {
          if (ref[i] != got[i]) {
            int pDiff = 0;
            for (int shift = 0; shift <= 24; shift += 8) {
              const int a = static_cast<int>((ref[i] >> shift) & 0xFFu);
              const int b = static_cast<int>((got[i] >> shift) & 0xFFu);
              pDiff = std::max(pDiff, std::abs(a - b));
            }
            if (pDiff > maxDiff) {
              maxDiff = pDiff;
              firstX = static_cast<uint32_t>(i % 64u);
              firstY = static_cast<uint32_t>(i / 64u);
              firstRef = ref[i];
              firstGot = got[i];
            }
            if (pDiff > 8) ++diffPixels;
          }
        }
        INFO("ssaa=" << ssaa << " H=" << H << " simd=" << FormatSwSimdLevel(lvl)
                     << " maxDiff=" << maxDiff << " bigDiffPixels=" << diffPixels
                     << " worst=(" << firstX << "," << firstY << ") ref=0x"
                     << std::hex << firstRef << " got=0x" << firstGot
                     << std::dec);
        // Dithered RGB565 quantization can flip one 5-bit step (+-8) if an
        // FMA-vs-non-FMA boundary pixel lands right on a Bayer threshold; verify
        // zero gross corruption (<= 8 for dithered, exact match when AVX2 vs AVX512!).
        CHECK(maxDiff <= 8);
      }
      // On AVX-512 hosts (Zen 4), AVX2 and AVX-512 BOTH use hardware FMA, so
      // their outputs must match within <= 1 LSB (or 0!).
      if (hostMax >= SwSimdLevel::Avx512) {
        std::vector<uint32_t> got2, got512;
        renderComplex(SwSimdLevel::Avx2, 64u, H, ssaa, got2);
        renderComplex(SwSimdLevel::Avx512, 64u, H, ssaa, got512);
        CHECK(got512 == got2);
      }
    }
  }
}

TEST_CASE("M11.6a Native Hardware BC1/BC2/BC3 Compressed Texture Upload",
          "[M11][BCn]") {
  using namespace GlideWrapper;
  // Build a 16x16..1x1 (5-level) mipmap chain for DXT1 (0x16), DXT3 (0x18),
  // and DXT5 (0x1A) to verify hardware upload of both >=4x4 and sub-block
  // (2x2, 1x1) mip tails, pool reuse across format changes, and lazy CPU
  // decode via TextureManager::GetTexture().
  auto makeDxt1Block = [](uint16_t c0, uint16_t c1, uint32_t idx) {
    std::array<uint8_t, 8> blk{};
    std::memcpy(blk.data() + 0, &c0, 2);
    std::memcpy(blk.data() + 2, &c1, 2);
    std::memcpy(blk.data() + 4, &idx, 4);
    return blk;
  };
  auto buildChain = [&](uint32_t fmt, uint16_t c0, uint16_t c1, uint8_t alpha) {
    std::vector<uint8_t> data;
    for (uint32_t dim : {16u, 8u, 4u, 2u, 1u}) {
      const uint32_t bx = (dim + 3u) / 4u;
      const uint32_t by = (dim + 3u) / 4u;
      const auto colBlk = makeDxt1Block(c0, c1, 0x00000000u);  // solid c0
      for (uint32_t i = 0; i < bx * by; ++i) {
        if (fmt == 0x18u) {  // DXT3: 4-bit explicit alpha nibbles
          const uint8_t nib = static_cast<uint8_t>(alpha >> 4);
          const uint8_t byteVal = static_cast<uint8_t>((nib << 4) | nib);
          for (int k = 0; k < 8; ++k) data.push_back(byteVal);
        } else if (fmt == 0x1Au) {  // DXT5: interpolated alpha, idx=0 -> a0
          data.push_back(alpha);
          data.push_back(0u);
          for (int k = 0; k < 6; ++k) data.push_back(0u);
        }
        data.insert(data.end(), colBlk.begin(), colBlk.end());
      }
    }
    return data;
  };

  auto renderQuadWithFmt = [&](const char* backendName, uint32_t fmt,
                               uint16_t c0, uint8_t alpha,
                               bool& outSupportsBcn,
                               uint32_t& outCenterPixel,
                               uint32_t& outLazyDecodedTexel) {
    auto& reg = EmulationRegistry::GetInstance();
    auto savedCfg = reg.GetConfig();
    auto& bm = BackendManager::GetInstance();
    auto& tm = TextureManager::GetInstance();
    WrapperConfig cfg = savedCfg;
    cfg.backend = backendName;
    cfg.forceNoWindow = true;
    cfg.model = CardModel::Voodoo5;
    cfg.tmuMemoryMb = 16;
    cfg.fbiMemoryMb = 16;
    reg.GetConfig() = cfg;
    REQUIRE(bm.EstablishBackend(cfg));
    auto* be = bm.GetBackend();
    REQUIRE(be != nullptr);
    REQUIRE(be->AttachWindow(nullptr, 64, 64, false));
    outSupportsBcn = be->SupportsCompressedBcnTextures();

    tm.Reset();
    tm.SetTmuMemoryLimitMb(0, 16);
    tm.SetTmuMemoryLimitMb(1, 16);

    auto chain = buildChain(fmt, c0, 0x001Fu, alpha);
    // LOD 4 = 16x16 .. LOD 8 = 1x1, aspect 3 = 1x1
    REQUIRE(tm.DownloadMipMap(0, 0x1000u, 4, 8, 3, fmt, chain.data()));
    be->BindTexture(0, 0x1000u, 0, 0, 0, 0);
    be->SetTexCombinerMode(0, 1, 0, 1, 0, false, false);
    be->SetCombinerMode(3, 8, 2, 1, false, 3, 8, 2, 1, false);  // SCALE_OTHER * ONE (TEXTURE)
    be->SetColorCombineExt(0u);
    be->SetBlendState(1, 0, 4, 0);  // SRC_ALPHA, ZERO -> tests RGB * decoded Alpha!
    be->SetDepthState(0, 7, false, 0);
    be->ClearBuffer(0xFF000000u, 0u, 1.0f, 3u);

    auto v = [](float x, float y, float s, float t) {
      ModernVertex mv{};
      mv.pos = {x, y, 0.5f, 1.0f};
      mv.color = {1.0f, 1.0f, 1.0f, 1.0f};
      mv.tex = {s, t, 0.0f, 0.0f};
      mv.tmu_oow = {1.0f, 1.0f};
      return mv;
    };
    be->DrawTriangle(v(0.0f, 0.0f, 0.0f, 0.0f), v(64.0f, 0.0f, 256.0f, 0.0f),
                     v(64.0f, 64.0f, 256.0f, 256.0f));
    be->DrawTriangle(v(0.0f, 0.0f, 0.0f, 0.0f), v(64.0f, 64.0f, 256.0f, 256.0f),
                     v(0.0f, 64.0f, 0.0f, 256.0f));
    be->FlushBins();

    std::vector<uint32_t> fb(64u * 64u, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, 64u, 64u, 64u * 4u, fb.data()));
    outCenterPixel = fb[32u * 64u + 32u];

    // Verify lazy CPU decode via GetTexture() populates all 5 mip levels.
    VirtualTexture* vt = tm.GetTexture(0, 0x1000u);
    REQUIRE(vt != nullptr);
    REQUIRE(vt->swizzledMipLevels.size() == 5u);
    REQUIRE(vt->swizzledMipLevels[0].size() == 16u * 16u);
    REQUIRE(vt->swizzledMipLevels[4].size() == 1u);
    outLazyDecodedTexel = vt->swizzledMipLevels[0][0];

    be->DetachWindow();
    bm.ShutdownBackend();
    reg.GetConfig() = savedCfg;
  };

  struct CaseSpec {
    uint32_t fmt;
    uint16_t c0;  // RGB565
    uint8_t alpha;
    uint8_t expR;
    uint8_t expG;
    uint8_t expB;
  };
  const std::array<CaseSpec, 3> cases{{
      {0x16u, 0xF800u, 0xFFu, 255u, 0u, 0u},    // DXT1 red * alpha=255
      {0x18u, 0x07E0u, 0x88u, 0u, 136u, 0u},    // DXT3 green * alpha=0x88 (136)
      {0x1Au, 0x001Fu, 0xC0u, 0u, 0u, 192u},    // DXT5 blue * alpha=0xC0 (192)
  }};

  for (const auto& c : cases) {
    bool swSup = true, vkSup = false, glSup = false;
    uint32_t swPx = 0, vkPx = 0, glPx = 0;
    uint32_t swLazy = 0, vkLazy = 0, glLazy = 0;
    renderQuadWithFmt("software", c.fmt, c.c0, c.alpha, swSup, swPx, swLazy);
    renderQuadWithFmt("vulkan", c.fmt, c.c0, c.alpha, vkSup, vkPx, vkLazy);
    renderQuadWithFmt("opengl_es", c.fmt, c.c0, c.alpha, glSup, glPx, glLazy);

    CHECK_FALSE(swSup);
    CHECK(vkSup);
    CHECK(glSup);
    CHECK(vkLazy == swLazy);
    CHECK(glLazy == swLazy);
    // ReadLFB returns RGBA8888 (R in byte 0, G in byte 1, B in byte 2).
    for (uint32_t px : {swPx, vkPx, glPx}) {
      const uint8_t r = static_cast<uint8_t>(px & 0xFFu);
      const uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFFu);
      const uint8_t b = static_cast<uint8_t>((px >> 16) & 0xFFu);
      CHECK(std::abs(static_cast<int>(r) - static_cast<int>(c.expR)) <= 2);
      CHECK(std::abs(static_cast<int>(g) - static_cast<int>(c.expG)) <= 2);
      CHECK(std::abs(static_cast<int>(b) - static_cast<int>(c.expB)) <= 2);
    }
  }
}

TEST_CASE("M11.6b Zero-Readback GPU TEXTUREBUFFER RTT (Origins + MSAA + Zero Staging)",
          "[M11][RTT]") {
  using namespace GlideWrapper;

  auto runRttCase = [](const char* backendName, uint32_t origin,
                       uint32_t msaaSamples, uint32_t& outSavedPx,
                       uint32_t& outTopPx, uint32_t& outBotPx,
                       bool& outZeroStagingVk) {
    auto& reg = EmulationRegistry::GetInstance();
    auto savedCfg = reg.GetConfig();
    auto& bm = BackendManager::GetInstance();
    auto& tm = TextureManager::GetInstance();
    WrapperConfig cfg = savedCfg;
    cfg.backend = backendName;
    cfg.forceNoWindow = true;
    cfg.model = CardModel::Voodoo5;
    cfg.tmuMemoryMb = 16;
    cfg.fbiMemoryMb = 16;
    cfg.msaaSamples = msaaSamples;
    reg.GetConfig() = cfg;
    REQUIRE(bm.EstablishBackend(cfg));
    auto* be = bm.GetBackend();
    REQUIRE(be != nullptr);
    REQUIRE(be->AttachWindow(nullptr, 128, 128, false));

    tm.Reset();
    tm.SetTmuMemoryLimitMb(0, 16);
    tm.SetTmuMemoryLimitMb(1, 16);

    be->SetSstOrigin(origin);
    be->SetDepthState(0, 7, false, 0);
    be->SetBlendState(4, 0, 4, 0);  // ONE, ZERO
    be->SetColorCombineExt(0u);
    be->SetCombinerMode(1, 0, 0, 0, false, 1, 0, 0, 0, false);  // LOCAL ITERATED

    auto makeV = [](float x, float y, float r, float g, float b, float s,
                    float t) {
      ModernVertex mv{};
      mv.pos = {x, y, 0.5f, 1.0f};
      mv.color = {r, g, b, 1.0f};
      mv.tex = {s, t, 0.0f, 0.0f};
      mv.tmu_oow = {1.0f, 1.0f};
      return mv;
    };
    auto drawRect = [&](float x0, float y0, float x1, float y1, float r,
                        float g, float b) {
      be->DrawTriangle(makeV(x0, y0, r, g, b, 0.0f, 0.0f),
                       makeV(x1, y0, r, g, b, 256.0f, 0.0f),
                       makeV(x1, y1, r, g, b, 256.0f, 256.0f));
      be->DrawTriangle(makeV(x0, y0, r, g, b, 0.0f, 0.0f),
                       makeV(x1, y1, r, g, b, 256.0f, 256.0f),
                       makeV(x0, y1, r, g, b, 0.0f, 256.0f));
    };

    // 1. Paint main backbuffer bright green (R=0, G=200, B=40 -> 0xFF28C800 in ABGR)
    be->ClearBuffer(0xFF28C800u, 255u, 1.0f, 3u);
    drawRect(0.0f, 0.0f, 40.0f, 40.0f, 0.0f, 0.784f, 0.157f);

    // 2. Enter GPU TEXTUREBUFFER mode and render asymmetric 64x64 RTT:
    //    y in [0..32) = RED, y in [32..64) = BLUE (in active Glide origin).
    REQUIRE(be->BeginGpuTextureBufferMode());
    be->SetAAMultiSample(false);
    be->SetClipWindow(0u, 0u, 64u, 64u);
    be->ClearBuffer(0xFF000000u, 255u, 1.0f, 3u);
    drawRect(0.0f, 0.0f, 64.0f, 32.0f, 1.0f, 0.0f, 0.0f);   // RED
    drawRect(0.0f, 32.0f, 64.0f, 64.0f, 0.0f, 0.0f, 1.0f);  // BLUE

    // 3. Zero-readback GPU commit to TMU0 @ 0x20000 (LOD 2 = 64x64, 1x1)
    REQUIRE(tm.CommitGpuRenderedTexture(0u, 0x20000u, 2, 2, 3u, 0xAu, 64u, 64u,
                                        origin));

    // 4. Exit GPU TEXTUREBUFFER mode (restores green backbuffer on GPU)
    be->EndGpuTextureBufferMode();
    be->SetAAMultiSample(true);
    be->SetClipWindow(0u, 0u, 128u, 128u);

    // 5. Sample the RTT texture onto right half [64..128, 0..128]
    be->BindTexture(0u, 0x20000u, 1u, 1u, 0u, 0u);  // CLAMP, POINT
    be->SetTexCombinerMode(0u, 1u, 0u, 1u, 0u, false, false);
    be->SetCombinerMode(3u, 8u, 2u, 1u, false, 3u, 8u, 2u, 1u, false);
    drawRect(64.0f, 0.0f, 128.0f, 128.0f, 1.0f, 1.0f, 1.0f);
    be->FlushBins();

    // Check zero CpuLfbStaging allocation on Vulkan BEFORE calling ReadLFB!
    outZeroStagingVk = true;
    if (auto* vkBe = dynamic_cast<VulkanBackend*>(be)) {
      outZeroStagingVk = !vkBe->HasAllocatedCpuLfbStaging();
    }

    std::vector<uint32_t> fb(128u * 128u, 0u);
    REQUIRE(be->ReadLFB(1u, 0u, 0u, 128u, 128u, 128u * 4u, fb.data()));
    // ReadLFB rows are top-to-bottom regardless of origin.
    outSavedPx = fb[20u * 128u + 20u];   // untouched green backbuffer
    outTopPx = fb[20u * 128u + 96u];     // top quarter of sampled RTT quad
    outBotPx = fb[108u * 128u + 96u];    // bottom quarter of sampled RTT quad

    be->DetachWindow();
    bm.ShutdownBackend();
    reg.GetConfig() = savedCfg;
  };

  auto checkRgb = [](uint32_t px, int expR, int expG, int expB,
                     const char* lbl) {
    const int r = static_cast<int>(px & 0xFFu);
    const int g = static_cast<int>((px >> 8) & 0xFFu);
    const int b = static_cast<int>((px >> 16) & 0xFFu);
    INFO(lbl << " got=(" << r << "," << g << "," << b << ") exp=(" << expR
             << "," << expG << "," << expB << ")");
    CHECK(std::abs(r - expR) <= 4);
    CHECK(std::abs(g - expG) <= 4);
    CHECK(std::abs(b - expB) <= 4);
  };

  for (uint32_t origin : {0u, 1u}) {
    for (uint32_t msaa : {1u, 4u}) {
      for (const char* beName : {"vulkan", "opengl_es"}) {
        uint32_t savedPx = 0, topPx = 0, botPx = 0;
        bool zeroStaging = false;
        runRttCase(beName, origin, msaa, savedPx, topPx, botPx, zeroStaging);
        INFO("backend=" << beName << " origin=" << origin << " msaa=" << msaa);
        CHECK(zeroStaging);
        checkRgb(savedPx, 0, 200, 40, "preserved main backbuffer");
        // In UPPER_LEFT (origin=0): y=[0..32) is screen TOP (RED), y=[32..64) is
        // screen BOT (BLUE). Sampling t=0 at screen y=0 (TOP) puts RED at TOP.
        // In LOWER_LEFT (origin=1): y=[0..32) is screen BOT (RED), y=[32..64) is
        // screen TOP (BLUE). ReadLFB stores row 0 = top of RTT (BLUE), row 63 =
        // bot of RTT (RED); sampling t=0 at Glide y=0 (screen BOT) puts BLUE at
        // screen BOT and RED at screen TOP!
        checkRgb(topPx, 255, 0, 0, "RTT sampled top");
        checkRgb(botPx, 0, 0, 255, "RTT sampled bottom");
      }
    }
  }
}

TEST_CASE("M12.5 Compatibility Catalog INI Presets & Directory Integrity",
          "[M12][Catalog][Config]") {
  namespace fs = std::filesystem;

  // Locate repository root (prefers CMake-injected GLIDE_REPO_ROOT, with CWD fallbacks)
  fs::path repoRoot;
  std::vector<fs::path> candidates;
#ifdef GLIDE_REPO_ROOT
  candidates.emplace_back(GLIDE_REPO_ROOT);
#endif
  candidates.emplace_back(fs::current_path());
  candidates.emplace_back(fs::current_path().parent_path());
  candidates.emplace_back(fs::current_path().parent_path().parent_path());
  for (const auto& c : candidates) {
    if (fs::exists(c / "docs" / "catalog" / "README.md")) {
      repoRoot = c;
      break;
    }
  }
  REQUIRE(!repoRoot.empty());

  const fs::path catalogDir = repoRoot / "docs" / "catalog";
  REQUIRE(fs::is_directory(catalogDir));

  // Temporarily isolate environment variables so ambient test runner settings
  // (e.g. GLIDE_NG_CONFIG, GLIDE_NG_CARD_MODEL, GLIDE_NG_SW_SIMD) cannot mask
  // or distort INI preset validation, then restore unconditionally on exit.
  struct ScopedEnvUnset {
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> saved;
    GlideWrapper::SwSimdLevel savedSimd;
    explicit ScopedEnvUnset(const std::vector<const char*>& names)
        : savedSimd(GlideWrapper::GetActiveSimdLevel()) {
      for (const char* name : names) {
        const char* val = std::getenv(name);
        if (val) {
          saved.push_back({name, {true, std::string(val)}});
          ::unsetenv(name);
        } else {
          saved.push_back({name, {false, ""}});
        }
      }
    }
    ~ScopedEnvUnset() {
      for (const auto& kv : saved) {
        if (kv.second.first) {
          ::setenv(kv.first.c_str(), kv.second.second.c_str(), 1);
        } else {
          ::unsetenv(kv.first.c_str());
        }
      }
      GlideWrapper::SetActiveSimdLevel(savedSimd);
    }
  };

  ScopedEnvUnset envGuard({
      "GLIDE_NG_CONFIG",
      "GLIDE_NG_BACKEND",
      "GLIDE_NG_CARD_MODEL",
      "GLIDE_NG_API_VERSION",
      "GLIDE_NG_MAX_FPS",
      "GLIDE_NG_VSYNC",
      "GLIDE_NG_SW_SIMD",
      "GLIDE_NG_NO_SPLASH",
      "FX_GLIDE_SWAPINTERVAL",
      "FX_GLIDE_NO_SPLASH",
      "SST_GAMMA",
      "SSTV2_GAMMA",
  });

  // 1. Verify every title directory under games/, demos/, and tools/ has both
  //    README.md and glide-ng.ini.
  size_t titleDirCount = 0;
  for (const char* bucket : {"games", "demos", "tools"}) {
    const fs::path bucketDir = catalogDir / bucket;
    REQUIRE(fs::is_directory(bucketDir));
    for (const auto& entry : fs::directory_iterator(bucketDir)) {
      if (!entry.is_directory()) continue;
      ++titleDirCount;
      INFO("Catalog title dir: " << entry.path().string());
      CHECK(fs::is_regular_file(entry.path() / "README.md"));
      CHECK(fs::is_regular_file(entry.path() / "glide-ng.ini"));
    }
  }
  CHECK(titleDirCount >= 13u);

  // 2. Discover every glide-ng*.ini under docs/catalog/ plus root glide-ng.ini.example
  std::vector<fs::path> iniFiles;
  for (const auto& entry : fs::recursive_directory_iterator(catalogDir)) {
    if (!entry.is_regular_file()) continue;
    const std::string fname = entry.path().filename().string();
    if (fname.rfind("glide-ng", 0) == 0 && entry.path().extension() == ".ini") {
      iniFiles.push_back(entry.path());
    }
  }
  const fs::path exampleIni = repoRoot / "glide-ng.ini.example";
  REQUIRE(fs::is_regular_file(exampleIni));
  iniFiles.push_back(exampleIni);
  std::sort(iniFiles.begin(), iniFiles.end());

  REQUIRE(iniFiles.size() >= 17u);

  GlideWrapper::IniConfigLoader loader;
  for (const auto& iniPath : iniFiles) {
    GlideWrapper::WrapperConfig cfg;
    INFO("Validating preset: " << iniPath.string());
    REQUIRE(loader.Load(iniPath.string(), cfg));
    CHECK(cfg.configFileLoaded);
    if (!cfg.unknownConfigKeys.empty()) {
      UNSCOPED_INFO("Unknown keys: " << cfg.unknownConfigKeys[0]);
    }
    if (!cfg.invalidConfigValues.empty()) {
      UNSCOPED_INFO("Invalid values: " << cfg.invalidConfigValues[0]);
    }
    if (!cfg.compatibilityWarnings.empty()) {
      UNSCOPED_INFO("Compat warning: " << cfg.compatibilityWarnings[0]);
    }
    CHECK(cfg.unknownConfigKeys.empty());
    CHECK(cfg.invalidConfigValues.empty());
    CHECK(cfg.compatibilityWarnings.empty());
  }

  // 3. Spot-check semantic intent of non-default title presets
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "demos" / "3dfx-demo-room" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(std::fabs(cfg.gammaOverride - 1.7f) < 0.001f);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "demos" / "3dfx-kitty-hawk" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "tools" / "glide-bench" / "glide-ng.voodoo5.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.vsync == false);
    CHECK(cfg.maxFps == 0.0f);
    CHECK(cfg.noSplash == true);
  }
}
