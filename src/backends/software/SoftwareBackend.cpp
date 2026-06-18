#include "SoftwareBackend.h"
#include "core/Logger.h"
#include "core/TextureManager.h"
#include "core/Telemetry.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <SDL2/SDL.h>

namespace GlideWrapper {

bool SoftwareBackend::Initialize(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    GLIDE_LOG(INFO, "Software", "Initializing Pure Software translation backend...");
    m_config = config;

    m_initialized = true;
    GLIDE_LOG(INFO, "Software", "Pure Software translation backend initialized successfully.");
    return true;
}

void SoftwareBackend::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    GLIDE_LOG(INFO, "Software", "Shutting down Pure Software backend.");
    DetachWindow();

    m_initialized = false;
}

bool SoftwareBackend::AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return false;
    if (m_windowAttached) DetachWindow();

    GLIDE_LOG(INFO, "Software", "Attaching software execution window (" << width << "x" << height << "), Windowed=" << (windowed ? "Yes" : "No"));

    m_headlessWidth = width;
    m_headlessHeight = height;
    m_headlessMode = true;

    // Allocate the CPU framebuffer double buffers
    AllocateCpuBuffers(width, height);
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
              << "------------------------------------------";
    GLIDE_LOG(INFO, "Software", logStream.str());

    m_headlessDepthBuffer.resize(width * height, 0.0f);
    SetClipWindow(0, 0, width, height);

    if (!m_config.forceNoWindow) {
        GLIDE_LOG(INFO, "Software", "Instantiating interactive SDL2 desktop presentation console...");
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            GLIDE_LOG(WARN, "Software", "SDL_Init failed: " << SDL_GetError() << ". Running pure offscreen testing mode.");
        } else {
            auto* win = SDL_CreateWindow("3dfx glide-ng Presentation Console (Software)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
            if (win) {
                auto* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
                
                if (ren) {
                    auto* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
                    if (tex) {
                        m_sdlWindow   = win;
                        m_sdlRenderer = ren;
                        m_sdlTexture  = tex;
                        GLIDE_LOG(INFO, "Software", "SDL2 live visual console structured beautifully.");
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

    m_windowAttached = true;
    return true;
}

void SoftwareBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "Software", "Detaching active display attachment resources.");
    
    if (m_sdlTexture)  { SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture)); m_sdlTexture = nullptr; }
    if (m_sdlRenderer) { SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer)); m_sdlRenderer = nullptr; }
    if (m_sdlWindow)   { SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow)); m_sdlWindow = nullptr; }
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
    if (m_activeRenderBuffer == 0) { // GR_BUFFER_FRONTBUFFER
        m_headlessPixelMap = m_cpuBuffers[m_frontBufferIdx].data();
    } else { // GR_BUFFER_BACKBUFFER (default)
        m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
    }

    // 3. Present the newly swapped front buffer (which holds the completed frame)
    if (m_sdlTexture && m_sdlRenderer && !m_cpuBuffers[m_frontBufferIdx].empty()) {
        SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_cpuBuffers[m_frontBufferIdx].data(), m_headlessWidth * 4);
        SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
        SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
    }

    return true;
}

void SoftwareBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    GLIDE_LOG(INFO, "SoftwareDraw", "DrawTriangle: "
              << "A: pos=(" << a.pos[0] << "," << a.pos[1] << "," << a.pos[2] << "," << a.pos[3] << "), tex=(" << a.tex[0] << "," << a.tex[1] << "), tmu_oow=" << a.tmu_oow[0]
              << " | B: pos=(" << b.pos[0] << "," << b.pos[1] << "," << b.pos[2] << "," << b.pos[3] << "), tex=(" << b.tex[0] << "," << b.tex[1] << "), tmu_oow=" << b.tmu_oow[0]
              << " | C: pos=(" << c.pos[0] << "," << c.pos[1] << "," << c.pos[2] << "," << c.pos[3] << "), tex=(" << c.tex[0] << "," << c.tex[1] << "), tmu_oow=" << c.tmu_oow[0]
              << " | stwHint=" << m_stwHintMask);

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    // 1. Evaluate backface culling using the original, pre-flipped coordinates!
    if (m_cullMode != 0) { // 1 = GR_CULL_NEGATIVE, 2 = GR_CULL_POSITIVE
        float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) - (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
        if (m_cullMode == 1 && area < 0.0f) return; // Cull negative area
        if (m_cullMode == 2 && area > 0.0f) return; // Cull positive area
    }

    // 2. Now apply Y-coordinate flipping for rasterization
    ModernVertex a_final = a;
    ModernVertex b_final = b;
    ModernVertex c_final = c;

    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        a_final.pos[1] = (float)m_headlessHeight - a.pos[1];
        b_final.pos[1] = (float)m_headlessHeight - b.pos[1];
        c_final.pos[1] = (float)m_headlessHeight - c.pos[1];
    }

    RasterizeSoftwareTriangle(a_final, b_final, c_final);
    TelemetryManager::GetInstance().IncrementTrianglesDrawn(1);
}

void SoftwareBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    ModernVertex v1_final = v1;
    ModernVertex v2_final = v2;

    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        v1_final.pos[1] = (float)m_headlessHeight - v1.pos[1];
        v2_final.pos[1] = (float)m_headlessHeight - v2.pos[1];
    }

    // Resolve scissor boundaries in top-left space
    int clipMinY_resolved = static_cast<int>(m_clipMinY);
    int clipMaxY_resolved = static_cast<int>(m_clipMaxY);
    if (m_sstOrigin == 1) {
        clipMinY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMaxY);
        clipMaxY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMinY);
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x0 = static_cast<int>(v1_final.pos[0]); int y0 = static_cast<int>(v1_final.pos[1]);
    int x1 = static_cast<int>(v2_final.pos[0]); int y1 = static_cast<int>(v2_final.pos[1]);

    int dx = std::abs(x1 - x0); int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0); int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
        float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
        ModernVertex vInterpolated;
        for (int i = 0; i < 4; ++i) {
            vInterpolated.color[i] = v1_final.color[i] * (1.0f - t) + v2_final.color[i] * t;
        }

        uint32_t color = EvaluateCombinerColor(vInterpolated);

        // Clip against scissor window
        if (x0 >= static_cast<int>(m_clipMinX) && x0 < static_cast<int>(m_clipMaxX) &&
            y0 >= clipMinY_resolved && y0 < clipMaxY_resolved) {
            pixels[y0 * m_headlessWidth + x0] = color;
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        step++;
    }
}

void SoftwareBackend::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    ModernVertex pt_final = pt;
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
        pt_final.pos[1] = (float)m_headlessHeight - pt.pos[1];
    }

    // Resolve scissor boundaries in top-left space
    int clipMinY_resolved = static_cast<int>(m_clipMinY);
    int clipMaxY_resolved = static_cast<int>(m_clipMaxY);
    if (m_sstOrigin == 1) {
        clipMinY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMaxY);
        clipMaxY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMinY);
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x = static_cast<int>(pt_final.pos[0]); int y = static_cast<int>(pt_final.pos[1]);
    uint32_t color = EvaluateCombinerColor(pt_final);

    // Clip against scissor window
    if (x >= static_cast<int>(m_clipMinX) && x < static_cast<int>(m_clipMaxX) &&
        y >= clipMinY_resolved && y < clipMaxY_resolved) {
        pixels[y * m_headlessWidth + x] = color;
    }
}

uint32_t SoftwareBackend::EvaluateCombinerColor(const ModernVertex& v) {
    uint32_t a = static_cast<uint32_t>(v.color[3] * 255.0f + 0.5f);
    uint32_t r = static_cast<uint32_t>(v.color[0] * 255.0f + 0.5f);
    uint32_t g = static_cast<uint32_t>(v.color[1] * 255.0f + 0.5f);
    uint32_t b = static_cast<uint32_t>(v.color[2] * 255.0f + 0.5f);

    uint32_t colorLocal = m_colorCombinerLocal & 0xFFFF;
    if (colorLocal == 1 || m_colorCombinerFactor == 1) { // CONSTANT
        r = (m_constantColor >> 16) & 0xFF;
        g = (m_constantColor >> 8)  & 0xFF;
        b = m_constantColor & 0xFF;
        if (m_pixelFormatOverride == 1) { // ABGR format override for packed constant color
            std::swap(r, b);
        }
    }
    uint32_t alphaLocal = m_alphaCombinerLocal & 0xFFFF;
    if (alphaLocal == 1 || m_alphaCombinerFactor == 1) { // CONSTANT
        a = (m_constantColor >> 24) & 0xFF;
    }

    if (a < 128) a = 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

static const float s_tableIndexToW[64] = {
    1.000000f,      1.142857f,      1.333333f,      1.600000f, 
    2.000000f,      2.285714f,      2.666667f,      3.200000f, 
    4.000000f,      4.571429f,      5.333333f,      6.400000f, 
    8.000000f,      9.142858f,     10.666667f,     12.800000f, 
    16.000000f,     18.285715f,     21.333334f,     25.600000f, 
    32.000000f,     36.571430f,     42.666668f,     51.200001f, 
    64.000000f,     73.142860f,     85.333336f,    102.400002f, 
    128.000000f,    146.285721f,    170.666672f,    204.800003f, 
    256.000000f,    292.571442f,    341.333344f,    409.600006f, 
    512.000000f,    585.142883f,    682.666687f,    819.200012f, 
    1024.000000f,   1170.285767f,   1365.333374f,   1638.400024f, 
    2048.000000f,   2340.571533f,   2730.666748f,   3276.800049f, 
    4096.000000f,   4681.143066f,   5461.333496f,   6553.600098f, 
    8192.000000f,   9362.286133f,  10922.666992f,  13107.200195f, 
    16384.000000f,  18724.572266f,  21845.333984f,  26214.400391f, 
    32768.000000f,  37449.144531f,  43690.667969f,  52428.800781f
};

void SoftwareBackend::RasterizeSoftwareTriangleImpl(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.pos[0], b.pos[0], c.pos[0]}))));
    int maxX = std::min(static_cast<int>(m_headlessWidth) - 1, static_cast<int>(std::ceil(std::max({a.pos[0], b.pos[0], c.pos[0]}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({a.pos[1], b.pos[1], c.pos[1]}))));
    int maxY = std::min(static_cast<int>(m_headlessHeight) - 1, static_cast<int>(std::ceil(std::max({a.pos[1], b.pos[1], c.pos[1]}))));

    // Resolve scissor boundaries in top-left space
    int clipMinX_resolved = static_cast<int>(m_clipMinX);
    int clipMaxX_resolved = static_cast<int>(m_clipMaxX);
    int clipMinY_resolved = static_cast<int>(m_clipMinY);
    int clipMaxY_resolved = static_cast<int>(m_clipMaxY);

    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        clipMinY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMaxY);
        clipMaxY_resolved = static_cast<int>(m_headlessHeight) - static_cast<int>(m_clipMinY);
    }

    minX = std::max({0, minX, clipMinX_resolved});
    minY = std::max({0, minY, clipMinY_resolved});
    maxX = std::min({static_cast<int>(m_headlessWidth) - 1, maxX, clipMaxX_resolved - 1});
    maxY = std::min({static_cast<int>(m_headlessHeight) - 1, maxY, clipMaxY_resolved - 1});

    // Compute triangle area (twice the signed area of the triangle)
    // edgeFunc(a, b, c) = (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x)
    float area = (c.pos[0] - a.pos[0]) * (b.pos[1] - a.pos[1]) - (c.pos[1] - a.pos[1]) * (b.pos[0] - a.pos[0]);

    GLIDE_LOG(DEBUG, "RasterizerDebug", "Rasterizing triangle: Area=" << area << ", A=(" << a.pos[0] << "," << a.pos[1] << "), B=(" << b.pos[0] << "," << b.pos[1] << "), C=(" << c.pos[0] << "," << c.pos[1] << ")");
    GLIDE_LOG(DEBUG, "RasterizerDebug", "Bounds: minX=" << minX << ", maxX=" << maxX << ", minY=" << minY << ", maxY=" << maxY);
    GLIDE_LOG(DEBUG, "RasterizerDebug", "Scissor bounds: " << m_clipMinX << "," << m_clipMinY << " to " << m_clipMaxX << "," << m_clipMaxY);

    if (std::abs(area) < 0.0001f) {
        GLIDE_LOG(DEBUG, "RasterizerDebug", "Triangle area is zero! Skipping.");
        return;
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);

    // Pre-resolve active texture parameters once per triangle to completely avoid per-pixel manager/hashmap lookups!
    bool hasTexture = (m_boundTexAddress[0] != 0xFFFFFFFF);
    const uint32_t* texPixels = nullptr;
    uint32_t texW = 0;
    uint32_t texH = 0;
    uint32_t clampS = 0;
    uint32_t clampT = 0;
    uint32_t minFilter = 0;
    float scaleU = 0.0f;
    float scaleV = 0.0f;
    uint32_t wMask = 0;
    uint32_t hMask = 0;

    if (hasTexture) {
        auto* tex = TextureManager::GetInstance().GetTexture(0, m_boundTexAddress[0]);
        if (tex && !tex->swizzledMipLevels.empty()) {
            texPixels = tex->swizzledMipLevels[0].data();
            texW = tex->baseWidth;
            texH = tex->baseHeight;
            clampS = m_texClampS[0];
            clampT = m_texClampT[0];
            minFilter = m_texMinFilter[0];
            float maxDim = static_cast<float>(std::max(texW, texH));
            scaleU = maxDim / 256.0f;
            scaleV = maxDim / 256.0f;
            wMask = texW - 1; // Used for fast wrapping mask (assuming power of two)
            hMask = texH - 1; // Used for fast wrapping mask (assuming power of two)
        } else {
            hasTexture = false; // Disable texturing if invalid
        }
    }

    // Define the core pixel rendering pipeline as a lambda
    auto renderPixel = [&](int x, int y, float w0, float w1, float w2) {
        bool isTestPixel = (x == 150 && y == 150) || (x == 310 && y == 310) || (x == 410 && y == 310);

        // 1. Interpolate vertex colors and reciprocal depth
        float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];

        // Near-plane clipping
        if (m_depthMode == 2) { // GR_DEPTHBUFFER_WBUFFER
            if (wVal > 1.0f) return;
        } else if (hasTexture) {
            if (wVal <= 0.0f) return;
        }

        float iterR = w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0];
        float iterG = w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1];
        float iterB = w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2];
        float iterA = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];

        // 2. Sample texture using perspective-correct division and optimized integer pipelines
        float texR = 1.0f; float texG = 1.0f; float texB = 1.0f; float texA = 1.0f;
        if (hasTexture) {
            float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
            float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];
            
            // Perspective division
            float divW = wVal;
            if (m_stwHintMask & 0x2) { // GR_STWHINT_W_DIFF_TMU0
                divW = w0 * a.tmu_oow[0] + w1 * b.tmu_oow[0] + w2 * c.tmu_oow[0];
            }
            float invW = 1.0f / divW;
            float trueS = texS * invW;
            float trueT = texT * invW;

            float texU = trueS * scaleU;
            float texV = trueT * scaleV;

            uint32_t tWord;
            if (minFilter == 1) { // BILINEAR
                float u_sub = texU - 0.5f;
                float v_sub = texV - 0.5f;
                int u0 = static_cast<int>(std::floor(u_sub));
                int v0 = static_cast<int>(std::floor(v_sub));
                
                int fx = static_cast<int>((u_sub - u0) * 256.0f);
                int fy = static_cast<int>((v_sub - v0) * 256.0f);

                int x0, x1, y0, y1;
                if (clampS == 1) { // CLAMP
                    x0 = std::max(0, std::min(static_cast<int>(texW - 1), u0));
                    x1 = std::max(0, std::min(static_cast<int>(texW - 1), u0 + 1));
                } else { // WRAP
                    x0 = u0 & wMask;
                    x1 = (u0 + 1) & wMask;
                }

                if (clampT == 1) { // CLAMP
                    y0 = std::max(0, std::min(static_cast<int>(texH - 1), v0));
                    y1 = std::max(0, std::min(static_cast<int>(texH - 1), v0 + 1));
                } else { // WRAP
                    y0 = v0 & hMask;
                    y1 = (v0 + 1) & hMask;
                }

                uint32_t c00 = texPixels[y0 * texW + x0];
                uint32_t c10 = texPixels[y0 * texW + x1];
                uint32_t c01 = texPixels[y1 * texW + x0];
                uint32_t c11 = texPixels[y1 * texW + x1];

                // Perform 8-bit fixed-point integer bilinear blend
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

                tWord = (a_val << 24) | (r << 16) | (g << 8) | b;
            } else { // POINT
                int u0 = static_cast<int>(texU + 0.0001f);
                int v0 = static_cast<int>(texV + 0.0001f);
                int x0 = (clampS == 1) ? std::max(0, std::min(static_cast<int>(texW - 1), u0)) : (u0 & wMask);
                int y0 = (clampT == 1) ? std::max(0, std::min(static_cast<int>(texH - 1), v0)) : (v0 & hMask);
                tWord = texPixels[y0 * texW + x0];
            }

            texR = ((tWord >> 16) & 0xFF) / 255.0f;
            texG = ((tWord >> 8)  & 0xFF) / 255.0f;
            texB = (tWord & 0xFF) / 255.0f;
            texA = ((tWord >> 24) & 0xFF) / 255.0f;

            if (isTestPixel) {
                GLIDE_LOG(DEBUG, "RasterizerDebug", "Test pixel (" << x << "," << y << ") sampled texture coordinate S=" << trueS << ", T=" << trueT 
                          << ", TexelColor=(" << texR << "," << texG << "," << texB << "," << texA << ")");
            }
        }

        // 3. Evaluate general Register Combiner inputs
        float aLocal = 1.0f;
        if (m_alphaCombinerLocal == 0) { // GR_COMBINE_LOCAL_ITERATED
            aLocal = iterA;
        } else if (m_alphaCombinerLocal == 1) { // GR_COMBINE_LOCAL_CONSTANT
            aLocal = ((m_constantColor >> 24) & 0xFF) / 255.0f;
        }

        float aOther = 1.0f;
        if (m_alphaCombinerOther == 0) { // GR_COMBINE_OTHER_ITERATED
            aOther = iterA;
        } else if (m_alphaCombinerOther == 1) { // GR_COMBINE_OTHER_TEXTURE
            aOther = texA;
        } else if (m_alphaCombinerOther == 2) { // GR_COMBINE_OTHER_CONSTANT
            aOther = ((m_constantColor >> 24) & 0xFF) / 255.0f;
        }

        float cLocalR = 0.0f, cLocalG = 0.0f, cLocalB = 0.0f;
        if (m_colorCombinerLocal == 0) { // GR_COMBINE_LOCAL_ITERATED
            cLocalR = iterR; cLocalG = iterG; cLocalB = iterB;
        } else if (m_colorCombinerLocal == 1) { // GR_COMBINE_LOCAL_CONSTANT / NONE
            cLocalR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
            cLocalG = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
            cLocalB = (m_constantColor & 0xFF) / 255.0f;
            if (m_pixelFormatOverride == 1) std::swap(cLocalR, cLocalB);
        }

        float cOtherR = 0.0f, cOtherG = 0.0f, cOtherB = 0.0f;
        if (m_colorCombinerOther == 0) { // GR_COMBINE_OTHER_ITERATED
            cOtherR = iterR; cOtherG = iterG; cOtherB = iterB;
        } else if (m_colorCombinerOther == 1) { // GR_COMBINE_OTHER_TEXTURE
            cOtherR = texR; cOtherG = texG; cOtherB = texB;
        } else if (m_colorCombinerOther == 2) { // GR_COMBINE_OTHER_CONSTANT
            cOtherR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
            cOtherG = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
            cOtherB = (m_constantColor & 0xFF) / 255.0f;
            if (m_pixelFormatOverride == 1) std::swap(cOtherR, cOtherB);
        }

        // 4. Evaluate Color Combiner Factor
        float factR = 0.0f, factG = 0.0f, factB = 0.0f;
        switch (m_colorCombinerFactor) {
            case 0: break;
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

        // 5. Evaluate Color Combiner Function
        float finalR = 0.0f, finalG = 0.0f, finalB = 0.0f;
        switch (m_colorCombinerFunc) {
            case 0: break;
            case 1: finalR = cLocalR; finalG = cLocalG; finalB = cLocalB; break;
            case 2: break;
            case 3: finalR = cOtherR * factR; finalG = cOtherG * factG; finalB = cOtherB * factB; break;
            case 4: finalR = cOtherR * factR + cLocalR; finalG = cOtherG * factG + cLocalG; finalB = cOtherB * factB + cLocalB; break;
            case 5: finalR = cOtherR * factR + aLocal; finalG = cOtherG * factG + aLocal; finalB = cOtherB * factB + aLocal; break;
            case 6: finalR = (cOtherR - cLocalR) * factR; finalG = (cOtherG - cLocalG) * factG; finalB = (cOtherB - cLocalB) * factB; break;
            case 7: finalR = (cOtherR - cLocalR) * factR + cLocalR; finalG = (cOtherG - cLocalG) * factR + cLocalG; finalB = (cOtherB - cLocalB) * factR + cLocalB; break;
            case 8: finalR = (cOtherR - cLocalR) * factR + aLocal; finalG = (cOtherG - cLocalG) * factR + aLocal; finalB = (cOtherB - cLocalB) * factR + aLocal; break;
        }

        if (m_colorCombinerInvert) {
            finalR = 1.0f - finalR; finalG = 1.0f - finalG; finalB = 1.0f - finalB;
        }

        // 6. Evaluate Alpha Combiner Factor
        float factA = 0.0f;
        switch (m_alphaCombinerFactor) {
            case 0: break;
            case 1:
            case 3: factA = aLocal; break;
            case 2: factA = aOther; break;
            case 4: factA = texA; break;
            case 8: factA = 1.0f; break;
            case 9:
            case 11: factA = 1.0f - aLocal; break;
            case 10: factA = 1.0f - aOther; break;
            case 12: factA = 1.0f - texA; break;
        }

        // 7. Evaluate Alpha Combiner Function
        float finalA = 0.0f;
        switch (m_alphaCombinerFunc) {
            case 0: break;
            case 1: finalA = aLocal; break;
            case 3: finalA = aOther * factA; break;
            case 4:
            case 5: finalA = aOther * factA + aLocal; break;
            case 6: finalA = (aOther - aLocal) * factA; break;
            case 7:
            case 8: finalA = (aOther - aLocal) * factA + aLocal; break;
        }

        if (m_alphaCombinerInvert) {
            finalA = 1.0f - finalA;
        }

        float red   = finalR;
        float green = finalG;
        float blue  = finalB;
        float alpha = finalA;

        // Chromakey Emulation
        if (m_chromakeyMode == 1) { // GR_CHROMAKEY_ENABLE
            float chromaR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
            float chromaG = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;
            float chromaB = (m_chromakeyValue & 0xFF) / 255.0f;
            if (m_pixelFormatOverride == 1) {
                std::swap(chromaR, chromaB);
            }
            if (std::abs(red - chromaR) < 0.01f &&
                std::abs(green - chromaG) < 0.01f &&
                std::abs(blue - chromaB) < 0.01f) {
                return; // Discard
            }
        }

        uint32_t aVal = static_cast<uint32_t>(std::min(255.0f, alpha * 255.0f + 0.5f));

        bool alphaPass = true;
        switch (m_alphaTestOp) {
            case 0: alphaPass = false; break; // NEVER
            case 1: alphaPass = (aVal < m_alphaTestRefVal); break; // LESS
            case 2: alphaPass = (aVal == m_alphaTestRefVal); break; // EQUAL
            case 3: alphaPass = (aVal <= m_alphaTestRefVal); break; // LEQUAL
            case 4: alphaPass = (aVal > m_alphaTestRefVal); break; // GREATER
            case 5: alphaPass = (aVal != m_alphaTestRefVal); break; // NOTEQUAL
            case 6: alphaPass = (aVal >= m_alphaTestRefVal); break; // GEQUAL
            case 7: alphaPass = true; break; // ALWAYS
        }

        if (!alphaPass) {
            if (isTestPixel) {
                GLIDE_LOG(DEBUG, "RasterizerDebug", "Test pixel (" << x << "," << y << ") failed alpha test! Op=" << m_alphaTestOp << ", Ref=" << m_alphaTestRefVal);
            }
            return;
        }

        float zVal = w0 * a.pos[2] + w1 * b.pos[2] + w2 * c.pos[2];
        float targetDepth = (m_depthMode == 2) ? (1.0f - wVal) : zVal;

        if (m_depthBiasLevel != 0) {
            targetDepth += static_cast<float>(m_depthBiasLevel) / 65535.0f;
            targetDepth = std::max(0.0f, std::min(1.0f, targetDepth));
        }

        uint32_t pIdx = y * m_headlessWidth + x;
        bool depthPass = true;

        if (m_depthMode != 0 && m_depthCompareOp != 7 && !m_headlessDepthBuffer.empty()) {
            float currentDepth = m_headlessDepthBuffer[pIdx];
            switch (m_depthCompareOp) {
                case 0: depthPass = false; break;
                case 1: depthPass = (targetDepth < currentDepth); break;
                case 2: depthPass = (targetDepth == currentDepth); break;
                case 3: depthPass = (targetDepth <= currentDepth); break;
                case 4: depthPass = (targetDepth > currentDepth); break;
                case 5: depthPass = (targetDepth != currentDepth); break;
                case 6: depthPass = (targetDepth >= currentDepth); break;
                case 7: depthPass = true; break;
            }
            if (isTestPixel) {
                GLIDE_LOG(DEBUG, "RasterizerDebug", "Test pixel (" << x << "," << y << ") depth test result=" << (depthPass ? "PASS" : "FAIL") 
                          << ", targetDepth=" << targetDepth << ", currentDepth=" << currentDepth << ", Op=" << m_depthCompareOp);
            }
        }

        if (!depthPass) return;

        if (m_depthMode != 0 && m_depthMask && !m_headlessDepthBuffer.empty()) {
            m_headlessDepthBuffer[pIdx] = targetDepth;
        }

        // Fogging Emulation
        if (m_fogMode != 0) {
            float eyeW = 1.0f / wVal;
            if (eyeW < 1.0f) eyeW = 1.0f;

            int idx = 0;
            for (int i = 0; i < 63; ++i) {
                if (s_tableIndexToW[i] < eyeW) {
                    idx = i;
                } else {
                    break;
                }
            }

            float w0_fog = s_tableIndexToW[idx];
            float w1_fog = s_tableIndexToW[idx + 1];
            float f0 = m_fogTable[idx] / 255.0f;
            float f1 = m_fogTable[idx + 1] / 255.0f;

            float t = 0.0f;
            if (w1_fog > w0_fog) {
                t = (eyeW - w0_fog) / (w1_fog - w0_fog);
            }
            t = std::max(0.0f, std::min(1.0f, t));
            float f = f0 * (1.0f - t) + f1 * t;

            float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
            float fogG = ((m_fogColor >> 8)  & 0xFF) / 255.0f;
            float fogB = (m_fogColor & 0xFF) / 255.0f;
            if (m_pixelFormatOverride == 1) {
                std::swap(fogR, fogB);
            }

            red   = red   * (1.0f - f) + fogR * f;
            green = green * (1.0f - f) + fogG * f;
            blue  = blue  * (1.0f - f) + fogB * f;
        }

        uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
        uint32_t gVal = static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
        uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));

        if (m_rgbSrcBlend != 4 || m_rgbDstBlend != 0) {
            uint32_t dstWord = pixels[pIdx];
            float dstR = ((dstWord >> 16) & 0xFF) / 255.0f;
            float dstG = ((dstWord >> 8) & 0xFF) / 255.0f;
            float dstB = (dstWord & 0xFF) / 255.0f;
            float dstA = ((dstWord >> 24) & 0xFF) / 255.0f;

            auto getFactor = [&](uint32_t factorMode, float srcC, float dstC, float srcAVal, float dstAVal, bool isDest) {
                switch (factorMode) {
                    case 0: return 0.0f;
                    case 1: return srcAVal;
                    case 2: return isDest ? srcC : dstC;
                    case 3: return dstAVal;
                    case 4: return 1.0f;
                    case 5: return 1.0f - srcAVal;
                    case 6: return isDest ? (1.0f - srcC) : (1.0f - dstC);
                    case 7: return 1.0f - dstAVal;
                    default: return 1.0f;
                }
            };

            float sFR = getFactor(m_rgbSrcBlend, red, dstR, alpha, dstA, false);
            float sFG = getFactor(m_rgbSrcBlend, green, dstG, alpha, dstA, false);
            float sFB = getFactor(m_rgbSrcBlend, blue, dstB, alpha, dstA, false);

            float dFR = getFactor(m_rgbDstBlend, red, dstR, alpha, dstA, true);
            float dFG = getFactor(m_rgbDstBlend, green, dstG, alpha, dstA, true);
            float dFB = getFactor(m_rgbDstBlend, blue, dstB, alpha, dstA, true);

            float sFA = getFactor(m_alphaSrcBlend, alpha, dstA, alpha, dstA, false);
            float dFA = getFactor(m_alphaDstBlend, alpha, dstA, alpha, dstA, true);
            float blendedAlpha = std::min(1.0f, alpha * sFA + dstA * dFA);

            red   = std::min(1.0f, red   * sFR + dstR * dFR);
            green = std::min(1.0f, green * sFG + dstG * dFG);
            blue  = std::min(1.0f, blue  * sFB + dstB * dFB);

            rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
            gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
            bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
            aVal = static_cast<uint32_t>(blendedAlpha * 255.0f + 0.5f);
        }

        if (isTestPixel) {
            GLIDE_LOG(DEBUG, "RasterizerDebug", "Test pixel (" << x << "," << y << ") successfully written to LFB! Color=0x" 
                      << std::hex << ((aVal << 24) | (rVal << 16) | (gVal << 8) | bVal) << std::dec);
        }

        pixels[pIdx] = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
    };

    // Incremental Edge Setup for Pineda's Algorithm
    // Edge 0 (B to C)
    float a0 = c.pos[1] - b.pos[1];
    float b0 = b.pos[0] - c.pos[0];
    float c0 = b.pos[1] * c.pos[0] - b.pos[0] * c.pos[1];

    // Edge 1 (C to A)
    float a1 = a.pos[1] - c.pos[1];
    float b1 = c.pos[0] - a.pos[0];
    float c1 = c.pos[1] * a.pos[0] - c.pos[0] * a.pos[1];

    // Edge 2 (A to B)
    float a2 = b.pos[1] - a.pos[1];
    float b2 = a.pos[0] - b.pos[0];
    float c2 = a.pos[1] * b.pos[0] - a.pos[0] * b.pos[1];

    float invArea = 1.0f / area;
    float startX = minX + 0.5f;

    // Unswitched loops to completely eliminate inner-loop branching on area orientation,
    // parallelized across host CPU threads using OpenMP row-by-row guided scheduling.
    // Threading is only spawned for triangles larger than 16 rows to eliminate thread overhead on tiny triangles!
    if (invArea >= 0.0f) {
        #pragma omp parallel for schedule(guided) if(maxY - minY > 16)
        for (int y = minY; y <= maxY; ++y) {
            float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
            float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
            float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
            for (int x = minX; x <= maxX; ++x) {
                if (E0 >= -1e-5f && E1 >= -1e-5f && E2 >= -1e-5f) {
                    renderPixel(x, y, E0 * invArea, E1 * invArea, E2 * invArea);
                }
                E0 += a0;
                E1 += a1;
                E2 += a2;
            }
        }
    } else {
        #pragma omp parallel for schedule(guided) if(maxY - minY > 16)
        for (int y = minY; y <= maxY; ++y) {
            float E0 = a0 * startX + b0 * (static_cast<float>(y) + 0.5f) + c0;
            float E1 = a1 * startX + b1 * (static_cast<float>(y) + 0.5f) + c1;
            float E2 = a2 * startX + b2 * (static_cast<float>(y) + 0.5f) + c2;
            for (int x = minX; x <= maxX; ++x) {
                if (E0 <= 1e-5f && E1 <= 1e-5f && E2 <= 1e-5f) {
                    renderPixel(x, y, E0 * invArea, E1 * invArea, E2 * invArea);
                }
                E0 += a0;
                E1 += a1;
                E2 += a2;
            }
        }
    }
}

__attribute__((target("avx2,fma")))
void SoftwareBackend::RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    if (!m_simdLogged) {
        GLIDE_LOG(DEBUG, "SoftwareBackend", "Dispatched to AVX2 + FMA rasterizer path!");
        m_simdLogged = true;
    }
    RasterizeSoftwareTriangleImpl(a, b, c);
}

__attribute__((target("sse4.2")))
void SoftwareBackend::RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    if (!m_simdLogged) {
        GLIDE_LOG(DEBUG, "SoftwareBackend", "Dispatched to SSE4.2 rasterizer path!");
        m_simdLogged = true;
    }
    RasterizeSoftwareTriangleImpl(a, b, c);
}

__attribute__((target("default")))
void SoftwareBackend::RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    if (!m_simdLogged) {
        GLIDE_LOG(DEBUG, "SoftwareBackend", "Dispatched to default scalar rasterizer path!");
        m_simdLogged = true;
    }
    RasterizeSoftwareTriangleImpl(a, b, c);
}


} // namespace GlideWrapper
