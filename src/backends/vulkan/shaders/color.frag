#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragTex;
layout(location = 2) in float fragOOW;
layout(location = 3) in vec2 fragTmuOow;
layout(location = 4) in float fragFogCoord;

layout(location = 0) out vec4 outColor;
#ifdef USE_DUAL_SRC_BLEND
layout(location = 0, index = 1) out vec4 outPreFogColor;
#endif

layout(push_constant) uniform PushConstants {
  // Block 0: Offset 0 (16 bytes)
  float viewportWidth;
  float viewportHeight;
  uint depthBufferMode;
  uint alphaTestOp;

  // Block 1: Offset 16 (16 bytes)
  uint constantColor;
  float alphaTestRef;
  float depthBias;
  uint fogMode;

  // Block 2: Offset 32 (16 bytes)
  uint fogColor;
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

layout(binding = 0) uniform sampler2D texSampler0;
layout(binding = 1) uniform sampler2D texSampler1;

// Forward declaration of IsColorInRange to allow out-of-order calls
bool IsColorInRange(vec3 color, vec3 minColor, vec3 maxColor, uint rangeMode);

vec4 unpackRGBA(uint packedColor) {
  return vec4(float((packedColor >> 0u) & 0xFFu) / 255.0,
              float((packedColor >> 8u) & 0xFFu) / 255.0,
              float((packedColor >> 16u) & 0xFFu) / 255.0,
              float((packedColor >> 24u) & 0xFFu) / 255.0);
}

vec4 sampleTexture(sampler2D tex, vec2 uv, uint tmuIdx, float tw, float th) {
  float maxDim = max(tw, th);
  vec2 finalUv = uv;
  finalUv.x *= (maxDim / tw);
  finalUv.y *= (maxDim / th);

  bool texChromaEnabled = ((pcs.flags >> (5u + tmuIdx)) & 1u) != 0u;
  if (texChromaEnabled) {
    ivec2 size = ivec2(int(tw), int(th));
    vec2 texelCoord = finalUv * vec2(tw, th) - vec2(0.5);
    ivec2 tl = ivec2(floor(texelCoord));
    vec2 f = fract(texelCoord);

    ivec2 c00_coord = clamp(tl + ivec2(0, 0), ivec2(0), size - ivec2(1));
    ivec2 c10_coord = clamp(tl + ivec2(1, 0), ivec2(0), size - ivec2(1));
    ivec2 c01_coord = clamp(tl + ivec2(0, 1), ivec2(0), size - ivec2(1));
    ivec2 c11_coord = clamp(tl + ivec2(1, 1), ivec2(0), size - ivec2(1));

    vec4 c00 = texelFetch(tex, c00_coord, 0);
    vec4 c10 = texelFetch(tex, c10_coord, 0);
    vec4 c01 = texelFetch(tex, c01_coord, 0);
    vec4 c11 = texelFetch(tex, c11_coord, 0);

    uint texChromaRangeMode = ((pcs.flags >> (7u + tmuIdx)) & 1u) != 0u ? 0x10000000u : 0u;
    vec3 chromaMinRgb = unpackRGBA(tmuIdx == 0u ? pcs.texChromaMin0 : pcs.texChromaMin1).rgb;
    vec3 chromaMaxRgb = unpackRGBA(tmuIdx == 0u ? pcs.texChromaMax0 : pcs.texChromaMax1).rgb;

    if (IsColorInRange(c00.rgb, chromaMinRgb, chromaMaxRgb, texChromaRangeMode))
      c00 = vec4(0.0);
    if (IsColorInRange(c10.rgb, chromaMinRgb, chromaMaxRgb, texChromaRangeMode))
      c10 = vec4(0.0);
    if (IsColorInRange(c01.rgb, chromaMinRgb, chromaMaxRgb, texChromaRangeMode))
      c01 = vec4(0.0);
    if (IsColorInRange(c11.rgb, chromaMinRgb, chromaMaxRgb, texChromaRangeMode))
      c11 = vec4(0.0);

    vec4 c0 = mix(c00, c10, f.x);
    vec4 c1 = mix(c01, c11, f.x);
    return mix(c0, c1, f.y);
  } else {
    return texture(tex, finalUv);
  }
}

vec4 evaluateTmuStage(uint rgbFunc, uint rgbFactor, uint alphaFunc,
                      uint alphaFactor, bool rgbInvert, bool alphaInvert,
                      vec4 localVal, vec4 otherVal, float detailFactor,
                      float lodFrac) {
  // 1. Evaluate Alpha
  float aLocal = localVal.a;
  float aOther = otherVal.a;

  float factA = 0.0;
  if (alphaFactor == 1u || alphaFactor == 3u)
    factA = aLocal;
  else if (alphaFactor == 2u)
    factA = aOther;
  else if (alphaFactor == 4u)
    factA = detailFactor;
  else if (alphaFactor == 5u)
    factA = lodFrac;
  else if (alphaFactor == 8u)
    factA = 1.0;
  else if (alphaFactor == 9u || alphaFactor == 11u)
    factA = 1.0 - aLocal;
  else if (alphaFactor == 10u)
    factA = 1.0 - aOther;
  else if (alphaFactor == 12u)
    factA = 1.0 - detailFactor;
  else if (alphaFactor == 13u)
    factA = 1.0 - lodFrac;

  float finalA = 0.0;
  if (alphaFunc == 1u || alphaFunc == 2u)
    finalA = aLocal;
  else if (alphaFunc == 3u)
    finalA = aOther * factA;
  else if (alphaFunc == 4u || alphaFunc == 5u)
    finalA = aOther * factA + aLocal;
  else if (alphaFunc == 6u)
    finalA = (aOther - aLocal) * factA;
  else if (alphaFunc == 7u || alphaFunc == 8u)
    finalA = (aOther - aLocal) * factA + aLocal;
  else if (alphaFunc == 9u || alphaFunc == 16u)
    finalA = aLocal * (1.0 - factA);

  if (alphaInvert) finalA = 1.0 - finalA;

  // 2. Evaluate RGB
  vec3 cLocal = localVal.rgb;
  vec3 cOther = otherVal.rgb;

  vec3 factC = vec3(0.0);
  if (rgbFactor == 1u)
    factC = cLocal;
  else if (rgbFactor == 2u)
    factC = vec3(aOther);
  else if (rgbFactor == 3u)
    factC = vec3(aLocal);
  else if (rgbFactor == 4u)
    factC = vec3(detailFactor);
  else if (rgbFactor == 5u)
    factC = vec3(lodFrac);
  else if (rgbFactor == 8u)
    factC = vec3(1.0);
  else if (rgbFactor == 9u)
    factC = vec3(1.0) - cLocal;
  else if (rgbFactor == 10u)
    factC = vec3(1.0 - aOther);
  else if (rgbFactor == 11u)
    factC = vec3(1.0 - aLocal);
  else if (rgbFactor == 12u)
    factC = vec3(1.0 - detailFactor);
  else if (rgbFactor == 13u)
    factC = vec3(1.0 - lodFrac);

  vec3 finalC = vec3(0.0);
  if (rgbFunc == 1u)
    finalC = cLocal;
  else if (rgbFunc == 2u)
    finalC = vec3(aLocal);
  else if (rgbFunc == 3u)
    finalC = cOther * factC;
  else if (rgbFunc == 4u)
    finalC = cOther * factC + cLocal;
  else if (rgbFunc == 5u)
    finalC = cOther * factC + vec3(aLocal);
  else if (rgbFunc == 6u)
    finalC = (cOther - cLocal) * factC;
  else if (rgbFunc == 7u)
    finalC = (cOther - cLocal) * factC + cLocal;
  else if (rgbFunc == 8u)
    finalC = (cOther - cLocal) * factC + vec3(aLocal);
  else if (rgbFunc == 9u)
    finalC = cLocal * (vec3(1.0) - factC);
  else if (rgbFunc == 16u)
    finalC = vec3(aLocal) - cLocal * factC;

  if (rgbInvert) finalC = vec3(1.0) - finalC;

  return vec4(finalC, finalA);
}

vec3 applyCombineMode3(vec3 x, uint mode) {
  if (mode == 0u) return vec3(0.0);
  if (mode == 1u) return x;
  if (mode == 2u) return vec3(1.0) - x;
  if (mode == 3u) return -x;
  if (mode == 4u) return x - vec3(0.5);
  if (mode == 5u) return vec3(1.0);
  if (mode == 6u) return vec3(-0.5);
  return x;
}

float applyCombineMode1(float x, uint mode) {
  if (mode == 0u) return 0.0;
  if (mode == 1u) return x;
  if (mode == 2u) return 1.0 - x;
  if (mode == 3u) return -x;
  if (mode == 4u) return x - 0.5;
  if (mode == 5u) return 1.0;
  if (mode == 6u) return -0.5;
  return x;
}

vec3 fetchTmuRgbSrc(uint src, vec4 localVal, vec4 otherVal, vec4 tmuConst,
                    vec4 iterColor, float detailFactor, float lodFrac) {
  if (src == 0x00u) return vec3(0.0);
  if (src == 0x01u) return vec3(localVal.a);
  if (src == 0x07u) return vec3(detailFactor);
  if (src == 0x08u) return vec3(iterColor.a);
  if (src == 0x09u) return iterColor.rgb;
  if (src == 0x0Au) return vec3(localVal.a);
  if (src == 0x0Bu) return localVal.rgb;
  if (src == 0x0Cu) return vec3(lodFrac);
  if (src == 0x0Du) return vec3(otherVal.a);
  if (src == 0x0Eu) return otherVal.rgb;
  if (src == 0x0Fu) return localVal.rgb;
  if (src == 0x10u) return vec3(tmuConst.a);
  if (src == 0x11u) return tmuConst.rgb;
  return vec3(0.0);
}

float fetchTmuAlphaSrc(uint src, vec4 localVal, vec4 otherVal, vec4 tmuConst,
                       vec4 iterColor, float detailFactor, float lodFrac) {
  if (src == 0x00u) return 0.0;
  if (src == 0x01u) return localVal.a;
  if (src == 0x07u) return detailFactor;
  if (src == 0x08u) return iterColor.a;
  if (src == 0x0Au) return localVal.a;
  if (src == 0x0Cu) return lodFrac;
  if (src == 0x0Du) return otherVal.a;
  if (src == 0x10u) return tmuConst.a;
  return 0.0;
}

vec3 evaluateTmuExtRgb(uint packed, vec4 localVal, vec4 otherVal, vec4 tmuConst,
                       vec4 iterColor, float detailFactor, float lodFrac) {
  uint aSrc = packed & 0x1Fu;
  uint aMode = (packed >> 5u) & 0x7u;
  uint bSrc = (packed >> 8u) & 0x1Fu;
  uint bMode = (packed >> 13u) & 0x7u;
  uint cSrc = (packed >> 16u) & 0x1Fu;
  bool cInv = ((packed >> 21u) & 1u) != 0u;
  uint dSrc = (packed >> 22u) & 0x1Fu;
  bool dInv = ((packed >> 27u) & 1u) != 0u;
  uint shift = min((packed >> 28u) & 0x3u, 2u);
  bool inv = ((packed >> 30u) & 1u) != 0u;

  vec3 aRaw = fetchTmuRgbSrc(aSrc, localVal, otherVal, tmuConst, iterColor,
                             detailFactor, lodFrac);
  vec3 bRaw = fetchTmuRgbSrc(bSrc, localVal, otherVal, tmuConst, iterColor,
                             detailFactor, lodFrac);
  vec3 aVal = applyCombineMode3(aRaw, aMode);
  vec3 bVal = applyCombineMode3(bRaw, bMode);
  vec3 cRaw = (cSrc == 0x04u)
                  ? bRaw
                  : fetchTmuRgbSrc(cSrc, localVal, otherVal, tmuConst,
                                   iterColor, detailFactor, lodFrac);
  vec3 dRaw = (dSrc == 0x04u)
                  ? bRaw
                  : fetchTmuRgbSrc(dSrc, localVal, otherVal, tmuConst,
                                   iterColor, detailFactor, lodFrac);
  vec3 cVal = cInv ? (vec3(1.0) - cRaw) : cRaw;
  vec3 dVal = dInv ? (vec3(1.0) - dRaw) : dRaw;
  vec3 res = ((aVal + bVal) * cVal + dVal) * float(1u << shift);
  if (inv) res = vec3(1.0) - res;
  return clamp(res, 0.0, 1.0);
}

float evaluateTmuExtAlpha(uint packed, vec4 localVal, vec4 otherVal,
                          vec4 tmuConst, vec4 iterColor, float detailFactor,
                          float lodFrac) {
  uint aSrc = packed & 0x1Fu;
  uint aMode = (packed >> 5u) & 0x7u;
  uint bSrc = (packed >> 8u) & 0x1Fu;
  uint bMode = (packed >> 13u) & 0x7u;
  uint cSrc = (packed >> 16u) & 0x1Fu;
  bool cInv = ((packed >> 21u) & 1u) != 0u;
  uint dSrc = (packed >> 22u) & 0x1Fu;
  bool dInv = ((packed >> 27u) & 1u) != 0u;
  uint shift = min((packed >> 28u) & 0x3u, 2u);
  bool inv = ((packed >> 30u) & 1u) != 0u;

  float aRaw = fetchTmuAlphaSrc(aSrc, localVal, otherVal, tmuConst, iterColor,
                                detailFactor, lodFrac);
  float bRaw = fetchTmuAlphaSrc(bSrc, localVal, otherVal, tmuConst, iterColor,
                                detailFactor, lodFrac);
  float aVal = applyCombineMode1(aRaw, aMode);
  float bVal = applyCombineMode1(bRaw, bMode);
  float cRaw = (cSrc == 0x04u)
                   ? bRaw
                   : fetchTmuAlphaSrc(cSrc, localVal, otherVal, tmuConst,
                                      iterColor, detailFactor, lodFrac);
  float dRaw = (dSrc == 0x04u)
                   ? bRaw
                   : fetchTmuAlphaSrc(dSrc, localVal, otherVal, tmuConst,
                                      iterColor, detailFactor, lodFrac);
  float cVal = cInv ? (1.0 - cRaw) : cRaw;
  float dVal = dInv ? (1.0 - dRaw) : dRaw;
  float res = ((aVal + bVal) * cVal + dVal) * float(1u << shift);
  if (inv) res = 1.0 - res;
  return clamp(res, 0.0, 1.0);
}

float fetchFbiAlphaBase(uint src, vec4 iterColor, vec4 constColor,
                        vec4 texColor) {
  if (src == 0x00u) return 0.0;
  if (src == 0x01u) return texColor.a;
  if (src == 0x05u || src == 0x06u) return constColor.a;
  if (src == 0x08u || src == 0x09u) return iterColor.a;
  if (src == 0x0Fu) return texColor.a;
  return 0.0;
}

vec3 fetchFbiColorSrc(uint src, vec4 iterColor, vec4 constColor, vec4 texColor,
                      float aLocal, float aOther) {
  if (src == 0x00u) return vec3(0.0);
  if (src == 0x01u) return vec3(texColor.a);
  if (src == 0x02u) return vec3(aLocal);
  if (src == 0x03u) return vec3(aOther);
  if (src == 0x05u) return vec3(constColor.a);
  if (src == 0x06u) return constColor.rgb;
  if (src == 0x08u) return vec3(iterColor.a);
  if (src == 0x09u) return iterColor.rgb;
  if (src == 0x0Fu) return texColor.rgb;
  return vec3(0.0);
}

float evaluateFbiExtAlpha(uint packed, vec4 iterColor, vec4 constColor,
                          vec4 texColor, out float outALocal,
                          out float outAOther) {
  uint aSrc = packed & 0x1Fu;
  uint aMode = (packed >> 5u) & 0x7u;
  uint bSrc = (packed >> 8u) & 0x1Fu;
  uint bMode = (packed >> 13u) & 0x7u;
  uint cSrc = (packed >> 16u) & 0x1Fu;
  bool cInv = ((packed >> 21u) & 1u) != 0u;
  uint dSrc = (packed >> 22u) & 0x1Fu;
  bool dInv = ((packed >> 27u) & 1u) != 0u;
  uint shift = min((packed >> 28u) & 0x3u, 2u);
  bool inv = ((packed >> 30u) & 1u) != 0u;

  float aRaw = fetchFbiAlphaBase(aSrc, iterColor, constColor, texColor);
  float bRaw = fetchFbiAlphaBase(bSrc, iterColor, constColor, texColor);
  outAOther = aRaw;
  outALocal = bRaw;
  float aVal = applyCombineMode1(aRaw, aMode);
  float bVal = applyCombineMode1(bRaw, bMode);
  float cRaw = (cSrc == 0x04u || cSrc == 0x02u)
                   ? bRaw
                   : ((cSrc == 0x03u)
                          ? aRaw
                          : fetchFbiAlphaBase(cSrc, iterColor, constColor,
                                              texColor));
  float dRaw = (dSrc == 0x04u || dSrc == 0x02u)
                   ? bRaw
                   : ((dSrc == 0x03u)
                          ? aRaw
                          : fetchFbiAlphaBase(dSrc, iterColor, constColor,
                                              texColor));
  float cVal = cInv ? (1.0 - cRaw) : cRaw;
  float dVal = dInv ? (1.0 - dRaw) : dRaw;
  float res = ((aVal + bVal) * cVal + dVal) * float(1u << shift);
  if (inv) res = 1.0 - res;
  return clamp(res, 0.0, 1.0);
}

vec3 evaluateFbiExtColor(uint packed, vec4 iterColor, vec4 constColor,
                         vec4 texColor, float aLocal, float aOther) {
  uint aSrc = packed & 0x1Fu;
  uint aMode = (packed >> 5u) & 0x7u;
  uint bSrc = (packed >> 8u) & 0x1Fu;
  uint bMode = (packed >> 13u) & 0x7u;
  uint cSrc = (packed >> 16u) & 0x1Fu;
  bool cInv = ((packed >> 21u) & 1u) != 0u;
  uint dSrc = (packed >> 22u) & 0x1Fu;
  bool dInv = ((packed >> 27u) & 1u) != 0u;
  uint shift = min((packed >> 28u) & 0x3u, 2u);
  bool inv = ((packed >> 30u) & 1u) != 0u;

  vec3 aRaw = fetchFbiColorSrc(aSrc, iterColor, constColor, texColor, aLocal,
                               aOther);
  vec3 bRaw = fetchFbiColorSrc(bSrc, iterColor, constColor, texColor, aLocal,
                               aOther);
  vec3 aVal = applyCombineMode3(aRaw, aMode);
  vec3 bVal = applyCombineMode3(bRaw, bMode);
  vec3 cRaw = (cSrc == 0x04u)
                  ? bRaw
                  : fetchFbiColorSrc(cSrc, iterColor, constColor, texColor,
                                     aLocal, aOther);
  vec3 dRaw = (dSrc == 0x04u)
                  ? bRaw
                  : fetchFbiColorSrc(dSrc, iterColor, constColor, texColor,
                                     aLocal, aOther);
  vec3 cVal = cInv ? (vec3(1.0) - cRaw) : cRaw;
  vec3 dVal = dInv ? (vec3(1.0) - dRaw) : dRaw;
  vec3 res = ((aVal + bVal) * cVal + dVal) * float(1u << shift);
  if (inv) res = vec3(1.0) - res;
  return clamp(res, 0.0, 1.0);
}

bool IsColorInRange(vec3 color, vec3 minColor, vec3 maxColor, uint rangeMode) {
  bool rangeEnabled = ((rangeMode >> 28u) & 1u) == 1u;
  if (rangeEnabled) {
    bool rMatch = (color.r >= minColor.r && color.r <= maxColor.r);
    bool gMatch = (color.g >= minColor.g && color.g <= maxColor.g);
    bool bMatch = (color.b >= minColor.b && color.b <= maxColor.b);

    bool blueExcl = ((rangeMode >> 24u) & 1u) == 1u;
    bool greenExcl = ((rangeMode >> 25u) & 1u) == 1u;
    bool redExcl = ((rangeMode >> 26u) & 1u) == 1u;

    bool rRes = rMatch != redExcl;
    bool gRes = gMatch != greenExcl;
    bool bRes = bMatch != blueExcl;

    bool unionMode = ((rangeMode >> 27u) & 1u) == 1u;
    if (unionMode) {
      return rRes || gRes || bRes;
    } else {
      return rRes && gRes && bRes;
    }
  } else {
    // Exact 8-bit channel match (< 0.5 / 255) so dark non-key colors like
    // RGB(1,1,1) or RGB(2,2,2) are not punched out when keying on black.
    return all(lessThan(abs(color - minColor), vec3(0.0019)));
  }
}

const float s_tableIndexToW[64] = float[](
    1.000000, 1.142857, 1.333333, 1.600000, 2.000000, 2.285714, 2.666667,
    3.200000, 4.000000, 4.571429, 5.333333, 6.400000, 8.000000, 9.142858,
    10.666667, 12.800000, 16.000000, 18.285715, 21.333334, 25.600000, 32.000000,
    36.571430, 42.666668, 51.200001, 64.000000, 73.142860, 85.333336,
    102.400002, 128.000000, 146.285721, 170.666672, 204.800003, 256.000000,
    292.571442, 341.333344, 409.600006, 512.000000, 585.142883, 682.666687,
    819.200012, 1024.000000, 1170.285767, 1365.333374, 1638.400024, 2048.000000,
    2340.571533, 2730.666748, 3276.800049, 4096.000000, 4681.143066,
    5461.333496, 6553.600098, 8192.000000, 9362.286133, 10922.666992,
    13107.200195, 16384.000000, 18724.572266, 21845.333984, 26214.400391,
    32768.000000, 37449.144531, 43690.667969, 52428.800781);

vec4 applyDithering(vec4 color, uint ditherMode) {
  if (ditherMode == 0u) return color;

  ivec2 coord = ivec2(gl_FragCoord.xy) & 3;
  uint threshold = 0u;
  uint matrixSize = 16u;
  
  if (ditherMode == 1u) { // 2x2
    const uint dither_matrix_2x2[4] = uint[](
        0u, 2u,
        3u, 1u
    );
    int idx = (coord.y & 1) * 2 + (coord.x & 1);
    threshold = dither_matrix_2x2[idx];
    matrixSize = 4u;
  } else if (ditherMode == 2u) { // 4x4
    const uint dither_matrix_4x4[16] = uint[](
         0u,  8u,  2u, 10u,
        12u,  4u, 14u,  6u,
         3u, 11u,  1u,  9u,
        15u,  7u, 13u,  5u
    );
    int idx = coord.y * 4 + coord.x;
    threshold = dither_matrix_4x4[idx];
    matrixSize = 16u;
  }

  // Authentic 3dfx framebuffer dithering quantizes RGB to 16-bit RGB565 only;
  // full 8-bit alpha is always preserved for hardware alpha blending.
  float r = color.r * 255.0;
  float g = color.g * 255.0;
  float b = color.b * 255.0;

  uint ditherR = (threshold * 8u) / matrixSize;
  uint r5 = uint(clamp(r + float(ditherR), 0.0, 255.0)) / 8u;
  r5 = min(r5, 31u);
  r = float((r5 << 3u) | (r5 >> 2u));

  uint ditherG = (threshold * 4u) / matrixSize;
  uint g6 = uint(clamp(g + float(ditherG), 0.0, 255.0)) / 4u;
  g6 = min(g6, 63u);
  g = float((g6 << 2u) | (g6 >> 4u));

  uint ditherB = (threshold * 8u) / matrixSize;
  uint b5 = uint(clamp(b + float(ditherB), 0.0, 255.0)) / 8u;
  b5 = min(b5, 31u);
  b = float((b5 << 3u) | (b5 >> 2u));

  return vec4(r / 255.0, g / 255.0, b / 255.0, color.a);
}

void applyStippling(uint stippleMode, uint stipplePattern) {
  if (stippleMode == 0u) return;

  ivec2 coord = ivec2(gl_FragCoord.xy);
  
  if (stippleMode == 1u) {
    int x = coord.x & 7;
    int y = coord.y & 3;
    int bitIdx = (y * 8) + (7 - x);
    uint bit = (stipplePattern >> uint(bitIdx)) & 1u;
    if (bit == 0u) {
      discard;
    }
  } else if (stippleMode == 2u) {
    int x = (coord.x + coord.y) & 7;
    int y = coord.y & 3;
    int bitIdx = (y * 8) + (7 - x);
    uint bit = (stipplePattern >> uint(bitIdx)) & 1u;
    if (bit == 0u) {
      discard;
    }
  }
}

void main() {
  // Unpack colors at the start of main()
  vec4 constantColor = unpackRGBA(pcs.constantColor);
  vec4 chromakeyValue = unpackRGBA(pcs.chromakeyValue);
  vec4 chromakeyRangeMin = unpackRGBA(pcs.chromakeyRangeMin);
  vec4 chromakeyRangeMax = unpackRGBA(pcs.chromakeyRangeMax);

  // Unpack flags
  bool colorInvert = (pcs.flags & 1u) != 0u;
  bool alphaInvert = ((pcs.flags >> 1u) & 1u) != 0u;
  bool chromakeyEnabled = ((pcs.flags >> 2u) & 1u) != 0u;
  uint useTmuOow = (pcs.flags >> 3u) & 3u;

  uint ditherMode = (pcs.flags >> 9u) & 3u;
  uint stippleMode = (pcs.flags >> 11u) & 3u;

  // Apply stippling early
  applyStippling(stippleMode, pcs.stipplePattern);

  // Unpack texture enabling and invert bits
  bool tmu0Active = (pcs.textureEnabled & 1u) != 0u;
  bool tmu1Active = ((pcs.textureEnabled >> 1u) & 1u) != 0u;
  bool tmu0RgbInvert = ((pcs.textureEnabled >> 2u) & 1u) != 0u;
  bool tmu0AlphaInvert = ((pcs.textureEnabled >> 3u) & 1u) != 0u;
  bool tmu1RgbInvert = ((pcs.textureEnabled >> 4u) & 1u) != 0u;
  bool tmu1AlphaInvert = ((pcs.textureEnabled >> 5u) & 1u) != 0u;

  // Unpack combiner modes for TMU0 and TMU1 (8-bit precision, separate fields)
  uint rgbFunc0 = (pcs.tmuCombinerModes0 >> 0u) & 0xFFu;
  uint rgbFactor0 = (pcs.tmuCombinerModes0 >> 8u) & 0xFFu;
  uint alphaFunc0 = (pcs.tmuCombinerModes0 >> 16u) & 0xFFu;
  uint alphaFactor0 = (pcs.tmuCombinerModes0 >> 24u) & 0xFFu;

  uint rgbFunc1 = (pcs.tmuCombinerModes1 >> 0u) & 0xFFu;
  uint rgbFactor1 = (pcs.tmuCombinerModes1 >> 8u) & 0xFFu;
  uint alphaFunc1 = (pcs.tmuCombinerModes1 >> 16u) & 0xFFu;
  uint alphaFactor1 = (pcs.tmuCombinerModes1 >> 24u) & 0xFFu;

  float divW0 = (useTmuOow & 1u) != 0u ? fragTmuOow.x : fragOOW;
  float divW1 = ((useTmuOow >> 1u) & 1u) != 0u ? fragTmuOow.y : fragOOW;

  // Near-plane clipping (respect per-TMU oow when GR_STWHINT_W_DIFF_TMU* is set)
  if (pcs.depthBufferMode == 2u) {
    if (fragOOW > 1.0) discard;
  } else {
    if ((tmu0Active && divW0 <= 0.0) || (tmu1Active && divW1 <= 0.0)) {
      discard;
    }
  }

  // 1. Sample TMU 1 (upstream)
  vec4 tmu1Color = vec4(1.0);
  float detailFactor1 = 0.0;
  float lodFrac1 = 0.0;
  if (tmu1Active) {
    ivec2 texSize = textureSize(texSampler1, 0);
    float tw = float(texSize.x);
    float th = float(texSize.y);
    float maxDim = max(tw, th);
    vec2 uv1 = (fragTex.zw / divW1) / 256.0;
    vec2 texelPos1 = uv1 * vec2(maxDim, maxDim);
    vec2 dx1 = dFdx(texelPos1);
    vec2 dy1 = dFdy(texelPos1);
    float rho2_1 = max(dot(dx1, dx1), dot(dy1, dy1));
    float rawLod1 = (rho2_1 > 1e-8) ? 0.5 * log2(rho2_1) : -10.0;
    float bias1 = float(int(pcs.texDetail1 & 0x3Fu) - 32);
    float scale1 = float((pcs.texDetail1 >> 8u) & 7u);
    float dmax1 = float((pcs.texDetail1 >> 16u) & 0xFFu) / 255.0;
    detailFactor1 = clamp((bias1 - rawLod1) * exp2(scale1) / 255.0, 0.0, dmax1);
    lodFrac1 = fract(max(0.0, rawLod1 + pcs.texLodBias1));
    tmu1Color = sampleTexture(texSampler1, uv1, 1u, tw, th);
  }

  // Evaluate TMU 1 combiner stage
  vec4 tmuConst0 = unpackRGBA(pcs.tmuConstantColor0);
  vec4 tmuConst1 = unpackRGBA(pcs.tmuConstantColor1);
  vec4 tmu1Out = tmu1Active ? evaluateTmuStage(rgbFunc1, rgbFactor1, alphaFunc1,
                                               alphaFactor1, tmu1RgbInvert,
                                               tmu1AlphaInvert, tmu1Color,
                                               fragColor, detailFactor1,
                                               lodFrac1)
                             : fragColor;
  if ((pcs.extTexColorCombine1 & 0x80000000u) != 0u) {
    tmu1Out.rgb = evaluateTmuExtRgb(pcs.extTexColorCombine1, tmu1Color,
                                    fragColor, tmuConst1, fragColor,
                                    detailFactor1, lodFrac1);
  }
  if ((pcs.extTexAlphaCombine1 & 0x80000000u) != 0u) {
    tmu1Out.a = evaluateTmuExtAlpha(pcs.extTexAlphaCombine1, tmu1Color,
                                    fragColor, tmuConst1, fragColor,
                                    detailFactor1, lodFrac1);
  }

  // 2. Sample TMU 0 (downstream)
  vec4 tmu0Color = vec4(1.0);
  float detailFactor0 = 0.0;
  float lodFrac0 = 0.0;
  if (tmu0Active) {
    ivec2 texSize = textureSize(texSampler0, 0);
    float tw = float(texSize.x);
    float th = float(texSize.y);
    float maxDim = max(tw, th);
    vec2 uv0 = (fragTex.xy / divW0) / 256.0;
    vec2 texelPos0 = uv0 * vec2(maxDim, maxDim);
    vec2 dx0 = dFdx(texelPos0);
    vec2 dy0 = dFdy(texelPos0);
    float rho2_0 = max(dot(dx0, dx0), dot(dy0, dy0));
    float rawLod0 = (rho2_0 > 1e-8) ? 0.5 * log2(rho2_0) : -10.0;
    float bias0 = float(int(pcs.texDetail0 & 0x3Fu) - 32);
    float scale0 = float((pcs.texDetail0 >> 8u) & 7u);
    float dmax0 = float((pcs.texDetail0 >> 16u) & 0xFFu) / 255.0;
    detailFactor0 = clamp((bias0 - rawLod0) * exp2(scale0) / 255.0, 0.0, dmax0);
    lodFrac0 = fract(max(0.0, rawLod0 + pcs.texLodBias0));
    tmu0Color = sampleTexture(texSampler0, uv0, 0u, tw, th);
  }

  // Evaluate TMU 0 combiner stage
  vec4 tmu0Out = tmu0Active ? evaluateTmuStage(rgbFunc0, rgbFactor0, alphaFunc0,
                                               alphaFactor0, tmu0RgbInvert,
                                               tmu0AlphaInvert, tmu0Color,
                                               tmu1Out, detailFactor0, lodFrac0)
                             : tmu1Out;
  if ((pcs.extTexColorCombine0 & 0x80000000u) != 0u) {
    tmu0Out.rgb = evaluateTmuExtRgb(pcs.extTexColorCombine0, tmu0Color,
                                    tmu1Out, tmuConst0, fragColor,
                                    detailFactor0, lodFrac0);
  }
  if ((pcs.extTexAlphaCombine0 & 0x80000000u) != 0u) {
    tmu0Out.a = evaluateTmuExtAlpha(pcs.extTexAlphaCombine0, tmu0Color,
                                    tmu1Out, tmuConst0, fragColor,
                                    detailFactor0, lodFrac0);
  }

  vec4 texColor = tmu0Out;

  // Route Alpha Combiner inputs
  float aLocal = 1.0;
  float aOther = 1.0;
  float finalA = 0.0;
  if ((pcs.extAlphaCombine & 0x80000000u) != 0u) {
    finalA = evaluateFbiExtAlpha(pcs.extAlphaCombine, fragColor, constantColor,
                                 texColor, aLocal, aOther);
  } else {
    if (pcs.alphaLocal == 0u)
      aLocal = fragColor.a;
    else if (pcs.alphaLocal == 1u)
      aLocal = constantColor.a;

    if (pcs.alphaOther == 0u)
      aOther = fragColor.a;
    else if (pcs.alphaOther == 1u)
      aOther = texColor.a;
    else if (pcs.alphaOther == 2u)
      aOther = constantColor.a;

    // Evaluate Alpha Combiner Factor
    float factA = 0.0;
    if (pcs.alphaFactor == 1u || pcs.alphaFactor == 3u)
      factA = aLocal;
    else if (pcs.alphaFactor == 2u)
      factA = aOther;
    else if (pcs.alphaFactor == 4u)
      factA = texColor.a;
    else if (pcs.alphaFactor == 8u)
      factA = 1.0;
    else if (pcs.alphaFactor == 9u || pcs.alphaFactor == 11u)
      factA = 1.0 - aLocal;
    else if (pcs.alphaFactor == 10u)
      factA = 1.0 - aOther;
    else if (pcs.alphaFactor == 12u)
      factA = 1.0 - texColor.a;

    // Evaluate Alpha Combiner Function
    if (pcs.alphaFunc == 1u || pcs.alphaFunc == 2u)
      finalA = aLocal;
    else if (pcs.alphaFunc == 3u)
      finalA = aOther * factA;
    else if (pcs.alphaFunc == 4u || pcs.alphaFunc == 5u)
      finalA = aOther * factA + aLocal;
    else if (pcs.alphaFunc == 6u)
      finalA = (aOther - aLocal) * factA;
    else if (pcs.alphaFunc == 7u || pcs.alphaFunc == 8u)
      finalA = (aOther - aLocal) * factA + aLocal;
    else if (pcs.alphaFunc == 9u || pcs.alphaFunc == 16u)
      finalA = aLocal * (1.0 - factA);

    if (alphaInvert) finalA = 1.0 - finalA;
  }

  // Route Color Combiner inputs
  vec3 cOther = texColor.rgb;
  vec3 finalC = vec3(0.0);
  if ((pcs.extColorCombine & 0x80000000u) != 0u) {
    finalC = evaluateFbiExtColor(pcs.extColorCombine, fragColor, constantColor,
                                 texColor, aLocal, aOther);
  } else {
    vec3 cLocal = vec3(0.0);
    if (pcs.colorLocal == 0u)
      cLocal = fragColor.rgb;
    else if (pcs.colorLocal == 1u)
      cLocal = constantColor.rgb;

    cOther = vec3(0.0);
    if (pcs.colorOther == 0u)
      cOther = fragColor.rgb;
    else if (pcs.colorOther == 1u)
      cOther = texColor.rgb;
    else if (pcs.colorOther == 2u)
      cOther = constantColor.rgb;

    // Evaluate Color Combiner Factor
    vec3 factC = vec3(0.0);
    if (pcs.colorFactor == 1u)
      factC = cLocal;
    else if (pcs.colorFactor == 2u)
      factC = vec3(aOther);
    else if (pcs.colorFactor == 3u)
      factC = vec3(aLocal);
    else if (pcs.colorFactor == 4u)
      factC = vec3(texColor.a);
    else if (pcs.colorFactor == 5u)
      factC = texColor.rgb;
    else if (pcs.colorFactor == 8u)
      factC = vec3(1.0);
    else if (pcs.colorFactor == 9u)
      factC = vec3(1.0) - cLocal;
    else if (pcs.colorFactor == 10u)
      factC = vec3(1.0 - aOther);
    else if (pcs.colorFactor == 11u)
      factC = vec3(1.0 - aLocal);
    else if (pcs.colorFactor == 12u)
      factC = vec3(1.0 - texColor.a);

    // Evaluate Color Combiner Function
    if (pcs.colorFunc == 1u)
      finalC = cLocal;
    else if (pcs.colorFunc == 2u)
      finalC = vec3(aLocal);
    else if (pcs.colorFunc == 3u)
      finalC = cOther * factC;
    else if (pcs.colorFunc == 4u)
      finalC = cOther * factC + cLocal;
    else if (pcs.colorFunc == 5u)
      finalC = cOther * factC + vec3(aLocal);
    else if (pcs.colorFunc == 6u)
      finalC = (cOther - cLocal) * factC;
    else if (pcs.colorFunc == 7u)
      finalC = (cOther - cLocal) * factC + cLocal;
    else if (pcs.colorFunc == 8u)
      finalC = (cOther - cLocal) * factC + vec3(aLocal);
    else if (pcs.colorFunc == 9u)
      finalC = cLocal * (vec3(1.0) - factC);
    else if (pcs.colorFunc == 16u)
      finalC = vec3(aLocal) - cLocal * factC;

    if (colorInvert) finalC = vec3(1.0) - finalC;
  }

  vec4 col = vec4(finalC, finalA);

  // Chromakey Emulation
  if (chromakeyEnabled) {
    vec3 chromaTestColor = ((pcs.textureEnabled & 3u) != 0u) ? cOther : col.rgb;
    bool rangeEnabled = ((pcs.chromakeyRangeMode >> 28u) & 1u) == 1u;
    bool discardPixel = false;
    if (rangeEnabled) {
      discardPixel =
          IsColorInRange(chromaTestColor, chromakeyRangeMin.rgb,
                         chromakeyRangeMax.rgb, pcs.chromakeyRangeMode);
    } else {
      discardPixel = IsColorInRange(chromaTestColor, chromakeyValue.rgb,
                                    chromakeyValue.rgb, pcs.chromakeyRangeMode);
    }
    if (discardPixel) {
      discard;
    }
  }

  vec4 preFogColor = col;

  // Fogging Emulation
  uint fogSource = pcs.fogMode & 0x0Fu;
  if (fogSource != 0u) {
    float f = 0.0;
    if (fogSource == 4u) {  // UNIFIED_FOG_WITH_ITERATED_ALPHA
      f = fragColor.a;
    } else {  // Table-based fog
      float eyeW;
      if (fogSource == 1u) {  // UNIFIED_FOG_WITH_TABLE_ON_FOGCOORD
        eyeW = fragFogCoord;
      } else {  // UNIFIED_FOG_WITH_TABLE_ON_W
        eyeW = 1.0 / max(fragOOW, 1e-6);
      }
      if (eyeW < 1.0) eyeW = 1.0;

      // O(1) unrolled logarithmic binary search
      int idx = 0;
      if (eyeW >= s_tableIndexToW[32]) idx += 32;
      if (eyeW >= s_tableIndexToW[idx + 16]) idx += 16;
      if (eyeW >= s_tableIndexToW[idx + 8]) idx += 8;
      if (eyeW >= s_tableIndexToW[idx + 4]) idx += 4;
      if (eyeW >= s_tableIndexToW[idx + 2]) idx += 2;
      if (eyeW >= s_tableIndexToW[idx + 1]) idx += 1;

      idx = min(idx, 62);

      float w0 = s_tableIndexToW[idx];
      float w1 = s_tableIndexToW[idx + 1];

      // Unpack f0 at idx
      uint packedWord0 = pcs.fogTable[idx / 16][(idx / 4) % 4];
      uint shift0 = uint(idx % 4) * 8u;
      float f0 = float((packedWord0 >> shift0) & 0xFFu) / 255.0;

      // Unpack f1 at idx + 1
      uint packedWord1 = pcs.fogTable[(idx + 1) / 16][((idx + 1) / 4) % 4];
      uint shift1 = uint((idx + 1) % 4) * 8u;
      float f1 = float((packedWord1 >> shift1) & 0xFFu) / 255.0;

      // Linear interpolation factor
      float t = 0.0;
      if (w1 > w0) {
        t = (eyeW - w0) / (w1 - w0);
      }
      t = clamp(t, 0.0, 1.0);

      f = mix(f0, f1, t);
    }

    vec3 fogColorRGB;
    fogColorRGB.r = float((pcs.fogColor >> 16u) & 0xFFu) / 255.0;
    fogColorRGB.g = float((pcs.fogColor >> 8u) & 0xFFu) / 255.0;
    fogColorRGB.b = float(pcs.fogColor & 0xFFu) / 255.0;

    // Extract FOGMODE flags
    float mult =
        float((pcs.fogMode & 0x100u) != 0u);  // GR_FOG_MULT2 (SST_FOGMULT)
    float add =
        float((pcs.fogMode & 0x200u) != 0u);  // GR_FOG_ADD2  (SST_FOGADD)

    // Apply the unified branch-free hardware equation
    col.rgb =
        col.rgb * (1.0 - mult) * (1.0 - f) + fogColorRGB * (1.0 - add) * f;
  }

  // Emulate legacy 3dfx Glide alpha testing
  if (pcs.alphaTestOp != 7u) {
    float alpha = col.a;
    float refVal = pcs.alphaTestRef;
    bool passed = true;

    if (pcs.alphaTestOp == 0u)
      passed = false;  // GR_CMP_NEVER
    else if (pcs.alphaTestOp == 1u)
      passed = (alpha < refVal);  // GR_CMP_LESS
    else if (pcs.alphaTestOp == 2u)
      passed = (alpha == refVal);  // GR_CMP_EQUAL
    else if (pcs.alphaTestOp == 3u)
      passed = (alpha <= refVal);  // GR_CMP_LEQUAL
    else if (pcs.alphaTestOp == 4u)
      passed = (alpha > refVal);  // GR_CMP_GREATER
    else if (pcs.alphaTestOp == 5u)
      passed = (alpha != refVal);  // GR_CMP_NOTEQUAL
    else if (pcs.alphaTestOp == 6u)
      passed = (alpha >= refVal);  // GR_CMP_GEQUAL

    if (!passed) {
      discard;
    }
  }

#ifdef USE_WBUFFER_DEPTH
  // True W-buffer depth write (only compiled into depthMode == 2 pipelines so
  // Z-buffer & depth-off pipelines omit DepthReplacing and preserve Early-Z).
  if (pcs.depthBufferMode == 2u) {
    float eyeW = 1.0 / fragOOW;
    float normalizedW = (eyeW - pcs.depthNear) / (pcs.depthFar - pcs.depthNear);
    gl_FragDepth = clamp(normalizedW, 0.0, 1.0);
  } else {
    gl_FragDepth = gl_FragCoord.z;
  }
#endif

  // Apply Dithering to outColor and outPreFogColor
  outColor = applyDithering(col, ditherMode);
#ifdef USE_DUAL_SRC_BLEND
  outPreFogColor = applyDithering(preFogColor, ditherMode);
#endif
}