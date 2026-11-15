#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Telemetry.h"
#include "core/WrapperConfig.h"

namespace GlideWrapper {

struct HudRuntimeInfo {
  const char* backendShort{"VK"};   // "VK", "GLES", "SW"
  uint32_t guestWidth{640};
  uint32_t guestHeight{480};
};

// Self-contained DXVK-style on-screen HUD rasterizer.
// Produces a tightly-sized RGBA8888 bitmap (plus bottom-up view for GLES and
// direct BGRA32 alpha-blend helper for SoftwareBackend) with zero external
// font dependencies and zero mutation of Glide rendering state.
class HudOverlay {
 public:
  HudOverlay() = default;

  void Configure(const WrapperConfig& config, const HudRuntimeInfo& runtime);
  void Reset();

  bool IsEnabled() const { return m_enabled; }

  // Called once per presented frame (from SwapBuffers / Present).
  // Returns true if the RGBA pixel buffer was regenerated (revision advanced).
  bool Update(const FrameMetrics& lastMetrics);

  // Force an immediate rasterization using the supplied metrics (useful in
  // unit tests without waiting for the 250ms throttle window).
  void ForceRender(const FrameMetrics& metrics, float displayFps);

  uint32_t GetWidth() const { return m_width; }
  uint32_t GetHeight() const { return m_height; }
  uint64_t GetRevision() const { return m_revision; }

  // RGBA8888 bytes (R, G, B, A), row 0 = top.
  const uint8_t* GetRgbaTopDown() const {
    return m_rgbaTopDown.empty() ? nullptr : m_rgbaTopDown.data();
  }

  // RGBA8888 bytes (R, G, B, A), row 0 = bottom (for OpenGL ES texture upload).
  const uint8_t* GetRgbaBottomUp() const {
    return m_rgbaBottomUp.empty() ? nullptr : m_rgbaBottomUp.data();
  }

  // Computes HUD destination rectangle inside an active presentation rectangle
  // (all coordinates in top-left-origin pixels).
  void ComputePlacement(int32_t presentX, int32_t presentY,
                        uint32_t presentW, uint32_t presentH,
                        int32_t& outX, int32_t& outY,
                        uint32_t& outW, uint32_t& outH) const;

  // Alpha-blends the scaled HUD over a top-down 0xAARRGGBB (little-endian BGRA)
  // framebuffer without modifying any pixels outside the HUD rect.
  void CompositeOverBgra32(uint32_t* dstPixels, uint32_t dstW, uint32_t dstH,
                           uint32_t dstStrideWords) const;

  const std::vector<std::string>& GetLinesForTest() const { return m_lines; }

 private:
  struct StyledLine {
    std::string label;
    std::string value;
  };

  void RebuildSurface(const FrameMetrics& metrics, float displayFps);
  void DrawChar(int32_t x, int32_t y, char ch, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a);
  void DrawTextWithShadow(int32_t x, int32_t y, const std::string& text,
                          uint8_t r, uint8_t g, uint8_t b);
  void BlendPixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b,
                  uint8_t a);
  void FillRect(int32_t x, int32_t y, int32_t w, int32_t h,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a);

  static constexpr size_t kHistorySize = 64;

  bool m_enabled{false};
  uint32_t m_mask{HUD_NONE};
  HudPosition m_position{HudPosition::TOP_LEFT};
  float m_scale{1.0f};
  float m_opacity{0.78f};

  std::string m_backendShort{"VK"};
  std::string m_cardShort{"Voodoo2"};
  std::string m_apiTag{"Glide"};
  std::string m_pacingTag{"60cap"};
  uint32_t m_guestWidth{640};
  uint32_t m_guestHeight{480};
  uint32_t m_msaaSamples{1};

  // Fast-converging FPS estimator (independent of startup stall on frame 0)
  bool m_hasPrevStamp{false};
  std::chrono::steady_clock::time_point m_windowStart{};
  std::chrono::steady_clock::time_point m_lastRedrawTime{};
  uint32_t m_windowFrames{0};
  float m_displayFps{0.0f};

  // Rolling frametime history (ms) for frametime sparkline
  std::array<float, kHistorySize> m_frameTimeHistory{};
  size_t m_historyHead{0};
  size_t m_historyCount{0};

  // Rendered RGBA surface
  uint32_t m_width{0};
  uint32_t m_height{0};
  uint64_t m_revision{0};
  std::vector<uint8_t> m_rgbaTopDown;
  std::vector<uint8_t> m_rgbaBottomUp;
  std::vector<std::string> m_lines;
};

}  // namespace GlideWrapper
