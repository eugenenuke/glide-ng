#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "core/WrapperConfig.h"

namespace GlideWrapper {

enum class VulkanDeviceKind : uint32_t {
  Other = 0,
  IntegratedGpu = 1,
  DiscreteGpu = 2,
  VirtualGpu = 3,
  Cpu = 4
};

struct VulkanDeviceCandidate {
  std::string name;
  VulkanDeviceKind kind{VulkanDeviceKind::Other};
  bool hasGraphicsQueue{false};
};

inline bool ContainsAsciiCaseInsensitive(const std::string& haystack,
                                         const std::string& needle) {
  if (needle.empty() || needle.size() > haystack.size()) {
    return false;
  }
  const auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
      });
  return it != haystack.end();
}

// Selects a graphics-capable Vulkan physical device index, or -1 if none
// satisfies the configured GPU policy. Default honors Vulkan loader / OS order
// (respecting MESA_VK_DEVICE_SELECT, VK_DRIVER_FILES, Optimus, OS settings).
inline int SelectVulkanDeviceCandidate(
    const std::vector<VulkanDeviceCandidate>& candidates, GpuPreference pref,
    const std::string& nameMatch) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto& c = candidates[i];
    if (!c.hasGraphicsQueue) continue;
    switch (pref) {
      case GpuPreference::Default:
        return static_cast<int>(i);
      case GpuPreference::Discrete:
        if (c.kind == VulkanDeviceKind::DiscreteGpu) {
          return static_cast<int>(i);
        }
        break;
      case GpuPreference::Integrated:
        if (c.kind == VulkanDeviceKind::IntegratedGpu) {
          return static_cast<int>(i);
        }
        break;
      case GpuPreference::LlvmPipe:
        if (c.kind == VulkanDeviceKind::Cpu) {
          return static_cast<int>(i);
        }
        break;
      case GpuPreference::NameMatch:
        if (ContainsAsciiCaseInsensitive(c.name, nameMatch)) {
          return static_cast<int>(i);
        }
        break;
    }
  }
  return -1;
}

inline bool IsSoftwareGlRenderer(const char* rendererRaw) {
  if (!rendererRaw) return false;
  const std::string renderer(rendererRaw);
  return ContainsAsciiCaseInsensitive(renderer, "llvmpipe") ||
         ContainsAsciiCaseInsensitive(renderer, "softpipe") ||
         ContainsAsciiCaseInsensitive(renderer, "swrast") ||
         ContainsAsciiCaseInsensitive(renderer, "swiftshader") ||
         ContainsAsciiCaseInsensitive(renderer, "gdi generic") ||
         ContainsAsciiCaseInsensitive(renderer, "microsoft basic render");
}

inline VulkanDeviceKind ClassifyExplicitGlHardwareKind(
    const std::string& renderer, const std::string& vendor) {
  const std::string combined = vendor + " " + renderer;
  if (ContainsAsciiCaseInsensitive(combined, "uhd graphics") ||
      ContainsAsciiCaseInsensitive(combined, "iris") ||
      ContainsAsciiCaseInsensitive(combined, "hd graphics") ||
      ContainsAsciiCaseInsensitive(combined, "radeon 610m") ||
      ContainsAsciiCaseInsensitive(combined, "radeon 680m") ||
      ContainsAsciiCaseInsensitive(combined, "radeon 780m") ||
      ContainsAsciiCaseInsensitive(combined, "radeon 880m") ||
      ContainsAsciiCaseInsensitive(combined, "radeon 890m") ||
      ContainsAsciiCaseInsensitive(combined, "radeon graphics") ||
      ContainsAsciiCaseInsensitive(combined, "raphael") ||
      ContainsAsciiCaseInsensitive(combined, "renoir") ||
      ContainsAsciiCaseInsensitive(combined, "cezanne") ||
      ContainsAsciiCaseInsensitive(combined, "phoenix") ||
      ContainsAsciiCaseInsensitive(combined, "vega")) {
    return VulkanDeviceKind::IntegratedGpu;
  }
  if (ContainsAsciiCaseInsensitive(combined, "geforce") ||
      ContainsAsciiCaseInsensitive(combined, "quadro") ||
      ContainsAsciiCaseInsensitive(combined, "rtx ") ||
      ContainsAsciiCaseInsensitive(combined, "gtx ") ||
      ContainsAsciiCaseInsensitive(combined, "radeon rx") ||
      ContainsAsciiCaseInsensitive(combined, "radeon pro")) {
    return VulkanDeviceKind::DiscreteGpu;
  }
  return VulkanDeviceKind::Other;
}

inline bool ValidateGlesRendererForGpuPreference(const char* rendererRaw,
                                                 const char* vendorRaw,
                                                 GpuPreference pref,
                                                 const std::string& nameMatch,
                                                 std::string& outReason) {
  outReason.clear();
  if (pref == GpuPreference::Default) {
    return true;
  }

  const std::string renderer = rendererRaw ? rendererRaw : "";
  const std::string vendor = vendorRaw ? vendorRaw : "";
  const bool isSw = IsSoftwareGlRenderer(rendererRaw);

  if (pref == GpuPreference::LlvmPipe) {
    if (isSw) return true;
    outReason = "Requested gpu='llvmpipe', but OpenGL ES/GL resolved to '" +
                renderer + "' (" + vendor +
                "). Set LIBGL_ALWAYS_SOFTWARE=1 and "
                "__EGL_VENDOR_LIBRARY_FILENAMES="
                "/usr/share/glvnd/egl_vendor.d/50_mesa.json "
                "(or pass --gpu=llvmpipe to glide-dosbox).";
    return false;
  }

  if (pref == GpuPreference::Discrete || pref == GpuPreference::Integrated) {
    const char* reqStr =
        (pref == GpuPreference::Discrete) ? "discrete" : "integrated";
    if (isSw) {
      outReason = "Requested gpu='" + std::string(reqStr) +
                  "', but OpenGL ES/GL resolved to software renderer '" +
                  renderer + "' (" + vendor + ").";
      return false;
    }
    const VulkanDeviceKind detected =
        ClassifyExplicitGlHardwareKind(renderer, vendor);
    if (pref == GpuPreference::Discrete &&
        detected == VulkanDeviceKind::IntegratedGpu) {
      outReason =
          "Requested gpu='discrete', but OpenGL ES/GL resolved to integrated "
          "adapter '" +
          renderer + "' (" + vendor +
          "). Set __NV_PRIME_RENDER_OFFLOAD=1 + "
          "__EGL_VENDOR_LIBRARY_FILENAMES="
          "/usr/share/glvnd/egl_vendor.d/10_nvidia.json or DRI_PRIME=1.";
      return false;
    }
    if (pref == GpuPreference::Integrated &&
        detected == VulkanDeviceKind::DiscreteGpu) {
      outReason =
          "Requested gpu='integrated', but OpenGL ES/GL resolved to discrete "
          "adapter '" +
          renderer + "' (" + vendor +
          "). Set __EGL_VENDOR_LIBRARY_FILENAMES="
          "/usr/share/glvnd/egl_vendor.d/50_mesa.json and DRI_PRIME=0.";
      return false;
    }
    return true;
  }

  if (pref == GpuPreference::NameMatch) {
    if (ContainsAsciiCaseInsensitive(renderer, nameMatch) ||
        ContainsAsciiCaseInsensitive(vendor, nameMatch)) {
      return true;
    }
    outReason = "OpenGL ES/GL adapter '" + renderer + "' (" + vendor +
                ") does not match gpu='name:" + nameMatch + "'.";
    return false;
  }

  return true;
}

}  // namespace GlideWrapper
