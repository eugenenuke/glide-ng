#ifndef PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC512_H_
#define PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC512_H_

#include <immintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef __AVX512F__

class Simd16f;
class Simd16i;
class SimdMask16;

// ============================================================================
// AVX-512 Path: Native 512-bit ZMM registers + 16-bit opmask registers (k1..k7)
// Processes 4x4 pixel tiles (16 pixels per iteration) on Zen 4 / AVX-512 CPUs.
// ============================================================================

class SimdMask16 {
 public:
  __mmask16 m;

  inline SimdMask16() = default;
  inline explicit SimdMask16(__mmask16 mask) : m(mask) {}

  static inline SimdMask16 True() {
    return SimdMask16(static_cast<__mmask16>(0xFFFFu));
  }

  static inline SimdMask16 False() {
    return SimdMask16(static_cast<__mmask16>(0u));
  }

  inline SimdMask16 operator&(const SimdMask16& other) const {
    return SimdMask16(static_cast<__mmask16>(m & other.m));
  }

  inline SimdMask16 operator|(const SimdMask16& other) const {
    return SimdMask16(static_cast<__mmask16>(m | other.m));
  }

  inline SimdMask16 operator^(const SimdMask16& other) const {
    return SimdMask16(static_cast<__mmask16>(m ^ other.m));
  }

  inline SimdMask16 operator!() const {
    return SimdMask16(static_cast<__mmask16>(~m));
  }

  inline SimdMask16 operator~() const {
    return SimdMask16(static_cast<__mmask16>(~m));
  }

  inline bool AllZero() const { return m == 0u; }
  inline bool AnyTrue() const { return m != 0u; }
  inline bool AllTrue() const { return m == static_cast<__mmask16>(0xFFFFu); }
  inline int MoveMask() const { return static_cast<int>(m); }

  static inline SimdMask16 FromBitmask(int bits) {
    return SimdMask16(static_cast<__mmask16>(bits & 0xFFFF));
  }
};

class Simd16f {
 public:
  __m512 v;

  inline Simd16f() = default;
  inline explicit Simd16f(__m512 val) : v(val) {}
  inline explicit Simd16f(float val) : v(_mm512_set1_ps(val)) {}
  inline Simd16f(float f0, float f1, float f2, float f3, float f4, float f5,
                 float f6, float f7, float f8, float f9, float f10, float f11,
                 float f12, float f13, float f14, float f15)
      : v(_mm512_setr_ps(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
                         f13, f14, f15)) {}

  static inline Simd16f Load(const float* ptr) {
    return Simd16f(_mm512_loadu_ps(ptr));
  }

  static inline Simd16f LoadAligned(const float* ptr) {
    return Simd16f(_mm512_load_ps(ptr));
  }

  static inline void Store(float* ptr, const Simd16f& val) {
    _mm512_storeu_ps(ptr, val.v);
  }

  static inline void StoreAligned(float* ptr, const Simd16f& val) {
    _mm512_store_ps(ptr, val.v);
  }

  static inline Simd16f Load4x4(const float* ptr, size_t stride) {
    __m512 res = _mm512_castps128_ps512(_mm_loadu_ps(ptr));
    res = _mm512_insertf32x4(res, _mm_loadu_ps(ptr + stride), 1);
    res = _mm512_insertf32x4(res, _mm_loadu_ps(ptr + 2u * stride), 2);
    res = _mm512_insertf32x4(res, _mm_loadu_ps(ptr + 3u * stride), 3);
    return Simd16f(res);
  }

  static inline void Store4x4(float* ptr, size_t stride, const Simd16f& val) {
    _mm_storeu_ps(ptr, _mm512_castps512_ps128(val.v));
    _mm_storeu_ps(ptr + stride, _mm512_extractf32x4_ps(val.v, 1));
    _mm_storeu_ps(ptr + 2u * stride, _mm512_extractf32x4_ps(val.v, 2));
    _mm_storeu_ps(ptr + 3u * stride, _mm512_extractf32x4_ps(val.v, 3));
  }

  static inline Simd16f Convert(const Simd16i& val);
  static inline Simd16f Gather(const float* basePtr, const Simd16i& offsets);

  inline Simd16f operator+(const Simd16f& other) const {
    return Simd16f(_mm512_add_ps(v, other.v));
  }

  inline Simd16f operator-(const Simd16f& other) const {
    return Simd16f(_mm512_sub_ps(v, other.v));
  }

  inline Simd16f operator*(const Simd16f& other) const {
    return Simd16f(_mm512_mul_ps(v, other.v));
  }

  inline Simd16f operator/(const Simd16f& other) const {
    return Simd16f(_mm512_div_ps(v, other.v));
  }

  inline Simd16f& operator+=(const Simd16f& other) {
    v = _mm512_add_ps(v, other.v);
    return *this;
  }

  inline Simd16f& operator-=(const Simd16f& other) {
    v = _mm512_sub_ps(v, other.v);
    return *this;
  }

  inline Simd16f& operator*=(const Simd16f& other) {
    v = _mm512_mul_ps(v, other.v);
    return *this;
  }

  inline Simd16f& operator/=(const Simd16f& other) {
    v = _mm512_div_ps(v, other.v);
    return *this;
  }

  friend inline Simd16f Min(const Simd16f& a, const Simd16f& b) {
    return Simd16f(_mm512_min_ps(a.v, b.v));
  }

  friend inline Simd16f Max(const Simd16f& a, const Simd16f& b) {
    return Simd16f(_mm512_max_ps(a.v, b.v));
  }

  friend inline Simd16f Fma(const Simd16f& a, const Simd16f& b,
                            const Simd16f& c) {
    return Simd16f(_mm512_fmadd_ps(a.v, b.v, c.v));
  }

  friend inline Simd16f Floor(const Simd16f& val) {
    return Simd16f(
        _mm512_roundscale_ps(val.v, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
  }

  friend inline Simd16f Abs(const Simd16f& val) {
    const __m512i mask = _mm512_set1_epi32(0x7FFFFFFF);
    return Simd16f(_mm512_castsi512_ps(
        _mm512_and_si512(_mm512_castps_si512(val.v), mask)));
  }
};

inline SimdMask16 operator==(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_EQ_OQ));
}
inline SimdMask16 operator!=(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_NEQ_OQ));
}
inline SimdMask16 operator<(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_LT_OQ));
}
inline SimdMask16 operator<=(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_LE_OQ));
}
inline SimdMask16 operator>(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_GT_OQ));
}
inline SimdMask16 operator>=(const Simd16f& a, const Simd16f& b) {
  return SimdMask16(_mm512_cmp_ps_mask(a.v, b.v, _CMP_GE_OQ));
}

class Simd16i {
 public:
  __m512i v;

  inline Simd16i() = default;
  inline explicit Simd16i(__m512i val) : v(val) {}
  inline explicit Simd16i(int32_t val) : v(_mm512_set1_epi32(val)) {}
  inline Simd16i(int32_t i0, int32_t i1, int32_t i2, int32_t i3, int32_t i4,
                 int32_t i5, int32_t i6, int32_t i7, int32_t i8, int32_t i9,
                 int32_t i10, int32_t i11, int32_t i12, int32_t i13,
                 int32_t i14, int32_t i15)
      : v(_mm512_setr_epi32(i0, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11,
                            i12, i13, i14, i15)) {}

  static inline Simd16i Load(const int32_t* ptr) {
    return Simd16i(_mm512_loadu_si512(reinterpret_cast<const void*>(ptr)));
  }

  static inline Simd16i LoadAligned(const int32_t* ptr) {
    return Simd16i(_mm512_load_si512(reinterpret_cast<const void*>(ptr)));
  }

  static inline void Store(int32_t* ptr, const Simd16i& val) {
    _mm512_storeu_si512(reinterpret_cast<void*>(ptr), val.v);
  }

  static inline void StoreAligned(int32_t* ptr, const Simd16i& val) {
    _mm512_store_si512(reinterpret_cast<void*>(ptr), val.v);
  }

  static inline Simd16i Load4x4(const uint32_t* ptr, size_t stride) {
    __m512i res = _mm512_castsi128_si512(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr)));
    res = _mm512_inserti32x4(
        res, _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + stride)),
        1);
    res = _mm512_inserti32x4(
        res,
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + 2u * stride)),
        2);
    res = _mm512_inserti32x4(
        res,
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + 3u * stride)),
        3);
    return Simd16i(res);
  }

  static inline void Store4x4(uint32_t* ptr, size_t stride,
                              const Simd16i& val) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr),
                     _mm512_castsi512_si128(val.v));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr + stride),
                     _mm512_extracti32x4_epi32(val.v, 1));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr + 2u * stride),
                     _mm512_extracti32x4_epi32(val.v, 2));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr + 3u * stride),
                     _mm512_extracti32x4_epi32(val.v, 3));
  }

  static inline Simd16i LoadStencil4x4(const uint8_t* ptr, size_t stride) {
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    std::memcpy(&r0, ptr, 4);
    std::memcpy(&r1, ptr + stride, 4);
    std::memcpy(&r2, ptr + 2u * stride, 4);
    std::memcpy(&r3, ptr + 3u * stride, 4);
    const __m128i bytes = _mm_setr_epi32(
        static_cast<int>(r0), static_cast<int>(r1), static_cast<int>(r2),
        static_cast<int>(r3));
    return Simd16i(_mm512_cvtepu8_epi32(bytes));
  }

  static inline void StoreStencil4x4(uint8_t* ptr, size_t stride,
                                     const Simd16i& val) {
    alignas(16) uint32_t rows[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(rows),
                    _mm512_cvtepi32_epi8(val.v));
    std::memcpy(ptr, &rows[0], 4);
    std::memcpy(ptr + stride, &rows[1], 4);
    std::memcpy(ptr + 2u * stride, &rows[2], 4);
    std::memcpy(ptr + 3u * stride, &rows[3], 4);
  }

  static inline Simd16i Convert(const Simd16f& val) {
    return Simd16i(_mm512_cvttps_epi32(val.v));
  }

  static inline Simd16i CastFromFloat(const Simd16f& val) {
    return Simd16i(_mm512_castps_si512(val.v));
  }

  inline Simd16i operator+(const Simd16i& other) const {
    return Simd16i(_mm512_add_epi32(v, other.v));
  }

  inline Simd16i operator-(const Simd16i& other) const {
    return Simd16i(_mm512_sub_epi32(v, other.v));
  }

  inline Simd16i operator*(const Simd16i& other) const {
    return Simd16i(_mm512_mullo_epi32(v, other.v));
  }

  inline Simd16i& operator+=(const Simd16i& other) {
    v = _mm512_add_epi32(v, other.v);
    return *this;
  }

  inline Simd16i& operator-=(const Simd16i& other) {
    v = _mm512_sub_epi32(v, other.v);
    return *this;
  }

  inline Simd16i& operator*=(const Simd16i& other) {
    v = _mm512_mullo_epi32(v, other.v);
    return *this;
  }

  inline Simd16i operator&(const Simd16i& other) const {
    return Simd16i(_mm512_and_si512(v, other.v));
  }

  inline Simd16i operator|(const Simd16i& other) const {
    return Simd16i(_mm512_or_si512(v, other.v));
  }

  inline Simd16i operator^(const Simd16i& other) const {
    return Simd16i(_mm512_xor_si512(v, other.v));
  }

  inline Simd16i operator~() const {
    return Simd16i(_mm512_xor_si512(v, _mm512_set1_epi32(-1)));
  }

  inline Simd16i& operator&=(const Simd16i& other) {
    v = _mm512_and_si512(v, other.v);
    return *this;
  }

  inline Simd16i& operator|=(const Simd16i& other) {
    v = _mm512_or_si512(v, other.v);
    return *this;
  }

  inline Simd16i& operator^=(const Simd16i& other) {
    v = _mm512_xor_si512(v, other.v);
    return *this;
  }

  inline Simd16i operator<<(int count) const {
    return Simd16i(_mm512_slli_epi32(v, static_cast<unsigned int>(count)));
  }

  inline Simd16i operator>>(int count) const {
    return Simd16i(_mm512_srai_epi32(v, static_cast<unsigned int>(count)));
  }

  inline Simd16i Srl(int count) const {
    return Simd16i(_mm512_srli_epi32(v, static_cast<unsigned int>(count)));
  }

  inline Simd16i Srlv(const Simd16i& counts) const {
    return Simd16i(_mm512_srlv_epi32(v, counts.v));
  }

  friend inline Simd16i Min(const Simd16i& a, const Simd16i& b) {
    return Simd16i(_mm512_min_epi32(a.v, b.v));
  }

  friend inline Simd16i Max(const Simd16i& a, const Simd16i& b) {
    return Simd16i(_mm512_max_epi32(a.v, b.v));
  }

  static inline Simd16i Gather(const uint32_t* basePtr,
                               const Simd16i& offsets) {
    return Simd16i(_mm512_i32gather_epi32(offsets.v, basePtr, 4));
  }
};

inline Simd16f Simd16f::Convert(const Simd16i& val) {
  return Simd16f(_mm512_cvtepi32_ps(val.v));
}

inline Simd16f Simd16f::Gather(const float* basePtr, const Simd16i& offsets) {
  return Simd16f(_mm512_i32gather_ps(offsets.v, basePtr, 4));
}

inline SimdMask16 operator==(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_EQ));
}
inline SimdMask16 operator!=(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_NE));
}
inline SimdMask16 operator<(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_LT));
}
inline SimdMask16 operator<=(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_LE));
}
inline SimdMask16 operator>(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_NLE));
}
inline SimdMask16 operator>=(const Simd16i& a, const Simd16i& b) {
  return SimdMask16(_mm512_cmp_epi32_mask(a.v, b.v, _MM_CMPINT_NLT));
}

inline Simd16f Select(const SimdMask16& mask, const Simd16f& trueVal,
                      const Simd16f& falseVal) {
  return Simd16f(_mm512_mask_blend_ps(mask.m, falseVal.v, trueVal.v));
}

inline Simd16i Select(const SimdMask16& mask, const Simd16i& trueVal,
                      const Simd16i& falseVal) {
  return Simd16i(_mm512_mask_blend_epi32(mask.m, falseVal.v, trueVal.v));
}

// ============================================================================
// Precomputed 4x4 Dither Vector Tables (entire 4x4 matrix in one ZMM!)
// ============================================================================
alignas(64) inline const int32_t kDitherMatrix4x4_16[4][16] = {
    {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5},
    {12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5, 0, 8, 2, 10},
    {3, 11, 1, 9, 15, 7, 13, 5, 0, 8, 2, 10, 12, 4, 14, 6},
    {15, 7, 13, 5, 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9},
};

alignas(64) inline const int32_t kDitherMatrix2x2_16[4][16] = {
    {2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6},
    {14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10},
    {2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6},
    {14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10},
};

static inline Simd16i GetDitherVector4x4_16(int y) {
  return Simd16i::LoadAligned(&kDitherMatrix4x4_16[y & 3][0]);
}

static inline Simd16i GetDitherVector2x2_16(int y) {
  return Simd16i::LoadAligned(&kDitherMatrix2x2_16[y & 3][0]);
}

#endif  // __AVX512F__

#endif  // PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC512_H_
