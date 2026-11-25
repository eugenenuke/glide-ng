// Auto-modularized Glide 2.x Catch2 test suite unit.
#include <dlfcn.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "core/BackendManager.h"
#include "core/IniConfigLoader.h"
#include "core/LfbManager.h"
#include "core/Logger.h"
#include "core/MathUtils.h"
#include "core/SimdDispatch.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/VertexLayout.h"
#include "core/WrapperConfig.h"
#include "software/SoftwareBackend.h"
#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include <glide.h>
#include "common/test_helpers.h"

namespace {
[[maybe_unused]] inline void* OpenGlide2Handle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideSelfHandle(flags);
}
[[maybe_unused]] inline void* OpenGlide2DsoHandle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideLibrary("libglide2x.so", flags);
}
[[maybe_unused]] inline void* OpenGlide3DsoHandle(int flags = RTLD_LAZY) {
  return GlideTestHelpers::OpenGlideLibrary("libglide3x.so", flags);
}
}  // namespace

TEST_CASE("Glide 2.x: LFB Access, 1024-px Stride & Aperture Safety", "[Glide2][LFB]") {
  SECTION("Direct LFB read/write access") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  using GlideInitFunc = void (*)(void);
  using SstSelectFunc = void (*)(int);
  using WinOpenFunc = FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                                 GrColorFormat_t, GrOriginLocation_t, int, int);
  using BufferClearFunc = void (*)(GrColor_t, GrAlpha_t, FxU16);
  using LfbReadRegionFunc =
      FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*);
  using GlideShutdownFunc = void (*)(void);

  using LfbLockFunc = FxBool (*)(GrLock_t, GrBuffer_t, GrLfbWriteMode_t,
                                 GrOriginLocation_t, FxBool, GrLfbInfo_t*);
  using LfbUnlockFunc = FxBool (*)(GrLock_t, GrBuffer_t);
  using LfbWriteRegionFunc = FxBool (*)(GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t,
                                        FxU32, FxU32, FxI32, void*);
  using DitherModeFunc = void (*)(GrDitherMode_t);

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
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  SECTION("SDK parity (11): LFB Direct Lock & Copy") {
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
    FxBool lockWriteOk =
        lfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565,
                GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
    REQUIRE(lockWriteOk == FXTRUE);
    REQUIRE(info.lfbPtr != nullptr);

    int startX = 100;
    int startY = 100;

    // Write image directly to LFB
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        uint16_t* pixel =
            (uint16_t*)(((char*)info.lfbPtr) +
                        (y + startY) * info.strideInBytes + (x + startX) * 2);
        *pixel = colorBuf[y * 64 + x];
      }
    }
    lfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);

    // Lock back buffer for reading
    FxBool lockReadOk =
        lfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                GR_ORIGIN_UPPER_LEFT, FXFALSE, &info);
    REQUIRE(lockReadOk == FXTRUE);

    // Grab image back from LFB
    std::vector<uint16_t> grabBuf(64 * 64, 0);
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        uint16_t* pixel =
            (uint16_t*)(((char*)info.lfbPtr) +
                        (y + startY) * info.strideInBytes + (x + startX) * 2);
        grabBuf[y * 64 + x] = *pixel;
      }
    }
    lfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER);

    // Assert byte-for-byte identity
    REQUIRE(std::memcmp(colorBuf.data(), grabBuf.data(), colorBuf.size() * 2) ==
            0);
  }

  SECTION("SDK parity (26): LFB Region Copying & Formats") {
    ditherMode(GR_DITHER_DISABLE);

    // Generate a 256x256 procedural 565 source pattern in memory
    std::vector<uint16_t> textureData(256 * 256);
    for (int y = 0; y < 256; y++) {
      for (int x = 0; x < 256; x++) {
        uint8_t red = x;
        uint8_t grn = y;
        uint8_t blu = x + y;
        textureData[y * 256 + x] =
            ((red & 0xF8) << 8) | ((grn & 0xFC) << 3) | ((blu & 0xF8) >> 3);
      }
    }

    // Helper function for converting our procedural texture to various formats
    auto imageConvert = [](void* dst, void* src, GrLfbSrcFmt_t format,
                           uint32_t* outBpp) {
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
              shortColor = ((0x8000) | ((shortColor >> 1) & 0x7C00) |
                            ((shortColor >> 1) & 0x03E0) | (shortColor & 0x1F));
              shortData[y * 256 + x] = shortColor;
              break;
            case GR_LFB_SRC_FMT_888:
            case GR_LFB_SRC_FMT_8888:
              longColor = ((0xFF000000) | ((longColor << 8) & 0x00F80000) |
                           ((longColor << 5) & 0x0000FC00) |
                           ((longColor << 3) & 0x000000F8));
              longData[y * 256 + x] = longColor;
              break;
            case GR_LFB_SRC_FMT_565_DEPTH:
              longData[y * 256 + x] = longColor;
              break;
            case GR_LFB_SRC_FMT_1555_DEPTH:
            case GR_LFB_SRC_FMT_555_DEPTH:
              longColor =
                  ((0x00008000) | ((longColor >> 1) & 0x00007C00) |
                   ((longColor >> 1) & 0x000003E0) | (longColor & 0x0000001F));
              longData[y * 256 + x] = longColor;
              break;
            default:
              break;
          }
        }
      }
    };

    const GrLfbSrcFmt_t validFormats[] = {
        GR_LFB_SRC_FMT_565,       GR_LFB_SRC_FMT_555,
        GR_LFB_SRC_FMT_1555,      GR_LFB_SRC_FMT_888,
        GR_LFB_SRC_FMT_8888,      GR_LFB_SRC_FMT_565_DEPTH,
        GR_LFB_SRC_FMT_555_DEPTH, GR_LFB_SRC_FMT_1555_DEPTH};

    std::vector<uint32_t> imageBuffer(256 * 256);

    for (auto sourceFormat : validFormats) {
      std::memset(imageBuffer.data(), 0, imageBuffer.size() * 4);
      uint32_t bpp = 0;
      imageConvert(imageBuffer.data(), textureData.data(), sourceFormat, &bpp);

      bufferClear(0x00303030, 0, 0);

      FxBool writeOk = lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, sourceFormat,
                                      256, 256, 256 * bpp, imageBuffer.data());
      REQUIRE(writeOk == FXTRUE);

      if (sourceFormat == GR_LFB_SRC_FMT_565) {
        std::vector<uint16_t> readImage(256 * 256, 0xFFFF);
        FxBool readOk = readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 256, 256, 256 * 2,
                                readImage.data());
        REQUIRE(readOk == FXTRUE);

        REQUIRE(std::memcmp(imageBuffer.data(), readImage.data(),
                            256 * 256 * 2) == 0);
      }
    }
  }

  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("SST-1/CVG fixed 1024-px stride & guTexMemReset filter/clamp persistence") {
#ifdef __linux__
  const char* prevBackend = std::getenv("GLIDE_NG_BACKEND");
  std::string savedBackend = prevBackend ? prevBackend : "";
  const bool hadPrevBackend = (prevBackend != nullptr);

  ::setenv("GLIDE_NG_FORCE_NO_WINDOW", "1", 1);
  ::setenv("GLIDE_NG_SPLASH", "0", 1);

  SECTION("grLfbLock exposes authentic 1024-pixel SST-1/CVG stride at 640x480") {
    ::setenv("GLIDE_NG_BACKEND", "software", 1);
    grGlideInit();
    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                         1) == FXTRUE);
    grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

    // 16-bit write mode (RGB565): fixed stride = 1024 * 2 = 2048 bytes.
    GrLfbInfo_t info16{};
    info16.size = sizeof(GrLfbInfo_t);
    REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                      &info16) == FXTRUE);
    REQUIRE(info16.lfbPtr != nullptr);
    CHECK(info16.strideInBytes == 2048u);

    // Write using hardcoded y * 1024 + x indexing all the way to bottom row
    // y = 479 without relying on info16.strideInBytes (mirrors 1997 Glide 2.x
    // Programming Guide callers such as Extreme Assault).
    auto* fb16 = static_cast<uint16_t*>(info16.lfbPtr);
    for (uint32_t y = 0; y < 480; ++y) {
      fb16[y * 1024u + 0u] = 0xF800u;    // Red left border
      fb16[y * 1024u + 639u] = 0x07E0u;  // Green right border
    }
    fb16[479u * 1024u + 320u] = 0x001Fu;  // Blue bottom-center pixel
    REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    grBufferSwap(0);
    uint32_t pxLeft = 0, pxRight = 0, pxBottom = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 479, 1, 1, 4, &pxLeft) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 639, 479, 1, 1, 4,
                            &pxRight) == FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 479, 1, 1, 4,
                            &pxBottom) == FXTRUE);
    CHECK(((pxLeft >> 16) & 0xFFu) >= 240);
    CHECK(((pxLeft >> 8) & 0xFFu) <= 15);
    CHECK(((pxRight >> 8) & 0xFFu) >= 240);
    CHECK((pxRight & 0xFFu) <= 15);
    CHECK((pxBottom & 0xFFu) >= 240);
    CHECK(((pxBottom >> 16) & 0xFFu) <= 15);

    // 32-bit write mode (8888): fixed stride = 1024 * 4 = 4096 bytes.
    GrLfbInfo_t info32{};
    info32.size = sizeof(GrLfbInfo_t);
    REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                      &info32) == FXTRUE);
    REQUIRE(info32.lfbPtr != nullptr);
    CHECK(info32.strideInBytes == 4096u);
    auto* fb32 = static_cast<uint32_t*>(info32.lfbPtr);
    fb32[479u * 1024u + 100u] = 0xFFFF00FFu;
    REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

    grSstWinClose();
    grGlideShutdown();
  }

  SECTION(
      "guTexMemReset preserves TMU bilinear filter and clamp register state") {
    for (const char* backendName : {"software", "vulkan", "opengl_es"}) {
      ::setenv("GLIDE_NG_BACKEND", backendName, 1);
      grGlideInit();
      grSstSelect(0);
      if (grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXFALSE) {
        grGlideShutdown();
        continue;
      }

      // Configure TMU0 via guTexSource(0) with bilinear + clamp, then call
      // guTexMemReset() just like Extreme Assault does before switching to
      // direct grTexDownloadMipMap() + grTexSource().
      GrMipMapId_t probeId = guTexAllocateMemory(
          GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565,
          GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16, GR_ASPECT_1x1,
          GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP,
          GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR, 0.0f, FXFALSE);
      REQUIRE(probeId != GR_NULL_MIPMAP_HANDLE);
      guTexSource(probeId);
      guTexMemReset();

      // Upload a 2x2 texel checkerboard via low-level grTexDownloadMipMap
      // (left column black 0x0000, right column white 0xFFFF) stored inside a
      // 16x16 RGB565 texture (left half x<8 black, right half x>=8 white).
      std::vector<uint16_t> tex(16 * 16, 0x0000u);
      for (int y = 0; y < 16; ++y) {
        for (int x = 8; x < 16; ++x) {
          tex[static_cast<size_t>(y) * 16 + x] = 0xFFFFu;
        }
      }
      GrTexInfo tInfo{};
      tInfo.smallLod = GR_LOD_16;
      tInfo.largeLod = GR_LOD_16;
      tInfo.aspectRatio = GR_ASPECT_1x1;
      tInfo.format = GR_TEXFMT_RGB_565;
      tInfo.data = tex.data();
      const FxU32 addr = grTexMinAddress(GR_TMU0);
      grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &tInfo);
      grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &tInfo);
      grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                   FXFALSE);
      grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
      grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
      grCullMode(GR_CULL_DISABLE);
      grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

      // Draw a wide magnified quad spanning [0..320] x [0..160] mapped tightly
      // across the black->white seam between texel 7 and texel 8.
      // With BILINEAR preserved, pixels inside the transition band contain
      // smooth intermediate grays (40..215). With broken POINT_SAMPLED reset,
      // every single pixel snaps to either 0 or 255!
      auto mkV = [](float x, float y, float s, float t) {
        GrVertex v{};
        v.x = x;
        v.y = y;
        v.oow = 1.0f;
        v.tmuvtx[0].sow = s;
        v.tmuvtx[0].tow = t;
        return v;
      };
      GrVertex q[4] = {
          mkV(0.0f, 0.0f, 0.0f, 0.0f),
          mkV(320.0f, 0.0f, 255.0f, 0.0f),
          mkV(320.0f, 160.0f, 255.0f, 255.0f),
          mkV(0.0f, 160.0f, 0.0f, 255.0f),
      };
      grDrawPolygonVertexList(4, q);

      // Draw a second quad at [0..160] x [200..300] sampling s in [256..512]
      // (outside [0..255]!). With CLAMP preserved, every texel remains WHITE
      // (~255). If guTexMemReset wiped clamp to WRAP, left half would wrap to
      // BLACK (0).
      GrVertex qc[4] = {
          mkV(0.0f, 200.0f, 260.0f, 128.0f),
          mkV(160.0f, 200.0f, 380.0f, 128.0f),
          mkV(160.0f, 300.0f, 380.0f, 128.0f),
          mkV(0.0f, 300.0f, 260.0f, 128.0f),
      };
      grDrawPolygonVertexList(4, qc);
      grBufferSwap(0);

      int intermediateCount = 0;
      for (uint32_t x = 130; x < 190; ++x) {
        uint32_t px = 0;
        REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, x, 80, 1, 1, 4, &px) ==
                FXTRUE);
        const uint32_t r = (px >> 16) & 0xFFu;
        if (r >= 40 && r <= 215) {
          ++intermediateCount;
        }
      }
      INFO("Backend=" << backendName
                      << " intermediateCount=" << intermediateCount);
      CHECK(intermediateCount >= 4);

      uint32_t pxClamp = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 40, 250, 1, 1, 4,
                              &pxClamp) == FXTRUE);
      INFO("Backend=" << backendName << " pxClamp=0x" << std::hex << pxClamp
                      << std::dec);
      CHECK(((pxClamp >> 16) & 0xFFu) >= 240);

      grSstWinClose();
      grGlideShutdown();
    }
  }

  if (hadPrevBackend) {
    ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
  }

  SECTION("Off-by-one vertical aperture guard padding") {
#if defined(HAVE_GLIDE2_WRAPPER_SYMBOLS) || !defined(_WIN32)
  const char* prevBackend = std::getenv("GLIDE_NG_BACKEND");
  const bool hadPrevBackend = (prevBackend != nullptr);
  const std::string savedBackend = hadPrevBackend ? prevBackend : "";

  ::setenv("GLIDE_NG_BACKEND", "software", 1);
  grGlideInit();
  grSstSelect(0);
  REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                       1) == FXTRUE);
  grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);

  // 16-bit write lock (RGB565, fixed 1024-pixel stride -> 2048 B/row).
  // Authentic SST-1/CVG exposes at least 1024 rows (2 MiB @ 16-bit). Guest code
  // with off-by-one LFB blit loops (e.g. y == 480) or full-aperture clears up
  // to y == 1023 must NOT corrupt adjacent heap chunks or visible scanlines.
  GrLfbInfo_t info16{};
  info16.size = sizeof(GrLfbInfo_t);
  REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXTRUE,
                    &info16) == FXTRUE);
  REQUIRE(info16.lfbPtr != nullptr);
  REQUIRE(info16.strideInBytes == 2048u);

  auto* fb16 = static_cast<uint16_t*>(info16.lfbPtr);
  // Visible bottom scanline (y = 479) and off-by-one scanline (y = 480) plus
  // the last SST-1 16-bit aperture scanline (y = 1023).
  for (uint32_t x = 0; x < 640u; ++x) {
    fb16[479u * 1024u + x] = 0x07E0u;   // Bright green visible bottom row
    fb16[480u * 1024u + x] = 0x10A2u;   // Off-by-one row (must be absorbed)
    fb16[1023u * 1024u + x] = 0xF800u;  // Last 2 MiB aperture row
  }
  REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

  // Nested re-entrant 16-bit lock should retain pointer & aperture guard.
  GrLfbInfo_t infoNestedA{}, infoNestedB{};
  infoNestedA.size = sizeof(GrLfbInfo_t);
  infoNestedB.size = sizeof(GrLfbInfo_t);
  REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &infoNestedA) == FXTRUE);
  REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &infoNestedB) == FXTRUE);
  CHECK(infoNestedA.lfbPtr == infoNestedB.lfbPtr);
  static_cast<uint16_t*>(infoNestedB.lfbPtr)[480u * 1024u + 10u] = 0x55AAu;
  REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

  // Visible bottom scanline must survive swap intact.
  grBufferSwap(0);
  uint32_t pxVisible = 0;
  REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 479, 1, 1, 4,
                          &pxVisible) == FXTRUE);
  CHECK(((pxVisible >> 8) & 0xFFu) >= 240);
  CHECK(((pxVisible >> 16) & 0xFFu) <= 15);
  CHECK((pxVisible & 0xFFu) <= 15);

  // Read-only lock also guards trailing off-by-one reads as zero.
  GrLfbInfo_t infoRead{};
  infoRead.size = sizeof(GrLfbInfo_t);
  REQUIRE(grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &infoRead) == FXTRUE);
  REQUIRE(infoRead.lfbPtr != nullptr);
  const auto* rd16 = static_cast<const uint16_t*>(infoRead.lfbPtr);
  CHECK(rd16[480u * 1024u + 0u] == 0u);
  CHECK(rd16[1023u * 1024u + 639u] == 0u);
  REQUIRE(grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER) == FXTRUE);

  // 32-bit write lock (8888, fixed 1024-pixel stride -> 4096 B/row -> 4 MiB).
  GrLfbInfo_t info32{};
  info32.size = sizeof(GrLfbInfo_t);
  REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &info32) == FXTRUE);
  REQUIRE(info32.lfbPtr != nullptr);
  REQUIRE(info32.strideInBytes == 4096u);
  auto* fb32 = static_cast<uint32_t*>(info32.lfbPtr);
  fb32[480u * 1024u + 0u] = 0xDEADBEEFu;
  fb32[1023u * 1024u + 639u] = 0xCAFEBABEu;
  REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);

  // Churn large heap allocations matching CpuLfbStaging overlay size to prove
  // allocator metadata was not corrupted.
  std::vector<uint8_t> churnA(640u * 480u * 4u, 0x11u);
  std::vector<uint8_t> churnB(640u * 480u * 4u, 0x22u);
  CHECK(churnA.back() == 0x11u);
  CHECK(churnB.front() == 0x22u);

  grSstWinClose();
  grGlideShutdown();

  if (hadPrevBackend) {
    ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
  }

}

TEST_CASE("Glide 2.x: LFB & Linear Draw Stride Bounds Validation", "[Glide2][LFB][Bounds]") {
  SECTION("LFB readback stride validation") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void (*)(void))dlsym(handle, "grGlideInit");
  auto sstSelect = (void (*)(int))dlsym(handle, "grSstSelect");
  auto winOpen = (FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                             GrColorFormat_t, GrOriginLocation_t, int,
                             int))dlsym(handle, "grSstWinOpen");
  auto readLfb = (FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32,
                             void*))dlsym(handle, "grLfbReadRegion");
  auto shutdown = (void (*)(void))dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(readLfb != nullptr);
  REQUIRE(shutdown != nullptr);

  glideInit();
  sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // We read a 100x100 region.
  // Native/determined format is 16-bit, so required stride is 100 * 2 = 200
  // bytes.
  std::vector<uint8_t> buffer(20000, 0);

  // Test case 1: Pass an invalid stride (100 bytes, which is less than 200)
  FxBool status1 =
      readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 100, 100, 100, buffer.data());
  // It should fail and return FXFALSE to prevent overflow!
  REQUIRE(status1 == FXFALSE);

  // Test case 2: Pass a valid stride (200 bytes)
  FxBool status2 =
      readLfb(GR_BUFFER_BACKBUFFER, 0, 0, 100, 100, 200, buffer.data());
  // It should succeed and return FXTRUE!
  REQUIRE(status2 == FXTRUE);

  shutdown();
  ::dlclose(handle);
#endif
  }

  SECTION("Linear draw stride validation") {
#ifdef __linux__
  void* handle = OpenGlide2Handle();
  REQUIRE(handle != nullptr);

  auto glideInit = (void (*)(void))dlsym(handle, "grGlideInit");
  auto sstSelect = (void (*)(int))dlsym(handle, "grSstSelect");
  auto winOpen = (FxBool (*)(FxU32, GrScreenResolution_t, GrScreenRefresh_t,
                             GrColorFormat_t, GrOriginLocation_t, int,
                             int))dlsym(handle, "grSstWinOpen");
  auto drawVertexArrayLinear = (void (*)(FxU32, FxU32, void*, FxU32))dlsym(
      handle, "grDrawVertexArrayLinear");
  auto shutdown = (void (*)(void))dlsym(handle, "grGlideShutdown");

  REQUIRE(glideInit != nullptr);
  REQUIRE(sstSelect != nullptr);
  REQUIRE(winOpen != nullptr);
  REQUIRE(drawVertexArrayLinear != nullptr);
  REQUIRE(shutdown != nullptr);

  glideInit();
  sstSelect(0);
  FxBool openOk = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
  REQUIRE(openOk == FXTRUE);

  // Set a custom layout to simulate active offsets
  auto& layout = GlideWrapper::VertexLayout::GetInstance();
  layout.ResetToGlide2Canonical();  // m_offsetXY = 0, m_offsetRGB = 12, etc.
                                    // (max offset is 48!)
  int32_t maxOffset = layout.GetMaxActiveOffset();
  REQUIRE(maxOffset > 0);

  // Create a buffer for 3 vertices.
  // Stride of 20 bytes is smaller than maxOffset (48 bytes).
  // Calling grDrawVertexArrayLinear with stride = 20 should be caught and
  // rejected!
  std::vector<uint8_t> dummyVertices(300, 0);

  // We check that this call runs without crashing!
  // (In a real legacy app, this would have crashed or corrupted memory because
  // maxOffset > 20) By returning early, the wrapper prevents the out-of-bounds
  // read!
  drawVertexArrayLinear(0, 3, dummyVertices.data(), 20);

  // Now verify that it works fine with a valid stride (64 bytes, which is >=
  // maxOffset)
  drawVertexArrayLinear(0, 3, dummyVertices.data(), 64);

  shutdown();
  ::dlclose(handle);
#endif
  }

}

TEST_CASE("Glide 2.x: Persistent LFB Write-Lock & HUD Overlay", "[Glide2][LFB][Overlay]") {
  SECTION("Persistent write-lock, 3D+HUD overlay & NOIDLE") {
#ifdef __linux__
  const char* prevNoSplash = std::getenv("FX_GLIDE_NO_SPLASH");
  const char* prevPlug = std::getenv("FX_GLIDE_SHAMELESS_PLUG");
  std::string savedNoSplash = prevNoSplash ? prevNoSplash : "";
  std::string savedPlug = prevPlug ? prevPlug : "";
  ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
  ::unsetenv("FX_GLIDE_SHAMELESS_PLUG");
  struct EnvRestorer {
    bool hadSplash;
    std::string splashVal;
    bool hadPlug;
    std::string plugVal;
    ~EnvRestorer() {
      if (hadSplash)
        ::setenv("FX_GLIDE_NO_SPLASH", splashVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_NO_SPLASH");
      if (hadPlug)
        ::setenv("FX_GLIDE_SHAMELESS_PLUG", plugVal.c_str(), 1);
      else
        ::unsetenv("FX_GLIDE_SHAMELESS_PLUG");
    }
  } restorer{prevNoSplash != nullptr, savedNoSplash, prevPlug != nullptr,
             savedPlug};

  SECTION("Persistent LFB write-lock flushes across grBufferSwap (menus)") {
    grGlideInit();
    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                         1) == FXTRUE);
    grDitherMode(GR_DITHER_DISABLE);

    // Carmageddon menu pattern: lock ONCE, draw frame 1, swap, draw frame 2,
    // swap, without calling grLfbUnlock between swaps.
    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                      &info) == FXTRUE);
    REQUIRE(info.lfbPtr != nullptr);
    REQUIRE(info.strideInBytes >= 640 * 2);

    auto* base = static_cast<uint8_t*>(info.lfbPtr);
    // Frame 1: draw a pure RED (0xF800) rectangle at x=[100..200), y=[100..150)
    for (uint32_t y = 100; y < 150; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
      for (uint32_t x = 100; x < 200; ++x) {
        row[x] = 0xF800u;
      }
    }
    grBufferSwap(0);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 150, 125, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 16) & 0xFFu) >= 240);  // R
    REQUIRE(((px >> 8) & 0xFFu) <= 10);    // G
    REQUIRE((px & 0xFFu) <= 10);           // B

    // Frame 2 (still locked! no grLfbUnlock!): clear and draw pure GREEN
    // (0x07E0) at x=[300..400), y=[200..250).
    grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
    for (uint32_t y = 200; y < 250; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
      for (uint32_t x = 300; x < 400; ++x) {
        row[x] = 0x07E0u;
      }
    }
    grBufferSwap(0);

    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 350, 225, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 16) & 0xFFu) <= 10);   // R
    REQUIRE(((px >> 8) & 0xFFu) >= 240);   // G
    REQUIRE((px & 0xFFu) <= 10);           // B

    // Frame 1's old pixels should NOT have leaked into Frame 2 (staging reset
    // to sentinel after swap).
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 150, 125, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE((px & 0x00FFFFFFu) == 0x00000000u);

    REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
    grSstWinClose();
    grGlideShutdown();
  }

  SECTION("3D geometry + persistent 2D HUD overlay + Shameless Plug") {
    ::setenv("FX_GLIDE_SHAMELESS_PLUG", "1", 1);
    grGlideInit();
    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                         1) == FXTRUE);
    grDitherMode(GR_DITHER_DISABLE);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);

    GrLfbInfo_t info{};
    info.size = sizeof(info);
    REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                      &info) == FXTRUE);
    auto* base = static_cast<uint8_t*>(info.lfbPtr);

    for (int frame = 0; frame < 2; ++frame) {
      grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
      // Draw large 3D GREEN triangle covering (320, 200) and extending into the
      // HUD/plug scanline band at (320, 425) to verify untouched pixels on the
      // same scanlines as the sparse overlay remain green.
      grConstantColorValue(0xFF00FF00);
      GrVertex a{}, b{}, c{};
      a.x = 120.0f;
      a.y = 60.0f;
      a.oow = 1.0f;
      b.x = 520.0f;
      b.y = 60.0f;
      b.oow = 1.0f;
      c.x = 320.0f;
      c.y = 460.0f;
      c.oow = 1.0f;
      grDrawTriangle(&a, &b, &c);

      // Write 2D HUD bar at y=[410..440), x=[40..240) via persistent LFB ptr:
      // frame 0 = RED (0xF800), frame 1 = BLUE (0x001F).
      const uint16_t hudColor = (frame == 0) ? 0xF800u : 0x001Fu;
      for (uint32_t y = 410; y < 440; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
        for (uint32_t x = 40; x < 240; ++x) {
          row[x] = hudColor;
        }
      }

      grBufferSwap(0);

      // 1. 3D triangle interior (both outside and inside the HUD scanline band)
      // must still be GREEN (NOT clobbered by sparse LFB overlay!).
      uint32_t px3d = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 200, 1, 1, 4,
                              &px3d) == FXTRUE);
      REQUIRE(((px3d >> 16) & 0xFFu) <= 10);
      REQUIRE(((px3d >> 8) & 0xFFu) >= 240);
      REQUIRE((px3d & 0xFFu) <= 10);

      uint32_t px3dInBand = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 320, 425, 1, 1, 4,
                              &px3dInBand) == FXTRUE);
      REQUIRE(((px3dInBand >> 16) & 0xFFu) <= 10);
      REQUIRE(((px3dInBand >> 8) & 0xFFu) >= 240);
      REQUIRE((px3dInBand & 0xFFu) <= 10);

      // 2. 2D HUD bar must be visible with the current frame's color
      uint32_t pxHud = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 140, 425, 1, 1, 4,
                              &pxHud) == FXTRUE);
      if (frame == 0) {
        REQUIRE(((pxHud >> 16) & 0xFFu) >= 240);  // R
        REQUIRE((pxHud & 0xFFu) <= 10);           // B
      } else {
        REQUIRE(((pxHud >> 16) & 0xFFu) <= 10);  // R
        REQUIRE((pxHud & 0xFFu) >= 240);         // B
      }

      // 3. Shameless Plug (lower-right 180x90) must also be non-black AND must
      // not have broken the persistent write lock!
      std::vector<uint32_t> plugRegion(180 * 90, 0);
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 640 - 180, 480 - 90, 180,
                              90, 180 * 4, plugRegion.data()) == FXTRUE);
      uint32_t nonBlackPlugPixels = 0;
      for (uint32_t p : plugRegion) {
        if ((p & 0x00FFFFFFu) != 0) ++nonBlackPlugPixels;
      }
      REQUIRE(nonBlackPlugPixels > 100);
    }

    REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
    grSstWinClose();
    grGlideShutdown();
  }

  SECTION("GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE lock/unlock writes work") {
    grGlideInit();
    grSstSelect(0);
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                         1) == FXTRUE);
    grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);

    GrLfbInfo_t info{};
    info.size = sizeof(info);
    const GrLock_t writeNoIdle = GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE;
    REQUIRE(grLfbLock(writeNoIdle, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565,
                      GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXTRUE);
    auto* base = static_cast<uint8_t*>(info.lfbPtr);
    for (uint32_t y = 50; y < 80; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
      for (uint32_t x = 50; x < 80; ++x) {
        row[x] = 0x001Fu;  // Pure BLUE
      }
    }
    REQUIRE(grLfbUnlock(writeNoIdle, GR_BUFFER_BACKBUFFER) == FXTRUE);

    uint32_t px = 0;
    REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 65, 65, 1, 1, 4, &px) ==
            FXTRUE);
    REQUIRE(((px >> 16) & 0xFFu) <= 10);
    REQUIRE(((px >> 8) & 0xFFu) <= 10);
    REQUIRE((px & 0xFFu) >= 240);

    grSstWinClose();
    grGlideShutdown();
  }
#endif
  }

  SECTION("Write-lock chronological flush before clear & draw") {
#ifdef __linux__
  ::setenv("GLIDE_NG_FORCE_NO_WINDOW", "1", 1);
  ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);
  ::setenv("GLIDE_NG_SHAMELESS_PLUG", "0", 1);

  const char* prevBackendEnv = std::getenv("GLIDE_NG_BACKEND");
  const std::string savedBackend = prevBackendEnv ? prevBackendEnv : "";
  const bool hadPrevBackend = (prevBackendEnv != nullptr);

  for (const char* backendName : {"software", "vulkan", "opengl_es"}) {
    DYNAMIC_SECTION("Backend: " << backendName) {
      ::setenv("GLIDE_NG_BACKEND", backendName, 1);

      grGlideInit();
      grSstSelect(0);
      if (grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1) !=
          FXTRUE) {
        grGlideShutdown();
        WARN("Skipping backend " << backendName
                                 << " (unavailable in this test environment)");
        continue;
      }

      // Carmageddon Voodoo Graphics mode locks BACK once at startup with
      // pixelPipeline=FXFALSE and never unlocks.
      GrLfbInfo_t lfbInfo{};
      lfbInfo.size = sizeof(lfbInfo);
      REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                        GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                        &lfbInfo) == FXTRUE);
      REQUIRE(lfbInfo.lfbPtr != nullptr);

      auto writeRect565 = [&](uint32_t x0, uint32_t y0, uint32_t x1,
                              uint32_t y1, uint16_t color565) {
        auto* base = static_cast<uint8_t*>(lfbInfo.lfbPtr);
        for (uint32_t y = y0; y < y1; ++y) {
          auto* row = reinterpret_cast<uint16_t*>(
              base + static_cast<size_t>(y) * lfbInfo.strideInBytes);
          for (uint32_t x = x0; x < x1; ++x) {
            row[x] = color565;
          }
        }
      };

      // Step 1: Write 2D map background (GREEN 0x07E0) with a solid WHITE
      // (0xFFFF) inset box at [10..150, 10..110] via the persistent LFB lock.
      writeRect565(0, 0, 640, 480, 0x07E0u);
      writeRect565(10, 10, 150, 110, 0xFFFFu);

      // Step 2: Depth-only clear (grColorMask(0, 0) + grBufferClear) — exact
      // Carmageddon map-screen sequence before rendering the 3D miniscreen.
      grColorMask(FXFALSE, FXFALSE);
      grDepthMask(FXTRUE);
      grBufferClear(0x00000000u, 0, GR_WDEPTHVALUE_FARTHEST);
      grColorMask(FXTRUE, FXFALSE);

      // Step 3: Draw a RED 3D quad covering the white inset [10..150, 10..110].
      grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
      grDepthBufferFunction(GR_CMP_LESS);
      grChromakeyMode(GR_CHROMAKEY_DISABLE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                           GR_BLEND_ZERO);
      guColorCombineFunction(GR_COLORCOMBINE_ITRGB);

      auto makeV = [](float x, float y) {
        GrVertex v{};
        v.x = x;
        v.y = y;
        v.oow = 0.5f;
        v.r = 255.0f;
        v.g = 0.0f;
        v.b = 0.0f;
        v.a = 255.0f;
        return v;
      };
      GrVertex q[4] = {
          makeV(10.0f, 10.0f),
          makeV(150.0f, 10.0f),
          makeV(150.0f, 110.0f),
          makeV(10.0f, 110.0f),
      };
      grDrawPolygonVertexList(4, q);

      // Step 4: Write a BLUE (0x001F) player-arrow marker inside the inset via
      // the SAME persistent LFB lock AFTER 3D rendering, then swap.
      writeRect565(70, 50, 90, 70, 0x001Fu);
      grBufferSwap(0);

      // Verify all three chronological layers survived in exact hardware order:
      //  - (300, 300): GREEN 2D map background (preserved across depth clear)
      //  - (30, 30):   RED 3D miniscreen (NOT clobbered by Step 1 WHITE box!)
      //  - (80, 60):   BLUE post-3D LFB player arrow (on top of RED 3D scene!)
      uint32_t pxMap = 0, pxInset3d = 0, pxArrow = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 300, 300, 1, 1, 4,
                              &pxMap) == FXTRUE);
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 30, 30, 1, 1, 4,
                              &pxInset3d) == FXTRUE);
      REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 80, 60, 1, 1, 4,
                              &pxArrow) == FXTRUE);

      INFO("Backend=" << backendName << " pxMap=0x" << std::hex << pxMap
                      << " pxInset3d=0x" << pxInset3d << " pxArrow=0x"
                      << pxArrow << std::dec);

      // Map surround = GREEN
      CHECK(((pxMap >> 16) & 0xFFu) <= 15);
      CHECK(((pxMap >> 8) & 0xFFu) >= 240);
      CHECK((pxMap & 0xFFu) <= 15);

      // Inset 3D scene = RED (not WHITE!)
      CHECK(((pxInset3d >> 16) & 0xFFu) >= 240);
      CHECK(((pxInset3d >> 8) & 0xFFu) <= 15);
      CHECK((pxInset3d & 0xFFu) <= 15);

      // Post-3D LFB marker = BLUE
      CHECK(((pxArrow >> 16) & 0xFFu) <= 15);
      CHECK(((pxArrow >> 8) & 0xFFu) <= 15);
      CHECK((pxArrow & 0xFFu) >= 240);

      // Also verify grLfbReadRegion reads pending writes from an active
      // persistent write lock on the backbuffer before swap!
      writeRect565(200, 200, 220, 220, 0xFFE0u);  // YELLOW
      uint32_t pxLiveRead = 0;
      REQUIRE(grLfbReadRegion(GR_BUFFER_BACKBUFFER, 210, 210, 1, 1, 4,
                              &pxLiveRead) == FXTRUE);
      CHECK(((pxLiveRead >> 16) & 0xFFu) >= 240);
      CHECK(((pxLiveRead >> 8) & 0xFFu) >= 240);
      CHECK((pxLiveRead & 0xFFu) <= 15);

      grSstWinClose();
      grGlideShutdown();
    }
  }

  if (hadPrevBackend) {
    ::setenv("GLIDE_NG_BACKEND", savedBackend.c_str(), 1);
  } else {
    ::unsetenv("GLIDE_NG_BACKEND");
  }
#endif
  }

}

TEST_CASE("Glide 2.x: Auxiliary Buffer Depth Readback (AUXBUFFER)", "[Glide2][LFB][Depth]") {
#ifdef __linux__
  struct EnvGuard {
    std::string key;
    bool had{false};
    std::string saved;
    explicit EnvGuard(const char* k) : key(k) {
      if (const char* v = std::getenv(k)) {
        had = true;
        saved = v;
      }
    }
    ~EnvGuard() {
      if (had) {
        ::setenv(key.c_str(), saved.c_str(), 1);
      } else {
        ::unsetenv(key.c_str());
      }
    }
  };
  EnvGuard backendGuard("GLIDE_NG_BACKEND");
  EnvGuard msaaGuard("GLIDE_NG_MSAA");

  struct Case {
    const char* backend;
    const char* msaa;
  };
  const Case cases[] = {
      {"software", "1"},
      {"vulkan", "1"},
      {"vulkan", "4"},
      {"opengl_es", "1"},
      {"opengl_es", "4"},
  };

  auto makeVtx = [](float x, float y, float ooz, float r, float g, float b) {
    GrVertex v{};
    v.x = x;
    v.y = y;
    v.z = 0.0f;
    v.ooz = ooz;
    v.oow = 1.0f;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = 255.0f;
    return v;
  };

  for (const auto& tc : cases) {
    CAPTURE(tc.backend, tc.msaa);
    ::setenv("GLIDE_NG_BACKEND", tc.backend, 1);
    ::setenv("GLIDE_NG_MSAA", tc.msaa, 1);

    grGlideInit();
    FxBool opened =
        grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                     GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(opened == FXTRUE);

    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    grDepthMask(FXTRUE);

    // 1. Inverted Z-buffer (depth cleared to 0 = far, GEQUAL — Motorhead style)
    grDepthBufferFunction(GR_CMP_GEQUAL);
    grBufferClear(0xFF000000u, 0, 0u);

    uint16_t clearedDepth = 0xBEEFu;
    REQUIRE(grLfbReadRegion(GR_BUFFER_AUXBUFFER, 320, 240, 1, 1, 2,
                            &clearedDepth) == FXTRUE);
    CHECK(clearedDepth == 0u);

    // Draw a triangle covering (160..480, 100..400) at ooz = 32768 (~0.5)
    GrVertex a = makeVtx(160.0f, 100.0f, 32768.0f, 255.0f, 0.0f, 0.0f);
    GrVertex b = makeVtx(480.0f, 100.0f, 32768.0f, 255.0f, 0.0f, 0.0f);
    GrVertex c = makeVtx(320.0f, 400.0f, 32768.0f, 255.0f, 0.0f, 0.0f);
    grDrawTriangle(&a, &b, &c);

    // Full-frame grLfbLock(READ_ONLY, AUXBUFFER) occlusion probe (unoccluded
    // sky must read 0, NOT 0xFFFF; triangle interior must read ~32768).
    GrLfbInfo_t lfb{};
    lfb.size = sizeof(GrLfbInfo_t);
    REQUIRE(grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_AUXBUFFER,
                      GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                      &lfb) == FXTRUE);
    REQUIRE(lfb.lfbPtr != nullptr);
    const auto* base = static_cast<const uint8_t*>(lfb.lfbPtr);
    const uint16_t skyZ =
        reinterpret_cast<const uint16_t*>(base + 20u * lfb.strideInBytes)[20u];
    const uint16_t triZ = reinterpret_cast<const uint16_t*>(
        base + 220u * lfb.strideInBytes)[320u];
    REQUIRE(grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_AUXBUFFER) == FXTRUE);
    CHECK(skyZ <= 4u);
    CHECK(triZ >= 32600u);
    CHECK(triZ <= 32900u);

    // Draw an additive billboard with depth write disabled; cached depth must
    // still read identical sky=0 / triangle=~32768.
    grDepthMask(FXFALSE);
    GrVertex f0 = makeVtx(10.0f, 10.0f, 60000.0f, 0.0f, 255.0f, 0.0f);
    GrVertex f1 = makeVtx(40.0f, 10.0f, 60000.0f, 0.0f, 255.0f, 0.0f);
    GrVertex f2 = makeVtx(25.0f, 40.0f, 60000.0f, 0.0f, 255.0f, 0.0f);
    grDrawTriangle(&f0, &f1, &f2);
    uint16_t skyAfterNoWrite = 0xBEEFu;
    REQUIRE(grLfbReadRegion(GR_BUFFER_AUXBUFFER, 20, 20, 1, 1, 2,
                            &skyAfterNoWrite) == FXTRUE);
    CHECK(skyAfterNoWrite <= 4u);

    // 2. Standard Z-buffer (cleared to 0xFFFF = far, LESS)
    grDepthMask(FXTRUE);
    grDepthBufferFunction(GR_CMP_LESS);
    grBufferClear(0xFF000000u, 0, 0xFFFFu);
    GrVertex s0 = makeVtx(160.0f, 100.0f, 16384.0f, 0.0f, 0.0f, 255.0f);
    GrVertex s1 = makeVtx(480.0f, 100.0f, 16384.0f, 0.0f, 0.0f, 255.0f);
    GrVertex s2 = makeVtx(320.0f, 400.0f, 16384.0f, 0.0f, 0.0f, 255.0f);
    grDrawTriangle(&s0, &s1, &s2);

    uint16_t stdSky = 0u;
    uint16_t stdTri = 0u;
    REQUIRE(grLfbReadRegion(GR_BUFFER_AUXBUFFER, 20, 20, 1, 1, 2, &stdSky) ==
            FXTRUE);
    REQUIRE(grLfbReadRegion(GR_BUFFER_AUXBUFFER, 320, 220, 1, 1, 2, &stdTri) ==
            FXTRUE);
    CHECK(stdSky >= 0xFFFBu);
    CHECK(stdTri >= 16250u);
    CHECK(stdTri <= 16520u);

    grSstWinClose();
    grGlideShutdown();
  }
#endif
}

TEST_CASE("Glide 2.x: Blended LFB Overlay Quads Cross-Backend Parity",
          "[Glide2][LFB][Blend]") {
#ifdef __linux__
  struct EnvGuard {
    std::string key;
    bool had{false};
    std::string saved;
    explicit EnvGuard(const char* k) : key(k) {
      if (const char* v = std::getenv(k)) {
        had = true;
        saved = v;
      }
    }
    ~EnvGuard() {
      if (had) {
        ::setenv(key.c_str(), saved.c_str(), 1);
      } else {
        ::unsetenv(key.c_str());
      }
    }
  };
  EnvGuard backendGuard("GLIDE_NG_BACKEND");
  EnvGuard msaaGuard("GLIDE_NG_MSAA");

  struct BackendCase {
    const char* backend;
    const char* msaa;
  };
  const BackendCase cases[] = {
      {"software", "1"},
      {"vulkan", "1"},
      {"vulkan", "4"},
      {"opengl_es", "1"},
      {"opengl_es", "4"},
  };

  auto makeTriVtx = [](float x, float y, float r, float g, float b) {
    GrVertex v{};
    v.x = x;
    v.y = y;
    v.z = 0.5f;
    v.ooz = 32768.0f;
    v.oow = 1.0f;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = 255.0f;
    return v;
  };

  auto writeBox565 = [](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                        uint16_t color565) {
    GrLfbInfo_t info{};
    info.size = sizeof(GrLfbInfo_t);
    REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXTRUE,
                      &info) == FXTRUE);
    REQUIRE(info.lfbPtr != nullptr);
    auto* base = static_cast<uint8_t*>(info.lfbPtr);
    for (uint32_t y = y0; y < y1; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
      for (uint32_t x = x0; x < x1; ++x) {
        row[x] = color565;
      }
    }
    REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
  };

  auto readPixelArgb = [](uint32_t x, uint32_t y) -> uint32_t {
    uint32_t px = 0u;
    REQUIRE(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, x, y, 1, 1, 4, &px) ==
            FXTRUE);
    return px;
  };

  auto drawBackground = [&](uint32_t clearColor) {
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grBufferClear(clearColor, 0xFFu, 0xFFFFu);
    // Large solid triangle covering [80..560, 80..400] with R=100, G=160, B=80.
    GrVertex v0 = makeTriVtx(80.0f, 80.0f, 100.0f, 160.0f, 80.0f);
    GrVertex v1 = makeTriVtx(600.0f, 80.0f, 100.0f, 160.0f, 80.0f);
    GrVertex v2 = makeTriVtx(80.0f, 440.0f, 100.0f, 160.0f, 80.0f);
    grDrawTriangle(&v0, &v1, &v2);
  };

  // Probe points: {x, y}
  struct ProbePoint {
    const char* name;
    uint32_t x;
    uint32_t y;
  };
  const ProbePoint probes[] = {
      {"untouched_clear", 20, 20},
      {"untouched_tri", 110, 110},
      {"add1_only", 135, 135},
      {"add_overlap", 175, 160},
      {"add2_only", 215, 185},
      {"add_alpha", 250, 135},
      {"mul_prefog", 135, 225},
      {"mul_prefog_chroma_hole", 145, 225},
      {"mul_src_color", 215, 225},
      {"mul_dst_color", 275, 225},
      {"screen_4_6", 135, 285},
      {"screen_6_4", 215, 285},
      {"interleaved_add_a", 135, 340},
      {"interleaved_opaque", 175, 340},
      {"interleaved_add_coal", 215, 340},
      {"interleaved_mul", 255, 340},
      {"interleaved_opaque_end", 295, 340},
  };
  constexpr size_t kNumProbes = sizeof(probes) / sizeof(probes[0]);
  std::vector<uint32_t> swRefPixels;

  for (size_t cIdx = 0; cIdx < sizeof(cases) / sizeof(cases[0]); ++cIdx) {
    const auto& tc = cases[cIdx];
    CAPTURE(tc.backend, tc.msaa);
    ::setenv("GLIDE_NG_BACKEND", tc.backend, 1);
    ::setenv("GLIDE_NG_MSAA", tc.msaa, 1);

    grGlideInit();
    REQUIRE(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                         1) == FXTRUE);

    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDitherMode(GR_DITHER_DISABLE);

    std::vector<uint32_t> actual(kNumProbes, 0u);

    // Frame A: Additive, Alpha-Additive, Multiply variants, Screen variants
    // Clear = ARGB(FF, 28, 3C, 50) = (40, 60, 80)
    drawBackground(0xFF283C50u);

    // 1) Additive (ONE, ONE): single + overlapping stacked writes before flush
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(120, 120, 190, 175, 0x318Cu);  // ~ (49, 48, 99)
    writeBox565(160, 145, 230, 200, 0x2948u);  // ~ (41, 40, 66)

    // 2) Alpha-scaled additive (SRC_ALPHA, ONE) with constantAlpha=128
    grLfbConstantAlpha(128u);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(240, 120, 270, 150, 0xF800u);  // ~ 50% of Red (128, 0, 0)

    // 3) Multiply (ZERO, PREFOG_COLOR=15) with chromakey hole at (145, 225)
    grChromakeyMode(GR_CHROMAKEY_ENABLE);
    grChromakeyValue(0xFF00FF00u);  // pure green key (expands to 0x07E0)
    grAlphaBlendFunction(GR_BLEND_ZERO, GR_BLEND_PREFOG_COLOR, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    {
      GrLfbInfo_t info{};
      info.size = sizeof(GrLfbInfo_t);
      REQUIRE(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                        GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXTRUE,
                        &info) == FXTRUE);
      auto* base = static_cast<uint8_t*>(info.lfbPtr);
      for (uint32_t y = 215; y < 245; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(base + y * info.strideInBytes);
        for (uint32_t x = 120; x < 160; ++x) {
          row[x] = (x == 145u && y == 225u) ? 0x07E0u : 0x8410u;  // key vs ~50% grey
        }
      }
      REQUIRE(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE);
    }
    grChromakeyMode(GR_CHROMAKEY_DISABLE);

    // 4) Multiply (ZERO, SRC_COLOR=2) & (DST_COLOR=2, ZERO)
    grAlphaBlendFunction(GR_BLEND_ZERO, GR_BLEND_SRC_COLOR, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(200, 215, 235, 245, 0xFC0Fu);  // ~(255, 130, 123)
    grAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(260, 215, 295, 245, 0x87E0u);  // ~(131, 255, 0)

    // 5) Screen (ONE, ONE_MINUS_SRC_COLOR=6) & (ONE_MINUS_DST_COLOR=6, ONE)
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE_MINUS_SRC_COLOR,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    writeBox565(120, 270, 160, 300, 0x4208u);  // ~ (66, 65, 66)
    grAlphaBlendFunction(GR_BLEND_ONE_MINUS_DST_COLOR, GR_BLEND_ONE,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    writeBox565(200, 270, 240, 300, 0x8410u);  // ~ (131, 130, 131)

    grBufferSwap(0);
    for (size_t i = 0; i < 12; ++i) {
      actual[i] = readPixelArgb(probes[i].x, probes[i].y);
    }

    // Frame B: Interleaved Extreme Assault sequence
    drawBackground(0xFF283C50u);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(120, 325, 155, 355, 0x318Cu);  // add A
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(160, 325, 195, 355, 0xF800u);  // opaque
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(200, 325, 235, 355, 0x2000u);  // coalesced add 1
    writeBox565(200, 325, 235, 355, 0x0400u);  // coalesced add 2
    writeBox565(200, 325, 235, 355, 0x0010u);  // coalesced add 3
    grAlphaBlendFunction(GR_BLEND_ZERO, GR_BLEND_PREFOG_COLOR, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(240, 325, 275, 355, 0x8410u);  // multiply
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                         GR_BLEND_ZERO);
    writeBox565(280, 325, 315, 355, 0x07E0u);  // opaque end
    grBufferSwap(0);

    for (size_t i = 12; i < kNumProbes; ++i) {
      actual[i] = readPixelArgb(probes[i].x, probes[i].y);
    }

    if (cIdx == 0u) {
      swRefPixels = actual;
      // Sanity-check key analytical outcomes on Software reference:
      // untouched_clear = (40, 60, 80)
      CHECK(std::abs(static_cast<int>((actual[0] >> 16) & 0xFFu) - 40) <= 1);
      // mul_prefog darkened triangle (not black 0!)
      CHECK(((actual[6] >> 16) & 0xFFu) >= 45u);
      CHECK(((actual[6] >> 8) & 0xFFu) >= 75u);
      // mul_prefog_chroma_hole still shows intact 3D triangle (R~100, G~160, B~80)
      CHECK(std::abs(static_cast<int>((actual[7] >> 16) & 0xFFu) - 100) <= 2);
      CHECK(std::abs(static_cast<int>((actual[7] >> 8) & 0xFFu) - 160) <= 2);
      // add_overlap > tri alone
      CHECK(((actual[3] >> 16) & 0xFFu) > 170u);
    } else {
      for (size_t i = 0; i < kNumProbes; ++i) {
        CAPTURE(probes[i].name, actual[i], swRefPixels[i]);
        for (int shift : {16, 8, 0}) {
          const int a = static_cast<int>((actual[i] >> shift) & 0xFFu);
          const int e = static_cast<int>((swRefPixels[i] >> shift) & 0xFFu);
          CHECK(std::abs(a - e) <= 2);
        }
      }
    }

    grSstWinClose();
    grGlideShutdown();
  }
#endif
}


