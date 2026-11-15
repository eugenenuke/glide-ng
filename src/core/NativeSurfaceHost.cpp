#include "core/NativeSurfaceHost.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_version.h>

#include <algorithm>
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
                               uint32_t windowScale) {
  if (IsAttached()) {
    Detach();
  }

  m_guestWidth = (guestW > 0) ? guestW : 640;
  m_guestHeight = (guestH > 0) ? guestH : 480;
  m_quitRequested = false;

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
    return AttachStandaloneSDL2(m_guestWidth, m_guestHeight, title,
                                extraSdlWindowFlags, windowScale);
  }

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
    m_sdlVideoInitializedByUs = true;
  }

  const uint32_t clampedScale = std::clamp<uint32_t>(windowScale, 1, 8);
  const int winW = static_cast<int>(guestW * clampedScale);
  const int winH = static_cast<int>(guestH * clampedScale);
  const uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                         SDL_WINDOW_ALLOW_HIGHDPI | extraSdlWindowFlags;

  const char* windowTitle =
      title.empty() ? "3dfx Glide Wrapper (Standalone)" : title.c_str();
  m_sdlWindow = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, winW, winH, flags);
  if (!m_sdlWindow) {
    GLIDE_LOG(CRITICAL, "WSI",
              "NativeSurfaceHost: SDL_CreateWindow failed: " << SDL_GetError());
    if (m_sdlVideoInitializedByUs) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      m_sdlVideoInitializedByUs = false;
    }
    return false;
  }

  m_sdlWindowId = SDL_GetWindowID(m_sdlWindow);
  const char* drv = SDL_GetCurrentVideoDriver();
  m_sdlVideoDriver = drv ? std::string(drv) : std::string();
  m_mode = Mode::StandaloneSDL2;

  QueryStandaloneDrawableSize(m_pixelWidth, m_pixelHeight);

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost attached in StandaloneSDL2 mode (driver="
                << (m_sdlVideoDriver.empty() ? "unknown" : m_sdlVideoDriver)
                << ", guest=" << m_guestWidth << "x" << m_guestHeight
                << ", drawable=" << m_pixelWidth << "x" << m_pixelHeight
                << ").");
  return true;
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
}

void NativeSurfaceHost::Detach() {
  if (m_mode == Mode::Detached) {
    return;
  }

  GLIDE_LOG(INFO, "WSI",
            "NativeSurfaceHost detaching mode=" << ModeToString(m_mode));

  if (m_mode == Mode::StandaloneSDL2) {
    if (m_sdlWindow) {
      SDL_DestroyWindow(m_sdlWindow);
      m_sdlWindow = nullptr;
    }
    m_sdlWindowId = 0;
    m_sdlVideoDriver.clear();
    if (m_sdlVideoInitializedByUs) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      m_sdlVideoInitializedByUs = false;
    }
  }
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

  m_mode = Mode::Detached;
  m_guestWidth = 0;
  m_guestHeight = 0;
  m_pixelWidth = 0;
  m_pixelHeight = 0;
  m_quitRequested = false;
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

  outPixelW = m_pixelWidth;
  outPixelH = m_pixelHeight;
  return false;
}

void NativeSurfaceHost::PollEvents() {
  // Strictly isolated to StandaloneSDL2 so we never steal DOSBox's SDL events.
  if (m_mode != Mode::StandaloneSDL2 || !m_sdlWindow) {
    return;
  }

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

}  // namespace GlideWrapper
