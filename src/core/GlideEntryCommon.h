#pragma once

#include <cstdint>

#include "core/IGraphicsBackend.h"

namespace GlideWrapper {
namespace EntryCommon {

// Shared pipeline state cached by both the Glide 2.x and Glide 3.x frontends.
// API-specific fields (Glide 2 guTex* / STW hints, Glide 3 viewport / stipple /
// coordinate space) stay in their respective entry translation units.
struct SharedFrontendState {
  uint32_t depthMode{0};
  uint32_t depthCompare{1};
  bool depthMask{true};
  int32_t depthBias{0};

  uint32_t alphaTestOp{7};
  uint32_t alphaTestRef{0};

  uint32_t clampS[2]{0, 0};      // 0=WRAP, 1=CLAMP
  uint32_t clampT[2]{0, 0};
  uint32_t minFilter[2]{0, 0};   // 0=POINT, 1=BILINEAR
  uint32_t magFilter[2]{0, 0};

  uint32_t colorFunc{0x3};       // GR_COMBINE_FUNCTION_SCALE_OTHER
  uint32_t colorFactor{0x8};     // GR_COMBINE_FACTOR_ONE
  uint32_t colorLocal{0x0};      // GR_COMBINE_LOCAL_ITERATED
  uint32_t colorOther{0x0};      // GR_COMBINE_OTHER_ITERATED
  bool colorInvert{false};

  uint32_t alphaFunc{0x3};       // GR_COMBINE_FUNCTION_SCALE_OTHER
  uint32_t alphaFactor{0x8};     // GR_COMBINE_FACTOR_ONE
  uint32_t alphaLocal{0x1};      // GR_COMBINE_LOCAL_NONE (constant)
  uint32_t alphaOther{0x2};      // GR_COMBINE_OTHER_CONSTANT
  bool alphaInvert{false};

  uint32_t texRgbFunc[2]{1, 1};
  uint32_t texRgbFactor[2]{0, 0};
  uint32_t texAlphaFunc[2]{1, 1};
  uint32_t texAlphaFactor[2]{0, 0};
  bool texRgbInvert[2]{false, false};
  bool texAlphaInvert[2]{false, false};
  uint32_t boundTex[2]{0xFFFFFFFFu, 0xFFFFFFFFu};

  struct TexChromaState {
    uint32_t mode{0};
    uint32_t minColor{0};
    uint32_t maxColor{0};
  };
  TexChromaState texChromaState[2]{};

  float texLodBias[2]{0.0f, 0.0f};
  uint32_t mipmapMode[2]{0, 0};
  bool lodBlend[2]{false, false};

  uint32_t constantColor{0xFFFFFFFFu};
  uint32_t rgbSrcBlend{4};       // GR_BLEND_ONE
  uint32_t rgbDstBlend{0};       // GR_BLEND_ZERO
  uint32_t alphaSrcBlend{4};     // GR_BLEND_ONE
  uint32_t alphaDstBlend{0};     // GR_BLEND_ZERO

  uint32_t cullMode{0};          // GR_CULL_DISABLE
  uint32_t clipMinX{0};
  uint32_t clipMinY{0};
  uint32_t clipMaxX{640};
  uint32_t clipMaxY{480};

  uint32_t fogMode{0};
  uint32_t fogColor{0xFF000000u};
  uint8_t fogTable[64]{};

  bool colorMaskRgb{true};
  bool colorMaskAlpha{true};
  uint32_t ditherMode{0};

  uint8_t lfbConstantAlpha{255};
  uint32_t lfbWriteColorFormat{0};  // GR_COLORFORMAT_ARGB

  uint32_t chromakeyMode{0};
  uint32_t chromakeyValue{0};
  uint32_t chromakeyRangeMin{0};
  uint32_t chromakeyRangeMax{0};
  uint32_t chromakeyRangeMode{0};

  void Reset(uint8_t defaultLfbConstantAlpha, uint32_t defaultTexCombineFunc = 1,
             uint32_t clipWidth = 640, uint32_t clipHeight = 480);
};

struct ResolutionDims {
  uint32_t width;
  uint32_t height;
};

// Resolution lookup shared by Glide 2.x (0x0..0xF) and Glide 3.x (0x0..0x17).
ResolutionDims ResolveResolution(uint32_t resolutionEnum);

// Lifecycle & initialization helpers.
void ConfigureTmuMemoryLimits();
void InitGlide(const char* apiTag, void (*resetFrontendState)());
void EnsureEngineInitialized(void (*resetFrontendState)());
void ShutdownGlide(const char* apiTag);
void CloseWindow(const char* logMessage);

// Window clear/swap/buffer helpers.
void ClearBuffer(uint32_t color, uint8_t alpha, uint32_t depth);
void SwapBuffers();
void SetRenderBuffer(uint32_t buffer);
void SstIdle();

// Shared state synchronization & backend dispatchers.
void ApplySharedStateToBackend(const SharedFrontendState& st,
                               uint32_t sstOrigin, bool includeTexInvert);
void SetClipWindow(SharedFrontendState& st, uint32_t minx, uint32_t miny,
                   uint32_t maxx, uint32_t maxy);
void SetColorCombine(SharedFrontendState& st, uint32_t fnc, uint32_t factor,
                     uint32_t local, uint32_t other, bool invert);
void SetAlphaCombine(SharedFrontendState& st, uint32_t fnc, uint32_t factor,
                     uint32_t local, uint32_t other, bool invert);
void SetTexCombine(SharedFrontendState& st, uint32_t tmu, uint32_t rgbFnc,
                   uint32_t rgbFactor, uint32_t alphaFnc, uint32_t alphaFactor,
                   bool rgbInvert, bool alphaInvert, bool forwardInverts);
void SetAlphaBlendFunction(SharedFrontendState& st, uint32_t rgbSf,
                           uint32_t rgbDf, uint32_t alphaSf, uint32_t alphaDf);
void SetAlphaTestFunction(SharedFrontendState& st, uint32_t fnc);
void SetAlphaTestReferenceValue(SharedFrontendState& st, uint32_t refVal);
void SetTexFilterMode(SharedFrontendState& st, uint32_t tmu, uint32_t minFilter,
                      uint32_t magFilter);
void SetTexClampMode(SharedFrontendState& st, uint32_t tmu, uint32_t sClamp,
                     uint32_t tClamp);
void SetTexMipMapMode(SharedFrontendState& st, uint32_t tmu, uint32_t mode,
                      bool lodBlend);
void SetTexLodBias(SharedFrontendState& st, uint32_t tmu, float bias);
void BindTexSource(SharedFrontendState& st, uint32_t tmu,
                   uint32_t startAddress);
void SetDepthMode(SharedFrontendState& st, uint32_t mode);
void SetDepthFunction(SharedFrontendState& st, uint32_t fnc);
void SetDepthMask(SharedFrontendState& st, bool mask);
void SetDepthBias(SharedFrontendState& st, int32_t level);
void SetSstOrigin(uint32_t& sstOrigin, uint32_t origin);
void SetCullMode(SharedFrontendState& st, uint32_t mode);
void SetConstantColor(SharedFrontendState& st, uint32_t value);
void SetColorMask(SharedFrontendState& st, bool rgb, bool alpha);
void SetDitherMode(SharedFrontendState& st, uint32_t mode);
void SetChromakeyMode(SharedFrontendState& st, uint32_t mode);
void SetChromakeyValue(SharedFrontendState& st, uint32_t value);
void SetChromakeyRange(SharedFrontendState& st, uint32_t minColor,
                       uint32_t maxColor, uint32_t mode);
void SetTexChromakeyMode(SharedFrontendState& st, uint32_t tmu, uint32_t mode);
void SetTexChromakeyRange(SharedFrontendState& st, uint32_t tmu,
                          uint32_t minColor, uint32_t maxColor, uint32_t mode);
void SetFogMode(SharedFrontendState& st, uint32_t unifiedMode);
void SetFogColor(SharedFrontendState& st, uint32_t fogColor);
void SetFogTable(SharedFrontendState& st, const uint8_t* ft);
void DisableAllEffects(SharedFrontendState& st, bool clearCachedFogMode);
void LoadGammaTable(uint32_t nentries, uint32_t* red, uint32_t* green,
                    uint32_t* blue);

// Vertex sanitization, primitive batch dispatch, and telemetry helpers.
void SanitizeVertex(const SharedFrontendState& st, ModernVertex& mv);
void DrawVertexList(uint32_t mode, uint32_t count, const ModernVertex* vertices,
                    bool allowPointsAndLines);
void GetTriStats(uint32_t* processed, uint32_t* drawn);
void ResetTriStats();
void GetPerfStats(uint32_t& outPixelsIn, uint32_t& outChromaFail);
void ResetPerfStats();

// LFB helpers.
bool LfbReadRegion(uint32_t srcBuf, uint32_t srcX, uint32_t srcY,
                   uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride,
                   void* dstData);
bool LfbWriteRegion(uint32_t dstBuf, uint32_t dstX, uint32_t dstY,
                    uint32_t srcFmt, uint32_t srcWidth, uint32_t srcHeight,
                    int32_t srcStride, const void* srcData);
bool LfbUnlockWithState(const SharedFrontendState& st, uint32_t type,
                        uint32_t buffer);

// String & 3df texture file helpers.
const char* GetRendererString();
int32_t Map3dfAspectToGlide2(int aspectW, int aspectH);
int32_t Map3dfLodToGlide2(int dim);
int32_t Map3dfAspectToGlide3(int aspectW, int aspectH);
int32_t Map3dfLodToGlide3(int dim);
bool Load3dfPixelsAndPalette(const char* filename, uint32_t format,
                             uint32_t* paletteOut256, void* pixelDataOut);

// Splash / ShamelessPlug / DOSBox handshake helpers.
void RenderShamelessPlug();
void RenderSplash(float x, float y, float w, float h, uint32_t frame,
                  void (*callback)(int frame));
void HandleDosboxSetConfig(const char* apiTag, uint32_t flags, void* magic);
void HandleDosboxSetConfigRes(const char* apiTag, int res, void (*swap12)());

}  // namespace EntryCommon
}  // namespace GlideWrapper
