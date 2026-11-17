#include "SoftwareBackend.h"
#include "SoftwareInternal.h"

#if !defined(_WIN32)
#include <SDL2/SDL.h>
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
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef None
#undef None
#endif
#ifdef Success
#undef Success
#endif
#ifdef Always
#undef Always
#endif
#ifdef Status
#undef Status
#endif
#ifdef Bool
#undef Bool
#endif
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#endif

namespace GlideWrapper {

namespace {

#if !defined(_WIN32)
// Fast CPU aspect-ratio preserving blit with opaque black letterbox/pillarbox
// bars into a 32-bit 0xXXRRGGBB (or 0xXXBBGGRR when swapRedBlue=true) surface.
void BlitScaledLetterbox(const uint32_t* srcPixels, uint32_t srcW,
                         uint32_t srcH, uint32_t* dstPixels,
                         uint32_t dstPitchWords, uint32_t surfaceW,
                         uint32_t surfaceH, bool swapRedBlue,
                         std::vector<uint32_t>& xLutScratch) {
  if (!srcPixels || !dstPixels || srcW == 0 || srcH == 0 || surfaceW == 0 ||
      surfaceH == 0 || dstPitchWords < surfaceW) {
    return;
  }

  const PresentRect rect =
      NativeSurfaceHost::ComputePresentRect(srcW, srcH, surfaceW, surfaceH,
                                            /*preserveAspect=*/true);
  const uint32_t dstX0 = static_cast<uint32_t>(std::max(0, rect.x));
  const uint32_t dstY0 = static_cast<uint32_t>(std::max(0, rect.y));
  const uint32_t dstW = std::min(rect.width, surfaceW - dstX0);
  const uint32_t dstH = std::min(rect.height, surfaceH - dstY0);
  const uint32_t dstX1 = dstX0 + dstW;
  const uint32_t dstY1 = dstY0 + dstH;

  constexpr uint32_t kOpaqueBlack = 0xFF000000u;

  // Top & bottom letterbox bars
  for (uint32_t y = 0; y < dstY0; ++y) {
    uint32_t* row = dstPixels + static_cast<size_t>(y) * dstPitchWords;
    std::fill_n(row, surfaceW, kOpaqueBlack);
  }
  for (uint32_t y = dstY1; y < surfaceH; ++y) {
    uint32_t* row = dstPixels + static_cast<size_t>(y) * dstPitchWords;
    std::fill_n(row, surfaceW, kOpaqueBlack);
  }

  if (dstW == 0 || dstH == 0) {
    return;
  }

  // Precompute horizontal source-pixel LUT once per frame.
  xLutScratch.resize(dstW);
  for (uint32_t x = 0; x < dstW; ++x) {
    uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(x) * srcW) /
                                        dstW);
    if (sx >= srcW) sx = srcW - 1;
    xLutScratch[x] = sx;
  }
  const uint32_t* xLut = xLutScratch.data();

#ifdef _OPENMP
#pragma omp parallel for if (dstH > 240)
#endif
  for (uint32_t y = 0; y < dstH; ++y) {
    uint32_t sy =
        static_cast<uint32_t>((static_cast<uint64_t>(y) * srcH) / dstH);
    if (sy >= srcH) sy = srcH - 1;
    const uint32_t* srcRow = srcPixels + static_cast<size_t>(sy) * srcW;
    uint32_t* dstRow =
        dstPixels + static_cast<size_t>(dstY0 + y) * dstPitchWords;

    if (dstX0 > 0) {
      std::fill_n(dstRow, dstX0, kOpaqueBlack);
    }
    if (dstX1 < surfaceW) {
      std::fill_n(dstRow + dstX1, surfaceW - dstX1, kOpaqueBlack);
    }

    uint32_t* out = dstRow + dstX0;
    if (!swapRedBlue) {
      for (uint32_t x = 0; x < dstW; ++x) {
        out[x] = srcRow[xLut[x]] | kOpaqueBlack;
      }
    } else {
      for (uint32_t x = 0; x < dstW; ++x) {
        const uint32_t px = srcRow[xLut[x]];
        const uint32_t r = (px >> 16) & 0xFFu;
        const uint32_t g = (px >> 8) & 0xFFu;
        const uint32_t b = px & 0xFFu;
        out[x] = kOpaqueBlack | (b << 16) | (g << 8) | r;
      }
    }
  }
}
#endif  // !defined(_WIN32)

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
std::mutex g_swX11TrapMutex;
bool g_swX11TrapHit = false;
unsigned char g_swX11ErrorCode = 0;
unsigned char g_swX11RequestCode = 0;
int HandleSwX11Error(Display* /*dpy*/, XErrorEvent* ev) {
  g_swX11TrapHit = true;
  if (ev) {
    g_swX11ErrorCode = ev->error_code;
    g_swX11RequestCode = ev->request_code;
  }
  return 0;
}

class ScopedSwX11Trap {
 public:
  ScopedSwX11Trap() : m_lock(g_swX11TrapMutex) {
    g_swX11TrapHit = false;
    g_swX11ErrorCode = 0;
    g_swX11RequestCode = 0;
    m_prev = XSetErrorHandler(HandleSwX11Error);
  }
  ~ScopedSwX11Trap() { XSetErrorHandler(m_prev); }
  bool Hit() const { return g_swX11TrapHit; }
  int ErrorCode() const { return static_cast<int>(g_swX11ErrorCode); }
  int RequestCode() const { return static_cast<int>(g_swX11RequestCode); }

 private:
  std::unique_lock<std::mutex> m_lock;
  XErrorHandler m_prev = nullptr;
};
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
int CreateAnonymousShmFile(size_t byteSize) {
  int fd = -1;
#if defined(__linux__) && defined(MFD_CLOEXEC)
  fd = ::memfd_create("glide-ng-wl-shm", MFD_CLOEXEC);
#endif
  if (fd < 0) {
    char tmpl[] = "/tmp/glide-ng-wl-shm-XXXXXX";
    fd = ::mkstemp(tmpl);
    if (fd >= 0) {
      ::unlink(tmpl);
      ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
  }
  if (fd < 0) {
    return -1;
  }

  int ret = -1;
  do {
    ret = ::ftruncate(fd, static_cast<off_t>(byteSize));
  } while (ret < 0 && errno == EINTR);
  if (ret < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}
#endif

}  // namespace

struct SoftwareBackend::X11Presenter {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  Display* display = nullptr;  // Borrowed from NativeSurfaceHost
  Window window = 0;           // Borrowed from NativeSurfaceHost
  GC gc = nullptr;
  Visual* visual = nullptr;
  int depth = 24;
  bool swapRedBlue = false;
  bool shmExtensionAvailable = false;

  XImage* image = nullptr;
  XShmSegmentInfo shmInfo{};
  bool usingShm = false;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint32_t> xLutScratch;

  bool Attach(Display* dpy, Window win, uint32_t w, uint32_t h) {
    Detach();
    if (!dpy || win == 0) return false;
    display = dpy;
    window = win;

    XWindowAttributes wa{};
    {
      ScopedSwX11Trap trap;
      if (!XGetWindowAttributes(display, window, &wa) || !wa.visual ||
          trap.Hit()) {
        GLIDE_LOG(CRITICAL, "Software",
                  "X11Presenter: XGetWindowAttributes failed.");
        Detach();
        return false;
      }
      visual = wa.visual;
      depth = wa.depth;
      swapRedBlue = (visual->red_mask == 0x0000FFUL);
      gc = XCreateGC(display, window, 0, nullptr);
      XSync(display, 0);
      if (!gc || trap.Hit()) {
        GLIDE_LOG(CRITICAL, "Software", "X11Presenter: XCreateGC failed.");
        Detach();
        return false;
      }
    }

    shmExtensionAvailable = (XShmQueryExtension(display) != 0);
    if (!EnsureSize(std::max<uint32_t>(1, w), std::max<uint32_t>(1, h))) {
      Detach();
      return false;
    }

    GLIDE_LOG(INFO, "Software",
              "X11Presenter attached (mode="
                  << (usingShm ? "XShmPutImage" : "XPutImage")
                  << ", visual=0x" << std::hex << XVisualIDFromVisual(visual)
                  << std::dec << ", class=" << visual->c_class
                  << ", depth=" << depth << ", bpp=" << image->bits_per_pixel
                  << ", size=" << width << "x" << height << ").");
    return true;
  }

  void DestroyImage() {
    if (!image) return;
    if (usingShm) {
      if (display) {
        ScopedSwX11Trap trap;
        XShmDetach(display, &shmInfo);
        XSync(display, 0);
      }
      image->data = nullptr;
      XDestroyImage(image);
      if (shmInfo.shmaddr && shmInfo.shmaddr != reinterpret_cast<char*>(-1)) {
        ::shmdt(shmInfo.shmaddr);
      }
      shmInfo = XShmSegmentInfo{};
      usingShm = false;
    } else {
      // XDestroyImage frees image->data allocated via std::malloc.
      XDestroyImage(image);
    }
    image = nullptr;
    width = 0;
    height = 0;
  }

  bool EnsureSize(uint32_t newW, uint32_t newH) {
    newW = std::max<uint32_t>(1, newW);
    newH = std::max<uint32_t>(1, newH);
    if (image && width == newW && height == newH) {
      return true;
    }
    DestroyImage();

    if (shmExtensionAvailable) {
      // CRITICAL: XShmCreateImage stores the pointer `&shmInfo` inside
      // `shmImg->obdata`, which XShmPutImage dereferences on every frame.
      // It MUST point to the persistent member `shmInfo`, never a stack local.
      shmInfo = XShmSegmentInfo{};
      shmInfo.shmid = -1;
      shmInfo.shmaddr = reinterpret_cast<char*>(-1);
      XImage* shmImg =
          XShmCreateImage(display, visual, static_cast<unsigned int>(depth),
                          ZPixmap, nullptr, &shmInfo, newW, newH);
      if (shmImg) {
        const size_t totalBytes =
            static_cast<size_t>(shmImg->bytes_per_line) * newH;
        shmInfo.shmid = ::shmget(IPC_PRIVATE, totalBytes, IPC_CREAT | 0600);
        if (shmInfo.shmid >= 0) {
          shmInfo.shmaddr =
              static_cast<char*>(::shmat(shmInfo.shmid, nullptr, 0));
          if (shmInfo.shmaddr != reinterpret_cast<char*>(-1)) {
            shmImg->data = shmInfo.shmaddr;
            shmInfo.readOnly = 0;
            bool attachedOk = false;
            {
              ScopedSwX11Trap trap;
              attachedOk = (XShmAttach(display, &shmInfo) != 0);
              XSync(display, 0);
              if (trap.Hit()) {
                attachedOk = false;
              }
            }
            // Mark for automatic kernel reclamation on last detach/exit.
            ::shmctl(shmInfo.shmid, IPC_RMID, nullptr);
            if (attachedOk) {
              image = shmImg;
              usingShm = true;
              width = newW;
              height = newH;
              return true;
            }
            ::shmdt(shmInfo.shmaddr);
          } else {
            ::shmctl(shmInfo.shmid, IPC_RMID, nullptr);
          }
        }
        shmImg->data = nullptr;
        XDestroyImage(shmImg);
      }
      shmInfo = XShmSegmentInfo{};
      // Disable XShm for subsequent resizes if attach was rejected (e.g., remote
      // X11 forwarding) and fall back cleanly to standard XPutImage.
      shmExtensionAvailable = false;
    }

    // Standard XPutImage fallback (heap-backed XImage).
    const size_t fallbackBytes = static_cast<size_t>(newW) * newH * 4;
    char* data = static_cast<char*>(std::malloc(fallbackBytes));
    if (!data) {
      GLIDE_LOG(CRITICAL, "Software",
                "X11Presenter: failed to allocate XImage buffer.");
      return false;
    }
    XImage* plainImg =
        XCreateImage(display, visual, static_cast<unsigned int>(depth), ZPixmap,
                     0, data, newW, newH, 32, 0);
    if (!plainImg) {
      std::free(data);
      GLIDE_LOG(CRITICAL, "Software", "X11Presenter: XCreateImage failed.");
      return false;
    }
    const size_t requiredBytes =
        static_cast<size_t>(plainImg->bytes_per_line) * newH;
    if (requiredBytes > fallbackBytes) {
      char* resized = static_cast<char*>(std::realloc(data, requiredBytes));
      if (!resized) {
        plainImg->data = data;
        XDestroyImage(plainImg);
        return false;
      }
      plainImg->data = resized;
    }
    image = plainImg;
    usingShm = false;
    width = newW;
    height = newH;
    return true;
  }

  void Present(const uint32_t* srcPixels, uint32_t srcW, uint32_t srcH,
               uint32_t hostW, uint32_t hostH) {
    if (!display || !gc || window == 0) return;
    if (!EnsureSize(hostW, hostH) || !image || !image->data) return;

    if (image->bits_per_pixel == 32 && image->byte_order == LSBFirst &&
        (image->bytes_per_line % 4) == 0) {
      BlitScaledLetterbox(
          srcPixels, srcW, srcH, reinterpret_cast<uint32_t*>(image->data),
          static_cast<uint32_t>(image->bytes_per_line / 4), width, height,
          swapRedBlue, xLutScratch);
    } else {
      // Exotic X11 depth/endianness fallback via XPutPixel.
      const PresentRect rect =
          NativeSurfaceHost::ComputePresentRect(srcW, srcH, width, height,
                                                /*preserveAspect=*/true);
      for (uint32_t y = 0; y < height; ++y) {
        const bool inY =
            (static_cast<int32_t>(y) >= rect.y &&
             static_cast<int32_t>(y) < rect.y + static_cast<int32_t>(rect.height));
        uint32_t sy = 0;
        if (inY && rect.height > 0) {
          sy = static_cast<uint32_t>(
              (static_cast<uint64_t>(y - static_cast<uint32_t>(rect.y)) *
               srcH) /
              rect.height);
          if (sy >= srcH) sy = srcH - 1;
        }
        for (uint32_t x = 0; x < width; ++x) {
          unsigned long pxVal = 0;
          if (inY && static_cast<int32_t>(x) >= rect.x &&
              static_cast<int32_t>(x) <
                  rect.x + static_cast<int32_t>(rect.width) &&
              rect.width > 0) {
            uint32_t sx = static_cast<uint32_t>(
                (static_cast<uint64_t>(x - static_cast<uint32_t>(rect.x)) *
                 srcW) /
                rect.width);
            if (sx >= srcW) sx = srcW - 1;
            pxVal = srcPixels[static_cast<size_t>(sy) * srcW + sx] & 0xFFFFFFUL;
          }
          XPutPixel(image, static_cast<int>(x), static_cast<int>(y), pxVal);
        }
      }
    }

    bool shmFailed = false;
    int errCode = 0;
    int reqCode = 0;
    {
      ScopedSwX11Trap trap;
      if (usingShm) {
        XShmPutImage(display, window, gc, image, 0, 0, 0, 0, width, height, 0);
        // Wait for X server copy completion before next frame reuses SHM buffer.
        XSync(display, 0);
        if (trap.Hit()) {
          shmFailed = true;
          errCode = trap.ErrorCode();
          reqCode = trap.RequestCode();
        }
      } else {
        XPutImage(display, window, gc, image, 0, 0, 0, 0, width, height);
        XFlush(display);
        if (trap.Hit()) {
          GLIDE_LOG(WARN, "Software",
                    "X11Presenter: XPutImage failed (err="
                        << trap.ErrorCode() << ", req=" << trap.RequestCode()
                        << ").");
        }
      }
    }
    if (shmFailed) {
      GLIDE_LOG(WARN, "Software",
                "X11Presenter: XShmPutImage failed (err="
                    << errCode << ", req=" << reqCode
                    << "); falling back to XPutImage.");
      shmExtensionAvailable = false;
      DestroyImage();
      if (EnsureSize(hostW, hostH) && image && image->data) {
        BlitScaledLetterbox(
            srcPixels, srcW, srcH, reinterpret_cast<uint32_t*>(image->data),
            static_cast<uint32_t>(image->bytes_per_line / 4), width, height,
            swapRedBlue, xLutScratch);
        ScopedSwX11Trap fallbackTrap;
        XPutImage(display, window, gc, image, 0, 0, 0, 0, width, height);
        XFlush(display);
      }
    }
  }

  void Detach() {
    DestroyImage();
    if (display && gc) {
      ScopedSwX11Trap trap;
      XFreeGC(display, gc);
      XSync(display, 0);
    }
    gc = nullptr;
    visual = nullptr;
    display = nullptr;
    window = 0;
    shmExtensionAvailable = false;
  }

  ~X11Presenter() { Detach(); }
#endif
};

struct SoftwareBackend::WaylandShmPresenter {
#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  struct ShmSlot {
    wl_buffer* buffer = nullptr;
    uint32_t* pixels = nullptr;
    size_t byteSize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool busy = false;
  };

  static constexpr size_t kNumSlots = 3;

  wl_display* display = nullptr;      // Borrowed from NativeSurfaceHost
  wl_surface* surface = nullptr;      // Borrowed from NativeSurfaceHost
  wl_shm* shm = nullptr;              // Borrowed from NativeSurfaceHost
  wl_event_queue* queue = nullptr;    // Borrowed from NativeSurfaceHost
  std::array<std::unique_ptr<ShmSlot>, kNumSlots> slots;
  std::vector<std::unique_ptr<ShmSlot>> retired;
  std::vector<uint32_t> xLutScratch;

  static void OnBufferRelease(void* data, wl_buffer* /*buffer*/) {
    auto* slot = static_cast<ShmSlot*>(data);
    if (slot) {
      slot->busy = false;
    }
  }

  static void DestroySlot(std::unique_ptr<ShmSlot>& slot) {
    if (!slot) return;
    if (slot->buffer) {
      wl_buffer_destroy(slot->buffer);
      slot->buffer = nullptr;
    }
    if (slot->pixels && slot->byteSize > 0) {
      ::munmap(slot->pixels, slot->byteSize);
      slot->pixels = nullptr;
      slot->byteSize = 0;
    }
    slot.reset();
  }

  std::unique_ptr<ShmSlot> CreateSlot(uint32_t w, uint32_t h) {
    if (!shm || w == 0 || h == 0) return nullptr;
    const uint32_t stride = w * 4;
    const size_t byteSize = static_cast<size_t>(stride) * h;

    const int fd = CreateAnonymousShmFile(byteSize);
    if (fd < 0) {
      GLIDE_LOG(CRITICAL, "Software",
                "WaylandShmPresenter: failed to create SHM fd.");
      return nullptr;
    }

    void* map =
        ::mmap(nullptr, byteSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      ::close(fd);
      GLIDE_LOG(CRITICAL, "Software", "WaylandShmPresenter: mmap failed.");
      return nullptr;
    }

    wl_shm_pool* pool =
        wl_shm_create_pool(shm, fd, static_cast<int32_t>(byteSize));
    ::close(fd);
    if (!pool) {
      ::munmap(map, byteSize);
      GLIDE_LOG(CRITICAL, "Software",
                "WaylandShmPresenter: wl_shm_create_pool failed.");
      return nullptr;
    }

    wl_buffer* buf = wl_shm_pool_create_buffer(
        pool, 0, static_cast<int32_t>(w), static_cast<int32_t>(h),
        static_cast<int32_t>(stride), WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!buf) {
      ::munmap(map, byteSize);
      GLIDE_LOG(CRITICAL, "Software",
                "WaylandShmPresenter: wl_shm_pool_create_buffer failed.");
      return nullptr;
    }

    static const wl_buffer_listener kBufferListener = {OnBufferRelease};
    auto slot = std::make_unique<ShmSlot>();
    slot->buffer = buf;
    slot->pixels = static_cast<uint32_t*>(map);
    slot->byteSize = byteSize;
    slot->width = w;
    slot->height = h;
    slot->busy = false;
    wl_buffer_add_listener(slot->buffer, &kBufferListener, slot.get());
    return slot;
  }

  void ReapRetired() {
    auto it = retired.begin();
    while (it != retired.end()) {
      if (!(*it) || !(*it)->busy) {
        DestroySlot(*it);
        it = retired.erase(it);
      } else {
        ++it;
      }
    }
  }

  bool Attach(wl_display* dpy, wl_surface* surf, wl_shm* shmGlobal,
              wl_event_queue* evQueue) {
    Detach();
    if (!dpy || !surf || !shmGlobal) {
      GLIDE_LOG(CRITICAL, "Software",
                "WaylandShmPresenter: missing wl_display, wl_surface, or "
                "wl_shm.");
      return false;
    }
    display = dpy;
    surface = surf;
    shm = shmGlobal;
    queue = evQueue;
    GLIDE_LOG(INFO, "Software",
              "WaylandShmPresenter attached (format=XRGB8888, slots="
                  << kNumSlots << ").");
    return true;
  }

  void Present(const uint32_t* srcPixels, uint32_t srcW, uint32_t srcH,
               uint32_t hostW, uint32_t hostH) {
    if (!display || !surface || !shm) return;
    hostW = std::max<uint32_t>(1, hostW);
    hostH = std::max<uint32_t>(1, hostH);

    if (queue) {
      wl_display_dispatch_queue_pending(display, queue);
    }
    ReapRetired();

    // Locate a free slot; if all slots are busy, flush our private queue once
    // to process any pending wl_buffer.release events.
    auto findFreeSlot = [&]() -> ShmSlot* {
      for (auto& s : slots) {
        if (!s) {
          s = CreateSlot(hostW, hostH);
          return s.get();
        }
        if (!s->busy) {
          if (s->width != hostW || s->height != hostH) {
            DestroySlot(s);
            s = CreateSlot(hostW, hostH);
          }
          return s.get();
        }
      }
      return nullptr;
    };

    ShmSlot* target = findFreeSlot();
    if (!target && queue) {
      wl_display_roundtrip_queue(display, queue);
      ReapRetired();
      target = findFreeSlot();
    }
    if (!target) {
      // All slots are still held by the compositor: retire slot 0 (freed once
      // its release event arrives) and allocate a fresh buffer of the target
      // dimensions rather than overwriting in-flight scanout memory.
      if (slots[0]) {
        retired.push_back(std::move(slots[0]));
      }
      slots[0] = CreateSlot(hostW, hostH);
      target = slots[0].get();
    }
    if (!target || !target->buffer || !target->pixels) {
      return;
    }

    BlitScaledLetterbox(srcPixels, srcW, srcH, target->pixels, target->width,
                        target->width, target->height, /*swapRedBlue=*/false,
                        xLutScratch);

    target->busy = true;
    wl_surface_attach(surface, target->buffer, 0, 0);
#ifdef WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION
    if (wl_surface_get_version(surface) >=
        WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
      wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
    } else
#endif
    {
      wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
    }
    wl_surface_commit(surface);
    wl_display_flush(display);
  }

  void Detach() {
    for (auto& s : slots) {
      DestroySlot(s);
    }
    for (auto& r : retired) {
      DestroySlot(r);
    }
    retired.clear();
    display = nullptr;
    surface = nullptr;
    shm = nullptr;
    queue = nullptr;
  }

  ~WaylandShmPresenter() { Detach(); }
#endif
};

struct SoftwareBackend::Win32GdiPresenter {
#if defined(_WIN32)
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  HBRUSH blackBrush = nullptr;
  bool linearFilter = false;

  bool Attach(HWND targetHwnd, bool useLinear) {
    Detach();
    if (!targetHwnd || !IsWindow(targetHwnd)) return false;
    hdc = GetDC(targetHwnd);
    if (!hdc) return false;
    hwnd = targetHwnd;
    linearFilter = useLinear;
    blackBrush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    return true;
  }

  void Present(const uint32_t* srcPixels, uint32_t srcW, uint32_t srcH,
               uint32_t hostW, uint32_t hostH) {
    if (!hdc || !srcPixels || srcW == 0 || srcH == 0 || hostW == 0 ||
        hostH == 0) {
      return;
    }
    const PresentRect rect = NativeSurfaceHost::ComputePresentRect(
        srcW, srcH, hostW, hostH, /*preserveAspect=*/true);

    // Fill letterbox / pillarbox bars cleanly without flickering the active rect.
    if (rect.x > 0) {
      RECT leftBar{0, 0, rect.x, static_cast<LONG>(hostH)};
      RECT rightBar{rect.x + static_cast<LONG>(rect.width), 0,
                    static_cast<LONG>(hostW), static_cast<LONG>(hostH)};
      FillRect(hdc, &leftBar, blackBrush);
      FillRect(hdc, &rightBar, blackBrush);
    } else if (rect.y > 0) {
      RECT topBar{0, 0, static_cast<LONG>(hostW), rect.y};
      RECT botBar{0, rect.y + static_cast<LONG>(rect.height),
                  static_cast<LONG>(hostW), static_cast<LONG>(hostH)};
      FillRect(hdc, &topBar, blackBrush);
      FillRect(hdc, &botBar, blackBrush);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(srcW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(srcH);  // top-down 0xAARRGGBB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, linearFilter ? HALFTONE : COLORONCOLOR);
    if (linearFilter) {
      SetBrushOrgEx(hdc, 0, 0, nullptr);
    }
    StretchDIBits(hdc, rect.x, rect.y, static_cast<int>(rect.width),
                  static_cast<int>(rect.height), 0, 0, static_cast<int>(srcW),
                  static_cast<int>(srcH), srcPixels, &bmi, DIB_RGB_COLORS,
                  SRCCOPY);
  }

  void Detach() {
    if (hdc && hwnd) {
      ReleaseDC(hwnd, hdc);
    }
    hdc = nullptr;
    hwnd = nullptr;
    blackBrush = nullptr;
  }

  ~Win32GdiPresenter() { Detach(); }
#endif
};

SoftwareBackend::SoftwareBackend() = default;
SoftwareBackend::~SoftwareBackend() { Shutdown(); }

bool SoftwareBackend::Initialize(const WrapperConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) return true;

  m_config = config;
  ResetState_Locked();

  m_useSimd = m_config.swUseSimd;
#if defined(__AVX2__) && (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
  if (m_useSimd && !__builtin_cpu_supports("avx2")) {
    GLIDE_LOG(WARN, "Software",
              "Host CPU lacks AVX2 support; falling back to scalar software "
              "rasterizer.");
    m_useSimd = false;
  }
#endif
  if (m_useSimd) {
    GLIDE_LOG(INFO, "Software",
              "SIMD optimization hook is ACTIVE (GLIDE_NG_SW_SIMD=1).");
  } else {
    GLIDE_LOG(INFO, "Software",
              "SIMD optimization hook is inactive. Using scalar reference "
              "rasterizer.");
  }

  int logicalCores =
      std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  int numThreads = (m_config.swThreads > 0)
                       ? static_cast<int>(m_config.swThreads)
                       : logicalCores;
  m_threadPool = std::make_unique<ThreadPool>(numThreads - 1);
  GLIDE_LOG(INFO, "Software",
            "Initialized Lock-Free Thread Pool with "
                << (numThreads - 1)
                << " worker threads (Total threads: " << numThreads << ").");

  uint32_t rawTileSize = std::max<uint32_t>(16, m_config.swTileSize);
  m_tileSize = 1;
  m_tileShift = 0;
  while (m_tileSize < rawTileSize) {
    m_tileSize <<= 1;
    m_tileShift++;
  }
  GLIDE_LOG(INFO, "Software",
            "Configured Software Rasterizer Tile Size to "
                << m_tileSize << "x" << m_tileSize << " (shift " << m_tileShift
                << ").");

  m_initialized = true;
  GLIDE_LOG(INFO, "Software",
            "Pure Software translation backend initialized successfully.");
  return true;
}

void SoftwareBackend::Shutdown() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) return;

  GLIDE_LOG(INFO, "Software", "Shutting down Pure Software backend.");
  DetachWindow_Locked();
  m_threadPool.reset();

  m_initialized = false;
}

bool SoftwareBackend::InitPresentationSurface() {
  const auto mode = m_surfaceHost.GetMode();
  if (mode == NativeSurfaceHost::Mode::Headless) {
    return true;
  }

#if !defined(_WIN32)
  if (mode == NativeSurfaceHost::Mode::StandaloneSDL2) {
    SDL_Window* win = m_surfaceHost.GetSDLWindow();
    if (!win) {
      GLIDE_LOG(CRITICAL, "Software",
                "StandaloneSDL2 mode missing SDL_Window.");
      return false;
    }
    const bool usePresentVsync =
        m_config.vsync &&
        (!m_surfaceHost.IsWaylandSurface() || m_config.vsyncStrictFifo);
    Uint32 flags = SDL_RENDERER_ACCELERATED |
                   (usePresentVsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, flags);
    if (!renderer) {
      renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
      GLIDE_LOG(CRITICAL, "Software",
                "Failed to create StandaloneSDL2 SDL_Renderer: "
                    << SDL_GetError());
      return false;
    }
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(m_guestWidth), static_cast<int>(m_guestHeight));
    if (!texture) {
      GLIDE_LOG(CRITICAL, "Software",
                "Failed to create StandaloneSDL2 SDL_Texture: "
                    << SDL_GetError());
      SDL_DestroyRenderer(renderer);
      return false;
    }
    m_sdlRenderer = renderer;
    m_sdlTexture = texture;
    return true;
  }
#endif

#if defined(_WIN32)
  if (mode == NativeSurfaceHost::Mode::Win32Window ||
      mode == NativeSurfaceHost::Mode::Win32Embedded) {
    HWND hwnd = static_cast<HWND>(m_surfaceHost.GetWin32Hwnd());
    if (!hwnd) {
      GLIDE_LOG(CRITICAL, "Software",
                "Win32 presentation surface missing HWND.");
      return false;
    }
    m_win32Presenter = std::make_unique<Win32GdiPresenter>();
    if (!m_win32Presenter->Attach(hwnd, m_config.presentationFilter != 0)) {
      m_win32Presenter.reset();
      return false;
    }
    return true;
  }
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (mode == NativeSurfaceHost::Mode::X11Child) {
    Display* x11Dpy = static_cast<Display*>(m_surfaceHost.GetX11Display());
    Window x11Win = static_cast<Window>(m_surfaceHost.GetX11Window());
    if (x11Dpy && x11Win != 0) {
      XWindowAttributes wa{};
      bool needTrueColor = false;
      unsigned long inheritedVisualId = 0;
      int inheritedClass = -1;
      {
        ScopedSwX11Trap trap;
        if (XGetWindowAttributes(x11Dpy, x11Win, &wa) && !trap.Hit() &&
            wa.visual) {
          inheritedVisualId =
              static_cast<unsigned long>(XVisualIDFromVisual(wa.visual));
          inheritedClass = wa.visual->c_class;
          // XShmPutImage/XPutImage write direct RGB values; on DirectColor
          // visuals (commonly inherited from SDL2 on XWayland), an unpopulated
          // colormap maps every pixel through an all-zero LUT -> pure black.
          needTrueColor = (wa.visual->c_class != TrueColor);
        }
      }
      if (needTrueColor) {
        const int screen = DefaultScreen(x11Dpy);
        XVisualInfo vinfo{};
        unsigned long trueColorVisualId = 0;
        if (XMatchVisualInfo(x11Dpy, screen, 24, TrueColor, &vinfo)) {
          trueColorVisualId = static_cast<unsigned long>(vinfo.visualid);
        } else if (XMatchVisualInfo(x11Dpy, screen, 32, TrueColor, &vinfo)) {
          trueColorVisualId = static_cast<unsigned long>(vinfo.visualid);
        } else {
          Visual* defVis = DefaultVisual(x11Dpy, screen);
          if (defVis && defVis->c_class == TrueColor) {
            trueColorVisualId =
                static_cast<unsigned long>(XVisualIDFromVisual(defVis));
          }
        }
        if (trueColorVisualId != 0) {
          GLIDE_LOG(INFO, "Software",
                    "X11Child inherited non-TrueColor visual 0x"
                        << std::hex << inheritedVisualId << std::dec
                        << " (class=" << inheritedClass
                        << "); recreating X11Child with TrueColor visual 0x"
                        << std::hex << trueColorVisualId << std::dec << ".");
          if (m_surfaceHost.RecreateX11ChildForVisual(trueColorVisualId)) {
            x11Win = static_cast<Window>(m_surfaceHost.GetX11Window());
          }
        }
      }
    }
    uint32_t pixW = m_guestWidth;
    uint32_t pixH = m_guestHeight;
    m_surfaceHost.GetDrawableSize(pixW, pixH);
    m_x11Presenter = std::make_unique<X11Presenter>();
    if (!m_x11Presenter->Attach(x11Dpy, x11Win, pixW, pixH)) {
      m_x11Presenter.reset();
      return false;
    }
    return true;
  }
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  if (mode == NativeSurfaceHost::Mode::WaylandSubsurface) {
    m_wlShmPresenter = std::make_unique<WaylandShmPresenter>();
    if (!m_wlShmPresenter->Attach(
            static_cast<wl_display*>(m_surfaceHost.GetWaylandDisplay()),
            static_cast<wl_surface*>(m_surfaceHost.GetWaylandSurface()),
            static_cast<wl_shm*>(m_surfaceHost.GetWaylandShm()),
            static_cast<wl_event_queue*>(
                m_surfaceHost.GetWaylandEventQueue()))) {
      m_wlShmPresenter.reset();
      return false;
    }
    return true;
  }
#endif

  GLIDE_LOG(CRITICAL, "Software",
            "Unsupported NativeSurfaceHost mode="
                << NativeSurfaceHost::ModeToString(mode) << ".");
  return false;
}

void SoftwareBackend::ShutdownPresentationSurface() {
#if !defined(_WIN32)
  if (m_sdlTexture) {
    SDL_DestroyTexture(static_cast<SDL_Texture*>(m_sdlTexture));
    m_sdlTexture = nullptr;
  }
  if (m_sdlRenderer) {
    SDL_DestroyRenderer(static_cast<SDL_Renderer*>(m_sdlRenderer));
    m_sdlRenderer = nullptr;
  }
#endif
  m_x11Presenter.reset();
  m_wlShmPresenter.reset();
  m_win32Presenter.reset();
}

bool SoftwareBackend::AttachWindow(void* nativeWindowHandle, uint32_t width,
                                   uint32_t height, bool windowed) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) return false;
  if (m_windowAttached) DetachWindow_Locked();

  GLIDE_LOG(INFO, "Software",
            "Attaching software execution window ("
                << width << "x" << height
                << "), Windowed=" << (windowed ? "Yes" : "No"));

#ifdef _OPENMP
  if (getenv("OMP_NUM_THREADS") == nullptr) {
    omp_set_num_threads(4);
  }
#endif

  ResolveAAMode(m_config.swAaMode);

  uint32_t samplesVal = m_ssaaScale * m_ssaaScale;
  if (samplesVal == 1) samplesVal = 0;

  std::cout << "Info: InitialiseSoftwareWindow(wnd=" << nativeWindowHandle
            << ", res=" << width << "x" << height << ")\r\n";
  std::cout << "Info: Host Software Adapter: pure-software reference "
               "rasterizer (glide-ng Software Reference Core)\r\n";
  std::cout << "Info: Pixel Format RGBA8888 D32S0 nAux 0 nSamples "
            << samplesVal << " " << samplesVal << "\r\n";
  std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n"
            << std::flush;
  m_headlessWidth = width * m_ssaaScale;
  m_headlessHeight = height * m_ssaaScale;
  m_guestWidth = width;
  m_guestHeight = height;
  m_headlessMode = true;

  // Allocate the CPU framebuffer double buffers (scaled for SSAA!)
  AllocateCpuBuffers_Locked(m_headlessWidth, m_headlessHeight);
  m_allocatedBuffer = true;

  size_t rawDepthSize = (size_t)m_headlessWidth * m_headlessHeight * 4;
  size_t paddedDepthSize = (rawDepthSize + 63) & ~63;
  size_t allocatedDepthSize = paddedDepthSize + 64;
  m_headlessDepthBuffer.resize(allocatedDepthSize / sizeof(float), 0.0f);
  SetClipWindow_Locked(0, 0, m_headlessWidth, m_headlessHeight);
  InitTileGrid();

  m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();

  if (!m_surfaceHost.Attach(nativeWindowHandle, width, height, windowed,
                            m_config.forceNoWindow,
                            "3dfx glide-ng Presentation Console (Software)", 0,
                            m_config.windowScale)) {
    GLIDE_LOG(CRITICAL, "Software",
              "NativeSurfaceHost::Attach failed for SoftwareBackend.");
    FreeCpuBuffers_Locked();
    m_allocatedBuffer = false;
    m_headlessMode = false;
    return false;
  }

  if (!InitPresentationSurface()) {
    ShutdownPresentationSurface();
    m_surfaceHost.Detach();
    FreeCpuBuffers_Locked();
    m_allocatedBuffer = false;
    m_headlessMode = false;
    return false;
  }

  unsigned int cores = std::thread::hardware_concurrency();
#if defined(__x86_64__) || defined(_M_X64)
  const char* arch = "x86_64";
#elif defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
  const char* arch = "arm64";
#else
  const char* arch = "Unknown CPU Architecture";
#endif

  std::ostringstream logStream;
  logStream << "\n--- Active Software Execution Adapter ---\n"
            << "  Adapter     : Host CPU Reference Rasterizer (Native "
            << NativeSurfaceHost::ModeToString(m_surfaceHost.GetMode())
            << ")\n"
            << "    Cores     : " << cores << " Hardware Threads\n"
            << "    Arch      : " << arch << "\n"
            << "    Engine    : glide-ng Software Reference Core (v0.1.0)\n"
            << "    AA Mode   : " << m_softwareAaMode << " (" << m_ssaaScale
            << "x scale)\n"
            << "------------------------------------------";
  GLIDE_LOG(INFO, "Software", logStream.str());

  m_hudOverlay.Configure(m_config, {"SW", width, height});
  m_hudPresentBuffer.clear();

  m_windowAttached = true;
  return true;
}

void SoftwareBackend::DetachWindow() {
  std::lock_guard<std::mutex> lock(m_mutex);
  DetachWindow_Locked();
}

void SoftwareBackend::DetachWindow_Locked() {
  if (!m_windowAttached) return;

  // Strict WSI teardown order: destroy presenter objects BEFORE detaching the
  // underlying X11 child window / Wayland subsurface / SDL_Window.
  ShutdownPresentationSurface();
  m_surfaceHost.Detach();

  if (m_allocatedBuffer) {
    FreeCpuBuffers_Locked();
    m_allocatedBuffer = false;
  }

  m_hudOverlay.Reset();
  m_hudPresentBuffer.clear();

  m_windowAttached = false;
  m_headlessMode = false;
  GLIDE_LOG(INFO, "Software", "Detached Software Backend.");
}

void SoftwareBackend::ResolveFrontBuffer(const uint32_t*& outPixels,
                                         uint32_t& outW, uint32_t& outH) {
  const uint32_t* fullSrc =
      reinterpret_cast<const uint32_t*>(m_cpuBuffers[m_frontBufferIdx].data());
  if (m_ssaaScale <= 1) {
    outPixels = fullSrc;
    outW = m_headlessWidth;
    outH = m_headlessHeight;
    return;
  }

  const uint32_t dstW = m_headlessWidth / m_ssaaScale;
  const uint32_t dstH = m_headlessHeight / m_ssaaScale;
  m_resolvedBuffer.resize(static_cast<size_t>(dstW) * dstH * 4);
  uint32_t* resolvedData =
      reinterpret_cast<uint32_t*>(m_resolvedBuffer.data());

#ifdef _OPENMP
#pragma omp parallel for collapse(2) if (dstH * dstW > 64000)
#endif
  for (uint32_t y = 0; y < dstH; ++y) {
    for (uint32_t x = 0; x < dstW; ++x) {
      uint32_t r = 0, g = 0, b = 0, a = 0;
      for (uint32_t dy = 0; dy < 2; ++dy) {
        for (uint32_t dx = 0; dx < 2; ++dx) {
          const uint32_t srcIdx =
              (2 * y + dy) * m_headlessWidth + (2 * x + dx);
          const uint32_t p = fullSrc[srcIdx];
          a += (p >> 24) & 0xFFu;
          r += (p >> 16) & 0xFFu;
          g += (p >> 8) & 0xFFu;
          b += p & 0xFFu;
        }
      }
      resolvedData[static_cast<size_t>(y) * dstW + x] =
          ((a / 4) << 24) | ((r / 4) << 16) | ((g / 4) << 8) | (b / 4);
    }
  }

  outPixels = resolvedData;
  outW = dstW;
  outH = dstH;
}

void SoftwareBackend::PresentFrame(const uint32_t* srcPixels, uint32_t srcW,
                                   uint32_t srcH) {
  const auto mode = m_surfaceHost.GetMode();
  if (mode == NativeSurfaceHost::Mode::Headless || !srcPixels || srcW == 0 ||
      srcH == 0) {
    return;
  }

  uint32_t hostW = m_guestWidth;
  uint32_t hostH = m_guestHeight;
  m_surfaceHost.SyncBounds(hostW, hostH);

#if !defined(_WIN32)
  if (mode == NativeSurfaceHost::Mode::StandaloneSDL2) {
    if (!m_sdlRenderer || !m_sdlTexture) return;
    GLIDE_PROFILE_SCOPE("Software::Sdl2Present");
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(m_sdlRenderer);
    SDL_Texture* texture = static_cast<SDL_Texture*>(m_sdlTexture);

    void* texturePixels = nullptr;
    int texturePitch = 0;
    if (SDL_LockTexture(texture, nullptr, &texturePixels, &texturePitch) == 0) {
      uint32_t* dstPixels = static_cast<uint32_t*>(texturePixels);
      const uint32_t dstPitchWords = static_cast<uint32_t>(texturePitch / 4);
      const uint32_t copyW = std::min(srcW, m_guestWidth);
      const uint32_t copyH = std::min(srcH, m_guestHeight);
      for (uint32_t y = 0; y < copyH; ++y) {
        const uint32_t* srcRow = srcPixels + static_cast<size_t>(y) * srcW;
        uint32_t* dstRow = dstPixels + static_cast<size_t>(y) * dstPitchWords;
        for (uint32_t x = 0; x < copyW; ++x) {
          dstRow[x] = srcRow[x] | 0xFF000000u;
        }
      }
      SDL_UnlockTexture(texture);

      int outW = static_cast<int>(hostW);
      int outH = static_cast<int>(hostH);
      SDL_GetRendererOutputSize(renderer, &outW, &outH);
      const PresentRect rect =
          NativeSurfaceHost::ComputePresentRect(
              srcW, srcH, static_cast<uint32_t>(std::max(1, outW)),
              static_cast<uint32_t>(std::max(1, outH)),
              /*preserveAspect=*/true);
      const SDL_Rect dstRect{rect.x, rect.y, static_cast<int>(rect.width),
                             static_cast<int>(rect.height)};

      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, nullptr, &dstRect);
      SDL_RenderPresent(renderer);
    } else {
      GLIDE_LOG(WARN, "Software",
                "Failed to lock SDL_Texture: " << SDL_GetError());
    }
    return;
  }
#endif

#if defined(_WIN32)
  if ((mode == NativeSurfaceHost::Mode::Win32Window ||
       mode == NativeSurfaceHost::Mode::Win32Embedded) &&
      m_win32Presenter) {
    GLIDE_PROFILE_SCOPE("Software::Win32GdiPresent");
    m_win32Presenter->Present(srcPixels, srcW, srcH, hostW, hostH);
    return;
  }
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)
  if (mode == NativeSurfaceHost::Mode::X11Child && m_x11Presenter) {
    GLIDE_PROFILE_SCOPE("Software::X11Present");
    m_x11Presenter->Present(srcPixels, srcW, srcH, hostW, hostH);
    return;
  }
#endif

#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)
  if (mode == NativeSurfaceHost::Mode::WaylandSubsurface && m_wlShmPresenter) {
    GLIDE_PROFILE_SCOPE("Software::WaylandShmPresent");
    m_wlShmPresenter->Present(srcPixels, srcW, srcH, hostW, hostH);
    return;
  }
#endif
}

bool SoftwareBackend::SwapBuffers() {
  std::unique_lock<std::mutex> lock(m_mutex);
  FlushBins_Locked();
  if (!m_initialized || !m_windowAttached) return false;

  // Strictly no-op for foreign hosts (X11Child / WaylandSubsurface /
  // Win32Embedded / Headless); pumps events and handles clean quit ONLY for
  // standalone windows.
  m_surfaceHost.PollEvents(/*force=*/false);
  if (m_surfaceHost.IsStandaloneMode() && m_surfaceHost.WasQuitRequested()) {
    GLIDE_LOG(INFO, "Software",
              "Standalone window quit requested; shutting down cleanly.");
    DetachWindow_Locked();
    lock.unlock();
    std::exit(0);
  }

  GLIDE_LOG(DEBUG, "Software", "Executing SwapBuffers presentation dispatch.");

  GLIDE_PROFILE_SCOPE("Software::SwapBuffers");

  const auto presentStart = std::chrono::steady_clock::now();

  // 1. Swap the front and back CPU buffer indices
  std::swap(m_frontBufferIdx, m_backBufferIdx);

  // 2. Update m_headlessPixelMap to point to the active render buffer's memory
  if (m_activeRenderBuffer == 0) {  // GR_BUFFER_FRONTBUFFER
    m_headlessPixelMap = m_cpuBuffers[m_frontBufferIdx].data();
  } else {  // GR_BUFFER_BACKBUFFER (default)
    m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
  }

  // 2.5 Apply CPU-level Gamma LUT in-place on the front buffer if active
  if (m_useGammaLut && !m_cpuBuffers[m_frontBufferIdx].empty()) {
    auto* pixels =
        reinterpret_cast<uint32_t*>(m_cpuBuffers[m_frontBufferIdx].data());
    int totalPixels = m_headlessWidth * m_headlessHeight;
#ifdef _OPENMP
#pragma omp parallel for if (totalPixels > 64000)
#endif
    for (int i = 0; i < totalPixels; ++i) {
      uint32_t pixel = pixels[i];
      uint8_t a = (pixel >> 24) & 0xFF;
      uint8_t r = (pixel >> 16) & 0xFF;
      uint8_t g = (pixel >> 8) & 0xFF;
      uint8_t b = pixel & 0xFF;
      pixels[i] = (a << 24) | (m_lutR[r] << 16) | (m_lutG[g] << 8) | m_lutB[b];
    }
  }

  // 3. Present to active native surface (no-op in Headless mode).
  if (m_surfaceHost.GetMode() != NativeSurfaceHost::Mode::Headless &&
      !m_cpuBuffers[m_frontBufferIdx].empty()) {
    const uint32_t* srcPixels = nullptr;
    uint32_t srcW = 0;
    uint32_t srcH = 0;
    ResolveFrontBuffer(srcPixels, srcW, srcH);
    if (m_hudOverlay.IsEnabled() && srcPixels && srcW > 0 && srcH > 0) {
      m_hudOverlay.Update(TelemetryManager::GetInstance().GetLastFrameMetrics());
      const size_t pixelCount = static_cast<size_t>(srcW) * srcH;
      if (m_hudPresentBuffer.size() != pixelCount) {
        m_hudPresentBuffer.resize(pixelCount);
      }
      std::memcpy(m_hudPresentBuffer.data(), srcPixels,
                  pixelCount * sizeof(uint32_t));
      m_hudOverlay.CompositeOverBgra32(m_hudPresentBuffer.data(), srcW, srcH,
                                       srcW);
      PresentFrame(m_hudPresentBuffer.data(), srcW, srcH);
    } else {
      PresentFrame(srcPixels, srcW, srcH);
    }
  }

  const auto presentUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - presentStart)
                             .count();
  const float maxFps = m_config.maxFps;
  if (m_surfaceHost.IsStandaloneMode()) {
    m_surfaceHost.PollEvents(/*force=*/false);
  }
  lock.unlock();

  auto& telemetry = TelemetryManager::GetInstance();
  telemetry.RecordPresentTimeUs(presentUs);
  auto& tracker = telemetry.GetFrameTracker();
  tracker.MarkFrameEnd(maxFps);
  tracker.MarkFrameStart();

  return true;
}

void SoftwareBackend::PollEvents() {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Safe no-op in Headless, X11Child, and WaylandSubsurface modes; throttled
  // SDL event pumping in StandaloneSDL2 mode.
  m_surfaceHost.PollEvents(/*force=*/false);
}

void SoftwareBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                                   const ModernVertex& c) {
  if (!m_windowAttached || !m_headlessPixelMap) return;

  // 1. Evaluate backface culling
  if (m_cullMode != 0) {
    float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) -
                 (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
    if (m_cullMode == 1 && area < 0.0f) return;
    if (m_cullMode == 2 && area > 0.0f) return;
  }

  // 2. Flip Y and scale coordinates for SSAA
  ModernVertex a_final = a;
  ModernVertex b_final = b;
  ModernVertex c_final = c;

  if (m_ssaaScale > 1) {
    a_final.pos[0] *= static_cast<float>(m_ssaaScale);
    a_final.pos[1] *= static_cast<float>(m_ssaaScale);
    b_final.pos[0] *= static_cast<float>(m_ssaaScale);
    b_final.pos[1] *= static_cast<float>(m_ssaaScale);
    c_final.pos[0] *= static_cast<float>(m_ssaaScale);
    c_final.pos[1] *= static_cast<float>(m_ssaaScale);
  }

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT
    a_final.pos[1] = (float)m_headlessHeight - a_final.pos[1];
    b_final.pos[1] = (float)m_headlessHeight - b_final.pos[1];
    c_final.pos[1] = (float)m_headlessHeight - c_final.pos[1];
  }

  ResolveActiveState();

  DrawCommand cmd;
  cmd.type = CommandType::TRIANGLE;
  cmd.stateId = m_activeStateId;
  cmd.vertices[0] = a_final;
  cmd.vertices[1] = b_final;
  cmd.vertices[2] = c_final;

  m_binnedCommands.push_back(cmd);
  uint32_t cmdIdx = m_binnedCommands.size() - 1;

  BinTriangle(cmdIdx, a_final, b_final, c_final);
  GLIDE_INCREMENT_TRIANGLES_DRAWN(1);
}

void SoftwareBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
  if (!m_windowAttached || !m_headlessPixelMap) return;

  ModernVertex v1_final = v1;
  ModernVertex v2_final = v2;

  if (m_ssaaScale > 1) {
    v1_final.pos[0] *= static_cast<float>(m_ssaaScale);
    v1_final.pos[1] *= static_cast<float>(m_ssaaScale);
    v2_final.pos[0] *= static_cast<float>(m_ssaaScale);
    v2_final.pos[1] *= static_cast<float>(m_ssaaScale);
  }

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT
    v1_final.pos[1] = (float)m_headlessHeight - v1_final.pos[1];
    v2_final.pos[1] = (float)m_headlessHeight - v2_final.pos[1];
  }

  ResolveActiveState();

  DrawCommand cmd;
  cmd.type = CommandType::LINE;
  cmd.stateId = m_activeStateId;
  cmd.vertices[0] = v1_final;
  cmd.vertices[1] = v2_final;

  m_binnedCommands.push_back(cmd);
  uint32_t cmdIdx = m_binnedCommands.size() - 1;

  BinLine(cmdIdx, v1_final, v2_final);
}

void SoftwareBackend::DrawPoint(const ModernVertex& pt) {
  if (!m_windowAttached || !m_headlessPixelMap) return;

  ModernVertex pt_final = pt;

  if (m_ssaaScale > 1) {
    pt_final.pos[0] *= static_cast<float>(m_ssaaScale);
    pt_final.pos[1] *= static_cast<float>(m_ssaaScale);
  }

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT
    pt_final.pos[1] = (float)m_headlessHeight - pt_final.pos[1];
  }

  ResolveActiveState();

  DrawCommand cmd;
  cmd.type = CommandType::POINT;
  cmd.stateId = m_activeStateId;
  cmd.vertices[0] = pt_final;

  m_binnedCommands.push_back(cmd);
  uint32_t cmdIdx = m_binnedCommands.size() - 1;

  BinPoint(cmdIdx, pt_final);
}

void SoftwareBackend::InitTileGrid() {
  m_tileCols = (m_headlessWidth + m_tileSize - 1) >> m_tileShift;
  m_tileRows = (m_headlessHeight + m_tileSize - 1) >> m_tileShift;
  m_tiles.resize(m_tileCols * m_tileRows);
  for (auto& tile : m_tiles) {
    tile.commandIndices.clear();
  }
  GLIDE_LOG(INFO, "Software",
            "Initialized tile grid: "
                << m_tileCols << "x" << m_tileRows << " = " << m_tiles.size()
                << " tiles (tile size " << m_tileSize << ").");
}

void SoftwareBackend::BinTriangle(uint32_t cmdIdx, const ModernVertex& a,
                                  const ModernVertex& b,
                                  const ModernVertex& c) {
  float minX = std::min({a.pos[0], b.pos[0], c.pos[0]});
  float maxX = std::max({a.pos[0], b.pos[0], c.pos[0]});
  float minY = std::min({a.pos[1], b.pos[1], c.pos[1]});
  float maxY = std::max({a.pos[1], b.pos[1], c.pos[1]});

  minX = std::max(0.0f, minX);
  minY = std::max(0.0f, minY);
  maxX = std::min((float)m_headlessWidth - 1.0f, maxX);
  maxY = std::min((float)m_headlessHeight - 1.0f, maxY);

  if (minX > maxX || minY > maxY) return;

  int tileMinCol = (int)(minX) >> m_tileShift;
  int tileMaxCol = (int)(maxX) >> m_tileShift;
  int tileMinRow = (int)(minY) >> m_tileShift;
  int tileMaxRow = (int)(maxY) >> m_tileShift;

  for (int r = tileMinRow; r <= tileMaxRow; ++r) {
    for (int c = tileMinCol; c <= tileMaxCol; ++c) {
      m_tiles[r * m_tileCols + c].commandIndices.push_back(cmdIdx);
    }
  }
}

void SoftwareBackend::BinLine(uint32_t cmdIdx, const ModernVertex& v1,
                              const ModernVertex& v2) {
  float minX = std::min(v1.pos[0], v2.pos[0]);
  float maxX = std::max(v1.pos[0], v2.pos[0]);
  float minY = std::min(v1.pos[1], v2.pos[1]);
  float maxY = std::max(v1.pos[1], v2.pos[1]);

  minX = std::max(0.0f, minX);
  minY = std::max(0.0f, minY);
  maxX = std::min((float)m_headlessWidth - 1.0f, maxX);
  maxY = std::min((float)m_headlessHeight - 1.0f, maxY);

  if (minX > maxX || minY > maxY) return;

  int tileMinCol = (int)(minX) >> m_tileShift;
  int tileMaxCol = (int)(maxX) >> m_tileShift;
  int tileMinRow = (int)(minY) >> m_tileShift;
  int tileMaxRow = (int)(maxY) >> m_tileShift;

  for (int r = tileMinRow; r <= tileMaxRow; ++r) {
    for (int c = tileMinCol; c <= tileMaxCol; ++c) {
      m_tiles[r * m_tileCols + c].commandIndices.push_back(cmdIdx);
    }
  }
}

void SoftwareBackend::BinPoint(uint32_t cmdIdx, const ModernVertex& pt) {
  float px = pt.pos[0];
  float py = pt.pos[1];

  if (px < 0.0f || px >= (float)m_headlessWidth || py < 0.0f ||
      py >= (float)m_headlessHeight)
    return;

  int tileCol = (int)(px) >> m_tileShift;
  int tileRow = (int)(py) >> m_tileShift;

  m_tiles[tileRow * m_tileCols + tileCol].commandIndices.push_back(cmdIdx);
}

void SoftwareBackend::RasterizeTile(int tileIdx) {
  int col = tileIdx % m_tileCols;
  int row = tileIdx / m_tileCols;

  int tileMinX = col << m_tileShift;
  int tileMaxX = tileMinX + m_tileSize;
  int tileMinY = row << m_tileShift;
  int tileMaxY = tileMinY + m_tileSize;

  const auto& cmdIndices = m_tiles[tileIdx].commandIndices;
  for (uint32_t cmdIdx : cmdIndices) {
    const auto& cmd = m_binnedCommands[cmdIdx];
    const auto& state = m_stateCatalog[cmd.stateId];

    int clipMinX = state.clipMinX;
    int clipMaxX = state.clipMaxX;
    int clipMinY = state.clipMinY;
    int clipMaxY = state.clipMaxY;

    if (state.sstOrigin == 1) {  // LOWER_LEFT
      clipMinY = m_headlessHeight - state.clipMaxY;
      clipMaxY = m_headlessHeight - state.clipMinY;
    }

    int minX = std::max(tileMinX, clipMinX);
    int maxX = std::min(tileMaxX, clipMaxX);
    int minY = std::max(tileMinY, clipMinY);
    int maxY = std::min(tileMaxY, clipMaxY);

    if (minX >= maxX || minY >= maxY) continue;

    if (cmd.type == CommandType::TRIANGLE) {
      RasterizeSoftwareTriangleTile(cmd.vertices[0], cmd.vertices[1],
                                    cmd.vertices[2], minX, maxX, minY, maxY,
                                    state);
    } else if (cmd.type == CommandType::LINE) {
      RasterizeSoftwareLineTile(cmd.vertices[0], cmd.vertices[1], minX, maxX,
                                minY, maxY, state);
    } else if (cmd.type == CommandType::POINT) {
      RasterizeSoftwarePointTile(cmd.vertices[0], minX, maxX, minY, maxY,
                                 state);
    }
  }
}

void SoftwareBackend::FlushBins() {
  std::lock_guard<std::mutex> lock(m_mutex);
  FlushBins_Locked();
}

void SoftwareBackend::FlushBins_Locked() {
  if (m_binnedCommands.empty()) return;

  GLIDE_CPU_SCOPE(CpuBucket::Draw);
  GLIDE_PROFILE_SCOPE("Software::FlushBins");
  TelemetryManager::GetInstance().RecordDrawCall(false);

  if (m_threadPool) {
    std::vector<int> activeTiles;
    activeTiles.reserve(m_tiles.size());
    for (size_t i = 0; i < m_tiles.size(); ++i) {
      if (!m_tiles[i].commandIndices.empty()) {
        activeTiles.push_back(static_cast<int>(i));
      }
    }

    if (!activeTiles.empty()) {
      // Refined thresholding: only bypass the thread pool if the active tile
      // count is extremely small (< 4), indicating a tiny screen footprint.
      // Large triangles (even if few in number, like the Glide Cube) cover many
      // pixels and benefit immensely from multi-threaded rasterization!
      if (activeTiles.size() < 4 || m_threadPool->GetThreadCount() == 0) {
        for (int tileIdx : activeTiles) {
          RasterizeTile(tileIdx);
        }
      } else {
        m_threadPool->ParallelFor(static_cast<int>(activeTiles.size()),
                                  [this, &activeTiles](int taskIdx) {
                                    RasterizeTile(activeTiles[taskIdx]);
                                  });
      }
    }
  } else {
    for (size_t i = 0; i < m_tiles.size(); ++i) {
      if (!m_tiles[i].commandIndices.empty()) {
        RasterizeTile(i);
      }
    }
  }

  m_binnedCommands.clear();
  m_stateCatalog.clear();
  for (auto& tile : m_tiles) {
    tile.commandIndices.clear();
  }
  m_activeStateId = 0;
  m_stateCacheDirty = true;
}

void SoftwareBackend::ResolveActiveState() {
  const uint64_t texRev =
      m_textureUploadRevision.load(std::memory_order_acquire);
  if (!m_stateCacheDirty && texRev == m_resolvedTextureRevision) return;
  m_resolvedTextureRevision = texRev;

  RasterizerState state;
  state.depthMode = m_depthMode;
  state.depthCompareOp = m_depthCompareOp;
  state.depthMask = m_depthMask;
  state.depthBiasLevel = m_depthBiasLevel;
  state.depthNear = m_depthNear;
  state.depthFar = m_depthFar;
  state.ditherMode = m_ditherMode;
  state.stippleMode = m_stippleMode;
  state.stipplePattern = m_stipplePattern;
  state.rgbSrcBlend = m_rgbSrcBlend;
  state.rgbDstBlend = m_rgbDstBlend;
  state.alphaSrcBlend = m_alphaSrcBlend;
  state.alphaDstBlend = m_alphaDstBlend;
  state.alphaTestOp = m_alphaTestOp;
  state.alphaTestRefVal = m_alphaTestRefVal;
  state.clipMinX = m_clipMinX;
  state.clipMinY = m_clipMinY;
  state.clipMaxX = m_clipMaxX;
  state.clipMaxY = m_clipMaxY;
  state.sstOrigin = m_sstOrigin;
  state.fogMode = m_fogMode;
  state.fogColor = m_fogColor;
  std::memcpy(state.fogTable, m_fogTable, 64);
  state.colorMaskRgb = m_colorMaskRgb;
  state.colorMaskAlpha = m_colorMaskAlpha;
  state.aaEnabled = m_aaEnabled;

  state.boundTexAddress[0] = m_boundTexAddress[0];
  state.boundTexAddress[1] = m_boundTexAddress[1];
  state.boundTex[0] =
      (m_boundTexAddress[0] != 0xFFFFFFFFu)
          ? TextureManager::GetInstance().GetTextureSnapshot(
                0, m_boundTexAddress[0])
          : nullptr;
  state.boundTex[1] =
      (m_boundTexAddress[1] != 0xFFFFFFFFu)
          ? TextureManager::GetInstance().GetTextureSnapshot(
                1, m_boundTexAddress[1])
          : nullptr;
  state.texClampS[0] = m_texClampS[0];
  state.texClampS[1] = m_texClampS[1];
  state.texClampT[0] = m_texClampT[0];
  state.texClampT[1] = m_texClampT[1];
  state.texMinFilter[0] = m_texMinFilter[0];
  state.texMinFilter[1] = m_texMinFilter[1];
  state.texMagFilter[0] = m_texMagFilter[0];
  state.texMagFilter[1] = m_texMagFilter[1];
  state.texMipMapMode[0] = m_texMipMapMode[0];
  state.texMipMapMode[1] = m_texMipMapMode[1];
  state.texLodBlend[0] = m_texLodBlend[0];
  state.texLodBlend[1] = m_texLodBlend[1];
  state.texLodBias[0] = m_texLodBias[0];
  state.texLodBias[1] = m_texLodBias[1];

  state.colorCombinerFunc = m_colorCombinerFunc;
  state.colorCombinerFactor = m_colorCombinerFactor;
  state.colorCombinerLocal = m_colorCombinerLocal;
  state.colorCombinerOther = m_colorCombinerOther;
  state.colorCombinerInvert = m_colorCombinerInvert;
  state.alphaCombinerFunc = m_alphaCombinerFunc;
  state.alphaCombinerFactor = m_alphaCombinerFactor;
  state.alphaCombinerLocal = m_alphaCombinerLocal;
  state.alphaCombinerOther = m_alphaCombinerOther;
  state.alphaCombinerInvert = m_alphaCombinerInvert;

  state.texCombinerRgbFunc[0] = m_texCombinerRgbFunc[0];
  state.texCombinerRgbFunc[1] = m_texCombinerRgbFunc[1];
  state.texCombinerRgbFactor[0] = m_texCombinerRgbFactor[0];
  state.texCombinerRgbFactor[1] = m_texCombinerRgbFactor[1];
  state.texCombinerAlphaFunc[0] = m_texCombinerAlphaFunc[0];
  state.texCombinerAlphaFunc[1] = m_texCombinerAlphaFunc[1];
  state.texCombinerAlphaFactor[0] = m_texCombinerAlphaFactor[0];
  state.texCombinerAlphaFactor[1] = m_texCombinerAlphaFactor[1];
  state.texCombinerRgbInvert[0] = m_texCombinerRgbInvert[0];
  state.texCombinerRgbInvert[1] = m_texCombinerRgbInvert[1];
  state.texCombinerAlphaInvert[0] = m_texCombinerAlphaInvert[0];
  state.texCombinerAlphaInvert[1] = m_texCombinerAlphaInvert[1];

  state.constantColor = m_constantColor;
  state.stwHintMask = m_stwHintMask;
  state.pixelFormatOverride = m_pixelFormatOverride;

  state.chromakeyMode = m_chromakeyMode;
  state.chromakeyValue = m_chromakeyValue;
  state.chromakeyRangeMin = m_chromakeyRangeMin;
  state.chromakeyRangeMax = m_chromakeyRangeMax;
  state.chromakeyRangeMode = m_chromakeyRangeMode;
  state.texChromaMode[0] = m_texChromaMode[0];
  state.texChromaMode[1] = m_texChromaMode[1];
  state.texChromaMin[0] = m_texChromaMin[0];
  state.texChromaMin[1] = m_texChromaMin[1];
  state.texChromaMax[0] = m_texChromaMax[0];
  state.texChromaMax[1] = m_texChromaMax[1];
  state.texChromaRangeMode[0] = m_texChromaRangeMode[0];
  state.texChromaRangeMode[1] = m_texChromaRangeMode[1];

  state.activeRenderBufferIdx =
      (m_activeRenderBuffer == 0) ? m_frontBufferIdx : m_backBufferIdx;

  auto it = std::find(m_stateCatalog.begin(), m_stateCatalog.end(), state);
  if (it != m_stateCatalog.end()) {
    m_activeStateId = std::distance(m_stateCatalog.begin(), it);
  } else {
    m_stateCatalog.push_back(state);
    m_activeStateId = m_stateCatalog.size() - 1;
  }
  m_stateCacheDirty = false;
}

void SoftwareBackend::UploadTexture(uint32_t tmu, uint32_t startAddress,
                                    const struct VirtualTexture& tex) {
  (void)tmu;
  (void)startAddress;
  (void)tex;
  // Lock-free atomic bump avoids AB-BA lock inversion with TextureManager::m_mutex.
  m_textureUploadRevision.fetch_add(1, std::memory_order_release);
}

void SoftwareBackend::UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                                           const struct VirtualTexture& tex,
                                           uint32_t lodLevel, uint32_t startRow,
                                           uint32_t endRow) {
  (void)tmu;
  (void)startAddress;
  (void)tex;
  (void)lodLevel;
  (void)startRow;
  (void)endRow;
  m_textureUploadRevision.fetch_add(1, std::memory_order_release);
}

void SoftwareBackend::PurgeTextures() {
  m_textureUploadRevision.fetch_add(1, std::memory_order_release);
}

bool SoftwareBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY,
                              uint32_t srcWidth, uint32_t srcHeight,
                              uint32_t dstStride, void* dstData) {
  bool ok = SoftwareBackendBase::ReadLFB(buffer, srcX, srcY, srcWidth,
                                         srcHeight, dstStride, dstData);
  if (!ok) return false;

  if (m_pixelFormatOverride == 1) {
    bool is32Bit = (dstStride >= srcWidth * 4);
    if (is32Bit) {
      auto* outBytes = reinterpret_cast<uint8_t*>(dstData);
      for (uint32_t y = 0; y < srcHeight; ++y) {
        auto* row32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
        for (uint32_t x = 0; x < srcWidth; ++x) {
          uint32_t val = row32[x];
          uint32_t a = val & 0xFF000000;
          uint32_t g = val & 0x0000FF00;
          uint32_t r = (val >> 16) & 0xFF;
          uint32_t b = val & 0xFF;
          row32[x] = a | (b << 16) | g | r;
        }
      }
    }
  }
  return true;
}

}  // namespace GlideWrapper
