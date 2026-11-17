#pragma once

#include "WrapperConfig.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#ifdef ERROR
#undef ERROR
#endif

namespace GlideWrapper {

enum class LogLevel : uint32_t {
    OFF = 0,
    CRITICAL = 1,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5
};

enum LogCategory : uint32_t {
    LOG_CAT_NONE    = 0u,
    LOG_CAT_GENERAL = 1u << 0,  // Startup, Backend, Stub, Telemetry, Core
    LOG_CAT_API     = 1u << 1,  // Frontend, API
    LOG_CAT_LFB     = 1u << 2,  // LFB, ReadLFB, WriteLFB, CpuLfbStaging
    LOG_CAT_TEX     = 1u << 3,  // TextureManager, 3dfParser
    LOG_CAT_STATE   = 1u << 4,  // StateTracker, Rasterizer
    LOG_CAT_DRAW    = 1u << 5,  // Vulkan, GLES, Software, SoftwareBackendBase, Draw
    LOG_CAT_WSI     = 1u << 6,  // WSI
    LOG_CAT_PERF    = 1u << 7,  // Perf
    LOG_CAT_ALL     = 0xFFFFFFFFu
};

// Map an existing component tag string to its LogCategory bit.
LogCategory MapComponentToCategory(const char* component);

// Parse comma-separated category spec (e.g. "lfb,api,perf", "all", "-draw").
// Returns true if at least one valid token was parsed.
bool ParseLogCategories(const std::string& spec, uint32_t& outMask);

// Format a LogCategory bitmask into a human-readable string (e.g. "all", "lfb,perf").
std::string FormatLogCategories(uint32_t mask);

// Format a LogLevel enum value into its human-readable name.
const char* LogLevelToName(LogLevel level);

class Logger {
public:
    static Logger& GetInstance();

    // Legacy 3-arg initializer (sets categories=ALL, trace window=inactive)
    void Initialize(const std::string& logFilePath, LogLevel level, bool logToConsole = false);

    // Full config initializer (applies level, categories, trace frame window, console mirror)
    void Initialize(const WrapperConfig& config);

    void SetCategoryMask(uint32_t mask) {
        m_categoryMask.store(mask, std::memory_order_relaxed);
    }
    uint32_t GetCategoryMask() const {
        return m_categoryMask.load(std::memory_order_relaxed);
    }

    void SetTraceFrameWindow(bool active, uint64_t startFrame, uint64_t endFrame) {
        m_traceFrameStart.store(startFrame, std::memory_order_relaxed);
        m_traceFrameEnd.store(endFrame, std::memory_order_relaxed);
        m_traceWindowActive.store(active, std::memory_order_relaxed);
    }

    void SetCurrentFrame(uint64_t frameIndex) {
        m_currentFrame.store(frameIndex, std::memory_order_relaxed);
    }
    uint64_t GetCurrentFrame() const {
        return m_currentFrame.load(std::memory_order_relaxed);
    }

    // Fast lock-free pre-filter invoked before formatting message strings.
    bool ShouldLog(LogLevel level, LogCategory category) const {
        if (level == LogLevel::OFF) return false;
        const uint32_t active = m_activeLevel.load(std::memory_order_relaxed);
        const uint32_t req = static_cast<uint32_t>(level);
        if (active < req) return false;

        if (level == LogLevel::TRACE &&
            m_traceWindowActive.load(std::memory_order_relaxed)) {
            const uint64_t frame = m_currentFrame.load(std::memory_order_relaxed);
            const uint64_t start = m_traceFrameStart.load(std::memory_order_relaxed);
            const uint64_t end = m_traceFrameEnd.load(std::memory_order_relaxed);
            if (frame < start || frame > end) return false;
        }

        // CRITICAL and WARN always bypass category filtering.
        if (req <= static_cast<uint32_t>(LogLevel::WARN)) return true;

        const uint32_t mask = m_categoryMask.load(std::memory_order_relaxed);
        return (mask & static_cast<uint32_t>(category)) != 0u;
    }

    bool ShouldLog(LogLevel level, const char* component) const {
        if (level == LogLevel::OFF) return false;
        const uint32_t active = m_activeLevel.load(std::memory_order_relaxed);
        const uint32_t req = static_cast<uint32_t>(level);
        if (active < req) return false;

        if (level == LogLevel::TRACE &&
            m_traceWindowActive.load(std::memory_order_relaxed)) {
            const uint64_t frame = m_currentFrame.load(std::memory_order_relaxed);
            const uint64_t start = m_traceFrameStart.load(std::memory_order_relaxed);
            const uint64_t end = m_traceFrameEnd.load(std::memory_order_relaxed);
            if (frame < start || frame > end) return false;
        }

        if (req <= static_cast<uint32_t>(LogLevel::WARN)) return true;

        // Always emit the Startup profile banner at INFO+ so trace files retain config context.
        if (component && component[0] == 'S' && std::strcmp(component, "Startup") == 0) {
            return true;
        }

        const uint32_t mask = m_categoryMask.load(std::memory_order_relaxed);
        if (mask == LOG_CAT_ALL) return true;
        return (mask & static_cast<uint32_t>(MapComponentToCategory(component))) != 0u;
    }

    void Log(LogLevel level, const char* component, const std::string& message);
    void LogCategoryMessage(LogLevel level, LogCategory category,
                            const char* component, const std::string& message);

    void Flush();

    // Feature Summary Injection
    void LogExecutionSummary(const WrapperConfig& config);
    void LogHostVulkanCapabilities();

    LogLevel GetLevel() const {
        return static_cast<LogLevel>(m_activeLevel.load(std::memory_order_relaxed));
    }

    static bool IsLevelEnabledFast(LogLevel level) noexcept {
        return level != LogLevel::OFF &&
               s_activeLevel.load(std::memory_order_relaxed) >=
                   static_cast<uint32_t>(level);
    }

private:
    Logger();
    ~Logger();

    void WriteLineLocked(LogLevel level, const char* component, const std::string& message);

    std::ofstream m_file;
    std::atomic<uint32_t> m_activeLevel{static_cast<uint32_t>(LogLevel::WARN)};
    inline static std::atomic<uint32_t> s_activeLevel{
        static_cast<uint32_t>(LogLevel::WARN)};
    std::atomic<uint32_t> m_categoryMask{LOG_CAT_ALL};
    std::atomic<bool> m_traceWindowActive{false};
    std::atomic<uint64_t> m_traceFrameStart{0};
    std::atomic<uint64_t> m_traceFrameEnd{UINT64_MAX};
    std::atomic<uint64_t> m_currentFrame{0};

    bool m_logToConsole{false};
    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::steady_clock::time_point m_lastFlushTime;
    size_t m_bytesSinceFlush{0};
    std::mutex m_mutex;
};

} // namespace GlideWrapper

#define GLIDE_LOG(level, component, msg) \
    do { \
        if (__builtin_expect(GlideWrapper::Logger::IsLevelEnabledFast(GlideWrapper::LogLevel::level), 0)) { \
            auto& __glide_logger = GlideWrapper::Logger::GetInstance(); \
            if (__glide_logger.ShouldLog(GlideWrapper::LogLevel::level, component)) { \
                std::ostringstream __log_ss; \
                __log_ss << msg; \
                __glide_logger.Log(GlideWrapper::LogLevel::level, component, __log_ss.str()); \
            } \
        } \
    } while (0)

#define GLIDE_LOG_CAT(level, category, component, msg) \
    do { \
        if (__builtin_expect(GlideWrapper::Logger::IsLevelEnabledFast(GlideWrapper::LogLevel::level), 0)) { \
            auto& __glide_logger = GlideWrapper::Logger::GetInstance(); \
            if (__glide_logger.ShouldLog(GlideWrapper::LogLevel::level, GlideWrapper::category)) { \
                std::ostringstream __log_ss; \
                __log_ss << msg; \
                __glide_logger.LogCategoryMessage(GlideWrapper::LogLevel::level, GlideWrapper::category, component, __log_ss.str()); \
            } \
        } \
    } while (0)

#define GLIDE_TRACE(category, component, msg) \
    GLIDE_LOG_CAT(TRACE, category, component, msg)
