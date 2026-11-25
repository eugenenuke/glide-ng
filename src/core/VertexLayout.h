#pragma once
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>

namespace GlideWrapper {

struct ModernVertex {
    std::array<float, 4> pos{0.0f, 0.0f, 0.0f, 1.0f};   // x, y, z, w (where w = 1.0f / oow)
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f}; // r, g, b, a (normalized 0..1)
    std::array<float, 4> tex{0.0f, 0.0f, 0.0f, 0.0f};   // s0, t0, s1, t1
    std::array<float, 2> tmu_oow{1.0f, 1.0f};           // oow0, oow1 (TMU-specific 1/W)
    float fog{0.0f};                  // custom per-vertex fog coordinate (GR_PARAM_FOG_EXT)
};

// Homogeneous pre-projection vertex decoded under Glide 3.x GR_CLIP_COORDS.
// All attributes remain linear in homogeneous clip space so 3D frustum clipping
// can interpolate them before perspective division by w.
struct ClipVertex {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
    float q{1.0f};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
    float s0{0.0f};
    float t0{0.0f};
    float q0{1.0f};
    float s1{0.0f};
    float t1{0.0f};
    float q1{1.0f};
    float fog{0.0f};
};

struct __attribute__((packed)) GrVParamInfo {
    uint32_t mode;                      // enable / disable
    int32_t offset;                     // offset to the parameter data
};

// Opaque state container matching GrVertexLayout (exactly 108 bytes for Glide 3.x ABI!)
struct __attribute__((packed)) Glide3VertexLayout {
    GrVParamInfo vertexInfo;            // xy (GR_PARAM_XY)
    GrVParamInfo zInfo;                 // z(ooz) (GR_PARAM_Z)
    GrVParamInfo wInfo;                 // w(oow) (GR_PARAM_W)
    GrVParamInfo aInfo;                 // a float (GR_PARAM_A)
    GrVParamInfo fogInfo;               // fog (GR_PARAM_FOG_EXT)
    GrVParamInfo rgbInfo;               // rgb float (GR_PARAM_RGB)
    GrVParamInfo pargbInfo;             // pargb byte (GR_PARAM_PARGB)
    GrVParamInfo st0Info;               // st0 (GR_PARAM_ST0)
    GrVParamInfo st1Info;               // st1 (GR_PARAM_ST1)
    GrVParamInfo qInfo;                 // q (GR_PARAM_Q)
    GrVParamInfo q0Info;                // q0 (GR_PARAM_Q0)
    GrVParamInfo q1Info;                // q1 (GR_PARAM_Q1)
    uint32_t vStride;                   // vertex stride
    uint32_t vSize;                     // vertex size
    uint32_t colorType;                 // float or byte
};

static_assert(sizeof(Glide3VertexLayout) == 108,
              "Glide3VertexLayout must be exactly 108 bytes for Glide 3.x ABI compatibility!");

static_assert(sizeof(Glide3VertexLayout) <= 256,
              "Glide3VertexLayout exceeds the 256-byte client allocation limit (GR_GLIDE_VERTEXLAYOUT_SIZE)!");

class VertexLayout {
public:
    static VertexLayout& GetInstance();

    void ResetToGlide2Canonical();
    void ResetToGlide3Canonical();
    void ResetAllParamsDisabled();
    void SetParamOffset(uint32_t param, int32_t offset, uint32_t mode);
    void SetParamOffsetGlide2(uint32_t param, int32_t offset, uint32_t mode);
    void SetParamOffsetGlide3(uint32_t param, int32_t offset, uint32_t mode);

    void GetLayoutState(void* layoutState) const;
    void SetLayoutState(const void* layoutState);

    void SetSTWHintMask(uint32_t mask) { m_stwHintMask = mask; }
    uint32_t GetSTWHintMask() const { return m_stwHintMask; }
    uint32_t GetEffectiveStwHintMask() const {
        uint32_t mask = m_stwHintMask;
        if (m_offsetQ0 >= 0) mask |= 0x2u;
        if (m_offsetQ1 >= 0) mask |= 0x8u;
        return mask;
    }

    bool HasExplicitZ() const { return m_glide3ExplicitZ && m_offsetZ >= 0; }
    bool HasParamQ() const { return m_offsetQ >= 0; }
    bool HasParamQ0() const { return m_offsetQ0 >= 0; }
    bool HasParamQ1() const { return m_offsetQ1 >= 0; }
    bool HasParamFog() const { return m_offsetFog >= 0; }

    ModernVertex DecodeVertex(const void* gameVertex) const {
        ModernVertex v;
        DecodeVertexInto(gameVertex, v);
        return v;
    }

    inline void DecodeVertexInto(const void* gameVertex, ModernVertex& v) const {
        if (__builtin_expect(m_isGlide2Canonical && gameVertex != nullptr, 1)) {
            DecodeGlide2CanonicalInto(gameVertex, v);
            return;
        }
        v = DecodeVertexSlow(gameVertex);
    }

    ClipVertex DecodeClipVertex(const void* gameVertex) const;

    int32_t GetMaxActiveOffset() const;

    static inline bool IsFiniteFloat(float f) noexcept {
        constexpr uint32_t kFloat32ExponentMask = 0x7F800000u;
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        return (bits & kFloat32ExponentMask) != kFloat32ExponentMask;
    }

    static inline float UnsnapAndSanitizeXY(float c) noexcept {
        if (__builtin_expect(!IsFiniteFloat(c), 0)) return 0.0f;
        // Unsnap 3dfx IEEE-754 vertex coordinate snap biases (each +/-2048 px
        // hardware 12.4/13.3 window):
        //   1<<19 =  524288.0f (0x49000000): Glide 2.11 unsigned 12.4 (1/16 px)
        //   3<<18 =  786432.0f (0x49400000): Glide 2.x/3.x signed 12.4 (1/16 px)
        //   1<<20 = 1048576.0f (0x49800000): unsigned 13.3 (1/8 px)
        //   3<<19 = 1572864.0f (0x49C00000): signed 13.3 (1/8 px)
        constexpr float kSnapHalfWindowPx = 2048.0f;
        constexpr float kSnapBiasU12_4 = 524288.0f;
        constexpr float kSnapBiasS12_4 = 786432.0f;
        constexpr float kSnapBiasU13_3 = 1048576.0f;
        constexpr float kSnapBiasS13_3 = 1572864.0f;
        if (__builtin_expect(c >= kSnapBiasU12_4 - kSnapHalfWindowPx, 0)) {
            if (c <= kSnapBiasU12_4 + kSnapHalfWindowPx) {
                c -= kSnapBiasU12_4;
            } else if (c >= kSnapBiasS12_4 - kSnapHalfWindowPx &&
                       c <= kSnapBiasS12_4 + kSnapHalfWindowPx) {
                c -= kSnapBiasS12_4;
            } else if (c >= kSnapBiasU13_3 - kSnapHalfWindowPx &&
                       c <= kSnapBiasU13_3 + kSnapHalfWindowPx) {
                c -= kSnapBiasU13_3;
            } else if (c >= kSnapBiasS13_3 - kSnapHalfWindowPx &&
                       c <= kSnapBiasS13_3 + kSnapHalfWindowPx) {
                c -= kSnapBiasS13_3;
            }
        }
        return c;
    }

    static inline float SanitizeColorNorm(float raw) noexcept {
        if (__builtin_expect(!IsFiniteFloat(raw), 0)) return 1.0f;
        const float c = raw / 255.0f;
        return c <= 0.0f ? 0.0f : (c >= 1.0f ? 1.0f : c);
    }

    static inline float SanitizeClipColorNorm(float raw) noexcept {
        if (__builtin_expect(!IsFiniteFloat(raw), 0)) return 1.0f;
        return raw <= 0.0f ? 0.0f : (raw >= 1.0f ? 1.0f : raw);
    }

    static inline float SanitizeFiniteZero(float f) noexcept {
        return __builtin_expect(IsFiniteFloat(f), 1) ? f : 0.0f;
    }

    static inline float SanitizePositiveOow(float f) noexcept {
        // Subnormal floats (< FLT_MIN ~= 1.175e-38f) flush to 0.0 under GPU
        // FTZ/DAZ, tripping `divW <= 0.0` discards inside Vulkan/GLES shaders
        // when legacy titles leave stack integers in an uninitialized `oow`
        // (e.g. Descent II D2_3DFX.EXE automap lines with `oow = 1.937e-41`).
        return __builtin_expect(IsFiniteFloat(f) && f >= FLT_MIN, 1) ? f : 1.0f;
    }

private:
    VertexLayout();
    ModernVertex DecodeVertexSlow(const void* gameVertex) const;
    void RefreshCanonicalFlags();

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    inline void DecodeGlide2CanonicalInto(const void* gameVertex,
                                          ModernVertex& v) const noexcept {
        const auto* rawBytes = reinterpret_cast<const uint8_t*>(gameVertex);
        auto loadF = [rawBytes](size_t off) noexcept -> float {
            float val;
            std::memcpy(&val, rawBytes + off, sizeof(float));
            return val;
        };

        v.pos[0] = UnsnapAndSanitizeXY(loadF(0));
        v.pos[1] = UnsnapAndSanitizeXY(loadF(4));
        v.pos[2] = SanitizeFiniteZero(loadF(24)) / 65535.0f;
        v.pos[3] = SanitizePositiveOow(loadF(32));

        v.color[0] = SanitizeColorNorm(loadF(12));
        v.color[1] = SanitizeColorNorm(loadF(16));
        v.color[2] = SanitizeColorNorm(loadF(20));
        v.color[3] = SanitizeColorNorm(loadF(28));

        v.tex[0] = SanitizeFiniteZero(loadF(36));
        v.tex[1] = SanitizeFiniteZero(loadF(40));
        v.tex[2] = SanitizeFiniteZero(loadF(48));
        v.tex[3] = SanitizeFiniteZero(loadF(52));

        if (__builtin_expect(m_stwHintMask == 0, 1)) {
            v.tmu_oow[0] = 1.0f;
            v.tmu_oow[1] = 1.0f;
        } else {
            v.tmu_oow[0] = (m_stwHintMask & 0x2)
                               ? SanitizePositiveOow(loadF(44))
                               : 1.0f;
            v.tmu_oow[1] = (m_stwHintMask & 0x8)
                               ? SanitizePositiveOow(loadF(56))
                               : 1.0f;
        }
        v.fog = v.pos[3];
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    uint32_t m_stwHintMask{0};
    bool m_isGlide2Canonical{true};
    bool m_glide3ExplicitW{false};
    bool m_glide3ExplicitZ{false};
    Glide3VertexLayout m_glide3LayoutState;

    // Byte offsets for each parameter component (-1 if disabled)
    int32_t m_offsetXY{0};
    int32_t m_offsetZ{8};
    int32_t m_offsetRGB{12};
    int32_t m_offsetOOZ{24};
    int32_t m_offsetA{28};
    int32_t m_offsetOOW{32};  // GR_PARAM_W (window oow / clip homogeneous W)
    int32_t m_offsetQ{-1};    // GR_PARAM_Q (independent from GR_PARAM_W)
    int32_t m_offsetST0{36};
    int32_t m_offsetFog{-1};
    int32_t m_offsetST1{-1};
    int32_t m_offsetPARGB{-1};
    int32_t m_offsetQ0{-1};
    int32_t m_offsetQ1{-1};
    uint32_t m_colorType{0};  // 0 = GR_FLOAT (RGB/A), 1 = GR_U8 (PARGB)
};

} // namespace GlideWrapper
