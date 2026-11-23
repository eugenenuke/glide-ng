---
name: gles-expert
description: Specialized OpenGL ES 3.2 (EGL 1.5 Linux) and OpenGL 3.3 Core (WGL Win32) backend graphics driver wrapper expert.
---

# Workspace Skill: OpenGL ES 3.2 / WGL OpenGL 3.3 Core Backend Expert

Activate this skill when analyzing, modifying, or debugging the OpenGL backend inside [src/backends/opengl_es/](../../../src/backends/opengl_es/).

## 1. Module Layout & Inheritance

`OpenGLESBackend` inherits from `GlideStateTracker` (decoupled from `SoftwareBackendBase`) and compiles for **two** native paths behind one shared backend interface:
- **Linux (`EGL 1.5` + OpenGL ES 3.2):** Isolated EGL display/context bound onto `NativeSurfaceHost`'s child surface.
- **Win32 (`WGL` + OpenGL 3.3 Core):** Dynamically loaded via `Win32GlLoader.h` with zero external runtime DLL dependencies.

Split across focused files: `OpenGLESBackend.{h,cpp}`, `OpenGLESShaders.cpp`, `OpenGLESTextures.cpp`, `OpenGLESDraw.cpp`, `OpenGLESInternal.h`, `Win32GlLoader.h`.

## 2. Context Isolation, Cross-Thread Safety & Teardown

- **Never Hijack Host GLX/OpenGL Contexts (Linux):**
  - Hosts like DOSBox-X (`output=opengl`) own a desktop OpenGL context on the parent window. Calling `SDL_GL_CreateContext` on a host or overlay window causes GLX vs. EGL/GLES conflicts.
  - Bind directly via **EGL 1.5** onto `NativeSurfaceHost`'s child surface:
    - **X11 / XWayland child window:** `eglGetPlatformDisplay(EGL_PLATFORM_X11_EXT, x11Display, nullptr)` → `eglCreateWindowSurface(eglDisplay, cfg, (EGLNativeWindowType)childWin, nullptr)`.
    - **Wayland subsurface:** `eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wlDisplay, nullptr)` → `wl_egl_window_create(wlChildSurface, w, h)` → `eglCreateWindowSurface(...)`. Resize via `wl_egl_window_resize(...)` whenever `NativeSurfaceHost::SyncBounds()` reports a size change.
  - **Save/restore `eglBindAPI()`:** Call `eglQueryAPI()` before `eglBindAPI(EGL_OPENGL_ES_API)` and restore the previous thread API afterward so host `EGL_OPENGL_API` callers on the same thread stay intact.
- **Cross-Thread Context Release (`FrontBufferFlushTimer`):**
  - Un-swapped front-buffer writes arm `m_frontFlushTimer` (runs on a timer thread). Because EGL/WGL forbids binding one context on two threads concurrently (`EGL_BAD_ACCESS`), release the active context via **`ReleaseFrontPresentContext_Locked()`** / **`ScopedFrontContextRelease`** (and **`ReleaseWglContextOnCurrentThread_Locked()`** on Win32) around hand-offs so the worker thread can acquire, flush, and release cleanly.
- **Strict Teardown Order in `DetachWindow()`:**
  1. Ensure the context is **current** and destroy GL textures, FBOs, buffers, VAOs, shaders, and programs first.
  2. Unbind the context: `eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)` (or `wglMakeCurrent(nullptr, nullptr)` on Win32).
  3. `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` (if Wayland) → `eglTerminate` (on Win32: `wglDeleteContext`).
  4. `m_surfaceHost.Detach()` **last** (never destroy the native X11 window, `wl_surface`, or `HWND` while `EGLSurface`/WGL DC is still alive).

## 3. Presentation, Aspect Ratio & Gamma LUT

- **Never Bypass `m_glProgram` With Raw `glBlitFramebuffer` to Default FBO `0`:**
  - Raw blits stretch 4:3 games across 16:9 screens and bypass the Gamma LUT fragment shader (`m_glProgram` / `m_gammaLutTexture`).
  - Always present to FBO `0` by clearing `glViewport(0, 0, hostW, hostH)` to opaque black, setting `glViewport(dst.x, dst.y, dst.width, dst.height)` from `NativeSurfaceHost::ComputePresentRect(...)`, drawing the fullscreen gamma-LUT quad, and calling `eglSwapBuffers` / `SwapBuffers`.

## 4. API & Dual Shader Conformity (`GLES 3.20` / `GL 3.30`)

- Stay strictly within the intersection of **OpenGL ES 3.2** (`#version 320 es`) and **OpenGL 3.3 Core** (`#version 330 core`) with shared helpers in `OpenGLESShaders.cpp` (avoid desktop-only DSA or fixed-function calls).
- Declare explicit precision qualifiers (`precision highp float;`, `precision highp int;`) and explicit `layout(location = ...)` / `layout(binding = ...)` (where GLES supports it).
- Handle `GR_ORIGIN_UPPER_LEFT` vs. `GR_ORIGIN_LOWER_LEFT` symmetrically in vertex Y coordinates and `glFrontFace(GL_CW / GL_CCW)`.
- Support `GR_BUFFER_AUXBUFFER` (`buffer == 2u` / `GR_LFB_SRC_FMT_ZA16`) uploads and readbacks inside `OpenGLESBackend.cpp`.
- Map Glide TMUs via `glActiveTexture(GL_TEXTURE0 + tmuIndex)`.
- Use `GLIDE_LOG(...)` (never raw `std::cout` debug prints).
