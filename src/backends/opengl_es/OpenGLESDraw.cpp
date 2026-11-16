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
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached || !EnsureContextCurrent()) return;

  // Flush any pending geometry before clearing the buffers!
  FlushBatch();

  // 1. Handle any pending sparse LFB overlay before clearing, and defer
  // CPU-side full framebuffer clear lazily so pure 3D frames allocate 0 bytes.
  if (clearMask & 0x1) {
    if (m_colorMaskRgb && m_colorMaskAlpha) {
      // Full color clear overwrites every pixel, so any pending overlay is
      // superseded.
      m_lfbStaging.ClearOverlay(m_activeRenderBuffer);
    } else if (m_lfbStaging.IsOverlayDirty(m_activeRenderBuffer)) {
      // Partial color mask leaves unmasked channels intact; flush pending
      // overlay first so those channels retain their LFB pixels.
      FlushLFBToGPU_Locked();
    }
    m_lfbStaging.ScheduleLazyClear(m_activeRenderBuffer, color, alpha,
                                   m_pixelFormatOverride, m_colorMaskRgb,
                                   m_colorMaskAlpha);
  }

  // Bind active offscreen FBO
  glBindFramebuffer(GL_FRAMEBUFFER, GetActiveGpuFbo());

  GLbitfield mask = 0;
  GLboolean oldDepthMask = m_depthMask ? GL_TRUE : GL_FALSE;

  if (clearMask & 0x1) {  // Color Clear
    float a = ((alpha > 0) ? alpha : 255) / 255.0f;
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;

    if (m_pixelFormatOverride == 1) {  // ABGR format override
      std::swap(r, b);
    }

    // Clear with native RGB color (driver BGRA format handles layout mapping)
    glClearColor(r, g, b, a);
    mask |= GL_COLOR_BUFFER_BIT;
  }

  if (clearMask & 0x2) {  // Depth Clear
    glDepthMask(GL_TRUE);  // Force depth write enabled so glClear actually
                           // clears the depth buffer!
    glClearDepthf(z);
    mask |= GL_DEPTH_BUFFER_BIT;
  }

  if (mask != 0) {
    // Apply active color write mask so glClear color clear respects the active
    // color mask!
    if (mask & GL_COLOR_BUFFER_BIT) {
      ApplyColorMaskGLES();
    }

    // In OpenGL, glClear is affected by the active scissor box.
    // But Glide grBufferClear always clears the entire screen.
    // We must temporarily disable the scissor test to force a full-screen
    // clear!
    glDisable(GL_SCISSOR_TEST);

    glClear(mask);

    glEnable(GL_SCISSOR_TEST);
  }

  if (clearMask & 0x2) {
    glDepthMask(oldDepthMask);  // Restore original depth write mask
  }

  // Restore default framebuffer binding
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

  checkGLError("CreateGeomBuffers");
  return true;
}

void OpenGLESBackend::DrawPoint(const ModernVertex& pt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU_Locked();

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
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU_Locked();

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
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU_Locked();

  bool incomingIs2D =
      (a.pos[3] == 1.0f && b.pos[3] == 1.0f && c.pos[3] == 1.0f);

  bool primitiveMismatch =
      (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
       m_batchPrimitiveMode != BatchPrimitiveMode::Triangles);
  bool projectionChanged = (m_batchPrimitiveMode != BatchPrimitiveMode::None &&
                            m_batchIs2DGeometry != incomingIs2D);
  bool capacityReached =
      (m_geomVBOOffset + (m_batchVertices.size() + 3) * sizeof(ModernVertex) >
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

#ifndef GL_SRC1_COLOR_EXT
#define GL_SRC1_COLOR_EXT 0x88F9
#endif
#ifndef GL_SRC1_ALPHA_EXT
#define GL_SRC1_ALPHA_EXT 0x8589
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
    case 15:
      return isDest
                 ? (isAlpha ? GL_SRC1_ALPHA_EXT : GL_SRC1_COLOR_EXT)
                 : GL_SRC_ALPHA_SATURATE;  // 15 = GR_BLEND_PREFOG_COLOR (dest)
                                           // / GR_BLEND_ALPHA_SATURATE (src)
    default:
      return GL_ONE;
  }
}

void OpenGLESBackend::ApplyBlendingGLES() {
  if (m_rgbSrcBlend == 4 && m_rgbDstBlend == 0 && m_alphaSrcBlend == 4 &&
      m_alphaDstBlend == 0) {  // GR_BLEND_ONE & GR_BLEND_ZERO (disabled)
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(MapBlendFactorGLES(m_rgbSrcBlend, false, false),
                        MapBlendFactorGLES(m_rgbDstBlend, true, false),
                        MapBlendFactorGLES(m_alphaSrcBlend, false, true),
                        MapBlendFactorGLES(m_alphaDstBlend, true, true));
  }
}

void OpenGLESBackend::ApplyColorMaskGLES() {
  glColorMask(m_colorMaskRgb ? GL_TRUE : GL_FALSE,
              m_colorMaskRgb ? GL_TRUE : GL_FALSE,
              m_colorMaskRgb ? GL_TRUE : GL_FALSE,
              m_colorMaskAlpha ? GL_TRUE : GL_FALSE);
}

void OpenGLESBackend::ApplyScissorGLES_Locked() {
  glEnable(GL_SCISSOR_TEST);

  // OpenGL scissor box expects bottom-up coordinates (0 = bottom).
  // If origin is UPPER_LEFT (0, top-down), we must flip Y.
  // If origin is LOWER_LEFT (1, bottom-up), we use raw coordinates.
  uint32_t glScissorY = m_clipMinY;
  if (m_sstOrigin == 0) {  // GR_ORIGIN_UPPER_LEFT
    glScissorY = m_headlessHeight - m_clipMaxY;
  }
  uint32_t glScissorW =
      (m_clipMaxX > m_clipMinX) ? (m_clipMaxX - m_clipMinX) : 0;
  uint32_t glScissorH =
      (m_clipMaxY > m_clipMinY) ? (m_clipMaxY - m_clipMinY) : 0;

  glScissor(m_clipMinX, glScissorY, glScissorW, glScissorH);
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

void OpenGLESBackend::FlushBatch(bool fromStateFlush) {
  if (m_batchVertices.empty()) return;
  if (!EnsureContextCurrent()) {
    m_batchVertices.clear();
    m_batchPrimitiveMode = BatchPrimitiveMode::None;
    return;
  }

  TelemetryManager::GetInstance().RecordDrawCall(fromStateFlush);

  uint32_t uploadSize = m_batchVertices.size() * sizeof(ModernVertex);

  // Handle VBO ring buffer wrapping and orphaning
  if (m_geomVBOOffset + uploadSize > m_geomVBOSizeConfigured) {
    GLIDE_LOG(DEBUG, "GLES",
              "Dynamic Geometry VBO ring buffer wrap-around & orphaning! "
              "Offset reset to 0.");
    m_geomVBOOffset = 0;
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferData(GL_ARRAY_BUFFER, m_geomVBOSizeConfigured, nullptr,
                 GL_DYNAMIC_DRAW);
  }

  // Upload the entire accumulated vertex array in a single contiguous transfer
  glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
  glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, uploadSize,
                  m_batchVertices.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  // Bind active offscreen headless FBO and configure viewport
  glBindFramebuffer(GL_FRAMEBUFFER, GetActiveGpuFbo());
  glViewport(0, 0, m_headlessWidth, m_headlessHeight);

  // Always bind the geometry shader program
  glUseProgram(m_geomProgram);

  // Always bind active textures and samplers to their texture units
  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    uint32_t addr = m_boundTexAddress[tmu];
    if (addr != 0xFFFFFFFF) {
      auto it = m_glesTextures[tmu].find(addr);
      if (it != m_glesTextures[tmu].end()) {
        glActiveTexture(GL_TEXTURE0 + tmu);
        glBindTexture(GL_TEXTURE_2D, it->second);
        glBindSampler(tmu, m_glesSamplers[tmu]);
      }
    }
  }

  // Lazy State Synchronization (uniforms, sampler configs, and GL pipeline
  // states)
  if (m_stateCacheDirty || m_is2DGeometry != m_batchIs2DGeometry) {
    m_is2DGeometry = m_batchIs2DGeometry;
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES_Locked();
    SyncCullStateGLES_Locked();
    ApplyColorMaskGLES();

    PopulateUniforms();

    // Configure active texture samplers (only if state is dirty)
    for (uint32_t tmu = 0; tmu < 2; ++tmu) {
      uint32_t addr = m_boundTexAddress[tmu];
      if (addr != 0xFFFFFFFF) {
        auto it = m_glesTextures[tmu].find(addr);
        if (it != m_glesTextures[tmu].end()) {
          // Set sampler wrap modes
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

          // Set sampler filter modes
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
    m_stateCacheDirty = false;
  }

  // Bind geometry VAO
  glBindVertexArray(m_geomVAO);
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

  // Statistics tracking
  if (m_batchPrimitiveMode == BatchPrimitiveMode::Triangles) {
    GLIDE_INCREMENT_TRIANGLES_DRAWN(vertexCount / 3);
  }

  // Cleanup state bindings
  glBindVertexArray(0);
  glUseProgram(0);
  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    if (m_boundTexAddress[tmu] != 0xFFFFFFFF) {
      glActiveTexture(GL_TEXTURE0 + tmu);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
  }
  glBindSampler(0, 0);
  glBindSampler(1, 0);
  glActiveTexture(GL_TEXTURE0);  // Reset active texture unit to 0
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Advance ring buffer offset
  m_geomVBOOffset += uploadSize;

  // Clear CPU-side accumulated vertices and reset batch mode
  m_batchVertices.clear();
  m_batchPrimitiveMode = BatchPrimitiveMode::None;

  checkGLError("FlushBatch");
}

}  // namespace GlideWrapper
