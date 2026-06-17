#include "TextureManager.h"
#include "core/BackendManager.h"
#include "core/IGraphicsBackend.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace GlideWrapper {

uint32_t TextureManager::CalculateMemoryRequired(uint32_t lodMin, uint32_t lodMax, uint32_t aspect, uint32_t format) {
    uint32_t start = std::min(lodMin, lodMax);
    uint32_t end   = std::max(lodMin, lodMax);

    uint32_t totalBytes = 0;
    uint32_t bytesPerTexel = (format >= 0x10) ? 4 : ((format >= 0x8) ? 2 : 1);

    for (uint32_t i = start; i <= end; ++i) {
        uint32_t maxDim = 256 >> i;
        uint32_t w = maxDim;
        uint32_t h = maxDim;

        switch (aspect) {
            case 0: h = std::max(1u, maxDim / 8); break; // 8x1
            case 1: h = std::max(1u, maxDim / 4); break; // 4x1
            case 2: h = std::max(1u, maxDim / 2); break; // 2x1
            case 3: break; // 1x1
            case 4: w = std::max(1u, maxDim / 2); break; // 1x2
            case 5: w = std::max(1u, maxDim / 4); break; // 1x4
            case 6: w = std::max(1u, maxDim / 8); break; // 1x8
        }

        uint32_t levelBytes = w * h * bytesPerTexel;
        // Align MIP level footprint to 8-byte silicon TMU memory bounds
        totalBytes += (levelBytes + 7) & ~7;
    }

    GLIDE_LOG(DEBUG, "TextureManager", "Calculated MIP-map footprint: " << totalBytes << " bytes for LODs " << start << ".." << end << ", Format=" << format);
    return totalBytes;
}

bool TextureManager::DownloadMipMap(uint32_t tmu, uint32_t startAddress, uint32_t lodMin, uint32_t lodMax, uint32_t aspect, uint32_t format, const void* data) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu >= 2 || !data) return false;

    uint32_t rawBytes = CalculateMemoryRequired(lodMin, lodMax, aspect, format);

    // 1. Calculate FNV-1a 64-bit hash of raw input data including metadata
    uint64_t incomingHash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    auto hash_integer = [&](uint32_t val) {
        for (int i = 0; i < 4; ++i) {
            incomingHash ^= (val & 0xFF);
            incomingHash *= prime;
            val >>= 8;
        }
    };
    hash_integer(lodMin);
    hash_integer(lodMax);
    hash_integer(aspect);
    hash_integer(format);

    const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < rawBytes; ++i) {
        incomingHash ^= byteData[i];
        incomingHash *= prime;
    }

    // 2. Intercept and bypass if identical texture is already loaded
    auto it = m_tmus[tmu].textures.find(startAddress);
    if (it != m_tmus[tmu].textures.end()) {
        auto& cachedTex = it->second;
        if (cachedTex->rawDataHash == incomingHash &&
            cachedTex->format == format &&
            cachedTex->aspectRatio == aspect) {
            GLIDE_LOG(DEBUG, "TextureManager", "DownloadMipMap Address=0x" << std::hex << startAddress << std::dec 
                      << " raw data unmodified (hash match: 0x" << std::hex << incomingHash << std::dec 
                      << "). Bypassing swizzle and download!");
            return true;
        }
    }

    // Enforce user-configured TmuMemoryMb silicon ceiling
    if (m_tmus[tmu].allocatedBytes + rawBytes > m_tmuMemoryLimits[tmu]) {
        GLIDE_LOG(CRITICAL, "TextureManager", "TMU" << tmu << " silicon budget exceeded! Total Memory Limit is " << m_tmuMemoryLimits[tmu] << " bytes.");
        return false;
    }

    auto tex = std::make_shared<VirtualTexture>();
    tex->startAddress = startAddress;
    tex->format = format;
    tex->aspectRatio = aspect;
    tex->rawDataHash = incomingHash;

    uint32_t start = std::min(lodMin, lodMax);
    uint32_t end   = std::max(lodMin, lodMax);

    uint32_t baseMaxDim = 256 >> start;
    tex->baseWidth  = baseMaxDim;
    tex->baseHeight = baseMaxDim;
    switch (aspect) {
        case 0: tex->baseHeight = std::max(1u, baseMaxDim / 8); break; // 8x1
        case 1: tex->baseHeight = std::max(1u, baseMaxDim / 4); break; // 4x1
        case 2: tex->baseHeight = std::max(1u, baseMaxDim / 2); break; // 2x1
        case 3: break; // 1x1
        case 4: tex->baseWidth  = std::max(1u, baseMaxDim / 2); break; // 1x2
        case 5: tex->baseWidth  = std::max(1u, baseMaxDim / 4); break; // 1x4
        case 6: tex->baseWidth  = std::max(1u, baseMaxDim / 8); break; // 1x8
    }

    const auto* rawData = reinterpret_cast<const uint8_t*>(data);
    size_t byteOffset = 0;
    uint32_t bytesPerTexel = (format >= 0x10) ? 4 : ((format >= 0x8) ? 2 : 1);

    for (uint32_t i = start; i <= end; ++i) {
        uint32_t maxDim = 256 >> i;
        uint32_t w = maxDim;
        uint32_t h = maxDim;

        switch (aspect) {
            case 0: h = std::max(1u, maxDim / 8); break; // 8x1
            case 1: h = std::max(1u, maxDim / 4); break; // 4x1
            case 2: h = std::max(1u, maxDim / 2); break; // 2x1
            case 3: break; // 1x1
            case 4: w = std::max(1u, maxDim / 2); break; // 1x2
            case 5: w = std::max(1u, maxDim / 4); break; // 1x4
            case 6: w = std::max(1u, maxDim / 8); break; // 1x8
        }

        std::vector<uint32_t> levelPixels(w * h, 0);
        std::vector<uint8_t> levelRaw;
        if (format == 0x5) { // GR_TEXFMT_P_8
            tex->isPaletted = true;
            levelRaw.resize(w * h, 0);
        }

        for (size_t pIdx = 0; pIdx < w * h; ++pIdx) {
            uint32_t a = 255; uint32_t r = 0; uint32_t g = 0; uint32_t b = 0;

            if (format >= 0x10) { // 32-bit ARGB_8888
                const auto* p = reinterpret_cast<const uint32_t*>(rawData + byteOffset) + pIdx;
                uint32_t val = *p;
                a = (val >> 24) & 0xFF;
                r = (val >> 16) & 0xFF;
                g = (val >> 8)  & 0xFF;
                b = val & 0xFF;
                levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
            } else if (format >= 0x8) { // 16-bit
                const auto* p = reinterpret_cast<const uint16_t*>(rawData + byteOffset) + pIdx;
                uint16_t val = *p;
                if (format == 0xa) { // RGB_565
                    r = ((val >> 11) & 0x1F) * 255 / 31;
                    g = ((val >> 5)  & 0x3F) * 255 / 63;
                    b = (val & 0x1F) * 255 / 31;
                    a = 255;
                } else if (format == 0xc) { // ARGB_4444
                    a = ((val >> 12) & 0xF) * 255 / 15;
                    r = ((val >> 8)  & 0xF) * 255 / 15;
                    g = ((val >> 4)  & 0xF) * 255 / 15;
                    b = (val & 0xF) * 255 / 15;
                } else if (format == 0xb) { // ARGB_1555
                    a = (val & 0x8000) ? 255 : 0;
                    r = ((val >> 10) & 0x1F) * 255 / 31;
                    g = ((val >> 5)  & 0x1F) * 255 / 31;
                    b = (val & 0x1F) * 255 / 31;
                } else if (format == 0xd) { // ALPHA_INTENSITY_88
                    a = (val >> 8) & 0xFF;
                    r = val & 0xFF;
                    g = r; b = r;
                }
                levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
            } else { // 8-bit
                uint8_t val = rawData[byteOffset + pIdx];
                if (format == 0x2) { // ALPHA_8
                    a = val; r = val; g = val; b = val;
                    levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
                } else if (format == 0x3) { // INTENSITY_8
                    r = val; g = val; b = val; a = 255;
                    levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
                } else if (format == 0x5) { // P_8
                    levelRaw[pIdx] = val;
                    if (m_tmus[tmu].paletteTableLoaded) {
                        levelPixels[pIdx] = m_tmus[tmu].paletteTable[val];
                    } else {
                        // Greyscale fallback before palette download
                        levelPixels[pIdx] = (255u << 24) | (val << 16) | (val << 8) | val;
                    }
                } else { // Unknown 8-bit format
                    r = val; g = val; b = val; a = 255;
                    levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }

        tex->swizzledMipLevels.push_back(std::move(levelPixels));
        if (format == 0x5) {
            tex->raw8BitMipLevels.push_back(std::move(levelRaw));
        }
        uint32_t levelBytes = w * h * bytesPerTexel;
        byteOffset += (levelBytes + 7) & ~7; // Advancing aligned silicon footprint
    }

    m_tmus[tmu].allocatedBytes += rawBytes;
    m_tmus[tmu].textures[startAddress] = tex;

    GLIDE_LOG(DEBUG, "TextureManager", "Successfully downloaded Virtual Texture MIP Chain at Address=0x" << std::hex << startAddress << std::dec 
                                      << " Base(" << tex->baseWidth << "x" << tex->baseHeight << "), Levels=" << tex->swizzledMipLevels.size()
                                      << " into TMU" << tmu << ". Allocated pool: " << m_tmus[tmu].allocatedBytes << " bytes.");

    // If this texture is fully swizzled (either not paletted, or paletted with a pre-loaded table), upload it immediately!
    if (!tex->isPaletted || m_tmus[tmu].paletteTableLoaded) {
        if (auto* b = BackendManager::GetInstance().GetBackend()) {
            b->UploadTexture(tmu, startAddress, *tex);
        }
    }

    return true;
}

bool TextureManager::DownloadTable(uint32_t tmu, uint32_t type, const void* data) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu >= 2 || !data) return false;

    // Currently, we only support 256-color palette tables (type = GR_TEXTABLE_PALETTE = 2)
    if (type != 2) {
        GLIDE_LOG(WARN, "TextureManager", "Unsupported texture table type: " << type << ". Skipping.");
        return false;
    }

    GLIDE_LOG(DEBUG, "TextureManager", "Downloading 256-color palette table to TMU" << tmu);
    const auto* paletteData = reinterpret_cast<const uint32_t*>(data);
    std::memcpy(m_tmus[tmu].paletteTable, paletteData, sizeof(m_tmus[tmu].paletteTable));
    m_tmus[tmu].paletteTableLoaded = true;

    // Re-swizzle and trigger GPU upload for all paletted textures in this TMU!
    int swizzledCount = 0;
    for (auto& pair : m_tmus[tmu].textures) {
        auto& tex = pair.second;
        if (tex->isPaletted && !tex->raw8BitMipLevels.empty()) {
            tex->swizzledMipLevels.clear();
            for (size_t lvl = 0; lvl < tex->raw8BitMipLevels.size(); ++lvl) {
                const auto& raw8 = tex->raw8BitMipLevels[lvl];
                std::vector<uint32_t> swizzled(raw8.size());
                for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
                    uint8_t idx = raw8[pIdx];
                    swizzled[pIdx] = m_tmus[tmu].paletteTable[idx];
                }
                tex->swizzledMipLevels.push_back(std::move(swizzled));
            }
            swizzledCount++;
            GLIDE_LOG(DEBUG, "TextureManager", "Deferred-swizzled paletted texture at Address=0x" << std::hex << tex->startAddress << std::dec << " using new palette table.");
            
            // Upload the fully swizzled texture to the GPU!
            if (auto* b = BackendManager::GetInstance().GetBackend()) {
                b->UploadTexture(tmu, tex->startAddress, *tex);
            }
        }
    }

    GLIDE_LOG(DEBUG, "TextureManager", "Completed palette download. Deferred-swizzled " << swizzledCount << " textures.");
    return true;
}

void TextureManager::SetTmuMemoryLimitMb(uint32_t tmu, uint32_t limitMb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu < 2) {
        m_tmuMemoryLimits[tmu] = limitMb * 1024 * 1024;
        GLIDE_LOG(INFO, "TextureManager", "Configured TMU" << tmu << " silicon budget to " << limitMb << " MB.");
    }
}

VirtualTexture* TextureManager::GetTexture(uint32_t tmu, uint32_t startAddress) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu >= 2) return nullptr;
    auto it = m_tmus[tmu].textures.find(startAddress);
    return (it != m_tmus[tmu].textures.end()) ? it->second.get() : nullptr;
}

void TextureManager::Reset() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (int i = 0; i < 2; ++i) {
        m_tmus[i].allocatedBytes = 0;
        m_tmus[i].textures.clear();
    }
    GLIDE_LOG(INFO, "TextureManager", "Purged and reset all Virtual TMU state pools.");
}

} // namespace GlideWrapper
