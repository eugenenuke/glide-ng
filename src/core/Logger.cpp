#include "Logger.h"
#include "build_version.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace GlideWrapper {

namespace {
std::string GetLoadedModulePath() {
#if defined(__unix__) || defined(__APPLE__)
    static int s_moduleAnchor = 0;
    Dl_info info{};
    if (dladdr(&s_moduleAnchor, &info) != 0 &&
        info.dli_fname != nullptr && info.dli_fname[0] != '\0') {
        return std::string(info.dli_fname);
    }
#endif
    return "unknown";
}
}  // namespace

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Logger::Initialize(const std::string& logFilePath, LogLevel level, bool logToConsole) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeLevel = level;
    m_logToConsole = logToConsole;
    if (m_activeLevel == LogLevel::OFF) return;

    if (m_file.is_open()) {
        m_file.close();
    }

    m_file.open(logFilePath, std::ios::out | std::ios::trunc);
    if (m_file.is_open()) {
        m_file << "--- " << WRAPPER_FULL_NAME << " (" << WRAPPER_VERSION
               << " | git:" << GLIDE_GIT_HASH
               << " | " << GLIDE_BUILD_TYPE << ") Log ---\n";
        m_file << "Module: " << GetLoadedModulePath() << "\n";
        m_file.flush();
    }
}

void Logger::Log(LogLevel level, const char* component, const std::string& message) {
    if (level == LogLevel::OFF) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) return;

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::CRITICAL: levelStr = "CRIT"; break;
        case LogLevel::WARN:     levelStr = "WARN"; break;
        case LogLevel::INFO:     levelStr = "INFO"; break;
        case LogLevel::DEBUG:    levelStr = "DBUG"; break;
        default: break;
    }

    m_file << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] "
           << "[" << levelStr << "] "
           << "[" << component << "] "
           << message << "\n";
           
    m_file.flush();

    // Also print to stdout for unit test runner visibility if enabled!
    if (m_logToConsole) {
        std::cout << "[" << levelStr << "] [" << component << "] " << message << std::endl;
    }
}

void Logger::LogExecutionSummary(const WrapperConfig& config) {
    if (m_activeLevel < LogLevel::INFO) return;

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
        case CardModel::Voodoo3:        ss << "Voodoo3 (H3)"; break;
        case CardModel::Voodoo5:        ss << "Voodoo5 (H5)"; break;
    }
    
    ss << "\n  FBI Framebuffer RAM  : " << config.fbiMemoryMb << " MB"
       << "\n  Active TMU Chips     : " << config.tmuCount
       << "\n  Dedicated RAM / TMU  : " << config.tmuMemoryMb << " MB"
       << "\n  Target Presentation  : " << config.maxFps << " FPS"
       << "\n  AGP Bus Draw Latency : " << config.drawCallDelayUs << " us";

    std::string backendPretty = "Vulkan 1.1 GPU Port";
    if (config.backend == "opengl_es") backendPretty = "OpenGL ES 3.2 GPU Port";
    else if (config.backend == "software") backendPretty = "Host CPU Software Reference";

    ss << "\n  Voodoo2 SLI Mode     : " << (config.enableSli ? "ENABLED" : "DISABLED")
       << "\n  Reported API Base    : " << config.apiVersionOverride
       << "\n  Active Emulation     : " << backendPretty
       << "\n========================================================";

    Log(LogLevel::INFO, "Startup", ss.str());
}

} // namespace GlideWrapper
