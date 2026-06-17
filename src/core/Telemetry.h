#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <iostream>

namespace GlideWrapper {

struct ScopeStats {
    uint64_t callCount{0};
    int64_t totalDurationUs{0};
    int64_t minDurationUs{999999999};
    int64_t maxDurationUs{0};
};

class FrameTracker {
public:
    FrameTracker();
    void MarkFrameStart();
    void MarkFrameEnd(float maxFps);

    float GetCurrentFps() const { return m_currentFps; }
    float GetAvgFrameTimeMs() const { return m_avgFrameTimeMs; }
    float GetRenderTimeMs() const { return m_lastRenderTimeUs / 1000.0f; }

private:
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_frameStart;
    Clock::time_point m_lastFrameEnd;

    int64_t m_lastRenderTimeUs{0};
    int64_t m_lastFrameIntervalUs{0};

    // Smoothed statistics for overlay representation
    float m_currentFps{0.0f};
    float m_avgFrameTimeMs{0.0f};
    int m_frameCount{0};
    int64_t m_accumulatedTimeUs{0};

    void UpdateStats(int64_t intervalUs);
};

class TelemetryManager {
public:
    static TelemetryManager& GetInstance();

    void RecordScope(const std::string& name, int64_t durationUs);
    FrameTracker& GetFrameTracker() { return m_frameTracker; }
    
    // Geometry / Triangle Tracking
    void IncrementTrianglesProcessed(uint64_t count) { m_trianglesProcessed += count; }
    void IncrementTrianglesDrawn(uint64_t count) { m_trianglesDrawn += count; }
    uint64_t GetTrianglesProcessed() const { return m_trianglesProcessed.load(); }
    uint64_t GetTrianglesDrawn() const { return m_trianglesDrawn.load(); }
    void ResetTriangleStats() {
        m_trianglesProcessed = 0;
        m_trianglesDrawn = 0;
    }

    void PrintReport();
    void Reset();

private:
    TelemetryManager();
    ~TelemetryManager() = default;
    TelemetryManager(const TelemetryManager&) = delete;
    TelemetryManager& operator=(const TelemetryManager&) = delete;

    std::mutex m_mutex;
    std::unordered_map<std::string, ScopeStats> m_stats;
    FrameTracker m_frameTracker;

    // Atomic metrics for standard Glide APIs
    std::atomic<uint64_t> m_trianglesProcessed{0};
    std::atomic<uint64_t> m_trianglesDrawn{0};
};

class TelemetryScope {
public:
    TelemetryScope(const char* name)
        : m_name(name), m_startTime(std::chrono::high_resolution_clock::now()) {}

    ~TelemetryScope() {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
        TelemetryManager::GetInstance().RecordScope(m_name, duration.count());
    }

private:
    const char* m_name;
    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace GlideWrapper

// RAII Profiler Helper Macro
#define GLIDE_PROFILE_SCOPE(name) GlideWrapper::TelemetryScope timer##__LINE__(name)
