---
name: wsi-window-injection
description: Native child surface injection (X11 child Window & Wayland wl_subsurface), DOSBox-X / SVN DOSBox windowing quirks, and aspect-ratio presentation authority.
---

# Workspace Skill: WSI Window Injection & Native Child Surfaces

Activate this skill whenever touching [src/core/NativeSurfaceHost.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/core/NativeSurfaceHost.h), `NativeSurfaceHost.cpp`, `PlatformWindowUtils.*`, backend `AttachWindow`/`DetachWindow`/`SwapBuffers` presentation, or DOSBox-X / SVN DOSBox integration.

## 1. Architecture: Unpatched Native Child Surface (Option A)

All three backends (`VulkanBackend`, `OpenGLESBackend`, `SoftwareBackend`) share `NativeSurfaceHost` for OS surface lifecycle, keeping SDL2 window creation strictly for standalone `hWnd == 0` demos/tests:

| Mode | Trigger | Surface Mechanism | Input Passthrough |
| :--- | :--- | :--- | :--- |
| `Headless` | `forceNoWindow == true` | None (CPU/offscreen only) | N/A |
| `StandaloneSDL2` | `hWnd == 0` | Owned `SDL_Window*` | Normal SDL events |
| `X11Child` | Host on X11 / XWayland | Private `XOpenDisplay(nullptr)` + `XCreateWindow(parent=hostX11Win)` | `XShapeCombineRectangles(..., ShapeInput, empty)` |
| `WaylandSubsurface` | Host on native Wayland | Private `wl_event_queue` + fresh roleless `wl_surface` + `wl_subcompositor_get_subsurface` + `wl_subsurface_set_desync` | Empty `wl_region` via `wl_surface_set_input_region` |

## 2. Hard Invariants & Anti-Patterns (NEVER VIOLATE)

1. **Pre-processor Macro Trap**:
   - ALWAYS use `#if defined(__unix__) && !defined(__APPLE__)` (and CMake defines `HAVE_X11`, `HAVE_WAYLAND`, `HAVE_WAYLAND_EGL`, `HAVE_EGL`).
   - **NEVER** use `#if defined(UNIX)` — GCC/Clang do not define `UNIX`, silently compiling out all Linux WSI code!
2. **Wayland Protocol Role Violation**:
   - **NEVER** call `SDL_CreateWindow()` and then pass its `wl_surface` to `wl_subcompositor_get_subsurface()`. SDL assigns `xdg_toplevel` role to its surface; attaching a second role (`wl_subsurface`) is a fatal Wayland protocol error that kills the process.
   - Always create a **fresh, roleless `wl_surface`** from `wl_compositor_create_surface` using a **private `wl_event_queue`** (`wl_display_create_queue` + `wl_proxy_create_wrapper`), call `wl_subsurface_set_desync`, and issue **one** `wl_surface_commit(hostSurface)` + `wl_display_flush(hostDisplay)` to latch the subsurface tree.
3. **X11 Thread Safety**:
   - **NEVER** share DOSBox's `Display*` or rely on a late `dlopen`-time `XInitThreads()` constructor (calling `XInitThreads()` after the host already opened X11 violates the Xlib contract).
   - Open a dedicated private connection (`XOpenDisplay(nullptr)`) in `NativeSurfaceHost` and wrap parent `XGetGeometry` probes with a scoped `XErrorHandler` so invalid handles never abort the process.
4. **Strict WSI Teardown Order**:
   - Always destroy GPU presentation objects **before** destroying the native child surface:
     - Vulkan: `vkDeviceWaitIdle()` → destroy swapchain → `vkDestroySurfaceKHR` → `NativeSurfaceHost::Detach()`.
     - GLES/EGL: `eglMakeCurrent(NO_SURFACE, NO_CONTEXT)` → `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` → `eglTerminate` → `NativeSurfaceHost::Detach()`.
5. **Event Loop Discipline**:
   - Never call `SDL_PumpEvents()` or `SDL_PollEvent()` when attached to a foreign DOSBox host window (`X11Child` / `WaylandSubsurface`). DOSBox owns its SDL event loop.

## 3. DOSBox Client Quirks (Verified Against Reference Trees)

- **DOSBox-X** ([refs/dosbox-x/src/hardware/glide.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/dosbox-x/src/hardware/glide.cpp)):
  - Links `libSDL2` (`sdl2-compat` on this host). `GLIDE_ResetScreen(true)` creates `SCREEN_OPENGL` or `SCREEN_SURFACE` (never `SDL_WINDOW_VULKAN`).
  - `statWMInfo()` unconditionally returns `wmi.info.x11.window` even on Wayland — which aliases `wmi.info.wl.surface` in `SDL_SysWMinfo`. When `SDL_GetCurrentVideoDriver()` is `"wayland"`, probe live SDL windows (`SDL_GetWindowFromID(1..64)`) to match either `SDL_Window*` directly or `wmi.info.wl.surface == hostHandle` to recover `(wl_display*, wl_surface*, SDL_Window*)`.
  - Known unpatched limitation: pressing `Alt+Enter` mid-game calls `GLIDE_ResetScreen(false)`. Track whether this triggers Phase 4 (Option B).
- **Patched SVN DOSBox (`dosbox-0`)**:
  - Links `libSDL-1.2` (`sdl12-compat` on modern Linux).
  - Probes `dlsym("setConfig")`: if exported and our wrapper writes `0x324c4453` (`'SDL2'`) to `*magic`, SVN DOSBox passes `SDL_Window*` via `SDL12COMPAT_GetWindow`; otherwise it passes a raw X11 `Window` ID.

## 4. Aspect-Ratio & HiDPI Presentation

- Always compute destination coordinates via `NativeSurfaceHost::ComputePresentRect(guestW, guestH, hostPixelW, hostPixelH, preserveAspect)` so 4:3 games (e.g., 640×480 GTA 1) render centered with black pillarbox/letterbox bars instead of stretching across 16:9 screens.
- On Wayland HiDPI displays, track physical drawable size vs. logical window size and synchronize `wl_surface_set_buffer_scale` / `wl_egl_window_resize` on `SyncBounds()`.
- Available system libs on this host (verified via `pkg-config`): `x11`, `xext` (`shape.h`, `XShm.h`), `wayland-client`, `wayland-egl`, `egl`, `glesv2`, `vulkan`, `sdl2`. (`x11-xcb` is **not** installed — use Xlib.)
