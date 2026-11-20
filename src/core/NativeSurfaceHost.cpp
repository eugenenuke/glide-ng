#include "core/NativeSurfaceHost.h"

#if !defined(_WIN32)
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_version.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/Logger.h"

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
// Scrub infamous Xlib macros so they never collide with C++ identifiers.
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
#include <wayland-client.h>
#endif

namespace GlideWrapper {

namespace {

#if !defined(_WIN32)
// Keep SDL_INIT_VIDEO alive across StandaloneSDL2 open/close cycles (e.g.,
// test27.exe) so Wayland/libdecor-gtk does not repeatedly tear down and
// re-initialize GTK on every grSstWinClose()/grSstWinOpen(), while still
// shutting down cleanly on process exit or library unload.
struct SdlVideoLifetimeGuard {
  bool ownedByUs = false;
  ~SdlVideoLifetimeGuard() {
    if (ownedByUs && SDL_WasInit(SDL_INIT_VIDEO) != 0) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
  }
};
SdlVideoLifetimeGuard g_sdlVideoGuard;
#else
HINSTANCE GetWin32ModuleInstance() {
  static int s_anchor = 0;
  HMODULE mod = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(&s_anchor), &mod) &&
      mod != nullptr) {
    return static_cast<HINSTANCE>(mod);
  }
  return GetModuleHandleA(nullptr);
}

LRESULT CALLBACK GlideWin32StandaloneWndProc(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
  auto* quitFlag =
      reinterpret_cast<bool*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_CLOSE:
      if (quitFlag) *quitFlag = true;
      return 0;
    case WM_DESTROY:
      if (quitFlag) *quitFlag = true;
      return 0;
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcA(hwnd, msg, wParam, lParam);
  }
}

const char* kWin32StandaloneClass = "GlideNGStandaloneWnd";

void EnsureWin32WindowClassesRegistered(HINSTANCE hInst) {
  static std::mutex s_regMutex;
  static bool s_registered = false;
  std::lock_guard<std::mutex> lock(s_regMutex);
  if (s_registered) return;

  WNDCLASSA wc{};
  wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = GlideWin32StandaloneWndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc.lpszClassName = kWin32StandaloneClass;
  RegisterClassA(&wc);

  s_registered = true;
}

bool IsUsableWin32HostWindow(HWND hwnd, bool requireVisible = true) {
  if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) {
    return false;
  }
  if (requireVisible && !IsWindowVisible(hwnd)) {
    return false;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId()) {
    return false;
  }
  const LONG style = GetWindowLongA(hwnd, GWL_STYLE);
  if ((style & WS_CHILD) != 0) {
    return false;
  }
  char cls[128] = {};
  if (GetClassNameA(hwnd, cls, static_cast<int>(sizeof(cls))) > 0) {
    if (std::strcmp(cls, kWin32StandaloneClass) == 0 ||
        std::strcmp(cls, "ConsoleWindowClass") == 0 ||
        std::strcmp(cls, "PseudoConsoleWindow") == 0 ||
        std::strcmp(cls, "IME") == 0 ||
        std::strcmp(cls, "Default IME") == 0 ||
        std::strcmp(cls, "MSCTFIME UI") == 0 ||
        std::strcmp(cls, "tooltips_class32") == 0 ||
        std::strcmp(cls, "OleMainThreadWndClass") == 0 ||
        std::strcmp(cls, "DDRAW_WIN") == 0) {
      return false;
    }
  }
  RECT rc{};
  if (GetClientRect(hwnd, &rc) && (rc.right - rc.left) >= 64 &&
      (rc.bottom - rc.top) >= 64) {
    return true;
  }
  // Pre-ShowWindow top-level windows (e.g. Glide 2.11 titles calling grSstOpen
  // before ShowWindow) may have a non-empty window rect before client layout.
  if (!requireVisible && GetWindowRect(hwnd, &rc) &&
      (rc.right - rc.left) >= 64 && (rc.bottom - rc.top) >= 64) {
    return true;
  }
  return false;
}

struct Win32HostSearchCtx {
  HWND found = nullptr;
  bool requireVisible = true;
};

BOOL CALLBACK EnumProcessGlideHostProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<Win32HostSearchCtx*>(lParam);
  if (IsUsableWin32HostWindow(hwnd, ctx->requireVisible)) {
    ctx->found = hwnd;
    return FALSE;
  }
  return TRUE;
}

// Authentic 3dfx Win32 behavior: fullscreen Glide games (e.g. Donut, Unreal,
// Tomb Raider, NFS, CyberGladiators) create a top-level window and pass hWnd=0
// (or call grSstOpen() with no hWnd parameter), expecting Glide to attach to
// the app's main window instead of spawning a second overlapping window.
HWND FindWin32ProcessHostHwnd() {
  HWND active = GetActiveWindow();
  if (IsUsableWin32HostWindow(active, true)) {
    return active;
  }
  HWND fg = GetForegroundWindow();
  if (IsUsableWin32HostWindow(fg, true)) {
    return fg;
  }
  // Pass 1: prefer already-visible top-level windows of this process.
  Win32HostSearchCtx visCtx{nullptr, true};
  EnumThreadWindows(GetCurrentThreadId(), EnumProcessGlideHostProc,
                    reinterpret_cast<LPARAM>(&visCtx));
  if (visCtx.found) {
    return visCtx.found;
  }
  EnumWindows(EnumProcessGlideHostProc, reinterpret_cast<LPARAM>(&visCtx));
  if (visCtx.found) {
    return visCtx.found;
  }
  // Pass 2: accept pre-ShowWindow top-level windows (e.g. Glide 2.11 titles
  // that call grSstOpen() immediately before ShowWindow(hWndMain, nCmdShow)).
  Win32HostSearchCtx hiddenCtx{nullptr, false};
  EnumThreadWindows(GetCurrentThreadId(), EnumProcessGlideHostProc,
                    reinterpret_cast<LPARAM>(&hiddenCtx));
  if (hiddenCtx.found) {
    return hiddenCtx.found;
  }
  EnumWindows(EnumProcessGlideHostProc, reinterpret_cast<LPARAM>(&hiddenCtx));
  return hiddenCtx.found;
}

bool GetWin32MonitorRect(HWND hwnd, RECT& outRc) {
  HMONITOR mon =
      hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
           : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (mon && GetMonitorInfoA(mon, &mi) &&
      mi.rcMonitor.right > mi.rcMonitor.left &&
      mi.rcMonitor.bottom > mi.rcMonitor.top) {
    outRc = mi.rcMonitor;
    return true;
  }
  const int sw = GetSystemMetrics(SM_CXSCREEN);
  const int sh = GetSystemMetrics(SM_CYSCREEN);
  if (sw > 0 && sh > 0) {
    outRc = RECT{0, 0, sw, sh};
    return true;
  }
  return false;
}
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
std::mutex g_x11ErrorTrapMutex;
bool g_x11ErrorOccurred = false;
int g_x11LastErrorCode = 0;

int HandleTrappedX11Error(Display* /*dpy*/, XErrorEvent* ev) {
  g_x11ErrorOccurred = true;
  g_x11LastErrorCode = ev ? static_cast<int>(ev->error_code) : -1;
  return 0;
}

class ScopedX11ErrorTrap {
 public:
  ScopedX11ErrorTrap() : m_lock(g_x11ErrorTrapMutex) {
    g_x11ErrorOccurred = false;
    g_x11LastErrorCode = 0;
    m_prevHandler = XSetErrorHandler(&HandleTrappedX11Error);
  }

  ~ScopedX11ErrorTrap() { XSetErrorHandler(m_prevHandler); }

  bool ErrorOccurred() const { return g_x11ErrorOccurred; }
  int LastErrorCode() const { return g_x11LastErrorCode; }

 private:
  std::unique_lock<std::mutex> m_lock;
  XErrorHandler m_prevHandler = nullptr;
};
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
// Minimal self-contained client bindings for the stable Wayland `viewporter`
// protocol (wp_viewporter v1 / wp_viewport v1, Collabora MIT license) so we
// handle arbitrary HiDPI integer & fractional scaling (125%, 150%, 175%, 200%)
// without requiring `wayland-protocols` at build time.
struct wp_viewporter;
struct wp_viewport;

extern const wl_interface g_wpViewportInterface;

const wl_interface* g_wpViewporterGetViewportTypes[] = {
    &g_wpViewportInterface,
    &wl_surface_interface,
};

const wl_interface* g_wpViewportNullTypes[] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const wl_message g_wpViewporterRequests[] = {
    {"destroy", "", g_wpViewportNullTypes},
    {"get_viewport", "no", g_wpViewporterGetViewportTypes},
};

const wl_interface g_wpViewporterInterface = {
    "wp_viewporter", 1, 2, g_wpViewporterRequests, 0, nullptr,
};

const wl_message g_wpViewportRequests[] = {
    {"destroy", "", g_wpViewportNullTypes},
    {"set_source", "ffff", g_wpViewportNullTypes},
    {"set_destination", "ii", g_wpViewportNullTypes},
};

const wl_interface g_wpViewportInterface = {
    "wp_viewport", 1, 3, g_wpViewportRequests, 0, nullptr,
};

constexpr uint32_t kWpViewporterDestroy = 0;
constexpr uint32_t kWpViewporterGetViewport = 1;
constexpr uint32_t kWpViewportDestroy = 0;
constexpr uint32_t kWpViewportSetDestination = 2;

void WpViewporterDestroy(wp_viewporter* viewporter) {
  auto* proxy = reinterpret_cast<wl_proxy*>(viewporter);
  wl_proxy_marshal_flags(proxy, kWpViewporterDestroy, nullptr,
                         wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

wp_viewport* WpViewporterGetViewport(wp_viewporter* viewporter,
                                     wl_surface* surface) {
  auto* proxy = reinterpret_cast<wl_proxy*>(viewporter);
  wl_proxy* id = wl_proxy_marshal_flags(
      proxy, kWpViewporterGetViewport, &g_wpViewportInterface,
      wl_proxy_get_version(proxy), 0, nullptr, surface);
  return reinterpret_cast<wp_viewport*>(id);
}

void WpViewportDestroy(wp_viewport* viewport) {
  auto* proxy = reinterpret_cast<wl_proxy*>(viewport);
  wl_proxy_marshal_flags(proxy, kWpViewportDestroy, nullptr,
                         wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

void WpViewportSetDestination(wp_viewport* viewport, int32_t width,
                              int32_t height) {
  auto* proxy = reinterpret_cast<wl_proxy*>(viewport);
  wl_proxy_marshal_flags(proxy, kWpViewportSetDestination, nullptr,
                         wl_proxy_get_version(proxy), 0, width, height);
}

// Minimal self-contained client bindings for the Wayland `fractional-scale-v1`
// protocol (wp_fractional_scale_manager_v1 / wp_fractional_scale_v1, MIT) so
// we query the exact compositor preferred_scale (numerator / 120) directly on
// our child subsurface without requiring `wayland-protocols` at build time.
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;

extern const wl_interface g_wpFractionalScaleV1Interface;

const wl_interface* g_wpFractionalScaleGetTypes[] = {
    &g_wpFractionalScaleV1Interface,
    &wl_surface_interface,
};

const wl_message g_wpFractionalScaleManagerRequests[] = {
    {"destroy", "", g_wpViewportNullTypes},
    {"get_fractional_scale", "no", g_wpFractionalScaleGetTypes},
};

const wl_interface g_wpFractionalScaleManagerV1Interface = {
    "wp_fractional_scale_manager_v1", 1, 2, g_wpFractionalScaleManagerRequests,
    0, nullptr,
};

const wl_message g_wpFractionalScaleRequests[] = {
    {"destroy", "", g_wpViewportNullTypes},
};

const wl_message g_wpFractionalScaleEvents[] = {
    {"preferred_scale", "u", g_wpViewportNullTypes},
};

const wl_interface g_wpFractionalScaleV1Interface = {
    "wp_fractional_scale_v1", 1, 1, g_wpFractionalScaleRequests,
    1, g_wpFractionalScaleEvents,
};

struct WpFractionalScaleV1Listener {
  void (*preferred_scale)(void* data, wp_fractional_scale_v1* obj,
                          uint32_t scale);
};

constexpr uint32_t kWpFractionalScaleManagerDestroy = 0;
constexpr uint32_t kWpFractionalScaleManagerGetFractionalScale = 1;
constexpr uint32_t kWpFractionalScaleDestroy = 0;

void WpFractionalScaleManagerDestroy(wp_fractional_scale_manager_v1* mgr) {
  auto* proxy = reinterpret_cast<wl_proxy*>(mgr);
  wl_proxy_marshal_flags(proxy, kWpFractionalScaleManagerDestroy, nullptr,
                         wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

wp_fractional_scale_v1* WpFractionalScaleManagerGetFractionalScale(
    wp_fractional_scale_manager_v1* mgr, wl_surface* surface) {
  auto* proxy = reinterpret_cast<wl_proxy*>(mgr);
  wl_proxy* id = wl_proxy_marshal_flags(
      proxy, kWpFractionalScaleManagerGetFractionalScale,
      &g_wpFractionalScaleV1Interface, wl_proxy_get_version(proxy), 0, nullptr,
      surface);
  return reinterpret_cast<wp_fractional_scale_v1*>(id);
}

void WpFractionalScaleDestroy(wp_fractional_scale_v1* fs) {
  auto* proxy = reinterpret_cast<wl_proxy*>(fs);
  wl_proxy_marshal_flags(proxy, kWpFractionalScaleDestroy, nullptr,
                         wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

struct WaylandSubsurfaceState {
  wl_display* hostDisplay = nullptr;  // Borrowed from host; NEVER disconnect!
  wl_surface* hostSurface = nullptr;  // Borrowed from host; NEVER destroy!
  SDL_Window* hostSdlWindow = nullptr;
  Uint32 hostSdlWindowId = 0;

  wl_event_queue* queue = nullptr;
  wl_display* displayWrapper = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  uint32_t compositorVersion = 0;
  wl_subcompositor* subcompositor = nullptr;
  wl_shm* shm = nullptr;
  wp_viewporter* viewporter = nullptr;
  wp_viewport* viewport = nullptr;
  wp_fractional_scale_manager_v1* fractionalScaleManager = nullptr;
  wp_fractional_scale_v1* fractionalScale = nullptr;
  wl_surface* childSurface = nullptr;
  wl_subsurface* subsurface = nullptr;
  uint32_t preferredScale120 = 120;  // Wayland 120ths (120 = 1.0x, 160 = 4/3x)
  uint32_t logicalWidth = 0;
  uint32_t logicalHeight = 0;
};

void WaylandPreferredScale(void* data, wp_fractional_scale_v1* /*obj*/,
                           uint32_t scale) {
  auto* state = static_cast<WaylandSubsurfaceState*>(data);
  if (state && scale > 0) {
    state->preferredScale120 = scale;
  }
}

const WpFractionalScaleV1Listener g_wpFractionalScaleListener = {
    WaylandPreferredScale,
};

void WaylandRegistryGlobal(void* data, wl_registry* registry, uint32_t name,
                           const char* interface, uint32_t version) {
  auto* state = static_cast<WaylandSubsurfaceState*>(data);
  if (!state || !interface) {
    return;
  }

  if (std::strcmp(interface, wl_compositor_interface.name) == 0 &&
      !state->compositor) {
    const uint32_t bindVer = std::min<uint32_t>(version, 4);
    state->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, bindVer));
    state->compositorVersion = bindVer;
  } else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0 &&
             !state->subcompositor) {
    state->subcompositor = static_cast<wl_subcompositor*>(
        wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
  } else if (std::strcmp(interface, wl_shm_interface.name) == 0 &&
             !state->shm) {
    state->shm = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, g_wpViewporterInterface.name) == 0 &&
             !state->viewporter) {
    state->viewporter = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &g_wpViewporterInterface, 1));
  } else if (std::strcmp(interface,
                         g_wpFractionalScaleManagerV1Interface.name) == 0 &&
             !state->fractionalScaleManager) {
    state->fractionalScaleManager =
        static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(
            registry, name, &g_wpFractionalScaleManagerV1Interface, 1));
  }
}

void WaylandRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/,
                                 uint32_t /*name*/) {}

const wl_registry_listener g_waylandRegistryListener = {
    WaylandRegistryGlobal,
    WaylandRegistryGlobalRemove,
};

void DestroyWaylandSubsurfaceState(WaylandSubsurfaceState* state,
                                   bool commitParentIfAlive) {
  if (!state) {
    return;
  }

  // Destroy surface-attached extensions BEFORE childSurface.
  if (state->fractionalScale) {
    WpFractionalScaleDestroy(state->fractionalScale);
    state->fractionalScale = nullptr;
  }
  if (state->viewport) {
    WpViewportDestroy(state->viewport);
    state->viewport = nullptr;
  }
  if (state->subsurface) {
    wl_subsurface_destroy(state->subsurface);
    state->subsurface = nullptr;
  }
  if (state->childSurface) {
    wl_surface_destroy(state->childSurface);
    state->childSurface = nullptr;
  }

  if (commitParentIfAlive && state->hostDisplay && state->hostSurface) {
    const bool parentStillAlive =
        (state->hostSdlWindow == nullptr) ||
        (SDL_WasInit(SDL_INIT_VIDEO) != 0 &&
         SDL_GetWindowFromID(state->hostSdlWindowId) == state->hostSdlWindow);
    if (parentStillAlive) {
      wl_surface_commit(state->hostSurface);
      wl_display_flush(state->hostDisplay);
    }
  }

  if (state->fractionalScaleManager) {
    WpFractionalScaleManagerDestroy(state->fractionalScaleManager);
    state->fractionalScaleManager = nullptr;
  }
  if (state->viewporter) {
    WpViewporterDestroy(state->viewporter);
    state->viewporter = nullptr;
  }
  if (state->shm) {
    wl_shm_destroy(state->shm);
    state->shm = nullptr;
  }
  if (state->subcompositor) {
    wl_subcompositor_destroy(state->subcompositor);
    state->subcompositor = nullptr;
  }
  if (state->compositor) {
    wl_compositor_destroy(state->compositor);
    state->compositor = nullptr;
  }
  if (state->registry) {
    wl_registry_destroy(state->registry);
    state->registry = nullptr;
  }
  if (state->displayWrapper) {
    wl_proxy_wrapper_destroy(state->displayWrapper);
    state->displayWrapper = nullptr;
  }
  if (state->queue) {
    wl_event_queue_destroy(state->queue);
    state->queue = nullptr;
  }

  delete state;
}

bool IsWaylandHostScaleToDisplay(SDL_Window* hostWin, uint32_t winW,
                                 uint32_t winH, uint32_t pixW, uint32_t pixH,
                                 uint32_t preferredScale120) {
  // No display scaling active -> logical == physical.
  if (!hostWin || preferredScale120 <= 120) {
    return false;
  }
  // If SDL already separated logical (win) and physical (pix) sizes, winW x
  // winH is already in Wayland logical surface coordinates.
  if (pixW != winW || pixH != winH) {
    return false;
  }

  // Respect explicit SDL hint / environment override if present.
  const char* hint = SDL_GetHint("SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY");
  if (!hint || hint[0] == '\0') {
    hint = std::getenv("SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY");
  }
  if (hint && hint[0] != '\0') {
    return (std::strcmp(hint, "1") == 0 || std::strcmp(hint, "true") == 0 ||
            std::strcmp(hint, "TRUE") == 0);
  }

  // If ALLOW_HIGHDPI is set on a scaled display yet SDL reports win == pix,
  // SDL is operating in 1:1 physical-pixel scale-to-display mode (where SDL
  // sets wp_viewport.set_destination(pix * 120 / preferred_scale)).
  if (hostWin &&
      (SDL_GetWindowFlags(hostWin) & SDL_WINDOW_ALLOW_HIGHDPI) != 0) {
    return true;
  }

  // sdl2-compat (SDL 2.32.50+ backed by SDL3, default on Arch/CachyOS/Fedora)
  // enables scale-to-display by default on Wayland even for non-ALLOW_HIGHDPI
  // apps (including DOSBox-X and patched SVN DOSBox).
  SDL_version ver{};
  SDL_GetVersion(&ver);
  const char* rev = SDL_GetRevision();
  const bool isSdl2Compat =
      (ver.major == 2 &&
       (ver.minor > 32 || (ver.minor == 32 && ver.patch >= 50))) ||
      (rev != nullptr && (std::strstr(rev, "sdl2-compat") != nullptr ||
                          std::strstr(rev, "release-3.") != nullptr ||
                          std::strstr(rev, "SDL3") != nullptr));
  return isSdl2Compat;
}

void QueryWaylandHostSizes(SDL_Window* hostWin, uint32_t fallbackW,
                           uint32_t fallbackH, uint32_t preferredScale120,
                           uint32_t& outLogicalW, uint32_t& outLogicalH,
                           uint32_t& outPixelW, uint32_t& outPixelH,
                           int32_t& outScale, bool& outScaleToDisplay) {
  int winW = 0;
  int winH = 0;
  int pixW = 0;
  int pixH = 0;

  if (hostWin) {
    SDL_GetWindowSize(hostWin, &winW, &winH);
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(hostWin, &pixW, &pixH);
#else
    pixW = winW;
    pixH = winH;
#endif
  }

  if (winW <= 0 || winH <= 0) {
    winW = (pixW > 0) ? pixW : static_cast<int>(fallbackW);
    winH = (pixH > 0) ? pixH : static_cast<int>(fallbackH);
  }
  if (pixW <= 0 || pixH <= 0) {
    pixW = winW;
    pixH = winH;
  }

  const uint32_t clampedWinW = static_cast<uint32_t>(std::max(1, winW));
  const uint32_t clampedWinH = static_cast<uint32_t>(std::max(1, winH));
  outPixelW = static_cast<uint32_t>(std::max(1, pixW));
  outPixelH = static_cast<uint32_t>(std::max(1, pixH));

  outScaleToDisplay = IsWaylandHostScaleToDisplay(
      hostWin, clampedWinW, clampedWinH, outPixelW, outPixelH,
      preferredScale120);

  if (outScaleToDisplay && preferredScale120 > 0) {
    // Match SDL3's scale-to-display wp_viewport destination size:
    // logical = round(pixels * 120 / preferred_scale).
    outLogicalW = std::max<uint32_t>(
        1, static_cast<uint32_t>(
               (static_cast<uint64_t>(outPixelW) * 120ULL +
                preferredScale120 / 2ULL) /
               preferredScale120));
    outLogicalH = std::max<uint32_t>(
        1, static_cast<uint32_t>(
               (static_cast<uint64_t>(outPixelH) * 120ULL +
                preferredScale120 / 2ULL) /
               preferredScale120));
  } else {
    outLogicalW = clampedWinW;
    outLogicalH = clampedWinH;
  }

  if (outLogicalW > 0 && outPixelW >= outLogicalW) {
    outScale = std::max<int32_t>(
        1, static_cast<int32_t>((outPixelW + outLogicalW / 2) / outLogicalW));
  } else {
    outScale = 1;
  }
}
#endif

}  // namespace

NativeSurfaceHost::~NativeSurfaceHost() { Detach(); }

const char* NativeSurfaceHost::ModeToString(Mode mode) {
  switch (mode) {
    case Mode::Detached:
      return "Detached";
    case Mode::Headless:
      return "Headless";
    case Mode::StandaloneSDL2:
      return "StandaloneSDL2";
    case Mode::X11Child:
      return "X11Child";
    case Mode::WaylandSubsurface:
      return "WaylandSubsurface";
    case Mode::Win32Window:
      return "Win32Window";
    case Mode::Win32Embedded:
      return "Win32Embedded";
  }
  return "Unknown";
}

PresentRect NativeSurfaceHost::ComputePresentRect(uint32_t guestW,
                                                  uint32_t guestH,
                                                  uint32_t hostW,
                                                  uint32_t hostH,
                                                  bool preserveAspect) {
  if (hostW == 0 || hostH == 0) {
    return PresentRect{0, 0, 0, 0};
  }
  if (!preserveAspect || guestW == 0 || guestH == 0) {
    return PresentRect{0, 0, hostW, hostH};
  }

  const uint64_t hostAspectTimesGuestH =
      static_cast<uint64_t>(hostW) * static_cast<uint64_t>(guestH);
  const uint64_t guestAspectTimesHostH =
      static_cast<uint64_t>(guestW) * static_cast<uint64_t>(hostH);

  if (hostAspectTimesGuestH > guestAspectTimesHostH) {
    // Host is wider than guest -> pillarbox (black bars left & right).
    uint64_t scaledW =
        (static_cast<uint64_t>(hostH) * static_cast<uint64_t>(guestW) +
         guestH / 2) /
        guestH;
    scaledW = std::clamp<uint64_t>(scaledW, 1, hostW);
    const uint32_t rectW = static_cast<uint32_t>(scaledW);
    const int32_t offsetX = static_cast<int32_t>((hostW - rectW) / 2);
    return PresentRect{offsetX, 0, rectW, hostH};
  }

  if (hostAspectTimesGuestH < guestAspectTimesHostH) {
    // Host is taller than guest -> letterbox (black bars top & bottom).
    uint64_t scaledH =
        (static_cast<uint64_t>(hostW) * static_cast<uint64_t>(guestH) +
         guestW / 2) /
        guestW;
    scaledH = std::clamp<uint64_t>(scaledH, 1, hostH);
    const uint32_t rectH = static_cast<uint32_t>(scaledH);
    const int32_t offsetY = static_cast<int32_t>((hostH - rectH) / 2);
    return PresentRect{0, offsetY, hostW, rectH};
  }

  // Exact aspect-ratio match.
  return PresentRect{0, 0, hostW, hostH};
}

bool NativeSurfaceHost::Attach(void* hostHandle, uint32_t guestW,
                               uint32_t guestH, bool /*windowed*/,
                               bool forceNoWindow, const std::string& title,
                               uint32_t extraSdlWindowFlags,
                               uint32_t windowScale, bool fullscreen) {
  if (IsAttached()) {
    Detach();
  }

  m_guestWidth = (guestW > 0) ? guestW : 640;
  m_guestHeight = (guestH > 0) ? guestH : 480;
  m_quitRequested = false;
  m_fullscreen = fullscreen;

  if (forceNoWindow) {
    m_mode = Mode::Headless;
    m_pixelWidth = m_guestWidth;
    m_pixelHeight = m_guestHeight;
    GLIDE_LOG(INFO, "WSI",
              "NativeSurfaceHost attached in Headless mode ("
                  << m_guestWidth << "x" << m_guestHeight << ").");
    return true;
  }

  if (hostHandle == nullptr) {
#if defined(_WIN32)
    (void)extraSdlWindowFlags;
    if (HWND appHwnd = FindWin32ProcessHostHwnd()) {
      GLIDE_LOG(INFO, "WSI",
                "NativeSurfaceHost: hWnd=0 resolved to active process HWND "
                    << static_cast<void*>(appHwnd)
                    << " (authentic 3dfx Win32 attachment).");
      if (AttachWin32Embedded(static_cast<void*>(appHwnd))) {
        return true;
      }
    }
    return AttachWin32Standalone(m_guestWidth, m_guestHeight, title,
                                 windowScale);
#else
    return AttachStandaloneSDL2(m_guestWidth, m_guestHeight, title,
                                extraSdlWindowFlags, windowScale);
#endif
  }

#if defined(_WIN32)
  (void)extraSdlWindowFlags;
  if (AttachWin32Embedded(hostHandle)) {
    return true;
  }
  GLIDE_LOG(WARN, "WSI",
            "NativeSurfaceHost: foreign Win32 handle "
                << hostHandle
                << " is not a valid HWND; falling back to standalone Win32 window.");
  return AttachWin32Standalone(m_guestWidth, m_guestHeight, title, windowScale);
#endif

#if defined(__unix__) && !defined(__APPLE__)
  // Safely probe live SDL2 windows without blindly dereferencing hostHandle
  // (which may be a numeric X11 Window XID).
  if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    const bool isWaylandDriver =
        (videoDriver != nullptr && std::strcmp(videoDriver, "wayland") == 0);

    for (Uint32 id = 1; id <= 64; ++id) {
      SDL_Window* candidate = SDL_GetWindowFromID(id);
      if (!candidate) {
        continue;
      }

      SDL_SysWMinfo wmi{};
      SDL_VERSION(&wmi.version);
      if (!SDL_GetWindowWMInfo(candidate, &wmi)) {
        continue;
      }

      if (static_cast<void*>(candidate) == hostHandle) {
        if (wmi.subsystem == SDL_SYSWM_X11) {
          GLIDE_LOG(INFO, "WSI",
                    "Resolved host SDL_Window* "
                        << hostHandle << " -> X11 Window 0x" << std::hex
                        << static_cast<unsigned long>(wmi.info.x11.window)
                        << std::dec);
          return AttachX11Child(
              static_cast<unsigned long>(wmi.info.x11.window));
        }
        if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
          GLIDE_LOG(INFO, "WSI",
                    "Resolved host SDL_Window* "
                        << hostHandle << " -> Wayland surface "
                        << wmi.info.wl.surface);
          return AttachWaylandSubsurface(wmi.info.wl.display,
                                         wmi.info.wl.surface, candidate);
        }
      } else if (isWaylandDriver && wmi.subsystem == SDL_SYSWM_WAYLAND &&
                 static_cast<void*>(wmi.info.wl.surface) == hostHandle) {
        // DOSBox-X statWMInfo() union alias on native Wayland:
        // wmi.info.x11.window aliases wmi.info.wl.surface!
        GLIDE_LOG(INFO, "WSI",
                  "Resolved DOSBox-X Wayland wl_surface* alias "
                      << hostHandle << " -> SDL_Window* " << candidate);
        return AttachWaylandSubsurface(wmi.info.wl.display,
                                       wmi.info.wl.surface, candidate);
      }
    }
  }

  // Raw X11 Window ID path (DOSBox-X on X11/XWayland, SVN DOSBox without
  // setConfig('SDL2'), or direct Xlib hosts). X11 XIDs are 32-bit integers.
  const uintptr_t rawVal = reinterpret_cast<uintptr_t>(hostHandle);
  if (rawVal != 0 && rawVal <= 0xFFFFFFFFUL) {
    if (AttachX11Child(static_cast<unsigned long>(rawVal))) {
      return true;
    }
  }
#endif

  GLIDE_LOG(WARN, "WSI",
            "NativeSurfaceHost: failed to attach foreign hostHandle="
                << hostHandle);
  m_guestWidth = 0;
  m_guestHeight = 0;
  return false;
}

bool NativeSurfaceHost::AttachStandaloneSDL2(uint32_t guestW, uint32_t guestH,
                                             const std::string& title,
                                             uint32_t extraSdlWindowFlags,
                                             uint32_t windowScale) {
#if !defined(_WIN32)
  // Never let SDL hijack SIGINT/SIGTERM in standalone demos: Ctrl+C and
  // SIGTERM should terminate immediately instead of requiring kill -9.
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

  if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
      GLIDE_LOG(CRITICAL, "WSI",
                "NativeSurfaceHost: SDL_InitSubSystem(SDL_INIT_VIDEO) failed: "
                    << SDL_GetError());
      return false;
    }
    g_sdlVideoGuard.ownedByUs = true;
  }

  const uint32_t clampedScale = std::clamp<uint32_t>(windowScale, 1, 8);
  const int winW = static_cast<int>(guestW * clampedScale);
  const int winH = static_cast<int>(guestH * clampedScale);
  uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                   SDL_WINDOW_ALLOW_HIGHDPI | extraSdlWindowFlags;
  if (m_fullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  }

  const char* windowTitle =
      title.empty() ? "3dfx Glide Wrapper (Standalone)" : title.c_str();
  m_sdlWindow = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, winW, winH, flags);
  if (!m_sdlWindow) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: SDL_CreateWindow failed: " << SDL_GetError());
    return false;
  }

  m_sdlWindowId = SDL_GetWindowID(m_sdlWindow);
  const char* drv = SDL_GetCurrentVideoDriver();
  m_sdlVideoDriver = drv ? std::string(drv) : std::string();
  m_mode = Mode::StandaloneSDL2;
  m_lastPollTime = {};
  m_postAttachForcePolls = 4;

  // Immediately pump initial Wayland/libdecor/X11 configure events so the
  // window is mapped and responsive even before the first SwapBuffers().
  PollEvents(/*force=*/true);

  QueryStandaloneDrawableSize(m_pixelWidth, m_pixelHeight);

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in StandaloneSDL2 mode (driver="
                << (m_sdlVideoDriver.empty() ? "unknown" : m_sdlVideoDriver)
                << ", guest=" << m_guestWidth << "x" << m_guestHeight
                << ", drawable=" << m_pixelWidth << "x" << m_pixelHeight
                << ").");
  return true;
#else
  (void)guestW;
  (void)guestH;
  (void)title;
  (void)extraSdlWindowFlags;
  (void)windowScale;
  return false;
#endif
}

bool NativeSurfaceHost::AttachWin32Standalone(uint32_t guestW, uint32_t guestH,
                                              const std::string& title,
                                              uint32_t windowScale) {
#if defined(_WIN32)
  HINSTANCE hInst = GetWin32ModuleInstance();
  EnsureWin32WindowClassesRegistered(hInst);

  const uint32_t clampedScale = std::clamp<uint32_t>(windowScale, 1u, 8u);
  const int clientW = static_cast<int>(guestW * clampedScale);
  const int clientH = static_cast<int>(guestH * clampedScale);

  DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
  int winW = clientW;
  int winH = clientH;
  int posX = CW_USEDEFAULT;
  int posY = CW_USEDEFAULT;

  RECT monRc{};
  if (m_fullscreen && GetWin32MonitorRect(nullptr, monRc)) {
    style = WS_POPUP | WS_VISIBLE;
    posX = monRc.left;
    posY = monRc.top;
    winW = monRc.right - monRc.left;
    winH = monRc.bottom - monRc.top;
  } else {
    RECT rc{0, 0, clientW, clientH};
    AdjustWindowRect(&rc, style, FALSE);
    winW = rc.right - rc.left;
    winH = rc.bottom - rc.top;

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    posX = (screenW > winW) ? (screenW - winW) / 2 : CW_USEDEFAULT;
    posY = (screenH > winH) ? (screenH - winH) / 2 : CW_USEDEFAULT;
  }

  const char* windowTitle =
      title.empty() ? "3dfx glide-ng Presentation Console" : title.c_str();
  HWND hwnd = CreateWindowExA(0, kWin32StandaloneClass, windowTitle, style,
                              posX, posY, winW, winH, nullptr, nullptr, hInst,
                              nullptr);
  if (!hwnd) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: CreateWindowExA failed (err="
                  << GetLastError() << ").");
    return false;
  }

  SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(&m_quitRequested));
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  RECT actualRc{};
  GetClientRect(hwnd, &actualRc);
  m_win32Hinstance = static_cast<void*>(hInst);
  m_win32ParentHwnd = nullptr;
  m_win32Hwnd = static_cast<void*>(hwnd);
  m_win32OwnsHwnd = true;
  m_mode = Mode::Win32Window;
  m_pixelWidth = (actualRc.right > actualRc.left)
                     ? static_cast<uint32_t>(actualRc.right - actualRc.left)
                     : static_cast<uint32_t>(clientW);
  m_pixelHeight = (actualRc.bottom > actualRc.top)
                      ? static_cast<uint32_t>(actualRc.bottom - actualRc.top)
                      : static_cast<uint32_t>(clientH);
  m_lastPollTime = {};
  m_postAttachForcePolls = 4;

  PollEvents(/*force=*/true);

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in Win32Window mode (hwnd="
                << m_win32Hwnd << ", guest=" << m_guestWidth << "x"
                << m_guestHeight << ", drawable=" << m_pixelWidth << "x"
                << m_pixelHeight << ").");
  return true;
#else
  (void)guestW;
  (void)guestH;
  (void)title;
  (void)windowScale;
  return false;
#endif
}

bool NativeSurfaceHost::AttachWin32Embedded(void* parentHwndPtr) {
#if defined(_WIN32)
  HWND parent = static_cast<HWND>(parentHwndPtr);
  if (!parent || !IsWindow(parent)) {
    return false;
  }

  HINSTANCE hInst = GetWin32ModuleInstance();
  EnsureWin32WindowClassesRegistered(hInst);

  // Attach directly to the host HWND (matching authentic Win32 3dfx Glide).
  // Creating a WS_CHILD overlay inside a top-level HWND forces Wine's
  // winex11.drv Vulkan/WGL onto a child-window blit fallback that gets wiped
  // to black whenever the parent flushes its GDI window_surface (e.g. while an
  // app pauses at a "Press any key" screen without calling grBufferSwap).
  m_win32Hinstance = static_cast<void*>(hInst);
  m_win32ParentHwnd = parentHwndPtr;
  m_win32Hwnd = parentHwndPtr;
  m_win32OwnsHwnd = false;
  m_mode = Mode::Win32Embedded;

  if (m_fullscreen) {
    ApplyWin32EmbeddedFullscreenIfNeeded();
  }
  RefreshWin32SurfaceFingerprint();
  m_win32SurfaceInvalidated = false;
  m_win32PendingForeground.store(false, std::memory_order_relaxed);
  m_win32ResetFullscreenTracking.store(false, std::memory_order_relaxed);

  RECT rc{};
  GetClientRect(parent, &rc);
  const int w = (rc.right > rc.left) ? (rc.right - rc.left)
                                     : static_cast<int>(m_guestWidth);
  const int h = (rc.bottom > rc.top) ? (rc.bottom - rc.top)
                                     : static_cast<int>(m_guestHeight);

  m_pixelWidth = static_cast<uint32_t>(std::max(1, w));
  m_pixelHeight = static_cast<uint32_t>(std::max(1, h));

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in Win32Embedded mode (parent="
                << m_win32ParentHwnd << ", surface=" << m_win32Hwnd
                << ", ownedChild=" << (m_win32OwnsHwnd ? "yes" : "no")
                << ", fullscreen=" << (m_fullscreen ? "yes" : "no")
                << ", drawable=" << m_pixelWidth << "x" << m_pixelHeight
                << ").");
  return true;
#else
  (void)parentHwndPtr;
  return false;
#endif
}

bool NativeSurfaceHost::ApplyWin32EmbeddedFullscreenIfNeeded() {
#if defined(_WIN32)
  if (!m_fullscreen || m_mode != Mode::Win32Embedded || !m_win32ParentHwnd ||
      m_inWin32FullscreenAdjust) {
    return false;
  }
  HWND hwnd = static_cast<HWND>(m_win32ParentHwnd);
  if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
    // Defer fullscreen resizing until the host application actually shows its
    // window (avoids flashing a premature black fullscreen canvas when titles
    // call grSstOpen() before ShowWindow() or before an intro AVI).
    return false;
  }

  RECT monRc{};
  if (!GetWin32MonitorRect(hwnd, monRc)) {
    return false;
  }

  const LONG curStyle = GetWindowLongA(hwnd, GWL_STYLE);
  const LONG curExStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
  RECT curRc{};
  GetWindowRect(hwnd, &curRc);

  if (!m_win32FullscreenSaved) {
    m_win32SavedStyle = static_cast<intptr_t>(curStyle);
    m_win32SavedExStyle = static_cast<intptr_t>(curExStyle);
    m_win32SavedRect[0] = static_cast<int32_t>(curRc.left);
    m_win32SavedRect[1] = static_cast<int32_t>(curRc.top);
    m_win32SavedRect[2] = static_cast<int32_t>(curRc.right);
    m_win32SavedRect[3] = static_cast<int32_t>(curRc.bottom);
    m_win32FullscreenSaved = true;
  }

  constexpr DWORD kChromeStyleMask =
      WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
      WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
  constexpr DWORD kChromeExStyleMask =
      WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE |
      WS_EX_WINDOWEDGE;

  const DWORD uStyle = static_cast<DWORD>(curStyle);
  const DWORD uExStyle = static_cast<DWORD>(curExStyle);
  const bool needStyleFix =
      ((uStyle & kChromeStyleMask) != 0u) || ((uStyle & WS_POPUP) == 0u) ||
      ((uExStyle & kChromeExStyleMask) != 0u);
  const bool needRectFix =
      (curRc.left != monRc.left || curRc.top != monRc.top ||
       curRc.right != monRc.right || curRc.bottom != monRc.bottom);

  if (!needStyleFix && !needRectFix) {
    return false;
  }

  // If the host window manager clamped our previous fullscreen SetWindowPos
  // (e.g. a top panel or tiling WM) and neither the monitor rect nor the
  // window's settled rect/style has changed since then, accept the clamped
  // geometry instead of spamming SetWindowPos on every SwapBuffers() frame.
  if (!needStyleFix && m_win32FullscreenApplied) {
    const bool sameMon =
        (monRc.left == m_win32AppliedMonRect[0] &&
         monRc.top == m_win32AppliedMonRect[1] &&
         monRc.right == m_win32AppliedMonRect[2] &&
         monRc.bottom == m_win32AppliedMonRect[3]);
    const bool sameSettled =
        (curRc.left == m_win32SettledRect[0] &&
         curRc.top == m_win32SettledRect[1] &&
         curRc.right == m_win32SettledRect[2] &&
         curRc.bottom == m_win32SettledRect[3]);
    if (sameMon && sameSettled) {
      return false;
    }
  }

  struct ReentrancyGuard {
    bool& flag;
    explicit ReentrancyGuard(bool& f) : flag(f) { flag = true; }
    ~ReentrancyGuard() { flag = false; }
  } guard(m_inWin32FullscreenAdjust);

  if (needStyleFix) {
    // Preserve existing WS_VISIBLE state so we do not prematurely show or
    // activate a window before the host application calls ShowWindow().
    const DWORD newStyle = (uStyle & ~kChromeStyleMask) | WS_POPUP;
    const DWORD newExStyle = (uExStyle & ~kChromeExStyleMask);
    SetWindowLongA(hwnd, GWL_STYLE, static_cast<LONG>(newStyle));
    SetWindowLongA(hwnd, GWL_EXSTYLE, static_cast<LONG>(newExStyle));
  }

  const int monW = monRc.right - monRc.left;
  const int monH = monRc.bottom - monRc.top;
  // Pass SWP_NOACTIVATE (and omit SWP_SHOWWINDOW) so SetWindowPos does not
  // synchronously trigger WM_ACTIVATE -> grSstPassthruMode() inside the
  // foreign host WndProc while Glide is attaching or swapping.
  SetWindowPos(hwnd, HWND_TOP, monRc.left, monRc.top, monW, monH,
               SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE);

  RECT settledRc{};
  GetWindowRect(hwnd, &settledRc);
  m_win32AppliedMonRect[0] = static_cast<int32_t>(monRc.left);
  m_win32AppliedMonRect[1] = static_cast<int32_t>(monRc.top);
  m_win32AppliedMonRect[2] = static_cast<int32_t>(monRc.right);
  m_win32AppliedMonRect[3] = static_cast<int32_t>(monRc.bottom);
  m_win32SettledRect[0] = static_cast<int32_t>(settledRc.left);
  m_win32SettledRect[1] = static_cast<int32_t>(settledRc.top);
  m_win32SettledRect[2] = static_cast<int32_t>(settledRc.right);
  m_win32SettledRect[3] = static_cast<int32_t>(settledRc.bottom);
  m_win32FullscreenApplied = true;

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost: enforced borderless fullscreen on Win32 "
            "HWND "
                << m_win32ParentHwnd << " (" << monW << "x" << monH << " at "
                << monRc.left << "," << monRc.top << ").");
  return true;
#else
  return false;
#endif
}

void NativeSurfaceHost::RestoreWin32EmbeddedFullscreenIfSaved() {
#if defined(_WIN32)
  if (!m_win32FullscreenSaved || m_win32OwnsHwnd || !m_win32ParentHwnd ||
      m_inWin32FullscreenAdjust) {
    m_win32FullscreenSaved = false;
    return;
  }
  struct ReentrancyGuard {
    bool& flag;
    explicit ReentrancyGuard(bool& f) : flag(f) { flag = true; }
    ~ReentrancyGuard() { flag = false; }
  } guard(m_inWin32FullscreenAdjust);

  HWND hwnd = static_cast<HWND>(m_win32ParentHwnd);
  if (IsWindow(hwnd)) {
    SetWindowLongA(hwnd, GWL_STYLE, static_cast<LONG>(m_win32SavedStyle));
    SetWindowLongA(hwnd, GWL_EXSTYLE, static_cast<LONG>(m_win32SavedExStyle));
    const int w = m_win32SavedRect[2] - m_win32SavedRect[0];
    const int h = m_win32SavedRect[3] - m_win32SavedRect[1];
    if (w > 0 && h > 0) {
      SetWindowPos(hwnd, HWND_NOTOPMOST, m_win32SavedRect[0],
                   m_win32SavedRect[1], w, h,
                   SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
  }
  m_win32FullscreenSaved = false;
#endif
}

bool NativeSurfaceHost::AttachX11Child(unsigned long parentWindow) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (parentWindow == 0) {
    return false;
  }

  // Open a dedicated private X11 Display connection so we never race with
  // DOSBox's Xlib Display* and never need a late dlopen-time XInitThreads().
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: XOpenDisplay(nullptr) failed (is DISPLAY "
              "set?).");
    return false;
  }

  Window parent = static_cast<Window>(parentWindow);
  Window root = 0;
  int x = 0;
  int y = 0;
  unsigned int parentW = 0;
  unsigned int parentH = 0;
  unsigned int borderW = 0;
  unsigned int depth = 0;
  Window child = 0;

  {
    ScopedX11ErrorTrap trap;

    // Select StructureNotifyMask on the parent via our private X11 connection
    // BEFORE querying initial geometry so no resize event can be missed.
    // (StructureNotifyMask is non-exclusive and isolated per X11 client.)
    XSelectInput(dpy, parent, StructureNotifyMask);
    int geomOk = XGetGeometry(dpy, parent, &root, &x, &y, &parentW, &parentH,
                              &borderW, &depth);
    XSync(dpy, False);

    if (!geomOk || trap.ErrorOccurred() || parentW == 0 || parentH == 0) {
      GLIDE_LOG(WARN, "WSI",
                "NativeSurfaceHost: XGetGeometry failed for X11 parent 0x"
                    << std::hex << parentWindow << std::dec
                    << " (error_code=" << trap.LastErrorCode() << ").");
      XCloseDisplay(dpy);
      return false;
    }

    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    attrs.border_pixel = 0;
    attrs.bit_gravity = NorthWestGravity;
    // Never select ButtonPressMask/KeyPressMask/PointerMotionMask on the child.
    attrs.event_mask = ExposureMask | StructureNotifyMask;

    child = XCreateWindow(
        dpy, parent, 0, 0, parentW, parentH, 0, CopyFromParent, InputOutput,
        CopyFromParent,
        CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask, &attrs);

    int shapeEventBase = 0;
    int shapeErrorBase = 0;
    if (child != 0 &&
        XShapeQueryExtension(dpy, &shapeEventBase, &shapeErrorBase)) {
      // Empty ShapeInput region -> 100% mouse & keyboard pass-through to host.
      XShapeCombineRectangles(dpy, child, ShapeInput, 0, 0, nullptr, 0,
                              ShapeSet, Unsorted);
    } else if (child != 0) {
      GLIDE_LOG(WARN, "WSI",
                "NativeSurfaceHost: X11 SHAPE extension unavailable; child "
                "window may intercept pointer events.");
    }

    if (child != 0) {
      XMapRaised(dpy, child);
    }
    XSync(dpy, False);

    if (trap.ErrorOccurred() || child == 0) {
      GLIDE_LOG(CRITICAL, "WSI",
                "NativeSurfaceHost: failed to create/map X11 child window "
                "under parent 0x"
                    << std::hex << parentWindow << std::dec
                    << " (error_code=" << trap.LastErrorCode() << ").");
      if (child != 0) {
        XDestroyWindow(dpy, child);
        XSync(dpy, False);
      }
      XCloseDisplay(dpy);
      return false;
    }
  }

  m_x11Display = dpy;
  m_x11ParentWindow = parentWindow;
  m_x11ChildWindow = static_cast<unsigned long>(child);
  m_x11ParentDestroyed = false;
  m_pixelWidth = static_cast<uint32_t>(parentW);
  m_pixelHeight = static_cast<uint32_t>(parentH);
  m_mode = Mode::X11Child;

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in X11Child mode (parent=0x"
                << std::hex << m_x11ParentWindow << ", child=0x"
                << m_x11ChildWindow << std::dec << ", size=" << m_pixelWidth
                << "x" << m_pixelHeight << ", input=click-through).");
  return true;
#else
  (void)parentWindow;
  return false;
#endif
}

bool NativeSurfaceHost::RecreateX11ChildForVisual(unsigned long visualId) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (m_mode != Mode::X11Child || !m_x11Display || m_x11ParentWindow == 0 ||
      visualId == 0) {
    return false;
  }

  Display* dpy = static_cast<Display*>(m_x11Display);
  Window parent = static_cast<Window>(m_x11ParentWindow);

  XVisualInfo vtmpl{};
  vtmpl.visualid = static_cast<VisualID>(visualId);
  int numVisuals = 0;
  XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask, &vtmpl, &numVisuals);
  if (!vi || numVisuals <= 0) {
    if (vi) XFree(vi);
    GLIDE_LOG(WARN, "WSI",
              "NativeSurfaceHost: XGetVisualInfo found no visual for id 0x"
                  << std::hex << visualId << std::dec << ".");
    return false;
  }

  Window newChild = 0;
  Colormap newCmap = 0;
  const unsigned int w = std::max<uint32_t>(1, m_pixelWidth);
  const unsigned int h = std::max<uint32_t>(1, m_pixelHeight);

  {
    ScopedX11ErrorTrap trap;

    // A dedicated Colormap + explicit CWBorderPixel/CWBackPixel is mandatory
    // in X11 whenever the child's visual or depth differs from its parent
    // (otherwise XCreateWindow fails with BadMatch).
    newCmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual,
                              AllocNone);
    XSetWindowAttributes attrs{};
    attrs.colormap = newCmap;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.bit_gravity = NorthWestGravity;
    attrs.event_mask = ExposureMask | StructureNotifyMask;

    newChild = XCreateWindow(
        dpy, parent, 0, 0, w, h, 0, vi->depth, InputOutput, vi->visual,
        CWColormap | CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask,
        &attrs);

    int shapeEventBase = 0;
    int shapeErrorBase = 0;
    if (newChild != 0 &&
        XShapeQueryExtension(dpy, &shapeEventBase, &shapeErrorBase)) {
      XShapeCombineRectangles(dpy, newChild, ShapeInput, 0, 0, nullptr, 0,
                              ShapeSet, Unsorted);
    }

    if (newChild != 0) {
      XMapRaised(dpy, newChild);
    }
    XSync(dpy, False);

    if (trap.ErrorOccurred() || newChild == 0) {
      GLIDE_LOG(WARN, "WSI",
                "NativeSurfaceHost: failed to recreate X11Child for visual 0x"
                    << std::hex << visualId << std::dec << " (depth="
                    << vi->depth << ", error_code=" << trap.LastErrorCode()
                    << "); keeping existing child.");
      if (newChild != 0) {
        XDestroyWindow(dpy, newChild);
      }
      if (newCmap != 0) {
        XFreeColormap(dpy, newCmap);
      }
      XSync(dpy, False);
      XFree(vi);
      return false;
    }

    // Swap in the new child window and tear down the previous child/colormap.
    if (m_x11ChildWindow != 0 && !m_x11ParentDestroyed) {
      XDestroyWindow(dpy, static_cast<Window>(m_x11ChildWindow));
    }
    if (m_x11Colormap != 0) {
      XFreeColormap(dpy, static_cast<Colormap>(m_x11Colormap));
    }
    XSync(dpy, False);
  }

  const int depth = vi->depth;
  XFree(vi);

  m_x11ChildWindow = static_cast<unsigned long>(newChild);
  m_x11Colormap = static_cast<unsigned long>(newCmap);

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost recreated X11Child for visual 0x"
                << std::hex << visualId << " (child=0x" << m_x11ChildWindow
                << std::dec << ", depth=" << depth << ", size=" << m_pixelWidth
                << "x" << m_pixelHeight << ", input=click-through).");
  return true;
#else
  (void)visualId;
  return false;
#endif
}

bool NativeSurfaceHost::AttachWaylandSubsurface(void* hostDisplayPtr,
                                                void* hostSurfacePtr,
                                                SDL_Window* hostSdlWindow) {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  if (!hostDisplayPtr || !hostSurfacePtr) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: null Wayland display or host surface.");
    return false;
  }

  auto* state = new WaylandSubsurfaceState();
  state->hostDisplay = static_cast<wl_display*>(hostDisplayPtr);
  state->hostSurface = static_cast<wl_surface*>(hostSurfacePtr);
  state->hostSdlWindow = hostSdlWindow;
  state->hostSdlWindowId = hostSdlWindow ? SDL_GetWindowID(hostSdlWindow) : 0;

  // Create a private Wayland event queue + proxy wrapper so roundtrips and
  // registry events NEVER race with or steal events from SDL/DOSBox's queue.
  state->queue = wl_display_create_queue(state->hostDisplay);
  if (!state->queue) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: wl_display_create_queue failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  state->displayWrapper =
      static_cast<wl_display*>(wl_proxy_create_wrapper(state->hostDisplay));
  if (!state->displayWrapper) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: wl_proxy_create_wrapper failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(state->displayWrapper),
                     state->queue);

  state->registry = wl_display_get_registry(state->displayWrapper);
  if (!state->registry) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: wl_display_get_registry failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }
  wl_registry_add_listener(state->registry, &g_waylandRegistryListener, state);

  if (wl_display_roundtrip_queue(state->hostDisplay, state->queue) < 0) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: Wayland registry roundtrip failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  if (!state->compositor || !state->subcompositor) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: Wayland compositor missing required globals "
              "(compositor="
                  << state->compositor
                  << ", subcompositor=" << state->subcompositor << ").");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  // Create a fresh, roleless wl_surface (NEVER use SDL_CreateWindow here, as
  // xdg_toplevel + wl_subsurface is a fatal Wayland role violation).
  state->childSurface = wl_compositor_create_surface(state->compositor);
  if (!state->childSurface) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: wl_compositor_create_surface failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  // Empty input region -> 100% mouse & keyboard pass-through to host DOSBox.
  wl_region* emptyRegion = wl_compositor_create_region(state->compositor);
  if (emptyRegion) {
    wl_surface_set_input_region(state->childSurface, emptyRegion);
    wl_region_destroy(emptyRegion);
  }

  state->subsurface = wl_subcompositor_get_subsurface(
      state->subcompositor, state->childSurface, state->hostSurface);
  if (!state->subsurface) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: wl_subcompositor_get_subsurface failed.");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  wl_subsurface_set_position(state->subsurface, 0, 0);
  wl_subsurface_set_desync(state->subsurface);

  if (state->fractionalScaleManager) {
    state->fractionalScale = WpFractionalScaleManagerGetFractionalScale(
        state->fractionalScaleManager, state->childSurface);
    if (state->fractionalScale) {
      wl_proxy_add_listener(
          reinterpret_cast<wl_proxy*>(state->fractionalScale),
          reinterpret_cast<void (**)(void)>(
              const_cast<WpFractionalScaleV1Listener*>(
                  &g_wpFractionalScaleListener)),
          state);
    }
  }

  if (state->viewporter) {
    state->viewport =
        WpViewporterGetViewport(state->viewporter, state->childSurface);
  }

  // Commit child + parent and roundtrip BEFORE computing the initial viewport
  // destination so compositors that immediately emit preferred_scale for a
  // mapped parent surface deliver it right here.
  wl_surface_commit(state->childSurface);
  wl_surface_commit(state->hostSurface);
  wl_display_flush(state->hostDisplay);

  if (wl_display_roundtrip_queue(state->hostDisplay, state->queue) < 0 ||
      wl_display_get_error(state->hostDisplay) != 0) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: Wayland protocol error during subsurface "
              "attachment (err="
                  << wl_display_get_error(state->hostDisplay) << ").");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  uint32_t logW = m_guestWidth;
  uint32_t logH = m_guestHeight;
  uint32_t pixW = m_guestWidth;
  uint32_t pixH = m_guestHeight;
  int32_t scale = 1;
  bool scaleToDisplay = false;
  QueryWaylandHostSizes(hostSdlWindow, m_guestWidth, m_guestHeight,
                        state->preferredScale120, logW, logH, pixW, pixH, scale,
                        scaleToDisplay);

  if (state->viewport) {
    // Use wp_viewport.set_destination(logicalW, logicalH) with buffer_scale=1
    // so fractional & integer HiDPI scales (125%, 133%, 150%, 175%, 200%, ...)
    // map the full physical pixel swapchain 1:1 into the host's logical window
    // without rounding mismatches or invalid_size protocol errors.
    scale = 1;
    if (state->compositorVersion >= 3) {
      wl_surface_set_buffer_scale(state->childSurface, 1);
    }
    WpViewportSetDestination(state->viewport, static_cast<int32_t>(logW),
                             static_cast<int32_t>(logH));
  } else {
    pixW = logW * static_cast<uint32_t>(scale);
    pixH = logH * static_cast<uint32_t>(scale);
    if (state->compositorVersion >= 3) {
      wl_surface_set_buffer_scale(state->childSurface, scale);
    }
  }
  state->logicalWidth = logW;
  state->logicalHeight = logH;

  wl_surface_commit(state->childSurface);
  wl_display_flush(state->hostDisplay);

  if (wl_display_roundtrip_queue(state->hostDisplay, state->queue) < 0 ||
      wl_display_get_error(state->hostDisplay) != 0) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: Wayland protocol error configuring viewport "
              "(err="
                  << wl_display_get_error(state->hostDisplay) << ").");
    DestroyWaylandSubsurfaceState(state, false);
    return false;
  }

  m_wlState = state;
  m_wlDisplay = state->hostDisplay;
  m_wlChildSurface = state->childSurface;
  m_wlShm = state->shm;
  m_wlBufferScale = scale;
  m_pixelWidth = pixW;
  m_pixelHeight = pixH;
  m_mode = Mode::WaylandSubsurface;

  SDL_version sdlVer{};
  SDL_GetVersion(&sdlVer);
  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in WaylandSubsurface mode "
            "(hostSurface="
                << state->hostSurface
                << ", childSurface=" << state->childSurface
                << ", logical=" << state->logicalWidth << "x"
                << state->logicalHeight << ", pixels=" << m_pixelWidth << "x"
                << m_pixelHeight
                << ", preferredScale=" << state->preferredScale120 << "/120"
                << ", scaleToDisplay=" << (scaleToDisplay ? "yes" : "no")
                << ", viewporter=" << (state->viewport ? "yes" : "no")
                << ", sdl=" << static_cast<int>(sdlVer.major) << "."
                << static_cast<int>(sdlVer.minor) << "."
                << static_cast<int>(sdlVer.patch)
                << ", shm=" << (m_wlShm ? "yes" : "no")
                << ", input=click-through).");
  return true;
#else
  (void)hostDisplayPtr;
  (void)hostSurfacePtr;
  (void)hostSdlWindow;
  return false;
#endif
}

void NativeSurfaceHost::QueryStandaloneDrawableSize(uint32_t& outW,
                                                    uint32_t& outH) const {
#if !defined(_WIN32)
  if (!m_sdlWindow) {
    outW = m_guestWidth;
    outH = m_guestHeight;
    return;
  }

  int w = 0;
  int h = 0;
#if SDL_VERSION_ATLEAST(2, 26, 0)
  SDL_GetWindowSizeInPixels(m_sdlWindow, &w, &h);
#else
  SDL_GetWindowSize(m_sdlWindow, &w, &h);
#endif
  if (w <= 0 || h <= 0) {
    SDL_GetWindowSize(m_sdlWindow, &w, &h);
  }
  outW = (w > 0) ? static_cast<uint32_t>(w) : m_guestWidth;
  outH = (h > 0) ? static_cast<uint32_t>(h) : m_guestHeight;
#else
  outW = m_guestWidth;
  outH = m_guestHeight;
#endif
}

void NativeSurfaceHost::Detach() {
  if (m_mode == Mode::Detached) {
    return;
  }

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost detaching mode=" << ModeToString(m_mode));

#if !defined(_WIN32)
  if (m_mode == Mode::StandaloneSDL2) {
    if (m_sdlWindow) {
      SDL_DestroyWindow(m_sdlWindow);
      m_sdlWindow = nullptr;
    }
    m_sdlWindowId = 0;
    m_lastPollTime = {};
    m_sdlVideoDriver.clear();
  }
#else
  if ((m_mode == Mode::Win32Window || m_mode == Mode::Win32Embedded) &&
      m_win32Hwnd) {
    if (m_mode == Mode::Win32Embedded) {
      RestoreWin32EmbeddedFullscreenIfSaved();
    }
    HWND hwnd = static_cast<HWND>(m_win32Hwnd);
    if (m_win32OwnsHwnd && IsWindow(hwnd)) {
      SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
      DestroyWindow(hwnd);
    }
  }
#endif
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  else if (m_mode == Mode::X11Child && m_x11Display) {
    Display* dpy = static_cast<Display*>(m_x11Display);
    {
      ScopedX11ErrorTrap trap;
      if (m_x11ChildWindow != 0 && !m_x11ParentDestroyed) {
        XDestroyWindow(dpy, static_cast<Window>(m_x11ChildWindow));
      }
      if (m_x11Colormap != 0) {
        XFreeColormap(dpy, static_cast<Colormap>(m_x11Colormap));
      }
      XSync(dpy, False);
    }
    XCloseDisplay(dpy);
  }
#endif
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  else if (m_mode == Mode::WaylandSubsurface && m_wlState) {
    DestroyWaylandSubsurfaceState(
        static_cast<WaylandSubsurfaceState*>(m_wlState),
        /*commitParentIfAlive=*/true);
  }
#endif

  m_x11Display = nullptr;
  m_x11ParentWindow = 0;
  m_x11ChildWindow = 0;
  m_x11Colormap = 0;
  m_x11ParentDestroyed = false;
  m_wlDisplay = nullptr;
  m_wlChildSurface = nullptr;
  m_wlShm = nullptr;
  m_wlBufferScale = 1;
  m_wlState = nullptr;
  m_win32Hinstance = nullptr;
  m_win32ParentHwnd = nullptr;
  m_win32Hwnd = nullptr;
  m_win32OwnsHwnd = false;
  m_win32FullscreenSaved = false;
  m_win32FullscreenApplied = false;
  m_inWin32FullscreenAdjust = false;
  m_inWin32ForegroundRaise = false;
  m_win32SurfaceInvalidated = false;
  m_win32PendingForeground.store(false, std::memory_order_relaxed);
  m_win32ResetFullscreenTracking.store(false, std::memory_order_relaxed);
  m_win32LastStyle = 0;
  m_win32LastExStyle = 0;
  m_win32LastWndProc = 0;
  m_win32LastPixelFormat = 0;
  m_win32SavedStyle = 0;
  m_win32SavedExStyle = 0;
  m_win32SavedRect[0] = m_win32SavedRect[1] = m_win32SavedRect[2] =
      m_win32SavedRect[3] = 0;
  m_win32AppliedMonRect[0] = m_win32AppliedMonRect[1] =
      m_win32AppliedMonRect[2] = m_win32AppliedMonRect[3] = 0;
  m_win32SettledRect[0] = m_win32SettledRect[1] = m_win32SettledRect[2] =
      m_win32SettledRect[3] = 0;

  m_mode = Mode::Detached;
  m_guestWidth = 0;
  m_guestHeight = 0;
  m_pixelWidth = 0;
  m_pixelHeight = 0;
  m_quitRequested = false;
  m_fullscreen = false;
  m_postAttachForcePolls = 0;
}

bool NativeSurfaceHost::SyncBounds(uint32_t& outPixelW, uint32_t& outPixelH) {
  if (m_mode == Mode::Detached || m_mode == Mode::Headless) {
    outPixelW = m_pixelWidth;
    outPixelH = m_pixelHeight;
    return false;
  }

  if (m_mode == Mode::StandaloneSDL2) {
    uint32_t newW = 0;
    uint32_t newH = 0;
    QueryStandaloneDrawableSize(newW, newH);
    const bool changed = (newW != m_pixelWidth || newH != m_pixelHeight);
    m_pixelWidth = newW;
    m_pixelHeight = newH;
    outPixelW = m_pixelWidth;
    outPixelH = m_pixelHeight;
    return changed;
  }

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (m_mode == Mode::X11Child && m_x11Display && m_x11ChildWindow != 0 &&
      !m_x11ParentDestroyed) {
    Display* dpy = static_cast<Display*>(m_x11Display);
    uint32_t targetW = m_pixelWidth;
    uint32_t targetH = m_pixelHeight;
    bool sawConfigure = false;

    while (XEventsQueued(dpy, QueuedAfterReading) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      if (ev.type == ConfigureNotify &&
          ev.xconfigure.window == static_cast<Window>(m_x11ParentWindow)) {
        if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0) {
          targetW = static_cast<uint32_t>(ev.xconfigure.width);
          targetH = static_cast<uint32_t>(ev.xconfigure.height);
          sawConfigure = true;
        }
      } else if (ev.type == DestroyNotify &&
                 ev.xdestroywindow.window ==
                     static_cast<Window>(m_x11ParentWindow)) {
        m_x11ParentDestroyed = true;
      }
    }

    if (!m_x11ParentDestroyed && sawConfigure &&
        (targetW != m_pixelWidth || targetH != m_pixelHeight)) {
      {
        ScopedX11ErrorTrap trap;
        XMoveResizeWindow(dpy, static_cast<Window>(m_x11ChildWindow), 0, 0,
                          targetW, targetH);
        XFlush(dpy);
      }
      m_pixelWidth = targetW;
      m_pixelHeight = targetH;
      outPixelW = m_pixelWidth;
      outPixelH = m_pixelHeight;
      return true;
    }
  }
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  if (m_mode == Mode::WaylandSubsurface && m_wlState) {
    auto* state = static_cast<WaylandSubsurfaceState*>(m_wlState);
    if (state->hostDisplay && state->queue) {
      wl_display_dispatch_queue_pending(state->hostDisplay, state->queue);
    }

    const bool parentStillAlive =
        (state->hostSdlWindow != nullptr) &&
        (SDL_WasInit(SDL_INIT_VIDEO) != 0) &&
        (SDL_GetWindowFromID(state->hostSdlWindowId) == state->hostSdlWindow);
    if (parentStillAlive) {
      uint32_t newLogW = state->logicalWidth;
      uint32_t newLogH = state->logicalHeight;
      uint32_t newPixW = m_pixelWidth;
      uint32_t newPixH = m_pixelHeight;
      int32_t newScale = m_wlBufferScale;
      bool scaleToDisplay = false;
      QueryWaylandHostSizes(state->hostSdlWindow, m_guestWidth, m_guestHeight,
                            state->preferredScale120, newLogW, newLogH, newPixW,
                            newPixH, newScale, scaleToDisplay);

      bool changed = false;
      if (state->viewport) {
        if (newLogW != state->logicalWidth || newLogH != state->logicalHeight) {
          state->logicalWidth = newLogW;
          state->logicalHeight = newLogH;
          WpViewportSetDestination(state->viewport,
                                   static_cast<int32_t>(newLogW),
                                   static_cast<int32_t>(newLogH));
          changed = true;
        }
        if (newPixW != m_pixelWidth || newPixH != m_pixelHeight) {
          changed = true;
        }
      } else {
        newPixW = newLogW * static_cast<uint32_t>(newScale);
        newPixH = newLogH * static_cast<uint32_t>(newScale);
        if (newScale != m_wlBufferScale) {
          m_wlBufferScale = newScale;
          if (state->childSurface && state->compositorVersion >= 3) {
            wl_surface_set_buffer_scale(state->childSurface, m_wlBufferScale);
          }
          changed = true;
        }
        if (newPixW != m_pixelWidth || newPixH != m_pixelHeight) {
          changed = true;
        }
        state->logicalWidth = newLogW;
        state->logicalHeight = newLogH;
      }

      if (changed) {
        if (state->hostDisplay) {
          wl_display_flush(state->hostDisplay);
        }
        m_pixelWidth = newPixW;
        m_pixelHeight = newPixH;
        GLIDE_LOG(INFO, "WSI",
                  "WaylandSubsurface resized: logical="
                      << state->logicalWidth << "x" << state->logicalHeight
                      << ", pixels=" << m_pixelWidth << "x" << m_pixelHeight
                      << ", preferredScale=" << state->preferredScale120
                      << "/120, scaleToDisplay="
                      << (scaleToDisplay ? "yes" : "no")
                      << ", viewporter=" << (state->viewport ? "yes" : "no"));
        outPixelW = m_pixelWidth;
        outPixelH = m_pixelHeight;
        return true;
      }
    }
  }
#endif

#if defined(_WIN32)
  if (m_mode == Mode::Win32Window && m_win32Hwnd) {
    HWND hwnd = static_cast<HWND>(m_win32Hwnd);
    if (!IsWindow(hwnd)) {
      outPixelW = 0;
      outPixelH = 0;
      return false;
    }
    RECT rc{};
    if (GetClientRect(hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
      const uint32_t newW = static_cast<uint32_t>(rc.right - rc.left);
      const uint32_t newH = static_cast<uint32_t>(rc.bottom - rc.top);
      const bool changed = (newW != m_pixelWidth || newH != m_pixelHeight);
      m_pixelWidth = newW;
      m_pixelHeight = newH;
      outPixelW = m_pixelWidth;
      outPixelH = m_pixelHeight;
      return changed;
    }
  } else if (m_mode == Mode::Win32Embedded && m_win32ParentHwnd) {
    HWND parent = static_cast<HWND>(m_win32ParentHwnd);
    if (!IsWindow(parent)) {
      outPixelW = 0;
      outPixelH = 0;
      return false;
    }

    // Track structural Win32 style mutations (e.g. DirectDraw fullscreen
    // WS_POPUP|WS_MAXIMIZE returning to WS_CAPTION|WS_THICKFRAME, or WS_VISIBLE
    // toggling). On Wine's winex11.drv under managed X11/XWayland WMs, these
    // transitions can unmap/remap or recreate the host X11 window and require
    // WSI surface recreation.
    constexpr DWORD kStructuralStyleMask =
        WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_VISIBLE | WS_MINIMIZE |
        WS_MAXIMIZE;
    constexpr DWORD kStructuralExStyleMask =
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_APPWINDOW |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    const LONG preStyle = GetWindowLongA(parent, GWL_STYLE);
    const LONG preExStyle = GetWindowLongA(parent, GWL_EXSTYLE);
    const intptr_t curWndProc =
        static_cast<intptr_t>(GetWindowLongPtrA(parent, GWLP_WNDPROC));
    int curPixelFormat = m_win32LastPixelFormat;
    if (HDC hdc = GetDC(parent)) {
      curPixelFormat = GetPixelFormat(hdc);
      ReleaseDC(parent, hdc);
    }
    const bool wndProcChanged = (curWndProc != m_win32LastWndProc);
    const bool pixelFormatChanged = (curPixelFormat != m_win32LastPixelFormat);
    const bool exStyleChanged =
        ((static_cast<DWORD>(preExStyle) & kStructuralExStyleMask) !=
         (static_cast<DWORD>(m_win32LastExStyle) & kStructuralExStyleMask));
    bool styleChanged =
        ((static_cast<DWORD>(preStyle) & kStructuralStyleMask) !=
         (static_cast<DWORD>(m_win32LastStyle) & kStructuralStyleMask));
    bool foreignTakeover = (wndProcChanged || pixelFormatChanged || exStyleChanged);
    if (foreignTakeover) {
      GLIDE_LOG(INFO, "WSI",
                "NativeSurfaceHost: detected host HWND "
                    << m_win32ParentHwnd << " surface mutation ("
                    << (pixelFormatChanged ? "pixelFormat " : "")
                    << (wndProcChanged ? "wndProc " : "")
                    << (exStyleChanged ? "exStyle " : "")
                    << "pfmt " << m_win32LastPixelFormat << "->"
                    << curPixelFormat << "); marking WSI surface invalidated.");
    }

    if (m_win32ResetFullscreenTracking.exchange(false,
                                                std::memory_order_relaxed)) {
      m_win32FullscreenApplied = false;
    }
    const bool hadAppliedFullscreen = m_win32FullscreenApplied;
    bool appliedFullscreen = false;
    if (m_fullscreen) {
      appliedFullscreen = ApplyWin32EmbeddedFullscreenIfNeeded();
    }

    // One-shot restore & foreground raise after returning from VGA pass-through
    // (e.g. DirectDraw fullscreen intro exiting back to Glide) or upon first
    // applying borderless fullscreen, so the host WM does not leave the game
    // iconic or hidden behind the launching terminal. Never runs on steady
    // frames so Alt+Tab works normally during gameplay.
    const bool wantInitialFullscreenRaise =
        (appliedFullscreen && !hadAppliedFullscreen);
    const bool wantPassthruRaise =
        m_win32PendingForeground.exchange(false, std::memory_order_relaxed);
    if ((wantPassthruRaise || wantInitialFullscreenRaise) &&
        !m_inWin32ForegroundRaise && !m_inWin32FullscreenAdjust) {
      struct RaiseGuard {
        bool& flag;
        explicit RaiseGuard(bool& f) : flag(f) { flag = true; }
        ~RaiseGuard() { flag = false; }
      } guard(m_inWin32ForegroundRaise);

      if (IsIconic(parent)) {
        ShowWindow(parent, SW_RESTORE);
      } else if (!IsWindowVisible(parent)) {
        ShowWindow(parent, SW_SHOW);
      }
      SetWindowPos(parent, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
      BringWindowToTop(parent);
      SetForegroundWindow(parent);
      GLIDE_LOG(INFO, "WSI",
                "NativeSurfaceHost: restored & raised Win32 HWND "
                    << m_win32ParentHwnd
                    << (wantInitialFullscreenRaise
                            ? " after entering borderless fullscreen."
                            : " after reclaiming 3dfx output from VGA."));
    }

    const LONG postStyle = GetWindowLongA(parent, GWL_STYLE);
    const LONG postExStyle = GetWindowLongA(parent, GWL_EXSTYLE);
    if (appliedFullscreen ||
        ((static_cast<DWORD>(postStyle) & kStructuralStyleMask) !=
         (static_cast<DWORD>(m_win32LastStyle) & kStructuralStyleMask)) ||
        ((static_cast<DWORD>(postExStyle) & kStructuralExStyleMask) !=
         (static_cast<DWORD>(m_win32LastExStyle) & kStructuralExStyleMask))) {
      styleChanged = true;
    }
    m_win32LastStyle = static_cast<intptr_t>(postStyle);
    m_win32LastExStyle = static_cast<intptr_t>(postExStyle);
    m_win32LastWndProc = curWndProc;
    m_win32LastPixelFormat = curPixelFormat;
    if (!IsWindowVisible(parent) || IsIconic(parent)) {
      // Skip WSI surface/swapchain recreation while the host HWND is hidden,
      // minimized, or closing (e.g. a title hiding its window before
      // grGlideShutdown); Wine's winex11 surface-support queries can fail on
      // unmapped windows. When WS_VISIBLE is later restored, the style diff
      // above will trigger a clean surface rebuild.
      m_win32SurfaceInvalidated = false;
      outPixelW = 0;
      outPixelH = 0;
      return false;
    }
    if (styleChanged || foreignTakeover) {
      m_win32SurfaceInvalidated = true;
    }

    RECT rc{};
    if (GetClientRect(parent, &rc) && rc.right > rc.left &&
        rc.bottom > rc.top) {
      const uint32_t newW = static_cast<uint32_t>(rc.right - rc.left);
      const uint32_t newH = static_cast<uint32_t>(rc.bottom - rc.top);
      const bool changed = (newW != m_pixelWidth || newH != m_pixelHeight);
      if (changed) {
        if (m_win32OwnsHwnd && m_win32Hwnd &&
            IsWindow(static_cast<HWND>(m_win32Hwnd))) {
          SetWindowPos(static_cast<HWND>(m_win32Hwnd), nullptr, 0, 0,
                       static_cast<int>(newW), static_cast<int>(newH),
                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
        m_pixelWidth = newW;
        m_pixelHeight = newH;
      }
      outPixelW = m_pixelWidth;
      outPixelH = m_pixelHeight;
      return changed;
    }
  }
#endif

  outPixelW = m_pixelWidth;
  outPixelH = m_pixelHeight;
  return false;
}

void NativeSurfaceHost::NotifyWin32PassthruReclaimed() {
#if defined(_WIN32)
  if (m_mode == Mode::Win32Embedded && m_win32ParentHwnd) {
    m_win32PendingForeground.store(true, std::memory_order_relaxed);
    m_win32ResetFullscreenTracking.store(true, std::memory_order_relaxed);
  }
#endif
}

bool NativeSurfaceHost::ConsumeSurfaceInvalidated() {
  const bool was = m_win32SurfaceInvalidated;
  m_win32SurfaceInvalidated = false;
  return was;
}

void NativeSurfaceHost::RefreshWin32SurfaceFingerprint() {
#if defined(_WIN32)
  if (m_mode == Mode::Win32Embedded && m_win32ParentHwnd) {
    HWND parent = static_cast<HWND>(m_win32ParentHwnd);
    if (IsWindow(parent)) {
      m_win32LastStyle = static_cast<intptr_t>(GetWindowLongA(parent, GWL_STYLE));
      m_win32LastExStyle =
          static_cast<intptr_t>(GetWindowLongA(parent, GWL_EXSTYLE));
      m_win32LastWndProc =
          static_cast<intptr_t>(GetWindowLongPtrA(parent, GWLP_WNDPROC));
      if (HDC hdc = GetDC(parent)) {
        m_win32LastPixelFormat = GetPixelFormat(hdc);
        ReleaseDC(parent, hdc);
      }
    }
  }
#endif
}

void NativeSurfaceHost::PollEvents(bool force) {
#if defined(_WIN32)
  // Strictly isolated to Win32Window so foreign game HWND loops are untouched.
  if (m_mode != Mode::Win32Window || !m_win32Hwnd) {
    return;
  }

  bool doForce = force;
  if (!doForce && m_postAttachForcePolls > 0) {
    --m_postAttachForcePolls;
    doForce = true;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!doForce && m_lastPollTime.time_since_epoch().count() != 0 &&
      (now - m_lastPollTime) < std::chrono::milliseconds(4)) {
    return;
  }
  m_lastPollTime = now;

  HWND hwnd = static_cast<HWND>(m_win32Hwnd);
  MSG msg{};
  while (PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      m_quitRequested = true;
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
#else
  // Strictly isolated to StandaloneSDL2 so we never steal DOSBox's SDL events.
  if (m_mode != Mode::StandaloneSDL2 || !m_sdlWindow) {
    return;
  }

  bool doForce = force;
  if (!doForce && m_postAttachForcePolls > 0) {
    --m_postAttachForcePolls;
    doForce = true;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!doForce && m_lastPollTime.time_since_epoch().count() != 0 &&
      (now - m_lastPollTime) < std::chrono::milliseconds(4)) {
    return;
  }
  m_lastPollTime = now;

  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) {
      m_quitRequested = true;
    } else if (ev.type == SDL_WINDOWEVENT &&
               ev.window.windowID == m_sdlWindowId) {
      if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
        m_quitRequested = true;
      }
    }
  }
#endif
}

bool NativeSurfaceHost::IsWaylandSurface() const {
  if (m_mode == Mode::WaylandSubsurface) {
    return true;
  }
  if (m_mode == Mode::StandaloneSDL2) {
    return m_sdlVideoDriver == "wayland";
  }
  return false;
}

bool NativeSurfaceHost::IsX11Surface() const {
  if (m_mode == Mode::X11Child) {
    return true;
  }
  if (m_mode == Mode::StandaloneSDL2) {
    return m_sdlVideoDriver == "x11";
  }
  return false;
}

void* NativeSurfaceHost::GetWaylandEventQueue() const {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  if (m_mode == Mode::WaylandSubsurface && m_wlState) {
    return static_cast<const WaylandSubsurfaceState*>(m_wlState)->queue;
  }
#endif
  return nullptr;
}

}  // namespace GlideWrapper
