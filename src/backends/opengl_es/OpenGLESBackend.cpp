#include "OpenGLESBackend.h"
#include "OpenGLESInternal.h"

#if !defined(_WIN32)
#include <SDL2/SDL.h>
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
#include <dlfcn.h>
#if defined(HAVE_WAYLAND_EGL)
#include <wayland-egl.h>
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(HAVE_X11)
#include <X11/Xlib.h>
#ifdef None
#undef None
#endif
#ifdef Success
#undef Success
#endif
#ifdef Always
#undef Always
#endif
#ifdef Status
#undef Status
#endif
#ifdef Bool
#undef Bool
#endif
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
#ifndef EGL_PLATFORM_X11_EXT
#define EGL_PLATFORM_X11_EXT 0x31D5
#endif
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#endif

namespace GlideWrapper {

#if defined(_WIN32)
namespace Win32Gl {

#define GLIDE_W32_DEF_(pfnType, name) pfnType pfn_##name = nullptr;
GLIDE_WIN32_GL_FUNCS_X(GLIDE_W32_DEF_)
#undef GLIDE_W32_DEF_

namespace {
void* ResolveWin32GlSymbol(HMODULE opengl32Mod, const char* name) {
  PROC proc = wglGetProcAddress(name);
  const uintptr_t addr = reinterpret_cast<uintptr_t>(proc);
  if (addr == 0 || addr == 1 || addr == 2 || addr == 3 ||
      addr == static_cast<uintptr_t>(-1)) {
    if (opengl32Mod) {
      proc = GetProcAddress(opengl32Mod, name);
    } else {
      proc = nullptr;
    }
  }
  return reinterpret_cast<void*>(proc);
}
}  // namespace

bool LoadCoreFunctions() {
  HMODULE opengl32Mod = GetModuleHandleA("opengl32.dll");
  bool allOk = true;
#define GLIDE_W32_LOAD_(pfnType, name)                                     \
  do {                                                                     \
    void* sym = ResolveWin32GlSymbol(opengl32Mod, #name);                  \
    std::memcpy(&pfn_##name, &sym, sizeof(pfn_##name));                    \
    if (!pfn_##name) {                                                     \
      GLIDE_LOG(CRITICAL, "GLES",                                          \
                "WGL missing required OpenGL function: " << #name);        \
      allOk = false;                                                       \
    }                                                                      \
  } while (0);
  GLIDE_WIN32_GL_FUNCS_X(GLIDE_W32_LOAD_)
#undef GLIDE_W32_LOAD_
  return allOk;
}

}  // namespace Win32Gl
#endif

bool OpenGLESBackend::Initialize(const WrapperConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) return true;

  GLIDE_LOG(INFO, "GLES", "Initializing OpenGL ES 3.2 staging context...");
  m_config = config;
  m_msaaSamples = config.msaaSamples;

  uint32_t vboSizeMb = (m_config.glesVboSizeMb > 0) ? m_config.glesVboSizeMb : 8;
  m_geomVBOSizeConfigured = static_cast<size_t>(vboSizeMb) * 1024 * 1024;
  GLIDE_LOG(INFO, "GLES",
            "Configured GLES Geometry VBO size to "
                << vboSizeMb << " MB (" << m_geomVBOSizeConfigured
                << " bytes)");

  m_initialized = true;
  return true;
}

void OpenGLESBackend::Shutdown() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) return;

  GLIDE_LOG(INFO, "GLES", "Shutting down OpenGL ES 3.2 staging context.");
  DetachWindow_Locked();
  m_initialized = false;
}

void OpenGLESBackend::ResetState() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetState_Locked();
}

void OpenGLESBackend::InvalidateGlBindingCache() {
  m_cachedBoundFbo = 0xFFFFFFFFu;
  m_cachedBoundProgram = 0xFFFFFFFFu;
  m_cachedBoundVao = 0xFFFFFFFFu;
  m_cachedBoundVbo = 0xFFFFFFFFu;
  m_cachedBoundTex[0] = 0xFFFFFFFFu;
  m_cachedBoundTex[1] = 0xFFFFFFFFu;
  m_cachedBoundSampler[0] = 0xFFFFFFFFu;
  m_cachedBoundSampler[1] = 0xFFFFFFFFu;
  m_cachedActiveTexUnit = 0xFFFFFFFFu;
  m_cachedViewportValid = false;
}

void OpenGLESBackend::ResetState_Locked() {
  GlideStateTracker::ResetState_Locked();
  m_lfbStaging.ResetPendingAndDirty();
  m_geomVBOOffset = 0;
  m_is2DGeometry = true;
  m_gammaCorrectionValue = 1.0f;
  m_useGammaLut = false;
  m_glesTextures[0].clear();  // Clear stale texture caches to prevent reusing
  m_glesTextures[1].clear();  // dead GL texture IDs in new contexts!
  m_batchVertices.clear();
  m_batchPrimitiveMode = BatchPrimitiveMode::None;
  m_batchIs2DGeometry = false;
  m_glesDirtyMask = kGlesDirtyAll;
  InvalidateGlBindingCache();
}

bool OpenGLESBackend::AttachWindow(void* nativeWindowHandle, uint32_t width,
                                   uint32_t height, bool windowed) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    GLIDE_LOG(CRITICAL, "GLES", "AttachWindow failed: !m_initialized");
    return false;
  }
  if (m_windowAttached) DetachWindow_Locked();

  GLIDE_LOG(INFO, "GLES",
            "Attaching OpenGL ES window surface ("
                << width << "x" << height
                << "), Windowed=" << (windowed ? "Yes" : "No"));

  m_headlessWidth = width;
  m_headlessHeight = height;
  m_guestWidth = width;
  m_guestHeight = height;
  ResetState_Locked();
  m_headlessMode = true;  // Force offscreen rendering unconditionally for
                          // hybrid presentation
  m_msaaSamples = ResolveDesiredMsaaSamples_Locked();

  if (!CreateGLContext(nativeWindowHandle, width, height, windowed)) {
    GLIDE_LOG(CRITICAL, "GLES", "Failed to create OpenGL ES 3.2 context!");
    return false;
  }

  // Detect glSampleMaski support (core in Desktop GL 3.2+ and GLES 3.1+).
#if defined(_WIN32)
  m_supportsSampleMask = (Win32Gl::pfn_glSampleMaski != nullptr);
  glEnable(GL_MULTISAMPLE);
#else
  GLint glMajor = 3, glMinor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
  glGetIntegerv(GL_MINOR_VERSION, &glMinor);
  m_supportsSampleMask = (glMajor > 3) || (glMajor == 3 && glMinor >= 1);
#endif

  // Presentation is now handled GPU-natively via presentation quad and
  // SwapBuffers. No SDL_Renderer or SDL_Texture is required on the window.

  // Dynamic queries of the real hardware renderer!
  const char* renderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

  std::ostringstream logStream;
  logStream << "\n--- Active OpenGL ES Execution Adapter ---\n"
            << "  Adapter     : "
            << (renderer ? renderer : "Generic GLES Device") << " ("
            << (vendor ? vendor : "Unknown") << ")\n"
            << "    Driver    : " << (version ? version : "OpenGL ES 3.2")
            << "\n"
            << "    Pixel Fmt : RGBA8888 D24S8 nAux 1\n"
            << "------------------------------------------";
  GLIDE_LOG(DEBUG, "GLES", logStream.str());

  // Milestone 1: Setup Offscreen Framebuffer Objects (FBOs) for GPU-native
  // rendering (Double-Buffered 1x resolved/aliased targets)
  glGenFramebuffers(2, m_headlessFBOs);
  glGenRenderbuffers(1, &m_sharedDepthRenderbuffer);

  // Allocate single shared packed depth24+stencil8 renderbuffer with the
  // active context dimensions (supports VSA-100 PIXEXT hardware stencil).
  glBindRenderbuffer(GL_RENDERBUFFER, m_sharedDepthRenderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

  for (int i = 0; i < 2; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[i]);

    // Attach our color offscreen GPU rendering texture as GL_COLOR_ATTACHMENT0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_fboTextures[i], 0);

    // Attach the single shared depth+stencil renderbuffer to both FBOs
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_sharedDepthRenderbuffer);

    // Verify FBO completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      GLIDE_LOG(CRITICAL, "GLES", "Headless FBO " << i << " is incomplete!");
      DestroyGLContext();
      return false;
    }

    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFFu);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  }

  // Allocate offscreen MSAA FBOs when MSAA > 1 (supports grSstWinOpenExt
  // VSA-100 Spatial AA, live GR_AA_MULTI_SAMPLE toggling, and T-Buffer masks).
  const uint32_t desiredMsaa = m_msaaSamples;
  m_msaaSamples = 1;
  m_msaaColorDirty[0] = m_msaaColorDirty[1] = false;
  m_singleSampleColorDirty[0] = m_singleSampleColorDirty[1] = false;

  if (desiredMsaa > 1) {
    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    uint32_t targetSamples = 1;
    if (maxSamples >= 2) {
      targetSamples =
          std::min<uint32_t>(desiredMsaa, static_cast<uint32_t>(maxSamples));
      if (targetSamples >= 8) {
        targetSamples = 8;
      } else if (targetSamples >= 4) {
        targetSamples = 4;
      } else if (targetSamples >= 2) {
        targetSamples = 2;
      } else {
        targetSamples = 1;
      }
    }

    if (targetSamples > 1) {
      glGenFramebuffers(2, m_msaaFBOs);
      glGenRenderbuffers(2, m_msaaColorRBOs);
      glGenRenderbuffers(1, &m_msaaDepthRBO);

      glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRBO);
      glRenderbufferStorageMultisample(
          GL_RENDERBUFFER, static_cast<GLsizei>(targetSamples),
          GL_DEPTH24_STENCIL8, static_cast<GLsizei>(width),
          static_cast<GLsizei>(height));

      bool msaaComplete = true;
      for (int i = 0; i < 2; ++i) {
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRBOs[i]);
        glRenderbufferStorageMultisample(
            GL_RENDERBUFFER, static_cast<GLsizei>(targetSamples), GL_RGBA8,
            static_cast<GLsizei>(width), static_cast<GLsizei>(height));

        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBOs[i]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, m_msaaColorRBOs[i]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_msaaDepthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
          msaaComplete = false;
          break;
        }

        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearDepthf(1.0f);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
      }

      if (msaaComplete) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBOs[0]);
        GLint actualSamples = 0;
        glGetIntegerv(GL_SAMPLES, &actualSamples);
        m_msaaSamples = (actualSamples > 1)
                            ? static_cast<uint32_t>(actualSamples)
                            : targetSamples;
        GLIDE_LOG(INFO, "GLES",
                  "Offscreen MSAA FBOs initialized: "
                      << m_msaaSamples << "x MSAA (requested=" << desiredMsaa
                      << ", max=" << maxSamples << ", sampleMask="
                      << (m_supportsSampleMask ? "yes" : "no") << ").");
      } else {
        GLIDE_LOG(WARN, "GLES",
                  "Offscreen MSAA FBO incomplete for "
                      << targetSamples << "x; falling back to 1x.");
        glDeleteRenderbuffers(1, &m_msaaDepthRBO);
        glDeleteRenderbuffers(2, m_msaaColorRBOs);
        glDeleteFramebuffers(2, m_msaaFBOs);
        m_msaaDepthRBO = 0;
        m_msaaColorRBOs[0] = m_msaaColorRBOs[1] = 0;
        m_msaaFBOs[0] = m_msaaFBOs[1] = 0;
        m_msaaSamples = 1;
      }
    }
  }

  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  // Restore default framebuffer binding
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Print dynamic startup telemetry info with the actual offscreen FBO samples
  {
    const uint32_t samplesVal = (m_msaaSamples > 1) ? m_msaaSamples : 0;
    std::cout << "Info: Pixel Format RGBA8888 D24S8 nAux 0 nSamples "
              << samplesVal << " " << samplesVal << "\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n"
              << std::flush;
  }

  GLIDE_LOG(INFO, "GLES",
            "Double-buffered offscreen FBOs & Depth Renderbuffers initialized "
            "successfully.");

  // Disable dithering to ensure 100% flat and mathematically consistent color
  // outputs, preventing 1-LSB noise from failing precision regression tests.
  glDisable(GL_DITHER);

  // Milestone 3: Compile shaders and allocate dynamic VBO/VAO
  if (!CreateGeomShaders()) {
    GLIDE_LOG(CRITICAL, "GLES", "Failed to compile GLES geometry shaders!");
    DestroyGLContext();
    return false;
  }
  if (!CreateGeomBuffers()) {
    GLIDE_LOG(CRITICAL, "GLES", "Failed to allocate GLES geometry buffers!");
    DestroyGLContext();
    return false;
  }

  // Configure lazily-allocated CPU LFB staging (0 bytes until LFB is touched)
  m_lfbStaging.Configure(width, height, /*allocateEagerly=*/false);
  m_activeRenderBuffer = 1;  // GR_BUFFER_BACKBUFFER
  m_swizzleBuffer.clear();
  m_swizzleBuffer.shrink_to_fit();

  SetClipWindow_Locked(0, 0, width, height);

  m_hudOverlay.Configure(m_config, {"GLES", width, height});
  m_hudUploadedRev = 0;

  m_windowAttached = true;

  // Preserve any foreign host EGL context (e.g. DOSBox-X output=opengl on
  // Wayland) when returning from grSstWinOpen.
  RestoreHostContextIfSaved();
  return true;
}

void OpenGLESBackend::DetachWindow() {
  std::lock_guard<std::mutex> lock(m_mutex);
  DetachWindow_Locked();
}

void OpenGLESBackend::DetachWindow_Locked() {
  if (!m_windowAttached) return;
  GLIDE_LOG(INFO, "GLES",
            "Detaching OpenGL ES window surface and freeing buffers.");

  DestroyGLContext();

  m_lfbStaging.Release();
  m_swizzleBuffer.clear();
  m_swizzleBuffer.shrink_to_fit();

  m_windowAttached = false;
}

void OpenGLESBackend::PresentToWindowSurface(uint32_t hostW, uint32_t hostH) {
  if (hostW == 0 || hostH == 0) return;

  GLIDE_PROFILE_SCOPE("GLES::SwapBuffers_PresentQuad");

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Neutralize guest rasterizer state for clean presentation blit.
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  if (m_supportsSampleMask) {
    glDisable(GL_SAMPLE_MASK);
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);

  // Clear full host surface to opaque black for letterbox/pillarbox bars.
  glViewport(0, 0, static_cast<GLsizei>(hostW), static_cast<GLsizei>(hostH));
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Compute aspect-ratio-preserving destination rect inside host surface.
  const PresentRect dst = NativeSurfaceHost::ComputePresentRect(
      m_headlessWidth, m_headlessHeight, hostW, hostH,
      /*preserveAspect=*/true);
  const int32_t glY =
      static_cast<int32_t>(hostH) - dst.y - static_cast<int32_t>(dst.height);
  glViewport(dst.x, glY, static_cast<GLsizei>(dst.width),
             static_cast<GLsizei>(dst.height));

  // Unbind guest TMU sampler objects so the presentation & Gamma LUT textures
  // use their own texture parameters.
  glBindSampler(0, 0);
  glBindSampler(1, 0);

  glUseProgram(m_glProgram);
  if (m_uUseGammaLutLoc != -1) {
    glUniform1i(m_uUseGammaLutLoc, m_useGammaLut ? 1 : 0);
  }
  if (m_uPreserveAlphaLoc != -1) {
    glUniform1i(m_uPreserveAlphaLoc, 0);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_fboTextures[0]);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_glGammaLutTex);

  glBindVertexArray(m_glVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  if (m_hudOverlay.IsEnabled()) {
    m_hudOverlay.Update(TelemetryManager::GetInstance().GetLastFrameMetrics());
    const uint8_t* hudPixels = m_hudOverlay.GetRgbaBottomUp();
    const uint32_t hudW = m_hudOverlay.GetWidth();
    const uint32_t hudH = m_hudOverlay.GetHeight();
    if (hudPixels && hudW > 0 && hudH > 0) {
      if (m_glHudTex == 0) {
        glGenTextures(1, &m_glHudTex);
      }
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m_glHudTex);
      if (m_hudOverlay.GetRevision() != m_hudUploadedRev) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(hudW),
                     static_cast<GLsizei>(hudH), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     hudPixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_hudUploadedRev = m_hudOverlay.GetRevision();
      }
      int32_t hx = 0;
      int32_t hy = 0;
      uint32_t hw = 0;
      uint32_t hh = 0;
      m_hudOverlay.ComputePlacement(dst.x, dst.y, dst.width, dst.height,
                                    hx, hy, hw, hh);
      if (hw > 0 && hh > 0) {
        const int32_t glHudY =
            static_cast<int32_t>(hostH) - hy - static_cast<int32_t>(hh);
        glViewport(hx, glHudY, static_cast<GLsizei>(hw),
                   static_cast<GLsizei>(hh));
        if (m_uUseGammaLutLoc != -1) {
          glUniform1i(m_uUseGammaLutLoc, 0);
        }
        if (m_uPreserveAlphaLoc != -1) {
          glUniform1i(m_uPreserveAlphaLoc, 1);
        }
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO,
                            GL_ONE);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisable(GL_BLEND);
      }
    }
  }

  glBindVertexArray(0);
  glUseProgram(0);
  glActiveTexture(GL_TEXTURE0);

  checkGLError("PresentToWindowSurface");
}

bool OpenGLESBackend::SwapBuffers() {
  std::unique_lock<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  // Pump events ONLY in standalone mode (guaranteed no-op for embedded hosts).
  m_surfaceHost.PollEvents(/*force=*/false);
  if (m_surfaceHost.IsStandaloneMode() && m_surfaceHost.WasQuitRequested()) {
    GLIDE_LOG(INFO, "GLES",
              "Standalone window close requested; exiting cleanly.");
    DetachWindow_Locked();
    lock.unlock();
    std::exit(0);
  }

  if (!EnsureContextCurrent()) {
    return false;
  }

  // Flush any pending geometry batches first!
  FlushBatch();

  // Proactively flush any pending CPU LFB writes to the GPU FBO before
  // presenting/swapping!
  FlushLFBToGPU_Locked();

  // Resolve any pending MSAA draws into the 1x presentation/readback textures.
  ResolveMsaaToSingle_Locked(0u);
  if (m_msaaColorDirty[1]) {
    ResolveMsaaToSingle_Locked(1u);
  }

  GLIDE_PROFILE_SCOPE("GLES::SwapBuffers");

  const auto presentStart = std::chrono::steady_clock::now();
  if (m_surfaceHost.GetMode() != NativeSurfaceHost::Mode::Headless) {
    uint32_t hostW = 0;
    uint32_t hostH = 0;
    const bool resized = m_surfaceHost.SyncBounds(hostW, hostH);
    if (hostW == 0 || hostH == 0) {
      m_surfaceHost.GetDrawableSize(hostW, hostH);
    }

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND_EGL)
    if (resized && m_wlEglWindow && hostW > 0 && hostH > 0) {
      wl_egl_window_resize(static_cast<wl_egl_window*>(m_wlEglWindow),
                           static_cast<int>(hostW), static_cast<int>(hostH), 0,
                           0);
    }
#else
    (void)resized;
#endif

    PresentToWindowSurface(hostW, hostH);

    // Swap the presentation surface.
    if (m_useWgl) {
#if defined(_WIN32)
      if (m_wglHdc) {
        if (!::SwapBuffers(static_cast<HDC>(m_wglHdc))) {
          GLIDE_LOG(WARN, "GLES",
                    "Win32 SwapBuffers failed: err=" << GetLastError());
        }
      }
#endif
    } else if (m_useDirectEgl) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
      if (m_eglDisplay && m_eglSurface) {
        if (!eglSwapBuffers(static_cast<EGLDisplay>(m_eglDisplay),
                            static_cast<EGLSurface>(m_eglSurface))) {
          GLIDE_LOG(WARN, "GLES",
                    "eglSwapBuffers failed: 0x" << std::hex << eglGetError()
                                                << std::dec);
        }
      }
#endif
    }
#if !defined(_WIN32)
    else if (m_surfaceHost.GetSDLWindow()) {
      SDL_GL_SwapWindow(m_surfaceHost.GetSDLWindow());
    }
#endif
  } else {
    // Headless mode: ensure offscreen GPU work completes without forcing an
    // eager CPU readback (ReadLFB reads directly from the GPU FBO on demand).
    glFinish();
  }

  // Swap CPU LFB staging indices for double-buffering (zero-cost when unused)
  m_lfbStaging.SwapBuffers();

  // Swap the GPU color targets for physical double-buffering!
  std::swap(m_headlessFBOs[0], m_headlessFBOs[1]);
  std::swap(m_fboTextures[0], m_fboTextures[1]);
  std::swap(m_msaaFBOs[0], m_msaaFBOs[1]);
  std::swap(m_msaaColorRBOs[0], m_msaaColorRBOs[1]);
  std::swap(m_msaaColorDirty[0], m_msaaColorDirty[1]);
  std::swap(m_singleSampleColorDirty[0], m_singleSampleColorDirty[1]);

  // Bind the new active drawing FBO and restore guest rasterizer state
  glBindFramebuffer(GL_FRAMEBUFFER, GetActiveGpuFbo());
  glViewport(0, 0, static_cast<GLsizei>(m_headlessWidth),
             static_cast<GLsizei>(m_headlessHeight));
  glEnable(GL_SCISSOR_TEST);
  ApplyScissorGLES_Locked();
  ApplyColorMaskGLES();
  SyncDepthState();
  SyncStencilStateGLES_Locked();
  SyncCullStateGLES_Locked();
  ApplyBlendingGLES();
  ApplySampleMaskGLES_Locked();

  // Invalidate cached GL object bindings for next frame's first draw
  // (m_geomProgram uniforms persist in our private EGL/GL context across
  // frames, so no full 43-uniform re-upload is needed).
  InvalidateGlBindingCache();

  // Restore any foreign host EGL context so host OpenGL rendering (e.g.
  // DOSBox-X output=opengl on Wayland) runs cleanly between Glide frames.
  RestoreHostContextIfSaved();

  const auto presentUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - presentStart)
                             .count();
  const float maxFps = m_config.maxFps;
  if (m_surfaceHost.IsStandaloneMode()) {
    m_surfaceHost.PollEvents(/*force=*/false);
  }
  lock.unlock();

  auto& telemetry = TelemetryManager::GetInstance();
  telemetry.RecordPresentTimeUs(presentUs);
  auto& tracker = telemetry.GetFrameTracker();
  tracker.MarkFrameEnd(maxFps);
  tracker.MarkFrameStart();

  return true;
}

void OpenGLESBackend::PollEvents() {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Safe no-op in Headless, X11Child, and WaylandSubsurface modes; throttled
  // SDL event pumping in StandaloneSDL2 mode.
  m_surfaceHost.PollEvents(/*force=*/false);
}

void OpenGLESBackend::SstIdle() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized && m_windowAttached && EnsureContextCurrent()) {
    FlushBatch();
    // Blocks the host CPU until all GLES commands and drawing dispatches
    // have been fully processed by the hardware GPU!
    glFinish();
  }
}

void* OpenGLESBackend::GetLfbWritePtr() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return nullptr;
  return m_lfbStaging.GetWritePtr(m_activeRenderBuffer);
}

bool OpenGLESBackend::WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                               uint32_t srcWidth, uint32_t srcHeight,
                               int32_t srcStride, uint32_t srcFmt,
                               const void* srcData, const void* refData,
                               const LfbPipelineConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return false;

  // Fast path: opaque LFB writes and standard (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
  // overlays composite directly onto the GPU FBO via a single alpha-blended
  // quad with ZERO GPU readback stalls, preserving any 3D geometry underneath!
  if (CpuLfbStaging::CanUseOverlayQuad(config)) {
    return m_lfbStaging.WriteOverlayLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                                        srcStride, srcFmt, srcData, refData,
                                        config, m_pixelFormatOverride);
  }

  // Fallback for exotic LFB blend modes (e.g., additive/multiply): snapshot the
  // current GPU FBO into CpuLfbStaging once so CPU blending sees the 3D scene.
  if (!m_lfbStaging.IsFullBufferDirty(buffer) && EnsureContextCurrent()) {
    FlushBatch();
    FlushLFBToGPU_Locked();
    const uint32_t readSlot = (buffer == 0) ? 1u : 0u;
    ResolveMsaaToSingle_Locked(readSlot);
    if (void* readDst = m_lfbStaging.PrepareBufferForGpuReadback(buffer)) {
      GLint prevFBO = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
      const GLuint fboToRead = m_headlessFBOs[readSlot];
      glBindFramebuffer(GL_FRAMEBUFFER, fboToRead);
      glReadPixels(0, 0, m_headlessWidth, m_headlessHeight, GL_RGBA,
                   GL_UNSIGNED_BYTE, readDst);
      auto* dest = reinterpret_cast<uint32_t*>(readDst);
      for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
        uint32_t rgba = dest[i];
        dest[i] = (rgba & 0xFF00FF00u) | ((rgba & 0xFFu) << 16) |
                  ((rgba >> 16) & 0xFFu);
      }
      std::vector<uint32_t> rowBuf(m_headlessWidth);
      for (uint32_t y = 0; y < m_headlessHeight / 2; ++y) {
        uint32_t* topRow = dest + static_cast<size_t>(y) * m_headlessWidth;
        uint32_t* botRow =
            dest + static_cast<size_t>(m_headlessHeight - 1 - y) * m_headlessWidth;
        std::memcpy(rowBuf.data(), topRow, m_headlessWidth * sizeof(uint32_t));
        std::memcpy(topRow, botRow, m_headlessWidth * sizeof(uint32_t));
        std::memcpy(botRow, rowBuf.data(), m_headlessWidth * sizeof(uint32_t));
      }
      glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
      InvalidateGlBindingCache();
    }
  }

  return m_lfbStaging.WriteLFB(buffer, dstX, dstY, srcWidth, srcHeight,
                               srcStride, srcFmt, srcData, refData, config,
                               m_pixelFormatOverride);
}

void OpenGLESBackend::FlushLFBToGPU_Locked() {
  if (!m_lfbStaging.IsDirty()) return;
  GLIDE_CPU_SCOPE(CpuBucket::Lfb);
  if (!EnsureContextCurrent()) return;

  glActiveTexture(GL_TEXTURE0);

  // Mapping invariant (matching GetActiveGpuFbo() & ReadLFB()):
  //   logical 0 (GR_BUFFER_FRONTBUFFER) -> m_headlessFBOs[1] / m_fboTextures[1]
  //   logical 1 (GR_BUFFER_BACKBUFFER)  -> m_headlessFBOs[0] / m_fboTextures[0]
  const uint32_t gpuTextures[2] = {m_fboTextures[1], m_fboTextures[0]};
  const uint32_t gpuFbos[2] = {m_headlessFBOs[1], m_headlessFBOs[0]};

  // 1. Full-buffer upload path (GetLfbWritePtr or exotic blend fallback).
  if (m_lfbStaging.IsFullBufferDirty(0) || m_lfbStaging.IsFullBufferDirty(1)) {
    const size_t requiredPixels =
        static_cast<size_t>(m_headlessWidth) * m_headlessHeight;
    if (m_swizzleBuffer.size() < requiredPixels) {
      m_swizzleBuffer.resize(requiredPixels);
    }
    auto flushFullBuffer = [this](const uint8_t* cpuBufferData,
                                  uint32_t gpuTexture) {
      const auto* src = reinterpret_cast<const uint32_t*>(cpuBufferData);
      if (!src || m_swizzleBuffer.empty()) return;
      for (uint32_t y = 0; y < m_headlessHeight; ++y) {
        const uint32_t dstY = m_headlessHeight - 1 - y;
        const uint32_t* srcRow = src + static_cast<size_t>(y) * m_headlessWidth;
        uint32_t* dstRow =
            m_swizzleBuffer.data() + static_cast<size_t>(dstY) * m_headlessWidth;
        for (uint32_t x = 0; x < m_headlessWidth; ++x) {
          uint32_t bgra = srcRow[x];
          dstRow[x] = (bgra & 0xFF00FF00u) | ((bgra & 0xFFu) << 16) |
                      ((bgra >> 16) & 0xFFu);
        }
      }
      glBindTexture(GL_TEXTURE_2D, gpuTexture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_headlessWidth, m_headlessHeight,
                      GL_RGBA, GL_UNSIGNED_BYTE, m_swizzleBuffer.data());
    };

    for (uint32_t buf = 0; buf < 2; ++buf) {
      if (m_lfbStaging.IsFullBufferDirty(buf)) {
        if (const uint8_t* data = m_lfbStaging.PrepareBufferForGpuUpload(buf)) {
          flushFullBuffer(data, gpuTextures[buf]);
          checkGLError("FlushLFBToGPU FullBuffer glTexSubImage2D");
          if (m_msaaSamples > 1) {
            const uint32_t slot = (buf == 0) ? 1u : 0u;
            m_singleSampleColorDirty[slot] = true;
            m_msaaColorDirty[slot] = false;
          }
        }
        m_lfbStaging.ClearBufferDirty(buf);
      }
    }
  }

  // 2. Sparse alpha-masked overlay composite path (zero readback, preserves 3D!).
  if (m_lfbStaging.IsOverlayDirty(0) || m_lfbStaging.IsOverlayDirty(1)) {
    // Resolve any pending MSAA 3D content into the 1x target BEFORE compositing
    // the 2D LFB overlay on top of it.
    for (uint32_t buf = 0; buf < 2; ++buf) {
      if (m_lfbStaging.IsOverlayDirty(buf)) {
        const uint32_t slot = (buf == 0) ? 1u : 0u;
        ResolveMsaaToSingle_Locked(slot);
      }
    }
    if (m_glLfbOverlayTex == 0 || m_glLfbOverlayWidth != m_headlessWidth ||
        m_glLfbOverlayHeight != m_headlessHeight) {
      if (m_glLfbOverlayTex == 0) {
        glGenTextures(1, &m_glLfbOverlayTex);
      }
      glBindTexture(GL_TEXTURE_2D, m_glLfbOverlayTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                   static_cast<GLsizei>(m_headlessWidth),
                   static_cast<GLsizei>(m_headlessHeight), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      m_glLfbOverlayWidth = m_headlessWidth;
      m_glLfbOverlayHeight = m_headlessHeight;
    }

    bool overlayStateBound = false;
    for (uint32_t buf = 0; buf < 2; ++buf) {
      const CpuLfbStaging::DirtyBand* bands = nullptr;
      size_t bandCount = 0;
      const uint32_t* overlay =
          m_lfbStaging.GetOverlayBandsForGpuUpload(buf, bands, bandCount);
      if (!overlay || !bands || bandCount == 0) continue;

      glActiveTexture(GL_TEXTURE0);
      glBindSampler(0, 0);
      glBindTexture(GL_TEXTURE_2D, m_glLfbOverlayTex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

      // Upload ONLY the disjoint dirty row bands (skipping untouched rows).
      for (size_t bi = 0; bi < bandCount; ++bi) {
        const uint32_t minY = bands[bi].minY;
        const uint32_t maxY = bands[bi].maxY;
        if (minY > maxY || maxY >= m_headlessHeight) continue;

        const uint32_t numRows = maxY - minY + 1;
        const size_t bandPixels = static_cast<size_t>(numRows) * m_headlessWidth;
        if (m_swizzleBuffer.size() < bandPixels) {
          m_swizzleBuffer.resize(bandPixels);
        }

        // Flip dirty band [minY..maxY] top-down -> bottom-up and swizzle ARGB -> RGBA.
        for (uint32_t rIdx = 0; rIdx < numRows; ++rIdx) {
          const uint32_t srcY = maxY - rIdx;
          const uint32_t* srcRow = overlay + static_cast<size_t>(srcY) * m_headlessWidth;
          uint32_t* dstRow =
              m_swizzleBuffer.data() + static_cast<size_t>(rIdx) * m_headlessWidth;
          for (uint32_t x = 0; x < m_headlessWidth; ++x) {
            uint32_t argb = srcRow[x];
            dstRow[x] = (argb & 0xFF00FF00u) | ((argb & 0xFFu) << 16) |
                        ((argb >> 16) & 0xFFu);
          }
        }

        const uint32_t glMinY = m_headlessHeight - 1 - maxY;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(glMinY),
                        static_cast<GLsizei>(m_headlessWidth),
                        static_cast<GLsizei>(numRows), GL_RGBA, GL_UNSIGNED_BYTE,
                        m_swizzleBuffer.data());
      }

      glBindFramebuffer(GL_FRAMEBUFFER, gpuFbos[buf]);
      if (!overlayStateBound) {
        glViewport(0, 0, static_cast<GLsizei>(m_headlessWidth),
                   static_cast<GLsizei>(m_headlessHeight));
        glEnable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        if (m_supportsSampleMask) {
          glDisable(GL_SAMPLE_MASK);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                            GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_glProgram);
        if (m_uUseGammaLutLoc != -1) {
          glUniform1i(m_uUseGammaLutLoc, 0);
        }
        if (m_uPreserveAlphaLoc != -1) {
          glUniform1i(m_uPreserveAlphaLoc, 1);
        }
        glBindVertexArray(m_glVAO);
        overlayStateBound = true;
      }

      // Composite each dirty band tightly with scissor so untouched rows in
      // m_glLfbOverlayTex are never sampled.
      for (size_t bi = 0; bi < bandCount; ++bi) {
        const uint32_t minY = bands[bi].minY;
        const uint32_t maxY = bands[bi].maxY;
        if (minY > maxY || maxY >= m_headlessHeight) continue;
        const uint32_t numRows = maxY - minY + 1;
        const uint32_t glMinY = m_headlessHeight - 1 - maxY;
        glScissor(0, static_cast<GLint>(glMinY),
                  static_cast<GLsizei>(m_headlessWidth),
                  static_cast<GLsizei>(numRows));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }

      m_lfbStaging.ClearOverlay(buf);
      if (m_msaaSamples > 1) {
        const uint32_t slot = (buf == 0) ? 1u : 0u;
        m_singleSampleColorDirty[slot] = true;
        m_msaaColorDirty[slot] = false;
      }
      checkGLError("FlushLFBToGPU OverlayQuad");
    }

    // Mark modified fixed-function state dirty so the next 3D FlushBatch lazily
    // restores it without redundant GL calls at SwapBuffers time.
    m_glesDirtyMask |= kGlesDirtyScissor | kGlesDirtyColorMask |
                       kGlesDirtyDepth | kGlesDirtyStencil | kGlesDirtyCull |
                       kGlesDirtyBlend | kGlesDirtySampleMask;
  }

  InvalidateGlBindingCache();
}

bool OpenGLESBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                              uint32_t srcWidth, uint32_t srcHeight,
                              uint32_t dstStride, void* dstData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached || !EnsureContextCurrent()) {
    return false;
  }

  // Flush any pending geometry batches before CPU readback!
  FlushBatch();

  // Flush any pending CPU LFB writes to the GPU first, ensuring they are
  // not obliterated by the subsequent glReadPixels readback!
  FlushLFBToGPU_Locked();

  // Resolve any pending MSAA draws into the 1x FBO before glReadPixels.
  const uint32_t readSlot = (buffer == 0) ? 1u : 0u;
  ResolveMsaaToSingle_Locked(readSlot);

  // --- STEP 1: GPU Readback Flush ---
  // Query the currently bound FBO to restore it at the end, preventing state
  // leakage!
  GLint previousFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);

  // Bind the correct offscreen FBO based on the requested buffer (0 = FRONT, 1
  // = BACK)
  GLuint fboToRead = m_headlessFBOs[readSlot];
  glBindFramebuffer(GL_FRAMEBUFFER, fboToRead);

  // Resolve the lazily-allocated destination CPU buffer for the requested slot
  void* readDst = m_lfbStaging.PrepareBufferForGpuReadback(buffer);

  if (readDst) {
    // Read pixels as GL_RGBA (which contains our R/B swapped BGRA-ordered
    // pixels) GLES glReadPixels is synchronous and guarantees the GPU finishes
    // all rendering before returning!
    glReadPixels(0, 0, m_headlessWidth, m_headlessHeight, GL_RGBA,
                 GL_UNSIGNED_BYTE, readDst);
    checkGLError("ReadLFB glReadPixels");

    // Convert RGBA from GPU glReadPixels to canonical 0xAARRGGBB (BGRA bytes)
    // and vertically flip the image in-place so CpuLfbStaging always holds
    // canonical ARGB pixels (required when LfbManager write-locks snapshot and
    // re-upload the buffer, e.g. grShamelessPlug under GR_COLORFORMAT_ABGR).
    auto* dest = reinterpret_cast<uint32_t*>(readDst);
    // 1. Always swizzle GL_RGBA -> canonical 0xAARRGGBB
    for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
      uint32_t rgba = dest[i];
      uint32_t r = rgba & 0xFFu;
      uint32_t g = (rgba >> 8) & 0xFFu;
      uint32_t b = (rgba >> 16) & 0xFFu;
      uint32_t a = (rgba >> 24) & 0xFFu;
      dest[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    // 2. Vertically flip rows to convert OpenGL's bottom-up layout to host's
    // top-down layout
    std::vector<uint32_t> rowBuffer(m_headlessWidth);
    for (uint32_t y = 0; y < m_headlessHeight / 2; ++y) {
      uint32_t topY = y;
      uint32_t bottomY = m_headlessHeight - 1 - y;
      uint32_t* topRow = dest + topY * m_headlessWidth;
      uint32_t* bottomRow = dest + bottomY * m_headlessWidth;
      std::memcpy(rowBuffer.data(), topRow, m_headlessWidth * 4);
      std::memcpy(topRow, bottomRow, m_headlessWidth * 4);
      std::memcpy(bottomRow, rowBuffer.data(), m_headlessWidth * 4);
    }
  }

  // Restore the previously bound framebuffer to prevent state leakage!
  glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
  InvalidateGlBindingCache();

  // --- STEP 2: Convert and copy to client ---
  if (!m_lfbStaging.ReadRegion(buffer, srcX, srcY, srcWidth, srcHeight,
                               dstStride, dstData)) {
    return false;
  }

  // Swizzle 32-bit client output from canonical ARGB -> ABGR only when the
  // window was opened with GR_COLORFORMAT_ABGR (matches SoftwareBackend).
  if (m_pixelFormatOverride == 1 && dstData && srcWidth > 0 &&
      dstStride >= srcWidth * 4) {
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);
    for (uint32_t y = 0; y < srcHeight; ++y) {
      auto* row32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
      for (uint32_t x = 0; x < srcWidth; ++x) {
        uint32_t val = row32[x];
        uint32_t a = val & 0xFF000000u;
        uint32_t g = val & 0x0000FF00u;
        uint32_t r = (val >> 16) & 0xFFu;
        uint32_t b = val & 0xFFu;
        row32[x] = a | (b << 16) | g | r;
      }
    }
  }
  return true;
}

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL) && \
    defined(HAVE_X11)
namespace {
struct GlxInterop {
  using PfnGetCurrentContext = void* (*)();
  using PfnGetCurrentDisplay = void* (*)();
  using PfnGetCurrentDrawable = unsigned long (*)();
  using PfnGetCurrentReadDrawable = unsigned long (*)();
  using PfnMakeContextCurrent = int (*)(void*, unsigned long, unsigned long,
                                        void*);
  using PfnMakeCurrent = int (*)(void*, unsigned long, void*);

  PfnGetCurrentContext getCurrentContext = nullptr;
  PfnGetCurrentDisplay getCurrentDisplay = nullptr;
  PfnGetCurrentDrawable getCurrentDrawable = nullptr;
  PfnGetCurrentReadDrawable getCurrentReadDrawable = nullptr;
  PfnMakeContextCurrent makeContextCurrent = nullptr;
  PfnMakeCurrent makeCurrent = nullptr;
  void* dlHandle = nullptr;

  bool TryLoadFrom(void* handle) {
    auto sym = [handle](const char* name) -> void* {
      return ::dlsym(handle, name);
    };
    auto ctxFn =
        reinterpret_cast<PfnGetCurrentContext>(sym("glXGetCurrentContext"));
    auto dpyFn =
        reinterpret_cast<PfnGetCurrentDisplay>(sym("glXGetCurrentDisplay"));
    auto drawFn =
        reinterpret_cast<PfnGetCurrentDrawable>(sym("glXGetCurrentDrawable"));
    auto makeCtxCurFn =
        reinterpret_cast<PfnMakeContextCurrent>(sym("glXMakeContextCurrent"));
    auto makeCurFn = reinterpret_cast<PfnMakeCurrent>(sym("glXMakeCurrent"));
    if (!ctxFn || !dpyFn || !drawFn || (!makeCtxCurFn && !makeCurFn)) {
      return false;
    }
    getCurrentContext = ctxFn;
    getCurrentDisplay = dpyFn;
    getCurrentDrawable = drawFn;
    getCurrentReadDrawable = reinterpret_cast<PfnGetCurrentReadDrawable>(
        sym("glXGetCurrentReadDrawable"));
    makeContextCurrent = makeCtxCurFn;
    makeCurrent = makeCurFn;
    return true;
  }

  void Resolve() {
    if (getCurrentContext) return;
    if (TryLoadFrom(RTLD_DEFAULT)) return;

    // SDL2 / SDL3 (sdl2-compat) typically dlopen()s libGLX.so.0 or libGL.so.1
    // with RTLD_LOCAL, so RTLD_DEFAULT cannot see their symbols. Probe only
    // already-loaded libraries via RTLD_NOLOAD (never force-load GLX).
    static constexpr const char* kGlxLibs[] = {"libGLX.so.0", "libGL.so.1"};
    for (const char* lib : kGlxLibs) {
      void* h = ::dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
      if (!h) continue;
      if (TryLoadFrom(h)) {
        dlHandle = h;
        return;
      }
      ::dlclose(h);
    }
  }

  bool Available() {
    Resolve();
    return getCurrentContext && getCurrentDisplay && getCurrentDrawable &&
           (makeContextCurrent || makeCurrent);
  }
};

GlxInterop& GetGlxInterop() {
  static GlxInterop s_glx;
  return s_glx;
}
}  // namespace
#endif

void OpenGLESBackend::SaveCurrentHostEglContext() {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
  if (!m_hasSavedHostEgl) {
    const EGLenum curApi = eglQueryAPI();
    const EGLContext curCtx = eglGetCurrentContext();
    if (curCtx != EGL_NO_CONTEXT && curCtx != m_eglContext) {
      m_savedHostEglApi = static_cast<uint32_t>(curApi);
      m_savedHostEglDisplay = eglGetCurrentDisplay();
      m_savedHostEglDraw = eglGetCurrentSurface(EGL_DRAW);
      m_savedHostEglRead = eglGetCurrentSurface(EGL_READ);
      m_savedHostEglContext = curCtx;
      m_hasSavedHostEgl = true;
    }
  }
#if defined(HAVE_X11)
  // On Linux libglvnd, a thread cannot have both a GLX context (used by
  // DOSBox-X output=opengl or SDL_Renderer on X11/XWayland) and an EGL context
  // current simultaneously (eglMakeCurrent returns EGL_BAD_ACCESS = 0x3002).
  // Dynamically save & suspend any active GLX context, then restore it cleanly
  // in RestoreHostContextIfSaved().
  if (!m_hasSavedHostGlx) {
    auto& glx = GetGlxInterop();
    if (glx.Available()) {
      void* glxCtx = glx.getCurrentContext();
      if (glxCtx != nullptr) {
        void* glxDpy = glx.getCurrentDisplay();
        unsigned long glxDraw = glx.getCurrentDrawable();
        unsigned long glxRead = glx.getCurrentReadDrawable
                                    ? glx.getCurrentReadDrawable()
                                    : glxDraw;
        m_savedHostGlxDisplay = glxDpy;
        m_savedHostGlxDraw = glxDraw;
        m_savedHostGlxRead = glxRead;
        m_savedHostGlxContext = glxCtx;
        m_hasSavedHostGlx = true;
        if (glxDpy) {
          if (glx.makeContextCurrent) {
            glx.makeContextCurrent(glxDpy, 0, 0, nullptr);
          } else if (glx.makeCurrent) {
            glx.makeCurrent(glxDpy, 0, nullptr);
          }
        }
      }
    }
  }
#endif
#endif
}

void OpenGLESBackend::RestoreHostContextIfSaved() {
  InvalidateGlBindingCache();
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
  if (m_hasSavedHostEgl) {
    const EGLenum api = static_cast<EGLenum>(m_savedHostEglApi);
    const EGLDisplay dpy = static_cast<EGLDisplay>(m_savedHostEglDisplay);
    const EGLSurface draw = static_cast<EGLSurface>(m_savedHostEglDraw);
    const EGLSurface read = static_cast<EGLSurface>(m_savedHostEglRead);
    const EGLContext ctx = static_cast<EGLContext>(m_savedHostEglContext);
    m_hasSavedHostEgl = false;
    m_savedHostEglApi = 0;
    m_savedHostEglDisplay = nullptr;
    m_savedHostEglDraw = nullptr;
    m_savedHostEglRead = nullptr;
    m_savedHostEglContext = nullptr;
    if (dpy != EGL_NO_DISPLAY && ctx != EGL_NO_CONTEXT) {
      if (api == EGL_OPENGL_API || api == EGL_OPENGL_ES_API ||
          api == EGL_OPENVG_API) {
        eglBindAPI(api);
      }
      eglMakeCurrent(dpy, draw, read, ctx);
    }
  }
#if defined(HAVE_X11)
  if (m_hasSavedHostGlx) {
    void* glxDpy = m_savedHostGlxDisplay;
    const unsigned long glxDraw = m_savedHostGlxDraw;
    const unsigned long glxRead = m_savedHostGlxRead;
    void* glxCtx = m_savedHostGlxContext;
    m_hasSavedHostGlx = false;
    m_savedHostGlxDisplay = nullptr;
    m_savedHostGlxDraw = 0;
    m_savedHostGlxRead = 0;
    m_savedHostGlxContext = nullptr;

    // Release our EGL context on this thread before re-binding GLX so libglvnd
    // never rejects glXMakeContextCurrent with BadAccess.
    if (m_useDirectEgl && m_eglDisplay) {
      eglBindAPI(EGL_OPENGL_ES_API);
      eglMakeCurrent(static_cast<EGLDisplay>(m_eglDisplay), EGL_NO_SURFACE,
                     EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    auto& glx = GetGlxInterop();
    if (glx.Available() && glxDpy && glxCtx) {
      if (glx.makeContextCurrent) {
        glx.makeContextCurrent(glxDpy, glxDraw, glxRead, glxCtx);
      } else if (glx.makeCurrent) {
        glx.makeCurrent(glxDpy, glxDraw, glxCtx);
      }
    }
  }
#endif
#endif
}

bool OpenGLESBackend::EnsureContextCurrent() {
  if (m_useWgl) {
#if defined(_WIN32)
    if (!m_wglHdc || !m_wglContext) return false;
    HDC hdc = static_cast<HDC>(m_wglHdc);
    HGLRC ctx = static_cast<HGLRC>(m_wglContext);
    if (wglGetCurrentContext() == ctx && wglGetCurrentDC() == hdc) {
      return true;
    }
    if (!wglMakeCurrent(hdc, ctx)) {
      GLIDE_LOG(CRITICAL, "GLES",
                "wglMakeCurrent failed: err=" << GetLastError());
      return false;
    }
    return true;
#else
    return false;
#endif
  }

  if (m_useDirectEgl) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
    if (!m_eglDisplay || !m_eglContext) return false;
    EGLDisplay dpy = static_cast<EGLDisplay>(m_eglDisplay);
    EGLContext ctx = static_cast<EGLContext>(m_eglContext);
    EGLSurface surf =
        m_eglSurface ? static_cast<EGLSurface>(m_eglSurface) : EGL_NO_SURFACE;
    const EGLenum curApi = eglQueryAPI();
    const EGLContext curCtx = eglGetCurrentContext();
    if (curApi == EGL_OPENGL_ES_API && curCtx == ctx &&
        eglGetCurrentDisplay() == dpy &&
        eglGetCurrentSurface(EGL_DRAW) == surf) {
      return true;
    }
    SaveCurrentHostEglContext();
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
      GLIDE_LOG(CRITICAL, "GLES",
                "eglMakeCurrent failed: 0x" << std::hex << eglGetError()
                                            << std::dec);
      return false;
    }
    return true;
#else
    return false;
#endif
  }

#if !defined(_WIN32)
  if (m_sdlGlContext && m_surfaceHost.GetSDLWindow()) {
    if (SDL_GL_GetCurrentContext() == m_sdlGlContext) {
      return true;
    }
    if (SDL_GL_MakeCurrent(m_surfaceHost.GetSDLWindow(), m_sdlGlContext) < 0) {
      GLIDE_LOG(CRITICAL, "GLES",
                "SDL_GL_MakeCurrent failed: " << SDL_GetError());
      return false;
    }
    return true;
  }
#endif
  return false;
}

bool OpenGLESBackend::CreateDirectEglContext() {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
  const auto mode = m_surfaceHost.GetMode();
  EGLDisplay eglDpy = EGL_NO_DISPLAY;
  bool ownedDisplay = false;
  EGLint nativeVisualId = 0;

  auto getPlatformDisplay = [&](EGLenum platform, void* nativeDpy) -> EGLDisplay {
    // Prefer EGL 1.5 core eglGetPlatformDisplay when linked, falling back to
    // EGL_EXT_platform_base if needed.
    EGLDisplay d = eglGetPlatformDisplay(platform, nativeDpy, nullptr);
    if (d == EGL_NO_DISPLAY) {
      using PfnGetPlatformDisplayExt =
          EGLDisplay (*)(EGLenum, void*, const EGLint*);
      auto fn = reinterpret_cast<PfnGetPlatformDisplayExt>(
          eglGetProcAddress("eglGetPlatformDisplayEXT"));
      if (fn) {
        d = fn(platform, nativeDpy, nullptr);
      }
    }
    return d;
  };

  if (mode == NativeSurfaceHost::Mode::Headless) {
    eglDpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                EGL_DEFAULT_DISPLAY);
    if (eglDpy == EGL_NO_DISPLAY) {
      eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    ownedDisplay = true;
  }
#if defined(HAVE_X11)
  else if (mode == NativeSurfaceHost::Mode::X11Child) {
    Display* x11Dpy = static_cast<Display*>(m_surfaceHost.GetX11Display());
    Window x11Win = static_cast<Window>(m_surfaceHost.GetX11Window());
    if (!x11Dpy || x11Win == 0) {
      GLIDE_LOG(CRITICAL, "GLES", "Missing X11Display or X11 child window.");
      return false;
    }
    eglDpy = getPlatformDisplay(EGL_PLATFORM_X11_EXT, x11Dpy);
    if (eglDpy == EGL_NO_DISPLAY) {
      eglDpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(x11Dpy));
    }
    // NativeSurfaceHost opens its own private XOpenDisplay(nullptr), so this
    // EGLDisplay is exclusively ours.
    ownedDisplay = true;

    XWindowAttributes wa{};
    if (XGetWindowAttributes(x11Dpy, x11Win, &wa) && wa.visual) {
      nativeVisualId = static_cast<EGLint>(XVisualIDFromVisual(wa.visual));
    }
  }
#endif
#if defined(HAVE_WAYLAND) && defined(HAVE_WAYLAND_EGL)
  else if (mode == NativeSurfaceHost::Mode::WaylandSubsurface) {
    void* wlDpy = m_surfaceHost.GetWaylandDisplay();
    if (!wlDpy || !m_surfaceHost.GetWaylandSurface()) {
      GLIDE_LOG(CRITICAL, "GLES",
                "Missing Wayland display or child subsurface.");
      return false;
    }
    eglDpy = getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wlDpy);
    // CRITICAL: wl_display* is borrowed from the host (DOSBox-X / SDL).
    // eglTerminate() is NOT reference-counted, so NEVER terminate a borrowed
    // Wayland EGLDisplay or we will destroy the host's OpenGL context!
    ownedDisplay = false;
  }
#endif
  else {
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateDirectEglContext unsupported for mode="
                  << NativeSurfaceHost::ModeToString(mode));
    return false;
  }

  if (eglDpy == EGL_NO_DISPLAY) {
    GLIDE_LOG(CRITICAL, "GLES",
              "Failed to acquire EGLDisplay for mode="
                  << NativeSurfaceHost::ModeToString(mode) << " (err=0x"
                  << std::hex << eglGetError() << std::dec << ").");
    return false;
  }

  EGLint major = 0;
  EGLint minor = 0;
  if (!eglInitialize(eglDpy, &major, &minor)) {
    GLIDE_LOG(CRITICAL, "GLES",
              "eglInitialize failed (err=0x" << std::hex << eglGetError()
                                             << std::dec << ").");
    return false;
  }

  m_eglDisplay = eglDpy;
  m_eglDisplayOwned = ownedDisplay;
  m_useDirectEgl = true;

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    GLIDE_LOG(CRITICAL, "GLES",
              "eglBindAPI(EGL_OPENGL_ES_API) failed (err=0x"
                  << std::hex << eglGetError() << std::dec << ").");
    return false;
  }

  // Choose an OpenGL ES 3.x EGLConfig matching the target surface type.
  EGLConfig cfg = nullptr;
  EGLint numConfigs = 0;

  if (mode == NativeSurfaceHost::Mode::Headless) {
    const EGLint pbufferAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    eglChooseConfig(eglDpy, pbufferAttribs, &cfg, 1, &numConfigs);
    if (numConfigs == 0) {
      const EGLint surfacelessAttribs[] = {
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
          EGL_RED_SIZE, 8,
          EGL_GREEN_SIZE, 8,
          EGL_BLUE_SIZE, 8,
          EGL_NONE,
      };
      eglChooseConfig(eglDpy, surfacelessAttribs, &cfg, 1, &numConfigs);
    }
  } else {
    // Per EGL 1.5 spec (Table 3.4), eglChooseConfig IGNORES
    // EGL_NATIVE_VISUAL_ID in the input attribute list. For X11 child windows,
    // we MUST enumerate candidate configs and match EGL_NATIVE_VISUAL_ID via
    // eglGetConfigAttrib(), otherwise real-GPU drivers reject the window
    // surface with EGL_BAD_CONFIG (0x3005).
    auto findConfigForVisual = [&](const EGLint* attribs,
                                   EGLint targetVisualId) -> EGLConfig {
      EGLint total = 0;
      if (!eglChooseConfig(eglDpy, attribs, nullptr, 0, &total) || total <= 0) {
        return nullptr;
      }
      std::vector<EGLConfig> candidates(static_cast<size_t>(total));
      EGLint returned = 0;
      if (!eglChooseConfig(eglDpy, attribs, candidates.data(), total,
                           &returned) ||
          returned <= 0) {
        return nullptr;
      }
      candidates.resize(static_cast<size_t>(returned));

      EGLConfig firstMatch = nullptr;
      for (EGLConfig c : candidates) {
        EGLint visId = 0;
        if (!eglGetConfigAttrib(eglDpy, c, EGL_NATIVE_VISUAL_ID, &visId) ||
            visId != targetVisualId) {
          continue;
        }
        if (!firstMatch) {
          firstMatch = c;
        }
        // Prefer a non-MSAA default framebuffer config because our internal
        // FBO handles Glide rendering and MSAA resolves explicitly.
        EGLint samples = 0;
        if (eglGetConfigAttrib(eglDpy, c, EGL_SAMPLES, &samples) &&
            samples == 0) {
          return c;
        }
      }
      return firstMatch;
    };

    if (nativeVisualId != 0) {
      // Omit EGL_ALPHA_SIZE here so 24-bit TrueColor X11 visuals (alpha=0)
      // match on the first pass.
      const EGLint rgb8WinAttribs[] = {
          EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
          EGL_RED_SIZE, 8,
          EGL_GREEN_SIZE, 8,
          EGL_BLUE_SIZE, 8,
          EGL_NONE,
      };
      cfg = findConfigForVisual(rgb8WinAttribs, nativeVisualId);

      if (!cfg) {
        const EGLint anyEs3WinAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_NONE,
        };
        cfg = findConfigForVisual(anyEs3WinAttribs, nativeVisualId);
      }

      if (!cfg) {
        // Last-resort scan of every EGLConfig on the display for an exact
        // X11 visual match supporting windowed ES3.
        EGLint total = 0;
        if (eglGetConfigs(eglDpy, nullptr, 0, &total) && total > 0) {
          std::vector<EGLConfig> allConfigs(static_cast<size_t>(total));
          EGLint returned = 0;
          if (eglGetConfigs(eglDpy, allConfigs.data(), total, &returned) &&
              returned > 0) {
            allConfigs.resize(static_cast<size_t>(returned));
            for (EGLConfig c : allConfigs) {
              EGLint visId = 0;
              EGLint surfType = 0;
              EGLint rendType = 0;
              if (eglGetConfigAttrib(eglDpy, c, EGL_NATIVE_VISUAL_ID, &visId) &&
                  visId == nativeVisualId &&
                  eglGetConfigAttrib(eglDpy, c, EGL_SURFACE_TYPE, &surfType) &&
                  (surfType & EGL_WINDOW_BIT) != 0 &&
                  eglGetConfigAttrib(eglDpy, c, EGL_RENDERABLE_TYPE,
                                     &rendType) &&
                  (rendType & EGL_OPENGL_ES3_BIT) != 0) {
                cfg = c;
                break;
              }
            }
          }
        }
      }

      if (cfg) {
        numConfigs = 1;
      } else {
        GLIDE_LOG(INFO, "GLES",
                  "No OpenGL ES 3.x window EGLConfig matched parent X11 visual "
                  "0x" << std::hex << nativeVisualId << std::dec
                       << "; selecting preferred EGL visual and recreating "
                          "X11Child.");
      }
    }

    if (numConfigs == 0) {
      const EGLint winAttribs[] = {
          EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
          EGL_RED_SIZE, 8,
          EGL_GREEN_SIZE, 8,
          EGL_BLUE_SIZE, 8,
          EGL_ALPHA_SIZE, 8,
          EGL_NONE,
      };
      eglChooseConfig(eglDpy, winAttribs, &cfg, 1, &numConfigs);
    }
    if (numConfigs == 0) {
      const EGLint fallbackAttribs[] = {
          EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
          EGL_RED_SIZE, 8,
          EGL_GREEN_SIZE, 8,
          EGL_BLUE_SIZE, 8,
          EGL_NONE,
      };
      eglChooseConfig(eglDpy, fallbackAttribs, &cfg, 1, &numConfigs);
    }
  }

  if (numConfigs == 0 || !cfg) {
    GLIDE_LOG(CRITICAL, "GLES",
              "eglChooseConfig found no suitable OpenGL ES 3.x config for mode="
                  << NativeSurfaceHost::ModeToString(mode) << ".");
    return false;
  }

  EGLint chosenConfigId = 0;
  EGLint chosenVisualId = 0;
  EGLint chosenR = 0, chosenG = 0, chosenB = 0, chosenA = 0;
  eglGetConfigAttrib(eglDpy, cfg, EGL_CONFIG_ID, &chosenConfigId);
  eglGetConfigAttrib(eglDpy, cfg, EGL_NATIVE_VISUAL_ID, &chosenVisualId);
  eglGetConfigAttrib(eglDpy, cfg, EGL_RED_SIZE, &chosenR);
  eglGetConfigAttrib(eglDpy, cfg, EGL_GREEN_SIZE, &chosenG);
  eglGetConfigAttrib(eglDpy, cfg, EGL_BLUE_SIZE, &chosenB);
  eglGetConfigAttrib(eglDpy, cfg, EGL_ALPHA_SIZE, &chosenA);

  // Create OpenGL ES 3.2 context (with graceful ES 3.0 fallback).
  const EGLint ctxAttribs32[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 2,
      EGL_NONE,
  };
  EGLContext eglCtx = eglCreateContext(eglDpy, cfg, EGL_NO_CONTEXT, ctxAttribs32);
  if (eglCtx == EGL_NO_CONTEXT) {
    const EGLint ctxAttribs30[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE,
    };
    eglCtx = eglCreateContext(eglDpy, cfg, EGL_NO_CONTEXT, ctxAttribs30);
  }
  if (eglCtx == EGL_NO_CONTEXT) {
    GLIDE_LOG(CRITICAL, "GLES",
              "eglCreateContext failed (err=0x" << std::hex << eglGetError()
                                                << std::dec << ").");
    return false;
  }
  m_eglContext = eglCtx;

  // Create the EGLSurface for this mode (or surfaceless for Headless).
  EGLSurface eglSurf = EGL_NO_SURFACE;
  if (mode == NativeSurfaceHost::Mode::Headless) {
    if (!eglMakeCurrent(eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, eglCtx)) {
      // Driver lacks surfaceless context -> create a minimal 1x1 Pbuffer.
      const EGLint pbufSize[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
      eglSurf = eglCreatePbufferSurface(eglDpy, cfg, pbufSize);
      if (eglSurf == EGL_NO_SURFACE ||
          !eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx)) {
        GLIDE_LOG(CRITICAL, "GLES",
                  "Headless EGL makeCurrent failed (err=0x"
                      << std::hex << eglGetError() << std::dec << ").");
        if (eglSurf != EGL_NO_SURFACE) {
          eglDestroySurface(eglDpy, eglSurf);
        }
        return false;
      }
      m_eglSurface = eglSurf;
    }
  }
#if defined(HAVE_X11)
  else if (mode == NativeSurfaceHost::Mode::X11Child) {
    // If the parent host window uses an X11 visual that has no matching
    // OpenGL ES 3.x EGLConfig (e.g., DirectColor or GLX-chosen visual),
    // recreate our click-through X11Child with the chosen EGLConfig's exact
    // EGL_NATIVE_VISUAL_ID so strict real-GPU drivers never reject it with
    // EGL_BAD_CONFIG (0x3005).
    if (chosenVisualId != 0 && chosenVisualId != nativeVisualId) {
      if (m_surfaceHost.RecreateX11ChildForVisual(
              static_cast<unsigned long>(chosenVisualId))) {
        nativeVisualId = chosenVisualId;
      }
    }
    Window x11Win = static_cast<Window>(m_surfaceHost.GetX11Window());
    eglSurf = eglCreatePlatformWindowSurface(eglDpy, cfg, &x11Win, nullptr);
    const EGLint platformErr = (eglSurf == EGL_NO_SURFACE) ? eglGetError() : EGL_SUCCESS;
    if (eglSurf == EGL_NO_SURFACE) {
      eglSurf = eglCreateWindowSurface(
          eglDpy, cfg,
          reinterpret_cast<EGLNativeWindowType>(
              static_cast<uintptr_t>(x11Win)),
          nullptr);
    }
    if (eglSurf == EGL_NO_SURFACE) {
      GLIDE_LOG(CRITICAL, "GLES",
                "eglCreatePlatformWindowSurface(X11Child) failed (platformErr=0x"
                    << std::hex << platformErr << ", legacyErr=0x"
                    << eglGetError() << ", configId=" << std::dec
                    << chosenConfigId << ", cfgVisual=0x" << std::hex
                    << chosenVisualId << ", targetVisual=0x" << nativeVisualId
                    << std::dec << ", rgba=" << chosenR << "/" << chosenG
                    << "/" << chosenB << "/" << chosenA << ").");
      return false;
    }
    m_eglSurface = eglSurf;
    if (!eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx)) {
      GLIDE_LOG(CRITICAL, "GLES",
                "eglMakeCurrent(X11Child) failed (err=0x"
                    << std::hex << eglGetError() << std::dec << ").");
      return false;
    }
    eglSwapInterval(eglDpy, m_config.vsync ? 1 : 0);
  }
#endif
#if defined(HAVE_WAYLAND) && defined(HAVE_WAYLAND_EGL)
  else if (mode == NativeSurfaceHost::Mode::WaylandSubsurface) {
    uint32_t pixW = m_headlessWidth;
    uint32_t pixH = m_headlessHeight;
    m_surfaceHost.GetDrawableSize(pixW, pixH);
    pixW = std::max<uint32_t>(1, pixW);
    pixH = std::max<uint32_t>(1, pixH);

    auto* wlSurf = static_cast<wl_surface*>(m_surfaceHost.GetWaylandSurface());
    wl_egl_window* wlEglWin =
        wl_egl_window_create(wlSurf, static_cast<int>(pixW),
                             static_cast<int>(pixH));
    if (!wlEglWin) {
      GLIDE_LOG(CRITICAL, "GLES", "wl_egl_window_create failed.");
      return false;
    }
    m_wlEglWindow = wlEglWin;

    eglSurf = eglCreatePlatformWindowSurface(eglDpy, cfg, wlEglWin, nullptr);
    if (eglSurf == EGL_NO_SURFACE) {
      eglSurf = eglCreateWindowSurface(
          eglDpy, cfg, reinterpret_cast<EGLNativeWindowType>(wlEglWin),
          nullptr);
    }
    if (eglSurf == EGL_NO_SURFACE) {
      GLIDE_LOG(CRITICAL, "GLES",
                "eglCreatePlatformWindowSurface(WaylandSubsurface) failed "
                "(err=0x"
                    << std::hex << eglGetError() << std::dec << ").");
      return false;
    }
    m_eglSurface = eglSurf;
    if (!eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx)) {
      GLIDE_LOG(CRITICAL, "GLES",
                "eglMakeCurrent(WaylandSubsurface) failed (err=0x"
                    << std::hex << eglGetError() << std::dec << ").");
      return false;
    }
    // Wayland compositing is always tear-free and FrameTracker paces maxFps;
    // avoid blocking on wl_surface.frame callbacks unless explicitly forced.
    const EGLint swapInterval =
        (m_config.vsync && m_config.vsyncStrictFifo) ? 1 : 0;
    eglSwapInterval(eglDpy, swapInterval);
  }
#endif

  GLIDE_LOG(INFO, "GLES",
            "Created Direct EGL " << major << "." << minor
                                  << " OpenGL ES context (mode="
                                  << NativeSurfaceHost::ModeToString(mode)
                                  << ", configId=" << chosenConfigId
                                  << ", visual=0x" << std::hex << chosenVisualId
                                  << "/0x" << nativeVisualId << std::dec
                                  << ", rgba=" << chosenR << "/" << chosenG
                                  << "/" << chosenB << "/" << chosenA
                                  << ", ownedDisplay="
                                  << (m_eglDisplayOwned ? "yes" : "no")
                                  << ").");
  return true;
#else
  GLIDE_LOG(CRITICAL, "GLES",
            "Direct EGL support not compiled in (HAVE_EGL=OFF).");
  return false;
#endif
}

bool OpenGLESBackend::CreateStandaloneSdlGlContext() {
#if !defined(_WIN32)
  SDL_Window* win = m_surfaceHost.GetSDLWindow();
  if (!win) {
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateStandaloneSdlGlContext: missing SDL_Window.");
    return false;
  }

  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) {
    GLIDE_LOG(CRITICAL, "GLES",
              "SDL_GL_CreateContext failed: " << SDL_GetError());
    return false;
  }
  m_sdlGlContext = ctx;
  m_useDirectEgl = false;

  if (SDL_GL_MakeCurrent(win, ctx) < 0) {
    GLIDE_LOG(CRITICAL, "GLES",
              "SDL_GL_MakeCurrent failed: " << SDL_GetError());
    return false;
  }

  const bool isWayland = m_surfaceHost.IsWaylandSurface();
  const int swapInterval =
      (m_config.vsync && (!isWayland || m_config.vsyncStrictFifo)) ? 1 : 0;
  SDL_GL_SetSwapInterval(swapInterval);
  return true;
#else
  return false;
#endif
}

bool OpenGLESBackend::CreateWin32WglContext() {
#if defined(_WIN32)
  const auto mode = m_surfaceHost.GetMode();
  HWND targetHwnd = nullptr;
  if (mode == NativeSurfaceHost::Mode::Headless) {
    // WGL requires a valid window DC even for offscreen FBO-only rendering.
    targetHwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1,
                                 nullptr, nullptr, GetModuleHandleA(nullptr),
                                 nullptr);
    if (!targetHwnd) {
      GLIDE_LOG(CRITICAL, "GLES",
                "CreateWin32WglContext: failed to create hidden dummy HWND "
                "(err=" << GetLastError() << ").");
      return false;
    }
    m_wglDummyHwnd = static_cast<void*>(targetHwnd);
  } else {
    targetHwnd = static_cast<HWND>(m_surfaceHost.GetWin32Hwnd());
    if (!targetHwnd) {
      GLIDE_LOG(CRITICAL, "GLES",
                "CreateWin32WglContext: NativeSurfaceHost missing HWND.");
      return false;
    }
  }

  HDC hdc = GetDC(targetHwnd);
  if (!hdc) {
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateWin32WglContext: GetDC failed (err=" << GetLastError()
                                                          << ").");
    return false;
  }
  m_wglHdc = static_cast<void*>(hdc);
  m_wglTargetHwnd = static_cast<void*>(targetHwnd);

  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cAlphaBits = 8;
  pfd.cDepthBits = 24;
  pfd.cStencilBits = 8;
  pfd.iLayerType = PFD_MAIN_PLANE;

  // Win32 allows SetPixelFormat only once per HWND; when re-attaching to the
  // same host HWND (e.g. resolution change), reuse its OpenGL pixel format if
  // already configured.
  int existingFormat = ::GetPixelFormat(hdc);
  bool needSetPixelFormat = true;
  if (existingFormat != 0) {
    PIXELFORMATDESCRIPTOR existingPfd{};
    existingPfd.nSize = sizeof(existingPfd);
    if (::DescribePixelFormat(hdc, existingFormat, sizeof(existingPfd),
                              &existingPfd) != 0 &&
        (existingPfd.dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL)) ==
            (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL)) {
      needSetPixelFormat = false;
    }
  }
  if (needSetPixelFormat) {
    int pixelFormat = ::ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0 || !::SetPixelFormat(hdc, pixelFormat, &pfd)) {
      GLIDE_LOG(CRITICAL, "GLES",
                "CreateWin32WglContext: ChoosePixelFormat/SetPixelFormat failed "
                "(err=" << GetLastError() << ").");
      return false;
    }
  }

  HGLRC tempCtx = wglCreateContext(hdc);
  if (!tempCtx) {
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateWin32WglContext: wglCreateContext failed (err="
                  << GetLastError() << ").");
    return false;
  }
  if (!wglMakeCurrent(hdc, tempCtx)) {
    wglDeleteContext(tempCtx);
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateWin32WglContext: wglMakeCurrent(temp) failed (err="
                  << GetLastError() << ").");
    return false;
  }

  // Upgrade to an OpenGL 3.3 Core Profile context when supported.
  HGLRC finalCtx = tempCtx;
  auto createCtxAttribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
      reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
  if (createCtxAttribs) {
    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0};
    HGLRC coreCtx = createCtxAttribs(hdc, nullptr, attribs);
    if (coreCtx) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(tempCtx);
      if (!wglMakeCurrent(hdc, coreCtx)) {
        wglDeleteContext(coreCtx);
        GLIDE_LOG(CRITICAL, "GLES",
                  "CreateWin32WglContext: wglMakeCurrent(3.3 Core) failed.");
        return false;
      }
      finalCtx = coreCtx;
    }
  }

  m_wglContext = static_cast<void*>(finalCtx);
  m_useWgl = true;
  m_useDirectEgl = false;

  if (!Win32Gl::LoadCoreFunctions()) {
    GLIDE_LOG(CRITICAL, "GLES",
              "CreateWin32WglContext: required OpenGL 3.3 functions missing.");
    return false;
  }

  // Enable shader-controlled gl_PointSize on Desktop OpenGL.
  glEnable(GL_PROGRAM_POINT_SIZE);

  auto swapIntervalExt = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
      reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT")));
  if (swapIntervalExt) {
    swapIntervalExt(m_config.vsync ? 1 : 0);
  }

  GLIDE_LOG(INFO, "GLES",
            "Created Win32 WGL OpenGL 3.3 Core context (mode="
                << NativeSurfaceHost::ModeToString(mode) << ").");
  return true;
#else
  return false;
#endif
}

// Context Creation & Shaders Compiler Helpers
bool OpenGLESBackend::CreateGLContext(void* nativeWindowHandle, uint32_t width,
                                      uint32_t height, bool windowed) {
  SaveCurrentHostEglContext();

  const bool isStandaloneRequest =
      (nativeWindowHandle == nullptr && !m_config.forceNoWindow);

#if !defined(_WIN32)
  // Configure SDL OpenGL ES 3.2 attributes ONLY when creating our own
  // StandaloneSDL2 demo window (never touch host SDL GL state for DOSBox).
  if (isStandaloneRequest) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Keep the presentation window 1x: MSAA is rendered to offscreen FBOs and
    // resolved to m_fboTextures before presentation.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
  }
  const uint32_t extraFlags = isStandaloneRequest ? SDL_WINDOW_OPENGL : 0;
#else
  (void)isStandaloneRequest;
  const uint32_t extraFlags = 0u;
#endif
  if (!m_surfaceHost.Attach(
          nativeWindowHandle, width, height, windowed, m_config.forceNoWindow,
          "3dfx glide-ng Presentation Console (OpenGL)", extraFlags,
          m_config.windowScale)) {
    GLIDE_LOG(CRITICAL, "GLES",
              "NativeSurfaceHost::Attach failed for OpenGLESBackend.");
    RestoreHostContextIfSaved();
    return false;
  }

  const auto mode = m_surfaceHost.GetMode();
  bool ctxOk = false;
#if defined(_WIN32)
  (void)mode;
  ctxOk = CreateWin32WglContext();
#else
  if (mode == NativeSurfaceHost::Mode::StandaloneSDL2) {
    ctxOk = CreateStandaloneSdlGlContext();
  } else {
    ctxOk = CreateDirectEglContext();
  }
#endif

  if (!ctxOk) {
    DestroyGLContext();
    return false;
  }

  // Output dynamic startup adapter info (Pixel Format + nSamples printed in
  // AttachWindow once offscreen MSAA FBOs are finalized).
  const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const char* glRenderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* glVersion =
      reinterpret_cast<const char*>(glGetString(GL_VERSION));

  std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle
            << ", res=" << width << "x" << height << ")\r\n";
  std::cout << "Info: Host OpenGL Adapter: "
            << (glVendor ? glVendor : "Unknown") << " - "
            << (glRenderer ? glRenderer : "Unknown") << " (GL Ver "
            << (glVersion ? glVersion : "Unknown") << ")\r\n"
            << std::flush;

  // Setup full-screen quad presentation shaders
  auto compileShader = [](GLenum type, const char* src) -> GLuint {
    const std::string adapted = AdaptGlslSourceForPlatform(src);
    const char* adaptedPtr = adapted.c_str();
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &adaptedPtr, nullptr);
    glCompileShader(s);
    GLint status = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) {
      char log[512];
      glGetShaderInfoLog(s, 512, nullptr, log);
      GLIDE_LOG(CRITICAL, "GLES", "Shader compilation failure: " << log);
    }
    return s;
  };

  GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);

  m_glProgram = glCreateProgram();
  glAttachShader(m_glProgram, vs);
  glAttachShader(m_glProgram, fs);
  glLinkProgram(m_glProgram);

  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linkStatus = 0;
  glGetProgramiv(m_glProgram, GL_LINK_STATUS, &linkStatus);
  if (!linkStatus) {
    char log[512];
    glGetProgramInfoLog(m_glProgram, 512, nullptr, log);
    GLIDE_LOG(CRITICAL, "GLES", "Program linking failure: " << log);
    DestroyGLContext();
    return false;
  }

  // Explicitly bind the presentation shader's texture uniform to texture unit 0
  GLint uTexLoc = glGetUniformLocation(m_glProgram, "uTex");
  if (uTexLoc != -1) {
    glUseProgram(m_glProgram);
    glUniform1i(uTexLoc, 0);
    glUseProgram(0);
  }

  // Explicitly bind the presentation shader's Gamma LUT sampler to texture unit
  // 1
  GLint uGammaLutLoc = glGetUniformLocation(m_glProgram, "uGammaLut");
  if (uGammaLutLoc != -1) {
    glUseProgram(m_glProgram);
    glUniform1i(uGammaLutLoc, 1);
    glUseProgram(0);
  }

  m_uUseGammaLutLoc = glGetUniformLocation(m_glProgram, "uUseGammaLut");
  m_uPreserveAlphaLoc = glGetUniformLocation(m_glProgram, "uPreserveAlpha");

  // Fullscreen triangle-strip quad sampling the bottom-up offscreen FBO texture
  // (no Y-flip: NDC +1 maps to V=1.0, NDC -1 maps to V=0.0).
  const float quadVertices[] = {
      -1.0f, 1.0f,  0.0f, 1.0f,  // Top-Left
      -1.0f, -1.0f, 0.0f, 0.0f,  // Bottom-Left
      1.0f,  1.0f,  1.0f, 1.0f,  // Top-Right
      1.0f,  -1.0f, 1.0f, 0.0f,  // Bottom-Right
  };

  glGenVertexArrays(1, &m_glVAO);
  glGenBuffers(1, &m_glVBO);

  glBindVertexArray(m_glVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_glVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices,
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);  // Pos
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void*>(0));

  glEnableVertexAttribArray(1);  // Tex
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  // Create the 256x1 Gamma LUT texture
  glGenTextures(1, &m_glGammaLutTex);
  glBindTexture(GL_TEXTURE_2D, m_glGammaLutTex);
  uint8_t identityLut[256 * 3];
  for (int i = 0; i < 256; ++i) {
    identityLut[i * 3 + 0] = static_cast<uint8_t>(i);
    identityLut[i * 3 + 1] = static_cast<uint8_t>(i);
    identityLut[i * 3 + 2] = static_cast<uint8_t>(i);
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
               identityLut);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Create the offscreen FBO GPU rendering textures (Double-Buffered)
  glGenTextures(2, m_fboTextures);
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, m_fboTextures[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, 0);

  glGenSamplers(2, m_glesSamplers);

  return true;
}

void OpenGLESBackend::DestroyGLContext() {
  // 1. Delete all GL objects while our context is still current.
  if (EnsureContextCurrent()) {
    for (uint32_t t = 0; t < 2; ++t) {
      for (auto& pair : m_glesTextures[t]) {
        if (pair.second.id) {
          glDeleteTextures(1, &pair.second.id);
        }
      }
      m_glesTextures[t].clear();
    }

    if (m_glesSamplers[0] != 0) {
      glDeleteSamplers(2, m_glesSamplers);
      m_glesSamplers[0] = m_glesSamplers[1] = 0;
    }
    if (m_glGammaLutTex) {
      glDeleteTextures(1, &m_glGammaLutTex);
      m_glGammaLutTex = 0;
    }
    if (m_glHudTex) {
      glDeleteTextures(1, &m_glHudTex);
      m_glHudTex = 0;
    }
    m_hudUploadedRev = 0;
    if (m_glLfbOverlayTex) {
      glDeleteTextures(1, &m_glLfbOverlayTex);
      m_glLfbOverlayTex = 0;
    }
    m_glLfbOverlayWidth = 0;
    m_glLfbOverlayHeight = 0;
    if (m_fboTextures[0]) {
      glDeleteTextures(2, m_fboTextures);
      m_fboTextures[0] = m_fboTextures[1] = 0;
    }
    if (m_glVBO) {
      glDeleteBuffers(1, &m_glVBO);
      m_glVBO = 0;
    }
    if (m_glVAO) {
      glDeleteVertexArrays(1, &m_glVAO);
      m_glVAO = 0;
    }
    if (m_glProgram) {
      glDeleteProgram(m_glProgram);
      m_glProgram = 0;
    }
    if (m_sharedDepthRenderbuffer) {
      glDeleteRenderbuffers(1, &m_sharedDepthRenderbuffer);
      m_sharedDepthRenderbuffer = 0;
    }
    if (m_headlessFBOs[0]) {
      glDeleteFramebuffers(2, m_headlessFBOs);
      m_headlessFBOs[0] = m_headlessFBOs[1] = 0;
    }
    if (m_msaaDepthRBO) {
      glDeleteRenderbuffers(1, &m_msaaDepthRBO);
      m_msaaDepthRBO = 0;
    }
    if (m_msaaColorRBOs[0]) {
      glDeleteRenderbuffers(2, m_msaaColorRBOs);
      m_msaaColorRBOs[0] = m_msaaColorRBOs[1] = 0;
    }
    if (m_msaaFBOs[0]) {
      glDeleteFramebuffers(2, m_msaaFBOs);
      m_msaaFBOs[0] = m_msaaFBOs[1] = 0;
    }
    if (m_geomVBO) {
      glDeleteBuffers(1, &m_geomVBO);
      m_geomVBO = 0;
    }
    if (m_geomVAO) {
      glDeleteVertexArrays(1, &m_geomVAO);
      m_geomVAO = 0;
    }
    if (m_geomProgram) {
      glDeleteProgram(m_geomProgram);
      m_geomProgram = 0;
    }
  } else {
    m_glesTextures[0].clear();
    m_glesTextures[1].clear();
    m_glesSamplers[0] = m_glesSamplers[1] = 0;
    m_glGammaLutTex = 0;
    m_glHudTex = 0;
    m_hudUploadedRev = 0;
    m_glLfbOverlayTex = 0;
    m_glLfbOverlayWidth = 0;
    m_glLfbOverlayHeight = 0;
    m_fboTextures[0] = m_fboTextures[1] = 0;
    m_glVBO = 0;
    m_glVAO = 0;
    m_glProgram = 0;
    m_sharedDepthRenderbuffer = 0;
    m_headlessFBOs[0] = m_headlessFBOs[1] = 0;
    m_msaaDepthRBO = 0;
    m_msaaColorRBOs[0] = m_msaaColorRBOs[1] = 0;
    m_msaaFBOs[0] = m_msaaFBOs[1] = 0;
    m_geomVBO = 0;
    m_geomVAO = 0;
    m_geomProgram = 0;
  }
  m_msaaColorDirty[0] = m_msaaColorDirty[1] = false;
  m_singleSampleColorDirty[0] = m_singleSampleColorDirty[1] = false;
  m_supportsSampleMask = false;

  // 2. Strict WGL / EGL / SDL teardown order BEFORE NativeSurfaceHost::Detach()
  if (m_useWgl) {
#if defined(_WIN32)
    if (m_wglContext) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(static_cast<HGLRC>(m_wglContext));
    }
    if (m_wglHdc && m_wglTargetHwnd) {
      ReleaseDC(static_cast<HWND>(m_wglTargetHwnd), static_cast<HDC>(m_wglHdc));
    }
    if (m_wglDummyHwnd) {
      DestroyWindow(static_cast<HWND>(m_wglDummyHwnd));
    }
#endif
  } else if (m_useDirectEgl) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_EGL)
    EGLDisplay dpy = static_cast<EGLDisplay>(m_eglDisplay);
    if (dpy != EGL_NO_DISPLAY) {
      eglBindAPI(EGL_OPENGL_ES_API);
      eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (m_eglContext) {
        eglDestroyContext(dpy, static_cast<EGLContext>(m_eglContext));
      }
      if (m_eglSurface) {
        eglDestroySurface(dpy, static_cast<EGLSurface>(m_eglSurface));
      }
    }
#if defined(HAVE_WAYLAND_EGL)
    if (m_wlEglWindow) {
      wl_egl_window_destroy(static_cast<wl_egl_window*>(m_wlEglWindow));
    }
#endif
    if (dpy != EGL_NO_DISPLAY && m_eglDisplayOwned) {
      eglTerminate(dpy);
    }
#endif
  }
#if !defined(_WIN32)
  else if (m_sdlGlContext) {
    SDL_GL_MakeCurrent(nullptr, nullptr);
    SDL_GL_DeleteContext(reinterpret_cast<SDL_GLContext>(m_sdlGlContext));
  }
#endif

  m_eglDisplay = nullptr;
  m_eglContext = nullptr;
  m_eglSurface = nullptr;
  m_wlEglWindow = nullptr;
  m_useDirectEgl = false;
  m_eglDisplayOwned = false;
  m_sdlGlContext = nullptr;
  m_wglHdc = nullptr;
  m_wglContext = nullptr;
  m_wglDummyHwnd = nullptr;
  m_wglTargetHwnd = nullptr;
  m_useWgl = false;

  // 3. Detach native child surface / standalone SDL window LAST.
  m_surfaceHost.Detach();

  // 4. Restore any saved foreign host EGL context (e.g. DOSBox-X OpenGL).
  RestoreHostContextIfSaved();
}

void OpenGLESBackend::SetBlendState(uint32_t rgbSrcFactor,
                                    uint32_t rgbDstFactor,
                                    uint32_t alphaSrcFactor,
                                    uint32_t alphaDstFactor) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_rgbSrcBlend == rgbSrcFactor && m_rgbDstBlend == rgbDstFactor &&
      m_alphaSrcBlend == alphaSrcFactor && m_alphaDstBlend == alphaDstFactor) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetBlendState_Locked(rgbSrcFactor, rgbDstFactor, alphaSrcFactor,
                                   alphaDstFactor);
  m_glesDirtyMask |= kGlesDirtyBlend;
}

void OpenGLESBackend::SetRenderBuffer(uint32_t target) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_activeRenderBuffer == target) return;
  FlushBatch(true);
  GlideStateTracker::SetRenderBuffer_Locked(target);
  if (m_initialized && m_windowAttached && EnsureContextCurrent()) {
    const uint32_t activeFbo = GetActiveGpuFbo();
    glBindFramebuffer(GL_FRAMEBUFFER, activeFbo);
    m_cachedBoundFbo = activeFbo;
  } else {
    m_cachedBoundFbo = 0xFFFFFFFFu;
  }
}

void OpenGLESBackend::SetDepthRange(float nearVal, float farVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_depthNear == nearVal && m_depthFar == farVal) return;
  FlushBatch(true);
  GlideStateTracker::SetDepthRange_Locked(nearVal, farVal);
  m_glesDirtyMask |= kGlesDirtyDepthRange;
}

void OpenGLESBackend::SetDitherMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_ditherMode == mode) return;
  FlushBatch(true);
  GlideStateTracker::SetDitherMode_Locked(mode);
  m_glesDirtyMask |= kGlesDirtyDitherStipple;
}

void OpenGLESBackend::SetStippleState(uint32_t mode, uint32_t pattern) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stippleMode == mode && m_stipplePattern == pattern) return;
  FlushBatch(true);
  GlideStateTracker::SetStippleState_Locked(mode, pattern);
  m_glesDirtyMask |= kGlesDirtyDitherStipple;
}

uint32_t OpenGLESBackend::ResolveDesiredMsaaSamples_Locked() const {
  const bool userOverride = (m_config.settingSources.count("msaaSamples") > 0);
  uint32_t desired = m_config.msaaSamples;
  if (!userOverride && m_requestedMsaaSamples > 0u) {
    desired = m_requestedMsaaSamples;
  }
  return (desired > 1u) ? desired : 1u;
}

uint32_t OpenGLESBackend::GetEffectiveSampleMask_Locked() const {
  if (m_msaaSamples <= 1) return 1u;
  const uint32_t fullMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  return m_tbufferWriteMask & fullMask;
}

bool OpenGLESBackend::ShouldUseSingleSampleFbo_Locked() const {
  if (m_msaaSamples <= 1 || m_msaaFBOs[0] == 0) return true;
  const uint32_t fullMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  return !m_aaMultiSample && ((m_tbufferWriteMask & fullMask) == fullMask);
}

uint32_t OpenGLESBackend::GetActiveGpuFbo() const {
  const uint32_t slot = GetActiveGpuSlot();
  if (m_msaaSamples > 1 && m_msaaFBOs[slot] != 0 &&
      !ShouldUseSingleSampleFbo_Locked()) {
    return m_msaaFBOs[slot];
  }
  return m_headlessFBOs[slot];
}

void OpenGLESBackend::ResolveMsaaToSingle_Locked(uint32_t slot) {
  if (m_msaaSamples <= 1 || slot >= 2 || !m_msaaColorDirty[slot] ||
      m_msaaFBOs[slot] == 0 || m_headlessFBOs[slot] == 0) {
    return;
  }
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (m_supportsSampleMask) {
    glDisable(GL_SAMPLE_MASK);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFBOs[slot]);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_headlessFBOs[slot]);
  glBlitFramebuffer(0, 0, static_cast<GLint>(m_headlessWidth),
                    static_cast<GLint>(m_headlessHeight), 0, 0,
                    static_cast<GLint>(m_headlessWidth),
                    static_cast<GLint>(m_headlessHeight),
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_msaaColorDirty[slot] = false;
  m_singleSampleColorDirty[slot] = false;
  m_glesDirtyMask |=
      (kGlesDirtyScissor | kGlesDirtyColorMask | kGlesDirtySampleMask);
  InvalidateGlBindingCache();
}

void OpenGLESBackend::CopySingleToMsaa_Locked(uint32_t slot) {
  if (m_msaaSamples <= 1 || slot >= 2 || !m_singleSampleColorDirty[slot] ||
      m_msaaFBOs[slot] == 0 || m_fboTextures[slot] == 0 || m_glProgram == 0) {
    return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBOs[slot]);
  glViewport(0, 0, static_cast<GLsizei>(m_headlessWidth),
             static_cast<GLsizei>(m_headlessHeight));
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (m_supportsSampleMask) {
    glDisable(GL_SAMPLE_MASK);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindSampler(0, 0);
  glBindTexture(GL_TEXTURE_2D, m_fboTextures[slot]);

  glUseProgram(m_glProgram);
  if (m_uUseGammaLutLoc != -1) {
    glUniform1i(m_uUseGammaLutLoc, 0);
  }
  if (m_uPreserveAlphaLoc != -1) {
    glUniform1i(m_uPreserveAlphaLoc, 1);
  }
  glBindVertexArray(m_glVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glBindVertexArray(0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  m_singleSampleColorDirty[slot] = false;
  m_msaaColorDirty[slot] = false;
  m_glesDirtyMask |= (kGlesDirtyScissor | kGlesDirtyDepth | kGlesDirtyStencil |
                      kGlesDirtyBlend | kGlesDirtyCull | kGlesDirtyColorMask |
                      kGlesDirtySampleMask);
  InvalidateGlBindingCache();
}

void OpenGLESBackend::SetColorMask(bool rgb, bool alpha) {
  SetColorMaskExt(rgb, rgb, rgb, alpha);
}

void OpenGLESBackend::SetColorMaskExt(bool r, bool g, bool b, bool a) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_colorMaskR == r && m_colorMaskG == g && m_colorMaskB == b &&
      m_colorMaskAlpha == a) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetColorMaskExt_Locked(r, g, b, a);
  m_glesDirtyMask |= kGlesDirtyColorMask;
}

void OpenGLESBackend::SetAAMultiSample(bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_aaMultiSample == enabled) return;
  FlushBatch(true);
  GlideStateTracker::SetAAMultiSample_Locked(enabled);
  m_glesDirtyMask |= kGlesDirtySampleMask;
}

void OpenGLESBackend::SetTBufferWriteMask(uint32_t mask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_tbufferWriteMask == mask) return;
  FlushBatch(true);
  GlideStateTracker::SetTBufferWriteMask_Locked(mask);
  m_glesDirtyMask |= kGlesDirtySampleMask;
}

void OpenGLESBackend::SetStencilMode(bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stencilEnabled == enabled) return;
  FlushBatch(true);
  GlideStateTracker::SetStencilMode_Locked(enabled);
  m_glesDirtyMask |= kGlesDirtyStencil;
}

void OpenGLESBackend::SetStencilFunc(uint32_t compareOp, uint8_t refVal,
                                     uint8_t readMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const uint32_t op = compareOp & 0x7u;
  if (m_stencilCompareOp == op && m_stencilRef == refVal &&
      m_stencilReadMask == readMask) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetStencilFunc_Locked(op, refVal, readMask);
  m_glesDirtyMask |= kGlesDirtyStencil;
}

void OpenGLESBackend::SetStencilMask(uint8_t writeMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stencilWriteMask == writeMask) return;
  FlushBatch(true);
  GlideStateTracker::SetStencilMask_Locked(writeMask);
  m_glesDirtyMask |= kGlesDirtyStencil;
}

void OpenGLESBackend::SetStencilOp(uint32_t failOp, uint32_t zFailOp,
                                   uint32_t zPassOp) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const uint32_t f = failOp & 0x7u;
  const uint32_t zf = zFailOp & 0x7u;
  const uint32_t zp = zPassOp & 0x7u;
  if (m_stencilFailOp == f && m_stencilZFailOp == zf &&
      m_stencilZPassOp == zp) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetStencilOp_Locked(f, zf, zp);
  m_glesDirtyMask |= kGlesDirtyStencil;
}

void OpenGLESBackend::SetGamma(float gamma) {
  std::lock_guard<std::mutex> lock(m_mutex);
  GLIDE_LOG(INFO, "GLES", "SetGamma: value=" << gamma);
  m_gammaCorrectionValue = gamma;
  if (gamma <= 0.0f) gamma = 1.0f;

  uint32_t rTable[256], gTable[256], bTable[256];
  for (int i = 0; i < 256; ++i) {
    rTable[i] = static_cast<uint32_t>(
        std::pow(i / 255.0f, 1.0f / gamma) * 255.0f + 0.5f);
    gTable[i] = rTable[i];
    bTable[i] = rTable[i];
  }
  LoadGammaTable_Locked(256, rTable, gTable, bTable);
  m_useGammaLut = (gamma != 1.0f);
}

void OpenGLESBackend::LoadGammaTable(uint32_t nentries, const uint32_t* rTable,
                                     const uint32_t* gTable,
                                     const uint32_t* bTable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  LoadGammaTable_Locked(nentries, rTable, gTable, bTable);
}

void OpenGLESBackend::LoadGammaTable_Locked(uint32_t nentries,
                                            const uint32_t* rTable,
                                            const uint32_t* gTable,
                                            const uint32_t* bTable) {
  GLIDE_LOG(
      INFO, "GLES",
      "LoadGammaTable: nentries=" << nentries << ", tex=" << m_glGammaLutTex);
  if (nentries == 0 || (!rTable && !gTable && !bTable)) return;

  const uint32_t count = std::min(nentries, 256u);
  for (uint32_t i = 0; i < count; ++i) {
    if (rTable) m_lutR[i] = static_cast<uint8_t>(rTable[i] & 0xFFu);
    if (gTable) m_lutG[i] = static_cast<uint8_t>(gTable[i] & 0xFFu);
    if (bTable) m_lutB[i] = static_cast<uint8_t>(bTable[i] & 0xFFu);
  }

  if (m_glGammaLutTex && EnsureContextCurrent()) {
    uint8_t lutData[256 * 3];
    for (int i = 0; i < 256; ++i) {
      lutData[i * 3 + 0] = m_lutR[i];
      lutData[i * 3 + 1] = m_lutG[i];
      lutData[i * 3 + 2] = m_lutB[i];
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_glGammaLutTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE,
                    lutData);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_cachedActiveTexUnit = 0;
    m_cachedBoundTex[0] = 0;
    checkGLError("LoadGammaTable glTexSubImage2D");
  }
  m_useGammaLut = true;
}

void OpenGLESBackend::SetDepthState(uint32_t depthMode, uint32_t compareOp,
                                    bool depthMask, int32_t biasLevel) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_depthMode == depthMode && m_depthCompareOp == compareOp &&
      m_depthMask == depthMask && m_depthBiasLevel == biasLevel) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetDepthState_Locked(depthMode, compareOp, depthMask, biasLevel);
  m_glesDirtyMask |= kGlesDirtyDepth;
}

void OpenGLESBackend::SetCullState(uint32_t cullMode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_cullMode == cullMode) return;
  FlushBatch(true);
  GlideStateTracker::SetCullState_Locked(cullMode);
  m_glesDirtyMask |= kGlesDirtyCull;
}

void OpenGLESBackend::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_alphaTestOp == compareOp && m_alphaTestRefVal == refVal) return;
  FlushBatch(true);
  GlideStateTracker::SetAlphaTestState_Locked(compareOp, refVal);
  m_glesDirtyMask |= kGlesDirtyAlphaTest;
}

void OpenGLESBackend::SetSstOrigin(uint32_t origin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_sstOrigin == origin) return;
  FlushBatch(true);
  GlideStateTracker::SetSstOrigin_Locked(origin);
  m_glesDirtyMask |=
      (kGlesDirtyViewportAndOrigin | kGlesDirtyScissor | kGlesDirtyCull);
}

void OpenGLESBackend::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX,
                                    uint32_t maxY) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_clipMinX == minX && m_clipMinY == minY && m_clipMaxX == maxX &&
      m_clipMaxY == maxY) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetClipWindow_Locked(minX, minY, maxX, maxY);
  m_glesDirtyMask |= kGlesDirtyScissor;
}

void OpenGLESBackend::SetConstantColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_constantColor == color) return;
  FlushBatch(true);
  GlideStateTracker::SetConstantColor_Locked(color);
  m_glesDirtyMask |= kGlesDirtyConstantColor;
}

void OpenGLESBackend::SetPixelFormat(uint32_t format) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_pixelFormatOverride == format) return;
  FlushBatch(true);
  GlideStateTracker::SetPixelFormat_Locked(format);
  m_glesDirtyMask |= (kGlesDirtyConstantColor | kGlesDirtyChromakey |
                      kGlesDirtyFogModeAndColor | kGlesDirtyTexChromakey);
}

void OpenGLESBackend::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor,
                                      uint32_t colorLocal, uint32_t colorOther,
                                      bool colorInvert, uint32_t alphaFunc,
                                      uint32_t alphaFactor, uint32_t alphaLocal,
                                      uint32_t alphaOther, bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_colorCombinerFunc == colorFunc &&
      m_colorCombinerFactor == colorFactor &&
      m_colorCombinerLocal == colorLocal &&
      m_colorCombinerOther == colorOther &&
      m_colorCombinerInvert == colorInvert &&
      m_alphaCombinerFunc == alphaFunc &&
      m_alphaCombinerFactor == alphaFactor &&
      m_alphaCombinerLocal == alphaLocal &&
      m_alphaCombinerOther == alphaOther &&
      m_alphaCombinerInvert == alphaInvert) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetCombinerMode_Locked(colorFunc, colorFactor, colorLocal,
                                     colorOther, colorInvert, alphaFunc,
                                     alphaFactor, alphaLocal, alphaOther,
                                     alphaInvert);
  m_glesDirtyMask |= kGlesDirtyColorCombiner;
}

void OpenGLESBackend::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc,
                                         uint32_t rgbFactor, uint32_t alphaFunc,
                                         uint32_t alphaFactor, bool rgbInvert,
                                         bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  if (m_texCombinerRgbFunc[tmu] == rgbFunc &&
      m_texCombinerRgbFactor[tmu] == rgbFactor &&
      m_texCombinerAlphaFunc[tmu] == alphaFunc &&
      m_texCombinerAlphaFactor[tmu] == alphaFactor &&
      m_texCombinerRgbInvert[tmu] == rgbInvert &&
      m_texCombinerAlphaInvert[tmu] == alphaInvert) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetTexCombinerMode_Locked(tmu, rgbFunc, rgbFactor, alphaFunc,
                                        alphaFactor, rgbInvert, alphaInvert);
  m_glesDirtyMask |= kGlesDirtyTexCombiner;
}

void OpenGLESBackend::SetSTWHintState(uint32_t hintMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stwHintMask == hintMask) return;
  FlushBatch(true);
  GlideStateTracker::SetSTWHintState_Locked(hintMask);
  m_glesDirtyMask |= kGlesDirtyStwHint;
}

void OpenGLESBackend::SetChromakeyMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_chromakeyMode == mode) return;
  FlushBatch(true);
  GlideStateTracker::SetChromakeyMode_Locked(mode);
  m_glesDirtyMask |= kGlesDirtyChromakey;
}

void OpenGLESBackend::SetChromakeyValue(uint32_t value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_chromakeyValue == value) return;
  FlushBatch(true);
  GlideStateTracker::SetChromakeyValue_Locked(value);
  m_glesDirtyMask |= kGlesDirtyChromakey;
}

void OpenGLESBackend::SetChromakeyRange(uint32_t minColor, uint32_t maxColor,
                                        uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_chromakeyRangeMin == minColor && m_chromakeyRangeMax == maxColor &&
      m_chromakeyRangeMode == mode) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetChromakeyRange_Locked(minColor, maxColor, mode);
  m_glesDirtyMask |= kGlesDirtyChromakey;
}

void OpenGLESBackend::SetTexChromakeyMode(uint32_t tmu, uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || m_texChromaMode[tmu] == mode) return;
  FlushBatch(true);
  GlideStateTracker::SetTexChromakeyMode_Locked(tmu, mode);
  m_glesDirtyMask |= kGlesDirtyTexChromakey;
}

void OpenGLESBackend::SetTexChromakeyRange(uint32_t tmu, uint32_t minColor,
                                           uint32_t maxColor, uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  if (m_texChromaMin[tmu] == minColor && m_texChromaMax[tmu] == maxColor &&
      m_texChromaRangeMode[tmu] == mode) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetTexChromakeyRange_Locked(tmu, minColor, maxColor, mode);
  m_glesDirtyMask |= kGlesDirtyTexChromakey;
}

void OpenGLESBackend::SetFogMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_fogMode == mode) return;
  FlushBatch(true);
  GlideStateTracker::SetFogMode_Locked(mode);
  m_glesDirtyMask |= kGlesDirtyFogModeAndColor;
}

void OpenGLESBackend::SetFogColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_fogColor == color) return;
  FlushBatch(true);
  GlideStateTracker::SetFogColor_Locked(color);
  m_glesDirtyMask |= kGlesDirtyFogModeAndColor;
}

void OpenGLESBackend::SetFogTable(const uint8_t* table) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!table || std::memcmp(m_fogTable, table, sizeof(m_fogTable)) == 0) {
    return;
  }
  FlushBatch(true);
  GlideStateTracker::SetFogTable_Locked(table);
  m_glesDirtyMask |= kGlesDirtyFogTable;
}

}  // namespace GlideWrapper
