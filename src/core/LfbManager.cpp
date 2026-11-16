#include "core/LfbManager.h"
#include "core/BackendManager.h"
#include "core/Logger.h"
#include "core/Telemetry.h"
#include <algorithm>

namespace GlideWrapper {

void* LfbManager::GetActiveBackend() {
    return BackendManager::GetInstance().GetBackend();
}

uint32_t LfbManager::GetBackendWidth() {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
        return b->GetWidth();
    }
    return 640;
}

uint32_t LfbManager::GetBackendHeight() {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
        return b->GetHeight();
    }
    return 480;
}

void LfbManager::ReadLFBData(uint32_t buffer, uint32_t width, uint32_t height, uint32_t stride, uint8_t* dest) {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
        b->ReadLFB(buffer, 0, 0, width, height, stride, dest);
    }
}

void LfbManager::FlipRows(uint8_t* data, uint32_t height, uint32_t stride) {
    std::vector<uint8_t> tempRow(stride);
    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* rowTop = data + static_cast<size_t>(y) * stride;
        uint8_t* rowBottom = data + static_cast<size_t>(height - 1 - y) * stride;
        std::memcpy(tempRow.data(), rowTop, stride);
        std::memcpy(rowTop, rowBottom, stride);
        std::memcpy(rowBottom, tempRow.data(), stride);
    }
}

void LfbManager::FillSentinel(uint8_t* dst, uint32_t pixelCount, uint32_t pixelSize) {
    if (!dst || pixelCount == 0) return;
    if (pixelSize == 2) {
        // RGB565 0xF81E (R=31, G=0, B=30): distinct from black (0x0000), white
        // (0xFFFF), and common magenta chroma key (0xF81F).
        constexpr uint16_t kSentinel16 = 0xF81Eu;
        auto* p16 = reinterpret_cast<uint16_t*>(dst);
        std::fill_n(p16, pixelCount, kSentinel16);
    } else if (pixelSize == 4) {
        constexpr uint32_t kSentinel32 = 0x01F800F7u;
        auto* p32 = reinterpret_cast<uint32_t*>(dst);
        std::fill_n(p32, pixelCount, kSentinel32);
    } else {
        std::memset(dst, 0xA5, static_cast<size_t>(pixelCount) * pixelSize);
    }
}

void LfbManager::LogErrorUnsupportedWriteMode(uint32_t writeMode) {
    GLIDE_LOG(CRITICAL, "LFB", "LfbManager: Unsupported writeMode=" << writeMode);
}

void LfbManager::LogLockSuccess(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, size_t byteSize) {
    if (!IsWriteLockType(type)) {
        TelemetryManager::GetInstance().RecordLfbRead(byteSize);
    }
    GLIDE_LOG(DEBUG, "LFB", "LfbManager: locked type=" << type
              << ", buffer=" << buffer << ", mode=" << writeMode
              << ", origin=" << origin << ", bytes=" << byteSize);
}

bool LfbManager::SubmitWriteLock_Locked(const LfbPipelineConfig& config, bool keepLocked) {
    if (!m_writeLock.isLocked) return false;
    auto* b = BackendManager::GetInstance().GetBackend();
    if (!b) return false;

    const size_t byteSize = static_cast<size_t>(m_writeLock.height) * m_writeLock.stride;
    if (byteSize == 0 || m_writeLock.stagingBuffer.size() < byteSize ||
        m_writeLock.originalBuffer.size() < byteSize) {
        return false;
    }

    // Fast path: if the guest wrote nothing since the last flush/lock, skip.
    if (std::memcmp(m_writeLock.stagingBuffer.data(),
                    m_writeLock.originalBuffer.data(), byteSize) == 0) {
        return true;
    }

    uint32_t srcFmt = 0;
    switch (m_writeLock.writeMode) {
        case 0x0: srcFmt = 0x0; break; // GR_LFBWRITEMODE_565
        case 0x1: srcFmt = 0x1; break; // GR_LFBWRITEMODE_555
        case 0x2: srcFmt = 0x2; break; // GR_LFBWRITEMODE_1555
        case 0x4: srcFmt = 0x4; break; // GR_LFBWRITEMODE_888
        case 0x5: srcFmt = 0x5; break; // GR_LFBWRITEMODE_8888
        case 0xc: srcFmt = 0xc; break; // GR_LFBWRITEMODE_565_DEPTH
        case 0xd: srcFmt = 0xd; break; // GR_LFBWRITEMODE_555_DEPTH
        case 0xe: srcFmt = 0xe; break; // GR_LFBWRITEMODE_1555_DEPTH
        case 0xf: srcFmt = 0xf; break; // GR_LFBWRITEMODE_ZA16
        default:
            LogErrorUnsupportedWriteMode(m_writeLock.writeMode);
            return false;
    }

    const void* submitPtr = m_writeLock.stagingBuffer.data();
    if (m_writeLock.origin == 1) { // GR_ORIGIN_LOWER_LEFT
        if (keepLocked) {
            // Preserve stagingBuffer layout/address for persistent locks.
            m_writeLock.flipScratch.resize(byteSize);
            for (uint32_t y = 0; y < m_writeLock.height; ++y) {
                const uint8_t* srcRow = m_writeLock.stagingBuffer.data() +
                                        static_cast<size_t>(m_writeLock.height - 1 - y) * m_writeLock.stride;
                uint8_t* dstRow = m_writeLock.flipScratch.data() +
                                  static_cast<size_t>(y) * m_writeLock.stride;
                std::memcpy(dstRow, srcRow, m_writeLock.stride);
            }
            submitPtr = m_writeLock.flipScratch.data();
        } else {
            FlipRows(m_writeLock.stagingBuffer.data(), m_writeLock.height, m_writeLock.stride);
        }
    }

    TelemetryManager::GetInstance().RecordLfbWrite(byteSize);
    b->WriteLFB(m_writeLock.buffer, 0, 0, m_writeLock.width, m_writeLock.height,
                m_writeLock.stride, srcFmt, submitPtr,
                m_writeLock.originalBuffer.data(), config);

    if (keepLocked) {
        // Reset stagingBuffer back to sentinel in-place (pointer never moves!)
        // so next frame detects newly written pixels cleanly.
        std::memcpy(m_writeLock.stagingBuffer.data(),
                    m_writeLock.originalBuffer.data(), byteSize);
    }
    return true;
}

bool LfbManager::Unlock(uint32_t type, uint32_t buffer, const LfbPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (IsWriteLockType(type)) {
        if (!m_writeLock.isLocked) {
            GLIDE_LOG(WARN, "LFB", "LfbManager::Unlock: No active LFB write lock found!");
            return false;
        }
        const bool ok = SubmitWriteLock_Locked(config, /*keepLocked=*/false);
        m_writeLock.isLocked = false;
        m_writeLocked.store(false, std::memory_order_release);
        GLIDE_LOG(DEBUG, "LFB", "LfbManager: unlocked write buffer=" << buffer << " (type=" << type << ") ok=" << ok);
        return ok;
    }

    if (!m_readLock.isLocked) {
        // Fallback: if caller mismatched unlock type while a write lock is active,
        // avoid leaving a dangling lock.
        if (m_writeLock.isLocked && m_writeLock.buffer == buffer) {
            m_writeLock.isLocked = false;
            m_writeLocked.store(false, std::memory_order_release);
            return true;
        }
        GLIDE_LOG(WARN, "LFB", "LfbManager::Unlock: No active LFB read lock found!");
        return false;
    }

    m_readLock.isLocked = false;
    GLIDE_LOG(DEBUG, "LFB", "LfbManager: unlocked read buffer=" << buffer << " (type=" << type << ") successfully.");
    return true;
}

bool LfbManager::FlushActiveWriteLock(const LfbPipelineConfig& config) {
    if (!m_writeLocked.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_writeLock.isLocked) {
        m_writeLocked.store(false, std::memory_order_release);
        return false;
    }
    return SubmitWriteLock_Locked(config, /*keepLocked=*/true);
}

void LfbManager::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writeLock.isLocked = false;
    m_readLock.isLocked = false;
    m_writeLocked.store(false, std::memory_order_release);
}

} // namespace GlideWrapper
