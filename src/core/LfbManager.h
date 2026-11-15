#pragma once

#include "core/IGraphicsBackend.h"
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

    template <typename LfbInfoType>
    bool Lock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pixelPipeline, LfbInfoType* info) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!info) return false;

        auto* b = GetActiveBackend();
        if (!b) return false;

        m_lockState.width = GetBackendWidth();
        m_lockState.height = GetBackendHeight();

        uint32_t resolvedMode = writeMode;
        if (resolvedMode == 0xFF) { // GR_LFBWRITEMODE_ANY
            resolvedMode = 0;      // GR_LFBWRITEMODE_565
        }

        // Resolve pixel size from Glide 2.x/3.x GR_LFBWRITEMODE_* enum values
        // (matching 3dfx glfb.c):
        //   2 BPP: 565 (0x0), 555 (0x1), 1555 (0x2), ZA16 (0xf)
        //   4 BPP: 888 (0x4), 8888 (0x5), 565_DEPTH (0xc), 555_DEPTH (0xd),
        //          1555_DEPTH (0xe)
        switch (resolvedMode) {
            case 0x0: // GR_LFBWRITEMODE_565
            case 0x1: // GR_LFBWRITEMODE_555
            case 0x2: // GR_LFBWRITEMODE_1555
            case 0xf: // GR_LFBWRITEMODE_ZA16
                m_lockState.pixelSize = 2;
                break;
            case 0x4: // GR_LFBWRITEMODE_888 (32-bit xRGB8888)
            case 0x5: // GR_LFBWRITEMODE_8888 (32-bit ARGB8888)
            case 0xc: // GR_LFBWRITEMODE_565_DEPTH
            case 0xd: // GR_LFBWRITEMODE_555_DEPTH
            case 0xe: // GR_LFBWRITEMODE_1555_DEPTH
                m_lockState.pixelSize = 4;
                break;
            default:
                LogErrorUnsupportedWriteMode(writeMode);
                return false;
        }

        m_lockState.stride = m_lockState.width * m_lockState.pixelSize;
        m_lockState.stagingBuffer.resize(m_lockState.stride * m_lockState.height);

        ReadLFBData(buffer, m_lockState.stagingBuffer.data());
        m_lockState.originalBuffer = m_lockState.stagingBuffer;

        if (origin == 1) { // GR_ORIGIN_LOWER_LEFT
            FlipRows(m_lockState.stagingBuffer.data(), m_lockState.height, m_lockState.stride);
        }

        info->size = sizeof(*info);
        info->writeMode = static_cast<decltype(info->writeMode)>(resolvedMode);
        info->origin = static_cast<decltype(info->origin)>(origin);
        info->strideInBytes = m_lockState.stride;
        info->lfbPtr = m_lockState.stagingBuffer.data();

        m_lockState.isLocked = true;
        m_lockState.type = type;
        m_lockState.buffer = buffer;
        m_lockState.writeMode = resolvedMode;
        m_lockState.origin = origin;
        m_lockState.pixelPipeline = pixelPipeline;

        LogLockSuccess(buffer, writeMode, origin);
        return true;
    }

    bool Unlock(uint32_t type, uint32_t buffer, const LfbPipelineConfig& config);

    // Expose whether a lock is active for verification/telemetry
    bool IsLocked() const { return m_lockState.isLocked; }
    bool IsPixelPipeline() const { return m_lockState.pixelPipeline; }

private:
    LfbManager() = default;
    ~LfbManager() = default;

    struct LockState {
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
        uint32_t stride{0};
        uint32_t pixelSize{0};
    };

    LockState m_lockState;
    mutable std::recursive_mutex m_mutex;

    // Helper functions implemented in LfbManager.cpp to hide backend dependencies
    void* GetActiveBackend();
    uint32_t GetBackendWidth();
    uint32_t GetBackendHeight();
    void ReadLFBData(uint32_t buffer, uint8_t* dest);
    void FlipRows(uint8_t* data, uint32_t height, uint32_t stride);
    void LogErrorUnsupportedWriteMode(uint32_t writeMode);
    void LogLockSuccess(uint32_t buffer, uint32_t writeMode, uint32_t origin);
};

} // namespace GlideWrapper
