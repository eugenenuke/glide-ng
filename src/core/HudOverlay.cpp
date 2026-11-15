#include "core/HudOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace GlideWrapper {

namespace {

// Compact 8x8 ASCII bitmap font (ASCII 0x20..0x7E, 95 glyphs).
// Each glyph is 8 rows of 8 pixels; bit 7 (0x80) is the leftmost pixel.
// clang-format off
constexpr uint8_t kFont8x8[95][8] = {
    // 0x20 ' ' .. 0x2F '/'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // '#'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // '/'
    // 0x30 '0' .. 0x39 '9'
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00}, // '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // '5'
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // '6'
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // '9'
    // 0x3A ':' .. 0x40 '@'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ';'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // '<'
    {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00}, // '='
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // '>'
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // '?'
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, // '@'
    // 0x41 'A' .. 0x5A 'Z'
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 'L'
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 'N'
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 'P'
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E}, // 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 'R'
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, // 'S'
    {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'U'
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00}, // 'V'
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // 'W'
    {0xC6,0x6C,0x38,0x38,0x6C,0xC6,0xC6,0x00}, // 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 'Y'
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, // 'Z'
    // 0x5B '[' .. 0x60 '`'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
    // 0x61 'a' .. 0x7A 'z'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 'a'
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 'b'
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 'c'
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 'd'
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 'e'
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00}, // 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C}, // 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
    {0x00,0x00,0xCC,0xFE,0xD6,0xC6,0xC6,0x00}, // 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 'q'
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 'r'
    {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00}, // 's'
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00}, // 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 'u'
    {0x00,0x00,0xC6,0xC6,0x6C,0x38,0x10,0x00}, // 'v'
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, // 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 'x'
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, // 'y'
    {0x00,0x00,0xFE,0x0C,0x38,0x60,0xFE,0x00}, // 'z'
    // 0x7B '{' .. 0x7E '~'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, // '~'
};
// clang-format on

const char* CardModelShortName(CardModel model) {
  switch (model) {
    case CardModel::VoodooGraphics: return "Voodoo1";
    case CardModel::VoodooRush:     return "Rush";
    case CardModel::Voodoo2:        return "Voodoo2";
    case CardModel::Voodoo3:        return "Voodoo3";
    case CardModel::Voodoo5:        return "Voodoo5";
  }
  return "Voodoo2";
}

constexpr int32_t kGlyphW = 8;
constexpr int32_t kGlyphH = 8;
constexpr int32_t kLineHeight = 11;
constexpr int32_t kPadX = 6;
constexpr int32_t kPadY = 5;
constexpr int32_t kGraphHeight = 18;
constexpr int32_t kMargin = 8;

}  // namespace

void HudOverlay::Configure(const WrapperConfig& config,
                           const HudRuntimeInfo& runtime) {
  m_mask = config.hudMask;
  m_enabled = (m_mask != HUD_NONE);
  m_position = config.hudPosition;
  m_scale = std::clamp(config.hudScale, 0.5f, 4.0f);
  m_opacity = std::clamp(config.hudOpacity, 0.0f, 1.0f);

  m_backendShort = runtime.backendShort ? runtime.backendShort : "VK";
  m_cardShort = CardModelShortName(config.model);
  m_apiTag = config.activeFrontendApi.empty() ? "Glide" : config.activeFrontendApi;
  m_guestWidth = runtime.guestWidth;
  m_guestHeight = runtime.guestHeight;
  m_msaaSamples = config.msaaSamples;

  if (config.maxFps > 0.0f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dcap", static_cast<int>(config.maxFps));
    m_pacingTag = buf;
  } else {
    m_pacingTag = "Uncap";
  }
  if (config.vsyncStrictFifo) {
    m_pacingTag += "/FIFO";
  } else if (config.vsync) {
    m_pacingTag += "/VSync";
  } else {
    m_pacingTag += "/NoVS";
  }

  Reset();
}

void HudOverlay::Reset() {
  m_hasPrevStamp = false;
  m_windowFrames = 0;
  m_displayFps = 0.0f;
  m_frameTimeHistory.fill(0.0f);
  m_historyHead = 0;
  m_historyCount = 0;
  m_width = 0;
  m_height = 0;
  m_rgbaTopDown.clear();
  m_rgbaBottomUp.clear();
  m_lines.clear();
  // Keep m_revision monotonically non-zero on next rebuild so backends upload.
}

bool HudOverlay::Update(const FrameMetrics& lastMetrics) {
  if (!m_enabled) return false;

  const auto now = std::chrono::steady_clock::now();

  // Record frametime sample for graph (ignore uninitialized 0th frame)
  if (lastMetrics.frameTimeMs > 0.0f) {
    m_frameTimeHistory[m_historyHead] = lastMetrics.frameTimeMs;
    m_historyHead = (m_historyHead + 1) % kHistorySize;
    if (m_historyCount < kHistorySize) {
      m_historyCount++;
    }
  }

  if (!m_hasPrevStamp) {
    m_hasPrevStamp = true;
    m_windowStart = now;
    m_lastRedrawTime = now;
    m_windowFrames = 0;
    m_displayFps = (lastMetrics.smoothedFps > 0.0f) ? lastMetrics.smoothedFps : 0.0f;
    RebuildSurface(lastMetrics, m_displayFps);
    return true;
  }

  m_windowFrames++;
  const double windowSec =
      std::chrono::duration<double>(now - m_windowStart).count();

  // Update smoothed HUD FPS every 250ms (fast convergence from frame 1 onward).
  if (windowSec >= 0.25) {
    const float instantFps = static_cast<float>(m_windowFrames / windowSec);
    if (m_displayFps <= 0.0f) {
      m_displayFps = instantFps;
    } else {
      m_displayFps = m_displayFps * 0.35f + instantFps * 0.65f;
    }
    m_windowStart = now;
    m_windowFrames = 0;
  } else if (m_displayFps <= 0.0f && windowSec >= 0.05 && m_windowFrames >= 2) {
    m_displayFps = static_cast<float>(m_windowFrames / windowSec);
  }

  const double sinceRedrawMs =
      std::chrono::duration<double, std::milli>(now - m_lastRedrawTime).count();
  const double redrawIntervalMs = (m_mask & HUD_FRAMETIMES) ? 100.0 : 250.0;
  if (m_width == 0 || sinceRedrawMs >= redrawIntervalMs) {
    m_lastRedrawTime = now;
    RebuildSurface(lastMetrics, m_displayFps);
    return true;
  }

  return false;
}

void HudOverlay::ForceRender(const FrameMetrics& metrics, float displayFps) {
  if (!m_enabled) return;
  if (metrics.frameTimeMs > 0.0f) {
    m_frameTimeHistory[m_historyHead] = metrics.frameTimeMs;
    m_historyHead = (m_historyHead + 1) % kHistorySize;
    if (m_historyCount < kHistorySize) {
      m_historyCount++;
    }
  }
  m_displayFps = displayFps;
  RebuildSurface(metrics, displayFps);
}

void HudOverlay::BlendPixel(int32_t x, int32_t y, uint8_t r, uint8_t g,
                            uint8_t b, uint8_t a) {
  if (x < 0 || y < 0 || x >= static_cast<int32_t>(m_width) ||
      y >= static_cast<int32_t>(m_height) || a == 0) {
    return;
  }
  const size_t idx =
      (static_cast<size_t>(y) * m_width + static_cast<size_t>(x)) * 4;
  if (a == 255) {
    m_rgbaTopDown[idx + 0] = r;
    m_rgbaTopDown[idx + 1] = g;
    m_rgbaTopDown[idx + 2] = b;
    m_rgbaTopDown[idx + 3] = 255;
    return;
  }
  const uint32_t sa = a;
  const uint32_t da = m_rgbaTopDown[idx + 3];
  const uint32_t outA = sa + ((da * (255u - sa) + 127u) / 255u);
  if (outA == 0) return;

  const uint32_t dr = m_rgbaTopDown[idx + 0];
  const uint32_t dg = m_rgbaTopDown[idx + 1];
  const uint32_t db = m_rgbaTopDown[idx + 2];
  const uint32_t invSa = 255u - sa;
  m_rgbaTopDown[idx + 0] = static_cast<uint8_t>(
      std::min<uint32_t>(255u, (r * sa + ((dr * da * invSa) / 255u)) / outA));
  m_rgbaTopDown[idx + 1] = static_cast<uint8_t>(
      std::min<uint32_t>(255u, (g * sa + ((dg * da * invSa) / 255u)) / outA));
  m_rgbaTopDown[idx + 2] = static_cast<uint8_t>(
      std::min<uint32_t>(255u, (b * sa + ((db * da * invSa) / 255u)) / outA));
  m_rgbaTopDown[idx + 3] = static_cast<uint8_t>(std::min<uint32_t>(255u, outA));
}

void HudOverlay::FillRect(int32_t x, int32_t y, int32_t w, int32_t h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  for (int32_t py = y; py < y + h; ++py) {
    for (int32_t px = x; px < x + w; ++px) {
      BlendPixel(px, py, r, g, b, a);
    }
  }
}

void HudOverlay::DrawChar(int32_t x, int32_t y, char ch, uint8_t r, uint8_t g,
                          uint8_t b, uint8_t a) {
  const unsigned char uc = static_cast<unsigned char>(ch);
  if (uc < 0x20 || uc > 0x7E) return;
  const uint8_t* glyph = kFont8x8[uc - 0x20];
  for (int32_t row = 0; row < kGlyphH; ++row) {
    const uint8_t bits = glyph[row];
    if (bits == 0) continue;
    for (int32_t col = 0; col < kGlyphW; ++col) {
      if (bits & (0x80u >> col)) {
        BlendPixel(x + col, y + row, r, g, b, a);
      }
    }
  }
}

void HudOverlay::DrawTextWithShadow(int32_t x, int32_t y,
                                    const std::string& text,
                                    uint8_t r, uint8_t g, uint8_t b) {
  int32_t cx = x;
  for (char ch : text) {
    DrawChar(cx + 1, y + 1, ch, 0, 0, 0, 220);
    DrawChar(cx, y, ch, r, g, b, 255);
    cx += kGlyphW;
  }
}

void HudOverlay::RebuildSurface(const FrameMetrics& metrics, float displayFps) {
  std::vector<StyledLine> rows;
  char buf[96];

  if (m_mask & HUD_FPS) {
    if (displayFps >= 999.5f) {
      std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(displayFps));
    } else if (displayFps > 0.0f) {
      std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(displayFps));
    } else {
      std::snprintf(buf, sizeof(buf), "--.-");
    }
    rows.push_back({"FPS ", buf});
  }

  if (m_mask & HUD_FRAMETIMES) {
    std::snprintf(buf, sizeof(buf), "%.1fms (R:%.1f P:%.1f)",
                  static_cast<double>(metrics.frameTimeMs),
                  static_cast<double>(metrics.renderTimeMs),
                  static_cast<double>(metrics.swapPresentMs));
    rows.push_back({"FT  ", buf});
  }

  if (m_mask & HUD_BACKEND) {
    std::snprintf(buf, sizeof(buf), "%s %s %s", m_backendShort.c_str(),
                  m_cardShort.c_str(), m_pacingTag.c_str());
    rows.push_back({"DRV ", buf});
  }

  if (m_mask & HUD_API) {
    if (m_msaaSamples > 1) {
      std::snprintf(buf, sizeof(buf), "%s %ux%u %uxAA", m_apiTag.c_str(),
                    m_guestWidth, m_guestHeight, m_msaaSamples);
    } else {
      std::snprintf(buf, sizeof(buf), "%s %ux%u", m_apiTag.c_str(),
                    m_guestWidth, m_guestHeight);
    }
    rows.push_back({"API ", buf});
  }

  if (m_mask & HUD_DRAWCALLS) {
    std::snprintf(buf, sizeof(buf), "%u (F:%u) TRI:%u", metrics.drawCalls,
                  metrics.stateFlushes, metrics.trianglesDrawn);
    rows.push_back({"DRW ", buf});
  }

  if (m_mask & HUD_LFB) {
    const double lfbKb =
        static_cast<double>(metrics.lfbBytesRead + metrics.lfbBytesWritten) /
        1024.0;
    std::snprintf(buf, sizeof(buf), "%uR/%uW %.0fK TEX:%u",
                  metrics.lfbReadLocks, metrics.lfbWriteLocks, lfbKb,
                  metrics.texUploads);
    rows.push_back({"LFB ", buf});
  }

  if (rows.empty()) {
    rows.push_back({"FPS ", "--.-"});
  }

  m_lines.clear();
  size_t maxChars = 0;
  for (const auto& r : rows) {
    const std::string full = r.label + r.value;
    maxChars = std::max(maxChars, full.size());
    m_lines.push_back(full);
  }

  const bool showGraph = (m_mask & HUD_FRAMETIMES) != 0;
  const int32_t textW = static_cast<int32_t>(maxChars) * kGlyphW;
  const int32_t minGraphW = showGraph ? static_cast<int32_t>(kHistorySize * 2) : 0;
  const int32_t contentW = std::max(textW, minGraphW);
  const int32_t textH = static_cast<int32_t>(rows.size()) * kLineHeight - 3;
  const int32_t graphExtraH = showGraph ? (kGraphHeight + 5) : 0;

  m_width = static_cast<uint32_t>(contentW + kPadX * 2);
  m_height = static_cast<uint32_t>(textH + graphExtraH + kPadY * 2);

  m_rgbaTopDown.assign(static_cast<size_t>(m_width) * m_height * 4, 0u);

  // Translucent dark backdrop pill + subtle 3dfx amber border
  const uint8_t bgAlpha =
      static_cast<uint8_t>(std::lround(std::clamp(m_opacity, 0.0f, 1.0f) * 255.0f));
  if (bgAlpha > 0) {
    FillRect(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
             12, 16, 22, bgAlpha);
    const uint8_t borderAlpha = static_cast<uint8_t>(
        std::min<int>(220, static_cast<int>(bgAlpha) + 24));
    // Top & bottom border
    FillRect(0, 0, static_cast<int32_t>(m_width), 1, 255, 192, 64, borderAlpha);
    FillRect(0, static_cast<int32_t>(m_height) - 1,
             static_cast<int32_t>(m_width), 1, 255, 192, 64, borderAlpha);
    // Left & right border
    FillRect(0, 1, 1, static_cast<int32_t>(m_height) - 2, 255, 192, 64,
             borderAlpha);
    FillRect(static_cast<int32_t>(m_width) - 1, 1, 1,
             static_cast<int32_t>(m_height) - 2, 255, 192, 64, borderAlpha);
  }

  // Render styled lines: amber label + crisp white value
  int32_t y = kPadY;
  for (const auto& r : rows) {
    DrawTextWithShadow(kPadX, y, r.label, 255, 204, 72);
    const int32_t valX = kPadX + static_cast<int32_t>(r.label.size()) * kGlyphW;
    DrawTextWithShadow(valX, y, r.value, 245, 248, 252);
    y += kLineHeight;
  }

  // Optional mini frametime bar graph (0..50ms scale, 60 FPS = 16.7ms guide)
  if (showGraph) {
    const int32_t gx = kPadX;
    const int32_t gy = y + 2;
    const int32_t gw = contentW;
    const int32_t gh = kGraphHeight;
    FillRect(gx, gy, gw, gh, 6, 8, 12,
             static_cast<uint8_t>(std::max<int>(120, bgAlpha)));
    // 16.67ms (60 FPS) subtle reference line (~1/3 height from bottom)
    const int32_t ref60Y = gy + gh - 1 - (gh * 17) / 50;
    FillRect(gx, ref60Y, gw, 1, 70, 95, 120, 180);

    const size_t count = m_historyCount;
    const int32_t barW = std::max<int32_t>(1, gw / static_cast<int32_t>(kHistorySize));
    const size_t startIdx =
        (m_historyHead + kHistorySize - count) % kHistorySize;
    for (size_t i = 0; i < count; ++i) {
      const float ms = m_frameTimeHistory[(startIdx + i) % kHistorySize];
      const float clamped = std::clamp(ms, 0.0f, 50.0f);
      const int32_t barH = std::max<int32_t>(
          1, static_cast<int32_t>(std::lround((clamped / 50.0f) * (gh - 2))));
      uint8_t br = 80, bg = 225, bb = 115;  // <= 18ms green
      if (ms > 34.0f) {
        br = 240; bg = 82; bb = 82;         // > 34ms red
      } else if (ms > 18.5f) {
        br = 250; bg = 196; bb = 64;        // 18.5..34ms amber
      }
      const int32_t bx = gx + static_cast<int32_t>(i) * barW;
      if (bx + barW <= gx + gw) {
        FillRect(bx, gy + gh - 1 - barH, barW, barH, br, bg, bb, 235);
      }
    }
  }

  // Generate bottom-up copy for OpenGL ES texture upload (reusing m_glVAO)
  m_rgbaBottomUp.resize(m_rgbaTopDown.size());
  const size_t rowBytes = static_cast<size_t>(m_width) * 4;
  for (uint32_t row = 0; row < m_height; ++row) {
    const uint8_t* srcRow = m_rgbaTopDown.data() + row * rowBytes;
    uint8_t* dstRow =
        m_rgbaBottomUp.data() + (m_height - 1u - row) * rowBytes;
    std::memcpy(dstRow, srcRow, rowBytes);
  }

  m_revision++;
}

void HudOverlay::ComputePlacement(int32_t presentX, int32_t presentY,
                                  uint32_t presentW, uint32_t presentH,
                                  int32_t& outX, int32_t& outY,
                                  uint32_t& outW, uint32_t& outH) const {
  if (!m_enabled || m_width == 0 || m_height == 0 ||
      presentW == 0 || presentH == 0) {
    outX = presentX;
    outY = presentY;
    outW = 0;
    outH = 0;
    return;
  }

  uint32_t drawW =
      std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(m_width * m_scale)));
  uint32_t drawH =
      std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(m_height * m_scale)));

  const uint32_t margin =
      (presentW > 64 && presentH > 64) ? static_cast<uint32_t>(kMargin) : 2u;
  const uint32_t maxW = (presentW > margin * 2) ? (presentW - margin * 2) : presentW;
  const uint32_t maxH = (presentH > margin * 2) ? (presentH - margin * 2) : presentH;

  // Shrink proportionally if the HUD would exceed the viewport
  if (drawW > maxW || drawH > maxH) {
    const float sx = static_cast<float>(maxW) / static_cast<float>(drawW);
    const float sy = static_cast<float>(maxH) / static_cast<float>(drawH);
    const float s = std::min(sx, sy);
    drawW = std::max<uint32_t>(1u, static_cast<uint32_t>(std::floor(drawW * s)));
    drawH = std::max<uint32_t>(1u, static_cast<uint32_t>(std::floor(drawH * s)));
  }

  int32_t x = presentX + static_cast<int32_t>(margin);
  int32_t y = presentY + static_cast<int32_t>(margin);

  switch (m_position) {
    case HudPosition::TOP_LEFT:
      x = presentX + static_cast<int32_t>(margin);
      y = presentY + static_cast<int32_t>(margin);
      break;
    case HudPosition::TOP_RIGHT:
      x = presentX + static_cast<int32_t>(presentW) - static_cast<int32_t>(margin) -
          static_cast<int32_t>(drawW);
      y = presentY + static_cast<int32_t>(margin);
      break;
    case HudPosition::BOTTOM_LEFT:
      x = presentX + static_cast<int32_t>(margin);
      y = presentY + static_cast<int32_t>(presentH) - static_cast<int32_t>(margin) -
          static_cast<int32_t>(drawH);
      break;
    case HudPosition::BOTTOM_RIGHT:
      x = presentX + static_cast<int32_t>(presentW) - static_cast<int32_t>(margin) -
          static_cast<int32_t>(drawW);
      y = presentY + static_cast<int32_t>(presentH) - static_cast<int32_t>(margin) -
          static_cast<int32_t>(drawH);
      break;
  }

  // Final strict clamp inside present rect
  const int32_t maxX = presentX + static_cast<int32_t>(presentW) - static_cast<int32_t>(drawW);
  const int32_t maxY = presentY + static_cast<int32_t>(presentH) - static_cast<int32_t>(drawH);
  outX = std::clamp(x, presentX, std::max(presentX, maxX));
  outY = std::clamp(y, presentY, std::max(presentY, maxY));
  outW = drawW;
  outH = drawH;
}

void HudOverlay::CompositeOverBgra32(uint32_t* dstPixels, uint32_t dstW,
                                     uint32_t dstH,
                                     uint32_t dstStrideWords) const {
  if (!m_enabled || !dstPixels || m_rgbaTopDown.empty() ||
      m_width == 0 || m_height == 0 || dstW == 0 || dstH == 0) {
    return;
  }

  int32_t ox = 0, oy = 0;
  uint32_t drawW = 0, drawH = 0;
  ComputePlacement(0, 0, dstW, dstH, ox, oy, drawW, drawH);
  if (drawW == 0 || drawH == 0) return;

  for (uint32_t dy = 0; dy < drawH; ++dy) {
    const int32_t py = oy + static_cast<int32_t>(dy);
    if (py < 0 || py >= static_cast<int32_t>(dstH)) continue;
    const uint32_t sy = std::min<uint32_t>(
        m_height - 1u, (dy * m_height) / drawH);
    uint32_t* dstRow = dstPixels + static_cast<size_t>(py) * dstStrideWords;
    const uint8_t* srcRow =
        m_rgbaTopDown.data() + static_cast<size_t>(sy) * m_width * 4;

    for (uint32_t dx = 0; dx < drawW; ++dx) {
      const int32_t px = ox + static_cast<int32_t>(dx);
      if (px < 0 || px >= static_cast<int32_t>(dstW)) continue;
      const uint32_t sx = std::min<uint32_t>(
          m_width - 1u, (dx * m_width) / drawW);
      const uint8_t* sp = srcRow + static_cast<size_t>(sx) * 4;
      const uint32_t sa = sp[3];
      if (sa == 0) continue;

      const uint32_t sr = sp[0];
      const uint32_t sg = sp[1];
      const uint32_t sb = sp[2];

      if (sa == 255) {
        dstRow[px] = 0xFF000000u | (sr << 16) | (sg << 8) | sb;
      } else {
        const uint32_t dp = dstRow[px];
        const uint32_t dr = (dp >> 16) & 0xFFu;
        const uint32_t dg = (dp >> 8) & 0xFFu;
        const uint32_t db = dp & 0xFFu;
        const uint32_t inv = 255u - sa;
        const uint32_t outR = (sr * sa + dr * inv + 127u) / 255u;
        const uint32_t outG = (sg * sa + dg * inv + 127u) / 255u;
        const uint32_t outB = (sb * sa + db * inv + 127u) / 255u;
        dstRow[px] = 0xFF000000u | (outR << 16) | (outG << 8) | outB;
      }
    }
  }
}

}  // namespace GlideWrapper
