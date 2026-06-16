#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include "core/Logger.h"

namespace GlideWrapper {

struct VirtualTexture {
    uint32_t startAddress;
    uint32_t format;
    uint32_t aspectRatio;
    uint32_t baseWidth;
    uint32_t baseHeight;
    std::vector<std::vector<uint32_t>> swizzledMipLevels; // Completely swizzled into 32-bit ARGB (0xAARRGGBB) per level
};

class TextureManager {
public:
    static TextureManager& GetInstance() {
        static TextureManager instance;
        return instance;
    }

    // 1. Calculate rigorous memory footprints matching 3dfx silicon MIP-map chains
    uint32_t CalculateMemoryRequired(uint32_t lodMin, uint32_t lodMax, uint32_t aspect, uint32_t format);

    // 2. Decode retro formats across full MIP-map chain and upload into active virtual TMU pool
    bool DownloadMipMap(uint32_t tmu, uint32_t startAddress, uint32_t lodMin, uint32_t lodMax, uint32_t aspect, uint32_t format, const void* data);

    // 3. Enforce silicon memory limits (e.g. 2MB or 4MB per TMU)
    void SetTmuMemoryLimitMb(uint32_t tmu, uint32_t limitMb);

    // 4. Retrieve bound virtual textures for rendering or software sampler verification
    VirtualTexture* GetTexture(uint32_t tmu, uint32_t startAddress);

    // 5. Purge and reset TMU memory state
    void Reset();

private:
    TextureManager() {
        m_tmuMemoryLimits[0] = 2 * 1024 * 1024; // 2MB default silicon ceiling
        m_tmuMemoryLimits[1] = 2 * 1024 * 1024;
    }
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    struct TmuState {
        uint32_t allocatedBytes = 0;
        std::unordered_map<uint32_t, std::shared_ptr<VirtualTexture>> textures;
    };

    std::recursive_mutex m_mutex;
    TmuState m_tmus[2];
    uint32_t m_tmuMemoryLimits[2];
};

} // namespace GlideWrapper
