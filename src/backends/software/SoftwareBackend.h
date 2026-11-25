#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backends/FrontBufferFlushTimer.h"
#include "backends/SoftwareBackendBase.h"
#include "core/HashUtils.h"
#include "backends/software/RasterizerState.h"
#include "backends/software/ThreadPool.h"
#include "core/HudOverlay.h"
#include "core/NativeSurfaceHost.h"

namespace GlideWrapper {

enum class CommandType { TRIANGLE, LINE, POINT };

struct DrawCommand {
  CommandType type;
  uint32_t stateId;
  ModernVertex vertices[3];
};

struct Tile {
  std::vector<uint32_t> commandIndices;
};

class SoftwareBackend : public SoftwareBackendBase {
 public:
  SoftwareBackend();
  ~SoftwareBackend() override;

  bool Initialize(const WrapperConfig& config) override;
  void Shutdown() override;

  bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height,
                    bool windowed) override;
  void DetachWindow() override;
  void SetPassthruMode(bool show3dfx) override;

  bool SwapBuffers() override;
  void PollEvents() override;
  void FlushBins() override;

  void ClearBuffer(uint32_t color, uint32_t alpha, float z,
                   uint32_t clearMask) override;
  void ClearBufferExt(uint32_t color, uint32_t alpha, float z, uint8_t stencil,
                      uint32_t clearMask) override;
  void DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                    const ModernVertex& c) override;
  void DrawTriangleFan(const ModernVertex* verts, uint32_t count) override;
  void DrawTriangleList(const ModernVertex* verts, uint32_t count) override;
  void DrawLine(const ModernVertex& v1, const ModernVertex& v2) override;
  void DrawPoint(const ModernVertex& pt) override;
  void UploadTexture(uint32_t tmu, uint32_t startAddress,
                     const struct VirtualTexture& tex) override;
  bool TryBindCachedTexture(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex) override;
  uint64_t GetTextureContentCacheHitCount() const {
    return m_swTexContentCacheHits.load(std::memory_order_relaxed);
  }
  void UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex, uint32_t lodLevel,
                            uint32_t startRow, uint32_t endRow) override;
  void PurgeTextures() override;

  bool WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY,
                uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride,
                uint32_t srcFmt, const void* srcData,
                const void* refData = nullptr,
                const LfbPipelineConfig& config = {}) override;
  bool ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth,
               uint32_t srcHeight, uint32_t dstStride, void* dstData) override;

 protected:
  void FlushBins_Locked() override;

 private:
  void DetachWindow_Locked(std::unique_lock<std::mutex>& lock);
  void PresentFrontBuffer_Locked();
  void NoteFrontBufferMutation_Locked(bool forceImmediate = false);
  void OnFrontBufferFlushTimer();
  void InitTileGrid();
  void BinTriangle(uint32_t cmdIdx, const ModernVertex& a,
                   const ModernVertex& b, const ModernVertex& c);
  void BinLine(uint32_t cmdIdx, const ModernVertex& v1, const ModernVertex& v2);
  void BinPoint(uint32_t cmdIdx, const ModernVertex& pt);
  void RasterizeTile(int tileIdx);
  void ResolveActiveState();

  void RasterizeSoftwareTriangleTile(const ModernVertex& a,
                                     const ModernVertex& b,
                                     const ModernVertex& c, int minX, int maxX,
                                     int minY, int maxY,
                                     const RasterizerState& state);
  void RasterizeSoftwareLineTile(const ModernVertex& v1, const ModernVertex& v2,
                                 int minX, int maxX, int minY, int maxY,
                                 const RasterizerState& state);
  void RasterizeSoftwarePointTile(const ModernVertex& pt, int minX, int maxX,
                                  int minY, int maxY,
                                  const RasterizerState& state);

  template <bool HasTexture, bool BlendEnabled, bool DitherEnabled>
  inline void RasterizeTriangleTemplate(
      const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
      int minX, int maxX, int minY, int maxY, const RasterizerState& state);

  template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
            bool Clockwise>
  void RasterizeTriangleLoops(
      const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
      int minX, int maxX, int minY, int maxY, const RasterizerState& state,
      float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
      float a2, float b2, float c2);

  template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
            bool Clockwise>
  void RasterizeTriangleLoopsSIMD(
      const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
      int minX, int maxX, int minY, int maxY, const RasterizerState& state,
      float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
      float a2, float b2, float c2);

  template <bool HasTexture, bool BlendEnabled, bool DitherEnabled,
            bool Clockwise>
  void RasterizeTriangleLoopsAvx512(
      const ModernVertex& a, const ModernVertex& b, const ModernVertex& c,
      int minX, int maxX, int minY, int maxY, const RasterizerState& state,
      float invArea, float a0, float b0, float c0, float a1, float b1, float c1,
      float a2, float b2, float c2);

  __attribute__((always_inline)) inline bool IsTexChromaMatch(
      uint32_t color, uint32_t tmu, const RasterizerState& state) const;
  __attribute__((always_inline)) inline uint32_t SampleTextureLevel(
      const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS,
      float levelTrueT, uint32_t targetClampS, uint32_t targetClampT,
      uint32_t targetMinFilter, uint32_t tmuIdx,
      const RasterizerState& state) const;
  uint32_t SampleTextureLevelSlow(
      const struct VirtualTexture* targetTex, int lodIdx, float levelTrueS,
      float levelTrueT, uint32_t targetClampS, uint32_t targetClampT,
      uint32_t targetMinFilter, uint32_t tmuIdx,
      const RasterizerState& state) const;

  uint32_t EvaluateCombinerColor(const ModernVertex& v,
                                 const RasterizerState& state);
  void WritePixelPipeline(int x, int y, const ModernVertex& v, float coverage,
                          int minX, int maxX, int minY, int maxY,
                          const RasterizerState& state);

  bool m_allocatedBuffer{false};
  SwSimdLevel m_activeSimdLevel{SwSimdLevel::Scalar};
  bool m_useSimd{false};
  bool m_simdLogged{false};

  void EnsureThreadPool_Locked();

  std::unique_ptr<ThreadPool> m_threadPool;
  uint32_t m_configuredThreads{1};
  std::vector<DrawCommand> m_binnedCommands;
  std::vector<RasterizerState> m_stateCatalog;
  std::vector<Tile> m_tiles;

  uint32_t m_activeStateId{0};
  std::atomic<uint64_t> m_textureUploadRevision{0};
  uint64_t m_resolvedTextureRevision{0};

  // Content-addressed CPU decoded-mipmap cache (bounded LRU) shared across
  // TMU address rotations and shade-palette switches.
  // Lock-order invariant: m_swTexMutex is strictly a LEAF mutex; never acquire
  // SoftwareBackend::m_mutex or call into TextureManager while holding it.
  struct SwTexContentKey {
    uint64_t dataHash{0};
    uint32_t format{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipLevels{0};
    bool operator==(const SwTexContentKey&) const = default;
  };
  struct SwTexContentKeyHash {
    size_t operator()(const SwTexContentKey& k) const noexcept {
      size_t h = std::hash<uint64_t>{}(k.dataHash);
      Hash::HashCombine(h, std::hash<uint32_t>{}(k.format));
      Hash::HashCombine(h, std::hash<uint32_t>{}(k.width));
      Hash::HashCombine(h, std::hash<uint32_t>{}(k.height));
      Hash::HashCombine(h, std::hash<uint32_t>{}(k.mipLevels));
      return h;
    }
  };
  struct SwTexCacheEntry {
    std::shared_ptr<const VirtualTexture> tex;
    size_t byteSize{0};
    std::list<SwTexContentKey>::iterator lruIt;
  };
  static constexpr size_t kMaxCachedSwTextures = 1024;
  static constexpr size_t kMaxCachedSwTextureBytes =
      (sizeof(void*) <= 4) ? (64ull * 1024ull * 1024ull)
                           : (128ull * 1024ull * 1024ull);
  void ClearSwTextureCache();

  mutable std::mutex m_swTexMutex;
  std::unordered_map<uint32_t, std::shared_ptr<const VirtualTexture>>
      m_swTextures[2];
  std::unordered_map<SwTexContentKey, SwTexCacheEntry, SwTexContentKeyHash>
      m_swTexCache;
  std::list<SwTexContentKey> m_swTexLru;
  size_t m_swTexCacheBytes{0};
  std::atomic<uint64_t> m_swTexContentCacheHits{0};
  uint32_t m_tileSize{32};
  uint32_t m_tileShift{5};
  uint32_t m_tileCols{0};
  uint32_t m_tileRows{0};

  void ResolveFrontBuffer(const uint32_t*& outPixels, uint32_t& outW,
                          uint32_t& outH);
  bool InitPresentationSurface();
  void ShutdownPresentationSurface();
  void PresentFrame(const uint32_t* srcPixels, uint32_t srcW, uint32_t srcH);

  struct X11Presenter;
  struct WaylandShmPresenter;
  struct Win32GdiPresenter;

  NativeSurfaceHost m_surfaceHost;
  void* m_sdlRenderer{nullptr};  // StandaloneSDL2 owned SDL_Renderer*
  void* m_sdlTexture{nullptr};   // StandaloneSDL2 owned SDL_Texture*
  std::unique_ptr<X11Presenter> m_x11Presenter;
  std::unique_ptr<WaylandShmPresenter> m_wlShmPresenter;
  std::unique_ptr<Win32GdiPresenter> m_win32Presenter;
  HudOverlay m_hudOverlay;
  FrontBufferPacer m_frontPacer{};
  std::vector<uint32_t> m_hudPresentBuffer;
  std::atomic<bool> m_passthruSuspended{false};

  FrontBufferFlushTimer m_frontFlushTimer{
      [this]() { OnFrontBufferFlushTimer(); }};
};

}  // namespace GlideWrapper
