#version 450

layout(binding = 0) uniform sampler2DMS uMsaaDepthTex;
layout(location = 0) in vec2 vUV;

void main() {
  gl_FragDepth =
      clamp(texelFetch(uMsaaDepthTex, ivec2(gl_FragCoord.xy), 0).r, 0.0, 1.0);
}
