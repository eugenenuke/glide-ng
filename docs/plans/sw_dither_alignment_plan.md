# Implementation Plan - Software Backend 1-Bit Alpha Dithering Alignment

This document outlines the technical design to align the Software backend's alpha dithering and blending logic with the GPU backends (Vulkan and OpenGL ES 3.2), resolving the stippling visual differences and achieving perfect pixel-level parity.

---

## 1. Goal Description

In our comparative pixel-by-pixel analysis of the headless screenshots, we discovered that:
*   The **Vulkan** and **OpenGL ES** backends emulate the Voodoo3's 16-bit `ARGB1555` format by **dithering the fragment alpha channel to exactly 1 bit (0.0 or 1.0)** when dithering is active. Because of this, the stippled-in pixels of the stripe are rendered completely opaque, maintaining maximum contrast against the background and producing a sharp, grainy retro stipple pattern.
*   The **Software** backend kept the alpha channel at full 8-bit precision, performing smooth, modernized blending. This washed out the stipple grain and caused it to blur into a solid, washed-out band when upscaled.

To achieve perfect visual parity, we will update the Software backend's pixel pipeline to implement the same **1-bit alpha dithering (`ARGB1555`)** when dithering is active.

---

## 2. Proposed Changes

### Core Rasterizer

#### [MODIFY] [SoftwareBackend.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/software/SoftwareBackend.cpp)

We will modify the inner pixel shader lambda (`renderPixel`) in [SoftwareBackend.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/software/SoftwareBackend.cpp#L1667-L1746) to:
1.  Compute a dithered 1-bit alpha value (`blendedAlpha`) before the blending equations are evaluated, matching the exact GPU fragment shader logic.
2.  Use this `blendedAlpha` in place of the high-precision `alpha` in all source/destination blending factor computations.
3.  Update the final output `aVal` to use this dithered alpha, ensuring the framebuffer also stores the quantized 1-bit alpha.
4.  Fix a minor, pre-existing typo in the Blue channel dithering formula (which used `gVal` instead of `bVal` for the LSB bit-shift).

Here is the precise planned code diff:

```diff
@@ -1666,38 +1666,69 @@
 
             uint32_t rVal = static_cast<uint32_t>(std::min(255.0f, red * 255.0f + 0.5f));
             uint32_t gVal = static_cast<uint32_t>(std::min(255.0f, green * 255.0f + 0.5f));
             uint32_t bVal = static_cast<uint32_t>(std::min(255.0f, blue * 255.0f + 0.5f));
 
+            // 1. Compute 1-bit dithered alpha (emulating ARGB1555 / RGB565 retro hardware limits)
+            float blendedAlpha = alpha;
+            if (m_ditherMode != 0) {
+                bool useAlpha = (m_alphaTestOp != 7) || (alpha < 0.99f);
+                if (useAlpha) {
+                    static const uint8_t dither_matrix_4x4[16] = {
+                         0,  8,  2, 10,
+                        12,  4, 14,  6,
+                         3, 11,  1,  9,
+                        15,  7, 13,  5
+                    };
+                    static const uint8_t dither_matrix_2x2[16] = {
+                         2, 10,  2, 10,
+                        14,  6, 14,  6,
+                         2, 10,  2, 10,
+                        14,  6, 14,  6
+                    };
+                    int ditherX = x & 3;
+                    int ditherY = y & 3;
+                    uint8_t dith = (m_ditherMode == 2) ? dither_matrix_4x4[ditherY * 4 + ditherX] :
+                                   (m_ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX] : 0;
+                    int matrixSize = (m_ditherMode == 2) ? 16 : 4;
+
+                    float a255 = alpha * 255.0f;
+                    int ditherA = (static_cast<int>(dith) * 128) / matrixSize;
+                    int a = static_cast<int>(a255) + ditherA;
+                    int a1 = std::max(0, std::min(255, a)) / 128;
+                    a1 = std::min(1, a1);
+                    blendedAlpha = static_cast<float>(a1);
+                } else {
+                    blendedAlpha = 1.0f; // RGB565 is fully opaque
+                }
+            }
+            aVal = static_cast<uint32_t>(blendedAlpha * 255.0f + 0.5f);
+
             if (m_rgbSrcBlend != 4 || m_rgbDstBlend != 0) {
                 uint32_t dstWord = pixels[pIdx];
                 float dstR = ((dstWord >> 16) & 0xFF) / 255.0f;
                 float dstG = ((dstWord >> 8) & 0xFF) / 255.0f;
                 float dstB = (dstWord & 0xFF) / 255.0f;
                 float dstA = ((dstWord >> 24) & 0xFF) / 255.0f;
 
                 auto getFactor = [&](uint32_t factorMode, float srcC, float dstC, float srcAVal, float dstAVal, bool isDest, float prefogC) {
                     switch (factorMode) {
                         case 0: return 0.0f;
                         case 1: return srcAVal;
                         case 2: return isDest ? srcC : dstC;
                         case 3: return dstAVal;
                         case 4: return 1.0f;
                         case 5: return 1.0f - srcAVal;
                         case 6: return isDest ? (1.0f - srcC) : (1.0f - dstC);
                         case 7: return 1.0f - dstAVal;
                         case 15: return prefogC;
                         default: return 1.0f;
                     }
                 };
 
-                float sFR = getFactor(m_rgbSrcBlend, red, dstR, alpha, dstA, false, prefogR);
-                float sFG = getFactor(m_rgbSrcBlend, green, dstG, alpha, dstA, false, prefogG);
-                float sFB = getFactor(m_rgbSrcBlend, blue, dstB, alpha, dstA, false, prefogB);
+                float sFR = getFactor(m_rgbSrcBlend, red, dstR, blendedAlpha, dstA, false, prefogR);
+                float sFG = getFactor(m_rgbSrcBlend, green, dstG, blendedAlpha, dstA, false, prefogG);
+                float sFB = getFactor(m_rgbSrcBlend, blue, dstB, blendedAlpha, dstA, false, prefogB);
 
-                float dFR = getFactor(m_rgbDstBlend, red, dstR, alpha, dstA, true, prefogR);
-                float dFG = getFactor(m_rgbDstBlend, green, dstG, alpha, dstA, true, prefogG);
-                float dFB = getFactor(m_rgbDstBlend, blue, dstB, alpha, dstA, true, prefogB);
+                float dFR = getFactor(m_rgbDstBlend, red, dstR, blendedAlpha, dstA, true, prefogR);
+                float dFG = getFactor(m_rgbDstBlend, green, dstG, blendedAlpha, dstA, true, prefogG);
+                float dFB = getFactor(m_rgbDstBlend, blue, dstB, blendedAlpha, dstA, true, prefogB);
 
-                float sFA = getFactor(m_alphaSrcBlend, alpha, dstA, alpha, dstA, false, prefogA);
-                float dFA = getFactor(m_alphaDstBlend, alpha, dstA, alpha, dstA, true, prefogA);
-                float blendedAlpha = std::min(1.0f, alpha * sFA + dstA * dFA);
+                float sFA = getFactor(m_alphaSrcBlend, blendedAlpha, dstA, blendedAlpha, dstA, false, prefogA);
+                float dFA = getFactor(m_alphaDstBlend, blendedAlpha, dstA, blendedAlpha, dstA, true, prefogA);
+                float finalBlendedAlpha = std::min(1.0f, blendedAlpha * sFA + dstA * dFA);
 
                 red   = std::min(1.0f, red   * sFR + dstR * dFR);
                 green = std::min(1.0f, green * sFG + dstG * dFG);
                 blue  = std::min(1.0f, blue  * sFB + dstB * dFB);
 
                 rVal = static_cast<uint32_t>(red * 255.0f + 0.5f);
                 gVal = static_cast<uint32_t>(green * 255.0f + 0.5f);
                 bVal = static_cast<uint32_t>(blue * 255.0f + 0.5f);
-                aVal = static_cast<uint32_t>(blendedAlpha * 255.0f + 0.5f);
+                aVal = static_cast<uint32_t>(finalBlendedAlpha * 255.0f + 0.5f);
             }
 
             // 16-Bit Dithering (Post-Blend RGB565 Quantization Only, as Alpha is already stippled!)
             if (m_ditherMode != 0) {
                 static const uint8_t dither_matrix_4x4[16] = {
                      0,  8,  2, 10,
                     12,  4, 14,  6,
                      3, 11,  1,  9,
                     15,  7, 13,  5
                 };
                 static const uint8_t dither_matrix_2x2[16] = {
                      2, 10,  2, 10,
                     14,  6, 14,  6,
                      2, 10,  2, 10,
                     14,  6, 14,  6
                 };
                 int ditherX = x & 3;
                 int ditherY = y & 3;
                 uint8_t dith = (m_ditherMode == 2) ? dither_matrix_4x4[ditherY * 4 + ditherX] :
                                (m_ditherMode == 1) ? dither_matrix_2x2[ditherY * 4 + ditherX] : 0;
 
                 uint32_t ditheredR = (((rVal << 1) - (rVal >> 4) + (rVal >> 7) + dith) >> 1);
                 uint32_t quantizedR = std::min(31u, ditheredR >> 3);
 
                 uint32_t ditheredG = (((gVal << 2) - (gVal >> 4) + (gVal >> 6) + dith) >> 2);
                 uint32_t quantizedG = std::min(63u, ditheredG >> 2);
 
-                uint32_t ditheredB = (((bVal << 1) - (bVal >> 4) + (gVal >> 7) + dith) >> 1);
+                // Fix typo: use bVal >> 7 instead of gVal >> 7
+                uint32_t ditheredB = (((bVal << 1) - (bVal >> 4) + (bVal >> 7) + dith) >> 1);
                 uint32_t quantizedB = std::min(31u, ditheredB >> 3);
 
                 rVal = (quantizedR << 3) | (quantizedR >> 2);
                 gVal = (quantizedG << 2) | (quantizedG >> 4);
                 bVal = (quantizedB << 3) | (quantizedB >> 2);
             }
```

---

## 3. Verification Plan

### Automated Tests
We will execute the full suite of Catch2 unit tests to verify that no functional regressions were introduced:
```bash
./build/bin/Glide3xWrapperTests
./build/bin/Glide2xWrapperTests
```

### Manual Verification & Visual Parity Check
1.  Compile the project:
    ```bash
    cmake --build build -j$(nproc)
    ```
2.  Capture a new headless Software screenshot under the exact same configuration:
    ```bash
    GLIDE_WRAPPER_BACKEND=software GLIDE_HEADLESS=1 ./build/bin/Glide3xShowcaseDemo --dither 2 --stipple 1 --depth 1 --alpha 0 --screenshot /usr/local/google/home/eianiuk/.gemini/jetski/brain/f96ca68c-6e31-4c86-a397-b3a8305ba563/new_sw_4x4.tga
    convert /usr/local/google/home/eianiuk/.gemini/jetski/brain/f96ca68c-6e31-4c86-a397-b3a8305ba563/new_sw_4x4.tga /usr/local/google/home/eianiuk/.gemini/jetski/brain/f96ca68c-6e31-4c86-a397-b3a8305ba563/new_sw_4x4.png
    ```
3.  Execute a pixel-level comparison script between the new Software output `new_sw_4x4.tga` and the Vulkan reference `init_vk_4x4.tga` specifically over the background to verify that:
    *   The number of differing pixels inside the stripe has dropped to **0**.
    *   The stripe's top/middle/bottom sections over the background are now beautifully and sharply stippled in Software, perfectly matching Vulkan!
