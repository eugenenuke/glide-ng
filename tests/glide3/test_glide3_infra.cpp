// Auto-modularized Glide 3.x Catch2 test suite unit.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
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

#include "backends/FrontBufferFlushTimer.h"
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
#include "opengl_es/OpenGLESBackend.h"
#include "software/SoftwareBackend.h"
#include "vulkan/VulkanBackend.h"
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

TEST_CASE("Config: IniConfigLoader & Authentic 3dfx Env Var Precedence", "[Config][EnvVars]") {
  SECTION("IniConfigLoader sections, comments & overrides") {
  const std::string tmpPath = "/tmp/glide_ng_test_config_m8.ini";

  // RAII helper to save/restore environment variables touched by this test.
  struct EnvGuard {
    std::string name;
    bool hadValue{false};
    std::string oldValue;
    explicit EnvGuard(const char* n) : name(n) {
      if (const char* v = ::getenv(n)) {
        hadValue = true;
        oldValue = v;
      }
      ::unsetenv(n);
    }
    ~EnvGuard() {
      if (hadValue) {
        ::setenv(name.c_str(), oldValue.c_str(), 1);
      } else {
        ::unsetenv(name.c_str());
      }
    }
  };

  EnvGuard guardConfig("GLIDE_NG_CONFIG");
  EnvGuard guardBackend("GLIDE_NG_BACKEND");
  EnvGuard guardHeadless("GLIDE_NG_HEADLESS");
  EnvGuard guardCard("GLIDE_NG_CARD_MODEL");
  EnvGuard guardApi("GLIDE_NG_API_VERSION");
  EnvGuard guardFbiMem("GLIDE_NG_FBI_MEM_MB");
  EnvGuard guardTmuCount("GLIDE_NG_TMU_COUNT");
  EnvGuard guardTmuMem("GLIDE_NG_TMU_MEM_MB");
  EnvGuard guardSli("GLIDE_NG_SLI");
  EnvGuard guardVendor("GLIDE_NG_VENDOR");
  EnvGuard guardRenderer("GLIDE_NG_RENDERER");
  EnvGuard guardVsync("GLIDE_NG_VSYNC");
  EnvGuard guardMaxFps("GLIDE_NG_MAX_FPS");
  EnvGuard guardMsaa("GLIDE_NG_MSAA");
  EnvGuard guardScale("GLIDE_NG_WINDOW_SCALE");
  EnvGuard guardFullscreen("GLIDE_NG_FULLSCREEN");
  EnvGuard guardFilter("GLIDE_NG_PRESENTATION_FILTER");
  EnvGuard guardLogLevel("GLIDE_NG_LOG_LEVEL");
  EnvGuard guardConsole("GLIDE_NG_LOG_CONSOLE");
  EnvGuard guardLogFile("GLIDE_NG_LOG_FILE");
  EnvGuard guardNoSplash("GLIDE_NG_NO_SPLASH");
  EnvGuard guardPlug("GLIDE_NG_SHAMELESS_PLUG");
  EnvGuard guardSwThreads("GLIDE_NG_SW_THREADS");
  EnvGuard guardSwSimd("GLIDE_NG_SW_SIMD");
  EnvGuard guardSwTile("GLIDE_NG_SW_TILE_SIZE");
  EnvGuard guardSwAa("GLIDE_NG_SW_AA");
  EnvGuard guardVkCache("GLIDE_NG_VK_CACHE_PATH");
  EnvGuard guardGlesVbo("GLIDE_NG_GLES_VBO_SIZE");
  EnvGuard guardFxNoSplash("FX_GLIDE_NO_SPLASH");
  EnvGuard guardFxPlug("FX_GLIDE_SHAMELESS_PLUG");
  EnvGuard guardFxSwap("FX_GLIDE_SWAPINTERVAL");
  // Legacy env vars guarded for clean-break tests
  EnvGuard guardOldBackend("GLIDE_WRAPPER_BACKEND");
  EnvGuard guardOldShortBackend("GLIDE_BACKEND");
  EnvGuard guardOldVsync("GLIDE_VSYNC");
  EnvGuard guardOldDevice("GLIDE_DEVICE");
  EnvGuard guardOldVersion("GLIDE_VERSION_OVERRIDE");
  EnvGuard guardOldHeadless("GLIDE_HEADLESS");
  EnvGuard guardOldForceNoWin("GLIDE_FORCE_NO_WINDOW");

  SECTION("Sections, comments, inline comments, quotes, BOM/CRLF, and unknown keys") {
    {
      std::ofstream out(tmpPath, std::ios::binary);
      // Include UTF-8 BOM + CRLF line endings
      out << "\xEF\xBB\xBF"
          << "# Full line comment: backend = vulkan\r\n"
          << "; Semicolon comment: card_model = Voodoo5\r\n"
          << "[core]\r\n"
          << "backend = opengl_es  # inline hash comment\r\n"
          << "headless = true ; inline semicolon comment\r\n"
          << "\r\n"
          << "[hardware]\r\n"
          << "card_model = Voodoo3\r\n"
          << "api_version = 2.42\r\n"
          << "vendor = \"3dfx # Custom ; Vendor\"\r\n"
          << "\r\n"
          << "[display]\r\n"
          << "vsync = false\r\n"
          << "max_fps = 120.5\r\n"
          << "msaa = 4\r\n"
          << "window_scale = 2\r\n"
          << "fullscreen = true\r\n"
          << "\r\n"
          << "[logging]\r\n"
          << "level = 4\r\n"
          << "console = true\r\n"
          << "\r\n"
          << "[unknown_section]\r\n"
          << "bogus_key = 123\r\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo3);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_42);
    CHECK(cfg.reportedVendorOverride == "3dfx # Custom ; Vendor");
    CHECK(cfg.logToConsole == true);
    CHECK(cfg.vsync == false);
    CHECK(cfg.forceNoWindow == true);
    CHECK(cfg.maxFps == 120.5f);
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.windowScale == 2u);
    CHECK(cfg.fullscreen == true);
    CHECK(cfg.logLevel == 4u);
    REQUIRE(cfg.unknownConfigKeys.size() == 1u);
    CHECK(cfg.unknownConfigKeys[0] == "unknown_section.bogus_key");

    std::remove(tmpPath.c_str());
  }

  SECTION("Flat sectionless INI parses cleanly, legacy JSON syntax is ignored, env overrides win") {
    {
      std::ofstream out(tmpPath);
      out << "card_model = VoodooGraphics\n"
          << "backend = gles\n"
          << "log_to_console = false\n"
          << "vsync = true\n"
          << "api_version = 2.40\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(tmpPath, cfg));
    CHECK(cfg.model == GlideWrapper::CardModel::VoodooGraphics);
    CHECK(cfg.backend == "opengl_es");
    CHECK(cfg.logToConsole == false);
    CHECK(cfg.vsync == true);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_40);

    // Environment variables must override INI values.
    ::setenv("GLIDE_NG_BACKEND", "sw", 1);
    ::setenv("GLIDE_NG_LOG_CONSOLE", "1", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_FULLSCREEN", "0", 1);

    GlideWrapper::WrapperConfig envCfg;
    REQUIRE(loader.Load(tmpPath, envCfg));
    CHECK(envCfg.backend == "software");
    CHECK(envCfg.logToConsole == true);
    CHECK(envCfg.vsync == false);
    CHECK(envCfg.fullscreen == false);
    CHECK(envCfg.settingSources["fullscreen"] == "GLIDE_NG_FULLSCREEN");

    // Clean break: a raw JSON file must NOT be parsed as config.
    ::unsetenv("GLIDE_NG_BACKEND");
    ::unsetenv("GLIDE_NG_LOG_CONSOLE");
    ::unsetenv("GLIDE_NG_VSYNC");
    ::unsetenv("GLIDE_NG_FULLSCREEN");
    {
      std::ofstream out(tmpPath);
      out << "{ \"backend\": \"software\", \"model\": \"Voodoo3\", \"vsync\": false }\n";
    }
    GlideWrapper::WrapperConfig jsonCfg;
    REQUIRE(loader.Load(tmpPath, jsonCfg));
    CHECK(jsonCfg.backend == "vulkan");
    CHECK(jsonCfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(jsonCfg.vsync == true);

    std::remove(tmpPath.c_str());
  }

  SECTION("Strict 4-layer precedence: Defaults < INI < FX_GLIDE_* < GLIDE_NG_*") {
    const std::string customPath = "/tmp/glide_ng_custom_cfg_test.ini";
    {
      std::ofstream out(customPath);
      out << "[glide]\n"
          << "no_splash = false\n"
          << "shameless_plug = false\n"
          << "[display]\n"
          << "vsync = true\n"
          << "presentation_filter = nearest\n"
          << "[logging]\n"
          << "level = warn\n"
          << "file = custom_ini.log\n"
          << "[software]\n"
          << "threads = 4\n"
          << "simd = false\n"
          << "tile_size = 64\n"
          << "aa = ssaa\n"
          << "[vulkan]\n"
          << "cache_path = /tmp/custom_vk.bin\n"
          << "[opengl_es]\n"
          << "vbo_size_mb = 16\n";
    }

    // GLIDE_NG_CONFIG redirects loader away from default path
    ::setenv("GLIDE_NG_CONFIG", customPath.c_str(), 1);
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.configFileLoaded == true);
    CHECK(cfg.configFilePath == customPath);
    CHECK(cfg.noSplash == false);
    CHECK(cfg.shamelessPlugMode == 0);
    CHECK(cfg.vsync == true);
    CHECK(cfg.logLevel == 2u);
    CHECK(cfg.presentationFilter == 0u);
    CHECK(cfg.logFile == "custom_ini.log");
    CHECK(cfg.swThreads == 4u);
    CHECK(cfg.swUseSimd == false);
    CHECK(cfg.swTileSize == 64u);
    CHECK(cfg.swAaMode == "ssaa");
    CHECK(cfg.vkCachePath == "/tmp/custom_vk.bin");
    CHECK(cfg.glesVboSizeMb == 16u);
    CHECK(cfg.settingSources.at("noSplash") == "ini");

    // Layer 3: Authentic FX_GLIDE_* overrides INI
    ::setenv("FX_GLIDE_NO_SPLASH", "1", 1);
    ::setenv("FX_GLIDE_SHAMELESS_PLUG", "1", 1);
    ::setenv("FX_GLIDE_SWAPINTERVAL", "0", 1);
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.noSplash == true);
    CHECK(cfg.shamelessPlugMode == 1);
    CHECK(cfg.vsync == false);
    CHECK(cfg.swapInterval == 0);
    CHECK(cfg.settingSources.at("noSplash") == "FX_GLIDE_NO_SPLASH");
    CHECK(cfg.settingSources.at("shamelessPlug") == "FX_GLIDE_SHAMELESS_PLUG");
    CHECK(cfg.settingSources.at("vsync") == "FX_GLIDE_SWAPINTERVAL");

    // Layer 4: GLIDE_NG_* overrides FX_GLIDE_* and INI
    ::setenv("GLIDE_NG_NO_SPLASH", "0", 1);
    ::setenv("GLIDE_NG_SHAMELESS_PLUG", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "fifo", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "trace", 1);
    ::setenv("GLIDE_NG_PRESENTATION_FILTER", "linear", 1);
    ::setenv("GLIDE_NG_LOG_FILE", "override.log", 1);
    ::setenv("GLIDE_NG_SW_THREADS", "8", 1);
    ::setenv("GLIDE_NG_SW_SIMD", "1", 1);
    ::setenv("GLIDE_NG_SW_TILE_SIZE", "32", 1);
    ::setenv("GLIDE_NG_SW_AA", "analytical", 1);
    ::setenv("GLIDE_NG_VK_CACHE_PATH", "/tmp/env_vk.bin", 1);
    ::setenv("GLIDE_NG_GLES_VBO_SIZE", "32", 1);
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.noSplash == false);
    CHECK(cfg.shamelessPlugMode == 0);
    CHECK(cfg.vsync == true);
    CHECK(cfg.vsyncStrictFifo == true);
    CHECK(cfg.swapInterval == 1);
    CHECK(cfg.logLevel == 5u);
    CHECK(cfg.presentationFilter == 1u);
    CHECK(cfg.logFile == "override.log");
    CHECK(cfg.swThreads == 8u);
    CHECK(cfg.swUseSimd == true);
    CHECK(cfg.swTileSize == 32u);
    CHECK(cfg.swAaMode == "analytical");
    CHECK(cfg.vkCachePath == "/tmp/env_vk.bin");
    CHECK(cfg.glesVboSizeMb == 32u);
    CHECK(cfg.settingSources.at("noSplash") == "GLIDE_NG_NO_SPLASH");
    CHECK(cfg.settingSources.at("shamelessPlug") == "GLIDE_NG_SHAMELESS_PLUG");
    CHECK(cfg.settingSources.at("vsync") == "GLIDE_NG_VSYNC");

    std::remove(customPath.c_str());
  }

  SECTION("Clean break: legacy GLIDE_WRAPPER_* and GLIDE_* env vars are ignored") {
    ::setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
    ::setenv("GLIDE_BACKEND", "software", 1);
    ::setenv("GLIDE_VSYNC", "0", 1);
    ::setenv("GLIDE_DEVICE", "voodoo1", 1);
    ::setenv("GLIDE_VERSION_OVERRIDE", "2.40", 1);
    ::setenv("GLIDE_HEADLESS", "1", 1);
    ::setenv("GLIDE_FORCE_NO_WINDOW", "1", 1);

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent_default.ini", cfg));
    CHECK(cfg.backend == "vulkan");
    CHECK(cfg.vsync == true);
    CHECK(cfg.model == GlideWrapper::CardModel::Voodoo2);
    CHECK(cfg.apiVersion == GlideWrapper::ApiVersion::GLIDE_2_56);
    CHECK(cfg.glide3ApiVersion == GlideWrapper::ApiVersion::GLIDE_3_03);
    CHECK(cfg.forceNoWindow == false);
  }

  SECTION("Shameless plug resolution respects app-controlled vs forced modes") {
    auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
    auto saved = reg.GetConfig();

    GlideWrapper::WrapperConfig cfg = saved;
    cfg.shamelessPlugMode = -1;  // app-controlled
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == false);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == true);

    cfg.shamelessPlugMode = 1;  // forced ON
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == true);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == true);

    cfg.shamelessPlugMode = 0;  // forced OFF
    reg.GetConfig() = cfg;
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(false) == false);
    CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == false);

    reg.GetConfig() = saved;
  }
  }

  SECTION("Authentic 3dfx env var parsing, precedence & runtime behavior") {
  struct ScopedEnvM8 {
    std::vector<std::string> keys = {
        "GLIDE_NG_CONFIG", "GLIDE_NG_HEADLESS", "GLIDE_NG_CARD_MODEL",
        "GLIDE_NG_FBI_MEM_MB", "GLIDE_NG_TMU_MEM_MB", "GLIDE_NG_TMU_COUNT",
        "GLIDE_NG_SLI", "GLIDE_NG_MSAA", "GLIDE_NG_MAX_FPS", "GLIDE_NG_VSYNC",
        "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_LOG_FILE", "GLIDE_NG_NO_SPLASH",
        "FX_GLIDE_FBRAM", "SST_FBIMEM_SIZE", "SSTV2_FBI_MEMSIZE", "SSTH3_FBRAM",
        "FX_GLIDE_TMU_MEMSIZE", "SST_TMUMEM_SIZE", "SSTV2_TMU_MEMSIZE",
        "SSTV2_NUM_TMUS", "FX_GLIDE_NUM_TMU",
        "SST_SLIDETECT", "SSTV2_SLIDETECT",
        "SST_GAMMA", "SSTV2_GAMMA",
        "SST_RGAMMA", "SST_GGAMMA", "SST_BGAMMA",
        "SSTV2_RGAMMA", "SSTV2_GGAMMA", "SSTV2_BGAMMA",
        "SSTH3_RGAMMA", "SSTH3_GGAMMA", "SSTH3_BGAMMA",
        "FX_GLIDE_GAMMARGB", "FX_GLIDE_USE_APP_GAMMA",
        "SST_SCREENREFRESH", "SSTV2_SCREENREFRESH", "FX_GLIDE_REFRESH",
        "FX_GLIDE_IGNORE_REOPEN", "FX_GLIDE_NO_DITHER_SUB",
        "SSTV2_FORCE_BILINEAR", "SSTV2_FORCE_TRILINEAR",
        "SST_TEXMAP_DISABLE", "SSTV2_TEXMAP_DISABLE",
        "FX_GLIDE_BOUNDS_CHECK",
        "FX_GLIDE_ALLOC_COLOR", "SSTV2_ALLOC_COLOR", "SST_TRIPLE_BUFFER",
        "FX_GLIDE_ALLOC_AUX", "SSTV2_ALLOC_AUX",
        "FX_GLIDE_SNAPSHOT", "FX_SNAPSHOT",
        "FX_GLIDE_REPORT_REAL_HW", "FX_GLIDE_EMUL_RUSH",
        "FX_GLIDE_AA_SAMPLE", "FX_GLIDE_TEXTURE_UMA"};
    std::vector<std::pair<std::string, std::string>> saved;
    ScopedEnvM8() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        ::unsetenv(k.c_str());
      }
    }
    ~ScopedEnvM8() {
      for (const auto& kv : saved) {
        if (kv.second == "\x01UNSET") {
          ::unsetenv(kv.first.c_str());
        } else {
          ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
      }
    }
  } guard;

  SECTION("INI [glide]/[display] Phase-2 keys and snapshot syntax parsing") {
    const std::string iniPath = "/tmp/glide_ng_m82_ini_test.ini";
    {
      std::ofstream out(iniPath);
      out << "[hardware]\n"
          << "fbi_memory_mb = 6\n"
          << "tmu_memory_mb = 4\n"
          << "tmu_count = 1\n"
          << "[display]\n"
          << "screen_refresh = 75\n"
          << "gamma = 1.7\n"
          << "[glide]\n"
          << "ignore_reopen = true\n"
          << "no_dither_sub = true\n"
          << "force_bilinear = true\n"
          << "force_trilinear = false\n"
          << "texmap_disable = true\n"
          << "bounds_check = true\n"
          << "alloc_color = 3\n"
          << "alloc_aux = 0\n"
          << "snapshot = /tmp/ini_snap.ppm@4\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(iniPath, cfg));
    CHECK(cfg.fbiMemoryMb == 6u);
    CHECK(cfg.tmuMemoryMb == 4u);
    CHECK(cfg.tmuCount == 1u);
    CHECK(cfg.screenRefreshHz == 75u);
    CHECK(cfg.maxFps == 75.0f);
    CHECK(std::fabs(cfg.gammaOverride - 1.7f) < 1e-4f);
    CHECK(cfg.ignoreReopen == true);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.forceBilinear == true);
    CHECK(cfg.forceTrilinear == false);
    CHECK(cfg.texmapDisable == true);
    CHECK(cfg.boundsCheck == true);
    CHECK(cfg.allocColorBuffers == 3);
    CHECK(cfg.allocAuxBuffers == 0);
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 4u);
    CHECK(cfg.snapshotPath == "/tmp/ini_snap.ppm");
    CHECK(cfg.settingSources.at("gamma") == "ini");
    CHECK(cfg.settingSources.at("screenRefresh") == "ini");
    CHECK(cfg.settingSources.at("snapshot") == "ini");
    std::remove(iniPath.c_str());
  }

  SECTION("Strict 4-layer precedence across all 17 Phase-2 env vars and aliases") {
    const std::string iniPath = "/tmp/glide_ng_m82_prec_test.ini";
    {
      std::ofstream out(iniPath);
      out << "[hardware]\n"
          << "fbi_memory_mb = 6\n"
          << "tmu_memory_mb = 4\n"
          << "tmu_count = 1\n"
          << "[display]\n"
          << "max_fps = 60\n"
          << "[glide]\n"
          << "gamma = 1.2\n"
          << "ignore_reopen = false\n"
          << "alloc_color = 2\n"
          << "alloc_aux = 1\n";
    }
    ::setenv("GLIDE_NG_CONFIG", iniPath.c_str(), 1);

    // Layer 3: Authentic 3dfx vars override INI; V2/primary aliases beat SST_/FX_ aliases
    ::setenv("FX_GLIDE_FBRAM", "8", 1);
    ::setenv("FX_GLIDE_TMU_MEMSIZE", "12", 1);
    ::setenv("SSTV2_NUM_TMUS", "2", 1);
    ::setenv("SST_GAMMA", "1.5", 1);
    ::setenv("SSTV2_GAMMA", "2.1", 1);
    ::setenv("SST_SCREENREFRESH", "72", 1);
    ::setenv("SSTV2_SCREENREFRESH", "85", 1);
    ::setenv("FX_GLIDE_IGNORE_REOPEN", "1", 1);
    ::setenv("FX_GLIDE_NO_DITHER_SUB", "1", 1);
    ::setenv("SSTV2_FORCE_BILINEAR", "1", 1);
    ::setenv("SSTV2_FORCE_TRILINEAR", "1", 1);
    ::setenv("SST_TEXMAP_DISABLE", "1", 1);
    ::setenv("FX_GLIDE_BOUNDS_CHECK", "1", 1);
    ::setenv("FX_GLIDE_ALLOC_COLOR", "3", 1);
    ::setenv("FX_GLIDE_ALLOC_AUX", "0", 1);
    ::setenv("FX_SNAPSHOT", "/tmp/alias.ppm@2", 1);
    ::setenv("FX_GLIDE_SNAPSHOT", "/tmp/primary.ppm@7", 1);

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 8u);
    CHECK(cfg.tmuMemoryMb == 12u);
    CHECK(cfg.tmuCount == 2u);
    CHECK(std::fabs(cfg.gammaOverride - 2.1f) < 1e-4f);
    CHECK(cfg.screenRefreshHz == 85u);
    CHECK(cfg.maxFps == 85.0f);
    CHECK(cfg.ignoreReopen == true);
    CHECK(cfg.noDitherSub == true);
    CHECK(cfg.forceBilinear == true);
    CHECK(cfg.forceTrilinear == true);
    CHECK(cfg.texmapDisable == true);
    CHECK(cfg.boundsCheck == true);
    CHECK(cfg.allocColorBuffers == 3);
    CHECK(cfg.allocAuxBuffers == 0);
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 7u);
    CHECK(cfg.snapshotPath == "/tmp/primary.ppm");
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "FX_GLIDE_FBRAM");
    CHECK(cfg.settingSources.at("tmuMemoryMb") == "FX_GLIDE_TMU_MEMSIZE");
    CHECK(cfg.settingSources.at("tmuCount") == "SSTV2_NUM_TMUS");
    CHECK(cfg.settingSources.at("gamma") == "SSTV2_GAMMA");
    CHECK(cfg.settingSources.at("screenRefresh") == "SSTV2_SCREENREFRESH");
    CHECK(cfg.settingSources.at("snapshot") == "FX_GLIDE_SNAPSHOT");

    // Verify numeric-only, @N, frame list, and periodic interval snapshot formats
    ::unsetenv("FX_GLIDE_SNAPSHOT");
    ::setenv("FX_SNAPSHOT", "@3", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 3u);
    CHECK(cfg.snapshotFrames.empty());
    CHECK(cfg.snapshotInterval == 0u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.IsMultiFrameSnapshot() == false);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_GLIDE_SNAPSHOT", "@10,30,20,10", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 10u);
    CHECK(cfg.snapshotFrames == std::vector<uint64_t>({10u, 20u, 30u}));
    CHECK(cfg.snapshotInterval == 0u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.IsMultiFrameSnapshot() == true);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_GLIDE_SNAPSHOT", "/tmp/sr.ppm@300,600,900", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 300u);
    CHECK(cfg.snapshotFrames == std::vector<uint64_t>({300u, 600u, 900u}));
    CHECK(cfg.snapshotPath == "/tmp/sr.ppm");
    CHECK(cfg.IsMultiFrameSnapshot() == true);

    ::setenv("FX_GLIDE_SNAPSHOT", "@+60", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 0u);
    CHECK(cfg.snapshotFrames.empty());
    CHECK(cfg.snapshotInterval == 60u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.IsMultiFrameSnapshot() == true);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_GLIDE_SNAPSHOT", "@300+120", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 300u);
    CHECK(cfg.snapshotInterval == 120u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_GLIDE_SNAPSHOT", "@300+120:5", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 300u);
    CHECK(cfg.snapshotInterval == 120u);
    CHECK(cfg.snapshotMaxCount == 5u);
    CHECK(cfg.snapshotPath.empty());

    ::setenv("FX_GLIDE_SNAPSHOT", "/tmp/shots/@+30", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 0u);
    CHECK(cfg.snapshotInterval == 30u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.snapshotPath == "/tmp/shots/");
    CHECK(cfg.IsMultiFrameSnapshot() == true);

    ::setenv("FX_GLIDE_SNAPSHOT", "/tmp/user@host.ppm", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == true);
    CHECK(cfg.snapshotFrame == 0u);
    CHECK(cfg.snapshotFrames.empty());
    CHECK(cfg.snapshotInterval == 0u);
    CHECK(cfg.snapshotPath == "/tmp/user@host.ppm");
    CHECK(cfg.IsMultiFrameSnapshot() == false);

    // Malformed specs must be rejected (ignored on env without overwriting INI/alias)
    for (const char* badSpec : {"@+0", "@10+0", "@10+5:0", "@10,", "@,10", "/tmp/sr.ppm@+0"}) {
      INFO("Rejecting invalid snapshot spec: " << badSpec);
      ::setenv("FX_SNAPSHOT", "@9", 1);
      ::setenv("FX_GLIDE_SNAPSHOT", badSpec, 1);
      REQUIRE(loader.Load("nonexistent.ini", cfg));
      CHECK(cfg.snapshotEnabled == true);
      CHECK(cfg.snapshotFrame == 9u);
      CHECK(cfg.snapshotInterval == 0u);
      CHECK(cfg.settingSources.at("snapshot") == "FX_SNAPSHOT");

      const std::string badIniPath = "/tmp/glide_ng_bad_snap.ini";
      {
        std::ofstream badOut(badIniPath);
        badOut << "[glide]\nsnapshot = " << badSpec << "\n";
      }
      ::unsetenv("FX_SNAPSHOT");
      ::unsetenv("FX_GLIDE_SNAPSHOT");
      ::unsetenv("GLIDE_NG_CONFIG");
      GlideWrapper::WrapperConfig badCfg;
      REQUIRE(loader.Load(badIniPath, badCfg));
      CHECK(badCfg.configFileLoaded == true);
      CHECK(badCfg.snapshotEnabled == false);
      CHECK(!badCfg.invalidConfigValues.empty());
      std::remove(badIniPath.c_str());
    }
    ::setenv("GLIDE_NG_CONFIG", iniPath.c_str(), 1);

    ::unsetenv("FX_GLIDE_SNAPSHOT");
    ::setenv("FX_SNAPSHOT", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.snapshotEnabled == false);
    CHECK(cfg.snapshotFrames.empty());
    CHECK(cfg.snapshotInterval == 0u);
    CHECK(cfg.snapshotMaxCount == 0u);
    CHECK(cfg.snapshotPath.empty());

    // Layer 4: GLIDE_NG_* overrides authentic 3dfx vars
    ::setenv("GLIDE_NG_FBI_MEM_MB", "32", 1);
    ::setenv("GLIDE_NG_TMU_MEM_MB", "16", 1);
    ::setenv("GLIDE_NG_TMU_COUNT", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "144", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 32u);
    CHECK(cfg.tmuMemoryMb == 16u);
    CHECK(cfg.tmuCount == 1u);
    CHECK(cfg.maxFps == 144.0f);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "GLIDE_NG_FBI_MEM_MB");
    CHECK(cfg.settingSources.at("tmuMemoryMb") == "GLIDE_NG_TMU_MEM_MB");
    CHECK(cfg.settingSources.at("tmuCount") == "GLIDE_NG_TMU_COUNT");
    CHECK(cfg.settingSources.at("maxFps") == "GLIDE_NG_MAX_FPS");

    std::remove(iniPath.c_str());
  }

  SECTION("Authentic card-family aliases, per-channel RGB gamma, and strict FX_GLIDE_REPORT_REAL_HW") {
    const std::string iniPath = "/tmp/glide_ng_tier_a_test.ini";
    {
      std::ofstream out(iniPath);
      out << "[glide]\n"
          << "gamma_r = 1.2\n"
          << "gamma_g = 1.3\n"
          << "gamma_b = 1.4\n"
          << "use_app_gamma = false\n"
          << "report_real_hw = true\n"
          << "emul_rush = true\n"
          << "texture_uma = true\n";
    }
    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(iniPath, cfg));
    CHECK(std::fabs(cfg.gammaOverrideR - 1.2f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideG - 1.3f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideB - 1.4f) < 1e-4f);
    CHECK(cfg.HasPerChannelGammaOverride() == true);
    CHECK(cfg.useAppGamma == false);
    CHECK(cfg.reportRealHw == true);
    CHECK(cfg.reportRealHwFromIni == true);
    CHECK(cfg.emulRush == true);
    CHECK(cfg.emulRushFromIni == true);
    CHECK(cfg.textureUmaMode == 1);
    std::remove(iniPath.c_str());

    // Family alias ordering: SST_ < SSTV2_ < SSTH3_ < FX_GLIDE_ < GLIDE_NG_
    ::setenv("SST_FBIMEM_SIZE", "4", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 4u);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "SST_FBIMEM_SIZE");

    ::setenv("SSTV2_FBI_MEMSIZE", "8", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 8u);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "SSTV2_FBI_MEMSIZE");

    ::setenv("SSTH3_FBRAM", "16", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 16u);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "SSTH3_FBRAM");

    ::setenv("FX_GLIDE_FBRAM", "32", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.fbiMemoryMb == 32u);
    CHECK(cfg.settingSources.at("fbiMemoryMb") == "FX_GLIDE_FBRAM");

    ::setenv("SST_TMUMEM_SIZE", "2", 1);
    ::setenv("SSTV2_TMU_MEMSIZE", "4", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.tmuMemoryMb == 4u);
    CHECK(cfg.settingSources.at("tmuMemoryMb") == "SSTV2_TMU_MEMSIZE");

    ::setenv("SSTV2_NUM_TMUS", "1", 1);
    ::setenv("FX_GLIDE_NUM_TMU", "2", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.tmuCount == 2u);
    CHECK(cfg.settingSources.at("tmuCount") == "FX_GLIDE_NUM_TMU");

    ::setenv("SST_SLIDETECT", "1", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.enableSli == true);
    CHECK(cfg.settingSources.at("enableSli") == "SST_SLIDETECT");

    ::setenv("SSTV2_SLIDETECT", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.enableSli == false);
    CHECK(cfg.settingSources.at("enableSli") == "SSTV2_SLIDETECT");

    ::setenv("GLIDE_NG_SLI", "1", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.enableSli == true);
    CHECK(cfg.settingSources.at("enableSli") == "GLIDE_NG_SLI");

    ::setenv("FX_GLIDE_AA_SAMPLE", "4", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.msaaSamples == 4u);
    CHECK(cfg.settingSources.at("msaaSamples") == "FX_GLIDE_AA_SAMPLE");

    ::setenv("GLIDE_NG_MSAA", "2", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.msaaSamples == 2u);
    CHECK(cfg.settingSources.at("msaaSamples") == "GLIDE_NG_MSAA");

    ::setenv("SST_TRIPLE_BUFFER", "1", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.allocColorBuffers == 3);
    CHECK(cfg.settingSources.at("allocColor") == "SST_TRIPLE_BUFFER");

    ::setenv("SSTV2_ALLOC_COLOR", "2", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.allocColorBuffers == 2);
    CHECK(cfg.settingSources.at("allocColor") == "SSTV2_ALLOC_COLOR");

    ::setenv("SSTV2_ALLOC_AUX", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.allocAuxBuffers == 0);
    CHECK(cfg.settingSources.at("allocAux") == "SSTV2_ALLOC_AUX");

    ::setenv("SSTV2_TEXMAP_DISABLE", "1", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.texmapDisable == true);
    CHECK(cfg.settingSources.at("texmapDisable") == "SSTV2_TEXMAP_DISABLE");

    ::setenv("SSTV2_SCREENREFRESH", "75", 1);
    ::setenv("FX_GLIDE_REFRESH", "100", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.screenRefreshHz == 100u);
    CHECK(cfg.maxFps == 100.0f);
    CHECK(cfg.settingSources.at("screenRefresh") == "FX_GLIDE_REFRESH");

    // Per-channel gamma precedence: SST_ < SSTV2_ < SSTH3_ < FX_GLIDE_GAMMARGB
    ::setenv("SST_RGAMMA", "1.1", 1);
    ::setenv("SSTV2_RGAMMA", "1.5", 1);
    ::setenv("SSTH3_RGAMMA", "1.9", 1);
    ::setenv("SSTH3_GGAMMA", "1.3", 1);
    ::setenv("SSTH3_BGAMMA", "0.9", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(std::fabs(cfg.gammaOverrideR - 1.9f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideG - 1.3f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideB - 0.9f) < 1e-4f);
    CHECK(cfg.settingSources.at("gammaR") == "SSTH3_RGAMMA");

    ::setenv("FX_GLIDE_GAMMARGB", "2.0 1.6 1.2", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(std::fabs(cfg.gammaOverrideR - 2.0f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideG - 1.6f) < 1e-4f);
    CHECK(std::fabs(cfg.gammaOverrideB - 1.2f) < 1e-4f);
    CHECK(cfg.settingSources.at("gammaR") == "FX_GLIDE_GAMMARGB");

    // Strict 3dfx != NULL check on FX_GLIDE_REPORT_REAL_HW ("0" still enables it!)
    ::unsetenv("FX_GLIDE_REPORT_REAL_HW");
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.reportRealHw == false);
    ::setenv("FX_GLIDE_REPORT_REAL_HW", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.reportRealHw == true);
    CHECK(cfg.reportRealHwFromIni == false);
    CHECK(cfg.settingSources.at("reportRealHw") == "FX_GLIDE_REPORT_REAL_HW");

    ::setenv("FX_GLIDE_EMUL_RUSH", "1", 1);
    ::setenv("FX_GLIDE_TEXTURE_UMA", "1", 1);
    ::setenv("FX_GLIDE_USE_APP_GAMMA", "0", 1);
    REQUIRE(loader.Load("nonexistent.ini", cfg));
    CHECK(cfg.emulRush == true);
    CHECK(cfg.textureUmaMode == 1);
    CHECK(cfg.useAppGamma == false);
  }

#ifdef __linux__
  SECTION("Runtime per-channel RGB gamma, FX_GLIDE_USE_APP_GAMMA=0, and FX_GLIDE_TEXTURE_UMA=1") {
    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_CARD_MODEL", "voodoo3", 1);
    ::setenv("GLIDE_NG_TMU_COUNT", "2", 1);
    ::setenv("GLIDE_NG_TMU_MEM_MB", "4", 1);
    ::setenv("FX_GLIDE_GAMMARGB", "2.0 1.0 0.5", 1);
    ::setenv("FX_GLIDE_TEXTURE_UMA", "1", 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);
    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto texMaxAddr = reinterpret_cast<FxU32 (*)(GrChipID_t)>(::dlsym(handle, "grTexMaxAddress"));
    auto loadGamma = reinterpret_cast<void (*)(FxU32, FxU32*, FxU32*, FxU32*)>(
        ::dlsym(handle, "grLoadGammaTable"));
    auto getGammaExt = reinterpret_cast<void (*)(FxU32, FxU32*, FxU32*, FxU32*)>(
        ::dlsym(handle, "grGetGammaTableExt"));
    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(getGammaExt != nullptr);

    glideInit();
    // FX_GLIDE_TEXTURE_UMA=1 pools both 4 MB TMUs right at init -> 8 MB on TMU0
    CHECK(texMaxAddr(GR_TMU0) == 8u * 1024u * 1024u);

    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    FxU32 rOut[256] = {}, gOut[256] = {}, bOut[256] = {};
    getGammaExt(256, rOut, gOut, bOut);
    // At mid-level 128: R gamma 2.0 lifts (~181), G gamma 1.0 stays 128, B gamma 0.5 drops (~64)
    CHECK(rOut[128] > 165u);
    CHECK(gOut[128] == 128u);
    CHECK(bOut[128] < 80u);

    winClose(ctx);
    glideShutdown();

    // Now test FX_GLIDE_USE_APP_GAMMA=0 ignoring app grLoadGammaTable
    ::unsetenv("FX_GLIDE_GAMMARGB");
    ::unsetenv("FX_GLIDE_TEXTURE_UMA");
    ::setenv("FX_GLIDE_USE_APP_GAMMA", "0", 1);
    glideInit();
    CHECK(texMaxAddr(GR_TMU0) == 4u * 1024u * 1024u);
    ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                  GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    FxU32 custom[256];
    for (FxU32 i = 0; i < 256; ++i) custom[i] = 255u - i;
    loadGamma(256, custom, custom, custom);
    getGammaExt(256, rOut, gOut, bOut);
    CHECK(rOut[128] == 128u);
    CHECK(rOut[0] == 0u);
    CHECK(rOut[255] == 255u);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);
  }
#endif

#ifdef __linux__
  SECTION("End-to-end Glide 3.x runtime verification (sizing, aux=0, no_dither, texmap_disable, reopen, bounds, gamma, PPM snapshot)") {
    const std::string logPath = "/tmp/glide_ng_m82_g3_runtime.log";
    const std::string snapPath = "/tmp/glide_ng_m82_g3_snap.ppm";
    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_VSYNC", "0", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("FX_GLIDE_FBRAM", "4", 1);
    ::setenv("FX_GLIDE_TMU_MEMSIZE", "8", 1);
    ::setenv("SSTV2_NUM_TMUS", "1", 1);
    ::setenv("SSTV2_GAMMA", "2.0", 1);
    ::setenv("FX_GLIDE_IGNORE_REOPEN", "1", 1);
    ::setenv("FX_GLIDE_NO_DITHER_SUB", "1", 1);
    ::setenv("SST_TEXMAP_DISABLE", "1", 1);
    ::setenv("FX_GLIDE_ALLOC_AUX", "0", 1);
    ::setenv("FX_GLIDE_BOUNDS_CHECK", "1", 1);
    const std::string snapArg = snapPath + "@0";
    ::setenv("FX_GLIDE_SNAPSHOT", snapArg.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto grGetFn = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxI32*)>(::dlsym(handle, "grGet"));
    auto texMaxAddr = reinterpret_cast<FxU32 (*)(GrChipID_t)>(::dlsym(handle, "grTexMaxAddress"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(::dlsym(handle, "grVertexLayout"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(::dlsym(handle, "grBufferClear"));
    auto depthMode = reinterpret_cast<void (*)(GrDepthBufferMode_t)>(::dlsym(handle, "grDepthBufferMode"));
    auto depthFunc = reinterpret_cast<void (*)(GrCmpFnc_t)>(::dlsym(handle, "grDepthBufferFunction"));
    auto depthMask = reinterpret_cast<void (*)(FxBool)>(::dlsym(handle, "grDepthMask"));
    auto ditherMode = reinterpret_cast<void (*)(GrDitherMode_t)>(::dlsym(handle, "grDitherMode"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbRead = reinterpret_cast<FxBool (*)(GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));
    auto loadGamma = reinterpret_cast<void (*)(FxU32, FxU32*, FxU32*, FxU32*)>(
        ::dlsym(handle, "grLoadGammaTable"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));

    glideInit();

    // 1. Hardware sizing queries
    FxI32 fbBytes = 0, tmuBytes = 0, numTmus = 0;
    REQUIRE(grGetFn(GR_MEMORY_FB, 4, &fbBytes) == 4);
    REQUIRE(grGetFn(GR_MEMORY_TMU, 4, &tmuBytes) == 4);
    REQUIRE(grGetFn(GR_NUM_TMU, 4, &numTmus) == 4);
    CHECK(fbBytes == 4 * 1024 * 1024);
    CHECK(tmuBytes == 8 * 1024 * 1024);
    CHECK(numTmus == 1);
    CHECK(texMaxAddr(GR_TMU0) == 8u * 1024u * 1024u);
    CHECK(texMaxAddr(GR_TMU1) == 0u);

    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // App tries to reset gamma to linear -> SSTV2_GAMMA=2.0 override remains active
    FxU32 linearRamp[256];
    for (FxU32 i = 0; i < 256; ++i) linearRamp[i] = i;
    loadGamma(256, linearRamp, linearRamp, linearRamp);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);

    depthMask(FXTRUE);
    bufferClear(0xFF000000u, 0xFF, 0xFFFF);

    auto drawQuad = [&](float x0, float y0, float x1, float y1, float oow,
                        float r, float g, float b) {
      StandardVert tl = {x0, y0, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert tr = {x1, y0, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert br = {x1, y1, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      StandardVert bl = {x0, y1, 0.0f, r, g, b, 0.0f, 255.0f, oow, 0.0f, 0.0f, oow};
      drawTri(&tl, &tr, &br);
      drawTri(&tl, &br, &bl);
    };

    // 2. FX_GLIDE_ALLOC_AUX=0: depth test disabled -> far BLUE overwrites near RED
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    depthMode(GR_DEPTHBUFFER_WBUFFER);
    depthFunc(GR_CMP_LESS);
    depthMask(FXTRUE);
    drawQuad(20.0f, 20.0f, 120.0f, 120.0f, 0.10f, 240.0f, 20.0f, 20.0f);
    drawQuad(30.0f, 30.0f, 110.0f, 110.0f, 0.01f, 20.0f, 40.0f, 240.0f);
    depthMode(GR_DEPTHBUFFER_DISABLE);
    depthMask(FXFALSE);

    // Pure RED patch at (150..250, 20..120) to verify PPM ABGR->RGB channel order
    drawQuad(150.0f, 20.0f, 250.0f, 120.0f, 1.0f, 240.0f, 16.0f, 16.0f);

    // 3. FX_GLIDE_NO_DITHER_SUB=1: flat (124,122,124) with GR_DITHER_4x4 has 0 R5 spread
    ditherMode(GR_DITHER_4x4);
    drawQuad(300.0f, 20.0f, 360.0f, 80.0f, 1.0f, 124.0f, 122.0f, 124.0f);
    ditherMode(GR_DITHER_DISABLE);

    // 4. FX_GLIDE_BOUNDS_CHECK=1: submit off-screen triangle
    StandardVert oa = {720.0f, 540.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert ob = {820.0f, 540.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert oc = {770.0f, 640.0f, 0.0f, 255.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    drawTri(&oa, &ob, &oc);

    // 5. Verify depth disabled (center of 30..110 is BLUE)
    uint16_t auxPx = 0;
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 70, 70, 1, 1, 2, &auxPx) == FXTRUE);
    const int auxR = ((auxPx >> 11) & 31) << 3;
    const int auxB = (auxPx & 31) << 3;
    CHECK(auxB > 180);
    CHECK(auxR < 60);

    // 6. Verify dither suppressed (16x16 block inside 300..360 has uniform R5)
    uint16_t dithBlock[16 * 16] = {};
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 316, 36, 16, 16, 16 * 2, dithBlock) == FXTRUE);
    int r5Min = 31, r5Max = 0;
    for (uint16_t p : dithBlock) {
      int r5 = (p >> 11) & 31;
      r5Min = std::min(r5Min, r5);
      r5Max = std::max(r5Max, r5);
    }
    CHECK(r5Min == r5Max);

    // 7. FX_GLIDE_IGNORE_REOPEN=1: redundant grSstWinOpen preserves red patch
    FxU32 ctx2 = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                         GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1);
    CHECK(ctx2 != 0);
    uint16_t redPx = 0;
    REQUIRE(lfbRead(GR_BUFFER_BACKBUFFER, 200, 70, 1, 1, 2, &redPx) == FXTRUE);
    CHECK((((redPx >> 11) & 31) << 3) > 200);
    CHECK(((redPx & 31) << 3) < 40);

    // 8. Swap frame 0 -> triggers FX_GLIDE_SNAPSHOT
    bufferSwap(0);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Verify PPM snapshot file exists, has exact binary P6 size, and accurate RGB (no R/B swap)
    std::ifstream snapIn(snapPath, std::ios::binary);
    REQUIRE(snapIn.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(snapIn)),
                               std::istreambuf_iterator<char>());
    const std::string expectedHdr = "P6\n640 480\n255\n";
    REQUIRE(bytes.size() == expectedHdr.size() + 640u * 480u * 3u);
    CHECK(std::string(bytes.begin(), bytes.begin() + expectedHdr.size()) == expectedHdr);
    const size_t redOffset = expectedHdr.size() + (70u * 640u + 200u) * 3u;
    CHECK(bytes[redOffset + 0] > 200);  // R
    CHECK(bytes[redOffset + 1] < 40);   // G
    CHECK(bytes[redOffset + 2] < 40);   // B (proves ABGR -> RGB unpack is exact)

    // Verify bounds check and gamma override were logged
    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    const std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                 std::istreambuf_iterator<char>());
    CHECK(logContent.find("[BoundsCheck]") != std::string::npos);
    CHECK(logContent.find("Applying authentic gamma override: 2") != std::string::npos);

    std::remove(logPath.c_str());
    std::remove(snapPath.c_str());
  }
#endif
  }

#ifdef __linux__
  SECTION("Multi-frame list and periodic interval FX_GLIDE_SNAPSHOT runtime capture") {
    namespace fs = std::filesystem;

    auto readPpmRgb = [](const fs::path& p, uint32_t x, uint32_t y) {
      std::ifstream in(p, std::ios::binary);
      REQUIRE(in.is_open());
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
      const std::string hdr = "P6\n640 480\n255\n";
      REQUIRE(bytes.size() == hdr.size() + 640u * 480u * 3u);
      REQUIRE(std::string(bytes.begin(), bytes.begin() + hdr.size()) == hdr);
      const size_t off = hdr.size() + (y * 640u + x) * 3u;
      return std::array<uint8_t, 3>{bytes[off + 0], bytes[off + 1], bytes[off + 2]};
    };

    auto runFramesWithSpec = [](const std::string& snapSpec, int numFrames) {
      ::setenv("GLIDE_NG_BACKEND", "software", 1);
      ::setenv("GLIDE_NG_HEADLESS", "1", 1);
      ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
      ::setenv("GLIDE_NG_VSYNC", "0", 1);
      ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);
      ::setenv("GLIDE_NG_LOG_LEVEL", "warn", 1);
      ::setenv("FX_GLIDE_SNAPSHOT", snapSpec.c_str(), 1);

      void* handle = OpenGlide3Handle();
      REQUIRE(handle != nullptr);

      auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
      auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
      auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
          ::dlsym(handle, "grSstWinOpen"));
      auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
      auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(::dlsym(handle, "grVertexLayout"));
      auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(::dlsym(handle, "grBufferClear"));
      auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                    GrCombineLocal_t, GrCombineOther_t, FxBool)>(
          ::dlsym(handle, "grColorCombine"));
      auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
          ::dlsym(handle, "grDrawTriangle"));
      auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));

      glideInit();
      FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                          GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 0);
      REQUIRE(ctx != 0);

      vertexLayout(0x01, 0, 1);
      vertexLayout(0x02, 8, 1);
      vertexLayout(0x20, 12, 1);
      vertexLayout(0x10, 28, 1);
      vertexLayout(0x03, 32, 1);
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

      for (int f = 0; f < numFrames; ++f) {
        bufferClear(0xFF000000u, 0xFF, 0xFFFF);
        const float r = (f == 0 || f == 1) ? 240.0f : 16.0f;
        const float g = (f == 2 || f == 3) ? 240.0f : 16.0f;
        const float b = (f >= 4) ? 240.0f : 16.0f;
        StandardVert tl = {80.0f, 80.0f, 0.0f, r, g, b, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert tr = {200.0f, 80.0f, 0.0f, r, g, b, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert br = {200.0f, 200.0f, 0.0f, r, g, b, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        StandardVert bl = {80.0f, 200.0f, 0.0f, r, g, b, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        drawTri(&tl, &tr, &br);
        drawTri(&tl, &br, &bl);
        bufferSwap(0);
      }

      winClose(ctx);
      glideShutdown();
      ::dlclose(handle);
    };

    // 1. Explicit list with auto-created directory
    const fs::path listDir = "/tmp/glide_ng_snap_list";
    std::error_code ec;
    fs::remove_all(listDir, ec);
    runFramesWithSpec((listDir / "multi.ppm@0,2,4").string(), 5);

    CHECK(fs::is_regular_file(listDir / "multi_000000.ppm"));
    CHECK(!fs::exists(listDir / "multi_000001.ppm"));
    CHECK(fs::is_regular_file(listDir / "multi_000002.ppm"));
    CHECK(!fs::exists(listDir / "multi_000003.ppm"));
    CHECK(fs::is_regular_file(listDir / "multi_000004.ppm"));
    CHECK(!fs::exists(listDir / "multi.ppm"));

    const auto rgb0 = readPpmRgb(listDir / "multi_000000.ppm", 140, 140);
    CHECK(rgb0[0] > 200);
    CHECK(rgb0[1] < 40);
    CHECK(rgb0[2] < 40);

    const auto rgb2 = readPpmRgb(listDir / "multi_000002.ppm", 140, 140);
    CHECK(rgb2[0] < 40);
    CHECK(rgb2[1] > 200);
    CHECK(rgb2[2] < 40);

    const auto rgb4 = readPpmRgb(listDir / "multi_000004.ppm", 140, 140);
    CHECK(rgb4[0] < 40);
    CHECK(rgb4[1] < 40);
    CHECK(rgb4[2] > 200);

    fs::remove_all(listDir, ec);

    // 2. Periodic interval with max cap (@1+2:3 -> frames 1, 3, 5 only; 7 skipped)
    const fs::path periodicDir = "/tmp/glide_ng_snap_periodic";
    fs::remove_all(periodicDir, ec);
    runFramesWithSpec(periodicDir.string() + "/@1+2:3", 8);

    CHECK(!fs::exists(periodicDir / "glide_snapshot_000000.ppm"));
    CHECK(fs::is_regular_file(periodicDir / "glide_snapshot_000001.ppm"));
    CHECK(!fs::exists(periodicDir / "glide_snapshot_000002.ppm"));
    CHECK(fs::is_regular_file(periodicDir / "glide_snapshot_000003.ppm"));
    CHECK(!fs::exists(periodicDir / "glide_snapshot_000004.ppm"));
    CHECK(fs::is_regular_file(periodicDir / "glide_snapshot_000005.ppm"));
    CHECK(!fs::exists(periodicDir / "glide_snapshot_000007.ppm"));

    const auto per1 = readPpmRgb(periodicDir / "glide_snapshot_000001.ppm", 140, 140);
    CHECK(per1[0] > 200);
    const auto per3 = readPpmRgb(periodicDir / "glide_snapshot_000003.ppm", 140, 140);
    CHECK(per3[1] > 200);
    const auto per5 = readPpmRgb(periodicDir / "glide_snapshot_000005.ppm", 140, 140);
    CHECK(per5[2] > 200);

    fs::remove_all(periodicDir, ec);
  }
#endif

  SECTION("Automatic one-shot startup splash policy") {
  auto& cfg = GlideWrapper::EmulationRegistry::GetInstance().GetConfig();
  const bool savedNoSplash = cfg.noSplash;
  const bool savedHeadless = cfg.forceNoWindow;
  const int savedPlugMode = cfg.shamelessPlugMode;

  static int s_calls = 0;
  static uint32_t s_lastW = 0, s_lastH = 0;
  static bool s_plugAllowedDuringSplash = true;
  static bool s_wsiAutoSplashActiveDuringSplash = false;
  auto spy = [](uint32_t w, uint32_t h) {
    ++s_calls;
    s_lastW = w;
    s_lastH = h;
    s_plugAllowedDuringSplash =
        GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true);
    s_wsiAutoSplashActiveDuringSplash =
        GlideWrapper::NativeSurfaceHost::IsAutoSplashActive();
  };

  // 1. Plays once when !noSplash && !headless, suppresses shameless plug inside
  GlideWrapper::EntryCommon::ResetAutoSplashState();
  s_calls = 0;
  s_lastW = s_lastH = 0;
  s_plugAllowedDuringSplash = true;
  s_wsiAutoSplashActiveDuringSplash = false;
  cfg.noSplash = false;
  cfg.forceNoWindow = false;
  cfg.shamelessPlugMode = 1;
  CHECK_FALSE(GlideWrapper::NativeSurfaceHost::IsAutoSplashActive());
  CHECK(GlideWrapper::EntryCommon::MaybeRunAutoStartupSplash(640, 480, spy));
  CHECK(s_calls == 1);
  CHECK(s_lastW == 640u);
  CHECK(s_lastH == 480u);
  CHECK(s_plugAllowedDuringSplash == false);
  CHECK(s_wsiAutoSplashActiveDuringSplash == true);
  CHECK_FALSE(GlideWrapper::NativeSurfaceHost::IsAutoSplashActive());
  // Shameless plug resumes after auto-splash exits
  CHECK(GlideWrapper::EntryCommon::ShouldRenderShamelessPlug(true) == true);

  // 2. Second grSstWinOpen in the same session does not replay the splash
  CHECK_FALSE(
      GlideWrapper::EntryCommon::MaybeRunAutoStartupSplash(800, 600, spy));
  CHECK(s_calls == 1);

  // 3. Suppressed when noSplash=true
  GlideWrapper::EntryCommon::ResetAutoSplashState();
  cfg.noSplash = true;
  cfg.forceNoWindow = false;
  CHECK_FALSE(
      GlideWrapper::EntryCommon::MaybeRunAutoStartupSplash(640, 480, spy));
  CHECK(s_calls == 1);

  // 4. Suppressed when headless=true (keeps headless tests instant)
  GlideWrapper::EntryCommon::ResetAutoSplashState();
  cfg.noSplash = false;
  cfg.forceNoWindow = true;
  CHECK_FALSE(
      GlideWrapper::EntryCommon::MaybeRunAutoStartupSplash(640, 480, spy));
  CHECK(s_calls == 1);

  // 5. ResetAutoSplashState allows a fresh session to play once again
  GlideWrapper::EntryCommon::ResetAutoSplashState();
  cfg.noSplash = false;
  cfg.forceNoWindow = false;
  CHECK(GlideWrapper::EntryCommon::MaybeRunAutoStartupSplash(640, 480, spy));
  CHECK(s_calls == 2);

  // Restore original config & clean splash state
  cfg.noSplash = savedNoSplash;
  cfg.forceNoWindow = savedHeadless;
  cfg.shamelessPlugMode = savedPlugMode;
  GlideWrapper::EntryCommon::ResetAutoSplashState();
  }

}

TEST_CASE("WSI: NativeSurfaceHost, Present Modes & DOSBox Handshake", "[WSI][NativeSurfaceHost]") {
  SECTION("Vulkan Mailbox preference on Wayland VSync") {
  const std::vector<vk::PresentModeKHR> allModes = {
      vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eMailbox,
      vk::PresentModeKHR::eFifo};
  const std::vector<vk::PresentModeKHR> fifoOnly = {vk::PresentModeKHR::eFifo};
  const std::vector<vk::PresentModeKHR> immAndFifo = {
      vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eFifo};

  // Wayland + VSync prefers Mailbox when available (tear-free, avoids wp_fifo_v1 stalls)
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, true, false) ==
        vk::PresentModeKHR::eMailbox);
  // Wayland + VSync falls back to FIFO when Mailbox is unavailable
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(immAndFifo, true, true, false) ==
        vk::PresentModeKHR::eFifo);
  // Explicit GLIDE_NG_VSYNC=fifo forces FIFO even on Wayland
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, true, true) ==
        vk::PresentModeKHR::eFifo);
  // X11 + VSync retains FIFO
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, true, false, false) ==
        vk::PresentModeKHR::eFifo);
  // VSync disabled prefers Mailbox -> Immediate -> FIFO
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(allModes, false, false, false) ==
        vk::PresentModeKHR::eMailbox);
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(immAndFifo, false, false, false) ==
        vk::PresentModeKHR::eImmediate);
  CHECK(GlideWrapper::VulkanBackend::SelectPresentMode(fifoOnly, false, false, false) ==
        vk::PresentModeKHR::eFifo);
  }

  SECTION("Vulkan X11 visual preference avoids DirectColor and unsupported visuals") {
    using Candidate = GlideWrapper::VulkanBackend::X11VisualCandidate;

    // Empty list yields 0.
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual({}) == 0UL);

    // DirectColor (isTrueColor=false) and non-presentable visuals are skipped in
    // favor of the default 24-bit TrueColor visual (e.g. 0x21).
    const std::vector<Candidate> mixed = {
        {0x1ea, 24, false, false, true},   // DirectColor inherited from SDL
        {0x20, 32, true, false, true},     // 32-bit TrueColor
        {0x22, 24, true, false, true},     // Non-default 24-bit TrueColor
        {0x21, 24, true, true, true},      // Default 24-bit TrueColor
    };
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual(mixed) == 0x21UL);

    // If the default 24-bit TrueColor visual lacks presentation support, select
    // another supported 24-bit TrueColor visual before falling back to 32-bit.
    const std::vector<Candidate> defUnsupported = {
        {0x21, 24, true, true, false},
        {0x30, 32, true, false, true},
        {0x22, 24, true, false, true},
    };
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual(defUnsupported) ==
          0x22UL);

    // When no 24-bit TrueColor visual is supported, pick default TrueColor,
    // then 32-bit TrueColor, then any other presentable TrueColor depth.
    const std::vector<Candidate> depth32Default = {
        {0x1f4, 24, false, false, true},
        {0x40, 32, true, false, true},
        {0x41, 16, true, true, true},
    };
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual(depth32Default) ==
          0x41UL);

    const std::vector<Candidate> depth32Only = {
        {0x1f4, 24, false, false, true},
        {0x40, 32, true, false, true},
        {0x42, 16, true, false, true},
    };
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual(depth32Only) ==
          0x40UL);

    // Last resort: fall back to a presentable non-TrueColor visual only when no
    // TrueColor visual supports presentation.
    const std::vector<Candidate> directOnly = {
        {0x21, 24, true, true, false},
        {0x1ea, 24, false, false, true},
    };
    CHECK(GlideWrapper::VulkanBackend::SelectPreferredX11Visual(directOnly) ==
          0x1eaUL);

    // Headless / detached NativeSurfaceHost liveness defaults.
    GlideWrapper::NativeSurfaceHost host;
    CHECK_FALSE(host.IsX11ParentDestroyed());
    CHECK(host.ProbeX11ParentAlive());
    CHECK(host.IsHostWaylandDisplayAlive());
  }

  SECTION("Aspect-ratio-preserving presentation rect calculation") {
  using GlideWrapper::NativeSurfaceHost;
  using GlideWrapper::PresentRect;

  // 4:3 guest (640x480) inside 16:9 host (1920x1080) -> pillarboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 1080, true) ==
        PresentRect{240, 0, 1440, 1080});

  // Exact aspect-ratio match (640x480 in 800x600)
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 800, 600, true) ==
        PresentRect{0, 0, 800, 600});

  // Wider guest (640x400, 16:10) inside 4:3 host (640x480) -> letterboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 400, 640, 480, true) ==
        PresentRect{0, 40, 640, 400});

  // 4:3 guest (800x600) inside 1440p host (2560x1440) -> pillarboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(800, 600, 2560, 1440, true) ==
        PresentRect{320, 0, 1920, 1440});

  // 4:3 guest (640x480) inside portrait host (1080x1920) -> letterboxed
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1080, 1920, true) ==
        PresentRect{0, 555, 1080, 810});

  // Stretch mode disables aspect-ratio preservation
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 1080, false) ==
        PresentRect{0, 0, 1920, 1080});

  // Zero-size safety guards
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 0, 1080, true) ==
        PresentRect{0, 0, 0, 0});
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 480, 1920, 0, true) ==
        PresentRect{0, 0, 0, 0});
  CHECK(NativeSurfaceHost::ComputePresentRect(0, 480, 1920, 1080, true) ==
        PresentRect{0, 0, 1920, 1080});
  CHECK(NativeSurfaceHost::ComputePresentRect(640, 0, 1920, 1080, true) ==
        PresentRect{0, 0, 1920, 1080});
  }

  SECTION("Headless lifecycle & idempotent detach") {
  using GlideWrapper::NativeSurfaceHost;

  NativeSurfaceHost host;
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
  CHECK(!host.WasQuitRequested());
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(host.GetSDLWindow() == nullptr);
  CHECK(host.GetX11Display() == nullptr);
  CHECK(host.GetX11Window() == 0UL);
  CHECK(host.GetWaylandDisplay() == nullptr);
  CHECK(host.GetWaylandSurface() == nullptr);
  CHECK(host.GetWaylandScale() == 1);

  uint32_t pw = 999, ph = 999;
  CHECK(!host.SyncBounds(pw, ph));
  CHECK(pw == 0);
  CHECK(ph == 0);
  host.PollEvents();  // Safe no-op when detached

  // Attach headless (zero OS/SDL calls)
  REQUIRE(host.Attach(nullptr, 640, 480, false, /*forceNoWindow=*/true,
                      "Headless Test"));
  CHECK(host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Headless);
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(host.GetSDLWindow() == nullptr);

  uint32_t gw = 0, gh = 0;
  host.GetGuestSize(gw, gh);
  CHECK(gw == 640);
  CHECK(gh == 480);

  host.GetDrawableSize(pw, ph);
  CHECK(pw == 640);
  CHECK(ph == 480);
  CHECK(!host.SyncBounds(pw, ph));
  CHECK(pw == 640);
  CHECK(ph == 480);
  host.NotifyWin32PassthruReclaimed();  // Safe no-op in Headless mode
  CHECK(!host.ConsumeSurfaceInvalidated());
  host.PollEvents();
  CHECK(!host.WasQuitRequested());

  // Re-attaching with a foreign handle while forceNoWindow=true stays Headless
  void* fakeHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345));
  REQUIRE(host.Attach(fakeHandle, 800, 600, true, /*forceNoWindow=*/true,
                      "Headless Foreign"));
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Headless);
  host.GetDrawableSize(pw, ph);
  CHECK(pw == 800);
  CHECK(ph == 600);

  // Detach resets cleanly and is idempotent
  host.Detach();
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
  host.Detach();
  CHECK(!host.IsAttached());
  }

  SECTION("StandaloneSDL2 scale, resize & SDL_QUIT handling") {
  using GlideWrapper::NativeSurfaceHost;

  struct SdlDriverGuard {
    bool hadPrev = false;
    std::string prev;
    SdlDriverGuard() {
      if (const char* v = std::getenv("SDL_VIDEODRIVER")) {
        hadPrev = true;
        prev = v;
      }
      ::setenv("SDL_VIDEODRIVER", "dummy", 1);
    }
    ~SdlDriverGuard() {
      if (hadPrev) {
        ::setenv("SDL_VIDEODRIVER", prev.c_str(), 1);
      } else {
        ::unsetenv("SDL_VIDEODRIVER");
      }
    }
  } driverGuard;

  NativeSurfaceHost host;
  REQUIRE(host.Attach(nullptr, 320, 240, false, /*forceNoWindow=*/false,
                      "Standalone Unit Test", 0, /*windowScale=*/2));
  CHECK(host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::StandaloneSDL2);
  CHECK(host.GetSDLWindow() != nullptr);
  CHECK(!host.IsWaylandSurface());
  CHECK(!host.IsX11Surface());
  CHECK(!host.WasQuitRequested());

  // Verify SDL_HINT_NO_SIGNAL_HANDLERS was set so Ctrl+C works in demos.
  const char* noSigHint = SDL_GetHint(SDL_HINT_NO_SIGNAL_HANDLERS);
  REQUIRE(noSigHint != nullptr);
  CHECK(std::string(noSigHint) == "1");

  uint32_t pw = 0, ph = 0;
  host.GetDrawableSize(pw, ph);
  CHECK(pw == 640);
  CHECK(ph == 480);
  CHECK(!host.SyncBounds(pw, ph));

  // Simulate user clicking window close / SDL_QUIT.
  SDL_Event quitEv{};
  quitEv.type = SDL_QUIT;
  REQUIRE(SDL_PushEvent(&quitEv) == 1);
  host.PollEvents();
  CHECK(host.WasQuitRequested());

  host.Detach();
  CHECK(!host.IsAttached());
  CHECK(host.GetSDLWindow() == nullptr);
  }

  SECTION("Graceful rejection of invalid X11 window ID") {
  using GlideWrapper::NativeSurfaceHost;

  // Invalid 32-bit X11 XID must fail cleanly without Xlib aborting the process.
  NativeSurfaceHost host;
  void* badX11Win = reinterpret_cast<void*>(static_cast<uintptr_t>(0x0000DEADUL));
  CHECK(!host.Attach(badX11Win, 640, 480, true, /*forceNoWindow=*/false,
                     "Invalid X11 Test"));
  CHECK(!host.IsAttached());
  CHECK(host.GetMode() == NativeSurfaceHost::Mode::Detached);
  }

  SECTION("DOSBox setConfig SDL2 handshake export check") {
#ifdef __linux__
  using SetConfigFn = void (*)(FxU32, void*);

  for (bool glide3 : {false, true}) {
    void* handle = glide3 ? OpenGlide3DsoHandle() : OpenGlide2DsoHandle();
    REQUIRE(handle != nullptr);

    auto setConfigFn = reinterpret_cast<SetConfigFn>(::dlsym(handle, "setConfig"));
    auto setConfigResFn = ::dlsym(handle, "setConfigRes");
    REQUIRE(setConfigFn != nullptr);
    REQUIRE(setConfigResFn != nullptr);

    uint32_t magic = 0x58326724;  // '$g2X' (SVN DOSBox initial sentinel)
    setConfigFn(1, &magic);
    CHECK(magic == 0x324c4453U);  // 'SDL2'

    // Null magic pointer must be safe.
    setConfigFn(0, nullptr);

    ::dlclose(handle);
  }
#endif
  }

  SECTION("Fullscreen pointer inverse mapping & auto config") {
  // 800x600 inside 2560x1600 (16:10 laptop display): pillarboxed (213,0,2133x1600)
  int32_t gx = -1, gy = -1;
  const auto rect1600 =
      GlideWrapper::NativeSurfaceHost::ComputePresentRect(800, 600, 2560, 1600, true);
  CHECK(rect1600.x == 213);
  CHECK(rect1600.y == 0);
  CHECK(rect1600.width == 2133);
  CHECK(rect1600.height == 1600);

  GlideWrapper::NativeSurfaceHost::MapHostPointToGuest(
      1280, 800, 800, 600, 2560, 1600, gx, gy, true);
  CHECK(gx == 400);
  CHECK(gy == 300);

  // Left & right pillarbox black bar clamping
  GlideWrapper::NativeSurfaceHost::MapHostPointToGuest(
      50, 0, 800, 600, 2560, 1600, gx, gy, true);
  CHECK(gx == 0);
  CHECK(gy == 0);
  GlideWrapper::NativeSurfaceHost::MapHostPointToGuest(
      2500, 1599, 800, 600, 2560, 1600, gx, gy, true);
  CHECK(gx == 799);
  CHECK(gy == 599);

  // 800x600 inside 1280x800 (16:10): exact center maps to (400, 300)
  GlideWrapper::NativeSurfaceHost::MapHostPointToGuest(
      640, 400, 800, 600, 1280, 800, gx, gy, true);
  CHECK(gx == 400);
  CHECK(gy == 300);

  // 1:1 desktop identity fast path
  GlideWrapper::NativeSurfaceHost::MapHostPointToGuest(
      123, 456, 800, 600, 800, 600, gx, gy, true);
  CHECK(gx == 123);
  CHECK(gy == 456);

  // Config fullscreen = auto parses cleanly in both INI and env overrides
  const std::string iniPath = "/tmp/glide_ng_fullscreen_auto_test.ini";
  {
    std::ofstream out(iniPath);
    out << "[display]\nfullscreen = auto\n";
  }
  GlideWrapper::IniConfigLoader loader;
  GlideWrapper::WrapperConfig cfg;
  REQUIRE(loader.Load(iniPath, cfg));
  CHECK(cfg.fullscreen == GlideWrapper::WrapperConfig::DefaultFullscreenForPlatform());
  CHECK(cfg.settingSources["fullscreen"] == "ini");
  CHECK(cfg.invalidConfigValues.empty());
  std::remove(iniPath.c_str());

  // Post-fullscreen Win32 settle-window deactivation filter
  intptr_t lr = -1;
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
      0x001Cu /* WM_ACTIVATEAPP */, 0u /* FALSE */, lr));
  CHECK(lr == 0);
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x001Cu /* WM_ACTIVATEAPP */, 1u /* TRUE */, lr));
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
      0x0008u /* WM_KILLFOCUS */, 0u, lr));
  CHECK(lr == 0);
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x0007u /* WM_SETFOCUS */, 0u, lr));
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
      0x0006u /* WM_ACTIVATE */, 0u /* WA_INACTIVE */, lr));
  CHECK(lr == 0);
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
      0x0006u /* WM_ACTIVATE */, 0x00010000u /* minimized | WA_INACTIVE */, lr));
  CHECK(lr == 0);
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x0006u /* WM_ACTIVATE */, 1u /* WA_ACTIVE */, lr));
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x0006u /* WM_ACTIVATE */, 2u /* WA_CLICKACTIVE */, lr));
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
      0x0086u /* WM_NCACTIVATE */, 0u /* FALSE */, lr));
  CHECK(lr == 1);
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x0086u /* WM_NCACTIVATE */, 1u /* TRUE */, lr));
  CHECK_FALSE(
      GlideWrapper::NativeSurfaceHost::ShouldSuppressWin32DeactivateBounce(
          0x0010u /* WM_CLOSE */, 0u, lr));

  // Deactivation SWP_NOACTIVATE guard (prevents SetWindowPos(HWND_BOTTOM)
  // inside WM_ACTIVATEAPP(FALSE) from stealing foreground focus back).
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldForceWin32SwpNoActivate(
      /*inDeactivating=*/true, /*hwndInsertAfter=*/1u /* HWND_BOTTOM */,
      0x0003u /* SWP_NOMOVE | SWP_NOSIZE */));
  CHECK(GlideWrapper::NativeSurfaceHost::ShouldForceWin32SwpNoActivate(
      /*inDeactivating=*/false, /*hwndInsertAfter=*/1u /* HWND_BOTTOM */,
      0x0003u /* SWP_NOMOVE | SWP_NOSIZE */));
  CHECK_FALSE(GlideWrapper::NativeSurfaceHost::ShouldForceWin32SwpNoActivate(
      /*inDeactivating=*/true, /*hwndInsertAfter=*/1u /* HWND_BOTTOM */,
      0x0013u /* SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE */));
  CHECK_FALSE(GlideWrapper::NativeSurfaceHost::ShouldForceWin32SwpNoActivate(
      /*inDeactivating=*/false, /*hwndInsertAfter=*/0u /* HWND_TOP */,
      0x0003u /* SWP_NOMOVE | SWP_NOSIZE */));
  CHECK_FALSE(GlideWrapper::NativeSurfaceHost::ShouldForceWin32SwpNoActivate(
      /*inDeactivating=*/false, /*hwndInsertAfter=*/1u /* HWND_BOTTOM */,
      0x0007u /* SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER */));

  // Style restoration preserves live WS_VISIBLE/WS_MINIMIZE/WS_EX_TOPMOST
  // instead of re-applying stale startup WS_EX_TOPMOST after Alt+Tab.
  uint32_t outStyle = 0u;
  uint32_t outExStyle = 0u;
  GlideWrapper::NativeSurfaceHost::ComputeRestoredWin32Styles(
      /*savedStyle=*/0x96000000u /* WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN */,
      /*savedExStyle=*/0x00000008u /* WS_EX_TOPMOST */,
      /*curStyle=*/0x96000000u,
      /*curExStyle=*/0x00000000u /* app cleared WS_EX_TOPMOST via HWND_BOTTOM */,
      /*forceDropTopmost=*/false, outStyle, outExStyle);
  CHECK(outStyle == 0x96000000u);
  CHECK((outExStyle & 0x00000008u) == 0u);

  // Explicit dropTopmost clears WS_EX_TOPMOST even if still set on curExStyle,
  // while preserving live WS_MINIMIZE and non-topmost extended styles.
  GlideWrapper::NativeSurfaceHost::ComputeRestoredWin32Styles(
      /*savedStyle=*/0x10CF0000u /* WS_VISIBLE | WS_OVERLAPPEDWINDOW */,
      /*savedExStyle=*/0x00040108u /* WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_TOPMOST */,
      /*curStyle=*/0xB6000000u /* WS_POPUP | WS_MINIMIZE | WS_VISIBLE | ... */,
      /*curExStyle=*/0x00040008u /* WS_EX_APPWINDOW | WS_EX_TOPMOST */,
      /*forceDropTopmost=*/true, outStyle, outExStyle);
  CHECK((outStyle & 0x20000000u /* WS_MINIMIZE */) != 0u);
  CHECK((outStyle & 0x00CF0000u /* chrome */) == 0x00CF0000u);
  CHECK((outExStyle & 0x00000008u /* WS_EX_TOPMOST */) == 0u);
  CHECK((outExStyle & 0x00040100u /* WS_EX_APPWINDOW | WS_EX_WINDOWEDGE */) == 0x00040100u);
  }

}

TEST_CASE("Architecture: GPU Backend GlideStateTracker Separation", "[Architecture][GlideStateTracker]") {
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::VulkanBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::OpenGLESBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::GlideStateTracker,
                                  GlideWrapper::SoftwareBackend>);
  static_assert(!std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                   GlideWrapper::VulkanBackend>);
  static_assert(!std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                   GlideWrapper::OpenGLESBackend>);
  static_assert(std::is_base_of_v<GlideWrapper::SoftwareBackendBase,
                                  GlideWrapper::SoftwareBackend>);

  SECTION("VulkanBackend allocates zero CPU LFB staging until LFB is touched") {
    GlideWrapper::WrapperConfig config;
    config.backend = "vulkan";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::VulkanBackend backend;
    GlideWrapper::IGraphicsBackend* basePtr = &backend;
    CHECK(dynamic_cast<GlideWrapper::GlideStateTracker*>(basePtr) != nullptr);
    CHECK(dynamic_cast<GlideWrapper::SoftwareBackendBase*>(basePtr) == nullptr);

    if (backend.Initialize(config) && backend.AttachWindow(nullptr, 64, 64, true)) {
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.ClearBuffer(0x0000FF00, 0xFF, 1.0f, 0x3);
      REQUIRE(backend.SwapBuffers());
      // Pure 3D clear + swap must still keep CPU LFB staging unallocated.
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());

      // First LFB write lazily allocates and survives GPU flush + readback.
      uint16_t white565 = 0xFFFF;
      REQUIRE(backend.WriteLFB(1, 4, 4, 1, 1, 2, 0 /* 565 */, &white565));
      CHECK(backend.HasAllocatedCpuLfbStaging());

      uint16_t readPx = 0;
      REQUIRE(backend.ReadLFB(1, 4, 4, 1, 1, 2, &readPx));
      CHECK(readPx == 0xFFFF);

      backend.DetachWindow();
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.Shutdown();
    }
  }

  SECTION("OpenGLESBackend allocates zero CPU LFB staging until LFB is touched") {
    GlideWrapper::WrapperConfig config;
    config.backend = "opengl_es";
    config.forceNoWindow = true;
    config.maxFps = 0.0;

    GlideWrapper::OpenGLESBackend backend;
    GlideWrapper::IGraphicsBackend* basePtr = &backend;
    CHECK(dynamic_cast<GlideWrapper::GlideStateTracker*>(basePtr) != nullptr);
    CHECK(dynamic_cast<GlideWrapper::SoftwareBackendBase*>(basePtr) == nullptr);

    if (backend.Initialize(config) && backend.AttachWindow(nullptr, 64, 64, true)) {
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.ClearBuffer(0x0000FF00, 0xFF, 1.0f, 0x3);
      REQUIRE(backend.SwapBuffers());
      // Pure 3D clear + swap must still keep CPU LFB staging unallocated.
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());

      // First LFB write lazily allocates and survives GPU flush + readback.
      uint16_t white565 = 0xFFFF;
      REQUIRE(backend.WriteLFB(1, 4, 4, 1, 1, 2, 0 /* 565 */, &white565));
      CHECK(backend.HasAllocatedCpuLfbStaging());

      uint16_t readPx = 0;
      REQUIRE(backend.ReadLFB(1, 4, 4, 1, 1, 2, &readPx));
      CHECK(readPx == 0xFFFF);

      backend.DetachWindow();
      CHECK_FALSE(backend.HasAllocatedCpuLfbStaging());
      backend.Shutdown();
    }
  }
}

TEST_CASE("Diagnostics: Tiered Logger, Telemetry & On-Screen HUD", "[Logger][Telemetry][HUD]") {
  SECTION("Tiered logger levels, categories & trace windowing") {
  using namespace GlideWrapper;

  SECTION("Category parser & formatter round-trip and negation") {
    uint32_t mask = 0u;
    REQUIRE(ParseLogCategories("all", mask));
    CHECK(mask == LOG_CAT_ALL);
    CHECK(FormatLogCategories(mask) == "all");

    REQUIRE(ParseLogCategories("none", mask));
    CHECK(mask == LOG_CAT_NONE);
    CHECK(FormatLogCategories(mask) == "none");

    REQUIRE(ParseLogCategories("lfb, perf, api", mask));
    CHECK((mask & LOG_CAT_LFB) != 0u);
    CHECK((mask & LOG_CAT_PERF) != 0u);
    CHECK((mask & LOG_CAT_API) != 0u);
    CHECK((mask & LOG_CAT_DRAW) == 0u);
    CHECK(FormatLogCategories(mask) == "api,lfb,perf");

    // Negation starts from ALL and clears specified categories
    REQUIRE(ParseLogCategories("-draw,-state", mask));
    CHECK((mask & LOG_CAT_DRAW) == 0u);
    CHECK((mask & LOG_CAT_STATE) == 0u);
    CHECK((mask & LOG_CAT_LFB) != 0u);
    CHECK((mask & LOG_CAT_API) != 0u);

    // Unknown-only string returns false
    uint32_t untouched = 0x1234u;
    CHECK_FALSE(ParseLogCategories("bogus_category", untouched));
    CHECK(untouched == 0x1234u);
  }

  SECTION("TRACE gating, category filtering, and microsecond + frame format") {
    const std::string logPath = "/tmp/glide_ng_m2_logger_test.log";
    std::remove(logPath.c_str());

    WrapperConfig cfg{};
    cfg.logFile = logPath;
    cfg.logLevel = 5u;  // TRACE
    cfg.logToConsole = false;
    REQUIRE(ParseLogCategories("lfb,perf", cfg.logCategoryMask));
    cfg.traceFrameWindowActive = true;
    cfg.traceFrameStart = 2;
    cfg.traceFrameEnd = 3;

    auto& logger = Logger::GetInstance();
    logger.Initialize(cfg);
    REQUIRE(logger.GetLevel() == LogLevel::TRACE);

    // Frame 1 (outside trace window [2,3]): TRACE suppressed, DEBUG on LFB passes
    logger.SetCurrentFrame(1);
    GLIDE_LOG(TRACE, "LFB", "frame1_trace_should_skip");
    GLIDE_LOG(DEBUG, "LFB", "frame1_debug_lfb_pass");
    GLIDE_LOG(DEBUG, "Vulkan", "frame1_debug_draw_filtered");
    GLIDE_LOG(WARN, "Vulkan", "frame1_warn_bypasses_category");
    GLIDE_LOG(INFO, "Startup", "frame1_startup_bypasses_category");

    // Frame 2 & 3 (inside trace window [2,3]): TRACE on LFB passes, TRACE on State filtered
    logger.SetCurrentFrame(2);
    GLIDE_LOG(TRACE, "LFB", "frame2_trace_lfb_pass");
    GLIDE_LOG(TRACE, "StateTracker", "frame2_trace_state_filtered");

    logger.SetCurrentFrame(3);
    GLIDE_TRACE(LOG_CAT_PERF, "Perf", "frame3_trace_perf_pass");

    // Frame 4 (after trace window [2,3]): TRACE suppressed again
    logger.SetCurrentFrame(4);
    GLIDE_LOG(TRACE, "LFB", "frame4_trace_should_skip");
    logger.Flush();

    std::ifstream in(logPath);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    std::remove(logPath.c_str());

    // Restore clean logger state for subsequent tests
    logger.Initialize("/tmp/glide_ng_test_restore.log", LogLevel::WARN, false);
    std::remove("/tmp/glide_ng_test_restore.log");

    CHECK(content.find("frame1_trace_should_skip") == std::string::npos);
    CHECK(content.find("frame1_debug_lfb_pass") != std::string::npos);
    CHECK(content.find("frame1_debug_draw_filtered") == std::string::npos);
    CHECK(content.find("frame1_warn_bypasses_category") != std::string::npos);
    CHECK(content.find("frame1_startup_bypasses_category") != std::string::npos);
    CHECK(content.find("frame2_trace_lfb_pass") != std::string::npos);
    CHECK(content.find("frame2_trace_state_filtered") == std::string::npos);
    CHECK(content.find("frame3_trace_perf_pass") != std::string::npos);
    CHECK(content.find("frame4_trace_should_skip") == std::string::npos);

    // Verify monotonic microsecond timestamp + frame ID format:
    // [+SSSSSS.UUUUUU] [F000002] [TRCE] [LFB] frame2_trace_lfb_pass
    std::regex lineRe(
        R"(\[\+\d{6}\.\d{6}\] \[F000002\] \[TRCE\] \[LFB\] frame2_trace_lfb_pass)");
    CHECK(std::regex_search(content, lineRe));
  }

  SECTION("DEBUG level suppresses TRACE messages even without frame window") {
    const std::string logPath = "/tmp/glide_ng_m2_debug_level_test.log";
    std::remove(logPath.c_str());

    auto& logger = Logger::GetInstance();
    logger.Initialize(logPath, LogLevel::DEBUG, false);
    GLIDE_LOG(DEBUG, "Frontend", "debug_msg_visible");
    GLIDE_LOG(TRACE, "Frontend", "trace_msg_hidden");
    logger.Flush();

    std::ifstream in(logPath);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    std::remove(logPath.c_str());
    logger.Initialize("/tmp/glide_ng_test_restore.log", LogLevel::WARN, false);
    std::remove("/tmp/glide_ng_test_restore.log");

    CHECK(content.find("debug_msg_visible") != std::string::npos);
    CHECK(content.find("trace_msg_hidden") == std::string::npos);
  }
  }

  SECTION("Per-frame telemetry config & CSV output") {
#ifdef __linux__
  struct EnvGuardM2 {
    std::string name;
    bool hadValue{false};
    std::string oldValue;
    explicit EnvGuardM2(const char* n) : name(n) {
      if (const char* v = std::getenv(n)) {
        hadValue = true;
        oldValue = v;
      }
      ::unsetenv(n);
    }
    ~EnvGuardM2() {
      if (hadValue) ::setenv(name.c_str(), oldValue.c_str(), 1);
      else ::unsetenv(name.c_str());
    }
  };

  EnvGuardM2 gConfig("GLIDE_NG_CONFIG");
  EnvGuardM2 gCats("GLIDE_NG_LOG_CATEGORIES");
  EnvGuardM2 gFrames("GLIDE_NG_TRACE_FRAMES");
  EnvGuardM2 gPerf("GLIDE_NG_PERF_LOG");
  EnvGuardM2 gLogFile("GLIDE_NG_LOG_FILE");
  EnvGuardM2 gLogLevel("GLIDE_NG_LOG_LEVEL");
  EnvGuardM2 gHeadless("GLIDE_NG_HEADLESS");
  EnvGuardM2 gMaxFps("GLIDE_NG_MAX_FPS");

  SECTION("INI & GLIDE_NG_* parsing for logCategories, traceFrames, perfLog") {
    const std::string cfgPath = "/tmp/glide_ng_m2_cfg_test.ini";
    {
      std::ofstream out(cfgPath);
      out << "[logging]\n"
          << "categories = api,lfb\n"
          << "trace_frames = 10-20\n"
          << "perf_log = /tmp/ini_perf.csv\n";
    }

    GlideWrapper::IniConfigLoader loader;
    GlideWrapper::WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.logCategoriesSpec == "api,lfb");
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 10u);
    CHECK(cfg.traceFrameEnd == 20u);
    CHECK(cfg.perfLogEnabled == true);
    CHECK(cfg.perfLogCsvPath == "/tmp/ini_perf.csv");
    CHECK(cfg.settingSources.at("logCategories") == "ini");
    CHECK(cfg.settingSources.at("traceFrames") == "ini");
    CHECK(cfg.settingSources.at("perfLog") == "ini");

    // Test single-frame, open-ended, and prefix ranges via env overrides
    ::setenv("GLIDE_NG_TRACE_FRAMES", "7", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 7u);
    CHECK(cfg.traceFrameEnd == 7u);

    ::setenv("GLIDE_NG_TRACE_FRAMES", "15-", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 15u);
    CHECK(cfg.traceFrameEnd == UINT64_MAX);

    ::setenv("GLIDE_NG_TRACE_FRAMES", "-5", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.traceFrameWindowActive == true);
    CHECK(cfg.traceFrameStart == 0u);
    CHECK(cfg.traceFrameEnd == 5u);

    ::setenv("GLIDE_NG_LOG_CATEGORIES", "tex,perf", 1);
    ::setenv("GLIDE_NG_PERF_LOG", "1", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.logCategoriesSpec == "tex,perf");
    CHECK(cfg.perfLogEnabled == true);
    CHECK(cfg.perfLogCsvPath.empty());
    CHECK(cfg.settingSources.at("perfLog") == "GLIDE_NG_PERF_LOG");

    ::setenv("GLIDE_NG_PERF_LOG", "0", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.perfLogEnabled == false);

    std::remove(cfgPath.c_str());
  }

  SECTION("End-to-end Glide 3.x session produces valid CSV & [PERF] summary") {
    const std::string csvPath = "/tmp/glide_ng_m2_perf_e2e.csv";
    const std::string logPath = "/tmp/glide_ng_m2_perf_e2e.log";
    std::remove(csvPath.c_str());
    std::remove(logPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_PERF_LOG", csvPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
        ::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));
    auto lfbWriteRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxBool, FxI32, void*)>(
        ::dlsym(handle, "grLfbWriteRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(drawTri != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbWriteRegion != nullptr);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    StandardVert v0 = {100.0f, 100.0f, 0.0f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v1 = {300.0f, 100.0f, 0.0f, 0.0f, 0.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v2 = {200.0f, 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<uint16_t> patch(16 * 16, 0xF800);

    for (int f = 0; f < 3; ++f) {
      bufferClear(0x00000000, 0, 0xFFFF);
      drawTri(&v0, &v1, &v2);
      drawTri(&v0, &v1, &v2);
      lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                     16, 16, FXFALSE, 16 * 2, patch.data());
      bufferSwap(0);
    }

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    // Verify CSV header + 3 frame rows with expected counters
    std::ifstream csvIn(csvPath);
    REQUIRE(csvIn.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(csvIn, line)) {
      if (!line.empty()) lines.push_back(line);
    }
    csvIn.close();
    REQUIRE(lines.size() == 4);  // 1 header + 3 frames
    CHECK(lines[0].find("frame,timestamp_ms,frame_ms,render_ms,present_ms") == 0);
    CHECK(lines[0].find(",wrapper_cpu_ms,app_cpu_ms,draw_cpu_ms,tex_cpu_ms,"
                        "lfb_cpu_ms,state_cpu_ms") != std::string::npos);
    // Row 1 (frame 0): check tris_processed=2, tris_drawn=2, lfb_writes=1, lfb_write_bytes=512
    CHECK(lines[1].find(",2,2,0,1,0,512,") != std::string::npos);

    // Verify log file contains [Perf] summary emitted on ShutdownPerfLog
    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    std::string logContent((std::istreambuf_iterator<char>(logIn)),
                           std::istreambuf_iterator<char>());
    logIn.close();
    CHECK(logContent.find("[Perf] FPS=") != std::string::npos);
    CHECK(logContent.find("[app=") != std::string::npos);
    CHECK(logContent.find("wrap=") != std::string::npos);
    CHECK(logContent.find("Perf Telemetry       : ENABLED") != std::string::npos);

    std::remove(csvPath.c_str());
    std::remove(logPath.c_str());
  }
#endif
  }

  SECTION("Telemetry CPU bucket attribution (app vs wrapper)") {
  using namespace GlideWrapper;
  auto& tm = TelemetryManager::GetInstance();

  SECTION("Disabled perf log keeps timing inactive with zero bucket time") {
    tm.ShutdownPerfLog();
    CHECK_FALSE(tm.IsPerfTimingEnabled());
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    tm.OnFrameComplete(2000, 2000, 0, 500.0f);
    const FrameMetrics m = tm.GetLastFrameMetrics();
    CHECK(m.wrapperCpuMs == 0.0f);
    CHECK(m.drawCpuMs == 0.0f);
    CHECK(m.texCpuMs == 0.0f);
    CHECK(m.lfbCpuMs == 0.0f);
    CHECK(m.stateCpuMs == 0.0f);
  }

  SECTION("Exclusive stack prevents double-counting on same & cross-bucket nesting") {
    tm.ConfigurePerfLog(true, "");
    REQUIRE(tm.IsPerfTimingEnabled());

    // 1. Same-bucket nesting: outer Draw sleeps ~2ms + inner Draw sleeps ~2ms
    //    -> total elapsed is ~4ms (NOT ~6ms from double-counting).
    const auto t0 = std::chrono::steady_clock::now();
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      {
        GLIDE_CPU_SCOPE(CpuBucket::Draw);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    // 2. Cross-bucket nesting: outer Draw sleeps ~2ms, inner Tex sleeps ~4ms
    //    -> Draw gains ~2ms, Tex gains ~4ms (Draw does NOT include Tex's 4ms).
    {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      {
        GLIDE_CPU_SCOPE(CpuBucket::Tex);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
    }
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
    const int64_t simulatedAppUs = 5000;
    const int64_t totalActiveUs = elapsedUs + simulatedAppUs;

    tm.OnFrameComplete(totalActiveUs, totalActiveUs, 0, 0.0f);
    const FrameMetrics m = tm.GetLastFrameMetrics();

    // Total wrapper CPU time must not exceed total measured elapsed wall time
    // (+ small scheduling epsilon), proving zero double-counting.
    const double elapsedMs = static_cast<double>(elapsedUs) / 1000.0;
    CHECK(m.drawCpuMs >= 5.0f);
    CHECK(m.texCpuMs >= 3.0f);
    CHECK(m.lfbCpuMs == 0.0f);
    CHECK(m.stateCpuMs == 0.0f);
    CHECK(std::fabs(m.wrapperCpuMs - (m.drawCpuMs + m.texCpuMs)) < 0.05f);
    CHECK(m.wrapperCpuMs <= static_cast<float>(elapsedMs + 0.5));
    CHECK(m.appCpuMs >= 4.0f);
    CHECK(std::fabs((m.appCpuMs + m.wrapperCpuMs) - m.renderTimeMs) < 0.05f);

    tm.ShutdownPerfLog();
    CHECK_FALSE(tm.IsPerfTimingEnabled());
  }

  SECTION("CpuBucketScope TSC fast path stays calibrated and low-overhead over 10k scopes") {
    tm.ConfigurePerfLog(true, "");
    REQUIRE(tm.IsPerfTimingEnabled());

    const auto tStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
      GLIDE_CPU_SCOPE(CpuBucket::Draw);
    }
    const double loopElapsedMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tStart)
            .count();

    if (tm.IsUsingTscClock()) {
      CHECK(tm.GetTscNsPerTick() >= 0.05);
      CHECK(tm.GetTscNsPerTick() <= 10.0);
      CHECK(loopElapsedMs < 15.0);
    }

    {
      GLIDE_CPU_SCOPE(CpuBucket::Tex);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const int64_t elapsedUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tStart)
            .count();
    tm.OnFrameComplete(elapsedUs, elapsedUs, 0, 0.0f);
    const FrameMetrics m = tm.GetLastFrameMetrics();
    CHECK(m.drawCpuMs >= 0.0f);
    CHECK(m.texCpuMs >= 2.0f);
    CHECK(m.texCpuMs <= 20.0f);

    tm.ShutdownPerfLog();
    CHECK_FALSE(tm.IsPerfTimingEnabled());
  }

#ifdef __linux__
  SECTION("End-to-end Glide 3.x session splits app_cpu_ms from wrapper buckets in CSV") {
    const std::string csvPath = "/tmp/glide_ng_m6_cpu_split.csv";
    std::remove(csvPath.c_str());

    struct EnvGuardM6 {
      std::vector<std::string> keys = {
          "GLIDE_NG_HEADLESS", "GLIDE_NG_MAX_FPS", "GLIDE_NG_PERF_LOG",
          "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_NO_SPLASH"};
      std::vector<std::pair<std::string, std::string>> saved;
      EnvGuardM6() {
        for (const auto& k : keys) {
          const char* v = std::getenv(k.c_str());
          saved.emplace_back(k, v ? std::string(v) : std::string("\x01UNSET"));
        }
      }
      ~EnvGuardM6() {
        for (const auto& kv : saved) {
          if (kv.second == "\x01UNSET") {
            ::unsetenv(kv.first.c_str());
          } else {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
          }
        }
      }
    } guard;

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_NO_SPLASH", "1", 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "warn", 1);
    ::setenv("GLIDE_NG_PERF_LOG", csvPath.c_str(), 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto vertexLayout = reinterpret_cast<void (*)(FxU32, FxI32, FxU32)>(
        ::dlsym(handle, "grVertexLayout"));
    auto colorCombine = reinterpret_cast<void (*)(GrCombineFunction_t, GrCombineFactor_t,
                                                  GrCombineLocal_t, GrCombineOther_t, FxBool)>(
        ::dlsym(handle, "grColorCombine"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto drawTri = reinterpret_cast<void (*)(const void*, const void*, const void*)>(
        ::dlsym(handle, "grDrawTriangle"));
    auto lfbWriteRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, GrLfbSrcFmt_t, FxU32, FxU32, FxBool, FxI32, void*)>(
        ::dlsym(handle, "grLfbWriteRegion"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    vertexLayout(0x01, 0, 1);
    vertexLayout(0x02, 8, 1);
    vertexLayout(0x20, 12, 1);
    vertexLayout(0x10, 28, 1);
    vertexLayout(0x03, 32, 1);
    colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    StandardVert v0 = {100.0f, 100.0f, 0.0f, 0.0f, 255.0f, 0.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v1 = {300.0f, 100.0f, 0.0f, 0.0f, 0.0f, 255.0f, 0.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    StandardVert v2 = {200.0f, 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255.0f, 255.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<uint16_t> patch(32 * 32, 0x07E0);

    for (int f = 0; f < 2; ++f) {
      // Simulate external app/DOSBox CPU work outside Glide API calls.
      std::this_thread::sleep_for(std::chrono::milliseconds(6));
      colorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
      bufferClear(0x00000000, 0, 0xFFFF);
      for (int t = 0; t < 20; ++t) {
        drawTri(&v0, &v1, &v2);
      }
      lfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                     32, 32, FXFALSE, 32 * 2, patch.data());
      bufferSwap(0);
    }

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    std::ifstream csvIn(csvPath);
    REQUIRE(csvIn.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(csvIn, line)) {
      if (!line.empty()) lines.push_back(line);
    }
    csvIn.close();
    REQUIRE(lines.size() == 3);  // header + 2 frames

    auto splitCsv = [](const std::string& row) {
      std::vector<std::string> cols;
      size_t start = 0;
      while (start <= row.size()) {
        size_t pos = row.find(',', start);
        if (pos == std::string::npos) {
          cols.push_back(row.substr(start));
          break;
        }
        cols.push_back(row.substr(start, pos - start));
        start = pos + 1;
      }
      return cols;
    };

    const auto hdr = splitCsv(lines[0]);
    REQUIRE(hdr.size() == 24);
    CHECK(hdr[18] == "wrapper_cpu_ms");
    CHECK(hdr[19] == "app_cpu_ms");
    CHECK(hdr[20] == "draw_cpu_ms");
    CHECK(hdr[21] == "tex_cpu_ms");
    CHECK(hdr[22] == "lfb_cpu_ms");
    CHECK(hdr[23] == "state_cpu_ms");

    // Frame 1 (lines[2]) has a clean frameStart->frameEnd interval including
    // the 6 ms simulated app sleep.
    const auto f1 = splitCsv(lines[2]);
    REQUIRE(f1.size() == 24);
    const double wrapMs = std::stod(f1[18]);
    const double appMs = std::stod(f1[19]);
    const double drawMs = std::stod(f1[20]);
    const double lfbMs = std::stod(f1[22]);
    CHECK(wrapMs > 0.0);
    CHECK(drawMs > 0.0);
    CHECK(lfbMs >= 0.0);
    CHECK(appMs >= 4.0);

    std::remove(csvPath.c_str());
  }
#endif
  }

  SECTION("DXVK-style on-screen HUD rendering & isolation") {
#ifdef __linux__
  using namespace GlideWrapper;

  struct HudEnvGuard {
    std::vector<std::pair<std::string, std::string>> saved;
    std::vector<std::string> unsetKeys;
    const std::vector<std::string> keys = {
        "GLIDE_NG_HUD", "GLIDE_NG_CONFIG", "GLIDE_NG_HEADLESS",
        "GLIDE_NG_LOG_FILE", "GLIDE_NG_LOG_LEVEL", "GLIDE_NG_MAX_FPS"};
    HudEnvGuard() {
      for (const auto& k : keys) {
        const char* v = std::getenv(k.c_str());
        if (v) {
          saved.emplace_back(k, v);
        } else {
          unsetKeys.push_back(k);
        }
        ::unsetenv(k.c_str());
      }
    }
    ~HudEnvGuard() {
      for (const auto& k : keys) ::unsetenv(k.c_str());
      for (const auto& kv : saved) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }
  } guard;

  SECTION("GLIDE_NG_HUD and INI [hud] parsing + precedence") {
    const std::string cfgPath = "/tmp/glide_ng_m3_hud_cfg.ini";
    {
      std::ofstream out(cfgPath);
      out << "[hud]\n"
          << "elements = fps,backend\n"
          << "position = br\n"
          << "scale = 1.5\n"
          << "opacity = 0.6\n";
    }

    IniConfigLoader loader;
    WrapperConfig cfg;
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == (HUD_FPS | HUD_BACKEND));
    CHECK(cfg.hudPosition == HudPosition::BOTTOM_RIGHT);
    CHECK(std::fabs(cfg.hudScale - 1.5f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.6f) < 1e-4f);
    CHECK(cfg.settingSources.at("hud") == "ini");

    // GLIDE_NG_HUD=full overrides INI
    ::setenv("GLIDE_NG_HUD", "full,position=tr,scale=2.0,opacity=0.9", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_FULL);
    CHECK(cfg.hudPosition == HudPosition::TOP_RIGHT);
    CHECK(std::fabs(cfg.hudScale - 2.0f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.9f) < 1e-4f);
    CHECK(cfg.settingSources.at("hud") == "GLIDE_NG_HUD");

    // GLIDE_NG_HUD=1 enables FPS shorthand
    ::setenv("GLIDE_NG_HUD", "1", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_FPS);

    // GLIDE_NG_HUD=0 disables even when INI enabled it
    ::setenv("GLIDE_NG_HUD", "0", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == HUD_NONE);

    // Specific multi-token & clamping checks
    ::setenv("GLIDE_NG_HUD", "frametimes,api,drawcalls,lfb,pos=bl,scale=9.0,opacity=2.5", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(cfg.hudMask == (HUD_FRAMETIMES | HUD_API | HUD_DRAWCALLS | HUD_LFB));
    CHECK(cfg.hudPosition == HudPosition::BOTTOM_LEFT);
    CHECK(std::fabs(cfg.hudScale - 4.0f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 1.0f) < 1e-4f);

    // Verify default opacity is 0.0 (borderless text+shadow) and scale accepts 0.25
    {
      WrapperConfig defCfg;
      CHECK(defCfg.hudOpacity == 0.0f);
      CHECK(defCfg.hudScale == 1.0f);
    }
    ::setenv("GLIDE_NG_HUD", "fps,scale=0.1,opacity=-0.5", 1);
    REQUIRE(loader.Load(cfgPath, cfg));
    CHECK(std::fabs(cfg.hudScale - 0.25f) < 1e-4f);
    CHECK(std::fabs(cfg.hudOpacity - 0.0f) < 1e-4f);

    std::remove(cfgPath.c_str());
  }

  SECTION("HudOverlay bitmap rasterization, placement & BGRA32 compositing") {
    WrapperConfig cfg;
    cfg.hudMask = HUD_FULL;
    cfg.hudPosition = HudPosition::TOP_LEFT;
    cfg.hudScale = 1.0f;
    cfg.hudOpacity = 0.8f;
    cfg.model = CardModel::Voodoo2;
    cfg.activeFrontendApi = "Glide3x";
    cfg.maxFps = 60.0f;

    HudOverlay hud;
    hud.Configure(cfg, {"VK", 640, 480});
    REQUIRE(hud.IsEnabled());

    FrameMetrics m{};
    m.frameTimeMs = 16.6f;
    m.renderTimeMs = 4.2f;
    m.swapPresentMs = 1.1f;
    m.drawCalls = 12;
    m.stateFlushes = 2;
    m.trianglesDrawn = 480;
    m.lfbWriteLocks = 3;
    m.lfbBytesWritten = 4096;
    m.texUploads = 1;
    m.texUploadBytes = 8192;

    hud.ForceRender(m, 60.0f);
    REQUIRE(hud.GetWidth() >= 100);
    REQUIRE(hud.GetHeight() >= 40);
    REQUIRE(hud.GetRevision() == 1);
    REQUIRE(hud.GetRgbaTopDown() != nullptr);
    REQUIRE(hud.GetRgbaBottomUp() != nullptr);

    // Verify lines cover all full elements
    const auto& lines = hud.GetLinesForTest();
    REQUIRE(lines.size() >= 6);
    CHECK(lines[0].find("FPS") != std::string::npos);
    CHECK(lines[0].find("60.0") != std::string::npos);

    // Top row of top-down buffer matches bottom row of bottom-up buffer
    const uint32_t w = hud.GetWidth();
    const uint32_t h = hud.GetHeight();
    const uint8_t* topDown = hud.GetRgbaTopDown();
    const uint8_t* bottomUp = hud.GetRgbaBottomUp();
    CHECK(std::memcmp(topDown, bottomUp + static_cast<size_t>(h - 1) * w * 4,
                      w * 4) == 0);

    // Border pixel at (0,0) should have non-zero alpha and amber color (R > G > B)
    CHECK(topDown[3] > 180);
    CHECK(topDown[0] > topDown[1]);
    CHECK(topDown[1] > topDown[2]);

    // Check corner placement math + small-viewport clamping
    int32_t x = 0, y = 0;
    uint32_t dw = 0, dh = 0;
    hud.ComputePlacement(0, 0, 640, 480, x, y, dw, dh);
    CHECK(x == 8);
    CHECK(y == 8);
    CHECK(dw == w);
    CHECK(dh == h);

    // Proportional scaling parity: 1280x960 host viewport (2x guest 640x480)
    // matches SoftwareBackend's 2x upscaled HUD height and margin.
    hud.ComputePlacement(0, 0, 1280, 960, x, y, dw, dh);
    CHECK(x == 16);
    CHECK(y == 16);
    CHECK(dw == w * 2u);
    CHECK(dh == h * 2u);

    // Transparent HUD (opacity=0.0): zero dark backdrop pill or border, and
    // frametime graph area keeps alpha == 0 (no stuck 120/255 box floor).
    {
      WrapperConfig clearCfg = cfg;
      clearCfg.hudOpacity = 0.0f;
      HudOverlay clearHud;
      clearHud.Configure(clearCfg, {"VK", 640, 480});
      clearHud.ForceRender(m, 60.0f);
      const uint8_t* td = clearHud.GetRgbaTopDown();
      const uint32_t cw = clearHud.GetWidth();
      const uint32_t ch = clearHud.GetHeight();
      CHECK(td[3] == 0u);  // Top-left corner (no border/backdrop pill)
      // Empty top-left cell inside frametime graph region must stay transparent
      const size_t graphRow = ch - 5u - 16u;
      const size_t graphCol = cw - 8u;
      CHECK(td[(graphRow * cw + graphCol) * 4u + 3u] == 0u);
    }

    cfg.hudPosition = HudPosition::BOTTOM_RIGHT;
    hud.Configure(cfg, {"VK", 640, 480});
    hud.ForceRender(m, 60.0f);
    hud.ComputePlacement(100, 20, 640, 480, x, y, dw, dh);
    CHECK(x + static_cast<int32_t>(dw) == 100 + 640 - 8);
    CHECK(y + static_cast<int32_t>(dh) == 20 + 480 - 8);

    // Small viewport forces proportional clamp without overflowing bounds
    hud.ComputePlacement(0, 0, 80, 60, x, y, dw, dh);
    CHECK(dw <= 80);
    CHECK(dh <= 60);

    // BGRA32 composite modifies inside HUD rect and leaves outside pixels untouched
    cfg.hudPosition = HudPosition::TOP_LEFT;
    hud.Configure(cfg, {"SW", 640, 480});
    hud.ForceRender(m, 60.0f);
    std::vector<uint32_t> frame(640 * 480, 0xFF112233u);
    hud.CompositeOverBgra32(frame.data(), 640, 480, 640);
    CHECK(frame[8 * 640 + 8] != 0xFF112233u);          // Inside HUD top-left border
    CHECK(frame[470 * 640 + 630] == 0xFF112233u);      // Outside HUD (bottom-right)
  }

  SECTION("End-to-end Glide 3.x session with GLIDE_NG_HUD=full keeps guest LFB pristine") {
    const std::string logPath = "/tmp/glide_ng_m3_hud_e2e.log";
    std::remove(logPath.c_str());

    ::setenv("GLIDE_NG_HEADLESS", "1", 1);
    ::setenv("GLIDE_NG_MAX_FPS", "0", 1);
    ::setenv("GLIDE_NG_LOG_FILE", logPath.c_str(), 1);
    ::setenv("GLIDE_NG_LOG_LEVEL", "info", 1);
    ::setenv("GLIDE_NG_HUD", "full", 1);

    void* handle = OpenGlide3Handle();
    REQUIRE(handle != nullptr);

    auto glideInit = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideInit"));
    auto glideShutdown = reinterpret_cast<void (*)()>(::dlsym(handle, "grGlideShutdown"));
    auto sstSelect = reinterpret_cast<void (*)(int)>(::dlsym(handle, "grSstSelect"));
    auto winOpen = reinterpret_cast<FxU32 (*)(FxU32, FxU32, FxU32, FxU32, FxU32, int, int)>(
        ::dlsym(handle, "grSstWinOpen"));
    auto winClose = reinterpret_cast<FxBool (*)(FxU32)>(::dlsym(handle, "grSstWinClose"));
    auto bufferClear = reinterpret_cast<void (*)(GrColor_t, GrAlpha_t, FxU32)>(
        ::dlsym(handle, "grBufferClear"));
    auto bufferSwap = reinterpret_cast<void (*)(FxU32)>(::dlsym(handle, "grBufferSwap"));
    auto lfbReadRegion = reinterpret_cast<FxBool (*)(
        GrBuffer_t, FxU32, FxU32, FxU32, FxU32, FxU32, void*)>(
        ::dlsym(handle, "grLfbReadRegion"));

    REQUIRE(glideInit != nullptr);
    REQUIRE(winOpen != nullptr);
    REQUIRE(bufferClear != nullptr);
    REQUIRE(bufferSwap != nullptr);
    REQUIRE(lfbReadRegion != nullptr);

    glideInit();
    sstSelect(0);
    FxU32 ctx = winOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                        GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    REQUIRE(ctx != 0);

    // Clear both buffers to solid bright green (ARGB 0xFF00FF00)
    for (int f = 0; f < 3; ++f) {
      bufferClear(0xFF00FF00, 0xFF, 0xFFFF);
      bufferSwap(0);
    }

    // Read top-left 16x16 region of frontbuffer where top-left HUD is drawn
    // during presentation: guest framebuffer must remain pure green (not overwritten by HUD).
    std::vector<uint16_t> pixels565(16 * 16, 0);
    FxBool ok = lfbReadRegion(GR_BUFFER_FRONTBUFFER, 8, 8, 16, 16, 16 * 2,
                              pixels565.data());
    REQUIRE(ok == FXTRUE);
    int pureGreenCount = 0;
    for (uint16_t p : pixels565) {
      if (p == 0x07E0) ++pureGreenCount;
    }
    CHECK(pureGreenCount == 16 * 16);

    winClose(ctx);
    glideShutdown();
    ::dlclose(handle);

    std::ifstream logIn(logPath);
    REQUIRE(logIn.is_open());
    std::string logContent((std::istreambuf_iterator<char>(logIn)),
                           std::istreambuf_iterator<char>());
    logIn.close();
    CHECK(logContent.find("HUD Overlay          : ENABLED") != std::string::npos);
    CHECK(logContent.find("hud <- GLIDE_NG_HUD") != std::string::npos);

    std::remove(logPath.c_str());
  }
#endif
  }

}

TEST_CASE("Infra: FrontBufferFlushTimer lazy spawn, coalescing, cancel, and reset",
          "[Infra][FrontBuffer]") {
  using Clock = GlideWrapper::FrontBufferFlushTimer::Clock;
  using Ms = std::chrono::milliseconds;

  SECTION("Unused timer spawns zero worker threads") {
    int fires = 0;
    GlideWrapper::FrontBufferFlushTimer timer([&]() { ++fires; });
    CHECK_FALSE(timer.HasWorker());
    timer.Clear();
    CHECK_FALSE(timer.HasWorker());
    timer.Stop();
    CHECK_FALSE(timer.HasWorker());
    CHECK(fires == 0);
  }

  SECTION("Coalesces rapid bursts into one callback and allows re-arming") {
    std::mutex mu;
    std::condition_variable cv;
    int fires = 0;

    GlideWrapper::FrontBufferFlushTimer timer([&]() {
      {
        std::lock_guard<std::mutex> lock(mu);
        ++fires;
      }
      cv.notify_all();
    });

    const auto d1 = Clock::now() + Ms(20);
    for (int i = 0; i < 64; ++i) {
      timer.Arm(d1 + Ms(i));
    }
    CHECK(timer.HasWorker());

    {
      std::unique_lock<std::mutex> lock(mu);
      REQUIRE(cv.wait_for(lock, Ms(1000), [&]() { return fires == 1; }));
    }
    // Brief settle window to confirm the other 63 coalesced Arm() calls do not
    // double-fire.
    std::this_thread::sleep_for(Ms(35));
    {
      std::lock_guard<std::mutex> lock(mu);
      CHECK(fires == 1);
    }

    // Re-arm after completion.
    timer.Arm(Clock::now() + Ms(15));
    {
      std::unique_lock<std::mutex> lock(mu);
      REQUIRE(cv.wait_for(lock, Ms(1000), [&]() { return fires == 2; }));
    }

    timer.Stop();
    CHECK_FALSE(timer.HasWorker());
  }

  SECTION("Clear cancels pending flush before deadline") {
    std::atomic<int> fires{0};
    GlideWrapper::FrontBufferFlushTimer timer(
        [&]() { fires.fetch_add(1, std::memory_order_relaxed); });

    timer.Arm(Clock::now() + Ms(30));
    CHECK(timer.HasWorker());
    timer.Clear();
    std::this_thread::sleep_for(Ms(70));
    CHECK(fires.load(std::memory_order_relaxed) == 0);

    timer.Stop();
    CHECK_FALSE(timer.HasWorker());
  }

  SECTION("Stop and ResetForAttach supports re-attaching a new window") {
    std::mutex mu;
    std::condition_variable cv;
    int fires = 0;

    GlideWrapper::FrontBufferFlushTimer timer([&]() {
      {
        std::lock_guard<std::mutex> lock(mu);
        ++fires;
      }
      cv.notify_all();
    });

    timer.Arm(Clock::now() + Ms(500));
    REQUIRE(timer.HasWorker());
    timer.Stop();
    CHECK_FALSE(timer.HasWorker());

    // Arm() while stopped is ignored until ResetForAttach().
    timer.Arm(Clock::now() + Ms(5));
    CHECK_FALSE(timer.HasWorker());
    CHECK(fires == 0);

    timer.ResetForAttach();
    timer.Arm(Clock::now() + Ms(15));
    CHECK(timer.HasWorker());
    {
      std::unique_lock<std::mutex> lock(mu);
      REQUIRE(cv.wait_for(lock, Ms(1000), [&]() { return fires == 1; }));
    }
    timer.Stop();
  }
}

