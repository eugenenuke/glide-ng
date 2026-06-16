#pragma once

#include "backends/SoftwareBackendBase.h"
#include <vector>

namespace GlideWrapper {

/**
 * @brief Concrete OpenGL ES 3.2 translation engine.
 * Inherits from SoftwareBackendBase for 100% deterministic pixel-perfect software rasterization.
 * Employs SDL2-managed EGL context creation for cross-platform portability.
 */
class OpenGLESBackend : public SoftwareBackendBase {
public:
    OpenGLESBackend() = default;
    ~OpenGLESBackend() override { Shutdown(); }

    // Overridden Context & Presentation Lifecycle
    bool Initialize(const WrapperConfig& config) override;
    void Shutdown() override;

    bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) override;
    void DetachWindow() override;

    bool SwapBuffers() override;

private:
    bool CreateGLContext(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed);
    void DestroyGLContext();

    // SDL2 & OpenGL ES Context Handles
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_glContext{nullptr};   // SDL_GLContext

    // GLES 3.2 presentation resources for streaming our software pixel map
    uint32_t m_glTexture{0};      // GLuint
    uint32_t m_glVAO{0};          // GLuint
    uint32_t m_glVBO{0};          // GLuint
    uint32_t m_glProgram{0};      // GLuint
};

} // namespace GlideWrapper
