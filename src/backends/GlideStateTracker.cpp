#include "GlideStateTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Logger.h"

namespace GlideWrapper {

GlideStateTracker::GlideStateTracker() {
  for (int i = 0; i < 256; ++i) {
    m_lutR[i] = static_cast<uint8_t>(i);
    m_lutG[i] = static_cast<uint8_t>(i);
    m_lutB[i] = static_cast<uint8_t>(i);
  }
}

void GlideStateTracker::ResetState() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetState_Locked();
}

void GlideStateTracker::ResetState_Locked() {
  MarkStateDirty();
  m_depthMode = 0;
  m_depthCompareOp = 1;
  m_depthMask = true;
  m_depthBiasLevel = 0;
  m_depthNear = 1.0f;
  m_depthFar = 65535.0f;
  m_ditherMode = 0;
  m_stippleMode = 0;
  m_stipplePattern = 0xFFFFFFFF;

  m_rgbSrcBlend = 4;
  m_rgbDstBlend = 0;
  m_alphaSrcBlend = 4;
  m_alphaDstBlend = 0;

  m_alphaTestOp = 7;
  m_alphaTestRefVal = 0;

  m_cullMode = 0;

  m_clipMinX = 0;
  m_clipMinY = 0;
  m_clipMaxX = m_headlessWidth;
  m_clipMaxY = m_headlessHeight;
  m_sstOrigin = 0;

  m_fogMode = 0;
  m_fogColor = 0xff000000;
  std::memset(m_fogTable, 0, sizeof(m_fogTable));

  m_colorMaskRgb = true;
  m_colorMaskAlpha = true;

  m_texLodBias[0] = m_texLodBias[1] = 0.0f;
  m_boundTexAddress[0] = 0xFFFFFFFF;
  m_boundTexAddress[1] = 0xFFFFFFFF;
  m_texClampS[0] = 0;
  m_texClampS[1] = 0;
  m_texClampT[0] = 0;
  m_texClampT[1] = 0;
  m_texMinFilter[0] = 0;
  m_texMinFilter[1] = 0;
  m_texMagFilter[0] = 0;
  m_texMagFilter[1] = 0;
  m_texMipMapMode[0] = m_texMipMapMode[1] = 0;
  m_texLodBlend[0] = m_texLodBlend[1] = false;

  m_colorCombinerFunc = 0;
  m_colorCombinerFactor = 0;
  m_colorCombinerLocal = 0;
  m_colorCombinerOther = 0;
  m_colorCombinerInvert = false;
  m_alphaCombinerFunc = 0;
  m_alphaCombinerFactor = 0;
  m_alphaCombinerLocal = 0;
  m_alphaCombinerOther = 0;
  m_alphaCombinerInvert = false;

  m_texCombinerRgbFunc[0] = m_texCombinerRgbFunc[1] = 0;
  m_texCombinerRgbFactor[0] = m_texCombinerRgbFactor[1] = 0;
  m_texCombinerAlphaFunc[0] = m_texCombinerAlphaFunc[1] = 0;
  m_texCombinerAlphaFactor[0] = m_texCombinerAlphaFactor[1] = 0;
  m_texCombinerRgbInvert[0] = m_texCombinerRgbInvert[1] = false;
  m_texCombinerAlphaInvert[0] = m_texCombinerAlphaInvert[1] = false;

  m_constantColor = 0xFFFFFFFF;
  m_stwHintMask = 0;
  m_pixelFormatOverride = 1;
  m_chromakeyMode = 0;
  m_chromakeyValue = 0;
  m_chromakeyRangeMin = 0;
  m_chromakeyRangeMax = 0;
  m_chromakeyRangeMode = 0;
  m_texChromaMode[0] = m_texChromaMode[1] = 0;
  m_texChromaMin[0] = m_texChromaMin[1] = 0;
  m_texChromaMax[0] = m_texChromaMax[1] = 0;
  m_texChromaRangeMode[0] = m_texChromaRangeMode[1] = 0;

  m_useGammaLut = false;
  for (int i = 0; i < 256; ++i) {
    m_lutR[i] = static_cast<uint8_t>(i);
    m_lutG[i] = static_cast<uint8_t>(i);
    m_lutB[i] = static_cast<uint8_t>(i);
  }
  m_aaEnabled = false;
}

void GlideStateTracker::PollEvents() {
  // Default no-op to prevent stealing host event queues.
}

void GlideStateTracker::SstIdle() {
  // Default no-op for synchronous backends; GPU backends override to flush/wait.
}

void GlideStateTracker::SetConstantColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetConstantColor_Locked(color);
}

void GlideStateTracker::SetConstantColor_Locked(uint32_t color) {
  MarkStateDirty();
  m_constantColor = color;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetConstantColor: 0x" << std::hex << color << std::dec);
}

void GlideStateTracker::SetPixelFormat(uint32_t format) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetPixelFormat_Locked(format);
}

void GlideStateTracker::SetPixelFormat_Locked(uint32_t format) {
  MarkStateDirty();
  m_pixelFormatOverride = format;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetPixelFormat active override format: " << format);
}

void GlideStateTracker::SetRenderBuffer(uint32_t target) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetRenderBuffer_Locked(target);
}

void GlideStateTracker::SetRenderBuffer_Locked(uint32_t target) {
  MarkStateDirty();
  m_activeRenderBuffer = target;
  GLIDE_LOG(DEBUG, "StateTracker", "SetRenderBuffer: target=" << target);
}

void GlideStateTracker::SetDepthState(uint32_t depthMode, uint32_t compareOp,
                                      bool depthMask, int32_t biasLevel) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetDepthState_Locked(depthMode, compareOp, depthMask, biasLevel);
}

void GlideStateTracker::SetDepthState_Locked(uint32_t depthMode, uint32_t compareOp,
                                      bool depthMask, int32_t biasLevel) {
  MarkStateDirty();
  m_depthMode = depthMode;
  m_depthCompareOp = compareOp;
  m_depthMask = depthMask;
  m_depthBiasLevel = biasLevel;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetDepthState: Mode=" << depthMode << ", Compare=" << compareOp
                                   << ", Mask=" << depthMask
                                   << ", BiasLevel=" << biasLevel);
}

void GlideStateTracker::SetDepthRange(float nearVal, float farVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetDepthRange_Locked(nearVal, farVal);
}

void GlideStateTracker::SetDepthRange_Locked(float nearVal, float farVal) {
  MarkStateDirty();
  m_depthNear = nearVal;
  m_depthFar = farVal;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetDepthRange: Near=" << nearVal << ", Far=" << farVal);
}

void GlideStateTracker::SetDitherMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetDitherMode_Locked(mode);
}

void GlideStateTracker::SetDitherMode_Locked(uint32_t mode) {
  MarkStateDirty();
  m_ditherMode = mode;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetDitherMode: Mode=" << mode);
}

void GlideStateTracker::SetStippleState(uint32_t mode, uint32_t pattern) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetStippleState_Locked(mode, pattern);
}

void GlideStateTracker::SetStippleState_Locked(uint32_t mode, uint32_t pattern) {
  MarkStateDirty();
  m_stippleMode = mode;
  m_stipplePattern = pattern;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetStippleState: Mode=" << mode << ", Pattern=0x" << std::hex
                                     << pattern << std::dec);
}

void GlideStateTracker::SetBlendState(uint32_t rgbSrcFactor,
                                      uint32_t rgbDstFactor,
                                      uint32_t alphaSrcFactor,
                                      uint32_t alphaDstFactor) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetBlendState_Locked(rgbSrcFactor, rgbDstFactor, alphaSrcFactor, alphaDstFactor);
}

void GlideStateTracker::SetBlendState_Locked(uint32_t rgbSrcFactor,
                                      uint32_t rgbDstFactor,
                                      uint32_t alphaSrcFactor,
                                      uint32_t alphaDstFactor) {
  MarkStateDirty();
  m_rgbSrcBlend = rgbSrcFactor;
  m_rgbDstBlend = rgbDstFactor;
  m_alphaSrcBlend = alphaSrcFactor;
  m_alphaDstBlend = alphaDstFactor;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetBlendState: Src=" << rgbSrcFactor << ", Dst=" << rgbDstFactor);
}

void GlideStateTracker::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetAlphaTestState_Locked(compareOp, refVal);
}

void GlideStateTracker::SetAlphaTestState_Locked(uint32_t compareOp, uint32_t refVal) {
  MarkStateDirty();
  m_alphaTestOp = compareOp;
  m_alphaTestRefVal = refVal;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetAlphaTestState: Op=" << compareOp << ", Ref=" << refVal);
}

void GlideStateTracker::SetCullState(uint32_t cullMode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetCullState_Locked(cullMode);
}

void GlideStateTracker::SetCullState_Locked(uint32_t cullMode) {
  MarkStateDirty();
  m_cullMode = cullMode;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetCullState: Mode=" << cullMode);
}

void GlideStateTracker::SetClipWindow(uint32_t minX, uint32_t minY,
                                      uint32_t maxX, uint32_t maxY) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetClipWindow_Locked(minX, minY, maxX, maxY);
}

void GlideStateTracker::SetClipWindow_Locked(uint32_t minX, uint32_t minY,
                                      uint32_t maxX, uint32_t maxY) {
  MarkStateDirty();

  m_clipMinX = minX;
  m_clipMinY = minY;
  m_clipMaxX = std::min(m_headlessWidth, maxX);
  m_clipMaxY = std::min(m_headlessHeight, maxY);

  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetClipWindow: (" << m_clipMinX << "," << m_clipMinY << ") to ("
                               << m_clipMaxX << "," << m_clipMaxY << ")");
}

void GlideStateTracker::SetSstOrigin(uint32_t origin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetSstOrigin_Locked(origin);
}

void GlideStateTracker::SetSstOrigin_Locked(uint32_t origin) {
  MarkStateDirty();
  m_sstOrigin = origin;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetSstOrigin: " << origin);
}

void GlideStateTracker::SetColorMask(bool rgb, bool alpha) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetColorMask_Locked(rgb, alpha);
}

void GlideStateTracker::SetColorMask_Locked(bool rgb, bool alpha) {
  MarkStateDirty();
  m_colorMaskRgb = rgb;
  m_colorMaskAlpha = alpha;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetColorMask: RGB=" << rgb << ", Alpha=" << alpha);
}

void GlideStateTracker::SetGamma(float gamma) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetGamma_Locked(gamma);
}

void GlideStateTracker::SetGamma_Locked(float gamma) {
  MarkStateDirty();
  GLIDE_LOG(INFO, "StateTracker", "SetGamma: value=" << gamma);
  if (gamma <= 0.0f) gamma = 1.0f;
  for (int i = 0; i < 256; i++) {
    m_lutR[i] = static_cast<uint8_t>(
        std::pow(i / 255.0f, 1.0f / gamma) * 255.0f + 0.5f);
    m_lutG[i] = m_lutR[i];
    m_lutB[i] = m_lutR[i];
  }
  m_useGammaLut = (gamma != 1.0f);
}

void GlideStateTracker::LoadGammaTable(uint32_t nentries,
                                       const uint32_t* rTable,
                                       const uint32_t* gTable,
                                       const uint32_t* bTable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  LoadGammaTable_Locked(nentries, rTable, gTable, bTable);
}

void GlideStateTracker::LoadGammaTable_Locked(uint32_t nentries,
                                       const uint32_t* rTable,
                                       const uint32_t* gTable,
                                       const uint32_t* bTable) {
  MarkStateDirty();
  GLIDE_LOG(INFO, "StateTracker", "LoadGammaTable: nentries=" << nentries);
  if (nentries != 256 || !rTable || !gTable || !bTable) return;
  for (int i = 0; i < 256; i++) {
    m_lutR[i] = static_cast<uint8_t>(rTable[i]);
    m_lutG[i] = static_cast<uint8_t>(gTable[i]);
    m_lutB[i] = static_cast<uint8_t>(bTable[i]);
  }
  m_useGammaLut = true;
}

void GlideStateTracker::SetFogMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetFogMode_Locked(mode);
}

void GlideStateTracker::SetFogMode_Locked(uint32_t mode) {
  MarkStateDirty();
  m_fogMode = mode;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetFogMode: " << mode);
}

void GlideStateTracker::SetFogColor(uint32_t color) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetFogColor_Locked(color);
}

void GlideStateTracker::SetFogColor_Locked(uint32_t color) {
  MarkStateDirty();
  m_fogColor = color;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetFogColor: 0x" << std::hex << color << std::dec);
}

void GlideStateTracker::SetFogTable(const uint8_t* table) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetFogTable_Locked(table);
}

void GlideStateTracker::SetFogTable_Locked(const uint8_t* table) {
  MarkStateDirty();
  if (table) {
    std::memcpy(m_fogTable, table, sizeof(m_fogTable));
    GLIDE_LOG(DEBUG, "Rasterizer", "SetFogTable: 64 entries uploaded.");
  }
}

void GlideStateTracker::BindTexture(uint32_t tmu, uint32_t startAddress,
                                    uint32_t clampS, uint32_t clampT,
                                    uint32_t minFilter, uint32_t magFilter) {
  std::lock_guard<std::mutex> lock(m_mutex);
  BindTexture_Locked(tmu, startAddress, clampS, clampT, minFilter, magFilter);
}

void GlideStateTracker::BindTexture_Locked(uint32_t tmu, uint32_t startAddress,
                                    uint32_t clampS, uint32_t clampT,
                                    uint32_t minFilter, uint32_t magFilter) {
  MarkStateDirty();
  if (tmu < 2) {
    m_boundTexAddress[tmu] = startAddress;
    m_texClampS[tmu] = clampS;
    m_texClampT[tmu] = clampT;
    m_texMinFilter[tmu] = minFilter;
    m_texMagFilter[tmu] = magFilter;
    GLIDE_LOG(DEBUG, "Rasterizer",
              "BindTexture TMU" << tmu << ": Address=0x" << std::hex
                                << startAddress << std::dec << ", Clamp("
                                << clampS << "," << clampT << "), Filter("
                                << minFilter << "," << magFilter << ")");
  }
}

void GlideStateTracker::UploadTexture(uint32_t tmu, uint32_t startAddress,
                                      const struct VirtualTexture& /*tex*/) {
  GLIDE_LOG(DEBUG, "Rasterizer",
            "UploadTexture (Software) TMU" << tmu << ": Address=0x" << std::hex
                                           << startAddress << std::dec
                                           << " registered.");
}

void GlideStateTracker::SetTexLodBias(uint32_t tmu, float bias) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetTexLodBias_Locked(tmu, bias);
}

void GlideStateTracker::SetTexLodBias_Locked(uint32_t tmu, float bias) {
  MarkStateDirty();
  if (tmu < 2) {
    m_texLodBias[tmu] = bias;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetTexLodBias TMU" << tmu << ": " << bias);
  }
}

void GlideStateTracker::SetTexMipMapMode(uint32_t tmu, uint32_t mode,
                                         bool lodBlend) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetTexMipMapMode_Locked(tmu, mode, lodBlend);
}

void GlideStateTracker::SetTexMipMapMode_Locked(uint32_t tmu, uint32_t mode,
                                         bool lodBlend) {
  MarkStateDirty();
  if (tmu < 2) {
    m_texMipMapMode[tmu] = mode;
    m_texLodBlend[tmu] = lodBlend;
    GLIDE_LOG(DEBUG, "Rasterizer",
              "SetTexMipMapMode TMU"
                  << tmu << ": Mode=" << mode
                  << ", LodBlend=" << (lodBlend ? "Yes" : "No"));
  }
}

void GlideStateTracker::UploadTexturePartial(
    uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& /*tex*/,
    uint32_t lodLevel, uint32_t startRow, uint32_t endRow) {
  GLIDE_LOG(DEBUG, "Rasterizer",
            "UploadTexturePartial (Software) TMU"
                << tmu << ": Address=0x" << std::hex << startAddress << std::dec
                << " level=" << lodLevel << " rows=" << startRow << ".."
                << endRow << " registered.");
}

void GlideStateTracker::PurgeTextures() {
  GLIDE_LOG(DEBUG, "Rasterizer", "PurgeTextures (Software) invoked.");
}

void GlideStateTracker::SetCombinerMode(
    uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal,
    uint32_t colorOther, bool colorInvert, uint32_t alphaFunc,
    uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther,
    bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetCombinerMode_Locked(colorFunc, colorFactor, colorLocal, colorOther, colorInvert, alphaFunc, alphaFactor, alphaLocal, alphaOther, alphaInvert);
}

void GlideStateTracker::SetCombinerMode_Locked(
    uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal,
    uint32_t colorOther, bool colorInvert, uint32_t alphaFunc,
    uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther,
    bool alphaInvert) {
  MarkStateDirty();
  m_colorCombinerFunc = colorFunc;
  m_colorCombinerFactor = colorFactor;
  m_colorCombinerLocal = colorLocal;
  m_colorCombinerOther = colorOther;
  m_colorCombinerInvert = colorInvert;
  m_alphaCombinerFunc = alphaFunc;
  m_alphaCombinerFactor = alphaFactor;
  m_alphaCombinerLocal = alphaLocal;
  m_alphaCombinerOther = alphaOther;
  m_alphaCombinerInvert = alphaInvert;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetCombinerMode: Color(Func="
                << colorFunc << ", Fac=" << colorFactor
                << ", Loc=" << colorLocal << ", Oth=" << colorOther
                << ", Inv=" << colorInvert << "), Alpha(Func=" << alphaFunc
                << ", Fac=" << alphaFactor << ", Loc=" << alphaLocal
                << ", Oth=" << alphaOther << ", Inv=" << alphaInvert << ")");
}

void GlideStateTracker::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc,
                                           uint32_t rgbFactor,
                                           uint32_t alphaFunc,
                                           uint32_t alphaFactor, bool rgbInvert,
                                           bool alphaInvert) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetTexCombinerMode_Locked(tmu, rgbFunc, rgbFactor, alphaFunc, alphaFactor, rgbInvert, alphaInvert);
}

void GlideStateTracker::SetTexCombinerMode_Locked(uint32_t tmu, uint32_t rgbFunc,
                                           uint32_t rgbFactor,
                                           uint32_t alphaFunc,
                                           uint32_t alphaFactor, bool rgbInvert,
                                           bool alphaInvert) {
  MarkStateDirty();
  if (tmu < 2) {
    m_texCombinerRgbFunc[tmu] = rgbFunc;
    m_texCombinerRgbFactor[tmu] = rgbFactor;
    m_texCombinerAlphaFunc[tmu] = alphaFunc;
    m_texCombinerAlphaFactor[tmu] = alphaFactor;
    m_texCombinerRgbInvert[tmu] = rgbInvert;
    m_texCombinerAlphaInvert[tmu] = alphaInvert;
    GLIDE_LOG(DEBUG, "Rasterizer",
              "SetTexCombinerMode TMU" << tmu << ": RGBFunc=" << rgbFunc
                                       << ", AlphaFunc=" << alphaFunc
                                       << ", RGBInvert=" << rgbInvert
                                       << ", AlphaInvert=" << alphaInvert);
  }
}

void GlideStateTracker::SetSTWHintState(uint32_t hintMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetSTWHintState_Locked(hintMask);
}

void GlideStateTracker::SetSTWHintState_Locked(uint32_t hintMask) {
  MarkStateDirty();
  m_stwHintMask = hintMask;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetSTWHintState: Mask=" << hintMask);
}

void GlideStateTracker::SetChromakeyMode(uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetChromakeyMode_Locked(mode);
}

void GlideStateTracker::SetChromakeyMode_Locked(uint32_t mode) {
  MarkStateDirty();
  m_chromakeyMode = mode;
  GLIDE_LOG(DEBUG, "Rasterizer", "SetChromakeyMode: Mode=" << mode);
}

void GlideStateTracker::SetChromakeyValue(uint32_t value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetChromakeyValue_Locked(value);
}

void GlideStateTracker::SetChromakeyValue_Locked(uint32_t value) {
  MarkStateDirty();
  m_chromakeyValue = value;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetChromakeyValue: Value=0x" << std::hex << value << std::dec);
}

void GlideStateTracker::SetChromakeyRange(uint32_t minColor, uint32_t maxColor,
                                          uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetChromakeyRange_Locked(minColor, maxColor, mode);
}

void GlideStateTracker::SetChromakeyRange_Locked(uint32_t minColor, uint32_t maxColor,
                                          uint32_t mode) {
  MarkStateDirty();
  m_chromakeyRangeMin = minColor;
  m_chromakeyRangeMax = maxColor;
  m_chromakeyRangeMode = mode;
  GLIDE_LOG(DEBUG, "Rasterizer",
            "SetChromakeyRange: min=0x" << std::hex << minColor << ", max=0x"
                                        << maxColor << ", mode=" << std::dec
                                        << mode);
}

void GlideStateTracker::SetTexChromakeyMode(uint32_t tmu, uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetTexChromakeyMode_Locked(tmu, mode);
}

void GlideStateTracker::SetTexChromakeyMode_Locked(uint32_t tmu, uint32_t mode) {
  MarkStateDirty();
  if (tmu < 2) {
    m_texChromaMode[tmu] = mode;
    GLIDE_LOG(DEBUG, "Rasterizer",
              "SetTexChromakeyMode TMU" << tmu << ": Mode=" << mode);
  }
}

void GlideStateTracker::SetTexChromakeyRange(uint32_t tmu, uint32_t minColor,
                                             uint32_t maxColor, uint32_t mode) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetTexChromakeyRange_Locked(tmu, minColor, maxColor, mode);
}

void GlideStateTracker::SetTexChromakeyRange_Locked(uint32_t tmu, uint32_t minColor,
                                             uint32_t maxColor, uint32_t mode) {
  MarkStateDirty();
  if (tmu < 2) {
    m_texChromaMin[tmu] = minColor;
    m_texChromaMax[tmu] = maxColor;
    m_texChromaRangeMode[tmu] = mode;
    GLIDE_LOG(DEBUG, "Rasterizer",
              "SetTexChromakeyRange TMU" << tmu << ": min=0x" << std::hex
                                         << minColor << ", max=0x" << maxColor
                                         << ", mode=" << std::dec << mode);
  }
}

void GlideStateTracker::SetAAState(bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SetAAState_Locked(enabled);
}

void GlideStateTracker::SetAAState_Locked(bool enabled) {
  MarkStateDirty();
  m_aaEnabled = enabled;
  GLIDE_LOG(INFO, "StateTracker",
            "SetAAState: AA active=" << (enabled ? "Yes" : "No"));
}

}  // namespace GlideWrapper
