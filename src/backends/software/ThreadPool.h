#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

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

        const int chunkSize = (totalTasks >= 64) ? 4 : 1;
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

        // Main thread helps with the work
        while (true) {
            int startIdx = m_nextTaskIndex.fetch_add(chunkSize, std::memory_order_relaxed);
            if (startIdx >= totalTasks) {
                break;
            }
            const int endIdx = (startIdx + chunkSize < totalTasks) ? (startIdx + chunkSize) : totalTasks;
            for (int taskIdx = startIdx; taskIdx < endIdx; ++taskIdx) {
                taskFunc(taskIdx);
            }
        }

        // Wait for workers to finish via condition variable
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvEnd.wait(lock, [this]() {
            return m_completedThreads.load(std::memory_order_acquire) == m_threads.size() || m_shutdown.load(std::memory_order_relaxed);
        });
    }

    size_t GetThreadCount() const { return m_threads.size(); }

private:
    void WorkerLoop() {
        uint32_t localFrameId = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cvStart.wait(lock, [this, localFrameId]() {
                    return m_frameId.load(std::memory_order_acquire) > localFrameId || m_shutdown.load(std::memory_order_relaxed);
                });
            }

            if (m_shutdown.load(std::memory_order_relaxed)) return;

            const int totalTasks = m_totalTasks;
            const int chunkSize = m_chunkSize;
            const auto* func = m_taskFunc;
            localFrameId = m_frameId.load(std::memory_order_relaxed);

            // Lock-free chunked task processing loop
            while (true) {
                int startIdx = m_nextTaskIndex.fetch_add(chunkSize, std::memory_order_relaxed);
                if (startIdx >= totalTasks) {
                    break;
                }
                const int endIdx = (startIdx + chunkSize < totalTasks) ? (startIdx + chunkSize) : totalTasks;
                for (int taskIdx = startIdx; taskIdx < endIdx; ++taskIdx) {
                    (*func)(taskIdx);
                }
            }

            // Mark completion
            size_t completed = m_completedThreads.fetch_add(1, std::memory_order_acq_rel) + 1;
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
    alignas(64) std::atomic<int> m_nextTaskIndex{0};
    int m_totalTasks{0};
    int m_chunkSize{1};
    alignas(64) std::atomic<size_t> m_completedThreads{0};
    std::atomic<bool> m_shutdown{false};
    std::atomic<uint32_t> m_frameId{0};
    const std::function<void(int)>* m_taskFunc{nullptr};
};

} // namespace GlideWrapper
