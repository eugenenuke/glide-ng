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
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_shutdown = true;
        }
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
            m_completedThreads = 0;
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

        // Wait for workers to finish
        std::unique_lock<std::mutex> lock(m_mutex);
        uint32_t activeFrame = m_frameId;
        m_cvEnd.wait(lock, [this, activeFrame]() {
            return m_completedThreads == m_threads.size() || m_shutdown;
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
                    return m_frameId > localFrameId || m_shutdown;
                });
                if (m_shutdown) return;
            }

            // Lock-free task processing loop
            while (true) {
                int taskIdx = m_nextTaskIndex.fetch_add(1);
                if (taskIdx >= m_totalTasks) {
                    break;
                }
                (*m_taskFunc)(taskIdx);
            }

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_completedThreads++;
                localFrameId = m_frameId; // Mark this frame as done for this thread
                if (m_completedThreads == m_threads.size()) {
                    m_cvEnd.notify_all();
                }
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cvStart;
    std::condition_variable m_cvEnd;
    std::atomic<int> m_nextTaskIndex{0};
    int m_totalTasks{0};
    size_t m_completedThreads{0};
    bool m_shutdown{false};
    uint32_t m_frameId{0};
    const std::function<void(int)>* m_taskFunc{nullptr};
};

} // namespace GlideWrapper
