#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#include <cstdint>

// All non-OpenGL-1.1 entry points used by OpenGLESBackend / Draw / Shaders /
// Textures. Loaded at WGL context creation via wglGetProcAddress() with
// opengl32.dll GetProcAddress() fallback — zero external loader dependencies.
#define GLIDE_WIN32_GL_FUNCS_X(X)                                      \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                           \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                             \
  X(PFNGLBINDBUFFERPROC, glBindBuffer)                                 \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                       \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                     \
  X(PFNGLBINDSAMPLERPROC, glBindSampler)                               \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                       \
  X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                           \
  X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)           \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                   \
  X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                       \
  X(PFNGLBUFFERDATAPROC, glBufferData)                                 \
  X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                           \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)         \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                           \
  X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D)             \
  X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D)       \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                           \
  X(PFNGLCREATESHADERPROC, glCreateShader)                             \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                           \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                 \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                           \
  X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)               \
  X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                         \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                             \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                 \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)       \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)       \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)             \
  X(PFNGLGENBUFFERSPROC, glGenBuffers)                                 \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                       \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                     \
  X(PFNGLGENSAMPLERSPROC, glGenSamplers)                               \
  X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                       \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                   \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                             \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                     \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                               \
  X(PFNGLGETSTRINGIPROC, glGetStringi)                                 \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                 \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                               \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)               \
  X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
  X(PFNGLSAMPLEMASKIPROC, glSampleMaski)                               \
  X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                   \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                             \
  X(PFNGLUNIFORM1FPROC, glUniform1f)                                   \
  X(PFNGLUNIFORM1FVPROC, glUniform1fv)                                 \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                   \
  X(PFNGLUNIFORM1IVPROC, glUniform1iv)                                 \
  X(PFNGLUNIFORM1UIPROC, glUniform1ui)                                 \
  X(PFNGLUNIFORM1UIVPROC, glUniform1uiv)                               \
  X(PFNGLUNIFORM2FPROC, glUniform2f)                                   \
  X(PFNGLUNIFORM3FPROC, glUniform3f)                                   \
  X(PFNGLUNIFORM3IPROC, glUniform3i)                                   \
  X(PFNGLUNIFORM4FPROC, glUniform4f)                                   \
  X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                 \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                 \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

namespace GlideWrapper {
namespace Win32Gl {

#define GLIDE_W32_DECL_(pfnType, name) extern pfnType pfn_##name;
GLIDE_WIN32_GL_FUNCS_X(GLIDE_W32_DECL_)
#undef GLIDE_W32_DECL_

bool LoadCoreFunctions();

}  // namespace Win32Gl
}  // namespace GlideWrapper

// Route calls cleanly to loaded pointers without colliding with GL headers.
#define glActiveTexture           ::GlideWrapper::Win32Gl::pfn_glActiveTexture
#define glAttachShader            ::GlideWrapper::Win32Gl::pfn_glAttachShader
#define glBindBuffer              ::GlideWrapper::Win32Gl::pfn_glBindBuffer
#define glBindFramebuffer         ::GlideWrapper::Win32Gl::pfn_glBindFramebuffer
#define glBindRenderbuffer        ::GlideWrapper::Win32Gl::pfn_glBindRenderbuffer
#define glBindSampler             ::GlideWrapper::Win32Gl::pfn_glBindSampler
#define glBindVertexArray         ::GlideWrapper::Win32Gl::pfn_glBindVertexArray
#define glBlendEquation           ::GlideWrapper::Win32Gl::pfn_glBlendEquation
#define glBlendEquationSeparate   ::GlideWrapper::Win32Gl::pfn_glBlendEquationSeparate
#define glBlendFuncSeparate       ::GlideWrapper::Win32Gl::pfn_glBlendFuncSeparate
#define glBlitFramebuffer         ::GlideWrapper::Win32Gl::pfn_glBlitFramebuffer
#define glBufferData              ::GlideWrapper::Win32Gl::pfn_glBufferData
#define glBufferSubData           ::GlideWrapper::Win32Gl::pfn_glBufferSubData
#define glCheckFramebufferStatus  ::GlideWrapper::Win32Gl::pfn_glCheckFramebufferStatus
#define glCompileShader           ::GlideWrapper::Win32Gl::pfn_glCompileShader
#define glCompressedTexImage2D    ::GlideWrapper::Win32Gl::pfn_glCompressedTexImage2D
#define glCompressedTexSubImage2D ::GlideWrapper::Win32Gl::pfn_glCompressedTexSubImage2D
#define glCreateProgram           ::GlideWrapper::Win32Gl::pfn_glCreateProgram
#define glCreateShader            ::GlideWrapper::Win32Gl::pfn_glCreateShader
#define glDeleteBuffers           ::GlideWrapper::Win32Gl::pfn_glDeleteBuffers
#define glDeleteFramebuffers      ::GlideWrapper::Win32Gl::pfn_glDeleteFramebuffers
#define glDeleteProgram           ::GlideWrapper::Win32Gl::pfn_glDeleteProgram
#define glDeleteRenderbuffers     ::GlideWrapper::Win32Gl::pfn_glDeleteRenderbuffers
#define glDeleteSamplers          ::GlideWrapper::Win32Gl::pfn_glDeleteSamplers
#define glDeleteShader            ::GlideWrapper::Win32Gl::pfn_glDeleteShader
#define glDeleteVertexArrays      ::GlideWrapper::Win32Gl::pfn_glDeleteVertexArrays
#define glEnableVertexAttribArray ::GlideWrapper::Win32Gl::pfn_glEnableVertexAttribArray
#define glFramebufferRenderbuffer ::GlideWrapper::Win32Gl::pfn_glFramebufferRenderbuffer
#define glFramebufferTexture2D    ::GlideWrapper::Win32Gl::pfn_glFramebufferTexture2D
#define glGenBuffers              ::GlideWrapper::Win32Gl::pfn_glGenBuffers
#define glGenFramebuffers         ::GlideWrapper::Win32Gl::pfn_glGenFramebuffers
#define glGenRenderbuffers        ::GlideWrapper::Win32Gl::pfn_glGenRenderbuffers
#define glGenSamplers             ::GlideWrapper::Win32Gl::pfn_glGenSamplers
#define glGenVertexArrays         ::GlideWrapper::Win32Gl::pfn_glGenVertexArrays
#define glGetProgramInfoLog       ::GlideWrapper::Win32Gl::pfn_glGetProgramInfoLog
#define glGetProgramiv            ::GlideWrapper::Win32Gl::pfn_glGetProgramiv
#define glGetShaderInfoLog        ::GlideWrapper::Win32Gl::pfn_glGetShaderInfoLog
#define glGetShaderiv             ::GlideWrapper::Win32Gl::pfn_glGetShaderiv
#define glGetStringi              ::GlideWrapper::Win32Gl::pfn_glGetStringi
#define glGetUniformLocation      ::GlideWrapper::Win32Gl::pfn_glGetUniformLocation
#define glLinkProgram             ::GlideWrapper::Win32Gl::pfn_glLinkProgram
#define glRenderbufferStorage     ::GlideWrapper::Win32Gl::pfn_glRenderbufferStorage
#define glRenderbufferStorageMultisample ::GlideWrapper::Win32Gl::pfn_glRenderbufferStorageMultisample
#define glSampleMaski             ::GlideWrapper::Win32Gl::pfn_glSampleMaski
#define glSamplerParameteri       ::GlideWrapper::Win32Gl::pfn_glSamplerParameteri
#define glShaderSource            ::GlideWrapper::Win32Gl::pfn_glShaderSource
#define glUniform1f               ::GlideWrapper::Win32Gl::pfn_glUniform1f
#define glUniform1fv              ::GlideWrapper::Win32Gl::pfn_glUniform1fv
#define glUniform1i               ::GlideWrapper::Win32Gl::pfn_glUniform1i
#define glUniform1iv              ::GlideWrapper::Win32Gl::pfn_glUniform1iv
#define glUniform1ui              ::GlideWrapper::Win32Gl::pfn_glUniform1ui
#define glUniform1uiv             ::GlideWrapper::Win32Gl::pfn_glUniform1uiv
#define glUniform2f               ::GlideWrapper::Win32Gl::pfn_glUniform2f
#define glUniform3f               ::GlideWrapper::Win32Gl::pfn_glUniform3f
#define glUniform3i               ::GlideWrapper::Win32Gl::pfn_glUniform3i
#define glUniform4f               ::GlideWrapper::Win32Gl::pfn_glUniform4f
#define glUniform4fv              ::GlideWrapper::Win32Gl::pfn_glUniform4fv
#define glUseProgram              ::GlideWrapper::Win32Gl::pfn_glUseProgram
#define glVertexAttribPointer     ::GlideWrapper::Win32Gl::pfn_glVertexAttribPointer

// Universal OpenGL 1.1 shim for ES glClearDepthf (avoids GL 4.1 requirement).
static inline void glClearDepthf(GLfloat d) {
  glClearDepth(static_cast<GLdouble>(d));
}

#endif  // _WIN32
