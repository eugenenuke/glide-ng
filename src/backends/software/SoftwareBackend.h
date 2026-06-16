#pragma once

#include "backends/SoftwareBackendBase.h"

namespace GlideWrapper {

/**
 * @brief Concrete pure software translation engine.
 * Inherits from SoftwareBackendBase for 100% deterministic pixel-perfect software rasterization.
 * Uses only SDL2 for presentation, with zero GPU API dependencies.
 */
class SoftwareBackend : public SoftwareBackendBase {
public:
    SoftwareBackend() = default;
    ~SoftwareBackend() override { Shutdown(); }

    // Overridden Context & Presentation Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;

    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;

    bool SwapBuffers() override;

private:
    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_sdlRenderer{nullptr}; // SDL_Renderer*
    void* m_sdlTexture{nullptr};  // SDL_Texture*

    // Dynamic CPU staging buffer allocation
    bool m_allocatedBuffer{false};
};

} // namespace GlideWrapper
