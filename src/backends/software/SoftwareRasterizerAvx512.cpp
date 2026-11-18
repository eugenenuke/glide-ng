#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends/software/SimdVec512.h"
#include "backends/software/SoftwareSimdInternal.h"
#include "core/TextureManager.h"

#include "backends/software/SoftwareRasterizerSimdImpl.inl"

namespace GlideWrapper {

namespace {

struct SimdTraits16 {
  using Float = Simd16f;
  using Int = Simd16i;
  using Mask = SimdMask16;

  static constexpr int kBlockW = 4;
  static constexpr int kBlockH = 4;
  static constexpr float kCenterOffsetX = 1.5f;
  static constexpr float kCenterOffsetY = 1.5f;

  static inline Mask MakeTbufMask(int32_t tm0, int32_t tm1, int32_t tm2,
                                  int32_t tm3) {
    const uint16_t rowEven = static_cast<uint16_t>((tm0 ? 0x5u : 0u) |
                                                   (tm1 ? 0xAu : 0u));
    const uint16_t rowOdd = static_cast<uint16_t>((tm2 ? 0x5u : 0u) |
                                                  (tm3 ? 0xAu : 0u));
    const uint16_t mask16 = static_cast<uint16_t>(
        rowEven | (rowOdd << 4) | (rowEven << 8) | (rowOdd << 12));
    return Mask(static_cast<__mmask16>(mask16));
  }

  static inline Float MakeInitX(int startX, bool collapseAa) {
    const float sx = static_cast<float>(startX);
    if (collapseAa) {
      const float x0 = sx + 1.0f;
      const float x2 = sx + 3.0f;
      return Float(x0, x0, x2, x2, x0, x0, x2, x2, x0, x0, x2, x2, x0, x0, x2,
                   x2);
    }
    const float x0 = sx + 0.5f;
    const float x1 = sx + 1.5f;
    const float x2 = sx + 2.5f;
    const float x3 = sx + 3.5f;
    return Float(x0, x1, x2, x3, x0, x1, x2, x3, x0, x1, x2, x3, x0, x1, x2,
                 x3);
  }

  static inline Float MakeInitY(int startY, bool collapseAa) {
    const float sy = static_cast<float>(startY);
    if (collapseAa) {
      const float y01 = sy + 1.0f;
      const float y23 = sy + 3.0f;
      return Float(y01, y01, y01, y01, y01, y01, y01, y01, y23, y23, y23, y23,
                   y23, y23, y23, y23);
    }
    const float y0 = sy + 0.5f;
    const float y1 = sy + 1.5f;
    const float y2 = sy + 2.5f;
    const float y3 = sy + 3.5f;
    return Float(y0, y0, y0, y0, y1, y1, y1, y1, y2, y2, y2, y2, y3, y3, y3,
                 y3);
  }

  static inline Float MakeCoordX(int x) {
    const float fx = static_cast<float>(x);
    const float x0 = fx + 0.5f;
    const float x1 = fx + 1.5f;
    const float x2 = fx + 2.5f;
    const float x3 = fx + 3.5f;
    return Float(x0, x1, x2, x3, x0, x1, x2, x3, x0, x1, x2, x3, x0, x1, x2,
                 x3);
  }

  static inline Float MakeCoordY(int y) {
    const float fy = static_cast<float>(y);
    const float y0 = fy + 0.5f;
    const float y1 = fy + 1.5f;
    const float y2 = fy + 2.5f;
    const float y3 = fy + 3.5f;
    return Float(y0, y0, y0, y0, y1, y1, y1, y1, y2, y2, y2, y2, y3, y3, y3,
                 y3);
  }

  static inline Int MakeIntX(int x) {
    return Int(x, x + 1, x + 2, x + 3, x, x + 1, x + 2, x + 3, x, x + 1, x + 2,
               x + 3, x, x + 1, x + 2, x + 3);
  }

  static inline Int MakeIntY(int y) {
    return Int(y, y, y, y, y + 1, y + 1, y + 1, y + 1, y + 2, y + 2, y + 2,
               y + 2, y + 3, y + 3, y + 3, y + 3);
  }

  static inline Int GetDither(int y, uint32_t ditherMode) {
    return (ditherMode == 2u) ? GetDitherVector4x4_16(y)
                              : GetDitherVector2x2_16(y);
  }

  static inline Float LoadFloatBlock(const float* ptr, size_t stride) {
    return Float::Load4x4(ptr, stride);
  }

  static inline void StoreFloatBlock(float* ptr, size_t stride,
                                     const Float& val) {
    Float::Store4x4(ptr, stride, val);
  }

  static inline Int LoadColorBlock(const uint32_t* ptr, size_t stride) {
    return Int::Load4x4(ptr, stride);
  }

  static inline void StoreColorBlock(uint32_t* ptr, size_t stride,
                                     const Int& val) {
    Int::Store4x4(ptr, stride, val);
  }

  static inline Int LoadStencilBlock(const uint8_t* ptr, size_t stride) {
    return Int::LoadStencil4x4(ptr, stride);
  }

  static inline void StoreStencilBlock(uint8_t* ptr, size_t stride,
                                       const Int& val) {
    Int::StoreStencil4x4(ptr, stride, val);
  }
};

}  // namespace

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
          bool Clockwise>
void SoftwareBackend::RasterizeTriangleLoopsAvx512(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int minX, int maxX, int minY, int maxY, const RasterizerState& state,
    float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
    float a2, float b2, float c2) {
  auto* pixels = reinterpret_cast<uint32_t*>(
      m_cpuBuffers[state.activeRenderBufferIdx].data());
  float* depthBuffer =
      m_headlessDepthBuffer.empty() ? nullptr : m_headlessDepthBuffer.data();
  uint8_t* stencilBuffer = m_headlessStencilBuffer.empty()
                               ? nullptr
                               : m_headlessStencilBuffer.data();
  RasterizeTriangleLoopsTraits<SimdTraits16, HasTexture, BlendEnabled,
                               DitherEnabled, Clockwise>(
      pixels, depthBuffer, stencilBuffer, static_cast<int>(m_headlessWidth),
      m_ssaaScale, a, b, c, minX, maxX, minY, maxY, state, invArea, a0, b0, c0,
      a1, b1, c1, a2, b2, c2);
}

#define GLIDE_INSTANTIATE_AVX512_LOOP(T, B, D, C)                             \
  template void SoftwareBackend::RasterizeTriangleLoopsAvx512<T, B, D, C>(    \
      const ModernVertex&, const ModernVertex&, const ModernVertex&, int,     \
      int, int, int, const RasterizerState&, float, float, float, float,      \
      float, float, float, float, float, float);

GLIDE_INSTANTIATE_AVX512_LOOP(false, false, false, true)
GLIDE_INSTANTIATE_AVX512_LOOP(false, false, false, false)
GLIDE_INSTANTIATE_AVX512_LOOP(false, false, true, true)
GLIDE_INSTANTIATE_AVX512_LOOP(false, false, true, false)
GLIDE_INSTANTIATE_AVX512_LOOP(false, true, false, true)
GLIDE_INSTANTIATE_AVX512_LOOP(false, true, false, false)
GLIDE_INSTANTIATE_AVX512_LOOP(false, true, true, true)
GLIDE_INSTANTIATE_AVX512_LOOP(false, true, true, false)
GLIDE_INSTANTIATE_AVX512_LOOP(true, false, false, true)
GLIDE_INSTANTIATE_AVX512_LOOP(true, false, false, false)
GLIDE_INSTANTIATE_AVX512_LOOP(true, false, true, true)
GLIDE_INSTANTIATE_AVX512_LOOP(true, false, true, false)
GLIDE_INSTANTIATE_AVX512_LOOP(true, true, false, true)
GLIDE_INSTANTIATE_AVX512_LOOP(true, true, false, false)
GLIDE_INSTANTIATE_AVX512_LOOP(true, true, true, true)
GLIDE_INSTANTIATE_AVX512_LOOP(true, true, true, false)

#undef GLIDE_INSTANTIATE_AVX512_LOOP

}  // namespace GlideWrapper
