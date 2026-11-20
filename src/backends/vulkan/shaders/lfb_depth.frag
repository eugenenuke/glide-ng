#version 450

layout(binding = 0) uniform sampler2D uDepthTex;
layout(location = 0) in vec2 vUV;

void main() {
  vec4 t = texture(uDepthTex, vUV);
  if (t.a < 0.5) {
    discard;
  }
  // B8G8R8A8_UNORM storing little-endian 0xAARRGGBB where R=bits[23:16],
  // G=bits[15:8], B=bits[7:0] carry a 24-bit normalized depth value.
  float r = floor(t.r * 255.0 + 0.5);
  float g = floor(t.g * 255.0 + 0.5);
  float b = floor(t.b * 255.0 + 0.5);
  gl_FragDepth = clamp((r * 65536.0 + g * 256.0 + b) / 16777215.0, 0.0, 1.0);
}
