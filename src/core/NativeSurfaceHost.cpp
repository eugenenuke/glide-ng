#include "core/NativeSurfaceHost.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_version.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "core/Logger.h"

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
#include <X11/Xlib.h>
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
          GLIDE_LOG(WARN, "WSI",
                    "Detected Wayland SDL_Window* hostHandle="
                        << hostHandle
                        << "; WaylandSubsurface attachment is scheduled for "
                           "T2.3.");
          m_guestWidth = 0;
          m_guestHeight = 0;
          return false;
        }
      } else if (isWaylandDriver && wmi.subsystem == SDL_SYSWM_WAYLAND &&
                 static_cast<void*>(wmi.info.wl.surface) == hostHandle) {
        // DOSBox-X statWMInfo() union alias on native Wayland.
        GLIDE_LOG(WARN, "WSI",
                  "Detected Wayland wl_surface* hostHandle="
                      << hostHandle
                      << "; WaylandSubsurface attachment is scheduled for "
                         "T2.3.");
        m_guestWidth = 0;
        m_guestHeight = 0;
        return false;
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
      XSync(dpy, False);
    }
    XCloseDisplay(dpy);
  }
#endif

  m_x11Display = nullptr;
  m_x11ParentWindow = 0;
  m_x11ChildWindow = 0;
  m_x11ParentDestroyed = false;
  m_wlDisplay = nullptr;
  m_wlChildSurface = nullptr;
  m_wlShm = nullptr;
  m_wlBufferScale = 1;

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
