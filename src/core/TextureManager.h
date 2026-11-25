#pragma once

#include <glide.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/HashUtils.h"

#include "core/Logger.h"

namespace GlideWrapper {

struct VirtualTexture {
  static constexpr int32_t kNoDeclaredLargeLod =
      std::numeric_limits<int32_t>::min();

  uint32_t startAddress{0};
  uint32_t format{0};
  uint32_t aspectRatio{0};
  uint32_t baseWidth{0};
  uint32_t baseHeight{0};
  int32_t smallLod{0};
  int32_t largeLod{0};
  int32_t declaredLargeLod{kNoDeclaredLargeLod};
  uint32_t allocatedByteSize{0};
  std::vector<std::vector<uint32_t>>
      swizzledMipLevels;  // Completely swizzled into 32-bit ARGB (0xAARRGGBB)
                          // per level
  std::vector<std::vector<uint8_t>>
      rawCompressedMipLevels;  // Raw BC1/BC2/BC3 (DXT1..DXT5) blocks per level
                               // for zero-CPU-decode hardware GPU upload
  std::vector<std::vector<uint8_t>>
      raw8BitMipLevels;  // Stored for deferred P8 palettization

  // Staging vectors for YIQ/AYIQ and AP_88 formats (deferred swizzling)
  std::vector<std::vector<uint8_t>>
      rawYiqMipLevels;  // For 8-bit YIQ_422 (format 0x1)
  std::vector<std::vector<uint16_t>>
      rawAyiqMipLevels;  // For 16-bit AYIQ_8422 (format 0x9)
  std::vector<std::vector<uint16_t>>
      rawAp88MipLevels;  // For 16-bit AP_88 (format 0xe)
  std::vector<std::vector<uint8_t>>
      rawUncompressedMipLevels;  // Compact raw bytes kept on GPU cache hits
                                 // for non-paletted/non-NCC formats so CPU
                                 // swizzledMipLevels can materialize lazily.

  bool isPaletted = false;
  bool isNcc = false;
  uint64_t rawDataHash{0};
  uint64_t swizzledTableHash{0};
  uint64_t uploadedTableHash{0};
  bool swizzledWithTable{false};
  bool uploadedToBackend{false};
  bool uploadedToBackendPerTmu[2]{false, false};
  uint64_t uploadedTableHashPerTmu[2]{0, 0};
};

// Shared canonical mip-count and content-hash helpers used by Vulkan, OpenGL ES,
// and Software content-addressed texture caches so metadata-only probes
// (empty swizzledMipLevels) match full uploads byte-for-byte.
inline uint32_t ResolveVirtualTextureMipCount(const VirtualTexture& tex,
                                              bool useCompressed) {
  if (useCompressed && !tex.rawCompressedMipLevels.empty()) {
    return static_cast<uint32_t>(tex.rawCompressedMipLevels.size());
  }
  if (!useCompressed && !tex.swizzledMipLevels.empty()) {
    return static_cast<uint32_t>(tex.swizzledMipLevels.size());
  }
  if (tex.smallLod >= tex.largeLod) {
    return static_cast<uint32_t>(tex.smallLod - tex.largeLod + 1);
  }
  return 0u;
}

inline uint64_t CalculateVirtualTextureHash(const VirtualTexture& tex,
                                            uint32_t mipCount) {
  uint64_t hash = Hash::kFnv1aOffsetBasis64;
  const uint32_t header[4] = {tex.baseWidth, tex.baseHeight, mipCount,
                              tex.format};
  hash = Hash::MixBytes64(hash, reinterpret_cast<const uint8_t*>(header),
                          sizeof(header));

  if (tex.rawDataHash != 0) {
    const uint64_t fastData[3] = {tex.swizzledWithTable ? 1ULL : 0ULL,
                                  tex.rawDataHash, tex.swizzledTableHash};
    hash = Hash::MixBytes64(hash, reinterpret_cast<const uint8_t*>(fastData),
                            sizeof(fastData));
    return Hash::NonZeroHash64(hash);
  }

  if (!tex.swizzledMipLevels.empty()) {
    for (const auto& level : tex.swizzledMipLevels) {
      const auto* byteData = reinterpret_cast<const uint8_t*>(level.data());
      hash = Hash::MixBytes64(hash, byteData, level.size() * sizeof(uint32_t));
    }
  } else {
    for (const auto& level : tex.rawCompressedMipLevels) {
      hash = Hash::MixBytes64(hash, level.data(), level.size());
    }
  }
  return Hash::NonZeroHash64(hash);
}

class TextureManager {
 public:
  static constexpr int32_t kNoBaseLargeLod =
      VirtualTexture::kNoDeclaredLargeLod;
  static constexpr uint32_t kMultibaseCompositeBase = 0xF0000000u;

  static TextureManager& GetInstance() {
    static TextureManager* instance = new TextureManager();
    return *instance;
  }

  // 1. Calculate rigorous memory footprints matching 3dfx silicon MIP-map
  // chains
  uint32_t CalculateMemoryRequired(int32_t lodMin, int32_t lodMax,
                                   uint32_t aspect, uint32_t format,
                                   bool packed = false);

  // 2. Decode retro formats across full MIP-map chain and upload into active
  // virtual TMU pool.
  // When replaceExisting is true (grTexDownloadMipMap), any existing texture at
  // startAddress or overlapping its TMU byte range is evicted cleanly.
  // When replaceExisting is false (grTexDownloadMipMapLevel), levels belonging
  // to the same baseLargeLod/format/aspect are merged using copy-on-write.
  bool DownloadMipMap(uint32_t tmu, uint32_t startAddress, int32_t lodMin,
                      int32_t lodMax, uint32_t aspect, uint32_t format,
                      const void* data, bool replaceExisting = true,
                      int32_t baseLargeLod = kNoBaseLargeLod);

  // Download a partial sub-region of a specific mipmap level
  bool DownloadMipMapPartial(uint32_t tmu, uint32_t startAddress,
                             int32_t thisLod, int32_t largeLod,
                             uint32_t aspect, uint32_t format, const void* data,
                             int startRow, int endRow);

  // Commit rendered 32-bit canonical ARGB (0xAARRGGBB) pixels from a
  // TEXTUREBUFFER render target into TMU storage and upload to the backend.
  bool CommitRenderedTexture(uint32_t tmu, uint32_t startAddress,
                             int32_t thisLod, int32_t largeLod,
                             uint32_t aspect, uint32_t format,
                             const std::vector<uint32_t>& canonicalArgb);

  // Zero-readback GPU-side TEXTUREBUFFER commit: keeps rendered pixels resident
  // on the GPU via IGraphicsBackend::CommitGpuTextureBuffer() and updates TMU
  // metadata. Returns false if the backend does not support GPU RTT or if the
  // target mip chain requires CPU fallback.
  bool CommitGpuRenderedTexture(uint32_t tmu, uint32_t startAddress,
                                int32_t thisLod, int32_t largeLod,
                                uint32_t aspect, uint32_t format,
                                uint32_t copyWidth, uint32_t copyHeight,
                                uint32_t origin);

  // Download a palette or NCC table to the specified TMU and trigger deferred
  // swizzling
  bool DownloadTable(uint32_t tmu, uint32_t type, const void* data);

  // Download an inclusive subrange [start..end] of a palette (or NCC table) to
  // the specified TMU, preserving palette entries outside [start..end].
  bool DownloadTablePartial(uint32_t tmu, uint32_t type, const void* data,
                            int start, int end);

  // Select the active NCC table (NCC0 or NCC1) for a TMU and trigger deferred
  // YIQ re-swizzling
  void SetActiveNccTable(uint32_t tmu, uint32_t table);

  // 3. Enforce silicon memory limits (e.g. 2MB or 4MB per TMU)
  void SetTmuMemoryLimitMb(uint32_t tmu, uint32_t limitMb);

  // TEXUMA (GR_TEXTURE_UMA_EXT): unified texture memory across TMU0 & TMU1
  void SetUmaMode(bool enabled);
  bool IsUmaMode() const {
    return m_umaEnabled.load(std::memory_order_acquire);
  }

  // grTexMultibase / grTexMultibaseAddress (canonical Glide 3 range:
  // 0=32_TO_1, 1=64, 2=128, 3=256, 4=256_TO_1, 5=512, 6=1024, 7=2048)
  void SetMultibase(uint32_t tmu, bool enabled);
  void SetMultibaseAddress(uint32_t tmu, uint32_t canonicalRange,
                           uint32_t startAddress);
  bool IsMultibaseEnabled(uint32_t tmu) const;
  uint32_t ResolveMultibaseSource(uint32_t tmu, uint32_t startAddress,
                                  int32_t glide2LargeLod,
                                  int32_t glide2SmallLod, uint32_t aspect,
                                  uint32_t format);

  // 4. Retrieve bound virtual textures for rendering or software sampler
  // verification (lazily re-swizzles on CPU if the active palette/NCC table
  // changed since this texture was last decoded).
  VirtualTexture* GetTexture(uint32_t tmu, uint32_t startAddress);

  // Return an immutable copy-on-write snapshot for deferred rasterizers
  // (SoftwareBackend tile bins), guaranteeing earlier draws in a frame are
  // never corrupted by subsequent TMU overwrites or palette swaps.
  std::shared_ptr<const VirtualTexture> GetTextureSnapshot(
      uint32_t tmu, uint32_t startAddress);

  // Lazily re-swizzle & upload ONLY the texture being bound when the active
  // palette or NCC table has changed (avoids O(N_resident) upload storms on
  // runtime palette swaps).
  void PrepareTextureForBind(uint32_t tmu, uint32_t startAddress);

  // Fast lock-free check before draw calls for games that change the active
  // palette/NCC table while keeping the same texture bound.
  bool HasPendingTableSync() const {
    return m_tmuTableDirty[0].load(std::memory_order_acquire) ||
           m_tmuTableDirty[1].load(std::memory_order_acquire);
  }
  static bool HasPendingTableSyncFast() noexcept {
    return s_anyTableDirty.load(std::memory_order_acquire);
  }
  void SyncBoundTextures();

  // Count of DownloadMipMap / EnsureTextureReady CPU decodes skipped via
  // backend content-addressed GPU texture cache hits (used by perf/unit tests).
  uint64_t GetSkippedCpuDecodeCount() const {
    return m_skippedCpuDecodeCount.load(std::memory_order_relaxed);
  }

  // 5. Purge TMU texture allocations while preserving hardware palette & NCC
  // tables (authentic guTexMemReset semantics).
  void ClearTextures();

  // 6. Full engine reset: purge TMU textures, palette/NCC tables, and UMA state.
  void Reset();

 private:
  TextureManager() {
    m_tmuMemoryLimits[0] = 2 * 1024 * 1024;  // 2MB default silicon ceiling
    m_tmuMemoryLimits[1] = 2 * 1024 * 1024;
  }
  ~TextureManager() = default;
  TextureManager(const TextureManager&) = delete;
  TextureManager& operator=(const TextureManager&) = delete;

  struct TmuState {
    uint32_t allocatedBytes = 0;
    std::unordered_map<uint32_t, std::shared_ptr<VirtualTexture>> textures;
    uint32_t paletteTable[256]{};
    bool paletteTableLoaded = false;
    uint64_t paletteHash = 0;

    // Two hardware NCC tables (NCC0 and NCC1)
    GuNccTable nccTables[2]{};
    bool nccTablesLoaded[2] = {false, false};
    uint64_t nccHashes[2] = {0, 0};
    uint32_t activeNccTable = 0;  // Default: NCC0 (0)
    uint32_t boundAddress = 0xFFFFFFFFu;
    bool multibaseEnabled = false;
    uint32_t multibaseAddr[8]{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                              0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                              0xFFFFFFFFu, 0xFFFFFFFFu};
  };

  uint32_t StorageTmu_Locked(uint32_t tmu) const {
    return m_umaEnabled.load(std::memory_order_relaxed) ? 0u : tmu;
  }
  const TmuState& TableState_Locked(uint32_t tmu) const {
    if (m_umaEnabled.load(std::memory_order_relaxed) && tmu == 1 &&
        !m_tmus[1].paletteTableLoaded &&
        !m_tmus[1].nccTablesLoaded[m_tmus[1].activeNccTable]) {
      return m_tmus[0];
    }
    return m_tmus[tmu];
  }

  static uint64_t GetActiveTableHash_Locked(const TmuState& state,
                                            const VirtualTexture& tex);
  static void ReswizzleTexture_Locked(const TmuState& state,
                                      VirtualTexture& tex);
  static void EnsureCompressedSwizzled_Locked(VirtualTexture& tex);
  static void EnsureUncompressedSwizzled_Locked(VirtualTexture& tex);
  void EnsureTextureReady_Locked(uint32_t tmu,
                                 std::shared_ptr<VirtualTexture>& texPtr,
                                 bool uploadToBackend);

  std::mutex m_mutex;
  TmuState m_tmus[2];
  uint32_t m_tmuMemoryLimits[2];
  std::atomic<bool> m_umaEnabled{false};
  std::atomic<bool> m_tmuTableDirty[2]{false, false};
  inline static std::atomic<bool> s_anyTableDirty{false};
  std::atomic<uint64_t> m_skippedCpuDecodeCount{0};
};

}  // namespace GlideWrapper
