#pragma once
#include <algorithm>
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

// Per-frame CPU time attribution buckets (exclusive of nested child buckets).
enum class CpuBucket : uint8_t {
  Draw = 0,
  Tex = 1,
  Lfb = 2,
  State = 3,
  Count = 4,
};

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
  // M6.1 CPU time split (populated when GLIDE_NG_PERF_LOG is enabled)
  float wrapperCpuMs{0.0f};     // Total CPU time inside glide-ng buckets
  float appCpuMs{0.0f};         // Guest/app/emulator CPU time outside glide-ng
  float drawCpuMs{0.0f};        // Draw submission + SW tile rasterization CPU time
  float texCpuMs{0.0f};         // Texture download / swizzle / GPU upload CPU time
  float lfbCpuMs{0.0f};         // LFB lock/unlock/diff/overlay flush CPU time
  float stateCpuMs{0.0f};       // Glide state-setter CPU time
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

  // Geometry / Triangle Tracking (always compiled; single-writer render thread,
  // relaxed load+store avoids locked RMW instructions on every triangle)
  void IncrementTrianglesProcessed(uint64_t count) {
    m_trianglesProcessed.store(
        m_trianglesProcessed.load(std::memory_order_relaxed) + count,
        std::memory_order_relaxed);
    m_frameTrianglesProcessed.store(
        m_frameTrianglesProcessed.load(std::memory_order_relaxed) +
            static_cast<uint32_t>(count),
        std::memory_order_relaxed);
  }
  void IncrementTrianglesDrawn(uint64_t count) {
    m_trianglesDrawn.store(
        m_trianglesDrawn.load(std::memory_order_relaxed) + count,
        std::memory_order_relaxed);
    m_frameTrianglesDrawn.store(
        m_frameTrianglesDrawn.load(std::memory_order_relaxed) +
            static_cast<uint32_t>(count),
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
  bool IsPerfTimingEnabled() const {
    return m_perfTimingEnabled.load(std::memory_order_relaxed);
  }
  static bool IsPerfTimingEnabledFast() noexcept {
    return s_perfTimingEnabled.load(std::memory_order_relaxed);
  }
  static bool UseTscClockFast() noexcept {
    return s_useTscClock.load(std::memory_order_relaxed);
  }
  bool IsUsingTscClock() const noexcept {
    return s_useTscClock.load(std::memory_order_relaxed);
  }
  double GetTscNsPerTick() const noexcept {
    return m_tscNsPerTick.load(std::memory_order_relaxed);
  }
  void RecordCpuBucketTicks(CpuBucket bucket, int64_t ticks) {
    if (ticks <= 0) return;
    const size_t idx = static_cast<size_t>(bucket);
    if (idx < static_cast<size_t>(CpuBucket::Count)) {
      m_frameCpuBucketTicks[idx].fetch_add(ticks, std::memory_order_relaxed);
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
  void EnsureClockCalibratedLocked();

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
  std::atomic<int64_t>
      m_frameCpuBucketTicks[static_cast<size_t>(CpuBucket::Count)]{};
  std::atomic<bool> m_perfTimingEnabled{false};
  inline static std::atomic<bool> s_perfTimingEnabled{false};
  inline static std::atomic<bool> s_useTscClock{false};
  std::atomic<double> m_tscNsPerTick{1.0};
  std::atomic<uint64_t> m_frameIndex{0};

  // Perf logging, CSV writer, and last-frame snapshot
  mutable std::mutex m_perfMutex;
  bool m_perfLogEnabled{false};
  bool m_tscProbed{false};
  bool m_tscAvailable{false};
  bool m_hasCalibAnchor{false};
  std::chrono::steady_clock::time_point m_lastCalibWall{};
  uint64_t m_lastCalibTsc{0};
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
  double m_summaryWrapperCpuMs{0.0};
  double m_summaryAppCpuMs{0.0};
  double m_summaryDrawCpuMs{0.0};
  double m_summaryTexCpuMs{0.0};
  double m_summaryLfbCpuMs{0.0};
  double m_summaryStateCpuMs{0.0};
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

// Lightweight exclusive-stack RAII timer for per-frame wrapper CPU breakdown.
// Zero clock calls when perf logging is disabled or when nested inside the
// same bucket; stores active state by value (no stack-frame pointers) and
// flushes/resumes the parent bucket cleanly across nested bucket transitions.
class CpuBucketScope {
 public:
  explicit CpuBucketScope(CpuBucket bucket) noexcept {
    if (__builtin_expect(!TelemetryManager::IsPerfTimingEnabledFast(), 1)) return;
    auto& tm = TelemetryManager::GetInstance();
    ActiveState& state = CurrentState();
    if (state.active && state.bucket == bucket) return;
    const int64_t nowTicks = NowTicks();
    if (state.active) {
      const int64_t parentSliceTicks =
          std::max<int64_t>(0, nowTicks - state.resumeTicks);
      tm.RecordCpuBucketTicks(state.bucket, parentSliceTicks);
      m_prevActive = true;
      m_prevBucket = state.bucket;
    }
    state.active = true;
    state.bucket = bucket;
    state.resumeTicks = nowTicks;
    m_bucket = bucket;
    m_active = true;
  }

  ~CpuBucketScope() noexcept {
    if (!m_active) return;
    const int64_t nowTicks = NowTicks();
    ActiveState& state = CurrentState();
    const int64_t selfSliceTicks =
        std::max<int64_t>(0, nowTicks - state.resumeTicks);
    TelemetryManager::GetInstance().RecordCpuBucketTicks(m_bucket,
                                                         selfSliceTicks);
    if (m_prevActive) {
      state.active = true;
      state.bucket = m_prevBucket;
      state.resumeTicks = nowTicks;
    } else {
      state.active = false;
    }
  }

  CpuBucketScope(const CpuBucketScope&) = delete;
  CpuBucketScope& operator=(const CpuBucketScope&) = delete;

 private:
  struct ActiveState {
    bool active{false};
    CpuBucket bucket{CpuBucket::Draw};
    int64_t resumeTicks{0};
  };

  static int64_t NowTicks() noexcept {
#if (defined(__x86_64__) || defined(__i386__)) && \
    (defined(__GNUC__) || defined(__clang__))
    if (__builtin_expect(TelemetryManager::UseTscClockFast(), 1)) {
      uint32_t lo = 0;
      uint32_t hi = 0;
      __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
      return static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
    }
#endif
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  static ActiveState& CurrentState() noexcept {
    static thread_local ActiveState s_state;
    return s_state;
  }

  CpuBucket m_bucket{CpuBucket::Draw};
  CpuBucket m_prevBucket{CpuBucket::Draw};
  bool m_active{false};
  bool m_prevActive{false};
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

// Always-compiled lightweight per-frame CPU attribution macro (zero clock
// overhead when GLIDE_NG_PERF_LOG is disabled).
#define GLIDE_CPU_SCOPE_CONCAT_INNER(a, b) a##b
#define GLIDE_CPU_SCOPE_CONCAT(a, b) GLIDE_CPU_SCOPE_CONCAT_INNER(a, b)
#define GLIDE_CPU_SCOPE(bucket) \
  GlideWrapper::CpuBucketScope GLIDE_CPU_SCOPE_CONCAT(_glideCpuScope_, __LINE__)(bucket)

// Always-compiled lightweight triangle stat macros
#define GLIDE_INCREMENT_TRIANGLES_PROCESSED(count)                           \
  GlideWrapper::TelemetryManager::GetInstance().IncrementTrianglesProcessed( \
      count)
#define GLIDE_INCREMENT_TRIANGLES_DRAWN(count) \
  GlideWrapper::TelemetryManager::GetInstance().IncrementTrianglesDrawn(count)
