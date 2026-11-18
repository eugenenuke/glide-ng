#include "Telemetry.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "Logger.h"

namespace GlideWrapper {

namespace {
// Precision hybrid sleep+spin frame pacer.
// On Win32/MinGW-w64, bypasses winpthreads' nanosleep() (which has a known
// double-sleep bug in newer MinGW-w64 releases where sleep_for(15ms) sleeps
// ~30ms under Wine, halving 60 FPS caps to ~30 FPS) by calling native Win32
// Sleep() directly for the coarse interval and spinning for the final ~1.5 ms.
void WaitUntilDeadline(std::chrono::steady_clock::time_point deadline) {
  using Clock = std::chrono::steady_clock;
  while (true) {
    const auto now = Clock::now();
    if (now >= deadline) return;
    const int64_t remUs =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - now)
            .count();
#if defined(_WIN32)
    if (remUs > 2500) {
      const DWORD sleepMs =
          static_cast<DWORD>(std::max<int64_t>(1, (remUs - 1800) / 1000));
      ::Sleep(sleepMs);
    } else if (remUs > 200) {
      ::SwitchToThread();
    } else {
#if defined(__x86_64__) || defined(__i386__)
      _mm_pause();
#endif
    }
#else
    if (remUs > 2000) {
      std::this_thread::sleep_for(std::chrono::microseconds(remUs - 1200));
    } else if (remUs > 200) {
      std::this_thread::yield();
    } else {
#if defined(__x86_64__) || defined(__i386__)
      _mm_pause();
#endif
    }
#endif
  }
}
}  // namespace

// ==========================================
// FrameTracker Implementation
// ==========================================

FrameTracker::FrameTracker() {
  m_frameStart = Clock::now();
  m_lastFrameEnd = m_frameStart;
}

void FrameTracker::MarkFrameStart() { m_frameStart = Clock::now(); }

void FrameTracker::MarkFrameEnd(float maxFps) {
  auto now = Clock::now();

  // 1. Active rendering + present duration for this frame (before cap sleep)
  int64_t activeUs = std::chrono::duration_cast<std::chrono::microseconds>(
                         now - m_frameStart)
                         .count();
  if (activeUs < 0) activeUs = 0;
  m_lastRenderTimeUs = std::max<int64_t>(1, activeUs);

  // 2. Enforce mathematically correct frame limiter and measure sleep time
  int64_t sleepUs = 0;
  auto postSleepNow = now;
  if (maxFps > 0.0f) {
    const auto targetInterval =
        std::chrono::microseconds(static_cast<int64_t>(1000000.0 / maxFps));
    if (activeUs < targetInterval.count()) {
      WaitUntilDeadline(m_frameStart + targetInterval);
      postSleepNow = Clock::now();
      sleepUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    postSleepNow - now)
                    .count();
      if (sleepUs < 0) sleepUs = 0;
    }
  }

  // 3. Calculate total wall-clock frame-to-frame interval
  int64_t intervalUs = std::chrono::duration_cast<std::chrono::microseconds>(
                           postSleepNow - m_lastFrameEnd)
                           .count();
  if (intervalUs <= 0) {
    intervalUs = std::max<int64_t>(1, activeUs + sleepUs);
  }
  m_lastFrameIntervalUs = intervalUs;
  m_lastFrameEnd = postSleepNow;

  // 4. Update smoothed stats with this interval
  UpdateStats(intervalUs);

  // 5. Snapshot per-frame counters, update Logger frame ID, write optional
  //    CSV trace row, and emit periodic [PERF] summary.
  TelemetryManager::GetInstance().OnFrameComplete(intervalUs, activeUs, sleepUs,
                                                  m_currentFps);
}

void FrameTracker::UpdateStats(int64_t intervalUs) {
  m_frameCount++;
  m_accumulatedTimeUs += intervalUs;

  // Update every 30 frames or 0.5 seconds (whichever comes first) so both
  // high-FPS and low-FPS workloads report responsive, non-flickering FPS.
  if (m_frameCount >= 30 || m_accumulatedTimeUs >= 500000) {
    double avgIntervalUs =
        static_cast<double>(m_accumulatedTimeUs) / m_frameCount;
    if (avgIntervalUs > 0.0) {
      m_currentFps = static_cast<float>(1000000.0 / avgIntervalUs);
      m_avgFrameTimeMs = static_cast<float>(avgIntervalUs / 1000.0);
    }

    m_frameCount = 0;
    m_accumulatedTimeUs = 0;
  }
}

// ==========================================
// TelemetryManager Implementation
// ==========================================

TelemetryManager::TelemetryManager()
    : m_perfStartTime(std::chrono::steady_clock::now()),
      m_lastSummaryTime(m_perfStartTime) {
  ResetTriangleStats();
  ResetFrameCounters();
}

TelemetryManager::~TelemetryManager() { ShutdownPerfLog(); }

TelemetryManager& TelemetryManager::GetInstance() {
  static TelemetryManager instance;
  return instance;
}

void TelemetryManager::ResetFrameCounters() {
  m_frameDrawCalls.store(0, std::memory_order_relaxed);
  m_frameStateFlushes.store(0, std::memory_order_relaxed);
  m_frameTrianglesProcessed.store(0, std::memory_order_relaxed);
  m_frameTrianglesDrawn.store(0, std::memory_order_relaxed);
  m_frameLfbReadLocks.store(0, std::memory_order_relaxed);
  m_frameLfbWriteLocks.store(0, std::memory_order_relaxed);
  m_frameLfbBytesRead.store(0, std::memory_order_relaxed);
  m_frameLfbBytesWritten.store(0, std::memory_order_relaxed);
  m_frameTexUploads.store(0, std::memory_order_relaxed);
  m_frameTexUploadBytes.store(0, std::memory_order_relaxed);
  m_framePsoBuilds.store(0, std::memory_order_relaxed);
  m_framePresentUs.store(0, std::memory_order_relaxed);
  for (auto& ns : m_frameCpuBucketNs) {
    ns.store(0, std::memory_order_relaxed);
  }
}

void TelemetryManager::ConfigurePerfLog(bool enabled,
                                        const std::string& csvPath) {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  if (m_csvFile.is_open()) {
    m_csvFile.flush();
    m_csvFile.close();
  }

  m_perfLogEnabled = enabled;
  m_perfTimingEnabled.store(enabled, std::memory_order_relaxed);
  s_perfTimingEnabled.store(enabled, std::memory_order_relaxed);
  m_csvRowsSinceFlush = 0;
  m_frameIndex.store(0, std::memory_order_relaxed);
  m_lastFrameMetrics = FrameMetrics{};
  m_perfStartTime = std::chrono::steady_clock::now();
  m_lastSummaryTime = m_perfStartTime;

  m_summaryFrames = 0;
  m_summaryFrameMs = 0.0;
  m_summaryRenderMs = 0.0;
  m_summaryPresentMs = 0.0;
  m_summarySleepMs = 0.0;
  m_summaryWrapperCpuMs = 0.0;
  m_summaryAppCpuMs = 0.0;
  m_summaryDrawCpuMs = 0.0;
  m_summaryTexCpuMs = 0.0;
  m_summaryLfbCpuMs = 0.0;
  m_summaryStateCpuMs = 0.0;
  m_summaryDrawCalls = 0;
  m_summaryStateFlushes = 0;
  m_summaryTrisDrawn = 0;
  m_summaryLfbReads = 0;
  m_summaryLfbWrites = 0;
  m_summaryLfbBytes = 0;
  m_summaryTexUploads = 0;
  m_summaryTexBytes = 0;
  m_summaryPsoBuilds = 0;

  ResetFrameCounters();
  Logger::GetInstance().SetCurrentFrame(0);

  if (enabled && !csvPath.empty()) {
    m_csvFile.open(csvPath, std::ios::out | std::ios::trunc);
    if (m_csvFile.is_open()) {
      m_csvFile << "frame,timestamp_ms,frame_ms,render_ms,present_ms,"
                   "cap_sleep_ms,fps,draw_calls,state_flushes,tris_processed,"
                   "tris_drawn,lfb_reads,lfb_writes,lfb_read_bytes,"
                   "lfb_write_bytes,tex_uploads,tex_upload_bytes,pso_builds,"
                   "wrapper_cpu_ms,app_cpu_ms,draw_cpu_ms,tex_cpu_ms,"
                   "lfb_cpu_ms,state_cpu_ms\n";
      m_csvFile.flush();
    } else {
      GLIDE_LOG(WARN, "Perf",
                "Failed to open perf CSV output file: " << csvPath);
    }
  }
}

void TelemetryManager::ShutdownPerfLog() {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  if (m_perfLogEnabled && m_summaryFrames > 0) {
    EmitPerfSummaryLocked();
  }
  if (m_csvFile.is_open()) {
    m_csvFile.flush();
    m_csvFile.close();
  }
  m_perfLogEnabled = false;
  m_perfTimingEnabled.store(false, std::memory_order_relaxed);
  s_perfTimingEnabled.store(false, std::memory_order_relaxed);
  m_csvRowsSinceFlush = 0;
}

void TelemetryManager::EmitPerfSummaryLocked() {
  if (m_summaryFrames == 0) return;
  const double invF = 1.0 / static_cast<double>(m_summaryFrames);
  const double avgFrameMs = m_summaryFrameMs * invF;
  const double avgRenderMs = m_summaryRenderMs * invF;
  const double avgPresentMs = m_summaryPresentMs * invF;
  const double avgSleepMs = m_summarySleepMs * invF;
  const double avgWrapMs = m_summaryWrapperCpuMs * invF;
  const double avgAppMs = m_summaryAppCpuMs * invF;
  const double avgDrawMs = m_summaryDrawCpuMs * invF;
  const double avgTexMs = m_summaryTexCpuMs * invF;
  const double avgLfbMs = m_summaryLfbCpuMs * invF;
  const double avgStateMs = m_summaryStateCpuMs * invF;
  const double avgFps = (avgFrameMs > 0.0) ? (1000.0 / avgFrameMs) : 0.0;
  const double avgDraws = static_cast<double>(m_summaryDrawCalls) * invF;
  const double avgFlushes = static_cast<double>(m_summaryStateFlushes) * invF;
  const double avgTris = static_cast<double>(m_summaryTrisDrawn) * invF;
  const double avgLfbR = static_cast<double>(m_summaryLfbReads) * invF;
  const double avgLfbW = static_cast<double>(m_summaryLfbWrites) * invF;
  const double avgLfbKb = (static_cast<double>(m_summaryLfbBytes) * invF) / 1024.0;
  const double avgTex = static_cast<double>(m_summaryTexUploads) * invF;
  const double avgTexKb = (static_cast<double>(m_summaryTexBytes) * invF) / 1024.0;

  char buf[512];
  std::snprintf(
      buf, sizeof(buf),
      "FPS=%.1f | frame=%.2fms (render=%.2fms [app=%.2f wrap=%.2f: "
      "draw=%.2f tex=%.2f lfb=%.2f state=%.2f] present=%.2fms cap=%.2fms) | "
      "draws=%.1f/f (flushes=%.1f/f) tris=%.0f/f | "
      "lfb=%.1fR/%.1fW (%.1fKB/f) | tex=%.1f/f (%.1fKB/f) | pso=+%llu",
      avgFps, avgFrameMs, avgRenderMs, avgAppMs, avgWrapMs, avgDrawMs, avgTexMs,
      avgLfbMs, avgStateMs, avgPresentMs, avgSleepMs, avgDraws, avgFlushes,
      avgTris, avgLfbR, avgLfbW, avgLfbKb, avgTex, avgTexKb,
      static_cast<unsigned long long>(m_summaryPsoBuilds));

  GLIDE_LOG_CAT(INFO, LOG_CAT_PERF, "Perf", buf);

  m_summaryFrames = 0;
  m_summaryFrameMs = 0.0;
  m_summaryRenderMs = 0.0;
  m_summaryPresentMs = 0.0;
  m_summarySleepMs = 0.0;
  m_summaryWrapperCpuMs = 0.0;
  m_summaryAppCpuMs = 0.0;
  m_summaryDrawCpuMs = 0.0;
  m_summaryTexCpuMs = 0.0;
  m_summaryLfbCpuMs = 0.0;
  m_summaryStateCpuMs = 0.0;
  m_summaryDrawCalls = 0;
  m_summaryStateFlushes = 0;
  m_summaryTrisDrawn = 0;
  m_summaryLfbReads = 0;
  m_summaryLfbWrites = 0;
  m_summaryLfbBytes = 0;
  m_summaryTexUploads = 0;
  m_summaryTexBytes = 0;
  m_summaryPsoBuilds = 0;
}

void TelemetryManager::OnFrameComplete(int64_t intervalUs, int64_t activeUs,
                                       int64_t sleepUs, float smoothedFps) {
  // Atomically harvest and reset per-frame counters
  const uint32_t drawCalls = m_frameDrawCalls.exchange(0, std::memory_order_relaxed);
  const uint32_t stateFlushes = m_frameStateFlushes.exchange(0, std::memory_order_relaxed);
  const uint32_t trisProc = m_frameTrianglesProcessed.exchange(0, std::memory_order_relaxed);
  const uint32_t trisDrawn = m_frameTrianglesDrawn.exchange(0, std::memory_order_relaxed);
  const uint32_t lfbReads = m_frameLfbReadLocks.exchange(0, std::memory_order_relaxed);
  const uint32_t lfbWrites = m_frameLfbWriteLocks.exchange(0, std::memory_order_relaxed);
  const uint64_t lfbReadBytes = m_frameLfbBytesRead.exchange(0, std::memory_order_relaxed);
  const uint64_t lfbWriteBytes = m_frameLfbBytesWritten.exchange(0, std::memory_order_relaxed);
  const uint32_t texUploads = m_frameTexUploads.exchange(0, std::memory_order_relaxed);
  const uint64_t texUploadBytes = m_frameTexUploadBytes.exchange(0, std::memory_order_relaxed);
  const uint32_t psoBuilds = m_framePsoBuilds.exchange(0, std::memory_order_relaxed);
  const int64_t presentUs = m_framePresentUs.exchange(0, std::memory_order_relaxed);
  const int64_t drawNs =
      m_frameCpuBucketNs[static_cast<size_t>(CpuBucket::Draw)].exchange(
          0, std::memory_order_relaxed);
  const int64_t texNs =
      m_frameCpuBucketNs[static_cast<size_t>(CpuBucket::Tex)].exchange(
          0, std::memory_order_relaxed);
  const int64_t lfbNs =
      m_frameCpuBucketNs[static_cast<size_t>(CpuBucket::Lfb)].exchange(
          0, std::memory_order_relaxed);
  const int64_t stateNs =
      m_frameCpuBucketNs[static_cast<size_t>(CpuBucket::State)].exchange(
          0, std::memory_order_relaxed);

  const uint64_t completedFrame = m_frameIndex.fetch_add(1, std::memory_order_relaxed);
  // Advance Logger frame ID for the upcoming frame
  Logger::GetInstance().SetCurrentFrame(completedFrame + 1);

  const int64_t renderUs = std::max<int64_t>(0, activeUs - presentUs);
  const double drawMs = static_cast<double>(drawNs) / 1e6;
  const double texMs = static_cast<double>(texNs) / 1e6;
  const double lfbMs = static_cast<double>(lfbNs) / 1e6;
  const double stateMs = static_cast<double>(stateNs) / 1e6;
  const double wrapperMs = drawMs + texMs + lfbMs + stateMs;
  const double renderMs = static_cast<double>(renderUs) / 1000.0;
  const double appMs = std::max(0.0, renderMs - wrapperMs);
  const auto now = std::chrono::steady_clock::now();

  FrameMetrics m{};
  m.frameIndex = completedFrame;
  m.timestampMs =
      std::chrono::duration<double, std::milli>(now - m_perfStartTime).count();
  m.frameTimeMs = static_cast<float>(intervalUs / 1000.0);
  m.renderTimeMs = static_cast<float>(renderMs);
  m.swapPresentMs = static_cast<float>(presentUs / 1000.0);
  m.fpsCapSleepMs = static_cast<float>(sleepUs / 1000.0);
  m.smoothedFps = (smoothedFps > 0.0f)
                      ? smoothedFps
                      : (m.frameTimeMs > 0.001f ? (1000.0f / m.frameTimeMs) : 0.0f);
  m.drawCalls = drawCalls;
  m.stateFlushes = stateFlushes;
  m.trianglesProcessed = trisProc;
  m.trianglesDrawn = trisDrawn;
  m.lfbReadLocks = lfbReads;
  m.lfbWriteLocks = lfbWrites;
  m.lfbBytesRead = lfbReadBytes;
  m.lfbBytesWritten = lfbWriteBytes;
  m.texUploads = texUploads;
  m.texUploadBytes = texUploadBytes;
  m.psoBuilds = psoBuilds;
  m.wrapperCpuMs = static_cast<float>(wrapperMs);
  m.appCpuMs = static_cast<float>(appMs);
  m.drawCpuMs = static_cast<float>(drawMs);
  m.texCpuMs = static_cast<float>(texMs);
  m.lfbCpuMs = static_cast<float>(lfbMs);
  m.stateCpuMs = static_cast<float>(stateMs);

  std::lock_guard<std::mutex> lock(m_perfMutex);
  m_lastFrameMetrics = m;

  if (!m_perfLogEnabled) return;

  if (m_csvFile.is_open()) {
    char row[512];
    const int len = std::snprintf(
        row, sizeof(row),
        "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%u,%u,%u,%u,%u,%u,%llu,%llu,%u,"
        "%llu,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
        static_cast<unsigned long long>(m.frameIndex), m.timestampMs,
        static_cast<double>(m.frameTimeMs), static_cast<double>(m.renderTimeMs),
        static_cast<double>(m.swapPresentMs), static_cast<double>(m.fpsCapSleepMs),
        static_cast<double>(m.smoothedFps), m.drawCalls, m.stateFlushes,
        m.trianglesProcessed, m.trianglesDrawn, m.lfbReadLocks, m.lfbWriteLocks,
        static_cast<unsigned long long>(m.lfbBytesRead),
        static_cast<unsigned long long>(m.lfbBytesWritten), m.texUploads,
        static_cast<unsigned long long>(m.texUploadBytes), m.psoBuilds,
        static_cast<double>(m.wrapperCpuMs), static_cast<double>(m.appCpuMs),
        static_cast<double>(m.drawCpuMs), static_cast<double>(m.texCpuMs),
        static_cast<double>(m.lfbCpuMs), static_cast<double>(m.stateCpuMs));
    if (len > 0) {
      m_csvFile.write(row, std::min<size_t>(static_cast<size_t>(len), sizeof(row) - 1));
    }
    if (++m_csvRowsSinceFlush >= 60) {
      m_csvFile.flush();
      m_csvRowsSinceFlush = 0;
    }
  }

  m_summaryFrames++;
  m_summaryFrameMs += m.frameTimeMs;
  m_summaryRenderMs += m.renderTimeMs;
  m_summaryPresentMs += m.swapPresentMs;
  m_summarySleepMs += m.fpsCapSleepMs;
  m_summaryWrapperCpuMs += m.wrapperCpuMs;
  m_summaryAppCpuMs += m.appCpuMs;
  m_summaryDrawCpuMs += m.drawCpuMs;
  m_summaryTexCpuMs += m.texCpuMs;
  m_summaryLfbCpuMs += m.lfbCpuMs;
  m_summaryStateCpuMs += m.stateCpuMs;
  m_summaryDrawCalls += m.drawCalls;
  m_summaryStateFlushes += m.stateFlushes;
  m_summaryTrisDrawn += m.trianglesDrawn;
  m_summaryLfbReads += m.lfbReadLocks;
  m_summaryLfbWrites += m.lfbWriteLocks;
  m_summaryLfbBytes += (m.lfbBytesRead + m.lfbBytesWritten);
  m_summaryTexUploads += m.texUploads;
  m_summaryTexBytes += m.texUploadBytes;
  m_summaryPsoBuilds += m.psoBuilds;

  if ((now - m_lastSummaryTime) >= std::chrono::seconds(1)) {
    EmitPerfSummaryLocked();
    if (m_csvFile.is_open() && m_csvRowsSinceFlush > 0) {
      m_csvFile.flush();
      m_csvRowsSinceFlush = 0;
    }
    m_lastSummaryTime = now;
  }
}

FrameMetrics TelemetryManager::GetLastFrameMetrics() const {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  return m_lastFrameMetrics;
}

void TelemetryManager::Reset() {
#ifdef ENABLE_TELEMETRY
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.clear();
  }
#endif
  ResetTriangleStats();
  ResetFrameCounters();
  m_frameTracker = FrameTracker();
}

#ifdef ENABLE_TELEMETRY
void TelemetryManager::RecordScope(const std::string& name,
                                   int64_t durationUs) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto& stats = m_stats[name];
  stats.callCount++;
  stats.totalDurationUs += durationUs;
  stats.minDurationUs = std::min(stats.minDurationUs, durationUs);
  stats.maxDurationUs = std::max(stats.maxDurationUs, durationUs);
}

void TelemetryManager::PrintReport() {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::stringstream reportStream;
  reportStream << "\n=========================================================="
                  "==========================\n"
               << "                       3DFX GLIDE-NG WRAPPER TELEMETRY "
                  "REPORT                       \n"
               << "============================================================"
                  "========================\n"
               << " Scope Name                     | Call Count | Total Time "
                  "(ms) | Avg (us) | Min/Max (us) \n"
               << "------------------------------------------------------------"
                  "------------------------\n";

  // Print scopes sorted alphabetically for clean reading
  std::vector<std::string> sortedNames;
  for (const auto& pair : m_stats) {
    sortedNames.push_back(pair.first);
  }
  std::sort(sortedNames.begin(), sortedNames.end());

  for (const auto& name : sortedNames) {
    const auto& stats = m_stats[name];
    double totalMs = stats.totalDurationUs / 1000.0;
    double avgUs =
        stats.callCount > 0
            ? (static_cast<double>(stats.totalDurationUs) / stats.callCount)
            : 0.0;

    reportStream << " " << std::left << std::setw(30) << name << " | "
                 << std::right << std::setw(10) << stats.callCount << " | "
                 << std::setw(15) << std::fixed << std::setprecision(2)
                 << totalMs << " | " << std::setw(8) << std::fixed
                 << std::setprecision(1) << avgUs << " | " << std::setw(6)
                 << stats.minDurationUs << "/" << std::left << std::setw(6)
                 << stats.maxDurationUs << "\n";
  }

  reportStream << "------------------------------------------------------------"
                  "------------------------\n"
               << " Global Emulated Geometry Statistics:\n"
               << "   Triangles Processed : " << GetTrianglesProcessed() << "\n"
               << "   Triangles Drawn     : " << GetTrianglesDrawn() << " ("
               << (GetTrianglesProcessed() > 0
                       ? (100.0 * GetTrianglesDrawn() / GetTrianglesProcessed())
                       : 0.0)
               << "% yield)\n"
               << "------------------------------------------------------------"
                  "------------------------\n"
               << " Presentation & Pacing Performance:\n"
               << "   Average Frame Rate  : " << std::fixed
               << std::setprecision(1) << m_frameTracker.GetCurrentFps()
               << " FPS\n"
               << "   Average Frame Time  : " << std::fixed
               << std::setprecision(2) << m_frameTracker.GetAvgFrameTimeMs()
               << " ms\n"
               << "   Active Render Time  : " << std::fixed
               << std::setprecision(2) << m_frameTracker.GetRenderTimeMs()
               << " ms\n"
               << "============================================================"
                  "========================";

  GLIDE_LOG(INFO, "Telemetry", reportStream.str());
}
#endif

}  // namespace GlideWrapper
