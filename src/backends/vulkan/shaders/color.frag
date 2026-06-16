#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragTex;

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
} pcs;

void main() {
    vec4 finalColor;
    finalColor.rgb = (pcs.useConstantColor == 1u) ? pcs.constantColor.rgb : fragColor.rgb;
    finalColor.a   = (pcs.useConstantAlpha == 1u) ? pcs.constantColor.a   : fragColor.a;

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
