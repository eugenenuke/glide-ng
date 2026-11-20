#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTex;
layout(location = 3) in vec2 inTmuOow;
layout(location = 4) in float inFogCoord;

layout(location = 0) centroid out vec4 fragColor;
layout(location = 1) centroid out vec4 fragTex;
layout(location = 2) centroid out float fragOOW;
layout(location = 3) centroid out vec2 fragTmuOow;
layout(location = 4) centroid out float fragFogCoord;
// Non-centroid (pixel-center) affine interpolants dedicated to screen-space
// dFdx/dFdy gradient computation so MSAA centroid shifts along triangle edges
// and internal quad diagonals never spike mipmap LOD.
layout(location = 5) out vec4 fragTexCenter;
layout(location = 6) out vec3 fragQCenter;

// Viewport and constant-color configuration passed via push constants
layout(push_constant) uniform PushConstants {
  // Block 0: Offset 0 (16 bytes)
  float viewportWidth;
  float viewportHeight;
  uint depthBufferMode;  // 0 = None, 1 = Z, 2 = W
  uint alphaTestOp;      // 0..7

  // Block 1: Offset 16 (16 bytes)
  uint constantColor;  // Packed RGBA
  float alphaTestRef;  // 0.0 .. 1.0
  float depthBias;     // Normalized
  uint fogMode;

  // Block 2: Offset 32 (16 bytes)
  uint fogColor;   // Packed ARGB
  uint chromakeyRangeMode;
  float depthNear;
  float depthFar;

  // Block 3: Offset 48 (16 bytes)
  uint chromakeyValue;
  uint chromakeyRangeMin;
  uint chromakeyRangeMax;
  uint tmuCombinerModes1;

  // Block 4: Offset 64 (64 bytes)
  uvec4 fogTable[4];

  // Block 5: Offset 128 (16 bytes)
  uint colorFunc;
  uint colorFactor;
  uint colorLocal;
  uint colorOther;

  // Block 6: Offset 144 (16 bytes)
  uint alphaFunc;
  uint alphaFactor;
  uint alphaLocal;
  uint alphaOther;

  // Block 7: Offset 160 (16 bytes)
  uint textureEnabled;
  uint tmuCombinerModes0;
  uint stipplePattern;
  uint flags;

  // Block 8: Offset 176 (16 bytes)
  uint texChromaMin0;
  uint texChromaMin1;
  uint texChromaMax0;
  uint texChromaMax1;

  // Block 9: Offset 192 (16 bytes)
  uint texDetail0;
  uint texDetail1;
  float texLodBias0;
  float texLodBias1;

  // Block 10: Offset 208 (16 bytes)
  uint extColorCombine;
  uint extAlphaCombine;
  uint extTexColorCombine0;
  uint extTexAlphaCombine0;

  // Block 11: Offset 224 (16 bytes)
  uint extTexColorCombine1;
  uint extTexAlphaCombine1;
  uint tmuConstantColor0;
  uint tmuConstantColor1;
}
pcs;

void main() {
  // Convert Glide window-space coordinates to Vulkan NDC:
  float ndcX = (inPos.x / (pcs.viewportWidth / 2.0)) - 1.0;
  float ndcY = (inPos.y / (pcs.viewportHeight / 2.0)) - 1.0;

  float rawDepth = inPos.z;
  if (pcs.depthBufferMode == 2u) {
    // True W-buffer depth is written per-fragment via gl_FragDepth in
    // color.frag (USE_WBUFFER_DEPTH). Keep clip-space Z strictly inside [0, 1]
    // so oow > 1.0 (W < 1.0) or negative depthBias never triggers near/far
    // frustum clipping even if VkPhysicalDeviceFeatures::depthClamp is absent.
    rawDepth = 0.5;
  } else if (pcs.depthBufferMode == 1u) {
    rawDepth += pcs.depthBias;
  }

  gl_Position = vec4(ndcX, ndcY, rawDepth, 1.0);
  gl_PointSize = 1.0;  // Canonical Glide point size (1px)
  fragColor = inColor;
  fragTex = inTex;
  fragOOW = inPos.w;
  fragTmuOow = inTmuOow;
  fragFogCoord = inFogCoord;
  fragTexCenter = inTex;
  fragQCenter = vec3(inPos.w, inTmuOow.x, inTmuOow.y);
}
