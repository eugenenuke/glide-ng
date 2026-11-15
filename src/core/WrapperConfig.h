#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace GlideWrapper {

// Authoritative Wrapper Project Identity
constexpr const char* WRAPPER_PROJECT_NAME = "glide-ng";
constexpr const char* WRAPPER_VERSION      = "v0.1.0-dev";
constexpr const char* WRAPPER_FULL_NAME    = "3dfx glide-ng (Glide Next Generation) Modernization Wrapper";

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
    std::string logFile{"glide-ng.log"};
    uint32_t logCategoryMask{0xFFFFFFFFu}; // Bitmask of LogCategory (GLIDE_NG_LOG_CATEGORIES)
    std::string logCategoriesSpec{"all"};  // Normalized/raw category spec for summary
    std::string traceFramesSpec{""};       // e.g. "120-125", "10", "50-", "-20" (GLIDE_NG_TRACE_FRAMES)
    uint64_t traceFrameStart{0};
    uint64_t traceFrameEnd{UINT64_MAX};
    bool traceFrameWindowActive{false};
    bool perfLogEnabled{false};            // Periodic [PERF] summary (GLIDE_NG_PERF_LOG)
    std::string perfLogCsvPath{""};        // Optional per-frame CSV path (GLIDE_NG_PERF_LOG=<path>)

    // Authentic 3dfx Glide overrides (Phase 1)
    bool noSplash{false};          // FX_GLIDE_NO_SPLASH / GLIDE_NG_NO_SPLASH
    int shamelessPlugMode{-1};     // -1 = app-controlled, 0 = force off, 1 = force on

    // Backend-specific tuning (unified; zero direct std::getenv() in backends)
    uint32_t swThreads{0};         // 0 = auto (hardware_concurrency)
    bool swUseSimd{false};         // AVX2 rasterizer hook (GLIDE_NG_SW_SIMD)
    uint32_t swTileSize{32};       // >= 16, rounded to power-of-two
    std::string swAaMode{"analytical"}; // "analytical" or "ssaa"
    std::string vkCachePath{".glide-vulkan-cache.bin"};
    uint32_t glesVboSizeMb{8};     // GLES geometry VBO size in MB

    // Config resolution metadata (for startup diagnostic summary)
    std::string configFilePath{"glide_config.json"};
    bool configFileLoaded{false};
    std::map<std::string, std::string> settingSources;
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
