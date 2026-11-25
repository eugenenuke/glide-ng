#version 450

layout(set = 0, binding = 0) uniform sampler2D uHudTex;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
  vec4 c = texture(uHudTex, vUV);
#ifdef DISCARD_ZERO_ALPHA
  if (c.a == 0.0) discard;
#endif
  outColor = c;
}
