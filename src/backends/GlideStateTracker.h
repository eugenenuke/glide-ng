#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>

#include "core/IGraphicsBackend.h"
#include "core/WrapperConfig.h"

namespace GlideWrapper {

// Convert a packed 32-bit GrColor_t encoded in Glide's active GrColorFormat_t
// (0=ARGB, 1=ABGR, 2=RGBA, 3=BGRA) into canonical 0xAARRGGBB.
inline uint32_t GrColorToCanonicalArgb(uint32_t color,
                                       uint32_t colorFormat) noexcept {
  switch (colorFormat) {
    case 1u:  // GR_COLORFORMAT_ABGR (0xAABBGGRR) -> 0xAARRGGBB
      return (color & 0xFF00FF00u) | ((color & 0x000000FFu) << 16) |
             ((color & 0x00FF0000u) >> 16);
    case 2u:  // GR_COLORFORMAT_RGBA (0xRRGGBBAA) -> 0xAARRGGBB
      return ((color & 0x000000FFu) << 24) | ((color & 0xFFFFFF00u) >> 8);
    case 3u:  // GR_COLORFORMAT_BGRA (0xBBGGRRAA) -> 0xAARRGGBB
      return ((color & 0x000000FFu) << 24) | ((color & 0x0000FF00u) << 8) |
             ((color & 0x00FF0000u) >> 8) | ((color & 0xFF000000u) >> 24);
    default:  // 0 = GR_COLORFORMAT_ARGB (0xAARRGGBB)
      return color;
  }
}

// Convert canonical 0xAARRGGBB into Glide's active GrColorFormat_t
// (0=ARGB, 1=ABGR, 2=RGBA, 3=BGRA).
inline uint32_t CanonicalArgbToGrColor(uint32_t argb,
                                       uint32_t colorFormat) noexcept {
  switch (colorFormat) {
    case 1u:  // GR_COLORFORMAT_ABGR
      return (argb & 0xFF00FF00u) | ((argb & 0x000000FFu) << 16) |
             ((argb & 0x00FF0000u) >> 16);
    case 2u:  // GR_COLORFORMAT_RGBA
      return ((argb & 0x00FFFFFFu) << 8) | ((argb & 0xFF000000u) >> 24);
    case 3u:  // GR_COLORFORMAT_BGRA
      return ((argb & 0x000000FFu) << 24) | ((argb & 0x0000FF00u) << 8) |
             ((argb & 0x00FF0000u) >> 8) | ((argb & 0xFF000000u) >> 24);
    default:  // 0 = GR_COLORFORMAT_ARGB
      return argb;
  }
}

inline void UnpackGrColorNorm(uint32_t color, uint32_t colorFormat, float& r,
                              float& g, float& b, float& a) noexcept {
  const uint32_t argb = GrColorToCanonicalArgb(color, colorFormat);
  a = static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
  r = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
  g = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
  b = static_cast<float>(argb & 0xFFu) / 255.0f;
}

/**
 * @brief Pure Glide 2.x/3.x pipeline state tracker implementing shared
 * IGraphicsBackend state setters, state getters, and lifecycle metadata.
 *
 * Intentionally carries zero CPU framebuffer memory, depth buffers, SSAA
 * arrays, or OS windowing headers so GPU backends (Vulkan, OpenGL ES) do not
 * inherit software-rasterizer resources.
 */
class GlideStateTracker : public IGraphicsBackend {
 public:
  GlideStateTracker();
  ~GlideStateTracker() override = default;

  // Derived classes MUST implement these context/lifecycle routines
  bool Initialize(const WrapperConfig& config) override = 0;
  void Shutdown() override = 0;
  void ResetState() override;
  bool AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height,
                    bool windowed) override = 0;
  void DetachWindow() override = 0;

  uint32_t GetWidth() const override { return m_headlessWidth; }
  uint32_t GetHeight() const override { return m_headlessHeight; }
  uint32_t GetGuestWidth() const override { return m_guestWidth; }
  uint32_t GetGuestHeight() const override { return m_guestHeight; }
  uint32_t GetMsaaSamples() const override { return 1; }
  void SetRequestedMsaaSamples(uint32_t samples) override;
  bool GetAAMultiSample() const { return m_aaMultiSample; }
  uint32_t GetTBufferWriteMask() const { return m_tbufferWriteMask; }
  bool IsHeadlessMode() const { return m_headlessMode; }

  // State Getters for Testing & Diagnostics
  uint32_t GetColorCombinerFunc() const { return m_colorCombinerFunc; }
  uint32_t GetColorCombinerFactor() const { return m_colorCombinerFactor; }
  uint32_t GetColorCombinerLocal() const { return m_colorCombinerLocal; }
  uint32_t GetColorCombinerOther() const { return m_colorCombinerOther; }
  uint32_t GetConstantColor() const { return m_constantColor; }
  uint32_t GetSTWHintMask() const { return m_stwHintMask; }
  uint32_t GetCullMode() const { return m_cullMode; }
  uint32_t GetSstOrigin() const { return m_sstOrigin; }
  uint32_t GetClipMinX() const { return m_clipMinX; }
  uint32_t GetClipMinY() const { return m_clipMinY; }
  uint32_t GetClipMaxX() const { return m_clipMaxX; }
  uint32_t GetClipMaxY() const { return m_clipMaxY; }
  bool GetColorMaskRgb() const { return m_colorMaskRgb; }
  bool GetColorMaskR() const { return m_colorMaskR; }
  bool GetColorMaskG() const { return m_colorMaskG; }
  bool GetColorMaskB() const { return m_colorMaskB; }
  bool GetColorMaskAlpha() const { return m_colorMaskAlpha; }
  uint8_t GetColorWriteMask4() const {
    return static_cast<uint8_t>((m_colorMaskR ? 1u : 0u) |
                                (m_colorMaskG ? 2u : 0u) |
                                (m_colorMaskB ? 4u : 0u) |
                                (m_colorMaskAlpha ? 8u : 0u));
  }
  bool IsFullColorMask() const {
    return m_colorMaskR && m_colorMaskG && m_colorMaskB && m_colorMaskAlpha;
  }
  bool GetTexCombinerRgbInvert(uint32_t tmu) const {
    return tmu < 2 ? m_texCombinerRgbInvert[tmu] : false;
  }
  bool GetTexCombinerAlphaInvert(uint32_t tmu) const {
    return tmu < 2 ? m_texCombinerAlphaInvert[tmu] : false;
  }
  float GetTexLodBias(uint32_t tmu) const {
    return tmu < 2 ? m_texLodBias[tmu] : 0.0f;
  }
  float GetDepthNear() const { return m_depthNear; }
  float GetDepthFar() const { return m_depthFar; }
  uint32_t GetDitherMode() const { return m_ditherMode; }
  uint32_t GetStippleMode() const { return m_stippleMode; }
  uint32_t GetStipplePattern() const { return m_stipplePattern; }
  uint32_t GetTexChromaMode(uint32_t tmu) const {
    return tmu < 2 ? m_texChromaMode[tmu] : 0;
  }
  uint32_t GetTexChromaMin(uint32_t tmu) const {
    return tmu < 2 ? m_texChromaMin[tmu] : 0;
  }
  uint32_t GetTexChromaMax(uint32_t tmu) const {
    return tmu < 2 ? m_texChromaMax[tmu] : 0;
  }
  uint32_t GetTexChromaRangeMode(uint32_t tmu) const {
    return tmu < 2 ? m_texChromaRangeMode[tmu] : 0;
  }

  bool SwapBuffers() override = 0;
  void FlushBins() override = 0;
  void PollEvents() override;
  void SstIdle() override;

  // Shared Glide Pipeline State Mutations
  void SetConstantColor(uint32_t color) override;
  void SetPixelFormat(uint32_t format) override;
  void SetRenderBuffer(uint32_t target) override;

  void SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask,
                     int32_t biasLevel) override;
  void SetDepthRange(float nearVal, float farVal) override;
  void SetDitherMode(uint32_t mode) override;
  void SetStippleState(uint32_t mode, uint32_t pattern) override;
  void SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                     uint32_t alphaSrcFactor, uint32_t alphaDstFactor) override;
  void SetAlphaTestState(uint32_t compareOp, uint32_t refVal) override;
  void SetCullState(uint32_t cullMode) override;
  void SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX,
                     uint32_t maxY) override;
  void SetSstOrigin(uint32_t origin) override;
  void SetColorMask(bool rgb, bool alpha) override;
  void SetColorMaskExt(bool r, bool g, bool b, bool a) override;
  void SetGamma(float gamma) override;
  void LoadGammaTable(uint32_t nentries, const uint32_t* rTable,
                      const uint32_t* gTable, const uint32_t* bTable) override;
  void GetGammaTable(uint32_t nentries, uint32_t* rTable, uint32_t* gTable,
                     uint32_t* bTable) const override;

  void SetFogMode(uint32_t mode) override;
  void SetFogColor(uint32_t color) override;
  void SetFogTable(const uint8_t* table) override;

  void BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS,
                   uint32_t clampT, uint32_t minFilter,
                   uint32_t magFilter) override;
  void UploadTexture(uint32_t tmu, uint32_t startAddress,
                     const struct VirtualTexture& tex) override;
  void SetTexLodBias(uint32_t tmu, float bias) override;
  void SetTexDetailControl(uint32_t tmu, int32_t lodBias, uint8_t detailScale,
                           float detailMax) override;
  void SetTexMipMapMode(uint32_t tmu, uint32_t mode, bool lodBlend) override;
  void UploadTexturePartial(uint32_t tmu, uint32_t startAddress,
                            const struct VirtualTexture& tex, uint32_t lodLevel,
                            uint32_t startRow, uint32_t endRow) override;
  void PurgeTextures() override;

  void SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor,
                       uint32_t colorLocal, uint32_t colorOther,
                       bool colorInvert, uint32_t alphaFunc,
                       uint32_t alphaFactor, uint32_t alphaLocal,
                       uint32_t alphaOther, bool alphaInvert) override;
  void SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor,
                          uint32_t alphaFunc, uint32_t alphaFactor,
                          bool rgbInvert, bool alphaInvert) override;
  void SetSTWHintState(uint32_t hintMask) override;
  void SetChromakeyMode(uint32_t mode) override;
  void SetChromakeyValue(uint32_t value) override;
  void SetChromakeyRange(uint32_t minColor, uint32_t maxColor,
                         uint32_t mode) override;
  void SetTexChromakeyMode(uint32_t tmu, uint32_t mode) override;
  void SetTexChromakeyRange(uint32_t tmu, uint32_t minColor, uint32_t maxColor,
                            uint32_t mode) override;
  void SetAAState(bool enabled) override;
  void SetAAMultiSample(bool enabled) override;
  void SetTBufferWriteMask(uint32_t mask) override;
  void SetStencilMode(bool enabled) override;
  void SetStencilFunc(uint32_t compareOp, uint8_t refVal,
                      uint8_t readMask) override;
  void SetStencilMask(uint8_t writeMask) override;
  void SetStencilOp(uint32_t failOp, uint32_t zFailOp,
                    uint32_t zPassOp) override;
  void SetBlendStateExt(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                        uint32_t rgbOp, uint32_t alphaSrcFactor,
                        uint32_t alphaDstFactor, uint32_t alphaOp) override;
  void SetColorCombineExt(uint32_t packed) override;
  void SetAlphaCombineExt(uint32_t packed) override;
  void SetTexColorCombineExt(uint32_t tmu, uint32_t packed) override;
  void SetTexAlphaCombineExt(uint32_t tmu, uint32_t packed) override;
  void SetTmuConstantColor(uint32_t tmu, uint32_t color) override;

 protected:
  mutable std::mutex m_mutex;
  bool m_stateCacheDirty{true};
  void MarkStateDirty() { m_stateCacheDirty = true; }

  // Internal lock-held hooks (callers MUST hold m_mutex)
  virtual void ResetState_Locked();
  virtual void FlushBins_Locked() {}

  void SetConstantColor_Locked(uint32_t color);
  void SetPixelFormat_Locked(uint32_t format);
  void SetRenderBuffer_Locked(uint32_t target);
  void SetDepthState_Locked(uint32_t depthMode, uint32_t compareOp,
                            bool depthMask, int32_t biasLevel);
  void SetDepthRange_Locked(float nearVal, float farVal);
  void SetDitherMode_Locked(uint32_t mode);
  void SetStippleState_Locked(uint32_t mode, uint32_t pattern);
  void SetBlendState_Locked(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                            uint32_t alphaSrcFactor, uint32_t alphaDstFactor);
  void SetAlphaTestState_Locked(uint32_t compareOp, uint32_t refVal);
  void SetCullState_Locked(uint32_t cullMode);
  void SetClipWindow_Locked(uint32_t minX, uint32_t minY, uint32_t maxX,
                            uint32_t maxY);
  bool ComputeClipRectTopDown_Locked(uint32_t& x0, uint32_t& y0, uint32_t& x1,
                                     uint32_t& y1) const {
    const uint32_t scaleX =
        (m_guestWidth > 0 && m_headlessWidth >= m_guestWidth)
            ? (m_headlessWidth / m_guestWidth)
            : 1u;
    const uint32_t scaleY =
        (m_guestHeight > 0 && m_headlessHeight >= m_guestHeight)
            ? (m_headlessHeight / m_guestHeight)
            : 1u;
    x0 = std::min(m_clipMinX * scaleX, m_headlessWidth);
    x1 = std::min(m_clipMaxX * scaleX, m_headlessWidth);
    uint32_t rawMinY = std::min(m_clipMinY * scaleY, m_headlessHeight);
    uint32_t rawMaxY = std::min(m_clipMaxY * scaleY, m_headlessHeight);
    if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT -> top-down framebuffer Y
      y0 = m_headlessHeight - rawMaxY;
      y1 = m_headlessHeight - rawMinY;
    } else {
      y0 = rawMinY;
      y1 = rawMaxY;
    }
    return (x0 < x1) && (y0 < y1);
  }
  bool IsFullScreenClip_Locked() const {
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    return ComputeClipRectTopDown_Locked(x0, y0, x1, y1) && x0 == 0 &&
           y0 == 0 && x1 == m_headlessWidth && y1 == m_headlessHeight;
  }
  void SetSstOrigin_Locked(uint32_t origin);
  void SetColorMask_Locked(bool rgb, bool alpha);
  void SetColorMaskExt_Locked(bool r, bool g, bool b, bool a);
  void SetGamma_Locked(float gamma);
  void LoadGammaTable_Locked(uint32_t nentries, const uint32_t* rTable,
                             const uint32_t* gTable, const uint32_t* bTable);
  void SetFogMode_Locked(uint32_t mode);
  void SetFogColor_Locked(uint32_t color);
  void SetFogTable_Locked(const uint8_t* table);
  void BindTexture_Locked(uint32_t tmu, uint32_t startAddress, uint32_t clampS,
                          uint32_t clampT, uint32_t minFilter,
                          uint32_t magFilter);
  void SetTexLodBias_Locked(uint32_t tmu, float bias);
  void SetTexDetailControl_Locked(uint32_t tmu, int32_t lodBias,
                                  uint8_t detailScale, float detailMax);
  void SetTexMipMapMode_Locked(uint32_t tmu, uint32_t mode, bool lodBlend);
  void SetCombinerMode_Locked(uint32_t colorFunc, uint32_t colorFactor,
                              uint32_t colorLocal, uint32_t colorOther,
                              bool colorInvert, uint32_t alphaFunc,
                              uint32_t alphaFactor, uint32_t alphaLocal,
                              uint32_t alphaOther, bool alphaInvert);
  void SetTexCombinerMode_Locked(uint32_t tmu, uint32_t rgbFunc,
                                 uint32_t rgbFactor, uint32_t alphaFunc,
                                 uint32_t alphaFactor, bool rgbInvert,
                                 bool alphaInvert);
  void SetSTWHintState_Locked(uint32_t hintMask);
  void SetChromakeyMode_Locked(uint32_t mode);
  void SetChromakeyValue_Locked(uint32_t value);
  void SetChromakeyRange_Locked(uint32_t minColor, uint32_t maxColor,
                                uint32_t mode);
  void SetTexChromakeyMode_Locked(uint32_t tmu, uint32_t mode);
  void SetTexChromakeyRange_Locked(uint32_t tmu, uint32_t minColor,
                                   uint32_t maxColor, uint32_t mode);
  void SetAAState_Locked(bool enabled);
  void SetAAMultiSample_Locked(bool enabled);
  void SetTBufferWriteMask_Locked(uint32_t mask);
  void SetStencilMode_Locked(bool enabled);
  void SetStencilFunc_Locked(uint32_t compareOp, uint8_t refVal,
                             uint8_t readMask);
  void SetStencilMask_Locked(uint8_t writeMask);
  void SetStencilOp_Locked(uint32_t failOp, uint32_t zFailOp,
                           uint32_t zPassOp);
  bool m_initialized{false};
  bool m_windowAttached{false};
  bool m_headlessMode{false};
  WrapperConfig m_config;

  uint32_t m_headlessWidth{800};
  uint32_t m_headlessHeight{600};
  uint32_t m_guestWidth{800};
  uint32_t m_guestHeight{600};
  uint32_t m_activeRenderBuffer{1};  // 1 = GR_BUFFER_BACKBUFFER (default)

  // Depth, Dither, Stipple, Blend, Alpha Test, Cull, Clip, Origin, Fog
  uint32_t m_depthMode{0};       // 0 = Z-buffer, 1 = W-buffer
  uint32_t m_depthCompareOp{1};  // 1 = LESS (GR_CMP_LESS)
  bool m_depthMask{true};
  int32_t m_depthBiasLevel{0};
  float m_depthNear{1.0f};
  float m_depthFar{65535.0f};
  uint32_t m_ditherMode{0};
  uint32_t m_stippleMode{0};
  uint32_t m_stipplePattern{0xFFFFFFFF};

  uint32_t m_rgbSrcBlend{4};  // 4 = ONE (GR_BLEND_ONE)
  uint32_t m_rgbDstBlend{0};  // 0 = ZERO (GR_BLEND_ZERO)
  uint32_t m_rgbBlendOp{0};   // 0 = ADD, 1 = SUB, 2 = REVSUB
  uint32_t m_alphaSrcBlend{4};
  uint32_t m_alphaDstBlend{0};
  uint32_t m_alphaBlendOp{0};

  uint32_t m_alphaTestOp{7};  // 7 = ALWAYS (GR_CMP_ALWAYS)
  uint32_t m_alphaTestRefVal{0};

  uint32_t m_cullMode{0};  // 0 = DISABLE (GR_CULL_DISABLE)

  uint32_t m_clipMinX{0};
  uint32_t m_clipMinY{0};
  uint32_t m_clipMaxX{800};
  uint32_t m_clipMaxY{600};
  uint32_t m_sstOrigin{0};  // 0 = GR_ORIGIN_UPPER_LEFT

  uint32_t m_fogMode{0};
  uint32_t m_fogColor{0xff000000};
  uint8_t m_fogTable[64] = {0};

  bool m_colorMaskRgb{true};
  bool m_colorMaskR{true};
  bool m_colorMaskG{true};
  bool m_colorMaskB{true};
  bool m_colorMaskAlpha{true};

  // Virtual TMU & Sampler States
  float m_texLodBias[2] = {0.0f, 0.0f};
  int32_t m_texDetailBias[2] = {0, 0};
  uint8_t m_texDetailScale[2] = {0, 0};
  float m_texDetailMax[2] = {0.0f, 0.0f};
  uint32_t PackTexDetail_Locked(uint32_t tmu) const {
    if (tmu >= 2) return 0u;
    const int32_t b = std::clamp(m_texDetailBias[tmu], -32, 31);
    const uint32_t s = static_cast<uint32_t>(std::min<uint8_t>(m_texDetailScale[tmu], 7u));
    const uint32_t m = static_cast<uint32_t>(
        std::clamp(m_texDetailMax[tmu], 0.0f, 1.0f) * 255.0f + 0.5f);
    return (static_cast<uint32_t>(b + 32) & 0x3Fu) | ((s & 0x7u) << 8) |
           ((m & 0xFFu) << 16);
  }
  uint32_t m_texMipMapMode[2] = {0, 0};
  bool m_texLodBlend[2] = {false, false};
  uint32_t m_boundTexAddress[2] = {0xFFFFFFFF, 0xFFFFFFFF};
  uint32_t m_texClampS[2] = {0, 0};
  uint32_t m_texClampT[2] = {0, 0};
  uint32_t m_texMinFilter[2] = {0, 0};
  uint32_t m_texMagFilter[2] = {0, 0};

  // Color & Alpha Combiner States
  uint32_t m_colorCombinerFunc = 0;
  uint32_t m_colorCombinerFactor = 0;
  uint32_t m_colorCombinerLocal = 0;
  uint32_t m_colorCombinerOther = 0;
  bool m_colorCombinerInvert = false;
  uint32_t m_alphaCombinerFunc = 0;
  uint32_t m_alphaCombinerFactor = 0;
  uint32_t m_alphaCombinerLocal = 0;
  uint32_t m_alphaCombinerOther = 0;
  bool m_alphaCombinerInvert = false;

  uint32_t m_texCombinerRgbFunc[2] = {0, 0};
  uint32_t m_texCombinerRgbFactor[2] = {0, 0};
  uint32_t m_texCombinerAlphaFunc[2] = {0, 0};
  uint32_t m_texCombinerAlphaFactor[2] = {0, 0};
  bool m_texCombinerRgbInvert[2] = {false, false};
  bool m_texCombinerAlphaInvert[2] = {false, false};

  uint32_t m_constantColor{0xFFFFFFFF};
  uint32_t m_stwHintMask{0};          // Default: 0 (GR_STWHINT_W_OK)
  uint32_t m_pixelFormatOverride{1};  // 1 = ABGR
  uint32_t m_chromakeyMode{0};        // 0 = GR_CHROMAKEY_DISABLE
  uint32_t m_chromakeyValue{0};
  uint32_t m_chromakeyRangeMin{0};
  uint32_t m_chromakeyRangeMax{0};
  uint32_t m_chromakeyRangeMode{0};
  uint32_t m_texChromaMode[2]{0, 0};
  uint32_t m_texChromaMin[2]{0, 0};
  uint32_t m_texChromaMax[2]{0, 0};
  uint32_t m_texChromaRangeMode[2]{0, 0};

  // Gamma correction LUT state
  bool m_useGammaLut{false};
  uint8_t m_lutR[256];
  uint8_t m_lutG[256];
  uint8_t m_lutB[256];

  bool m_aaEnabled{false};
  uint32_t m_requestedMsaaSamples{0};
  bool m_aaMultiSample{true};
  uint32_t m_tbufferWriteMask{0xFFFFFFFFu};

  // VSA-100 PIXEXT hardware stencil state
  bool m_stencilEnabled{false};
  uint32_t m_stencilCompareOp{7u};  // GR_CMP_ALWAYS
  uint8_t m_stencilRef{0u};
  uint8_t m_stencilReadMask{0xFFu};
  uint8_t m_stencilWriteMask{0xFFu};
  uint32_t m_stencilFailOp{0u};     // GR_STENCILOP_KEEP
  uint32_t m_stencilZFailOp{0u};    // GR_STENCILOP_KEEP
  uint32_t m_stencilZPassOp{0u};    // GR_STENCILOP_KEEP

  // VSA-100 COMBINE extension state (bit 31 = active; 0 = legacy mode)
  uint32_t m_extColorCombine{0u};
  uint32_t m_extAlphaCombine{0u};
  uint32_t m_extTexColorCombine[2]{0u, 0u};
  uint32_t m_extTexAlphaCombine[2]{0u, 0u};
  uint32_t m_tmuConstantColor[2]{0xFFFFFFFFu, 0xFFFFFFFFu};

  void SetBlendStateExt_Locked(uint32_t rgbSrcFactor, uint32_t rgbDstFactor,
                               uint32_t rgbOp, uint32_t alphaSrcFactor,
                               uint32_t alphaDstFactor, uint32_t alphaOp);
  void SetColorCombineExt_Locked(uint32_t packed);
  void SetAlphaCombineExt_Locked(uint32_t packed);
  void SetTexColorCombineExt_Locked(uint32_t tmu, uint32_t packed);
  void SetTexAlphaCombineExt_Locked(uint32_t tmu, uint32_t packed);
  void SetTmuConstantColor_Locked(uint32_t tmu, uint32_t color);
};

}  // namespace GlideWrapper
