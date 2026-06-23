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

inline uint32_t SoftwareBackend::SampleTextureLevel(const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS, float levelTrueT,
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

  int logicalCores = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  const char* envThreads = std::getenv("GLIDE_NUM_THREADS");
  int numThreads = logicalCores;
  if (envThreads) {
      numThreads = std::max(1, std::atoi(envThreads));
  }
  m_threadPool = std::make_unique<ThreadPool>(numThreads - 1);
  GLIDE_LOG(INFO, "Software", "Initialized Lock-Free Thread Pool with " << (numThreads - 1) << " worker threads (Total threads: " << numThreads << ").");

  const char* envTileSize = std::getenv("GLIDE_WRAPPER_TILE_SIZE");
  if (envTileSize) {
      m_tileSize = std::max(8, std::atoi(envTileSize));
  } else {
      m_tileSize = 32;
  }
  GLIDE_LOG(INFO, "Software", "Configured Software Rasterizer Tile Size to " << m_tileSize << "x" << m_tileSize << ".");

  m_initialized = true;
  GLIDE_LOG(INFO, "Software",
            "Pure Software translation backend initialized successfully.");
  return true;
}

void SoftwareBackend::Shutdown() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized) return;

  GLIDE_LOG(INFO, "Software", "Shutting down Pure Software backend.");
  DetachWindow();
  m_threadPool.reset();

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
  InitTileGrid();

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
          auto* tex =
              SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
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
  FlushBins();
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

void SoftwareBackend::WritePixelPipeline(
    int x, int y, const ModernVertex& v, float coverage,
    int minX, int maxX, int minY, int maxY,
    const RasterizerState& state) {
  
  if (x < minX || x >= maxX || y < minY || y >= maxY) {
    return;
  }

  // Stipple Transparency
  if (state.stippleMode != 0) {
    int stippleX = (state.stippleMode == 2) ? ((x + y) & 7) : (x & 7);
    int stippleY = y & 3;
    int stippleIndex = (stippleY << 3) | (7 - stippleX);
    if (((state.stipplePattern >> stippleIndex) & 1) == 0) {
      return;
    }
  }

  // 2. Color Combiner
  uint32_t combinerColor = EvaluateCombinerColor(v, state);
  float red = ((combinerColor >> 16) & 0xFF) / 255.0f;
  float green = ((combinerColor >> 8) & 0xFF) / 255.0f;
  float blue = (combinerColor & 0xFF) / 255.0f;
  float alpha = ((combinerColor >> 24) & 0xFF) / 255.0f;

  alpha *= coverage;

  // 3. Fogging
  uint32_t fogSource = state.fogMode & 0x0F;
  if (fogSource != 0) {
    float f = 0.0f;
    if (fogSource == 4) {
      f = v.color[3];
    } else {
      float eyeW = 0.0f;
      if (fogSource == 1) {
        eyeW = v.fog;
      } else {
        eyeW = 1.0f / v.pos[3];
      }
      if (eyeW < 1.0f) eyeW = 1.0f;

      int idx = 0;
      if (eyeW >= s_tableIndexToW[32]) idx += 32;
      if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
      if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
      if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
      if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
      if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

      float w0_fog = s_tableIndexToW[idx];
      float w1_fog = s_tableIndexToW[idx + 1];
      float f0 = state.fogTable[idx] / 255.0f;
      float f1 = state.fogTable[idx + 1] / 255.0f;

      float t = 0.0f;
      if (w1_fog > w0_fog) {
        t = (eyeW - w0_fog) / (w1_fog - w0_fog);
      }
      t = std::max(0.0f, std::min(1.0f, t));
      f = f0 * (1.0f - t) + f1 * t;
    }

    float fogR = ((state.fogColor >> 16) & 0xFF) / 255.0f;
    float fogG = ((state.fogColor >> 8) & 0xFF) / 255.0f;
    float fogB = (state.fogColor & 0xFF) / 255.0f;
    if (state.pixelFormatOverride == 1) {
      std::swap(fogR, fogB);
    }


    float mult = ((state.fogMode & 0x100) != 0) ? 1.0f : 0.0f;
    float add = ((state.fogMode & 0x200) != 0) ? 1.0f : 0.0f;

    red = red * (1.0f - mult) * (1.0f - f) + fogR * (1.0f - add) * f;
    green = green * (1.0f - mult) * (1.0f - f) + fogG * (1.0f - add) * f;
    blue = blue * (1.0f - mult) * (1.0f - f) + fogB * (1.0f - add) * f;
  }

  float blendAlphaVal = alpha;
  if (state.ditherMode != 0) {
    static const uint8_t dither_matrix_4x4[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                                  3, 11, 1, 9,  15, 7, 13, 5};
    static const uint8_t dither_matrix_2x2[16] = {0, 2, 0, 2, 3, 1, 3, 1,
                                                  0, 2, 0, 2, 3, 1, 3, 1};
    int ditherX = x & 3;
    int ditherY = y & 3;
    uint8_t dith =
        (state.ditherMode == 2)   ? dither_matrix_4x4[ditherY * 4 + ditherX]
        : (state.ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX]
                              : 0;

    bool useAlpha = (state.alphaTestOp != 7) || (alpha < 0.99f);
    if (useAlpha) {
      uint32_t matrixSize = (state.ditherMode == 2) ? 16 : 4;
      float alpha255 = std::round(alpha * 255.0f);
      float ditherVal =
          (static_cast<float>(dith) * 128.0f) / static_cast<float>(matrixSize);
      if (alpha255 + ditherVal < 128.0f) {
        return;
      }
      blendAlphaVal = 1.0f;
    }
  }

  uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
  uint32_t gVal =
      static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
  uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));
  uint32_t aVal =
      static_cast<uint32_t>(std::min(255.0f, blendAlphaVal * 255.0f + 0.5f));

  // 4. Alpha Blending
  uint32_t* pixels = reinterpret_cast<uint32_t*>(m_cpuBuffers[state.activeRenderBufferIdx].data());
  uint32_t dstWord = pixels[y * m_headlessWidth + x];
  uint32_t dstR = (dstWord >> 16) & 0xFF;
  uint32_t dstG = (dstWord >> 8) & 0xFF;
  uint32_t dstB = dstWord & 0xFF;


  float sFactor = 1.0f;
  float dFactor = 0.0f;

  if (state.rgbSrcBlend == 1) {
    sFactor = aVal / 255.0f;
  } else if (state.rgbSrcBlend == 0) {
    sFactor = 0.0f;
  } else if (state.rgbSrcBlend == 4) {
    sFactor = 1.0f;
  } else {
    sFactor = 1.0f;
  }

  if (state.rgbDstBlend == 5) {
    dFactor = 1.0f - (aVal / 255.0f);
  } else if (state.rgbDstBlend == 0) {
    dFactor = 0.0f;
  } else if (state.rgbDstBlend == 4) {
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
  uint32_t finalA = aVal;

  finalR = std::min(255u, finalR);
  finalG = std::min(255u, finalG);
  finalB = std::min(255u, finalB);

  if (state.ditherMode != 0) {
    static const uint8_t dither_matrix_4x4[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                                  3, 11, 1, 9,  15, 7, 13, 5};
    static const uint8_t dither_matrix_2x2[16] = {2, 10, 2, 10, 14, 6, 14, 6,
                                                  2, 10, 2, 10, 14, 6, 14, 6};
    int ditherX = x & 3;
    int ditherY = y & 3;
    uint8_t dith =
        (state.ditherMode == 2)   ? dither_matrix_4x4[ditherY * 4 + ditherX]
        : (state.ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX]
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

  if (!state.colorMaskRgb || !state.colorMaskAlpha) {
    uint32_t mask = 0;
    if (state.colorMaskRgb) mask |= 0x00FFFFFF;
    if (state.colorMaskAlpha) mask |= 0xFF000000;
    finalColor = (finalColor & mask) | (dstWord & ~mask);
  }



  pixels[y * m_headlessWidth + x] = finalColor;
}

uint32_t SoftwareBackend::EvaluateCombinerColor(const ModernVertex& v, const RasterizerState& state) {
  uint32_t a = static_cast<uint32_t>(v.color[3] * 255.0f + 0.5f);
  uint32_t r = static_cast<uint32_t>(v.color[0] * 255.0f + 0.5f);
  uint32_t g = static_cast<uint32_t>(v.color[1] * 255.0f + 0.5f);
  uint32_t b = static_cast<uint32_t>(v.color[2] * 255.0f + 0.5f);

  uint32_t colorLocal = state.colorCombinerLocal & 0xFFFF;
  if (colorLocal == 1 || state.colorCombinerFactor == 1) {
    r = (state.constantColor >> 16) & 0xFF;
    g = (state.constantColor >> 8) & 0xFF;
    b = state.constantColor & 0xFF;
    if (state.pixelFormatOverride == 1) {
      std::swap(r, b);
    }
  }
  uint32_t alphaLocal = state.alphaCombinerLocal & 0xFFFF;
  if (alphaLocal == 1 || state.alphaCombinerFactor == 1) {
    a = (state.constantColor >> 24) & 0xFF;
  }

  if (a < 128) a = 255;

  return (a << 24) | (r << 16) | (g << 8) | b;
}
template <bool HasTexture, bool BlendEnabled, bool DitherEnabled>
inline void SoftwareBackend::RasterizeTriangleTemplate(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int tileMinX, int tileMaxX, int tileMinY, int tileMaxY,
    const RasterizerState& state) {
  int minX = std::max(tileMinX, static_cast<int>(std::floor(std::min({a.pos[0], b.pos[0], c.pos[0]}))));
  int maxX = std::min(tileMaxX - 1, static_cast<int>(std::ceil(std::max({a.pos[0], b.pos[0], c.pos[0]}))));
  int minY = std::max(tileMinY, static_cast<int>(std::floor(std::min({a.pos[1], b.pos[1], c.pos[1]}))));
  int maxY = std::min(tileMaxY - 1, static_cast<int>(std::ceil(std::max({a.pos[1], b.pos[1], c.pos[1]}))));

  if (minX > maxX || minY > maxY) {
    return;
  }

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

  if (invArea >= 0.0f) {
    RasterizeTriangleLoops<HasTexture, BlendEnabled, DitherEnabled, true>(
        a, b, c, minX, maxX, minY, maxY, state, invArea,
        a0, b0, c0, a1, b1, c1, a2, b2, c2);
  } else {
    RasterizeTriangleLoops<HasTexture, BlendEnabled, DitherEnabled, false>(
        a, b, c, minX, maxX, minY, maxY, state, invArea,
        a0, b0, c0, a1, b1, c1, a2, b2, c2);
  }
}

template <bool HasTexture, bool BlendEnabled, bool DitherEnabled, bool Clockwise>
inline void SoftwareBackend::RasterizeTriangleLoops(
    const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
    int minX, int maxX, int minY, int maxY,
    const RasterizerState& state, float invArea,
    float a0, float b0, float c0,
    float a1, float b1, float c1,
    float a2, float b2, float c2) {

  auto* pixels = reinterpret_cast<uint32_t*>(m_cpuBuffers[state.activeRenderBufferIdx].data());
  float* depthBuffer = m_headlessDepthBuffer.empty() ? nullptr : m_headlessDepthBuffer.data();
  const int width = m_headlessWidth;

  // Cache read-only state variables outside the loops to maximize register allocation
  const uint32_t stippleMode = state.stippleMode;
  const uint32_t stipplePattern = state.stipplePattern;
  const uint32_t depthMode = state.depthMode;
  const uint32_t depthCompareOp = state.depthCompareOp;
  const bool depthMask = state.depthMask;
  const int depthBiasLevel = state.depthBiasLevel;
  const uint32_t fogMode = state.fogMode;
  const uint32_t fogColor = state.fogColor;
  const uint32_t chromakeyMode = state.chromakeyMode;
  const uint32_t chromakeyRangeMode = state.chromakeyRangeMode;
  const uint32_t chromakeyRangeMin = state.chromakeyRangeMin;
  const uint32_t chromakeyRangeMax = state.chromakeyRangeMax;
  const uint32_t chromakeyValue = state.chromakeyValue;
  const uint32_t pixelFormatOverride = state.pixelFormatOverride;
  const uint32_t alphaTestOp = state.alphaTestOp;
  const uint32_t alphaTestRefVal = state.alphaTestRefVal;
  const bool colorMaskRgb = state.colorMaskRgb;
  const bool colorMaskAlpha = state.colorMaskAlpha;
  const uint32_t rgbSrcBlend = state.rgbSrcBlend;
  const uint32_t rgbDstBlend = state.rgbDstBlend;
  const uint32_t alphaSrcBlend = state.alphaSrcBlend;
  const uint32_t alphaDstBlend = state.alphaDstBlend;
  const uint32_t constantColor = state.constantColor;
  const uint32_t stwHintMask = state.stwHintMask;
  const uint32_t alphaCombinerLocal = state.alphaCombinerLocal;
  const uint32_t alphaCombinerOther = state.alphaCombinerOther;
  const uint32_t colorCombinerLocal = state.colorCombinerLocal;
  const uint32_t colorCombinerOther = state.colorCombinerOther;
  const uint32_t colorCombinerFactor = state.colorCombinerFactor;
  const uint32_t colorCombinerFunc = state.colorCombinerFunc;
  const uint32_t alphaCombinerFactor = state.alphaCombinerFactor;
  const uint32_t alphaCombinerFunc = state.alphaCombinerFunc;
  const bool alphaCombinerInvert = state.alphaCombinerInvert;
  const bool colorCombinerInvert = state.colorCombinerInvert;

  // Pre-calculate expensive division and format operations outside the loop
  const float depthFar = state.depthFar;
  const float depthNear = state.depthNear;
  const float depthRangeInv = (std::abs(depthFar - depthNear) > 1e-6f) ? (1.0f / (depthFar - depthNear)) : 0.0f;

  const float constantAlpha = ((constantColor >> 24) & 0xFF) / 255.0f;
  float constantR = ((constantColor >> 16) & 0xFF) / 255.0f;
  float constantG = ((constantColor >> 8) & 0xFF) / 255.0f;
  float constantB = (constantColor & 0xFF) / 255.0f;
  if (pixelFormatOverride == 1) std::swap(constantR, constantB);

  const float alphaTestRef = static_cast<float>(alphaTestRefVal) / 255.0f;

  float chromaR = 0.0f, chromaG = 0.0f, chromaB = 0.0f;
  float chromaMinR = 0.0f, chromaMinG = 0.0f, chromaMinB = 0.0f;
  float chromaMaxR = 0.0f, chromaMaxG = 0.0f, chromaMaxB = 0.0f;
  if (chromakeyMode == 1) {
    bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1) == 1;
    if (rangeEnabled) {
      chromaMinR = ((chromakeyRangeMin >> 16) & 0xFF) / 255.0f;
      chromaMinG = ((chromakeyRangeMin >> 8) & 0xFF) / 255.0f;
      chromaMinB = (chromakeyRangeMin & 0xFF) / 255.0f;
      chromaMaxR = ((chromakeyRangeMax >> 16) & 0xFF) / 255.0f;
      chromaMaxG = ((chromakeyRangeMax >> 8) & 0xFF) / 255.0f;
      chromaMaxB = (chromakeyRangeMax & 0xFF) / 255.0f;
    } else {
      chromaR = ((chromakeyValue >> 16) & 0xFF) / 255.0f;
      chromaG = ((chromakeyValue >> 8) & 0xFF) / 255.0f;
      chromaB = (chromakeyValue & 0xFF) / 255.0f;
      if (pixelFormatOverride == 1) { std::swap(chromaR, chromaB); }
    }
  }

  float fogR = 0.0f, fogG = 0.0f, fogB = 0.0f;
  if (fogMode != 0) {
    fogR = ((fogColor >> 16) & 0xFF) / 255.0f;
    fogG = ((fogColor >> 8) & 0xFF) / 255.0f;
    fogB = (fogColor & 0xFF) / 255.0f;
    if (pixelFormatOverride == 1) std::swap(fogR, fogB);
  }

  // TMU 0 Setup (Downstream - runtime resolved)
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
    tex = TextureManager::GetInstance().GetTexture(0, state.boundTexAddress[0]);
    if (tex && !tex->swizzledMipLevels.empty()) {
      texW = tex->baseWidth;
      texH = tex->baseHeight;
      clampS = state.texClampS[0];
      clampT = state.texClampT[0];
      minFilter = state.texMinFilter[0];
      float maxDim = static_cast<float>(std::max(texW, texH));
      scaleU = maxDim / 256.0f;
      scaleV = maxDim / 256.0f;

      mipmapMode = state.texMipMapMode[0];
      lodBlend = state.texLodBlend[0];
      mipmappingEnabled = (mipmapMode != 0);
    }
  }

  // TMU 1 Setup (Upstream - runtime resolved)
  bool hasTexture1 = (state.boundTexAddress[1] != 0xFFFFFFFF);
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
    tex1 = TextureManager::GetInstance().GetTexture(1, state.boundTexAddress[1]);
    if (tex1 && !tex1->swizzledMipLevels.empty()) {
      texW1 = tex1->baseWidth;
      texH1 = tex1->baseHeight;
      clampS1 = state.texClampS[1];
      clampT1 = state.texClampT[1];
      minFilter1 = state.texMinFilter[1];
      float maxDim1 = static_cast<float>(std::max(texW1, texH1));
      scaleU1 = maxDim1 / 256.0f;
      scaleV1 = maxDim1 / 256.0f;

      mipmapMode1 = state.texMipMapMode[1];
      lodBlend1 = state.texLodBlend[1];
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
    dSp_dy = dw0_dy * a.tex[0] + dw1_dy * b.tex[0] + dw2_dx * c.tex[0];
    dTp_dx = dw0_dx * a.tex[1] + dw1_dx * b.tex[1] + dw2_dx * c.tex[1];
    dTp_dy = dw0_dy * a.tex[1] + dw1_dy * b.tex[1] + dw2_dx * c.tex[1];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
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
    dTp1_dy = dw0_dy * a.tex[3] + dw1_dy * b.tex[3] + dw2_dx * c.tex[3];

    float qa = a.pos[3];
    float qb = b.pos[3];
    float qc = c.pos[3];
    if (stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
      qa = a.tmu_oow[1];
      qb = b.tmu_oow[1];
      qc = c.tmu_oow[1];
    }
    dQ1_dx = dw0_dx * qa + dw1_dx * qb + dw2_dx * qc;
    dQ1_dy = dw0_dy * qa + dw1_dy * qb + dw2_dy * qc;
  }

  float startX = minX + 0.5f;

  for (int y = minY; y <= maxY; ++y) {
    float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
    float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
    float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
    for (int x = minX; x <= maxX; ++x) {
      bool inside = false;
      if constexpr (Clockwise) {
        inside = (E0 >= -1e-5f && E1 >= -1e-5f && E2 >= -1e-5f);
      } else {
        inside = (E0 <= 1e-5f && E1 <= 1e-5f && E2 <= 1e-5f);
      }
      if (inside) {
        float w0 = E0 * invArea;
        float w1 = E1 * invArea;
        float w2 = E2 * invArea;

        int pIdx = y * width + x;
        float cOtherR = 0.0f;
        float cOtherG = 0.0f;
        float cOtherB = 0.0f;
        bool keepPixel = true;
        float blendAlphaVal = 1.0f;

        // Stipple Transparency Check
        if (stippleMode != 0) {
          int stippleX = (stippleMode == 2) ? ((x + y) & 7) : (x & 7);
          int stippleY = y & 3;
          int stippleIndex = (stippleY << 3) | (7 - stippleX);
          if (((stipplePattern >> stippleIndex) & 1) == 0) {
            keepPixel = false;
          }
        }

        float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];

        // Near-plane clipping
        if (depthMode == 2) {  // GR_DEPTHBUFFER_WBUFFER
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
          if (stwHintMask & 0x4) {  // GR_STWHINT_W_DIFF_TMU1
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
            lod1 += state.texLodBias[1];

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
              state.texCombinerRgbFunc[1], state.texCombinerRgbFactor[1],
              state.texCombinerAlphaFunc[1], state.texCombinerAlphaFactor[1],
              state.texCombinerRgbInvert[1], state.texCombinerAlphaInvert[1], tmu1Color,
              iteratedColor, iteratedColor);
        }

        // 3. Sample TMU 0 (Downstream)
        TmuColor tmu0Color = {1.0f, 1.0f, 1.0f, 1.0f};
        if constexpr (HasTexture) {
          if (keepPixel && tex) {
            float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
            float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];

            float divW0 = wVal;
            if (stwHintMask & 0x2) {  // GR_STWHINT_W_DIFF_TMU0
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
              lod += state.texLodBias[0];

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
              tWord = SampleTextureLevel(tex, 0, trueS, trueT, clampS, clampT,
                                        minFilter, 0u, state);
            }

            tmu0Color.r = ((tWord >> 16) & 0xFF) / 255.0f;
            tmu0Color.g = ((tWord >> 8) & 0xFF) / 255.0f;
            tmu0Color.b = (tWord & 0xFF) / 255.0f;
            tmu0Color.a = ((tWord >> 24) & 0xFF) / 255.0f;
          }
        }

        // Evaluate TMU 0 Stage
        TmuColor tmu0Out = tmu1Out;
        if constexpr (HasTexture) {
          if (keepPixel) {
            tmu0Out = EvaluateTmuStage(
                state.texCombinerRgbFunc[0], state.texCombinerRgbFactor[0],
                state.texCombinerAlphaFunc[0], state.texCombinerAlphaFactor[0],
                state.texCombinerRgbInvert[0], state.texCombinerAlphaInvert[0], tmu0Color,
                tmu1Out, iteratedColor);
          }
        }

        // Save original color from texture stage for chromakeying reference
        if (keepPixel && chromakeyMode == 1) {
          cOtherR = tmu0Out.r;
          cOtherG = tmu0Out.g;
          cOtherB = tmu0Out.b;
        }

        // 4. Color Combiner Stage
        TmuColor texColor = tmu0Out;
        float texR = texColor.r;
        float texG = texColor.g;
        float texB = texColor.b;
        float texA = texColor.a;

        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;

        if (keepPixel) {
          float aLocal = (alphaCombinerLocal == 0) ? iterA : constantAlpha;
          float aOther = (alphaCombinerOther == 0) ? iterA : ((alphaCombinerOther == 1) ? texA : constantAlpha);

          float cLocalR = 0.0f;
          float cLocalG = 0.0f;
          float cLocalB = 0.0f;
          if (colorCombinerLocal == 0) {  // GR_COMBINE_LOCAL_ITERATED
            cLocalR = iterR;
            cLocalG = iterG;
            cLocalB = iterB;
          } else if (colorCombinerLocal == 1) {  // GR_COMBINE_LOCAL_CONSTANT / NONE
            cLocalR = constantR;
            cLocalG = constantG;
            cLocalB = constantB;
          }

          cOtherR = (colorCombinerOther == 0) ? iterR : ((colorCombinerOther == 1) ? texR : constantR);
          cOtherG = (colorCombinerOther == 0) ? iterG : ((colorCombinerOther == 1) ? texG : constantG);
          cOtherB = (colorCombinerOther == 0) ? iterB : ((colorCombinerOther == 1) ? texB : constantB);

          // Evaluate Color Combiner Factor
          float factR = 0.0f; float factG = 0.0f; float factB = 0.0f;
          switch (colorCombinerFactor) {
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
          switch (colorCombinerFunc) {
            case 1: finalR = cLocalR; finalG = cLocalG; finalB = cLocalB; break;
            case 3: finalR = cOtherR * factR; finalG = cOtherG * factG; finalB = cOtherB * factB; break;
            case 4: finalR = cOtherR * factR + cLocalR; finalG = cOtherG * factG + cLocalG; finalB = cOtherB * factB + cLocalB; break;
            case 5: finalR = cOtherR * factR + aLocal; finalG = cOtherG * factG + aLocal; finalB = cOtherB * factB + aLocal; break;
            case 6: finalR = (cOtherR - cLocalR) * factR; finalG = (cOtherG - cLocalG) * factG; finalB = (cOtherB - cLocalB) * factB; break;
            case 7: finalR = (cOtherR - cLocalR) * factR + cLocalR; finalG = (cOtherG - cLocalG) * factR + cLocalG; finalB = (cOtherB - cLocalB) * factR + cLocalB; break;
            case 8: finalR = (cOtherR - cLocalR) * factR + aLocal; finalG = (cOtherG - cLocalG) * factR + aLocal; finalB = (cOtherB - cLocalB) * factR + aLocal; break;
          }
          if (colorCombinerInvert) { finalR = 1.0f - finalR; finalG = 1.0f - finalG; finalB = 1.0f - finalB; }

          // Evaluate Alpha Combiner Factor
          float factA = 0.0f;
          switch (alphaCombinerFactor) {
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
          switch (alphaCombinerFunc) {
            case 1: finalA = aLocal; break;
            case 3: finalA = aOther * factA; break;
            case 4: case 5: finalA = aOther * factA + aLocal; break;
            case 6: finalA = (aOther - aLocal) * factA; break;
            case 7: case 8: finalA = (aOther - aLocal) * factA + aLocal; break;
          }
          if (alphaCombinerInvert) finalA = 1.0f - finalA;

          red = finalR; green = finalG; blue = finalB; alpha = finalA;
          blendAlphaVal = alpha;

          // Pre-blend Alpha Dithering / Quantization
          if constexpr (DitherEnabled) {
            static const uint8_t dither_matrix_4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            static const uint8_t dither_matrix_2x2[16] = {0, 2, 0, 2, 3, 1, 3, 1, 0, 2, 0, 2, 3, 1, 3, 1};
            int ditherX = x & 3; int ditherY = y & 3;
            uint8_t dith = (state.ditherMode == 2) ? dither_matrix_4x4[ditherY * 4 + ditherX] : dither_matrix_2x2[ditherY * 4 + ditherX];
            if ((alphaTestOp != 7) || (alpha < 0.99f)) {
              float alpha255 = std::round(alpha * 255.0f);
              if (alpha255 + (static_cast<float>(dith) * 128.0f / (state.ditherMode == 2 ? 16.0f : 4.0f)) < 128.0f) keepPixel = false;
              else blendAlphaVal = 1.0f;
            }
          }
        }

        // Chromakey Emulation
        if (keepPixel && chromakeyMode == 1) {  // GR_CHROMAKEY_ENABLE
          bool hasTex = HasTexture || hasTexture1;
          float testR = hasTex ? cOtherR : red;
          float testG = hasTex ? cOtherG : green;
          float testB = hasTex ? cOtherB : blue;

          bool rangeEnabled = ((chromakeyRangeMode >> 28) & 1) == 1;
          if (rangeEnabled) {
            bool rMatch = (testR >= chromaMinR && testR <= chromaMaxR);
            bool gMatch = (testG >= chromaMinG && testG <= chromaMaxG);
            bool bMatch = (testB >= chromaMinB && testB <= chromaMaxB);
            bool rRes = rMatch != ((chromakeyRangeMode >> 26) & 1);
            bool gRes = gMatch != ((chromakeyRangeMode >> 25) & 1);
            bool bRes = bMatch != ((chromakeyRangeMode >> 24) & 1);
            if (((chromakeyRangeMode >> 27) & 1) ? (rRes || gRes || bRes) : (rRes && gRes && bRes)) keepPixel = false;
          } else {
            if (std::abs(testR - chromaR) < 0.001f && std::abs(testG - chromaG) < 0.001f && std::abs(testB - chromaB) < 0.001f) keepPixel = false;
          }
        }

        // Alpha Testing
        if (keepPixel && alphaTestOp != 7) {
          bool alphaPass = false;
          switch (alphaTestOp) {
            case 0: alphaPass = false; break;
            case 1: alphaPass = (alpha < alphaTestRef); break;
            case 2: alphaPass = (alpha == alphaTestRef); break;
            case 3: alphaPass = (alpha <= alphaTestRef); break;
            case 4: alphaPass = (alpha > alphaTestRef); break;
            case 5: alphaPass = (alpha != alphaTestRef); break;
            case 6: alphaPass = (alpha >= alphaTestRef); break;
          }
          if (!alphaPass) keepPixel = false;
        }

        // Depth Testing
        if (keepPixel) {
          float targetDepth = 0.0f;
          if (depthMode == 2) {
            float w = 1.0f / wVal;
            targetDepth = (w - depthNear) * depthRangeInv;
          } else {
            targetDepth = w0 * a.pos[2] + w1 * b.pos[2] + w2 * c.pos[2];
          }
          if (depthBiasLevel != 0) targetDepth = std::max(0.0f, std::min(1.0f, targetDepth + static_cast<float>(depthBiasLevel) / 65535.0f));
          if (depthMode != 0 && depthCompareOp != 7 && depthBuffer) {
            float currentDepth = depthBuffer[pIdx];
            bool depthPass = false;
            switch (depthCompareOp) {
              case 1: depthPass = (targetDepth < currentDepth); break;
              case 2: depthPass = (targetDepth == currentDepth); break;
              case 3: depthPass = (targetDepth <= currentDepth); break;
              case 4: depthPass = (targetDepth > currentDepth); break;
              case 5: depthPass = (targetDepth != currentDepth); break;
              case 6: depthPass = (targetDepth >= currentDepth); break;
            }
            if (!depthPass) keepPixel = false;
          }

          if (keepPixel) {
            if (depthMode != 0 && depthMask && depthBuffer) {
              depthBuffer[pIdx] = targetDepth;
            }
          }
        }

        // Fog Stage
        if (keepPixel && (fogMode != 0)) {
          uint32_t fogSource = fogMode & 0x0F;
          float f = 0.0f;
          if (fogSource == 4) { // UNIFIED_FOG_WITH_ITERATED_ALPHA
            f = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];
          } else { // Table-based fog
            float eyeW = 1.0f / wVal;
            if (eyeW < 1.0f) eyeW = 1.0f;

            int idx = 0;
            if (eyeW >= s_tableIndexToW[32]) idx += 32;
            if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
            if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
            if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
            if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
            if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

            float w0_fog = s_tableIndexToW[idx];
            float w1_fog = s_tableIndexToW[idx + 1];
            float f0 = state.fogTable[idx] / 255.0f;
            float f1 = state.fogTable[idx + 1] / 255.0f;

            float t = 0.0f;
            if (w1_fog > w0_fog) {
              t = (eyeW - w0_fog) / (w1_fog - w0_fog);
            }
            t = std::max(0.0f, std::min(1.0f, t));
            f = f0 * (1.0f - t) + f1 * t;
          }

          float mult = ((fogMode & 0x100) != 0) ? 1.0f : 0.0f;
          float add = ((fogMode & 0x200) != 0) ? 1.0f : 0.0f;

          red = red * (1.0f - mult) * (1.0f - f) + fogR * (1.0f - add) * f;
          green = green * (1.0f - mult) * (1.0f - f) + fogG * (1.0f - add) * f;
          blue = blue * (1.0f - mult) * (1.0f - f) + fogB * (1.0f - add) * f;
        }

        // Framebuffer Write / Alpha Blending
        if (keepPixel) {
          uint32_t rVal = 0, gVal = 0, bVal = 0, aVal = 255;
          if constexpr (BlendEnabled) {
            uint32_t currentPixel = pixels[pIdx];
            float dstR = ((currentPixel >> 16) & 0xFF) / 255.0f;
            float dstG = ((currentPixel >> 8) & 0xFF) / 255.0f;
            float dstB = (currentPixel & 0xFF) / 255.0f;
            float dstA = ((currentPixel >> 24) & 0xFF) / 255.0f;

            auto getF = [&](uint32_t mode, float srcC, float dstC, float srcA, float dstA, bool isDest) {
              switch(mode) {
                case 0: return 0.0f; case 1: return srcA; case 2: return isDest ? srcC : dstC; case 3: return dstA;
                case 4: return 1.0f; case 5: return 1.0f - srcA; case 6: return isDest ? (1.0f - srcC) : (1.0f - dstC); case 7: return 1.0f - dstA;
                case 15: return srcC; default: return 1.0f;
              }
            };
            red = std::min(1.0f, red * getF(rgbSrcBlend, red, dstR, blendAlphaVal, dstA, false) + dstR * getF(rgbDstBlend, red, dstR, blendAlphaVal, dstA, true));
            green = std::min(1.0f, green * getF(rgbSrcBlend, green, dstG, blendAlphaVal, dstA, false) + dstG * getF(rgbDstBlend, green, dstG, blendAlphaVal, dstA, true));
            blue = std::min(1.0f, blue * getF(rgbSrcBlend, blue, dstB, blendAlphaVal, dstA, false) + dstB * getF(rgbDstBlend, blue, dstB, blendAlphaVal, dstA, true));
            aVal = static_cast<uint32_t>(std::min(1.0f, blendAlphaVal * getF(alphaSrcBlend, 0,0,blendAlphaVal, dstA, false) + dstA * getF(alphaDstBlend, 0,0,blendAlphaVal, dstA, true)) * 255.0f + 0.5f);
            rVal = static_cast<uint32_t>(red * 255.0f + 0.5f); gVal = static_cast<uint32_t>(green * 255.0f + 0.5f); bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
          } else {
            aVal = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
            rVal = static_cast<uint32_t>(red * 255.0f + 0.5f); gVal = static_cast<uint32_t>(green * 255.0f + 0.5f); bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
          }
          if constexpr (DitherEnabled) {
            static const uint8_t dither_matrix_4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            static const uint8_t dither_matrix_2x2[16] = {2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6};
            uint8_t dith = (state.ditherMode == 2) ? dither_matrix_4x4[(y&3)*4+(x&3)] : dither_matrix_2x2[(y&3)*4+(x&3)];
            uint32_t r = std::min(31u, (((rVal << 1) - (rVal >> 4) + (rVal >> 7) + dith) >> 1) >> 3);
            uint32_t g = std::min(63u, (((gVal << 2) - (gVal >> 4) + (gVal >> 6) + dith) >> 2) >> 2);
            uint32_t b = std::min(31u, (((bVal << 1) - (bVal >> 4) + (bVal >> 7) + dith) >> 1) >> 3);
            rVal = (r << 3) | (r >> 2); gVal = (g << 2) | (g >> 4); bVal = (b << 3) | (b >> 2);
          }
          uint32_t finalColor = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
          if (!colorMaskRgb || !colorMaskAlpha) {
            uint32_t mask = (colorMaskRgb ? 0x00FFFFFF : 0) | (colorMaskAlpha ? 0xFF000000 : 0);
            finalColor = (finalColor & mask) | (pixels[pIdx] & ~mask);
          }
          pixels[pIdx] = finalColor;
        }
      }
      E0 += a0; E1 += a1; E2 += a2;
    }
  }
}

void SoftwareBackend::RasterizeSoftwareTriangleTile(const ModernVertex& a,
                                                   const ModernVertex& b,
                                                   const ModernVertex& c,
                                                   int minX, int maxX, int minY, int maxY,
                                                   const RasterizerState& state) {
  bool hasTexture = (state.boundTexAddress[0] != 0xFFFFFFFF);
  bool blendEnabled = (state.rgbSrcBlend != 4 || state.rgbDstBlend != 0);
  bool ditherEnabled = (state.ditherMode != 0);
  uint32_t key = (hasTexture ? 4 : 0) | (blendEnabled ? 2 : 0) | (ditherEnabled ? 1 : 0);
  switch (key) {
    case 0: RasterizeTriangleTemplate<false, false, false>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 1: RasterizeTriangleTemplate<false, false, true>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 2: RasterizeTriangleTemplate<false, true, false>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 3: RasterizeTriangleTemplate<false, true, true>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 4: RasterizeTriangleTemplate<true, false, false>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 5: RasterizeTriangleTemplate<true, false, true>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 6: RasterizeTriangleTemplate<true, true, false>(a, b, c, minX, maxX, minY, maxY, state); break;
    case 7: RasterizeTriangleTemplate<true, true, true>(a, b, c, minX, maxX, minY, maxY, state); break;
  }
}

void SoftwareBackend::InitTileGrid() {
  m_tileCols = (m_headlessWidth + m_tileSize - 1) / m_tileSize;
  m_tileRows = (m_headlessHeight + m_tileSize - 1) / m_tileSize;
  m_tiles.resize(m_tileCols * m_tileRows);
  for (auto& tile : m_tiles) {
    tile.commandIndices.clear();
  }
  GLIDE_LOG(INFO, "Software", "Initialized tile grid: " << m_tileCols << "x" << m_tileRows << " = " << m_tiles.size() << " tiles (tile size " << m_tileSize << ").");
}

void SoftwareBackend::BinTriangle(uint32_t cmdIdx, const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
  float minX = std::min({a.pos[0], b.pos[0], c.pos[0]});
  float maxX = std::max({a.pos[0], b.pos[0], c.pos[0]});
  float minY = std::min({a.pos[1], b.pos[1], c.pos[1]});
  float maxY = std::max({a.pos[1], b.pos[1], c.pos[1]});

  minX = std::max(0.0f, minX);
  minY = std::max(0.0f, minY);
  maxX = std::min((float)m_headlessWidth - 1.0f, maxX);
  maxY = std::min((float)m_headlessHeight - 1.0f, maxY);

  if (minX > maxX || minY > maxY) return;

  int tileMinCol = (int)(minX) / m_tileSize;
  int tileMaxCol = (int)(maxX) / m_tileSize;
  int tileMinRow = (int)(minY) / m_tileSize;
  int tileMaxRow = (int)(maxY) / m_tileSize;

  for (int r = tileMinRow; r <= tileMaxRow; ++r) {
    for (int c = tileMinCol; c <= tileMaxCol; ++c) {
      m_tiles[r * m_tileCols + c].commandIndices.push_back(cmdIdx);
    }
  }
}

void SoftwareBackend::BinLine(uint32_t cmdIdx, const ModernVertex& v1, const ModernVertex& v2) {
  float minX = std::min(v1.pos[0], v2.pos[0]);
  float maxX = std::max(v1.pos[0], v2.pos[0]);
  float minY = std::min(v1.pos[1], v2.pos[1]);
  float maxY = std::max(v1.pos[1], v2.pos[1]);

  minX = std::max(0.0f, minX);
  minY = std::max(0.0f, minY);
  maxX = std::min((float)m_headlessWidth - 1.0f, maxX);
  maxY = std::min((float)m_headlessHeight - 1.0f, maxY);

  if (minX > maxX || minY > maxY) return;

  int tileMinCol = (int)(minX) / m_tileSize;
  int tileMaxCol = (int)(maxX) / m_tileSize;
  int tileMinRow = (int)(minY) / m_tileSize;
  int tileMaxRow = (int)(maxY) / m_tileSize;

  for (int r = tileMinRow; r <= tileMaxRow; ++r) {
    for (int c = tileMinCol; c <= tileMaxCol; ++c) {
      m_tiles[r * m_tileCols + c].commandIndices.push_back(cmdIdx);
    }
  }
}

void SoftwareBackend::BinPoint(uint32_t cmdIdx, const ModernVertex& pt) {
  float px = pt.pos[0];
  float py = pt.pos[1];

  if (px < 0.0f || px >= (float)m_headlessWidth || py < 0.0f || py >= (float)m_headlessHeight) return;

  int tileCol = (int)(px) / m_tileSize;
  int tileRow = (int)(py) / m_tileSize;

  m_tiles[tileRow * m_tileCols + tileCol].commandIndices.push_back(cmdIdx);
}

void SoftwareBackend::RasterizeTile(int tileIdx) {
  int col = tileIdx % m_tileCols;
  int row = tileIdx / m_tileCols;

  int tileMinX = col * m_tileSize;
  int tileMaxX = tileMinX + m_tileSize;
  int tileMinY = row * m_tileSize;
  int tileMaxY = tileMinY + m_tileSize;

  const auto& cmdIndices = m_tiles[tileIdx].commandIndices;
  for (uint32_t cmdIdx : cmdIndices) {
    const auto& cmd = m_binnedCommands[cmdIdx];
    const auto& state = m_stateCatalog[cmd.stateId];

    int clipMinX = state.clipMinX;
    int clipMaxX = state.clipMaxX;
    int clipMinY = state.clipMinY;
    int clipMaxY = state.clipMaxY;

    if (state.sstOrigin == 1) { // LOWER_LEFT
      clipMinY = m_headlessHeight - state.clipMaxY;
      clipMaxY = m_headlessHeight - state.clipMinY;
    }

    int minX = std::max(tileMinX, clipMinX);
    int maxX = std::min(tileMaxX, clipMaxX);
    int minY = std::max(tileMinY, clipMinY);
    int maxY = std::min(tileMaxY, clipMaxY);

    if (minX >= maxX || minY >= maxY) continue;

    if (cmd.type == CommandType::TRIANGLE) {
      RasterizeSoftwareTriangleTile(cmd.vertices[0], cmd.vertices[1], cmd.vertices[2], minX, maxX, minY, maxY, state);
    } else if (cmd.type == CommandType::LINE) {
      RasterizeSoftwareLineTile(cmd.vertices[0], cmd.vertices[1], minX, maxX, minY, maxY, state);
    } else if (cmd.type == CommandType::POINT) {
      RasterizeSoftwarePointTile(cmd.vertices[0], minX, maxX, minY, maxY, state);
    }
  }
}

void SoftwareBackend::FlushBins() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_binnedCommands.empty()) return;

  GLIDE_PROFILE_SCOPE("Software::FlushBins");

  if (m_threadPool) {
    std::vector<int> activeTiles;
    activeTiles.reserve(m_tiles.size());
    for (size_t i = 0; i < m_tiles.size(); ++i) {
      if (!m_tiles[i].commandIndices.empty()) {
        activeTiles.push_back(static_cast<int>(i));
      }
    }

    if (!activeTiles.empty()) {
      m_threadPool->ParallelFor(static_cast<int>(activeTiles.size()), [this, &activeTiles](int taskIdx) {
        RasterizeTile(activeTiles[taskIdx]);
      });
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

void SoftwareBackend::RasterizeSoftwareLineTile(
    const ModernVertex& v1, const ModernVertex& v2,
    int minX, int maxX, int minY, int maxY,
    const RasterizerState& state) {
  
  if (state.aaEnabled) {
    float x0_f = v1.pos[0];
    float y0_f = v1.pos[1];
    float x1_f = v2.pos[0];
    float y1_f = v2.pos[1];

    bool steep = std::abs(y1_f - y0_f) > std::abs(x1_f - x0_f);
    if (steep) {
      std::swap(x0_f, y0_f);
      std::swap(x1_f, y1_f);
    }
    ModernVertex v1_temp = v1;
    ModernVertex v2_temp = v2;
    if (x0_f > x1_f) {
      std::swap(x0_f, x1_f);
      std::swap(y0_f, y1_f);
      std::swap(v1_temp, v2_temp);
    }

    float dx = x1_f - x0_f;
    float dy = y1_f - y0_f;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    auto plot = [&](int px, int py, float c, float t) {
      if (steep) std::swap(px, py);
      ModernVertex vInterpolated;
      for (int i = 0; i < 4; ++i) {
        vInterpolated.color[i] = v1_temp.color[i] * (1.0f - t) + v2_temp.color[i] * t;
      }
      vInterpolated.fog = v1_temp.fog * (1.0f - t) + v2_temp.fog * t;
      vInterpolated.pos[3] = v1_temp.pos[3] * (1.0f - t) + v2_temp.pos[3] * t;

      WritePixelPipeline(px, py, vInterpolated, c, minX, maxX, minY, maxY, state);
    };

    float intery = y0_f;
    for (int x = static_cast<int>(std::round(x0_f)); x <= static_cast<int>(std::round(x1_f)); ++x) {
      float t = (dx == 0.0f) ? 0.0f : (x - x0_f) / dx;
      t = std::max(0.0f, std::min(1.0f, t));
      int ipart = static_cast<int>(std::floor(intery));
      float fpart = intery - ipart;
      plot(x, ipart, 1.0f - fpart, t);
      plot(x, ipart + 1, fpart, t);
      intery += gradient;
    }
  } else {
    int x0 = static_cast<int>(v1.pos[0]);
    int y0 = static_cast<int>(v1.pos[1]);
    int x1 = static_cast<int>(v2.pos[0]);
    int y1 = static_cast<int>(v2.pos[1]);

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
        vInterpolated.color[i] = v1.color[i] * (1.0f - t) + v2.color[i] * t;
      }
      vInterpolated.fog = v1.fog * (1.0f - t) + v2.fog * t;
      vInterpolated.pos[3] = v1.pos[3] * (1.0f - t) + v2.pos[3] * t;

      WritePixelPipeline(x0, y0, vInterpolated, 1.0f, minX, maxX, minY, maxY, state);

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

void SoftwareBackend::RasterizeSoftwarePointTile(
    const ModernVertex& pt,
    int minX, int maxX, int minY, int maxY,
    const RasterizerState& state) {
  
  if (state.aaEnabled) {
    float x_f = pt.pos[0];
    float y_f = pt.pos[1];
    float r_max = 0.75f * static_cast<float>(m_ssaaScale);

    int pmin_x = static_cast<int>(std::floor(x_f - r_max));
    int pmax_x = static_cast<int>(std::ceil(x_f + r_max));
    int pmin_y = static_cast<int>(std::floor(y_f - r_max));
    int pmax_y = static_cast<int>(std::ceil(y_f + r_max));

    for (int py = pmin_y; py <= pmax_y; ++py) {
      for (int px = pmin_x; px <= pmax_x; ++px) {
        float dx = (static_cast<float>(px) + 0.5f) - x_f;
        float dy = (static_cast<float>(py) + 0.5f) - y_f;
        float d = std::sqrt(dx * dx + dy * dy);
        float C = 1.0f - (d / r_max);
        if (C > 0.0f) {
          C = std::max(0.0f, std::min(1.0f, C));
          WritePixelPipeline(px, py, pt, C, minX, maxX, minY, maxY, state);
        }
      }
    }
  } else {
    int x = static_cast<int>(pt.pos[0]);
    int y = static_cast<int>(pt.pos[1]);
    WritePixelPipeline(x, y, pt, 1.0f, minX, maxX, minY, maxY, state);
  }
}

void SoftwareBackend::ResolveActiveState() {
  if (!m_stateCacheDirty) return;

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

  state.activeRenderBufferIdx = (m_activeRenderBuffer == 0) ? m_frontBufferIdx : m_backBufferIdx;

  auto it = std::find(m_stateCatalog.begin(), m_stateCatalog.end(), state);
  if (it != m_stateCatalog.end()) {
    m_activeStateId = std::distance(m_stateCatalog.begin(), it);
  } else {
    m_stateCatalog.push_back(state);
    m_activeStateId = m_stateCatalog.size() - 1;
  }
  m_stateCacheDirty = false;
}

bool SoftwareBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight,
                              uint32_t dstStride, void* dstData) {
  // 1. First, call the base class to perform the standard copy and conversion
  bool ok = SoftwareBackendBase::ReadLFB(buffer, srcX, srcY, srcWidth, srcHeight, dstStride, dstData);
  if (!ok) return false;

  // 2. If ABGR format override is active, perform the Red/Blue channel swap on the copied client buffer
  if (m_pixelFormatOverride == 1) {
    bool is32Bit = (dstStride >= srcWidth * 4);
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);

    for (uint32_t y = 0; y < srcHeight; ++y) {
      if (is32Bit) {
        auto* row32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
        for (uint32_t x = 0; x < srcWidth; ++x) {
          uint32_t val = row32[x];
          uint32_t a = val & 0xFF000000;
          uint32_t g = val & 0x0000FF00;
          uint32_t r = (val >> 16) & 0xFF;
          uint32_t b = val & 0xFF;
          row32[x] = a | (b << 16) | g | r;
        }
      } else {
        auto* row16 = reinterpret_cast<uint16_t*>(outBytes + y * dstStride);
        for (uint32_t x = 0; x < srcWidth; ++x) {
          uint16_t c16 = row16[x];
          // Swap Red (bits 11..15) and Blue (bits 0..4) in RGB565 to get BGR565
          uint16_t r = (c16 >> 11) & 0x1F;
          uint16_t g = (c16 >> 5) & 0x3F;
          uint16_t b = c16 & 0x1F;
          row16[x] = (b << 11) | (g << 5) | r;
        }
      }
    }
  }
  return true;
}

} // namespace GlideWrapper
