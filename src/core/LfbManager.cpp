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
        return b->GetGuestWidth();
    }
    return 640;
}

uint32_t LfbManager::GetBackendHeight() {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
        return b->GetGuestHeight();
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

void LfbManager::RefreshWriteLockedFlag_Locked() {
    const bool anyLocked = m_writeLocks[0].isLocked ||
                           m_writeLocks[1].isLocked ||
                           m_writeLocks[2].isLocked;
    m_writeLocked.store(anyLocked, std::memory_order_release);
}

bool LfbManager::SubmitWriteLock_Locked(WriteLockState& slot, const LfbPipelineConfig& config, bool keepLocked) {
    if (!slot.isLocked) return false;
    auto* b = BackendManager::GetInstance().GetBackend();
    if (!b) return false;

    const uint32_t height = slot.height;
    const uint32_t stride = slot.stride;
    const size_t byteSize = static_cast<size_t>(height) * stride;
    if (byteSize == 0 || slot.stagingBuffer.size() < byteSize ||
        slot.originalBuffer.size() < byteSize) {
        return false;
    }

    uint32_t srcFmt = 0;
    switch (slot.writeMode) {
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
            LogErrorUnsupportedWriteMode(slot.writeMode);
            return false;
    }

    uint8_t* stagingData = slot.stagingBuffer.data();
    const uint8_t* origData = slot.originalBuffer.data();

    // Scan rows to find disjoint contiguous dirty bands so sparse HUD writes
    // (e.g. top + bottom status bars) skip all untouched middle rows on CPU and GPU.
    struct RowBand {
        uint32_t startY;
        uint32_t endY;
    };
    std::vector<RowBand> dirtyBands;
    dirtyBands.reserve(8);
    uint32_t y = 0;
    while (y < height) {
        const size_t rowOff = static_cast<size_t>(y) * stride;
        if (std::memcmp(stagingData + rowOff, origData + rowOff, stride) != 0) {
            const uint32_t bandStart = y;
            uint32_t bandEnd = y;
            ++y;
            while (y < height) {
                const size_t nextOff = static_cast<size_t>(y) * stride;
                if (std::memcmp(stagingData + nextOff, origData + nextOff, stride) != 0) {
                    bandEnd = y;
                    ++y;
                } else {
                    break;
                }
            }
            dirtyBands.push_back({bandStart, bandEnd});
        } else {
            ++y;
        }
    }

    if (dirtyBands.empty()) {
        if (!keepLocked) {
            slot.stagingClean = true;
        }
        return true;
    }

    if (slot.origin == 0) { // GR_ORIGIN_UPPER_LEFT
        for (const auto& band : dirtyBands) {
            const uint32_t bandRows = band.endY - band.startY + 1;
            const size_t bandOffset = static_cast<size_t>(band.startY) * stride;
            const size_t bandBytes = static_cast<size_t>(bandRows) * stride;
            TelemetryManager::GetInstance().RecordLfbWrite(bandBytes);
            b->WriteLFB(slot.buffer, 0, band.startY, slot.width,
                        bandRows, stride, srcFmt, stagingData + bandOffset,
                        origData + bandOffset, config);
            // Reset only the modified band back to sentinel (pointer never moves!).
            std::memcpy(stagingData + bandOffset, origData + bandOffset, bandBytes);
        }
    } else { // GR_ORIGIN_LOWER_LEFT: map each guest band [startY..endY] to top-down dstY
        for (const auto& band : dirtyBands) {
            const uint32_t bandRows = band.endY - band.startY + 1;
            const size_t bandBytes = static_cast<size_t>(bandRows) * stride;
            if (slot.flipScratch.size() < bandBytes) {
                slot.flipScratch.resize(bandBytes);
            }
            for (uint32_t r = 0; r < bandRows; ++r) {
                const uint32_t guestRow = band.endY - r;
                const uint8_t* srcRow = stagingData + static_cast<size_t>(guestRow) * stride;
                uint8_t* dstRow = slot.flipScratch.data() + static_cast<size_t>(r) * stride;
                std::memcpy(dstRow, srcRow, stride);
            }
            const uint32_t dstY = height - 1 - band.endY;
            TelemetryManager::GetInstance().RecordLfbWrite(bandBytes);
            // originalBuffer is uniform sentinel across all rows, so offset 0 matches any band.
            b->WriteLFB(slot.buffer, 0, dstY, slot.width,
                        bandRows, stride, srcFmt, slot.flipScratch.data(),
                        origData, config);
            const size_t bandOffset = static_cast<size_t>(band.startY) * stride;
            std::memcpy(stagingData + bandOffset, origData + bandOffset, bandBytes);
        }
    }

    if (!keepLocked) {
        // Every modified row has been restored to sentinel; next Lock() of the
        // same resolution/mode can skip the full-buffer sentinel memcpy.
        slot.stagingClean = true;
    }
    return true;
}

bool LfbManager::Unlock(uint32_t type, uint32_t buffer, const LfbPipelineConfig& config) {
    GLIDE_CPU_SCOPE(CpuBucket::Lfb);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (IsWriteLockType(type)) {
        const uint32_t slotIdx = WriteSlotForBuffer(buffer);
        WriteLockState& slot = m_writeLocks[slotIdx];
        if (!slot.isLocked) {
            // Backward-compatible fallback: if a caller unlocked an alias buffer
            // when exactly one write slot is active, release that slot cleanly.
            WriteLockState* singleActive = nullptr;
            uint32_t activeCount = 0;
            for (auto& s : m_writeLocks) {
                if (s.isLocked) {
                    singleActive = &s;
                    ++activeCount;
                }
            }
            if (activeCount == 1 && singleActive != nullptr && buffer < 2u &&
                singleActive->buffer < 2u) {
                const bool ok = SubmitWriteLock_Locked(*singleActive, config, /*keepLocked=*/false);
                singleActive->isLocked = false;
                singleActive->lockCount = 0;
                RefreshWriteLockedFlag_Locked();
                return ok;
            }
            GLIDE_LOG(WARN, "LFB", "LfbManager::Unlock: No active LFB write lock found!");
            return false;
        }
        if (slot.lockCount > 1u) {
            --slot.lockCount;
            // Flush modified bands immediately so interleaved graphics / locks see
            // inner writes in order, while keeping the outer lock pointer live.
            const bool ok = SubmitWriteLock_Locked(slot, config, /*keepLocked=*/true);
            GLIDE_LOG(DEBUG, "LFB", "LfbManager: unlocked nested write buffer=" << buffer
                      << " (remaining depth=" << slot.lockCount << ") ok=" << ok);
            return ok;
        }
        const bool ok = SubmitWriteLock_Locked(slot, config, /*keepLocked=*/false);
        slot.isLocked = false;
        slot.lockCount = 0;
        RefreshWriteLockedFlag_Locked();
        GLIDE_LOG(DEBUG, "LFB", "LfbManager: unlocked write buffer=" << buffer << " (type=" << type << ") ok=" << ok);
        return ok;
    }

    if (!m_readLock.isLocked) {
        // Fallback: if caller mismatched unlock type while a write lock is active,
        // avoid leaving a dangling lock.
        const uint32_t slotIdx = WriteSlotForBuffer(buffer);
        if (m_writeLocks[slotIdx].isLocked && m_writeLocks[slotIdx].buffer == buffer) {
            m_writeLocks[slotIdx].isLocked = false;
            m_writeLocks[slotIdx].lockCount = 0;
            RefreshWriteLockedFlag_Locked();
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
    GLIDE_CPU_SCOPE(CpuBucket::Lfb);
    std::lock_guard<std::mutex> lock(m_mutex);
    bool anyFlushed = false;
    for (auto& slot : m_writeLocks) {
        if (slot.isLocked) {
            LfbPipelineConfig slotCfg = config;
            slotCfg.pixelPipeline = slot.pixelPipeline;
            anyFlushed |= SubmitWriteLock_Locked(slot, slotCfg, /*keepLocked=*/true);
        }
    }
    if (!anyFlushed) {
        RefreshWriteLockedFlag_Locked();
    }
    return anyFlushed;
}

bool LfbManager::ReconfigureActiveWriteLock(uint32_t buffer, uint32_t newWriteMode,
                                            uint32_t newOrigin, bool newPixelPipeline,
                                            const LfbPipelineConfig& flushConfig) {
    GLIDE_CPU_SCOPE(CpuBucket::Lfb);
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint32_t slotIdx = WriteSlotForBuffer(buffer);
    WriteLockState& slot = m_writeLocks[slotIdx];
    if (!slot.isLocked) {
        return false;
    }

    const uint32_t resolvedMode = (newWriteMode == 0xFFu) ? 0u : newWriteMode;
    const uint32_t newPixelSize = PixelSizeForWriteMode(resolvedMode);
    if (newPixelSize == 0u || newPixelSize != slot.pixelSize) {
        return false;
    }

    if (slot.writeMode == resolvedMode && slot.origin == newOrigin &&
        slot.pixelPipeline == newPixelPipeline) {
        return true;
    }

    // Flush any pending writes under the previous mode/origin/bypass state
    // before switching interpretation of the staging pointer in-place.
    LfbPipelineConfig prevCfg = flushConfig;
    prevCfg.pixelPipeline = slot.pixelPipeline;
    SubmitWriteLock_Locked(slot, prevCfg, /*keepLocked=*/true);

    slot.writeMode = resolvedMode;
    slot.origin = newOrigin;
    slot.pixelPipeline = newPixelPipeline;
    return true;
}

void LfbManager::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& slot : m_writeLocks) {
        slot.isLocked = false;
        slot.lockCount = 0;
        slot.stagingClean = false;
    }
    m_readLock.isLocked = false;
    m_writeLocked.store(false, std::memory_order_release);
}

} // namespace GlideWrapper
