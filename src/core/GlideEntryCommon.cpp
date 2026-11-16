#include "core/GlideEntryCommon.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/3dfParser.h"
#include "core/BackendManager.h"
#include "core/GlideSplashAnimator.h"
#include "core/JsonConfigLoader.h"
#include "core/LfbManager.h"
#include "core/Logger.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/WrapperConfig.h"

namespace GlideWrapper {
namespace EntryCommon {

namespace {
// Glide 2.x/3.x shared primitive type enums (identical in both headers).
constexpr uint32_t kGrPoints = 0;
constexpr uint32_t kGrLineStrip = 1;
constexpr uint32_t kGrLines = 2;
constexpr uint32_t kGrPolygon = 3;
constexpr uint32_t kGrTriangleStrip = 4;
constexpr uint32_t kGrTriangleFan = 5;
constexpr uint32_t kGrTriangles = 6;

// Glide 2.x/3.x shared paletted texture format IDs.
constexpr uint32_t kGrTexFmtP8 = 0x5;
constexpr uint32_t kGrTexFmtP8_6666 = 0x6;
}  // namespace

void SharedFrontendState::Reset(uint8_t defaultLfbConstantAlpha,
                                uint32_t defaultTexCombineFunc,
                                uint32_t clipWidth, uint32_t clipHeight) {
  depthMode = 0;
  depthCompare = 1;
  depthMask = true;
  depthBias = 0;

  alphaTestOp = 7;
  alphaTestRef = 0;

  clampS[0] = clampS[1] = 0;
  clampT[0] = clampT[1] = 0;
  minFilter[0] = minFilter[1] = 0;
  magFilter[0] = magFilter[1] = 0;

  colorFunc = 0x3;
  colorFactor = 0x8;
  colorLocal = 0x0;
  colorOther = 0x0;
  colorInvert = false;

  alphaFunc = 0x3;
  alphaFactor = 0x8;
  alphaLocal = 0x1;
  alphaOther = 0x2;
  alphaInvert = false;

  texRgbFunc[0] = texRgbFunc[1] = defaultTexCombineFunc;
  texRgbFactor[0] = texRgbFactor[1] = 0;
  texAlphaFunc[0] = texAlphaFunc[1] = defaultTexCombineFunc;
  texAlphaFactor[0] = texAlphaFactor[1] = 0;
  texRgbInvert[0] = texRgbInvert[1] = false;
  texAlphaInvert[0] = texAlphaInvert[1] = false;
  boundTex[0] = boundTex[1] = 0xFFFFFFFFu;

  texChromaState[0] = {0, 0, 0};
  texChromaState[1] = {0, 0, 0};
  texLodBias[0] = texLodBias[1] = 0.0f;
  mipmapMode[0] = mipmapMode[1] = 0;
  lodBlend[0] = lodBlend[1] = false;

  constantColor = 0xFFFFFFFFu;
  rgbSrcBlend = 4;
  rgbDstBlend = 0;
  alphaSrcBlend = 4;
  alphaDstBlend = 0;

  cullMode = 0;
  clipMinX = 0;
  clipMinY = 0;
  clipMaxX = clipWidth;
  clipMaxY = clipHeight;

  fogMode = 0;
  fogColor = 0xFF000000u;
  std::memset(fogTable, 0, sizeof(fogTable));

  colorMaskRgb = true;
  colorMaskAlpha = true;
  ditherMode = 0;

  lfbConstantAlpha = defaultLfbConstantAlpha;
  lfbWriteColorFormat = 0;

  chromakeyMode = 0;
  chromakeyValue = 0;
  chromakeyRangeMin = 0;
  chromakeyRangeMax = 0;
  chromakeyRangeMode = 0;
}

ResolutionDims ResolveResolution(uint32_t resolutionEnum) {
  switch (resolutionEnum) {
    case 0x0:   return {320, 200};    // GR_RESOLUTION_320x200
    case 0x1:   return {320, 240};    // GR_RESOLUTION_320x240
    case 0x2:   return {400, 256};    // GR_RESOLUTION_400x256
    case 0x3:   return {512, 384};    // GR_RESOLUTION_512x384
    case 0x4:   return {640, 200};    // GR_RESOLUTION_640x200
    case 0x5:   return {640, 350};    // GR_RESOLUTION_640x350
    case 0x6:   return {640, 400};    // GR_RESOLUTION_640x400
    case 0x7:   return {640, 480};    // GR_RESOLUTION_640x480
    case 0x8:   return {800, 600};    // GR_RESOLUTION_800x600
    case 0x9:   return {960, 720};    // GR_RESOLUTION_960x720
    case 0xa:   return {856, 480};    // GR_RESOLUTION_856x480
    case 0xb:   return {512, 256};    // GR_RESOLUTION_512x256
    case 0xc:   return {1024, 768};   // GR_RESOLUTION_1024x768
    case 0xd:   return {1280, 1024};  // GR_RESOLUTION_1280x1024
    case 0xe:   return {1600, 1200};  // GR_RESOLUTION_1600x1200
    case 0xf:   return {400, 300};    // GR_RESOLUTION_400x300
    case 0x10:  return {1152, 864};   // GR_RESOLUTION_1152x864 (Glide 3.x)
    case 0x11:  return {1280, 960};   // GR_RESOLUTION_1280x960 (Glide 3.x)
    case 0x12:  return {1600, 1024};  // GR_RESOLUTION_1600x1024 (Glide 3.x)
    case 0x13:  return {1792, 1344};  // GR_RESOLUTION_1792x1344 (Glide 3.x)
    case 0x14:  return {1856, 1392};  // GR_RESOLUTION_1856x1392 (Glide 3.x)
    case 0x15:  return {1920, 1440};  // GR_RESOLUTION_1920x1440 (Glide 3.x)
    case 0x16:  return {2048, 1536};  // GR_RESOLUTION_2048x1536 (Glide 3.x)
    case 0x17:  return {2048, 2048};  // GR_RESOLUTION_2048x2048 (Glide 3.x)
    default:    return {640, 480};
  }
}

void ConfigureTmuMemoryLimits() {
  auto& reg = EmulationRegistry::GetInstance();
  for (uint32_t tmu = 0; tmu < reg.GetConfig().tmuCount; ++tmu) {
    TextureManager::GetInstance().SetTmuMemoryLimitMb(
        tmu, reg.GetConfig().tmuMemoryMb);
  }
}

void InitGlide(const char* apiTag, void (*resetFrontendState)()) {
  if (resetFrontendState) resetFrontendState();
  auto& reg = EmulationRegistry::GetInstance();
  JsonConfigLoader loader;
  loader.Load("glide_config.json", reg.GetConfig());
  reg.GetConfig().activeFrontendApi = apiTag ? apiTag : "";

  const auto& cfg = reg.GetConfig();
  auto& logger = Logger::GetInstance();
  logger.Initialize(cfg);
  TelemetryManager::GetInstance().ConfigurePerfLog(cfg.perfLogEnabled,
                                                   cfg.perfLogCsvPath);
  GLIDE_LOG(INFO, "Frontend", "grGlideInit (" << apiTag << ") invoked.");

  logger.LogExecutionSummary(cfg);
  BackendManager::GetInstance().EstablishBackend(cfg);
  ConfigureTmuMemoryLimits();
}

void EnsureEngineInitializedSlow(void (*resetFrontendState)()) {
  auto& reg = EmulationRegistry::GetInstance();
  auto& bm = BackendManager::GetInstance();
  if (!bm.GetBackend()) {
    if (resetFrontendState) resetFrontendState();
    JsonConfigLoader loader;
    loader.Load("glide_config.json", reg.GetConfig());

    const auto& cfg = reg.GetConfig();
    auto& logger = Logger::GetInstance();
    logger.Initialize(cfg);
    TelemetryManager::GetInstance().ConfigurePerfLog(cfg.perfLogEnabled,
                                                     cfg.perfLogCsvPath);
    logger.LogExecutionSummary(cfg);
    bm.EstablishBackend(cfg);
    ConfigureTmuMemoryLimits();
    GLIDE_LOG(INFO, "Frontend", "Implicit grGlideInit executed seamlessly.");
  }
}

void ShutdownGlide(const char* apiTag) {
  GLIDE_LOG(INFO, "Frontend", "grGlideShutdown (" << apiTag << ") invoked.");
  LfbManager::GetInstance().Reset();
  TelemetryManager::GetInstance().ShutdownPerfLog();
  TelemetryManager::GetInstance().PrintReport();
  BackendManager::GetInstance().ShutdownBackend();
  Logger::GetInstance().Flush();
}

void CloseWindow(const char* logMessage) {
  LfbManager::GetInstance().Reset();
  BackendManager::GetInstance().SetSplashAnimator(nullptr);
  GLIDE_LOG(INFO, "Frontend", logMessage);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->DetachWindow();
  }
}

void ClearBuffer(uint32_t color, uint8_t alpha, uint32_t depth) {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->ClearBuffer(color, alpha, static_cast<float>(depth) / 65535.0f,
                         0x3);
  }
}

void SwapBuffers() {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SwapBuffers();
  }
}

void SetRenderBuffer(uint32_t buffer) {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetRenderBuffer(buffer);
  }
}

void SstIdle() {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->PollEvents();
    backend->SstIdle();
  }
}

void ApplySharedStateToBackend(const SharedFrontendState& st,
                               uint32_t sstOrigin, bool includeTexInvert) {
  auto* b = BackendManager::GetInstance().GetBackend();
  if (!b) return;

  b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
  b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
  b->SetCombinerMode(st.colorFunc, st.colorFactor, st.colorLocal, st.colorOther,
                     st.colorInvert, st.alphaFunc, st.alphaFactor,
                     st.alphaLocal, st.alphaOther, st.alphaInvert);
  b->SetBlendState(st.rgbSrcBlend, st.rgbDstBlend, st.alphaSrcBlend,
                   st.alphaDstBlend);
  b->SetCullState(st.cullMode);
  b->SetSstOrigin(sstOrigin);
  b->SetClipWindow(st.clipMinX, st.clipMinY, st.clipMaxX, st.clipMaxY);
  b->SetColorMask(st.colorMaskRgb, st.colorMaskAlpha);
  b->SetDitherMode(st.ditherMode);
  b->SetConstantColor(st.constantColor);
  b->SetFogMode(st.fogMode);
  b->SetFogColor(st.fogColor);
  b->SetFogTable(st.fogTable);
  b->SetChromakeyMode(st.chromakeyMode);
  b->SetChromakeyValue(st.chromakeyValue);
  b->SetChromakeyRange(st.chromakeyRangeMin, st.chromakeyRangeMax,
                       st.chromakeyRangeMode);

  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    b->SetTexCombinerMode(tmu, st.texRgbFunc[tmu], st.texRgbFactor[tmu],
                          st.texAlphaFunc[tmu], st.texAlphaFactor[tmu],
                          includeTexInvert ? st.texRgbInvert[tmu] : false,
                          includeTexInvert ? st.texAlphaInvert[tmu] : false);
    b->BindTexture(tmu, st.boundTex[tmu], st.clampS[tmu], st.clampT[tmu],
                   st.minFilter[tmu], st.magFilter[tmu]);
    b->SetTexChromakeyMode(tmu, st.texChromaState[tmu].mode);
    b->SetTexChromakeyRange(tmu, st.texChromaState[tmu].minColor,
                            st.texChromaState[tmu].maxColor,
                            st.texChromaState[tmu].mode);
    b->SetTexMipMapMode(tmu, st.mipmapMode[tmu], st.lodBlend[tmu]);
    b->SetTexLodBias(tmu, st.texLodBias[tmu]);
  }
}

void SetClipWindow(SharedFrontendState& st, uint32_t minx, uint32_t miny,
                   uint32_t maxx, uint32_t maxy) {
  st.clipMinX = minx;
  st.clipMinY = miny;
  st.clipMaxX = maxx;
  st.clipMaxY = maxy;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetClipWindow(minx, miny, maxx, maxy);
  }
}

void SetColorCombine(SharedFrontendState& st, uint32_t fnc, uint32_t factor,
                     uint32_t local, uint32_t other, bool invert) {
  st.colorFunc = fnc;
  st.colorFactor = factor;
  st.colorLocal = local;
  st.colorOther = other;
  st.colorInvert = invert;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetCombinerMode(st.colorFunc, st.colorFactor, st.colorLocal,
                       st.colorOther, st.colorInvert, st.alphaFunc,
                       st.alphaFactor, st.alphaLocal, st.alphaOther,
                       st.alphaInvert);
  }
}

void SetAlphaCombine(SharedFrontendState& st, uint32_t fnc, uint32_t factor,
                     uint32_t local, uint32_t other, bool invert) {
  st.alphaFunc = fnc;
  st.alphaFactor = factor;
  st.alphaLocal = local;
  st.alphaOther = other;
  st.alphaInvert = invert;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetCombinerMode(st.colorFunc, st.colorFactor, st.colorLocal,
                       st.colorOther, st.colorInvert, st.alphaFunc,
                       st.alphaFactor, st.alphaLocal, st.alphaOther,
                       st.alphaInvert);
  }
}

void SetTexCombine(SharedFrontendState& st, uint32_t tmu, uint32_t rgbFnc,
                   uint32_t rgbFactor, uint32_t alphaFnc, uint32_t alphaFactor,
                   bool rgbInvert, bool alphaInvert, bool forwardInverts) {
  if (tmu >= 2) return;
  st.texRgbFunc[tmu] = rgbFnc;
  st.texRgbFactor[tmu] = rgbFactor;
  st.texAlphaFunc[tmu] = alphaFnc;
  st.texAlphaFactor[tmu] = alphaFactor;
  if (forwardInverts) {
    st.texRgbInvert[tmu] = rgbInvert;
    st.texAlphaInvert[tmu] = alphaInvert;
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexCombinerMode(tmu, rgbFnc, rgbFactor, alphaFnc, alphaFactor,
                          forwardInverts ? rgbInvert : false,
                          forwardInverts ? alphaInvert : false);
  }
}

void SetAlphaBlendFunction(SharedFrontendState& st, uint32_t rgbSf,
                           uint32_t rgbDf, uint32_t alphaSf, uint32_t alphaDf) {
  st.rgbSrcBlend = rgbSf;
  st.rgbDstBlend = rgbDf;
  st.alphaSrcBlend = alphaSf;
  st.alphaDstBlend = alphaDf;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetBlendState(rgbSf, rgbDf, alphaSf, alphaDf);
  }
}

void SetAlphaTestFunction(SharedFrontendState& st, uint32_t fnc) {
  st.alphaTestOp = fnc;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
  }
}

void SetAlphaTestReferenceValue(SharedFrontendState& st, uint32_t refVal) {
  st.alphaTestRef = refVal;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
  }
}

void SetTexFilterMode(SharedFrontendState& st, uint32_t tmu, uint32_t minFilter,
                      uint32_t magFilter) {
  if (tmu >= 2) return;
  st.minFilter[tmu] = minFilter;
  st.magFilter[tmu] = magFilter;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->BindTexture(tmu, st.boundTex[tmu], st.clampS[tmu], st.clampT[tmu],
                   st.minFilter[tmu], st.magFilter[tmu]);
  }
}

void SetTexClampMode(SharedFrontendState& st, uint32_t tmu, uint32_t sClamp,
                     uint32_t tClamp) {
  if (tmu >= 2) return;
  st.clampS[tmu] = sClamp;
  st.clampT[tmu] = tClamp;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->BindTexture(tmu, st.boundTex[tmu], st.clampS[tmu], st.clampT[tmu],
                   st.minFilter[tmu], st.magFilter[tmu]);
  }
}

void SetTexMipMapMode(SharedFrontendState& st, uint32_t tmu, uint32_t mode,
                      bool lodBlend) {
  if (tmu >= 2) return;
  st.mipmapMode[tmu] = mode;
  st.lodBlend[tmu] = lodBlend;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexMipMapMode(tmu, mode, lodBlend);
  }
}

void SetTexLodBias(SharedFrontendState& st, uint32_t tmu, float bias) {
  if (tmu >= 2) return;
  st.texLodBias[tmu] = bias;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexLodBias(tmu, bias);
  }
}

void BindTexSource(SharedFrontendState& st, uint32_t tmu,
                   uint32_t startAddress) {
  if (tmu >= 2) return;
  st.boundTex[tmu] = startAddress;
  TextureManager::GetInstance().PrepareTextureForBind(tmu, startAddress);
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->BindTexture(tmu, startAddress, st.clampS[tmu], st.clampT[tmu],
                   st.minFilter[tmu], st.magFilter[tmu]);
  }
}

void SetDepthMode(SharedFrontendState& st, uint32_t mode) {
  st.depthMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
  }
}

void SetDepthFunction(SharedFrontendState& st, uint32_t fnc) {
  st.depthCompare = fnc;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
  }
}

void SetDepthMask(SharedFrontendState& st, bool mask) {
  st.depthMask = mask;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
  }
}

void SetDepthBias(SharedFrontendState& st, int32_t level) {
  st.depthBias = level;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
  }
}

void SetSstOrigin(uint32_t& sstOrigin, uint32_t origin) {
  sstOrigin = origin;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetSstOrigin(origin);
  }
}

void SetCullMode(SharedFrontendState& st, uint32_t mode) {
  st.cullMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetCullState(mode);
  }
}

void SetConstantColor(SharedFrontendState& st, uint32_t value) {
  st.constantColor = value;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetConstantColor(value);
  }
}

void SetColorMask(SharedFrontendState& st, bool rgb, bool alpha) {
  st.colorMaskRgb = rgb;
  st.colorMaskAlpha = alpha;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetColorMask(rgb, alpha);
  }
}

void SetDitherMode(SharedFrontendState& st, uint32_t mode) {
  st.ditherMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetDitherMode(mode);
  }
}

void SetChromakeyMode(SharedFrontendState& st, uint32_t mode) {
  st.chromakeyMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetChromakeyMode(mode);
  }
}

void SetChromakeyValue(SharedFrontendState& st, uint32_t value) {
  st.chromakeyValue = value;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetChromakeyValue(value);
  }
}

void SetChromakeyRange(SharedFrontendState& st, uint32_t minColor,
                       uint32_t maxColor, uint32_t mode) {
  st.chromakeyRangeMin = minColor;
  st.chromakeyRangeMax = maxColor;
  st.chromakeyRangeMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetChromakeyRange(minColor, maxColor, mode);
  }
}

void SetTexChromakeyMode(SharedFrontendState& st, uint32_t tmu, uint32_t mode) {
  if (tmu >= 2) return;
  st.texChromaState[tmu].mode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexChromakeyMode(tmu, mode);
  }
}

void SetTexChromakeyRange(SharedFrontendState& st, uint32_t tmu,
                          uint32_t minColor, uint32_t maxColor, uint32_t mode) {
  if (tmu >= 2) return;
  st.texChromaState[tmu].mode = mode;
  st.texChromaState[tmu].minColor = minColor;
  st.texChromaState[tmu].maxColor = maxColor;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexChromakeyRange(tmu, minColor, maxColor, mode);
  }
}

void SetFogMode(SharedFrontendState& st, uint32_t unifiedMode) {
  st.fogMode = unifiedMode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetFogMode(st.fogMode);
  }
}

void SetFogColor(SharedFrontendState& st, uint32_t fogColor) {
  st.fogColor = fogColor;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetFogColor(st.fogColor);
  }
}

void SetFogTable(SharedFrontendState& st, const uint8_t* ft) {
  if (!ft) return;
  std::memcpy(st.fogTable, ft, sizeof(st.fogTable));
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetFogTable(st.fogTable);
  }
}

void DisableAllEffects(SharedFrontendState& st, bool clearCachedFogMode) {
  st.rgbSrcBlend = 4;    // GR_BLEND_ONE
  st.rgbDstBlend = 0;    // GR_BLEND_ZERO
  st.alphaSrcBlend = 4;  // GR_BLEND_ONE
  st.alphaDstBlend = 0;  // GR_BLEND_ZERO
  st.depthCompare = 1;   // GR_CMP_LESS / existing default
  st.depthMask = false;
  st.alphaTestOp = 7;    // GR_CMP_ALWAYS
  if (clearCachedFogMode) {
    st.fogMode = 0;      // GR_FOG_DISABLE
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetBlendState(st.rgbSrcBlend, st.rgbDstBlend, st.alphaSrcBlend,
                     st.alphaDstBlend);
    b->SetDepthState(st.depthMode, st.depthCompare, st.depthMask, st.depthBias);
    b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
    b->SetFogMode(0);
    b->SetChromakeyMode(0);
  }
}

void LoadGammaTable(uint32_t nentries, uint32_t* red, uint32_t* green,
                    uint32_t* blue) {
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->LoadGammaTable(nentries, red, green, blue);
  }
}

void SyncTexturesBeforeDraw() {
  auto& tm = TextureManager::GetInstance();
  if (tm.HasPendingTableSync()) {
    tm.SyncBoundTextures();
  }
}

void SanitizeVertex(const SharedFrontendState& st, ModernVertex& mv) {
  const bool depthActive = (st.depthMode != 0);
  const bool textureActive =
      (st.boundTex[0] != 0xFFFFFFFFu || st.boundTex[1] != 0xFFFFFFFFu);
  const bool fogActive = (st.fogMode != 0);
  const bool wActive = (st.depthMode == 2 || st.depthMode == 4);

  if (!depthActive) {
    mv.pos[2] = 0.0f;
  }
  if (!textureActive && !fogActive && !wActive) {
    mv.pos[3] = 1.0f;
  }
}

void DrawVertexList(uint32_t mode, uint32_t count, const ModernVertex* vertices,
                    bool allowPointsAndLines) {
  auto* backend = BackendManager::GetInstance().GetBackend();
  if (!backend || !vertices || count == 0) return;
  SyncTexturesBeforeDraw();

  if (allowPointsAndLines) {
    if (mode == kGrPoints) {
      for (uint32_t i = 0; i < count; ++i) {
        backend->DrawPoint(vertices[i]);
      }
      return;
    }
    if (mode == kGrLines) {
      for (uint32_t i = 0; i + 1 < count; i += 2) {
        backend->DrawLine(vertices[i], vertices[i + 1]);
      }
      return;
    }
    if (mode == kGrLineStrip) {
      for (uint32_t i = 0; i + 1 < count; ++i) {
        backend->DrawLine(vertices[i], vertices[i + 1]);
      }
      return;
    }
  }

  if (count < 3) return;

  if (mode == kGrTriangles) {
    GLIDE_INCREMENT_TRIANGLES_PROCESSED(count / 3);
    for (uint32_t i = 0; i + 2 < count; i += 3) {
      backend->DrawTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
    }
  } else if (mode == kGrTriangleStrip) {
    GLIDE_INCREMENT_TRIANGLES_PROCESSED(count > 2 ? count - 2 : 0);
    for (uint32_t i = 0; i + 2 < count; ++i) {
      if (i % 2 == 0) {
        backend->DrawTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
      } else {
        backend->DrawTriangle(vertices[i], vertices[i + 2], vertices[i + 1]);
      }
    }
  } else if (mode == kGrTriangleFan || mode == kGrPolygon) {
    GLIDE_INCREMENT_TRIANGLES_PROCESSED(count > 2 ? count - 2 : 0);
    for (uint32_t i = 1; i + 1 < count; ++i) {
      backend->DrawTriangle(vertices[0], vertices[i], vertices[i + 1]);
    }
  }
}

void GetTriStats(uint32_t* processed, uint32_t* drawn) {
  if (processed) {
    *processed = static_cast<uint32_t>(
        TelemetryManager::GetInstance().GetTrianglesProcessed());
  }
  if (drawn) {
    *drawn = static_cast<uint32_t>(
        TelemetryManager::GetInstance().GetTrianglesDrawn());
  }
}

void ResetTriStats() { TelemetryManager::GetInstance().ResetTriangleStats(); }

void GetPerfStats(uint32_t& outPixelsIn, uint32_t& outChromaFail) {
  auto& tracker = TelemetryManager::GetInstance().GetFrameTracker();
  outPixelsIn = static_cast<uint32_t>(tracker.GetCurrentFps());
  outChromaFail = static_cast<uint32_t>(tracker.GetRenderTimeMs() * 1000.0f);
}

void ResetPerfStats() { TelemetryManager::GetInstance().Reset(); }

bool LfbReadRegion(uint32_t srcBuf, uint32_t srcX, uint32_t srcY,
                   uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride,
                   void* dstData) {
  TelemetryManager::GetInstance().RecordLfbRead(
      static_cast<uint64_t>(srcHeight) * dstStride);
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    return b->ReadLFB(srcBuf, srcX, srcY, srcWidth, srcHeight, dstStride,
                      dstData);
  }
  if (dstData) {
    std::memset(dstData, 0, static_cast<size_t>(srcHeight) * dstStride);
  }
  return true;
}

bool LfbWriteRegion(uint32_t dstBuf, uint32_t dstX, uint32_t dstY,
                    uint32_t srcFmt, uint32_t srcWidth, uint32_t srcHeight,
                    int32_t srcStride, const void* srcData) {
  const uint64_t absStride =
      static_cast<uint64_t>(srcStride < 0 ? -static_cast<int64_t>(srcStride)
                                          : static_cast<int64_t>(srcStride));
  TelemetryManager::GetInstance().RecordLfbWrite(
      static_cast<uint64_t>(srcHeight) * absStride);
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    return b->WriteLFB(dstBuf, dstX, dstY, srcWidth, srcHeight, srcStride,
                       srcFmt, srcData);
  }
  return true;
}

bool LfbUnlockWithState(const SharedFrontendState& st, uint32_t type,
                        uint32_t buffer) {
  LfbPipelineConfig config;
  config.pixelPipeline = LfbManager::GetInstance().IsPixelPipeline();
  config.chromakeyEnabled = (st.chromakeyMode != 0);
  config.chromakeyValue = st.chromakeyValue;
  config.constantAlpha = st.lfbConstantAlpha;
  config.rgbSrcBlend = st.rgbSrcBlend;
  config.rgbDstBlend = st.rgbDstBlend;
  return LfbManager::GetInstance().Unlock(type, buffer, config);
}

void FlushActiveLfbWriteLock(const SharedFrontendState& st) {
  auto& lfb = LfbManager::GetInstance();
  if (!lfb.IsWriteLocked()) return;
  LfbPipelineConfig config;
  config.pixelPipeline = lfb.IsPixelPipeline();
  config.chromakeyEnabled = (st.chromakeyMode != 0);
  config.chromakeyValue = st.chromakeyValue;
  config.constantAlpha = st.lfbConstantAlpha;
  config.rgbSrcBlend = st.rgbSrcBlend;
  config.rgbDstBlend = st.rgbDstBlend;
  lfb.FlushActiveWriteLock(config);
}

const char* GetRendererString() {
  auto& reg = EmulationRegistry::GetInstance();
  if (reg.GetConfig().backend == "opengl_es") {
    return "3dfx glide-ng OpenGL ES Wrapper";
  }
  if (reg.GetConfig().backend == "software") {
    return "3dfx glide-ng Software Wrapper";
  }
  return "3dfx glide-ng Vulkan Wrapper";
}

int32_t Map3dfAspectToGlide2(int aspectW, int aspectH) {
  if (aspectW == 1 && aspectH == 1) return 0x3;  // GR_ASPECT_1x1
  if (aspectW == 1 && aspectH == 2) return 0x4;  // GR_ASPECT_1x2
  if (aspectW == 1 && aspectH == 4) return 0x5;  // GR_ASPECT_1x4
  if (aspectW == 1 && aspectH == 8) return 0x6;  // GR_ASPECT_1x8
  if (aspectW == 2 && aspectH == 1) return 0x2;  // GR_ASPECT_2x1
  if (aspectW == 4 && aspectH == 1) return 0x1;  // GR_ASPECT_4x1
  if (aspectW == 8 && aspectH == 1) return 0x0;  // GR_ASPECT_8x1
  return 0x3;
}

int32_t Map3dfLodToGlide2(int dim) {
  if (dim <= 1) return 0x8;    // GR_LOD_1
  if (dim <= 2) return 0x7;    // GR_LOD_2
  if (dim <= 4) return 0x6;    // GR_LOD_4
  if (dim <= 8) return 0x5;    // GR_LOD_8
  if (dim <= 16) return 0x4;   // GR_LOD_16
  if (dim <= 32) return 0x3;   // GR_LOD_32
  if (dim <= 64) return 0x2;   // GR_LOD_64
  if (dim <= 128) return 0x1;  // GR_LOD_128
  return 0x0;                  // GR_LOD_256
}

int32_t Map3dfAspectToGlide3(int aspectW, int aspectH) {
  if (aspectW == 1 && aspectH == 1) return 0;   // GR_ASPECT_LOG2_1x1
  if (aspectW == 1 && aspectH == 2) return -1;  // GR_ASPECT_LOG2_1x2
  if (aspectW == 1 && aspectH == 4) return -2;  // GR_ASPECT_LOG2_1x4
  if (aspectW == 1 && aspectH == 8) return -3;  // GR_ASPECT_LOG2_1x8
  if (aspectW == 2 && aspectH == 1) return 1;   // GR_ASPECT_LOG2_2x1
  if (aspectW == 4 && aspectH == 1) return 2;   // GR_ASPECT_LOG2_4x1
  if (aspectW == 8 && aspectH == 1) return 3;   // GR_ASPECT_LOG2_8x1
  return 0;
}

int32_t Map3dfLodToGlide3(int dim) {
  if (dim <= 1) return 0x0;    // GR_LOD_LOG2_1
  if (dim <= 2) return 0x1;    // GR_LOD_LOG2_2
  if (dim <= 4) return 0x2;    // GR_LOD_LOG2_4
  if (dim <= 8) return 0x3;    // GR_LOD_LOG2_8
  if (dim <= 16) return 0x4;   // GR_LOD_LOG2_16
  if (dim <= 32) return 0x5;   // GR_LOD_LOG2_32
  if (dim <= 64) return 0x6;   // GR_LOD_LOG2_64
  if (dim <= 128) return 0x7;  // GR_LOD_LOG2_128
  return 0x8;                  // GR_LOD_LOG2_256
}

bool Load3dfPixelsAndPalette(const char* filename, uint32_t format,
                             uint32_t* paletteOut256, void* pixelDataOut) {
  if (!filename || !pixelDataOut) return false;

  Texture::Shared3dfInfo sharedInfo;
  if (!Texture::Load3dfFile(filename, sharedInfo)) {
    return false;
  }

  if (paletteOut256 && (format == kGrTexFmtP8 || format == kGrTexFmtP8_6666)) {
    for (int i = 0; i < 256; ++i) {
      paletteOut256[i] = sharedInfo.palette[i];
    }
  }

  std::memcpy(pixelDataOut, sharedInfo.pixelData.data(),
              sharedInfo.pixelData.size());
  return true;
}

bool ShouldRenderShamelessPlug(bool appEnabled) {
  const int mode = EmulationRegistry::GetInstance().GetConfig().shamelessPlugMode;
  if (mode > 0) return true;
  if (mode == 0) return false;
  return appEnabled;
}

void RenderShamelessPlug() {
  uint32_t scrWidth = 640;
  uint32_t scrHeight = 480;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    scrWidth = backend->GetWidth();
    scrHeight = backend->GetHeight();
  }
  if (auto* animator = BackendManager::GetInstance().GetSplashAnimator()) {
    animator->RenderBanner(scrWidth, scrHeight);
  }
}

void RenderSplash(float x, float y, float w, float h, uint32_t frame,
                  void (*callback)(int frame)) {
  if (EmulationRegistry::GetInstance().GetConfig().noSplash) {
    GLIDE_LOG(DEBUG, "Frontend",
              "grSplash suppressed by config (FX_GLIDE_NO_SPLASH / GLIDE_NG_NO_SPLASH).");
    return;
  }
  if (auto* animator = BackendManager::GetInstance().GetSplashAnimator()) {
    animator->Render(x, y, w, h, frame, callback);
  }
}

void HandleDosboxSetConfig(const char* apiTag, uint32_t flags, void* magic) {
  GLIDE_LOG(INFO, "Frontend",
            "setConfig (DOSBox extension, " << apiTag
                                            << ") called. flags=" << flags);
  if (magic) {
    uint32_t signature = 0x324c4453;  // 'SDL2'
    std::memcpy(magic, &signature, sizeof(signature));
  }
}

void HandleDosboxSetConfigRes(const char* apiTag, int res, void (*swap12)()) {
  GLIDE_LOG(INFO, "Frontend",
            "setConfigRes (DOSBox extension, "
                << apiTag << ") called. res=" << res
                << ", swap12=" << reinterpret_cast<void*>(swap12));
}

}  // namespace EntryCommon
}  // namespace GlideWrapper
