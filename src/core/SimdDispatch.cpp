#include "core/SimdDispatch.h"

#include <atomic>
#include <cstdint>

#if (defined(__x86_64__) || defined(__i386__)) && \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#define GLIDE_HAS_X86_CPUID 1
#else
#define GLIDE_HAS_X86_CPUID 0
#endif

namespace GlideWrapper {

namespace {

#if GLIDE_HAS_X86_CPUID
inline uint64_t ReadXcr0() {
  uint32_t eax = 0;
  uint32_t edx = 0;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0u));
  return (static_cast<uint64_t>(edx) << 32) | static_cast<uint64_t>(eax);
}
#endif

SwSimdLevel ProbeHostSimdOnce() {
#if GLIDE_HAS_X86_CPUID
  const unsigned int maxLeaf = __get_cpuid_max(0, nullptr);
  if (maxLeaf < 7u) return SwSimdLevel::Scalar;

  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
  __cpuid(1, eax, ebx, ecx, edx);

  const bool hasOsXsave = (ecx & (1u << 27)) != 0u;
  const bool hasAvx = (ecx & (1u << 28)) != 0u;
  const bool hasFma = (ecx & (1u << 12)) != 0u;
  if (!hasOsXsave || !hasAvx || !hasFma) {
    return SwSimdLevel::Scalar;
  }

  const uint64_t xcr0 = ReadXcr0();
  const bool osAvxState = (xcr0 & 0x06u) == 0x06u;  // XMM + YMM
  if (!osAvxState) {
    return SwSimdLevel::Scalar;
  }

  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  const bool hasAvx2 = (ebx & (1u << 5)) != 0u;
  if (!hasAvx2) {
    return SwSimdLevel::Scalar;
  }

  const bool osAvx512State = (xcr0 & 0xE6u) == 0xE6u;  // OPMASK + ZMM + YMM + XMM
  const bool hasAvx512F = (ebx & (1u << 16)) != 0u;
  const bool hasAvx512DQ = (ebx & (1u << 17)) != 0u;
  const bool hasAvx512BW = (ebx & (1u << 30)) != 0u;
  const bool hasAvx512VL = (ebx & (1u << 31)) != 0u;
  if (osAvx512State && hasAvx512F && hasAvx512DQ && hasAvx512BW &&
      hasAvx512VL) {
    return SwSimdLevel::Avx512;
  }

  return SwSimdLevel::Avx2;
#else
  return SwSimdLevel::Scalar;
#endif
}

std::atomic<uint32_t> g_activeSimdLevel{0xFFFFFFFFu};

}  // namespace

SwSimdLevel DetectHostMaxSimdLevel() {
  static const SwSimdLevel kHostMax = ProbeHostSimdOnce();
  return kHostMax;
}

SwSimdLevel ResolveEffectiveSimdLevel(SwSimdLevel requested,
                                      std::string* warnOut) {
  const SwSimdLevel hostMax = DetectHostMaxSimdLevel();
  if (warnOut) warnOut->clear();

  switch (requested) {
    case SwSimdLevel::Scalar:
      return SwSimdLevel::Scalar;
    case SwSimdLevel::Auto:
      return hostMax;
    case SwSimdLevel::Avx2:
      if (hostMax >= SwSimdLevel::Avx2) return SwSimdLevel::Avx2;
      if (warnOut) {
        *warnOut =
            "Requested Software SIMD level 'avx2', but host CPU/OS lacks "
            "AVX2+FMA support; falling back to scalar.";
      }
      return SwSimdLevel::Scalar;
    case SwSimdLevel::Avx512:
      if (hostMax >= SwSimdLevel::Avx512) return SwSimdLevel::Avx512;
      if (hostMax >= SwSimdLevel::Avx2) {
        if (warnOut) {
          *warnOut =
              "Requested Software SIMD level 'avx512', but host CPU/OS lacks "
              "AVX-512 (F/DQ/BW/VL) support; falling back to AVX2+FMA.";
        }
        return SwSimdLevel::Avx2;
      }
      if (warnOut) {
        *warnOut =
            "Requested Software SIMD level 'avx512', but host CPU/OS lacks "
            "AVX-512 and AVX2+FMA support; falling back to scalar.";
      }
      return SwSimdLevel::Scalar;
  }
  return (hostMax >= SwSimdLevel::Avx2) ? SwSimdLevel::Avx2
                                        : SwSimdLevel::Scalar;
}

void SetActiveSimdLevel(SwSimdLevel resolved) {
  const SwSimdLevel safe = ResolveEffectiveSimdLevel(resolved, nullptr);
  g_activeSimdLevel.store(static_cast<uint32_t>(safe),
                          std::memory_order_relaxed);
}

SwSimdLevel GetActiveSimdLevel() {
  const uint32_t cur = g_activeSimdLevel.load(std::memory_order_relaxed);
  if (cur != 0xFFFFFFFFu) {
    return static_cast<SwSimdLevel>(cur);
  }
  // Default before config load: AVX2 (with host fallback)
  const SwSimdLevel def = ResolveEffectiveSimdLevel(SwSimdLevel::Avx2, nullptr);
  g_activeSimdLevel.store(static_cast<uint32_t>(def),
                          std::memory_order_relaxed);
  return def;
}

const char* FormatResolvedSimdIsa(SwSimdLevel resolved) {
  switch (resolved) {
    case SwSimdLevel::Avx512:
      return "AVX-512 (F/DQ/BW/VL)";
    case SwSimdLevel::Avx2:
      return "AVX2+FMA";
    case SwSimdLevel::Scalar:
    case SwSimdLevel::Auto:
    default:
      return "Scalar";
  }
}

}  // namespace GlideWrapper
