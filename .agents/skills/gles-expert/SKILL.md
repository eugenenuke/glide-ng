---
name: gles-expert
description: Specialized EGL & OpenGL ES 3.2 backend graphics driver wrapper expert.
---

# Workspace Skill: OpenGL ES 3.2 & EGL Backend Expert

Activate this skill when analyzing, modifying, or debugging the OpenGL ES 3.2 backend inside [src/backends/opengl_es/](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/opengl_es/) (`OpenGLESBackend.h` / `OpenGLESBackend.cpp`).

## 1. EGL Surface & Context Isolation (DOSBox Co-existence)

- **Never Hijack Host GLX/OpenGL Contexts**:
  - Host clients like DOSBox-X (`output=opengl`) maintain their own desktop OpenGL context on the parent window. Calling `SDL_GL_CreateContext` on a host or SDL overlay window causes GLX vs. EGL/GLES context conflicts and crashes.
  - Instead, bind directly via **EGL 1.5** onto `NativeSurfaceHost`'s isolated child surface:
    - **X11 / XWayland child window**: `eglGetPlatformDisplay(EGL_PLATFORM_X11_EXT, x11Display, nullptr)` → `eglCreateWindowSurface(eglDisplay, cfg, (EGLNativeWindowType)childWin, nullptr)`.
    - **Wayland subsurface**: `eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wlDisplay, nullptr)` → `wl_egl_window_create(wlChildSurface, w, h)` → `eglCreateWindowSurface(...)`. Call `wl_egl_window_resize(wlEglWin, w, h, 0, 0)` whenever `NativeSurfaceHost::SyncBounds()` reports a size change.
  - Explicitly call `eglBindAPI(EGL_OPENGL_ES_API)` and request ES 3.2 (`EGL_CONTEXT_MAJOR_VERSION=3, EGL_CONTEXT_MINOR_VERSION=2`).
- **Strict Teardown Order in `DetachWindow()`**:
  1. `eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`
  2. Destroy GL textures, FBOs, buffers, VAOs, programs (while context is current, before step 1!)
  3. `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` (if Wayland) → `eglTerminate`
  4. `m_surfaceHost.Detach()` **last** (never destroy the native X11 window or `wl_surface` while `EGLSurface` is still alive).

## 2. Presentation, Aspect Ratio & Gamma LUT

- **Never Bypass `m_glProgram` With Raw `glBlitFramebuffer` to Default FBO `0`**:
  - Blitting the offscreen FBO directly to `0..winW, 0..winH` both stretches 4:3 games across 16:9 screens and completely bypasses the Gamma LUT fragment shader (`m_glProgram` / `m_gammaLutTexture`).
  - Always present to FBO `0` by:
    1. Setting `glViewport(0, 0, hostW, hostH)`, clearing to opaque black (`glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT)`).
    2. Computing `PresentRect dst = NativeSurfaceHost::ComputePresentRect(guestW, guestH, hostW, hostH, preserveAspect)` and setting `glViewport(dst.x, dst.y, dst.width, dst.height)`.
    3. Drawing a fullscreen quad with `m_glProgram` sampling the resolved offscreen color texture + Gamma LUT.
    4. Calling `eglSwapBuffers(eglDisplay, eglSurface)`.

## 3. OpenGL ES 3.2 API & Coordinate Conformity

- Stay strictly within OpenGL ES 3.2 (no desktop-only DSA or fixed-function calls).
- Declare explicit precision qualifiers at the top of every GLSL ES 3.20 shader (`#version 320 es`, `precision highp float;`, `precision highp int;`) and explicit `layout(location = ...)` / `layout(binding = ...)` qualifiers.
- Handle Glide's upper-left (`GR_ORIGIN_UPPER_LEFT`) vs. lower-left (`GR_ORIGIN_LOWER_LEFT`) origin symmetrically in both vertex coordinates and `glFrontFace(GL_CW / GL_CCW)`.
- Map Glide TMUs cleanly via `glActiveTexture(GL_TEXTURE0 + tmuIndex)`.
- Use `GLIDE_LOG(...)` (never raw `std::cout << "[DEBUG-GLES] ..."`).
- Do **not** call `SoftwareBackendBase::ClearBuffer(...)` inside `OpenGLESBackend::ClearBuffer(...)` once GPU clears own the framebuffer.
