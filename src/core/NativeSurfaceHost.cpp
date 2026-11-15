#include "core/NativeSurfaceHost.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_version.h>

#include <algorithm>
#include <cstring>

#include "core/Logger.h"

namespace GlideWrapper {

NativeSurfaceHost::~NativeSurfaceHost() { Detach(); }

const char* NativeSurfaceHost::ModeToString(Mode mode) {
  switch (mode) {
    case Mode::None:
      return "None";
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

  // X11Child (T2.2) and WaylandSubsurface (T2.3) will hook in here.
  GLIDE_LOG(WARN, "WSI",
            "NativeSurfaceHost: foreign hostHandle="
                << hostHandle
                << " attachment is not implemented yet (scheduled for "
                   "T2.2/T2.3).");
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
  if (m_mode == Mode::None) {
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

  m_x11Display = nullptr;
  m_x11ParentWindow = 0;
  m_x11ChildWindow = 0;
  m_wlDisplay = nullptr;
  m_wlChildSurface = nullptr;
  m_wlShm = nullptr;
  m_wlBufferScale = 1;

  m_mode = Mode::None;
  m_guestWidth = 0;
  m_guestHeight = 0;
  m_pixelWidth = 0;
  m_pixelHeight = 0;
  m_quitRequested = false;
}

bool NativeSurfaceHost::SyncBounds(uint32_t& outPixelW, uint32_t& outPixelH) {
  if (m_mode == Mode::None || m_mode == Mode::Headless) {
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
