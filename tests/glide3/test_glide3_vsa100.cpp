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
#include "opengl_es/OpenGLESBackend.h"
#include "software/SoftwareBackend.h"
#include "vulkan/VulkanBackend.h"
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

TEST_CASE("VSA-100: Voodoo4/Voodoo5 Profile & Spatial AA / T-Buffer", "[Glide3][VSA100][SpatialAA]") {
  SECTION("Voodoo5 spatial AA & T-Buffer extensions") {
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

  SECTION("Voodoo4 profile & strict card-model extension gating") {
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
    void* handle = OpenGlide2DsoHandle();
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

}

TEST_CASE("VSA-100: GETGAMMA, grColorMaskExt, TEXUMA & Multibase", "[Glide3][VSA100][GETGAMMA]") {
  SECTION("GETGAMMA round-trip & missing public exports") {
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
  auto getGamma = GlideTestHelpers::CastProc<GetGammaFunc>(getProc(procName));
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

  SECTION("Per-channel grColorMaskExt, TEXUMA, multibase & detail control") {
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
  auto colorMaskExt = GlideTestHelpers::CastProc<ColorMaskExtFunc>(getProc(cmExtName));
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

}

TEST_CASE("VSA-100: Hardware Stencil (PIXEXT)", "[Glide3][VSA100][Stencil]") {
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

TEST_CASE("VSA-100: Extended Texture Formats (TEXFMT / DXT / FXT1 / YUV)", "[Glide3][VSA100][TexFmt]") {
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

TEST_CASE("VSA-100: Extended Combine & Render-to-Texture (TEXTUREBUFFER)", "[Glide3][VSA100][CombineExt]") {
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
      // Never advertise unimplemented private ICD extensions whose
      // grGetProcAddress() lookups return NULL (breaks 3dfxVGL/3dfxOGL ICDs).
      CHECK(ext.find("SURFACE") == std::string::npos);
      CHECK(ext.find("COMMAND_TRANSPORT") == std::string::npos);
      REQUIRE(!ext.empty());
      CHECK(ext.front() == ' ');
      CHECK(ext.back() == ' ');

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

TEST_CASE("GPU: Native Hardware BC1-BC3 Compressed Texture Upload", "[Glide3][BCn]") {
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

TEST_CASE("GPU: Zero-Readback TEXTUREBUFFER RTT (Origins & MSAA)", "[Glide3][RTT]") {
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

TEST_CASE("VSA-100: TEXUMA Pre-WinOpen Retention & FOGCOORD Cross-Backend Parity",
          "[Glide3][VSA100][TEXUMA][FOGCOORD]") {
  SECTION("grEnable(GR_TEXTURE_UMA_EXT) persists across grSstWinOpenExt") {
#ifdef __linux__
    const char* origCard = std::getenv("GLIDE_NG_CARD_MODEL");
    const char* origBe = std::getenv("GLIDE_NG_BACKEND");
    const char* origUma = std::getenv("FX_GLIDE_TEXTURE_UMA");
    std::string savedCard = origCard ? origCard : "";
    std::string savedBe = origBe ? origBe : "";
    std::string savedUma = origUma ? origUma : "";
    bool hadCard = (origCard != nullptr);
    bool hadBe = (origBe != nullptr);
    bool hadUma = (origUma != nullptr);
    auto restoreEnv = [&]() {
      if (hadCard) ::setenv("GLIDE_NG_CARD_MODEL", savedCard.c_str(), 1);
      else ::unsetenv("GLIDE_NG_CARD_MODEL");
      if (hadBe) ::setenv("GLIDE_NG_BACKEND", savedBe.c_str(), 1);
      else ::unsetenv("GLIDE_NG_BACKEND");
      if (hadUma) ::setenv("FX_GLIDE_TEXTURE_UMA", savedUma.c_str(), 1);
      else ::unsetenv("FX_GLIDE_TEXTURE_UMA");
    };

    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo5", 1);
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    ::unsetenv("FX_GLIDE_TEXTURE_UMA");

    void* handle = GlideTestHelpers::OpenGlideLibrary("libglide3x.so");
    REQUIRE(handle != nullptr);

    using InitFn = void (*)();
    using GetFn = FxU32 (*)(FxU32, FxU32, FxI32*);
    using GetProcFn = GrProc (*)(char*);
    using EnableFn = void (*)(FxU32);
    using WinOpenExtFn = FxU32 (*)(FxU32, GrScreenResolution_t,
                                   GrScreenRefresh_t, GrColorFormat_t,
                                   GrOriginLocation_t, FxU32, int, int);
    using WinCloseFn = FxBool (*)(FxU32);
    using MaxAddrFn = FxU32 (*)(GrChipID_t);
    using TexDlFn = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexSrcFn = void (*)(GrChipID_t, FxU32, FxU32, GrTexInfo*);
    using TexCombFn = void (*)(GrChipID_t, GrCombineFunction_t,
                               GrCombineFactor_t, GrCombineFunction_t,
                               GrCombineFactor_t, FxBool, FxBool);
    using ColorCombFn = void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                 GrCombineLocal_t, GrCombineOther_t, FxBool);
    using VertLayoutFn = void (*)(FxU32, FxI32, FxU32);
    using DrawTriFn = void (*)(const void*, const void*, const void*);
    using ClearFn = void (*)(GrColor_t, GrAlpha_t, FxU32);
    using LfbReadFn = FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                                 void*);

    auto glideInit = reinterpret_cast<InitFn>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown =
        reinterpret_cast<InitFn>(::dlsym(handle, "grGlideShutdown"));
    auto grGetFn = reinterpret_cast<GetFn>(::dlsym(handle, "grGet"));
    auto getProc =
        reinterpret_cast<GetProcFn>(::dlsym(handle, "grGetProcAddress"));
    auto enableFn = reinterpret_cast<EnableFn>(::dlsym(handle, "grEnable"));
    auto winClose =
        reinterpret_cast<WinCloseFn>(::dlsym(handle, "grSstWinClose"));
    auto texMax =
        reinterpret_cast<MaxAddrFn>(::dlsym(handle, "grTexMaxAddress"));
    auto texDl =
        reinterpret_cast<TexDlFn>(::dlsym(handle, "grTexDownloadMipMap"));
    auto texSrc = reinterpret_cast<TexSrcFn>(::dlsym(handle, "grTexSource"));
    auto texComb =
        reinterpret_cast<TexCombFn>(::dlsym(handle, "grTexCombine"));
    auto colorComb =
        reinterpret_cast<ColorCombFn>(::dlsym(handle, "grColorCombine"));
    auto vertexLayout =
        reinterpret_cast<VertLayoutFn>(::dlsym(handle, "grVertexLayout"));
    auto drawTri =
        reinterpret_cast<DrawTriFn>(::dlsym(handle, "grDrawTriangle"));
    auto bufClear = reinterpret_cast<ClearFn>(::dlsym(handle, "grBufferClear"));
    auto lfbRead =
        reinterpret_cast<LfbReadFn>(::dlsym(handle, "grLfbReadRegion"));
    REQUIRE(glideInit != nullptr);
    REQUIRE(glideShutdown != nullptr);
    REQUIRE(grGetFn != nullptr);
    REQUIRE(getProc != nullptr);
    REQUIRE(enableFn != nullptr);

    glideInit();
    FxI32 tmuMemBefore = 0;
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuMemBefore) == 4);
    const FxU32 maxBefore = texMax(GR_TMU0);
    CHECK(static_cast<FxU32>(tmuMemBefore) == maxBefore);

    // Authentic 3dfx TEXUMA sequence: grEnable(GR_TEXTURE_UMA_EXT) BEFORE
    // grSstWinOpenExt(). Window open must NOT reset UMA back to disabled.
    enableFn(0x06u /* GR_TEXTURE_UMA_EXT */);
    FxI32 tmuMemUmaPre = 0;
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuMemUmaPre) == 4);
    CHECK(tmuMemUmaPre == tmuMemBefore * 2);
    CHECK(texMax(GR_TMU0) == maxBefore * 2u);

    char openExtName[] = "grSstWinOpenExt";
    auto winOpenExt = GlideTestHelpers::CastProc<WinOpenExtFn>(
        getProc(openExtName));
    REQUIRE(winOpenExt != nullptr);
    const FxU32 ctx = winOpenExt(0u, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                                 GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                                 0x3u /* GR_PIXFMT_RGB_565 */, 2, 1);
    REQUIRE(ctx != 0u);

    FxI32 tmuMemUmaPost = 0;
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuMemUmaPost) == 4);
    CHECK(tmuMemUmaPost == tmuMemBefore * 2);
    CHECK(texMax(GR_TMU0) == maxBefore * 2u);

    // Download magenta RGB565 texture to TMU0, then sample it from TMU1 via
    // unified address space (TMU0 passes OTHER = TMU1 output).
    uint16_t texData[16 * 16];
    for (uint16_t& px : texData) px = 0xF81Fu;  // RGB565 magenta (255,0,255)
    GrTexInfo info{};
    info.smallLodLog2 = 4;   // GR_LOD_LOG2_16
    info.largeLodLog2 = 4;
    info.aspectRatioLog2 = 0;  // GR_ASPECT_LOG2_1x1
    info.format = GR_TEXFMT_RGB_565;
    info.data = texData;
    const FxU32 addr = 0x1000u;
    texDl(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texSrc(GR_TMU1, addr, GR_MIPMAPLEVELMASK_BOTH, &info);
    texComb(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
            GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
            FXFALSE);
    texComb(GR_TMU0, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
            GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, FXFALSE,
            FXFALSE);
    colorComb(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
              GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    struct UmaVtx {
      float x, y, oow, s1, t1;
    };
    vertexLayout(0x01u /* XY */, offsetof(UmaVtx, x), 1);
    vertexLayout(0x04u /* Q */, offsetof(UmaVtx, oow), 1);
    vertexLayout(0x41u /* ST1 */, offsetof(UmaVtx, s1), 1);
    bufClear(0xFF000000u, 0xFF, 0xFFFF);
    UmaVtx v0{0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    UmaVtx v1{64.0f, 0.0f, 1.0f, 255.0f, 0.0f};
    UmaVtx v2{64.0f, 64.0f, 1.0f, 255.0f, 255.0f};
    UmaVtx v3{0.0f, 64.0f, 1.0f, 0.0f, 255.0f};
    drawTri(&v0, &v1, &v2);
    drawTri(&v0, &v2, &v3);

    uint32_t fbRow[64]{};
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 0, 32, 64, 1, sizeof(fbRow), fbRow));
    const uint32_t sample = fbRow[32];
    // ARGB8888 returned by LFB read: expect magenta (~255, 0, 255), NOT white!
    const int sr = static_cast<int>((sample >> 16) & 0xFFu);
    const int sg = static_cast<int>((sample >> 8) & 0xFFu);
    const int sb = static_cast<int>(sample & 0xFFu);
    CHECK(sr >= 245);
    CHECK(sg <= 10);
    CHECK(sb >= 245);

    winClose(ctx);
    glideShutdown();

    // Fresh grGlideInit() must reset UMA back to disabled (no stale leak).
    glideInit();
    FxI32 tmuMemReset = 0;
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuMemReset) == 4);
    CHECK(tmuMemReset == tmuMemBefore);
    glideShutdown();

    ::dlclose(handle);
    restoreEnv();
#endif
  }

  SECTION("GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT uses reciprocal 1/W_fog on all backends") {
    using namespace GlideWrapper;

    uint8_t fogTbl[64];
    for (int i = 0; i < 64; ++i) {
      // Linearly ramping factor in table index: low near W=1, ~128 mid, 255 far.
      fogTbl[i] = static_cast<uint8_t>((i * 255) / 63);
    }

    auto makeV = [](float x, float y, float fogOow) {
      ModernVertex v{};
      v.pos = {x, y, 0.5f, 1.0f};  // Fixed pos[3]=1.0 (oow=1 => eyeW=1, idx=0!)
      v.color = {1.0f, 0.0f, 0.0f, 1.0f};  // Pure RED
      v.fog = fogOow;                       // Authentic reciprocal 1/W_fog
      return v;
    };

    for (const char* beName : {"software", "vulkan", "opengl_es"}) {
      auto& reg = EmulationRegistry::GetInstance();
      const WrapperConfig savedCfg = reg.GetConfig();
      WrapperConfig cfg{};
      cfg.backend = beName;
      cfg.model = CardModel::Voodoo5;
      cfg.tmuCount = 2;
      cfg.forceNoWindow = true;
      cfg.msaaSamples = 1;
      cfg.windowScale = 1;
      reg.GetConfig() = cfg;

      auto& bm = BackendManager::GetInstance();
      bm.ShutdownBackend();
      bm.EstablishBackend(cfg);
      IGraphicsBackend* be = bm.GetBackend();
      if (!be) {
        reg.GetConfig() = savedCfg;
        continue;
      }
      if (!be->AttachWindow(nullptr, 96u, 32u, /*windowed=*/true)) {
        bm.ShutdownBackend();
        reg.GetConfig() = savedCfg;
        continue;
      }

      be->SetSstOrigin(0u);
      be->SetPixelFormat(1u);  // GR_COLORFORMAT_ABGR (R in byte 0)
      be->SetClipWindow(0u, 0u, 96u, 32u);
      be->SetDitherMode(0u);
      be->SetDepthState(0u, 7u, false, 0);
      be->SetCullState(0u);
      be->SetColorMaskExt(true, true, true, true);
      be->SetCombinerMode(1u, 0u, 0u, 0u, false, 1u, 0u, 0u, 0u, false);
      be->SetBlendState(4u, 0u, 4u, 0u);  // ONE, ZERO
      be->ClearBuffer(0xFF000000u, 0xFFu, 1.0f, 3u);

      be->SetFogTable(fogTbl);
      be->SetFogColor(0xFF00FF00u);  // Green in ABGR/ARGB
      be->SetFogMode(1u);            // GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT (0x1)

      auto drawBand = [&](float x0, float x1, float fogOow) {
        ModernVertex a = makeV(x0, 0.0f, fogOow);
        ModernVertex b = makeV(x1, 0.0f, fogOow);
        ModernVertex c = makeV(x1, 32.0f, fogOow);
        ModernVertex d = makeV(x0, 32.0f, fogOow);
        be->DrawTriangle(a, b, c);
        be->DrawTriangle(a, c, d);
      };

      // Near (W_fog = 1.0 -> fog = 1.0): fogTbl[0] = 0 -> pure RED (255, 0, 0)
      drawBand(0.0f, 32.0f, 1.0f);
      // Mid (W_fog = 256.0 -> fog = 1/256): fogTbl[32] = 129 -> ~50/50 RED+GREEN
      drawBand(32.0f, 64.0f, 1.0f / 256.0f);
      // Far (W_fog = 65535.0 -> fog = 1/65535): fogTbl[63] = 255 -> pure GREEN
      drawBand(64.0f, 96.0f, 1.0f / 65535.0f);

      // Point primitive at pixel center (48.5, 28.5) on far fog (verifies
      // point-list fog on Vulkan/GLES + WritePixelPipeline on Software)
      ModernVertex pt = makeV(48.5f, 28.5f, 1.0f / 65535.0f);
      be->DrawPoint(pt);
      be->FlushBins();

      std::vector<uint32_t> fb(96u * 32u, 0u);
      REQUIRE(be->ReadLFB(1u, 0u, 0u, 96u, 32u, 96u * 4u, fb.data()));

      auto getRg = [&](uint32_t x, uint32_t y, int& r, int& g) {
        const uint32_t px = fb[y * 96u + x];
        r = static_cast<int>(px & 0xFFu);
        g = static_cast<int>((px >> 8) & 0xFFu);
      };

      int nearR = 0, nearG = 0, midR = 0, midG = 0, farR = 0, farG = 0;
      getRg(16u, 16u, nearR, nearG);
      getRg(48u, 16u, midR, midG);
      getRg(80u, 16u, farR, farG);
      INFO("backend=" << beName << " near=(" << nearR << "," << nearG << ")"
                      << " mid=(" << midR << "," << midG << ")"
                      << " far=(" << farR << "," << farG << ")");
      CHECK(nearR >= 250);
      CHECK(nearG <= 5);
      CHECK(midR >= 118);
      CHECK(midR <= 136);
      CHECK(midG >= 120);
      CHECK(midG <= 138);
      CHECK(farR <= 5);
      CHECK(farG >= 250);

      int ptR = 0, ptG = 0;
      getRg(48u, 28u, ptR, ptG);
      INFO("backend=" << beName << " point=(" << ptR << "," << ptG << ")");
      CHECK(ptR <= 5);
      CHECK(ptG >= 250);

      be->DetachWindow();
      bm.ShutdownBackend();
      reg.GetConfig() = savedCfg;
    }
  }

  SECTION("Software WritePixelPipeline executes extTexColorCombine when unbound") {
    using namespace GlideWrapper;
    WrapperConfig cfg{};
    cfg.backend = "software";
    cfg.model = CardModel::Voodoo5;
    cfg.forceNoWindow = true;
    SoftwareBackend be;
    REQUIRE(be.Initialize(cfg));
    REQUIRE(be.AttachWindow(nullptr, 16u, 16u, /*windowed=*/true));
    be.SetPixelFormat(1u);  // ABGR (R=bits 0..7)
    be.SetClipWindow(0u, 0u, 16u, 16u);
    be.SetDitherMode(0u);
    be.SetDepthState(0u, 7u, false, 0);
    be.SetColorMaskExt(true, true, true, true);
    be.SetBlendState(4u, 0u, 4u, 0u);
    be.ClearBuffer(0xFF000000u, 0xFFu, 1.0f, 3u);

    // No texture bound on TMU0 or TMU1; TMU0 ext combiner returns TMU_CCOLOR
    // (0x11 = GR_CMBX_TMU_CCOLOR) using bit-packed formula (A + 0) * 1 + 0.
    be.SetTmuConstantColor(0u, 0xFFAA4020u);  // ABGR: R=0x20, G=0x40, B=0xAA
    be.SetTexColorCombineExt(
        0u, IGraphicsBackend::PackCombineExt(
                0x11u /* GR_CMBX_TMU_CCOLOR */, 1u /* MODE_X */,
                0x00u /* ZERO */, 0u /* MODE_ZERO */, 0x00u /* ZERO */,
                true /* invert -> 1.0 */, 0x00u /* ZERO */, false, 0u, false));
    be.SetCombinerMode(3u /* SCALE_OTHER */, 8u /* ONE */, 0u, 1u /* TEX */,
                       false, 1u, 0u, 0u, 0u, false);

    ModernVertex pt{};
    pt.pos = {8.0f, 8.0f, 0.5f, 1.0f};
    pt.color = {1.0f, 1.0f, 1.0f, 1.0f};
    be.DrawPoint(pt);
    be.FlushBins();

    std::vector<uint32_t> fb(16u * 16u, 0u);
    REQUIRE(be.ReadLFB(1u, 0u, 0u, 16u, 16u, 16u * 4u, fb.data()));
    const uint32_t px = fb[8u * 16u + 8u];
    const int r = static_cast<int>(px & 0xFFu);
    const int g = static_cast<int>((px >> 8) & 0xFFu);
    const int b = static_cast<int>((px >> 16) & 0xFFu);
    CHECK(std::abs(r - 0x20) <= 2);
    CHECK(std::abs(g - 0x40) <= 2);
    CHECK(std::abs(b - 0xAA) <= 2);

    be.DetachWindow();
    be.Shutdown();
  }
}

