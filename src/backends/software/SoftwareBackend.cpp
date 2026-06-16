#include "SoftwareBackend.h"
#include "core/Logger.h"
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

    // Allocate the CPU framebuffer
    m_headlessPixelMap = ::malloc(width * height * 4);
    if (!m_headlessPixelMap) {
        GLIDE_LOG(CRITICAL, "Software", "Failed to allocate memory for software framebuffer!");
        return false;
    }
    m_allocatedBuffer = true;

    // Pre-initialize buffer to opaque black
    std::memset(m_headlessPixelMap, 0, width * height * 4);

    // Format a clean driver details line for our dynamic diagnostics!
    std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle << ", res=" << width << "x" << height << ")\r\n";
    std::cout << "Info: Software Rasterizer (glide-ng Reference Core)\r\n";
    std::cout << "Info: Pixel Format RGBA8888 D24S0 nAux 0 nSamples 0 0\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n" << std::flush;

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

    if (m_allocatedBuffer && m_headlessPixelMap) {
        ::free(m_headlessPixelMap);
        m_headlessPixelMap = nullptr;
        m_allocatedBuffer = false;
    }

    m_windowAttached = false;
    m_headlessMode = false;
}

bool SoftwareBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    GLIDE_LOG(DEBUG, "Software", "Executing SwapBuffers presentation dispatch.");

    // Enforce artificial retro game frame pacing
    if (m_config.maxFps > 0.0f) {
        auto delay = std::chrono::microseconds(static_cast<int>(1000000.0f / m_config.maxFps));
        std::this_thread::sleep_for(delay);
    }

    if (m_sdlTexture && m_sdlRenderer && m_headlessPixelMap) {
        SDL_UpdateTexture(reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, m_headlessPixelMap, m_headlessWidth * 4);
        SDL_RenderCopy(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer), reinterpret_cast<SDL_Texture*>(m_sdlTexture), nullptr, nullptr);
        SDL_RenderPresent(reinterpret_cast<SDL_Renderer*>(m_sdlRenderer));
    }

    return true;
}

} // namespace GlideWrapper
