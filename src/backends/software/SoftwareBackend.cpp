#include "SoftwareBackend.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace GlideWrapper {

// Fast log2 approximation using IEEE-754 float exponent extraction
static inline float fast_log2(float val) {
  union {
    float f;
    uint32_t i;
  } vx = {val};
  float y = static_cast<float>(vx.i);
  y *= 1.1920928955078125e-7f;  // 1 / 2^23
  return y - 126.94269504f;
}

static const float s_tableIndexToW[64] = {
    1.000000f,     1.142857f,     1.333333f,     1.600000f,     2.000000f,
    2.285714f,     2.666667f,     3.200000f,     4.000000f,     4.571429f,
    5.333333f,     6.400000f,     8.000000f,     9.142858f,     10.666667f,
    12.800000f,    16.000000f,    18.285715f,    21.333334f,    25.600000f,
    32.000000f,    36.571430f,    42.666668f,    51.200001f,    64.000000f,
    73.142860f,    85.333336f,    102.400002f,   128.000000f,   146.285721f,
    170.666672f,   204.800003f,   256.000000f,   292.571442f,   341.333344f,
    409.600006f,   512.000000f,   585.142883f,   682.666687f,   819.200012f,
    1024.000000f,  1170.285767f,  1365.333374f,  1638.400024f,  2048.000000f,
    2340.571533f,  2730.666748f,  3276.800049f,  4096.000000f,  4681.143066f,
    5461.333496f,  6553.600098f,  8192.000000f,  9362.286133f,  10922.666992f,
    13107.200195f, 16384.000000f, 18724.572266f, 21845.333984f, 26214.400391f,
    32768.000000f, 37449.144531f, 43690.667969f, 52428.800781f};

inline bool SoftwareBackend::IsTexChromaMatch(uint32_t color, uint32_t tmu, const RasterizerState& state) const {
  if (state.texChromaMode[tmu] != 1) return false;

  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;

  bool rangeEnabled = ((state.texChromaRangeMode[tmu] >> 28) & 1) == 1;
  if (rangeEnabled) {
    uint32_t minR = (state.texChromaMin[tmu] >> 16) & 0xFF;
    uint32_t minG = (state.texChromaMin[tmu] >> 8) & 0xFF;
    uint32_t minB = state.texChromaMin[tmu] & 0xFF;

    uint32_t maxR = (state.texChromaMax[tmu] >> 16) & 0xFF;
    uint32_t maxG = (state.texChromaMax[tmu] >> 8) & 0xFF;
    uint32_t maxB = state.texChromaMax[tmu] & 0xFF;

    bool rMatch = (r >= minR && r <= maxR);
    bool gMatch = (g >= minG && g <= maxG);
    bool bMatch = (b >= minB && b <= maxB);

    bool blueExcl = ((state.texChromaRangeMode[tmu] >> 24) & 1) == 1;
    bool greenExcl = ((state.texChromaRangeMode[tmu] >> 25) & 1) == 1;
    bool redExcl = ((state.texChromaRangeMode[tmu] >> 26) & 1) == 1;

    bool rRes = rMatch != redExcl;
    bool gRes = gMatch != greenExcl;
    bool bRes = bMatch != blueExcl;

    bool unionMode = ((state.texChromaRangeMode[tmu] >> 27) & 1) == 1;
    return unionMode ? (rRes || gRes || bRes) : (rRes && gRes && bRes);
  } else {
    uint32_t chromaR = (state.texChromaMin[tmu] >> 16) & 0xFF;
    uint32_t chromaG = (state.texChromaMin[tmu] >> 8) & 0xFF;
    uint32_t chromaB = state.texChromaMin[tmu] & 0xFF;

    return (r == chromaR && g == chromaG && b == chromaB);
  }
}

inline uint32_t SoftwareBackend::SampleTextureLevel(
    const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS, float levelTrueT,
    uint32_t targetClampS, uint32_t targetClampT, uint32_t targetMinFilter,
    uint32_t tmuIdx, const RasterizerState& state) const {
  auto mirrorCoord = [](int coord, uint32_t size) -> int {
    int doubleSize = static_cast<int>(size * 2);
    int mask = doubleSize - 1;
    int wrapped = coord & mask;
    return (wrapped < static_cast<int>(size)) ? wrapped : (mask - wrapped);
  };

  const uint32_t* levelPixels = targetTex->swizzledMipLevels[lodIdx].data();
  uint32_t targetW = targetTex->baseWidth;
  uint32_t targetH = targetTex->baseHeight;
  uint32_t levelW = std::max(1u, targetW >> lodIdx);
  uint32_t levelH = std::max(1u, targetH >> lodIdx);
  uint32_t levelWMask = levelW - 1;
  uint32_t levelHMask = levelH - 1;

  float levelScale = 1.0f / static_cast<float>(1 << lodIdx);
  float maxDim = static_cast<float>(std::max(targetW, targetH));
  float targetScaleU = maxDim / 256.0f;
  float targetScaleV = maxDim / 256.0f;
  float levelTexU = levelTrueS * targetScaleU * levelScale;
  float levelTexV = levelTrueT * targetScaleV * levelScale;

  if (targetMinFilter == 1) {  // BILINEAR
    float u_sub = levelTexU - 0.5f;
    float v_sub = levelTexV - 0.5f;
    int u0 = static_cast<int>(std::floor(u_sub));
    int v0 = static_cast<int>(std::floor(v_sub));

    int fx = static_cast<int>((u_sub - u0) * 256.0f);
    int fy = static_cast<int>((v_sub - v0) * 256.0f);

    int x0, x1, y0, y1;
    if (targetClampS == 1) {  // CLAMP
      x0 = std::max(0, std::min(static_cast<int>(levelW - 1), u0));
      x1 = std::max(0, std::min(static_cast<int>(levelW - 1), u0 + 1));
    } else if (targetClampS == 2) {  // MIRROR
      x0 = mirrorCoord(u0, levelW);
      x1 = mirrorCoord(u0 + 1, levelW);
    } else {  // WRAP
      x0 = u0 & levelWMask;
      x1 = (u0 + 1) & levelWMask;
    }

    if (targetClampT == 1) {  // CLAMP
      y0 = std::max(0, std::min(static_cast<int>(levelH - 1), v0));
      y1 = std::max(0, std::min(static_cast<int>(levelH - 1), v0 + 1));
    } else if (targetClampT == 2) {  // MIRROR
      y0 = mirrorCoord(v0, levelH);
      y1 = mirrorCoord(v0 + 1, levelH);
    } else {  // WRAP
      y0 = v0 & levelHMask;
      y1 = (v0 + 1) & levelHMask;
    }

    uint32_t c00 = levelPixels[y0 * levelW + x0];
    uint32_t c10 = levelPixels[y0 * levelW + x1];
    uint32_t c01 = levelPixels[y1 * levelW + x0];
    uint32_t c11 = levelPixels[y1 * levelW + x1];

    if (IsTexChromaMatch(c00, tmuIdx, state)) c00 = 0;
    if (IsTexChromaMatch(c10, tmuIdx, state)) c10 = 0;
    if (IsTexChromaMatch(c01, tmuIdx, state)) c01 = 0;
    if (IsTexChromaMatch(c11, tmuIdx, state)) c11 = 0;

    int w00 = (256 - fx) * (256 - fy);
    int w10 = fx * (256 - fy);
    int w01 = (256 - fx) * fy;
    int w11 = fx * fy;

    auto blendChannel = [&](int shift) -> uint32_t {
      uint32_t val00 = (c00 >> shift) & 0xFF;
      uint32_t val10 = (c10 >> shift) & 0xFF;
      uint32_t val01 = (c01 >> shift) & 0xFF;
      uint32_t val11 = (c11 >> shift) & 0xFF;
      return (val00 * w00 + val10 * w10 + val01 * w01 + val11 * w11) >> 16;
    };

    uint32_t r = blendChannel(16);
    uint32_t g = blendChannel(8);
    uint32_t b = blendChannel(0);
    uint32_t a_val = blendChannel(24);

    return (a_val << 24) | (r << 16) | (g << 8) | b;
  } else {  // POINT
    int u0 = static_cast<int>(levelTexU + 0.0001f);
    int v0 = static_cast<int>(levelTexV + 0.0001f);

    int x0, y0;
    if (targetClampS == 1) {
      x0 = std::max(0, std::min(static_cast<int>(levelW - 1), u0));
    } else if (targetClampS == 2) {
      x0 = mirrorCoord(u0, levelW);
    } else {
      x0 = u0 & levelWMask;
    }

    if (targetClampT == 1) {
      y0 = std::max(0, std::min(static_cast<int>(levelH - 1), v0));
    } else if (targetClampT == 2) {
      y0 = mirrorCoord(v0, levelH);
    } else {
      y0 = v0 & levelHMask;
    }

    uint32_t val = levelPixels[y0 * levelW + x0];
    if (IsTexChromaMatch(val, tmuIdx, state)) {
      val = 0;
    }
    return val;
  }
}

struct TmuColor {
  float r, g, b, a;
};

static TmuColor EvaluateTmuStage(uint32_t rgbFunc, uint32_t rgbFactor,
                                 uint32_t alphaFunc, uint32_t alphaFactor,
                                 bool rgbInvert, bool alphaInvert,
                                 const TmuColor& localVal,
                                 const TmuColor& otherVal,
                                 const TmuColor& iteratedCol) {
  // 1. Evaluate Alpha
  float aLocal = localVal.a;
  float aOther = otherVal.a;
  float factA = 0.0f;
  switch (alphaFactor) {
    case 1:
    case 3:
      factA = aLocal;
      break;
    case 2:
      factA = aOther;
      break;
    case 4:
      factA = localVal.a;
      break;
    case 8:
      factA = 1.0f;
      break;
    case 9:
    case 11:
      factA = 1.0f - aLocal;
      break;
    case 10:
      factA = 1.0f - aOther;
      break;
    case 12:
      factA = 1.0f - localVal.a;
      break;
  }

  float finalA = 0.0f;
  switch (alphaFunc) {
    case 0:
      break;
    case 1:
      finalA = aLocal;
      break;
    case 3:
      finalA = aOther * factA;
      break;
    case 4:
    case 5:
      finalA = aOther * factA + aLocal;
      break;
    case 6:
      finalA = (aOther - aLocal) * factA;
      break;
    case 7:
    case 8:
      finalA = (aOther - aLocal) * factA + aLocal;
      break;
    case 9:
    case 16:
      finalA = (aLocal - aOther) * factA + aOther;
      break;
  }
  if (alphaInvert) finalA = 1.0f - finalA;

  // 2. Evaluate RGB
  float cLocalR = localVal.r;
  float cLocalG = localVal.g;
  float cLocalB = localVal.b;
  float cOtherR = otherVal.r;
  float cOtherG = otherVal.g;
  float cOtherB = otherVal.b;

  float factCR = 0.0f;
  float factCG = 0.0f;
  float factCB = 0.0f;
  switch (rgbFactor) {
    case 1:
      factCR = cLocalR;
      factCG = cLocalG;
      factCB = cLocalB;
      break;
    case 2:
      factCR = aOther;
      factCG = aOther;
      factCB = aOther;
      break;
    case 3:
      factCR = aLocal;
      factCG = aLocal;
      factCB = aLocal;
      break;
    case 4:
      factCR = localVal.a;
      factCG = localVal.a;
      factCB = localVal.a;
      break;
    case 5:
      factCR = localVal.r;
      factCG = localVal.g;
      factCB = localVal.b;
      break;
    case 8:
      factCR = 1.0f;
      factCG = 1.0f;
      factCB = 1.0f;
      break;
    case 9:
      factCR = 1.0f - cLocalR;
      factCG = 1.0f - cLocalG;
      factCB = 1.0f - cLocalB;
      break;
    case 10:
      factCR = 1.0f - aOther;
      factCG = 1.0f - aOther;
      factCB = 1.0f - aOther;
      break;
    case 11:
      factCR = 1.0f - aLocal;
      factCG = 1.0f - aLocal;
      factCB = 1.0f - aLocal;
      break;
    case 12:
      factCR = 1.0f - localVal.a;
      factCG = 1.0f - localVal.a;
      factCB = 1.0f - localVal.a;
      break;
  }

  float finalCR = 0.0f;
  float finalCG = 0.0f;
  float finalCB = 0.0f;
  switch (rgbFunc) {
    case 0:
      break;
    case 1:
      finalCR = cLocalR;
      finalCG = cLocalG;
      finalCB = cLocalB;
      break;
    case 3:
      finalCR = cOtherR * factCR;
      finalCG = cOtherG * factCG;
      finalCB = cOtherB * factCB;
      break;
    case 4:
      finalCR = cOtherR * factCR + cLocalR;
      finalCG = cOtherG * factCG + cLocalG;
      finalCB = cOtherB * factCB + cLocalB;
      break;
    case 5:
      finalCR = cOtherR * factCR + aLocal;
      finalCG = cOtherG * factCG + aLocal;
      finalCB = cOtherB * factCB + aLocal;
      break;
    case 6:
      finalCR = (cOtherR - cLocalR) * factCR;
      finalCG = (cOtherG - cLocalG) * factCR;
      finalCB = (cOtherB - cLocalB) * factCR;
      break;
    case 7:
      finalCR = (cOtherR - cLocalR) * factCR + cLocalR;
      finalCG = (cOtherG - cLocalG) * factCR + cLocalG;
      finalCB = (cOtherB - cLocalB) * factCR + cLocalB;
      break;
    case 8:
      finalCR = (cOtherR - cLocalR) * factCR + aLocal;
      finalCG = (cOtherG - cLocalG) * factCR + aLocal;
      finalCB = (cOtherB - cLocalB) * factCR + aLocal;
      break;
    case 9:
      finalCR = (cLocalR - cOtherR) * factCR + cOtherR;
      finalCG = (cLocalG - cOtherG) * factCG + cOtherG;
      finalCB = (cLocalB - cOtherB) * factCB + cOtherB;
      break;
    case 16:
      // In GLSL: (vec3(aLocal) - cOther) * factC + cOther;
      finalCR = (aLocal - cOtherR) * factCR + cOtherR;
      finalCG = (aLocal - cOtherG) * factCG + cOtherG;
      finalCB = (aLocal - cOtherB) * factCB + cOtherB;
      break;
  }
  if (rgbInvert) {
    finalCR = 1.0f - finalCR;
    finalCG = 1.0f - finalCG;
    finalCB = 1.0f - finalCB;
  }

  return {finalCR, finalCG, finalCB, finalA};
}

bool SoftwareBackend::Initialize(const WrapperConfig& config) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_initialized) return true;

  m_config = config;
  ResetState();

  int numThreads = 1;
  const char* ompEnv = std::getenv("OMP_NUM_THREADS");
  if (ompEnv != nullptr) {
    numThreads = std::max(1, std::atoi(ompEnv));
    GLIDE_LOG(INFO, "Software", "ThreadPool size controlled by environment (OMP_NUM_THREADS): " << numThreads);
  } else {
    numThreads = std::max(1u, std::thread::hardware_concurrency());
    GLIDE_LOG(INFO, "Software", "ThreadPool size configured to hardware concurrency: " << numThreads);
  }

  m_threadPool = std::make_unique<ThreadPool>(numThreads, [this](int tileIdx) {
    RasterizeTile(tileIdx);
  });

  InitTileGrid();

  m_initialized = true;
  GLIDE_LOG(INFO, "Software", "Pure Software translation backend initialized successfully with Tile-Based ThreadPool.");
  return true;
}

void SoftwareBackend::Shutdown() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized) return;

  GLIDE_LOG(INFO, "Software", "Shutting down Pure Software backend and joining thread pool.");
  m_threadPool = nullptr; // Join all threads
  DetachWindow();

  m_initialized = false;
}

bool SoftwareBackend::AttachWindow(void* nativeWindowHandle, uint32_t width,
                                   uint32_t height, bool windowed) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized) return false;
  m_windowShown = false;
  if (m_windowAttached) DetachWindow();

  GLIDE_LOG(INFO, "Software",
            "Attaching software execution window ("
                << width << "x" << height
                << "), Windowed=" << (windowed ? "Yes" : "No"));

  ResolveAAMode();
  m_headlessWidth = width * m_ssaaScale;
  m_headlessHeight = height * m_ssaaScale;
  m_headlessMode = true;

  // Allocate the CPU framebuffer double buffers (scaled for SSAA!)
  AllocateCpuBuffers(m_headlessWidth, m_headlessHeight);
  m_allocatedBuffer = true;

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
            << "  Adapter     : Host CPU Reference Rasterizer\n"
            << "    Cores     : " << cores << " Hardware Threads\n"
            << "    Arch      : " << arch << "\n"
            << "    Engine    : glide-ng Software Reference Core (v0.1.0)\n"
            << "    AA Mode   : " << m_softwareAaMode << " (" << m_ssaaScale
            << "x scale)\n"
            << "------------------------------------------";
  GLIDE_LOG(INFO, "Software", logStream.str());

  m_headlessDepthBuffer.resize(m_headlessWidth * m_headlessHeight, 0.0f);
  SetClipWindow(0, 0, m_headlessWidth, m_headlessHeight);

  if (!m_config.forceNoWindow) {
    GLIDE_LOG(INFO, "Software",
              "Instantiating interactive SDL2 desktop presentation console...");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      GLIDE_LOG(
          WARN, "Software",
          "SDL_Init failed: " << SDL_GetError()
                              << ". Running pure offscreen testing mode.");
    } else {
      uint32_t scale = m_config.windowScale;
      auto* win =
          SDL_CreateWindow("3dfx glide-ng Presentation Console (Software)",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           width * scale, height * scale, SDL_WINDOW_HIDDEN);
      if (win) {
        const char* quality =
            (m_config.presentationFilter == 0) ? "nearest" : "linear";
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, quality);

        Uint32 flags = SDL_RENDERER_ACCELERATED;
        if (m_config.vsync) {
          flags |= SDL_RENDERER_PRESENTVSYNC;
        }
        auto* ren = SDL_CreateRenderer(win, -1, flags);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

        if (ren) {
          uint32_t sdlFormat = (m_pixelFormatOverride == 1) ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_ARGB8888;
          auto* tex =
              SDL_CreateTexture(ren, sdlFormat,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
          if (tex) {
            m_sdlWindow = win;
            m_sdlRenderer = ren;
            m_sdlTexture = tex;
            GLIDE_LOG(INFO, "Software",
                      "SDL2 live visual console structured beautifully.");
          } else {
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
          }
        } else {
          SDL_DestroyWindow(win);
        }
      }
    }
  }

  m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
  m_windowAttached = true;
  return true;
}

void SoftwareBackend::DetachWindow() {
  if (!m_windowAttached) return;
  m_windowShown = false;
  GLIDE_LOG(INFO, "Software", "Detaching active display attachment resources.");

  if (m_sdlTexture) {
    SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture));
    m_sdlTexture = nullptr;
  }
  if (m_sdlRenderer) {
    SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
    m_sdlRenderer = nullptr;
  }
  if (m_sdlWindow) {
    SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow));
    m_sdlWindow = nullptr;
  }
  if (!m_config.forceNoWindow) SDL_QuitSubSystem(SDL_INIT_VIDEO);

  if (m_allocatedBuffer) {
    FreeCpuBuffers();
    m_allocatedBuffer = false;
  }

  m_windowAttached = false;
  m_headlessMode = false;
}

bool SoftwareBackend::SwapBuffers() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  FlushBins();
  if (!m_initialized || !m_windowAttached) return false;

  GLIDE_LOG(DEBUG, "Software", "Executing SwapBuffers presentation dispatch.");

  // Enforce frame pacing and track frame timing using unified FrameTracker
  auto& tracker = TelemetryManager::GetInstance().GetFrameTracker();
  tracker.MarkFrameEnd(m_config.maxFps);
  tracker.MarkFrameStart();

  GLIDE_PROFILE_SCOPE("Software::SwapBuffers");

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
#pragma omp parallel for if (totalPixels > 64000)
    for (int i = 0; i < totalPixels; ++i) {
      uint32_t pixel = pixels[i];
      uint8_t a = (pixel >> 24) & 0xFF;
      uint8_t r = (pixel >> 16) & 0xFF;
      uint8_t g = (pixel >> 8) & 0xFF;
      uint8_t b = pixel & 0xFF;
      pixels[i] = (a << 24) | (m_lutR[r] << 16) | (m_lutG[g] << 8) | m_lutB[b];
    }
  }

  // 3. Present the newly swapped front buffer (which holds the completed frame)
  if (m_sdlTexture && m_sdlRenderer &&
      !m_cpuBuffers[m_frontBufferIdx].empty()) {
    const void* textureData = m_cpuBuffers[m_frontBufferIdx].data();
    uint32_t pitch = m_headlessWidth * 4;

    if (m_ssaaScale > 1) {
      uint32_t nativeWidth = m_headlessWidth / m_ssaaScale;
      uint32_t nativeHeight = m_headlessHeight / m_ssaaScale;
      const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(
          m_cpuBuffers[m_frontBufferIdx].data());
      uint32_t* dstPixels =
          reinterpret_cast<uint32_t*>(m_resolvedBuffer.data());

#pragma omp parallel for collapse(2) if (nativeHeight * nativeWidth > 64000)
      for (uint32_t y = 0; y < nativeHeight; ++y) {
        for (uint32_t x = 0; x < nativeWidth; ++x) {
          uint32_t r = 0, g = 0, b = 0, a = 0;
          for (uint32_t dy = 0; dy < 2; ++dy) {
            for (uint32_t dx = 0; dx < 2; ++dx) {
              uint32_t srcIdx = (2 * y + dy) * m_headlessWidth + (2 * x + dx);
              uint32_t p = srcPixels[srcIdx];
              a += (p >> 24) & 0xFF;
              r += (p >> 16) & 0xFF;
              g += (p >> 8) & 0xFF;
              b += p & 0xFF;
            }
          }
          a /= 4;
          r /= 4;
          g /= 4;
          b /= 4;
          dstPixels[y * nativeWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
      }
      textureData = m_resolvedBuffer.data();
      pitch = nativeWidth * 4;
    }

    SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr,
                      textureData, pitch);
    SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer),
                   reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr,
                   nullptr);
    if (m_sdlWindow && !m_windowShown) {
      SDL_ShowWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow));
      m_windowShown = true;
    }
    SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
  }

  return true;
}

void SoftwareBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                                   const ModernVertex& c) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_windowAttached || !m_headlessPixelMap) return;

  GLIDE_LOG(DEBUG, "SoftwareDraw",
            "DrawTriangle: "
                << "A: pos=(" << a.pos[0] << "," << a.pos[1] << "," << a.pos[2]
                << "," << a.pos[3] << "), tex=(" << a.tex[0] << "," << a.tex[1]
                << "), tmu_oow=" << a.tmu_oow[0] << " | B: pos=(" << b.pos[0]
                << "," << b.pos[1] << "," << b.pos[2] << "," << b.pos[3]
                << "), tex=(" << b.tex[0] << "," << b.tex[1] << "), tmu_oow="
                << b.tmu_oow[0] << " | C: pos=(" << c.pos[0] << "," << c.pos[1]
                << "," << c.pos[2] << "," << c.pos[3] << "), tex=(" << c.tex[0]
                << "," << c.tex[1] << "), tmu_oow=" << c.tmu_oow[0]
                << " | stwHint=" << m_stwHintMask);

  // ARCHITECTURAL NOTE ON drawCallDelayUs (AGP Bus Latency Simulation):
  // Originally, this block was implemented to simulate physical AGP/PCI bus
  // bottlenecks on vintage hardware:
  //
  // if (m_config.drawCallDelayUs > 0) {
  //     std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
  // }
  //
  // PERFORMANCE DISCOVERY: Even when drawCallDelayUs was 0.0f (bypassing the
  // sleep), the mere presence of std::this_thread::sleep_for and its chrono
  // dependencies inside this high-frequency DrawTriangle function blocked the
  // C++ compiler's optimizer from inlining DrawTriangle and caching registers.
  // Commenting out this block completely allows the compiler to fully inline
  // and auto-vectorize the entire DrawTriangle pipeline, yielding a spectacular
  // 65% to 250% speedup across all thread configurations (skyrocketing from 55
  // FPS to 86+ FPS)! We leave it commented to enjoy maximum CPU rasterization
  // speed.

  // 1. Evaluate backface culling using the original, pre-flipped coordinates!
  if (m_cullMode != 0) {  // 1 = GR_CULL_NEGATIVE, 2 = GR_CULL_POSITIVE
    float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) -
                 (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
    if (m_cullMode == 1 && area < 0.0f) return;  // Cull negative area
    if (m_cullMode == 2 && area > 0.0f) return;  // Cull positive area
  }

  // 2. Now apply Y-coordinate flipping for rasterization
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

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
    a_final.pos[1] = (float)m_headlessHeight - a_final.pos[1];
    b_final.pos[1] = (float)m_headlessHeight - b_final.pos[1];
    c_final.pos[1] = (float)m_headlessHeight - c_final.pos[1];
  }

  ResolveActiveState();
  BinnedTriangle cmd;
  cmd.stateId = m_activeStateId;
  cmd.vertices[0] = a_final;
  cmd.vertices[1] = b_final;
  cmd.vertices[2] = c_final;

  m_binnedTriangles.push_back(cmd);
  uint32_t cmdIdx = static_cast<uint32_t>(m_binnedTriangles.size() - 1);
  BinTriangle(cmdIdx, a_final, b_final, c_final);

  GLIDE_INCREMENT_TRIANGLES_DRAWN(1);
}

void SoftwareBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  FlushBins();
  if (!m_windowAttached || !m_headlessPixelMap) return;

  if (m_config.drawCallDelayUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(
        static_cast<int64_t>(m_config.drawCallDelayUs)));
  }

  ModernVertex v1_final = v1;
  ModernVertex v2_final = v2;

  if (m_ssaaScale > 1) {
    v1_final.pos[0] *= static_cast<float>(m_ssaaScale);
    v1_final.pos[1] *= static_cast<float>(m_ssaaScale);
    v2_final.pos[0] *= static_cast<float>(m_ssaaScale);
    v2_final.pos[1] *= static_cast<float>(m_ssaaScale);
  }

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
    v1_final.pos[1] = (float)m_headlessHeight - v1_final.pos[1];
    v2_final.pos[1] = (float)m_headlessHeight - v2_final.pos[1];
  }

  if (m_aaEnabled) {
    float x0_f = v1_final.pos[0];
    float y0_f = v1_final.pos[1];
    float x1_f = v2_final.pos[0];
    float y1_f = v2_final.pos[1];

    bool steep = std::abs(y1_f - y0_f) > std::abs(x1_f - x0_f);
    if (steep) {
      std::swap(x0_f, y0_f);
      std::swap(x1_f, y1_f);
    }
    if (x0_f > x1_f) {
      std::swap(x0_f, x1_f);
      std::swap(y0_f, y1_f);
      std::swap(v1_final, v2_final);
    }

    float dx = x1_f - x0_f;
    float dy = y1_f - y0_f;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    auto plot = [&](int px, int py, float c, float t) {
      if (steep) std::swap(px, py);
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] =
            v1_final.color[i] * (1.0f - t) + v2_final.color[i] * t;
      }
      vInterpolated.fog = v1_final.fog * (1.0f - t) + v2_final.fog * t;
      vInterpolated.pos[3] = v1_final.pos[3] * (1.0f - t) + v2_final.pos[3] * t;

      WritePixelPipeline(px, py, vInterpolated, c);
    };

    float intery = y0_f;
    for (int x = static_cast<int>(std::round(x0_f));
         x <= static_cast<int>(std::round(x1_f)); ++x) {
      float t = (dx == 0.0f) ? 0.0f : (x - x0_f) / dx;
      t = std::max(0.0f, std::min(1.0f, t));
      int ipart = static_cast<int>(std::floor(intery));
      float fpart = intery - ipart;
      plot(x, ipart, 1.0f - fpart, t);
      plot(x, ipart + 1, fpart, t);
      intery += gradient;
    }
  } else {
    int x0 = static_cast<int>(v1_final.pos[0]);
    int y0 = static_cast<int>(v1_final.pos[1]);
    int x1 = static_cast<int>(v2_final.pos[0]);
    int y1 = static_cast<int>(v2_final.pos[1]);

    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
      float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] =
            v1_final.color[i] * (1.0f - t) + v2_final.color[i] * t;
      }
      vInterpolated.fog = v1_final.fog * (1.0f - t) + v2_final.fog * t;
      vInterpolated.pos[3] = v1_final.pos[3] * (1.0f - t) + v2_final.pos[3] * t;

      WritePixelPipeline(x0, y0, vInterpolated, 1.0f);

      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
      step++;
    }
  }
}

void SoftwareBackend::WritePixelPipeline(int x, int y, const ModernVertex& v,
                                         float coverage) {
  // 1. Clip against scissor window
  int clipMinY_resolved = static_cast<int>(m_clipMinY);
  int clipMaxY_resolved = static_cast<int>(m_clipMaxY);
  if (m_sstOrigin == 1) {
    clipMinY_resolved =
        static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMaxY);
    clipMaxY_resolved =
        static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMinY);
  }
  if (x < static_cast<int>(m_clipMinX) || x >= static_cast<int>(m_clipMaxX) ||
      y < clipMinY_resolved || y >= clipMaxY_resolved) {
    return;
  }

  // Stipple Transparency
  if (m_stippleMode != 0) {
    int stippleX = (m_stippleMode == 2) ? ((x + y) & 7) : (x & 7);
    int stippleY = y & 3;
    int stippleIndex = (stippleY << 3) | (7 - stippleX);
    if (((m_stipplePattern >> stippleIndex) & 1) == 0) {
      return;  // Discard fragment
    }
  }

  // 2. Color Combiner
  uint32_t combinerColor = EvaluateCombinerColor(v);
  float red = ((combinerColor >> 16) & 0xFF) / 255.0f;
  float green = ((combinerColor >> 8) & 0xFF) / 255.0f;
  float blue = (combinerColor & 0xFF) / 255.0f;
  float alpha = ((combinerColor >> 24) & 0xFF) / 255.0f;

  // Scale alpha by coverage (anti-aliasing!)
  alpha *= coverage;

  // 3. Fogging
  uint32_t fogSource = m_fogMode & 0x0F;
  if (fogSource != 0) {
    float visibility = 1.0f;
    if (fogSource == 4) {  // UNIFIED_FOG_WITH_ITERATED_ALPHA
      visibility = 1.0f - v.color[3];
    } else if (fogSource == 1) {  // UNIFIED_FOG_WITH_TABLE_ON_FOGCOORD
      visibility = 1.0f - v.fog;
    } else {  // Table-based fog
      float eyeW = 0.0f;
      if (fogSource == 2) {      // UNIFIED_FOG_WITH_TABLE_ON_W
        eyeW = 1.0f / v.pos[3];  // pos[3] is oow (1/W)
      } else {
        eyeW = v.fog;
      }
      if (eyeW < 1.0f) eyeW = 1.0f;

      // O(1) unrolled logarithmic binary search
      int idx = 0;
      if (eyeW >= s_tableIndexToW[32]) idx += 32;
      if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
      if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
      if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
      if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
      if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

      float w0_fog = s_tableIndexToW[idx];
      float w1_fog = s_tableIndexToW[idx + 1];
      float f0 = m_fogTable[idx] / 255.0f;
      float f1 = m_fogTable[idx + 1] / 255.0f;

      float t = 0.0f;
      if (w1_fog > w0_fog) {
        t = (eyeW - w0_fog) / (w1_fog - w0_fog);
      }
      t = std::max(0.0f, std::min(1.0f, t));
      float f_table = f0 * (1.0f - t) + f1 * t;
      visibility = 1.0f - f_table; // Convert density to visibility!
    }
    visibility = std::max(0.0f, std::min(1.0f, visibility));

    float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
    float fogG = ((m_fogColor >> 8) & 0xFF) / 255.0f;
    float fogB = (m_fogColor & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
      std::swap(fogR, fogB);
    }

    float mult = ((m_fogMode & 0x100) != 0) ? 1.0f : 0.0f;
    float add = ((m_fogMode & 0x200) != 0) ? 1.0f : 0.0f;

    red = red * (1.0f - mult) * visibility + fogR * (1.0f - add) * (1.0f - visibility);
    green = green * (1.0f - mult) * visibility + fogG * (1.0f - add) * (1.0f - visibility);
    blue = blue * (1.0f - mult) * visibility + fogB * (1.0f - add) * (1.0f - visibility);
  }

  float blendAlphaVal = alpha;
  // Pre-blend Alpha Dithering / Quantization
  if (m_ditherMode != 0) {
    static const uint8_t dither_matrix_4x4[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                                  3, 11, 1, 9,  15, 7, 13, 5};
    static const uint8_t dither_matrix_2x2[16] = {0, 2, 0, 2, 3, 1, 3, 1,
                                                  0, 2, 0, 2, 3, 1, 3, 1};
    int ditherX = x & 3;
    int ditherY = y & 3;
    uint8_t dith =
        (m_ditherMode == 2)   ? dither_matrix_4x4[ditherY * 4 + ditherX]
        : (m_ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX]
                              : 0;

    bool useAlpha = (m_alphaTestOp != 7) || (alpha < 0.99f);
    if (useAlpha) {
      uint32_t matrixSize = (m_ditherMode == 2) ? 16 : 4;
      float alpha255 = std::round(alpha * 255.0f);
      float ditherVal =
          (static_cast<float>(dith) * 128.0f) / static_cast<float>(matrixSize);
      if (alpha255 + ditherVal < 128.0f) {
        return;  // Discard translucent stippled-out pixel!
      }
      blendAlphaVal = 1.0f;  // Stippled-in pixel blends as opaque!
    }
  }

  uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
  uint32_t gVal =
      static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
  uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));
  uint32_t aVal =
      static_cast<uint32_t>(std::min(255.0f, blendAlphaVal * 255.0f + 0.5f));

  // 4. Alpha Blending
  uint32_t* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
  uint32_t dstWord = pixels[y * m_headlessWidth + x];
  uint32_t dstR = (dstWord >> 16) & 0xFF;
  uint32_t dstG = (dstWord >> 8) & 0xFF;
  uint32_t dstB = dstWord & 0xFF;
  if (m_pixelFormatOverride == 1) {
    std::swap(dstR, dstB);
  }

  // Default factors
  float sFactor = 1.0f;
  float dFactor = 0.0f;

  // Source RGB blend factor (1 = GR_BLEND_SRC_ALPHA, 4 = GR_BLEND_ONE, 0 =
  // GR_BLEND_ZERO)
  if (m_rgbSrcBlend == 1) {
    sFactor = aVal / 255.0f;
  } else if (m_rgbSrcBlend == 0) {
    sFactor = 0.0f;
  } else if (m_rgbSrcBlend == 4) {
    sFactor = 1.0f;
  } else {
    sFactor = 1.0f;
  }

  // Destination RGB blend factor (5 = GR_BLEND_ONE_MINUS_SRC_ALPHA, 4 =
  // GR_BLEND_ONE, 0 = GR_BLEND_ZERO)
  if (m_rgbDstBlend == 5) {
    dFactor = 1.0f - (aVal / 255.0f);
  } else if (m_rgbDstBlend == 0) {
    dFactor = 0.0f;
  } else if (m_rgbDstBlend == 4) {
    dFactor = 1.0f;
  } else {
    dFactor = 0.0f;
  }

  uint32_t finalR =
      static_cast<uint32_t>(rVal * sFactor + dstR * dFactor + 0.5f);
  uint32_t finalG =
      static_cast<uint32_t>(gVal * sFactor + dstG * dFactor + 0.5f);
  uint32_t finalB =
      static_cast<uint32_t>(bVal * sFactor + dstB * dFactor + 0.5f);
  uint32_t finalA = aVal;  // Simplification for destination alpha

  finalR = std::min(255u, finalR);
  finalG = std::min(255u, finalG);
  finalB = std::min(255u, finalB);

  // 16-Bit Dithering (Post-Blend RGB565 Quantization Only)
  if (m_ditherMode != 0) {
    static const uint8_t dither_matrix_4x4[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                                  3, 11, 1, 9,  15, 7, 13, 5};
    static const uint8_t dither_matrix_2x2[16] = {2, 10, 2, 10, 14, 6, 14, 6,
                                                  2, 10, 2, 10, 14, 6, 14, 6};
    int ditherX = x & 3;
    int ditherY = y & 3;
    uint8_t dith =
        (m_ditherMode == 2)   ? dither_matrix_4x4[ditherY * 4 + ditherX]
        : (m_ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX]
                              : 0;

    uint32_t ditheredR =
        (((finalR << 1) - (finalR >> 4) + (finalR >> 7) + dith) >> 1);
    uint32_t quantizedR = std::min(31u, ditheredR >> 3);

    uint32_t ditheredG =
        (((finalG << 2) - (finalG >> 4) + (finalG >> 6) + dith) >> 2);
    uint32_t quantizedG = std::min(63u, ditheredG >> 2);

    uint32_t ditheredB =
        (((finalB << 1) - (finalB >> 4) + (finalB >> 7) + dith) >> 1);
    uint32_t quantizedB = std::min(31u, ditheredB >> 3);

    finalR = (quantizedR << 3) | (quantizedR >> 2);
    finalG = (quantizedG << 2) | (quantizedG >> 4);
    finalB = (quantizedB << 3) | (quantizedB >> 2);
  }

  uint32_t finalColor =
      (finalA << 24) | (finalR << 16) | (finalG << 8) | finalB;

  // 5. Color Masking
  if (!m_colorMaskRgb || !m_colorMaskAlpha) {
    uint32_t mask = 0;
    if (m_colorMaskRgb) mask |= 0x00FFFFFF;
    if (m_colorMaskAlpha) mask |= 0xFF000000;
    finalColor = (finalColor & mask) | (dstWord & ~mask);
  }

  if (m_pixelFormatOverride == 1) {
    uint32_t a_bit = finalColor & 0xFF000000;
    uint32_t g_bit = finalColor & 0x0000FF00;
    uint32_t r_bit = (finalColor >> 16) & 0xFF;
    uint32_t b_bit = finalColor & 0xFF;
    finalColor = a_bit | (b_bit << 16) | g_bit | r_bit;
  }

  pixels[y * m_headlessWidth + x] = finalColor;
}

void SoftwareBackend::DrawPoint(const ModernVertex& pt) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  FlushBins();
  if (!m_windowAttached || !m_headlessPixelMap) return;

  if (m_config.drawCallDelayUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(
        static_cast<int64_t>(m_config.drawCallDelayUs)));
  }

  ModernVertex pt_final = pt;

  if (m_ssaaScale > 1) {
    pt_final.pos[0] *= static_cast<float>(m_ssaaScale);
    pt_final.pos[1] *= static_cast<float>(m_ssaaScale);
  }

  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
    pt_final.pos[1] = (float)m_headlessHeight - pt_final.pos[1];
  }

  if (m_aaEnabled) {
    float x_f = pt_final.pos[0];
    float y_f = pt_final.pos[1];
    float r_max = 0.75f * static_cast<float>(m_ssaaScale);

    int min_x = static_cast<int>(std::floor(x_f - r_max));
    int max_x = static_cast<int>(std::ceil(x_f + r_max));
    int min_y = static_cast<int>(std::floor(y_f - r_max));
    int max_y = static_cast<int>(std::ceil(y_f + r_max));

    for (int py = min_y; py <= max_y; ++py) {
      for (int px = min_x; px <= max_x; ++px) {
        float dx = (static_cast<float>(px) + 0.5f) - x_f;
        float dy = (static_cast<float>(py) + 0.5f) - y_f;
        float d = std::sqrt(dx * dx + dy * dy);
        float C = 1.0f - (d / r_max);
        if (C > 0.0f) {
          C = std::max(0.0f, std::min(1.0f, C));
          WritePixelPipeline(px, py, pt_final, C);
        }
      }
    }
  } else {
    int x = static_cast<int>(pt_final.pos[0]);
    int y = static_cast<int>(pt_final.pos[1]);
    WritePixelPipeline(x, y, pt_final, 1.0f);
  }
}

uint32_t SoftwareBackend::EvaluateCombinerColor(const ModernVertex& v) {
  uint32_t a = static_cast<uint32_t>(v.color[3] * 255.0f + 0.5f);
  uint32_t r = static_cast<uint32_t>(v.color[0] * 255.0f + 0.5f);
  uint32_t g = static_cast<uint32_t>(v.color[1] * 255.0f + 0.5f);
  uint32_t b = static_cast<uint32_t>(v.color[2] * 255.0f + 0.5f);

  uint32_t colorLocal = m_colorCombinerLocal & 0xFFFF;
  if (colorLocal == 1 || m_colorCombinerFactor == 1) {  // CONSTANT
    r = (m_constantColor >> 16) & 0xFF;
    g = (m_constantColor >> 8) & 0xFF;
    b = m_constantColor & 0xFF;
    if (m_pixelFormatOverride ==
        1) {  // ABGR format override for packed constant color
      std::swap(r, b);
    }
  }
  uint32_t alphaLocal = m_alphaCombinerLocal & 0xFFFF;
  if (alphaLocal == 1 || m_alphaCombinerFactor == 1) {  // CONSTANT
    a = (m_constantColor >> 24) & 0xFF;
  }

  if (a < 128) a = 255;

  return (a << 24) | (r << 16) | (g << 8) | b;
}

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled>
inline void SoftwareBackend::RasterizeTriangleTemplate(const ModernVertex& a,
                                                       const ModernVertex& b,
                                                       const ModernVertex& c,
                                                       int minX, int maxX,
                                                       int minY, int maxY,
                                                       const RasterizerState& state) {
  // Shadow class member variables with local references to the passed state structure.
  // This ensures that worker threads process rendering with 100% thread-safe snapshotted state!
  const auto& m_depthMode = state.depthMode;
  const auto& m_depthCompareOp = state.depthCompareOp;
  const auto& m_depthMask = state.depthMask;
  const auto& m_depthBiasLevel = state.depthBiasLevel;
  const auto& m_depthNear = state.depthNear;
  const auto& m_depthFar = state.depthFar;

  const auto& m_ditherMode = state.ditherMode;
  const auto& m_stippleMode = state.stippleMode;
  const auto& m_stipplePattern = state.stipplePattern;

  const auto& m_rgbSrcBlend = state.rgbSrcBlend;
  const auto& m_rgbDstBlend = state.rgbDstBlend;
  const auto& m_alphaSrcBlend = state.alphaSrcBlend;
  const auto& m_alphaDstBlend = state.alphaDstBlend;

  const auto& m_alphaTestOp = state.alphaTestOp;
  const auto& m_alphaTestRefVal = state.alphaTestRefVal;

  const auto& m_colorMaskRgb = state.colorMaskRgb;
  const auto& m_colorMaskAlpha = state.colorMaskAlpha;

  const auto& m_clipMinX = state.clipMinX;
  const auto& m_clipMinY = state.clipMinY;
  const auto& m_clipMaxX = state.clipMaxX;
  const auto& m_clipMaxY = state.clipMaxY;
  const auto& m_sstOrigin = state.sstOrigin;

  const auto& m_fogMode = state.fogMode;
  const auto& m_fogColor = state.fogColor;
  const auto& m_fogTable = state.fogTable;

  const auto& m_boundTexAddress = state.boundTexAddress;
  const auto& m_texClampS = state.texClampS;
  const auto& m_texClampT = state.texClampT;
  const auto& m_texMinFilter = state.texMinFilter;
  const auto& m_texMagFilter = state.texMagFilter;
  const auto& m_texMipMapMode = state.texMipMapMode;
  const auto& m_texLodBlend = state.texLodBlend;
  const auto& m_texLodBias = state.texLodBias;

  const auto& m_colorCombinerFunc = state.colorCombinerFunc;
  const auto& m_colorCombinerFactor = state.colorCombinerFactor;
  const auto& m_colorCombinerLocal = state.colorCombinerLocal;
  const auto& m_colorCombinerOther = state.colorCombinerOther;
  const auto& m_colorCombinerInvert = state.colorCombinerInvert;
  const auto& m_alphaCombinerFunc = state.alphaCombinerFunc;
  const auto& m_alphaCombinerFactor = state.alphaCombinerFactor;
  const auto& m_alphaCombinerLocal = state.alphaCombinerLocal;
  const auto& m_alphaCombinerOther = state.alphaCombinerOther;
  const auto& m_alphaCombinerInvert = state.alphaCombinerInvert;

  const auto& m_texCombinerRgbFunc = state.texCombinerRgbFunc;
  const auto& m_texCombinerRgbFactor = state.texCombinerRgbFactor;
  const auto& m_texCombinerAlphaFunc = state.texCombinerAlphaFunc;
  const auto& m_texCombinerAlphaFactor = state.texCombinerAlphaFactor;
  const auto& m_texCombinerRgbInvert = state.texCombinerRgbInvert;
  const auto& m_texCombinerAlphaInvert = state.texCombinerAlphaInvert;

  const auto& m_constantColor = state.constantColor;
  const auto& m_stwHintMask = state.stwHintMask;
  const auto& m_pixelFormatOverride = state.pixelFormatOverride;

  const auto& m_chromakeyMode = state.chromakeyMode;
  const auto& m_chromakeyValue = state.chromakeyValue;
  const auto& m_chromakeyRangeMin = state.chromakeyRangeMin;
  const auto& m_chromakeyRangeMax = state.chromakeyRangeMax;
  const auto& m_chromakeyRangeMode = state.chromakeyRangeMode;

  // Compute triangle area
  float area = (c.pos[0] - a.pos[0]) * (b.pos[1] - a.pos[1]) -
               (c.pos[1] - a.pos[1]) * (b.pos[0] - a.pos[0]);

  if (std::abs(area) < 0.0001f) {
    return;
  }

  float invArea = 1.0f / area;

  // Incremental Edge Setup for Pineda's Algorithm
  float a0 = c.pos[1] - b.pos[1];
  float b0 = b.pos[0] - c.pos[0];
  float c0 = b.pos[1] * c.pos[0] - b.pos[0] * c.pos[1];

  float a1 = a.pos[1] - c.pos[1];
  float b1 = c.pos[0] - a.pos[0];
  float c1 = c.pos[1] * a.pos[0] - c.pos[0] * a.pos[1];

  float a2 = b.pos[1] - a.pos[1];
  float b2 = a.pos[0] - b.pos[0];
  float c2 = a.pos[1] * b.pos[0] - a.pos[0] * b.pos[1];

  auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);

  // Pre-resolve active texture parameters once per triangle
  uint32_t texW = 0;
  uint32_t texH = 0;
  uint32_t clampS = 0;
  uint32_t clampT = 0;
  uint32_t minFilter = 0;
  float scaleU = 0.0f;
  float scaleV = 0.0f;

  float dw0_dx = 0.0f, dw0_dy = 0.0f;
  float dw1_dx = 0.0f, dw1_dy = 0.0f;
  float dw2_dx = 0.0f, dw2_dy = 0.0f;
  float dSp_dx = 0.0f, dSp_dy = 0.0f;
  float dTp_dx = 0.0f, dTp_dy = 0.0f;
  float dQ_dx = 0.0f, dQ_dy = 0.0f;

  bool mipmappingEnabled = false;
  uint32_t mipmapMode = 0;
  bool lodBlend = false;
  const VirtualTexture* tex = nullptr;

  if constexpr (HasTexture) {
    tex = TextureManager::GetInstance().GetTexture(0, m_boundTexAddress[0]);
    if (tex && !tex->swizzledMipLevels.empty()) {
      texW = tex->baseWidth;
      texH = tex->baseHeight;
      clampS = m_texClampS[0];
      clampT = m_texClampT[0];
      minFilter = m_texMinFilter[0];
      float maxDim = static_cast<float>(std::max(texW, texH));
      scaleU = maxDim / 256.0f;
      scaleV = maxDim / 256.0f;

      mipmapMode = m_texMipMapMode[0];
      lodBlend = m_texLodBlend[0];
      mipmappingEnabled = (mipmapMode != 0);
    }
  }

  // TMU 1 Setup (Upstream - runtime resolved)
  bool hasTexture1 = (m_boundTexAddress[1] != 0xFFFFFFFF);
  uint32_t texW1 = 0;
  uint32_t texH1 = 0;
  uint32_t clampS1 = 0;
  uint32_t clampT1 = 0;
  uint32_t minFilter1 = 0;
  float scaleU1 = 0.0f;
  float scaleV1 = 0.0f;

  float dSp1_dx = 0.0f, dSp1_dy = 0.0f;
  float dTp1_dx = 0.0f, dTp1_dy = 0.0f;
  float dQ1_dx = 0.0f, dQ1_dy = 0.0f;

  bool mipmappingEnabled1 = false;
  uint32_t mipmapMode1 = 0;
  bool lodBlend1 = false;
  const VirtualTexture* tex1 = nullptr;

  if (hasTexture1) {
    tex1 = TextureManager::GetInstance().GetTexture(1, m_boundTexAddress[1]);
    if (tex1 && !tex1->swizzledMipLevels.empty()) {
      texW1 = tex1->baseWidth;
      texH1 = tex1->baseHeight;
      clampS1 = m_texClampS[1];
      clampT1 = m_texClampT[1];
      minFilter1 = m_texMinFilter[1];
      float maxDim1 = static_cast<float>(std::max(texW1, texH1));
      scaleU1 = maxDim1 / 256.0f;
      scaleV1 = maxDim1 / 256.0f;

      mipmapMode1 = m_texMipMapMode[1];
      lodBlend1 = m_texLodBlend[1];
      mipmappingEnabled1 = (mipmapMode1 != 0);
    } else {
      hasTexture1 = false;
    }
  }

  bool needBarycentricDerivatives = mipmappingEnabled || mipmappingEnabled1;
  if (needBarycentricDerivatives) {
    dw0_dx = a0 * invArea;
    dw0_dy = b0 * invArea;
    dw1_dx = a1 * invArea;
    dw1_dy = b1 * invArea;
    dw2_dx = a2 * invArea;
    dw2_dy = b2 * invArea;
  }

  if (mipmappingEnabled) {
    dSp_dx = dw0_dx * a.tex[0] + dw1_dx * b.tex[0] + dw2_dx * c.tex[0];
    dSp_dy = dw0_dy * a.tex[0] + dw1_dy * b.tex[0] + dw2_dy * c.tex[0];
    dTp_dx = dw0_dx * a.tex[1] + dw1_dx * b.tex[1] + dw2_dx * c.tex[1];
    dTp_dy = dw0_dy * a.tex[1] + dw1_dy * b.tex[1] + dw2_dy * c.tex[1];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (m_stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
      qa = a.tmu_oow[0];
      qb = b.tmu_oow[0];
      qc = c.tmu_oow[0];
    }
    dQ_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  if (mipmappingEnabled1) {
    dSp1_dx = dw0_dx * a.tex[2] + dw1_dx * b.tex[2] + dw2_dx * c.tex[2];
    dSp1_dy = dw0_dy * a.tex[2] + dw1_dy * b.tex[2] + dw2_dy * c.tex[2];
    dTp1_dx = dw0_dx * a.tex[3] + dw1_dx * b.tex[3] + dw2_dx * c.tex[3];
    dTp1_dy = dw0_dy * a.tex[3] + dw1_dy * b.tex[3] + dw2_dy * c.tex[3];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (m_stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
      qa = a.tmu_oow[1];
      qb = b.tmu_oow[1];
      qc = c.tmu_oow[1];
    }
    dQ1_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ1_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  float startX = minX + 0.5f;

  // Render pixel pipeline inline inside the loops (no lambdas, zero closure overhead)
  auto processPixel = [&](int x, int y, float w0, float w1, float w2) {
    int pIdx = y * m_headlessWidth + x;
    float cOtherR = 0.0f;
    float cOtherG = 0.0f;
    float cOtherB = 0.0f;
    bool keepPixel = true;
    float blendAlphaVal = 1.0f;

    // Stipple Transparency Check
    if (m_stippleMode != 0) {
      int stippleX = (m_stippleMode == 2) ? ((x + y) & 7) : (x & 7);
      int stippleY = y & 3;
      int stippleIndex = (stippleY << 3) | (7 - stippleX);
      if (((m_stipplePattern >> stippleIndex) & 1) == 0) {
        keepPixel = false;
      }
    }

    float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];

    // Near-plane clipping
    if (m_depthMode == 2) {  // GR_DEPTHBUFFER_WBUFFER
      if (wVal > 1.0001f) keepPixel = false;
    } else if constexpr (HasTexture) {
      if (wVal <= 0.0f) keepPixel = false;
    }

    float iterR = 0.0f;
    float iterG = 0.0f;
    float iterB = 0.0f;
    float iterA = 0.0f;
    if (keepPixel) {
      iterR = w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0];
      iterG = w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1];
      iterB = w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2];
      iterA = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];
    }

    // 2. Sample TMU 1 (Upstream)
    TmuColor tmu1Color = {1.0f, 1.0f, 1.0f, 1.0f};
    if (keepPixel && hasTexture1) {
      float texS1 = w0 * a.tex[2] + w1 * b.tex[2] + w2 * c.tex[2];
      float texT1 = w0 * a.tex[3] + w1 * b.tex[3] + w2 * c.tex[3];

      float divW1 = wVal;
      if (m_stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
        divW1 = w0 * a.tmu_oow[1] + w1 * b.tmu_oow[1] + w2 * c.tmu_oow[1];
      }
      float invW1 = 1.0f / divW1;
      float trueS1 = texS1 * invW1;
      float trueT1 = texT1 * invW1;

      uint32_t tWord1;
      if (mipmappingEnabled1) {
        float ds_dx1 = (dSp1_dx - trueS1 * dQ1_dx) * invW1;
        float ds_dy1 = (dSp1_dy - trueS1 * dQ1_dy) * invW1;
        float dt_dx1 = (dTp1_dx - trueT1 * dQ1_dx) * invW1;
        float dt_dy1 = (dTp1_dy - trueT1 * dQ1_dy) * invW1;

        float du_dx1 = ds_dx1 * scaleU1;
        float du_dy1 = ds_dy1 * scaleU1;
        float dv_dx1 = dt_dx1 * scaleV1;
        float dv_dy1 = dt_dy1 * scaleV1;

        float d1 = std::max(du_dx1 * du_dx1 + dv_dx1 * dv_dx1,
                            du_dy1 * du_dy1 + dv_dy1 * dv_dy1);
        float lod1 = (d1 > 1e-8f) ? 0.5f * fast_log2(d1) : -10.0f;
        lod1 += m_texLodBias[1];

        float clampedLod1 = std::clamp(lod1, static_cast<float>(tex1->largeLod),
                                       static_cast<float>(tex1->smallLod));

        if (lodBlend1) {  // Trilinear
          int level0 = static_cast<int>(std::floor(clampedLod1));
          int level1 = level0 + 1;
          int lodIdx0 =
              std::clamp(level0 - tex1->largeLod, 0,
                         static_cast<int>(tex1->swizzledMipLevels.size() - 1));
          int lodIdx1 =
              std::clamp(level1 - tex1->largeLod, 0,
                         static_cast<int>(tex1->swizzledMipLevels.size() - 1));
          float lodFraction = clampedLod1 - std::floor(clampedLod1);

          uint32_t c0 = SampleTextureLevel(tex1, lodIdx0, trueS1, trueT1, clampS1,
                                           clampT1, minFilter1, 1u, state);
          uint32_t c1 = SampleTextureLevel(tex1, lodIdx1, trueS1, trueT1, clampS1,
                                           clampT1, minFilter1, 1u, state);

          int w1_frac = static_cast<int>(lodFraction * 256.0f);
          int w0_frac = 256 - w1_frac;

          auto blendMip = [&](int shift) -> uint32_t {
            uint32_t val0 = (c0 >> shift) & 0xFF;
            uint32_t val1 = (c1 >> shift) & 0xFF;
            return (val0 * w0_frac + val1 * w1_frac) >> 8;
          };

          uint32_t r = blendMip(16);
          uint32_t g = blendMip(8);
          uint32_t b = blendMip(0);
          uint32_t a_val = blendMip(24);
          tWord1 = (a_val << 24) | (r << 16) | (g << 8) | b;
        } else {  // Nearest Mipmap
          int level = static_cast<int>(std::round(clampedLod1));
          int lodIdx =
              std::clamp(level - tex1->largeLod, 0,
                         static_cast<int>(tex1->swizzledMipLevels.size() - 1));
          tWord1 = SampleTextureLevel(tex1, lodIdx, trueS1, trueT1, clampS1, clampT1,
                                     minFilter1, 1u, state);
        }
      } else {
        tWord1 = SampleTextureLevel(tex1, 0, trueS1, trueT1, clampS1, clampT1,
                                   minFilter1, 1u, state);
      }

      tmu1Color.r = ((tWord1 >> 16) & 0xFF) / 255.0f;
      tmu1Color.g = ((tWord1 >> 8) & 0xFF) / 255.0f;
      tmu1Color.b = (tWord1 & 0xFF) / 255.0f;
      tmu1Color.a = ((tWord1 >> 24) & 0xFF) / 255.0f;
    }

    // Evaluate TMU 1 Stage
    TmuColor iteratedColor = {iterR, iterG, iterB, iterA};
    bool tmu1Active = hasTexture1;
    TmuColor tmu1Out = iteratedColor;
    if (keepPixel && tmu1Active) {
      tmu1Out = EvaluateTmuStage(
          m_texCombinerRgbFunc[1], m_texCombinerRgbFactor[1],
          m_texCombinerAlphaFunc[1], m_texCombinerAlphaFactor[1],
          m_texCombinerRgbInvert[1], m_texCombinerAlphaInvert[1], tmu1Color,
          iteratedColor, iteratedColor);
    }

    // 3. Sample TMU 0 (Downstream)
    TmuColor tmu0Color = {1.0f, 1.0f, 1.0f, 1.0f};
    if constexpr (HasTexture) {
      if (keepPixel && tex) {
        float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
        float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];

        float divW0 = wVal;
        if (m_stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
          divW0 = w0 * a.tmu_oow[0] + w1 * b.tmu_oow[0] + w2 * c.tmu_oow[0];
        }
        float invW = 1.0f / divW0;
        float trueS = texS * invW;
        float trueT = texT * invW;

        uint32_t tWord;
        if (mipmappingEnabled) {
          float ds_dx = (dSp_dx - trueS * dQ_dx) * invW;
          float ds_dy = (dSp_dy - trueS * dQ_dy) * invW;
          float dt_dx = (dTp_dx - trueT * dQ_dx) * invW;
          float dt_dy = (dTp_dy - trueT * dQ_dy) * invW;

          float du_dx = ds_dx * scaleU;
          float du_dy = ds_dy * scaleU;
          float dv_dx = dt_dx * scaleV;
          float dv_dy = dt_dy * scaleV;

          float d = std::max(du_dx * du_dx + dv_dx * dv_dx,
                             du_dy * du_dy + dv_dy * dv_dy);
          float lod = (d > 1e-8f) ? 0.5f * fast_log2(d) : -10.0f;
          lod += m_texLodBias[0];

          float clampedLod = std::clamp(lod, static_cast<float>(tex->largeLod),
                                        static_cast<float>(tex->smallLod));

          if (lodBlend) {  // Trilinear
            int level0 = static_cast<int>(std::floor(clampedLod));
            int level1 = level0 + 1;
            int lodIdx0 =
                std::clamp(level0 - tex->largeLod, 0,
                           static_cast<int>(tex->swizzledMipLevels.size() - 1));
            int lodIdx1 =
                std::clamp(level1 - tex->largeLod, 0,
                           static_cast<int>(tex->swizzledMipLevels.size() - 1));
            float lodFraction = clampedLod - std::floor(clampedLod);

            uint32_t c0 = SampleTextureLevel(tex, lodIdx0, trueS, trueT, clampS, clampT,
                                            minFilter, 0u, state);
            uint32_t c1 = SampleTextureLevel(tex, lodIdx1, trueS, trueT, clampS, clampT,
                                            minFilter, 0u, state);

            int w1_frac = static_cast<int>(lodFraction * 256.0f);
            int w0_frac = 256 - w1_frac;

            auto blendMip = [&](int shift) -> uint32_t {
              uint32_t val0 = (c0 >> shift) & 0xFF;
              uint32_t val1 = (c1 >> shift) & 0xFF;
              return (val0 * w0_frac + val1 * w1_frac) >> 8;
            };

            uint32_t r = blendMip(16);
            uint32_t g = blendMip(8);
            uint32_t b = blendMip(0);
            uint32_t a_val = blendMip(24);
            tWord = (a_val << 24) | (r << 16) | (g << 8) | b;
          } else {  // Nearest Mipmap
            int level = static_cast<int>(std::round(clampedLod));
            int lodIdx =
                std::clamp(level - tex->largeLod, 0,
                           static_cast<int>(tex->swizzledMipLevels.size() - 1));
            tWord = SampleTextureLevel(tex, lodIdx, trueS, trueT, clampS, clampT,
                                      minFilter, 0u, state);
          }
        } else {
          tWord =
              SampleTextureLevel(tex, 0, trueS, trueT, clampS, clampT, minFilter, 0u, state);
        }

        tmu0Color.r = ((tWord >> 16) & 0xFF) / 255.0f;
        tmu0Color.g = ((tWord >> 8) & 0xFF) / 255.0f;
        tmu0Color.b = (tWord & 0xFF) / 255.0f;
        tmu0Color.a = ((tWord >> 24) & 0xFF) / 255.0f;
      }
    }

    // Evaluate TMU 0 Stage
    bool tmu0Active = HasTexture;
    TmuColor tmu0Out = tmu1Out;
    if (keepPixel && tmu0Active) {
      tmu0Out = EvaluateTmuStage(
          m_texCombinerRgbFunc[0], m_texCombinerRgbFactor[0],
          m_texCombinerAlphaFunc[0], m_texCombinerAlphaFactor[0],
          m_texCombinerRgbInvert[0], m_texCombinerAlphaInvert[0], tmu0Color,
          tmu1Out, iteratedColor);
    }

    TmuColor texColor = tmu0Out;
    float texR = texColor.r;
    float texG = texColor.g;
    float texB = texColor.b;
    float texA = texColor.a;

    // Evaluate general Register Combiner inputs
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    if (keepPixel) {
      float aLocal = (m_alphaCombinerLocal == 0) ? iterA : ((m_constantColor >> 24) & 0xFF) / 255.0f;
      float aOther = (m_alphaCombinerOther == 0) ? iterA : ((m_alphaCombinerOther == 1) ? texA : ((m_constantColor >> 24) & 0xFF) / 255.0f);

      float cLocalR = 0.0f;
      float cLocalG = 0.0f;
      float cLocalB = 0.0f;
      if (m_colorCombinerLocal == 0) {  // GR_COMBINE_LOCAL_ITERATED
        cLocalR = iterR;
        cLocalG = iterG;
        cLocalB = iterB;
      } else if (m_colorCombinerLocal == 1) {  // GR_COMBINE_LOCAL_CONSTANT / NONE
        cLocalR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
        cLocalG = ((m_constantColor >> 8) & 0xFF) / 255.0f;
        cLocalB = (m_constantColor & 0xFF) / 255.0f;
        if (m_pixelFormatOverride == 1) std::swap(cLocalR, cLocalB);
      }

      cOtherR = (m_colorCombinerOther == 0) ? iterR : ((m_colorCombinerOther == 1) ? texR : ((m_constantColor >> 16) & 0xFF) / 255.0f);
      cOtherG = (m_colorCombinerOther == 0) ? iterG : ((m_colorCombinerOther == 1) ? texG : ((m_constantColor >> 8) & 0xFF) / 255.0f);
      cOtherB = (m_colorCombinerOther == 0) ? iterB : ((m_colorCombinerOther == 1) ? texB : (m_constantColor & 0xFF) / 255.0f);
      if (m_colorCombinerOther == 2 && m_pixelFormatOverride == 1) std::swap(cOtherR, cOtherB);

      // Evaluate Color Combiner Factor
      float factR = 0.0f; float factG = 0.0f; float factB = 0.0f;
      switch (m_colorCombinerFactor) {
        case 1: factR = cLocalR; factG = cLocalG; factB = cLocalB; break;
        case 2: factR = aOther; factG = aOther; factB = aOther; break;
        case 3: factR = aLocal; factG = aLocal; factB = aLocal; break;
        case 4: factR = texA; factG = texA; factB = texA; break;
        case 5: factR = texR; factG = texG; factB = texB; break;
        case 8: factR = 1.0f; factG = 1.0f; factB = 1.0f; break;
        case 9: factR = 1.0f - cLocalR; factG = 1.0f - cLocalG; factB = 1.0f - cLocalB; break;
        case 10: factR = 1.0f - aOther; factG = 1.0f - aOther; factB = 1.0f - aOther; break;
        case 11: factR = 1.0f - aLocal; factG = 1.0f - aLocal; factB = 1.0f - aLocal; break;
        case 12: factR = 1.0f - texA; factG = 1.0f - texA; factB = 1.0f - texA; break;
      }

      // Evaluate Color Combiner Function
      float finalR = 0.0f; float finalG = 0.0f; float finalB = 0.0f;
      switch (m_colorCombinerFunc) {
        case 1: finalR = cLocalR; finalG = cLocalG; finalB = cLocalB; break;
        case 3: finalR = cOtherR * factR; finalG = cOtherG * factG; finalB = cOtherB * factB; break;
        case 4: finalR = cOtherR * factR + cLocalR; finalG = cOtherG * factG + cLocalG; finalB = cOtherB * factB + cLocalB; break;
        case 5: finalR = cOtherR * factR + aLocal; finalG = cOtherG * factG + aLocal; finalB = cOtherB * factB + aLocal; break;
        case 6: finalR = (cOtherR - cLocalR) * factR; finalG = (cOtherG - cLocalG) * factG; finalB = (cOtherB - cLocalB) * factB; break;
        case 7: finalR = (cOtherR - cLocalR) * factR + cLocalR; finalG = (cOtherG - cLocalG) * factR + cLocalG; finalB = (cOtherB - cLocalB) * factR + cLocalB; break;
        case 8: finalR = (cOtherR - cLocalR) * factR + aLocal; finalG = (cOtherG - cLocalG) * factR + aLocal; finalB = (cOtherB - cLocalB) * factR + aLocal; break;
      }
      if (m_colorCombinerInvert) { finalR = 1.0f - finalR; finalG = 1.0f - finalG; finalB = 1.0f - finalB; }

      // Evaluate Alpha Combiner Factor
      float factA = 0.0f;
      switch (m_alphaCombinerFactor) {
        case 1: case 3: factA = aLocal; break;
        case 2: factA = aOther; break;
        case 4: factA = texA; break;
        case 8: factA = 1.0f; break;
        case 9: case 11: factA = 1.0f - aLocal; break;
        case 10: factA = 1.0f - aOther; break;
        case 12: factA = 1.0f - texA; break;
      }

      // Evaluate Alpha Combiner Function
      float finalA = 0.0f;
      switch (m_alphaCombinerFunc) {
        case 1: finalA = aLocal; break;
        case 3: finalA = aOther * factA; break;
        case 4: case 5: finalA = aOther * factA + aLocal; break;
        case 6: finalA = (aOther - aLocal) * factA; break;
        case 7: case 8: finalA = (aOther - aLocal) * factA + aLocal; break;
      }
      if (m_alphaCombinerInvert) finalA = 1.0f - finalA;

      red = finalR; green = finalG; blue = finalB; alpha = finalA;
      blendAlphaVal = alpha;
      // Pre-blend Alpha Dithering / Quantization
      if constexpr (DitherEnabled) {
        static const uint8_t dither_matrix_4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        static const uint8_t dither_matrix_2x2[16] = {0, 2, 0, 2, 3, 1, 3, 1, 0, 2, 0, 2, 3, 1, 3, 1};
        int ditherX = x & 3; int ditherY = y & 3;
        uint8_t dith = (m_ditherMode == 2) ? dither_matrix_4x4[ditherY * 4 + ditherX] : dither_matrix_2x2[ditherY * 4 + ditherX];
        if ((m_alphaTestOp != 7) || (alpha < 0.99f)) {
          float alpha255 = std::round(alpha * 255.0f);
          if (alpha255 + (static_cast<float>(dith) * 128.0f / (m_ditherMode == 2 ? 16.0f : 4.0f)) < 128.0f) keepPixel = false;
          else blendAlphaVal = 1.0f;
        }
      }
    }

    // Chromakey Emulation
    if (keepPixel && m_chromakeyMode == 1) {  // GR_CHROMAKEY_ENABLE
      bool hasTex = HasTexture || hasTexture1;
      float testR = hasTex ? cOtherR : red;
      float testG = hasTex ? cOtherG : green;
      float testB = hasTex ? cOtherB : blue;

      bool rangeEnabled = ((m_chromakeyRangeMode >> 28) & 1) == 1;
      if (rangeEnabled) {
        float minR = ((m_chromakeyRangeMin >> 16) & 0xFF) / 255.0f;
        float minG = ((m_chromakeyRangeMin >> 8) & 0xFF) / 255.0f;
        float minB = (m_chromakeyRangeMin & 0xFF) / 255.0f;
        float maxR = ((m_chromakeyRangeMax >> 16) & 0xFF) / 255.0f;
        float maxG = ((m_chromakeyRangeMax >> 8) & 0xFF) / 255.0f;
        float maxB = (m_chromakeyRangeMax & 0xFF) / 255.0f;
        if (m_pixelFormatOverride == 1) { std::swap(minR, minB); std::swap(maxR, maxB); }
        bool rMatch = (testR >= minR && testR <= maxR);
        bool gMatch = (testG >= minG && testG <= maxG);
        bool bMatch = (testB >= minB && testB <= maxB);
        bool rRes = rMatch != ((m_chromakeyRangeMode >> 26) & 1);
        bool gRes = gMatch != ((m_chromakeyRangeMode >> 25) & 1);
        bool bRes = bMatch != ((m_chromakeyRangeMode >> 24) & 1);
        if (((m_chromakeyRangeMode >> 27) & 1) ? (rRes || gRes || bRes) : (rRes && gRes && bRes)) keepPixel = false;
      } else {
        float chromaR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
        float chromaG = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;
        float chromaB = (m_chromakeyValue & 0xFF) / 255.0f;
        if (m_pixelFormatOverride == 1) { std::swap(chromaR, chromaB); }
        if (std::abs(testR - chromaR) < 0.001f && std::abs(testG - chromaG) < 0.001f && std::abs(testB - chromaB) < 0.001f) keepPixel = false;
      }
    }

    // Alpha Testing
    if (keepPixel && m_alphaTestOp != 7) {
      float ref = static_cast<float>(m_alphaTestRefVal) / 255.0f;
      bool alphaPass = false;
      switch (m_alphaTestOp) {
        case 0: alphaPass = false; break;
        case 1: alphaPass = (alpha < ref); break;
        case 2: alphaPass = (alpha == ref); break;
        case 3: alphaPass = (alpha <= ref); break;
        case 4: alphaPass = (alpha > ref); break;
        case 5: alphaPass = (alpha != ref); break;
        case 6: alphaPass = (alpha >= ref); break;
      }
      if (!alphaPass) keepPixel = false;
    }

    // Depth Testing
    if (keepPixel) {
      float targetDepth = 0.0f;
      if (m_depthMode == 2) {
        float w = 1.0f / wVal;
        float range = m_depthFar - m_depthNear;
        targetDepth = (std::abs(range) > 1e-6f) ? ((w - m_depthNear) / range) : 0.0f;
      } else {
        targetDepth = w0 * a.pos[2] + w1 * b.pos[2] + w2 * c.pos[2];
      }
      if (m_depthBiasLevel != 0) targetDepth = std::max(0.0f, std::min(1.0f, targetDepth + static_cast<float>(m_depthBiasLevel) / 65535.0f));
      if (m_depthMode != 0 && m_depthCompareOp != 7 && !m_headlessDepthBuffer.empty()) {
        float currentDepth = m_headlessDepthBuffer[pIdx];
        bool depthPass = false;
        switch (m_depthCompareOp) {
          case 1: depthPass = (targetDepth < currentDepth); break;
          case 2: depthPass = (targetDepth == currentDepth); break;
          case 3: depthPass = (targetDepth <= currentDepth); break;
          case 4: depthPass = (targetDepth > currentDepth); break;
          case 5: depthPass = (targetDepth != currentDepth); break;
          case 6: depthPass = (targetDepth >= currentDepth); break;
        }
        if (!depthPass) keepPixel = false;
      }
      if (keepPixel && m_depthMode != 0 && m_depthMask && !m_headlessDepthBuffer.empty()) m_headlessDepthBuffer[pIdx] = targetDepth;
    }

    // Fog Application
    uint32_t fogSource = m_fogMode & 0x0F;
    if (keepPixel && fogSource != 0) {
      float visibility = 1.0f;
      if (fogSource == 4) {  // UNIFIED_FOG_WITH_ITERATED_ALPHA
        visibility = 1.0f - (w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3]);
      } else if (fogSource == 1) {  // UNIFIED_FOG_WITH_TABLE_ON_FOGCOORD
        visibility = 1.0f - (w0 * a.fog + w1 * b.fog + w2 * c.fog);
      } else {  // Table-based fog
        float eyeW = 0.0f;
        if (fogSource == 2) {      // UNIFIED_FOG_WITH_TABLE_ON_W
          eyeW = 1.0f / wVal;
        } else {
          eyeW = w0 * a.fog + w1 * b.fog + w2 * c.fog;
        }
        if (eyeW < 1.0f) eyeW = 1.0f;

        // O(1) unrolled logarithmic binary search on the fog table
        int idx = 0;
        if (eyeW >= s_tableIndexToW[32]) idx += 32;
        if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
        if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
        if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
        if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
        if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

        float w0_fog = s_tableIndexToW[idx];
        float w1_fog = s_tableIndexToW[idx + 1];
        float f0 = m_fogTable[idx] / 255.0f;
        float f1 = m_fogTable[idx + 1] / 255.0f;

        float t = 0.0f;
        if (w1_fog > w0_fog) {
          t = (eyeW - w0_fog) / (w1_fog - w0_fog);
        }
        t = std::max(0.0f, std::min(1.0f, t));
        float f_table = f0 * (1.0f - t) + f1 * t;
        visibility = 1.0f - f_table; // Convert density to visibility!
      }
      visibility = std::max(0.0f, std::min(1.0f, visibility));

      float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
      float fogG = ((m_fogColor >> 8) & 0xFF) / 255.0f;
      float fogB = (m_fogColor & 0xFF) / 255.0f;
      if (m_pixelFormatOverride == 1) std::swap(fogR, fogB);
      float mult = ((m_fogMode & 0x100) != 0) ? 1.0f : 0.0f;
      float add = ((m_fogMode & 0x200) != 0) ? 1.0f : 0.0f;
      red = red * (1.0f - mult) * visibility + fogR * (1.0f - add) * (1.0f - visibility);
      green = green * (1.0f - mult) * visibility + fogG * (1.0f - add) * (1.0f - visibility);
      blue = blue * (1.0f - mult) * visibility + fogB * (1.0f - add) * (1.0f - visibility);
    }

    if (keepPixel) {
      uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
      uint32_t gVal = static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
      uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));
      uint32_t aVal = static_cast<uint32_t>(std::min(255.0f, blendAlphaVal * 255.0f + 0.5f));
      if constexpr (BlendEnabled) {
        uint32_t dstWord = pixels[pIdx];
        float dstR = ((dstWord >> 16) & 0xFF) / 255.0f, dstG = ((dstWord >> 8) & 0xFF) / 255.0f, dstB = (dstWord & 0xFF) / 255.0f, dstA = ((dstWord >> 24) & 0xFF) / 255.0f;
        if (m_pixelFormatOverride == 1) {
          std::swap(dstR, dstB);
        }
        auto getF = [&](uint32_t mode, float srcC, float dstC, float srcA, float dstA, bool isDest) {
          switch(mode) {
            case 0: return 0.0f; case 1: return srcA; case 2: return isDest ? srcC : dstC; case 3: return dstA;
            case 4: return 1.0f; case 5: return 1.0f - srcA; case 6: return isDest ? (1.0f - srcC) : (1.0f - dstC); case 7: return 1.0f - dstA;
            case 15: return srcC; default: return 1.0f;
          }
        };
        red = std::min(1.0f, red * getF(m_rgbSrcBlend, red, dstR, blendAlphaVal, dstA, false) + dstR * getF(m_rgbDstBlend, red, dstR, blendAlphaVal, dstA, true));
        green = std::min(1.0f, green * getF(m_rgbSrcBlend, green, dstG, blendAlphaVal, dstA, false) + dstG * getF(m_rgbDstBlend, green, dstG, blendAlphaVal, dstA, true));
        blue = std::min(1.0f, blue * getF(m_rgbSrcBlend, blue, dstB, blendAlphaVal, dstA, false) + dstB * getF(m_rgbDstBlend, blue, dstB, blendAlphaVal, dstA, true));
        aVal = static_cast<uint32_t>(std::min(1.0f, blendAlphaVal * getF(m_alphaSrcBlend, 0,0,blendAlphaVal, dstA, false) + dstA * getF(m_alphaDstBlend, 0,0,blendAlphaVal, dstA, true)) * 255.0f + 0.5f);
        rVal = static_cast<uint32_t>(red * 255.0f + 0.5f); gVal = static_cast<uint32_t>(green * 255.0f + 0.5f); bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
      }
      if constexpr (DitherEnabled) {
        static const uint8_t dither_matrix_4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        static const uint8_t dither_matrix_2x2[16] = {2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6};
        uint8_t dith = (m_ditherMode == 2) ? dither_matrix_4x4[(y&3)*4+(x&3)] : dither_matrix_2x2[(y&3)*4+(x&3)];
        uint32_t r = std::min(31u, (((rVal << 1) - (rVal >> 4) + (rVal >> 7) + dith) >> 1) >> 3);
        uint32_t g = std::min(63u, (((gVal << 2) - (gVal >> 4) + (gVal >> 6) + dith) >> 2) >> 2);
        uint32_t b = std::min(31u, (((bVal << 1) - (bVal >> 4) + (bVal >> 7) + dith) >> 1) >> 3);
        rVal = (r << 3) | (r >> 2); gVal = (g << 2) | (g >> 4); bVal = (b << 3) | (b >> 2);
      }
      uint32_t finalColor = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
      if (!m_colorMaskRgb || !m_colorMaskAlpha) {
        uint32_t mask = (m_colorMaskRgb ? 0x00FFFFFF : 0) | (m_colorMaskAlpha ? 0xFF000000 : 0);
        finalColor = (finalColor & mask) | (pixels[pIdx] & ~mask);
      }
      if (m_pixelFormatOverride == 1) {
        uint32_t a_bit = finalColor & 0xFF000000;
        uint32_t g_bit = finalColor & 0x0000FF00;
        uint32_t r_bit = (finalColor >> 16) & 0xFF;
        uint32_t b_bit = finalColor & 0xFF;
        finalColor = a_bit | (b_bit << 16) | g_bit | r_bit;
      }
      pixels[pIdx] = finalColor;
    }
  };

  if (invArea >= 0.0f) {
    for (int y = minY; y <= maxY; ++y) {
      float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
      float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
      float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
      for (int x = minX; x <= maxX; ++x) {
        if (E0 >= -1e-5f && E1 >= -1e-5f && E2 >= -1e-5f) processPixel(x, y, E0 * invArea, E1 * invArea, E2 * invArea);
        E0 += a0; E1 += a1; E2 += a2;
      }
    }
  } else {
    for (int y = minY; y <= maxY; ++y) {
      float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
      float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
      float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
      for (int x = minX; x <= maxX; ++x) {
        if (E0 <= 1e-5f && E1 <= 1e-5f && E2 <= 1e-5f) processPixel(x, y, E0 * invArea, E1 * invArea, E2 * invArea);
        E0 += a0; E1 += a1; E2 += a2;
      }
    }
  }
}

void SoftwareBackend::InitTileGrid() {
  m_tileCols = (m_headlessWidth + m_tileSize - 1) / m_tileSize;
  m_tileRows = (m_headlessHeight + m_tileSize - 1) / m_tileSize;
  m_tiles.clear();
  m_tiles.resize(m_tileCols * m_tileRows);
}

void SoftwareBackend::ResolveActiveState() {
  if (!m_stateCacheDirty && !m_stateCatalog.empty()) {
    return; // Cache is clean!
  }

  RasterizerState s;
  s.depthMode = m_depthMode;
  s.depthCompareOp = m_depthCompareOp;
  s.depthMask = m_depthMask;
  s.depthBiasLevel = m_depthBiasLevel;
  s.depthNear = m_depthNear;
  s.depthFar = m_depthFar;

  s.ditherMode = m_ditherMode;
  s.stippleMode = m_stippleMode;
  s.stipplePattern = m_stipplePattern;

  s.rgbSrcBlend = m_rgbSrcBlend;
  s.rgbDstBlend = m_rgbDstBlend;
  s.alphaSrcBlend = m_alphaSrcBlend;
  s.alphaDstBlend = m_alphaDstBlend;

  s.alphaTestOp = m_alphaTestOp;
  s.alphaTestRefVal = m_alphaTestRefVal;

  s.colorMaskRgb = m_colorMaskRgb;
  s.colorMaskAlpha = m_colorMaskAlpha;

  s.clipMinX = m_clipMinX;
  s.clipMinY = m_clipMinY;
  s.clipMaxX = m_clipMaxX;
  s.clipMaxY = m_clipMaxY;
  s.sstOrigin = m_sstOrigin;

  s.fogMode = m_fogMode;
  s.fogColor = m_fogColor;
  std::memcpy(s.fogTable, m_fogTable, sizeof(s.fogTable));

  s.boundTexAddress[0] = m_boundTexAddress[0];
  s.boundTexAddress[1] = m_boundTexAddress[1];
  s.texClampS[0] = m_texClampS[0];
  s.texClampS[1] = m_texClampS[1];
  s.texClampT[0] = m_texClampT[0];
  s.texClampT[1] = m_texClampT[1];
  s.texMinFilter[0] = m_texMinFilter[0];
  s.texMinFilter[1] = m_texMinFilter[1];
  s.texMagFilter[0] = m_texMagFilter[0];
  s.texMagFilter[1] = m_texMagFilter[1];
  s.texMipMapMode[0] = m_texMipMapMode[0];
  s.texMipMapMode[1] = m_texMipMapMode[1];
  s.texLodBlend[0] = m_texLodBlend[0];
  s.texLodBlend[1] = m_texLodBlend[1];
  s.texLodBias[0] = m_texLodBias[0];
  s.texLodBias[1] = m_texLodBias[1];

  s.colorCombinerFunc = m_colorCombinerFunc;
  s.colorCombinerFactor = m_colorCombinerFactor;
  s.colorCombinerLocal = m_colorCombinerLocal;
  s.colorCombinerOther = m_colorCombinerOther;
  s.colorCombinerInvert = m_colorCombinerInvert;
  s.alphaCombinerFunc = m_alphaCombinerFunc;
  s.alphaCombinerFactor = m_alphaCombinerFactor;
  s.alphaCombinerLocal = m_alphaCombinerLocal;
  s.alphaCombinerOther = m_alphaCombinerOther;
  s.alphaCombinerInvert = m_alphaCombinerInvert;

  s.texCombinerRgbFunc[0] = m_texCombinerRgbFunc[0];
  s.texCombinerRgbFunc[1] = m_texCombinerRgbFunc[1];
  s.texCombinerRgbFactor[0] = m_texCombinerRgbFactor[0];
  s.texCombinerRgbFactor[1] = m_texCombinerRgbFactor[1];
  s.texCombinerAlphaFunc[0] = m_texCombinerAlphaFunc[0];
  s.texCombinerAlphaFunc[1] = m_texCombinerAlphaFunc[1];
  s.texCombinerAlphaFactor[0] = m_texCombinerAlphaFactor[0];
  s.texCombinerAlphaFactor[1] = m_texCombinerAlphaFactor[1];
  s.texCombinerRgbInvert[0] = m_texCombinerRgbInvert[0];
  s.texCombinerRgbInvert[1] = m_texCombinerRgbInvert[1];
  s.texCombinerAlphaInvert[0] = m_texCombinerAlphaInvert[0];
  s.texCombinerAlphaInvert[1] = m_texCombinerAlphaInvert[1];

  s.constantColor = m_constantColor;
  s.stwHintMask = m_stwHintMask;
  s.pixelFormatOverride = m_pixelFormatOverride;

  s.chromakeyMode = m_chromakeyMode;
  s.chromakeyValue = m_chromakeyValue;
  s.chromakeyRangeMin = m_chromakeyRangeMin;
  s.chromakeyRangeMax = m_chromakeyRangeMax;
  s.chromakeyRangeMode = m_chromakeyRangeMode;
  s.texChromaMode[0] = m_texChromaMode[0];
  s.texChromaMode[1] = m_texChromaMode[1];
  s.texChromaMin[0] = m_texChromaMin[0];
  s.texChromaMin[1] = m_texChromaMin[1];
  s.texChromaMax[0] = m_texChromaMax[0];
  s.texChromaMax[1] = m_texChromaMax[1];
  s.texChromaRangeMode[0] = m_texChromaRangeMode[0];
  s.texChromaRangeMode[1] = m_texChromaRangeMode[1];

  // Deduplicate in catalog
  for (size_t i = 0; i < m_stateCatalog.size(); ++i) {
    if (m_stateCatalog[i] == s) {
      m_activeStateId = static_cast<uint32_t>(i);
      m_stateCacheDirty = false;
      return;
    }
  }

  m_stateCatalog.push_back(s);
  m_activeStateId = static_cast<uint32_t>(m_stateCatalog.size() - 1);
  m_stateCacheDirty = false;
}

void SoftwareBackend::BinTriangle(uint32_t cmdIdx, const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
  float minX = std::min({a.pos[0], b.pos[0], c.pos[0]});
  float maxX = std::max({a.pos[0], b.pos[0], c.pos[0]});
  float minY = std::min({a.pos[1], b.pos[1], c.pos[1]});
  float maxY = std::max({a.pos[1], b.pos[1], c.pos[1]});

  int clipMinX_resolved = static_cast<int>(m_clipMinX);
  int clipMaxX_resolved = static_cast<int>(m_clipMaxX);
  int clipMinY_resolved = static_cast<int>(m_clipMinY);
  int clipMaxY_resolved = static_cast<int>(m_clipMaxY);

  if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT
    clipMinY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMaxY);
    clipMaxY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMinY);
  }

  int startX = std::max({0, static_cast<int>(std::floor(minX)), clipMinX_resolved});
  int endX = std::min({static_cast<int>(m_headlessWidth) - 1, static_cast<int>(std::ceil(maxX)), clipMaxX_resolved - 1});
  int startY = std::max({0, static_cast<int>(std::floor(minY)), clipMinY_resolved});
  int endY = std::min({static_cast<int>(m_headlessHeight) - 1, static_cast<int>(std::ceil(maxY)), clipMaxY_resolved - 1});

  if (startX > endX || startY > endY) return;

  int startTileX = startX / m_tileSize;
  int endTileX = endX / m_tileSize;
  int startTileY = startY / m_tileSize;
  int endTileY = endY / m_tileSize;

  for (int ty = startTileY; ty <= endTileY; ++ty) {
    for (int tx = startTileX; tx <= endTileX; ++tx) {
      int tileIdx = ty * m_tileCols + tx;
      m_tiles[tileIdx].triangleIndices.push_back(cmdIdx);
    }
  }
}

void SoftwareBackend::FlushBins() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_binnedTriangles.empty()) return;

  GLIDE_LOG(DEBUG, "Software", "Flushing binned command queue: " << m_binnedTriangles.size() << " triangles, " << m_tiles.size() << " tiles.");

  m_threadPool->StartFrame(static_cast<int>(m_tiles.size()));
  m_threadPool->Wait();

  m_binnedTriangles.clear();
  m_stateCatalog.clear();
  for (auto& tile : m_tiles) {
    tile.triangleIndices.clear();
  }
  m_stateCacheDirty = true;
}

void SoftwareBackend::RasterizeTile(int tileIdx) {
  int ty = tileIdx / m_tileCols;
  int tx = tileIdx % m_tileCols;

  int tileMinX = tx * m_tileSize;
  int tileMaxX = std::min(m_headlessWidth, (tx + 1) * m_tileSize) - 1;
  int tileMinY = ty * m_tileSize;
  int tileMaxY = std::min(m_headlessHeight, (ty + 1) * m_tileSize) - 1;

  const auto& triangleIndices = m_tiles[tileIdx].triangleIndices;
  if (triangleIndices.empty()) return;

  for (uint32_t cmdIdx : triangleIndices) {
    const auto& cmd = m_binnedTriangles[cmdIdx];
    const auto& state = m_stateCatalog[cmd.stateId];

    int clipMinX_resolved = static_cast<int>(state.clipMinX);
    int clipMaxX_resolved = static_cast<int>(state.clipMaxX);
    int clipMinY_resolved = static_cast<int>(state.clipMinY);
    int clipMaxY_resolved = static_cast<int>(state.clipMaxY);

    if (state.sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT
      clipMinY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(state.clipMaxY);
      clipMaxY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(state.clipMinY);
    }

    int finalMinX = std::max({tileMinX, clipMinX_resolved});
    int finalMaxX = std::min({tileMaxX, clipMaxX_resolved - 1});
    int finalMinY = std::max({tileMinY, clipMinY_resolved});
    int finalMaxY = std::min({tileMaxY, clipMaxY_resolved - 1});

    if (finalMinX > finalMaxX || finalMinY > finalMaxY) {
      continue;
    }

    const auto& a = cmd.vertices[0];
    const auto& b = cmd.vertices[1];
    const auto& c = cmd.vertices[2];

    int triMinX = static_cast<int>(std::floor(std::min({a.pos[0], b.pos[0], c.pos[0]})));
    int triMaxX = static_cast<int>(std::ceil(std::max({a.pos[0], b.pos[0], c.pos[0]})));
    int triMinY = static_cast<int>(std::floor(std::min({a.pos[1], b.pos[1], c.pos[1]})));
    int triMaxY = static_cast<int>(std::ceil(std::max({a.pos[1], b.pos[1], c.pos[1]})));

    finalMinX = std::max(finalMinX, triMinX);
    finalMaxX = std::min(finalMaxX, triMaxX);
    finalMinY = std::max(finalMinY, triMinY);
    finalMaxY = std::min(finalMaxY, triMaxY);

    if (finalMinX > finalMaxX || finalMinY > finalMaxY) {
      continue;
    }

    bool hasTexture = (state.boundTexAddress[0] != 0xFFFFFFFF);
    bool blendEnabled = (state.rgbSrcBlend != 4 || state.rgbDstBlend != 0);
    bool ditherEnabled = (state.ditherMode != 0);
    uint32_t key = (hasTexture ? 4 : 0) | (blendEnabled ? 2 : 0) | (ditherEnabled ? 1 : 0);

    switch (key) {
      case 0: RasterizeTriangleTemplate<false, false, false>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 1: RasterizeTriangleTemplate<false, false, true>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 2: RasterizeTriangleTemplate<false, true, false>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 3: RasterizeTriangleTemplate<false, true, true>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 4: RasterizeTriangleTemplate<true, false, false>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 5: RasterizeTriangleTemplate<true, false, true>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 6: RasterizeTriangleTemplate<true, true, false>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
      case 7: RasterizeTriangleTemplate<true, true, true>(a, b, c, finalMinX, finalMaxX, finalMinY, finalMaxY, state); break;
    }
  }
}

}  // namespace GlideWrapper
