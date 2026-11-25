#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace GlideWrapper {

// Lightweight lazy-spawned trailing-edge timer mirroring authentic 3dfx RAMDAC
// front-buffer scanout. When a Glide application modifies GR_BUFFER_FRONTBUFFER
// (buffer == 0) via LFB writes or 3D draws without calling grBufferSwap(), a
// quick burst of glyph/UI writes can finish inside the 33 ms inline throttle
// window and then sit idle waiting for keyboard input. Arming this timer
// coalesces the burst and issues one present callback after the deadline, then
// sleeps with zero CPU overhead until the next front-buffer mutation.
//
// Zero threads are spawned for headless contexts or double-buffered titles that
// never call Arm(). Never holds its internal mutex while invoking the callback,
// so lock-order inversion with a backend's primary mutex cannot occur.
class FrontBufferFlushTimer {
 public:
  using Clock = std::chrono::steady_clock;
  using Callback = std::function<void()>;

  explicit FrontBufferFlushTimer(Callback onDue)
      : m_onDue(std::move(onDue)) {}

  ~FrontBufferFlushTimer() { Stop(); }

  FrontBufferFlushTimer(const FrontBufferFlushTimer&) = delete;
  FrontBufferFlushTimer& operator=(const FrontBufferFlushTimer&) = delete;

  // Arms a pending flush for `deadline`. Cheaply exits in a single atomic load
  // if already armed so tight draw/LFB loops incur zero mutex or syscall cost.
  void Arm(Clock::time_point deadline) {
    if (m_armed.load(std::memory_order_acquire)) {
      return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping) {
      return;
    }
    if (m_armed.load(std::memory_order_relaxed)) {
      return;
    }
    m_deadline = deadline;
    m_pending = true;
    m_armed.store(true, std::memory_order_release);
    if (!m_thread.joinable()) {
      m_thread = std::thread([this]() { WorkerLoop(); });
      m_started.store(true, std::memory_order_release);
    }
    m_cv.notify_one();
  }

  // Cancels any pending wake-up after an inline present or SwapBuffers().
  void Clear() {
    m_armed.store(false, std::memory_order_release);
  }

  // Allows re-use after Stop() (e.g., when a window is re-attached).
  void ResetForAttach() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopping = false;
    m_pending = false;
    m_armed.store(false, std::memory_order_release);
  }

  bool HasWorker() const {
    return m_started.load(std::memory_order_acquire);
  }

  // Stops and joins the worker thread (if ever spawned). Callers MUST invoke
  // Stop() WITHOUT holding any mutex that the callback itself acquires.
  void Stop() {
    if (!m_started.load(std::memory_order_acquire)) {
      m_armed.store(false, std::memory_order_release);
      return;
    }
    std::thread toJoin;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopping = true;
      m_pending = false;
      m_armed.store(false, std::memory_order_release);
      toJoin = std::move(m_thread);
      m_started.store(false, std::memory_order_release);
    }
    m_cv.notify_all();
    if (!toJoin.joinable()) {
      return;
    }
    if (toJoin.get_id() == std::this_thread::get_id()) {
      toJoin.detach();
      return;
    }
#if defined(_WIN32)
    // Match ThreadPool: during Win32 ExitProcess / DLL unload under the loader
    // lock, worker threads may already be terminated by the kernel.
    using RtlShutdownFn = unsigned char(__stdcall*)();
    bool processTerminating = false;
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
      const FARPROC proc = GetProcAddress(ntdll, "RtlDllShutdownInProgress");
      if (proc) {
        RtlShutdownFn fn = nullptr;
        std::memcpy(&fn, &proc, sizeof(fn));
        if (fn() != 0) {
          processTerminating = true;
        }
      }
    }
    if (processTerminating) {
      toJoin.detach();
      return;
    }
#endif
    toJoin.join();
  }

 private:
  void WorkerLoop() {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_stopping || m_pending; });
        if (m_stopping) {
          return;
        }
        while (m_pending && !m_stopping) {
          if (!m_armed.load(std::memory_order_acquire)) {
            m_pending = false;
            break;
          }
          const Clock::time_point target = m_deadline;
          if (Clock::now() >= target) {
            break;
          }
          m_cv.wait_until(lock, target);
        }
        if (m_stopping) {
          return;
        }
        const bool shouldFire =
            m_pending && m_armed.exchange(false, std::memory_order_acq_rel);
        m_pending = false;
        if (!shouldFire) {
          continue;
        }
      }
      if (m_onDue) {
        m_onDue();
      }
    }
  }

  Callback m_onDue;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::thread m_thread;
  Clock::time_point m_deadline{};
  bool m_pending{false};
  bool m_stopping{false};
  std::atomic<bool> m_armed{false};
  std::atomic<bool> m_started{false};
};

// Shared state machine governing RAMDAC-style front-buffer presentation pacing
// across Vulkan, OpenGL ES, and Software backends. Coalesces multi-write
// bursts within kBurstCoalesceDelay (16 ms) without mid-frame stalls, while
// capping continuous un-swapped streaming loops to ~30 Hz (33 ms).
struct FrontBufferPacer {
  using Clock = FrontBufferFlushTimer::Clock;
  static constexpr auto kFrontPresentInterval = std::chrono::milliseconds(33);
  static constexpr auto kBurstCoalesceDelay = std::chrono::milliseconds(16);

  struct Decision {
    bool presentNow{false};
    bool armTimer{false};
    Clock::time_point deadline{};
  };

  Decision OnMutation(Clock::time_point now, bool forceImmediate,
                      bool canUseAsyncThread) {
    if (forceImmediate) {
      return Decision{true, false, {}};
    }
    const bool hasPrev = (m_lastPresentTime.time_since_epoch().count() != 0);
    const bool intervalElapsed =
        !hasPrev || (now - m_lastPresentTime) >= kFrontPresentInterval;

    if (!m_needsPresent) {
      if (!canUseAsyncThread && intervalElapsed) {
        return Decision{true, false, {}};
      }
      m_needsPresent = true;
      m_burstStart = now;
      if (!hasPrev) {
        m_lastPresentTime = now;
      }
      if (!canUseAsyncThread) {
        return Decision{false, false, {}};
      }
      const auto deadline =
          (hasPrev && (now - m_lastPresentTime) < kFrontPresentInterval)
              ? (m_lastPresentTime + kFrontPresentInterval)
              : (now + kBurstCoalesceDelay);
      return Decision{false, true, deadline};
    }

    // Already in an active burst: only trigger synchronous inline streaming
    // presentation once the un-swapped burst itself spans >= 33 ms (never on
    // back-to-back writes within the same frame's burst).
    const bool burstStreaming = (now - m_burstStart) >= kFrontPresentInterval;
    if (burstStreaming && intervalElapsed) {
      return Decision{true, false, {}};
    }
    if (!canUseAsyncThread) {
      return Decision{false, false, {}};
    }
    const auto deadline =
        (hasPrev && (now - m_lastPresentTime) < kFrontPresentInterval)
            ? (m_lastPresentTime + kFrontPresentInterval)
            : (m_burstStart + kBurstCoalesceDelay);
    return Decision{false, true, deadline};
  }

  void ClearPending() {
    m_needsPresent = false;
    m_burstStart = {};
  }

  void OnPresented(Clock::time_point now) {
    m_needsPresent = false;
    m_burstStart = {};
    m_lastPresentTime = now;
  }

  void OnSwapBuffers(Clock::time_point now) {
    m_needsPresent = false;
    m_burstStart = {};
    m_lastPresentTime = now;
  }

  void Reset() {
    m_needsPresent = false;
    m_burstStart = {};
    m_lastPresentTime = {};
  }

  bool NeedsPresent() const { return m_needsPresent; }

 private:
  bool m_needsPresent{false};
  Clock::time_point m_burstStart{};
  Clock::time_point m_lastPresentTime{};
};

}  // namespace GlideWrapper
