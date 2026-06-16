#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTex;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragTex;

// Viewport and constant-color configuration passed via push constants
layout(push_constant) uniform PushConstants {
    float viewportWidth;
    float viewportHeight;
    vec4 constantColor;
    uint useConstantColor;
} pcs;

void main() {
    // Convert Glide window-space coordinates to Vulkan NDC:
    float ndcX = (inPos.x / (pcs.viewportWidth / 2.0)) - 1.0;
    float ndcY = (inPos.y / (pcs.viewportHeight / 2.0)) - 1.0;

    gl_Position = vec4(ndcX, ndcY, inPos.z, 1.0);
    gl_PointSize = 1.0; // Canonical Glide point size (1px)
    fragColor = inColor;
    fragTex = inTex;
}
