#include "JsonConfigLoader.h"
#include "Logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace GlideWrapper {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return s;
}

bool IsJsonWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsJsonValueDelimiter(char c) {
    return IsJsonWhitespace(c) || c == ',' || c == '}' || c == ']';
}

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

bool ParseBoolString(const std::string& raw, bool& outVal) {
    std::string v = ToLower(raw);
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        outVal = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        outVal = false;
        return true;
    }
    return false;
}

bool ParseIntString(const std::string& raw, int& outVal) {
    if (raw.empty()) return false;
    char* endPtr = nullptr;
    long parsed = std::strtol(raw.c_str(), &endPtr, 10);
    if (endPtr == raw.c_str() || *endPtr != '\0') return false;
    outVal = static_cast<int>(parsed);
    return true;
}

bool ParseDoubleString(const std::string& raw, double& outVal) {
    if (raw.empty()) return false;
    char* endPtr = nullptr;
    double parsed = std::strtod(raw.c_str(), &endPtr);
    if (endPtr == raw.c_str() || *endPtr != '\0') return false;
    outVal = parsed;
    return true;
}

bool ParseCardModelString(const std::string& rawStr, CardModel& outModel) {
    std::string m = ToUpper(rawStr);
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

bool ParseApiVersionString(const std::string& rawStr, ApiVersion& outVer) {
    if (rawStr == "2.1") { outVer = ApiVersion::GLIDE_2_1; return true; }
    if (rawStr == "2.40" || rawStr == "2.4") { outVer = ApiVersion::GLIDE_2_40; return true; }
    if (rawStr == "2.42") { outVer = ApiVersion::GLIDE_2_42; return true; }
    if (rawStr == "2.43") { outVer = ApiVersion::GLIDE_2_43; return true; }
    if (rawStr == "2.61") { outVer = ApiVersion::GLIDE_2_61; return true; }
    return false;
}

std::string NormalizeBackendString(const std::string& rawStr) {
    std::string b = ToLower(rawStr);
    if (b == "opengl_es" || b == "gles" || b == "goes") return "opengl_es";
    if (b == "vulkan" || b == "vk") return "vulkan";
    if (b == "software" || b == "sw") return "software";
    return rawStr;
}

bool ParseLogLevelString(const std::string& rawStr, uint32_t& outLevel) {
    int numeric = 0;
    if (ParseIntString(rawStr, numeric)) {
        if (numeric < 0) numeric = 0;
        if (numeric > 5) numeric = 5;
        outLevel = static_cast<uint32_t>(numeric);
        return true;
    }
    std::string v = ToLower(rawStr);
    if (v == "off" || v == "none") { outLevel = 0; return true; }
    if (v == "crit" || v == "critical" || v == "error" || v == "err") { outLevel = 1; return true; }
    if (v == "warn" || v == "warning") { outLevel = 2; return true; }
    if (v == "info") { outLevel = 3; return true; }
    if (v == "debug" || v == "dbug") { outLevel = 4; return true; }
    if (v == "trace") { outLevel = 5; return true; }
    return false;
}

bool ParsePresentationFilterString(const std::string& rawStr, uint32_t& outFilter) {
    std::string v = ToLower(rawStr);
    if (v == "0" || v == "nearest" || v == "sharp" || v == "point") {
        outFilter = 0;
        return true;
    }
    if (v == "1" || v == "linear" || v == "bilinear" || v == "smooth") {
        outFilter = 1;
        return true;
    }
    return false;
}

bool ParseVsyncString(const std::string& rawStr, bool& outVsync, bool& outStrictFifo) {
    std::string v = ToLower(rawStr);
    if (v == "fifo" || v == "strict") {
        outVsync = true;
        outStrictFifo = true;
        return true;
    }
    bool b = false;
    if (ParseBoolString(v, b)) {
        outVsync = b;
        outStrictFifo = false;
        return true;
    }
    return false;
}

const char* GetNonEmptyEnv(const char* name) {
    const char* val = std::getenv(name);
    return (val && val[0] != '\0') ? val : nullptr;
}

}  // namespace

bool JsonConfigLoader::Load(const std::string& configFilePath, WrapperConfig& outConfig) {
    // 1. Reset to baseline defaults to prevent state leakage across reloads
    outConfig = WrapperConfig{};

    bool hasApiVersion = false;
    bool hasFbiMem = false;
    bool hasTmuCount = false;
    bool hasTmuMem = false;

    // Resolve config file path (GLIDE_NG_CONFIG overrides default path)
    std::string effectivePath = configFilePath;
    if (const char* envConfigPath = GetNonEmptyEnv("GLIDE_NG_CONFIG")) {
        effectivePath = envConfigPath;
        outConfig.settingSources["configFile"] = "GLIDE_NG_CONFIG";
    }
    outConfig.configFilePath = effectivePath;

    // 2. Layer 2: JSON configuration file (overrides defaults)
    std::ifstream file(effectivePath);
    if (file.is_open()) {
        outConfig.configFileLoaded = true;
        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        std::string strVal;
        bool boolVal = false;
        int intVal = 0;
        double dblVal = 0.0;

        if (ExtractJsonString(content, "cardModel", strVal) ||
            ExtractJsonString(content, "model", strVal)) {
            if (ParseCardModelString(strVal, outConfig.model)) {
                outConfig.settingSources["cardModel"] = "json";
            }
        }

        if (ExtractJsonStringOrNumber(content, "apiVersion", strVal)) {
            if (ParseApiVersionString(strVal, outConfig.apiVersion)) {
                hasApiVersion = true;
                outConfig.settingSources["apiVersion"] = "json";
            }
        }

        if (ExtractJsonString(content, "backend", strVal)) {
            outConfig.backend = NormalizeBackendString(strVal);
            outConfig.settingSources["backend"] = "json";
        }

        if (ExtractJsonInt(content, "fbiMemoryMb", intVal) && intVal > 0) {
            outConfig.fbiMemoryMb = static_cast<uint32_t>(intVal);
            hasFbiMem = true;
            outConfig.settingSources["fbiMemoryMb"] = "json";
        }
        if (ExtractJsonInt(content, "tmuCount", intVal) && intVal >= 1 && intVal <= 3) {
            outConfig.tmuCount = static_cast<uint32_t>(intVal);
            hasTmuCount = true;
            outConfig.settingSources["tmuCount"] = "json";
        }
        if (ExtractJsonInt(content, "tmuMemoryMb", intVal) && intVal > 0) {
            outConfig.tmuMemoryMb = static_cast<uint32_t>(intVal);
            hasTmuMem = true;
            outConfig.settingSources["tmuMemoryMb"] = "json";
        }
        if (ExtractJsonBool(content, "enableSli", boolVal)) {
            outConfig.enableSli = boolVal;
            outConfig.settingSources["enableSli"] = "json";
        }
        if (ExtractJsonString(content, "vendor", strVal)) {
            outConfig.reportedVendorOverride = strVal;
            outConfig.settingSources["vendor"] = "json";
        }
        if (ExtractJsonString(content, "renderer", strVal)) {
            outConfig.reportedRendererOverride = strVal;
            outConfig.settingSources["renderer"] = "json";
        }

        if (ExtractJsonBool(content, "headless", boolVal) ||
            ExtractJsonBool(content, "forceNoWindow", boolVal)) {
            outConfig.forceNoWindow = boolVal;
            outConfig.settingSources["headless"] = "json";
        }

        if (ExtractJsonBool(content, "vsync", boolVal)) {
            outConfig.vsync = boolVal;
            outConfig.vsyncStrictFifo = false;
            outConfig.settingSources["vsync"] = "json";
        } else if (ExtractJsonString(content, "vsync", strVal)) {
            if (ParseVsyncString(strVal, outConfig.vsync, outConfig.vsyncStrictFifo)) {
                outConfig.settingSources["vsync"] = "json";
            }
        }
        if (ExtractJsonBool(content, "vsyncStrictFifo", boolVal)) {
            outConfig.vsyncStrictFifo = boolVal;
            if (boolVal) outConfig.vsync = true;
            outConfig.settingSources["vsyncStrictFifo"] = "json";
        }

        if (ExtractJsonDouble(content, "maxFps", dblVal) && dblVal >= 0.0) {
            outConfig.maxFps = static_cast<float>(dblVal);
            outConfig.settingSources["maxFps"] = "json";
        }
        if (ExtractJsonInt(content, "msaaSamples", intVal) && intVal >= 0) {
            outConfig.msaaSamples = static_cast<uint32_t>(intVal);
            outConfig.settingSources["msaaSamples"] = "json";
        }
        if (ExtractJsonInt(content, "windowScale", intVal) && intVal >= 1 && intVal <= 8) {
            outConfig.windowScale = static_cast<uint32_t>(intVal);
            outConfig.settingSources["windowScale"] = "json";
        }
        if (ExtractJsonStringOrNumber(content, "presentationFilter", strVal)) {
            if (ParsePresentationFilterString(strVal, outConfig.presentationFilter)) {
                outConfig.settingSources["presentationFilter"] = "json";
            }
        }

        if (ExtractJsonStringOrNumber(content, "logLevel", strVal)) {
            if (ParseLogLevelString(strVal, outConfig.logLevel)) {
                outConfig.settingSources["logLevel"] = "json";
            }
        }
        if (ExtractJsonBool(content, "logToConsole", boolVal)) {
            outConfig.logToConsole = boolVal;
            outConfig.settingSources["logToConsole"] = "json";
        }
        if (ExtractJsonString(content, "logFile", strVal) && !strVal.empty()) {
            outConfig.logFile = strVal;
            outConfig.settingSources["logFile"] = "json";
        }

        // Authentic Glide overrides in JSON
        if (ExtractJsonBool(content, "noSplash", boolVal)) {
            outConfig.noSplash = boolVal;
            outConfig.settingSources["noSplash"] = "json";
        }
        if (ExtractJsonBool(content, "shamelessPlug", boolVal)) {
            outConfig.shamelessPlugMode = boolVal ? 1 : 0;
            outConfig.settingSources["shamelessPlug"] = "json";
        } else if (ExtractJsonInt(content, "shamelessPlug", intVal)) {
            outConfig.shamelessPlugMode = (intVal < 0) ? -1 : (intVal != 0 ? 1 : 0);
            outConfig.settingSources["shamelessPlug"] = "json";
        }

        // Backend-specific tuning in JSON
        if (ExtractJsonInt(content, "swThreads", intVal) && intVal >= 0) {
            outConfig.swThreads = static_cast<uint32_t>(intVal);
            outConfig.settingSources["swThreads"] = "json";
        }
        if (ExtractJsonBool(content, "swSimd", boolVal)) {
            outConfig.swUseSimd = boolVal;
            outConfig.settingSources["swSimd"] = "json";
        }
        if (ExtractJsonInt(content, "swTileSize", intVal) && intVal > 0) {
            outConfig.swTileSize = static_cast<uint32_t>(intVal);
            outConfig.settingSources["swTileSize"] = "json";
        }
        if (ExtractJsonString(content, "swAa", strVal)) {
            outConfig.swAaMode = ToLower(strVal);
            outConfig.settingSources["swAa"] = "json";
        }
        if (ExtractJsonString(content, "vkCachePath", strVal) && !strVal.empty()) {
            outConfig.vkCachePath = strVal;
            outConfig.settingSources["vkCachePath"] = "json";
        }
        if (ExtractJsonInt(content, "glesVboSize", intVal) && intVal > 0) {
            outConfig.glesVboSizeMb = static_cast<uint32_t>(intVal);
            outConfig.settingSources["glesVboSize"] = "json";
        }
    }

    // 3. Card model & API version env overrides (evaluated before deriving card-model defaults)
    if (const char* envCard = GetNonEmptyEnv("GLIDE_NG_CARD_MODEL")) {
        if (ParseCardModelString(envCard, outConfig.model)) {
            outConfig.settingSources["cardModel"] = "GLIDE_NG_CARD_MODEL";
        }
    }
    if (const char* envApi = GetNonEmptyEnv("GLIDE_NG_API_VERSION")) {
        if (ParseApiVersionString(envApi, outConfig.apiVersion)) {
            hasApiVersion = true;
            outConfig.settingSources["apiVersion"] = "GLIDE_NG_API_VERSION";
        }
    }

    // Derive hardware defaults from selected CardModel ONLY for fields not explicitly overridden
    switch (outConfig.model) {
        case CardModel::VoodooGraphics:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_40;
            if (!hasTmuCount) outConfig.tmuCount = 1;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 4;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 2;
            break;
        case CardModel::VoodooRush:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_40;
            if (!hasTmuCount) outConfig.tmuCount = 1;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 6;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 2;
            break;
        case CardModel::Voodoo2:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_43;
            if (!hasTmuCount) outConfig.tmuCount = 2;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 16;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 16;
            break;
        case CardModel::Voodoo3:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            if (!hasTmuCount) outConfig.tmuCount = 2;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 16;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 16;
            break;
        case CardModel::Voodoo5:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            if (!hasTmuCount) outConfig.tmuCount = 2;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 64;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 32;
            break;
    }

    // 4. Layer 3: Authentic 3dfx Glide environment variables (Phase 1)
    // Precedence: Defaults < JSON < Authentic FX_GLIDE_* < GLIDE_NG_*
    if (const char* envNoSplash = GetNonEmptyEnv("FX_GLIDE_NO_SPLASH")) {
        bool b = true;
        if (!ParseBoolString(envNoSplash, b)) {
            // Authentic 3dfx convention: any non-zero/non-empty value enables suppression
            b = true;
        }
        outConfig.noSplash = b;
        outConfig.settingSources["noSplash"] = "FX_GLIDE_NO_SPLASH";
    }

    if (const char* envPlug = GetNonEmptyEnv("FX_GLIDE_SHAMELESS_PLUG")) {
        bool b = true;
        if (!ParseBoolString(envPlug, b)) {
            int n = 1;
            b = ParseIntString(envPlug, n) ? (n != 0) : true;
        }
        outConfig.shamelessPlugMode = b ? 1 : 0;
        outConfig.settingSources["shamelessPlug"] = "FX_GLIDE_SHAMELESS_PLUG";
    }

    if (const char* envSwapInterval = GetNonEmptyEnv("FX_GLIDE_SWAPINTERVAL")) {
        int interval = 0;
        if (ParseIntString(envSwapInterval, interval) && interval >= 0) {
            outConfig.swapInterval = interval;
            outConfig.vsync = (interval > 0);
            outConfig.vsyncStrictFifo = false;
            outConfig.settingSources["vsync"] = "FX_GLIDE_SWAPINTERVAL";
            outConfig.settingSources["swapInterval"] = "FX_GLIDE_SWAPINTERVAL";
        }
    }

    // 5. Layer 4: glide-ng Wrapper Environment Overrides (GLIDE_NG_* — highest priority)
    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_BACKEND")) {
        outConfig.backend = NormalizeBackendString(envVal);
        outConfig.settingSources["backend"] = "GLIDE_NG_BACKEND";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_HEADLESS")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.forceNoWindow = b;
            outConfig.settingSources["headless"] = "GLIDE_NG_HEADLESS";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_FBI_MEM_MB")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.fbiMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["fbiMemoryMb"] = "GLIDE_NG_FBI_MEM_MB";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_TMU_COUNT")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 1 && v <= 3) {
            outConfig.tmuCount = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuCount"] = "GLIDE_NG_TMU_COUNT";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_TMU_MEM_MB")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.tmuMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuMemoryMb"] = "GLIDE_NG_TMU_MEM_MB";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SLI")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.enableSli = b;
            outConfig.settingSources["enableSli"] = "GLIDE_NG_SLI";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_VENDOR")) {
        outConfig.reportedVendorOverride = envVal;
        outConfig.settingSources["vendor"] = "GLIDE_NG_VENDOR";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_RENDERER")) {
        outConfig.reportedRendererOverride = envVal;
        outConfig.settingSources["renderer"] = "GLIDE_NG_RENDERER";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_VSYNC")) {
        if (ParseVsyncString(envVal, outConfig.vsync, outConfig.vsyncStrictFifo)) {
            outConfig.swapInterval = outConfig.vsync ? 1 : 0;
            outConfig.settingSources["vsync"] = "GLIDE_NG_VSYNC";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_MAX_FPS")) {
        double v = 0.0;
        if (ParseDoubleString(envVal, v) && v >= 0.0) {
            outConfig.maxFps = static_cast<float>(v);
            outConfig.settingSources["maxFps"] = "GLIDE_NG_MAX_FPS";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_MSAA")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 0) {
            outConfig.msaaSamples = static_cast<uint32_t>(v);
            outConfig.settingSources["msaaSamples"] = "GLIDE_NG_MSAA";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_WINDOW_SCALE")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 1 && v <= 8) {
            outConfig.windowScale = static_cast<uint32_t>(v);
            outConfig.settingSources["windowScale"] = "GLIDE_NG_WINDOW_SCALE";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_PRESENTATION_FILTER")) {
        if (ParsePresentationFilterString(envVal, outConfig.presentationFilter)) {
            outConfig.settingSources["presentationFilter"] = "GLIDE_NG_PRESENTATION_FILTER";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_LOG_LEVEL")) {
        if (ParseLogLevelString(envVal, outConfig.logLevel)) {
            outConfig.settingSources["logLevel"] = "GLIDE_NG_LOG_LEVEL";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_LOG_CONSOLE")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.logToConsole = b;
            outConfig.settingSources["logToConsole"] = "GLIDE_NG_LOG_CONSOLE";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_LOG_FILE")) {
        outConfig.logFile = envVal;
        outConfig.settingSources["logFile"] = "GLIDE_NG_LOG_FILE";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_NO_SPLASH")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.noSplash = b;
            outConfig.settingSources["noSplash"] = "GLIDE_NG_NO_SPLASH";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SHAMELESS_PLUG")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.shamelessPlugMode = b ? 1 : 0;
            outConfig.settingSources["shamelessPlug"] = "GLIDE_NG_SHAMELESS_PLUG";
        }
    }

    // Backend-specific tuning overrides
    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SW_THREADS")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 0) {
            outConfig.swThreads = static_cast<uint32_t>(v);
            outConfig.settingSources["swThreads"] = "GLIDE_NG_SW_THREADS";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SW_SIMD")) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.swUseSimd = b;
            outConfig.settingSources["swSimd"] = "GLIDE_NG_SW_SIMD";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SW_TILE_SIZE")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.swTileSize = static_cast<uint32_t>(v);
            outConfig.settingSources["swTileSize"] = "GLIDE_NG_SW_TILE_SIZE";
        }
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_SW_AA")) {
        outConfig.swAaMode = ToLower(envVal);
        outConfig.settingSources["swAa"] = "GLIDE_NG_SW_AA";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_VK_CACHE_PATH")) {
        outConfig.vkCachePath = envVal;
        outConfig.settingSources["vkCachePath"] = "GLIDE_NG_VK_CACHE_PATH";
    }

    if (const char* envVal = GetNonEmptyEnv("GLIDE_NG_GLES_VBO_SIZE")) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.glesVboSizeMb = static_cast<uint32_t>(v);
            outConfig.settingSources["glesVboSize"] = "GLIDE_NG_GLES_VBO_SIZE";
        }
    }

    // 6. Final validation & normalization
    if (outConfig.msaaSamples != 1 && outConfig.msaaSamples != 2 &&
        outConfig.msaaSamples != 4 && outConfig.msaaSamples != 8 &&
        outConfig.msaaSamples != 16) {
        outConfig.msaaSamples = 1;
    }
    if (outConfig.swTileSize < 16) {
        outConfig.swTileSize = 16;
    }
    if (outConfig.glesVboSizeMb < 1) {
        outConfig.glesVboSizeMb = 8;
    }
    if (outConfig.swAaMode != "ssaa") {
        outConfig.swAaMode = "analytical";
    }

    return true;
}

}  // namespace GlideWrapper
