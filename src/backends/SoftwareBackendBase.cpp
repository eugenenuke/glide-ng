#include "SoftwareBackendBase.h"
#include "core/Logger.h"
#include "core/TextureManager.h"
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
    GLIDE_LOG(INFO, "Rasterizer", "ClearBuffer CPU side invoked: color=0x" << std::hex << color << ", alpha=" << alpha << ", mask=" << clearMask << std::dec << ", map=" << m_headlessPixelMap);
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
    GLIDE_LOG(INFO, "Rasterizer", "SetPixelFormat active override format: " << format);
}

void SoftwareBackendBase::SetDepthState(uint32_t depthMode, uint32_t compareOp, bool depthMask, int32_t biasLevel) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_depthMode = depthMode;
    m_depthCompareOp = compareOp;
    m_depthMask = depthMask;
    m_depthBiasLevel = biasLevel;
    GLIDE_LOG(INFO, "Rasterizer", "SetDepthState: Mode=" << depthMode << ", Compare=" << compareOp << ", Mask=" << depthMask);
}

void SoftwareBackendBase::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_rgbSrcBlend = rgbSrcFactor;
    m_rgbDstBlend = rgbDstFactor;
    m_alphaSrcBlend = alphaSrcFactor;
    m_alphaDstBlend = alphaDstFactor;
    GLIDE_LOG(INFO, "Rasterizer", "SetBlendState: Src=" << rgbSrcFactor << ", Dst=" << rgbDstFactor);
}

void SoftwareBackendBase::SetAlphaTestState(uint32_t compareOp, uint32_t refVal) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_alphaTestOp = compareOp;
    m_alphaTestRefVal = refVal;
    GLIDE_LOG(INFO, "Rasterizer", "SetAlphaTestState: Op=" << compareOp << ", Ref=" << refVal);
}

void SoftwareBackendBase::SetCullState(uint32_t cullMode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_cullMode = cullMode;
    GLIDE_LOG(INFO, "Rasterizer", "SetCullState: Mode=" << cullMode);
}

void SoftwareBackendBase::SetClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_clipMinX = minX;
    m_clipMinY = minY;
    m_clipMaxX = std::min(m_headlessWidth, maxX);
    m_clipMaxY = std::min(m_headlessHeight, maxY);
    GLIDE_LOG(INFO, "Rasterizer", "SetClipWindow: (" << m_clipMinX << "," << m_clipMinY << ") to (" << m_clipMaxX << "," << m_clipMaxY << ")");
}

void SoftwareBackendBase::SetSstOrigin(uint32_t origin) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_sstOrigin = origin;
    GLIDE_LOG(INFO, "Rasterizer", "SetSstOrigin: " << origin);
}

void SoftwareBackendBase::SetFogMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fogMode = mode;
    GLIDE_LOG(INFO, "Rasterizer", "SetFogMode: " << mode);
}

void SoftwareBackendBase::SetFogColor(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fogColor = color;
    GLIDE_LOG(INFO, "Rasterizer", "SetFogColor: 0x" << std::hex << color << std::dec);
}

void SoftwareBackendBase::SetFogTable(const uint8_t* table) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (table) {
        std::memcpy(m_fogTable, table, sizeof(m_fogTable));
        GLIDE_LOG(INFO, "Rasterizer", "SetFogTable: 64 entries uploaded.");
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
        GLIDE_LOG(INFO, "Rasterizer", "BindTexture TMU" << tmu << ": Address=0x" << std::hex << startAddress << std::dec 
                                  << ", Clamp(" << clampS << "," << clampT << "), Filter(" << minFilter << "," << magFilter << ")");
    }
}

void SoftwareBackendBase::UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) {
    // Software backend doesn't need physical GPU uploads; it queries TextureManager directly.
    GLIDE_LOG(INFO, "Rasterizer", "UploadTexture (Software) TMU" << tmu << ": Address=0x" << std::hex << startAddress << std::dec << " registered.");
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
    GLIDE_LOG(INFO, "Rasterizer", "SetCombinerMode: Color(Func=" << colorFunc << ", Fac=" << colorFactor << ", Loc=" << colorLocal << ", Oth=" << colorOther << ", Inv=" << colorInvert 
                                  << "), Alpha(Func=" << alphaFunc << ", Fac=" << alphaFactor << ", Loc=" << alphaLocal << ", Oth=" << alphaOther << ", Inv=" << alphaInvert << ")");
}

void SoftwareBackendBase::SetTexCombinerMode(uint32_t tmu, uint32_t rgbFunc, uint32_t rgbFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (tmu < 2) {
        m_texCombinerRgbFunc[tmu]    = rgbFunc;
        m_texCombinerRgbFactor[tmu]  = rgbFactor;
        m_texCombinerAlphaFunc[tmu]  = alphaFunc;
        m_texCombinerAlphaFactor[tmu]= alphaFactor;
        GLIDE_LOG(INFO, "Rasterizer", "SetTexCombinerMode TMU" << tmu << ": RGBFunc=" << rgbFunc << ", AlphaFunc=" << alphaFunc);
    }
}

void SoftwareBackendBase::SetSTWHintState(uint32_t hintMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_stwHintMask = hintMask;
    GLIDE_LOG(INFO, "Rasterizer", "SetSTWHintState: Mask=" << hintMask);
}

void SoftwareBackendBase::SetChromakeyMode(uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_chromakeyMode = mode;
    GLIDE_LOG(INFO, "Rasterizer", "SetChromakeyMode: Mode=" << mode);
}

void SoftwareBackendBase::SetChromakeyValue(uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_chromakeyValue = value;
    GLIDE_LOG(INFO, "Rasterizer", "SetChromakeyValue: Value=0x" << std::hex << value << std::dec);
}

void SoftwareBackendBase::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    // 1. Evaluate backface culling using the original, pre-flipped coordinates!
    if (m_cullMode != 0) { // 1 = GR_CULL_NEGATIVE, 2 = GR_CULL_POSITIVE
        float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) - (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
        if (m_cullMode == 1 && area < 0.0f) return; // Cull negative area
        if (m_cullMode == 2 && area > 0.0f) return; // Cull positive area
    }

    // 2. Now apply Y-coordinate flipping for rasterization
    ModernVertex a_final = a;
    ModernVertex b_final = b;
    ModernVertex c_final = c;

    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        a_final.pos[1] = (float)m_headlessHeight - a.pos[1];
        b_final.pos[1] = (float)m_headlessHeight - b.pos[1];
        c_final.pos[1] = (float)m_headlessHeight - c.pos[1];
    }

    RasterizeSoftwareTriangle(a_final, b_final, c_final);
}

void SoftwareBackendBase::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    ModernVertex v1_final = v1;
    ModernVertex v2_final = v2;

    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        v1_final.pos[1] = (float)m_headlessHeight - v1.pos[1];
        v2_final.pos[1] = (float)m_headlessHeight - v2.pos[1];
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x0 = static_cast<int>(v1_final.pos[0]); int y0 = static_cast<int>(v1_final.pos[1]);
    int x1 = static_cast<int>(v2_final.pos[0]); int y1 = static_cast<int>(v2_final.pos[1]);

    int dx = std::abs(x1 - x0); int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0); int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
        float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
        ModernVertex vInterpolated;
        for (int i = 0; i < 4; ++i) {
            vInterpolated.color[i] = v1_final.color[i] * (1.0f - t) + v2_final.color[i] * t;
        }

        uint32_t color = EvaluateCombinerColor(vInterpolated);

        // Clip against scissor window
        if (x0 >= static_cast<int>(m_clipMinX) && x0 < static_cast<int>(m_clipMaxX) &&
            y0 >= static_cast<int>(m_clipMinY) && y0 < static_cast<int>(m_clipMaxY)) {
            pixels[y0 * m_headlessWidth + x0] = color;
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        step++;
    }
}

void SoftwareBackendBase::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    ModernVertex pt_final = pt;
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
        pt_final.pos[1] = (float)m_headlessHeight - pt.pos[1];
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x = static_cast<int>(pt_final.pos[0]); int y = static_cast<int>(pt_final.pos[1]);
    uint32_t color = EvaluateCombinerColor(pt_final);

    // Clip against scissor window
    if (x >= static_cast<int>(m_clipMinX) && x < static_cast<int>(m_clipMaxX) &&
        y >= static_cast<int>(m_clipMinY) && y < static_cast<int>(m_clipMaxY)) {
        pixels[y * m_headlessWidth + x] = color;
    }
}

uint32_t SoftwareBackendBase::EvaluateCombinerColor(const ModernVertex& v) {
    uint32_t a = static_cast<uint32_t>(v.color[3] * 255.0f + 0.5f);
    uint32_t r = static_cast<uint32_t>(v.color[0] * 255.0f + 0.5f);
    uint32_t g = static_cast<uint32_t>(v.color[1] * 255.0f + 0.5f);
    uint32_t b = static_cast<uint32_t>(v.color[2] * 255.0f + 0.5f);

    uint32_t colorLocal = m_colorCombinerLocal & 0xFFFF;
    if (colorLocal == 1 || m_colorCombinerFactor == 1) { // CONSTANT
        r = (m_constantColor >> 16) & 0xFF;
        g = (m_constantColor >> 8)  & 0xFF;
        b = m_constantColor & 0xFF;
        if (m_pixelFormatOverride == 1) { // ABGR format override for packed constant color
            std::swap(r, b);
        }
    }
    uint32_t alphaLocal = m_alphaCombinerLocal & 0xFFFF;
    if (alphaLocal == 1 || m_alphaCombinerFactor == 1) { // CONSTANT
        a = (m_constantColor >> 24) & 0xFF;
    }

    if (a < 128) a = 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}
static const float s_tableIndexToW[64] = {
    1.000000f,      1.142857f,      1.333333f,      1.600000f, 
    2.000000f,      2.285714f,      2.666667f,      3.200000f, 
    4.000000f,      4.571429f,      5.333333f,      6.400000f, 
    8.000000f,      9.142858f,     10.666667f,     12.800000f, 
    16.000000f,     18.285715f,     21.333334f,     25.600000f, 
    32.000000f,     36.571430f,     42.666668f,     51.200001f, 
    64.000000f,     73.142860f,     85.333336f,    102.400002f, 
    128.000000f,    146.285721f,    170.666672f,    204.800003f, 
    256.000000f,    292.571442f,    341.333344f,    409.600006f, 
    512.000000f,    585.142883f,    682.666687f,    819.200012f, 
    1024.000000f,   1170.285767f,   1365.333374f,   1638.400024f, 
    2048.000000f,   2340.571533f,   2730.666748f,   3276.800049f, 
    4096.000000f,   4681.143066f,   5461.333496f,   6553.600098f, 
    8192.000000f,   9362.286133f,  10922.666992f,  13107.200195f, 
    16384.000000f,  18724.572266f,  21845.333984f,  26214.400391f, 
    32768.000000f,  37449.144531f,  43690.667969f,  52428.800781f
};

void SoftwareBackendBase::RasterizeSoftwareTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    auto edgeFunc = [](const float* p1, const float* p2, const float* p) -> float {
        return (p[0] - p1[0]) * (p2[1] - p1[1]) - (p[1] - p1[1]) * (p2[0] - p1[0]);
    };

    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.pos[0], b.pos[0], c.pos[0]}))));
    int maxX = std::min(static_cast<int>(m_headlessWidth) - 1, static_cast<int>(std::ceil(std::max({a.pos[0], b.pos[0], c.pos[0]}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({a.pos[1], b.pos[1], c.pos[1]}))));
    int maxY = std::min(static_cast<int>(m_headlessHeight) - 1, static_cast<int>(std::ceil(std::max({a.pos[1], b.pos[1], c.pos[1]}))));

    minX = std::max({0, minX, static_cast<int>(m_clipMinX)});
    minY = std::max({0, minY, static_cast<int>(m_clipMinY)});
    maxX = std::min({static_cast<int>(m_headlessWidth) - 1, maxX, static_cast<int>(m_clipMaxX) - 1});
    maxY = std::min({static_cast<int>(m_headlessHeight) - 1, maxY, static_cast<int>(m_clipMaxY) - 1});

    float area = edgeFunc(a.pos.data(), b.pos.data(), c.pos.data());
    GLIDE_LOG(INFO, "RasterizerDebug", "Rasterizing triangle: Area=" << area << ", A=(" << a.pos[0] << "," << a.pos[1] << "), B=(" << b.pos[0] << "," << b.pos[1] << "), C=(" << c.pos[0] << "," << c.pos[1] << ")");
    GLIDE_LOG(INFO, "RasterizerDebug", "Bounds: minX=" << minX << ", maxX=" << maxX << ", minY=" << minY << ", maxY=" << maxY);
    GLIDE_LOG(INFO, "RasterizerDebug", "Scissor bounds: " << m_clipMinX << "," << m_clipMinY << " to " << m_clipMaxX << "," << m_clipMaxY);

    if (std::abs(area) < 0.0001f) {
        GLIDE_LOG(INFO, "RasterizerDebug", "Triangle area is zero! Skipping.");
        return;
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);

    auto sampleTexture = [&](uint32_t tmu, float s, float t) -> uint32_t {
        if (tmu >= 2 || m_boundTexAddress[tmu] == 0xFFFFFFFF) return 0xFFFFFFFF; // Solid white fallback
        auto* tex = TextureManager::GetInstance().GetTexture(tmu, m_boundTexAddress[tmu]);
        if (!tex || tex->swizzledMipLevels.empty()) return 0xFFFFFFFF;

        const auto& basePixels = tex->swizzledMipLevels[0];
        uint32_t w = tex->baseWidth;
        uint32_t h = tex->baseHeight;

        float maxDim = static_cast<float>(std::max(w, h));
        float texU = (s / 256.0f) * maxDim;
        float texV = (t / 256.0f) * maxDim;

        auto getCoord = [](int c, int maxDim, uint32_t clampMode) -> int {
            if (clampMode == 1) { // CLAMP
                return std::max(0, std::min(maxDim - 1, c));
            } else { // WRAP
                int mod = c % maxDim;
                return (mod < 0) ? (mod + maxDim) : mod;
            }
        };

        if (m_texMinFilter[tmu] == 1) { // BILINEAR
            int u0 = static_cast<int>(std::floor(texU - 0.5f));
            int v0 = static_cast<int>(std::floor(texV - 0.5f));
            int u1 = u0 + 1;
            int v1 = v0 + 1;

            float fU = (texU - 0.5f) - u0;
            float fV = (texV - 0.5f) - v0;

            int x0 = getCoord(u0, w, m_texClampS[tmu]);
            int x1 = getCoord(u1, w, m_texClampS[tmu]);
            int y0 = getCoord(v0, h, m_texClampT[tmu]);
            int y1 = getCoord(v1, h, m_texClampT[tmu]);

            uint32_t c00 = basePixels[y0 * w + x0];
            uint32_t c10 = basePixels[y0 * w + x1];
            uint32_t c01 = basePixels[y1 * w + x0];
            uint32_t c11 = basePixels[y1 * w + x1];

            auto mix = [](uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, float fx, float fy) -> uint32_t {
                auto b = [](uint32_t c, int shift) { return static_cast<float>((c >> shift) & 0xFF); };
                float r0 = b(p00, 16) * (1.0f - fx) + b(p10, 16) * fx;
                float r1 = b(p01, 16) * (1.0f - fx) + b(p11, 16) * fx;
                float resR = r0 * (1.0f - fy) + r1 * fy;

                float g0 = b(p00, 8) * (1.0f - fx) + b(p10, 8) * fx;
                float g1 = b(p01, 8) * (1.0f - fx) + b(p11, 8) * fx;
                float resG = g0 * (1.0f - fy) + g1 * fy;

                float bl0 = b(p00, 0) * (1.0f - fx) + b(p10, 0) * fx;
                float bl1 = b(p01, 0) * (1.0f - fx) + b(p11, 0) * fx;
                float resB = bl0 * (1.0f - fy) + bl1 * fy;

                float a0 = b(p00, 24) * (1.0f - fx) + b(p10, 24) * fx;
                float a1 = b(p01, 24) * (1.0f - fx) + b(p11, 24) * fx;
                float resA = a0 * (1.0f - fy) + a1 * fy;

                return (static_cast<uint32_t>(resA + 0.5f) << 24) | (static_cast<uint32_t>(resR + 0.5f) << 16) |
                   (static_cast<uint32_t>(resG + 0.5f) << 8)  | static_cast<uint32_t>(resB + 0.5f);
            };

            return mix(c00, c10, c01, c11, fU, fV);
        } else { // POINT
            int x = getCoord(static_cast<int>(texU), w, m_texClampS[tmu]);
            int y = getCoord(static_cast<int>(texV), h, m_texClampT[tmu]);
            return basePixels[y * w + x];
        }
    };

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float p[2] = {x + 0.5f, y + 0.5f};
            bool isTestPixel = (x == 150 && y == 150) || (x == 310 && y == 310) || (x == 410 && y == 310);

            float w0 = edgeFunc(b.pos.data(), c.pos.data(), p) / area;
            float w1 = edgeFunc(c.pos.data(), a.pos.data(), p) / area;
            float w2 = edgeFunc(a.pos.data(), b.pos.data(), p) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                // 1. Interpolate vertex colors and reciprocal depth
                float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];

                // Near-plane clipping:
                // 1. If W-buffering is active, the W-buffer cannot represent values < 1.0, so we clip at W < 1.0 (oow > 1.0).
                // 2. If texturing is enabled, we must clip if W <= 0.0 (oow <= 0.0) to prevent division by zero/negative in texturing.
                // 3. Otherwise, we do not perform W-based clipping (e.g. flat-shaded Z-buffered geometry has no W).
                if (m_depthMode == 2) { // GR_DEPTHBUFFER_WBUFFER
                    if (wVal > 1.0f) continue;
                } else if (m_boundTexAddress[0] != 0xFFFFFFFF) {
                    if (wVal <= 0.0f) continue;
                }

                float iterR = w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0];
                float iterG = w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1];
                float iterB = w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2];
                float iterA = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];

                // 2. Sample texture using perspective-correct division
                float texR = 1.0f; float texG = 1.0f; float texB = 1.0f; float texA = 1.0f;
                if (m_boundTexAddress[0] != 0xFFFFFFFF) {
                    float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
                    float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];
                    
                    // Perspective division!
                    float divW = wVal;
                    if (m_stwHintMask & 0x2) { // GR_STWHINT_W_DIFF_TMU0
                        divW = w0 * a.tmu_oow[0] + w1 * b.tmu_oow[0] + w2 * c.tmu_oow[0];
                    }
                    float trueS = texS / divW;
                    float trueT = texT / divW;

                    uint32_t tWord = sampleTexture(0, trueS, trueT);
                    texR = ((tWord >> 16) & 0xFF) / 255.0f;
                    texG = ((tWord >> 8)  & 0xFF) / 255.0f;
                    texB = (tWord & 0xFF) / 255.0f;
                    texA = ((tWord >> 24) & 0xFF) / 255.0f;

                    if (isTestPixel) {
                        GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") sampled texture coordinate S=" << trueS << ", T=" << trueT 
                                  << ", TexelColor=(" << texR << "," << texG << "," << texB << "," << texA << ")");
                    }
                }

                // 3. Evaluate general Register Combiner inputs
                // 3a. Alpha Combiner inputs (ALOCAL, AOTHER)
                float aLocal = 1.0f;
                if (m_alphaCombinerLocal == 0) { // GR_COMBINE_LOCAL_ITERATED
                    aLocal = iterA;
                } else if (m_alphaCombinerLocal == 1) { // GR_COMBINE_LOCAL_CONSTANT
                    aLocal = ((m_constantColor >> 24) & 0xFF) / 255.0f;
                }

                float aOther = 1.0f;
                if (m_alphaCombinerOther == 0) { // GR_COMBINE_OTHER_ITERATED
                    aOther = iterA;
                } else if (m_alphaCombinerOther == 1) { // GR_COMBINE_OTHER_TEXTURE
                    aOther = texA;
                } else if (m_alphaCombinerOther == 2) { // GR_COMBINE_OTHER_CONSTANT
                    aOther = ((m_constantColor >> 24) & 0xFF) / 255.0f;
                }

                // 3b. Color Combiner inputs (CLOCAL, COTHER)
                float cLocalR = 0.0f, cLocalG = 0.0f, cLocalB = 0.0f;
                if (m_colorCombinerLocal == 0) { // GR_COMBINE_LOCAL_ITERATED
                    cLocalR = iterR; cLocalG = iterG; cLocalB = iterB;
                } else if (m_colorCombinerLocal == 1) { // GR_COMBINE_LOCAL_CONSTANT / NONE
                    cLocalR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
                    cLocalG = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
                    cLocalB = (m_constantColor & 0xFF) / 255.0f;
                    if (m_pixelFormatOverride == 1) std::swap(cLocalR, cLocalB);
                }

                float cOtherR = 0.0f, cOtherG = 0.0f, cOtherB = 0.0f;
                if (m_colorCombinerOther == 0) { // GR_COMBINE_OTHER_ITERATED
                    cOtherR = iterR; cOtherG = iterG; cOtherB = iterB;
                } else if (m_colorCombinerOther == 1) { // GR_COMBINE_OTHER_TEXTURE
                    cOtherR = texR; cOtherG = texG; cOtherB = texB;
                } else if (m_colorCombinerOther == 2) { // GR_COMBINE_OTHER_CONSTANT
                    cOtherR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
                    cOtherG = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
                    cOtherB = (m_constantColor & 0xFF) / 255.0f;
                    if (m_pixelFormatOverride == 1) std::swap(cOtherR, cOtherB);
                }

                // 4. Evaluate Color Combiner Factor
                float factR = 0.0f, factG = 0.0f, factB = 0.0f;
                switch (m_colorCombinerFactor) {
                    case 0: // ZERO
                        break;
                    case 1: // LOCAL
                        factR = cLocalR; factG = cLocalG; factB = cLocalB;
                        break;
                    case 2: // OTHER_ALPHA
                        factR = aOther; factG = aOther; factB = aOther;
                        break;
                    case 3: // LOCAL_ALPHA
                        factR = aLocal; factG = aLocal; factB = aLocal;
                        break;
                    case 4: // TEXTURE_ALPHA
                        factR = texA; factG = texA; factB = texA;
                        break;
                    case 5: // TEXTURE_RGB
                        factR = texR; factG = texG; factB = texB;
                        break;
                    case 8: // ONE
                        factR = 1.0f; factG = 1.0f; factB = 1.0f;
                        break;
                    case 9: // ONE_MINUS_LOCAL
                        factR = 1.0f - cLocalR; factG = 1.0f - cLocalG; factB = 1.0f - cLocalB;
                        break;
                    case 10: // ONE_MINUS_OTHER_ALPHA
                        factR = 1.0f - aOther; factG = 1.0f - aOther; factB = 1.0f - aOther;
                        break;
                    case 11: // ONE_MINUS_LOCAL_ALPHA
                        factR = 1.0f - aLocal; factG = 1.0f - aLocal; factB = 1.0f - aLocal;
                        break;
                    case 12: // ONE_MINUS_TEXTURE_ALPHA
                        factR = 1.0f - texA; factG = 1.0f - texA; factB = 1.0f - texA;
                        break;
                }

                // 5. Evaluate Color Combiner Function
                float finalR = 0.0f, finalG = 0.0f, finalB = 0.0f;
                switch (m_colorCombinerFunc) {
                    case 0: // ZERO
                        break;
                    case 1: // LOCAL
                        finalR = cLocalR; finalG = cLocalG; finalB = cLocalB;
                        break;
                    case 2: // NONE
                        break;
                    case 3: // SCALE_OTHER (OTHER * FACTOR)
                        finalR = cOtherR * factR; finalG = cOtherG * factG; finalB = cOtherB * factB;
                        break;
                    case 4: // SCALE_OTHER_ADD_LOCAL (OTHER * FACTOR + LOCAL)
                        finalR = cOtherR * factR + cLocalR;
                        finalG = cOtherG * factG + cLocalG;
                        finalB = cOtherB * factB + cLocalB;
                        break;
                    case 5: // SCALE_OTHER_ADD_LOCAL_ALPHA (OTHER * FACTOR + ALOCAL)
                        finalR = cOtherR * factR + aLocal;
                        finalG = cOtherG * factG + aLocal;
                        finalB = cOtherB * factB + aLocal;
                        break;
                    case 6: // SCALE_OTHER_MINUS_LOCAL ((OTHER - LOCAL) * FACTOR)
                        finalR = (cOtherR - cLocalR) * factR;
                        finalG = (cOtherG - cLocalG) * factG;
                        finalB = (cOtherB - cLocalB) * factB;
                        break;
                    case 7: // SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ((OTHER - LOCAL) * FACTOR + LOCAL)
                        finalR = (cOtherR - cLocalR) * factR + cLocalR;
                        finalG = (cOtherG - cLocalG) * factG + cLocalG;
                        finalB = (cOtherB - cLocalB) * factB + cLocalB;
                        break;
                    case 8: // SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA ((OTHER - LOCAL) * FACTOR + ALOCAL)
                        finalR = (cOtherR - cLocalR) * factR + aLocal;
                        finalG = (cOtherG - cLocalG) * factG + aLocal;
                        finalB = (cOtherB - cLocalB) * factB + aLocal;
                        break;
                }

                if (m_colorCombinerInvert) {
                    finalR = 1.0f - finalR; finalG = 1.0f - finalG; finalB = 1.0f - finalB;
                }

                // 6. Evaluate Alpha Combiner Factor
                float factA = 0.0f;
                switch (m_alphaCombinerFactor) {
                    case 0: // ZERO
                        break;
                    case 1: // LOCAL
                    case 3: // LOCAL_ALPHA
                        factA = aLocal;
                        break;
                    case 2: // OTHER_ALPHA
                        factA = aOther;
                        break;
                    case 4: // TEXTURE_ALPHA
                        factA = texA;
                        break;
                    case 8: // ONE
                        factA = 1.0f;
                        break;
                    case 9: // ONE_MINUS_LOCAL
                    case 11: // ONE_MINUS_LOCAL_ALPHA
                        factA = 1.0f - aLocal;
                        break;
                    case 10: // ONE_MINUS_OTHER_ALPHA
                        factA = 1.0f - aOther;
                        break;
                    case 12: // ONE_MINUS_TEXTURE_ALPHA
                        factA = 1.0f - texA;
                        break;
                }

                // 7. Evaluate Alpha Combiner Function
                float finalA = 0.0f;
                switch (m_alphaCombinerFunc) {
                    case 0: // ZERO
                        break;
                    case 1: // LOCAL
                        finalA = aLocal;
                        break;
                    case 3: // SCALE_OTHER (OTHER * FACTOR)
                        finalA = aOther * factA;
                        break;
                    case 4: // SCALE_OTHER_ADD_LOCAL (OTHER * FACTOR + LOCAL)
                    case 5: // SCALE_OTHER_ADD_LOCAL_ALPHA
                        finalA = aOther * factA + aLocal;
                        break;
                    case 6: // SCALE_OTHER_MINUS_LOCAL ((OTHER - LOCAL) * FACTOR)
                        finalA = (aOther - aLocal) * factA;
                        break;
                    case 7: // SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ((OTHER - LOCAL) * FACTOR + LOCAL)
                    case 8: // SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA
                        finalA = (aOther - aLocal) * factA + aLocal;
                        break;
                }

                if (m_alphaCombinerInvert) {
                    finalA = 1.0f - finalA;
                }

                // Write results to downstream pipeline variables!
                float red   = finalR;
                float green = finalG;
                float blue  = finalB;
                float alpha = finalA;

                // Milestone 10: Chromakey Emulation
                if (m_chromakeyMode == 1) { // 1 = GR_CHROMAKEY_ENABLE
                    float chromaR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
                    float chromaG = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;
                    float chromaB = (m_chromakeyValue & 0xFF) / 255.0f;
                    if (m_pixelFormatOverride == 1) {
                        std::swap(chromaR, chromaB);
                    }
                    if (std::abs(red - chromaR) < 0.01f &&
                        std::abs(green - chromaG) < 0.01f &&
                        std::abs(blue - chromaB) < 0.01f) {
                        continue; // Discard! Skip depth, blending, and color writes.
                    }
                }

                uint32_t aVal = static_cast<uint32_t>(std::min(255.0f, alpha * 255.0f + 0.5f));

                bool alphaPass = true;
                switch (m_alphaTestOp) {
                    case 0: alphaPass = false; break; // NEVER
                    case 1: alphaPass = (aVal < m_alphaTestRefVal); break; // LESS
                    case 2: alphaPass = (aVal == m_alphaTestRefVal); break; // EQUAL
                    case 3: alphaPass = (aVal <= m_alphaTestRefVal); break; // LEQUAL
                    case 4: alphaPass = (aVal > m_alphaTestRefVal); break; // GREATER
                    case 5: alphaPass = (aVal != m_alphaTestRefVal); break; // NOTEQUAL
                    case 6: alphaPass = (aVal >= m_alphaTestRefVal); break; // GEQUAL
                    case 7: alphaPass = true; break; // ALWAYS
                }

                if (!alphaPass) {
                    if (isTestPixel) {
                        GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") failed alpha test! Op=" << m_alphaTestOp << ", Ref=" << m_alphaTestRefVal);
                    }
                    continue;
                }

                float zVal = w0 * a.pos[2] + w1 * b.pos[2] + w2 * c.pos[2];
                float targetDepth = (m_depthMode == 2) ? (1.0f - wVal) : zVal;

                if (m_depthBiasLevel != 0) {
                    targetDepth += static_cast<float>(m_depthBiasLevel) / 65535.0f;
                    targetDepth = std::max(0.0f, std::min(1.0f, targetDepth));
                }

                uint32_t pIdx = y * m_headlessWidth + x;
                bool depthPass = true;

                // Only perform depth test if depth buffering is enabled (m_depthMode != 0)
                if (m_depthMode != 0 && m_depthCompareOp != 7 && !m_headlessDepthBuffer.empty()) {
                    float currentDepth = m_headlessDepthBuffer[pIdx];
                    switch (m_depthCompareOp) {
                        case 0: depthPass = false; break;
                        case 1: depthPass = (targetDepth < currentDepth); break;
                        case 2: depthPass = (targetDepth == currentDepth); break;
                        case 3: depthPass = (targetDepth <= currentDepth); break;
                        case 4: depthPass = (targetDepth > currentDepth); break;
                        case 5: depthPass = (targetDepth != currentDepth); break;
                        case 6: depthPass = (targetDepth >= currentDepth); break;
                        case 7: depthPass = true; break;
                    }
                    if (isTestPixel) {
                        GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") depth test result=" << (depthPass ? "PASS" : "FAIL") 
                                  << ", targetDepth=" << targetDepth << ", currentDepth=" << currentDepth << ", Op=" << m_depthCompareOp);
                    }
                }

                if (!depthPass) continue;

                // Only write depth if depth buffering is enabled and depth mask is true
                if (m_depthMode != 0 && m_depthMask && !m_headlessDepthBuffer.empty()) {
                    m_headlessDepthBuffer[pIdx] = targetDepth;
                }

                // Milestone 8: Fogging Emulation (with piecewise linear interpolation)
                if (m_fogMode != 0) {
                    float eyeW = 1.0f / wVal;
                    if (eyeW < 1.0f) eyeW = 1.0f;

                    int idx = 0;
                    for (int i = 0; i < 63; ++i) {
                        if (s_tableIndexToW[i] < eyeW) {
                            idx = i;
                        } else {
                            break;
                        }
                    }

                    float w0 = s_tableIndexToW[idx];
                    float w1 = s_tableIndexToW[idx + 1];
                    float f0 = m_fogTable[idx] / 255.0f;
                    float f1 = m_fogTable[idx + 1] / 255.0f;

                    float t = 0.0f;
                    if (w1 > w0) {
                        t = (eyeW - w0) / (w1 - w0);
                    }
                    t = std::max(0.0f, std::min(1.0f, t));
                    float f = f0 * (1.0f - t) + f1 * t;

                    float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
                    float fogG = ((m_fogColor >> 8)  & 0xFF) / 255.0f;
                    float fogB = (m_fogColor & 0xFF) / 255.0f;
                    if (m_pixelFormatOverride == 1) {
                        std::swap(fogR, fogB);
                    }

                    red   = red   * (1.0f - f) + fogR * f;
                    green = green * (1.0f - f) + fogG * f;
                    blue  = blue  * (1.0f - f) + fogB * f;
                }

                uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
                uint32_t gVal = static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
                uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));

                if (m_rgbSrcBlend != 4 || m_rgbDstBlend != 0) {
                    uint32_t dstWord = pixels[pIdx];
                    float dstR = ((dstWord >> 16) & 0xFF) / 255.0f;
                    float dstG = ((dstWord >> 8) & 0xFF) / 255.0f;
                    float dstB = (dstWord & 0xFF) / 255.0f;
                    float dstA = ((dstWord >> 24) & 0xFF) / 255.0f;

                    auto getFactor = [&](uint32_t factorMode, float srcC, float dstC, float srcAVal, float dstAVal, bool isDest) {
                        switch (factorMode) {
                            case 0: return 0.0f;
                            case 1: return srcAVal;
                            case 2: return isDest ? srcC : dstC; // 2 = GR_BLEND_SRC_COLOR (dest) / GR_BLEND_DST_COLOR (src)
                            case 3: return dstAVal;
                            case 4: return 1.0f;
                            case 5: return 1.0f - srcAVal;
                            case 6: return isDest ? (1.0f - srcC) : (1.0f - dstC); // 6 = ONE_MINUS_SRC_COLOR (dest) / ONE_MINUS_DST_COLOR (src)
                            case 7: return 1.0f - dstAVal;
                            default: return 1.0f;
                        }
                    };

                    float sFR = getFactor(m_rgbSrcBlend, red, dstR, alpha, dstA, false);
                    float sFG = getFactor(m_rgbSrcBlend, green, dstG, alpha, dstA, false);
                    float sFB = getFactor(m_rgbSrcBlend, blue, dstB, alpha, dstA, false);

                    float dFR = getFactor(m_rgbDstBlend, red, dstR, alpha, dstA, true);
                    float dFG = getFactor(m_rgbDstBlend, green, dstG, alpha, dstA, true);
                    float dFB = getFactor(m_rgbDstBlend, blue, dstB, alpha, dstA, true);

                    float sFA = getFactor(m_alphaSrcBlend, alpha, dstA, alpha, dstA, false);
                    float dFA = getFactor(m_alphaDstBlend, alpha, dstA, alpha, dstA, true);
                    float blendedAlpha = std::min(1.0f, alpha * sFA + dstA * dFA);

                    red   = std::min(1.0f, red   * sFR + dstR * dFR);
                    green = std::min(1.0f, green * sFG + dstG * dFG);
                    blue  = std::min(1.0f, blue  * sFB + dstB * dFB);

                    rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
                    gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
                    bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
                    aVal = static_cast<uint32_t>(blendedAlpha * 255.0f + 0.5f);
                }

                if (isTestPixel) {
                    GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") successfully written to LFB! Color=0x" 
                              << std::hex << ((aVal << 24) | (rVal << 16) | (gVal << 8) | bVal) << std::dec);
                }

                pixels[pIdx] = (aVal << 24) | (rVal << 16) | (gVal << 8) | bVal;
            }
        }
    }
}

void* SoftwareBackendBase::GetLfbWritePtr() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_headlessPixelMap;
}

bool SoftwareBackendBase::ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!dstData || !m_headlessPixelMap) return false;

    const auto* pixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
    auto* outBytes = reinterpret_cast<uint8_t*>(dstData);

    bool is32Bit = (dstStride >= srcWidth * 4);
    GLIDE_LOG(INFO, "ReadLFB", "ReadLFB invoked: srcX=" << srcX << ", srcY=" << srcY << ", srcWidth=" << srcWidth << ", srcHeight=" << srcHeight << ", dstStride=" << dstStride << ", is32Bit=" << is32Bit);

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
                    GLIDE_LOG(INFO, "ReadLFB", "ReadLFB (32-bit) at (" << readX << "," << readY << ") = 0x" << std::hex << val << std::dec);
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

bool SoftwareBackendBase::WriteLFB(uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData, const void* refData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!srcData || !m_headlessPixelMap) return false;

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    const auto* inBytes = reinterpret_cast<const uint8_t*>(srcData);
    const auto* refBytes = reinterpret_cast<const uint8_t*>(refData);

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
            if (srcFmt == 0) { // GR_LFB_SRC_FMT_565
                uint16_t c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
                uint32_t g = ((c16 >> 5)  & 0x3F) * 255 / 63;
                uint32_t b = (c16         & 0x1F) * 255 / 31;
                pWord = (255 << 24) | (r << 16) | (g << 8) | b;
            } else if (srcFmt == 1 || srcFmt == 2) { // GR_LFB_SRC_FMT_555 / 1555
                uint16_t c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                uint32_t r = ((c16 >> 10) & 0x1F) * 255 / 31;
                uint32_t g = ((c16 >> 5)  & 0x1F) * 255 / 31;
                uint32_t b = (c16         & 0x1F) * 255 / 31;
                uint32_t a = (srcFmt == 2) ? (((c16 >> 15) & 1) ? 255 : 0) : 255;
                pWord = (a << 24) | (r << 16) | (g << 8) | b;
            } else { // 32-bit ARGB8888 / RGB888
                pWord = reinterpret_cast<const uint32_t*>(inBytes + y * srcStride)[x];
            }
            pixels[writeY * m_headlessWidth + writeX] = pWord;
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
            GLIDE_LOG(INFO, "Backend", "SDL window keydown event captured! Setting keyboard hit hook flag.");
            g_glideWrapperSdlKeyHit = 1;
        }
    }
}

void SoftwareBackendBase::SstIdle() {
    // Software rasterizer is completely synchronous; all drawing dispatches 
    // are completed on the host CPU thread during the draw call itself.
}

} // namespace GlideWrapper
