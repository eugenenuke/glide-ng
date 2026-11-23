---
name: wsi-window-injection
description: Native child surface injection (X11 child Window, Wayland wl_subsurface, Win32 HWND), DOSBox-X / SVN DOSBox windowing quirks, and aspect-ratio presentation authority.
---

# Workspace Skill: WSI Window Injection & Native Child Surfaces

Activate this skill whenever touching [src/core/NativeSurfaceHost.h](../../../src/core/NativeSurfaceHost.h), `NativeSurfaceHost.cpp`, backend `AttachWindow`/`DetachWindow`/`SwapBuffers` presentation, Win32/Wine `HWND` hosting, or DOSBox-X / SVN DOSBox integration.

## 1. Architecture: Unpatched Native Child Surface

All three backends (`VulkanBackend`, `OpenGLESBackend`, `SoftwareBackend`) share `NativeSurfaceHost` for OS surface lifecycle, keeping SDL2 window creation strictly for standalone `hWnd == 0` Linux demos/tests:

| Mode | Trigger | Surface Mechanism | Input Passthrough |
| :--- | :--- | :--- | :--- |
| `Headless` | `forceNoWindow == true` | None (CPU/offscreen only) | N/A |
| `StandaloneSDL2` | Linux `hWnd == 0` | Owned `SDL_Window*` | Normal SDL events |
| `X11Child` | Linux host on X11 / XWayland | Private `XOpenDisplay(nullptr)` + `XCreateWindow(parent=hostX11Win)` | `XShapeCombineRectangles(..., ShapeInput, empty)` |
| `WaylandSubsurface` | Linux host on native Wayland | Private `wl_event_queue` + fresh roleless `wl_surface` + `wl_subcompositor_get_subsurface` + `wl_subsurface_set_desync` | Empty `wl_region` via `wl_surface_set_input_region` |
| `Win32Child` / `Standalone` | Win32 / Wine `HWND` | Native Win32 `HWND` (`user32.dll`, zero SDL2 dependency) | Native subclass / `WM_*` forwarding |

## 2. Hard Invariants & Anti-Patterns (NEVER VIOLATE)

1. **Preprocessor Macro Trap:**
   - ALWAYS use `#if defined(__unix__) && !defined(__APPLE__)` (and CMake defines `HAVE_X11`, `HAVE_WAYLAND`, `HAVE_WAYLAND_EGL`, `HAVE_EGL`).
   - **NEVER** use `#if defined(UNIX)` — GCC/Clang do not define `UNIX`, silently compiling out all Linux WSI code.
2. **Wayland Protocol Role Violation:**
   - **NEVER** call `SDL_CreateWindow()` and pass its `wl_surface` to `wl_subcompositor_get_subsurface()`. SDL assigns `xdg_toplevel` role to its surface; attaching a second role (`wl_subsurface`) is a fatal Wayland protocol error.
   - Always create a fresh, roleless `wl_surface` using a private `wl_event_queue` (`wl_display_create_queue` + `wl_proxy_create_wrapper`), call `wl_subsurface_set_desync`, and latch via **one** `wl_surface_commit(hostSurface)` + `wl_display_flush(hostDisplay)`.
   - Guard late teardown against a disconnected borrowed `wl_display*` so host shutdown race conditions never crash.
3. **X11 Thread Safety, Visuals & Late Teardown:**
   - **NEVER** share DOSBox's `Display*` or rely on a late `dlopen`-time `XInitThreads()` constructor. Open a dedicated private connection (`XOpenDisplay(nullptr)`) and wrap parent `XGetGeometry` probes with a scoped `XErrorHandler`.
   - Use **`RecreateX11ChildForVisual(visualId)`** when a parent host window holds a non-`TrueColor` visual (e.g. `DirectColor`) so Vulkan/EGL present onto a compatible visual.
   - Guard late teardown against already-destroyed parents (`DestroyNotify` / `BadWindow`).
4. **Win32 / Wine `HWND` Hosting Invariants:**
   - Call **`HideWin32EmbeddedChildren()`** cleanly when attaching to games with embedded child controls / launchers so stale sibling windows don't occlude the Glide viewport.
   - Respect **`kWin32FullscreenDeactivateGrace` (`400 ms`)** via `ShouldSuppressWin32DeactivateBounce()` so transient `WM_ACTIVATE`/`WM_ACTIVATEAPP` deactivation bounces right after borderless-fullscreen resize do not auto-minimize the game.
   - On `grSstPassthruMode` **`SHOW_VGA` ↔ `SHOW_SST1`** transitions, reclaim bounds cleanly with `SyncBounds()` called **outside** backend `m_mutex` to prevent deadlocks with the host message pump.
5. **Strict WSI Teardown Order:**
   - Always destroy GPU presentation objects **before** destroying the native child surface:
     - **Vulkan:** `vkDeviceWaitIdle()` → destroy swapchain → `vkDestroySurfaceKHR` → `NativeSurfaceHost::Detach()`.
     - **GLES/EGL:** destroy GL objects (while context current) → `eglMakeCurrent(NO_SURFACE, NO_CONTEXT)` → `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` (Wayland) → `eglTerminate` → `NativeSurfaceHost::Detach()`.
6. **Event Loop Discipline:**
   - Never call `SDL_PumpEvents()` or `SDL_PollEvent()` when attached to a foreign DOSBox host window (`X11Child` / `WaylandSubsurface`). DOSBox owns its event loop.

## 3. DOSBox Client Quirks

- **DOSBox-X** (`src/hardware/glide.cpp`):
  - Links `libSDL2`. `GLIDE_ResetScreen(true)` creates `SCREEN_OPENGL` or `SCREEN_SURFACE` (never `SDL_WINDOW_VULKAN`).
  - `statWMInfo()` returns `wmi.info.x11.window` even on Wayland, aliasing `wmi.info.wl.surface` inside `SDL_SysWMinfo`. When `SDL_GetCurrentVideoDriver()` is `"wayland"`, probe live SDL windows (`SDL_GetWindowFromID(1..64)`) to recover `(wl_display*, wl_surface*, SDL_Window*)`.
  - Pressing `Alt+Enter` mid-game calls `GLIDE_ResetScreen(false)` on unpatched DOSBox-X; handle detached/recreated parent handles defensively.
- **Patched SVN DOSBox (`dosbox-0`):**
  - Probes `dlsym("setConfig")`: if exported and the wrapper writes `0x324c4453` (`'SDL2'`) to `*magic`, SVN DOSBox passes `SDL_Window*` via `SDL12COMPAT_GetWindow`; otherwise it passes a raw X11 `Window` ID.

## 4. Aspect-Ratio & HiDPI Presentation

- Always compute destination coordinates via `NativeSurfaceHost::ComputePresentRect(guestW, guestH, hostPixelW, hostPixelH, preserveAspect)` so 4:3 games render centered with opaque black pillarbox/letterbox bars instead of stretching across 16:9 displays.
- On Wayland HiDPI displays, track physical drawable size vs. logical window size and synchronize `wl_surface_set_buffer_scale` / `wl_egl_window_resize` on `SyncBounds()`.
- Optional Xlib dependency uses `x11` + `xext` (`shape.h`, `XShm.h`) rather than `x11-xcb`.
