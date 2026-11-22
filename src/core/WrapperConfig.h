#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "build_version.h"

namespace GlideWrapper {

// Authoritative Wrapper Project Identity
constexpr const char* WRAPPER_PROJECT_NAME = "glide-ng";
constexpr const char* WRAPPER_VERSION      = GLIDE_VERSION_STRING;
constexpr const char* WRAPPER_VERSION_BASE = GLIDE_VERSION_BASE;
constexpr const char* WRAPPER_FULL_NAME    = "3dfx glide-ng (Glide Next Generation) Modernization Wrapper";

// Unified Default Runtime Filenames
constexpr const char* DEFAULT_CONFIG_FILE   = "glide-ng.ini";
constexpr const char* DEFAULT_LOG_FILE      = "glide-ng.log";
constexpr const char* DEFAULT_VK_CACHE_FILE = ".glide-ng-vkcache.bin";

enum class CardModel {
    VoodooGraphics, // SST-1
    VoodooRush,     // SST-96
    Voodoo2,        // CVG
    VoodooBanshee,  // Banshee (H3 single-TMU, 16 MB UMA)
    Voodoo3,        // Avenger (H3 dual-TMU)
    Voodoo4,        // VSA-100 single-chip (Napalm)
    Voodoo5         // VSA-100 dual-chip (Napalm)
};

enum class ApiVersion {
    // Authentic Glide 2.x versions (verified against 3dfx Win32 DLLs & DOS OVLs)
    GLIDE_2_1  = 211,  // Glide 2.11 (GLIDE.DLL, SST-1; accepts "2.1" / "2.11")
    GLIDE_2_11 = 211,
    GLIDE_2_30 = 230,  // Glide 2.30 (first GLIDE2X.OVL, SST-1)
    GLIDE_2_40 = 240,  // Glide 2.40 (SST-1 / Rush)
    GLIDE_2_42 = 242,  // Glide 2.42 (Voodoo Rush / SST-96)
    GLIDE_2_43 = 243,  // Glide 2.43 (SST-1 / Rush)
    GLIDE_2_45 = 245,  // Glide 2.45 (Voodoo Rush)
    GLIDE_2_46 = 246,  // Glide 2.46 (SST-1 / Rush)
    GLIDE_2_48 = 248,  // Glide 2.48 (SST-1 / Rush final)
    GLIDE_2_50 = 250,  // Glide 2.50 (Voodoo2 launch)
    GLIDE_2_51 = 251,  // Glide 2.51 (Voodoo2 dual-head)
    GLIDE_2_53 = 253,  // Glide 2.53 (Voodoo2)
    GLIDE_2_54 = 254,  // Glide 2.54 (Voodoo2)
    GLIDE_2_56 = 256,  // Glide 2.56 (Voodoo2 final)
    GLIDE_2_60 = 260,  // Glide 2.60 (Banshee / Voodoo3)
    GLIDE_2_61 = 261,  // Glide 2.61 (Banshee / Voodoo3 / Voodoo4 / Voodoo5 final)

    // Authentic Glide 3.x versions (verified against 3dfx Win32 DLLs)
    GLIDE_3_00 = 300,  // Glide 3.00 (SST-1 / Rush)
    GLIDE_3_01 = 301,  // Glide 3.01 (Voodoo2)
    GLIDE_3_02 = 302,  // Glide 3.02 (SST-1 / Rush)
    GLIDE_3_03 = 303,  // Glide 3.03 (Voodoo2 final)
    GLIDE_3_04 = 304,  // Glide 3.04 (SST-1 / Rush final)
    GLIDE_3_10 = 310   // Glide 3.10 (Banshee / Voodoo3 / Voodoo4 / Voodoo5 final)
};

inline bool IsGlide3ApiVersion(ApiVersion ver) {
    return static_cast<int>(ver) >= 300;
}

ApiVersion GetDefaultGlide2Version(CardModel model);
ApiVersion GetDefaultGlide3Version(CardModel model);
const char* FormatApiVersionString(ApiVersion ver);
bool IsVersionHistoricallyValidForCard(CardModel model, ApiVersion ver);
const char* DescribeAuthenticVersionsForCard(CardModel model, bool glide3);

enum HudElementFlags : uint32_t {
    HUD_NONE       = 0u,
    HUD_FPS        = 1u << 0,
    HUD_FRAMETIMES = 1u << 1,
    HUD_BACKEND    = 1u << 2,
    HUD_API        = 1u << 3,
    HUD_DRAWCALLS  = 1u << 4,
    HUD_LFB        = 1u << 5,
    HUD_FULL       = HUD_FPS | HUD_FRAMETIMES | HUD_BACKEND | HUD_API | HUD_DRAWCALLS | HUD_LFB
};

enum class HudPosition : uint32_t {
    TOP_LEFT = 0,
    TOP_RIGHT = 1,
    BOTTOM_LEFT = 2,
    BOTTOM_RIGHT = 3
};

// Runtime-selectable Software & shared-CPU SIMD tier (GLIDE_NG_SW_SIMD / [software] simd).
// Default is Avx2; users on AVX-512 CPUs (e.g. AMD Zen 4/5) may select Avx512 or Auto.
enum class SwSimdLevel : uint32_t {
    Auto = 0,
    Scalar = 1,
    Avx2 = 2,
    Avx512 = 3
};

const char* FormatSwSimdLevel(SwSimdLevel level);
bool ParseSwSimdLevel(const std::string& raw, SwSimdLevel& out);

struct WrapperConfig {
    // Emulated hardware profile
    CardModel model{CardModel::Voodoo2};
    ApiVersion apiVersion{ApiVersion::GLIDE_2_56};
    ApiVersion glide3ApiVersion{ApiVersion::GLIDE_3_03};
    uint32_t fbiMemoryMb{16};
    uint32_t tmuCount{2};
    uint32_t tmuMemoryMb{16};
    bool enableSli{false}; // Disabled by default for maximum classic interop
    std::string apiVersionOverride{"2.56"};
    std::string reportedVendorOverride{"3Dfx Interactive"}; // Authentic retro default
    std::string reportedRendererOverride{"3dfx glide-ng Vulkan Wrapper"};

    // Core & presentation
    std::string backend{"vulkan"}; // "vulkan", "opengl_es", or "software"
    bool forceNoWindow{false};     // Headless offscreen CI mode (GLIDE_NG_HEADLESS)
    float maxFps{60.0f};           // 0 = uncapped
    float drawCallDelayUs{0.0f};   // Stub / planned AGP bus delay
    uint32_t msaaSamples{1};       // 1, 2, 4, 8, 16
    bool vsync{true};              // Swapchain vertical sync
    bool vsyncStrictFifo{false};   // Opt-in strict FIFO on Wayland (GLIDE_NG_VSYNC=fifo)
    int swapInterval{-1};          // -1 = app-controlled, 0 = off, >=1 = interval (FX_GLIDE_SWAPINTERVAL)
    static constexpr bool DefaultFullscreenForPlatform() noexcept {
#if defined(_WIN32)
        // Authentic 3dfx Win32/Wine default: borderless desktop fullscreen
        // with centered aspect-ratio presentation (matches real pass-through
        // hardware, nGlide, and dgVoodoo2). Override via `[display]
        // fullscreen = false` or `GLIDE_NG_FULLSCREEN=0` for windowed mode.
        return true;
#else
        // On Linux/Unix, DOSBox owns window/fullscreen sizing for embedded
        // X11/Wayland child surfaces, and standalone demos default windowed.
        return false;
#endif
    }

    uint32_t windowScale{1};       // 1..8
    bool fullscreen{DefaultFullscreenForPlatform()}; // [display] fullscreen / GLIDE_NG_FULLSCREEN (auto|true|false)
    uint32_t presentationFilter{0}; // 0 = Nearest-Neighbor, 1 = Linear/Bilinear

    // Logging & diagnostics
    uint32_t logLevel{3};                  // 0=OFF, 1=CRIT, 2=WARN, 3=INFO, 4=DEBUG, 5=TRACE
    bool logToConsole{false};              // Mirror log output to stdout
    std::string logFile{DEFAULT_LOG_FILE};
    uint32_t logCategoryMask{0xFFFFFFFFu}; // Bitmask of LogCategory (GLIDE_NG_LOG_CATEGORIES)
    std::string logCategoriesSpec{"all"};  // Normalized/raw category spec for summary
    std::string traceFramesSpec{""};       // e.g. "120-125", "10", "50-", "-20" (GLIDE_NG_TRACE_FRAMES)
    uint64_t traceFrameStart{0};
    uint64_t traceFrameEnd{UINT64_MAX};
    bool traceFrameWindowActive{false};
    bool perfLogEnabled{false};            // Periodic [PERF] summary (GLIDE_NG_PERF_LOG)
    std::string perfLogCsvPath{""};        // Optional per-frame CSV path (GLIDE_NG_PERF_LOG=<path>)

    // On-screen HUD overlay (DXVK-style GLIDE_NG_HUD / INI [hud])
    uint32_t hudMask{HUD_NONE};            // Bitmask of HudElementFlags
    HudPosition hudPosition{HudPosition::TOP_LEFT};
    float hudScale{1.0f};                  // 0.25 .. 4.0 (relative to guest resolution)
    float hudOpacity{0.0f};                // Backdrop opacity 0.0 (borderless text+shadow) .. 1.0
    std::string hudSpec{""};               // Raw/normalized HUD spec for summary
    std::string activeFrontendApi{""};     // Populated at InitGlide ("Glide 2.x" / "Glide 3.x")

    // Authentic 3dfx Glide overrides
    bool noSplash{false};          // FX_GLIDE_NO_SPLASH / GLIDE_NG_NO_SPLASH
    int shamelessPlugMode{-1};     // -1 = app-controlled, 0 = force off, 1 = force on
    float gammaOverride{0.0f};     // <= 0 = app-controlled, > 0 = SST_GAMMA / SSTV2_GAMMA
    float gammaOverrideR{0.0f};    // <= 0 = inherit gammaOverride, > 0 = SST*_RGAMMA / FX_GLIDE_GAMMARGB
    float gammaOverrideG{0.0f};    // <= 0 = inherit gammaOverride, > 0 = SST*_GGAMMA / FX_GLIDE_GAMMARGB
    float gammaOverrideB{0.0f};    // <= 0 = inherit gammaOverride, > 0 = SST*_BGAMMA / FX_GLIDE_GAMMARGB
    bool useAppGamma{true};        // FX_GLIDE_USE_APP_GAMMA (false ignores app gamma writes)
    uint32_t screenRefreshHz{0};   // 0 = unset, > 0 = SST*_SCREENREFRESH / FX_GLIDE_REFRESH
    bool ignoreReopen{false};      // FX_GLIDE_IGNORE_REOPEN
    bool noDitherSub{false};       // FX_GLIDE_NO_DITHER_SUB
    bool forceBilinear{false};     // SSTV2_FORCE_BILINEAR
    bool forceTrilinear{false};    // SSTV2_FORCE_TRILINEAR
    bool texmapDisable{false};     // SST_TEXMAP_DISABLE / SSTV2_TEXMAP_DISABLE
    bool boundsCheck{false};       // FX_GLIDE_BOUNDS_CHECK
    int allocColorBuffers{-1};     // -1 = app-controlled, 2 or 3 (FX_GLIDE_ALLOC_COLOR / SSTV2_ALLOC_COLOR / SST_TRIPLE_BUFFER)
    int allocAuxBuffers{-1};       // -1 = app-controlled, 0 or 1 (FX_GLIDE_ALLOC_AUX / SSTV2_ALLOC_AUX)
    bool snapshotEnabled{false};   // FX_GLIDE_SNAPSHOT / FX_SNAPSHOT
    uint64_t snapshotFrame{0};     // 0-indexed frame to capture
    std::string snapshotPath{""};  // Explicit output PPM path (empty = auto glide_snapshot_NNNNNN.ppm)
    bool reportRealHw{false};      // FX_GLIDE_REPORT_REAL_HW (off by default; Voodoo2 CVG raw enum toggle)
    bool reportRealHwFromIni{false}; // True only when enabled via glide-ng.ini [glide] report_real_hw
    bool emulRush{false};          // FX_GLIDE_EMUL_RUSH (off by default; H3/VSA-100 reports GR_SSTTYPE_SST96)
    bool emulRushFromIni{false};   // True only when enabled via glide-ng.ini [glide] emul_rush
    int textureUmaMode{-1};        // -1 = app-controlled, 0 = off at init, 1 = on at init (FX_GLIDE_TEXTURE_UMA)

    bool HasGammaOverride() const noexcept {
        return gammaOverride > 0.0f || gammaOverrideR > 0.0f ||
               gammaOverrideG > 0.0f || gammaOverrideB > 0.0f;
    }
    bool HasPerChannelGammaOverride() const noexcept {
        return gammaOverrideR > 0.0f || gammaOverrideG > 0.0f ||
               gammaOverrideB > 0.0f;
    }

    // Backend-specific tuning (unified; zero direct std::getenv() in backends)
    uint32_t swThreads{0};         // 0 = auto (hardware_concurrency)
    SwSimdLevel swSimdLevel{SwSimdLevel::Avx2}; // Default AVX2; supports scalar|avx2|avx512|auto
    bool swUseSimd{true};          // Mirrors swSimdLevel != SwSimdLevel::Scalar
    uint32_t swTileSize{32};       // >= 16, rounded to power-of-two
    std::string swAaMode{"analytical"}; // "analytical" or "ssaa"
    std::string vkCachePath{DEFAULT_VK_CACHE_FILE};
    uint32_t glesVboSizeMb{8};     // GLES geometry VBO size in MB

    // Config resolution metadata (for startup diagnostic summary)
    std::string configFilePath{DEFAULT_CONFIG_FILE};
    bool configFileLoaded{false};
    std::map<std::string, std::string> settingSources;
    std::vector<std::string> unknownConfigKeys;
    std::vector<std::string> invalidConfigValues;
    std::vector<std::string> compatibilityWarnings;
    std::string simdFallbackWarning;
};

class EmulationRegistry {
public:
    static EmulationRegistry& GetInstance();
    
    WrapperConfig& GetConfig() { return m_config; }
    const WrapperConfig& GetConfig() const { return m_config; }

    // Check if a function is supported under the active card model and API version (Glide 2.x)
    bool IsFunctionAvailable(const std::string& funcName) const;

    // Check if a feature/extension proc is supported by the active card model (Glide 3.x)
    bool IsCardFeatureAvailable(const std::string& funcName) const;

private:
    EmulationRegistry();
    WrapperConfig m_config;
};

} // namespace GlideWrapper
