#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glide.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct SimpleVertex {
  float x;
  float y;
  float r;
  float g;
  float b;
  float a;
};

void PrintUsage(const char* argv0) {
  std::cout
      << "Usage: " << argv0 << " [options]\n"
      << "  --raw-handle       Pass native X11 Window ID / wl_surface* to "
         "grSstWinOpen (DOSBox-X style, default)\n"
      << "  --sdl-window       Pass SDL_Window* directly to grSstWinOpen "
         "(SVN DOSBox setConfig('SDL2') style)\n"
      << "  --parent-gl        Give parent SDL2 window an active OpenGL "
         "context (DOSBox-X output=opengl style)\n"
      << "  --res <WxH>        Guest Glide resolution: 640x480 (default) or "
         "800x600\n"
      << "  --smoke-ms <ms>    Automated resize + reopen smoke test & exit "
         "after <ms> milliseconds\n"
      << "  --help             Show this help\n";
}

void PaintParentSurface(SDL_Window* win, SDL_GLContext glCtx) {
  if (glCtx) {
    using GlClearColorFn = void (*)(float, float, float, float);
    using GlClearFn = void (*)(unsigned int);
    auto pClearColor =
        reinterpret_cast<GlClearColorFn>(SDL_GL_GetProcAddress("glClearColor"));
    auto pClear = reinterpret_cast<GlClearFn>(SDL_GL_GetProcAddress("glClear"));
    if (pClearColor && pClear) {
      constexpr unsigned int kGlColorBufferBit = 0x00004000u;
      pClearColor(0.18f, 0.08f, 0.22f, 1.0f);
      pClear(kGlColorBufferBit);
    }
    SDL_GL_SwapWindow(win);
    return;
  }

  SDL_Surface* surf = SDL_GetWindowSurface(win);
  if (surf) {
    SDL_FillRect(surf, nullptr, SDL_MapRGB(surf->format, 36, 20, 48));
    SDL_UpdateWindowSurface(win);
  }
}

AnyPtr ResolveHostHandle(SDL_Window* win, bool passSdlWindow,
                         std::string& outDesc) {
  if (passSdlWindow) {
    std::ostringstream ss;
    ss << "SDL_Window* " << static_cast<void*>(win);
    outDesc = ss.str();
    return static_cast<AnyPtr>(reinterpret_cast<uintptr_t>(win));
  }

  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if (!SDL_GetWindowWMInfo(win, &wmInfo)) {
    std::cerr << "[GlideEmbed] SDL_GetWindowWMInfo failed: " << SDL_GetError()
              << "\n";
    return 0;
  }

  if (wmInfo.subsystem == SDL_SYSWM_X11) {
    std::ostringstream ss;
    ss << "X11 Window 0x" << std::hex << wmInfo.info.x11.window;
    outDesc = ss.str();
    return static_cast<AnyPtr>(wmInfo.info.x11.window);
  }
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
  if (wmInfo.subsystem == SDL_SYSWM_WAYLAND) {
    std::ostringstream ss;
    ss << "wl_surface* " << static_cast<void*>(wmInfo.info.wl.surface);
    outDesc = ss.str();
    return static_cast<AnyPtr>(
        reinterpret_cast<uintptr_t>(wmInfo.info.wl.surface));
  }
#endif

  outDesc = "unsupported subsystem";
  return 0;
}

void DrawQuad(float x0, float y0, float x1, float y1, float r, float g,
              float b) {
  SimpleVertex v0{x0, y0, r, g, b, 255.0f};
  SimpleVertex v1{x1, y0, r, g, b, 255.0f};
  SimpleVertex v2{x1, y1, r, g, b, 255.0f};
  SimpleVertex v3{x0, y1, r, g, b, 255.0f};
  grDrawTriangle(&v0, &v1, &v2);
  grDrawTriangle(&v0, &v2, &v3);
}

void RenderGlideFrame(float guestW, float guestH, float angle,
                      uint32_t interactionCount) {
  // Deep navy background (RGB 0x10, 0x1E, 0x38)
  grBufferClear(0x00101E38, 0, 0xFFFF);

  // Crisp cyan border hugging the exact 4:3 guest framebuffer edges so aspect
  // ratio preservation and letterboxing/pillarboxing are immediately obvious.
  const float t = 4.0f;
  DrawQuad(0.0f, 0.0f, guestW, t, 0.0f, 229.0f, 255.0f);
  DrawQuad(0.0f, guestH - t, guestW, guestH, 0.0f, 229.0f, 255.0f);
  DrawQuad(0.0f, 0.0f, t, guestH, 0.0f, 229.0f, 255.0f);
  DrawQuad(guestW - t, 0.0f, guestW, guestH, 0.0f, 229.0f, 255.0f);

  // Corner markers (red TL, green TR, yellow BR, magenta BL) to catch flips
  const float c = 28.0f;
  DrawQuad(t + 4.0f, t + 4.0f, t + 4.0f + c, t + 4.0f + c, 255.0f, 64.0f,
           64.0f);
  DrawQuad(guestW - t - 4.0f - c, t + 4.0f, guestW - t - 4.0f, t + 4.0f + c,
           64.0f, 255.0f, 96.0f);
  DrawQuad(guestW - t - 4.0f - c, guestH - t - 4.0f - c, guestW - t - 4.0f,
           guestH - t - 4.0f, 255.0f, 220.0f, 64.0f);
  DrawQuad(t + 4.0f, guestH - t - 4.0f - c, t + 4.0f + c, guestH - t - 4.0f,
           255.0f, 80.0f, 220.0f);

  // Rotating Gouraud-shaded star/triangle whose palette flips on input events
  const float cx = guestW * 0.5f;
  const float cy = guestH * 0.5f;
  const float radius = std::min(guestW, guestH) * 0.34f;
  const bool altPalette = (interactionCount % 2) == 1;

  SimpleVertex tri[3];
  for (int i = 0; i < 3; ++i) {
    float a = angle + static_cast<float>(i) * (2.0f * 3.14159265f / 3.0f);
    tri[i].x = cx + radius * std::cos(a);
    tri[i].y = cy + radius * std::sin(a);
    tri[i].a = 255.0f;
  }
  if (!altPalette) {
    tri[0].r = 255.0f;
    tri[0].g = 64.0f;
    tri[0].b = 64.0f;
    tri[1].r = 64.0f;
    tri[1].g = 255.0f;
    tri[1].b = 110.0f;
    tri[2].r = 70.0f;
    tri[2].g = 150.0f;
    tri[2].b = 255.0f;
  } else {
    tri[0].r = 255.0f;
    tri[0].g = 210.0f;
    tri[0].b = 40.0f;
    tri[1].r = 255.0f;
    tri[1].g = 70.0f;
    tri[1].b = 210.0f;
    tri[2].r = 40.0f;
    tri[2].g = 240.0f;
    tri[2].b = 255.0f;
  }
  grDrawTriangle(&tri[0], &tri[1], &tri[2]);

  // Counter-rotating smaller inner triangle
  SimpleVertex inner[3];
  for (int i = 0; i < 3; ++i) {
    float a =
        -angle * 1.6f + static_cast<float>(i) * (2.0f * 3.14159265f / 3.0f);
    inner[i].x = cx + (radius * 0.45f) * std::cos(a);
    inner[i].y = cy + (radius * 0.45f) * std::sin(a);
    inner[i].r = 245.0f;
    inner[i].g = 245.0f;
    inner[i].b = 255.0f;
    inner[i].a = 255.0f;
  }
  grDrawTriangle(&inner[0], &inner[1], &inner[2]);

  grBufferSwap(1);
}

GrContext_t OpenGlideContext(AnyPtr hostHandle, GrScreenResolution_t res) {
  GrContext_t ctx = grSstWinOpen(hostHandle, res, GR_REFRESH_60Hz,
                                 GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2,
                                 1);
  if (!ctx) return 0;

  grVertexLayout(GR_PARAM_XY, offsetof(SimpleVertex, x), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_RGB, offsetof(SimpleVertex, r), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A, offsetof(SimpleVertex, a), GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE,
                       GR_BLEND_ZERO);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grCullMode(GR_CULL_DISABLE);
  return ctx;
}

}  // namespace

int main(int argc, char** argv) {
  bool passSdlWindow = false;
  bool parentOpenGL = false;
  int smokeMs = 0;
  GrScreenResolution_t glideRes = GR_RESOLUTION_640x480;
  float guestW = 640.0f;
  float guestH = 480.0f;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--sdl-window") {
      passSdlWindow = true;
    } else if (arg == "--raw-handle") {
      passSdlWindow = false;
    } else if (arg == "--parent-gl") {
      parentOpenGL = true;
    } else if (arg == "--res" && i + 1 < argc) {
      std::string r = argv[++i];
      if (r == "800x600") {
        glideRes = GR_RESOLUTION_800x600;
        guestW = 800.0f;
        guestH = 600.0f;
      } else {
        glideRes = GR_RESOLUTION_640x480;
        guestW = 640.0f;
        guestH = 480.0f;
      }
    } else if (arg == "--smoke-ms" && i + 1 < argc) {
      smokeMs = std::atoi(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "[GlideEmbed] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  const char* driver = SDL_GetCurrentVideoDriver();
  std::cout << "[GlideEmbed] SDL2 video driver: "
            << (driver ? driver : "unknown")
            << " | parent=" << (parentOpenGL ? "OpenGL" : "Surface") << "\n";

  Uint32 winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                    SDL_WINDOW_ALLOW_HIGHDPI;
  if (parentOpenGL) {
    winFlags |= SDL_WINDOW_OPENGL;
  }

  SDL_Window* parentWin = SDL_CreateWindow(
      "Host DOSBox Simulator (Glide 3.x Embedded)", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600, winFlags);
  if (!parentWin) {
    std::cerr << "[GlideEmbed] SDL_CreateWindow failed: " << SDL_GetError()
              << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glCtx = nullptr;
  if (parentOpenGL) {
    glCtx = SDL_GL_CreateContext(parentWin);
    if (!glCtx) {
      std::cerr << "[GlideEmbed] SDL_GL_CreateContext failed: "
                << SDL_GetError() << "\n";
      SDL_DestroyWindow(parentWin);
      SDL_Quit();
      return 1;
    }
  }

  PaintParentSurface(parentWin, glCtx);

  std::string handleDesc;
  AnyPtr hostHandle = ResolveHostHandle(parentWin, passSdlWindow, handleDesc);
  if (hostHandle == 0) {
    std::cerr << "[GlideEmbed] Failed to resolve host handle (" << handleDesc
              << ")\n";
    if (glCtx) SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(parentWin);
    SDL_Quit();
    return 1;
  }

  std::cout << "[GlideEmbed] Opening Glide 3.x (" << static_cast<int>(guestW)
            << "x" << static_cast<int>(guestH) << ") on " << handleDesc << "\n";

  grGlideInit();
  grSstSelect(0);
  GrContext_t glideCtx = OpenGlideContext(hostHandle, glideRes);
  if (!glideCtx) {
    std::cerr << "[GlideEmbed] grSstWinOpen FAILED!\n";
    grGlideShutdown();
    if (glCtx) SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(parentWin);
    SDL_Quit();
    return 2;
  }

  std::cout << "[GlideEmbed] Controls: Resize/Maximize, F or Alt+Enter="
               "Fullscreen, Space=Toggle Glide On/Off (view host), "
               "R=Close/Reopen Glide, Click/Keys=Input passthrough, "
               "ESC/Q=Quit.\n";

  bool running = true;
  bool isFullscreen = false;
  bool glideActive = true;
  uint32_t clickCount = 0;
  uint32_t keyCount = 0;
  uint32_t frameIdx = 0;
  uint32_t reopenCount = 0;
  bool smokeResized = false;
  bool smokeReopened = false;
  const auto startTime = std::chrono::steady_clock::now();

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        running = false;
      } else if (ev.type == SDL_WINDOWEVENT &&
                 (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                  ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                  ev.window.event == SDL_WINDOWEVENT_EXPOSED)) {
        PaintParentSurface(parentWin, glCtx);
      } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
        ++clickCount;
      } else if (ev.type == SDL_KEYDOWN) {
        ++keyCount;
        const SDL_Keycode key = ev.key.keysym.sym;
        const Uint16 mod = ev.key.keysym.mod;
        if (key == SDLK_ESCAPE || key == SDLK_q) {
          running = false;
        } else if (key == SDLK_f ||
                   (key == SDLK_RETURN && (mod & KMOD_ALT))) {
          isFullscreen = !isFullscreen;
          SDL_SetWindowFullscreen(
              parentWin, isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
          PaintParentSurface(parentWin, glCtx);
        } else if (key == SDLK_SPACE) {
          if (glideActive) {
            grSstWinClose(glideCtx);
            glideCtx = 0;
            glideActive = false;
            PaintParentSurface(parentWin, glCtx);
            std::cout << "[GlideEmbed] Glide closed (host window visible)\n";
          } else {
            glideCtx = OpenGlideContext(hostHandle, glideRes);
            if (!glideCtx) {
              std::cerr << "[GlideEmbed] Failed to reopen Glide!\n";
              running = false;
            } else {
              glideActive = true;
              ++reopenCount;
              std::cout << "[GlideEmbed] Glide reopened (#" << reopenCount
                        << ")\n";
            }
          }
        } else if (key == SDLK_r && glideActive) {
          grSstWinClose(glideCtx);
          glideCtx = 0;
          PaintParentSurface(parentWin, glCtx);
          SDL_Delay(200);
          glideCtx = OpenGlideContext(hostHandle, glideRes);
          if (!glideCtx) {
            std::cerr << "[GlideEmbed] Failed to reopen Glide!\n";
            running = false;
          } else {
            ++reopenCount;
            std::cout << "[GlideEmbed] Glide close+reopen OK (#" << reopenCount
                      << ")\n";
          }
        }
      }
    }

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime)
            .count();

    if (smokeMs > 0) {
      if (!smokeResized && elapsedMs >= smokeMs / 3) {
        std::cout << "[GlideEmbed] Smoke test: resizing parent to 1024x600...\n";
        SDL_SetWindowSize(parentWin, 1024, 600);
        PaintParentSurface(parentWin, glCtx);
        smokeResized = true;
      }
      if (!smokeReopened && elapsedMs >= (smokeMs * 2) / 3 && glideActive) {
        std::cout << "[GlideEmbed] Smoke test: close+reopen Glide context...\n";
        grSstWinClose(glideCtx);
        glideCtx = 0;
        PaintParentSurface(parentWin, glCtx);
        glideCtx = OpenGlideContext(hostHandle, glideRes);
        if (!glideCtx) {
          std::cerr << "[GlideEmbed] Smoke test reopen FAILED!\n";
          grGlideShutdown();
          if (glCtx) SDL_GL_DeleteContext(glCtx);
          SDL_DestroyWindow(parentWin);
          SDL_Quit();
          return 3;
        }
        ++reopenCount;
        smokeReopened = true;
      }
      if (elapsedMs >= smokeMs) {
        running = false;
      }
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(parentWin, &winW, &winH);
    std::ostringstream title;
    title << "Glide Embed Test | " << (driver ? driver : "?")
          << (parentOpenGL ? " +GL" : "") << " | host=" << winW << "x" << winH
          << " | glide=" << (glideActive ? "ON" : "OFF")
          << " | clicks=" << clickCount << " keys=" << keyCount
          << " reopens=" << reopenCount;
    SDL_SetWindowTitle(parentWin, title.str().c_str());

    if (glideActive) {
      RenderGlideFrame(guestW, guestH, static_cast<float>(frameIdx) * 0.03f,
                       clickCount + keyCount);
      ++frameIdx;
    } else {
      SDL_Delay(16);
    }
  }

  if (glideActive && glideCtx) {
    grSstWinClose(glideCtx);
  }
  grGlideShutdown();

  if (glCtx) {
    SDL_GL_DeleteContext(glCtx);
  }
  SDL_DestroyWindow(parentWin);
  SDL_Quit();

  if (smokeMs > 0) {
    std::cout << "[GlideEmbed] Smoke test complete: frames=" << frameIdx
              << " resized=" << (smokeResized ? 1 : 0)
              << " reopens=" << reopenCount << "\n";
  }
  return 0;
}
