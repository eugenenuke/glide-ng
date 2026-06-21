#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTex;
layout(location = 3) in vec2 inTmuOow;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragTex;
layout(location = 2) out float fragOOW;
layout(location = 3) out vec2 fragTmuOow;

// Viewport and constant-color configuration passed via push constants
layout(push_constant) uniform PushConstants {
    // Block 0: Offset 0 (16 bytes)
    float viewportWidth;
    float viewportHeight;
    uint depthBufferMode;
    uint alphaTestOp;

    // Block 1: Offset 16 (16 bytes)
    vec4 constantColor;

    // Block 2: Offset 32 (16 bytes)
    float alphaTestRef;
    float depthBias;
    uint fogMode;
    uint fogColor;

    // Block 3: Offset 48 (16 bytes)
    uint useTmuOow;
    uint padding0;
    uint padding1;
    uint padding2;

    // Block 4: Offset 64 (64 bytes)
    uvec4 fogTable[4];

    // Block 5: Offset 128 (16 bytes)
    uint colorFunc;
    uint colorFactor;
    uint colorLocal;
    uint colorOther;

    // Block 6: Offset 144 (16 bytes)
    uint colorInvert;
    uint alphaFunc;
    uint alphaFactor;
    uint alphaLocal;

    // Block 7: Offset 160 (16 bytes)
    uint alphaOther;
    uint alphaInvert;
    uint textureEnabled;
    uint chromakeyEnabled;

    // Block 8: Offset 176 (16 bytes)
    uint chromakeyRangeMode;
    uint padding3;
    uint padding4;
    uint padding5;

    // Block 9: Offset 192 (16 bytes)
    vec4 chromakeyValue;

    // Block 10: Offset 208 (16 bytes)
    vec4 chromakeyRangeMin;

    // Block 11: Offset 224 (16 bytes)
    vec4 chromakeyRangeMax;
} pcs;

void main() {
    // Convert Glide window-space coordinates to Vulkan NDC:
    float ndcX = (inPos.x / (pcs.viewportWidth / 2.0)) - 1.0;
    float ndcY = (inPos.y / (pcs.viewportHeight / 2.0)) - 1.0;

    float rawDepth = inPos.z;
    if (pcs.depthBufferMode == 2u) {
        rawDepth = 1.0 - inPos.w;
    }

    rawDepth += pcs.depthBias;

    gl_Position = vec4(ndcX, ndcY, rawDepth, 1.0);
    gl_PointSize = 1.0; // Canonical Glide point size (1px)
    fragColor = inColor;
    fragTex = inTex;
    fragOOW = inPos.w;
    fragTmuOow = inTmuOow;
}
