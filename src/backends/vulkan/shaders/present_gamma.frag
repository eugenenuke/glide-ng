#version 450

layout(set = 0, binding = 0) uniform sampler2D uSrcTex;
layout(set = 0, binding = 1) uniform sampler2D uGammaLut;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
  vec4 c = texture(uSrcTex, vUV);
  vec3 corrected;
  corrected.r = texture(uGammaLut, vec2((c.r * 255.0 + 0.5) / 256.0, 0.5)).r;
  corrected.g = texture(uGammaLut, vec2((c.g * 255.0 + 0.5) / 256.0, 0.5)).g;
  corrected.b = texture(uGammaLut, vec2((c.b * 255.0 + 0.5) / 256.0, 0.5)).b;
  outColor = vec4(corrected, 1.0);
}
