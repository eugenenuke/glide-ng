#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace GlideWrapper {

/**
 * @brief Pixel-space presentation rectangle inside the host drawable surface.
 *
 * Used by all rendering backends (Vulkan, OpenGL ES, Software) to present the
 * guest Glide framebuffer centered with black letterbox/pillarbox bars while
 * preserving the guest aspect ratio.
 */
struct PresentRect {
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;

  bool operator==(const PresentRect& other) const {
    return x == other.x && y == other.y && width == other.width &&
           height == other.height;
  }
  bool operator!=(const PresentRect& other) const { return !(*this == other); }
};

/**
 * @brief Unified OS surface & window host for Option A (Unpatched Native Child
 * Surface) across Vulkan, OpenGL ES, and Software backends.
 *
 * Design invariants:
 * - Never includes X11 or Wayland headers in this public header to avoid
 *   macro pollution (e.g. Xlib's `#define None 0L`).
 * - `Headless`: zero OS/SDL/WSI calls (safe for automated tests & CI).
 * - `StandaloneSDL2`: used ONLY when `hostHandle == nullptr` (demos/tools).
 *   Sets `SDL_HINT_NO_SIGNAL_HANDLERS=1` so Ctrl+C/SIGTERM exit cleanly and
 *   tracks `SDL_QUIT` / window close in `PollEvents()`.
 * - `X11Child` (T2.2) & `WaylandSubsurface` (T2.3): embed a click-through
 *   native child surface inside unpatched DOSBox-X / SVN DOSBox without
 *   touching the host's SDL event loop or GLX/EGL context.
 */
class NativeSurfaceHost {
 public:
  enum class Mode {
    Detached,
    Headless,
    StandaloneSDL2,
    X11Child,
    WaylandSubsurface,
    Win32Window,
    Win32Embedded,
  };

  NativeSurfaceHost() = default;
  ~NativeSurfaceHost();

  NativeSurfaceHost(const NativeSurfaceHost&) = delete;
  NativeSurfaceHost& operator=(const NativeSurfaceHost&) = delete;

  /**
   * @brief Computes the aspect-ratio-preserving presentation rectangle inside a
   * host surface using exact 64-bit integer cross-multiplication.
   */
  static PresentRect ComputePresentRect(uint32_t guestW, uint32_t guestH,
                                        uint32_t hostW, uint32_t hostH,
                                        bool preserveAspect = true);

  /**
   * @brief Inverse-maps a client-space host pointer coordinate inside a scaled
   *        aspect-preserving surface back into guest Glide pixel space
   *        `[0, guestW - 1] x [0, guestH - 1]`, clamping coordinates falling
   *        inside letterbox/pillarbox bars to the nearest guest edge.
   */
  static void MapHostPointToGuest(int32_t hostX, int32_t hostY,
                                  uint32_t guestW, uint32_t guestH,
                                  uint32_t hostW, uint32_t hostH,
                                  int32_t& outGuestX, int32_t& outGuestY,
                                  bool preserveAspect = true);

  static const char* ModeToString(Mode mode);

  /**
   * @brief Attaches or creates the presentation surface.
   *
   * @param hostHandle Raw host handle from `grSstWinOpen` (`nullptr` for
   *     standalone, X11 Window ID or `SDL_Window*` / `wl_surface*` for DOSBox).
   * @param guestW Guest Glide framebuffer width in pixels.
   * @param guestH Guest Glide framebuffer height in pixels.
   * @param windowed Hint from Glide frontend (`hWnd != 0` in Glide 2/3 entry).
   *     In `StandaloneSDL2` mode, windows always open as desktop windows scaled
   *     by `windowScale`.
   * @param forceNoWindow If true, forces `Mode::Headless` with zero OS calls.
   * @param title Window title used in `StandaloneSDL2` mode.
   * @param extraSdlWindowFlags Additional `SDL_WindowFlags` (e.g.,
   *     `SDL_WINDOW_VULKAN`, `SDL_WINDOW_OPENGL`) used ONLY in
   *     `StandaloneSDL2` mode.
   * @param windowScale Integer scale factor (1..8) for initial `StandaloneSDL2`
   *     window dimensions.
   * @param fullscreen If true, requests borderless desktop fullscreen in
   *     `StandaloneSDL2` / `Win32Window` modes, or strips window chrome and
   *     expands the attached host `HWND` to cover its monitor in
   *     `Win32Embedded` mode (aspect-preserving 4:3 pillarbox/letterbox).
   */
  bool Attach(void* hostHandle, uint32_t guestW, uint32_t guestH, bool windowed,
              bool forceNoWindow, const std::string& title,
              uint32_t extraSdlWindowFlags = 0, uint32_t windowScale = 1,
              bool fullscreen = false);

  /**
   * @brief Destroys our owned surface/child window and resets state.
   *
   * Callers MUST destroy GPU presentation objects (Vulkan swapchain/surface,
   * EGL context/surface, etc.) BEFORE calling `Detach()`. Idempotent.
   */
  void Detach();

  /**
   * @brief Synchronizes child surface dimensions with the host window.
   *
   * @param outPixelW Receives current drawable width in physical pixels.
   * @param outPixelH Receives current drawable height in physical pixels.
   * @return true if physical drawable dimensions changed since the last check.
   */
  bool SyncBounds(uint32_t& outPixelW, uint32_t& outPixelH);

  /**
   * @brief Notifies NativeSurfaceHost that a Win32Embedded host window is
   *        reclaiming 3dfx presentation after VGA pass-through
   *        (grSstPassthruMode SHOW_VGA -> SHOW_SST1).
   *
   * Schedules a one-shot post-transition restore/foreground check in
   * SyncBounds() so window managers do not leave the game minimized or behind
   * the launching terminal after a DirectDraw fullscreen intro exits.
   * Thread-safe / lock-free so backends may call it without risking WndProc
   * re-entrancy deadlocks.
   */
  void NotifyWin32PassthruReclaimed();

  /**
   * @brief Marks whether a Win32Embedded host window is currently yielded to
   *        VGA pass-through (grSstPassthruMode SHOW_VGA).
   *
   * While suspended, our borderless-fullscreen WndProc guard acts as a pure
   * pass-through and SyncBounds() refrains from resizing the host window so
   * DirectDraw/AVI intro playback is undisturbed.
   */
  void SetWin32PassthruSuspended(bool suspended);

  /**
   * @brief Returns and clears whether the embedded Win32 host window underwent
   *        a structural mutation (e.g. WS_POPUP <-> WS_CAPTION, WS_VISIBLE
   *        toggle, GWLP_WNDPROC subclassing, or pixel format change installed
   *        by DirectDraw/wined3d SetCooperativeLevel) that requires WSI
   *        surface recreation.
   */
  bool ConsumeSurfaceInvalidated();

  /**
   * @brief Re-baselines the cached Win32 host HWND fingerprint (style, extended
   *        style, WndProc, and pixel format) after the wrapper itself mutates
   *        the surface (e.g. SetPixelFormat or vkCreateWin32SurfaceKHR) so the
   *        next SyncBounds() check does not treat its own attachment as a
   *        foreign window takeover.
   */
  void RefreshWin32SurfaceFingerprint();

  /**
   * @brief Pumps window events ONLY in `StandaloneSDL2` mode.
   *
   * Guaranteed no-op in `Headless`, `X11Child`, and `WaylandSubsurface` modes
   * so foreign host event loops (DOSBox-X / SVN DOSBox) are never disturbed.
   *
   * @param force When true (default), pumps immediately (used at frame
   *     boundaries, window creation, and direct calls). Pass false from
   *     high-frequency Glide API hooks to throttle polling to at most once
   *     every 4 ms.
   */
  void PollEvents(bool force = true);

  bool IsAttached() const { return m_mode != Mode::Detached; }
  bool WasQuitRequested() const { return m_quitRequested; }
  Mode GetMode() const { return m_mode; }
  bool IsStandaloneMode() const {
    return m_mode == Mode::StandaloneSDL2 || m_mode == Mode::Win32Window;
  }
  bool IsWin32Surface() const {
    return m_mode == Mode::Win32Window || m_mode == Mode::Win32Embedded;
  }

  bool IsWaylandSurface() const;
  bool IsX11Surface() const;

  void GetDrawableSize(uint32_t& outPixelW, uint32_t& outPixelH) const {
    outPixelW = m_pixelWidth;
    outPixelH = m_pixelHeight;
  }

  void GetGuestSize(uint32_t& outGuestW, uint32_t& outGuestH) const {
    outGuestW = m_guestWidth;
    outGuestH = m_guestHeight;
  }

  // Native handles (opaque to keep X11/Wayland headers out of this header).
  void* GetX11Display() const { return m_x11Display; }
  unsigned long GetX11Window() const { return m_x11ChildWindow; }
  /**
   * @brief Recreates the owned X11 child window using a specific X11 VisualID
   *        and matching depth/colormap (preserving click-through ShapeInput).
   *
   * Needed by Direct EGL on X11 when the parent host window uses a visual
   * (e.g., DirectColor or GLX-chosen visual) that has no matching OpenGL ES 3.x
   * EGLConfig, preventing strict real-GPU drivers from returning EGL_BAD_CONFIG.
   */
  bool RecreateX11ChildForVisual(unsigned long visualId);
  void* GetWaylandDisplay() const { return m_wlDisplay; }
  void* GetWaylandSurface() const { return m_wlChildSurface; }
  void* GetWaylandShm() const { return m_wlShm; }
  void* GetWaylandEventQueue() const;
  int32_t GetWaylandScale() const { return m_wlBufferScale; }
  SDL_Window* GetSDLWindow() const { return m_sdlWindow; }
  void* GetWin32Hwnd() const { return m_win32Hwnd; }
  void* GetWin32Hinstance() const { return m_win32Hinstance; }

 private:
  bool AttachStandaloneSDL2(uint32_t guestW, uint32_t guestH,
                            const std::string& title,
                            uint32_t extraSdlWindowFlags, uint32_t windowScale);
  bool AttachX11Child(unsigned long parentWindow);
  bool AttachWaylandSubsurface(void* hostDisplay, void* hostSurface,
                               SDL_Window* hostSdlWindow);
  bool AttachWin32Standalone(uint32_t guestW, uint32_t guestH,
                             const std::string& title, uint32_t windowScale);
  bool AttachWin32Embedded(void* parentHwnd);
  bool ApplyWin32EmbeddedFullscreenIfNeeded();
  void RestoreWin32EmbeddedFullscreenIfSaved();
  void InstallWin32FullscreenSubclass();
  void UninstallWin32FullscreenSubclass();
  void SyncWin32SubclassState();
  void QueryStandaloneDrawableSize(uint32_t& outW, uint32_t& outH) const;

  Mode m_mode = Mode::Detached;
  uint32_t m_guestWidth = 0;
  uint32_t m_guestHeight = 0;
  uint32_t m_pixelWidth = 0;
  uint32_t m_pixelHeight = 0;
  bool m_quitRequested = false;
  bool m_fullscreen = false;

  // StandaloneSDL2 state
  SDL_Window* m_sdlWindow = nullptr;
  uint32_t m_sdlWindowId = 0;
  uint32_t m_postAttachForcePolls = 0;
  std::chrono::steady_clock::time_point m_lastPollTime{};
  std::string m_sdlVideoDriver;

  // X11Child state
  void* m_x11Display = nullptr;
  unsigned long m_x11ParentWindow = 0;
  unsigned long m_x11ChildWindow = 0;
  unsigned long m_x11Colormap = 0;
  bool m_x11ParentDestroyed = false;

  // WaylandSubsurface state (internal details live in NativeSurfaceHost.cpp)
  void* m_wlDisplay = nullptr;
  void* m_wlChildSurface = nullptr;
  void* m_wlShm = nullptr;
  int32_t m_wlBufferScale = 1;
  void* m_wlState = nullptr;

  // Win32 native surface state (opaque handles; zero <windows.h> in header)
  void* m_win32Hinstance = nullptr;
  void* m_win32ParentHwnd = nullptr;
  void* m_win32Hwnd = nullptr;
  bool m_win32OwnsHwnd = false;
  bool m_win32FullscreenSaved = false;
  bool m_win32FullscreenApplied = false;
  bool m_inWin32FullscreenAdjust = false;
  bool m_inWin32ForegroundRaise = false;
  bool m_win32SurfaceInvalidated = false;
  bool m_win32SubclassInstalled = false;
  std::atomic<bool> m_win32PassthruSuspended{false};
  std::atomic<bool> m_win32PendingForeground{false};
  std::atomic<bool> m_win32PendingInitialFocus{false};
  std::atomic<bool> m_win32ResetFullscreenTracking{false};
  intptr_t m_win32LastStyle = 0;
  intptr_t m_win32LastExStyle = 0;
  intptr_t m_win32LastWndProc = 0;
  int m_win32LastPixelFormat = 0;
  intptr_t m_win32SavedStyle = 0;
  intptr_t m_win32SavedExStyle = 0;
  int32_t m_win32SavedRect[4] = {0, 0, 0, 0};
  int32_t m_win32AppliedMonRect[4] = {0, 0, 0, 0};
  int32_t m_win32SettledRect[4] = {0, 0, 0, 0};
};

}  // namespace GlideWrapper
