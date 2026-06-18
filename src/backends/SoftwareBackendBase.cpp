#include "SoftwareBackendBase.h"
#include "core/Logger.h"
#include "core/TextureManager.h"
#include "core/Telemetry.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <SDL2/SDL.h>

extern "C" {
    volatile int g_glideWrapperSdlKeyHit = 0;
}

namespace GlideWrapper {

void SoftwareBackendBase::ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    GLIDE_LOG(DEBUG, "Rasterizer", "ClearBuffer CPU side invoked: color=0x" << std::hex << color << ", alpha=" << alpha << ", mask=" << clearMask << std::dec << ", map=" << m_headlessPixelMap);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (clearMask & 0x1) { // Color Clear
        uint32_t a = (alpha > 0) ? alpha : 255;
        uint32_t r = (color >> 16) & 0xFF;
        uint32_t g = (color >> 8) & 0xFF;
        uint32_t b = color & 0xFF;

        if (m_pixelFormatOverride == 1) { // ABGR format override
            std::swap(r, b);
        }

        uint32_t clearWord = (a << 24) | (r << 16) | (g << 8) | b;
        m_lastCpuClearColor = clearWord & 0x00FFFFFF; // Track clear color with alpha = 0!
        uint32_t* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        std::fill_n(pixels, m_headlessWidth * m_headlessHeight, clearWord);
        GLIDE_LOG(DEBUG, "Rasterizer", "Cleared color buffer with word 0x" << std::hex << clearWord << std::dec);
    }

    if (clearMask & 0x2) { // Depth Clear
        if (!m_headlessDepthBuffer.empty()) {
            std::fill(m_headlessDepthBuffer.begin(), m_headlessDepthBuffer.end(), z);
            GLIDE_LOG(DEBUG, "Rasterizer", "Cleared depth buffer with value " << z);
        }
    }
}

void SoftwareBackendBase::SetConstantColor(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_constantColor = color;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetConstantColor: 0x" << std::hex << color << std::dec);
}

void SoftwareBackendBase::SetPixelFormat(uint32_t format) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_pixelFormatOverride = format;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetPixelFormat active override format: " << format);
}

void SoftwareBackendBase::SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_depthMode = depthMode;
    m_depthCompareOp = compareOp;
    m_depthMask = depthMask;
    m_depthBiasLevel = biasLevel;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetDepthState: Mode=" << depthMode << ", Compare=" << compareOp << ", Mask=" << depthMask);
}

void SoftwareBackendBase::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_rgbSrcBlend = rgbSrcFactor;
    m_rgbDstBlend = rgbDstFactor;
    m_alphaSrcBlend = alphaSrcFactor;
    m_alphaDstBlend = alphaDstFactor;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetBlendState: Src=" << rgbSrcFactor << ", Dst=" << rgbDstFactor);
}

void SoftwareBackendBase::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_alphaTestOp = compareOp;
    m_alphaTestRefVal = refVal;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetAlphaTestState: Op=" << compareOp << ", Ref=" << refVal);
}

void SoftwareBackendBase::SetCullState(uint32_t cullMode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_cullMode = cullMode;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetCullState: Mode=" << cullMode);
}

void SoftwareBackendBase::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_clipMinX = minX;
    m_clipMinY = minY;
    m_clipMaxX = std::min(m_headlessWidth, maxX);
    m_clipMaxY = std::min(m_headlessHeight, maxY);
    GLIDE_LOG(DEBUG, "Rasterizer", "SetClipWindow: (" << m_clipMinX << "," << m_clipMinY << ") to (" << m_clipMaxX << "," << m_clipMaxY << ")");
}

void SoftwareBackendBase::SetSstOrigin(uint32_t origin) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_sstOrigin = origin;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetSstOrigin: " << origin);
}

void SoftwareBackendBase::SetColorMask(bool rgb, bool alpha) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_colorMaskRgb = rgb;
    m_colorMaskAlpha = alpha;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetColorMask: RGB=" << rgb << ", Alpha=" << alpha);
}

void SoftwareBackendBase::SetFogMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fogMode = mode;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetFogMode: " << mode);
}

void SoftwareBackendBase::SetFogColor(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fogColor = color;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetFogColor: 0x" << std::hex << color << std::dec);
}

void SoftwareBackendBase::SetFogTable(const uint8_t* table) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (table) {
        std::memcpy(m_fogTable, table, sizeof(m_fogTable));
        GLIDE_LOG(DEBUG, "Rasterizer", "SetFogTable: 64 entries uploaded.");
    }
}

void SoftwareBackendBase::BindTexture(uint32_t tmu, uint32_t startAddress, uint32_t clampS, uint32_t clampT, uint32_t minFilter, uint32_t magFilter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu < 2) {
        m_boundTexAddress[tmu] = startAddress;
        m_texClampS[tmu] = clampS;
        m_texClampT[tmu] = clampT;
        m_texMinFilter[tmu] = minFilter;
        m_texMagFilter[tmu] = magFilter;
        GLIDE_LOG(DEBUG, "Rasterizer", "BindTexture TMU" << tmu << ": Address=0x" << std::hex << startAddress << std::dec 
                                  << ", Clamp(" << clampS << "," << clampT << "), Filter(" << minFilter << "," << magFilter << ")");
    }
}

void SoftwareBackendBase::UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) {
    // Software backend doesn't need physical GPU uploads; it queries TextureManager directly.
    GLIDE_LOG(DEBUG, "Rasterizer", "UploadTexture (Software) TMU" << tmu << ": Address=0x" << std::hex << startAddress << std::dec << " registered.");
}

void SoftwareBackendBase::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t colorLocal, uint32_t colorOther, bool colorInvert,
                                         uint32_t alphaFunc, uint32_t alphaFactor, uint32_t alphaLocal, uint32_t alphaOther, bool alphaInvert) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_colorCombinerFunc   = colorFunc;
    m_colorCombinerFactor = colorFactor;
    m_colorCombinerLocal  = colorLocal;
    m_colorCombinerOther  = colorOther;
    m_colorCombinerInvert = colorInvert;
    m_alphaCombinerFunc   = alphaFunc;
    m_alphaCombinerFactor = alphaFactor;
    m_alphaCombinerLocal  = alphaLocal;
    m_alphaCombinerOther  = alphaOther;
    m_alphaCombinerInvert = alphaInvert;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetCombinerMode: Color(Func=" << colorFunc << ", Fac=" << colorFactor << ", Loc=" << colorLocal << ", Oth=" << colorOther << ", Inv=" << colorInvert 
                                  << "), Alpha(Func=" << alphaFunc << ", Fac=" << alphaFactor << ", Loc=" << alphaLocal << ", Oth=" << alphaOther << ", Inv=" << alphaInvert << ")");
}

void SoftwareBackendBase::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu < 2) {
        m_texCombinerRgbFunc[tmu]    = rgbFunc;
        m_texCombinerRgbFactor[tmu]  = rgbFactor;
        m_texCombinerAlphaFunc[tmu]  = alphaFunc;
        m_texCombinerAlphaFactor[tmu]= alphaFactor;
        GLIDE_LOG(DEBUG, "Rasterizer", "SetTexCombinerMode TMU" << tmu << ": RGBFunc=" << rgbFunc << ", AlphaFunc=" << alphaFunc);
    }
}

void SoftwareBackendBase::SetSTWHintState(uint32_t hintMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_stwHintMask = hintMask;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetSTWHintState: Mask=" << hintMask);
}

void SoftwareBackendBase::SetChromakeyMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_chromakeyMode = mode;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetChromakeyMode: Mode=" << mode);
}

void SoftwareBackendBase::SetChromakeyValue(uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_chromakeyValue = value;
    GLIDE_LOG(DEBUG, "Rasterizer", "SetChromakeyValue: Value=0x" << std::hex << value << std::dec);
}

// Note: DrawTriangle, DrawLine, DrawPoint, EvaluateCombinerColor, and RasterizeSoftwareTriangle
// have been relocated to the concrete SoftwareBackend subclass (SoftwareBackend.cpp).

void* SoftwareBackendBase::GetLfbWritePtr() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lfbDirty = true;
    m_lfbBufferDirty[0] = true;
    m_lfbBufferDirty[1] = true;
    return m_headlessPixelMap;
}

bool SoftwareBackendBase::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!dstData || m_cpuBuffers[0].empty()) return false;

    const uint32_t* pixels = nullptr;
    if (buffer == 0) { // FRONTBUFFER
        pixels = reinterpret_cast<const uint32_t*>(m_cpuBuffers[m_frontBufferIdx].data());
    } else { // BACKBUFFER
        pixels = reinterpret_cast<const uint32_t*>(m_cpuBuffers[m_backBufferIdx].data());
    }
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);

    bool is32Bit = (dstStride >= srcWidth * 4);
    GLIDE_LOG(DEBUG, "ReadLFB", "ReadLFB invoked: srcX=" << srcX << ", srcY=" << srcY << ", srcWidth=" << srcWidth << ", srcHeight=" << srcHeight << ", dstStride=" << dstStride << ", is32Bit=" << is32Bit);

    for (uint32_t y = 0; y < srcHeight; ++y) {
        uint32_t readY = srcY + y;
        if (readY >= m_headlessHeight) break;

        if (is32Bit) {
            uint32_t* destRow32 = reinterpret_cast<uint32_t*>(outBytes + y * dstStride);
            for (uint32_t x = 0; x < srcWidth; ++x) {
                uint32_t readX = srcX + x;
                if (readX >= m_headlessWidth) break;
                uint32_t val = pixels[readY * m_headlessWidth + readX];
                if ((readX == 150 && readY == 150) || (readX == 310 && readY == 310)) {
                    GLIDE_LOG(DEBUG, "ReadLFB", "ReadLFB (32-bit) at (" << readX << "," << readY << ") = 0x" << std::hex << val << std::dec);
                }
                destRow32[x] = val;
            }
        } else {
            uint16_t* destRow16 = reinterpret_cast<uint16_t*>(outBytes + y * dstStride);
            for (uint32_t x = 0; x < srcWidth; ++x) {
                uint32_t readX = srcX + x;
                if (readX >= m_headlessWidth) break;

                uint32_t pWord = pixels[readY * m_headlessWidth + readX];
                uint16_t r = (((pWord >> 16) & 0xFF) * 31 + 127) / 255;
                uint16_t g = (((pWord >> 8)  & 0xFF) * 63 + 127) / 255;
                uint16_t b = ((pWord         & 0xFF) * 31 + 127) / 255;
                uint16_t c16 = (r << 11) | (g << 5) | b;
                destRow16[x] = c16;
            }
        }
    }
    return true;
}

bool SoftwareBackendBase::WriteLFB(uint32_t buffer, uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData, const void* refData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!srcData || m_cpuBuffers[0].empty()) return false;

    // Validate LFB source format (must be one of the 9 defined Glide formats)
    bool isValidFormat = false;
    switch (srcFmt) {
        case 0:  // GR_LFB_SRC_FMT_565
        case 1:  // GR_LFB_SRC_FMT_555
        case 2:  // GR_LFB_SRC_FMT_1555
        case 4:  // GR_LFB_SRC_FMT_888
        case 5:  // GR_LFB_SRC_FMT_8888
        case 12: // GR_LFB_SRC_FMT_565_DEPTH
        case 13: // GR_LFB_SRC_FMT_555_DEPTH
        case 14: // GR_LFB_SRC_FMT_1555_DEPTH
        case 15: // GR_LFB_SRC_FMT_ZA16
            isValidFormat = true;
            break;
        default:
            isValidFormat = false;
            break;
    }

    if (!isValidFormat) {
        GLIDE_LOG(WARN, "WriteLFB", "Invalid or undefined LFB source format: " << srcFmt);
        return false;
    }

    uint32_t* pixels = nullptr;
    if (buffer == 0) { // FRONTBUFFER
        pixels = reinterpret_cast<uint32_t*>(m_cpuBuffers[m_frontBufferIdx].data());
    } else { // BACKBUFFER
        pixels = reinterpret_cast<uint32_t*>(m_cpuBuffers[m_backBufferIdx].data());
    }
    const auto* inBytes = reinterpret_cast<const uint8_t*>(srcData);
    const auto* refBytes = reinterpret_cast<const uint8_t*>(refData);

    bool wrotePixel = false;
    for (uint32_t y = 0; y < srcHeight; ++y) {
        uint32_t writeY = dstY + y;
        if (writeY >= m_headlessHeight) break;

        for (uint32_t x = 0; x < srcWidth; ++x) {
            uint32_t writeX = dstX + x;
            if (writeX >= m_headlessWidth) break;

            // 1. If refData is provided, check if the pixel has changed.
            // If it hasn't changed, skip writing it completely (preserving GPU-rendered pixels!)
            if (refBytes) {
                if (srcFmt <= 2) { // 16-bit formats (565, 555, 1555)
                    uint16_t c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                    uint16_t ref16 = reinterpret_cast<const uint16_t*>(refBytes + y * srcStride)[x];
                    if (c16 == ref16) {
                        continue;
                    }
                } else { // 32-bit formats
                    uint32_t c32 = reinterpret_cast<const uint32_t*>(inBytes + y * srcStride)[x];
                    uint32_t ref32 = reinterpret_cast<const uint32_t*>(refBytes + y * srcStride)[x];
                    if (c32 == ref32) {
                        continue;
                    }
                }
            }

            // 2. Unpack and write the modified pixel
            uint32_t pWord = 0xFF000000;
            if (srcFmt == 0 || srcFmt == 12) { // GR_LFB_SRC_FMT_565 or GR_LFB_SRC_FMT_565_DEPTH
                uint16_t c16;
                if (srcFmt == 12) {
                    c16 = (uint16_t)(reinterpret_cast<const uint32_t*>(inBytes + y * srcStride)[x] & 0xFFFF);
                } else {
                    c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                }
                uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
                uint32_t g = ((c16 >> 5)  & 0x3F) * 255 / 63;
                uint32_t b = (c16         & 0x1F) * 255 / 31;
                pWord = (255 << 24) | (r << 16) | (g << 8) | b;
            } else if (srcFmt == 1 || srcFmt == 2 || srcFmt == 13 || srcFmt == 14) { // 555 / 1555 or 555_DEPTH / 1555_DEPTH
                uint16_t c16;
                if (srcFmt == 13 || srcFmt == 14) {
                    c16 = (uint16_t)(reinterpret_cast<const uint32_t*>(inBytes + y * srcStride)[x] & 0xFFFF);
                } else {
                    c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                }
                uint32_t r = ((c16 >> 10) & 0x1F) * 255 / 31;
                uint32_t g = ((c16 >> 5)  & 0x1F) * 255 / 31;
                uint32_t b = (c16         & 0x1F) * 255 / 31;
                uint32_t a = (srcFmt == 2 || srcFmt == 14) ? (((c16 >> 15) & 1) ? 255 : 0) : 255;
                pWord = (a << 24) | (r << 16) | (g << 8) | b;
            } else { // 32-bit ARGB8888 / RGB888 / ZA16
                pWord = reinterpret_cast<const uint32_t*>(inBytes + y * srcStride)[x];
                
                // Quantize to 16-bit 565 to emulate Voodoo VRAM storage limits
                uint32_t r = ((pWord >> 16) & 0xFF) >> 3;
                uint32_t g = ((pWord >> 8)  & 0xFF) >> 2;
                uint32_t b = (pWord         & 0xFF) >> 3;
                
                uint32_t r8 = r * 255 / 31;
                uint32_t g8 = g * 255 / 63;
                uint32_t b8 = b * 255 / 31;
                uint32_t a8 = (pWord >> 24) & 0xFF;
                
                if (srcFmt == 4 || srcFmt == 15) { // Force alpha = 255 for RGB888 and ZA16 to make them opaque
                    a8 = 255;
                }
                pWord = (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
            }
            pixels[writeY * m_headlessWidth + writeX] = pWord;
            wrotePixel = true;
        }
    }
    if (wrotePixel) {
        m_lfbDirty = true;
        if (buffer == 0) { // FRONTBUFFER
            m_lfbBufferDirty[0] = true;
        } else { // BACKBUFFER
            m_lfbBufferDirty[1] = true;
        }
    }
    return true;
}

void SoftwareBackendBase::PollEvents() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            GLIDE_LOG(INFO, "Backend", "SDL window quit request received.");
        } else if (event.type == SDL_KEYDOWN) {
            GLIDE_LOG(DEBUG, "Backend", "SDL window keydown event captured! Setting keyboard hit hook flag.");
            g_glideWrapperSdlKeyHit = 1;
        }
    }
}

void SoftwareBackendBase::SstIdle() {
    // Software rasterizer is completely synchronous; all drawing dispatches 
    // are completed on the host CPU thread during the draw call itself.
}

void SoftwareBackendBase::AllocateCpuBuffers(uint32_t width, uint32_t height) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_cpuBuffers[0].resize(width * height * 4);
    m_cpuBuffers[1].resize(width * height * 4);
    std::memset(m_cpuBuffers[0].data(), 0, m_cpuBuffers[0].size());
    std::memset(m_cpuBuffers[1].data(), 0, m_cpuBuffers[1].size());
    m_frontBufferIdx = 0;
    m_backBufferIdx = 1;
    m_activeRenderBuffer = 1; // BACKBUFFER
    m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
    GLIDE_LOG(INFO, "SoftwareBackendBase", "Allocated CPU double buffers: " << width << "x" << height << ", map=" << m_headlessPixelMap);
}

void SoftwareBackendBase::FreeCpuBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_cpuBuffers[0].clear();
    m_cpuBuffers[0].shrink_to_fit();
    m_cpuBuffers[1].clear();
    m_cpuBuffers[1].shrink_to_fit();
    m_headlessPixelMap = nullptr;
    GLIDE_LOG(INFO, "SoftwareBackendBase", "Freed CPU double buffers.");
}

void SoftwareBackendBase::SetRenderBuffer(uint32_t target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_activeRenderBuffer = target;
    if (m_activeRenderBuffer == 0) { // GR_BUFFER_FRONTBUFFER
        m_headlessPixelMap = m_cpuBuffers[m_frontBufferIdx].data();
    } else { // GR_BUFFER_BACKBUFFER (and others default to back)
        m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
    }
    GLIDE_LOG(INFO, "SoftwareBackendBase", "SetRenderBuffer: target=" << target << ", map=" << m_headlessPixelMap);
}

} // namespace GlideWrapper
