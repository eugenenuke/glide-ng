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
        : m_shutdown(false), m_workAvailable(false), m_activeWorkers(0) {
        m_threads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            m_threads.emplace_back([this]() { WorkerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shutdown = true;
            m_workAvailable = true;
        }
        m_cv.notify_all();
        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
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

        m_taskFunc = taskFunc;
        m_totalTasks = totalTasks;
        m_nextTaskIndex.store(0);
        m_activeWorkers.store(0);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_workAvailable = true;
        }
        m_cv.notify_all();

        while (true) {
            int taskIdx = m_nextTaskIndex.fetch_add(1);
            if (taskIdx >= totalTasks) {
                break;
            }
            m_taskFunc(taskIdx);
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        m_mainCv.wait(lock, [this]() {
            return m_activeWorkers.load(std::memory_order_relaxed) == 0 && 
                   m_nextTaskIndex.load(std::memory_order_relaxed) >= m_totalTasks;
        });
        m_workAvailable = false;
        m_cv.notify_all();
    }

    size_t GetThreadCount() const { return m_threads.size(); }

private:
    void WorkerLoop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_shutdown || m_workAvailable; });
                if (m_shutdown) {
                    return;
                }
                m_activeWorkers.fetch_add(1);
            }

            while (true) {
                int taskIdx = m_nextTaskIndex.fetch_add(1);
                if (taskIdx >= m_totalTasks) {
                    break;
                }
                m_taskFunc(taskIdx);
            }

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                int active = m_activeWorkers.fetch_sub(1) - 1;
                if (active == 0 && m_nextTaskIndex.load(std::memory_order_relaxed) >= m_totalTasks) {
                    m_mainCv.notify_one();
                }
                
                m_cv.wait(lock, [this]() { return m_shutdown || !m_workAvailable; });
                if (m_shutdown) {
                    return;
                }
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_mainCv;
    
    std::atomic<bool> m_shutdown;
    std::atomic<bool> m_workAvailable;
    std::atomic<int> m_nextTaskIndex{0};
    std::atomic<int> m_activeWorkers{0};
    int m_totalTasks{0};
    std::function<void(int)> m_taskFunc;
};

} // namespace GlideWrapper
