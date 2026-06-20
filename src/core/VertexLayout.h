#pragma once
#include <array>
#include <cstdint>

namespace GlideWrapper {

struct ModernVertex {
    std::array<float, 4> pos;   // x, y, z, w (where w = 1.0f / oow)
    std::array<float, 4> color; // r, g, b, a (normalized 0..1)
    std::array<float, 4> tex;   // s0, t0, s1, t1
    std::array<float, 2> tmu_oow; // oow0, oow1 (TMU-specific 1/W)
};

class VertexLayout {
public:
    static VertexLayout& GetInstance();

    void ResetToGlide2Canonical();
    void ResetToGlide3Canonical();
    void SetParamOffset(uint32_t param, int32_t offset, uint32_t mode);
    void SetParamOffsetGlide2(uint32_t param, int32_t offset, uint32_t mode);
    void SetParamOffsetGlide3(uint32_t param, int32_t offset, uint32_t mode);

    void SetSTWHintMask(uint32_t mask) { m_stwHintMask = mask; }
    uint32_t GetSTWHintMask() const { return m_stwHintMask; }

    ModernVertex DecodeVertex(const void* gameVertex) const;

private:
    VertexLayout();

    uint32_t m_stwHintMask{0};

    // Byte offsets for each parameter component (-1 if disabled)
    int32_t m_offsetXY{0};
    int32_t m_offsetZ{8};
    int32_t m_offsetRGB{12};
    int32_t m_offsetOOZ{24};
    int32_t m_offsetA{28};
    int32_t m_offsetOOW{32};
    int32_t m_offsetST0{36};
};

} // namespace GlideWrapper
