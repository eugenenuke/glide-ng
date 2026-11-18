#include "IniConfigLoader.h"
#include "Logger.h"
#include "glide_ng/env_vars.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

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

std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

// Strips inline '#' or ';' comments outside quotes when preceded by whitespace
// (or at position 0), then unquotes matching "..." or '...' literals.
std::string ParseIniValue(const std::string& raw) {
    std::string trimmed = Trim(raw);
    if (trimmed.empty()) return "";

    char quote = '\0';
    size_t cutPos = trimmed.size();
    for (size_t i = 0; i < trimmed.size(); ++i) {
        char c = trimmed[i];
        if (quote != '\0') {
            if (c == '\\' && i + 1 < trimmed.size()) {
                ++i;
                continue;
            }
            if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if ((c == '#' || c == ';') &&
            (i == 0 || std::isspace(static_cast<unsigned char>(trimmed[i - 1])))) {
            cutPos = i;
            break;
        }
    }

    std::string val = Trim(trimmed.substr(0, cutPos));
    if (val.size() >= 2 &&
        ((val.front() == '"' && val.back() == '"') ||
         (val.front() == '\'' && val.back() == '\''))) {
        const char q = val.front();
        std::string unquoted;
        unquoted.reserve(val.size() - 2);
        for (size_t i = 1; i + 1 < val.size(); ++i) {
            if (val[i] == '\\' && i + 2 < val.size()) {
                char next = val[++i];
                if (next == q || next == '\\') {
                    unquoted.push_back(next);
                } else {
                    unquoted.push_back('\\');
                    unquoted.push_back(next);
                }
            } else {
                unquoted.push_back(val[i]);
            }
        }
        return unquoted;
    }
    return val;
}

struct IniEntry {
    std::string section;  // normalized lowercase, empty if before any [section]
    std::string key;      // normalized lowercase
    std::string value;
};

std::vector<IniEntry> ParseIniDocument(const std::string& content) {
    std::vector<IniEntry> entries;
    size_t pos = 0;
    // Strip UTF-8 BOM if present
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        pos = 3;
    }

    std::string currentSection;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string line = content.substr(pos, nl - pos);
        pos = (nl < content.size()) ? nl + 1 : content.size();

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // Section header: [section] (optional trailing inline comment)
        if (trimmed.front() == '[') {
            size_t closeBracket = trimmed.find(']');
            if (closeBracket != std::string::npos) {
                currentSection = ToLower(Trim(trimmed.substr(1, closeBracket - 1)));
            }
            continue;
        }

        // Key = Value
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            // Non-INI line (e.g. stray braces or JSON lines) — ignore cleanly
            continue;
        }

        std::string key = ToLower(Trim(trimmed.substr(0, eq)));
        if (key.empty()) continue;
        std::string value = ParseIniValue(trimmed.substr(eq + 1));
        entries.push_back({currentSection, std::move(key), std::move(value)});
    }
    return entries;
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

const char* CardModelCanonicalName(CardModel model) {
    switch (model) {
        case CardModel::VoodooGraphics: return "voodoo1";
        case CardModel::VoodooRush:     return "rush";
        case CardModel::Voodoo2:        return "voodoo2";
        case CardModel::Voodoo3:        return "voodoo3";
        case CardModel::Voodoo4:        return "voodoo4";
        case CardModel::Voodoo5:        return "voodoo5";
    }
    return "voodoo2";
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
    if (m == "H4" || m == "VOODOO4" || m == "V4" || m == "VSA100" || m == "VSA-100") {
        outModel = CardModel::Voodoo4;
        return true;
    }
    if (m == "H5" || m == "VOODOO5" || m == "V5") {
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

bool ParseUint64String(const std::string& raw, uint64_t& outVal) {
    if (raw.empty()) return false;
    for (char c : raw) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    char* endPtr = nullptr;
    unsigned long long parsed = std::strtoull(raw.c_str(), &endPtr, 10);
    if (endPtr == raw.c_str() || *endPtr != '\0') return false;
    outVal = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseTraceFramesString(const std::string& raw, bool& outActive,
                            uint64_t& outStart, uint64_t& outEnd) {
    std::string trimmed = Trim(raw);
    if (trimmed.empty()) return false;

    std::string lower = ToLower(trimmed);
    if (lower == "all" || lower == "*" || lower == "off" || lower == "none") {
        outActive = false;
        outStart = 0;
        outEnd = UINT64_MAX;
        return true;
    }

    size_t dash = trimmed.find('-');
    if (dash == std::string::npos) {
        uint64_t single = 0;
        if (!ParseUint64String(trimmed, single)) return false;
        outActive = true;
        outStart = single;
        outEnd = single;
        return true;
    }

    if (trimmed.find('-', dash + 1) != std::string::npos) return false;

    std::string left = trimmed.substr(0, dash);
    std::string right = trimmed.substr(dash + 1);
    if (left.empty() && right.empty()) return false;

    uint64_t startVal = 0;
    uint64_t endVal = UINT64_MAX;
    if (!left.empty() && !ParseUint64String(left, startVal)) return false;
    if (!right.empty() && !ParseUint64String(right, endVal)) return false;
    if (startVal > endVal) return false;

    outActive = true;
    outStart = startVal;
    outEnd = endVal;
    return true;
}

bool ParsePerfLogString(const std::string& raw, bool& outEnabled, std::string& outCsvPath) {
    if (raw.empty()) return false;
    bool b = false;
    if (ParseBoolString(raw, b)) {
        outEnabled = b;
        outCsvPath.clear();
        return true;
    }
    std::string lower = ToLower(raw);
    if (lower == "log" || lower == "summary") {
        outEnabled = true;
        outCsvPath.clear();
        return true;
    }
    outEnabled = true;
    outCsvPath = raw;
    return true;
}

bool ParseSnapshotString(const std::string& raw, bool& outEnabled,
                         uint64_t& outFrame, std::string& outPath) {
    std::string trimmed = Trim(raw);
    if (trimmed.empty()) return false;

    std::string lower = ToLower(trimmed);
    if (lower == "0" || lower == "false" || lower == "off" || lower == "no" || lower == "none") {
        outEnabled = false;
        outFrame = 0;
        outPath.clear();
        return true;
    }
    if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        outEnabled = true;
        outFrame = 0;
        outPath.clear();
        return true;
    }

    // "@N" -> capture frame N to auto-generated path
    if (trimmed[0] == '@') {
        uint64_t frame = 0;
        if (!ParseUint64String(trimmed.substr(1), frame)) return false;
        outEnabled = true;
        outFrame = frame;
        outPath.clear();
        return true;
    }

    // Pure integer >= 2 -> capture frame N to auto-generated path
    uint64_t numericFrame = 0;
    if (ParseUint64String(trimmed, numericFrame)) {
        outEnabled = true;
        outFrame = numericFrame;
        outPath.clear();
        return true;
    }

    // "<path>@N" -> capture frame N to <path>
    size_t atPos = trimmed.rfind('@');
    if (atPos != std::string::npos && atPos > 0 && atPos + 1 < trimmed.size()) {
        uint64_t frame = 0;
        if (ParseUint64String(trimmed.substr(atPos + 1), frame)) {
            outEnabled = true;
            outFrame = frame;
            outPath = trimmed.substr(0, atPos);
            return true;
        }
    }

    // Plain file path -> capture frame 0 to <path>
    outEnabled = true;
    outFrame = 0;
    outPath = trimmed;
    return true;
}

const char* HudPositionToShortString(HudPosition pos) {
    switch (pos) {
        case HudPosition::TOP_LEFT:     return "tl";
        case HudPosition::TOP_RIGHT:    return "tr";
        case HudPosition::BOTTOM_LEFT:  return "bl";
        case HudPosition::BOTTOM_RIGHT: return "br";
    }
    return "tl";
}

bool ParseHudPositionString(const std::string& raw, HudPosition& outPos) {
    const std::string val = ToLower(Trim(raw));
    if (val == "tl" || val == "top-left" || val == "topleft") {
        outPos = HudPosition::TOP_LEFT;
        return true;
    }
    if (val == "tr" || val == "top-right" || val == "topright") {
        outPos = HudPosition::TOP_RIGHT;
        return true;
    }
    if (val == "bl" || val == "bottom-left" || val == "bottomleft") {
        outPos = HudPosition::BOTTOM_LEFT;
        return true;
    }
    if (val == "br" || val == "bottom-right" || val == "bottomright") {
        outPos = HudPosition::BOTTOM_RIGHT;
        return true;
    }
    return false;
}

std::string FormatHudMask(uint32_t mask) {
    if (mask == HUD_NONE) return "off";
    if (mask == HUD_FULL) return "full";
    std::string out;
    auto add = [&](uint32_t bit, const char* name) {
        if (mask & bit) {
            if (!out.empty()) out += ",";
            out += name;
        }
    };
    add(HUD_FPS, "fps");
    add(HUD_FRAMETIMES, "frametimes");
    add(HUD_BACKEND, "backend");
    add(HUD_API, "api");
    add(HUD_DRAWCALLS, "drawcalls");
    add(HUD_LFB, "lfb");
    return out;
}

std::string BuildHudSpec(uint32_t mask, HudPosition pos, float scale) {
    if (mask == HUD_NONE) return "off";
    std::string spec = FormatHudMask(mask);
    char buf[64];
    std::snprintf(buf, sizeof(buf), ",pos=%s,scale=%.2g",
                  HudPositionToShortString(pos), static_cast<double>(scale));
    spec += buf;
    return spec;
}

bool ParseHudString(const std::string& raw,
                    uint32_t& outMask,
                    HudPosition& outPos,
                    float& outScale,
                    float& outOpacity,
                    std::string& outSpec) {
    std::string trimmed = Trim(raw);
    if (trimmed.empty()) return false;

    const std::string wholeLower = ToLower(trimmed);
    if (wholeLower == "0" || wholeLower == "false" || wholeLower == "off" || wholeLower == "none") {
        outMask = HUD_NONE;
        outSpec = "off";
        return true;
    }
    if (wholeLower == "1" || wholeLower == "true" || wholeLower == "on") {
        outMask = HUD_FPS;
        outSpec = "fps";
        return true;
    }

    uint32_t mask = HUD_NONE;
    HudPosition pos = outPos;
    float scale = outScale;
    float opacity = outOpacity;
    bool recognizedAny = false;
    bool hasExplicitElement = false;
    bool explicitlyDisabled = false;

    size_t start = 0;
    while (start <= trimmed.size()) {
        size_t sep = trimmed.find_first_of(",;", start);
        if (sep == std::string::npos) sep = trimmed.size();
        std::string tok = Trim(trimmed.substr(start, sep - start));

        if (!tok.empty()) {
            const std::string lowerTok = ToLower(tok);
            const size_t eq = lowerTok.find('=');
            if (eq != std::string::npos) {
                const std::string key = lowerTok.substr(0, eq);
                const std::string val = lowerTok.substr(eq + 1);
                if (key == "scale") {
                    double d = 1.0;
                    if (ParseDoubleString(val, d)) {
                        scale = static_cast<float>(std::clamp(d, 0.5, 4.0));
                        recognizedAny = true;
                    }
                } else if (key == "opacity") {
                    double d = 0.78;
                    if (ParseDoubleString(val, d)) {
                        opacity = static_cast<float>(std::clamp(d, 0.0, 1.0));
                        recognizedAny = true;
                    }
                } else if (key == "position" || key == "pos") {
                    if (ParseHudPositionString(val, pos)) {
                        recognizedAny = true;
                    }
                }
            } else {
                if (lowerTok == "0" || lowerTok == "off" || lowerTok == "none" || lowerTok == "false") {
                    mask = HUD_NONE;
                    explicitlyDisabled = true;
                    recognizedAny = true;
                } else if (lowerTok == "1" || lowerTok == "on" || lowerTok == "true" || lowerTok == "fps") {
                    mask |= HUD_FPS;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "frametimes" || lowerTok == "frametime") {
                    mask |= HUD_FRAMETIMES;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "backend" || lowerTok == "devinfo") {
                    mask |= HUD_BACKEND;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "api") {
                    mask |= HUD_API;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "drawcalls" || lowerTok == "draws") {
                    mask |= HUD_DRAWCALLS;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "lfb") {
                    mask |= HUD_LFB;
                    hasExplicitElement = true;
                    recognizedAny = true;
                } else if (lowerTok == "full" || lowerTok == "all") {
                    mask |= HUD_FULL;
                    hasExplicitElement = true;
                    recognizedAny = true;
                }
            }
        }
        if (sep == trimmed.size()) break;
        start = sep + 1;
    }

    if (!recognizedAny) return false;
    if (!hasExplicitElement && !explicitlyDisabled) {
        mask = HUD_FPS;
    }

    outMask = mask;
    outPos = pos;
    outScale = scale;
    outOpacity = opacity;
    outSpec = BuildHudSpec(mask, pos, scale);
    return true;
}

const char* GetNonEmptyEnv(const char* name) {
    const char* val = std::getenv(name);
    return (val && val[0] != '\0') ? val : nullptr;
}

bool SectionMatches(const std::string& sec, const char* expected) {
    return sec.empty() || sec == expected;
}

}  // namespace

bool IniConfigLoader::Load(const std::string& configFilePath, WrapperConfig& outConfig) {
    // 1. Reset to baseline defaults to prevent state leakage across reloads
    outConfig = WrapperConfig{};

    bool hasApiVersion = false;
    bool hasFbiMem = false;
    bool hasTmuCount = false;
    bool hasTmuMem = false;
    bool hasIniMaxFps = false;

    // Resolve config file path (GLIDE_NG_CONFIG overrides default path)
    std::string effectivePath = configFilePath.empty() ? std::string(DEFAULT_CONFIG_FILE)
                                                       : configFilePath;
    if (const char* envConfigPath = GetNonEmptyEnv(GLIDE_NG_ENV_CONFIG)) {
        effectivePath = envConfigPath;
        outConfig.settingSources["configFile"] = GLIDE_NG_ENV_CONFIG;
    }
    outConfig.configFilePath = effectivePath;

    // 2. Layer 2: INI configuration file (overrides defaults)
    std::ifstream file(effectivePath);
    if (file.is_open()) {
        outConfig.configFileLoaded = true;
        std::stringstream ss;
        ss << file.rdbuf();
        const std::vector<IniEntry> entries = ParseIniDocument(ss.str());

        bool hudSubOptionSet = false;

        for (const auto& entry : entries) {
            const std::string& sec = entry.section;
            const std::string& key = entry.key;
            const std::string& val = entry.value;

            bool handled = false;
            bool boolVal = false;
            int intVal = 0;
            double dblVal = 0.0;

            // [core]
            if (SectionMatches(sec, "core") && key == "backend") {
                handled = true;
                if (!val.empty()) {
                    outConfig.backend = NormalizeBackendString(val);
                    outConfig.settingSources["backend"] = "ini";
                }
            } else if (SectionMatches(sec, "core") &&
                       (key == "headless" || key == "force_no_window")) {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.forceNoWindow = boolVal;
                    outConfig.settingSources["headless"] = "ini";
                }
            }
            // [hardware]
            else if (SectionMatches(sec, "hardware") &&
                     (key == "card_model" || key == "model")) {
                handled = true;
                if (ParseCardModelString(val, outConfig.model)) {
                    outConfig.settingSources["cardModel"] = "ini";
                } else if (!val.empty()) {
                    outConfig.invalidConfigValues.push_back(
                        "Unrecognized hardware.card_model='" + val +
                        "'; keeping '" + CardModelCanonicalName(outConfig.model) +
                        "' (supported: voodoo1, rush, voodoo2, voodoo3, voodoo4, voodoo5)");
                }
            } else if (SectionMatches(sec, "hardware") && key == "api_version") {
                handled = true;
                if (ParseApiVersionString(val, outConfig.apiVersion)) {
                    hasApiVersion = true;
                    outConfig.settingSources["apiVersion"] = "ini";
                } else if (!val.empty()) {
                    outConfig.invalidConfigValues.push_back(
                        "Unrecognized hardware.api_version='" + val +
                        "' (supported: 2.1, 2.40, 2.42, 2.43, 2.61)");
                }
            } else if (SectionMatches(sec, "hardware") && key == "fbi_memory_mb") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal > 0) {
                    outConfig.fbiMemoryMb = static_cast<uint32_t>(intVal);
                    hasFbiMem = true;
                    outConfig.settingSources["fbiMemoryMb"] = "ini";
                }
            } else if (SectionMatches(sec, "hardware") && key == "tmu_count") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal >= 1 && intVal <= 3) {
                    outConfig.tmuCount = static_cast<uint32_t>(intVal);
                    hasTmuCount = true;
                    outConfig.settingSources["tmuCount"] = "ini";
                }
            } else if (SectionMatches(sec, "hardware") && key == "tmu_memory_mb") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal > 0) {
                    outConfig.tmuMemoryMb = static_cast<uint32_t>(intVal);
                    hasTmuMem = true;
                    outConfig.settingSources["tmuMemoryMb"] = "ini";
                }
            } else if (SectionMatches(sec, "hardware") &&
                       (key == "sli" || key == "enable_sli")) {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.enableSli = boolVal;
                    outConfig.settingSources["enableSli"] = "ini";
                }
            } else if (SectionMatches(sec, "hardware") && key == "vendor") {
                handled = true;
                outConfig.reportedVendorOverride = val;
                outConfig.settingSources["vendor"] = "ini";
            } else if (SectionMatches(sec, "hardware") && key == "renderer") {
                handled = true;
                outConfig.reportedRendererOverride = val;
                outConfig.settingSources["renderer"] = "ini";
            }
            // [display]
            else if (SectionMatches(sec, "display") && key == "vsync") {
                handled = true;
                if (ParseVsyncString(val, outConfig.vsync, outConfig.vsyncStrictFifo)) {
                    outConfig.settingSources["vsync"] = "ini";
                }
            } else if (SectionMatches(sec, "display") && key == "vsync_strict_fifo") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.vsyncStrictFifo = boolVal;
                    if (boolVal) outConfig.vsync = true;
                    outConfig.settingSources["vsyncStrictFifo"] = "ini";
                }
            } else if (SectionMatches(sec, "display") && key == "max_fps") {
                handled = true;
                if (ParseDoubleString(val, dblVal) && dblVal >= 0.0) {
                    outConfig.maxFps = static_cast<float>(dblVal);
                    hasIniMaxFps = true;
                    outConfig.settingSources["maxFps"] = "ini";
                }
            } else if (SectionMatches(sec, "display") &&
                       (key == "msaa" || key == "msaa_samples")) {
                handled = true;
                if (ParseIntString(val, intVal) && intVal >= 0) {
                    outConfig.msaaSamples = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["msaaSamples"] = "ini";
                }
            } else if (SectionMatches(sec, "display") && key == "window_scale") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal >= 1 && intVal <= 8) {
                    outConfig.windowScale = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["windowScale"] = "ini";
                }
            } else if (SectionMatches(sec, "display") && key == "presentation_filter") {
                handled = true;
                if (ParsePresentationFilterString(val, outConfig.presentationFilter)) {
                    outConfig.settingSources["presentationFilter"] = "ini";
                }
            }
            // [logging]
            else if ((sec == "logging" && (key == "level" || key == "log_level")) ||
                     (sec.empty() && key == "log_level")) {
                handled = true;
                if (ParseLogLevelString(val, outConfig.logLevel)) {
                    outConfig.settingSources["logLevel"] = "ini";
                }
            } else if ((sec == "logging" && (key == "console" || key == "log_to_console")) ||
                       (sec.empty() && key == "log_to_console")) {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.logToConsole = boolVal;
                    outConfig.settingSources["logToConsole"] = "ini";
                }
            } else if ((sec == "logging" && (key == "file" || key == "log_file")) ||
                       (sec.empty() && key == "log_file")) {
                handled = true;
                if (!val.empty()) {
                    outConfig.logFile = val;
                    outConfig.settingSources["logFile"] = "ini";
                }
            } else if ((sec == "logging" && (key == "categories" || key == "log_categories")) ||
                       (sec.empty() && key == "log_categories")) {
                handled = true;
                uint32_t mask = 0u;
                if (ParseLogCategories(val, mask)) {
                    outConfig.logCategoryMask = mask;
                    outConfig.logCategoriesSpec = FormatLogCategories(mask);
                    outConfig.settingSources["logCategories"] = "ini";
                }
            } else if (SectionMatches(sec, "logging") && key == "trace_frames") {
                handled = true;
                bool active = false;
                uint64_t startF = 0;
                uint64_t endF = UINT64_MAX;
                if (ParseTraceFramesString(val, active, startF, endF)) {
                    outConfig.traceFrameWindowActive = active;
                    outConfig.traceFrameStart = startF;
                    outConfig.traceFrameEnd = endF;
                    outConfig.traceFramesSpec = val;
                    outConfig.settingSources["traceFrames"] = "ini";
                }
            } else if (SectionMatches(sec, "logging") && key == "perf_log") {
                handled = true;
                if (ParsePerfLogString(val, outConfig.perfLogEnabled, outConfig.perfLogCsvPath)) {
                    outConfig.settingSources["perfLog"] = "ini";
                }
            }
            // [hud]
            else if ((sec == "hud" && (key == "elements" || key == "hud")) ||
                     (sec.empty() && key == "hud")) {
                handled = true;
                if (ParseHudString(val, outConfig.hudMask, outConfig.hudPosition,
                                   outConfig.hudScale, outConfig.hudOpacity, outConfig.hudSpec)) {
                    outConfig.settingSources["hud"] = "ini";
                }
            } else if (sec == "hud" && (key == "position" || key == "pos")) {
                handled = true;
                if (ParseHudPositionString(val, outConfig.hudPosition)) {
                    hudSubOptionSet = true;
                    outConfig.settingSources["hud"] = "ini";
                }
            } else if (sec == "hud" && key == "scale") {
                handled = true;
                if (ParseDoubleString(val, dblVal)) {
                    outConfig.hudScale = static_cast<float>(std::clamp(dblVal, 0.5, 4.0));
                    hudSubOptionSet = true;
                    outConfig.settingSources["hud"] = "ini";
                }
            } else if (sec == "hud" && key == "opacity") {
                handled = true;
                if (ParseDoubleString(val, dblVal)) {
                    outConfig.hudOpacity = static_cast<float>(std::clamp(dblVal, 0.0, 1.0));
                    hudSubOptionSet = true;
                    outConfig.settingSources["hud"] = "ini";
                }
            }
            // [glide] (authentic 3dfx overrides)
            else if ((SectionMatches(sec, "glide") || sec == "display") && key == "no_splash") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.noSplash = boolVal;
                    outConfig.settingSources["noSplash"] = "ini";
                }
            } else if ((SectionMatches(sec, "glide") || sec == "display") &&
                       key == "shameless_plug") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.shamelessPlugMode = boolVal ? 1 : 0;
                    outConfig.settingSources["shamelessPlug"] = "ini";
                } else if (ParseIntString(val, intVal)) {
                    outConfig.shamelessPlugMode = (intVal < 0) ? -1 : (intVal != 0 ? 1 : 0);
                    outConfig.settingSources["shamelessPlug"] = "ini";
                }
            } else if ((SectionMatches(sec, "glide") || sec == "display") &&
                       key == "swap_interval") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal >= 0) {
                    outConfig.swapInterval = intVal;
                    outConfig.vsync = (intVal > 0);
                    outConfig.vsyncStrictFifo = false;
                    outConfig.settingSources["swapInterval"] = "ini";
                    outConfig.settingSources["vsync"] = "ini";
                }
            } else if ((SectionMatches(sec, "glide") || sec == "display") && key == "gamma") {
                handled = true;
                if (ParseDoubleString(val, dblVal) && dblVal > 0.0) {
                    outConfig.gammaOverride = static_cast<float>(std::clamp(dblVal, 0.1, 10.0));
                    outConfig.settingSources["gamma"] = "ini";
                }
            } else if ((SectionMatches(sec, "glide") || sec == "display") &&
                       key == "screen_refresh") {
                handled = true;
                if (ParseIntString(val, intVal) && intVal > 0) {
                    outConfig.screenRefreshHz = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["screenRefresh"] = "ini";
                    if (!hasIniMaxFps) {
                        outConfig.maxFps = static_cast<float>(intVal);
                        outConfig.settingSources["maxFps"] = "ini";
                    }
                }
            } else if (SectionMatches(sec, "glide") && key == "ignore_reopen") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.ignoreReopen = boolVal;
                    outConfig.settingSources["ignoreReopen"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "no_dither_sub") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.noDitherSub = boolVal;
                    outConfig.settingSources["noDitherSub"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "force_bilinear") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.forceBilinear = boolVal;
                    outConfig.settingSources["forceBilinear"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "force_trilinear") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.forceTrilinear = boolVal;
                    outConfig.settingSources["forceTrilinear"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "texmap_disable") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.texmapDisable = boolVal;
                    outConfig.settingSources["texmapDisable"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "bounds_check") {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.boundsCheck = boolVal;
                    outConfig.settingSources["boundsCheck"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "alloc_color") {
                handled = true;
                if (ParseIntString(val, intVal) && (intVal == 2 || intVal == 3)) {
                    outConfig.allocColorBuffers = intVal;
                    outConfig.settingSources["allocColor"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "alloc_aux") {
                handled = true;
                if (ParseIntString(val, intVal) && (intVal == 0 || intVal == 1)) {
                    outConfig.allocAuxBuffers = intVal;
                    outConfig.settingSources["allocAux"] = "ini";
                }
            } else if (SectionMatches(sec, "glide") && key == "snapshot") {
                handled = true;
                if (ParseSnapshotString(val, outConfig.snapshotEnabled,
                                        outConfig.snapshotFrame, outConfig.snapshotPath)) {
                    outConfig.settingSources["snapshot"] = "ini";
                }
            }
            // [software]
            else if ((sec == "software" && (key == "threads" || key == "sw_threads")) ||
                     (sec.empty() && key == "sw_threads")) {
                handled = true;
                if (ParseIntString(val, intVal) && intVal >= 0) {
                    outConfig.swThreads = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["swThreads"] = "ini";
                }
            } else if ((sec == "software" && (key == "simd" || key == "sw_simd")) ||
                       (sec.empty() && key == "sw_simd")) {
                handled = true;
                if (ParseBoolString(val, boolVal)) {
                    outConfig.swUseSimd = boolVal;
                    outConfig.settingSources["swSimd"] = "ini";
                }
            } else if ((sec == "software" && (key == "tile_size" || key == "sw_tile_size")) ||
                       (sec.empty() && key == "sw_tile_size")) {
                handled = true;
                if (ParseIntString(val, intVal) && intVal > 0) {
                    outConfig.swTileSize = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["swTileSize"] = "ini";
                }
            } else if ((sec == "software" && (key == "aa" || key == "sw_aa")) ||
                       (sec.empty() && key == "sw_aa")) {
                handled = true;
                if (!val.empty()) {
                    outConfig.swAaMode = ToLower(val);
                    outConfig.settingSources["swAa"] = "ini";
                }
            }
            // [vulkan]
            else if ((sec == "vulkan" && (key == "cache_path" || key == "vk_cache_path")) ||
                     (sec.empty() && key == "vk_cache_path")) {
                handled = true;
                if (!val.empty()) {
                    outConfig.vkCachePath = val;
                    outConfig.settingSources["vkCachePath"] = "ini";
                }
            }
            // [opengl_es]
            else if (((sec == "opengl_es" || sec == "gles") &&
                      (key == "vbo_size_mb" || key == "gles_vbo_size")) ||
                     (sec.empty() && key == "gles_vbo_size")) {
                handled = true;
                if (ParseIntString(val, intVal) && intVal > 0) {
                    outConfig.glesVboSizeMb = static_cast<uint32_t>(intVal);
                    outConfig.settingSources["glesVboSize"] = "ini";
                }
            }

            if (!handled) {
                std::string fullKey = sec.empty() ? key : (sec + "." + key);
                outConfig.unknownConfigKeys.push_back(std::move(fullKey));
            }
        }

        if (hudSubOptionSet && outConfig.hudMask != HUD_NONE) {
            outConfig.hudSpec = BuildHudSpec(outConfig.hudMask, outConfig.hudPosition,
                                             outConfig.hudScale);
        }
    }

    // 3. Card model & API version env overrides (evaluated before deriving card-model defaults)
    if (const char* envCard = GetNonEmptyEnv(GLIDE_NG_ENV_CARD_MODEL)) {
        if (ParseCardModelString(envCard, outConfig.model)) {
            outConfig.settingSources["cardModel"] = GLIDE_NG_ENV_CARD_MODEL;
        } else {
            outConfig.invalidConfigValues.push_back(
                std::string("Unrecognized ") + GLIDE_NG_ENV_CARD_MODEL + "='" + envCard +
                "'; keeping '" + CardModelCanonicalName(outConfig.model) +
                "' (supported: voodoo1, rush, voodoo2, voodoo3, voodoo4, voodoo5)");
        }
    }
    if (const char* envApi = GetNonEmptyEnv(GLIDE_NG_ENV_API_VERSION)) {
        if (ParseApiVersionString(envApi, outConfig.apiVersion)) {
            hasApiVersion = true;
            outConfig.settingSources["apiVersion"] = GLIDE_NG_ENV_API_VERSION;
        } else {
            outConfig.invalidConfigValues.push_back(
                std::string("Unrecognized ") + GLIDE_NG_ENV_API_VERSION + "='" + envApi +
                "' (supported: 2.1, 2.40, 2.42, 2.43, 2.61)");
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
        case CardModel::Voodoo4:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            if (!hasTmuCount) outConfig.tmuCount = 2;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 32;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 16;
            break;
        case CardModel::Voodoo5:
            if (!hasApiVersion) outConfig.apiVersion = ApiVersion::GLIDE_2_61;
            if (!hasTmuCount) outConfig.tmuCount = 2;
            if (!hasFbiMem) outConfig.fbiMemoryMb = 64;
            if (!hasTmuMem) outConfig.tmuMemoryMb = 32;
            break;
    }

    // 4. Layer 3: Authentic 3dfx Glide environment variables
    // Precedence: Defaults < INI < Authentic FX_GLIDE_*/SST_* < GLIDE_NG_*
    if (const char* envNoSplash = GetNonEmptyEnv(GLIDE_FX_ENV_NO_SPLASH)) {
        bool b = true;
        if (!ParseBoolString(envNoSplash, b)) {
            b = true;
        }
        outConfig.noSplash = b;
        outConfig.settingSources["noSplash"] = GLIDE_FX_ENV_NO_SPLASH;
    }

    if (const char* envPlug = GetNonEmptyEnv(GLIDE_FX_ENV_SHAMELESS_PLUG)) {
        bool b = true;
        if (!ParseBoolString(envPlug, b)) {
            int n = 1;
            b = ParseIntString(envPlug, n) ? (n != 0) : true;
        }
        outConfig.shamelessPlugMode = b ? 1 : 0;
        outConfig.settingSources["shamelessPlug"] = GLIDE_FX_ENV_SHAMELESS_PLUG;
    }

    if (const char* envSwapInterval = GetNonEmptyEnv(GLIDE_FX_ENV_SWAPINTERVAL)) {
        int interval = 0;
        if (ParseIntString(envSwapInterval, interval) && interval >= 0) {
            outConfig.swapInterval = interval;
            outConfig.vsync = (interval > 0);
            outConfig.vsyncStrictFifo = false;
            outConfig.settingSources["vsync"] = GLIDE_FX_ENV_SWAPINTERVAL;
            outConfig.settingSources["swapInterval"] = GLIDE_FX_ENV_SWAPINTERVAL;
        }
    }

    // Layer 3 (Phase 2): Authentic hardware sizing, display, compatibility & diagnostic env vars
    if (const char* envVal = GetNonEmptyEnv(GLIDE_FX_ENV_FBRAM)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.fbiMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["fbiMemoryMb"] = GLIDE_FX_ENV_FBRAM;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_FX_ENV_TMU_MEMSIZE)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.tmuMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuMemoryMb"] = GLIDE_FX_ENV_TMU_MEMSIZE;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_FX_ENV_NUM_TMUS)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 1 && v <= 3) {
            outConfig.tmuCount = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuCount"] = GLIDE_FX_ENV_NUM_TMUS;
        }
    }

    auto applyGammaEnv = [&](const char* envName) {
        if (const char* envVal = GetNonEmptyEnv(envName)) {
            double g = 0.0;
            if (ParseDoubleString(envVal, g) && g > 0.0) {
                outConfig.gammaOverride = static_cast<float>(std::clamp(g, 0.1, 10.0));
                outConfig.settingSources["gamma"] = envName;
            }
        }
    };
    applyGammaEnv(GLIDE_FX_ENV_GAMMA);
    applyGammaEnv(GLIDE_FX_ENV_V2_GAMMA);

    auto applyRefreshEnv = [&](const char* envName) {
        if (const char* envVal = GetNonEmptyEnv(envName)) {
            int hz = 0;
            if (ParseIntString(envVal, hz) && hz > 0) {
                outConfig.screenRefreshHz = static_cast<uint32_t>(hz);
                outConfig.maxFps = static_cast<float>(hz);
                outConfig.settingSources["screenRefresh"] = envName;
                outConfig.settingSources["maxFps"] = envName;
            }
        }
    };
    applyRefreshEnv(GLIDE_FX_ENV_SCREENREFRESH);
    applyRefreshEnv(GLIDE_FX_ENV_V2_SCREENREFRESH);

    auto applyFxBoolEnv = [&](const char* envName, const char* sourceKey, bool& target) {
        if (const char* envVal = GetNonEmptyEnv(envName)) {
            bool b = true;
            if (!ParseBoolString(envVal, b)) {
                int n = 1;
                b = ParseIntString(envVal, n) ? (n != 0) : true;
            }
            target = b;
            outConfig.settingSources[sourceKey] = envName;
        }
    };
    applyFxBoolEnv(GLIDE_FX_ENV_IGNORE_REOPEN, "ignoreReopen", outConfig.ignoreReopen);
    applyFxBoolEnv(GLIDE_FX_ENV_NO_DITHER_SUB, "noDitherSub", outConfig.noDitherSub);
    applyFxBoolEnv(GLIDE_FX_ENV_FORCE_BILINEAR, "forceBilinear", outConfig.forceBilinear);
    applyFxBoolEnv(GLIDE_FX_ENV_FORCE_TRILINEAR, "forceTrilinear", outConfig.forceTrilinear);
    applyFxBoolEnv(GLIDE_FX_ENV_TEXMAP_DISABLE, "texmapDisable", outConfig.texmapDisable);
    applyFxBoolEnv(GLIDE_FX_ENV_BOUNDS_CHECK, "boundsCheck", outConfig.boundsCheck);

    if (const char* envVal = GetNonEmptyEnv(GLIDE_FX_ENV_ALLOC_COLOR)) {
        int v = 0;
        if (ParseIntString(envVal, v) && (v == 2 || v == 3)) {
            outConfig.allocColorBuffers = v;
            outConfig.settingSources["allocColor"] = GLIDE_FX_ENV_ALLOC_COLOR;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_FX_ENV_ALLOC_AUX)) {
        int v = 0;
        if (ParseIntString(envVal, v) && (v == 0 || v == 1)) {
            outConfig.allocAuxBuffers = v;
            outConfig.settingSources["allocAux"] = GLIDE_FX_ENV_ALLOC_AUX;
        }
    }

    auto applySnapshotEnv = [&](const char* envName) {
        if (const char* envVal = GetNonEmptyEnv(envName)) {
            if (ParseSnapshotString(envVal, outConfig.snapshotEnabled,
                                    outConfig.snapshotFrame, outConfig.snapshotPath)) {
                outConfig.settingSources["snapshot"] = envName;
            }
        }
    };
    applySnapshotEnv(GLIDE_FX_ENV_SNAPSHOT_ALIAS);
    applySnapshotEnv(GLIDE_FX_ENV_SNAPSHOT);

    // 5. Layer 4: glide-ng Wrapper Environment Overrides (GLIDE_NG_* — highest priority)
    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_BACKEND)) {
        outConfig.backend = NormalizeBackendString(envVal);
        outConfig.settingSources["backend"] = GLIDE_NG_ENV_BACKEND;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_HEADLESS)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.forceNoWindow = b;
            outConfig.settingSources["headless"] = GLIDE_NG_ENV_HEADLESS;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_FBI_MEM_MB)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.fbiMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["fbiMemoryMb"] = GLIDE_NG_ENV_FBI_MEM_MB;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_TMU_COUNT)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 1 && v <= 3) {
            outConfig.tmuCount = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuCount"] = GLIDE_NG_ENV_TMU_COUNT;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_TMU_MEM_MB)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.tmuMemoryMb = static_cast<uint32_t>(v);
            outConfig.settingSources["tmuMemoryMb"] = GLIDE_NG_ENV_TMU_MEM_MB;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SLI)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.enableSli = b;
            outConfig.settingSources["enableSli"] = GLIDE_NG_ENV_SLI;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_VENDOR)) {
        outConfig.reportedVendorOverride = envVal;
        outConfig.settingSources["vendor"] = GLIDE_NG_ENV_VENDOR;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_RENDERER)) {
        outConfig.reportedRendererOverride = envVal;
        outConfig.settingSources["renderer"] = GLIDE_NG_ENV_RENDERER;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_VSYNC)) {
        if (ParseVsyncString(envVal, outConfig.vsync, outConfig.vsyncStrictFifo)) {
            outConfig.swapInterval = outConfig.vsync ? 1 : 0;
            outConfig.settingSources["vsync"] = GLIDE_NG_ENV_VSYNC;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_MAX_FPS)) {
        double v = 0.0;
        if (ParseDoubleString(envVal, v) && v >= 0.0) {
            outConfig.maxFps = static_cast<float>(v);
            outConfig.settingSources["maxFps"] = GLIDE_NG_ENV_MAX_FPS;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_MSAA)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 0) {
            outConfig.msaaSamples = static_cast<uint32_t>(v);
            outConfig.settingSources["msaaSamples"] = GLIDE_NG_ENV_MSAA;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_WINDOW_SCALE)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 1 && v <= 8) {
            outConfig.windowScale = static_cast<uint32_t>(v);
            outConfig.settingSources["windowScale"] = GLIDE_NG_ENV_WINDOW_SCALE;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_PRESENTATION_FILTER)) {
        if (ParsePresentationFilterString(envVal, outConfig.presentationFilter)) {
            outConfig.settingSources["presentationFilter"] = GLIDE_NG_ENV_PRESENTATION_FILTER;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_LOG_LEVEL)) {
        if (ParseLogLevelString(envVal, outConfig.logLevel)) {
            outConfig.settingSources["logLevel"] = GLIDE_NG_ENV_LOG_LEVEL;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_LOG_CONSOLE)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.logToConsole = b;
            outConfig.settingSources["logToConsole"] = GLIDE_NG_ENV_LOG_CONSOLE;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_LOG_FILE)) {
        outConfig.logFile = envVal;
        outConfig.settingSources["logFile"] = GLIDE_NG_ENV_LOG_FILE;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_LOG_CATEGORIES)) {
        uint32_t mask = 0u;
        if (ParseLogCategories(envVal, mask)) {
            outConfig.logCategoryMask = mask;
            outConfig.logCategoriesSpec = FormatLogCategories(mask);
            outConfig.settingSources["logCategories"] = GLIDE_NG_ENV_LOG_CATEGORIES;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_TRACE_FRAMES)) {
        bool active = false;
        uint64_t startF = 0;
        uint64_t endF = UINT64_MAX;
        if (ParseTraceFramesString(envVal, active, startF, endF)) {
            outConfig.traceFrameWindowActive = active;
            outConfig.traceFrameStart = startF;
            outConfig.traceFrameEnd = endF;
            outConfig.traceFramesSpec = envVal;
            outConfig.settingSources["traceFrames"] = GLIDE_NG_ENV_TRACE_FRAMES;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_PERF_LOG)) {
        if (ParsePerfLogString(envVal, outConfig.perfLogEnabled, outConfig.perfLogCsvPath)) {
            outConfig.settingSources["perfLog"] = GLIDE_NG_ENV_PERF_LOG;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_HUD)) {
        if (ParseHudString(envVal, outConfig.hudMask, outConfig.hudPosition,
                           outConfig.hudScale, outConfig.hudOpacity, outConfig.hudSpec)) {
            outConfig.settingSources["hud"] = GLIDE_NG_ENV_HUD;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_NO_SPLASH)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.noSplash = b;
            outConfig.settingSources["noSplash"] = GLIDE_NG_ENV_NO_SPLASH;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SHAMELESS_PLUG)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.shamelessPlugMode = b ? 1 : 0;
            outConfig.settingSources["shamelessPlug"] = GLIDE_NG_ENV_SHAMELESS_PLUG;
        }
    }

    // Backend-specific tuning overrides
    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SW_THREADS)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v >= 0) {
            outConfig.swThreads = static_cast<uint32_t>(v);
            outConfig.settingSources["swThreads"] = GLIDE_NG_ENV_SW_THREADS;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SW_SIMD)) {
        bool b = false;
        if (ParseBoolString(envVal, b)) {
            outConfig.swUseSimd = b;
            outConfig.settingSources["swSimd"] = GLIDE_NG_ENV_SW_SIMD;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SW_TILE_SIZE)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.swTileSize = static_cast<uint32_t>(v);
            outConfig.settingSources["swTileSize"] = GLIDE_NG_ENV_SW_TILE_SIZE;
        }
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_SW_AA)) {
        outConfig.swAaMode = ToLower(envVal);
        outConfig.settingSources["swAa"] = GLIDE_NG_ENV_SW_AA;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_VK_CACHE_PATH)) {
        outConfig.vkCachePath = envVal;
        outConfig.settingSources["vkCachePath"] = GLIDE_NG_ENV_VK_CACHE_PATH;
    }

    if (const char* envVal = GetNonEmptyEnv(GLIDE_NG_ENV_GLES_VBO_SIZE)) {
        int v = 0;
        if (ParseIntString(envVal, v) && v > 0) {
            outConfig.glesVboSizeMb = static_cast<uint32_t>(v);
            outConfig.settingSources["glesVboSize"] = GLIDE_NG_ENV_GLES_VBO_SIZE;
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
