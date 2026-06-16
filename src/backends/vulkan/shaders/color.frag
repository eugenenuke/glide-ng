#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragTex;

layout(location = 0) out vec4 outColor;

// Viewport and constant-color configuration passed via push constants
layout(push_constant) uniform PushConstants {
    float viewportWidth;
    float viewportHeight;
    vec4 constantColor;
    uint useConstantColor;
} pcs;

void main() {
    if (pcs.useConstantColor == 1) {
        outColor = pcs.constantColor;
    } else {
        outColor = fragColor;
    }
}
