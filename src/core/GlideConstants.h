#pragma once

#include <cstdint>

namespace GlideWrapper {

// Sentinel TMU start address indicating no texture is currently bound.
inline constexpr uint32_t kUnboundTexAddress = 0xFFFFFFFFu;

// Bit 31 of packed VSA-100 / Napalm extended combiner state words; when set,
// the extended combiner equation overrides legacy Glide 2.x/3.x combine mode.
inline constexpr uint32_t kExtCombineActiveBit = 0x80000000u;

// Glide LFB target buffer identifiers (mirrors GR_BUFFER_* ABI without pulling
// platform Glide headers into backend cores).
inline constexpr uint32_t kLfbBufferFront = 0u;  // GR_BUFFER_FRONTBUFFER
inline constexpr uint32_t kLfbBufferBack = 1u;   // GR_BUFFER_BACKBUFFER
inline constexpr uint32_t kLfbBufferAux = 2u;    // GR_BUFFER_AUXBUFFER

// Glide LFB 16-bit depth/alpha source format (GR_LFB_SRC_FMT_ZA16).
inline constexpr uint32_t kLfbSrcFmtZa16 = 0x0Fu;

// 24-bit unsigned normalized depth buffer scaling constants (2^24 - 1).
inline constexpr uint32_t kDepth24MaxU32 = 0x00FFFFFFu;  // 16777215
inline constexpr float kDepth24MaxF = 16777215.0f;
inline constexpr float kInvDepth24MaxF = 1.0f / kDepth24MaxF;

// Byte-replication multipliers for expanding an 8-bit intensity/alpha sample
// across packed 32-bit RGB (0x00RRGGBB) or RGBA (0xAARRGGBB) lanes.
inline constexpr uint32_t kRgbByteBroadcast = 0x00010101u;
inline constexpr uint32_t kRgbaByteBroadcast = 0x01010101u;

}  // namespace GlideWrapper
