#include "OpenGLESBackend.h"
#include "OpenGLESInternal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

void OpenGLESBackend::UploadTexture(uint32_t tmu, uint32_t startAddress,
                                    const struct VirtualTexture& tex) {
  std::lock_guard<std::mutex> lock(m_mutex);
  UploadTexture_Locked(tmu, startAddress, tex);
}

void OpenGLESBackend::UploadTexture_Locked(uint32_t tmu, uint32_t startAddress,
                                           const struct VirtualTexture& tex) {
  FlushBatch();
  if (!m_windowAttached && !m_headlessMode) {
    GLIDE_LOG(WARN, "GLES",
              "UploadTexture called without active GL context! Postponing or "
              "ignoring.");
    return;
  }
  if (!EnsureContextCurrent()) return;

  GLIDE_PROFILE_SCOPE("GLES::UploadTexture");

  GLuint texId = 0;
  auto it = m_glesTextures.find(startAddress);
  if (it == m_glesTextures.end()) {
    glGenTextures(1, &texId);
    m_glesTextures[startAddress] = texId;
    GLIDE_LOG(DEBUG, "GLES",
              "Generated new GLES Texture ID " << texId << " for Address 0x"
                                               << std::hex << startAddress
                                               << std::dec);
  } else {
    texId = it->second;
    GLIDE_LOG(DEBUG, "GLES",
              "Re-uploading to existing GLES Texture ID "
                  << texId << " for Address 0x" << std::hex << startAddress
                  << std::dec);
  }

  glBindTexture(GL_TEXTURE_2D, texId);

  uint32_t baseW = tex.baseWidth;
  uint32_t baseH = tex.baseHeight;

  for (size_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
    uint32_t w = std::max(1u, baseW >> i);
    uint32_t h = std::max(1u, baseH >> i);
    const uint32_t* pixels = tex.swizzledMipLevels[i].data();

    // Swizzle BGRA from TextureManager to RGBA on the CPU before uploading to
    // GLES
    std::vector<uint32_t> rgbaPixels(w * h);
    for (uint32_t j = 0; j < w * h; ++j) {
      uint32_t bgra = pixels[j];
      uint32_t b = bgra & 0xFF;
      uint32_t g = (bgra >> 8) & 0xFF;
      uint32_t r = (bgra >> 16) & 0xFF;
      uint32_t a = (bgra >> 24) & 0xFF;
      rgbaPixels[j] = (a << 24) | (b << 16) | (g << 8) | r;
    }

    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), GL_RGBA,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgbaPixels.data());

    GLIDE_LOG(DEBUG, "GLES",
              "Uploaded Mip Level " << i << " (" << w << "x" << h
                                    << ") to GLES Texture ID " << texId);
  }

  // Set default wrap/filter parameters so texture is complete
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  tex.swizzledMipLevels.size() > 1 ? GL_NEAREST_MIPMAP_NEAREST
                                                   : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  if (!tex.swizzledMipLevels.empty()) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(tex.swizzledMipLevels.size() - 1));
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  glBindTexture(GL_TEXTURE_2D, 0);
  MarkStateDirty();
  checkGLError("UploadTexture");
}

void OpenGLESBackend::UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                                           const struct VirtualTexture& tex,
                                           uint32_t lodLevel, uint32_t startRow,
                                           uint32_t endRow) {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBatch();
  if (!m_windowAttached && !m_headlessMode) return;
  if (!EnsureContextCurrent()) return;

  GLIDE_PROFILE_SCOPE("GLES::UploadTexturePartial");

  // 1. Check if the texture exists. If not, promote to a full upload!
  auto it = m_glesTextures.find(startAddress);
  if (it == m_glesTextures.end()) {
    GLIDE_LOG(INFO, "GLES",
              "UploadTexturePartial: Texture at 0x"
                  << std::hex << startAddress << std::dec
                  << " not found. Promoting to full upload.");
    UploadTexture_Locked(tmu, startAddress, tex);
    return;
  }
  GLuint texId = it->second;

  // 2. Validate existence of LOD level in the virtual texture
  if (lodLevel >= tex.swizzledMipLevels.size()) {
    GLIDE_LOG(CRITICAL, "GLES",
              "UploadTexturePartial: lodLevel "
                  << lodLevel << " out of range (max: "
                  << tex.swizzledMipLevels.size() << ")");
    return;
  }

  // 3. Compute dimensions of the specific LOD level
  uint32_t w = std::max(1u, tex.baseWidth >> lodLevel);
  uint32_t h = std::max(1u, tex.baseHeight >> lodLevel);

  // 4. Validate row range bounds
  if (startRow >= h || endRow > h || startRow >= endRow) {
    GLIDE_LOG(CRITICAL, "GLES",
              "UploadTexturePartial: invalid row range ["
                  << startRow << ".." << endRow << ") for height " << h);
    return;
  }

  // 5. Ensure the row range does not exceed the physical pixel vector size
  uint32_t numRows = endRow - startRow;
  uint32_t startPixelIndex = startRow * w;
  uint32_t requiredPixels = startPixelIndex + numRows * w;
  if (requiredPixels > tex.swizzledMipLevels[lodLevel].size()) {
    GLIDE_LOG(CRITICAL, "GLES",
              "UploadTexturePartial: required pixels "
                  << requiredPixels << " exceed mip level size "
                  << tex.swizzledMipLevels[lodLevel].size());
    return;
  }

  // Save current texture binding to restore later
  GLint previousTexture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

  glBindTexture(GL_TEXTURE_2D, texId);

  // Swizzle only the modified sub-image rows
  const uint32_t* pixels = tex.swizzledMipLevels[lodLevel].data();
  std::vector<uint32_t> rgbaSubPixels(w * numRows);
  for (uint32_t j = 0; j < w * numRows; ++j) {
    uint32_t bgra = pixels[startPixelIndex + j];
    uint32_t b = bgra & 0xFF;
    uint32_t g = (bgra >> 8) & 0xFF;
    uint32_t r = (bgra >> 16) & 0xFF;
    uint32_t a = (bgra >> 24) & 0xFF;
    rgbaSubPixels[j] = (a << 24) | (b << 16) | (g << 8) | r;
  }

  // Upload using glTexSubImage2D
  glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(lodLevel), 0,
                  static_cast<GLint>(startRow), static_cast<GLsizei>(w),
                  static_cast<GLsizei>(numRows), GL_RGBA, GL_UNSIGNED_BYTE,
                  rgbaSubPixels.data());

  GLIDE_LOG(DEBUG, "GLES",
            "UploadTexturePartial TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " level=" << lodLevel << " rows=" << startRow << ".."
                << endRow << " uploaded via glTexSubImage2D.");

  glBindTexture(GL_TEXTURE_2D, previousTexture);
  checkGLError("UploadTexturePartial");
}

void OpenGLESBackend::PurgeTextures() {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBatch();
  if (m_glesTextures.empty()) return;
  GLIDE_LOG(DEBUG, "GLES",
            "PurgeTextures: deleting " << m_glesTextures.size()
                                       << " textures from GPU.");
  if (EnsureContextCurrent()) {
    for (auto& pair : m_glesTextures) {
      if (pair.second) {
        glDeleteTextures(1, &pair.second);
      }
    }
    checkGLError("PurgeTextures");
  }
  m_glesTextures.clear();
}

void OpenGLESBackend::BindTexture(uint32_t tmu, uint32_t startAddress,
                                  uint32_t clampS, uint32_t clampT,
                                  uint32_t minFilter, uint32_t magFilter) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && m_boundTexAddress[tmu] == startAddress &&
      m_texClampS[tmu] == clampS && m_texClampT[tmu] == clampT &&
      m_texMinFilter[tmu] == minFilter && m_texMagFilter[tmu] == magFilter) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::BindTexture_Locked(tmu, startAddress, clampS, clampT,
                                        minFilter, magFilter);
}

void OpenGLESBackend::SetTexLodBias(uint32_t tmu, float bias) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && m_texLodBias[tmu] == bias) return;
  FlushBatch(true);
  GlideStateTracker::SetTexLodBias_Locked(tmu, bias);
}

void OpenGLESBackend::SetTexMipMapMode(uint32_t tmu, uint32_t mode,
                                       bool lodBlend) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && m_texMipMapMode[tmu] == mode &&
      m_texLodBlend[tmu] == lodBlend) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetTexMipMapMode_Locked(tmu, mode, lodBlend);
}

}  // namespace GlideWrapper
