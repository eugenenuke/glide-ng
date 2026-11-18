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

void VertexLayout::RefreshCanonicalFlags() {
  m_isGlide2Canonical =
      (m_offsetXY == 0 && m_offsetRGB == 12 && m_offsetOOZ == 24 &&
       m_offsetA == 28 && m_offsetOOW == 32 && m_offsetST0 == 36 &&
       m_offsetST1 == 48 && m_offsetFog == -1 && m_colorType == 0 &&
       m_offsetPARGB == -1 && m_offsetQ0 == -1 && m_offsetQ1 == -1);
}

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
  m_offsetPARGB = -1;
  m_offsetQ0 = -1;
  m_offsetQ1 = -1;
  m_colorType = 0;
  m_isGlide2Canonical = true;
}

void VertexLayout::ResetToGlide3Canonical() {
  m_stwHintMask = 0;
  m_isGlide2Canonical = false;
  m_offsetXY = 0;
  m_offsetZ = -1;
  m_offsetOOZ = 8;
  m_offsetOOW = 12;
  m_offsetRGB = 16;
  m_offsetA = 28;
  m_offsetST0 = 36;
  m_offsetFog = -1;
  m_offsetST1 = -1;
  m_offsetPARGB = -1;
  m_offsetQ0 = -1;
  m_offsetQ1 = -1;
  m_colorType = 0;

  // Initialize the 108-byte struct to match the canonical Glide 3 state
  std::memset(&m_glide3LayoutState, 0, sizeof(m_glide3LayoutState));

  m_glide3LayoutState.vertexInfo.offset = 0;
  m_glide3LayoutState.vertexInfo.mode = 1;  // ENABLE

  m_glide3LayoutState.zInfo.offset = 8;  // canonical depth is OOZ at offset 8
  m_glide3LayoutState.zInfo.mode = 1;

  m_glide3LayoutState.wInfo.offset = 12;
  m_glide3LayoutState.wInfo.mode = 1;

  m_glide3LayoutState.rgbInfo.offset = 16;
  m_glide3LayoutState.rgbInfo.mode = 1;

  m_glide3LayoutState.aInfo.offset = 28;
  m_glide3LayoutState.aInfo.mode = 1;

  m_glide3LayoutState.st0Info.offset = 36;
  m_glide3LayoutState.st0Info.mode = 1;

  m_glide3LayoutState.st1Info.offset = -1;
  m_glide3LayoutState.st1Info.mode = 0;

  m_glide3LayoutState.fogInfo.offset = -1;
  m_glide3LayoutState.fogInfo.mode = 0;

  m_glide3LayoutState.pargbInfo.offset = -1;
  m_glide3LayoutState.pargbInfo.mode = 0;

  m_glide3LayoutState.qInfo.offset = -1;
  m_glide3LayoutState.qInfo.mode = 0;

  m_glide3LayoutState.q0Info.offset = -1;
  m_glide3LayoutState.q0Info.mode = 0;

  m_glide3LayoutState.q1Info.offset = -1;
  m_glide3LayoutState.q1Info.mode = 0;

  m_glide3LayoutState.vStride = 44;  // standard canonical stride
  m_glide3LayoutState.vSize = 44;
  m_glide3LayoutState.colorType = 0;  // float
}

void VertexLayout::ResetAllParamsDisabled() {
  m_stwHintMask = 0;
  m_isGlide2Canonical = false;
  m_offsetXY = -1;
  m_offsetZ = -1;
  m_offsetOOZ = -1;
  m_offsetOOW = -1;
  m_offsetRGB = -1;
  m_offsetA = -1;
  m_offsetST0 = -1;
  m_offsetFog = -1;
  m_offsetST1 = -1;
  m_offsetPARGB = -1;
  m_offsetQ0 = -1;
  m_offsetQ1 = -1;
  m_colorType = 0;
  std::memset(&m_glide3LayoutState, 0, sizeof(m_glide3LayoutState));
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
  RefreshCanonicalFlags();
}

void VertexLayout::SetParamOffsetGlide3(uint32_t param, int32_t offset,
                                        uint32_t mode) {
  if (mode == 0) {
    offset = -1;
  }
  switch (param) {
    case 0x01:  // GR_PARAM_XY
      m_offsetXY = offset;
      m_glide3LayoutState.vertexInfo.offset = offset;
      m_glide3LayoutState.vertexInfo.mode = mode;
      break;
    case 0x02:  // GR_PARAM_Z
      m_offsetZ = offset;
      m_offsetOOZ = -1;
      m_glide3LayoutState.zInfo.offset = offset;
      m_glide3LayoutState.zInfo.mode = mode;
      break;
    case 0x03:  // GR_PARAM_W
      m_offsetOOW = offset;
      m_glide3LayoutState.wInfo.offset = offset;
      m_glide3LayoutState.wInfo.mode = mode;
      if (mode != 0) {
        m_glide3LayoutState.qInfo.offset = -1;
        m_glide3LayoutState.qInfo.mode = 0;
      }
      break;
    case 0x04:  // GR_PARAM_Q
      m_offsetOOW = offset;
      m_glide3LayoutState.qInfo.offset = offset;
      m_glide3LayoutState.qInfo.mode = mode;
      if (mode != 0) {
        m_glide3LayoutState.wInfo.offset = -1;
        m_glide3LayoutState.wInfo.mode = 0;
      }
      break;
    case 0x05:  // GR_PARAM_FOG_EXT
      m_offsetFog = offset;
      m_glide3LayoutState.fogInfo.offset = offset;
      m_glide3LayoutState.fogInfo.mode = mode;
      break;
    case 0x10:  // GR_PARAM_A
      m_offsetA = offset;
      m_glide3LayoutState.aInfo.offset = offset;
      m_glide3LayoutState.aInfo.mode = mode;
      if (mode != 0) {
        m_colorType = 0;  // GR_FLOAT
        m_glide3LayoutState.colorType = 0;
      }
      break;
    case 0x20:  // GR_PARAM_RGB
      m_offsetRGB = offset;
      m_glide3LayoutState.rgbInfo.offset = offset;
      m_glide3LayoutState.rgbInfo.mode = mode;
      if (mode != 0) {
        m_colorType = 0;  // GR_FLOAT
        m_glide3LayoutState.colorType = 0;
      }
      break;
    case 0x30:  // GR_PARAM_PARGB
      m_offsetPARGB = offset;
      m_glide3LayoutState.pargbInfo.offset = offset;
      m_glide3LayoutState.pargbInfo.mode = mode;
      if (mode != 0) {
        m_colorType = 1;  // GR_U8 (packed 0xAARRGGBB)
        m_glide3LayoutState.colorType = 1;
      }
      break;
    case 0x40:  // GR_PARAM_ST0
      m_offsetST0 = offset;
      m_glide3LayoutState.st0Info.offset = offset;
      m_glide3LayoutState.st0Info.mode = mode;
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
      m_glide3LayoutState.st1Info.offset = offset;
      m_glide3LayoutState.st1Info.mode = mode;
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
    case 0x50:  // GR_PARAM_Q0
      m_offsetQ0 = offset;
      m_glide3LayoutState.q0Info.offset = offset;
      m_glide3LayoutState.q0Info.mode = mode;
      break;
    case 0x51:  // GR_PARAM_Q1
      m_offsetQ1 = offset;
      m_glide3LayoutState.q1Info.offset = offset;
      m_glide3LayoutState.q1Info.mode = mode;
      break;
    default:
      break;
  }
  RefreshCanonicalFlags();
}

ModernVertex VertexLayout::DecodeVertexSlow(const void* gameVertex) const {
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
    v.pos[0] = UnsnapAndSanitizeXY(x);
    v.pos[1] = UnsnapAndSanitizeXY(y);
  }

  // Parse Depth (OOZ or Z)
  if (m_offsetOOZ >= 0) {
    float ooz = 0.0f;
    std::memcpy(&ooz, rawBytes + m_offsetOOZ, sizeof(float));
    v.pos[2] = SanitizeFiniteZero(ooz) / 65535.0f;
  } else if (m_offsetZ >= 0) {
    float z = 0.0f;
    std::memcpy(&z, rawBytes + m_offsetZ, sizeof(float));
    v.pos[2] = SanitizeFiniteZero(z) / 65535.0f;
  }

  // Parse 1/W
  if (m_offsetOOW >= 0) {
    float oow = 1.0f;
    std::memcpy(&oow, rawBytes + m_offsetOOW, sizeof(float));
    v.pos[3] = SanitizePositiveOow(oow);
  }

  // Parse Color: Glide 3.x GR_PARAM_PARGB (GR_U8, packed 0xAARRGGBB) vs
  // float RGB/A (GR_FLOAT, 0.0f .. 255.0f)
  if (m_colorType == 1) {
    if (m_offsetPARGB >= 0) {
      uint32_t pargb = 0xFFFFFFFFu;
      std::memcpy(&pargb, rawBytes + m_offsetPARGB, sizeof(uint32_t));
      v.color[0] = static_cast<float>((pargb >> 16) & 0xFFu) / 255.0f;
      v.color[1] = static_cast<float>((pargb >> 8) & 0xFFu) / 255.0f;
      v.color[2] = static_cast<float>(pargb & 0xFFu) / 255.0f;
      v.color[3] = static_cast<float>((pargb >> 24) & 0xFFu) / 255.0f;
    }
  } else {
    // Parse RGB (Legacy Glide specifies colors from 0.0f .. 255.0f!)
    if (m_offsetRGB >= 0) {
      float r = 255.0f;
      float g = 255.0f;
      float b = 255.0f;
      std::memcpy(&r, rawBytes + m_offsetRGB, sizeof(float));
      std::memcpy(&g, rawBytes + m_offsetRGB + sizeof(float), sizeof(float));
      std::memcpy(&b, rawBytes + m_offsetRGB + 2 * sizeof(float), sizeof(float));
      v.color[0] = SanitizeColorNorm(r);
      v.color[1] = SanitizeColorNorm(g);
      v.color[2] = SanitizeColorNorm(b);
    }

    // Parse Alpha (0.0f .. 255.0f)
    if (m_offsetA >= 0) {
      float a = 255.0f;
      std::memcpy(&a, rawBytes + m_offsetA, sizeof(float));
      v.color[3] = SanitizeColorNorm(a);
    }
  }

  // Parse TMU0 Texture Coordinates (S, T)
  if (m_offsetST0 >= 0) {
    float s = 0.0f;
    float t = 0.0f;
    std::memcpy(&s, rawBytes + m_offsetST0, sizeof(float));
    std::memcpy(&t, rawBytes + m_offsetST0 + sizeof(float), sizeof(float));
    v.tex[0] = SanitizeFiniteZero(s);
    v.tex[1] = SanitizeFiniteZero(t);

    if (m_offsetQ0 >= 0) {
      float oow0 = 1.0f;
      std::memcpy(&oow0, rawBytes + m_offsetQ0, sizeof(float));
      v.tmu_oow[0] = SanitizePositiveOow(oow0);
    } else if (m_stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
      float oow0 = 1.0f;
      std::memcpy(&oow0, rawBytes + m_offsetST0 + 2 * sizeof(float),
                  sizeof(float));
      v.tmu_oow[0] = SanitizePositiveOow(oow0);
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
    v.tex[2] = SanitizeFiniteZero(s);
    v.tex[3] = SanitizeFiniteZero(t);

    if (m_offsetQ1 >= 0) {
      float oow1 = 1.0f;
      std::memcpy(&oow1, rawBytes + m_offsetQ1, sizeof(float));
      v.tmu_oow[1] = SanitizePositiveOow(oow1);
    } else if (m_stwHintMask & 0x8) {  // GR_STWHINT_W_DIFF_TMU1
      float oow1 = 1.0f;
      std::memcpy(&oow1, rawBytes + m_offsetST1 + 2 * sizeof(float),
                  sizeof(float));
      v.tmu_oow[1] = SanitizePositiveOow(oow1);
    } else {
      v.tmu_oow[1] = 1.0f;
    }
  }

  // Parse Custom Fog Coordinate
  if (m_offsetFog >= 0) {
    float fog = 0.0f;
    std::memcpy(&fog, rawBytes + m_offsetFog, sizeof(float));
    v.fog = SanitizeFiniteZero(fog);
  }

  return v;
}

void VertexLayout::GetLayoutState(void* layoutState) const {
  if (!layoutState) return;
  std::memcpy(layoutState, &m_glide3LayoutState, sizeof(Glide3VertexLayout));
}

void VertexLayout::SetLayoutState(const void* layoutState) {
  if (!layoutState) return;
  std::memcpy(&m_glide3LayoutState, layoutState, sizeof(Glide3VertexLayout));

  // Symmetrically restore all the internal flat offsets from the loaded struct
  auto resolveOff = [](GrVParamInfo info) -> int32_t {
    return (info.mode == 0) ? -1 : info.offset;
  };
  m_offsetXY = resolveOff(m_glide3LayoutState.vertexInfo);
  m_offsetZ = resolveOff(m_glide3LayoutState.zInfo);
  m_offsetOOZ = -1;  // Reset OOZ and map depth to offsetZ for Glide 3.x consistency

  m_offsetOOW = resolveOff(m_glide3LayoutState.wInfo);
  if (m_offsetOOW == -1) {
    m_offsetOOW = resolveOff(m_glide3LayoutState.qInfo);
  }
  m_offsetA = resolveOff(m_glide3LayoutState.aInfo);
  m_offsetRGB = resolveOff(m_glide3LayoutState.rgbInfo);
  m_offsetPARGB = resolveOff(m_glide3LayoutState.pargbInfo);
  m_offsetST0 = resolveOff(m_glide3LayoutState.st0Info);
  m_offsetST1 = resolveOff(m_glide3LayoutState.st1Info);
  m_offsetQ0 = resolveOff(m_glide3LayoutState.q0Info);
  m_offsetQ1 = resolveOff(m_glide3LayoutState.q1Info);
  m_offsetFog = resolveOff(m_glide3LayoutState.fogInfo);
  m_colorType = m_glide3LayoutState.colorType;

  // Reconstruct m_stwHintMask from the restored TMU modes
  m_stwHintMask = 0;
  if (m_offsetST0 != -1) {
    if (m_glide3LayoutState.st0Info.mode & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
      m_stwHintMask |= 0x2;
    }
  }
  if (m_offsetST1 != -1) {
    if (m_glide3LayoutState.st1Info.mode & 0x8) {  // GR_STWHINT_W_DIFF_TMU1
      m_stwHintMask |= 0x8;
    }
  }
  RefreshCanonicalFlags();
}

int32_t VertexLayout::GetMaxActiveOffset() const {
  int32_t maxOffset = -1;

  if (m_offsetXY >= 0) maxOffset = std::max(maxOffset, m_offsetXY + 8);
  if (m_offsetZ >= 0) maxOffset = std::max(maxOffset, m_offsetZ + 4);
  if (m_offsetOOZ >= 0) maxOffset = std::max(maxOffset, m_offsetOOZ + 4);
  if (m_colorType == 1) {
    if (m_offsetPARGB >= 0) maxOffset = std::max(maxOffset, m_offsetPARGB + 4);
  } else {
    if (m_offsetRGB >= 0) maxOffset = std::max(maxOffset, m_offsetRGB + 12);
    if (m_offsetA >= 0) maxOffset = std::max(maxOffset, m_offsetA + 4);
  }
  if (m_offsetOOW >= 0) maxOffset = std::max(maxOffset, m_offsetOOW + 4);
  if (m_offsetST0 >= 0) {
    int32_t size = (m_stwHintMask & 0x2) ? 12 : 8;
    maxOffset = std::max(maxOffset, m_offsetST0 + size);
  }
  if (m_offsetST1 >= 0) {
    int32_t size = (m_stwHintMask & 0x8) ? 12 : 8;
    maxOffset = std::max(maxOffset, m_offsetST1 + size);
  }
  if (m_offsetQ0 >= 0) maxOffset = std::max(maxOffset, m_offsetQ0 + 4);
  if (m_offsetQ1 >= 0) maxOffset = std::max(maxOffset, m_offsetQ1 + 4);
  if (m_offsetFog >= 0) maxOffset = std::max(maxOffset, m_offsetFog + 4);

  return maxOffset;
}

}  // namespace GlideWrapper
