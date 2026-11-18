#include "OpenGLESBackend.h"
#include "OpenGLESInternal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Logger.h"
#include "core/SimdKernels.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

namespace GlideWrapper {

namespace {
constexpr GLenum kGlCompressedRgbaS3tcDxt1Ext = 0x83F1u;
constexpr GLenum kGlCompressedRgbaS3tcDxt3Ext = 0x83F2u;
constexpr GLenum kGlCompressedRgbaS3tcDxt5Ext = 0x83F3u;

bool ResolveGlesCompressedFormat(const VirtualTexture& tex, bool s3tcEnabled,
                                 GLenum& outFormat, uint32_t& outBlockBytes) {
  if (!s3tcEnabled || tex.rawCompressedMipLevels.empty()) {
    return false;
  }
  switch (tex.format) {
    case 0x16u:  // GR_TEXFMT_ARGB_CMP_DXT1
      outFormat = kGlCompressedRgbaS3tcDxt1Ext;
      outBlockBytes = 8u;
      return true;
    case 0x17u:  // GR_TEXFMT_ARGB_CMP_DXT2
    case 0x18u:  // GR_TEXFMT_ARGB_CMP_DXT3
      outFormat = kGlCompressedRgbaS3tcDxt3Ext;
      outBlockBytes = 16u;
      return true;
    case 0x19u:  // GR_TEXFMT_ARGB_CMP_DXT4
    case 0x1Au:  // GR_TEXFMT_ARGB_CMP_DXT5
      outFormat = kGlCompressedRgbaS3tcDxt5Ext;
      outBlockBytes = 16u;
      return true;
    default:
      return false;
  }
}
}  // namespace

void OpenGLESBackend::UploadTexture(uint32_t tmu, uint32_t startAddress,
                                    const struct VirtualTexture& tex) {
  std::lock_guard<std::mutex> lock(m_mutex);
  UploadTexture_Locked(tmu, startAddress, tex);
}

void OpenGLESBackend::UploadTexture_Locked(uint32_t tmu, uint32_t startAddress,
                                           const struct VirtualTexture& tex) {
  FlushBatch();
  if (tmu >= 2) return;
  GLenum compressedFmt = 0;
  uint32_t bcBlockBytes = 0;
  const bool useCompressed = ResolveGlesCompressedFormat(
      tex, m_supportsS3tc, compressedFmt, bcBlockBytes);
  const uint32_t mipCount = static_cast<uint32_t>(
      useCompressed ? tex.rawCompressedMipLevels.size()
                    : tex.swizzledMipLevels.size());
  if (tex.baseWidth == 0 || tex.baseHeight == 0 || mipCount == 0) {
    return;
  }
  if (!m_windowAttached && !m_headlessMode) {
    GLIDE_LOG(WARN, "GLES",
              "UploadTexture called without active GL context! Postponing or "
              "ignoring.");
    return;
  }
  if (!EnsureContextCurrent()) return;

  GLIDE_PROFILE_SCOPE("GLES::UploadTexture");

  const uint32_t targetInternalFmt =
      useCompressed ? static_cast<uint32_t>(compressedFmt)
                    : static_cast<uint32_t>(GL_RGBA);
  auto& tmuMap = m_glesTextures[tmu];
  GLuint texId = 0;
  bool canSubImage = false;
  auto it = tmuMap.find(startAddress);
  if (it == tmuMap.end()) {
    glGenTextures(1, &texId);
    it = tmuMap.emplace(startAddress, GlesTexEntry{texId, 0, 0, 0, 0}).first;
    GLIDE_LOG(DEBUG, "GLES",
              "Generated new GLES Texture ID " << texId << " for TMU" << tmu
                                               << " Address 0x" << std::hex
                                               << startAddress << std::dec);
  } else {
    texId = it->second.id;
    canSubImage = (it->second.width == tex.baseWidth &&
                   it->second.height == tex.baseHeight &&
                   it->second.mipLevels == mipCount &&
                   it->second.internalFormat == targetInternalFmt);
    GLIDE_LOG(DEBUG, "GLES",
              "Re-uploading to existing GLES Texture ID "
                  << texId << " for TMU" << tmu << " Address 0x" << std::hex
                  << startAddress << std::dec
                  << (canSubImage ? " (SubImage)" : " (TexImage)"));
  }

  if (m_cachedActiveTexUnit != 0) {
    glActiveTexture(GL_TEXTURE0);
    m_cachedActiveTexUnit = 0;
  }
  if (m_cachedBoundTex[0] != texId) {
    glBindTexture(GL_TEXTURE_2D, texId);
    m_cachedBoundTex[0] = texId;
  }

  uint32_t baseW = tex.baseWidth;
  uint32_t baseH = tex.baseHeight;

  if (useCompressed) {
    for (uint32_t i = 0; i < mipCount; ++i) {
      const uint32_t w = std::max(1u, baseW >> i);
      const uint32_t h = std::max(1u, baseH >> i);
      const size_t expectedBytes = static_cast<size_t>((w + 3u) / 4u) *
                                   ((h + 3u) / 4u) * bcBlockBytes;
      const auto& levelBytes = tex.rawCompressedMipLevels[i];
      const uint8_t* srcPtr = levelBytes.data();
      std::vector<uint8_t> padded;
      if (levelBytes.size() < expectedBytes) {
        padded.assign(expectedBytes, 0u);
        if (!levelBytes.empty()) {
          std::memcpy(padded.data(), levelBytes.data(), levelBytes.size());
        }
        srcPtr = padded.data();
      }
      if (canSubImage) {
        glCompressedTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), 0, 0,
                                  static_cast<GLsizei>(w),
                                  static_cast<GLsizei>(h), compressedFmt,
                                  static_cast<GLsizei>(expectedBytes), srcPtr);
      } else {
        glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i),
                               compressedFmt, static_cast<GLsizei>(w),
                               static_cast<GLsizei>(h), 0,
                               static_cast<GLsizei>(expectedBytes), srcPtr);
      }
    }
  } else {
    for (size_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
      uint32_t w = std::max(1u, baseW >> i);
      uint32_t h = std::max(1u, baseH >> i);
      const auto& levelVec = tex.swizzledMipLevels[i];
      const size_t expectedPixels = static_cast<size_t>(w) * h;
      const size_t copyCount = std::min(expectedPixels, levelVec.size());
      const uint32_t* pixels = levelVec.data();

      // Swizzle BGRA from TextureManager to RGBA into persistent scratch buffer;
      // defensively zero-fill any missing texels to prevent OOB reads.
      if (m_texUploadScratch.size() < expectedPixels) {
        m_texUploadScratch.resize(expectedPixels);
      }
      uint32_t* dst = m_texUploadScratch.data();
      GetSimdKernels().SwapRB32(pixels, dst, copyCount);
      if (copyCount < expectedPixels) {
        std::memset(dst + copyCount, 0,
                    (expectedPixels - copyCount) * sizeof(uint32_t));
      }

      if (canSubImage) {
        glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), 0, 0,
                        static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                        GL_RGBA, GL_UNSIGNED_BYTE, dst);
      } else {
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), GL_RGBA,
                     static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, dst);
      }

      GLIDE_LOG(DEBUG, "GLES",
                "Uploaded Mip Level " << i << " (" << w << "x" << h
                                      << ") to GLES Texture ID " << texId);
    }
  }

  if (!canSubImage) {
    it->second.width = baseW;
    it->second.height = baseH;
    it->second.mipLevels = mipCount;
    it->second.internalFormat = targetInternalFmt;
    // Set default wrap/filter parameters so texture is complete
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipCount > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (mipCount > 0) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                      static_cast<GLint>(mipCount - 1));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

  m_glesDirtyMask |=
      kGlesDirtyTexBinding |
      (tmu == 0 ? kGlesDirtyTexSampler0 : kGlesDirtyTexSampler1);
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

  if (tmu >= 2) return;

  // 1. Check if the texture exists. If not, promote to a full upload!
  auto it = m_glesTextures[tmu].find(startAddress);
  if (it == m_glesTextures[tmu].end()) {
    GLIDE_LOG(INFO, "GLES",
              "UploadTexturePartial: Texture at 0x"
                  << std::hex << startAddress << std::dec
                  << " not found. Promoting to full upload.");
    UploadTexture_Locked(tmu, startAddress, tex);
    return;
  }
  GLuint texId = it->second.id;
  if (it->second.internalFormat != static_cast<uint32_t>(GL_RGBA)) {
    UploadTexture_Locked(tmu, startAddress, tex);
    return;
  }

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

  if (m_cachedActiveTexUnit != 0) {
    glActiveTexture(GL_TEXTURE0);
    m_cachedActiveTexUnit = 0;
  }
  if (m_cachedBoundTex[0] != texId) {
    glBindTexture(GL_TEXTURE_2D, texId);
    m_cachedBoundTex[0] = texId;
  }

  // Swizzle only the modified sub-image rows into persistent scratch buffer
  const uint32_t* pixels = tex.swizzledMipLevels[lodLevel].data();
  const size_t subPixels = static_cast<size_t>(w) * numRows;
  if (m_texUploadScratch.size() < subPixels) {
    m_texUploadScratch.resize(subPixels);
  }
  uint32_t* dst = m_texUploadScratch.data();
  GetSimdKernels().SwapRB32(pixels + startPixelIndex, dst, subPixels);

  // Upload using glTexSubImage2D
  glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(lodLevel), 0,
                  static_cast<GLint>(startRow), static_cast<GLsizei>(w),
                  static_cast<GLsizei>(numRows), GL_RGBA, GL_UNSIGNED_BYTE,
                  dst);

  GLIDE_LOG(DEBUG, "GLES",
            "UploadTexturePartial TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " level=" << lodLevel << " rows=" << startRow << ".."
                << endRow << " uploaded via glTexSubImage2D.");

  checkGLError("UploadTexturePartial");
}

void OpenGLESBackend::PurgeTextures() {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBatch();
  const size_t total = m_glesTextures[0].size() + m_glesTextures[1].size();
  if (total == 0) return;
  GLIDE_LOG(DEBUG, "GLES",
            "PurgeTextures: deleting " << total << " textures from GPU.");
  if (EnsureContextCurrent()) {
    for (uint32_t t = 0; t < 2; ++t) {
      for (auto& pair : m_glesTextures[t]) {
        if (pair.second.id) {
          glDeleteTextures(1, &pair.second.id);
        }
      }
    }
    checkGLError("PurgeTextures");
  }
  m_glesTextures[0].clear();
  m_glesTextures[1].clear();
  m_cachedBoundTex[0] = 0xFFFFFFFFu;
  m_cachedBoundTex[1] = 0xFFFFFFFFu;
  m_glesDirtyMask |= kGlesDirtyTexBinding;
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
  const bool addrChanged = (tmu < 2 && m_boundTexAddress[tmu] != startAddress);
  const bool samplerChanged =
      (tmu < 2 && (m_texClampS[tmu] != clampS || m_texClampT[tmu] != clampT ||
                   m_texMinFilter[tmu] != minFilter ||
                   m_texMagFilter[tmu] != magFilter));
  GlideStateTracker::BindTexture_Locked(tmu, startAddress, clampS, clampT,
                                        minFilter, magFilter);
  if (addrChanged) {
    m_glesDirtyMask |= kGlesDirtyTexBinding;
  }
  if (samplerChanged) {
    m_glesDirtyMask |=
        (tmu == 0 ? kGlesDirtyTexSampler0 : kGlesDirtyTexSampler1);
  }
}

void OpenGLESBackend::SetTexLodBias(uint32_t tmu, float bias) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2 && m_texLodBias[tmu] == bias) return;
  FlushBatch(true);
  GlideStateTracker::SetTexLodBias_Locked(tmu, bias);
  m_glesDirtyMask |= kGlesDirtyTexBinding;
}

void OpenGLESBackend::SetTexDetailControl(uint32_t tmu, int32_t lodBias,
                                          uint8_t detailScale,
                                          float detailMax) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  const int32_t clampedBias = std::clamp(lodBias, -32, 31);
  const uint8_t clampedScale = std::min<uint8_t>(detailScale, 7u);
  const float clampedMax = std::clamp(detailMax, 0.0f, 1.0f);
  if (m_texDetailBias[tmu] == clampedBias &&
      m_texDetailScale[tmu] == clampedScale &&
      m_texDetailMax[tmu] == clampedMax) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetTexDetailControl_Locked(tmu, clampedBias, clampedScale,
                                                clampedMax);
  m_glesDirtyMask |= kGlesDirtyTexDetail;
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
  m_glesDirtyMask |=
      (tmu == 0 ? kGlesDirtyTexSampler0 : kGlesDirtyTexSampler1);
}

}  // namespace GlideWrapper
