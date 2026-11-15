#pragma once

#include <GLES3/gl32.h>

#include "core/Logger.h"

// Universal fallback definition for BGRA extension format if header is missing
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

namespace GlideWrapper {

extern const char* vsSource;
extern const char* fsSource;

inline void checkGLError(const char* label) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    GLIDE_LOG(CRITICAL, "GLES",
              "GL Error at " << label << ": 0x" << std::hex << err << std::dec);
  }
}

}  // namespace GlideWrapper
