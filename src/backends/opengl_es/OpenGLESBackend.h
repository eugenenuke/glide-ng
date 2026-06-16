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
    void ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) override;
    bool ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) override;
    void DrawPoint(const ModernVertex& pt) override;
    void DrawLine(const ModernVertex& v0, const ModernVertex& v1) override;
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;

private:
    bool CreateGLContext(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed);
    void DestroyGLContext();
    void SyncDepthState();

    // SDL2 & OpenGL ES Context Handles
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_glContext{nullptr};   // SDL_GLContext

    // GLES 3.2 presentation resources for streaming our software pixel map
    uint32_t m_glTexture{0};      // GLuint
    uint32_t m_glVAO{0};          // GLuint
    uint32_t m_glVBO{0};          // GLuint
    uint32_t m_glProgram{0};      // GLuint
    uint32_t m_headlessFBO{0};              // GLuint
    uint32_t m_headlessDepthRenderbuffer{0}; // GLuint

    // Milestone 3: GPU-Native Geometry Resources
    bool CreateGeomShaders();
    bool CreateGeomBuffers();

    uint32_t m_geomProgram{0};              // GLuint
    uint32_t m_geomVAO{0};                  // GLuint
    uint32_t m_geomVBO{0};                  // GLuint
    uint32_t m_geomVBOOffset{0};
    const uint32_t m_geomVBOSize{1024 * 1024}; // 1MB dynamic ring buffer
};

} // namespace GlideWrapper
