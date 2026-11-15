#include "OpenGLESBackend.h"
#include "OpenGLESInternal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Logger.h"

namespace GlideWrapper {

const char* vsSource = R"(#version 300 es
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTex;
out vec2 fragTex;
void main() {
    fragTex = inTex;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
)";

const char* fsSource = R"(#version 300 es
precision highp float;
in vec2 fragTex;
out vec4 outColor;
uniform sampler2D uTex;
uniform sampler2D uGammaLut;
uniform bool uUseGammaLut;
void main() {
    vec4 texColor = texture(uTex, fragTex);
    if (uUseGammaLut) {
        vec3 corrected;
        corrected.r = texture(uGammaLut, vec2((texColor.r * 255.0 + 0.5) / 256.0, 0.5)).r;
        corrected.g = texture(uGammaLut, vec2((texColor.g * 255.0 + 0.5) / 256.0, 0.5)).g;
        corrected.b = texture(uGammaLut, vec2((texColor.b * 255.0 + 0.5) / 256.0, 0.5)).b;
        outColor = vec4(corrected, 1.0);
    } else {
        outColor = vec4(texColor.rgb, 1.0);
    }
}
)";

static const char* geomVsSource = R"(#version 300 es
layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTex;
layout(location = 3) in vec2 inTmuOow;
layout(location = 4) in float inFogCoord;

out vec4 fragColor;
out float fragOOW;
out vec4 fragTexCoord; // Passes s0, t0, s1, t1
out vec2 fragTmuOow;
out float fragFogCoord;

uniform int uSstOrigin; // 0 = UPPER_LEFT, 1 = LOWER_LEFT
uniform vec2 uViewportSize;
uniform int uDepthClipOverride;
uniform highp int uDepthBufferMode; // 0=None, 1=Z, 2=W
uniform float uDepthBias;

void main() {
    // Convert Glide window-space coordinates to GLES NDC (Y-up)
    float ndcX = (inPos.x / (uViewportSize.x / 2.0)) - 1.0;
    float ndcY;
    if (uSstOrigin == 0) { // GR_ORIGIN_UPPER_LEFT
        ndcY = 1.0 - (inPos.y / (uViewportSize.y / 2.0));
    } else {              // GR_ORIGIN_LOWER_LEFT
        ndcY = (inPos.y / (uViewportSize.y / 2.0)) - 1.0;
    }
    
    float rawDepth = inPos.z;
    if (uDepthBufferMode == 2) {
        rawDepth = 1.0 - inPos.w; // W-buffer mapping: 1.0 - oow
    }

    rawDepth += uDepthBias;

    float ndcZ;
    if (uDepthClipOverride == 1) {
        ndcZ = 0.0; // Force to middle of NDC clip volume if depth testing is disabled (prevents stack garbage clipping)
    } else {
        ndcZ = rawDepth * 2.0 - 1.0;
    }

    gl_Position = vec4(ndcX, ndcY, ndcZ, 1.0);
    gl_PointSize = 1.0; // Canonical Glide point size (1px)
    fragColor = inColor;
    fragOOW = inPos.w;
    fragTexCoord = inTex; // Pass all 4 coordinates!
    fragTmuOow = inTmuOow;
    fragFogCoord = inFogCoord;
}
)";

static const char* geomFsSource = R"(#version 300 es
#extension GL_EXT_blend_func_extended : enable
precision highp float;

in vec4 fragColor;
in float fragOOW;
in vec4 fragTexCoord; // Passes s0, t0, s1, t1
in vec2 fragTmuOow;
in float fragFogCoord;

#ifdef GL_EXT_blend_func_extended
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outPreFogColor;
#else
out vec4 outColor;
#endif

uniform vec4 uConstantColor;

// Combiner Uniforms
uniform int uColorFunc;
uniform int uColorFactor;
uniform int uColorLocal;
uniform int uColorOther;
uniform int uColorInvert;

uniform int uAlphaFunc;
uniform int uAlphaFactor;
uniform int uAlphaLocal;
uniform int uAlphaOther;
uniform int uAlphaInvert;

uniform int uTextureEnabled[2]; // Array of size 2
uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform float uTexLodBias[2];
uniform int uUseTmuOow[2]; // Array of size 2

// New TMU combiner uniforms
uniform int uTexRgbFunc[2];
uniform int uTexRgbFactor[2];
uniform int uTexAlphaFunc[2];
uniform int uTexAlphaFactor[2];
uniform int uTexRgbInvert[2];
uniform int uTexAlphaInvert[2];

uniform int uChromakeyEnabled;
uniform vec4 uChromakeyValue;
uniform int uChromakeyRangeEnabled;
uniform vec3 uChromakeyRangeMin;
uniform vec3 uChromakeyRangeMax;
uniform ivec3 uChromakeyRangeExclusive;
uniform int uChromakeyRangeUnion;

uniform uint uTexChromaEnabled[2];
uniform uint uTexChromaRangeMode[2];
uniform vec4 uTexChromaMin[2];
uniform vec4 uTexChromaMax[2];

uniform int uAlphaTestOp;
uniform float uAlphaTestRef;

// Milestone 8: Fogging Uniforms
uniform int uFogMode;
uniform vec4 uFogColor;
uniform float uFogTable[64];

// Depth Buffer Mode Uniform for Clipping
uniform highp int uDepthBufferMode;

uniform float uDepthNear;
uniform float uDepthFar;
uniform uint uDitherMode;
uniform uint uStippleMode;
uniform uint uStipplePattern;
uniform vec2 uViewportSize;

const float s_tableIndexToW[64] = float[](
    1.000000,      1.142857,      1.333333,      1.600000, 
    2.000000,      2.285714,      2.666667,      3.200000, 
    4.000000,      4.571429,      5.333333,      6.400000, 
    8.000000,      9.142858,     10.666667,     12.800000, 
    16.000000,     18.285715,     21.333334,     25.600000, 
    32.000000,     36.571430,     42.666668,     51.200001, 
    64.000000,     73.142860,     85.333336,    102.400002, 
    128.000000,    146.285721,    170.666672,    204.800003, 
    256.000000,    292.571442,    341.333344,    409.600006, 
    512.000000,    585.142883,    682.666687,    819.200012, 
    1024.000000,   1170.285767,   1365.333374,   1638.400024, 
    2048.000000,   2340.571533,   2730.666748,   3276.800049, 
    4096.000000,   4681.143066,   5461.333496,   6553.600098, 
    8192.000000,   9362.286133,  10922.666992,  13107.200195, 
    16384.000000,  18724.572266,  21845.333984,  26214.400391, 
    32768.000000,  37449.144531,  43690.667969,  52428.800781
);

bool IsColorInRange(vec3 color, vec3 minColor, vec3 maxColor, uint rangeMode) {
    bool rangeEnabled = ((rangeMode >> 28u) & 1u) == 1u;
    if (rangeEnabled) {
        bool rMatch = (color.r >= minColor.r && color.r <= maxColor.r);
        bool gMatch = (color.g >= minColor.g && color.g <= maxColor.g);
        bool bMatch = (color.b >= minColor.b && color.b <= maxColor.b);
        
        bool blueExcl  = ((rangeMode >> 24u) & 1u) == 1u;
        bool greenExcl = ((rangeMode >> 25u) & 1u) == 1u;
        bool redExcl   = ((rangeMode >> 26u) & 1u) == 1u;
        
        bool rRes = rMatch != redExcl;
        bool gRes = gMatch != greenExcl;
        bool bRes = bMatch != blueExcl;
        
        bool unionMode = ((rangeMode >> 27u) & 1u) == 1u;
        if (unionMode) {
            return rRes || gRes || bRes;
        } else {
            return rRes && gRes && bRes;
        }
    } else {
        return distance(color, minColor) < 0.01;
    }
}

vec4 sampleTextureGLES(sampler2D tex, vec2 uv, int tmuIdx, float tw, float th) {
    float maxDim = max(tw, th);
    vec2 finalUv = uv;
    finalUv.x *= (maxDim / tw);
    finalUv.y *= (maxDim / th);
    if (uTexChromaEnabled[tmuIdx] == 1u) {
        ivec2 size = ivec2(int(tw), int(th));
        vec2 texelCoord = finalUv * vec2(tw, th) - vec2(0.5);
        ivec2 tl = ivec2(floor(texelCoord));
        vec2 f = fract(texelCoord);
        ivec2 c00_coord = clamp(tl + ivec2(0, 0), ivec2(0), size - ivec2(1));
        ivec2 c10_coord = clamp(tl + ivec2(1, 0), ivec2(0), size - ivec2(1));
        ivec2 c01_coord = clamp(tl + ivec2(0, 1), ivec2(0), size - ivec2(1));
        ivec2 c11_coord = clamp(tl + ivec2(1, 1), ivec2(0), size - ivec2(1));
        vec4 c00 = texelFetch(tex, c00_coord, 0);
        vec4 c10 = texelFetch(tex, c10_coord, 0);
        vec4 c01 = texelFetch(tex, c01_coord, 0);
        vec4 c11 = texelFetch(tex, c11_coord, 0);
        if (IsColorInRange(c00.rgb, uTexChromaMin[tmuIdx].rgb, uTexChromaMax[tmuIdx].rgb, uTexChromaRangeMode[tmuIdx])) c00 = vec4(0.0);
        if (IsColorInRange(c10.rgb, uTexChromaMin[tmuIdx].rgb, uTexChromaMax[tmuIdx].rgb, uTexChromaRangeMode[tmuIdx])) c10 = vec4(0.0);
        if (IsColorInRange(c01.rgb, uTexChromaMin[tmuIdx].rgb, uTexChromaMax[tmuIdx].rgb, uTexChromaRangeMode[tmuIdx])) c01 = vec4(0.0);
        if (IsColorInRange(c11.rgb, uTexChromaMin[tmuIdx].rgb, uTexChromaMax[tmuIdx].rgb, uTexChromaRangeMode[tmuIdx])) c11 = vec4(0.0);
        vec4 c0 = mix(c00, c10, f.x);
        vec4 c1 = mix(c01, c11, f.x);
        return mix(c0, c1, f.y);
    } else {
        return texture(tex, finalUv, uTexLodBias[tmuIdx]);
    }
}

vec4 evaluateTmuStageGLES(
    int rgbFunc, int rgbFactor, int alphaFunc, int alphaFactor,
    bool rgbInvert, bool alphaInvert,
    vec4 localVal, vec4 otherVal) {
    
    // 1. Evaluate Alpha
    float aLocal = localVal.a;
    float aOther = otherVal.a;
    
    float factA = 0.0;
    if (alphaFactor == 1 || alphaFactor == 3) factA = aLocal;
    else if (alphaFactor == 2) factA = aOther;
    else if (alphaFactor == 4) factA = localVal.a;
    else if (alphaFactor == 8) factA = 1.0;
    else if (alphaFactor == 9 || alphaFactor == 11) factA = 1.0 - aLocal;
    else if (alphaFactor == 10) factA = 1.0 - aOther;
    else if (alphaFactor == 12) factA = 1.0 - localVal.a;
    
    float finalA = 0.0;
    if (alphaFunc == 1) finalA = aLocal;
    else if (alphaFunc == 3) finalA = aOther * factA;
    else if (alphaFunc == 4 || alphaFunc == 5) finalA = aOther * factA + aLocal;
    else if (alphaFunc == 6) finalA = (aOther - aLocal) * factA;
    else if (alphaFunc == 7 || alphaFunc == 8) finalA = (aOther - aLocal) * factA + aLocal;
    else if (alphaFunc == 9 || alphaFunc == 16) finalA = (aLocal - aOther) * factA + aOther;
    
    if (alphaInvert) finalA = 1.0 - finalA;
    
    // 2. Evaluate RGB
    vec3 cLocal = localVal.rgb;
    vec3 cOther = otherVal.rgb;
    
    vec3 factC = vec3(0.0);
    if (rgbFactor == 1) factC = cLocal;
    else if (rgbFactor == 2) factC = vec3(aOther);
    else if (rgbFactor == 3) factC = vec3(aLocal);
    else if (rgbFactor == 4) factC = vec3(localVal.a);
    else if (rgbFactor == 5) factC = localVal.rgb;
    else if (rgbFactor == 8) factC = vec3(1.0);
    else if (rgbFactor == 9) factC = vec3(1.0) - cLocal;
    else if (rgbFactor == 10) factC = vec3(1.0 - aOther);
    else if (rgbFactor == 11) factC = vec3(1.0 - aLocal);
    else if (rgbFactor == 12) factC = vec3(1.0 - localVal.a);
    
    vec3 finalC = vec3(0.0);
    if (rgbFunc == 1) finalC = cLocal;
    else if (rgbFunc == 3) finalC = cOther * factC;
    else if (rgbFunc == 4) finalC = cOther * factC + cLocal;
    else if (rgbFunc == 5) finalC = cOther * factC + vec3(aLocal);
    else if (rgbFunc == 6) finalC = (cOther - cLocal) * factC;
    else if (rgbFunc == 7) finalC = (cOther - cLocal) * factC + cLocal;
    else if (rgbFunc == 8) finalC = (cOther - cLocal) * factC + vec3(aLocal);
    else if (rgbFunc == 9) finalC = (cLocal - cOther) * factC + cOther;
    else if (rgbFunc == 16) finalC = (vec3(aLocal) - cOther) * factC + cOther;
    
    if (rgbInvert) finalC = vec3(1.0) - finalC;
    
    return vec4(finalC, finalA);
}

void main() {
    // Stipple Transparency Emulation
    if (uStippleMode == 1u) { // GR_STIPPLE_PATTERN
        int stippleX = int(gl_FragCoord.x) & 7;
        int stippleY = int(gl_FragCoord.y) & 3;
        int stippleBitIdx = (stippleY << 3) | (7 - stippleX);
        uint bit = (uStipplePattern >> uint(stippleBitIdx)) & 1u;
        if (bit == 0u) {
            discard;
        }
    } else if (uStippleMode == 2u) { // GR_STIPPLE_ROTATE
        int stippleX = (int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 7;
        int stippleY = int(gl_FragCoord.y) & 3;
        int stippleBitIdx = (stippleY << 3) | (7 - stippleX);
        uint bit = (uStipplePattern >> uint(stippleBitIdx)) & 1u;
        if (bit == 0u) {
            discard;
        }
    }

    // W-based coordinate near-plane and camera-space clipping
    if (uDepthBufferMode == 2) {
        if (fragOOW > 1.0) discard;
    } else if (uTextureEnabled[0] == 1 || uTextureEnabled[1] == 1) {
        if (fragOOW <= 0.0) discard;
    }

    // Unpack texture active flags
    bool tmu0Active = uTextureEnabled[0] == 1;
    bool tmu1Active = uTextureEnabled[1] == 1;

    // 1. Sample TMU 1 (upstream)
    vec4 tmu1Color = vec4(1.0);
    if (tmu1Active) {
        ivec2 texSize = textureSize(uTexture1, 0);
        float tw = float(texSize.x);
        float th = float(texSize.y);
        float divW1 = (uUseTmuOow[1] == 1) ? fragTmuOow.y : fragOOW;
        vec2 uv1 = (fragTexCoord.zw / divW1) / 256.0;
        tmu1Color = sampleTextureGLES(uTexture1, uv1, 1, tw, th);
    }

    // Evaluate TMU 1 combiner stage (upstream, passes iterated)
    vec4 tmu1Out = tmu1Active ? evaluateTmuStageGLES(
        uTexRgbFunc[1], uTexRgbFactor[1], uTexAlphaFunc[1], uTexAlphaFactor[1],
        uTexRgbInvert[1] == 1, uTexAlphaInvert[1] == 1,
        tmu1Color, fragColor
    ) : fragColor;

    // 2. Sample TMU 0 (downstream)
    vec4 tmu0Color = vec4(1.0);
    if (tmu0Active) {
        ivec2 texSize = textureSize(uTexture0, 0);
        float tw = float(texSize.x);
        float th = float(texSize.y);
        float divW0 = (uUseTmuOow[0] == 1) ? fragTmuOow.x : fragOOW;
        vec2 uv0 = (fragTexCoord.xy / divW0) / 256.0;
        tmu0Color = sampleTextureGLES(uTexture0, uv0, 0, tw, th);
    }

    // Evaluate TMU 0 combiner stage (downstream, other is tmu1Out)
    vec4 tmu0Out = tmu0Active ? evaluateTmuStageGLES(
        uTexRgbFunc[0], uTexRgbFactor[0], uTexAlphaFunc[0], uTexAlphaFactor[0],
        uTexRgbInvert[0] == 1, uTexAlphaInvert[0] == 1,
        tmu0Color, tmu1Out
    ) : tmu1Out;

    vec4 texColor = tmu0Out;

    // 2. Route Alpha Combiner inputs
    float aLocal = 1.0;
    if (uAlphaLocal == 0) aLocal = fragColor.a;
    else if (uAlphaLocal == 1) aLocal = uConstantColor.a;

    float aOther = 1.0;
    if (uAlphaOther == 0) aOther = fragColor.a;
    else if (uAlphaOther == 1) aOther = texColor.a;
    else if (uAlphaOther == 2) aOther = uConstantColor.a;

    // 3. Evaluate Alpha Combiner Factor
    float factA = 0.0;
    if (uAlphaFactor == 1 || uAlphaFactor == 3) factA = aLocal;
    else if (uAlphaFactor == 2) factA = aOther;
    else if (uAlphaFactor == 4) factA = texColor.a;
    else if (uAlphaFactor == 8) factA = 1.0;
    else if (uAlphaFactor == 9 || uAlphaFactor == 11) factA = 1.0 - aLocal;
    else if (uAlphaFactor == 10) factA = 1.0 - aOther;
    else if (uAlphaFactor == 12) factA = 1.0 - texColor.a;

    // 4. Evaluate Alpha Combiner Function
    float finalA = 0.0;
    if (uAlphaFunc == 1) finalA = aLocal;
    else if (uAlphaFunc == 3) finalA = aOther * factA;
    else if (uAlphaFunc == 4 || uAlphaFunc == 5) finalA = aOther * factA + aLocal;
    else if (uAlphaFunc == 6) finalA = (aOther - aLocal) * factA;
    else if (uAlphaFunc == 7 || uAlphaFunc == 8) finalA = (aOther - aLocal) * factA + aLocal;
    else if (uAlphaFunc == 9 || uAlphaFunc == 16) finalA = (aLocal - aOther) * factA + aOther;
    
    if (uAlphaInvert == 1) finalA = 1.0 - finalA;

    // 5. Route Color Combiner inputs
    vec3 cLocal = vec3(0.0);
    if (uColorLocal == 0) cLocal = fragColor.rgb;
    else if (uColorLocal == 1) cLocal = uConstantColor.rgb;

    vec3 cOther = vec3(0.0);
    if (uColorOther == 0) cOther = fragColor.rgb;
    else if (uColorOther == 1) cOther = texColor.rgb;
    else if (uColorOther == 2) cOther = uConstantColor.rgb;

    // 6. Evaluate Color Combiner Factor
    vec3 factC = vec3(0.0);
    if (uColorFactor == 1) factC = cLocal;
    else if (uColorFactor == 2) factC = vec3(aOther);
    else if (uColorFactor == 3) factC = vec3(aLocal);
    else if (uColorFactor == 4) factC = vec3(texColor.a);
    else if (uColorFactor == 5) factC = texColor.rgb;
    else if (uColorFactor == 8) factC = vec3(1.0);
    else if (uColorFactor == 9) factC = vec3(1.0) - cLocal;
    else if (uColorFactor == 10) factC = vec3(1.0 - aOther);
    else if (uColorFactor == 11) factC = vec3(1.0 - aLocal);
    else if (uColorFactor == 12) factC = vec3(1.0 - texColor.a);

    // 7. Evaluate Color Combiner Function
    vec3 finalC = vec3(0.0);
    if (uColorFunc == 1) finalC = cLocal;
    else if (uColorFunc == 3) finalC = cOther * factC;
    else if (uColorFunc == 4) finalC = cOther * factC + cLocal;
    else if (uColorFunc == 5) finalC = cOther * factC + vec3(aLocal);
    else if (uColorFunc == 6) finalC = (cOther - cLocal) * factC;
    else if (uColorFunc == 7) finalC = (cOther - cLocal) * factC + cLocal;
    else if (uColorFunc == 8) finalC = (cOther - cLocal) * factC + vec3(aLocal);
    else if (uColorFunc == 9) finalC = (cLocal - cOther) * factC + cOther;
    else if (uColorFunc == 16) finalC = (vec3(aLocal) - cOther) * factC + cOther;

    if (uColorInvert == 1) finalC = vec3(1.0) - finalC;

    vec4 col = vec4(finalC, finalA);

    // Milestone 10: Chromakey Emulation
    if (uChromakeyEnabled == 1) {
        vec3 chromaTestColor = (uTextureEnabled[0] == 1 || uTextureEnabled[1] == 1) ? cOther : col.rgb;
        if (uChromakeyRangeEnabled == 1) {
            bool rMatch = (chromaTestColor.r >= uChromakeyRangeMin.r && chromaTestColor.r <= uChromakeyRangeMax.r);
            bool gMatch = (chromaTestColor.g >= uChromakeyRangeMin.g && chromaTestColor.g <= uChromakeyRangeMax.g);
            bool bMatch = (chromaTestColor.b >= uChromakeyRangeMin.b && chromaTestColor.b <= uChromakeyRangeMax.b);
            
            bool rRes = rMatch != (uChromakeyRangeExclusive.x != 0);
            bool gRes = gMatch != (uChromakeyRangeExclusive.y != 0);
            bool bRes = bMatch != (uChromakeyRangeExclusive.z != 0);
            
            bool discardPixel = false;
            if (uChromakeyRangeUnion == 1) {
                discardPixel = rRes || gRes || bRes;
            } else {
                discardPixel = rRes && gRes && bRes;
            }
            
            if (discardPixel) {
                discard;
            }
        } else {
            if (distance(chromaTestColor, uChromakeyValue.rgb) < 0.01) {
                discard;
            }
        }
    }

    vec4 preFogColor = col;

    // Milestone 8: Fogging Emulation
    int fogSource = uFogMode & 0x0F;
    if (fogSource != 0) {
        float f = 0.0;
        if (fogSource == 4) { // UNIFIED_FOG_WITH_ITERATED_ALPHA
            f = fragColor.a; // Raw interpolated vertex alpha
        } else { // Table-based fog
            float eyeW;
            if (fogSource == 1) { // UNIFIED_FOG_WITH_TABLE_ON_FOGCOORD
                eyeW = fragFogCoord;
            } else { // UNIFIED_FOG_WITH_TABLE_ON_W
                eyeW = 1.0 / fragOOW;
            }
            if (eyeW < 1.0) eyeW = 1.0;

            // O(1) unrolled logarithmic binary search
            int idx = 0;
            if (eyeW >= s_tableIndexToW[32]) idx += 32;
            if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
            if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
            if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
            if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
            if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

            idx = min(idx, 62);

            float w0 = s_tableIndexToW[idx];
            float w1 = s_tableIndexToW[idx + 1];
            float f0 = uFogTable[idx];
            float f1 = uFogTable[idx + 1];

            float t = 0.0;
            if (w1 > w0) {
                t = (eyeW - w0) / (w1 - w0);
            }
            t = clamp(t, 0.0, 1.0);

            f = mix(f0, f1, t);
        }

        // Extract FOGMODE flags
        float mult = float((uFogMode & 0x100) != 0); // GR_FOG_MULT2 (SST_FOGMULT)
        float add  = float((uFogMode & 0x200) != 0); // GR_FOG_ADD2  (SST_FOGADD)

        // Apply the unified branch-free hardware equation
        col.rgb = col.rgb * (1.0 - mult) * (1.0 - f) + uFogColor.rgb * (1.0 - add) * f;
    }

    // Emulate legacy 3dfx Glide alpha testing
    if (uAlphaTestOp != 7) {
        float alpha = col.a;
        float refVal = uAlphaTestRef;
        bool passed = true;

        if (uAlphaTestOp == 0) passed = false; // GR_CMP_NEVER
        else if (uAlphaTestOp == 1) passed = (alpha < refVal); // GR_CMP_LESS
        else if (uAlphaTestOp == 2) passed = (alpha == refVal); // GR_CMP_EQUAL
        else if (uAlphaTestOp == 3) passed = (alpha <= refVal); // GR_CMP_LEQUAL
        else if (uAlphaTestOp == 4) passed = (alpha > refVal); // GR_CMP_GREATER
        else if (uAlphaTestOp == 5) passed = (alpha != refVal); // GR_CMP_NOTEQUAL
        else if (uAlphaTestOp == 6) passed = (alpha >= refVal); // GR_CMP_GEQUAL
        
        if (!passed) {
            discard;
        }
    }

    // 16-Bit Dithering Emulation
    if (uDitherMode != 0u) {
        int x = int(gl_FragCoord.x) & 3;
        int y = int(gl_FragCoord.y) & 3;
        int d = 0;
        if (uDitherMode == 2u) { // GR_DITHER_4x4
            int idx = (y << 2) | x;
            if (idx == 0) d = 0;
            else if (idx == 1) d = 8;
            else if (idx == 2) d = 2;
            else if (idx == 3) d = 10;
            else if (idx == 4) d = 12;
            else if (idx == 5) d = 4;
            else if (idx == 6) d = 14;
            else if (idx == 7) d = 6;
            else if (idx == 8) d = 3;
            else if (idx == 9) d = 11;
            else if (idx == 10) d = 1;
            else if (idx == 11) d = 9;
            else if (idx == 12) d = 15;
            else if (idx == 13) d = 7;
            else if (idx == 14) d = 13;
            else if (idx == 15) d = 5;
        } else if (uDitherMode == 1u) { // GR_DITHER_2x2
            int idx = ((y & 1) << 1) | (x & 1);
            if (idx == 0) d = 0;
            else if (idx == 1) d = 8;
            else if (idx == 2) d = 12;
            else if (idx == 3) d = 4;
        }
        
        vec3 c255 = col.rgb * 255.0;
        float a255 = col.a * 255.0;
        bool useAlpha = (uAlphaTestOp != 7) || (col.a < 0.99);
        
        if (useAlpha) {
            // ARGB1555
            int matrixSize = (uDitherMode == 2u) ? 16 : 4;
            int ditherA = (d * 128) / matrixSize;
            
            int r_dither = d >> 1;
            int g_dither = d >> 1;
            int b_dither = d >> 1;
            
            int r = int(c255.r) + r_dither;
            int g = int(c255.g) + g_dither;
            int b = int(c255.b) + b_dither;
            int a = int(a255) + ditherA;
            
            r = clamp(r, 0, 255) & 0xF8;
            g = clamp(g, 0, 255) & 0xF8;
            b = clamp(b, 0, 255) & 0xF8;
            
            int a1 = clamp(a, 0, 255) / 128;
            a1 = min(a1, 1);
            
            col.r = float(r | (r >> 5)) / 255.0;
            col.g = float(g | (g >> 5)) / 255.0;
            col.b = float(b | (b >> 5)) / 255.0;
            col.a = float(a1 * 255) / 255.0;
        } else {
            // RGB565
            int r_dither = d >> 1;
            int g_dither = d >> 2;
            int b_dither = d >> 1;
            
            int r = int(c255.r) + r_dither;
            int g = int(c255.g) + g_dither;
            int b = int(c255.b) + b_dither;
            
            r = clamp(r, 0, 255) & 0xF8;
            g = clamp(g, 0, 255) & 0xFC;
            b = clamp(b, 0, 255) & 0xF8;
            
            col.r = float(r | (r >> 5)) / 255.0;
            col.g = float(g | (g >> 6)) / 255.0;
            col.b = float(b | (b >> 5)) / 255.0;
            col.a = 1.0;
        }
    }

    // True W-Buffering Depth Write
    if (uDepthBufferMode == 2) { // 2 = W-buffer
        float eyeW = 1.0 / fragOOW;
        float normalizedW = (eyeW - uDepthNear) / (uDepthFar - uDepthNear);
        gl_FragDepth = clamp(normalizedW, 0.0, 1.0);
    } else {
        gl_FragDepth = gl_FragCoord.z;
    }

    outColor = col;
#ifdef GL_EXT_blend_func_extended
    outPreFogColor = preFogColor;
#endif
}
)";

bool OpenGLESBackend::CreateGeomShaders() {
  auto compileShader = [](GLenum type, const char* src) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint status;
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) {
      char log[512];
      glGetShaderInfoLog(s, 512, nullptr, log);
      GLIDE_LOG(CRITICAL, "GLES",
                "Geometry Shader compilation failure: " << log);
    }
    return s;
  };

  GLuint vs = compileShader(GL_VERTEX_SHADER, geomVsSource);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, geomFsSource);

  m_geomProgram = glCreateProgram();
  glAttachShader(m_geomProgram, vs);
  glAttachShader(m_geomProgram, fs);
  glLinkProgram(m_geomProgram);

  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linkStatus;
  glGetProgramiv(m_geomProgram, GL_LINK_STATUS, &linkStatus);
  if (!linkStatus) {
    char log[512];
    glGetProgramInfoLog(m_geomProgram, 512, nullptr, log);
    GLIDE_LOG(CRITICAL, "GLES", "Geometry Program linking failure: " << log);
    return false;
  }

  // Resolve and cache all uniform locations once
  m_uSstOriginLoc = glGetUniformLocation(m_geomProgram, "uSstOrigin");
  m_uViewportSizeLoc = glGetUniformLocation(m_geomProgram, "uViewportSize");
  m_uDepthClipOverrideLoc =
      glGetUniformLocation(m_geomProgram, "uDepthClipOverride");
  m_uDepthBufferModeLoc =
      glGetUniformLocation(m_geomProgram, "uDepthBufferMode");
  m_uConstantColorLoc = glGetUniformLocation(m_geomProgram, "uConstantColor");
  m_uChromakeyEnabledLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyEnabled");
  m_uChromakeyValueLoc = glGetUniformLocation(m_geomProgram, "uChromakeyValue");
  m_uChromakeyRangeEnabledLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyRangeEnabled");
  m_uChromakeyRangeMinLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyRangeMin");
  m_uChromakeyRangeMaxLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyRangeMax");
  m_uChromakeyRangeExclusiveLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyRangeExclusive");
  m_uChromakeyRangeUnionLoc =
      glGetUniformLocation(m_geomProgram, "uChromakeyRangeUnion");
  m_uColorFuncLoc = glGetUniformLocation(m_geomProgram, "uColorFunc");
  m_uColorFactorLoc = glGetUniformLocation(m_geomProgram, "uColorFactor");
  m_uColorLocalLoc = glGetUniformLocation(m_geomProgram, "uColorLocal");
  m_uColorOtherLoc = glGetUniformLocation(m_geomProgram, "uColorOther");
  m_uColorInvertLoc = glGetUniformLocation(m_geomProgram, "uColorInvert");
  m_uAlphaFuncLoc = glGetUniformLocation(m_geomProgram, "uAlphaFunc");
  m_uAlphaFactorLoc = glGetUniformLocation(m_geomProgram, "uAlphaFactor");
  m_uAlphaLocalLoc = glGetUniformLocation(m_geomProgram, "uAlphaLocal");
  m_uAlphaOtherLoc = glGetUniformLocation(m_geomProgram, "uAlphaOther");
  m_uAlphaInvertLoc = glGetUniformLocation(m_geomProgram, "uAlphaInvert");
  m_uTextureEnabledLoc = glGetUniformLocation(m_geomProgram, "uTextureEnabled");
  m_uTexture0Loc = glGetUniformLocation(m_geomProgram, "uTexture0");
  m_uTexture1Loc = glGetUniformLocation(m_geomProgram, "uTexture1");
  m_uUseTmuOowLoc = glGetUniformLocation(m_geomProgram, "uUseTmuOow");
  m_uTexRgbFuncLoc = glGetUniformLocation(m_geomProgram, "uTexRgbFunc");
  m_uTexRgbFactorLoc = glGetUniformLocation(m_geomProgram, "uTexRgbFactor");
  m_uTexAlphaFuncLoc = glGetUniformLocation(m_geomProgram, "uTexAlphaFunc");
  m_uTexAlphaFactorLoc = glGetUniformLocation(m_geomProgram, "uTexAlphaFactor");
  m_uTexRgbInvertLoc = glGetUniformLocation(m_geomProgram, "uTexRgbInvert");
  m_uTexAlphaInvertLoc = glGetUniformLocation(m_geomProgram, "uTexAlphaInvert");
  m_uAlphaTestOpLoc = glGetUniformLocation(m_geomProgram, "uAlphaTestOp");
  m_uAlphaTestRefLoc = glGetUniformLocation(m_geomProgram, "uAlphaTestRef");
  m_uDepthBiasLoc = glGetUniformLocation(m_geomProgram, "uDepthBias");
  m_uFogModeLoc = glGetUniformLocation(m_geomProgram, "uFogMode");
  m_uFogColorLoc = glGetUniformLocation(m_geomProgram, "uFogColor");
  m_uFogTableLoc = glGetUniformLocation(m_geomProgram, "uFogTable");
  m_uTexChromaEnabledLoc =
      glGetUniformLocation(m_geomProgram, "uTexChromaEnabled");
  m_uTexChromaRangeModeLoc =
      glGetUniformLocation(m_geomProgram, "uTexChromaRangeMode");
  m_uTexChromaMinLoc = glGetUniformLocation(m_geomProgram, "uTexChromaMin");
  m_uTexChromaMaxLoc = glGetUniformLocation(m_geomProgram, "uTexChromaMax");
  m_uTexLodBiasLoc = glGetUniformLocation(m_geomProgram, "uTexLodBias");
  m_uDepthNearLoc = glGetUniformLocation(m_geomProgram, "uDepthNear");
  m_uDepthFarLoc = glGetUniformLocation(m_geomProgram, "uDepthFar");
  m_uDitherModeLoc = glGetUniformLocation(m_geomProgram, "uDitherMode");
  m_uStippleModeLoc = glGetUniformLocation(m_geomProgram, "uStippleMode");
  m_uStipplePatternLoc = glGetUniformLocation(m_geomProgram, "uStipplePattern");

  // Log critical uniform location failure
  if (m_uSstOriginLoc == -1 || m_uViewportSizeLoc == -1 ||
      m_uColorFuncLoc == -1 || m_uAlphaFuncLoc == -1 ||
      m_uTextureEnabledLoc == -1) {
    GLIDE_LOG(CRITICAL, "GLES",
              "Critical uniform location query failed! Some uniforms could not "
              "be resolved.");
  }

  checkGLError("CreateGeomShaders");
  return true;
}

void OpenGLESBackend::PopulateUniforms() {
  glUniform1i(m_uSstOriginLoc, (int)m_sstOrigin);
  glUniform2f(m_uViewportSizeLoc, (float)m_headlessWidth,
              (float)m_headlessHeight);
  bool depthClipOverride =
      (m_depthMode == 0) ||
      (m_depthCompareOp == 7 &&
       (!m_depthMask ||
        m_is2DGeometry));  // 7 = GR_CMP_ALWAYS (prevents uninitialized ooz
                           // stack garbage clipping in text overlays, but only
                           // if depth writes are disabled OR if it's 2D
                           // geometry!)
  glUniform1i(m_uDepthClipOverrideLoc, depthClipOverride ? 1 : 0);
  GLint dbgLoc = m_uDepthBufferModeLoc;
  GLIDE_LOG(DEBUG, "GLES",
            "Uploading uDepthBufferMode. Location: " << dbgLoc << ", Value: "
                                                     << m_depthMode);
  glUniform1i(dbgLoc, m_depthMode);

  float cr = ((m_constantColor >> 16) & 0xFF) / 255.0f;
  float cg = ((m_constantColor >> 8) & 0xFF) / 255.0f;
  float cb = (m_constantColor & 0xFF) / 255.0f;
  float ca = ((m_constantColor >> 24) & 0xFF) / 255.0f;
  if (m_pixelFormatOverride == 1) {
    std::swap(cr, cb);
  }
  glUniform4f(m_uConstantColorLoc, cr, cg, cb, ca);

  // Upload Chromakey State Uniforms
  glUniform1i(m_uChromakeyEnabledLoc, (int)m_chromakeyMode);
  float chR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
  float chG = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;
  float chB = (m_chromakeyValue & 0xFF) / 255.0f;
  float chA = ((m_chromakeyValue >> 24) & 0xFF) / 255.0f;
  if (m_pixelFormatOverride == 1) {
    std::swap(chR, chB);
  }
  glUniform4f(m_uChromakeyValueLoc, chR, chG, chB, chA);

  // Upload Chromakey Range Uniforms
  bool rangeEnabled = ((m_chromakeyRangeMode >> 28) & 1) != 0;
  glUniform1i(m_uChromakeyRangeEnabledLoc, rangeEnabled ? 1 : 0);

  float rMin = ((m_chromakeyRangeMin >> 16) & 0xFF) / 255.0f;
  float gMin = ((m_chromakeyRangeMin >> 8) & 0xFF) / 255.0f;
  float bMin = (m_chromakeyRangeMin & 0xFF) / 255.0f;

  float rMax = ((m_chromakeyRangeMax >> 16) & 0xFF) / 255.0f;
  float gMax = ((m_chromakeyRangeMax >> 8) & 0xFF) / 255.0f;
  float bMax = (m_chromakeyRangeMax & 0xFF) / 255.0f;

  if (m_pixelFormatOverride == 1) {
    std::swap(rMin, bMin);
    std::swap(rMax, bMax);
  }
  GLIDE_LOG(DEBUG, "GLES",
            "ChromaRangeMin: R=" << rMin << ", G=" << gMin << ", B=" << bMin);
  GLIDE_LOG(DEBUG, "GLES",
            "ChromaRangeMax: R=" << rMax << ", G=" << gMax << ", B=" << bMax);
  GLIDE_LOG(
      DEBUG, "GLES",
      "ChromaRangeMode: 0x" << std::hex << m_chromakeyRangeMode << std::dec);
  GLIDE_LOG(DEBUG, "GLES", "PixelFormatOverride: " << m_pixelFormatOverride);

  glUniform3f(m_uChromakeyRangeMinLoc, rMin, gMin, bMin);
  glUniform3f(m_uChromakeyRangeMaxLoc, rMax, gMax, bMax);

  int blueExcl = ((m_chromakeyRangeMode >> 24) & 1);
  int greenExcl = ((m_chromakeyRangeMode >> 25) & 1);
  int redExcl = ((m_chromakeyRangeMode >> 26) & 1);
  glUniform3i(m_uChromakeyRangeExclusiveLoc, redExcl, greenExcl, blueExcl);

  int unionMode = ((m_chromakeyRangeMode >> 27) & 1);
  glUniform1i(m_uChromakeyRangeUnionLoc, unionMode);

  // Upload Combiner State Uniforms
  glUniform1i(m_uColorFuncLoc, (int)m_colorCombinerFunc);
  glUniform1i(m_uColorFactorLoc, (int)m_colorCombinerFactor);
  glUniform1i(m_uColorLocalLoc, (int)m_colorCombinerLocal);
  glUniform1i(m_uColorOtherLoc, (int)m_colorCombinerOther);
  glUniform1i(m_uColorInvertLoc, m_colorCombinerInvert ? 1 : 0);

  glUniform1i(m_uAlphaFuncLoc, (int)m_alphaCombinerFunc);
  glUniform1i(m_uAlphaFactorLoc, (int)m_alphaCombinerFactor);
  glUniform1i(m_uAlphaLocalLoc, (int)m_alphaCombinerLocal);
  glUniform1i(m_uAlphaOtherLoc, (int)m_alphaCombinerOther);
  glUniform1i(m_uAlphaInvertLoc, m_alphaCombinerInvert ? 1 : 0);

  // Upload Texture Binding State (double bindings for multi-texturing)
  int texEnabled[2] = {(m_boundTexAddress[0] != 0xFFFFFFFF) ? 1 : 0,
                       (m_boundTexAddress[1] != 0xFFFFFFFF) ? 1 : 0};
  glUniform1iv(m_uTextureEnabledLoc, 2, texEnabled);
  glUniform1i(m_uTexture0Loc, 0);  // Always texture unit 0 for TMU0
  glUniform1i(m_uTexture1Loc, 1);  // Always texture unit 1 for TMU1
  float lodBias[2] = {m_texLodBias[0], m_texLodBias[1]};
  glUniform1fv(m_uTexLodBiasLoc, 2, lodBias);

  int useTmuOow[2] = {(m_stwHintMask & 0x2) ? 1 : 0,
                      (m_stwHintMask & 0x8) ? 1 : 0};
  glUniform1iv(m_uUseTmuOowLoc, 2, useTmuOow);

  // Upload TMU combiner states (functions, factors, and inverts)
  int rgbFuncs[2] = {(int)m_texCombinerRgbFunc[0],
                     (int)m_texCombinerRgbFunc[1]};
  int rgbFactors[2] = {(int)m_texCombinerRgbFactor[0],
                       (int)m_texCombinerRgbFactor[1]};
  int alphaFuncs[2] = {(int)m_texCombinerAlphaFunc[0],
                       (int)m_texCombinerAlphaFunc[1]};
  int alphaFactors[2] = {(int)m_texCombinerAlphaFactor[0],
                         (int)m_texCombinerAlphaFactor[1]};
  int rgbInverts[2] = {m_texCombinerRgbInvert[0] ? 1 : 0,
                       m_texCombinerRgbInvert[1] ? 1 : 0};
  int alphaInverts[2] = {m_texCombinerAlphaInvert[0] ? 1 : 0,
                         m_texCombinerAlphaInvert[1] ? 1 : 0};

  glUniform1iv(m_uTexRgbFuncLoc, 2, rgbFuncs);
  glUniform1iv(m_uTexRgbFactorLoc, 2, rgbFactors);
  glUniform1iv(m_uTexAlphaFuncLoc, 2, alphaFuncs);
  glUniform1iv(m_uTexAlphaFactorLoc, 2, alphaFactors);
  glUniform1iv(m_uTexRgbInvertLoc, 2, rgbInverts);
  glUniform1iv(m_uTexAlphaInvertLoc, 2, alphaInverts);

  glUniform1i(m_uAlphaTestOpLoc, (int)m_alphaTestOp);
  glUniform1f(m_uAlphaTestRefLoc, (float)m_alphaTestRefVal / 255.0f);
  glUniform1f(m_uDepthBiasLoc, static_cast<float>(m_depthBiasLevel) / 65535.0f);

  // Milestone 8: Fogging State
  glUniform1i(m_uFogModeLoc, (int)m_fogMode);

  float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
  float fogG = ((m_fogColor >> 8) & 0xFF) / 255.0f;
  float fogB = (m_fogColor & 0xFF) / 255.0f;
  if (m_pixelFormatOverride == 1) {
    std::swap(fogR, fogB);
  }
  glUniform4f(m_uFogColorLoc, fogR, fogG, fogB, 1.0f);

  float glesFogTable[64];
  for (int i = 0; i < 64; ++i) {
    glesFogTable[i] = m_fogTable[i] / 255.0f;
  }
  glUniform1fv(m_uFogTableLoc, 64, glesFogTable);

  // Upload Texture-specific Chromakey Uniforms for TMU0 and TMU1
  uint32_t enabled[2];
  uint32_t rangeMode[2];
  float minColor[8];  // 2 vec4s flat
  float maxColor[8];  // 2 vec4s flat

  for (int i = 0; i < 2; ++i) {
    enabled[i] = m_texChromaMode[i];
    rangeMode[i] = m_texChromaRangeMode[i];

    float rMin = ((m_texChromaMin[i] >> 16) & 0xFF) / 255.0f;
    float gMin = ((m_texChromaMin[i] >> 8) & 0xFF) / 255.0f;
    float bMin = (m_texChromaMin[i] & 0xFF) / 255.0f;
    float aMin = ((m_texChromaMin[i] >> 24) & 0xFF) / 255.0f;

    float rMax = ((m_texChromaMax[i] >> 16) & 0xFF) / 255.0f;
    float gMax = ((m_texChromaMax[i] >> 8) & 0xFF) / 255.0f;
    float bMax = (m_texChromaMax[i] & 0xFF) / 255.0f;
    float aMax = ((m_texChromaMax[i] >> 24) & 0xFF) / 255.0f;

    if (m_pixelFormatOverride == 1) {
      std::swap(rMin, bMin);
      std::swap(rMax, bMax);
    }

    minColor[i * 4 + 0] = rMin;
    minColor[i * 4 + 1] = gMin;
    minColor[i * 4 + 2] = bMin;
    minColor[i * 4 + 3] = aMin;

    maxColor[i * 4 + 0] = rMax;
    maxColor[i * 4 + 1] = gMax;
    maxColor[i * 4 + 2] = bMax;
    maxColor[i * 4 + 3] = aMax;
  }

  glUniform1uiv(m_uTexChromaEnabledLoc, 2, enabled);
  glUniform1uiv(m_uTexChromaRangeModeLoc, 2, rangeMode);
  glUniform4fv(m_uTexChromaMinLoc, 2, minColor);
  glUniform4fv(m_uTexChromaMaxLoc, 2, maxColor);

  glUniform1f(m_uDepthNearLoc, m_depthNear);
  glUniform1f(m_uDepthFarLoc, m_depthFar);
  glUniform1ui(m_uDitherModeLoc, m_ditherMode);
  glUniform1ui(m_uStippleModeLoc, m_stippleMode);
  glUniform1ui(m_uStipplePatternLoc, m_stipplePattern);
}

}  // namespace GlideWrapper
