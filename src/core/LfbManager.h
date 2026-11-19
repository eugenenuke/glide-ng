#pragma once

#include "core/IGraphicsBackend.h"
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>
#include <cstring>

namespace GlideWrapper {

class LfbManager {
public:
    static LfbManager& GetInstance() {
        static LfbManager instance;
        return instance;
    }

    // Glide 2.x/3.x lock type bitmask:
    //   GR_LFB_READ_ONLY = 0x00, GR_LFB_WRITE_ONLY = 0x01,
    //   GR_LFB_WRITE_ONLY_EXPLICIT_EXT = 0x02, GR_LFB_NOIDLE = 0x10.
    static constexpr bool IsWriteLockType(uint32_t type) {
        const uint32_t baseType = type & 0x0Fu;
        return baseType == 1u || baseType == 2u;
    }

    template <typename LfbInfoType>
    bool Lock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pixelPipeline, LfbInfoType* info, uint32_t forcedStridePixels = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!info) return false;

        auto* b = GetActiveBackend();
        if (!b) return false;

        const uint32_t width = GetBackendWidth();
        const uint32_t height = GetBackendHeight();

        uint32_t resolvedMode = writeMode;
        if (resolvedMode == 0xFF) { // GR_LFBWRITEMODE_ANY
            resolvedMode = 0;      // GR_LFBWRITEMODE_565
        }

        // Resolve pixel size from Glide 2.x/3.x GR_LFBWRITEMODE_* enum values
        // (matching 3dfx glfb.c):
        //   2 BPP: 565 (0x0), 555 (0x1), 1555 (0x2), ZA16 (0xf)
        //   4 BPP: 888 (0x4), 8888 (0x5), 565_DEPTH (0xc), 555_DEPTH (0xd),
        //          1555_DEPTH (0xe)
        uint32_t pixelSize = 0;
        switch (resolvedMode) {
            case 0x0: // GR_LFBWRITEMODE_565
            case 0x1: // GR_LFBWRITEMODE_555
            case 0x2: // GR_LFBWRITEMODE_1555
            case 0xf: // GR_LFBWRITEMODE_ZA16
                pixelSize = 2;
                break;
            case 0x4: // GR_LFBWRITEMODE_888 (32-bit xRGB8888)
            case 0x5: // GR_LFBWRITEMODE_8888 (32-bit ARGB8888)
            case 0xc: // GR_LFBWRITEMODE_565_DEPTH
            case 0xd: // GR_LFBWRITEMODE_555_DEPTH
            case 0xe: // GR_LFBWRITEMODE_1555_DEPTH
                pixelSize = 4;
                break;
            default:
                LogErrorUnsupportedWriteMode(writeMode);
                return false;
        }

        const uint32_t stridePixels =
            (forcedStridePixels > width) ? forcedStridePixels : width;
        const uint32_t stride = stridePixels * pixelSize;
        const size_t byteSize = static_cast<size_t>(stride) * height;

        if (IsWriteLockType(type)) {
            m_writeLock.width = width;
            m_writeLock.height = height;
            m_writeLock.pixelSize = pixelSize;
            m_writeLock.stride = stride;

            // Fill with a non-black, non-cyan sentinel instead of performing a
            // synchronous GPU ReadLFB stall (write-only locks may only be
            // written, never read, per the Glide 2.x/3.x specification).
            // Reuse the existing sentinel reference buffer and already-cleaned
            // staging buffer across frames of the same resolution/format.
            if (m_writeLock.originalBuffer.size() != byteSize ||
                m_writeLock.originalSentinelPixelSize != pixelSize) {
                m_writeLock.originalBuffer.resize(byteSize);
                FillSentinel(m_writeLock.originalBuffer.data(), stridePixels * height, pixelSize);
                m_writeLock.originalSentinelPixelSize = pixelSize;
                m_writeLock.stagingClean = false;
            }
            if (!m_writeLock.stagingClean || m_writeLock.stagingBuffer.size() != byteSize) {
                m_writeLock.stagingBuffer.resize(byteSize);
                std::memcpy(m_writeLock.stagingBuffer.data(),
                            m_writeLock.originalBuffer.data(), byteSize);
            }
            // Staging buffer is now handed to the guest and may be modified.
            m_writeLock.stagingClean = false;

            info->size = sizeof(*info);
            info->writeMode = static_cast<decltype(info->writeMode)>(resolvedMode);
            info->origin = static_cast<decltype(info->origin)>(origin);
            info->strideInBytes = m_writeLock.stride;
            info->lfbPtr = m_writeLock.stagingBuffer.data();

            m_writeLock.isLocked = true;
            m_writeLock.type = type;
            m_writeLock.buffer = buffer;
            m_writeLock.writeMode = resolvedMode;
            m_writeLock.origin = origin;
            m_writeLock.pixelPipeline = pixelPipeline;
            m_writeLocked.store(true, std::memory_order_release);

            LogLockSuccess(type, buffer, writeMode, origin, byteSize);
            return true;
        }

        // Read-only lock uses an independent staging buffer so it never
        // disturbs a persistent write-only lock pointer (e.g. DOSBox LFB map).
        m_readLock.width = width;
        m_readLock.height = height;
        m_readLock.pixelSize = pixelSize;
        m_readLock.stride = stride;
        m_readLock.stagingBuffer.assign(byteSize, 0u);

        if (stridePixels > width) {
            // Read packed rows first so CpuLfbStaging's stride-based 16/32-bit
            // selection never misclassifies a padded 1024-pixel 16-bit stride
            // (e.g. 2048 bytes at 512x384 or 320x200) as 32-bit BGRA.
            const uint32_t packedStride = width * pixelSize;
            std::vector<uint8_t> packed(static_cast<size_t>(packedStride) * height, 0u);
            ReadLFBData(buffer, width, height, packedStride, packed.data());
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(m_readLock.stagingBuffer.data() + static_cast<size_t>(y) * stride,
                            packed.data() + static_cast<size_t>(y) * packedStride,
                            packedStride);
            }
        } else {
            ReadLFBData(buffer, width, height, stride, m_readLock.stagingBuffer.data());
        }

        if (origin == 1) { // GR_ORIGIN_LOWER_LEFT
            FlipRows(m_readLock.stagingBuffer.data(), height, stride);
        }

        info->size = sizeof(*info);
        info->writeMode = static_cast<decltype(info->writeMode)>(resolvedMode);
        info->origin = static_cast<decltype(info->origin)>(origin);
        info->strideInBytes = m_readLock.stride;
        info->lfbPtr = m_readLock.stagingBuffer.data();

        m_readLock.isLocked = true;
        m_readLock.buffer = buffer;

        LogLockSuccess(type, buffer, writeMode, origin, byteSize);
        return true;
    }

    bool Unlock(uint32_t type, uint32_t buffer, const LfbPipelineConfig& config);

    // Flush any modified pixels from a persistent write-only lock (games like
    // Carmageddon lock BACK once at startup and never call grLfbUnlock) without
    // invalidating or reallocating the mapped lfbPtr.
    bool FlushActiveWriteLock(const LfbPipelineConfig& config);

    // Clear lock flags when closing a window or shutting down Glide.
    void Reset();

    // Expose lock state for verification/telemetry and fast-path swap checks.
    bool IsLocked() const { return m_writeLock.isLocked || m_readLock.isLocked; }
    bool IsWriteLocked() const { return m_writeLocked.load(std::memory_order_acquire); }
    bool IsPixelPipeline() const { return m_writeLock.pixelPipeline; }

private:
    LfbManager() = default;
    ~LfbManager() = default;

    struct WriteLockState {
        bool isLocked{false};
        uint32_t type{0};
        uint32_t buffer{0};
        uint32_t writeMode{0};
        uint32_t origin{0};
        bool pixelPipeline{false};
        uint32_t width{0};
        uint32_t height{0};
        std::vector<uint8_t> stagingBuffer;
        std::vector<uint8_t> originalBuffer;
        std::vector<uint8_t> flipScratch;
        uint32_t stride{0};
        uint32_t pixelSize{0};
        uint32_t originalSentinelPixelSize{0};
        bool stagingClean{false};
    };

    struct ReadLockState {
        bool isLocked{false};
        uint32_t buffer{0};
        uint32_t width{0};
        uint32_t height{0};
        std::vector<uint8_t> stagingBuffer;
        uint32_t stride{0};
        uint32_t pixelSize{0};
    };

    WriteLockState m_writeLock;
    ReadLockState m_readLock;
    std::atomic<bool> m_writeLocked{false};
    mutable std::mutex m_mutex;

    // Helper functions implemented in LfbManager.cpp to hide backend dependencies
    void* GetActiveBackend();
    uint32_t GetBackendWidth();
    uint32_t GetBackendHeight();
    void ReadLFBData(uint32_t buffer, uint32_t width, uint32_t height, uint32_t stride, uint8_t* dest);
    void FlipRows(uint8_t* data, uint32_t height, uint32_t stride);
    static void FillSentinel(uint8_t* dst, uint32_t pixelCount, uint32_t pixelSize);
    bool SubmitWriteLock_Locked(const LfbPipelineConfig& config, bool keepLocked);
    void LogErrorUnsupportedWriteMode(uint32_t writeMode);
    void LogLockSuccess(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, size_t byteSize);
};

} // namespace GlideWrapper
