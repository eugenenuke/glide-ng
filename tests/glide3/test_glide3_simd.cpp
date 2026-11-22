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

TEST_CASE("SIMD: Bit-Exact Kernels, Vectorized Fog/Stipple/Stencil & Multi-ISA Parity", "[SIMD][Parity]") {
  SECTION("Shared CPU & post-process kernel bit-exactness") {
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

  SECTION("Vectorized table fog, stipple & stencil parity") {
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

      // Strip 1: Standard W-table fog (GR_FOG_WITH_TABLE_ON_W = 0x2) sweeping
      // oow across 1.2 -> 1e-6.
      be.SetFogMode(2u);
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

      // Strip 2: Multi-pass GR_FOG_ADD2 (0x202) + GR_FOG_MULT2 (0x102)
      be.SetFogMode(0x202u);
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
        be.SetFogMode(0x102u);
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
    } else if (mode == 2) {
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
    } else if (mode == 3) {
      // FOGCOORD table fog (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT = 0x1):
      // keep vertex pos[3] (oow) fixed at 1.0 so ONLY v.fog drives the table.
      uint8_t fogTbl[64];
      for (int i = 0; i < 64; ++i) {
        fogTbl[i] = static_cast<uint8_t>(((i * 37 + (i & 1 ? 120 : 10)) & 0xFF));
      }
      be.SetFogTable(fogTbl);
      be.SetFogColor(0x004090E0u);
      be.SetDepthState(0, 7, false, 0);
      be.ClearBufferExt(0x00102030u, 255u, 1.0f, 0u, 0x3u);
      be.SetFogMode(1u);
      for (int band = 0; band < 16; ++band) {
        const float y0 = static_cast<float>(band * 3);
        const float y1 = static_cast<float>((band + 1) * 3);
        const float fogL = 1.2f / static_cast<float>(1u << band);
        const float fogR =
            std::max(1e-6f, 0.45f / static_cast<float>(1u << band));
        ModernVertex v0 = makeVert(0.0f, y0, 0.5f, 1.0f, 0.9f, 0.2f, 0.1f, 1.0f);
        ModernVertex v1 =
            makeVert(static_cast<float>(W), y0, 0.5f, 1.0f, 0.1f, 0.9f, 0.3f, 1.0f);
        ModernVertex v2 =
            makeVert(static_cast<float>(W), y1, 0.5f, 1.0f, 0.2f, 0.3f, 0.9f, 1.0f);
        ModernVertex v3 = makeVert(0.0f, y1, 0.5f, 1.0f, 0.8f, 0.7f, 0.2f, 1.0f);
        v0.fog = fogL;
        v1.fog = fogR;
        v2.fog = fogR;
        v3.fog = fogL;
        be.DrawTriangle(v0, v1, v2);
        be.DrawTriangle(v0, v2, v3);
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
    for (int mode = 0; mode < 4; ++mode) {
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
        if (mode == 0 || mode == 3) {
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

  SECTION("Software rasterizer multi-ISA parity (VSA-100, dither, T-buffer)") {
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

}

