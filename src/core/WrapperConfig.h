#pragma once

#include <string>
#include <cstdint>

namespace GlideWrapper {

// Authoritative Wrapper Project Identity
constexpr const char* WRAPPER_PROJECT_NAME = "glide-ng";
constexpr const char* WRAPPER_VERSION      = "v0.1.0-dev";
constexpr const char* WRAPPER_FULL_NAME    = "3dfx glide-ng (Glide Next Generation) Modernization Wrapper";

enum class CardModel {
    VoodooGraphics,
    Voodoo2,
    Voodoo3,
    Voodoo5
};

struct WrapperConfig {
    CardModel model{CardModel::Voodoo2};
    uint32_t fbiMemoryMb{16};
    uint32_t tmuCount{2};
    uint32_t tmuMemoryMb{16};
    float maxFps{60.0f};
    float drawCallDelayUs{1.5f};
    bool enableSli{false}; // Disabled by default for maximum classic interop
    std::string apiVersionOverride{"2.43 (Latest)"};
    std::string reportedVendorOverride{"3Dfx Interactive"}; // Authentic retro default!
    std::string reportedRendererOverride{"3dfx glide-ng Vulkan Wrapper"};
    uint32_t logLevel{3}; // 3 = INFO
    bool forceNoWindow{false}; // Enforces headless offscreen CI mode upon hWnd=0
    bool logToConsole{false}; // Enforces console logging to stdout/stderr in addition to file
    std::string backend{"vulkan"}; // "vulkan" or "opengl_es"
};

class EmulationRegistry {
public:
    static EmulationRegistry& GetInstance();
    
    WrapperConfig& GetConfig() { return m_config; }
    const WrapperConfig& GetConfig() const { return m_config; }

private:
    EmulationRegistry();
    WrapperConfig m_config;
};

} // namespace GlideWrapper
