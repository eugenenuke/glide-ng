/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room procedural 3D startup splash ("Chrome & Amber Shield")
 * and 180x90 corner watermark renderer (GR_SHAMELESS_PLUG).
 *
 * Zero external binary assets or third-party SDK meshes/bitmaps are used:
 * both the 3D beveled hexagonal shield animation and the 180x90 RGB565
 * chromakey corner badge are synthesized procedurally at runtime.
 */
#include "GlideSplashAnimator.h"
#include "BackendManager.h"
#include "Telemetry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" uint32_t s_sstOrigin;

namespace GlideWrapper {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x, y, z;
};

struct Rgb {
    float r, g, b;
};

struct ShieldFace {
    int v0, v1, v2;
    Rgb baseColor;
};

struct ProjectedTri {
    float x[3];
    float y[3];
    float w[3];
    float avgZ;
    float r, g, b;
};

// Minimal 5x7 uppercase bitmap glyphs used for the procedural "GLIDE NG" mark.
struct Glyph5x7 {
    char ch;
    uint8_t rows[7];
};

constexpr Glyph5x7 kSplashGlyphs[] = {
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

const uint8_t* LookupGlyphRows(char ch) {
    for (const auto& g : kSplashGlyphs) {
        if (g.ch == ch) {
            return g.rows;
        }
    }
    return kSplashGlyphs[6].rows;
}

inline uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t px = static_cast<uint16_t>(((r & 0xF8u) << 8) |
                                        ((g & 0xFCu) << 3) |
                                        (b >> 3));
    // Guard against accidentally colliding with the 0x0000 chromakey value.
    return (px == 0x0000u && (r | g | b) != 0u) ? 0x0821u : px;
}

inline uint8_t LerpU8(uint8_t a, uint8_t b, float t) {
    float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

inline Vec3 RotateXYZ(Vec3 v, float rx, float ry, float rz) {
    // Rotate X
    float cx = std::cos(rx), sx = std::sin(rx);
    float y1 = v.y * cx - v.z * sx;
    float z1 = v.y * sx + v.z * cx;
    v.y = y1;
    v.z = z1;

    // Rotate Y
    float cy = std::cos(ry), sy = std::sin(ry);
    float x2 = v.x * cy + v.z * sy;
    float z2 = -v.x * sy + v.z * cy;
    v.x = x2;
    v.z = z2;

    // Rotate Z
    float cz = std::cos(rz), sz = std::sin(rz);
    float x3 = v.x * cz - v.y * sz;
    float y3 = v.x * sz + v.y * cz;
    return {x3, y3, v.z};
}

inline Vec3 Normalize(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) {
        return {0.0f, 0.0f, -1.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

// Build the beveled 3D hexagonal Chrome & Amber Shield (26 vertices, 48 faces).
struct ShieldMesh {
    std::array<Vec3, 26> verts;
    std::array<ShieldFace, 48> faces;
};

const ShieldMesh& GetShieldMesh() {
    static const ShieldMesh kMesh = []() {
        ShieldMesh m{};
        constexpr int kSides = 6;
        constexpr float kOuterR = 1.15f;
        constexpr float kInnerR = 0.78f;
        constexpr float kAspectY = 1.12f;
        constexpr float kOuterD = 0.22f;
        constexpr float kInnerD = 0.42f;
        constexpr float kApexD = kInnerD + 0.14f;

        for (int i = 0; i < kSides; ++i) {
            float angle = (static_cast<float>(i) * 2.0f * kPi) / static_cast<float>(kSides) +
                          (kPi / 6.0f);
            float ca = std::cos(angle);
            float sa = std::sin(angle);
            m.verts[i * 4 + 0] = {ca * kOuterR, sa * kOuterR * kAspectY, -kOuterD}; // outer front
            m.verts[i * 4 + 1] = {ca * kOuterR, sa * kOuterR * kAspectY,  kOuterD}; // outer back
            m.verts[i * 4 + 2] = {ca * kInnerR, sa * kInnerR * kAspectY, -kInnerD}; // inner front
            m.verts[i * 4 + 3] = {ca * kInnerR, sa * kInnerR * kAspectY,  kInnerD}; // inner back
        }
        constexpr int kCenterFront = 24;
        constexpr int kCenterBack = 25;
        m.verts[kCenterFront] = {0.0f, 0.0f, -kApexD};
        m.verts[kCenterBack]  = {0.0f, 0.0f,  kApexD};

        constexpr Rgb kSteel = {184.0f, 202.0f, 224.0f};
        constexpr Rgb kAmber = {255.0f, 156.0f, 32.0f};

        int fIdx = 0;
        for (int i = 0; i < kSides; ++i) {
            int j = (i + 1) % kSides;
            int a = i * 4;
            int b = j * 4;

            // Brushed steel outer rim
            m.faces[fIdx++] = {a,     a + 1, b + 1, kSteel};
            m.faces[fIdx++] = {a,     b + 1, b,     kSteel};
            // Brushed steel front bevel
            m.faces[fIdx++] = {a + 2, a,     b,     kSteel};
            m.faces[fIdx++] = {a + 2, b,     b + 2, kSteel};
            // Warm amber faceted front face
            m.faces[fIdx++] = {kCenterFront, a + 2, b + 2, kAmber};
            // Brushed steel back bevel
            m.faces[fIdx++] = {b + 3, b + 1, a + 1, kSteel};
            m.faces[fIdx++] = {b + 3, a + 1, a + 3, kSteel};
            // Warm amber faceted back face
            m.faces[fIdx++] = {kCenterBack, b + 3, a + 3, kAmber};
        }
        return m;
    }();
    return kMesh;
}

inline GrVertex MakeSplashVertex(float x, float y, float r, float g, float b,
                                 float a = 255.0f, float oow = 1.0f) {
    GrVertex v{};
    v.x = x;
    v.y = y;
    v.z = 0.0f;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = a;
    v.ooz = 1.0f;
    v.oow = oow;
    return v;
}

void DrawQuad2D(float x0, float y0, float x1, float y1,
                float r, float g, float b, float a = 255.0f) {
    GrVertex v0 = MakeSplashVertex(x0, y0, r, g, b, a);
    GrVertex v1 = MakeSplashVertex(x1, y0, r, g, b, a);
    GrVertex v2 = MakeSplashVertex(x1, y1, r, g, b, a);
    GrVertex v3 = MakeSplashVertex(x0, y1, r, g, b, a);
    grDrawTriangle(&v0, &v1, &v2);
    grDrawTriangle(&v0, &v2, &v3);
}

void DrawItalicWordmark2D(const char* text, float startX, float startY,
                          float scale, float r, float g, float b,
                          float spacing = 1.0f) {
    float cx = startX;
    for (const char* p = text; *p != '\0'; ++p) {
        const uint8_t* rows = LookupGlyphRows(*p);
        for (int row = 0; row < 7; ++row) {
            float slant = static_cast<float>(6 - row) * 0.35f * scale;
            for (int col = 0; col < 5; ++col) {
                if (rows[row] & (1u << (4 - col))) {
                    float px = cx + static_cast<float>(col) * scale + slant;
                    float py = startY + static_cast<float>(row) * scale;
                    DrawQuad2D(px, py, px + scale, py + scale, r, g, b);
                }
            }
        }
        cx += (5.0f + spacing) * scale;
    }
}

void RasterizeBannerGlyph(uint16_t* dst, int w, int h, const char* text,
                          int startX, int startY, int scale, uint16_t color,
                          int spacing = 1) {
    int cx = startX;
    for (const char* p = text; *p != '\0'; ++p) {
        const uint8_t* rows = LookupGlyphRows(*p);
        for (int row = 0; row < 7; ++row) {
            int slant = static_cast<int>(std::lround(static_cast<float>(6 - row) * 0.35f *
                                                     static_cast<float>(scale)));
            for (int col = 0; col < 5; ++col) {
                if (rows[row] & (1u << (4 - col))) {
                    int x0 = cx + col * scale + slant;
                    int y0 = startY + row * scale;
                    for (int dy = 0; dy < scale; ++dy) {
                        int py = y0 + dy;
                        if (py < 0 || py >= h) continue;
                        for (int dx = 0; dx < scale; ++dx) {
                            int px = x0 + dx;
                            if (px >= 0 && px < w) {
                                dst[py * w + px] = color;
                            }
                        }
                    }
                }
            }
        }
        cx += (5 + spacing) * scale;
    }
}

} // namespace

void GlideSplashAnimator::RenderSingleFrame(float vpX, float vpY, float vpW, float vpH,
                                            int frame, bool drawFloorGrid) const {
    const float t = static_cast<float>(frame) / static_cast<float>(kTotalFrames - 1);

    // Smooth fade-in (frames 0..11) and fade-out (frames 63..74)
    float fade = 1.0f;
    if (frame < 12) {
        fade = static_cast<float>(frame) / 12.0f;
    } else if (frame > 62) {
        fade = static_cast<float>((static_cast<int>(kTotalFrames) - 1) - frame) / 12.0f;
    }
    fade = std::clamp(fade, 0.12f, 1.0f);

    // 1. Subtle receding amber perspective grid on the lower floor
    if (drawFloorGrid) {
        const float gridR = 255.0f * 0.24f * fade;
        const float gridG = 146.0f * 0.24f * fade;
        const float gridB = 36.0f * 0.24f * fade;

        const float yHorizon = vpY + vpH * 0.67f;
        const float yBottom = vpY + vpH - 1.0f;
        const float cx = vpX + vpW * 0.5f;
        const float vpRight = vpX + vpW - 1.0f;

        for (int i = -6; i <= 6; ++i) {
            float x0 = cx + static_cast<float>(i) * (vpW * 0.068f);
            float x1 = cx + static_cast<float>(i) * (vpW * 0.247f);
            float y0 = yHorizon;
            float y1 = yBottom;

            // Clip line segment cleanly to [vpX, vpRight]
            if (x0 < vpX && x1 < vpX) continue;
            if (x0 > vpRight && x1 > vpRight) continue;
            if (x1 < vpX && x0 != x1) {
                float u = (vpX - x0) / (x1 - x0);
                x1 = vpX;
                y1 = y0 + u * (y1 - y0);
            } else if (x1 > vpRight && x0 != x1) {
                float u = (vpRight - x0) / (x1 - x0);
                x1 = vpRight;
                y1 = y0 + u * (y1 - y0);
            }
            if (x0 < vpX && x0 != x1) {
                float u = (vpX - x0) / (x1 - x0);
                x0 = vpX;
                y0 = y0 + u * (y1 - y0);
            } else if (x0 > vpRight && x0 != x1) {
                float u = (vpRight - x0) / (x1 - x0);
                x0 = vpRight;
                y0 = y0 + u * (y1 - y0);
            }

            GrVertex l0 = MakeSplashVertex(x0, y0, gridR, gridG, gridB);
            GrVertex l1 = MakeSplashVertex(x1, y1, gridR, gridG, gridB);
            grDrawLine(&l0, &l1);
        }

        for (int r = 0; r < 5; ++r) {
            float frac = std::pow(static_cast<float>(r) / 4.0f, 1.5f);
            float y = yHorizon + frac * (vpH * 0.33f - 1.0f);
            GrVertex l0 = MakeSplashVertex(vpX, y, gridR, gridG, gridB);
            GrVertex l1 = MakeSplashVertex(vpRight, y, gridR, gridG, gridB);
            grDrawLine(&l0, &l1);
        }
    }

    // 2. Transform, shade, backface-cull, and painter-sort the 3D Chrome & Amber Shield
    const ShieldMesh& mesh = GetShieldMesh();
    const float spinY = t * kPi * 2.2f - 0.5f;
    const float wobbleX = (0.32f + std::sin(t * kPi * 2.0f) * 0.16f) * 0.7f;

    std::array<Vec3, 26> xformVerts{};
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        xformVerts[i] = RotateXYZ(mesh.verts[i], wobbleX, spinY, 0.0f);
    }

    const float fov = vpH * 0.903f;
    constexpr float kZOffset = 4.2f;
    const float centerX = vpX + vpW * 0.5f;
    const float centerY = vpY + vpH * 0.44f;

    const Vec3 keyLight = Normalize({0.42f, 0.72f, -0.55f});
    const Vec3 rimLight = Normalize({-0.55f, -0.30f, -0.60f});
    constexpr Rgb kSpecColor = {255.0f, 240.0f, 200.0f};

    std::vector<ProjectedTri> visible;
    visible.reserve(mesh.faces.size());

    for (const auto& face : mesh.faces) {
        const Vec3& a = xformVerts[face.v0];
        const Vec3& b = xformVerts[face.v1];
        const Vec3& c = xformVerts[face.v2];

        const float za = a.z + kZOffset;
        const float zb = b.z + kZOffset;
        const float zc = c.z + kZOffset;
        if (za <= 0.2f || zb <= 0.2f || zc <= 0.2f) continue;

        const float sa = fov / za;
        const float sb = fov / zb;
        const float sc = fov / zc;

        const float pax = centerX + a.x * sa;
        const float pay = centerY - a.y * sa;
        const float pbx = centerX + b.x * sb;
        const float pby = centerY - b.y * sb;
        const float pcx = centerX + c.x * sc;
        const float pcy = centerY - c.y * sc;

        // Screen-space signed area (Y-down): front faces have area2 < 0
        const float area2 = (pbx - pax) * (pcy - pay) - (pby - pay) * (pcx - pax);
        if (area2 >= 0.0f) continue;

        // Outward face normal = (c - a) x (b - a)
        const Vec3 u = {b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 v = {c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 n = Normalize({
            v.y * u.z - v.z * u.y,
            v.z * u.x - v.x * u.z,
            v.x * u.y - v.y * u.x
        });

        const float ndl = std::max(0.0f, n.x * keyLight.x + n.y * keyLight.y + n.z * keyLight.z);
        const float rim = std::max(0.0f, n.x * rimLight.x + n.y * rimLight.y + n.z * rimLight.z);
        const float spec = std::pow(ndl, 10.0f) * 0.68f;
        const float diffuse = 0.30f + 0.72f * ndl + 0.14f * rim;

        ProjectedTri tri{};
        tri.x[0] = pax; tri.y[0] = pay; tri.w[0] = 1.0f / za;
        tri.x[1] = pbx; tri.y[1] = pby; tri.w[1] = 1.0f / zb;
        tri.x[2] = pcx; tri.y[2] = pcy; tri.w[2] = 1.0f / zc;
        tri.avgZ = (za + zb + zc) * (1.0f / 3.0f);
        tri.r = std::min(255.0f, (face.baseColor.r * diffuse + kSpecColor.r * spec) * fade);
        tri.g = std::min(255.0f, (face.baseColor.g * diffuse + kSpecColor.g * spec) * fade);
        tri.b = std::min(255.0f, (face.baseColor.b * diffuse + kSpecColor.b * spec) * fade);
        visible.push_back(tri);
    }

    // Painter's sort: draw farthest triangles first
    std::sort(visible.begin(), visible.end(),
              [](const ProjectedTri& lhs, const ProjectedTri& rhs) {
                  return lhs.avgZ > rhs.avgZ;
              });

    for (const auto& tri : visible) {
        GrVertex v0 = MakeSplashVertex(tri.x[0], tri.y[0], tri.r, tri.g, tri.b, 255.0f, tri.w[0]);
        GrVertex v1 = MakeSplashVertex(tri.x[1], tri.y[1], tri.r, tri.g, tri.b, 255.0f, tri.w[1]);
        GrVertex v2 = MakeSplashVertex(tri.x[2], tri.y[2], tri.r, tri.g, tri.b, 255.0f, tri.w[2]);
        grDrawTriangle(&v0, &v1, &v2);
    }

    // 3. Italic "GLIDE" (chrome) + "NG" (amber) wordmark & gold accent rule
    const float scale = std::max(1.0f, vpH / 90.0f);
    // Total pixel-grid width: "GLIDE" (5*6 - 1 = 29 cells) + 3.5 gap + "NG" (2*6 - 1 = 11 cells) + 2 italic slant
    const float totalWordW = 45.5f * scale;
    const float wordX = centerX - totalWordW * 0.5f;
    const float wordY = vpY + vpH * 0.79f;

    DrawItalicWordmark2D("GLIDE", wordX, wordY, scale,
                         241.0f * fade, 245.0f * fade, 249.0f * fade);
    DrawItalicWordmark2D("NG", wordX + 32.5f * scale, wordY, scale,
                         255.0f * fade, 146.0f * fade, 36.0f * fade);

    // Subtle warm-gold underline beneath "GLIDE"
    const float barY = wordY + 8.0f * scale;
    const float barH = std::max(1.5f, scale * 0.65f);
    DrawQuad2D(wordX, barY, wordX + 29.0f * scale, barY + barH,
               255.0f * fade, 209.0f * fade, 102.0f * fade);
}

void GlideSplashAnimator::Render(float x, float y, float w, float h, FxU32 frame,
                                 void (*callback)(int frame)) {
    uint32_t winWidth = 640;
    uint32_t winHeight = 480;
    if (auto* b = BackendManager::GetInstance().GetBackend()) {
        winWidth = b->GetGuestWidth();
        winHeight = b->GetGuestHeight();
    }

    if (w <= 0.0f || h <= 0.0f || x < 0.0f || y < 0.0f ||
        x > static_cast<float>(winWidth) || (x + w) > static_cast<float>(winWidth) + 0.5f ||
        y > static_cast<float>(winHeight) || (y + h) > static_cast<float>(winHeight) + 0.5f) {
        return;
    }

    // Normalize caller viewport into GR_ORIGIN_UPPER_LEFT coordinates.
    float vpY = y;
    if (s_sstOrigin == GR_ORIGIN_LOWER_LEFT) {
        vpY = static_cast<float>(winHeight) - y - h;
    }

    // Glide 3.x GlideWrapperState is currently 512 bytes (ABI pad <= 1024 B),
    // whereas Glide 2.x's GrState struct is 528 bytes. Use an aligned 1024-byte
    // buffer so state save/restore has generous headroom across all frontends.
    alignas(16) uint8_t oldStateBuf[1024]{};
    auto* oldState = reinterpret_cast<GrState*>(oldStateBuf);
    grGlideGetState(oldState);

    grSstOrigin(GR_ORIGIN_UPPER_LEFT);
    grClipWindow(0, 0, winWidth, winHeight);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);
    grCullMode(GR_CULL_DISABLE);
    grFogMode(GR_FOG_DISABLE);
    grColorMask(FXTRUE, FXFALSE);

    if (frame == 0) {
        // Full 75-frame animation sequence (frames 1..74, invoking callback on each)
        for (int f = 1; f < static_cast<int>(kTotalFrames); ++f) {
            grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
            RenderSingleFrame(x, vpY, w, h, f, true);
            grBufferSwap(2);
            if (callback) {
                callback(f);
            }
        }
        // Leave both buffers clean black for the host application
        for (int i = 0; i < 2; ++i) {
            grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);
            grBufferSwap(0);
        }
    } else {
        // Single-frame mode (e.g., interactive demo stepping or overlay)
        const int f = static_cast<int>((frame - 1u) % (kTotalFrames - 1u)) + 1;
        RenderSingleFrame(x, vpY, w, h, f, true);
    }

    grGlideSetState(oldState);
}

void GlideSplashAnimator::BuildBannerRgb565(uint16_t* dst) {
    constexpr int W = static_cast<int>(kBannerWidth);
    constexpr int H = static_cast<int>(kBannerHeight);
    std::memset(dst, 0, static_cast<size_t>(W * H) * sizeof(uint16_t));

    // 1. Slanted Chrome & Dark-Slate outer badge body (18,16)-(172,16)-(162,74)-(8,74)
    for (int y = 16; y <= 74; ++y) {
        const float ty = static_cast<float>(y - 16) / 58.0f;
        const int xLeft  = static_cast<int>(std::lround(18.0f - 10.0f * ty));
        const int xRight = static_cast<int>(std::lround(172.0f - 10.0f * ty));

        // Vertical dark brushed-slate gradient with subtle metallic sheen band
        uint8_t baseR = LerpU8(40, 22, ty);
        uint8_t baseG = LerpU8(49, 27, ty);
        uint8_t baseB = LerpU8(66, 38, ty);
        if (y >= 24 && y <= 29) {
            baseR = static_cast<uint8_t>(std::min(255, static_cast<int>(baseR) + 12));
            baseG = static_cast<uint8_t>(std::min(255, static_cast<int>(baseG) + 14));
            baseB = static_cast<uint8_t>(std::min(255, static_cast<int>(baseB) + 18));
        }
        const uint16_t bodyPx = PackRgb565(baseR, baseG, baseB);
        const uint16_t hiBorderPx = PackRgb565(226, 234, 244); // Bright chrome top/left bevel
        const uint16_t loBorderPx = PackRgb565(156, 174, 196); // Brushed steel bottom/right bevel

        const bool isTopBorder = (y <= 18);
        const bool isBotBorder = (y >= 72);

        for (int x = xLeft; x <= xRight; ++x) {
            if (x < 0 || x >= W) continue;
            const bool isLeftBorder = (x <= xLeft + 2);
            const bool isRightBorder = (x >= xRight - 2);
            if (isTopBorder || isLeftBorder) {
                dst[y * W + x] = hiBorderPx;
            } else if (isBotBorder || isRightBorder) {
                dst[y * W + x] = loBorderPx;
            } else {
                dst[y * W + x] = bodyPx;
            }
        }
    }

    // 2. Right-side slanted Amber pill (122,22)-(164,22)-(156,68)-(114,68)
    for (int y = 22; y <= 68; ++y) {
        const float ty = static_cast<float>(y - 22) / 46.0f;
        const int xLeft  = static_cast<int>(std::lround(122.0f - 8.0f * ty));
        const int xRight = static_cast<int>(std::lround(164.0f - 8.0f * ty));

        uint16_t pillPx;
        if (y == 22) {
            pillPx = PackRgb565(255, 209, 102); // Warm gold top highlight edge
        } else {
            pillPx = PackRgb565(LerpU8(255, 224, ty), LerpU8(162, 112, ty), LerpU8(42, 12, ty));
        }
        for (int x = xLeft; x <= xRight; ++x) {
            if (x >= 0 && x < W) {
                dst[y * W + x] = pillPx;
            }
        }
    }

    // 3. Warm gold accent bar beneath "GLIDE"
    const uint16_t goldBarPx = PackRgb565(255, 209, 102);
    for (int y = 60; y <= 62; ++y) {
        for (int x = 22; x < 108; ++x) {
            dst[y * W + x] = goldBarPx;
        }
    }

    // 4. Italic pixel-block wordmark: chrome "GLIDE" + charcoal "NG" inside amber pill
    const uint16_t shadowPx = PackRgb565(12, 16, 24);
    const uint16_t chromePx = PackRgb565(248, 250, 252);
    const uint16_t darkInkPx = PackRgb565(17, 24, 39);

    RasterizeBannerGlyph(dst, W, H, "GLIDE", 23, 33, 3, shadowPx, 1);
    RasterizeBannerGlyph(dst, W, H, "GLIDE", 22, 32, 3, chromePx, 1);
    RasterizeBannerGlyph(dst, W, H, "NG", 123, 32, 3, darkInkPx, 1);
}

void GlideSplashAnimator::RenderBanner(uint32_t scrWidth, uint32_t scrHeight) {
    GLIDE_CPU_SCOPE(CpuBucket::Lfb);
    if (scrWidth < kBannerWidth || scrHeight < kBannerHeight) {
        return;
    }

    auto* backend = BackendManager::GetInstance().GetBackend();
    if (!backend) {
        return;
    }

    // Rasterize the 180x90 top-down RGB565 badge once, then composite directly
    // via WriteLFB without touching guest LFB locks or Glide pipeline state.
    static uint16_t s_bannerTopDown[kBannerWidth * kBannerHeight];
    static bool s_bannerInitialized = false;
    if (!s_bannerInitialized) {
        BuildBannerRgb565(s_bannerTopDown);
        s_bannerInitialized = true;
    }

    LfbPipelineConfig cfg;
    cfg.pixelPipeline = true;
    cfg.chromakeyEnabled = true;
    cfg.chromakeyValue = 0x00000000u;  // Pure black transparent chromakey
    cfg.constantAlpha = 90;
    cfg.rgbSrcBlend = 1;  // GR_BLEND_SRC_ALPHA
    cfg.rgbDstBlend = 5;  // GR_BLEND_ONE_MINUS_SRC_ALPHA

    const uint32_t dstX = scrWidth - kBannerWidth;
    const uint32_t dstY = (scrHeight > kBannerHeight) ? (scrHeight - 1 - kBannerHeight) : 0;
    backend->WriteLFB(1 /* GR_BUFFER_BACKBUFFER */, dstX, dstY, kBannerWidth,
                      kBannerHeight, static_cast<int32_t>(kBannerWidth * sizeof(uint16_t)),
                      0 /* GR_LFB_SRC_FMT_565 */, s_bannerTopDown, nullptr, cfg);
}

} // namespace GlideWrapper
