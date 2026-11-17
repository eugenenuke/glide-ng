#pragma once

#include <string>

#if defined(_WIN32)
#include "Win32GlLoader.h"
#else
#include <GLES3/gl32.h>
#endif

#include "core/Logger.h"

// Universal fallback definition for BGRA extension format if header is missing
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

namespace GlideWrapper {

extern const char* vsSource;
extern const char* fsSource;

// Adapts `#version 300 es` shaders to `#version 330 core` on Desktop OpenGL
// (Win32 WGL), while leaving them untouched on native OpenGL ES 3.2 (Linux).
inline std::string AdaptGlslSourceForPlatform(const char* es3Source) {
  std::string out(es3Source ? es3Source : "");
#if defined(_WIN32)
  const std::string esVer = "#version 300 es";
  const size_t pos = out.find(esVer);
  if (pos != std::string::npos) {
    out.replace(pos, esVer.size(), "#version 330 core");
  }
  const std::string extLine = "#extension GL_EXT_blend_func_extended : enable";
  const size_t extPos = out.find(extLine);
  if (extPos != std::string::npos) {
    out.erase(extPos, extLine.size());
  }
  // OpenGL 3.3 Core includes ARB_blend_func_extended (layout index=1) as core,
  // and GLSL forbids defining user macros starting with "GL_".
  const std::string ifdefExt = "#ifdef GL_EXT_blend_func_extended";
  size_t ifdefPos = 0;
  while ((ifdefPos = out.find(ifdefExt, ifdefPos)) != std::string::npos) {
    out.replace(ifdefPos, ifdefExt.size(), "#if 1");
    ifdefPos += 5;
  }
#endif
  return out;
}

inline void checkGLError(const char* label) {
  if (__builtin_expect(!Logger::IsLevelEnabledFast(LogLevel::DEBUG), 1)) {
    return;
  }
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    GLIDE_LOG(CRITICAL, "GLES",
              "GL Error at " << label << ": 0x" << std::hex << err << std::dec);
  }
}

}  // namespace GlideWrapper
