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
#include "backends/SoftwareBackendBase.h"
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

TEST_CASE("Glide 3.x: State Control, Hardware Identity & grGet Queries", "[Glide3][State][grGet]") {
  SECTION("Dynamic capabilities & state control") {
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

  SECTION("grGet query invariants (Z/W ranges, busy, state/layout sizes)") {
#ifdef __linux__
  // Load the runtime wrapper dynamic library
  void* handle = OpenGlide3DsoHandle();
  REQUIRE(handle != nullptr);

  // Resolve entry points
  auto init = (void(FX_CALL*)(void))::dlsym(handle, "grGlideInit");
  auto shutdown = (void(FX_CALL*)(void))::dlsym(handle, "grGlideShutdown");
  auto sstSelect = (void(FX_CALL*)(int))::dlsym(handle, "grSstSelect");
  auto sstWinOpen = (FxBool(FX_CALL*)(
      FxU32, GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t,
      GrOriginLocation_t, int, int))::dlsym(handle, "grSstWinOpen");
  auto get = (FxU32(FX_CALL*)(FxU32, FxU32, FxI32*))::dlsym(handle, "grGet");

  REQUIRE(init != nullptr);
  REQUIRE(shutdown != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(sstWinOpen != nullptr);
  REQUIRE(get != nullptr);

  init();
  sstSelect(0);

  // Open window to initialize the engine
  FxBool open_ok = sstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(open_ok == FXTRUE);

  SECTION("Query Z-depth range returns Voodoo3 hardware specs (65535, 0)") {
    FxI32 zrange[2] = {-999, -999};
    FxU32 bytes = get(0x28, 8, zrange);  // 0x28 = GR_ZDEPTH_MIN_MAX
    REQUIRE(bytes == 8);
    REQUIRE(zrange[0] == 65535);  // Nearest
    REQUIRE(zrange[1] == 0);      // Farthest
  }

  SECTION("Query W-depth range returns (0, 65535)") {
    FxI32 wrange[2] = {-999, -999};
    FxU32 bytes = get(0x27, 8, wrange);  // 0x27 = GR_WDEPTH_MIN_MAX
    REQUIRE(bytes == 8);
    REQUIRE(wrange[0] == 0);
    REQUIRE(wrange[1] == 65535);
  }

  SECTION("Query IS_BUSY returns 0") {
    FxI32 is_busy = -999;
    FxU32 bytes = get(0x08, 4, &is_busy);  // 0x08 = GR_IS_BUSY
    REQUIRE(bytes == 4);
    REQUIRE(is_busy == 0);
  }

  SECTION("Query GLIDE_STATE_SIZE returns 1024") {
    FxI32 state_size = -999;
    FxU32 bytes = get(0x06, 4, &state_size);
    REQUIRE(bytes == 4);
    REQUIRE(state_size == 1024);
  }

  SECTION("Query GLIDE_VERTEXLAYOUT_SIZE returns 256") {
    FxI32 layout_size = -999;
    FxU32 bytes = get(0x07, 4, &layout_size);
    REQUIRE(bytes == 4);
    REQUIRE(layout_size == 256);
  }

  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("Authentic short GR_HARDWARE card names") {
#ifdef __linux__
  struct CardExpectation {
    const char* envVal;
    const char* expectedName;
  };
  const CardExpectation cases[] = {
      {"voodoo1", "Voodoo Graphics"},
      {"rush", "Voodoo Rush"},
      {"voodoo2", "Voodoo2"},
      {"banshee", "Voodoo Banshee (tm)"},
      {"voodoo_banshee", "Voodoo Banshee (tm)"},
      {"voodoo3", "Voodoo3 (tm)"},
      {"voodoo4", "Voodoo4 (tm)"},
      {"vsa100", "Voodoo4 (tm)"},
      {"h4", "Voodoo4 (tm)"},
      {"voodoo5", "Voodoo5 (tm)"},
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

  // Verify authentic GR_REVISION_FB / GR_REVISION_TMU values across profiles,
  // including CVG dual-TMU (0x102) and single-TMU "Voodoo 1.5" (0x82).
  struct RevExpectation {
    const char* card;
    const char* tmus;
    FxI32 expectedFbRev;
    FxI32 expectedTmuRev;
  };
  const RevExpectation revCases[] = {
      {"voodoo1", nullptr, 2, 1},
      {"rush", nullptr, 1, 1},
      {"voodoo2", "2", 0x102, 1},
      {"voodoo2", "1", 0x82, 1},
      {"banshee", nullptr, 0x1000, 0x1000},
      {"voodoo3", nullptr, 0x11100, 0x11100},
      {"voodoo4", nullptr, 0x11104, 0x11104},
      {"voodoo5", nullptr, 0x11105, 0x11105},
  };
  const char* origTmu = std::getenv("GLIDE_NG_TMU_COUNT");
  std::string savedTmu = origTmu ? origTmu : "";
  for (const auto& rc : revCases) {
    ::setenv("GLIDE_NG_CARD_MODEL", rc.card, 1);
    if (rc.tmus) {
      ::setenv("GLIDE_NG_TMU_COUNT", rc.tmus, 1);
    } else {
      ::unsetenv("GLIDE_NG_TMU_COUNT");
    }
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);
    auto glideInit = reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideInit"));
    auto grGetFn =
        reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxI32*)>(::dlsym(handle, "grGet"));
    auto glideShutdown =
        reinterpret_cast<void (*)(void)>(::dlsym(handle, "grGlideShutdown"));
    REQUIRE(glideInit != nullptr);
    REQUIRE(grGetFn != nullptr);
    REQUIRE(glideShutdown != nullptr);
    glideInit();
    FxI32 fbRev = -1;
    FxI32 tmuRev = -1;
    REQUIRE(grGetFn(GR_REVISION_FB, 4, &fbRev) == 4);
    REQUIRE(grGetFn(GR_REVISION_TMU, 4, &tmuRev) == 4);
    CHECK(fbRev == rc.expectedFbRev);
    CHECK(tmuRev == rc.expectedTmuRev);
    glideShutdown();
    ::dlclose(handle);
  }
  if (origTmu) {
    ::setenv("GLIDE_NG_TMU_COUNT", savedTmu.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_TMU_COUNT");
  }

  if (origCard) {
    ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_CARD_MODEL");
  }
#endif
  }

}

TEST_CASE("Glide 3.x: State Save/Restore & Sanitization", "[Glide3][State][Serialization]") {
  SECTION("Combiner save/restore") {
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

  SECTION("Clip window & color mask save/restore") {
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

  SECTION("State restoration bounds sanitization") {
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

  SECTION("Retro aesthetic state serialization") {
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

}

TEST_CASE("Glide 3.x: Framebuffer Clear, ClipWindow & Depth Scaling", "[Glide3][Clear][ClipWindow]") {
  SECTION("Depth clear scaling") {
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

  SECTION("grBufferClear respects grClipWindow & origin") {
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

}

TEST_CASE("Glide 3.x: Splash & Shameless Plug State Preservation", "[Glide3][Splash][State]") {
  SECTION("Shameless plug preserves W-buffered scene across swaps") {
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

  SECTION("grSplash preserves implicit vertex layout defaults") {
  SECTION("VertexLayout GetLayoutState/SetLayoutState preserves implicit default Z/W flags") {
    auto& vl = GlideWrapper::VertexLayout::GetInstance();
    vl.ResetToGlide3Canonical();
    CHECK_FALSE(vl.HasExplicitZ());

    GlideWrapper::Glide3VertexLayout saved{};
    vl.GetLayoutState(&saved);
    vl.ResetToGlide2Canonical();
    vl.SetLayoutState(&saved);

    // Must NOT falsely promote canonical OOZ at +8 to explicit GR_PARAM_Z.
    CHECK_FALSE(vl.HasExplicitZ());

    // Simulate a Hitman 47 vertex with unused garbage float at offset +8.
    struct HitmanLikeVert {
      float x, y;
      float garbageZ;  // byte offset +8: NOT registered with GR_PARAM_Z
      float w;         // byte offset +12
      float q;         // byte offset +16
      float r, g, b;   // byte offset +20
      float a;         // byte offset +32
      float s0, t0;    // byte offset +36
      float q0;        // byte offset +44
    };
    vl.SetParamOffsetGlide3(0x01, 0, 1);   // GR_PARAM_XY
    vl.SetParamOffsetGlide3(0x03, 12, 1);  // GR_PARAM_W
    vl.SetParamOffsetGlide3(0x04, 16, 1);  // GR_PARAM_Q
    vl.SetParamOffsetGlide3(0x20, 20, 1);  // GR_PARAM_RGB
    vl.SetParamOffsetGlide3(0x10, 32, 1);  // GR_PARAM_A
    vl.SetParamOffsetGlide3(0x40, 36, 1);  // GR_PARAM_ST0
    vl.SetParamOffsetGlide3(0x50, 44, 1);  // GR_PARAM_Q0

    HitmanLikeVert v{};
    v.x = 0.0f;
    v.y = 0.0f;
    v.garbageZ = -9999.0f;
    v.w = 2.0f;
    v.q = 0.5f;
    v.r = 1.0f;
    v.g = 0.5f;
    v.b = 0.25f;
    v.a = 1.0f;
    const auto cv = vl.DecodeClipVertex(&v);
    CHECK_FALSE(vl.HasExplicitZ());
    CHECK(cv.z == 0.0f);
    CHECK(cv.w == 2.0f);
    CHECK(cv.q == 0.5f);

    // Explicit GR_PARAM_Z must still survive a layout save/restore round-trip.
    vl.SetParamOffsetGlide3(0x02, 8, 1);  // GR_PARAM_Z explicit enable
    CHECK(vl.HasExplicitZ());
    vl.GetLayoutState(&saved);
    vl.ResetAllParamsDisabled();
    CHECK_FALSE(vl.HasExplicitZ());
    vl.SetLayoutState(&saved);
    CHECK(vl.HasExplicitZ());
    v.garbageZ = 1.25f;
    CHECK(vl.DecodeClipVertex(&v).z == 1.25f);

    // Implicit canonical W cleared when only GR_PARAM_Q is touched after restore.
    vl.ResetToGlide3Canonical();
    vl.GetLayoutState(&saved);
    vl.ResetToGlide2Canonical();
    vl.SetLayoutState(&saved);
    vl.SetParamOffsetGlide3(0x04, 12, 0);  // disable GR_PARAM_Q clears default W
    CHECK(vl.DecodeClipVertex(&v).w == 1.0f);

    vl.ResetToGlide2Canonical();
  }

#ifdef __linux__
  SECTION("Multi-backend grSplash() + GR_CLIP_COORDS draw without GR_PARAM_Z") {
    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    using InitFunc = void (*)();
    using ShutdownFunc = void (*)();
    using WinOpenFunc = FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int);
    using WinCloseFunc = FxBool (*)(FxU32);
    using SelectCtxFunc = FxBool (*)(FxU32);
    using SplashFunc = void (*)(float, float, float, float, FxU32);
    using ClearFunc = void (*)(FxU32, FxU32, FxU32);
    using CoordSpaceFunc = void (*)(FxU32);
    using ViewportFunc = void (*)(FxI32, FxI32, FxI32, FxI32);
    using LayoutFunc = void (*)(FxU32, FxI32, FxU32);
    using ColorCombineFunc = void (*)(FxI32, FxI32, FxI32, FxI32, FxBool);
    using AlphaBlendFunc = void (*)(FxI32, FxI32, FxI32, FxI32);
    using DepthModeFunc = void (*)(FxI32);
    using CullFunc = void (*)(FxI32);
    using DrawTriFunc = void (*)(const void*, const void*, const void*);
    using ReadRegionFunc = FxBool (*)(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, void*);

    auto glideInit = reinterpret_cast<InitFunc>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<ShutdownFunc>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<WinOpenFunc>(::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<WinCloseFunc>(::dlsym(handle, "grSstWinClose"));
    auto selectCtx = reinterpret_cast<SelectCtxFunc>(::dlsym(handle, "grSelectContext"));
    auto splash = reinterpret_cast<SplashFunc>(::dlsym(handle, "grSplash"));
    auto bufClear = reinterpret_cast<ClearFunc>(::dlsym(handle, "grBufferClear"));
    auto coordSpace = reinterpret_cast<CoordSpaceFunc>(::dlsym(handle, "grCoordinateSpace"));
    auto viewport = reinterpret_cast<ViewportFunc>(::dlsym(handle, "grViewport"));
    auto vtxLayout = reinterpret_cast<LayoutFunc>(::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<ColorCombineFunc>(::dlsym(handle, "grColorCombine"));
    auto alphaBlend = reinterpret_cast<AlphaBlendFunc>(::dlsym(handle, "grAlphaBlendFunction"));
    auto depthMode = reinterpret_cast<DepthModeFunc>(::dlsym(handle, "grDepthBufferMode"));
    auto cullMode = reinterpret_cast<CullFunc>(::dlsym(handle, "grCullMode"));
    auto drawTri = reinterpret_cast<DrawTriFunc>(::dlsym(handle, "grDrawTriangle"));
    auto readRegion = reinterpret_cast<ReadRegionFunc>(::dlsym(handle, "grLfbReadRegion"));
    REQUIRE((glideInit && glideShutdown && winOpen && winClose && selectCtx &&
             splash && bufClear && coordSpace && viewport && vtxLayout &&
             colorCombine && alphaBlend && depthMode && cullMode && drawTri &&
             readRegion));

    struct ClipNoZVert {
      float x, y;
      float garbageZ;  // byte +8: intentionally outside [-w, +w]
      float w;
      float q;
      float r, g, b;
      float a;
    };

    for (const char* be : {"software", "vulkan", "opengl_es"}) {
      ::setenv("GLIDE_NG_BACKEND", be, 1);
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
      ::unsetenv("FX_GLIDE_NO_SPLASH");
      ::unsetenv("GLIDE_NG_NO_SPLASH");

      glideInit();
      const FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                                GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
      REQUIRE(ctx != 0);
      selectCtx(ctx);

      // Explicitly invoke grSplash (single frame) to exercise its layout
      // save/restore right before configuring a W-buffered GR_CLIP_COORDS layout.
      splash(0.0f, 0.0f, 640.0f, 480.0f, 1u);

      coordSpace(1 /*GR_CLIP_COORDS*/);
      viewport(0, 0, 640, 480);
      depthMode(GR_DEPTHBUFFER_DISABLE);
      cullMode(GR_CULL_DISABLE);
      alphaBlend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

      // Exact Hitman 47 Render3DFX.dll pattern: never binds GR_PARAM_Z.
      vtxLayout(0x01 /*XY*/, 0, 1);
      vtxLayout(0x03 /*W*/, 12, 1);
      vtxLayout(0x04 /*Q*/, 16, 1);
      vtxLayout(0x20 /*RGB*/, 20, 1);
      vtxLayout(0x10 /*A*/, 32, 1);

      bufClear(0xFF000020u, 0, 0xFFFF);

      ClipNoZVert v0{-0.8f,  0.8f, -9999.0f, 1.0f, 1.0f, 0.1f, 0.95f, 0.1f, 1.0f};
      ClipNoZVert v1{ 0.8f,  0.8f, +9999.0f, 1.0f, 1.0f, 0.1f, 0.95f, 0.1f, 1.0f};
      ClipNoZVert v2{ 0.0f, -0.8f, -5000.0f, 1.0f, 1.0f, 0.1f, 0.95f, 0.1f, 1.0f};
      drawTri(&v0, &v1, &v2);

      uint16_t px = 0;
      REQUIRE(readRegion(GR_BUFFER_BACKBUFFER, 320, 240, 1, 1, 2, &px) == FXTRUE);
      const uint32_t g = ((px >> 5) & 0x3Fu) * 255u / 63u;
      const uint32_t b = (px & 0x1Fu) * 255u / 31u;
      INFO("backend=" << be << " center px=0x" << std::hex << px);
      CHECK(g > 180u);
      CHECK(b < 60u);

      winClose(ctx);
      glideShutdown();
      ::unsetenv("GLIDE_NG_BACKEND");
      ::unsetenv("GLIDE_NG_HEADLESS");
    }
    ::dlclose(handle);
  }
#endif
  }

}

TEST_CASE("Glide 3.x: Capabilities, Resolutions, grReset & Card-Version Matrix", "[Glide3][Capabilities]") {
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

