#include <catch2/catch_test_macros.hpp>
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
    texInfo.smallLod = 5; // Glide 3.x GR_LOD_LOG2_32 = 5 (corresponds to Glide 2.x GR_LOD_32 = 3)
    texInfo.largeLod = 5;
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
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(sstSelect != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(winClose != nullptr);
    REQUIRE(colorComb != nullptr);
    REQUIRE(getState != nullptr);
    REQUIRE(setState != nullptr);
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

    // 1. Configure initial combiner state: GR_COMBINE_FUNCTION_LOCAL, ITERATED, NONE
    colorComb(1, 0, 0, 2, 0); // function=1, factor=0, local=0, other=2
    REQUIRE(backend->GetColorCombinerFunc() == 1);
    REQUIRE(backend->GetColorCombinerLocal() == 0);

    // 2. Save state using grGlideGetState
    char savedState[1024];
    getState(savedState);

    // 3. Modify combiner state to simulate watermark drawing: FUNCTION_SCALE_OTHER, CONSTANT, TEXTURE
    colorComb(3, 8, 1, 1, 0); // function=3, factor=8, local=1, other=1
    REQUIRE(backend->GetColorCombinerFunc() == 3);
    REQUIRE(backend->GetColorCombinerLocal() == 1);

    // 4. Restore state using grGlideSetState
    setState(savedState);

    // 5. Verify that the combiner state was successfully restored in the backend!
    REQUIRE(backend->GetColorCombinerFunc() == 1);
    REQUIRE(backend->GetColorCombinerLocal() == 0);

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
        REQUIRE(zRange[0] == 0);
        REQUIRE(zRange[1] == 65535);

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

        REQUIRE(glideInit != nullptr);
        REQUIRE(winOpen != nullptr);
        REQUIRE(colorCombine != nullptr);
        REQUIRE(constantColor != nullptr);
        REQUIRE(cullMode != nullptr);
        REQUIRE(drawTriangle != nullptr);
        REQUIRE(bufferClear != nullptr);
        REQUIRE(swap != nullptr);
        REQUIRE(shutdown != nullptr);

        glideInit();
        if (sstSelect) sstSelect(0);

        G3Context ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
        REQUIRE(ctx != 0);

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
#endif
}
