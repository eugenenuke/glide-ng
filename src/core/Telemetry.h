#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace GlideWrapper {

#ifdef ENABLE_TELEMETRY
struct ScopeStats {
  uint64_t callCount{0};
  int64_t totalDurationUs{0};
  int64_t minDurationUs{999999999};
  int64_t maxDurationUs{0};
};
#endif

// Always-compiled lightweight per-frame performance snapshot (used by M2 perf
// logging/CSV tracing and M3 HUD).
struct FrameMetrics {
  uint64_t frameIndex{0};
  double timestampMs{0.0};
  float frameTimeMs{0.0f};      // Total frame-to-frame interval (render+present+sleep)
  float renderTimeMs{0.0f};     // Active render/CPU time excluding present & cap sleep
  float swapPresentMs{0.0f};    // Backend presentation / submit / blit time
  float fpsCapSleepMs{0.0f};    // Frame-rate limiter sleep time
  float smoothedFps{0.0f};      // Smoothed FPS
  uint32_t drawCalls{0};        // Total GPU draw calls / SW tile flushes
  uint32_t stateFlushes{0};     // Batch flushes triggered mid-frame by state changes
  uint32_t trianglesProcessed{0};
  uint32_t trianglesDrawn{0};
  uint32_t lfbReadLocks{0};
  uint32_t lfbWriteLocks{0};
  uint64_t lfbBytesRead{0};
  uint64_t lfbBytesWritten{0};
  uint32_t texUploads{0};
  uint64_t texUploadBytes{0};
  uint32_t psoBuilds{0};
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
  using Clock = std::chrono::steady_clock;
  Clock::time_point m_frameStart;
  Clock::time_point m_lastFrameEnd;

  int64_t m_lastRenderTimeUs{0};
  int64_t m_lastFrameIntervalUs{0};

  // Smoothed statistics for overlay & reporting
  float m_currentFps{0.0f};
  float m_avgFrameTimeMs{0.0f};
  int m_frameCount{0};
  int64_t m_accumulatedTimeUs{0};

  void UpdateStats(int64_t intervalUs);
};

class TelemetryManager {
 public:
  static TelemetryManager& GetInstance();

#ifdef ENABLE_TELEMETRY
  void RecordScope(const std::string& name, int64_t durationUs);
  void PrintReport();
#else
  void RecordScope(const std::string&, int64_t) {}
  void PrintReport() {}
#endif

  FrameTracker& GetFrameTracker() { return m_frameTracker; }

  // Always-compiled perf logging & per-frame CSV configuration
  void ConfigurePerfLog(bool enabled, const std::string& csvPath);
  void ShutdownPerfLog();
  void Reset();

  // Geometry / Triangle Tracking (always compiled; relaxed atomics)
  void IncrementTrianglesProcessed(uint64_t count) {
    m_trianglesProcessed.fetch_add(count, std::memory_order_relaxed);
    m_frameTrianglesProcessed.fetch_add(static_cast<uint32_t>(count),
                                        std::memory_order_relaxed);
  }
  void IncrementTrianglesDrawn(uint64_t count) {
    m_trianglesDrawn.fetch_add(count, std::memory_order_relaxed);
    m_frameTrianglesDrawn.fetch_add(static_cast<uint32_t>(count),
                                    std::memory_order_relaxed);
  }
  uint64_t GetTrianglesProcessed() const {
    return m_trianglesProcessed.load(std::memory_order_relaxed);
  }
  uint64_t GetTrianglesDrawn() const {
    return m_trianglesDrawn.load(std::memory_order_relaxed);
  }
  void ResetTriangleStats() {
    m_trianglesProcessed.store(0, std::memory_order_relaxed);
    m_trianglesDrawn.store(0, std::memory_order_relaxed);
  }

  // Always-compiled lightweight per-frame counters
  void RecordDrawCall(bool fromStateFlush = false) {
    m_frameDrawCalls.fetch_add(1, std::memory_order_relaxed);
    if (fromStateFlush) {
      m_frameStateFlushes.fetch_add(1, std::memory_order_relaxed);
    }
  }
  void RecordLfbRead(uint64_t bytes) {
    m_frameLfbReadLocks.fetch_add(1, std::memory_order_relaxed);
    m_frameLfbBytesRead.fetch_add(bytes, std::memory_order_relaxed);
  }
  void RecordLfbWrite(uint64_t bytes) {
    m_frameLfbWriteLocks.fetch_add(1, std::memory_order_relaxed);
    m_frameLfbBytesWritten.fetch_add(bytes, std::memory_order_relaxed);
  }
  void RecordTexUpload(uint64_t bytes) {
    m_frameTexUploads.fetch_add(1, std::memory_order_relaxed);
    m_frameTexUploadBytes.fetch_add(bytes, std::memory_order_relaxed);
  }
  void RecordPsoBuild() {
    m_framePsoBuilds.fetch_add(1, std::memory_order_relaxed);
  }
  void RecordPresentTimeUs(int64_t us) {
    if (us > 0) {
      m_framePresentUs.fetch_add(us, std::memory_order_relaxed);
    }
  }

  // Invoked once per completed frame from FrameTracker::MarkFrameEnd
  void OnFrameComplete(int64_t intervalUs, int64_t activeUs, int64_t sleepUs,
                       float smoothedFps);

  FrameMetrics GetLastFrameMetrics() const;
  uint64_t GetFrameCount() const {
    return m_frameIndex.load(std::memory_order_relaxed);
  }

 private:
  TelemetryManager();
  ~TelemetryManager();
  TelemetryManager(const TelemetryManager&) = delete;
  TelemetryManager& operator=(const TelemetryManager&) = delete;

  void ResetFrameCounters();
  void EmitPerfSummaryLocked();

#ifdef ENABLE_TELEMETRY
  std::mutex m_mutex;
  std::unordered_map<std::string, ScopeStats> m_stats;
#endif
  FrameTracker m_frameTracker;

  // Always-compiled cumulative triangle counters (for grTriStats)
  std::atomic<uint64_t> m_trianglesProcessed{0};
  std::atomic<uint64_t> m_trianglesDrawn{0};

  // Always-compiled per-frame counters (swapped to 0 on frame completion)
  std::atomic<uint32_t> m_frameDrawCalls{0};
  std::atomic<uint32_t> m_frameStateFlushes{0};
  std::atomic<uint32_t> m_frameTrianglesProcessed{0};
  std::atomic<uint32_t> m_frameTrianglesDrawn{0};
  std::atomic<uint32_t> m_frameLfbReadLocks{0};
  std::atomic<uint32_t> m_frameLfbWriteLocks{0};
  std::atomic<uint64_t> m_frameLfbBytesRead{0};
  std::atomic<uint64_t> m_frameLfbBytesWritten{0};
  std::atomic<uint32_t> m_frameTexUploads{0};
  std::atomic<uint64_t> m_frameTexUploadBytes{0};
  std::atomic<uint32_t> m_framePsoBuilds{0};
  std::atomic<int64_t> m_framePresentUs{0};
  std::atomic<uint64_t> m_frameIndex{0};

  // Perf logging, CSV writer, and last-frame snapshot
  mutable std::mutex m_perfMutex;
  bool m_perfLogEnabled{false};
  std::ofstream m_csvFile;
  uint64_t m_csvRowsSinceFlush{0};
  FrameMetrics m_lastFrameMetrics{};
  std::chrono::steady_clock::time_point m_perfStartTime;
  std::chrono::steady_clock::time_point m_lastSummaryTime;

  // Periodic [PERF] window accumulators
  uint64_t m_summaryFrames{0};
  double m_summaryFrameMs{0.0};
  double m_summaryRenderMs{0.0};
  double m_summaryPresentMs{0.0};
  double m_summarySleepMs{0.0};
  uint64_t m_summaryDrawCalls{0};
  uint64_t m_summaryStateFlushes{0};
  uint64_t m_summaryTrisDrawn{0};
  uint64_t m_summaryLfbReads{0};
  uint64_t m_summaryLfbWrites{0};
  uint64_t m_summaryLfbBytes{0};
  uint64_t m_summaryTexUploads{0};
  uint64_t m_summaryTexBytes{0};
  uint64_t m_summaryPsoBuilds{0};
};

#ifdef ENABLE_TELEMETRY
class TelemetryScope {
 public:
  TelemetryScope(const char* name)
      : m_name(name), m_startTime(std::chrono::high_resolution_clock::now()) {}

  ~TelemetryScope() {
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - m_startTime);
    TelemetryManager::GetInstance().RecordScope(m_name, duration.count());
  }

 private:
  const char* m_name;
  std::chrono::high_resolution_clock::time_point m_startTime;
};
#endif

}  // namespace GlideWrapper

// RAII Profiler Helper Macro
#ifdef ENABLE_TELEMETRY
#define GLIDE_PROFILE_SCOPE(name) \
  GlideWrapper::TelemetryScope timer##__LINE__(name)
#else
#define GLIDE_PROFILE_SCOPE(name) \
  do {                            \
  } while (0)
#endif

// Always-compiled lightweight triangle stat macros
#define GLIDE_INCREMENT_TRIANGLES_PROCESSED(count)                           \
  GlideWrapper::TelemetryManager::GetInstance().IncrementTrianglesProcessed( \
      count)
#define GLIDE_INCREMENT_TRIANGLES_DRAWN(count) \
  GlideWrapper::TelemetryManager::GetInstance().IncrementTrianglesDrawn(count)
