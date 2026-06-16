#include "WrapperConfig.h"
#include <cstdlib>
#include <string>

namespace GlideWrapper {

EmulationRegistry& EmulationRegistry::GetInstance() {
    static EmulationRegistry instance;
    return instance;
}

EmulationRegistry::EmulationRegistry() {
    if (const char* env = std::getenv("GLIDE_FORCE_NO_WINDOW")) {
        m_config.forceNoWindow = (std::string(env) == "1");
    }
    if (const char* env = std::getenv("GLIDE_HEADLESS")) {
        m_config.forceNoWindow = (std::string(env) == "1");
    }
}

} // namespace GlideWrapper
