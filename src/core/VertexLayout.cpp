#include "VertexLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include "core/Logger.h"

namespace GlideWrapper {

// Authentic retro 3dfx preprocessor SDK parameter defines
#ifndef GR_PARAM_XY
#define GR_PARAM_XY 0
#define GR_PARAM_Z 1
#define GR_PARAM_W 2
#define GR_PARAM_Q 3
#define GR_PARAM_RGB 4
#define GR_PARAM_A 5
#define GR_PARAM_ST0 6
#define GR_PARAM_ST1 7
#endif

VertexLayout& VertexLayout::GetInstance() {
  static VertexLayout instance;
  return instance;
}

VertexLayout::VertexLayout() { ResetToGlide2Canonical(); }

void VertexLayout::ResetToGlide2Canonical() {
  m_stwHintMask = 0;
  m_offsetXY = 0;
  m_offsetZ = 8;
  m_offsetRGB = 12;
  m_offsetOOZ = 24;
  m_offsetA = 28;
  m_offsetOOW = 32;
  m_offsetST0 = 36;
  m_offsetFog = -1;
  m_offsetST1 = 48;
}

void VertexLayout::ResetToGlide3Canonical() {
  m_stwHintMask = 0;
  m_offsetXY = 0;
  m_offsetZ = -1;
  m_offsetOOZ = 8;
  m_offsetOOW = 12;
  m_offsetRGB = 16;
  m_offsetA = 28;
  m_offsetST0 = 36;
  m_offsetFog = -1;
  m_offsetST1 = -1;
}

void VertexLayout::SetParamOffset(uint32_t param, int32_t offset,
                                  uint32_t mode) {
  SetParamOffsetGlide2(param, offset, mode);
}

void VertexLayout::SetParamOffsetGlide2(uint32_t param, int32_t offset,
                                        uint32_t mode) {
  if (mode == 0) {
    offset = -1;
  }
  switch (param) {
    case 0:
      m_offsetXY = offset;
      break;  // GR_PARAM_XY
    case 1:
      m_offsetZ = offset;
      break;  // GR_PARAM_Z
    case 2:
      m_offsetOOW = offset;
      break;  // GR_PARAM_W
    case 4:
      m_offsetRGB = offset;
      break;  // GR_PARAM_RGB
    case 5:
      m_offsetA = offset;
      break;  // GR_PARAM_A
    case 6:
      m_offsetST0 = offset;
      break;  // GR_PARAM_ST0
    case 7:
      m_offsetST1 = offset;
      break;  // GR_PARAM_ST1
    default:
      break;
  }
}

void VertexLayout::SetParamOffsetGlide3(uint32_t param, int32_t offset,
                                        uint32_t mode) {
  if (mode == 0) {
    offset = -1;
  }
  switch (param) {
    case 0x01:
      m_offsetXY = offset;
      break;  // GR_PARAM_XY
    case 0x02:
      m_offsetZ = offset;
      break;    // GR_PARAM_Z
    case 0x03:  // GR_PARAM_W
    case 0x04:
      m_offsetOOW = offset;
      break;  // GR_PARAM_Q
    case 0x05:
      m_offsetFog = offset;
      break;  // GR_PARAM_FOG_EXT
    case 0x10:
      m_offsetA = offset;
      break;  // GR_PARAM_A
    case 0x20:
      m_offsetRGB = offset;
      break;    // GR_PARAM_RGB
    case 0x40:  // GR_PARAM_ST0
      m_offsetST0 = offset;
      if (offset != -1) {
        if (mode & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
          m_stwHintMask |= 0x2;
        } else {
          m_stwHintMask &= ~0x2;
        }
      } else {
        m_stwHintMask &= ~0x2;
      }
      break;
    case 0x41:  // GR_PARAM_ST1
      m_offsetST1 = offset;
      if (offset != -1) {
        if (mode & 0x8) {  // GR_STWHINT_W_DIFF_TMU1
          m_stwHintMask |= 0x8;
        } else {
          m_stwHintMask &= ~0x8;
        }
      } else {
        m_stwHintMask &= ~0x8;
      }
      break;
    default:
      break;
  }
}

ModernVertex VertexLayout::DecodeVertex(const void* gameVertex) const {
  ModernVertex v;
  v.pos = {0.0f, 0.0f, 0.0f, 1.0f};
  v.color = {1.0f, 1.0f, 1.0f, 1.0f};
  v.tex = {0.0f, 0.0f, 0.0f, 0.0f};
  v.tmu_oow = {1.0f, 1.0f};
  v.fog = 0.0f;

  if (!gameVertex) return v;

  const auto* rawBytes = reinterpret_cast<const uint8_t*>(gameVertex);

  // Parse X, Y
  if (m_offsetXY >= 0) {
    float x = 0.0f;
    float y = 0.0f;
    std::memcpy(&x, rawBytes + m_offsetXY, sizeof(float));
    std::memcpy(&y, rawBytes + m_offsetXY + sizeof(float), sizeof(float));

    // Unsnap Voodoo-specific coordinate snap bias (3 << 18 = 786432.0f or 3 <<
    // 19 = 1572864.0f)
    constexpr float SNAP_BIAS_18 = 786432.0f;
    constexpr float SNAP_BIAS_19 = 1572864.0f;
    if (x >= SNAP_BIAS_18 - 2048.0f && x <= SNAP_BIAS_18 + 2048.0f) {
      x -= SNAP_BIAS_18;
    } else if (x >= SNAP_BIAS_19 - 2048.0f && x <= SNAP_BIAS_19 + 2048.0f) {
      x -= SNAP_BIAS_19;
    }

    if (y >= SNAP_BIAS_18 - 2048.0f && y <= SNAP_BIAS_18 + 2048.0f) {
      y -= SNAP_BIAS_18;
    } else if (y >= SNAP_BIAS_19 - 2048.0f && y <= SNAP_BIAS_19 + 2048.0f) {
      y -= SNAP_BIAS_19;
    }

    v.pos[0] = x;
    v.pos[1] = y;
  }

  // Parse Depth (OOZ or Z)
  if (m_offsetOOZ >= 0) {
    float ooz = 0.0f;
    std::memcpy(&ooz, rawBytes + m_offsetOOZ, sizeof(float));
    v.pos[2] = ooz / 65535.0f;
  } else if (m_offsetZ >= 0) {
    float z = 0.0f;
    std::memcpy(&z, rawBytes + m_offsetZ, sizeof(float));
    v.pos[2] = z / 65535.0f;
  }

  // Parse 1/W
  if (m_offsetOOW >= 0) {
    float oow = 1.0f;
    std::memcpy(&oow, rawBytes + m_offsetOOW, sizeof(float));
    v.pos[3] = oow;
  }

  // Parse RGB (Legacy Glide specifies colors from 0.0f .. 255.0f!)
  if (m_offsetRGB >= 0) {
    float r = 255.0f;
    float g = 255.0f;
    float b = 255.0f;
    std::memcpy(&r, rawBytes + m_offsetRGB, sizeof(float));
    std::memcpy(&g, rawBytes + m_offsetRGB + sizeof(float), sizeof(float));
    std::memcpy(&b, rawBytes + m_offsetRGB + 2 * sizeof(float), sizeof(float));
    v.color[0] = r / 255.0f;
    v.color[1] = g / 255.0f;
    v.color[2] = b / 255.0f;
  }

  // Parse Alpha (0.0f .. 255.0f)
  if (m_offsetA >= 0) {
    float a = 255.0f;
    std::memcpy(&a, rawBytes + m_offsetA, sizeof(float));
    v.color[3] = a / 255.0f;
  }

  // Sanitize and clamp color channels to prevent NaN/Inf driver crashes and
  // garbage pixels
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(v.color[i]) || std::isinf(v.color[i])) {
      v.color[i] = 1.0f;
    } else {
      v.color[i] = std::max(0.0f, std::min(1.0f, v.color[i]));
    }
  }

  // Parse TMU0 Texture Coordinates (S, T)
  if (m_offsetST0 >= 0) {
    float s = 0.0f;
    float t = 0.0f;
    std::memcpy(&s, rawBytes + m_offsetST0, sizeof(float));
    std::memcpy(&t, rawBytes + m_offsetST0 + sizeof(float), sizeof(float));
    v.tex[0] = s;
    v.tex[1] = t;

    if (m_stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
      float oow0 = 1.0f;
      std::memcpy(&oow0, rawBytes + m_offsetST0 + 2 * sizeof(float),
                  sizeof(float));
      v.tmu_oow[0] = oow0;
    } else {
      v.tmu_oow[0] = 1.0f;  // Safe default when separate W is disabled
    }
  }

  // Parse TMU1 Texture Coordinates (S, T)
  if (m_offsetST1 >= 0) {
    float s = 0.0f;
    float t = 0.0f;
    std::memcpy(&s, rawBytes + m_offsetST1, sizeof(float));
    std::memcpy(&t, rawBytes + m_offsetST1 + sizeof(float), sizeof(float));
    v.tex[2] = s;
    v.tex[3] = t;

    if (m_stwHintMask & 0x8) {  // GR_STWHINT_W_DIFF_TMU1
      float oow1 = 1.0f;
      std::memcpy(&oow1, rawBytes + m_offsetST1 + 2 * sizeof(float),
                  sizeof(float));
      v.tmu_oow[1] = oow1;
    } else {
      v.tmu_oow[1] = 1.0f;
    }
  }

  // Parse Custom Fog Coordinate
  if (m_offsetFog >= 0) {
    float fog = 0.0f;
    std::memcpy(&fog, rawBytes + m_offsetFog, sizeof(float));
    v.fog = fog;
  }

  return v;
}

void VertexLayout::GetLayoutState(void* layoutState) const {
  if (!layoutState) return;
  auto* state = reinterpret_cast<Glide3VertexLayout*>(layoutState);
  state->stwHintMask = m_stwHintMask;
  state->offsetXY = m_offsetXY;
  state->offsetZ = m_offsetZ;
  state->offsetRGB = m_offsetRGB;
  state->offsetOOZ = m_offsetOOZ;
  state->offsetA = m_offsetA;
  state->offsetOOW = m_offsetOOW;
  state->offsetST0 = m_offsetST0;
  state->offsetFog = m_offsetFog;
  state->offsetST1 = m_offsetST1;
}

void VertexLayout::SetLayoutState(const void* layoutState) {
  if (!layoutState) return;
  auto* state = reinterpret_cast<const Glide3VertexLayout*>(layoutState);
  m_stwHintMask = state->stwHintMask;
  m_offsetXY = state->offsetXY;
  m_offsetZ = state->offsetZ;
  m_offsetRGB = state->offsetRGB;
  m_offsetOOZ = state->offsetOOZ;
  m_offsetA = state->offsetA;
  m_offsetOOW = state->offsetOOW;
  m_offsetST0 = state->offsetST0;
  m_offsetFog = state->offsetFog;
  m_offsetST1 = state->offsetST1;
}

}  // namespace GlideWrapper
