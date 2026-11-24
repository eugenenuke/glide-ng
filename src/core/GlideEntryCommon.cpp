#include "core/GlideEntryCommon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/3dfParser.h"
#include "core/BackendManager.h"
#include "core/GlideSplashAnimator.h"
#include "core/IniConfigLoader.h"
#include "core/LfbManager.h"
#include "core/Logger.h"
#include "core/NativeSurfaceHost.h"
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

// Phase-2 diagnostic state (bounds check & framebuffer snapshots).
std::atomic<uint64_t> s_boundsViolations{0};
std::atomic<uint64_t> s_boundsWarnEmitted{0};
constexpr uint64_t kMaxBoundsWarnings = 16;

uint64_t s_snapshotFrameCounter = 0;
uint32_t s_snapshotCaptureCount = 0;
size_t s_snapshotListCursor = 0;
bool s_snapshotDone = false;
uint32_t s_activeColorFormat = 0;
bool s_autoSplashPlayed = false;
bool s_inAutoSplash = false;

inline uint32_t ResolveEffectiveFilter(uint32_t filter,
                                       const WrapperConfig& cfg) {
  if (cfg.forceBilinear || cfg.forceTrilinear) {
    return 1u;  // GR_TEXTUREFILTER_BILINEAR
  }
  return filter;
}

inline void ResolveEffectiveMipMap(uint32_t& mode, bool& lodBlend,
                                   const WrapperConfig& cfg) {
  if (cfg.forceTrilinear) {
    if (mode == 0u) {
      mode = 1u;  // Upgrade GR_MIPMAP_DISABLE -> GR_MIPMAP_NEAREST
    }
    lodBlend = true;
  }
}

inline uint32_t ResolveEffectiveBoundTex(uint32_t addr,
                                         const WrapperConfig& cfg) {
  return cfg.texmapDisable ? 0xFFFFFFFFu : addr;
}

inline uint32_t ResolveEffectiveDither(uint32_t mode,
                                       const WrapperConfig& cfg) {
  return cfg.noDitherSub ? 0u : mode;
}

std::string FormatSnapshotOutputPath(const WrapperConfig& cfg, uint64_t frameIdx) {
  char frameBuf[32];
  std::snprintf(frameBuf, sizeof(frameBuf), "%06llu",
                static_cast<unsigned long long>(frameIdx));
  const std::string frameTag(frameBuf);

  if (cfg.snapshotPath.empty()) {
    return "glide_snapshot_" + frameTag + ".ppm";
  }

  const char lastCh = cfg.snapshotPath.back();
  if (lastCh == '/' || lastCh == '\\') {
    return cfg.snapshotPath + "glide_snapshot_" + frameTag + ".ppm";
  }

  if (!cfg.IsMultiFrameSnapshot()) {
    return cfg.snapshotPath;
  }

  const size_t lastSep = cfg.snapshotPath.find_last_of("/\\");
  const size_t lastDot = cfg.snapshotPath.rfind('.');
  const bool hasExtension =
      (lastDot != std::string::npos) &&
      (lastSep == std::string::npos ? lastDot > 0 : lastDot > lastSep + 1);
  if (hasExtension) {
    return cfg.snapshotPath.substr(0, lastDot) + "_" + frameTag +
           cfg.snapshotPath.substr(lastDot);
  }
  return cfg.snapshotPath + "_" + frameTag + ".ppm";
}

void MaybeCaptureSnapshot(IGraphicsBackend* backend) {
  if (s_inAutoSplash) {
    return;
  }
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  const uint64_t frameIdx = s_snapshotFrameCounter++;
  if (!cfg.snapshotEnabled || s_snapshotDone || !backend) {
    return;
  }

  bool shouldCapture = false;
  if (cfg.snapshotInterval > 0) {
    if (frameIdx >= cfg.snapshotFrame &&
        ((frameIdx - cfg.snapshotFrame) % cfg.snapshotInterval == 0)) {
      shouldCapture = true;
    }
  } else if (!cfg.snapshotFrames.empty()) {
    while (s_snapshotListCursor < cfg.snapshotFrames.size() &&
           cfg.snapshotFrames[s_snapshotListCursor] < frameIdx) {
      ++s_snapshotListCursor;
    }
    if (s_snapshotListCursor < cfg.snapshotFrames.size() &&
        cfg.snapshotFrames[s_snapshotListCursor] == frameIdx) {
      shouldCapture = true;
      ++s_snapshotListCursor;
    }
  } else if (frameIdx == cfg.snapshotFrame) {
    shouldCapture = true;
  }

  if (!shouldCapture) {
    return;
  }

  const uint32_t w = backend->GetGuestWidth();
  const uint32_t h = backend->GetGuestHeight();
  if (w == 0 || h == 0) return;

  std::vector<uint32_t> pixels(static_cast<size_t>(w) * h, 0u);
  // GR_BUFFER_BACKBUFFER == 1
  if (!backend->ReadLFB(1u, 0, 0, w, h, w * 4u, pixels.data())) {
    GLIDE_LOG(WARN, "Snapshot",
              "FX_GLIDE_SNAPSHOT failed to read backbuffer on frame "
                  << frameIdx);
    s_snapshotDone = true;
    return;
  }

  const std::string outPath = FormatSnapshotOutputPath(cfg, frameIdx);
  std::error_code dirEc;
  const std::filesystem::path fsOut(outPath);
  if (fsOut.has_parent_path() && !fsOut.parent_path().empty()) {
    std::filesystem::create_directories(fsOut.parent_path(), dirEc);
  }

  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    GLIDE_LOG(WARN, "Snapshot",
              "FX_GLIDE_SNAPSHOT could not open output file: " << outPath);
    s_snapshotDone = true;
    return;
  }

  const std::string header =
      "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
  out.write(header.data(), static_cast<std::streamsize>(header.size()));

  std::vector<uint8_t> rgbRow(static_cast<size_t>(w) * 3u);
  for (uint32_t y = 0; y < h; ++y) {
    const uint32_t* srcRow = pixels.data() + static_cast<size_t>(y) * w;
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t p = srcRow[x];
      uint32_t argb = p;
      if (s_activeColorFormat == 1u) {
        argb = (p & 0xFF00FF00u) | ((p & 0x000000FFu) << 16) |
               ((p & 0x00FF0000u) >> 16);
      } else if (s_activeColorFormat == 2u) {
        argb = ((p & 0x000000FFu) << 24) | ((p & 0xFFFFFF00u) >> 8);
      } else if (s_activeColorFormat == 3u) {
        argb = ((p & 0x000000FFu) << 24) | ((p & 0x0000FF00u) << 8) |
               ((p & 0x00FF0000u) >> 8) | ((p & 0xFF000000u) >> 24);
      }
      rgbRow[x * 3u + 0u] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
      rgbRow[x * 3u + 1u] = static_cast<uint8_t>((argb >> 8) & 0xFFu);
      rgbRow[x * 3u + 2u] = static_cast<uint8_t>(argb & 0xFFu);
    }
    out.write(reinterpret_cast<const char*>(rgbRow.data()),
              static_cast<std::streamsize>(rgbRow.size()));
  }
  out.close();

  ++s_snapshotCaptureCount;
  const bool isPeriodic = (cfg.snapshotInterval > 0);
  const bool isList = (!cfg.snapshotFrames.empty());
  if ((!isPeriodic && !isList) ||
      (isList && s_snapshotListCursor >= cfg.snapshotFrames.size()) ||
      (isPeriodic && cfg.snapshotMaxCount > 0 &&
       s_snapshotCaptureCount >= cfg.snapshotMaxCount)) {
    s_snapshotDone = true;
  }

  GLIDE_LOG(INFO, "Snapshot",
            "Saved " << w << "x" << h << " backbuffer snapshot (frame "
                     << frameIdx << ") to " << outPath);
}

void LogBoundsCheckSummaryIfAny() {
  const uint64_t total = s_boundsViolations.load(std::memory_order_relaxed);
  if (total > 0) {
    GLIDE_LOG(WARN, "BoundsCheck",
              "FX_GLIDE_BOUNDS_CHECK recorded " << total
                                                << " out-of-bounds / invalid "
                                                   "vertex event(s).");
  }
}
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
  texDetailBias[0] = texDetailBias[1] = 0;
  texDetailScale[0] = texDetailScale[1] = 0;
  texDetailMax[0] = texDetailMax[1] = 0.0f;
  mipmapMode[0] = mipmapMode[1] = 0;
  lodBlend[0] = lodBlend[1] = false;

  constantColor = 0xFFFFFFFFu;
  rgbSrcBlend = 4;
  rgbDstBlend = 0;
  rgbBlendOp = 0;
  alphaSrcBlend = 4;
  alphaDstBlend = 0;
  alphaBlendOp = 0;

  extColorCombine = 0;
  extAlphaCombine = 0;
  extTexColorCombine[0] = extTexColorCombine[1] = 0;
  extTexAlphaCombine[0] = extTexAlphaCombine[1] = 0;
  tmuConstantColor[0] = tmuConstantColor[1] = 0xFFFFFFFFu;

  cullMode = 0;
  clipMinX = 0;
  clipMinY = 0;
  clipMaxX = clipWidth;
  clipMaxY = clipHeight;

  fogMode = 0;
  fogColor = 0xFF000000u;
  std::memset(fogTable, 0, sizeof(fogTable));

  colorMaskRgb = true;
  colorMaskR = true;
  colorMaskG = true;
  colorMaskB = true;
  colorMaskAlpha = true;
  ditherMode = 0;

  lfbConstantAlpha = defaultLfbConstantAlpha;
  lfbConstantStencil = 0;
  lfbWriteColorFormat = 0;

  stencilEnabled = false;
  stencilCompareOp = 7u;
  stencilRef = 0u;
  stencilReadMask = 0xFFu;
  stencilWriteMask = 0xFFu;
  stencilFailOp = 0u;
  stencilZFailOp = 0u;
  stencilZPassOp = 0u;

  chromakeyMode = 0;
  chromakeyValue = 0;
  chromakeyRangeMin = 0;
  chromakeyRangeMax = 0;
  chromakeyRangeMode = 0;

  screenWidth = clipWidth;
  screenHeight = clipHeight;
  colorFormat = 0;
  effectiveColorBuffers = 2;
  effectiveAuxBuffers = 1;
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
  const auto& cfg = reg.GetConfig();
  for (uint32_t tmu = 0; tmu < cfg.tmuCount; ++tmu) {
    TextureManager::GetInstance().SetTmuMemoryLimitMb(tmu, cfg.tmuMemoryMb);
  }
}

void InitGlide(const char* apiTag, void (*resetFrontendState)(),
               CardModel defaultModel, ApiVersion defaultGlide2Api,
               bool hasCustomDefaultGlide2Api) {
  if (resetFrontendState) resetFrontendState();
  auto& reg = EmulationRegistry::GetInstance();
  IniConfigLoader loader;
  loader.LoadWithDefaults(DEFAULT_CONFIG_FILE, reg.GetConfig(), defaultModel,
                          defaultGlide2Api, hasCustomDefaultGlide2Api);
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
  // Authentic FX_GLIDE_TEXTURE_UMA / [glide] texture_uma initial state
  // (mirrors glide3x/h5/glide3/src/gpci.c:1204). Applied once at init time so
  // stale state does not leak across grGlideInit calls, while preserving any
  // grEnable(GR_TEXTURE_UMA_EXT) called before grSstWinOpen/grSstWinOpenExt.
  TextureManager::GetInstance().SetUmaMode(cfg.textureUmaMode == 1);
}

void EnsureEngineInitializedSlow(void (*resetFrontendState)(),
                                 CardModel defaultModel,
                                 ApiVersion defaultGlide2Api,
                                 bool hasCustomDefaultGlide2Api) {
  auto& reg = EmulationRegistry::GetInstance();
  auto& bm = BackendManager::GetInstance();
  if (!bm.GetBackend()) {
    if (resetFrontendState) resetFrontendState();
    IniConfigLoader loader;
    loader.LoadWithDefaults(DEFAULT_CONFIG_FILE, reg.GetConfig(), defaultModel,
                            defaultGlide2Api, hasCustomDefaultGlide2Api);

    const auto& cfg = reg.GetConfig();
    auto& logger = Logger::GetInstance();
    logger.Initialize(cfg);
    TelemetryManager::GetInstance().ConfigurePerfLog(cfg.perfLogEnabled,
                                                     cfg.perfLogCsvPath);
    logger.LogExecutionSummary(cfg);
    bm.EstablishBackend(cfg);
    ConfigureTmuMemoryLimits();
    TextureManager::GetInstance().SetUmaMode(cfg.textureUmaMode == 1);
    GLIDE_LOG(INFO, "Frontend", "Implicit grGlideInit executed seamlessly.");
  }
}

void ShutdownGlide(const char* apiTag) {
  GLIDE_LOG(INFO, "Frontend", "grGlideShutdown (" << apiTag << ") invoked.");
  LogBoundsCheckSummaryIfAny();
  g_boundsCheckEnabled.store(false, std::memory_order_relaxed);
  LfbManager::GetInstance().Reset();
  TelemetryManager::GetInstance().ShutdownPerfLog();
  TelemetryManager::GetInstance().PrintReport();
  BackendManager::GetInstance().ShutdownBackend();
  Logger::GetInstance().Flush();
}

void CloseWindow(const char* logMessage) {
  LogBoundsCheckSummaryIfAny();
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

void ClearBufferExt(uint32_t color, uint8_t alpha, uint32_t depth,
                    uint8_t stencil) {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    const float normDepth =
        (depth > 65535u) ? (static_cast<float>(depth & 0xFFFFFFu) / 16777215.0f)
                         : (static_cast<float>(depth) / 65535.0f);
    backend->ClearBufferExt(color, alpha, normDepth, stencil, 0x7u);
  }
}

void SwapBuffers() {
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    MaybeCaptureSnapshot(backend);
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
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();

  const bool auxEnabled = (st.effectiveAuxBuffers > 0);
  b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                   auxEnabled ? st.depthMask : false, st.depthBias);
  b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
  b->SetCombinerMode(st.colorFunc, st.colorFactor, st.colorLocal, st.colorOther,
                     st.colorInvert, st.alphaFunc, st.alphaFactor,
                     st.alphaLocal, st.alphaOther, st.alphaInvert);
  b->SetColorCombineExt(st.extColorCombine);
  b->SetAlphaCombineExt(st.extAlphaCombine);
  b->SetBlendStateExt(st.rgbSrcBlend, st.rgbDstBlend, st.rgbBlendOp,
                      st.alphaSrcBlend, st.alphaDstBlend, st.alphaBlendOp);
  b->SetCullState(st.cullMode);
  b->SetSstOrigin(sstOrigin);
  b->SetClipWindow(st.clipMinX, st.clipMinY, st.clipMaxX, st.clipMaxY);
  b->SetColorMaskExt(st.colorMaskR, st.colorMaskG, st.colorMaskB,
                     st.colorMaskAlpha);
  b->SetStencilMode(st.stencilEnabled);
  b->SetStencilFunc(st.stencilCompareOp, st.stencilRef, st.stencilReadMask);
  b->SetStencilMask(st.stencilWriteMask);
  b->SetStencilOp(st.stencilFailOp, st.stencilZFailOp, st.stencilZPassOp);
  b->SetDitherMode(ResolveEffectiveDither(st.ditherMode, cfg));
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
    b->SetTexColorCombineExt(tmu, st.extTexColorCombine[tmu]);
    b->SetTexAlphaCombineExt(tmu, st.extTexAlphaCombine[tmu]);
    b->SetTmuConstantColor(tmu, st.tmuConstantColor[tmu]);
    b->BindTexture(tmu, ResolveEffectiveBoundTex(st.boundTex[tmu], cfg),
                   st.clampS[tmu], st.clampT[tmu],
                   ResolveEffectiveFilter(st.minFilter[tmu], cfg),
                   ResolveEffectiveFilter(st.magFilter[tmu], cfg));
    b->SetTexChromakeyMode(tmu, st.texChromaState[tmu].mode);
    b->SetTexChromakeyRange(tmu, st.texChromaState[tmu].minColor,
                            st.texChromaState[tmu].maxColor,
                            st.texChromaState[tmu].mode);
    uint32_t effMipMode = st.mipmapMode[tmu];
    bool effLodBlend = st.lodBlend[tmu];
    ResolveEffectiveMipMap(effMipMode, effLodBlend, cfg);
    b->SetTexMipMapMode(tmu, effMipMode, effLodBlend);
    b->SetTexLodBias(tmu, st.texLodBias[tmu]);
    b->SetTexDetailControl(tmu, st.texDetailBias[tmu], st.texDetailScale[tmu],
                           st.texDetailMax[tmu]);
  }
}

namespace {
void UploadGammaRgbLut(IGraphicsBackend* b, float red, float green,
                       float blue) {
  if (!b) return;
  const float gr = (red > 0.0f) ? red : 1.0f;
  const float gg = (green > 0.0f) ? green : 1.0f;
  const float gb = (blue > 0.0f) ? blue : 1.0f;
  uint32_t rTable[256], gTable[256], bTable[256];
  for (int i = 0; i < 256; ++i) {
    rTable[i] = static_cast<uint32_t>(
        std::pow(i / 255.0f, 1.0f / gr) * 255.0f + 0.5f);
    gTable[i] = static_cast<uint32_t>(
        std::pow(i / 255.0f, 1.0f / gg) * 255.0f + 0.5f);
    bTable[i] = static_cast<uint32_t>(
        std::pow(i / 255.0f, 1.0f / gb) * 255.0f + 0.5f);
  }
  b->LoadGammaTable(256, rTable, gTable, bTable);
}

void ApplyConfiguredGammaToBackend(IGraphicsBackend* b,
                                   const WrapperConfig& cfg) {
  if (!b) return;
  if (cfg.HasPerChannelGammaOverride()) {
    const float base = (cfg.gammaOverride > 0.0f) ? cfg.gammaOverride : 1.0f;
    const float gr = (cfg.gammaOverrideR > 0.0f) ? cfg.gammaOverrideR : base;
    const float gg = (cfg.gammaOverrideG > 0.0f) ? cfg.gammaOverrideG : base;
    const float gb = (cfg.gammaOverrideB > 0.0f) ? cfg.gammaOverrideB : base;
    UploadGammaRgbLut(b, gr, gg, gb);
  } else if (cfg.gammaOverride > 0.0f) {
    b->SetGamma(cfg.gammaOverride);
  }
}
}  // namespace

void ApplyPostWinOpenOverrides(SharedFrontendState& st, uint32_t colorFormat,
                               int reqColBuffers, int reqAuxBuffers) {
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  st.colorFormat = colorFormat;
  s_activeColorFormat = colorFormat;
  st.effectiveColorBuffers =
      (cfg.allocColorBuffers >= 2) ? cfg.allocColorBuffers : reqColBuffers;
  // Preserve legacy compatibility when apps pass nAuxBuffers=0 yet still call
  // grDepthBufferMode(); only an explicit FX_GLIDE_ALLOC_AUX=0 / alloc_aux=0
  // override disables the auxiliary depth buffer.
  st.effectiveAuxBuffers = (cfg.allocAuxBuffers >= 0)
                               ? cfg.allocAuxBuffers
                               : (reqAuxBuffers > 0 ? reqAuxBuffers : 1);

  if (cfg.allocColorBuffers >= 2 || cfg.allocAuxBuffers >= 0) {
    GLIDE_LOG(INFO, "Frontend",
              "Effective buffer allocation: color="
                  << st.effectiveColorBuffers << " (requested " << reqColBuffers
                  << "), aux=" << st.effectiveAuxBuffers << " (requested "
                  << reqAuxBuffers << ")");
  }

  s_boundsViolations.store(0, std::memory_order_relaxed);
  s_boundsWarnEmitted.store(0, std::memory_order_relaxed);
  g_boundsCheckEnabled.store(cfg.boundsCheck, std::memory_order_relaxed);
  s_snapshotFrameCounter = 0;
  s_snapshotCaptureCount = 0;
  s_snapshotListCursor = 0;
  s_snapshotDone = false;

  auto* b = BackendManager::GetInstance().GetBackend();
  if (!b) return;

  if (cfg.HasGammaOverride()) {
    if (cfg.HasPerChannelGammaOverride()) {
      const float base = (cfg.gammaOverride > 0.0f) ? cfg.gammaOverride : 1.0f;
      const float gr = (cfg.gammaOverrideR > 0.0f) ? cfg.gammaOverrideR : base;
      const float gg = (cfg.gammaOverrideG > 0.0f) ? cfg.gammaOverrideG : base;
      const float gb = (cfg.gammaOverrideB > 0.0f) ? cfg.gammaOverrideB : base;
      GLIDE_LOG(INFO, "Frontend",
                "Applying authentic RGB gamma override: R=" << gr << " G=" << gg
                                                            << " B=" << gb);
    } else {
      GLIDE_LOG(INFO, "Frontend",
                "Applying authentic gamma override: " << cfg.gammaOverride);
    }
    ApplyConfiguredGammaToBackend(b, cfg);
  }
  if (cfg.noDitherSub) {
    b->SetDitherMode(0u);
  }
  if (st.effectiveAuxBuffers == 0) {
    b->SetDepthState(0u, st.depthCompare, false, st.depthBias);
  }
  if (cfg.forceBilinear || cfg.forceTrilinear || cfg.texmapDisable) {
    for (uint32_t tmu = 0; tmu < 2; ++tmu) {
      b->BindTexture(tmu, ResolveEffectiveBoundTex(st.boundTex[tmu], cfg),
                     st.clampS[tmu], st.clampT[tmu],
                     ResolveEffectiveFilter(st.minFilter[tmu], cfg),
                     ResolveEffectiveFilter(st.magFilter[tmu], cfg));
      if (cfg.forceTrilinear) {
        uint32_t effMipMode = st.mipmapMode[tmu];
        bool effLodBlend = st.lodBlend[tmu];
        ResolveEffectiveMipMap(effMipMode, effLodBlend, cfg);
        b->SetTexMipMapMode(tmu, effMipMode, effLodBlend);
      }
    }
  }
}

void SetGammaCorrection(float value) {
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  if (cfg.HasGammaOverride()) {
    GLIDE_LOG(DEBUG, "Frontend",
              "App requested gamma=" << value
                                     << ", preserving configured gamma override.");
    ApplyConfiguredGammaToBackend(BackendManager::GetInstance().GetBackend(),
                                  cfg);
    return;
  }
  if (!cfg.useAppGamma) {
    GLIDE_LOG(DEBUG, "Frontend",
              "Ignoring app grGammaCorrectionValue(" << value
                                                     << ") due to "
                                                        "FX_GLIDE_USE_APP_GAMMA=0.");
    return;
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetGamma(value);
  }
}

void SetGammaCorrectionRGB(float red, float green, float blue) {
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  if (cfg.HasGammaOverride()) {
    GLIDE_LOG(DEBUG, "Frontend",
              "App requested guGammaCorrectionRGB("
                  << red << "," << green << "," << blue
                  << "), preserving configured gamma override.");
    ApplyConfiguredGammaToBackend(BackendManager::GetInstance().GetBackend(),
                                  cfg);
    return;
  }
  if (!cfg.useAppGamma) {
    GLIDE_LOG(DEBUG, "Frontend",
              "Ignoring app guGammaCorrectionRGB due to "
              "FX_GLIDE_USE_APP_GAMMA=0.");
    return;
  }
  UploadGammaRgbLut(BackendManager::GetInstance().GetBackend(), red, green,
                    blue);
}

uint64_t GetBoundsCheckViolationCount() {
  return s_boundsViolations.load(std::memory_order_relaxed);
}

void CheckVertexBoundsSlow(const SharedFrontendState& st,
                           const ModernVertex& mv) {
  const float maxX = static_cast<float>(st.screenWidth) + 1.0f;
  const float maxY = static_cast<float>(st.screenHeight) + 1.0f;
  const bool badFinite = !std::isfinite(mv.pos[0]) || !std::isfinite(mv.pos[1]) ||
                         !std::isfinite(mv.pos[2]) || !std::isfinite(mv.pos[3]);
  const bool badRange = (mv.pos[0] < -1.0f || mv.pos[0] > maxX ||
                         mv.pos[1] < -1.0f || mv.pos[1] > maxY ||
                         mv.pos[3] <= 0.0f);
  if (!badFinite && !badRange) return;

  s_boundsViolations.fetch_add(1, std::memory_order_relaxed);
  const uint64_t emitted =
      s_boundsWarnEmitted.fetch_add(1, std::memory_order_relaxed);
  if (emitted < kMaxBoundsWarnings) {
    GLIDE_LOG(WARN, "BoundsCheck",
              "Vertex out of bounds/invalid: pos=("
                  << mv.pos[0] << ", " << mv.pos[1] << ", z=" << mv.pos[2]
                  << ", oow=" << mv.pos[3] << "), screen=" << st.screenWidth
                  << "x" << st.screenHeight);
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
  st.extColorCombine = 0u;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetColorCombineExt(0u);
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
  st.extAlphaCombine = 0u;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetAlphaCombineExt(0u);
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
  st.extTexColorCombine[tmu] = 0u;
  st.extTexAlphaCombine[tmu] = 0u;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexColorCombineExt(tmu, 0u);
    b->SetTexAlphaCombineExt(tmu, 0u);
    b->SetTexCombinerMode(tmu, rgbFnc, rgbFactor, alphaFnc, alphaFactor,
                          forwardInverts ? rgbInvert : false,
                          forwardInverts ? alphaInvert : false);
  }
}

void SetAlphaBlendFunction(SharedFrontendState& st, uint32_t rgbSf,
                           uint32_t rgbDf, uint32_t alphaSf, uint32_t alphaDf) {
  st.rgbSrcBlend = rgbSf;
  st.rgbDstBlend = rgbDf;
  st.rgbBlendOp = 0u;
  st.alphaSrcBlend = alphaSf;
  st.alphaDstBlend = alphaDf;
  st.alphaBlendOp = 0u;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetBlendState(rgbSf, rgbDf, alphaSf, alphaDf);
  }
}

void SetAlphaBlendFunctionExt(SharedFrontendState& st, uint32_t rgbSf,
                              uint32_t rgbDf, uint32_t rgbOp, uint32_t alphaSf,
                              uint32_t alphaDf, uint32_t alphaOp) {
  st.rgbSrcBlend = rgbSf;
  st.rgbDstBlend = rgbDf;
  st.rgbBlendOp = rgbOp;
  st.alphaSrcBlend = alphaSf;
  st.alphaDstBlend = alphaDf;
  st.alphaBlendOp = alphaOp;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetBlendStateExt(rgbSf, rgbDf, rgbOp, alphaSf, alphaDf, alphaOp);
  }
}

void SetColorCombineExt(SharedFrontendState& st, uint32_t a, uint32_t aMode,
                        uint32_t b, uint32_t bMode, uint32_t c, bool cInvert,
                        uint32_t d, bool dInvert, uint32_t shift, bool invert) {
  st.extColorCombine = IGraphicsBackend::PackCombineExt(
      a, aMode, b, bMode, c, cInvert, d, dInvert, shift, invert);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetColorCombineExt(st.extColorCombine);
  }
}

void SetAlphaCombineExt(SharedFrontendState& st, uint32_t a, uint32_t aMode,
                        uint32_t b, uint32_t bMode, uint32_t c, bool cInvert,
                        uint32_t d, bool dInvert, uint32_t shift, bool invert) {
  st.extAlphaCombine = IGraphicsBackend::PackCombineExt(
      a, aMode, b, bMode, c, cInvert, d, dInvert, shift, invert);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetAlphaCombineExt(st.extAlphaCombine);
  }
}

void SetTexColorCombineExt(SharedFrontendState& st, uint32_t tmu, uint32_t a,
                           uint32_t aMode, uint32_t b, uint32_t bMode,
                           uint32_t c, bool cInvert, uint32_t d, bool dInvert,
                           uint32_t shift, bool invert) {
  if (tmu >= 2) return;
  st.extTexColorCombine[tmu] = IGraphicsBackend::PackCombineExt(
      a, aMode, b, bMode, c, cInvert, d, dInvert, shift, invert);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetTexColorCombineExt(tmu, st.extTexColorCombine[tmu]);
  }
}

void SetTexAlphaCombineExt(SharedFrontendState& st, uint32_t tmu, uint32_t a,
                           uint32_t aMode, uint32_t b, uint32_t bMode,
                           uint32_t c, bool cInvert, uint32_t d, bool dInvert,
                           uint32_t shift, bool invert) {
  if (tmu >= 2) return;
  st.extTexAlphaCombine[tmu] = IGraphicsBackend::PackCombineExt(
      a, aMode, b, bMode, c, cInvert, d, dInvert, shift, invert);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetTexAlphaCombineExt(tmu, st.extTexAlphaCombine[tmu]);
  }
}

void SetTmuConstantColor(SharedFrontendState& st, uint32_t tmu,
                         uint32_t value) {
  if (tmu >= 2) return;
  st.tmuConstantColor[tmu] = value;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetTmuConstantColor(tmu, value);
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
    const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
    b->BindTexture(tmu, ResolveEffectiveBoundTex(st.boundTex[tmu], cfg),
                   st.clampS[tmu], st.clampT[tmu],
                   ResolveEffectiveFilter(st.minFilter[tmu], cfg),
                   ResolveEffectiveFilter(st.magFilter[tmu], cfg));
  }
}

void SetTexClampMode(SharedFrontendState& st, uint32_t tmu, uint32_t sClamp,
                     uint32_t tClamp) {
  if (tmu >= 2) return;
  st.clampS[tmu] = sClamp;
  st.clampT[tmu] = tClamp;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
    b->BindTexture(tmu, ResolveEffectiveBoundTex(st.boundTex[tmu], cfg),
                   st.clampS[tmu], st.clampT[tmu],
                   ResolveEffectiveFilter(st.minFilter[tmu], cfg),
                   ResolveEffectiveFilter(st.magFilter[tmu], cfg));
  }
}

void SetTexMipMapMode(SharedFrontendState& st, uint32_t tmu, uint32_t mode,
                      bool lodBlend) {
  if (tmu >= 2) return;
  st.mipmapMode[tmu] = mode;
  st.lodBlend[tmu] = lodBlend;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
    uint32_t effMode = mode;
    bool effLodBlend = lodBlend;
    ResolveEffectiveMipMap(effMode, effLodBlend, cfg);
    b->SetTexMipMapMode(tmu, effMode, effLodBlend);
  }
}

void SetTexLodBias(SharedFrontendState& st, uint32_t tmu, float bias) {
  if (tmu >= 2) return;
  st.texLodBias[tmu] = bias;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexLodBias(tmu, bias);
  }
}

void SetTexDetailControl(SharedFrontendState& st, uint32_t tmu, int32_t lodBias,
                         uint8_t detailScale, float detailMax) {
  if (tmu >= 2) return;
  st.texDetailBias[tmu] = std::clamp(lodBias, -32, 31);
  st.texDetailScale[tmu] = std::min<uint8_t>(detailScale, 7u);
  st.texDetailMax[tmu] = std::clamp(detailMax, 0.0f, 1.0f);
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->SetTexDetailControl(tmu, st.texDetailBias[tmu], st.texDetailScale[tmu],
                           st.texDetailMax[tmu]);
  }
}

void BindTexSource(SharedFrontendState& st, uint32_t tmu,
                   uint32_t startAddress) {
  if (tmu >= 2) return;
  st.boundTex[tmu] = startAddress;
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  if (!cfg.texmapDisable) {
    TextureManager::GetInstance().PrepareTextureForBind(tmu, startAddress);
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->BindTexture(tmu, ResolveEffectiveBoundTex(startAddress, cfg),
                   st.clampS[tmu], st.clampT[tmu],
                   ResolveEffectiveFilter(st.minFilter[tmu], cfg),
                   ResolveEffectiveFilter(st.magFilter[tmu], cfg));
  }
}

void SetDepthMode(SharedFrontendState& st, uint32_t mode) {
  st.depthMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const bool auxEnabled = (st.effectiveAuxBuffers > 0);
    b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                     auxEnabled ? st.depthMask : false, st.depthBias);
  }
}

void SetDepthFunction(SharedFrontendState& st, uint32_t fnc) {
  st.depthCompare = fnc;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const bool auxEnabled = (st.effectiveAuxBuffers > 0);
    b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                     auxEnabled ? st.depthMask : false, st.depthBias);
  }
}

void SetDepthMask(SharedFrontendState& st, bool mask) {
  st.depthMask = mask;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const bool auxEnabled = (st.effectiveAuxBuffers > 0);
    b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                     auxEnabled ? st.depthMask : false, st.depthBias);
  }
}

void SetDepthBias(SharedFrontendState& st, int32_t level) {
  st.depthBias = level;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const bool auxEnabled = (st.effectiveAuxBuffers > 0);
    b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                     auxEnabled ? st.depthMask : false, st.depthBias);
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
  SetColorMaskExt(st, rgb, rgb, rgb, alpha);
}

void SetColorMaskExt(SharedFrontendState& st, bool r, bool g, bool b, bool a) {
  st.colorMaskR = r;
  st.colorMaskG = g;
  st.colorMaskB = b;
  st.colorMaskRgb = (r || g || b);
  st.colorMaskAlpha = a;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetColorMaskExt(r, g, b, a);
  }
}

void SetStencilMode(SharedFrontendState& st, bool enabled) {
  st.stencilEnabled = enabled;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetStencilMode(enabled);
  }
}

void SetStencilFunc(SharedFrontendState& st, uint32_t compareOp, uint8_t refVal,
                    uint8_t readMask) {
  st.stencilCompareOp = compareOp & 0x7u;
  st.stencilRef = refVal;
  st.stencilReadMask = readMask;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetStencilFunc(st.stencilCompareOp, refVal, readMask);
  }
}

void SetStencilMask(SharedFrontendState& st, uint8_t writeMask) {
  st.stencilWriteMask = writeMask;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetStencilMask(writeMask);
  }
}

void SetStencilOp(SharedFrontendState& st, uint32_t failOp, uint32_t zFailOp,
                  uint32_t zPassOp) {
  st.stencilFailOp = failOp & 0x7u;
  st.stencilZFailOp = zFailOp & 0x7u;
  st.stencilZPassOp = zPassOp & 0x7u;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->SetStencilOp(st.stencilFailOp, st.stencilZFailOp,
                          st.stencilZPassOp);
  }
}

void SetDitherMode(SharedFrontendState& st, uint32_t mode) {
  st.ditherMode = mode;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
    b->SetDitherMode(ResolveEffectiveDither(mode, cfg));
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
  st.rgbBlendOp = 0;     // GR_BLEND_OP_ADD
  st.alphaSrcBlend = 4;  // GR_BLEND_ONE
  st.alphaDstBlend = 0;  // GR_BLEND_ZERO
  st.alphaBlendOp = 0;   // GR_BLEND_OP_ADD
  st.depthCompare = 1;   // GR_CMP_LESS / existing default
  st.depthMask = false;
  st.alphaTestOp = 7;    // GR_CMP_ALWAYS
  if (clearCachedFogMode) {
    st.fogMode = 0;      // GR_FOG_DISABLE
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    const bool auxEnabled = (st.effectiveAuxBuffers > 0);
    b->SetBlendState(st.rgbSrcBlend, st.rgbDstBlend, st.alphaSrcBlend,
                     st.alphaDstBlend);
    b->SetDepthState(auxEnabled ? st.depthMode : 0u, st.depthCompare,
                     auxEnabled ? st.depthMask : false, st.depthBias);
    b->SetAlphaTestState(st.alphaTestOp, st.alphaTestRef);
    b->SetFogMode(0);
    b->SetChromakeyMode(0);
  }
}

void LoadGammaTable(uint32_t nentries, uint32_t* red, uint32_t* green,
                    uint32_t* blue) {
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  if (cfg.HasGammaOverride()) {
    GLIDE_LOG(DEBUG, "Frontend",
              "App called grLoadGammaTable, preserving configured gamma "
              "override.");
    ApplyConfiguredGammaToBackend(BackendManager::GetInstance().GetBackend(),
                                  cfg);
    return;
  }
  if (!cfg.useAppGamma) {
    GLIDE_LOG(DEBUG, "Frontend",
              "Ignoring app grLoadGammaTable due to "
              "FX_GLIDE_USE_APP_GAMMA=0.");
    return;
  }
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->LoadGammaTable(nentries, red, green, blue);
  }
}

void GetGammaTable(uint32_t nentries, uint32_t* red, uint32_t* green,
                   uint32_t* blue) {
  if (nentries == 0 || (!red && !green && !blue)) return;
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    b->GetGammaTable(nentries, red, green, blue);
    return;
  }
  const uint32_t count = std::min(nentries, 256u);
  for (uint32_t i = 0; i < count; ++i) {
    if (red) red[i] = i;
    if (green) green[i] = i;
    if (blue) blue[i] = i;
  }
}

int EncodeRLE16(void* dst, const void* src, uint32_t width, uint32_t height) {
  if (!src || width == 0 || height == 0) return 0;
  int byteCount = 0;
  uint64_t remaining = static_cast<uint64_t>(width) * height;
  const uint16_t* srcPixels = reinterpret_cast<const uint16_t*>(src);
  uint32_t* dstPixels = reinterpret_cast<uint32_t*>(dst);

  while (remaining > 0) {
    uint16_t length = 1;
    const uint16_t color = *srcPixels;
    while (length < remaining && length < 0xFFFFu &&
           color == srcPixels[length]) {
      ++length;
    }
    if (dstPixels) {
      *dstPixels++ = (static_cast<uint32_t>(length) << 16) | color;
    }
    byteCount += 4;
    srcPixels += length;
    remaining -= length;
  }
  return byteCount;
}

void SyncTexturesBeforeDrawSlow() {
  auto& tm = TextureManager::GetInstance();
  if (tm.HasPendingTableSync()) {
    tm.SyncBoundTextures();
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
  if (srcFmt == 0x100u) return false;  // Reserve internal lossless format.
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
  config.pixelPipeline = LfbManager::GetInstance().IsPixelPipeline(buffer);
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
  if (dim <= 1) return 0x0;     // GR_LOD_LOG2_1
  if (dim <= 2) return 0x1;     // GR_LOD_LOG2_2
  if (dim <= 4) return 0x2;     // GR_LOD_LOG2_4
  if (dim <= 8) return 0x3;     // GR_LOD_LOG2_8
  if (dim <= 16) return 0x4;    // GR_LOD_LOG2_16
  if (dim <= 32) return 0x5;    // GR_LOD_LOG2_32
  if (dim <= 64) return 0x6;    // GR_LOD_LOG2_64
  if (dim <= 128) return 0x7;   // GR_LOD_LOG2_128
  if (dim <= 256) return 0x8;   // GR_LOD_LOG2_256
  if (dim <= 512) return 0x9;   // GR_LOD_LOG2_512
  if (dim <= 1024) return 0xa;  // GR_LOD_LOG2_1024
  return 0xb;                   // GR_LOD_LOG2_2048
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
  if (s_inAutoSplash) return false;
  const int mode = EmulationRegistry::GetInstance().GetConfig().shamelessPlugMode;
  if (mode > 0) return true;
  if (mode == 0) return false;
  return appEnabled;
}

void RenderShamelessPlug() {
  uint32_t scrWidth = 640;
  uint32_t scrHeight = 480;
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    scrWidth = backend->GetGuestWidth();
    scrHeight = backend->GetGuestHeight();
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

void ResetAutoSplashState() {
  s_autoSplashPlayed = false;
  s_inAutoSplash = false;
  NativeSurfaceHost::SetAutoSplashActive(false);
}

bool MaybeRunAutoStartupSplash(uint32_t width, uint32_t height,
                               void (*invokeSplash)(uint32_t, uint32_t)) {
  const auto& cfg = EmulationRegistry::GetInstance().GetConfig();
  if (s_autoSplashPlayed || cfg.noSplash || cfg.forceNoWindow || !invokeSplash ||
      width == 0u || height == 0u) {
    return false;
  }
  s_autoSplashPlayed = true;
  struct AutoSplashGuard {
    AutoSplashGuard() {
      s_inAutoSplash = true;
      NativeSurfaceHost::SetAutoSplashActive(true);
    }
    ~AutoSplashGuard() {
      s_inAutoSplash = false;
      NativeSurfaceHost::SetAutoSplashActive(false);
    }
  } splashGuard;
  GLIDE_LOG(INFO, "Frontend",
            "Playing automatic startup splash on grSstWinOpen ("
                << width << "x" << height
                << "; suppress with FX_GLIDE_NO_SPLASH=1 or GLIDE_NG_NO_SPLASH=1).");
  invokeSplash(width, height);
  if (auto* backend = BackendManager::GetInstance().GetBackend()) {
    backend->ReleaseThreadContext();
  }
  return true;
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
