#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "core/NativeSurfaceHost.h"

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
#include <X11/Xlib.h>
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

using GlideWrapper::NativeSurfaceHost;
using GlideWrapper::PresentRect;

namespace {

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [options]\n"
            << "  --raw-handle       Pass native X11 Window ID / wl_surface* "
               "(DOSBox-X style, default)\n"
            << "  --sdl-window       Pass SDL_Window* directly (SVN DOSBox "
               "setConfig('SDL2') style)\n"
            << "  --smoke-ms <ms>    Automated resize smoke test & exit after "
               "<ms> milliseconds\n"
            << "  --help             Show this help\n";
}

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
void DrawX11TestPattern(Display* dpy, Window childWin, GC gc, uint32_t hostW,
                        uint32_t hostH, uint32_t guestW, uint32_t guestH,
                        uint32_t frameIdx, uint32_t clickCount,
                        uint32_t keyCount) {
  if (!dpy || childWin == 0 || !gc || hostW == 0 || hostH == 0) {
    return;
  }

  // 1. Fill full child window with dark letterbox/pillarbox black.
  XSetForeground(dpy, gc, 0x00080810UL);
  XFillRectangle(dpy, childWin, gc, 0, 0, hostW, hostH);

  // 2. Compute 4:3 aspect-preserving PresentRect.
  PresentRect dst =
      NativeSurfaceHost::ComputePresentRect(guestW, guestH, hostW, hostH, true);

  // 3. Draw 4:3 guest viewport interior (deep blue) + bright cyan frame.
  XSetForeground(dpy, gc, 0x00102850UL);
  XFillRectangle(dpy, childWin, gc, dst.x, dst.y, dst.width, dst.height);

  XSetForeground(dpy, gc, 0x0000E5FFUL);
  if (dst.width > 2 && dst.height > 2) {
    XDrawRectangle(dpy, childWin, gc, dst.x, dst.y, dst.width - 1,
                   dst.height - 1);
    XDrawRectangle(dpy, childWin, gc, dst.x + 2, dst.y + 2,
                   (dst.width > 5) ? dst.width - 5 : 1,
                   (dst.height > 5) ? dst.height - 5 : 1);
  }

  // 4. Draw animated bouncing box inside the 4:3 region so frame pacing and
  //    bounds tracking are immediately visible.
  const uint32_t boxSize =
      std::max<uint32_t>(24, std::min(dst.width, dst.height) / 6);
  const double phase = static_cast<double>(frameIdx) * 0.04;
  const double nx = 0.5 + 0.35 * std::cos(phase);
  const double ny = 0.5 + 0.35 * std::sin(phase * 1.3);
  const int32_t bx = dst.x + static_cast<int32_t>(nx * (dst.width - boxSize));
  const int32_t by = dst.y + static_cast<int32_t>(ny * (dst.height - boxSize));

  // Color shifts on clicks/keys so input pass-through is visually confirmed!
  const unsigned long boxColor =
      ((clickCount + keyCount) % 2 == 0) ? 0x0033FF66UL : 0x00FFAA22UL;
  XSetForeground(dpy, gc, boxColor);
  XFillRectangle(dpy, childWin, gc, bx, by, boxSize, boxSize);

  // 5. Draw center crosshair.
  XSetForeground(dpy, gc, 0x00FFFFFFUL);
  const int32_t cx = dst.x + static_cast<int32_t>(dst.width / 2);
  const int32_t cy = dst.y + static_cast<int32_t>(dst.height / 2);
  XDrawLine(dpy, childWin, gc, cx - 16, cy, cx + 16, cy);
  XDrawLine(dpy, childWin, gc, cx, cy - 16, cx, cy + 16);

  XFlush(dpy);
}
#endif

}  // namespace

int main(int argc, char** argv) {
  bool passSdlWindow = false;
  int smokeMs = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--sdl-window") == 0) {
      passSdlWindow = true;
    } else if (std::strcmp(argv[i], "--raw-handle") == 0) {
      passSdlWindow = false;
    } else if (std::strcmp(argv[i], "--smoke-ms") == 0 && i + 1 < argc) {
      smokeMs = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  const char* driver = SDL_GetCurrentVideoDriver();
  std::cout << "[EmbedTest] SDL2 video driver: "
            << (driver ? driver : "unknown") << "\n";

  const uint32_t guestW = 640;
  const uint32_t guestH = 480;

  SDL_Window* parentWin = SDL_CreateWindow(
      "NativeSurfaceHost Embed Test (Parent)", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!parentWin) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_SysWMinfo wmi{};
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(parentWin, &wmi)) {
    std::cerr << "SDL_GetWindowWMInfo failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(parentWin);
    SDL_Quit();
    return 1;
  }

  void* hostHandle = nullptr;
  if (passSdlWindow) {
    hostHandle = static_cast<void*>(parentWin);
    std::cout << "[EmbedTest] Passing SDL_Window* hostHandle=" << hostHandle
              << "\n";
  } else if (wmi.subsystem == SDL_SYSWM_X11) {
    hostHandle = reinterpret_cast<void*>(
        static_cast<uintptr_t>(wmi.info.x11.window));
    std::cout << "[EmbedTest] Passing raw X11 Window ID hostHandle="
              << hostHandle << "\n";
  } else if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
    hostHandle = static_cast<void*>(wmi.info.wl.surface);
    std::cout << "[EmbedTest] Passing raw wl_surface* hostHandle=" << hostHandle
              << "\n";
  } else {
    std::cerr << "[EmbedTest] Unsupported SDL WM subsystem: " << wmi.subsystem
              << "\n";
    SDL_DestroyWindow(parentWin);
    SDL_Quit();
    return 1;
  }

  NativeSurfaceHost surfaceHost;
  if (!surfaceHost.Attach(hostHandle, guestW, guestH, /*windowed=*/true,
                          /*forceNoWindow=*/false, "EmbedChild")) {
    std::cerr << "[EmbedTest] NativeSurfaceHost::Attach failed!\n";
    SDL_DestroyWindow(parentWin);
    SDL_Quit();
    return 2;
  }

  uint32_t curW = 0, curH = 0;
  surfaceHost.GetDrawableSize(curW, curH);
  std::cout << "[EmbedTest] Attached mode="
            << NativeSurfaceHost::ModeToString(surfaceHost.GetMode())
            << " initial_size=" << curW << "x" << curH << "\n";
  std::cout << "[EmbedTest] Controls: Click/type anywhere inside window to "
               "verify input passthrough, F or Alt+Enter=Fullscreen, "
               "ESC/Q=Quit.\n"
            << std::flush;

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  GC x11Gc = nullptr;
  if (surfaceHost.GetMode() == NativeSurfaceHost::Mode::X11Child) {
    Display* dpy = static_cast<Display*>(surfaceHost.GetX11Display());
    Window child = static_cast<Window>(surfaceHost.GetX11Window());
    x11Gc = XCreateGC(dpy, child, 0, nullptr);
  }
#endif

  bool running = true;
  bool fullscreen = false;
  uint32_t clickCount = 0;
  uint32_t keyCount = 0;
  uint32_t resizeCount = 0;
  uint32_t frameIdx = 0;
  bool smokeResizeIssued = false;
  bool smokeResizeObserved = false;

  const auto startTime = std::chrono::steady_clock::now();

  auto updateTitle = [&]() {
    std::ostringstream oss;
    oss << "NativeSurfaceHost Embed Test ["
        << NativeSurfaceHost::ModeToString(surfaceHost.GetMode()) << "] "
        << curW << "x" << curH << " | clicks=" << clickCount
        << " keys=" << keyCount << " resizes=" << resizeCount
        << " | F=Fullscreen ESC=Quit";
    SDL_SetWindowTitle(parentWin, oss.str().c_str());
  };
  updateTitle();

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        running = false;
      } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
        ++clickCount;
        std::cout << "[EmbedTest] Parent received click #" << clickCount
                  << " at (" << ev.button.x << ", " << ev.button.y << ")\n"
                  << std::flush;
        updateTitle();
      } else if (ev.type == SDL_KEYDOWN) {
        ++keyCount;
        const SDL_Keycode sym = ev.key.keysym.sym;
        std::cout << "[EmbedTest] Parent received key #" << keyCount << " ("
                  << SDL_GetKeyName(sym) << ")\n"
                  << std::flush;
        if (sym == SDLK_ESCAPE || sym == SDLK_q) {
          running = false;
        } else if (sym == SDLK_f ||
                   (sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(
              parentWin, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        updateTitle();
      }
    }

    uint32_t newW = 0, newH = 0;
    if (surfaceHost.SyncBounds(newW, newH)) {
      ++resizeCount;
      curW = newW;
      curH = newH;
      PresentRect dst = NativeSurfaceHost::ComputePresentRect(
          guestW, guestH, curW, curH, true);
      std::cout << "[EmbedTest] SyncBounds resize #" << resizeCount << " -> "
                << curW << "x" << curH << " (4:3 PresentRect: x=" << dst.x
                << " y=" << dst.y << " " << dst.width << "x" << dst.height
                << ")\n"
                << std::flush;
      if (smokeResizeIssued && curW == 1024 && curH == 600) {
        smokeResizeObserved = true;
      }
      updateTitle();
    }

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
    if (surfaceHost.GetMode() == NativeSurfaceHost::Mode::X11Child && x11Gc) {
      DrawX11TestPattern(static_cast<Display*>(surfaceHost.GetX11Display()),
                         static_cast<Window>(surfaceHost.GetX11Window()), x11Gc,
                         curW, curH, guestW, guestH, frameIdx, clickCount,
                         keyCount);
    }
#endif

    ++frameIdx;

    if (smokeMs > 0) {
      const auto elapsedMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - startTime)
              .count();
      if (!smokeResizeIssued && elapsedMs >= smokeMs / 3) {
        smokeResizeIssued = true;
        std::cout << "[EmbedTest] Smoke test: resizing parent to 1024x600...\n"
                  << std::flush;
        SDL_SetWindowSize(parentWin, 1024, 600);
      }
      if (elapsedMs >= smokeMs) {
        running = false;
      }
    }

    SDL_Delay(16);
  }

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (x11Gc && surfaceHost.GetX11Display()) {
    XFreeGC(static_cast<Display*>(surfaceHost.GetX11Display()), x11Gc);
    x11Gc = nullptr;
  }
#endif

  surfaceHost.Detach();
  SDL_DestroyWindow(parentWin);
  SDL_Quit();

  if (smokeMs > 0) {
    std::cout << "[EmbedTest] Smoke test complete: frames=" << frameIdx
              << " resizes=" << resizeCount
              << " resize_1024x600_observed=" << (smokeResizeObserved ? 1 : 0)
              << "\n";
    return smokeResizeObserved ? 0 : 3;
  }

  return 0;
}
