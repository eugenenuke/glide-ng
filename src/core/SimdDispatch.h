#pragma once

#include <string>

#include "core/WrapperConfig.h"

namespace GlideWrapper {

// Detect highest x86 SIMD tier supported by BOTH host CPU and OS XSAVE state
// (Avx512 = AVX-512 F+DQ+BW+VL + AVX2+FMA, Avx2 = AVX2+FMA, else Scalar).
SwSimdLevel DetectHostMaxSimdLevel();

// Resolve a requested SwSimdLevel against host CPU/OS capabilities.
// Never returns SwSimdLevel::Auto. Populates warnOut if a requested tier had
// to fall back to a lower tier.
SwSimdLevel ResolveEffectiveSimdLevel(SwSimdLevel requested,
                                      std::string* warnOut = nullptr);

// Global active SIMD tier used by shared CPU routines (CpuLfbStaging,
// TextureManager, OpenGLESTextures) and SoftwareBackend.
void SetActiveSimdLevel(SwSimdLevel resolved);
SwSimdLevel GetActiveSimdLevel();

// Human-readable ISA description for startup banners & diagnostics.
const char* FormatResolvedSimdIsa(SwSimdLevel resolved);

}  // namespace GlideWrapper
