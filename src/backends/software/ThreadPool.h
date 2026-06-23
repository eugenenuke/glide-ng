#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

#if defined(__x86_64__) || defined(_M_X64)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define GLIDE_SPIN_PAUSE() _mm_pause()
    #else
        #define GLIDE_SPIN_PAUSE() __builtin_ia32_pause()
    #endif
#else
    #define GLIDE_SPIN_PAUSE() std::this_thread::yield()
#endif

namespace GlideWrapper {

class ThreadPool {
public:
    ThreadPool(size_t numThreads)
        : m_totalTasks(0), m_completedThreads(0), m_shutdown(false), m_frameId(0) {
        for (size_t i = 0; i < numThreads; ++i) {
            m_threads.emplace_back([this]() { WorkerLoop(); });
        }
    }

    ~ThreadPool() {
        m_shutdown.store(true);
        m_cvStart.notify_all();
        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
    }

    void ParallelFor(int totalTasks, const std::function<void(int)>& taskFunc) {
        if (totalTasks <= 0) return;
        if (m_threads.empty()) {
            for (int i = 0; i < totalTasks; ++i) {
                taskFunc(i);
            }
            return;
        }

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_taskFunc = &taskFunc;
            m_nextTaskIndex.store(0);
            m_totalTasks = totalTasks;
            m_completedThreads.store(0);
            m_frameId++;
        }
        m_cvStart.notify_all();

        // Main thread helps with the work
        while (true) {
            int taskIdx = m_nextTaskIndex.fetch_add(1);
            if (taskIdx >= totalTasks) {
                break;
            }
            taskFunc(taskIdx);
        }

        // Spin-wait on completion before falling back to condition variable
        bool allDone = false;
        for (int spin = 0; spin < 40000; ++spin) {
            if (m_completedThreads.load(std::memory_order_relaxed) == m_threads.size() || 
                m_shutdown.load(std::memory_order_relaxed)) {
                allDone = true;
                break;
            }
            GLIDE_SPIN_PAUSE();
        }

        if (!allDone) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cvEnd.wait(lock, [this]() {
                return m_completedThreads.load() == m_threads.size() || m_shutdown.load();
            });
        }
    }

    size_t GetThreadCount() const { return m_threads.size(); }

private:
    void WorkerLoop() {
        uint32_t localFrameId = 0;
        while (true) {
            // Spin-wait to avoid kernel futex latency
            bool gotWork = false;
            for (int spin = 0; spin < 40000; ++spin) {
                if (m_frameId.load(std::memory_order_relaxed) > localFrameId || 
                    m_shutdown.load(std::memory_order_relaxed)) {
                    gotWork = true;
                    break;
                }
                GLIDE_SPIN_PAUSE();
            }

            // Fall back to condition variable if spin-wait timed out
            if (!gotWork) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cvStart.wait(lock, [this, localFrameId]() {
                    return m_frameId.load() > localFrameId || m_shutdown.load();
                });
            }

            if (m_shutdown.load()) return;

            // Lock-free task processing loop
            while (true) {
                int taskIdx = m_nextTaskIndex.fetch_add(1);
                if (taskIdx >= m_totalTasks) {
                    break;
                }
                (*m_taskFunc)(taskIdx);
            }

            // Mark completion
            size_t completed = m_completedThreads.fetch_add(1) + 1;
            localFrameId = m_frameId.load(std::memory_order_relaxed);
            
            if (completed == m_threads.size()) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cvEnd.notify_all();
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cvStart;
    std::condition_variable m_cvEnd;
    std::atomic<int> m_nextTaskIndex{0};
    int m_totalTasks{0};
    std::atomic<size_t> m_completedThreads{0};
    std::atomic<bool> m_shutdown{false};
    std::atomic<uint32_t> m_frameId{0};
    const std::function<void(int)>* m_taskFunc{nullptr};
};

} // namespace GlideWrapper
