#ifndef PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC_H_
#define PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC_H_

#include <immintrin.h>
#include <cstdint>
#include <cstddef>

// Forward declarations
class Simd8f;
class Simd8i;
class SimdMask;

// ============================================================================
// SimdMask: Wraps 8-lane boolean comparison masks
// ============================================================================
class SimdMask {
public:
    __m256 v; // Wraps comparison mask as __m256 for float blending

    inline SimdMask() = default;
    inline explicit SimdMask(__m256 val) : v(val) {}
    inline explicit SimdMask(__m256i val) : v(_mm256_castsi256_ps(val)) {}

    static inline SimdMask True() {
        __m256i t = _mm256_set1_epi32(-1);
        return SimdMask(_mm256_castsi256_ps(t));
    }

    static inline SimdMask False() {
        return SimdMask(_mm256_setzero_ps());
    }

    inline SimdMask operator&(const SimdMask& other) const {
        return SimdMask(_mm256_and_ps(v, other.v));
    }

    inline SimdMask operator|(const SimdMask& other) const {
        return SimdMask(_mm256_or_ps(v, other.v));
    }

    inline SimdMask operator^(const SimdMask& other) const {
        return SimdMask(_mm256_xor_ps(v, other.v));
    }

    inline SimdMask operator!() const {
        __m256i t = _mm256_set1_epi32(-1);
        return SimdMask(_mm256_xor_ps(v, _mm256_castsi256_ps(t)));
    }

    inline SimdMask operator~() const {
        __m256i t = _mm256_set1_epi32(-1);
        return SimdMask(_mm256_xor_ps(v, _mm256_castsi256_ps(t)));
    }

    inline bool AllZero() const {
        return _mm256_movemask_ps(v) == 0;
    }

    inline bool AnyTrue() const {
        return _mm256_movemask_ps(v) != 0;
    }

    inline bool AllTrue() const {
        return _mm256_movemask_ps(v) == 0xFF;
    }

    inline __m256i AsInteger() const {
        return _mm256_castps_si256(v);
    }

    inline float operator[](size_t idx) const {
        alignas(32) float arr[8];
        _mm256_store_ps(arr, v);
        return arr[idx];
    }
};

// ============================================================================
// Simd8f: Wraps __m256 for floating-point arithmetic
// ============================================================================
class Simd8f {
public:
    __m256 v;

    inline Simd8f() = default;
    inline explicit Simd8f(__m256 val) : v(val) {}
    inline explicit Simd8f(float val) : v(_mm256_set1_ps(val)) {}
    inline Simd8f(float f0, float f1, float f2, float f3, float f4, float f5, float f6, float f7)
        : v(_mm256_setr_ps(f0, f1, f2, f3, f4, f5, f6, f7)) {}

    // Static loaders/storers
    static inline Simd8f Load(const float* ptr) {
        return Simd8f(_mm256_loadu_ps(ptr));
    }

    static inline Simd8f LoadAligned(const float* ptr) {
        return Simd8f(_mm256_load_ps(ptr));
    }

    static inline void Store(float* ptr, const Simd8f& val) {
        _mm256_storeu_ps(ptr, val.v);
    }

    static inline void StoreAligned(float* ptr, const Simd8f& val) {
        _mm256_store_ps(ptr, val.v);
    }

    // Load2x4 / Store2x4 for 2x4 pixel block rows (non-contiguous depth or float attributes)
    static inline Simd8f Load2x4(const float* ptr, size_t stride) {
        __m128 r0 = _mm_loadu_ps(ptr);
        __m128 r1 = _mm_loadu_ps(ptr + stride);
        __m256 val = _mm256_castps128_ps256(r0);
        val = _mm256_insertf128_ps(val, r1, 1);
        return Simd8f(val);
    }

    static inline void Store2x4(float* ptr, size_t stride, const Simd8f& val) {
        _mm_storeu_ps(ptr, _mm256_castps256_ps128(val.v));
        _mm_storeu_ps(ptr + stride, _mm256_extractf128_ps(val.v, 1));
    }

    // Conversion
    static inline Simd8f Convert(const Simd8i& val);

    // Arithmetic operators
    inline Simd8f operator+(const Simd8f& other) const {
        return Simd8f(_mm256_add_ps(v, other.v));
    }

    inline Simd8f operator-(const Simd8f& other) const {
        return Simd8f(_mm256_sub_ps(v, other.v));
    }

    inline Simd8f operator*(const Simd8f& other) const {
        return Simd8f(_mm256_mul_ps(v, other.v));
    }

    inline Simd8f operator/(const Simd8f& other) const {
        return Simd8f(_mm256_div_ps(v, other.v));
    }

    inline Simd8f& operator+=(const Simd8f& other) {
        v = _mm256_add_ps(v, other.v);
        return *this;
    }

    inline Simd8f& operator-=(const Simd8f& other) {
        v = _mm256_sub_ps(v, other.v);
        return *this;
    }

    inline Simd8f& operator*=(const Simd8f& other) {
        v = _mm256_mul_ps(v, other.v);
        return *this;
    }

    inline Simd8f& operator/=(const Simd8f& other) {
        v = _mm256_div_ps(v, other.v);
        return *this;
    }

    // Element access (primarily for verification and debug)
    inline float operator[](size_t idx) const {
        alignas(32) float arr[8];
        _mm256_store_ps(arr, v);
        return arr[idx];
    }

    friend inline Simd8f Min(const Simd8f& a, const Simd8f& b) {
        return Simd8f(_mm256_min_ps(a.v, b.v));
    }

    friend inline Simd8f Max(const Simd8f& a, const Simd8f& b) {
        return Simd8f(_mm256_max_ps(a.v, b.v));
    }
};

// Float comparison operators returning SimdMask
inline SimdMask operator==(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_EQ_OQ));
}
inline SimdMask operator!=(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_NEQ_OQ));
}
inline SimdMask operator<(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_LT_OQ));
}
inline SimdMask operator<=(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_LE_OQ));
}
inline SimdMask operator>(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_GT_OQ));
}
inline SimdMask operator>=(const Simd8f& a, const Simd8f& b) {
    return SimdMask(_mm256_cmp_ps(a.v, b.v, _CMP_GE_OQ));
}

// ============================================================================
// Simd8i: Wraps __m256i for integer arithmetic and bitwise operations
// ============================================================================
class Simd8i {
public:
    __m256i v;

    inline Simd8i() = default;
    inline explicit Simd8i(__m256i val) : v(val) {}
    inline explicit Simd8i(int32_t val) : v(_mm256_set1_epi32(val)) {}
    inline Simd8i(int32_t i0, int32_t i1, int32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6, int32_t i7)
        : v(_mm256_setr_epi32(i0, i1, i2, i3, i4, i5, i6, i7)) {}

    // Static loaders/storers
    static inline Simd8i Load(const int32_t* ptr) {
        return Simd8i(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr)));
    }

    static inline Simd8i LoadAligned(const int32_t* ptr) {
        return Simd8i(_mm256_load_si256(reinterpret_cast<const __m256i*>(ptr)));
    }

    static inline void Store(int32_t* ptr, const Simd8i& val) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr), val.v);
    }

    static inline void StoreAligned(int32_t* ptr, const Simd8i& val) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(ptr), val.v);
    }

    // Load2x4 / Store2x4 for 2x4 pixel block rows (non-contiguous uint32_t pixels)
    static inline Simd8i Load2x4(const uint32_t* ptr, size_t stride) {
        __m128i r0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
        __m128i r1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr + stride));
        __m256i val = _mm256_castsi128_si256(r0);
        val = _mm256_insertf128_si256(val, r1, 1);
        return Simd8i(val);
    }

    static inline void Store2x4(uint32_t* ptr, size_t stride, const Simd8i& val) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr), _mm256_castsi256_si128(val.v));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr + stride), _mm256_extractf128_si256(val.v, 1));
    }

    // Conversion
    static inline Simd8i Convert(const Simd8f& val) {
        return Simd8i(_mm256_cvttps_epi32(val.v));
    }

    // Arithmetic operators
    inline Simd8i operator+(const Simd8i& other) const {
        return Simd8i(_mm256_add_epi32(v, other.v));
    }

    inline Simd8i operator-(const Simd8i& other) const {
        return Simd8i(_mm256_sub_epi32(v, other.v));
    }

    inline Simd8i operator*(const Simd8i& other) const {
        return Simd8i(_mm256_mullo_epi32(v, other.v));
    }

    inline Simd8i& operator+=(const Simd8i& other) {
        v = _mm256_add_epi32(v, other.v);
        return *this;
    }

    inline Simd8i& operator-=(const Simd8i& other) {
        v = _mm256_sub_epi32(v, other.v);
        return *this;
    }

    inline Simd8i& operator*=(const Simd8i& other) {
        v = _mm256_mullo_epi32(v, other.v);
        return *this;
    }

    // Bitwise operators
    inline Simd8i operator&(const Simd8i& other) const {
        return Simd8i(_mm256_and_si256(v, other.v));
    }

    inline Simd8i operator|(const Simd8i& other) const {
        return Simd8i(_mm256_or_si256(v, other.v));
    }

    inline Simd8i operator^(const Simd8i& other) const {
        return Simd8i(_mm256_xor_si256(v, other.v));
    }

    inline Simd8i operator~() const {
        return Simd8i(_mm256_xor_si256(v, _mm256_set1_epi32(-1)));
    }

    inline Simd8i& operator&=(const Simd8i& other) {
        v = _mm256_and_si256(v, other.v);
        return *this;
    }

    inline Simd8i& operator|=(const Simd8i& other) {
        v = _mm256_or_si256(v, other.v);
        return *this;
    }

    inline Simd8i& operator^=(const Simd8i& other) {
        v = _mm256_xor_si256(v, other.v);
        return *this;
    }

    // Shifts
    inline Simd8i operator<<(int count) const {
        return Simd8i(_mm256_slli_epi32(v, count));
    }

    inline Simd8i operator>>(int count) const {
        return Simd8i(_mm256_srai_epi32(v, count)); // Arithmetic right shift
    }

    inline Simd8i Srl(int count) const {
        return Simd8i(_mm256_srli_epi32(v, count)); // Logical right shift
    }

    inline Simd8i& operator<<=(int count) {
        v = _mm256_slli_epi32(v, count);
        return *this;
    }

    inline Simd8i& operator>>=(int count) {
        v = _mm256_srai_epi32(v, count);
        return *this;
    }

    // Element access (primarily for verification and debug)
    inline int32_t operator[](size_t idx) const {
        alignas(32) int32_t arr[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(arr), v);
        return arr[idx];
    }

    friend inline Simd8i Min(const Simd8i& a, const Simd8i& b) {
        return Simd8i(_mm256_min_epi32(a.v, b.v));
    }

    friend inline Simd8i Max(const Simd8i& a, const Simd8i& b) {
        return Simd8i(_mm256_max_epi32(a.v, b.v));
    }
};

// Inline implementation of Simd8f::Convert
inline Simd8f Simd8f::Convert(const Simd8i& val) {
    return Simd8f(_mm256_cvtepi32_ps(val.v));
}

// Integer comparison operators returning SimdMask
inline SimdMask operator==(const Simd8i& a, const Simd8i& b) {
    return SimdMask(_mm256_castsi256_ps(_mm256_cmpeq_epi32(a.v, b.v)));
}
inline SimdMask operator>(const Simd8i& a, const Simd8i& b) {
    return SimdMask(_mm256_castsi256_ps(_mm256_cmpgt_epi32(a.v, b.v)));
}
inline SimdMask operator<(const Simd8i& a, const Simd8i& b) {
    return SimdMask(_mm256_castsi256_ps(_mm256_cmpgt_epi32(b.v, a.v)));
}
inline SimdMask operator!=(const Simd8i& a, const Simd8i& b) {
    return !(a == b);
}
inline SimdMask operator<=(const Simd8i& a, const Simd8i& b) {
    return !(a > b);
}
inline SimdMask operator>=(const Simd8i& a, const Simd8i& b) {
    return !(a < b);
}

// ============================================================================
// Select (Branchless selection mapping to _mm256_blendv_ps)
// ============================================================================
inline Simd8f Select(const SimdMask& mask, const Simd8f& trueVal, const Simd8f& falseVal) {
    return Simd8f(_mm256_blendv_ps(falseVal.v, trueVal.v, mask.v));
}

inline Simd8i Select(const SimdMask& mask, const Simd8i& trueVal, const Simd8i& falseVal) {
    return Simd8i(_mm256_castps_si256(_mm256_blendv_ps(
        _mm256_castsi256_ps(falseVal.v),
        _mm256_castsi256_ps(trueVal.v),
        mask.v
    )));
}

// ============================================================================
// Precomputed Dither Vector Tables (Row-wise 2x4 block loading)
// ============================================================================
alignas(32) inline const int32_t kDitherMatrix4x4[4][8] = {
    { 0,  8,  2, 10, 12,  4, 14,  6}, // y % 4 == 0 (Row 0: 0,8,2,10; Row 1: 12,4,14,6)
    {12,  4, 14,  6,  3, 11,  1,  9}, // y % 4 == 1 (Row 0: 12,4,14,6; Row 1: 3,11,1,9)
    { 3, 11,  1,  9, 15,  7, 13,  5}, // y % 4 == 2 (Row 0: 3,11,1,9; Row 1: 15,7,13,5)
    {15,  7, 13,  5,  0,  8,  2, 10}  // y % 4 == 3 (Row 0: 15,7,13,5; Row 1: 0,8,2,10)
};

alignas(32) inline const int32_t kDitherMatrix2x2[4][8] = {
    { 2, 10,  2, 10, 14,  6, 14,  6}, // y % 4 == 0 (Row 0: 2,10,2,10; Row 1: 14,6,14,6)
    {14,  6, 14,  6,  2, 10,  2, 10}, // y % 4 == 1 (Row 0: 14,6,14,6; Row 1: 2,10,2,10)
    { 2, 10,  2, 10, 14,  6, 14,  6}, // y % 4 == 2 (Row 0: 2,10,2,10; Row 1: 14,6,14,6)
    {14,  6, 14,  6,  2, 10,  2, 10}  // y % 4 == 3 (Row 0: 14,6,14,6; Row 1: 2,10,2,10)
};

static inline Simd8i GetDitherVector4x4(int y) {
    return Simd8i::LoadAligned(&kDitherMatrix4x4[y & 3][0]);
}

static inline Simd8i GetDitherVector2x2(int y) {
    return Simd8i::LoadAligned(&kDitherMatrix2x2[y & 3][0]);
}

#endif  // PROJECT_NG_BACKENDS_SOFTWARE_SIMDVEC_H_
