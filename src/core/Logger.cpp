#include "Logger.h"
#include "build_version.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace GlideWrapper {

namespace {

constexpr size_t kFlushByteThreshold = 32768;  // 32 KB
constexpr auto kFlushTimeThreshold = std::chrono::milliseconds(250);

std::string GetLoadedModulePath() {
#if defined(__unix__) || defined(__APPLE__)
    static int s_moduleAnchor = 0;
    Dl_info info{};
    if (dladdr(&s_moduleAnchor, &info) != 0 &&
        info.dli_fname != nullptr && info.dli_fname[0] != '\0') {
        return std::string(info.dli_fname);
    }
#elif defined(_WIN32)
    static int s_moduleAnchor = 0;
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&s_moduleAnchor), &hMod) &&
        hMod != nullptr) {
        char pathBuf[MAX_PATH] = {};
        DWORD len = GetModuleFileNameA(hMod, pathBuf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            return std::string(pathBuf, len);
        }
    }
#endif
    return "unknown";
}

std::string TrimAndLower(std::string s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    s = s.substr(start, end - start);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

bool TokenToCategoryBit(const std::string& token, uint32_t& outBit) {
    if (token == "all" || token == "*") { outBit = LOG_CAT_ALL; return true; }
    if (token == "none") { outBit = LOG_CAT_NONE; return true; }
    if (token == "general" || token == "core" || token == "startup") { outBit = LOG_CAT_GENERAL; return true; }
    if (token == "api" || token == "frontend") { outBit = LOG_CAT_API; return true; }
    if (token == "lfb") { outBit = LOG_CAT_LFB; return true; }
    if (token == "tex" || token == "texture" || token == "textures") { outBit = LOG_CAT_TEX; return true; }
    if (token == "state" || token == "rasterizer") { outBit = LOG_CAT_STATE; return true; }
    if (token == "draw" || token == "backend" || token == "gpu") { outBit = LOG_CAT_DRAW; return true; }
    if (token == "wsi" || token == "window") { outBit = LOG_CAT_WSI; return true; }
    if (token == "perf" || token == "telemetry") { outBit = LOG_CAT_PERF; return true; }
    return false;
}

std::string FormatWallClockNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

}  // namespace

LogCategory MapComponentToCategory(const char* component) {
    if (!component || component[0] == '\0') return LOG_CAT_GENERAL;
    switch (component[0]) {
        case 'F':
            if (std::strcmp(component, "Frontend") == 0) return LOG_CAT_API;
            break;
        case 'A':
            if (std::strcmp(component, "API") == 0) return LOG_CAT_API;
            break;
        case 'L':
            if (std::strcmp(component, "LFB") == 0) return LOG_CAT_LFB;
            break;
        case 'R':
            if (std::strcmp(component, "ReadLFB") == 0) return LOG_CAT_LFB;
            if (std::strcmp(component, "Rasterizer") == 0) return LOG_CAT_STATE;
            break;
        case 'W':
            if (std::strcmp(component, "WriteLFB") == 0) return LOG_CAT_LFB;
            if (std::strcmp(component, "WSI") == 0) return LOG_CAT_WSI;
            break;
        case 'C':
            if (std::strcmp(component, "CpuLfbStaging") == 0) return LOG_CAT_LFB;
            if (std::strcmp(component, "Core") == 0) return LOG_CAT_GENERAL;
            break;
        case 'T':
            if (std::strcmp(component, "TextureManager") == 0) return LOG_CAT_TEX;
            if (std::strcmp(component, "Telemetry") == 0) return LOG_CAT_PERF;
            break;
        case '3':
            if (std::strcmp(component, "3dfParser") == 0) return LOG_CAT_TEX;
            break;
        case 'S':
            if (std::strcmp(component, "StateTracker") == 0) return LOG_CAT_STATE;
            if (std::strcmp(component, "Software") == 0 ||
                std::strcmp(component, "SoftwareBackendBase") == 0) {
                return LOG_CAT_DRAW;
            }
            if (std::strcmp(component, "Startup") == 0 ||
                std::strcmp(component, "Stub") == 0) {
                return LOG_CAT_GENERAL;
            }
            break;
        case 'V':
            if (std::strcmp(component, "Vulkan") == 0) return LOG_CAT_DRAW;
            break;
        case 'G':
            if (std::strcmp(component, "GLES") == 0) return LOG_CAT_DRAW;
            break;
        case 'D':
            if (std::strcmp(component, "Draw") == 0) return LOG_CAT_DRAW;
            break;
        case 'P':
            if (std::strcmp(component, "Perf") == 0) return LOG_CAT_PERF;
            break;
        default:
            break;
    }
    return LOG_CAT_GENERAL;
}

bool ParseLogCategories(const std::string& spec, uint32_t& outMask) {
    if (spec.empty()) return false;

    uint32_t mask = 0u;
    bool initialized = false;
    bool parsedAny = false;

    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t comma = spec.find(',', pos);
        std::string raw = (comma == std::string::npos)
                              ? spec.substr(pos)
                              : spec.substr(pos, comma - pos);
        std::string tok = TrimAndLower(raw);
        if (!tok.empty()) {
            bool negate = false;
            if (tok[0] == '-') {
                negate = true;
                tok = TrimAndLower(tok.substr(1));
            } else if (tok[0] == '+') {
                tok = TrimAndLower(tok.substr(1));
            }

            uint32_t bit = 0u;
            if (TokenToCategoryBit(tok, bit)) {
                if (!initialized) {
                    mask = negate ? LOG_CAT_ALL : 0u;
                    initialized = true;
                }
                if (negate) {
                    mask &= ~bit;
                } else if (bit == LOG_CAT_NONE) {
                    mask = 0u;
                } else {
                    mask |= bit;
                }
                parsedAny = true;
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }

    if (!parsedAny) return false;
    outMask = mask;
    return true;
}

std::string FormatLogCategories(uint32_t mask) {
    constexpr uint32_t kAllKnownBits =
        LOG_CAT_GENERAL | LOG_CAT_API | LOG_CAT_LFB | LOG_CAT_TEX |
        LOG_CAT_STATE | LOG_CAT_DRAW | LOG_CAT_WSI | LOG_CAT_PERF;
    if ((mask & kAllKnownBits) == kAllKnownBits) return "all";
    if ((mask & kAllKnownBits) == 0u) return "none";

    struct Entry { uint32_t bit; const char* name; };
    static constexpr Entry kEntries[] = {
        {LOG_CAT_GENERAL, "general"},
        {LOG_CAT_API,     "api"},
        {LOG_CAT_LFB,     "lfb"},
        {LOG_CAT_TEX,     "tex"},
        {LOG_CAT_STATE,   "state"},
        {LOG_CAT_DRAW,    "draw"},
        {LOG_CAT_WSI,     "wsi"},
        {LOG_CAT_PERF,    "perf"},
    };

    std::string out;
    for (const auto& e : kEntries) {
        if ((mask & e.bit) != 0u) {
            if (!out.empty()) out += ',';
            out += e.name;
        }
    }
    return out.empty() ? "none" : out;
}

const char* LogLevelToName(LogLevel level) {
    switch (level) {
        case LogLevel::OFF:      return "OFF";
        case LogLevel::CRITICAL: return "CRIT";
        case LogLevel::WARN:     return "WARN";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::TRACE:    return "TRACE";
    }
    return "INFO";
}

Logger::Logger()
    : m_startTime(std::chrono::steady_clock::now()),
      m_lastFlushTime(m_startTime) {}

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

void Logger::Initialize(const std::string& logFilePath, LogLevel level, bool logToConsole) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeLevel.store(static_cast<uint32_t>(level), std::memory_order_relaxed);
    s_activeLevel.store(static_cast<uint32_t>(level), std::memory_order_relaxed);
    m_categoryMask.store(LOG_CAT_ALL, std::memory_order_relaxed);
    m_traceWindowActive.store(false, std::memory_order_relaxed);
    m_traceFrameStart.store(0, std::memory_order_relaxed);
    m_traceFrameEnd.store(UINT64_MAX, std::memory_order_relaxed);
    m_currentFrame.store(0, std::memory_order_relaxed);
    m_logToConsole = logToConsole;
    m_startTime = std::chrono::steady_clock::now();
    m_lastFlushTime = m_startTime;
    m_bytesSinceFlush = 0;

    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }

    if (level == LogLevel::OFF) return;

    m_file.open(logFilePath, std::ios::out | std::ios::trunc);
    if (m_file.is_open()) {
        m_file << "--- " << WRAPPER_FULL_NAME << " (" << WRAPPER_VERSION
               << " | git:" << GLIDE_GIT_HASH
               << " | " << GLIDE_BUILD_TYPE << ") Log ---\n";
        m_file << "Start Time: " << FormatWallClockNow() << "\n";
        m_file << "Module: " << GetLoadedModulePath() << "\n";
        m_file.flush();
    }
}

void Logger::Initialize(const WrapperConfig& config) {
    const uint32_t clamped = std::min<uint32_t>(
        config.logLevel, static_cast<uint32_t>(LogLevel::TRACE));
    Initialize(config.logFile, static_cast<LogLevel>(clamped), config.logToConsole);
    SetCategoryMask(config.logCategoryMask);
    SetTraceFrameWindow(config.traceFrameWindowActive,
                        config.traceFrameStart,
                        config.traceFrameEnd);
}

void Logger::WriteLineLocked(LogLevel level, const char* component, const std::string& message) {
    auto now = std::chrono::steady_clock::now();
    int64_t elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                            now - m_startTime)
                            .count();
    if (elapsedUs < 0) elapsedUs = 0;
    const long long secs = static_cast<long long>(elapsedUs / 1000000);
    const long long usec = static_cast<long long>(elapsedUs % 1000000);
    const unsigned long long frame =
        static_cast<unsigned long long>(m_currentFrame.load(std::memory_order_relaxed));

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::CRITICAL: levelStr = "CRIT"; break;
        case LogLevel::WARN:     levelStr = "WARN"; break;
        case LogLevel::INFO:     levelStr = "INFO"; break;
        case LogLevel::DEBUG:    levelStr = "DBUG"; break;
        case LogLevel::TRACE:    levelStr = "TRCE"; break;
        default: break;
    }

    char prefix[96];
    const int prefixLen = std::snprintf(
        prefix, sizeof(prefix), "[+%06lld.%06lld] [F%06llu] [%s] [%s] ",
        secs, usec, frame, levelStr, component ? component : "Core");

    if (m_file.is_open()) {
        if (prefixLen > 0) {
            m_file.write(prefix, std::min<size_t>(static_cast<size_t>(prefixLen), sizeof(prefix) - 1));
        }
        m_file.write(message.data(), static_cast<std::streamsize>(message.size()));
        m_file.put('\n');

        m_bytesSinceFlush += static_cast<size_t>(std::max(0, prefixLen)) + message.size() + 1;
        const bool urgent = (static_cast<uint32_t>(level) <= static_cast<uint32_t>(LogLevel::WARN));
        if (urgent || m_bytesSinceFlush >= kFlushByteThreshold ||
            (now - m_lastFlushTime) >= kFlushTimeThreshold) {
            m_file.flush();
            m_bytesSinceFlush = 0;
            m_lastFlushTime = now;
        }
    }

    if (m_logToConsole) {
        if (prefixLen > 0) {
            std::cout.write(prefix, std::min<size_t>(static_cast<size_t>(prefixLen), sizeof(prefix) - 1));
        }
        std::cout.write(message.data(), static_cast<std::streamsize>(message.size()));
        std::cout.put('\n');
        if (static_cast<uint32_t>(level) <= static_cast<uint32_t>(LogLevel::INFO)) {
            std::cout.flush();
        }
    }
}

void Logger::Log(LogLevel level, const char* component, const std::string& message) {
    if (!ShouldLog(level, component)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open() && !m_logToConsole) return;
    WriteLineLocked(level, component, message);
}

void Logger::LogCategoryMessage(LogLevel level, LogCategory category,
                                const char* component, const std::string& message) {
    if (!ShouldLog(level, category)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open() && !m_logToConsole) return;
    WriteLineLocked(level, component, message);
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.flush();
        m_bytesSinceFlush = 0;
        m_lastFlushTime = std::chrono::steady_clock::now();
    }
    if (m_logToConsole) {
        std::cout.flush();
    }
}

void Logger::LogExecutionSummary(const WrapperConfig& config) {
    if (GetLevel() < LogLevel::INFO) return;

    const auto clampedLevel = static_cast<LogLevel>(
        std::min<uint32_t>(config.logLevel, static_cast<uint32_t>(LogLevel::TRACE)));

    std::ostringstream ss;
    ss << "\n========================================================\n"
       << "       3DFX " << WRAPPER_PROJECT_NAME << " (" << WRAPPER_VERSION
       << " | git:" << GLIDE_GIT_HASH << ") EMULATION PROFILE\n"
       << "========================================================\n"
       << "  Wrapper Build        : " << WRAPPER_VERSION
       << " (git:" << GLIDE_GIT_HASH << ", branch:" << GLIDE_GIT_BRANCH
       << ", " << GLIDE_BUILD_TYPE << ")\n"
       << "  Loaded Module        : " << GetLoadedModulePath() << "\n"
       << "  Emulated Accelerator : ";

    switch (config.model) {
        case CardModel::VoodooGraphics: ss << "Voodoo Graphics (SST-1)"; break;
        case CardModel::VoodooRush:     ss << "Voodoo Rush (SST-96)"; break;
        case CardModel::Voodoo2:        ss << "Voodoo2 (CVG)"; break;
        case CardModel::VoodooBanshee:  ss << "Voodoo Banshee (H3)"; break;
        case CardModel::Voodoo3:        ss << "Voodoo3 (H3)"; break;
        case CardModel::Voodoo4:        ss << "Voodoo4 (VSA-100)"; break;
        case CardModel::Voodoo5:        ss << "Voodoo5 (H5)"; break;
    }

    ss << "\n  Config File          : " << config.configFilePath
       << (config.configFileLoaded ? " (loaded)" : " (not found, defaults)")
       << "\n  FBI Framebuffer RAM  : " << config.fbiMemoryMb << " MB"
       << "\n  Active TMU Chips     : " << config.tmuCount
       << "\n  Dedicated RAM / TMU  : " << config.tmuMemoryMb << " MB"
       << "\n  Target Presentation  : "
       << (config.maxFps > 0.0f ? (std::to_string(static_cast<int>(config.maxFps)) + " FPS") : "Uncapped")
       << " (VSync=" << (config.vsync ? (config.vsyncStrictFifo ? "FIFO" : "ON") : "OFF") << ")"
       << "\n  MSAA / Scale / Filter: " << config.msaaSamples << "x / "
       << config.windowScale << "x / "
       << (config.presentationFilter == 1 ? "Linear" : "Nearest")
       << "\n  Splash / Plug Policy : Splash=" << (config.noSplash ? "SUPPRESSED" : "ENABLED")
       << ", Plug=" << (config.shamelessPlugMode < 0 ? "APP" : (config.shamelessPlugMode > 0 ? "FORCED_ON" : "FORCED_OFF"));
    if (config.gammaOverride > 0.0f || config.screenRefreshHz > 0 ||
        config.ignoreReopen || config.noDitherSub || config.forceBilinear ||
        config.forceTrilinear || config.texmapDisable || config.boundsCheck ||
        config.allocColorBuffers >= 2 || config.allocAuxBuffers >= 0 ||
        config.snapshotEnabled) {
        ss << "\n  Authentic 3dfx Flags :";
        if (config.gammaOverride > 0.0f) ss << " gamma=" << config.gammaOverride;
        if (config.screenRefreshHz > 0) ss << " refresh=" << config.screenRefreshHz << "Hz";
        if (config.ignoreReopen) ss << " ignore_reopen=1";
        if (config.noDitherSub) ss << " no_dither_sub=1";
        if (config.forceBilinear) ss << " force_bilinear=1";
        if (config.forceTrilinear) ss << " force_trilinear=1";
        if (config.texmapDisable) ss << " texmap_disable=1";
        if (config.boundsCheck) ss << " bounds_check=1";
        if (config.allocColorBuffers >= 2) ss << " alloc_color=" << config.allocColorBuffers;
        if (config.allocAuxBuffers >= 0) ss << " alloc_aux=" << config.allocAuxBuffers;
        if (config.snapshotEnabled) {
            ss << " snapshot=" << (config.snapshotPath.empty() ? "auto" : config.snapshotPath)
               << "@" << config.snapshotFrame;
        }
    }
    ss << "\n  Log Output           : " << config.logFile
       << " (level=" << config.logLevel << "/" << LogLevelToName(clampedLevel)
       << ", categories=" << FormatLogCategories(config.logCategoryMask)
       << ", console=" << (config.logToConsole ? "ON" : "OFF") << ")";

    if (config.traceFrameWindowActive) {
        ss << "\n  Trace Frame Window   : frames " << config.traceFrameStart << "-";
        if (config.traceFrameEnd == UINT64_MAX) {
            ss << "end";
        } else {
            ss << config.traceFrameEnd;
        }
    }

    ss << "\n  Perf Telemetry       : ";
    if (!config.perfLogEnabled) {
        ss << "DISABLED";
    } else if (!config.perfLogCsvPath.empty()) {
        ss << "ENABLED (periodic [PERF] + CSV: " << config.perfLogCsvPath << ")";
    } else {
        ss << "ENABLED (periodic [PERF] log summary)";
    }

    ss << "\n  HUD Overlay          : ";
    if (config.hudMask == HUD_NONE) {
        ss << "DISABLED";
    } else {
        ss << "ENABLED (" << (config.hudSpec.empty() ? "fps" : config.hudSpec) << ")";
    }

    std::string backendPretty = "Vulkan 1.1 GPU Port";
    if (config.backend == "opengl_es") backendPretty = "OpenGL ES 3.2 GPU Port";
    else if (config.backend == "software") backendPretty = "Host CPU Software Reference";

    const char* apiBaseStr = (config.activeFrontendApi == "Glide 3.x")
                                 ? FormatApiVersionString(config.glide3ApiVersion)
                                 : FormatApiVersionString(config.apiVersion);

    ss << "\n  Voodoo2 SLI Mode     : " << (config.enableSli ? "ENABLED" : "DISABLED")
       << "\n  Reported API Base    : " << apiBaseStr
       << " (2.x=" << FormatApiVersionString(config.apiVersion)
       << ", 3.x=" << FormatApiVersionString(config.glide3ApiVersion) << ")"
       << "\n  Active Emulation     : " << backendPretty;

    if (!config.settingSources.empty()) {
        ss << "\n  Active Overrides     :";
        for (const auto& kv : config.settingSources) {
            ss << "\n    - " << kv.first << " <- " << kv.second;
        }
    }

    ss << "\n========================================================";

    Log(LogLevel::INFO, "Startup", ss.str());

    if (!config.unknownConfigKeys.empty()) {
        std::stringstream warnSs;
        warnSs << "Unrecognized key(s) in " << config.configFilePath << ": ";
        for (size_t i = 0; i < config.unknownConfigKeys.size(); ++i) {
            if (i > 0) warnSs << ", ";
            warnSs << config.unknownConfigKeys[i];
        }
        Log(LogLevel::WARN, "Config", warnSs.str());
    }

    if (!config.invalidConfigValues.empty()) {
        for (const auto& msg : config.invalidConfigValues) {
            Log(LogLevel::WARN, "Config", msg);
            std::fprintf(stderr, "[glide-ng] WARN: %s\n", msg.c_str());
        }
    }

    if (!config.compatibilityWarnings.empty()) {
        for (const auto& msg : config.compatibilityWarnings) {
            Log(LogLevel::WARN, "Config", msg);
            std::fprintf(stderr, "[glide-ng] WARN: %s\n", msg.c_str());
        }
    }
}

} // namespace GlideWrapper
