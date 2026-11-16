#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/BackendManager.h"
#include "core/GlideEntryCommon.h"
#include "core/IGraphicsBackend.h"
#include "core/Telemetry.h"
#include "core/WrapperConfig.h"

namespace GlideWrapper {

namespace {
uint64_t CalcSwizzledUploadBytes(const VirtualTexture& tex) {
  uint64_t total = 0;
  for (const auto& lvl : tex.swizzledMipLevels) {
    total += static_cast<uint64_t>(lvl.size()) * sizeof(uint32_t);
  }
  return total;
}
GuNccTable GetActiveNccTableOrDefault(const GuNccTable* nccTables,
                                      const bool* nccTablesLoaded,
                                      uint32_t activeNccTable) {
  if (nccTablesLoaded[activeNccTable]) {
    return nccTables[activeNccTable];
  }
  // Return a default identity table (grayscale mapping)
  GuNccTable ncc;
  for (int y = 0; y < 16; ++y) {
    int val = y * 255 / 15;
    ncc.yRGB[y] = val;
  }
  std::memset(ncc.iRGB, 0, sizeof(ncc.iRGB));
  std::memset(ncc.qRGB, 0, sizeof(ncc.qRGB));
  return ncc;
}

inline int16_t sign_extend_9bit(int16_t val) {
  // If bit 8 is set, it represents a negative 9-bit two's complement number
  if (val & 0x0100) {
    return val - 512;
  }
  return val;
}

uint32_t swizzleYiq(uint8_t val, const GuNccTable& ncc) {
  uint8_t y = (val >> 4) & 0xF;
  uint8_t i = (val >> 2) & 0x3;
  uint8_t q = val & 0x3;

  int r = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][0]) +
          sign_extend_9bit(ncc.qRGB[q][0]);
  int g = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][1]) +
          sign_extend_9bit(ncc.qRGB[q][1]);
  int b = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][2]) +
          sign_extend_9bit(ncc.qRGB[q][2]);

  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  return (255u << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t swizzleAyiq(uint16_t val, const GuNccTable& ncc) {
  uint8_t a = (val >> 8) & 0xFF;
  uint8_t y = (val >> 4) & 0xF;
  uint8_t i = (val >> 2) & 0x3;
  uint8_t q = val & 0x3;

  int r = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][0]) +
          sign_extend_9bit(ncc.qRGB[q][0]);
  int g = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][1]) +
          sign_extend_9bit(ncc.qRGB[q][1]);
  int b = ncc.yRGB[y] + sign_extend_9bit(ncc.iRGB[i][2]) +
          sign_extend_9bit(ncc.qRGB[q][2]);

  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t swizzleAp88(uint16_t val, const uint32_t* palette) {
  uint8_t a = (val >> 8) & 0xFF;
  uint8_t idx = val & 0xFF;
  uint32_t color = palette[idx];
  return (static_cast<uint32_t>(a) << 24) | (color & 0x00FFFFFF);
}

uint64_t HashTableBytes(const void* data, size_t len) {
  uint64_t h = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= kPrime;
  }
  return (h == 0) ? 1ULL : h;
}

uint32_t LodToDimension(int32_t lod) {
  return (lod >= 0) ? (256 >> lod) : (256 << (-lod));
}

void ComputeLodDimensions(int32_t lod, uint32_t aspect, uint32_t& outW,
                          uint32_t& outH) {
  const uint32_t maxDim = LodToDimension(lod);
  outW = maxDim;
  outH = maxDim;
  switch (aspect) {
    case 0:
      outH = std::max(1u, maxDim / 8);
      break;  // 8x1
    case 1:
      outH = std::max(1u, maxDim / 4);
      break;  // 4x1
    case 2:
      outH = std::max(1u, maxDim / 2);
      break;  // 2x1
    case 3:
      break;  // 1x1
    case 4:
      outW = std::max(1u, maxDim / 2);
      break;  // 1x2
    case 5:
      outW = std::max(1u, maxDim / 4);
      break;  // 1x4
    case 6:
      outW = std::max(1u, maxDim / 8);
      break;  // 1x8
  }
}
}  // namespace

uint64_t TextureManager::GetActiveTableHash_Locked(const TmuState& state,
                                                   const VirtualTexture& tex) {
  if (tex.isPaletted) {
    return state.paletteTableLoaded ? state.paletteHash : 0;
  }
  if (tex.isNcc) {
    return state.nccTablesLoaded[state.activeNccTable]
               ? state.nccHashes[state.activeNccTable]
               : 0;
  }
  return 0;
}

void TextureManager::ReswizzleTexture_Locked(const TmuState& state,
                                             VirtualTexture& tex) {
  if (tex.isPaletted) {
    if (tex.format == 0x5 || tex.format == 0x6) {  // P_8 or P_8_6666
      const size_t nLevels = tex.raw8BitMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw8 = tex.raw8BitMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw8.size());
        if (state.paletteTableLoaded) {
          for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
            dst[pIdx] = state.paletteTable[raw8[pIdx]];
          }
        } else {
          for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
            uint8_t val = raw8[pIdx];
            dst[pIdx] = (255u << 24) | (static_cast<uint32_t>(val) << 16) |
                        (static_cast<uint32_t>(val) << 8) | val;
          }
        }
      }
    } else if (tex.format == 0xe) {  // AP_88
      const size_t nLevels = tex.rawAp88MipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw16 = tex.rawAp88MipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw16.size());
        if (state.paletteTableLoaded) {
          for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
            dst[pIdx] = swizzleAp88(raw16[pIdx], state.paletteTable);
          }
        } else {
          for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
            uint8_t a = static_cast<uint8_t>((raw16[pIdx] >> 8) & 0xFF);
            uint8_t idx = static_cast<uint8_t>(raw16[pIdx] & 0xFF);
            dst[pIdx] = (static_cast<uint32_t>(a) << 24) |
                        (static_cast<uint32_t>(idx) << 16) |
                        (static_cast<uint32_t>(idx) << 8) | idx;
          }
        }
      }
    }
    tex.swizzledWithTable = state.paletteTableLoaded;
    tex.swizzledTableHash = GetActiveTableHash_Locked(state, tex);
  } else if (tex.isNcc) {
    const GuNccTable activeNcc = GetActiveNccTableOrDefault(
        state.nccTables, state.nccTablesLoaded, state.activeNccTable);
    if (tex.format == 0x1) {  // YIQ_422
      const size_t nLevels = tex.rawYiqMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw8 = tex.rawYiqMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw8.size());
        for (size_t pIdx = 0; pIdx < raw8.size(); ++pIdx) {
          dst[pIdx] = swizzleYiq(raw8[pIdx], activeNcc);
        }
      }
    } else if (tex.format == 0x9) {  // AYIQ_8422
      const size_t nLevels = tex.rawAyiqMipLevels.size();
      tex.swizzledMipLevels.resize(nLevels);
      for (size_t lvl = 0; lvl < nLevels; ++lvl) {
        const auto& raw16 = tex.rawAyiqMipLevels[lvl];
        auto& dst = tex.swizzledMipLevels[lvl];
        dst.resize(raw16.size());
        for (size_t pIdx = 0; pIdx < raw16.size(); ++pIdx) {
          dst[pIdx] = swizzleAyiq(raw16[pIdx], activeNcc);
        }
      }
    }
    tex.swizzledWithTable = state.nccTablesLoaded[state.activeNccTable];
    tex.swizzledTableHash = GetActiveTableHash_Locked(state, tex);
  }
}

void TextureManager::EnsureTextureReady_Locked(
    uint32_t tmu, std::shared_ptr<VirtualTexture>& texPtr,
    bool uploadToBackend) {
  if (!texPtr) return;
  if (!texPtr->isPaletted && !texPtr->isNcc) return;

  const auto& state = m_tmus[tmu];
  const uint64_t activeHash = GetActiveTableHash_Locked(state, *texPtr);

  const bool needsSwizzle =
      (texPtr->isPaletted && state.paletteTableLoaded &&
       (!texPtr->swizzledWithTable || texPtr->swizzledTableHash != activeHash)) ||
      (texPtr->isNcc && (texPtr->swizzledTableHash != activeHash));

  if (needsSwizzle) {
    // Copy-on-write: never mutate a VirtualTexture snapshot still held by
    // SoftwareBackend's deferred tile bins.
    if (texPtr.use_count() > 1) {
      texPtr = std::make_shared<VirtualTexture>(*texPtr);
    }
    ReswizzleTexture_Locked(state, *texPtr);
  }

  if (uploadToBackend) {
    if (!texPtr->uploadedToBackend ||
        texPtr->uploadedTableHash != texPtr->swizzledTableHash) {
      if (auto* b = BackendManager::GetInstance().GetBackend()) {
        TelemetryManager::GetInstance().RecordTexUpload(
            CalcSwizzledUploadBytes(*texPtr));
        b->UploadTexture(tmu, texPtr->startAddress, *texPtr);
        texPtr->uploadedToBackend = true;
        texPtr->uploadedTableHash = texPtr->swizzledTableHash;
      }
    }
  }
}

uint32_t TextureManager::CalculateMemoryRequired(int32_t lodMin,
                                                 int32_t lodMax,
                                                 uint32_t aspect,
                                                 uint32_t format, bool packed) {
  int32_t start = std::min(lodMin, lodMax);
  int32_t end = std::max(lodMin, lodMax);

  uint32_t totalBytes = 0;
  uint32_t bytesPerTexel = (format >= 0x10) ? 4 : ((format >= 0x8) ? 2 : 1);

  for (int32_t i = start; i <= end; ++i) {
    uint32_t maxDim = LodToDimension(i);
    uint32_t w = maxDim;
    uint32_t h = maxDim;

    switch (aspect) {
      case 0:
        h = std::max(1u, maxDim / 8);
        break;  // 8x1
      case 1:
        h = std::max(1u, maxDim / 4);
        break;  // 4x1
      case 2:
        h = std::max(1u, maxDim / 2);
        break;  // 2x1
      case 3:
        break;  // 1x1
      case 4:
        w = std::max(1u, maxDim / 2);
        break;  // 1x2
      case 5:
        w = std::max(1u, maxDim / 4);
        break;  // 1x4
      case 6:
        w = std::max(1u, maxDim / 8);
        break;  // 1x8
    }

    uint32_t levelBytes = w * h * bytesPerTexel;
    if (packed) {
      totalBytes += levelBytes;
    } else {
      // Align MIP level footprint to 8-byte silicon TMU memory bounds
      totalBytes += (levelBytes + 7) & ~7;
    }
  }

  GLIDE_LOG(DEBUG, "TextureManager",
            "Calculated MIP-map footprint: " << totalBytes << " bytes for LODs "
                                             << start << ".." << end
                                             << ", Format=" << format);
  return totalBytes;
}

bool TextureManager::DownloadMipMap(uint32_t tmu, uint32_t startAddress,
                                    int32_t lodMin, int32_t lodMax,
                                    uint32_t aspect, uint32_t format,
                                    const void* data, bool replaceExisting,
                                    int32_t baseLargeLod) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // VULN-05: Strict validation of LOD limits and aspect ratios to prevent out-of-bounds memory access
  if (lodMin > lodMax) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: lodMin (" << lodMin << ") is greater than lodMax (" << lodMax << ")!");
    return false;
  }
  if (aspect > 6) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: aspect ratio (" << aspect << ") is out of range (0..6)!");
    return false;
  }

  // Card-specific bounds check
  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  uint32_t maxDimension = (model >= CardModel::Voodoo3) ? 2048 : 256;
  int32_t minValidLod = 8 - static_cast<int32_t>(std::log2(maxDimension));
  if (lodMin < minValidLod || lodMin > 8 || lodMax < minValidLod || lodMax > 8) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMap: LOD index out of emulated card bounds!");
    return false;
  }

  const int32_t start = std::min(lodMin, lodMax);
  const int32_t end = std::max(lodMin, lodMax);
  const int32_t effectiveDeclaredLarge =
      (!replaceExisting && baseLargeLod != kNoBaseLargeLod) ? baseLargeLod
                                                            : start;

  uint32_t rawBytes = CalculateMemoryRequired(
      lodMin, lodMax, aspect, format,
      true);  // Use packed unaligned size for raw input data hashing!

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
  hash_integer(static_cast<uint32_t>(lodMin));
  hash_integer(static_cast<uint32_t>(lodMax));
  hash_integer(aspect);
  hash_integer(format);

  const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < rawBytes; ++i) {
    incomingHash ^= byteData[i];
    incomingHash *= prime;
  }

  // 2. Intercept and bypass if an identical complete texture is already loaded
  auto it = m_tmus[tmu].textures.find(startAddress);
  if (it != m_tmus[tmu].textures.end()) {
    auto& cachedTex = it->second;
    if (cachedTex->rawDataHash == incomingHash && cachedTex->format == format &&
        cachedTex->aspectRatio == aspect && cachedTex->largeLod == start &&
        cachedTex->smallLod == end &&
        (replaceExisting ||
         cachedTex->declaredLargeLod == effectiveDeclaredLarge)) {
      if (cachedTex->isPaletted || cachedTex->isNcc) {
        EnsureTextureReady_Locked(tmu, it->second, true);
      } else if (!cachedTex->uploadedToBackend) {
        if (auto* b = BackendManager::GetInstance().GetBackend()) {
          TelemetryManager::GetInstance().RecordTexUpload(
              CalcSwizzledUploadBytes(*cachedTex));
          b->UploadTexture(tmu, startAddress, *cachedTex);
          cachedTex->uploadedToBackend = true;
        }
      }
      GLIDE_LOG(DEBUG, "TextureManager",
                "DownloadMipMap Address=0x"
                    << std::hex << startAddress << std::dec
                    << " raw data unmodified (hash match: 0x" << std::hex
                    << incomingHash << std::dec
                    << "). Bypassing swizzle and download!");
      return true;
    }
  }

  // Determine whether this call merges a single level into an existing mipmap
  // chain (grTexDownloadMipMapLevel) or replaces the texture at startAddress
  // (grTexDownloadMipMap, or a new texture reusing the address).
  std::shared_ptr<VirtualTexture> existingToMerge;
  int32_t newLarge = start;
  int32_t newSmall = end;

  if (!replaceExisting && it != m_tmus[tmu].textures.end()) {
    const auto& existing = it->second;
    const bool sameBaseChain =
        (existing->format == format && existing->aspectRatio == aspect &&
         (existing->declaredLargeLod == effectiveDeclaredLarge ||
          existing->largeLod == effectiveDeclaredLarge) &&
         start >= effectiveDeclaredLarge);
    if (sameBaseChain) {
      existingToMerge = existing;
      newLarge = std::min(existing->largeLod, start);
      newSmall = std::max(existing->smallLod, end);
    }
  }

  const uint32_t newBytes =
      CalculateMemoryRequired(newLarge, newSmall, aspect, format);

  // Identify all resident textures in this TMU whose byte range overlaps the
  // incoming texture footprint [startAddress, startAddress + newBytes).
  const uint64_t newRangeStart = static_cast<uint64_t>(startAddress);
  const uint64_t newRangeEnd = newRangeStart + static_cast<uint64_t>(newBytes);
  std::vector<uint32_t> overlappingAddrs;
  uint64_t evictedBytes = 0;

  for (const auto& entry : m_tmus[tmu].textures) {
    const uint64_t eStart = static_cast<uint64_t>(entry.first);
    const uint64_t eSize =
        (entry.second->allocatedByteSize > 0)
            ? static_cast<uint64_t>(entry.second->allocatedByteSize)
            : static_cast<uint64_t>(CalculateMemoryRequired(
                  entry.second->largeLod, entry.second->smallLod,
                  entry.second->aspectRatio, entry.second->format));
    const uint64_t eEnd = eStart + eSize;
    if (entry.first == startAddress ||
        (eStart < newRangeEnd && newRangeStart < eEnd)) {
      overlappingAddrs.push_back(entry.first);
      evictedBytes += eSize;
    }
  }

  // Enforce user-configured TmuMemoryMb silicon ceiling atomically before
  // evicting any existing textures.
  const uint64_t currentUsed = m_tmus[tmu].allocatedBytes;
  const uint64_t usedAfterEvict =
      (currentUsed >= evictedBytes) ? (currentUsed - evictedBytes) : 0ULL;
  if (usedAfterEvict + newBytes > m_tmuMemoryLimits[tmu]) {
    GLIDE_LOG(CRITICAL, "TextureManager",
              "TMU" << tmu << " silicon budget exceeded!");
    return false;
  }

  // Always construct a fresh VirtualTexture instance so active copy-on-write
  // snapshots held by SoftwareBackend's deferred tile bins remain immutable.
  auto tex = std::make_shared<VirtualTexture>();
  tex->startAddress = startAddress;
  tex->format = format;
  tex->aspectRatio = aspect;
  tex->declaredLargeLod = effectiveDeclaredLarge;
  tex->allocatedByteSize = newBytes;

  uint32_t numLevels = static_cast<uint32_t>(newSmall - newLarge + 1);
  std::vector<std::vector<uint32_t>> newSwizzled(numLevels);
  std::vector<std::vector<uint8_t>> newRaw8Bit;
  std::vector<std::vector<uint8_t>> newRawYiq;
  std::vector<std::vector<uint16_t>> newRawAyiq;
  std::vector<std::vector<uint16_t>> newRawAp88;

  if (format == 0x5 || format == 0x6) newRaw8Bit.resize(numLevels);
  if (format == 0x1) newRawYiq.resize(numLevels);
  if (format == 0x9) newRawAyiq.resize(numLevels);
  if (format == 0xe) newRawAp88.resize(numLevels);

  if (existingToMerge) {
    tex->isPaletted = existingToMerge->isPaletted;
    tex->isNcc = existingToMerge->isNcc;
    for (int32_t L = existingToMerge->largeLod; L <= existingToMerge->smallLod;
         ++L) {
      uint32_t oldIdx = static_cast<uint32_t>(L - existingToMerge->largeLod);
      uint32_t newIdx = static_cast<uint32_t>(L - newLarge);
      if (oldIdx < existingToMerge->swizzledMipLevels.size()) {
        newSwizzled[newIdx] = existingToMerge->swizzledMipLevels[oldIdx];
      }
      if (existingToMerge->format == 0x5 || existingToMerge->format == 0x6) {
        if (oldIdx < existingToMerge->raw8BitMipLevels.size()) {
          newRaw8Bit[newIdx] = existingToMerge->raw8BitMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0x1) {
        if (oldIdx < existingToMerge->rawYiqMipLevels.size()) {
          newRawYiq[newIdx] = existingToMerge->rawYiqMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0x9) {
        if (oldIdx < existingToMerge->rawAyiqMipLevels.size()) {
          newRawAyiq[newIdx] = existingToMerge->rawAyiqMipLevels[oldIdx];
        }
      } else if (existingToMerge->format == 0xe) {
        if (oldIdx < existingToMerge->rawAp88MipLevels.size()) {
          newRawAp88[newIdx] = existingToMerge->rawAp88MipLevels[oldIdx];
        }
      }
    }
  }

  const auto* rawData = reinterpret_cast<const uint8_t*>(data);
  size_t byteOffset = 0;
  uint32_t bytesPerTexel = (format >= 0x10) ? 4 : ((format >= 0x8) ? 2 : 1);

  for (int32_t i = start; i <= end; ++i) {
    uint32_t maxDim = LodToDimension(i);
    uint32_t w = maxDim;
    uint32_t h = maxDim;

    switch (aspect) {
      case 0:
        h = std::max(1u, maxDim / 8);
        break;  // 8x1
      case 1:
        h = std::max(1u, maxDim / 4);
        break;  // 4x1
      case 2:
        h = std::max(1u, maxDim / 2);
        break;  // 2x1
      case 3:
        break;  // 1x1
      case 4:
        w = std::max(1u, maxDim / 2);
        break;  // 1x2
      case 5:
        w = std::max(1u, maxDim / 4);
        break;  // 1x4
      case 6:
        w = std::max(1u, maxDim / 8);
        break;  // 1x8
    }

    std::vector<uint32_t> levelPixels(w * h, 0);
    std::vector<uint8_t> levelRaw;
    if (format == 0x5 ||
        format == 0x6) {  // GR_TEXFMT_P_8 or GR_TEXFMT_P_8_6666
      tex->isPaletted = true;
      levelRaw.resize(w * h, 0);
    }
    std::vector<uint8_t> levelYiq;
    if (format == 0x1) {  // GR_TEXFMT_YIQ_422
      tex->isNcc = true;
      levelYiq.resize(w * h, 0);
    }
    std::vector<uint16_t> levelAyiq;
    if (format == 0x9) {  // GR_TEXFMT_AYIQ_8422
      tex->isNcc = true;
      levelAyiq.resize(w * h, 0);
    }
    std::vector<uint16_t> levelAp88;
    if (format == 0xe) {  // GR_TEXFMT_AP_88
      tex->isPaletted = true;
      levelAp88.resize(w * h, 0);
    }

    GuNccTable activeNcc = GetActiveNccTableOrDefault(
        m_tmus[tmu].nccTables, m_tmus[tmu].nccTablesLoaded,
        m_tmus[tmu].activeNccTable);

    for (size_t pIdx = 0; pIdx < w * h; ++pIdx) {
      uint32_t a = 255;
      uint32_t r = 0;
      uint32_t g = 0;
      uint32_t b = 0;

      if (format >= 0x10) {  // 32-bit ARGB_8888
        const auto* p =
            reinterpret_cast<const uint32_t*>(rawData + byteOffset) + pIdx;
        uint32_t val = *p;
        a = (val >> 24) & 0xFF;
        r = (val >> 16) & 0xFF;
        g = (val >> 8) & 0xFF;
        b = val & 0xFF;
        levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      } else if (format >= 0x8) {  // 16-bit
        const auto* p =
            reinterpret_cast<const uint16_t*>(rawData + byteOffset) + pIdx;
        uint16_t val = *p;
        if (format == 0xa) {  // RGB_565
          r = ((val >> 11) & 0x1F) * 255 / 31;
          g = ((val >> 5) & 0x3F) * 255 / 63;
          b = (val & 0x1F) * 255 / 31;
          a = 255;
        } else if (format == 0xc) {  // ARGB_4444
          a = ((val >> 12) & 0xF) * 255 / 15;
          r = ((val >> 8) & 0xF) * 255 / 15;
          g = ((val >> 4) & 0xF) * 255 / 15;
          b = (val & 0xF) * 255 / 15;
        } else if (format == 0xb) {  // ARGB_1555
          a = (val & 0x8000) ? 255 : 0;
          r = ((val >> 10) & 0x1F) * 255 / 31;
          g = ((val >> 5) & 0x1F) * 255 / 31;
          b = (val & 0x1F) * 255 / 31;
        } else if (format == 0xd) {  // ALPHA_INTENSITY_88
          a = (val >> 8) & 0xFF;
          r = val & 0xFF;
          g = r;
          b = r;
        } else if (format == 0x8) {  // ARGB_8332
          a = (val >> 8) & 0xFF;
          r = ((val >> 5) & 0x7) * 255 / 7;
          g = ((val >> 2) & 0x7) * 255 / 7;
          b = (val & 0x3) * 255 / 3;
        } else if (format == 0x9) {  // AYIQ_8422
          levelAyiq[pIdx] = val;
          levelPixels[pIdx] = swizzleAyiq(val, activeNcc);
        } else if (format == 0xe) {  // AP_88
          levelAp88[pIdx] = val;
          if (m_tmus[tmu].paletteTableLoaded) {
            levelPixels[pIdx] = swizzleAp88(val, m_tmus[tmu].paletteTable);
          } else {
            uint8_t alphaVal = (val >> 8) & 0xFF;
            uint8_t indexVal = val & 0xFF;
            levelPixels[pIdx] = (alphaVal << 24) | (indexVal << 16) |
                                (indexVal << 8) | indexVal;
          }
        }
        if (format != 0x9 && format != 0xe) {
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
      } else {  // 8-bit
        uint8_t val = rawData[byteOffset + pIdx];
        if (format == 0x2) {  // ALPHA_8
          a = val;
          r = val;
          g = val;
          b = val;
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        } else if (format == 0x3) {  // INTENSITY_8
          r = val;
          g = val;
          b = val;
          a = 255;
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        } else if (format == 0x5 || format == 0x6) {  // P_8 or P_8_6666
          levelRaw[pIdx] = val;
          if (m_tmus[tmu].paletteTableLoaded) {
            levelPixels[pIdx] = m_tmus[tmu].paletteTable[val];
          } else {
            // Greyscale fallback before palette download
            levelPixels[pIdx] = (255u << 24) | (val << 16) | (val << 8) | val;
          }
        } else if (format == 0x0) {  // RGB_332
          r = ((val >> 5) & 0x7) * 255 / 7;
          g = ((val >> 2) & 0x7) * 255 / 7;
          b = (val & 0x3) * 255 / 3;
          a = 255;
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        } else if (format == 0x1) {  // YIQ_422
          levelYiq[pIdx] = val;
          levelPixels[pIdx] = swizzleYiq(val, activeNcc);
        } else if (format == 0x4) {  // ALPHA_INTENSITY_44
          a = ((val >> 4) & 0xF) * 255 / 15;
          uint32_t iVal = (val & 0xF) * 255 / 15;
          r = iVal;
          g = iVal;
          b = iVal;
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        } else {  // Unknown 8-bit format
          r = val;
          g = val;
          b = val;
          a = 255;
          levelPixels[pIdx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
      }
    }

    uint32_t destIdx = static_cast<uint32_t>(i - newLarge);
    newSwizzled[destIdx] = std::move(levelPixels);
    if (format == 0x5 || format == 0x6) {
      newRaw8Bit[destIdx] = std::move(levelRaw);
    } else if (format == 0x1) {
      newRawYiq[destIdx] = std::move(levelYiq);
    } else if (format == 0x9) {
      newRawAyiq[destIdx] = std::move(levelAyiq);
    } else if (format == 0xe) {
      newRawAp88[destIdx] = std::move(levelAp88);
    }
    uint32_t levelBytes = w * h * bytesPerTexel;
    byteOffset += levelBytes;  // Advancing packed unaligned input bytes offset!
  }

  // Guarantee every mip level slot in [newLarge..newSmall] is populated to its
  // exact w*h size so no backend can ever dereference an empty gap mip level.
  for (int32_t L = newLarge; L <= newSmall; ++L) {
    const uint32_t idx = static_cast<uint32_t>(L - newLarge);
    uint32_t lw = 1, lh = 1;
    ComputeLodDimensions(L, aspect, lw, lh);
    const size_t expectedTexels = static_cast<size_t>(lw) * lh;
    if (newSwizzled[idx].size() != expectedTexels) {
      newSwizzled[idx].resize(expectedTexels, 0);
    }
    if ((format == 0x5 || format == 0x6) &&
        newRaw8Bit[idx].size() != expectedTexels) {
      newRaw8Bit[idx].resize(expectedTexels, 0);
    } else if (format == 0x1 && newRawYiq[idx].size() != expectedTexels) {
      newRawYiq[idx].resize(expectedTexels, 0);
    } else if (format == 0x9 && newRawAyiq[idx].size() != expectedTexels) {
      newRawAyiq[idx].resize(expectedTexels, 0);
    } else if (format == 0xe && newRawAp88[idx].size() != expectedTexels) {
      newRawAp88[idx].resize(expectedTexels, 0);
    }
  }

  tex->swizzledMipLevels = std::move(newSwizzled);
  if (format == 0x5 || format == 0x6)
    tex->raw8BitMipLevels = std::move(newRaw8Bit);
  else if (format == 0x1)
    tex->rawYiqMipLevels = std::move(newRawYiq);
  else if (format == 0x9)
    tex->rawAyiqMipLevels = std::move(newRawAyiq);
  else if (format == 0xe)
    tex->rawAp88MipLevels = std::move(newRawAp88);

  tex->largeLod = newLarge;
  tex->smallLod = newSmall;

  // Update base dimensions to correspond to newLarge
  ComputeLodDimensions(newLarge, aspect, tex->baseWidth, tex->baseHeight);

  // Only store rawDataHash when the stored chain exactly matches the hashed
  // download range; merged chains should not falsely bypass subsequent calls.
  tex->rawDataHash =
      (newLarge == start && newSmall == end) ? incomingHash : 0ULL;
  tex->swizzledTableHash = GetActiveTableHash_Locked(m_tmus[tmu], *tex);
  tex->swizzledWithTable =
      tex->isPaletted
          ? m_tmus[tmu].paletteTableLoaded
          : (tex->isNcc
                 ? m_tmus[tmu].nccTablesLoaded[m_tmus[tmu].activeNccTable]
                 : true);
  tex->uploadedToBackend = false;

  // Commit overlapping evictions and insert the new VirtualTexture
  for (uint32_t addr : overlappingAddrs) {
    m_tmus[tmu].textures.erase(addr);
  }
  m_tmus[tmu].allocatedBytes = static_cast<uint32_t>(usedAfterEvict + newBytes);
  m_tmus[tmu].textures[startAddress] = tex;

  GLIDE_LOG(DEBUG, "TextureManager",
            "Successfully downloaded Virtual Texture MIP Chain at Address=0x"
                << std::hex << startAddress << std::dec << " Base("
                << tex->baseWidth << "x" << tex->baseHeight
                << "), Levels=" << tex->swizzledMipLevels.size() << " into TMU"
                << tmu << ". Allocated pool: " << m_tmus[tmu].allocatedBytes
                << " bytes.");

  // If this texture is fully swizzled (either not paletted, or paletted with a
  // pre-loaded table), upload it immediately!
  if (!tex->isPaletted || m_tmus[tmu].paletteTableLoaded) {
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
      TelemetryManager::GetInstance().RecordTexUpload(
          CalcSwizzledUploadBytes(*tex));
      b->UploadTexture(tmu, startAddress, *tex);
      tex->uploadedToBackend = true;
      tex->uploadedTableHash = tex->swizzledTableHash;
    }
  }

  return true;
}

bool TextureManager::DownloadMipMapPartial(uint32_t tmu, uint32_t startAddress,
                                           int32_t thisLod, int32_t largeLod,
                                           uint32_t aspectRatio,
                                           uint32_t format, const void* data,
                                           int startRow, int endRow) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // VULN-05: Strict validation of LOD limits and aspect ratios to prevent out-of-bounds memory access
  if (thisLod < largeLod) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: thisLod (" << thisLod << ") is smaller than base largeLod (" << largeLod << ")!");
    return false;
  }
  if (aspectRatio > 6) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: aspect ratio (" << aspectRatio << ") is out of range (0..6)!");
    return false;
  }

  // Card-specific bounds check
  auto model = EmulationRegistry::GetInstance().GetConfig().model;
  uint32_t maxDimension = (model >= CardModel::Voodoo3) ? 2048 : 256;
  int32_t minValidLod = 8 - static_cast<int32_t>(std::log2(maxDimension));
  if (thisLod < minValidLod || thisLod > 8 || largeLod < minValidLod || largeLod > 8) {
    GLIDE_LOG(CRITICAL, "TextureManager", "DownloadMipMapPartial: LOD index out of emulated card bounds!");
    return false;
  }

  // Find the existing texture
  auto it = m_tmus[tmu].textures.find(startAddress);
  if (it == m_tmus[tmu].textures.end()) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: texture at address 0x"
                  << std::hex << startAddress << std::dec
                  << " does not exist! Cannot do partial update.");
    return false;
  }

  // Copy-on-write before mutating pixels so deferred SoftwareBackend snapshots
  // remain immutable.
  if (it->second.use_count() > 1) {
    it->second = std::make_shared<VirtualTexture>(*it->second);
  }
  auto& tex = it->second;

  // Validate LOD level
  if (thisLod < tex->largeLod || thisLod > tex->smallLod) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: LOD level "
                  << thisLod << " is out of texture LOD range " << tex->largeLod
                  << ".." << tex->smallLod);
    return false;
  }

  uint32_t lodIdx = static_cast<uint32_t>(thisLod - tex->largeLod);
  if (lodIdx >= tex->swizzledMipLevels.size()) {
    GLIDE_LOG(
        WARN, "TextureManager",
        "DownloadMipMapPartial: LOD index " << lodIdx << " out of bounds");
    return false;
  }

  uint32_t maxDim = LodToDimension(thisLod);
  uint32_t w = maxDim;
  uint32_t h = maxDim;
  switch (aspectRatio) {
    case 0:
      h = std::max(1u, maxDim / 8);
      break;
    case 1:
      h = std::max(1u, maxDim / 4);
      break;
    case 2:
      h = std::max(1u, maxDim / 2);
      break;
    case 3:
      break;
    case 4:
      w = std::max(1u, maxDim / 2);
      break;
    case 5:
      w = std::max(1u, maxDim / 4);
      break;
    case 6:
      w = std::max(1u, maxDim / 8);
      break;
  }

  if (startRow < 0 || startRow >= static_cast<int>(h) || endRow <= startRow ||
      endRow > static_cast<int>(h)) {
    GLIDE_LOG(WARN, "TextureManager",
              "DownloadMipMapPartial: invalid row range "
                  << startRow << ".." << endRow << " for height " << h);
    return false;
  }

  // Decode only the partial rows
  int numRows = endRow - startRow;
  size_t partialTexels = numRows * w;

  const auto* rawData = reinterpret_cast<const uint8_t*>(data);
  GuNccTable activeNcc = GetActiveNccTableOrDefault(m_tmus[tmu].nccTables,
                                                    m_tmus[tmu].nccTablesLoaded,
                                                    m_tmus[tmu].activeNccTable);

  // If a paletted/NCC texture was previously swizzled with a different table,
  // re-swizzle its existing levels first so partial rows don't mix palettes.
  if (tex->isPaletted || tex->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  tex->rawDataHash = 0;  // Partial edit invalidates full-mipmap raw hash

  // Update swizzled mip levels
  auto& levelPixels = tex->swizzledMipLevels[lodIdx];

  for (size_t pIdx = 0; pIdx < partialTexels; ++pIdx) {
    size_t destIdx = startRow * w + pIdx;
    uint32_t a = 255;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;

    if (format >= 0x10) {
      const auto* p = reinterpret_cast<const uint32_t*>(rawData) + pIdx;
      uint32_t val = *p;
      a = (val >> 24) & 0xFF;
      r = (val >> 16) & 0xFF;
      g = (val >> 8) & 0xFF;
      b = val & 0xFF;
      levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
    } else if (format >= 0x8) {
      const auto* p = reinterpret_cast<const uint16_t*>(rawData) + pIdx;
      uint16_t val = *p;
      if (format == 0xa) {
        r = ((val >> 11) & 0x1F) * 255 / 31;
        g = ((val >> 5) & 0x3F) * 255 / 63;
        b = (val & 0x1F) * 255 / 31;
        a = 255;
      } else if (format == 0xc) {
        a = ((val >> 12) & 0xF) * 255 / 15;
        r = ((val >> 8) & 0xF) * 255 / 15;
        g = ((val >> 4) & 0xF) * 255 / 15;
        b = (val & 0xF) * 255 / 15;
      } else if (format == 0xb) {
        a = (val & 0x8000) ? 255 : 0;
        r = ((val >> 10) & 0x1F) * 255 / 31;
        g = ((val >> 5) & 0x1F) * 255 / 31;
        b = (val & 0x1F) * 255 / 31;
      } else if (format == 0xd) {
        a = (val >> 8) & 0xFF;
        r = val & 0xFF;
        g = r;
        b = r;
      } else if (format == 0x8) {
        a = (val >> 8) & 0xFF;
        r = ((val >> 5) & 0x7) * 255 / 7;
        g = ((val >> 2) & 0x7) * 255 / 7;
        b = (val & 0x3) * 255 / 3;
      } else if (format == 0x9) {
        if (lodIdx < tex->rawAyiqMipLevels.size()) {
          tex->rawAyiqMipLevels[lodIdx][destIdx] = val;
        }
        levelPixels[destIdx] = swizzleAyiq(val, activeNcc);
      } else if (format == 0xe) {
        if (lodIdx < tex->rawAp88MipLevels.size()) {
          tex->rawAp88MipLevels[lodIdx][destIdx] = val;
        }
        if (m_tmus[tmu].paletteTableLoaded) {
          levelPixels[destIdx] = swizzleAp88(val, m_tmus[tmu].paletteTable);
        } else {
          uint8_t alphaVal = (val >> 8) & 0xFF;
          uint8_t indexVal = val & 0xFF;
          levelPixels[destIdx] =
              (alphaVal << 24) | (indexVal << 16) | (indexVal << 8) | indexVal;
        }
      }
      if (format != 0x9 && format != 0xe) {
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    } else {
      uint8_t val = rawData[pIdx];
      if (format == 0x2) {
        a = val;
        r = val;
        g = val;
        b = val;
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      } else if (format == 0x3) {
        r = val;
        g = val;
        b = val;
        a = 255;
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      } else if (format == 0x5 || format == 0x6) {
        if (lodIdx < tex->raw8BitMipLevels.size()) {
          tex->raw8BitMipLevels[lodIdx][destIdx] = val;
        }
        if (m_tmus[tmu].paletteTableLoaded) {
          levelPixels[destIdx] = m_tmus[tmu].paletteTable[val];
        } else {
          levelPixels[destIdx] = (255u << 24) | (val << 16) | (val << 8) | val;
        }
      } else if (format == 0x0) {
        r = ((val >> 5) & 0x7) * 255 / 7;
        g = ((val >> 2) & 0x7) * 255 / 7;
        b = (val & 0x3) * 255 / 3;
        a = 255;
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      } else if (format == 0x1) {
        if (lodIdx < tex->rawYiqMipLevels.size()) {
          tex->rawYiqMipLevels[lodIdx][destIdx] = val;
        }
        levelPixels[destIdx] = swizzleYiq(val, activeNcc);
      } else if (format == 0x4) {
        a = ((val >> 4) & 0xF) * 255 / 15;
        uint32_t iVal = (val & 0xF) * 255 / 15;
        r = iVal;
        g = iVal;
        b = iVal;
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      } else {
        r = val;
        g = val;
        b = val;
        a = 255;
        levelPixels[destIdx] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
  }

  // Call the backend to perform the partial (or full, if palette changed) GPU upload
  if (auto* b = BackendManager::GetInstance().GetBackend()) {
    if (!tex->uploadedToBackend ||
        ((tex->isPaletted || tex->isNcc) &&
         tex->uploadedTableHash != tex->swizzledTableHash)) {
      TelemetryManager::GetInstance().RecordTexUpload(
          CalcSwizzledUploadBytes(*tex));
      b->UploadTexture(tmu, startAddress, *tex);
      tex->uploadedToBackend = true;
      tex->uploadedTableHash = tex->swizzledTableHash;
    } else {
      TelemetryManager::GetInstance().RecordTexUpload(
          static_cast<uint64_t>(partialTexels) * sizeof(uint32_t));
      b->UploadTexturePartial(tmu, startAddress, *tex, lodIdx, startRow,
                              endRow);
    }
  }

  return true;
}
bool TextureManager::DownloadTable(uint32_t tmu, uint32_t type,
                                   const void* data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || !data) return false;

  // type can be: GR_TEXTABLE_NCC0 (0), GR_TEXTABLE_NCC1 (1),
  // GR_TEXTABLE_PALETTE (2), or GR_TEXTABLE_PALETTE_6666_EXT (3)
  if (type > 3) {
    GLIDE_LOG(WARN, "TextureManager",
              "Unsupported texture table type: " << type << ". Skipping.");
    return false;
  }

  if (type == 0 || type == 1) {
    const auto* incomingNcc = reinterpret_cast<const GuNccTable*>(data);
    if (m_tmus[tmu].nccTablesLoaded[type] &&
        std::memcmp(&m_tmus[tmu].nccTables[type], incomingNcc,
                    sizeof(GuNccTable)) == 0) {
      return true;  // Identical NCC table — zero work!
    }

    const bool wasLoaded = m_tmus[tmu].nccTablesLoaded[type];
    m_tmus[tmu].nccTables[type] = *incomingNcc;
    m_tmus[tmu].nccTablesLoaded[type] = true;
    m_tmus[tmu].nccHashes[type] = HashTableBytes(incomingNcc, sizeof(GuNccTable));

    if (m_tmus[tmu].activeNccTable == type) {
      m_tmuTableDirty[tmu].store(true, std::memory_order_release);
      s_anyTableDirty.store(true, std::memory_order_release);
      EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);
      if (!wasLoaded) {
        // First-ever NCC table load: decode any pre-downloaded NCC textures
        // on CPU immediately (GPU upload remains lazy on bind/draw).
        int swizzledCount = 0;
        for (auto& pair : m_tmus[tmu].textures) {
          auto& tex = pair.second;
          if (tex->isNcc && !tex->swizzledWithTable) {
            if (tex.use_count() > 1) {
              tex = std::make_shared<VirtualTexture>(*tex);
            }
            ReswizzleTexture_Locked(m_tmus[tmu], *tex);
            swizzledCount++;
          }
        }
        GLIDE_LOG(DEBUG, "TextureManager",
                  "Initial NCC" << type << " download on TMU" << tmu
                                << " decoded " << swizzledCount
                                << " queued textures (GPU upload deferred).");
      } else {
        GLIDE_LOG(DEBUG, "TextureManager",
                  "Updated NCC" << type << " table on TMU" << tmu
                                << " (lazy per-bind swizzle enabled).");
      }
    }
  } else if (type == 2 || type == 3) {
    uint32_t newPalette[256];
    const auto* paletteData = reinterpret_cast<const uint32_t*>(data);
    if (type == 2) {
      // GR_TEXTABLE_PALETTE entries are 24-bit 0x00RRGGBB; top byte is unused
      // by hardware and P_8 texels have implicit opaque alpha (0xFF).
      for (int i = 0; i < 256; ++i) {
        newPalette[i] = paletteData[i] | 0xFF000000u;
      }
    } else {
      for (int i = 0; i < 256; ++i) {
        uint32_t val = paletteData[i];
        newPalette[i] =
            (((val & 0x3F) << 2) | ((val >> 4) & 0x3)) |
            ((((val >> 6) & 0x3F) << 10) | (((val >> 10) & 0x3) << 8)) |
            ((((val >> 12) & 0x3F) << 18) | (((val >> 16) & 0x3) << 16)) |
            ((((val >> 18) & 0x3F) << 26) | (((val >> 22) & 0x3) << 24));
      }
    }

    if (m_tmus[tmu].paletteTableLoaded &&
        std::memcmp(m_tmus[tmu].paletteTable, newPalette, sizeof(newPalette)) ==
            0) {
      return true;  // Identical palette — zero work!
    }

    const bool wasLoaded = m_tmus[tmu].paletteTableLoaded;
    std::memcpy(m_tmus[tmu].paletteTable, newPalette, sizeof(newPalette));
    m_tmus[tmu].paletteTableLoaded = true;
    m_tmus[tmu].paletteHash = HashTableBytes(newPalette, sizeof(newPalette));
    m_tmuTableDirty[tmu].store(true, std::memory_order_release);
    s_anyTableDirty.store(true, std::memory_order_release);
    EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);

    if (!wasLoaded) {
      // First-ever palette load: decode any pre-downloaded paletted textures
      // on CPU immediately (GPU upload remains lazy on bind/draw).
      int swizzledCount = 0;
      for (auto& pair : m_tmus[tmu].textures) {
        auto& tex = pair.second;
        if (tex->isPaletted && !tex->swizzledWithTable) {
          if (tex.use_count() > 1) {
            tex = std::make_shared<VirtualTexture>(*tex);
          }
          ReswizzleTexture_Locked(m_tmus[tmu], *tex);
          swizzledCount++;
        }
      }
      GLIDE_LOG(DEBUG, "TextureManager",
                "Initial palette download (type=" << type << ") on TMU" << tmu
                                                  << " decoded "
                                                  << swizzledCount
                                                  << " queued textures (GPU upload deferred).");
    } else {
      GLIDE_LOG(DEBUG, "TextureManager",
                "Updated palette (type=" << type << ") on TMU" << tmu
                                         << " (lazy per-bind swizzle enabled).");
    }
  }

  return true;
}

void TextureManager::SetActiveNccTable(uint32_t tmu, uint32_t table) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || table >= 2) return;

  if (m_tmus[tmu].activeNccTable == table) return;  // No change

  m_tmus[tmu].activeNccTable = table;
  m_tmuTableDirty[tmu].store(true, std::memory_order_release);
  s_anyTableDirty.store(true, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(true, std::memory_order_release);
  GLIDE_LOG(DEBUG, "TextureManager",
            "TMU" << tmu << " active NCC table switched to: NCC" << table
                  << " (lazy per-bind swizzle enabled).");
}

void TextureManager::SetTmuMemoryLimitMb(uint32_t tmu, uint32_t limitMb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu < 2) {
    m_tmuMemoryLimits[tmu] = limitMb * 1024 * 1024;
    GLIDE_LOG(
        INFO, "TextureManager",
        "Configured TMU" << tmu << " silicon budget to " << limitMb << " MB.");
  }
}

VirtualTexture* TextureManager::GetTexture(uint32_t tmu,
                                           uint32_t startAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return nullptr;
  auto it = m_tmus[tmu].textures.find(startAddress);
  if (it == m_tmus[tmu].textures.end()) return nullptr;
  if (it->second->isPaletted || it->second->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  return it->second.get();
}

std::shared_ptr<const VirtualTexture> TextureManager::GetTextureSnapshot(
    uint32_t tmu, uint32_t startAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2 || startAddress == 0xFFFFFFFFu) return nullptr;
  auto it = m_tmus[tmu].textures.find(startAddress);
  if (it == m_tmus[tmu].textures.end()) return nullptr;
  if (it->second->isPaletted || it->second->isNcc) {
    EnsureTextureReady_Locked(tmu, it->second, false);
  }
  return it->second;
}

void TextureManager::PrepareTextureForBind(uint32_t tmu,
                                           uint32_t startAddress) {
  GLIDE_CPU_SCOPE(CpuBucket::Tex);
  std::lock_guard<std::mutex> lock(m_mutex);
  if (tmu >= 2) return;
  m_tmus[tmu].boundAddress = startAddress;
  if (startAddress == 0xFFFFFFFFu) return;
  auto it = m_tmus[tmu].textures.find(startAddress);
  if (it != m_tmus[tmu].textures.end()) {
    EnsureTextureReady_Locked(tmu, it->second, true);
  }
}

void TextureManager::SyncBoundTextures() {
  GLIDE_CPU_SCOPE(CpuBucket::Tex);
  std::lock_guard<std::mutex> lock(m_mutex);
  s_anyTableDirty.store(false, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(false, std::memory_order_release);
  for (uint32_t tmu = 0; tmu < 2; ++tmu) {
    if (!m_tmuTableDirty[tmu].exchange(false, std::memory_order_acq_rel)) {
      continue;
    }
    const uint32_t addr = m_tmus[tmu].boundAddress;
    if (addr == 0xFFFFFFFFu) continue;
    auto it = m_tmus[tmu].textures.find(addr);
    if (it != m_tmus[tmu].textures.end()) {
      EnsureTextureReady_Locked(tmu, it->second, true);
    }
  }
}

void TextureManager::Reset() {
  std::lock_guard<std::mutex> lock(m_mutex);
  s_anyTableDirty.store(false, std::memory_order_release);
  EntryCommon::g_anyTmuTableDirty.store(false, std::memory_order_release);
  for (int i = 0; i < 2; ++i) {
    m_tmus[i].allocatedBytes = 0;
    m_tmus[i].textures.clear();
    m_tmus[i].paletteTableLoaded = false;
    m_tmus[i].paletteHash = 0;
    std::memset(m_tmus[i].paletteTable, 0, sizeof(m_tmus[i].paletteTable));
    std::memset(m_tmus[i].nccTables, 0, sizeof(m_tmus[i].nccTables));
    m_tmus[i].nccTablesLoaded[0] = false;
    m_tmus[i].nccTablesLoaded[1] = false;
    m_tmus[i].nccHashes[0] = 0;
    m_tmus[i].nccHashes[1] = 0;
    m_tmus[i].activeNccTable = 0;
    m_tmus[i].boundAddress = 0xFFFFFFFFu;
    m_tmuTableDirty[i].store(false, std::memory_order_release);
  }
  GLIDE_LOG(INFO, "TextureManager",
            "Purged and reset all Virtual TMU state pools.");
}

}  // namespace GlideWrapper
