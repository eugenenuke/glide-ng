#include "JsonConfigLoader.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace GlideWrapper {

bool JsonConfigLoader::Load(const std::string& configFilePath, WrapperConfig& outConfig) {
    GLIDE_LOG(INFO, "Config", "Attempting to load profile from: " << configFilePath);

    std::ifstream file(configFilePath);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        
        // Simple heuristic JSON parsing for our foundation layer
        if (content.find("\"VoodooGraphics\"") != std::string::npos) outConfig.model = CardModel::VoodooGraphics;
        if (content.find("\"Voodoo2\"") != std::string::npos)        outConfig.model = CardModel::Voodoo2;
        if (content.find("\"Voodoo3\"") != std::string::npos)        outConfig.model = CardModel::Voodoo3;
        if (content.find("\"Voodoo5\"") != std::string::npos)        outConfig.model = CardModel::Voodoo5;
        
        if (content.find("\"opengl_es\"") != std::string::npos ||
            content.find("\"gles\"") != std::string::npos) {
            outConfig.backend = "opengl_es";
        }
        if (content.find("\"vulkan\"") != std::string::npos)         outConfig.backend = "vulkan";
            if (content.find("\"logToConsole\"") != std::string::npos) {
            outConfig.logToConsole = (content.find("\"logToConsole\": true") != std::string::npos ||
                                      content.find("\"logToConsole\":true") != std::string::npos);
        }
        
        // Parse custom frame rate ceiling (e.g. "maxFps": 60 or 0 for uncapped)
        size_t maxFpsPos = content.find("\"maxFps\"");
        if (maxFpsPos != std::string::npos) {
            size_t colonPos = content.find(":", maxFpsPos);
            if (colonPos != std::string::npos) {
                size_t valPos = content.find_first_of("-0123456789.", colonPos);
                if (valPos != std::string::npos) {
                    outConfig.maxFps = std::atof(content.c_str() + valPos);
                }
            }
        }
        
        GLIDE_LOG(INFO, "Config", "Parsed local JSON configuration file successfully.");
    } else {
        GLIDE_LOG(INFO, "Config", "No local glide_config.json file detected. Using baseline defaults.");
    }

    // Process Environment Overrides (Highest Priority next to app arguments)
    if (const char* envBackend = std::getenv("GLIDE_BACKEND")) {
        outConfig.backend = envBackend;
        GLIDE_LOG(WARN, "Config", "Overrode active Rendering Backend via GLIDE_BACKEND=" << outConfig.backend);
    }

    // Normalize backend aliases
    if (outConfig.backend == "gles") {
        outConfig.backend = "opengl_es";
        GLIDE_LOG(INFO, "Config", "Normalized backend alias 'gles' to 'opengl_es'");
    }

    if (const char* envModel = std::getenv("GLIDE_WRAPPER_CARD_MODEL")) {
        std::string m(envModel);
        if (m == "SST1") outConfig.model = CardModel::VoodooGraphics;
        if (m == "CVG")  outConfig.model = CardModel::Voodoo2;
        if (m == "H3")   outConfig.model = CardModel::Voodoo3;
        if (m == "H5")   outConfig.model = CardModel::Voodoo5;
        GLIDE_LOG(WARN, "Config", "Overrode emulated Card Model via GLIDE_WRAPPER_CARD_MODEL=" << m);
    }
    
    if (const char* envFbi = std::getenv("GLIDE_WRAPPER_FBI_MEM_MB")) {
        outConfig.fbiMemoryMb = std::atoi(envFbi);
        GLIDE_LOG(WARN, "Config", "Overrode FBI Memory via GLIDE_WRAPPER_FBI_MEM_MB=" << outConfig.fbiMemoryMb);
    }
    
    if (const char* envLog = std::getenv("GLIDE_WRAPPER_LOG_LEVEL")) {
        outConfig.logLevel = std::atoi(envLog);
        GLIDE_LOG(WARN, "Config", "Overrode Log Level via GLIDE_WRAPPER_LOG_LEVEL=" << outConfig.logLevel);
    }
    
    if (const char* envMaxFps = std::getenv("GLIDE_WRAPPER_MAX_FPS")) {
        outConfig.maxFps = std::atof(envMaxFps);
        GLIDE_LOG(WARN, "Config", "Overrode Max FPS via GLIDE_WRAPPER_MAX_FPS=" << outConfig.maxFps);
    }
    
    if (const char* envVendor = std::getenv("GLIDE_WRAPPER_VENDOR")) {
        outConfig.reportedVendorOverride = envVendor;
        GLIDE_LOG(WARN, "Config", "Overrode Reported Vendor via GLIDE_WRAPPER_VENDOR=" << outConfig.reportedVendorOverride);
    }
    
    if (const char* envRenderer = std::getenv("GLIDE_WRAPPER_RENDERER")) {
        outConfig.reportedRendererOverride = envRenderer;
        GLIDE_LOG(WARN, "Config", "Overrode Reported Renderer via GLIDE_WRAPPER_RENDERER=" << outConfig.reportedRendererOverride);
    }

    if (const char* envConsole = std::getenv("GLIDE_WRAPPER_LOG_TO_CONSOLE")) {
        std::string val(envConsole);
        outConfig.logToConsole = (val == "1" || val == "true" || val == "TRUE");
        GLIDE_LOG(WARN, "Config", "Overrode Log To Console via GLIDE_WRAPPER_LOG_TO_CONSOLE=" << outConfig.logToConsole);
    }

    return true;
}

} // namespace GlideWrapper
