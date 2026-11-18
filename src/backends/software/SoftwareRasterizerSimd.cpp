#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends/software/SimdVec.h"
#include "backends/software/SoftwareSimdInternal.h"
#include "core/TextureManager.h"

#include "backends/software/SoftwareRasterizerSimdImpl.inl"

namespace GlideWrapper {

namespace {

struct SimdTraits8 {
  using Float = Simd8f;
  using Int = Simd8i;
  using Mask = SimdMask;

  static constexpr int kBlockW = 4;
  static constexpr int kBlockH = 2;
  static constexpr float kCenterOffsetX = 1.5f;
  static constexpr float kCenterOffsetY = 0.5f;

  static inline Mask MakeTbufMask(int32_t tm0, int32_t tm1, int32_t tm2,
                                  int32_t tm3) {
    return (Int(tm0, tm1, tm0, tm1, tm2, tm3, tm2, tm3) != Int(0));
  }

  static inline Float MakeInitX(int startX, bool collapseAa) {
    const float sx = static_cast<float>(startX);
    if (collapseAa) {
      return Float(sx + 1.0f, sx + 1.0f, sx + 3.0f, sx + 3.0f, sx + 1.0f,
                   sx + 1.0f, sx + 3.0f, sx + 3.0f);
    }
    return Float(sx + 0.5f, sx + 1.5f, sx + 2.5f, sx + 3.5f, sx + 0.5f,
                 sx + 1.5f, sx + 2.5f, sx + 3.5f);
  }

  static inline Float MakeInitY(int startY, bool collapseAa) {
    const float sy = static_cast<float>(startY);
    if (collapseAa) {
      return Float(sy + 1.0f);
    }
    return Float(sy + 0.5f, sy + 0.5f, sy + 0.5f, sy + 0.5f, sy + 1.5f,
                 sy + 1.5f, sy + 1.5f, sy + 1.5f);
  }

  static inline Float MakeCoordX(int x) {
    const float fx = static_cast<float>(x);
    return Float(fx + 0.5f, fx + 1.5f, fx + 2.5f, fx + 3.5f, fx + 0.5f,
                 fx + 1.5f, fx + 2.5f, fx + 3.5f);
  }

  static inline Float MakeCoordY(int y) {
    const float fy = static_cast<float>(y);
    return Float(fy + 0.5f, fy + 0.5f, fy + 0.5f, fy + 0.5f, fy + 1.5f,
                 fy + 1.5f, fy + 1.5f, fy + 1.5f);
  }

  static inline Int MakeIntX(int x) {
    return Int(x, x + 1, x + 2, x + 3, x, x + 1, x + 2, x + 3);
  }

  static inline Int MakeIntY(int y) {
    return Int(y, y, y, y, y + 1, y + 1, y + 1, y + 1);
  }

  static inline Int GetDither(int y, uint32_t ditherMode) {
    return (ditherMode == 2u) ? GetDitherVector4x4(y) : GetDitherVector2x2(y);
  }

  static inline Float LoadFloatBlock(const float* ptr, size_t stride) {
    return Float::Load2x4(ptr, stride);
  }

  static inline void StoreFloatBlock(float* ptr, size_t stride,
                                     const Float& val) {
    Float::Store2x4(ptr, stride, val);
  }

  static inline Int LoadColorBlock(const uint32_t* ptr, size_t stride) {
    return Int::Load2x4(ptr, stride);
  }

  static inline void StoreColorBlock(uint32_t* ptr, size_t stride,
                                     const Int& val) {
    Int::Store2x4(ptr, stride, val);
  }

  static inline Int LoadStencilBlock(const uint8_t* ptr, size_t stride) {
    return Int::LoadStencil2x4(ptr, stride);
  }

  static inline void StoreStencilBlock(uint8_t* ptr, size_t stride,
                                       const Int& val) {
    Int::StoreStencil2x4(ptr, stride, val);
  }
};

}  // namespace

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
          bool Clockwise>
void SoftwareBackend::RasterizeTriangleLoopsSIMD(
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
  RasterizeTriangleLoopsTraits<SimdTraits8, HasTexture, BlendEnabled,
                               DitherEnabled, Clockwise>(
      pixels, depthBuffer, stencilBuffer, static_cast<int>(m_headlessWidth),
      m_ssaaScale, a, b, c, minX, maxX, minY, maxY, state, invArea, a0, b0, c0,
      a1, b1, c1, a2, b2, c2);
}

#define GLIDE_INSTANTIATE_SIMD_LOOP(T, B, D, C)                            \
  template void SoftwareBackend::RasterizeTriangleLoopsSIMD<T, B, D, C>(   \
      const ModernVertex&, const ModernVertex&, const ModernVertex&, int,  \
      int, int, int, const RasterizerState&, float, float, float, float,   \
      float, float, float, float, float, float);

GLIDE_INSTANTIATE_SIMD_LOOP(false, false, false, true)
GLIDE_INSTANTIATE_SIMD_LOOP(false, false, false, false)
GLIDE_INSTANTIATE_SIMD_LOOP(false, false, true, true)
GLIDE_INSTANTIATE_SIMD_LOOP(false, false, true, false)
GLIDE_INSTANTIATE_SIMD_LOOP(false, true, false, true)
GLIDE_INSTANTIATE_SIMD_LOOP(false, true, false, false)
GLIDE_INSTANTIATE_SIMD_LOOP(false, true, true, true)
GLIDE_INSTANTIATE_SIMD_LOOP(false, true, true, false)
GLIDE_INSTANTIATE_SIMD_LOOP(true, false, false, true)
GLIDE_INSTANTIATE_SIMD_LOOP(true, false, false, false)
GLIDE_INSTANTIATE_SIMD_LOOP(true, false, true, true)
GLIDE_INSTANTIATE_SIMD_LOOP(true, false, true, false)
GLIDE_INSTANTIATE_SIMD_LOOP(true, true, false, true)
GLIDE_INSTANTIATE_SIMD_LOOP(true, true, false, false)
GLIDE_INSTANTIATE_SIMD_LOOP(true, true, true, true)
GLIDE_INSTANTIATE_SIMD_LOOP(true, true, true, false)

#undef GLIDE_INSTANTIATE_SIMD_LOOP

}  // namespace GlideWrapper
