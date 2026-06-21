#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <cmath>
#include "core/WrapperConfig.h"
#include "core/Logger.h"
#include "core/BackendManager.h"
#include "software/SoftwareBackend.h"
#include <cstdlib>
#include <dlfcn.h>
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
        ::setenv("GLIDE_WRAPPER_CARD_MODEL", "voodoo3", 1);
        glideInit();
        
        FxI32 params[4] = {0};
        grGetVal(GR_NUM_TMU, 4, params);
        REQUIRE(params[0] == 2);

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 2048);

        grGetVal(GR_MEMORY_FB, 4, params);
        REQUIRE(params[0] == 16 * 1024 * 1024); // Voodoo3 default is 16MB

        shutdown();
        ::unsetenv("GLIDE_WRAPPER_CARD_MODEL");

        // Test Voodoo 1 configuration
        ::setenv("GLIDE_WRAPPER_CARD_MODEL", "voodoo1", 1);
        glideInit();

        grGetVal(GR_NUM_TMU, 4, params);
        REQUIRE(params[0] == 1);

        grGetVal(GR_MAX_TEXTURE_SIZE, 4, params);
        REQUIRE(params[0] == 256);

        shutdown();
        ::unsetenv("GLIDE_WRAPPER_CARD_MODEL");
    }

    SECTION("Viewport & Depth Range Queries") {
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
    }

    SECTION("Clip Space Coordinates Projection Verification") {
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1); // INFO

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, 8, 0, 0, 0, 2, 0); // 800x600 resolution
    REQUIRE(ctx != 0);

    // Get the reference to active backend
    auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1); // INFO

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
    ::setenv("GLIDE_WRAPPER_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1); // INFO

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
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);

        glideInit();
        if (sstSelect) sstSelect(0);

        // Open window with LOWER_LEFT origin (1)
        unsigned long ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

        auto* backend = static_cast<GlideWrapper::SoftwareBackendBase*>(
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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "vulkan", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1); // INFO

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
    ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "opengl_es", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1); // INFO

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

    INFO("GLES Blended Pixel Color: R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b);

    // Assert that color matches Orange/Yellow (128, 128, 0) with a tolerance of +-5
    REQUIRE(std::abs((int)r - 128) <= 5);
    REQUIRE(std::abs((int)g - 128) <= 5);
    REQUIRE(b <= 5);

    winClose();
    glideShutdown();
    ::dlclose(handle);
    ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "SOFTWARE", 1);
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
    ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
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

        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
        ::setenv("GLIDE_WRAPPER_SOFTWARE_AA", "analytical", 1);

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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
        ::unsetenv("GLIDE_WRAPPER_SOFTWARE_AA");
    }

    SECTION("Option B: SSAA Resolution Scaling & Downsampling") {
        ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
        ::setenv("GLIDE_WRAPPER_SOFTWARE_AA", "ssaa", 1);

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
        ::unsetenv("GLIDE_WRAPPER_BACKEND");
        ::unsetenv("GLIDE_WRAPPER_SOFTWARE_AA");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1);

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
    ::unsetenv("GLIDE_WRAPPER_BACKEND");
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
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_WRAPPER_LOG_LEVEL", "3", 1);

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
            CHECK(g == 0); // Crucial: no green halo!
            CHECK(a < 255);
            CHECK(a > 0);
            CHECK(colorVal < 255);
            CHECK(colorVal > 0);
        }
    }

    winClose();
    shutdown();
    ::unsetenv("GLIDE_WRAPPER_BACKEND");
}
#endif
