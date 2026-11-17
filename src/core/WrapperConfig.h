#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace GlideWrapper {

// Authoritative Wrapper Project Identity
constexpr const char* WRAPPER_PROJECT_NAME = "glide-ng";
constexpr const char* WRAPPER_VERSION      = "v0.1.0-dev";
constexpr const char* WRAPPER_FULL_NAME    = "3dfx glide-ng (Glide Next Generation) Modernization Wrapper";

// Unified Default Runtime Filenames
constexpr const char* DEFAULT_CONFIG_FILE   = "glide-ng.ini";
constexpr const char* DEFAULT_LOG_FILE      = "glide-ng.log";
constexpr const char* DEFAULT_VK_CACHE_FILE = ".glide-ng-vkcache.bin";

enum class CardModel {
    VoodooGraphics, // SST-1
    VoodooRush,     // SST-96
    Voodoo2,        // CVG
    Voodoo3,        // Avenger
    Voodoo5         // Napalm
};

enum class ApiVersion {
    GLIDE_2_1 = 210,
    GLIDE_2_40 = 240,
    GLIDE_2_42 = 242,
    GLIDE_2_43 = 243,
    GLIDE_2_61 = 261
};

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

struct WrapperConfig {
    // Emulated hardware profile
    CardModel model{CardModel::Voodoo2};
    ApiVersion apiVersion{ApiVersion::GLIDE_2_43};
    uint32_t fbiMemoryMb{16};
    uint32_t tmuCount{2};
    uint32_t tmuMemoryMb{16};
    bool enableSli{false}; // Disabled by default for maximum classic interop
    std::string apiVersionOverride{"2.43 (Latest)"};
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
    uint32_t windowScale{1};       // 1..8
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
    float hudScale{1.0f};                  // 0.5 .. 4.0
    float hudOpacity{0.78f};               // Backdrop opacity 0.0 .. 1.0
    std::string hudSpec{""};               // Raw/normalized HUD spec for summary
    std::string activeFrontendApi{""};     // Populated at InitGlide ("Glide 2.x" / "Glide 3.x")

    // Authentic 3dfx Glide overrides (Phase 1 & Phase 2)
    bool noSplash{false};          // FX_GLIDE_NO_SPLASH / GLIDE_NG_NO_SPLASH
    int shamelessPlugMode{-1};     // -1 = app-controlled, 0 = force off, 1 = force on
    float gammaOverride{0.0f};     // <= 0 = app-controlled, > 0 = SST_GAMMA / SSTV2_GAMMA
    uint32_t screenRefreshHz{0};   // 0 = unset, > 0 = SST_SCREENREFRESH / SSTV2_SCREENREFRESH
    bool ignoreReopen{false};      // FX_GLIDE_IGNORE_REOPEN
    bool noDitherSub{false};       // FX_GLIDE_NO_DITHER_SUB
    bool forceBilinear{false};     // SSTV2_FORCE_BILINEAR
    bool forceTrilinear{false};    // SSTV2_FORCE_TRILINEAR
    bool texmapDisable{false};     // SST_TEXMAP_DISABLE
    bool boundsCheck{false};       // FX_GLIDE_BOUNDS_CHECK
    int allocColorBuffers{-1};     // -1 = app-controlled, 2 or 3 (FX_GLIDE_ALLOC_COLOR)
    int allocAuxBuffers{-1};       // -1 = app-controlled, 0 or 1 (FX_GLIDE_ALLOC_AUX)
    bool snapshotEnabled{false};   // FX_GLIDE_SNAPSHOT / FX_SNAPSHOT
    uint64_t snapshotFrame{0};     // 0-indexed frame to capture
    std::string snapshotPath{""};  // Explicit output PPM path (empty = auto glide_snapshot_NNNNNN.ppm)

    // Backend-specific tuning (unified; zero direct std::getenv() in backends)
    uint32_t swThreads{0};         // 0 = auto (hardware_concurrency)
    bool swUseSimd{true};          // AVX2 rasterizer hook (GLIDE_NG_SW_SIMD)
    uint32_t swTileSize{32};       // >= 16, rounded to power-of-two
    std::string swAaMode{"analytical"}; // "analytical" or "ssaa"
    std::string vkCachePath{DEFAULT_VK_CACHE_FILE};
    uint32_t glesVboSizeMb{8};     // GLES geometry VBO size in MB

    // Config resolution metadata (for startup diagnostic summary)
    std::string configFilePath{DEFAULT_CONFIG_FILE};
    bool configFileLoaded{false};
    std::map<std::string, std::string> settingSources;
    std::vector<std::string> unknownConfigKeys;
};

class EmulationRegistry {
public:
    static EmulationRegistry& GetInstance();
    
    WrapperConfig& GetConfig() { return m_config; }
    const WrapperConfig& GetConfig() const { return m_config; }

    // Check if a function is supported under the active card model and API version
    bool IsFunctionAvailable(const std::string& funcName) const;

private:
    EmulationRegistry();
    WrapperConfig m_config;
};

} // namespace GlideWrapper
