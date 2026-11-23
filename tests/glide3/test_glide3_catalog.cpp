// Auto-modularized Glide 3.x Catch2 test suite unit.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <type_traits>
#include <vector>

#include <SDL2/SDL.h>

#include "build_version.h"
#include "backends/GlideStateTracker.h"
#include "core/BackendManager.h"
#include "core/GlideEntryCommon.h"
#include "core/HudOverlay.h"
#include "core/IniConfigLoader.h"
#include "core/Logger.h"
#include "core/NativeSurfaceHost.h"
#include "core/SimdDispatch.h"
#include "core/SimdKernels.h"
#include "core/Telemetry.h"
#include "core/TextureManager.h"
#include "core/VertexLayout.h"
#include "core/WrapperConfig.h"
#include "common/test_helpers.h"

#ifdef __linux__
#include <GLES3/gl32.h>
#endif
#include <glide.h>

namespace {
struct StandardVert {
  float x, y;     // 0, 4
  float z;        // 8
  float r, g, b;  // 12, 16, 20
  float ooz;      // 24
  float a;        // 28
  float oow;      // 32
  float s, t;     // 36, 40
  float w;        // 44
};

[[maybe_unused]] inline void* OpenGlide3Handle() {
  return GlideTestHelpers::OpenGlideSelfHandle();
}
[[maybe_unused]] inline void* OpenGlide3DsoHandle() {
  return GlideTestHelpers::OpenGlideLibrary("libglide3x.so");
}
[[maybe_unused]] inline void* OpenGlide2DsoHandle() {
  return GlideTestHelpers::OpenGlideLibrary("libglide2x.so");
}
}  // namespace

TEST_CASE("Catalog: Compatibility INI Presets & Directory Integrity", "[Catalog][Config]") {
  namespace fs = std::filesystem;

  // Locate repository root (prefers CMake-injected GLIDE_REPO_ROOT, with CWD fallbacks)
  fs::path repoRoot;
  std::vector<fs::path> candidates;
#ifdef GLIDE_REPO_ROOT
  candidates.emplace_back(GLIDE_REPO_ROOT);
#endif
  candidates.emplace_back(fs::current_path());
  candidates.emplace_back(fs::current_path().parent_path());
  candidates.emplace_back(fs::current_path().parent_path().parent_path());
  for (const auto& c : candidates) {
    if (fs::exists(c / "docs" / "catalog" / "README.md")) {
      repoRoot = c;
      break;
    }
  }
  REQUIRE(!repoRoot.empty());

  const fs::path catalogDir = repoRoot / "docs" / "catalog";
  REQUIRE(fs::is_directory(catalogDir));

  // Temporarily isolate environment variables so ambient test runner settings
  // (e.g. GLIDE_NG_CONFIG, GLIDE_NG_CARD_MODEL, GLIDE_NG_SW_SIMD) cannot mask
  // or distort INI preset validation, then restore unconditionally on exit.
  struct ScopedEnvUnset {
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> saved;
    GlideWrapper::SwSimdLevel savedSimd;
    explicit ScopedEnvUnset(const std::vector<const char*>& names)
        : savedSimd(GlideWrapper::GetActiveSimdLevel()) {
      for (const char* name : names) {
        const char* val = std::getenv(name);
        if (val) {
          saved.push_back({name, {true, std::string(val)}});
          ::unsetenv(name);
        } else {
          saved.push_back({name, {false, ""}});
        }
      }
    }
    ~ScopedEnvUnset() {
      for (const auto& kv : saved) {
        if (kv.second.first) {
          ::setenv(kv.first.c_str(), kv.second.second.c_str(), 1);
        } else {
          ::unsetenv(kv.first.c_str());
        }
      }
      GlideWrapper::SetActiveSimdLevel(savedSimd);
    }
  };

  ScopedEnvUnset envGuard({
      "GLIDE_NG_CONFIG",
      "GLIDE_NG_BACKEND",
      "GLIDE_NG_CARD_MODEL",
      "GLIDE_NG_API_VERSION",
      "GLIDE_NG_MAX_FPS",
      "GLIDE_NG_VSYNC",
      "GLIDE_NG_SW_SIMD",
      "GLIDE_NG_NO_SPLASH",
      "FX_GLIDE_SWAPINTERVAL",
      "FX_GLIDE_NO_SPLASH",
      "SST_GAMMA",
      "SSTV2_GAMMA",
  });

  // 1. Verify every title directory under games/, demos/, and tools/ has both
  //    README.md and glide-ng.ini.
  size_t titleDirCount = 0;
  for (const char* bucket : {"games", "demos", "tools"}) {
    const fs::path bucketDir = catalogDir / bucket;
    REQUIRE(fs::is_directory(bucketDir));
    for (const auto& entry : fs::directory_iterator(bucketDir)) {
      if (!entry.is_directory()) continue;
      ++titleDirCount;
      INFO("Catalog title dir: " << entry.path().string());
      CHECK(fs::is_regular_file(entry.path() / "README.md"));
      CHECK(fs::is_regular_file(entry.path() / "glide-ng.ini"));
    }
  }
  CHECK(titleDirCount >= 15u);

  // 2. Discover every glide-ng*.ini under docs/catalog/ plus root glide-ng.ini.example
  std::vector<fs::path> iniFiles;
  for (const auto& entry : fs::recursive_directory_iterator(catalogDir)) {
    if (!entry.is_regular_file()) continue;
    const std::string fname = entry.path().filename().string();
    if (fname.rfind("glide-ng", 0) == 0 && entry.path().extension() == ".ini") {
      iniFiles.push_back(entry.path());
    }
  }
  const fs::path exampleIni = repoRoot / "glide-ng.ini.example";
  REQUIRE(fs::is_regular_file(exampleIni));
  iniFiles.push_back(exampleIni);
  std::sort(iniFiles.begin(), iniFiles.end());

  REQUIRE(iniFiles.size() >= 24u);

  GlideWrapper::IniConfigLoader loader;
  for (const auto& iniPath : iniFiles) {
    GlideWrapper::WrapperConfig cfg;
    INFO("Validating preset: " << iniPath.string());
    REQUIRE(loader.Load(iniPath.string(), cfg));
    CHECK(cfg.configFileLoaded);
    if (!cfg.unknownConfigKeys.empty()) {
      UNSCOPED_INFO("Unknown keys: " << cfg.unknownConfigKeys[0]);
    }
    if (!cfg.invalidConfigValues.empty()) {
      UNSCOPED_INFO("Invalid values: " << cfg.invalidConfigValues[0]);
    }
    if (!cfg.compatibilityWarnings.empty()) {
      UNSCOPED_INFO("Compat warning: " << cfg.compatibilityWarnings[0]);
    }
    CHECK(cfg.unknownConfigKeys.empty());
    CHECK(cfg.invalidConfigValues.empty());
    CHECK(cfg.compatibilityWarnings.empty());
  }

  // 3. Spot-check semantic intent of non-default title presets
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "demos" / "3dfx-demo-room" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(std::fabs(cfg.gammaOverride - 1.7f) < 0.001f);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "demos" / "3dfx-kitty-hawk" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "tools" / "glide-bench" / "glide-ng.voodoo5.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.vsync == false);
    CHECK(cfg.maxFps == 0.0f);
    CHECK(cfg.noSplash == true);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "tomb-raider" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.gammaOverride <= 0.0f);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "tomb-raider" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.forceTrilinear == false);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.gammaOverride <= 0.0f);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "carmageddon" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "carmageddon" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "gta" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "gta" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.forceTrilinear == false);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "gta" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "cybergladiators" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "cybergladiators" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "cybergladiators" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "blood" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "blood" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "redguard" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.gammaOverride == 0.0f);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "redguard" / "glide-ng.hq.ini").string(), cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.gammaOverride == 0.0f);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "redguard" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.gammaOverride == 0.0f);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "redguard" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.gammaOverride == 0.0f);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "pandemonium" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "pandemonium" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "pandemonium" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_11);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "hitman-codename-47" / "glide-ng.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "hitman-codename-47" / "glide-ng.hq.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "hitman-codename-47" / "glide-ng.voodoo3.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "hitman-codename-47" /
                         "glide-ng.software.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo5);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "archimedean-dynasty" / "glide-ng.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "archimedean-dynasty" / "glide-ng.hq.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "archimedean-dynasty" / "glide-ng.voodoo2.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "archimedean-dynasty" /
                         "glide-ng.software.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "lands-of-lore-2" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "lands-of-lore-2" / "glide-ng.hq.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "lands-of-lore-2" / "glide-ng.voodoo2.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "lands-of-lore-2" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  // MechWarrior 2: 31st Century Combat — Titanium Edition (1998, Win32 Glide 2.x)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mechwarrior-2" / "glide-ng.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mechwarrior-2" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mechwarrior-2" / "glide-ng.voodoo1.ini")
            .string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mechwarrior-2" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  // MDK (1997, Win32 3dfx patch — MDK3DFX.EXE)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mdk" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mdk" / "glide-ng.hq.ini").string(), cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mdk" / "glide-ng.voodoo2.ini").string(),
        cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "mdk" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  // Diablo II (2000, Win32 Glide 3.x — D2Glide.dll -> glide3x.dll)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "diablo-2" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "diablo-2" / "glide-ng.hq.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "diablo-2" / "glide-ng.voodoo3.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "diablo-2" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  // Need for Speed: Porsche Unleashed (2000, Win32 Glide 3.x — voodoo2z.dll)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "nfs-porsche-unleashed" /
                         "glide-ng.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "nfs-porsche-unleashed" /
                         "glide-ng.hq.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "nfs-porsche-unleashed" /
                         "glide-ng.voodoo3.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_10);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load((catalogDir / "games" / "nfs-porsche-unleashed" /
                         "glide-ng.software.ini")
                            .string(),
                        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  // Unreal (1998, Win32 Glide 2.x — GlideDrv.dll -> glide2x.dll)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "unreal" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "unreal" / "glide-ng.hq.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "unreal" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "unreal" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  // Screamer 2 (1996, DOS/32 dynamic GLIDE2X.OVL — s2_3dfx.exe)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "screamer-2" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "screamer-2" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "screamer-2" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "screamer-2" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.noSplash == false);
  }
  // Descent (1995, D1_3Dfx Beta 01 — Win32 glide2x.dll)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent" / "glide-ng.hq.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent" / "glide-ng.software.ini").string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  // Descent II (1996, DOS/32 dynamic GLIDE2X.OVL — D2_3DFX.EXE)
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent-2" / "glide-ng.ini").string(), cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent-2" / "glide-ng.hq.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent-2" / "glide-ng.voodoo1.ini").string(),
        cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_48);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
  {
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(
        (catalogDir / "games" / "descent-2" / "glide-ng.software.ini")
            .string(),
        cfg));
    CHECK(cfg.backend == "software");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.msaaSamples == 1u);
    CHECK(cfg.maxFps == 60.0f);
    CHECK(cfg.noSplash == false);
  }
}

TEST_CASE("Build: Single-Source SemVer Version & Metadata", "[Version]") {
  namespace fs = std::filesystem;
  const fs::path versionFile = fs::path(GLIDE_REPO_ROOT) / "VERSION";
  REQUIRE(fs::is_regular_file(versionFile));

  std::ifstream in(versionFile);
  REQUIRE(in.is_open());
  std::string fileVersion;
  std::getline(in, fileVersion);
  while (!fileVersion.empty() &&
         (fileVersion.back() == '\r' || fileVersion.back() == '\n' ||
          fileVersion.back() == ' ' || fileVersion.back() == '\t')) {
    fileVersion.pop_back();
  }
  REQUIRE(!fileVersion.empty());

  const std::string base(GLIDE_VERSION_BASE);
  const std::string full(GlideWrapper::WRAPPER_VERSION);
  const std::string numeric =
      std::to_string(GLIDE_VERSION_MAJOR) + "." +
      std::to_string(GLIDE_VERSION_MINOR) + "." +
      std::to_string(GLIDE_VERSION_PATCH);

  CHECK(base == fileVersion);
  CHECK(std::string(GlideWrapper::WRAPPER_VERSION_BASE) == fileVersion);
  CHECK(base.rfind(numeric, 0) == 0);

  // WRAPPER_VERSION is either exact "<base>" (on clean tag) or "<base>+g<hash>"
  REQUIRE(full.rfind(base, 0) == 0);
  if (full.size() > base.size()) {
    CHECK(full.substr(base.size(), 2) == "+g");
  }

  CHECK(std::string(GLIDE_GIT_HASH).size() > 0u);
  CHECK(std::string(GLIDE_BUILD_TYPE).size() > 0u);
}

