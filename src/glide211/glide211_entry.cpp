/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng Glide 2.11 (glide.dll / libglide.so) Frontend Entry Points
 *
 * Translates the pre-2.3 SST-1 ABI (grSstOpen, grSstPassthruMode, legacy
 * fixed-1024-pixel-stride grLfbBegin/End/WriteMode/Origin/BypassMode/
 * GetReadPtr/GetWritePtr, guFbReadRegion/WriteRegion) onto GlideWrapperCore
 * while sharing the common Glide 2.x pipeline, texture, and gu* entry points.
 *
 * Defaults to authentic Voodoo Graphics (SST-1, 4 MB FB, 1x 2 MB TMU) with
 * API version 2.11 unless overridden via glide-ng.ini or GLIDE_NG_* env vars.
 */

#define GLIDE_NG_FRONTEND_211 1

#include <glide.h>

#include "../glide2/glide2_entry.cpp"

namespace {

// Glide 2.11 legacy LFB & pass-through state
GrLfbWriteMode_t s_lfb211WriteMode = GR_LFBWRITEMODE_565;
GrOriginLocation_t s_lfb211Origin = GR_ORIGIN_UPPER_LEFT;
GrLfbBypassMode_t s_lfb211BypassMode = GR_LFBBYPASS_ENABLE;
GrBuffer_t s_lfb211ReadBuffer = GR_BUFFER_BACKBUFFER;
GrBuffer_t s_lfb211WriteBuffer = GR_BUFFER_BACKBUFFER;
bool s_lfb211WriteLockActive = false;
bool s_lfb211ReadLockActive = false;
bool s_lfb211InBegin = false;
void* s_lfb211WritePtr = nullptr;
GrLfbWriteMode_t s_lfb211LockedWriteMode = GR_LFBWRITEMODE_565;
GrOriginLocation_t s_lfb211LockedOrigin = GR_ORIGIN_UPPER_LEFT;
GrLfbBypassMode_t s_lfb211LockedBypass = GR_LFBBYPASS_ENABLE;
GrBuffer_t s_lfb211LockedWriteBuf = GR_BUFFER_BACKBUFFER;
GrPassthruMode_t s_passthruMode = GR_PASSTHRU_SHOW_SST1;

void ResetGlide211State(GrOriginLocation_t defaultOrigin) {
  s_lfb211WriteMode = GR_LFBWRITEMODE_565;
  s_lfb211Origin = defaultOrigin;
  s_lfb211BypassMode = GR_LFBBYPASS_ENABLE;
  s_lfb211ReadBuffer = GR_BUFFER_BACKBUFFER;
  s_lfb211WriteBuffer = GR_BUFFER_BACKBUFFER;
  s_lfb211WriteLockActive = false;
  s_lfb211ReadLockActive = false;
  s_lfb211InBegin = false;
  s_lfb211WritePtr = nullptr;
  s_lfb211LockedWriteMode = GR_LFBWRITEMODE_565;
  s_lfb211LockedOrigin = defaultOrigin;
  s_lfb211LockedBypass = GR_LFBBYPASS_ENABLE;
  s_lfb211LockedWriteBuf = GR_BUFFER_BACKBUFFER;
  s_passthruMode = GR_PASSTHRU_SHOW_SST1;
}

uint32_t WriteModeToSrcFmt(GrLfbWriteMode_t mode) {
  switch (mode) {
    case GR_LFBWRITEMODE_565:        return GR_LFB_SRC_FMT_565;
    case GR_LFBWRITEMODE_555:        return GR_LFB_SRC_FMT_555;
    case GR_LFBWRITEMODE_1555:       return GR_LFB_SRC_FMT_1555;
    case GR_LFBWRITEMODE_888:        return GR_LFB_SRC_FMT_888;
    case GR_LFBWRITEMODE_8888:       return GR_LFB_SRC_FMT_8888;
    case GR_LFBWRITEMODE_565_DEPTH:  return GR_LFB_SRC_FMT_565_DEPTH;
    case GR_LFBWRITEMODE_555_DEPTH:  return GR_LFB_SRC_FMT_555_DEPTH;
    case GR_LFBWRITEMODE_1555_DEPTH: return GR_LFB_SRC_FMT_1555_DEPTH;
    case GR_LFBWRITEMODE_ZA16:       return GR_LFB_SRC_FMT_ZA16;
    default:                         return GR_LFB_SRC_FMT_565;
  }
}

uint32_t WriteModePixelSize(GrLfbWriteMode_t mode) {
  switch (mode) {
    case GR_LFBWRITEMODE_888:
    case GR_LFBWRITEMODE_8888:
    case GR_LFBWRITEMODE_565_DEPTH:
    case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555_DEPTH:
      return 4u;
    default:
      return 2u;
  }
}

void FlushGlide211WriteLockIfActive() {
  if (!s_lfb211WriteLockActive) return;
  if (!GlideWrapper::LfbManager::GetInstance().IsWriteLocked()) {
    s_lfb211WriteLockActive = false;
    s_lfb211WritePtr = nullptr;
    return;
  }
  // Flush modified bands immediately while keeping the staging pointer valid
  // across subsequent grLfbBegin()/grLfbEnd() pairs (authentic SST-1 BAR reuse).
  EntryCommon::FlushActiveLfbWriteLock(s_state);
}

void CommitGlide211WriteLockIfActive() {
  if (!s_lfb211WriteLockActive) return;
  if (GlideWrapper::LfbManager::GetInstance().IsWriteLocked()) {
    EntryCommon::LfbUnlockWithState(s_state, GR_LFB_WRITE_ONLY,
                                    s_lfb211LockedWriteBuf);
  }
  s_lfb211WriteLockActive = false;
  s_lfb211WritePtr = nullptr;
}

void UpdateGlide211WriteLockModeIfActive() {
  if (!s_lfb211WriteLockActive) return;
  auto& lfb = GlideWrapper::LfbManager::GetInstance();
  if (!lfb.IsWriteLocked()) {
    s_lfb211WriteLockActive = false;
    s_lfb211WritePtr = nullptr;
    return;
  }
  if (s_lfb211LockedWriteMode == s_lfb211WriteMode &&
      s_lfb211LockedOrigin == s_lfb211Origin &&
      s_lfb211LockedBypass == s_lfb211BypassMode) {
    return;
  }
  // On SST-1 hardware, grLfbGetWritePtr() returns a fixed PCI BAR0 aperture
  // address that games cache once at startup (e.g. MechWarrior 2 3Dfx Edition).
  // Reconfigure the active staging lock in-place after flushing dirty bands
  // under the previous mode so cached pointers remain valid across subsequent
  // grLfbWriteMode/grLfbOrigin/grLfbBypassMode calls of matching pixel width.
  GlideWrapper::LfbPipelineConfig cfg;
  cfg.pixelPipeline = (s_lfb211LockedBypass == GR_LFBBYPASS_DISABLE);
  cfg.chromakeyEnabled = (s_state.chromakeyMode != 0);
  cfg.chromakeyValue = s_state.chromakeyValue;
  cfg.constantAlpha = s_state.lfbConstantAlpha;
  cfg.rgbSrcBlend = s_state.rgbSrcBlend;
  cfg.rgbDstBlend = s_state.rgbDstBlend;
  const bool newPixelPipeline = (s_lfb211BypassMode == GR_LFBBYPASS_DISABLE);
  if (lfb.ReconfigureActiveWriteLock(s_lfb211LockedWriteBuf, s_lfb211WriteMode,
                                     s_lfb211Origin, newPixelPipeline, cfg)) {
    s_lfb211LockedWriteMode = s_lfb211WriteMode;
    s_lfb211LockedOrigin = s_lfb211Origin;
    s_lfb211LockedBypass = s_lfb211BypassMode;
    return;
  }
  CommitGlide211WriteLockIfActive();
}

void ReleaseGlide211ReadLockIfActive() {
  if (!s_lfb211ReadLockActive) return;
  EntryCommon::LfbUnlockWithState(s_state, GR_LFB_READ_ONLY,
                                  s_lfb211ReadBuffer);
  s_lfb211ReadLockActive = false;
}

}  // namespace

extern "C" {

FX_ENTRY FxBool FX_CALL grSstOpen(GrScreenResolution_t screen_resolution,
                                  GrScreenRefresh_t refresh_rate,
                                  GrColorFormat_t color_format,
                                  GrOriginLocation_t origin_location,
                                  GrSmoothingMode_t smoothing_filter,
                                  int num_buffers) {
  EnsureEngineInitialized();
  GLIDE_LOG(INFO, "Frontend",
            "grSstOpen (Glide 2.11) invoked. Res="
                << screen_resolution << ", Refresh=" << refresh_rate
                << ", ColorFmt=" << color_format
                << ", Origin=" << origin_location
                << ", Smoothing=" << smoothing_filter
                << ", Buffers=" << num_buffers);

  ResetGlide211State(origin_location);
  const int nColBuffers = (num_buffers >= 2) ? num_buffers : 2;
  // Glide 2.11 implicitly allocates an auxiliary depth/alpha buffer on SST-1
  // unless triple buffering fills a 2 MB FB (our ApplyPostWinOpenOverrides
  // still honors explicit alloc_aux=0 overrides if requested).
  const int nAuxBuffers = 1;
  return grSstWinOpen(0, screen_resolution, refresh_rate, color_format,
                      origin_location, nColBuffers, nAuxBuffers);
}

FX_ENTRY void FX_CALL grSstPassthruMode(GrPassthruMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::State);
  EnsureEngineInitialized();
  GLIDE_LOG(INFO, "Frontend",
            "grSstPassthruMode (Glide 2.11) invoked: mode="
                << (mode == GR_PASSTHRU_SHOW_VGA ? "SHOW_VGA (0)"
                                                 : "SHOW_SST1 (1)"));
  FlushGlide211WriteLockIfActive();
  s_passthruMode = mode;
  if (auto* b =
          GlideWrapper::BackendManager::GetInstance().GetBackend()) {
    b->SetPassthruMode(mode != GR_PASSTHRU_SHOW_VGA);
  }
}

FX_ENTRY void FX_CALL grLfbWriteMode(GrLfbWriteMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbWriteMode (Glide 2.11): mode=" << mode);
  s_lfb211WriteMode = mode;
  UpdateGlide211WriteLockModeIfActive();
}

FX_ENTRY void FX_CALL grLfbOrigin(GrOriginLocation_t origin) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbOrigin (Glide 2.11): origin=" << origin);
  s_lfb211Origin = origin;
  UpdateGlide211WriteLockModeIfActive();
}

FX_ENTRY void FX_CALL grLfbBypassMode(GrLfbBypassMode_t mode) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbBypassMode (Glide 2.11): mode=" << mode);
  s_lfb211BypassMode = mode;
  UpdateGlide211WriteLockModeIfActive();
}

FX_ENTRY void FX_CALL grLfbBegin(void) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbBegin (Glide 2.11) invoked.");
  s_lfb211InBegin = true;
}

FX_ENTRY void FX_CALL grLfbEnd(void) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend", "grLfbEnd (Glide 2.11) invoked.");
  FlushGlide211WriteLockIfActive();
  ReleaseGlide211ReadLockIfActive();
  s_lfb211InBegin = false;
}

FX_ENTRY const void* FX_CALL grLfbGetReadPtr(GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbGetReadPtr (Glide 2.11): buffer=" << buffer);
  // Flush any prior LFB write lock so readback sees the latest writes while
  // keeping any cached grLfbGetWritePtr() pointer valid.
  FlushGlide211WriteLockIfActive();
  ReleaseGlide211ReadLockIfActive();

  s_lfb211ReadBuffer = buffer;
  // Glide 2.11 read pointers always present 16-bit pixels (RGB565 for color
  // buffers, 16-bit depth for aux/depth) at fixed 1024-pixel (2048-byte) stride.
  const GrLfbWriteMode_t readFmt =
      (buffer == GR_BUFFER_AUXBUFFER || buffer == GR_BUFFER_DEPTHBUFFER)
          ? GR_LFBWRITEMODE_ZA16
          : GR_LFBWRITEMODE_565;

  GrLfbInfo_t info{};
  info.size = sizeof(info);
  const bool ok = GlideWrapper::LfbManager::GetInstance().Lock(
      GR_LFB_READ_ONLY, buffer, readFmt, s_lfb211Origin,
      /*pixelPipeline=*/false, &info, GR_LFB_STRIDE_PIXELS_211);
  if (!ok) return nullptr;
  s_lfb211ReadLockActive = true;
  return info.lfbPtr;
}

FX_ENTRY void* FX_CALL grLfbGetWritePtr(GrBuffer_t buffer) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  GLIDE_LOG(DEBUG, "Frontend",
            "grLfbGetWritePtr (Glide 2.11): buffer="
                << buffer << ", writeMode=" << s_lfb211WriteMode
                << ", origin=" << s_lfb211Origin
                << ", bypass=" << s_lfb211BypassMode);

  s_lfb211WriteBuffer = buffer;

  // Reuse active fixed-1024-stride staging lock if parameters are unchanged.
  if (s_lfb211WriteLockActive && s_lfb211WritePtr &&
      s_lfb211LockedWriteBuf == buffer &&
      s_lfb211LockedWriteMode == s_lfb211WriteMode &&
      s_lfb211LockedOrigin == s_lfb211Origin &&
      s_lfb211LockedBypass == s_lfb211BypassMode &&
      GlideWrapper::LfbManager::GetInstance().IsWriteLocked()) {
    return s_lfb211WritePtr;
  }

  CommitGlide211WriteLockIfActive();

  const bool pixelPipeline = (s_lfb211BypassMode == GR_LFBBYPASS_DISABLE);
  GrLfbInfo_t info{};
  info.size = sizeof(info);
  const bool ok = GlideWrapper::LfbManager::GetInstance().Lock(
      GR_LFB_WRITE_ONLY, buffer, s_lfb211WriteMode, s_lfb211Origin,
      pixelPipeline, &info, GR_LFB_STRIDE_PIXELS_211);
  if (!ok) return nullptr;

  s_lfb211WriteLockActive = true;
  s_lfb211WritePtr = info.lfbPtr;
  s_lfb211LockedWriteBuf = buffer;
  s_lfb211LockedWriteMode = s_lfb211WriteMode;
  s_lfb211LockedOrigin = s_lfb211Origin;
  s_lfb211LockedBypass = s_lfb211BypassMode;
  return info.lfbPtr;
}

FX_ENTRY void FX_CALL guFbReadRegion(const int src_x, const int src_y,
                                     const int w, const int h, const void* dst,
                                     const int strideInBytes) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  if (w <= 0 || h <= 0 || !dst || src_x < 0 || src_y < 0 ||
      strideInBytes < w * 2) {
    return;
  }
  FlushGlide211WriteLockIfActive();

  // Glide 2.11 guFbReadRegion always outputs 16-bit RGB565 pixels. Read into a
  // tight w*2 staging buffer first so CpuLfbStaging never misclassifies a
  // wide caller stride (e.g. strideInBytes >= w*4) as 32-bit BGRA.
  const uint32_t screenH = s_state.screenHeight;
  const uint32_t topY =
      (s_lfb211Origin == GR_ORIGIN_LOWER_LEFT &&
       static_cast<uint32_t>(src_y + h) <= screenH)
          ? (screenH - static_cast<uint32_t>(src_y + h))
          : static_cast<uint32_t>(src_y);
  const uint32_t packedStride = static_cast<uint32_t>(w) * 2u;
  std::vector<uint8_t> packed(static_cast<size_t>(packedStride) * h, 0u);
  if (!EntryCommon::LfbReadRegion(
          s_lfb211ReadBuffer, static_cast<uint32_t>(src_x), topY,
          static_cast<uint32_t>(w), static_cast<uint32_t>(h), packedStride,
          packed.data())) {
    return;
  }

  auto* dstBytes = static_cast<uint8_t*>(const_cast<void*>(dst));
  for (int y = 0; y < h; ++y) {
    const int srcRow =
        (s_lfb211Origin == GR_ORIGIN_LOWER_LEFT) ? (h - 1 - y) : y;
    std::memcpy(dstBytes + static_cast<size_t>(y) * strideInBytes,
                packed.data() + static_cast<size_t>(srcRow) * packedStride,
                packedStride);
  }
}

FX_ENTRY void FX_CALL guFbWriteRegion(const int dst_x, const int dst_y,
                                      const int w, const int h, const void* src,
                                      const int strideInBytes) {
  GLIDE_CPU_SCOPE(GlideWrapper::CpuBucket::Lfb);
  EnsureEngineInitialized();
  if (w <= 0 || h <= 0 || !src || dst_x < 0 || dst_y < 0) {
    return;
  }
  FlushGlide211WriteLockIfActive();

  const uint32_t srcFmt = WriteModeToSrcFmt(s_lfb211WriteMode);
  const uint32_t bpp = WriteModePixelSize(s_lfb211WriteMode);
  if (s_lfb211Origin == GR_ORIGIN_LOWER_LEFT &&
      static_cast<uint32_t>(dst_y + h) <= s_state.screenHeight) {
    const uint32_t rowBytes = static_cast<uint32_t>(w) * bpp;
    std::vector<uint8_t> flipped(static_cast<size_t>(rowBytes) * h);
    const auto* srcBytes = static_cast<const uint8_t*>(src);
    for (int y = 0; y < h; ++y) {
      std::memcpy(flipped.data() + static_cast<size_t>(h - 1 - y) * rowBytes,
                  srcBytes + static_cast<size_t>(y) * strideInBytes, rowBytes);
    }
    const uint32_t topY = s_state.screenHeight - static_cast<uint32_t>(dst_y + h);
    EntryCommon::LfbWriteRegion(s_lfb211WriteBuffer,
                                static_cast<uint32_t>(dst_x), topY, srcFmt,
                                static_cast<uint32_t>(w),
                                static_cast<uint32_t>(h),
                                static_cast<int32_t>(rowBytes), flipped.data());
  } else {
    EntryCommon::LfbWriteRegion(
        s_lfb211WriteBuffer, static_cast<uint32_t>(dst_x),
        static_cast<uint32_t>(dst_y), srcFmt, static_cast<uint32_t>(w),
        static_cast<uint32_t>(h), static_cast<int32_t>(strideInBytes),
        const_cast<void*>(src));
  }
}

}  // extern "C"
