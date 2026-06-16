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

void SoftwareBackendBase::SetCombinerMode(uint32_t colorFunc, uint32_t colorFactor, uint32_t alphaFunc, uint32_t alphaFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_colorCombinerFunc   = colorFunc;
    m_colorCombinerFactor = colorFactor;
    m_alphaCombinerFunc   = alphaFunc;
    m_alphaCombinerFactor = alphaFactor;
    GLIDE_LOG(INFO, "Rasterizer", "SetCombinerMode: ColorFunc=" << colorFunc << ", AlphaFunc=" << alphaFunc);
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

void SoftwareBackendBase::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    if (m_cullMode != 0) { // 1 = GR_CULL_NEGATIVE, 2 = GR_CULL_POSITIVE
        float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) - (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
        if (m_cullMode == 1 && area < 0.0f) return; // Cull negative area
        if (m_cullMode == 2 && area > 0.0f) return; // Cull positive area
    }

    RasterizeSoftwareTriangle(a, b, c);
}

void SoftwareBackendBase::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached || !m_headlessPixelMap) return;

    if (m_config.drawCallDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(m_config.drawCallDelayUs)));
    }

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x0 = static_cast<int>(v1.pos[0]); int y0 = static_cast<int>(v1.pos[1]);
    int x1 = static_cast<int>(v2.pos[0]); int y1 = static_cast<int>(v2.pos[1]);

    int dx = std::abs(x1 - x0); int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0); int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(dx, -dy);
    int step = 0;

    while (true) {
        float t = totalSteps > 0 ? static_cast<float>(step) / totalSteps : 0.0f;
        ModernVertex vInterpolated;
        for (int i = 0; i < 4; ++i) {
            vInterpolated.color[i] = v1.color[i] * (1.0f - t) + v2.color[i] * t;
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

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    int x = static_cast<int>(pt.pos[0]); int y = static_cast<int>(pt.pos[1]);
    uint32_t color = EvaluateCombinerColor(pt);

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

    uint32_t colorLocal = m_colorCombinerFactor & 0xFFFF;
    if (colorLocal == 1 || m_colorCombinerFactor == 1) { // CONSTANT
        r = (m_constantColor >> 16) & 0xFF;
        g = (m_constantColor >> 8)  & 0xFF;
        b = m_constantColor & 0xFF;
        if (m_pixelFormatOverride == 1) { // ABGR format override for packed constant color
            std::swap(r, b);
        }
    }
    uint32_t alphaLocal = m_alphaCombinerFactor & 0xFFFF;
    if (alphaLocal == 1 || m_alphaCombinerFactor == 1) { // CONSTANT
        a = (m_constantColor >> 24) & 0xFF;
    }

    if (a < 128) a = 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

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
        if (tmu >= 2 || m_boundTexAddress[tmu] == 0) return 0xFFFFFFFF; // Solid white fallback
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

            float w0 = edgeFunc(b.pos.data(), c.pos.data(), p) / area;
            float w1 = edgeFunc(c.pos.data(), a.pos.data(), p) / area;
            float w2 = edgeFunc(a.pos.data(), b.pos.data(), p) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                float red   = w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0];
                float green = w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1];
                float blue  = w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2];
                float alpha = w0 * a.color[3] + w1 * b.color[3] + w2 * c.color[3];

                uint32_t colorLocal = m_colorCombinerFactor & 0xFFFF;
                if (colorLocal == 1 || m_colorCombinerFactor == 1) { // CONSTANT
                    uint32_t cr = (m_constantColor >> 16) & 0xFF;
                    uint32_t cg = (m_constantColor >> 8)  & 0xFF;
                    uint32_t cb = m_constantColor & 0xFF;
                    if (m_pixelFormatOverride == 1) { // ABGR format override for packed constant color
                        std::swap(cr, cb);
                    }
                    red   = cr / 255.0f;
                    green = cg / 255.0f;
                    blue  = cb / 255.0f;
                }
                uint32_t alphaLocal = m_alphaCombinerFactor & 0xFFFF;
                if (alphaLocal == 1 || m_alphaCombinerFactor == 1) { // CONSTANT
                    alpha = ((m_constantColor >> 24) & 0xFF) / 255.0f;
                    if (alpha < 0.001f) alpha = 1.0f;
                }

                bool isTestPixel = (x == 150 && y == 150) || (x == 310 && y == 310) || (x == 410 && y == 310);
                if (isTestPixel) {
                    GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") edge pass. w0=" << w0 << ", w1=" << w1 << ", w2=" << w2 
                              << ", Color=(" << red << "," << green << "," << blue << "," << alpha << ")");
                }

                if (m_boundTexAddress[0] != 0) {
                    float texS = w0 * a.tex[0] + w1 * b.tex[0] + w2 * c.tex[0];
                    float texT = w0 * a.tex[1] + w1 * b.tex[1] + w2 * c.tex[1];

                    uint32_t tWord = sampleTexture(0, texS, texT);
                    float texR = ((tWord >> 16) & 0xFF) / 255.0f;
                    float texG = ((tWord >> 8)  & 0xFF) / 255.0f;
                    float texB = (tWord & 0xFF) / 255.0f;
                    float texA = ((tWord >> 24) & 0xFF) / 255.0f;

                    if (isTestPixel) {
                        GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") sampled texture coordinate S=" << texS << ", T=" << texT 
                                  << ", TexelColor=(" << texR << "," << texG << "," << texB << "," << texA << ")");
                    }

                    if (m_colorCombinerFunc == 3 || m_colorCombinerFunc == 4) { // Modulate
                        red   *= texR;
                        green *= texG;
                        blue  *= texB;
                        alpha  = texA;
                    } else { // Replace
                        red   = texR;
                        green = texG;
                        blue  = texB;
                        alpha = texA;
                    }
                }

                // Removed global format override swap for vertex colors

                uint32_t aVal = static_cast<uint32_t>(std::min(255.0f, alpha * 255.0f));

                if (aVal < 128 && m_boundTexAddress[0] != 0) {
                    if (isTestPixel) {
                        GLIDE_LOG(INFO, "RasterizerDebug", "Test pixel (" << x << "," << y << ") discarded due to alpha threshold (" << aVal << " < 128)");
                    }
                    continue;
                }

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
                float wVal = w0 * a.pos[3] + w1 * b.pos[3] + w2 * c.pos[3];
                float targetDepth = (m_depthMode == 2) ? wVal : zVal;

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

                uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
                uint32_t gVal = static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
                uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));

                if (m_rgbSrcBlend != 1 || m_rgbDstBlend != 0) {
                    uint32_t dstWord = pixels[pIdx];
                    float dstR = ((dstWord >> 16) & 0xFF) / 255.0f;
                    float dstG = ((dstWord >> 8) & 0xFF) / 255.0f;
                    float dstB = (dstWord & 0xFF) / 255.0f;
                    float dstA = ((dstWord >> 24) & 0xFF) / 255.0f;

                    auto getFactor = [&](uint32_t factorMode, float srcC, float dstC, float srcAVal, float dstAVal) {
                        switch (factorMode) {
                            case 0: return 0.0f;
                            case 1: return srcAVal;
                            case 2: return srcC;
                            case 3: return dstAVal;
                            case 4: return 1.0f;
                            case 5: return 1.0f - srcAVal;
                            case 6: return 1.0f - srcC;
                            case 7: return 1.0f - dstAVal;
                            default: return 1.0f;
                        }
                    };

                    float sFR = getFactor(m_rgbSrcBlend, red, dstR, alpha, dstA);
                    float sFG = getFactor(m_rgbSrcBlend, green, dstG, alpha, dstA);
                    float sFB = getFactor(m_rgbSrcBlend, blue, dstB, alpha, dstA);

                    float dFR = getFactor(m_rgbDstBlend, red, dstR, alpha, dstA);
                    float dFG = getFactor(m_rgbDstBlend, green, dstG, alpha, dstA);
                    float dFB = getFactor(m_rgbDstBlend, blue, dstB, alpha, dstA);

                    red   = std::min(1.0f, red   * sFR + dstR * dFR);
                    green = std::min(1.0f, green * sFG + dstG * dFG);
                    blue  = std::min(1.0f, blue  * sFB + dstB * dFB);

                    rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
                    gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
                    bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
                    aVal = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
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

bool SoftwareBackendBase::WriteLFB(uint32_t dstX, uint32_t dstY, uint32_t srcWidth, uint32_t srcHeight, int32_t srcStride, uint32_t srcFmt, const void* srcData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!srcData || !m_headlessPixelMap) return false;

    auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
    const auto* inBytes = reinterpret_cast<const uint8_t*>(srcData);

    for (uint32_t y = 0; y < srcHeight; ++y) {
        uint32_t writeY = dstY + y;
        if (writeY >= m_headlessHeight) break;

        for (uint32_t x = 0; x < srcWidth; ++x) {
            uint32_t writeX = dstX + x;
            if (writeX >= m_headlessWidth) break;

            uint32_t pWord = 0xFF000000;
            if (srcFmt == 1 || srcFmt == 2) { // 565 or 555
                uint16_t c16 = reinterpret_cast<const uint16_t*>(inBytes + y * srcStride)[x];
                uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
                uint32_t g = ((c16 >> 5)  & 0x3F) * 255 / 63;
                uint32_t b = (c16         & 0x1F) * 255 / 31;
                pWord = (255 << 24) | (r << 16) | (g << 8) | b;
            } else { // 888 or ARGB
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

} // namespace GlideWrapper
