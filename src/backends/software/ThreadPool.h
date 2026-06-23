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
    ThreadPool(size_t numThreads, std::function<void(int)> workerFunc)
        : m_workerFunc(workerFunc), m_shutdown(false), m_frameId(0) {
        for (size_t i = 0; i < numThreads; ++i) {
            m_threads.emplace_back([this]() { ThreadLoop(); });
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

    void StartFrame(int totalTiles) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_nextTile = 0;
            m_totalTiles = totalTiles;
            m_completedThreads = 0;
            m_frameId++;
        }
        m_cvStart.notify_all();
    }

    void Wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        uint32_t activeFrame = m_frameId;
        m_cvEnd.wait(lock, [this, activeFrame]() {
            return m_completedThreads == m_threads.size() || m_shutdown;
        });
    }

private:
    void ThreadLoop() {
        uint32_t localFrameId = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cvStart.wait(lock, [this, localFrameId]() {
                    return m_frameId > localFrameId || m_shutdown;
                });
                if (m_shutdown) return;
            }

            // Lock-free tile processing loop
            while (true) {
                int tileIdx = m_nextTile.fetch_add(1);
                if (tileIdx >= m_totalTiles) {
                    break;
                }
                m_workerFunc(tileIdx);
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
    std::function<void(int)> m_workerFunc;
    std::mutex m_mutex;
    std::condition_variable m_cvStart;
    std::condition_variable m_cvEnd;
    std::atomic<int> m_nextTile{0};
    int m_totalTiles{0};
    size_t m_completedThreads{0};
    bool m_shutdown{false};
    uint32_t m_frameId{0};
};

} // namespace GlideWrapper
