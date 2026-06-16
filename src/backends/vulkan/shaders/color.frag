#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragTex;
layout(location = 2) in float fragOOW;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float viewportWidth;
    float viewportHeight;
    vec4 constantColor;
    uint useConstantColor;
    uint useConstantAlpha;
    uint depthBufferMode;
    uint alphaTestOp;
    float alphaTestRef;
    float depthBias;
    
    uint fogMode;
    uint fogColor;
    uint pad1;
    uint fogTable[16];
} pcs;

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
    vec4 finalColor;
    finalColor.rgb = (pcs.useConstantColor == 1u) ? pcs.constantColor.rgb : fragColor.rgb;
    finalColor.a   = (pcs.useConstantAlpha == 1u) ? pcs.constantColor.a   : fragColor.a;

    // Milestone 8: Fogging Emulation (applied before alpha testing with linear interpolation)
    if (pcs.fogMode > 0u) {
        float eyeW = 1.0 / fragOOW;
        if (eyeW < 1.0) eyeW = 1.0;

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

        float f = mix(f0, f1, t);

        vec3 fogColorRGB;
        fogColorRGB.r = float((pcs.fogColor >> 16u) & 0xFFu) / 255.0;
        fogColorRGB.g = float((pcs.fogColor >> 8u)  & 0xFFu) / 255.0;
        fogColorRGB.b = float(pcs.fogColor & 0xFFu)        / 255.0;

        finalColor.rgb = finalColor.rgb * (1.0 - f) + fogColorRGB * f;
    }

    // Emulate legacy 3dfx Glide alpha testing (7 = GR_CMP_ALWAYS / disabled)
    if (pcs.alphaTestOp != 7u) {
        float alpha = finalColor.a;
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

    outColor = finalColor;
}
