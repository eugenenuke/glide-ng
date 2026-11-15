#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
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

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
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

void PaintParentBackground(SDL_Window* parentWin) {
  // On native Wayland, an xdg_toplevel parent window is not mapped by the
  // compositor until at least one buffer is committed to it (DOSBox-X does
  // this via SDL_UpdateWindowSurface or SDL_GL_SwapWindow).
  SDL_Surface* surf = SDL_GetWindowSurface(parentWin);
  if (surf) {
    SDL_FillRect(surf, nullptr, SDL_MapRGB(surf->format, 24, 24, 32));
    SDL_UpdateWindowSurface(parentWin);
  }
}

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
void DrawX11TestPattern(Display* dpy, Window childWin, GC gc, uint32_t hostW,
                        uint32_t hostH, uint32_t guestW, uint32_t guestH,
                        uint32_t frameIdx, uint32_t clickCount,
                        uint32_t keyCount) {
  if (!dpy || childWin == 0 || !gc || hostW == 0 || hostH == 0) {
    return;
  }

  XSetForeground(dpy, gc, 0x00080810UL);
  XFillRectangle(dpy, childWin, gc, 0, 0, hostW, hostH);

  PresentRect dst =
      NativeSurfaceHost::ComputePresentRect(guestW, guestH, hostW, hostH, true);

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

  const uint32_t boxSize =
      std::max<uint32_t>(24, std::min(dst.width, dst.height) / 6);
  const double phase = static_cast<double>(frameIdx) * 0.04;
  const double nx = 0.5 + 0.35 * std::cos(phase);
  const double ny = 0.5 + 0.35 * std::sin(phase * 1.3);
  const int32_t bx = dst.x + static_cast<int32_t>(nx * (dst.width - boxSize));
  const int32_t by = dst.y + static_cast<int32_t>(ny * (dst.height - boxSize));

  const unsigned long boxColor =
      ((clickCount + keyCount) % 2 == 0) ? 0x0033FF66UL : 0x00FFAA22UL;
  XSetForeground(dpy, gc, boxColor);
  XFillRectangle(dpy, childWin, gc, bx, by, boxSize, boxSize);

  XSetForeground(dpy, gc, 0x00FFFFFFUL);
  const int32_t cx = dst.x + static_cast<int32_t>(dst.width / 2);
  const int32_t cy = dst.y + static_cast<int32_t>(dst.height / 2);
  XDrawLine(dpy, childWin, gc, cx - 16, cy, cx + 16, cy);
  XDrawLine(dpy, childWin, gc, cx, cy - 16, cx, cy + 16);

  XFlush(dpy);
}
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
class WaylandShmDrawer {
 public:
  ~WaylandShmDrawer() { DestroyBuffers(); }

  void Init(wl_display* dpy, wl_shm* shm, wl_surface* surface) {
    m_dpy = dpy;
    m_shm = shm;
    m_surface = surface;
  }

  void DestroyBuffers() {
    for (auto& slot : m_slots) {
      if (slot.buffer) {
        wl_buffer_destroy(slot.buffer);
        slot.buffer = nullptr;
      }
      if (slot.pixels && slot.pixels != MAP_FAILED) {
        munmap(slot.pixels, slot.byteSize);
        slot.pixels = nullptr;
      }
      slot.byteSize = 0;
      slot.busy = false;
    }
    m_width = 0;
    m_height = 0;
  }

  bool DrawFrame(uint32_t hostW, uint32_t hostH, uint32_t guestW,
                 uint32_t guestH, uint32_t frameIdx, uint32_t clickCount,
                 uint32_t keyCount) {
    if (!m_dpy || !m_shm || !m_surface || hostW == 0 || hostH == 0) {
      return false;
    }

    if (hostW != m_width || hostH != m_height) {
      if (!AllocateBuffers(hostW, hostH)) {
        return false;
      }
    }

    BufferSlot* slot = nullptr;
    for (auto& candidate : m_slots) {
      if (!candidate.busy) {
        slot = &candidate;
        break;
      }
    }
    if (!slot) {
      // Both buffers temporarily held by compositor; skip this tick cleanly.
      return true;
    }

    uint32_t* px = static_cast<uint32_t*>(slot->pixels);
    const uint32_t bgColor = 0xFF080810U;
    std::fill(px, px + static_cast<size_t>(hostW) * hostH, bgColor);

    auto fillRect = [&](int32_t rx, int32_t ry, uint32_t rw, uint32_t rh,
                        uint32_t argb) {
      const int32_t x0 = std::max<int32_t>(0, rx);
      const int32_t y0 = std::max<int32_t>(0, ry);
      const int32_t x1 =
          std::min<int32_t>(static_cast<int32_t>(hostW), rx + static_cast<int32_t>(rw));
      const int32_t y1 =
          std::min<int32_t>(static_cast<int32_t>(hostH), ry + static_cast<int32_t>(rh));
      for (int32_t y = y0; y < y1; ++y) {
        uint32_t* row = px + static_cast<size_t>(y) * hostW;
        std::fill(row + x0, row + x1, argb);
      }
    };

    PresentRect dst = NativeSurfaceHost::ComputePresentRect(
        guestW, guestH, hostW, hostH, true);

    // 4:3 viewport interior + 3px cyan border.
    fillRect(dst.x, dst.y, dst.width, dst.height, 0xFF102850U);
    if (dst.width > 6 && dst.height > 6) {
      const uint32_t border = 0xFF00E5FFU;
      fillRect(dst.x, dst.y, dst.width, 3, border);
      fillRect(dst.x, dst.y + static_cast<int32_t>(dst.height) - 3, dst.width, 3,
               border);
      fillRect(dst.x, dst.y, 3, dst.height, border);
      fillRect(dst.x + static_cast<int32_t>(dst.width) - 3, dst.y, 3,
               dst.height, border);
    }

    // Animated bouncing box (flips color on clicks/keys).
    const uint32_t boxSize =
        std::max<uint32_t>(24, std::min(dst.width, dst.height) / 6);
    const double phase = static_cast<double>(frameIdx) * 0.04;
    const double nx = 0.5 + 0.35 * std::cos(phase);
    const double ny = 0.5 + 0.35 * std::sin(phase * 1.3);
    const int32_t bx = dst.x + static_cast<int32_t>(nx * (dst.width - boxSize));
    const int32_t by =
        dst.y + static_cast<int32_t>(ny * (dst.height - boxSize));
    const uint32_t boxColor =
        ((clickCount + keyCount) % 2 == 0) ? 0xFF33FF66U : 0xFFFFAA22U;
    fillRect(bx, by, boxSize, boxSize, boxColor);

    // White center crosshair.
    const int32_t cx = dst.x + static_cast<int32_t>(dst.width / 2);
    const int32_t cy = dst.y + static_cast<int32_t>(dst.height / 2);
    fillRect(cx - 16, cy - 1, 33, 3, 0xFFFFFFFFU);
    fillRect(cx - 1, cy - 16, 3, 33, 0xFFFFFFFFU);

    slot->busy = true;
    wl_surface_attach(m_surface, slot->buffer, 0, 0);
    wl_surface_damage_buffer(m_surface, 0, 0, static_cast<int32_t>(hostW),
                             static_cast<int32_t>(hostH));
    wl_surface_commit(m_surface);
    wl_display_flush(m_dpy);
    return true;
  }

 private:
  struct BufferSlot {
    wl_buffer* buffer = nullptr;
    void* pixels = nullptr;
    size_t byteSize = 0;
    bool busy = false;
  };

  static void OnBufferRelease(void* data, wl_buffer* /*buffer*/) {
    auto* slot = static_cast<BufferSlot*>(data);
    if (slot) {
      slot->busy = false;
    }
  }

  bool AllocateBuffers(uint32_t w, uint32_t h) {
    DestroyBuffers();
    const uint32_t stride = w * 4;
    const size_t size = static_cast<size_t>(stride) * h;

    static const wl_buffer_listener kBufListener = {OnBufferRelease};

    for (auto& slot : m_slots) {
      int fd = memfd_create("glide_embed_test", MFD_CLOEXEC);
      if (fd < 0) {
        DestroyBuffers();
        return false;
      }
      if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        DestroyBuffers();
        return false;
      }
      void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (map == MAP_FAILED) {
        close(fd);
        DestroyBuffers();
        return false;
      }
      wl_shm_pool* pool =
          wl_shm_create_pool(m_shm, fd, static_cast<int32_t>(size));
      close(fd);
      if (!pool) {
        munmap(map, size);
        DestroyBuffers();
        return false;
      }
      wl_buffer* buf = wl_shm_pool_create_buffer(
          pool, 0, static_cast<int32_t>(w), static_cast<int32_t>(h),
          static_cast<int32_t>(stride), WL_SHM_FORMAT_XRGB8888);
      wl_shm_pool_destroy(pool);
      if (!buf) {
        munmap(map, size);
        DestroyBuffers();
        return false;
      }
      wl_buffer_add_listener(buf, &kBufListener, &slot);
      slot.buffer = buf;
      slot.pixels = map;
      slot.byteSize = size;
      slot.busy = false;
    }

    m_width = w;
    m_height = h;
    return true;
  }

  wl_display* m_dpy = nullptr;
  wl_shm* m_shm = nullptr;
  wl_surface* m_surface = nullptr;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  BufferSlot m_slots[2];
};
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

  // Commit initial parent frame (maps xdg_toplevel on Wayland immediately).
  PaintParentBackground(parentWin);

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
            << " initial_size=" << curW << "x" << curH
            << " scale=" << surfaceHost.GetWaylandScale() << "\n";
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

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  WaylandShmDrawer wlDrawer;
  if (surfaceHost.GetMode() == NativeSurfaceHost::Mode::WaylandSubsurface) {
    wlDrawer.Init(static_cast<wl_display*>(surfaceHost.GetWaylandDisplay()),
                  static_cast<wl_shm*>(surfaceHost.GetWaylandShm()),
                  static_cast<wl_surface*>(surfaceHost.GetWaylandSurface()));
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
      } else if (ev.type == SDL_WINDOWEVENT &&
                 (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                  ev.window.event == SDL_WINDOWEVENT_EXPOSED)) {
        PaintParentBackground(parentWin);
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
                << curW << "x" << curH
                << " (scale=" << surfaceHost.GetWaylandScale()
                << ", 4:3 PresentRect: x=" << dst.x << " y=" << dst.y << " "
                << dst.width << "x" << dst.height << ")\n"
                << std::flush;
      const int32_t scale = std::max<int32_t>(1, surfaceHost.GetWaylandScale());
      if (smokeResizeIssued && curW == static_cast<uint32_t>(1024 * scale) &&
          curH == static_cast<uint32_t>(600 * scale)) {
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

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
    if (surfaceHost.GetMode() == NativeSurfaceHost::Mode::WaylandSubsurface) {
      wlDrawer.DrawFrame(curW, curH, guestW, guestH, frameIdx, clickCount,
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
        PaintParentBackground(parentWin);
      }
      if (elapsedMs >= smokeMs) {
        running = false;
      }
    }

    SDL_Delay(16);
  }

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  wlDrawer.DestroyBuffers();
#endif

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
