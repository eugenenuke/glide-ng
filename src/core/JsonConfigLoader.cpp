#include "JsonConfigLoader.h"
#include "Logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace GlideWrapper {

namespace {

bool IsJsonWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsJsonValueDelimiter(char c) {
    return IsJsonWhitespace(c) || c == ',' || c == '}' || c == ']';
}

// Parses a JSON string literal starting at `pos` (pointing at opening '"').
// Advances `pos` to one past the closing '"' and returns true on success.
bool ParseJsonStringLiteral(const std::string& content, size_t& pos, std::string& outStr) {
    if (pos >= content.size() || content[pos] != '"') return false;
    ++pos;
    outStr.clear();
    while (pos < content.size()) {
        char c = content[pos++];
        if (c == '\\') {
            if (pos >= content.size()) return false;
            char esc = content[pos++];
            switch (esc) {
                case '"':  outStr += '"'; break;
                case '\\': outStr += '\\'; break;
                case '/':  outStr += '/'; break;
                case 'b':  outStr += '\b'; break;
                case 'f':  outStr += '\f'; break;
                case 'n':  outStr += '\n'; break;
                case 'r':  outStr += '\r'; break;
                case 't':  outStr += '\t'; break;
                default:   outStr += esc; break;
            }
        } else if (c == '"') {
            return true;
        } else {
            outStr += c;
        }
    }
    return false;
}

// Finds the value start offset for a top-level JSON object key (depth == 1).
// Skips string values and nested objects/arrays so values or nested keys cannot
// spoof top-level configuration fields. If a key appears more than once, the
// last occurrence wins.
bool FindJsonKeyValue(const std::string& content, const std::string& targetKey, size_t& outValStart) {
    int depth = 0;
    bool found = false;
    size_t i = 0;
    const size_t n = content.size();

    while (i < n) {
        char c = content[i];
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"') {
            std::string str;
            if (!ParseJsonStringLiteral(content, i, str)) {
                return found;
            }
            // Only treat string literals at top-level object depth followed by ':' as keys.
            if (depth == 1) {
                size_t j = i;
                while (j < n && IsJsonWhitespace(content[j])) ++j;
                if (j < n && content[j] == ':') {
                    ++j;
                    while (j < n && IsJsonWhitespace(content[j])) ++j;
                    if (str == targetKey && j < n) {
                        outValStart = j;
                        found = true;
                    }
                    i = j;
                }
            }
            continue;
        }
        ++i;
    }
    return found;
}

bool ExtractJsonString(const std::string& content, const std::string& key, std::string& outVal) {
    size_t pos = 0;
    if (!FindJsonKeyValue(content, key, pos)) return false;
    return ParseJsonStringLiteral(content, pos, outVal);
}

bool ExtractJsonStringOrNumber(const std::string& content, const std::string& key, std::string& outVal) {
    size_t pos = 0;
    if (!FindJsonKeyValue(content, key, pos)) return false;
    if (content[pos] == '"') {
        return ParseJsonStringLiteral(content, pos, outVal);
    }
    size_t start = pos;
    while (pos < content.size() && !IsJsonValueDelimiter(content[pos])) {
        ++pos;
    }
    if (pos == start) return false;
    outVal = content.substr(start, pos - start);
    return true;
}

bool ExtractJsonBool(const std::string& content, const std::string& key, bool& outVal) {
    size_t pos = 0;
    if (!FindJsonKeyValue(content, key, pos)) return false;
    if (content.compare(pos, 4, "true") == 0 &&
        (pos + 4 == content.size() || IsJsonValueDelimiter(content[pos + 4]))) {
        outVal = true;
        return true;
    }
    if (content.compare(pos, 5, "false") == 0 &&
        (pos + 5 == content.size() || IsJsonValueDelimiter(content[pos + 5]))) {
        outVal = false;
        return true;
    }
    return false;
}

bool ExtractJsonDouble(const std::string& content, const std::string& key, double& outVal) {
    size_t pos = 0;
    if (!FindJsonKeyValue(content, key, pos)) return false;
    const char* startPtr = content.c_str() + pos;
    char* endPtr = nullptr;
    double parsed = std::strtod(startPtr, &endPtr);
    if (endPtr == startPtr) return false;
    if (*endPtr != '\0' && !IsJsonValueDelimiter(*endPtr)) return false;
    outVal = parsed;
    return true;
}

bool ExtractJsonInt(const std::string& content, const std::string& key, int& outVal) {
    size_t pos = 0;
    if (!FindJsonKeyValue(content, key, pos)) return false;
    const char* startPtr = content.c_str() + pos;
    char* endPtr = nullptr;
    long parsed = std::strtol(startPtr, &endPtr, 10);
    if (endPtr == startPtr) return false;
    if (*endPtr != '\0' && !IsJsonValueDelimiter(*endPtr)) return false;
    outVal = static_cast<int>(parsed);
    return true;
}

bool ParseCardModelString(const std::string& rawStr, CardModel& outModel) {
    std::string m = rawStr;
    std::transform(m.begin(), m.end(), m.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (m == "SST1" || m == "VOODOO1" || m == "VOODOOGRAPHICS") {
        outModel = CardModel::VoodooGraphics;
        return true;
    }
    if (m == "SST96" || m == "VOODOORUSH" || m == "VOODOO_RUSH" || m == "RUSH") {
        outModel = CardModel::VoodooRush;
        return true;
    }
    if (m == "CVG" || m == "VOODOO2") {
        outModel = CardModel::Voodoo2;
        return true;
    }
    if (m == "H3" || m == "VOODOO3") {
        outModel = CardModel::Voodoo3;
        return true;
    }
    if (m == "H5" || m == "VOODOO5") {
        outModel = CardModel::Voodoo5;
        return true;
    }
    return false;
}

}  // namespace

bool JsonConfigLoader::Load(const std::string& configFilePath, WrapperConfig& outConfig) {
    // Reset to baseline defaults to prevent state leakage across dynamic library reloads
    outConfig = WrapperConfig{};
    bool hasApiVerInJson = false;

    GLIDE_LOG(INFO, "Config", "Attempting to load profile from: " << configFilePath);

    std::ifstream file(configFilePath);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        // Key-scoped JSON parsing (immune to unrelated fields containing substrings)
        std::string modelStr;
        if (ExtractJsonString(content, "cardModel", modelStr) ||
            ExtractJsonString(content, "model", modelStr)) {
            ParseCardModelString(modelStr, outConfig.model);
        }

        // Parse API Version (e.g. "apiVersion": "2.43" or "apiVersion": 2.43)
        std::string apiVerStr;
        if (ExtractJsonStringOrNumber(content, "apiVersion", apiVerStr)) {
            if (apiVerStr == "2.1") { outConfig.apiVersion = ApiVersion::GLIDE_2_1; hasApiVerInJson = true; }
            else if (apiVerStr == "2.40" || apiVerStr == "2.4") { outConfig.apiVersion = ApiVersion::GLIDE_2_40; hasApiVerInJson = true; }
            else if (apiVerStr == "2.42") { outConfig.apiVersion = ApiVersion::GLIDE_2_42; hasApiVerInJson = true; }
            else if (apiVerStr == "2.43") { outConfig.apiVersion = ApiVersion::GLIDE_2_43; hasApiVerInJson = true; }
            else if (apiVerStr == "2.61") { outConfig.apiVersion = ApiVersion::GLIDE_2_61; hasApiVerInJson = true; }
        }

        std::string backendStr;
        if (ExtractJsonString(content, "backend", backendStr)) {
            std::string bLower = backendStr;
            std::transform(bLower.begin(), bLower.end(), bLower.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (bLower == "opengl_es" || bLower == "gles" || bLower == "goes") {
                outConfig.backend = "opengl_es";
            } else if (bLower == "vulkan" || bLower == "vk") {
                outConfig.backend = "vulkan";
            } else if (bLower == "software" || bLower == "sw") {
                outConfig.backend = "software";
            } else {
                outConfig.backend = backendStr;
            }
        }

        bool boolVal = false;
        if (ExtractJsonBool(content, "logToConsole", boolVal)) {
            outConfig.logToConsole = boolVal;
        }
        if (ExtractJsonBool(content, "vsync", boolVal)) {
            outConfig.vsync = boolVal;
        }
        if (ExtractJsonBool(content, "forceNoWindow", boolVal)) {
            outConfig.forceNoWindow = boolVal;
        }

        double maxFpsVal = 0.0;
        if (ExtractJsonDouble(content, "maxFps", maxFpsVal)) {
            outConfig.maxFps = static_cast<float>(maxFpsVal);
        }

        int intVal = 0;
        if (ExtractJsonInt(content, "logLevel", intVal) && intVal >= 0) {
            outConfig.logLevel = static_cast<uint32_t>(intVal);
        }
        if (ExtractJsonInt(content, "msaaSamples", intVal) && intVal >= 0) {
            outConfig.msaaSamples = static_cast<uint32_t>(intVal);
        }
        if (ExtractJsonInt(content, "windowScale", intVal) && intVal >= 1 && intVal <= 8) {
            outConfig.windowScale = static_cast<uint32_t>(intVal);
        }

        GLIDE_LOG(INFO, "Config", "Parsed local JSON configuration file successfully.");
    } else {
        GLIDE_LOG(INFO, "Config", "No local glide_config.json file detected. Using baseline defaults.");
    }

    // Process Environment Overrides (Highest Priority next to app arguments)
    if (const char* envForceNoWin = std::getenv("GLIDE_FORCE_NO_WINDOW")) {
        outConfig.forceNoWindow = (std::string(envForceNoWin) == "1");
        GLIDE_LOG(WARN, "Config", "Overrode forceNoWindow via GLIDE_FORCE_NO_WINDOW=" << outConfig.forceNoWindow);
    }
    if (const char* envHeadless = std::getenv("GLIDE_HEADLESS")) {
        outConfig.forceNoWindow = (std::string(envHeadless) == "1");
        GLIDE_LOG(WARN, "Config", "Overrode forceNoWindow via GLIDE_HEADLESS=" << outConfig.forceNoWindow);
    }

    const char* envWrapperBackend = std::getenv("GLIDE_WRAPPER_BACKEND");
    const char* envLegacyBackend  = std::getenv("GLIDE_BACKEND");

    if (envWrapperBackend && envLegacyBackend) {
        GLIDE_LOG(CRITICAL, "Config", "ENVIRONMENT CONFLICT: Both 'GLIDE_WRAPPER_BACKEND' ("
            << envWrapperBackend << ") and legacy alias 'GLIDE_BACKEND' ("
            << envLegacyBackend << ") are set simultaneously! This is highly ambiguous. "
            << "Please unset one of these variables to resolve the conflict.");
        std::cerr << "\n===============================================================================\n"
                  << "[CRIT] [Config] ENVIRONMENT CONFLICT: Both 'GLIDE_WRAPPER_BACKEND' and 'GLIDE_BACKEND'\n"
                  << "               are set simultaneously! This is ambiguous. Aborting execution.\n"
                  << "               Please unset one of these environment variables.\n"
                  << "===============================================================================\n\n";
        std::exit(EXIT_FAILURE);
    }

    const char* envBackend = envWrapperBackend ? envWrapperBackend : envLegacyBackend;
    if (envBackend) {
        outConfig.backend = envBackend;
        GLIDE_LOG(WARN, "Config", "Overrode active Rendering Backend via environment=" << outConfig.backend);
    }

    // Normalize backend aliases
    if (outConfig.backend == "gles" || outConfig.backend == "goes") {
        std::string original = outConfig.backend;
        outConfig.backend = "opengl_es";
        GLIDE_LOG(INFO, "Config", "Normalized backend alias '" << original << "' to 'opengl_es'");
    } else if (outConfig.backend == "vk") {
        outConfig.backend = "vulkan";
        GLIDE_LOG(INFO, "Config", "Normalized backend alias 'vk' to 'vulkan'");
    } else if (outConfig.backend == "sw") {
        outConfig.backend = "software";
        GLIDE_LOG(INFO, "Config", "Normalized backend alias 'sw' to 'software'");
    }

    // Parse Card Model Override (GLIDE_WRAPPER_CARD_MODEL or GLIDE_DEVICE)
    const char* envCardOverride = std::getenv("GLIDE_WRAPPER_CARD_MODEL") ? std::getenv("GLIDE_WRAPPER_CARD_MODEL") : std::getenv("GLIDE_DEVICE");
    if (envCardOverride) {
        std::string rawStr(envCardOverride);
        std::string m = rawStr;
        std::transform(m.begin(), m.end(), m.begin(), ::toupper);
        
        if (m == "SST1" || m == "VOODOO1" || m == "VOODOOGRAPHICS") outConfig.model = CardModel::VoodooGraphics;
        else if (m == "SST96" || m == "VOODOORUSH" || m == "VOODOO_RUSH" || m == "RUSH") outConfig.model = CardModel::VoodooRush;
        else if (m == "CVG" || m == "VOODOO2")  outConfig.model = CardModel::Voodoo2;
        else if (m == "H3" || m == "VOODOO3")   outConfig.model = CardModel::Voodoo3;
        else if (m == "H5" || m == "VOODOO5")   outConfig.model = CardModel::Voodoo5;
        
        GLIDE_LOG(WARN, "Config", "Overrode emulated Card Model via environment=" << rawStr << " (matched: " << m << ")");
    }

    // Parse API Version Override (GLIDE_WRAPPER_API_VERSION or GLIDE_VERSION_OVERRIDE)
    const char* envApiOverride = std::getenv("GLIDE_WRAPPER_API_VERSION") ? std::getenv("GLIDE_WRAPPER_API_VERSION") : std::getenv("GLIDE_VERSION_OVERRIDE");
    bool overrideApi = false;
    if (envApiOverride) {
        std::string v(envApiOverride);
        if (v == "2.1") outConfig.apiVersion = ApiVersion::GLIDE_2_1;
        else if (v == "2.40" || v == "2.4") outConfig.apiVersion = ApiVersion::GLIDE_2_40;
        else if (v == "2.42") outConfig.apiVersion = ApiVersion::GLIDE_2_42;
        else if (v == "2.43") outConfig.apiVersion = ApiVersion::GLIDE_2_43;
        else if (v == "2.61") outConfig.apiVersion = ApiVersion::GLIDE_2_61;
        GLIDE_LOG(WARN, "Config", "Overrode emulated API Version via environment=" << v);
        overrideApi = true;
    }

    // Auto-default API version, VRAM sizes, and TMU counts based on selected Card Model if not overridden
    switch (outConfig.model) {
        case CardModel::VoodooGraphics:
            if (!overrideApi && !hasApiVerInJson) outConfig.apiVersion = ApiVersion::GLIDE_2_40;
            outConfig.tmuCount = 1;
            outConfig.fbiMemoryMb = 4;
            outConfig.tmuMemoryMb = 2;
            break;
        case CardModel::VoodooRush:
            if (!overrideApi && !hasApiVerInJson) outConfig.apiVersion = ApiVersion::GLIDE_2_40;
            outConfig.tmuCount = 1;
            outConfig.fbiMemoryMb = 6;
            outConfig.tmuMemoryMb = 2;
            break;
        case CardModel::Voodoo2:
            if (!overrideApi && !hasApiVerInJson) outConfig.apiVersion = ApiVersion::GLIDE_2_43;
            outConfig.tmuCount = 2;
            outConfig.fbiMemoryMb = 16;
            outConfig.tmuMemoryMb = 16;
            break;
        case CardModel::Voodoo3:
            if (!overrideApi && !hasApiVerInJson) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            outConfig.tmuCount = 2;      // Authentic Voodoo3 2-TMU driver compatibility illusion!
            outConfig.fbiMemoryMb = 16;
            outConfig.tmuMemoryMb = 16;
            break;
        case CardModel::Voodoo5:
            if (!overrideApi && !hasApiVerInJson) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            outConfig.tmuCount = 2;      // Dual VSA-100 SLI!
            outConfig.fbiMemoryMb = 64;
            outConfig.tmuMemoryMb = 32;
            break;
    }
    GLIDE_LOG(INFO, "Config", "Auto-configured hardware specs (TMUs, VRAM, API) based on active Card Model.");

    
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

    if (const char* envMsaa = std::getenv("GLIDE_WRAPPER_MSAA")) {
        outConfig.msaaSamples = std::atoi(envMsaa);
        GLIDE_LOG(WARN, "Config", "Overrode MSAA Sample Count via GLIDE_WRAPPER_MSAA=" << outConfig.msaaSamples);
    }

    if (const char* envVsync = std::getenv("GLIDE_VSYNC")) {
        std::string val(envVsync);
        outConfig.vsync = !(val == "0" || val == "false" || val == "FALSE" || val == "off" || val == "OFF");
        GLIDE_LOG(WARN, "Config", "Overrode VSync via GLIDE_VSYNC=" << (outConfig.vsync ? "ENABLED" : "DISABLED"));
    }

    if (const char* envScale = std::getenv("GLIDE_WRAPPER_WINDOW_SCALE")) {
        int val = std::atoi(envScale);
        if (val >= 1 && val <= 8) {
            outConfig.windowScale = static_cast<uint32_t>(val);
            GLIDE_LOG(WARN, "Config", "Overrode Window Scale via GLIDE_WRAPPER_WINDOW_SCALE=" << outConfig.windowScale);
        }
    }

    // Normalize MSAA sample count to valid GPU powers of two
    if (outConfig.msaaSamples != 1 && outConfig.msaaSamples != 2 && 
        outConfig.msaaSamples != 4 && outConfig.msaaSamples != 8 && 
        outConfig.msaaSamples != 16) {
        uint32_t original = outConfig.msaaSamples;
        outConfig.msaaSamples = 1;
        GLIDE_LOG(WARN, "Config", "Invalid MSAA sample count '" << original << "' requested. Defaulting to 1 (MSAA disabled).");
    }



    return true;
}

} // namespace GlideWrapper
