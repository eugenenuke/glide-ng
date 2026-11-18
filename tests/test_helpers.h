#pragma once

#include <glide.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace GlideTestHelpers {

// Temporary path helper so unit tests never pollute the repository root.
inline std::string TempTestPath(const std::string& filename) {
  return (std::filesystem::temp_directory_path() / filename).string();
}

// RAII wrapper for temporary generated test fixture files.
struct ScopedTempFile {
  std::string path;
  explicit ScopedTempFile(std::string p) : path(std::move(p)) {}
  ~ScopedTempFile() {
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  }
};

// Write an authentic 3df v1.1 RGB565 texture file (big-endian 16-bit texels).
inline bool WriteSynthetic3dfRgb565(
    const std::string& path, int largeLod, int smallLod,
    const std::function<uint16_t(int x, int y, int dim)>& pixelFn) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return false;
  std::fprintf(fp, "3df v1.1\nrgb565\nlod range: %d %d\naspect ratio: 1 1\n",
               smallLod, largeLod);
  for (int dim = largeLod; dim >= smallLod; dim >>= 1) {
    for (int y = 0; y < dim; ++y) {
      for (int x = 0; x < dim; ++x) {
        const uint16_t p = pixelFn(x, y, dim);
        const uint8_t be[2] = {static_cast<uint8_t>((p >> 8) & 0xFFu),
                               static_cast<uint8_t>(p & 0xFFu)};
        if (std::fwrite(be, 1, 2, fp) != 2) {
          std::fclose(fp);
          return false;
        }
      }
    }
  }
  std::fclose(fp);
  return true;
}

// Write an authentic 3df v1.1 ARGB4444 texture file (big-endian 16-bit texels).
inline bool WriteSynthetic3dfArgb4444(
    const std::string& path, int largeLod, int smallLod,
    const std::function<uint16_t(int x, int y, int dim)>& pixelFn) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return false;
  std::fprintf(fp, "3df v1.1\nargb4444\nlod range: %d %d\naspect ratio: 1 1\n",
               smallLod, largeLod);
  for (int dim = largeLod; dim >= smallLod; dim >>= 1) {
    for (int y = 0; y < dim; ++y) {
      for (int x = 0; x < dim; ++x) {
        const uint16_t p = pixelFn(x, y, dim);
        const uint8_t be[2] = {static_cast<uint8_t>((p >> 8) & 0xFFu),
                               static_cast<uint8_t>(p & 0xFFu)};
        if (std::fwrite(be, 1, 2, fp) != 2) {
          std::fclose(fp);
          return false;
        }
      }
    }
  }
  std::fclose(fp);
  return true;
}

// Write an authentic 3df v1.1 ARGB8888 texture file (big-endian 32-bit texels).
inline bool WriteSynthetic3dfArgb8888(
    const std::string& path, int largeLod, int smallLod,
    const std::function<uint32_t(int x, int y, int dim)>& pixelFn) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return false;
  std::fprintf(fp, "3df v1.1\nargb8888\nlod range: %d %d\naspect ratio: 1 1\n",
               smallLod, largeLod);
  for (int dim = largeLod; dim >= smallLod; dim >>= 1) {
    for (int y = 0; y < dim; ++y) {
      for (int x = 0; x < dim; ++x) {
        const uint32_t p = pixelFn(x, y, dim);
        const uint8_t be[4] = {static_cast<uint8_t>((p >> 24) & 0xFFu),
                               static_cast<uint8_t>((p >> 16) & 0xFFu),
                               static_cast<uint8_t>((p >> 8) & 0xFFu),
                               static_cast<uint8_t>(p & 0xFFu)};
        if (std::fwrite(be, 1, 4, fp) != 4) {
          std::fclose(fp);
          return false;
        }
      }
    }
  }
  std::fclose(fp);
  return true;
}

inline uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t PackArgb4444(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((a >> 4) << 12) | ((r >> 4) << 8) |
                               ((g >> 4) << 4) | (b >> 4));
}

}  // namespace GlideTestHelpers

// Clean-room minimal 2D screen scaling & 3D row-vector transform helpers
// replacing the external legacy tlib.h / tlib.c dependency in unit tests.
struct TlVertex3D {
  float x, y, z, w;
  float s, t;
  float r, g, b, a;
};

using TlMatrix = float[4][4];

namespace GlideTestTlib {
inline float g_screenW = 640.0f;
inline float g_screenH = 480.0f;
inline TlMatrix g_currentMatrix = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};
}  // namespace GlideTestTlib

inline void tlSetScreen(float width, float height) {
  GlideTestTlib::g_screenW = width;
  GlideTestTlib::g_screenH = height;
}

inline float tlScaleX(float coord) { return coord * GlideTestTlib::g_screenW; }
inline float tlScaleY(float coord) { return coord * GlideTestTlib::g_screenH; }

inline const float* tlIdentity() {
  static const TlMatrix m = {
      {1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  };
  return &m[0][0];
}

inline const float* tlXRotation(float degrees) {
  static TlMatrix m;
  const float rad = degrees * 0.01745328f;
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  std::memset(m, 0, sizeof(m));
  m[0][0] = 1.0f;
  m[1][1] = c;
  m[1][2] = s;
  m[2][1] = -s;
  m[2][2] = c;
  m[3][3] = 1.0f;
  return &m[0][0];
}

inline const float* tlTranslation(float x, float y, float z) {
  static TlMatrix m;
  std::memset(m, 0, sizeof(m));
  m[0][0] = 1.0f;
  m[1][1] = 1.0f;
  m[2][2] = 1.0f;
  m[3][0] = x;
  m[3][1] = y;
  m[3][2] = z;
  m[3][3] = 1.0f;
  return &m[0][0];
}

inline void tlSetMatrix(const float* m) {
  std::memcpy(GlideTestTlib::g_currentMatrix, m, sizeof(TlMatrix));
}

inline void tlMultMatrix(const float* m) {
  TlMatrix mat;
  TlMatrix res;
  std::memcpy(mat, m, sizeof(TlMatrix));
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      res[r][c] = GlideTestTlib::g_currentMatrix[r][0] * mat[0][c] +
                  GlideTestTlib::g_currentMatrix[r][1] * mat[1][c] +
                  GlideTestTlib::g_currentMatrix[r][2] * mat[2][c] +
                  GlideTestTlib::g_currentMatrix[r][3] * mat[3][c];
    }
  }
  std::memcpy(GlideTestTlib::g_currentMatrix, res, sizeof(TlMatrix));
}

inline void tlTransformVertices(TlVertex3D* dstVerts, const TlVertex3D* srcVerts,
                                unsigned length) {
  const auto& m = GlideTestTlib::g_currentMatrix;
  for (unsigned i = 0; i < length; ++i) {
    const TlVertex3D v = srcVerts[i];
    TlVertex3D out = v;
    out.x = v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + v.w * m[3][0];
    out.y = v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + v.w * m[3][1];
    out.z = v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + v.w * m[3][2];
    out.w = v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + v.w * m[3][3];
    dstVerts[i] = out;
  }
}

inline void tlProjectVertices(TlVertex3D* dstVerts, const TlVertex3D* srcVerts,
                              unsigned length) {
  for (unsigned i = 0; i < length; ++i) {
    const TlVertex3D v = srcVerts[i];
    TlVertex3D out = v;
    const float w = v.z;
    out.x = (v.x / w + 1.0f) * 0.5f;
    out.y = (v.y / w + 1.0f) * 0.5f;
    out.z = v.z / w;
    out.w = w;
    dstVerts[i] = out;
  }
}
