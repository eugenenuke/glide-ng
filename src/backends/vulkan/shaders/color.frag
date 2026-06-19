#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragTex;
layout(location = 2) in float fragOOW;
layout(location = 3) in vec2 fragTmuOow;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float viewportWidth;
    float viewportHeight;
    vec4 constantColor;
    uint depthBufferMode;
    uint alphaTestOp;
    float alphaTestRef;
    float depthBias;
    
    uint fogMode;
    uint fogColor;
    uint useTmuOow;
    uint fogTable[16];

    // Combiner & Texture State
    uint colorFunc;
    uint colorFactor;
    uint colorLocal;
    uint colorOther;
    uint colorInvert;

    uint alphaFunc;
    uint alphaFactor;
    uint alphaLocal;
    uint alphaOther;
    uint alphaInvert;

    uint textureEnabled;
    uint chromakeyEnabled;
    uint chromakeyRangeMode;
    vec4 chromakeyValue;
    vec4 chromakeyRangeMin;
    vec4 chromakeyRangeMax;
} pcs;

layout(binding = 0) uniform sampler2D texSampler0;

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

void main() {
    // Near-plane clipping:
    // 1. If W-buffering is active (pcs.depthBufferMode == 2), the W-buffer cannot represent values < 1.0, so we clip at W < 1.0 (fragOOW > 1.0).
    // 2. If texturing is enabled (pcs.textureEnabled == 1), we must clip if the vertex is behind the camera (W <= 0, i.e. fragOOW <= 0.0) to prevent division by negative.
    // 3. Otherwise, we do not perform W-based clipping (e.g. flat-shaded Z-buffered geometry has no W).
    if (pcs.depthBufferMode == 2u) {
        if (fragOOW > 1.0) discard;
    } else if (pcs.textureEnabled == 1u) {
        if (fragOOW <= 0.0) discard;
    }

    // 1. Sample texture if enabled
    vec4 texColor = vec4(1.0);
    if (pcs.textureEnabled == 1u) {
        ivec2 texSize = textureSize(texSampler0, 0);
        float tw = float(texSize.x);
        float th = float(texSize.y);
        float maxDim = max(tw, th);
        float divW = (pcs.useTmuOow == 1u) ? fragTmuOow.x : fragOOW;
        vec2 uv = (fragTex.xy / divW) / 256.0;
        uv.x *= (maxDim / tw);
        uv.y *= (maxDim / th);
        texColor = texture(texSampler0, uv);
    }

    // 2. Route Alpha Combiner inputs
    float aLocal = 1.0;
    if (pcs.alphaLocal == 0u) aLocal = fragColor.a;
    else if (pcs.alphaLocal == 1u) aLocal = pcs.constantColor.a;

    float aOther = 1.0;
    if (pcs.alphaOther == 0u) aOther = fragColor.a;
    else if (pcs.alphaOther == 1u) aOther = texColor.a;
    else if (pcs.alphaOther == 2u) aOther = pcs.constantColor.a;

    // 3. Evaluate Alpha Combiner Factor
    float factA = 0.0;
    if (pcs.alphaFactor == 1u || pcs.alphaFactor == 3u) factA = aLocal;
    else if (pcs.alphaFactor == 2u) factA = aOther;
    else if (pcs.alphaFactor == 4u) factA = texColor.a;
    else if (pcs.alphaFactor == 8u) factA = 1.0;
    else if (pcs.alphaFactor == 9u || pcs.alphaFactor == 11u) factA = 1.0 - aLocal;
    else if (pcs.alphaFactor == 10u) factA = 1.0 - aOther;
    else if (pcs.alphaFactor == 12u) factA = 1.0 - texColor.a;

    // 4. Evaluate Alpha Combiner Function
    float finalA = 0.0;
    if (pcs.alphaFunc == 1u) finalA = aLocal;
    else if (pcs.alphaFunc == 3u) finalA = aOther * factA;
    else if (pcs.alphaFunc == 4u || pcs.alphaFunc == 5u) finalA = aOther * factA + aLocal;
    else if (pcs.alphaFunc == 6u) finalA = (aOther - aLocal) * factA;
    else if (pcs.alphaFunc == 7u || pcs.alphaFunc == 8u) finalA = (aOther - aLocal) * factA + aLocal;
    
    if (pcs.alphaInvert == 1u) finalA = 1.0 - finalA;

    // 5. Route Color Combiner inputs
    vec3 cLocal = vec3(0.0);
    if (pcs.colorLocal == 0u) cLocal = fragColor.rgb;
    else if (pcs.colorLocal == 1u) cLocal = pcs.constantColor.rgb;

    vec3 cOther = vec3(0.0);
    if (pcs.colorOther == 0u) cOther = fragColor.rgb;
    else if (pcs.colorOther == 1u) cOther = texColor.rgb;
    else if (pcs.colorOther == 2u) cOther = pcs.constantColor.rgb;

    // 6. Evaluate Color Combiner Factor
    vec3 factC = vec3(0.0);
    if (pcs.colorFactor == 1u) factC = cLocal;
    else if (pcs.colorFactor == 2u) factC = vec3(aOther);
    else if (pcs.colorFactor == 3u) factC = vec3(aLocal);
    else if (pcs.colorFactor == 4u) factC = vec3(texColor.a);
    else if (pcs.colorFactor == 5u) factC = texColor.rgb;
    else if (pcs.colorFactor == 8u) factC = vec3(1.0);
    else if (pcs.colorFactor == 9u) factC = vec3(1.0) - cLocal;
    else if (pcs.colorFactor == 10u) factC = vec3(1.0 - aOther);
    else if (pcs.colorFactor == 11u) factC = vec3(1.0 - aLocal);
    else if (pcs.colorFactor == 12u) factC = vec3(1.0 - texColor.a);

    // 7. Evaluate Color Combiner Function
    vec3 finalC = vec3(0.0);
    if (pcs.colorFunc == 1u) finalC = cLocal;
    else if (pcs.colorFunc == 3u) finalC = cOther * factC;
    else if (pcs.colorFunc == 4u) finalC = cOther * factC + cLocal;
    else if (pcs.colorFunc == 5u) finalC = cOther * factC + vec3(aLocal);
    else if (pcs.colorFunc == 6u) finalC = (cOther - cLocal) * factC;
    else if (pcs.colorFunc == 7u) finalC = (cOther - cLocal) * factC + cLocal;
    else if (pcs.colorFunc == 8u) finalC = (cOther - cLocal) * factC + vec3(aLocal);

    if (pcs.colorInvert == 1u) finalC = vec3(1.0) - finalC;

    vec4 col = vec4(finalC, finalA);

    // Milestone 10: Chromakey Emulation
    if (pcs.chromakeyEnabled == 1u) {
        vec3 chromaTestColor = col.rgb;
        bool rangeEnabled = ((pcs.chromakeyRangeMode >> 28u) & 1u) == 1u;
        if (rangeEnabled) {
            bool rMatch = (chromaTestColor.r >= pcs.chromakeyRangeMin.r && chromaTestColor.r <= pcs.chromakeyRangeMax.r);
            bool gMatch = (chromaTestColor.g >= pcs.chromakeyRangeMin.g && chromaTestColor.g <= pcs.chromakeyRangeMax.g);
            bool bMatch = (chromaTestColor.b >= pcs.chromakeyRangeMin.b && chromaTestColor.b <= pcs.chromakeyRangeMax.b);
            
            bool blueExcl  = ((pcs.chromakeyRangeMode >> 24u) & 1u) == 1u;
            bool greenExcl = ((pcs.chromakeyRangeMode >> 25u) & 1u) == 1u;
            bool redExcl   = ((pcs.chromakeyRangeMode >> 26u) & 1u) == 1u;
            
            bool rRes = rMatch != redExcl;
            bool gRes = gMatch != greenExcl;
            bool bRes = bMatch != blueExcl;
            
            bool unionMode = ((pcs.chromakeyRangeMode >> 27u) & 1u) == 1u;
            bool discardPixel = false;
            if (unionMode) {
                discardPixel = rRes || gRes || bRes;
            } else {
                discardPixel = rRes && gRes && bRes;
            }
            
            if (discardPixel) {
                discard;
            }
        } else {
            if (distance(chromaTestColor, pcs.chromakeyValue.rgb) < 0.01) {
                discard;
            }
        }
    }

    // Milestone 8: Fogging Emulation
    uint fogSource = pcs.fogMode & 0x0Fu;
    if (fogSource != 0u) {
        float f = 0.0;
        if (fogSource == 1u) { // GR_FOG_WITH_ITERATED_ALPHA
            f = fragColor.a;
        } else { // Table-based fog (GR_FOG_WITH_TABLE)
            float eyeW = 1.0 / fragOOW;
            if (eyeW < 1.0) eyeW = 1.0;

            // O(1) unrolled logarithmic binary search
            int idx = 0;
            if (eyeW >= s_tableIndexToW[32]) idx += 32;
            if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
            if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
            if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
            if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
            if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

            float w0 = s_tableIndexToW[idx];
            float w1 = s_tableIndexToW[idx + 1];

            // Unpack f0 at idx
            uint packedWord0 = pcs.fogTable[idx / 4];
            uint shift0 = uint(idx % 4) * 8u;
            float f0 = float((packedWord0 >> shift0) & 0xFFu) / 255.0;

            // Unpack f1 at idx + 1
            uint packedWord1 = pcs.fogTable[(idx + 1) / 4];
            uint shift1 = uint((idx + 1) % 4) * 8u;
            float f1 = float((packedWord1 >> shift1) & 0xFFu) / 255.0;

            // Linear interpolation factor
            float t = 0.0;
            if (w1 > w0) {
                t = (eyeW - w0) / (w1 - w0);
            }
            t = clamp(t, 0.0, 1.0);

            f = mix(f0, f1, t);
        }

        vec3 fogColorRGB;
        fogColorRGB.r = float((pcs.fogColor >> 16u) & 0xFFu) / 255.0;
        fogColorRGB.g = float((pcs.fogColor >> 8u)  & 0xFFu) / 255.0;
        fogColorRGB.b = float(pcs.fogColor & 0xFFu)        / 255.0;

        // Extract FOGMODE flags
        float mult = float((pcs.fogMode & 0x100u) != 0u); // GR_FOG_MULT2 (SST_FOGMULT)
        float add  = float((pcs.fogMode & 0x200u) != 0u); // GR_FOG_ADD2  (SST_FOGADD)

        // Apply the unified branch-free hardware equation
        col.rgb = col.rgb * (1.0 - mult) * (1.0 - f) + fogColorRGB * (1.0 - add) * f;
    }

    // Emulate legacy 3dfx Glide alpha testing
    if (pcs.alphaTestOp != 7u) {
        float alpha = col.a;
        float refVal = pcs.alphaTestRef;
        bool passed = true;

        if (pcs.alphaTestOp == 0u) passed = false; // GR_CMP_NEVER
        else if (pcs.alphaTestOp == 1u) passed = (alpha < refVal); // GR_CMP_LESS
        else if (pcs.alphaTestOp == 2u) passed = (alpha == refVal); // GR_CMP_EQUAL
        else if (pcs.alphaTestOp == 3u) passed = (alpha <= refVal); // GR_CMP_LEQUAL
        else if (pcs.alphaTestOp == 4u) passed = (alpha > refVal); // GR_CMP_GREATER
        else if (pcs.alphaTestOp == 5u) passed = (alpha != refVal); // GR_CMP_NOTEQUAL
        else if (pcs.alphaTestOp == 6u) passed = (alpha >= refVal); // GR_CMP_GEQUAL

        if (!passed) {
            discard;
        }
    }

    outColor = col;
}
