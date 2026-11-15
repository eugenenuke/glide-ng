#version 450

layout(push_constant) uniform HudPush {
  vec4 rectNdc;  // (x0, y0, x1, y1) in Vulkan NDC [-1, +1]
} pc;

layout(location = 0) out vec2 vUV;

void main() {
  // 4-vertex TRIANGLE_STRIP: 0=(x0,y0), 1=(x1,y0), 2=(x0,y1), 3=(x1,y1)
  vec2 uv = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
  vec2 pos = mix(pc.rectNdc.xy, pc.rectNdc.zw, uv);
  gl_Position = vec4(pos, 0.0, 1.0);
  vUV = uv;
}
