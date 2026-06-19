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
