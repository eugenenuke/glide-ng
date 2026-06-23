#pragma once

#include "backends/SoftwareBackendBase.h"
#include "backends/software/RasterizerState.h"
#include "backends/software/ThreadPool.h"
#include <memory>
#include <vector>

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
    void FlushBins() override;

    // Overridden Software Primitive Rasterization
    void DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) override;
    void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
    void DrawPoint(const ModernVertex& pt) override;

private:
    // Core CPU Scanline & Barycentric Rasterizer
    
    template <bool HasTexture, bool BlendEnabled, bool DitherEnabled>
    __attribute__((always_inline)) inline void RasterizeTriangleTemplate(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c, int minX, int maxX, int minY, int maxY, const RasterizerState& state);

    __attribute__((always_inline)) inline bool IsTexChromaMatch(uint32_t color, uint32_t tmu, const RasterizerState& state) const;
    __attribute__((always_inline)) inline uint32_t SampleTextureLevel(
        const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS, float levelTrueT,
        uint32_t targetClampS, uint32_t targetClampT, uint32_t targetMinFilter,
        uint32_t tmuIdx, const RasterizerState& state) const;

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

    // Tile-Based Binning & Thread Pool Architectural Structures
    struct BinnedTriangle {
        uint32_t stateId;
        ModernVertex vertices[3];
    };

    struct Tile {
        std::vector<uint32_t> triangleIndices;
    };

    void InitTileGrid();
    void BinTriangle(uint32_t cmdIdx, const ModernVertex& a, const ModernVertex& b, const ModernVertex& c);
    void RasterizeTile(int tileIdx);
    void ResolveActiveState();

    std::vector<Tile> m_tiles;
    std::vector<BinnedTriangle> m_binnedTriangles;
    std::vector<RasterizerState> m_stateCatalog;
    uint32_t m_activeStateId{0};
    std::unique_ptr<ThreadPool> m_threadPool;

    uint32_t m_tileSize{32};
    uint32_t m_tileCols{0};
    uint32_t m_tileRows{0};
};

} // namespace GlideWrapper
