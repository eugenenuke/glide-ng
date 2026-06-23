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

    // Overridden Software Primitive Rasterization
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;
    void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
    void DrawPoint(const ModernVertex& pt) override;

private:
    // Core CPU Scanline & Barycentric Rasterizer
    __attribute__((target("avx2,fma")))
    void RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c);

    __attribute__((target("sse4.2")))
    void RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c);

    __attribute__((target("default")))
    void RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c);

    __attribute__((always_inline)) inline void RasterizeSoftwareTriangleImpl(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c);
    uint32_t EvaluateCombinerColor(const ModernVertex& v);
    void WritePixelPipeline(int x, int y, const ModernVertex& v, float coverage = 1.0f);
    // SDL2 Visual Presentation Console Singletons
    void* m_sdlWindow{nullptr};   // SDL_Window*
    void* m_sdlRenderer{nullptr}; // SDL_Renderer*
    void* m_sdlTexture{nullptr};  // SDL_Texture*

    // Dynamic CPU staging buffer allocation
    bool m_allocatedBuffer{false};
    bool m_simdLogged{false};
    bool m_windowShown{false};
};

} // namespace GlideWrapper
