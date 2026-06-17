#include "SoftwareBackend.h"
#include "core/Logger.h"
#include "core/Telemetry.h"
#include <iostream>
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

} // namespace GlideWrapper
