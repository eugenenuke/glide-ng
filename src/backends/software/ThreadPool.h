#pragma once

#include <atomic>
#include <functional>
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
#else
#include <condition_variable>
#include <mutex>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace GlideWrapper {

class ThreadPool {
 public:
  explicit ThreadPool(size_t numThreads)
      : m_totalTasks(0), m_completedThreads(0), m_shutdown(false), m_frameId(0) {
#if defined(_WIN32)
    InitializeSRWLock(&m_srwLock);
    InitializeConditionVariable(&m_cvStart);
    InitializeConditionVariable(&m_cvEnd);
#endif
    for (size_t i = 0; i < numThreads; ++i) {
      m_threads.emplace_back([this]() { WorkerLoop(); });
    }
  }

  ~ThreadPool() {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&m_srwLock);
    m_shutdown.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&m_srwLock);
    WakeAllConditionVariable(&m_cvStart);
    WakeAllConditionVariable(&m_cvEnd);
#else
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_shutdown.store(true, std::memory_order_release);
    }
    m_cvStart.notify_all();
    m_cvEnd.notify_all();
#endif
    for (auto& t : m_threads) {
      if (t.joinable()) t.join();
    }
  }

  void ParallelFor(int totalTasks, const std::function<void(int)>& taskFunc) {
    if (totalTasks <= 0) return;
    if (m_threads.empty() || totalTasks == 1) {
      for (int i = 0; i < totalTasks; ++i) {
        taskFunc(i);
      }
      return;
    }

    const int chunkSize = (totalTasks >= 64) ? 4 : 1;
#if defined(_WIN32)
    AcquireSRWLockExclusive(&m_srwLock);
    m_taskFunc = &taskFunc;
    m_nextTaskIndex.store(0, std::memory_order_relaxed);
    m_totalTasks = totalTasks;
    m_chunkSize = chunkSize;
    m_completedThreads.store(0, std::memory_order_relaxed);
    m_frameId.fetch_add(1, std::memory_order_release);
    ReleaseSRWLockExclusive(&m_srwLock);
    WakeAllConditionVariable(&m_cvStart);
#else
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_taskFunc = &taskFunc;
      m_nextTaskIndex.store(0, std::memory_order_relaxed);
      m_totalTasks = totalTasks;
      m_chunkSize = chunkSize;
      m_completedThreads.store(0, std::memory_order_relaxed);
      m_frameId.fetch_add(1, std::memory_order_release);
    }
    m_cvStart.notify_all();
#endif

    // Main thread helps with the work
    while (true) {
      int startIdx =
          m_nextTaskIndex.fetch_add(chunkSize, std::memory_order_relaxed);
      if (startIdx >= totalTasks) {
        break;
      }
      const int endIdx =
          (startIdx + chunkSize < totalTasks) ? (startIdx + chunkSize) : totalTasks;
      for (int taskIdx = startIdx; taskIdx < endIdx; ++taskIdx) {
        taskFunc(taskIdx);
      }
    }

    // Brief spin before taking the lock/sleeping so sub-millisecond frames
    // complete without kernel/wineserver round-trips.
    const size_t targetWorkers = m_threads.size();
    for (int spin = 0; spin < 512; ++spin) {
      if (m_completedThreads.load(std::memory_order_acquire) == targetWorkers) {
        return;
      }
      CpuPause();
    }

#if defined(_WIN32)
    AcquireSRWLockExclusive(&m_srwLock);
    while (m_completedThreads.load(std::memory_order_acquire) != targetWorkers &&
           !m_shutdown.load(std::memory_order_relaxed)) {
      SleepConditionVariableSRW(&m_cvEnd, &m_srwLock, INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&m_srwLock);
#else
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cvEnd.wait(lock, [this, targetWorkers]() {
      return m_completedThreads.load(std::memory_order_acquire) == targetWorkers ||
             m_shutdown.load(std::memory_order_relaxed);
    });
#endif
  }

  size_t GetThreadCount() const { return m_threads.size(); }

 private:
  static inline void CpuPause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#endif
  }

  void WorkerLoop() {
    uint32_t localFrameId = 0;
    while (true) {
      // Short spin between rapid consecutive flushes within the same frame.
      bool wokeFast = false;
      for (int spin = 0; spin < 256; ++spin) {
        if (m_frameId.load(std::memory_order_acquire) != localFrameId ||
            m_shutdown.load(std::memory_order_relaxed)) {
          wokeFast = true;
          break;
        }
        CpuPause();
      }
      if (!wokeFast) {
#if defined(_WIN32)
        AcquireSRWLockExclusive(&m_srwLock);
        while (m_frameId.load(std::memory_order_acquire) == localFrameId &&
               !m_shutdown.load(std::memory_order_relaxed)) {
          SleepConditionVariableSRW(&m_cvStart, &m_srwLock, INFINITE, 0);
        }
        ReleaseSRWLockExclusive(&m_srwLock);
#else
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvStart.wait(lock, [this, localFrameId]() {
          return m_frameId.load(std::memory_order_acquire) != localFrameId ||
                 m_shutdown.load(std::memory_order_relaxed);
        });
#endif
      }

      if (m_shutdown.load(std::memory_order_acquire)) return;

      const int totalTasks = m_totalTasks;
      const int chunkSize = m_chunkSize;
      const auto* func = m_taskFunc;
      localFrameId = m_frameId.load(std::memory_order_relaxed);

      // Lock-free chunked task processing loop
      while (true) {
        int startIdx =
            m_nextTaskIndex.fetch_add(chunkSize, std::memory_order_relaxed);
        if (startIdx >= totalTasks) {
          break;
        }
        const int endIdx = (startIdx + chunkSize < totalTasks)
                               ? (startIdx + chunkSize)
                               : totalTasks;
        for (int taskIdx = startIdx; taskIdx < endIdx; ++taskIdx) {
          (*func)(taskIdx);
        }
      }

      // Mark completion
      size_t completed =
          m_completedThreads.fetch_add(1, std::memory_order_acq_rel) + 1;
      if (completed == m_threads.size()) {
#if defined(_WIN32)
        AcquireSRWLockExclusive(&m_srwLock);
        ReleaseSRWLockExclusive(&m_srwLock);
        WakeAllConditionVariable(&m_cvEnd);
#else
        {
          std::unique_lock<std::mutex> lock(m_mutex);
        }
        m_cvEnd.notify_all();
#endif
      }
    }
  }

  std::vector<std::thread> m_threads;
#if defined(_WIN32)
  SRWLOCK m_srwLock{};
  CONDITION_VARIABLE m_cvStart{};
  CONDITION_VARIABLE m_cvEnd{};
#else
  std::mutex m_mutex;
  std::condition_variable m_cvStart;
  std::condition_variable m_cvEnd;
#endif
  alignas(64) std::atomic<int> m_nextTaskIndex{0};
  int m_totalTasks{0};
  int m_chunkSize{1};
  alignas(64) std::atomic<size_t> m_completedThreads{0};
  std::atomic<bool> m_shutdown{false};
  std::atomic<uint32_t> m_frameId{0};
  const std::function<void(int)>* m_taskFunc{nullptr};
};

}  // namespace GlideWrapper
