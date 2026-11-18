#include "OpenGLESBackend.h"
#include "OpenGLESInternal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"

namespace GlideWrapper {

void OpenGLESBackend::ClearBuffer(uint32_t color, uint32_t alpha, float z,
                                  uint32_t clearMask) {
  ClearBufferExt(color, alpha, z, 0u, clearMask & ~0x4u);
}

void OpenGLESBackend::ClearBufferExt(uint32_t color, uint32_t alpha, float z,
                                     uint8_t stencil, uint32_t clearMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached || !EnsureContextCurrent()) return;

  uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ComputeClipRectTopDown_Locked(x0, y0, x1, y1)) {
    return;
  }
  const bool fullClip =
      (x0 == 0 && y0 == 0 && x1 == m_headlessWidth && y1 == m_headlessHeight);

  // Flush any pending geometry before clearing the buffers!
  FlushBatch();

  GLbitfield mask = 0;
  GLboolean oldDepthMask = m_depthMask ? GL_TRUE : GL_FALSE;
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
  bool doColorClear = false;
  bool doDepthClear = false;
  bool doStencilClear = false;

  if ((clearMask & 0x1) && (GetColorWriteMask4() != 0)) {  // Color Clear
    a = ((alpha > 0) ? alpha : 255) / 255.0f;
    r = ((color >> 16) & 0xFF) / 255.0f;
    g = ((color >> 8) & 0xFF) / 255.0f;
    b = (color & 0xFF) / 255.0f;

    if (m_pixelFormatOverride == 1) {  // ABGR format override
      std::swap(r, b);
    }

    mask |= GL_COLOR_BUFFER_BIT;
    doColorClear = true;
  }

  if ((clearMask & 0x2) && m_depthMask) {  // Depth Clear (obeys grDepthMask)
    mask |= GL_DEPTH_BUFFER_BIT;
    doDepthClear = true;
  }

  if ((clearMask & 0x4) && m_stencilEnabled && m_stencilWriteMask != 0u) {
    mask |= GL_STENCIL_BUFFER_BIT;
    doStencilClear = true;
  }

  if (mask == 0) return;

  // Check if VSA-100 grTBufferWriteMaskExt restricts this clear to a subset of
  // MSAA samples. glClear ignores GL_SAMPLE_MASK, so partial sample-mask clears
  // must execute as a sample-masked fullscreen quad (matching VSA-100 H5).
  const uint32_t fullSampleMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  const uint32_t effSampleMask = GetEffectiveSampleMask_Locked();
  const bool isMaskedClear =
      (m_msaaSamples > 1 && m_msaaFBOs[0] != 0 && effSampleMask != fullSampleMask);

  if (isMaskedClear) {
    if (effSampleMask == 0u) return;
    if ((clearMask & 0x1) && m_lfbStaging.IsOverlayDirty(m_activeRenderBuffer)) {
      FlushLFBToGPU_Locked();
    }
    DrawMaskedClearQuadGLES_Locked(r, g, b, a, z, stencil, doColorClear,
                                   doDepthClear, doStencilClear, effSampleMask);
    return;
  }

  // 1. Handle any pending sparse LFB overlay before clearing, and defer
  // CPU-side full framebuffer clear lazily so pure 3D frames allocate 0 bytes.
  if (clearMask & 0x1) {
    if (fullClip && IsFullColorMask()) {
      // Full unmasked color clear overwrites every pixel, so any pending
      // overlay is superseded.
      m_lfbStaging.ClearOverlay(m_activeRenderBuffer);
    } else if (m_lfbStaging.IsOverlayDirty(m_activeRenderBuffer)) {
      // Partial clip window or partial color mask leaves outside pixels or
      // unmasked channels intact; flush pending overlay first.
      FlushLFBToGPU_Locked();
    }
    if (fullClip) {
      m_lfbStaging.ScheduleLazyClearMask(m_activeRenderBuffer, color, alpha,
                                         m_pixelFormatOverride,
                                         GetColorWriteMask4());
    }
  }

  const uint32_t slot = GetActiveGpuSlot();
  const bool useSingle = ShouldUseSingleSampleFbo_Locked();

  // If this clear does NOT overwrite every color pixel + channel, synchronize
  // MSAA <-> 1x color first so untouched regions/channels remain intact.
  if (m_msaaSamples > 1 && (!fullClip || !doColorClear || !IsFullColorMask())) {
    if (useSingle && m_msaaColorDirty[slot]) {
      ResolveMsaaToSingle_Locked(slot);
    } else if (!useSingle && m_singleSampleColorDirty[slot]) {
      CopySingleToMsaa_Locked(slot);
    }
  }

  if (m_supportsSampleMask) {
    glDisable(GL_SAMPLE_MASK);
  }

  // Bind active offscreen FBO
  glBindFramebuffer(GL_FRAMEBUFFER, GetActiveGpuFbo());

  if (doColorClear) {
    // Clear with native RGB color (driver BGRA format handles layout mapping)
    glClearColor(r, g, b, a);
    ApplyColorMaskGLES();
  }

  if (doDepthClear) {
    glDepthMask(GL_TRUE);
    glClearDepthf(z);
  }

  if (doStencilClear) {
    glStencilMask(static_cast<GLuint>(m_stencilWriteMask));
    glClearStencil(static_cast<GLint>(stencil));
  }

  // Authentic 3dfx Glide grBufferClear clears ONLY within the active
  // grClipWindow rectangle (e.g., split-screen panes and divider stripes).
  ApplyScissorGLES_Locked();
  m_glesDirtyMask &= ~kGlesDirtyScissor;

  glClear(mask);

  // Keep both 1x and MSAA depth/stencil buffers synchronized on unmasked clears
  // so mid-frame GR_AA_MULTI_SAMPLE transitions see authentic cleared state.
  if (m_msaaSamples > 1 && (doDepthClear || doStencilClear) &&
      m_msaaFBOs[slot] != 0) {
    const GLuint otherFbo = useSingle ? m_msaaFBOs[slot] : m_headlessFBOs[slot];
    glBindFramebuffer(GL_FRAMEBUFFER, otherFbo);
    GLbitfield dsMask = 0;
    if (doDepthClear) dsMask |= GL_DEPTH_BUFFER_BIT;
    if (doStencilClear) dsMask |= GL_STENCIL_BUFFER_BIT;
    glClear(dsMask);
  }

  if (m_msaaSamples > 1 && doColorClear) {
    if (useSingle) {
      m_singleSampleColorDirty[slot] = true;
      m_msaaColorDirty[slot] = false;
    } else {
      m_msaaColorDirty[slot] = true;
      m_singleSampleColorDirty[slot] = false;
    }
  }

  if (clearMask & 0x2) {
    glDepthMask(oldDepthMask);  // Restore original depth write mask
  }

  // Restore default framebuffer binding
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_glesDirtyMask |= kGlesDirtySampleMask;
  InvalidateGlBindingCache();
}

void OpenGLESBackend::DrawMaskedClearQuadGLES_Locked(
    float r, float g, float b, float a, float z, uint8_t stencil,
    bool doColorClear, bool doDepthClear, bool doStencilClear,
    uint32_t sampleMask) {
  const uint32_t slot = GetActiveGpuSlot();
  if (m_msaaFBOs[slot] == 0 || m_geomProgram == 0) return;

  if (m_singleSampleColorDirty[slot]) {
    CopySingleToMsaa_Locked(slot);
  }

  const float w = static_cast<float>(m_headlessWidth);
  const float h = static_cast<float>(m_headlessHeight);
  const float clampedZ = std::clamp(z, 0.0f, 1.0f);

  auto makeVert = [&](float x, float y) -> ModernVertex {
    ModernVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = clampedZ;
    v.pos[3] = 1.0f;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = a;
    return v;
  };

  const ModernVertex quad[6] = {
      makeVert(0.0f, 0.0f), makeVert(w, 0.0f), makeVert(w, h),
      makeVert(0.0f, 0.0f), makeVert(w, h),    makeVert(0.0f, h),
  };

  const uint32_t uploadSize = static_cast<uint32_t>(sizeof(quad));
  glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
  if (m_geomVBOOffset + uploadSize > m_geomVBOSizeConfigured) {
    m_geomVBOOffset = 0;
    glBufferData(GL_ARRAY_BUFFER, m_geomVBOSizeConfigured, nullptr,
                 GL_DYNAMIC_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, uploadSize, quad);
  const uint32_t firstVertex = m_geomVBOOffset / sizeof(ModernVertex);
  m_geomVBOOffset += uploadSize;

  glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBOs[slot]);
  glViewport(0, 0, static_cast<GLsizei>(m_headlessWidth),
             static_cast<GLsizei>(m_headlessHeight));
  ApplyScissorGLES_Locked();
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  glColorMask((doColorClear && m_colorMaskR) ? GL_TRUE : GL_FALSE,
              (doColorClear && m_colorMaskG) ? GL_TRUE : GL_FALSE,
              (doColorClear && m_colorMaskB) ? GL_TRUE : GL_FALSE,
              (doColorClear && m_colorMaskAlpha) ? GL_TRUE : GL_FALSE);

  if (doDepthClear) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
  } else {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
  }

  if (doStencilClear) {
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, static_cast<GLint>(stencil), 0xFFu);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glStencilMask(static_cast<GLuint>(m_stencilWriteMask));
  } else {
    glDisable(GL_STENCIL_TEST);
  }

  if (m_supportsSampleMask) {
    glEnable(GL_SAMPLE_MASK);
    glSampleMaski(0, static_cast<GLbitfield>(sampleMask));
  }

  glUseProgram(m_geomProgram);
  glUniform1i(m_uSstOriginLoc, 0);
  glUniform2f(m_uViewportSizeLoc, w, h);
  glUniform1i(m_uDepthClipOverrideLoc, doDepthClear ? 0 : 1);
  glUniform1i(m_uDepthBufferModeLoc, doDepthClear ? 1 : 0);
  glUniform1f(m_uDepthBiasLoc, 0.0f);
  glUniform1i(m_uColorFuncLoc, 1);
  glUniform1i(m_uColorLocalLoc, 0);
  glUniform1i(m_uColorInvertLoc, 0);
  glUniform1i(m_uAlphaFuncLoc, 1);
  glUniform1i(m_uAlphaLocalLoc, 0);
  glUniform1i(m_uAlphaInvertLoc, 0);
  if (m_uExtCombineLoc != -1) {
    const GLuint zeroExt[6] = {0u, 0u, 0u, 0u, 0u, 0u};
    glUniform1uiv(m_uExtCombineLoc, 6, zeroExt);
  }
  const int zeroTex[2] = {0, 0};
  glUniform1iv(m_uTextureEnabledLoc, 2, zeroTex);
  glUniform1i(m_uChromakeyEnabledLoc, 0);
  glUniform1i(m_uAlphaTestOpLoc, 7);
  glUniform1i(m_uFogModeLoc, 0);
  glUniform1f(m_uDepthNearLoc, 0.0f);
  glUniform1f(m_uDepthFarLoc, 1.0f);
  glUniform1ui(m_uDitherModeLoc, 0u);
  glUniform1ui(m_uStippleModeLoc, 0u);

  glBindVertexArray(m_geomVAO);
  glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), 6);

  if (m_supportsSampleMask) {
    glDisable(GL_SAMPLE_MASK);
  }
  glBindVertexArray(0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (doColorClear) {
    m_msaaColorDirty[slot] = true;
    m_singleSampleColorDirty[slot] = false;
  }

  m_glesDirtyMask = kGlesDirtyAll;
  InvalidateGlBindingCache();
}

bool OpenGLESBackend::CreateGeomBuffers() {
  glGenVertexArrays(1, &m_geomVAO);
  glGenBuffers(1, &m_geomVBO);

  glBindVertexArray(m_geomVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);

  // Allocate configurable dynamic ring buffer
  glBufferData(GL_ARRAY_BUFFER, m_geomVBOSizeConfigured, nullptr,
               GL_DYNAMIC_DRAW);

  // Map ModernVertex attributes:
  // Location 0: inPos (pos) -> vec4 (16 bytes)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex),
                        (void*)offsetof(ModernVertex, pos));

  // Location 1: inColor (color) -> vec4 (16 bytes)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex),
                        (void*)offsetof(ModernVertex, color));

  // Location 2: inTex (tex) -> vec4 (16 bytes)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex),
                        (void*)offsetof(ModernVertex, tex));

  // Location 3: inTmuOow (tmu_oow) -> vec2 (8 bytes)
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(ModernVertex),
                        (void*)offsetof(ModernVertex, tmu_oow));

  // Location 4: inFogCoord (fog) -> float (4 bytes)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ModernVertex),
                        (void*)offsetof(ModernVertex, fog));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  InvalidateGlBindingCache();

  m_batchVertices.reserve(16384);

  checkGLError("CreateGeomBuffers");
  return true;
}

void OpenGLESBackend::DrawPoint(const ModernVertex& pt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  bool incomingIs2D = (pt.pos[3] == 1.0f);

  bool primitiveMismatch = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchPrimitiveMode != BatchPrimitiveMode::Points);
  bool projectionChanged = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchIs2DGeometry != incomingIs2D);
  bool capacityReached =
      (m_geomVBOOffset + (m_batchVertices.size() + 1) * sizeof(ModernVertex) >
       m_geomVBOSizeConfigured);

  if (primitiveMismatch || projectionChanged || capacityReached) {
    FlushBatch(primitiveMismatch || projectionChanged);
  }

  if (m_batchPrimitiveMode == BatchPrimitiveMode::None) {
    m_batchPrimitiveMode = BatchPrimitiveMode::Points;
    m_batchIs2DGeometry = incomingIs2D;
  }

  m_batchVertices.push_back(pt);
}

void OpenGLESBackend::DrawLine(const ModernVertex& v0, const ModernVertex& v1) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  bool incomingIs2D = (v0.pos[3] == 1.0f && v1.pos[3] == 1.0f);

  bool primitiveMismatch = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchPrimitiveMode != BatchPrimitiveMode::Lines);
  bool projectionChanged = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchIs2DGeometry != incomingIs2D);
  bool capacityReached =
      (m_geomVBOOffset + (m_batchVertices.size() + 2) * sizeof(ModernVertex) >
       m_geomVBOSizeConfigured);

  if (primitiveMismatch || projectionChanged || capacityReached) {
    FlushBatch(primitiveMismatch || projectionChanged);
  }

  if (m_batchPrimitiveMode == BatchPrimitiveMode::None) {
    m_batchPrimitiveMode = BatchPrimitiveMode::Lines;
    m_batchIs2DGeometry = incomingIs2D;
  }

  m_batchVertices.push_back(v0);
  m_batchVertices.push_back(v1);
}

void OpenGLESBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                                   const ModernVertex& c) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  const bool incomingIs2D =
      (a.pos[3] == 1.0f && b.pos[3] == 1.0f && c.pos[3] == 1.0f);
  const size_t curVerts = m_batchVertices.size();

  if (__builtin_expect(
          m_batchPrimitiveMode == BatchPrimitiveMode::Triangles &&
              m_batchIs2DGeometry == incomingIs2D &&
              m_geomVBOOffset + (curVerts + 3) * sizeof(ModernVertex) <=
                  m_geomVBOSizeConfigured,
          1)) {
    m_batchVertices.push_back(a);
    m_batchVertices.push_back(b);
    m_batchVertices.push_back(c);
    return;
  }

  bool primitiveMismatch =
      (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
       m_batchPrimitiveMode != BatchPrimitiveMode::Triangles);
  bool projectionChanged = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchIs2DGeometry != incomingIs2D);
  bool capacityReached =
      (m_geomVBOOffset + (curVerts + 3) * sizeof(ModernVertex) >
       m_geomVBOSizeConfigured);

  if (primitiveMismatch || projectionChanged || capacityReached) {
    FlushBatch(primitiveMismatch || projectionChanged);
  }

  if (m_batchPrimitiveMode == BatchPrimitiveMode::None) {
    m_batchPrimitiveMode = BatchPrimitiveMode::Triangles;
    m_batchIs2DGeometry = incomingIs2D;
  }

  m_batchVertices.push_back(a);
  m_batchVertices.push_back(b);
  m_batchVertices.push_back(c);
}

void OpenGLESBackend::SyncDepthState() {
  if (m_depthMode != 0) {  // GR_DEPTHBUFFER_DISABLE is 0
    glEnable(GL_DEPTH_TEST);

    GLenum glDepthFuncVal = GL_LESS;
    switch (m_depthCompareOp) {
      case 0:
        glDepthFuncVal = GL_NEVER;
        break;
      case 1:
        glDepthFuncVal = GL_LESS;
        break;
      case 2:
        glDepthFuncVal = GL_EQUAL;
        break;
      case 3:
        glDepthFuncVal = GL_LEQUAL;
        break;
      case 4:
        glDepthFuncVal = GL_GREATER;
        break;
      case 5:
        glDepthFuncVal = GL_NOTEQUAL;
        break;
      case 6:
        glDepthFuncVal = GL_GEQUAL;
        break;
      case 7:
        glDepthFuncVal = GL_ALWAYS;
        break;
      default:
        break;
    }
    glDepthFunc(glDepthFuncVal);
    glDepthMask(m_depthMask ? GL_TRUE : GL_FALSE);
  } else {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // Explicitly disable depth writes when depth test
                            // is disabled!
  }
}

static GLenum MapCompareOpGLES(uint32_t op) {
  switch (op) {
    case 0: return GL_NEVER;
    case 1: return GL_LESS;
    case 2: return GL_EQUAL;
    case 3: return GL_LEQUAL;
    case 4: return GL_GREATER;
    case 5: return GL_NOTEQUAL;
    case 6: return GL_GEQUAL;
    case 7: return GL_ALWAYS;
    default: return GL_ALWAYS;
  }
}

static GLenum MapStencilOpGLES(uint32_t op) {
  switch (op) {
    case 0: return GL_KEEP;
    case 1: return GL_ZERO;
    case 2: return GL_REPLACE;
    case 3: return GL_INCR;
    case 4: return GL_DECR;
    case 5: return GL_INVERT;
    case 6: return GL_INCR_WRAP;
    case 7: return GL_DECR_WRAP;
    default: return GL_KEEP;
  }
}

void OpenGLESBackend::SyncStencilStateGLES_Locked() {
  if (m_stencilEnabled) {
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(MapCompareOpGLES(m_stencilCompareOp),
                  static_cast<GLint>(m_stencilRef),
                  static_cast<GLuint>(m_stencilReadMask));
    glStencilMask(static_cast<GLuint>(m_stencilWriteMask));
    glStencilOp(MapStencilOpGLES(m_stencilFailOp),
                MapStencilOpGLES(m_stencilZFailOp),
                MapStencilOpGLES(m_stencilZPassOp));
  } else {
    glDisable(GL_STENCIL_TEST);
  }
}

#ifndef GL_SRC1_COLOR_EXT
#define GL_SRC1_COLOR_EXT 0x88F9
#endif
#ifndef GL_SRC1_ALPHA_EXT
#define GL_SRC1_ALPHA_EXT 0x8589
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_FUNC_SUBTRACT
#define GL_FUNC_SUBTRACT 0x800A
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif

static GLenum MapBlendFactorGLES(uint32_t factor, bool isDest, bool isAlpha) {
  switch (factor) {
    case 0:
      return GL_ZERO;  // GR_BLEND_ZERO
    case 1:
      return GL_SRC_ALPHA;  // GR_BLEND_SRC_ALPHA
    case 2:
      return isDest ? GL_SRC_COLOR
                    : GL_DST_COLOR;  // 2 = GR_BLEND_SRC_COLOR (dest) /
                                     // GR_BLEND_DST_COLOR (src)
    case 3:
      return GL_DST_ALPHA;  // GR_BLEND_DST_ALPHA
    case 4:
      return GL_ONE;  // GR_BLEND_ONE
    case 5:
      return GL_ONE_MINUS_SRC_ALPHA;  // GR_BLEND_ONE_MINUS_SRC_ALPHA
    case 6:
      return isDest ? GL_ONE_MINUS_SRC_COLOR
                    : GL_ONE_MINUS_DST_COLOR;  // 6 = ONE_MINUS_SRC_COLOR (dest)
                                               // / ONE_MINUS_DST_COLOR (src)
    case 7:
      return GL_ONE_MINUS_DST_ALPHA;  // GR_BLEND_ONE_MINUS_DST_ALPHA
    case 8:
      return isDest ? GL_DST_COLOR : GL_SRC_COLOR;  // GR_BLEND_SAME_COLOR_EXT
    case 9:
      return isDest
                 ? GL_ONE_MINUS_DST_COLOR
                 : GL_ONE_MINUS_SRC_COLOR;  // GR_BLEND_ONE_MINUS_SAME_COLOR_EXT
    case 15:
      return isDest
                 ? (isAlpha ? GL_SRC1_ALPHA_EXT : GL_SRC1_COLOR_EXT)
                 : GL_SRC_ALPHA_SATURATE;  // 15 = GR_BLEND_PREFOG_COLOR (dest)
                                           // / GR_BLEND_ALPHA_SATURATE (src)
    default:
      return GL_ONE;
  }
}

static GLenum MapBlendOpGLES(uint32_t op) {
  switch (op) {
    case 1:
      return GL_FUNC_SUBTRACT;  // GR_BLEND_OP_SUB
    case 2:
      return GL_FUNC_REVERSE_SUBTRACT;  // GR_BLEND_OP_REVSUB
    case 0:
    default:
      return GL_FUNC_ADD;  // GR_BLEND_OP_ADD
  }
}

void OpenGLESBackend::ApplyBlendingGLES() {
  if (m_rgbSrcBlend == 4 && m_rgbDstBlend == 0 && m_rgbBlendOp == 0 &&
      m_alphaSrcBlend == 4 && m_alphaDstBlend == 0 &&
      m_alphaBlendOp == 0) {  // GR_BLEND_ONE & GR_BLEND_ZERO & ADD (disabled)
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendEquationSeparate(MapBlendOpGLES(m_rgbBlendOp),
                            MapBlendOpGLES(m_alphaBlendOp));
    glBlendFuncSeparate(MapBlendFactorGLES(m_rgbSrcBlend, false, false),
                        MapBlendFactorGLES(m_rgbDstBlend, true, false),
                        MapBlendFactorGLES(m_alphaSrcBlend, false, true),
                        MapBlendFactorGLES(m_alphaDstBlend, true, true));
  }
}

void OpenGLESBackend::ApplyColorMaskGLES() {
  glColorMask(m_colorMaskR ? GL_TRUE : GL_FALSE,
              m_colorMaskG ? GL_TRUE : GL_FALSE,
              m_colorMaskB ? GL_TRUE : GL_FALSE,
              m_colorMaskAlpha ? GL_TRUE : GL_FALSE);
}

void OpenGLESBackend::ApplyScissorGLES_Locked() {
  glEnable(GL_SCISSOR_TEST);

  // OpenGL scissor box expects bottom-up coordinates (0 = bottom).
  // ComputeClipRectTopDown_Locked gives top-down [x0, x1) x [y0, y1) already
  // clamped and origin-adjusted, so flipping Y once gives bottom-up GL coords.
  uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ComputeClipRectTopDown_Locked(x0, y0, x1, y1)) {
    glScissor(0, 0, 0, 0);
    return;
  }
  const uint32_t glScissorY = m_headlessHeight - y1;
  glScissor(static_cast<GLint>(x0), static_cast<GLint>(glScissorY),
            static_cast<GLsizei>(x1 - x0), static_cast<GLsizei>(y1 - y0));
}

void OpenGLESBackend::SyncCullStateGLES_Locked() {
  GLIDE_LOG(DEBUG, "GLES",
            "SyncCullStateGLES: m_cullMode=" << m_cullMode << ", m_sstOrigin="
                                             << m_sstOrigin);
  if (m_cullMode == 0) {  // GR_CULL_DISABLE
    glDisable(GL_CULL_FACE);
    GLIDE_LOG(DEBUG, "GLES", "  glDisable(GL_CULL_FACE)");
  } else {
    glEnable(GL_CULL_FACE);
    GLint frontFace;
    if (m_sstOrigin == 1) {
      frontFace = GL_CCW;  // LOWER_LEFT is native Y-up; Glide front-face
                           // (positive area) is CCW!
    } else {
      frontFace = GL_CW;  // UPPER_LEFT is Y-down, shader Y-flip (reflection)
                          // maps Glide front-face (positive area) to CW!
    }
    glFrontFace(frontFace);

    GLint cullFace;
    if (m_cullMode ==
        1) {  // GR_CULL_NEGATIVE (cull negative winding / CCW in Glide)
      cullFace = GL_BACK;  // Culls CCW under GL_CW, or CW under GL_CCW. Maps
                           // perfectly!
    } else {  // m_cullMode == 2: GR_CULL_POSITIVE (cull positive winding / CW
              // in Glide)
      cullFace = GL_FRONT;  // Culls CW under GL_CW, or CCW under GL_CCW. Maps
                            // perfectly!
    }
    glCullFace(cullFace);
    GLIDE_LOG(DEBUG, "GLES",
              "  glEnable(GL_CULL_FACE) | glFrontFace("
                  << (frontFace == GL_CCW ? "GL_CCW" : "GL_CW")
                  << ") | glCullFace("
                  << (cullFace == GL_BACK ? "GL_BACK" : "GL_FRONT") << ")");
  }
}

void OpenGLESBackend::ApplySampleMaskGLES_Locked() {
  if (!m_supportsSampleMask) return;
  const uint32_t fullMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  const uint32_t effMask = GetEffectiveSampleMask_Locked();
  if (m_msaaSamples > 1 && !ShouldUseSingleSampleFbo_Locked() &&
      effMask != fullMask) {
    glEnable(GL_SAMPLE_MASK);
    glSampleMaski(0, static_cast<GLbitfield>(effMask));
  } else {
    glDisable(GL_SAMPLE_MASK);
  }
}

void OpenGLESBackend::FlushBatch(bool fromStateFlush) {
  if (m_batchVertices.empty()) return;
  GLIDE_CPU_SCOPE(CpuBucket::Draw);
  if (!EnsureContextCurrent()) {
    m_batchVertices.clear();
    m_batchPrimitiveMode = BatchPrimitiveMode::None;
    return;
  }

  TelemetryManager::GetInstance().RecordDrawCall(fromStateFlush);

  // Synchronize MSAA <-> 1x color targets BEFORE binding the active draw FBO
  // (e.g., mid-frame GR_AA_MULTI_SAMPLE split-screen transition or LFB overlay).
  const uint32_t slot = GetActiveGpuSlot();
  const bool useSingle = ShouldUseSingleSampleFbo_Locked();
  if (m_msaaSamples > 1) {
    if (useSingle && m_msaaColorDirty[slot]) {
      ResolveMsaaToSingle_Locked(slot);
    } else if (!useSingle && m_singleSampleColorDirty[slot]) {
      CopySingleToMsaa_Locked(slot);
    }
  }

  uint32_t uploadSize = m_batchVertices.size() * sizeof(ModernVertex);

  // Bind dynamic VBO once and keep bound across consecutive batches
  if (m_cachedBoundVbo != m_geomVBO) {
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    m_cachedBoundVbo = m_geomVBO;
  }

  // Handle VBO ring buffer wrapping and orphaning
  if (m_geomVBOOffset + uploadSize > m_geomVBOSizeConfigured) {
    GLIDE_LOG(DEBUG, "GLES",
              "Dynamic Geometry VBO ring buffer wrap-around & orphaning! "
              "Offset reset to 0.");
    m_geomVBOOffset = 0;
    glBufferData(GL_ARRAY_BUFFER, m_geomVBOSizeConfigured, nullptr,
                 GL_DYNAMIC_DRAW);
  }

  // Upload the entire accumulated vertex array in a single contiguous transfer
  glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, uploadSize,
                  m_batchVertices.data());

  // Bind active offscreen headless FBO and configure viewport only when changed
  const uint32_t activeFbo = GetActiveGpuFbo();
  if (m_cachedBoundFbo != activeFbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, activeFbo);
    m_cachedBoundFbo = activeFbo;
  }
  if (!m_cachedViewportValid) {
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    m_cachedViewportValid = true;
  }

  // Bind geometry shader program only when changed
  if (m_cachedBoundProgram != m_geomProgram) {
    glUseProgram(m_geomProgram);
    m_cachedBoundProgram = m_geomProgram;
  }

  // Bind active textures and samplers to their texture units only when changed
  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    uint32_t addr = m_boundTexAddress[tmu];
    if (addr != 0xFFFFFFFF) {
      auto it = m_glesTextures[tmu].find(addr);
      if (it != m_glesTextures[tmu].end()) {
        const uint32_t texId = it->second.id;
        if (m_cachedBoundTex[tmu] != texId) {
          if (m_cachedActiveTexUnit != tmu) {
            glActiveTexture(GL_TEXTURE0 + tmu);
            m_cachedActiveTexUnit = tmu;
          }
          glBindTexture(GL_TEXTURE_2D, texId);
          m_cachedBoundTex[tmu] = texId;
        }
        if (m_cachedBoundSampler[tmu] != m_glesSamplers[tmu]) {
          glBindSampler(tmu, m_glesSamplers[tmu]);
          m_cachedBoundSampler[tmu] = m_glesSamplers[tmu];
        }
      }
    }
  }

  // Fine-grained lazy state synchronization (only touch dirty GL state &
  // uniforms)
  if (m_is2DGeometry != m_batchIs2DGeometry) {
    m_is2DGeometry = m_batchIs2DGeometry;
    m_glesDirtyMask |= kGlesDirtyDepth;
  }
  if (m_glesDirtyMask != 0) {
    const uint32_t dirty = m_glesDirtyMask;
    if (dirty & kGlesDirtyDepth) SyncDepthState();
    if (dirty & kGlesDirtyStencil) SyncStencilStateGLES_Locked();
    if (dirty & kGlesDirtyBlend) ApplyBlendingGLES();
    if (dirty & kGlesDirtyScissor) ApplyScissorGLES_Locked();
    if (dirty & kGlesDirtyCull) SyncCullStateGLES_Locked();
    if (dirty & kGlesDirtyColorMask) ApplyColorMaskGLES();
    if (dirty & kGlesDirtySampleMask) ApplySampleMaskGLES_Locked();

    PopulateUniforms(dirty);

    // Configure active texture samplers only when that TMU's sampler changed
    for (uint32_t tmu = 0; tmu < 2; ++tmu) {
      const uint32_t samplerBit =
          (tmu == 0) ? kGlesDirtyTexSampler0 : kGlesDirtyTexSampler1;
      if (!(dirty & samplerBit)) continue;
      uint32_t addr = m_boundTexAddress[tmu];
      if (addr != 0xFFFFFFFF) {
        auto it = m_glesTextures[tmu].find(addr);
        if (it != m_glesTextures[tmu].end()) {
          GLint wrapS = GL_REPEAT;
          if (m_texClampS[tmu] == 1) {
            wrapS = GL_CLAMP_TO_EDGE;
          } else if (m_texClampS[tmu] == 2) {
            wrapS = GL_MIRRORED_REPEAT;
          }

          GLint wrapT = GL_REPEAT;
          if (m_texClampT[tmu] == 1) {
            wrapT = GL_CLAMP_TO_EDGE;
          } else if (m_texClampT[tmu] == 2) {
            wrapT = GL_MIRRORED_REPEAT;
          }

          glSamplerParameteri(m_glesSamplers[tmu], GL_TEXTURE_WRAP_S, wrapS);
          glSamplerParameteri(m_glesSamplers[tmu], GL_TEXTURE_WRAP_T, wrapT);

          GLint minFilter = GL_NEAREST;
          if (m_texMipMapMode[tmu] == 0) {  // Mipmap Disabled
            minFilter = (m_texMinFilter[tmu] == 1) ? GL_LINEAR : GL_NEAREST;
          } else {                           // Mipmap Enabled
            if (m_texMinFilter[tmu] == 1) {  // Bilinear
              minFilter = m_texLodBlend[tmu] ? GL_LINEAR_MIPMAP_LINEAR
                                             : GL_LINEAR_MIPMAP_NEAREST;
            } else {  // Point
              minFilter = m_texLodBlend[tmu] ? GL_NEAREST_MIPMAP_LINEAR
                                             : GL_NEAREST_MIPMAP_NEAREST;
            }
          }
          GLint magFilter = (m_texMagFilter[tmu] == 1) ? GL_LINEAR : GL_NEAREST;
          glSamplerParameteri(m_glesSamplers[tmu], GL_TEXTURE_MIN_FILTER,
                              minFilter);
          glSamplerParameteri(m_glesSamplers[tmu], GL_TEXTURE_MAG_FILTER,
                              magFilter);
        }
      }
    }
    m_glesDirtyMask = 0;
    m_stateCacheDirty = false;
  }

  // Bind geometry VAO only when changed
  if (m_cachedBoundVao != m_geomVAO) {
    glBindVertexArray(m_geomVAO);
    m_cachedBoundVao = m_geomVAO;
  }
  uint32_t firstVertex = m_geomVBOOffset / sizeof(ModernVertex);

  // Dispatch a single glDrawArrays call for the batch
  GLenum mode = GL_TRIANGLES;
  if (m_batchPrimitiveMode == BatchPrimitiveMode::Points) {
    mode = GL_POINTS;
  } else if (m_batchPrimitiveMode == BatchPrimitiveMode::Lines) {
    mode = GL_LINES;
  }

  uint32_t vertexCount = m_batchVertices.size();
  glDrawArrays(mode, firstVertex, vertexCount);

  if (m_msaaSamples > 1 && GetColorWriteMask4() != 0) {
    if (useSingle) {
      m_singleSampleColorDirty[slot] = true;
      m_msaaColorDirty[slot] = false;
    } else {
      m_msaaColorDirty[slot] = true;
      m_singleSampleColorDirty[slot] = false;
    }
  }

  // Statistics tracking
  if (m_batchPrimitiveMode == BatchPrimitiveMode::Triangles) {
    GLIDE_INCREMENT_TRIANGLES_DRAWN(vertexCount / 3);
  }

  // Advance ring buffer offset
  m_geomVBOOffset += uploadSize;

  // Clear CPU-side accumulated vertices and reset batch mode
  m_batchVertices.clear();
  m_batchPrimitiveMode = BatchPrimitiveMode::None;

  checkGLError("FlushBatch");
}

}  // namespace GlideWrapper
