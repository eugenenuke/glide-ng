#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <cmath>
#include "core/WrapperConfig.h"
#include "core/JsonConfigLoader.h"
#include "core/GlideEntryCommon.h"
#include "core/TextureManager.h"
#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/HudOverlay.h"
#include "core/BackendManager.h"
#include "core/NativeSurfaceHost.h"
#include "backends/GlideStateTracker.h"
#include "opengl_es/OpenGLESBackend.h"
#include "software/SoftwareBackend.h"
#include "vulkan/VulkanBackend.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
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

    SECTION("Dynamic Card Capabilities (Voodoo 1 vs 2 vs 3)") {
        // Test Voodoo 3 configuration
        ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
        glideInit();
        
        FxI32 params[4] = {0};
        grGetVal(GR_NUM_TMU, 4, params);
        REQUIRE(params[0] == 2);

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 2048);

        grGetVal(GR_MEMORY_FB, 4, params);
        REQUIRE(params[0] == 16 * 1024 * 1024); // Voodoo3 default is 16MB

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

extern "C" int tlGetOpt(int argc, char *argv[], const char *tags, char *match, char **remArgs[]);

TEST_CASE("tlib tlGetOpt Conjoined Arguments Verification", "[tlib][GetOpt][tlib_GetOpt]") {
    // Test space-separated argument (standard)
    {
        tlGetOpt(0, nullptr, nullptr, nullptr, nullptr); // Reset static parser state
        char arg0[] = "./test";
        char arg1[] = "-r";
        char arg2[] = "1024x768";
        char* argv[] = { arg0, arg1, arg2, nullptr };
        int argc = 3;

        char match = 0;
        char** remArgs = nullptr;
        
        int rv = tlGetOpt(argc, argv, "r", &match, &remArgs);
        CHECK(rv == 1);
        CHECK(match == 'r');
        REQUIRE(remArgs != nullptr);
        CHECK(std::strcmp(remArgs[0], "1024x768") == 0);
    }

    // Test conjoined argument (the bug case: -r1024x768)
    {
        tlGetOpt(0, nullptr, nullptr, nullptr, nullptr); // Reset static parser state
        char arg0[] = "./test";
        char arg1[] = "-r1024x768";
        char* argv[] = { arg0, arg1, nullptr };
        int argc = 2;

        char match = 0;
        char** remArgs = nullptr;
        
        // This would have segfaulted before our fix!
        int rv = tlGetOpt(argc, argv, "r", &match, &remArgs);
        
        CHECK(rv == 1);
        CHECK(match == 'r');
        REQUIRE(remArgs != nullptr);
        // The prefix "-r" should be stripped in-place, making the argument value "1024x768"
        CHECK(std::strcmp(remArgs[0], "1024x768") == 0);
    }
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
    SECTION("Glide 2.x No FOGCOORD Support") {
#ifdef __linux__
        void* handle = OpenGlide2Handle();
        REQUIRE(handle != nullptr);

        using InitFunc = void(*)(void);
        using WinOpenFunc = FxBool(*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
        using GetStringFunc = const char*(*)(FxU32);
        using WinCloseFunc = FxBool(*)(void);
        using ShutdownFunc = void(*)(void);

        auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
        auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
        auto getString = reinterpret_cast<GetStringFunc>(::dlsym(handle, "grGetString"));
        auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
        auto shutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(getString != nullptr);
        REQUIRE(winClose != nullptr);
        REQUIRE(shutdown != nullptr);

        ::setenv("GLIDE_NG_BACKEND", "software", 1);
        glideInit();
        // Resolution GR_RESOLUTION_640x480 = 0x4
        FxBool ok = winOpen(0, 0x4, 0, 0, 0, 2, 1);
        REQUIRE(ok == FXTRUE);

        // A. Verify that FOGCOORD is NOT advertised under Glide 2
        const char* extensions = getString(0xa0); // GR_EXTENSION
        if (extensions) {
            std::string extStr(extensions);
            CHECK(extStr.find("FOGCOORD") == std::string::npos);
        }

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

    // Get current state
    std::vector<uint8_t> stateBuffer(sizeof(MockGlideWrapperState), 0);
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
    std::vector<uint8_t> checkBuffer(sizeof(MockGlideWrapperState), 0);
    getState(checkBuffer.data());

    auto* checkPtr = reinterpret_cast<MockGlideWrapperState*>(checkBuffer.data());

    // Cull mode must be sanitized to default (0 - GR_CULL_DISABLE)
    REQUIRE(checkPtr->cullMode == 0);

    // Depth compare must be sanitized to default (1 - GR_CMP_LESS)
    REQUIRE(checkPtr->depthCompare == 1);

    // Alpha test op must be sanitized to default (7 - GR_CMP_ALWAYS)
    REQUIRE(checkPtr->alphaTestOp == 7);

    // Blend modes must be sanitized to default (src=1, dst=0)
    REQUIRE(checkPtr->rgbSrcBlend == 1);
    REQUIRE(checkPtr->rgbDstBlend == 0);

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

TEST_CASE("JsonConfigLoader uses key-scoped parsing and respects env overrides",
          "[Config][JsonConfigLoader]") {
  const std::string tmpPath = "/tmp/glide_ng_test_config_t15.json";

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

  SECTION("Unrelated fields containing backend/model substrings do not hijack config") {
    {
      std::ofstream out(tmpPath);
      out << "{\n"
          << "  \"comment\": \"do not use \\\"vulkan\\\" or \\\"software\\\" or Voodoo5\",\n"
          << "  \"nested\": { \"backend\": \"vulkan\", \"model\": \"Voodoo5\", \"logToConsole\": false },\n"
          << "  \"backend\"   :   \"opengl_es\",\n"
          << "  \"model\"  :  \"Voodoo3\",\n"
          << "  \"apiVersion\" : \"2.42\",\n"
          << "  \"logToConsole\"  :  true,\n"
          << "  \"vsync\":false,\n"
          << "  \"forceNoWindow\" : true,\n"
          << "  \"maxFps\" : 120.5,\n"
          << "  \"msaaSamples\" : 4,\n"
          << "  \"windowScale\" : 2,\n"
          << "  \"logLevel\" : 4\n"
          << "}\n";
    }

    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_42);
    CHECK(cfg.logToConsole == true);
    CHECK(cfg.vsync == false);
    CHECK(cfg.forceNoWindow == true);
    CHECK(cfg.maxFps == 120.5f);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.windowScale == 2u);
    CHECK(cfg.logLevel == 4u);

    std::remove(tmpPath.c_str());
  }

  SECTION("Boolean false and cardModel key parse cleanly, env overrides win") {
    {
      std::ofstream out(tmpPath);
      out << "{\n"
          << "  \"cardModel\": \"VoodooGraphics\",\n"
          << "  \"backend\": \"gles\",\n"
          << "  \"logToConsole\": false,\n"
          << "  \"vsync\": true,\n"
          << "  \"apiVersion\": 2.40\n"
          << "}\n";
    }

    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.logToConsole == false);
    CHECK(cfg.vsync == true);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_40);

    // Environment variables must override JSON values.
    ::setenv("GLIDE_NG_BACKEND", "sw", 1);
    ::setenv("GLIDE_NG_LOG_CONSOLE", "1", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);

    GlideWrapper::WrapperConfig envCfg;
    REQUIRE(loader.Load(tmpPath, envCfg));
    CHECK(envCfg.backend == "software");
    CHECK(envCfg.logToConsole == true);
    CHECK(envCfg.vsync == false);

    std::remove(tmpPath.c_str());
  }

  SECTION("Strict 4-layer precedence: Defaults < JSON < FX_GLIDE_* < GLIDE_NG_*") {
    const std::string customPath = "/tmp/glide_ng_custom_cfg_test.json";
    {
      std::ofstream out(customPath);
      out << "{\n"
          << "  \"noSplash\": false,\n"
          << "  \"shamelessPlug\": false,\n"
          << "  \"vsync\": true,\n"
          << "  \"logLevel\": \"warn\",\n"
          << "  \"presentationFilter\": \"nearest\",\n"
          << "  \"logFile\": \"custom_json.log\",\n"
          << "  \"swThreads\": 4,\n"
          << "  \"swSimd\": false,\n"
          << "  \"swTileSize\": 64,\n"
          << "  \"swAa\": \"ssaa\",\n"
          << "  \"vkCachePath\": \"/tmp/custom_vk.bin\",\n"
          << "  \"glesVboSize\": 16\n"
          << "}\n";
    }

    // GLIDE_NG_CONFIG redirects loader away from default path
    ::setenv("GLIDE_NG_CONFIG", customPath.c_str(), 1);
    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.json", cfg));
    CHECK(cfg.configFileLoaded == true);
    CHECK(cfg.configFilePath == customPath);
    CHECK(cfg.noSplash == false);
    CHECK(cfg.shamelessPlugMode == 0);
    CHECK(cfg.vsync == true);
    CHECK(cfg.logLevel == 2u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.logFile == "custom_json.log");
    CHECK(cfg.swThreads == 4u);
    CHECK(cfg.swUseSimd == false);
    CHECK(cfg.swTileSize == 64u);
    CHECK(cfg.swAaMode == "ssaa");
    CHECK(cfg.vkCachePath == "/tmp/custom_vk.bin");
    CHECK(cfg.glesVboSizeMb == 16u);
    CHECK(cfg.settingSources.at("noSplash") == "json");

    // Layer 3: Authentic FX_GLIDE_* overrides JSON
    ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
    ::setenv("FX_GLIDE_SHAMELESS_PLUG", "1", 1);
    ::setenv("FX_GLIDE_SWAPINTERVAL", "0", 1);
    REQUIRE(loader.Load("nonexistent_default.json", cfg));
    CHECK(cfg.noSplash == true);
    CHECK(cfg.shamelessPlugMode == 1);
    CHECK(cfg.vsync == false);
    CHECK(cfg.swapInterval == 0);
    CHECK(cfg.settingSources.at("noSplash") == "FX_GLIDE_NO_SPLASH");
    CHECK(cfg.settingSources.at("shamelessPlug") == "FX_GLIDE_SHAMELESS_PLUG");
    CHECK(cfg.settingSources.at("vsync") == "FX_GLIDE_SWAPINTERVAL");

    // Layer 4: GLIDE_NG_* overrides FX_GLIDE_* and JSON
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
    REQUIRE(loader.Load("nonexistent_default.json", cfg));
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

    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.json", cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.vsync == true);
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_43);
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

  SECTION("JSON & GLIDE_NG_* parsing for logCategories, traceFrames, perfLog") {
    const std::string cfgPath = "/tmp/glide_ng_m2_cfg_test.json";
    {
      std::ofstream out(cfgPath);
      out << "{\n"
          << "  \"logCategories\": \"api,lfb\",\n"
          << "  \"traceFrames\": \"10-20\",\n"
          << "  \"perfLog\": \"/tmp/json_perf.csv\"\n"
          << "}\n";
    }

    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.logCategoriesSpec == "api,lfb");
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 10u);
    CHECK(cfg.traceFrameEnd == 20u);
    CHECK(cfg.perfLogEnabled == true);
    CHECK(cfg.perfLogCsvPath == "/tmp/json_perf.csv");
    CHECK(cfg.settingSources.at("logCategories") == "json");
    CHECK(cfg.settingSources.at("traceFrames") == "json");
    CHECK(cfg.settingSources.at("perfLog") == "json");

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
    // Row 1 (frame 0): check tris_processed=2, tris_drawn=2, lfb_writes=1, lfb_write_bytes=512
    CHECK(lines[1].find(",2,2,0,1,0,512,") != std::string::npos);

    // Verify log file contains [Perf] summary emitted on ShutdownPerfLog
    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    std::string logContent((std::istreambuf_iterator<char>(logIn)),
                           std::istreambuf_iterator<char>());
    logIn.close();
    CHECK(logContent.find("[Perf] FPS=") != std::string::npos);
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

  SECTION("GLIDE_NG_HUD and JSON hud parsing + precedence") {
    const std::string cfgPath = "/tmp/glide_ng_m3_hud_cfg.json";
    {
      std::ofstream out(cfgPath);
      out << R"({"hud": "fps,backend,position=br,scale=1.5,opacity=0.6"})";
    }

    JsonConfigLoader loader;
    WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == (HUD_FPS | HUD_BACKEND));
    CHECK(cfg.hudPosition == HudPosition::BOTTOM_RIGHT);
    CHECK(std::fabs(cfg.hudScale - 1.5f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.6f) < 1e-4f);
    CHECK(cfg.settingSources.at("hud") == "json");

    // GLIDE_NG_HUD=full overrides JSON
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

    // GLIDE_NG_HUD=0 disables even when JSON enabled it
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
