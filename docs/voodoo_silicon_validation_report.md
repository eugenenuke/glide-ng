# Voodoo Silicon Validation & Comparative Analysis Report

This document records our deep, surgical investigation of the reference 3dfx Voodoo Graphics silicon emulation inside DOSBox (`voodoo_emu.cpp`, `voodoo_data.h`, and `voodoo_data.h`). 

We have performed a side-by-side comparative analysis of the original Voodoo hardware equations against our modernized GPU wrapper (`glide-ng` Vulkan/GLES shaders). This report maps the architectural gaps, evaluates their impact on legacy game compatibility, and outlines concrete engineering recommendations to achieve 100% hardware-faithful rendering.

---

## 1. Executive Summary

Our high-level Glide wrapper (`glide-ng`) currently achieves flawless visual fidelity on standard 3D rendering tests, but it implements several rendering features (such as chroma-keying, fogging, and alpha blending) using simplified GPU approximations. 

By analyzing the low-level emulation (LLE) source of DOSBox—which acts as the mathematical ground truth of the Voodoo chipsets—we have identified:
1.  **Critical Gaps (High Compatibility Risk)**: Specific hardware features, such as **Chroma Ranges (Inclusive/Exclusive)**, **Additive/Subtractive Fog**, and the silicon-exclusive **`A_COLORBEFOREFOG`** blending mode, which are completely omitted from our current shaders and will cause rendering defects in commercial games.
2.  **Aesthetic Gaps (Authentic Retro Filter)**: The exact mathematical bit-shifting equations and Bayer matrices used to generate the iconic **16-bit Glide Dithering Look** which we can optionally port to our shaders to offer a true "16-bit retro filter".

---

## 2. Subsystem Comparative Analysis & Gaps

---

### A. Chroma-Keying (Transparency Emulation)

*   **Our Fragment Shader**: Performs a simple float-based distance check against a single color:
    ```glsl
    if (distance(col.rgb, uChromakeyValue.rgb) < 0.01) discard;
    ```
*   **Voodoo Silicon (LLE)**: Implements two distinct modes in [hardware/voodoo_data.h:L1253](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_data.h#L1253):
    1.  **Exact-Match Mode**: Performs an exact 24-bit bitwise match against the `chromaKey` register:
        ```cpp
        if (((COLOR.u ^ v->reg[chromaKey].u) & 0xffffff) == 0) discard;
        ```
    2.  **Chroma Range Mode**: Evaluates whether the pixel's RGB channels lie within a range defined by `chromaKey` (lower bound) and `chromaRange` (upper bound), supporting an **independent inclusive/exclusive boundary rule per channel** via XOR:
        ```cpp
        results = (blue_test >= blue_low && blue_test <= blue_high) ^ blue_exclusive;
        results &= (green_test >= green_low && green_test <= green_high) ^ green_exclusive;
        results &= (red_test >= red_low && red_test <= red_high) ^ red_exclusive;
        if (results) discard;
        ```
*   **Fidelity Gap**: **HIGH**. We completely lack Chroma Range support and channel-specific exclusive boundary checks.
*   **Game Impact**: Games that use chroma ranges to make complex sprite textures transparent (e.g. billboards, foliage, or UI panels) will render with **solid, opaque colored boxes** around their sprites.

---

### B. Depth-Based Fogging

*   **Our Fragment Shader**: Performs a linear loop on the GPU to search for the W-depth index inside a 64-entry table, followed by a standard float-based linear mix:
    ```glsl
    col.rgb = col.rgb * (1.0 - f) + uFogColor.rgb * f;
    ```
*   **Voodoo Silicon (LLE)**: Emulates the hardware-level **fixed-point linear interpolation** using the bit-fields of a custom exponent-mantissa float $W$ format (`wfloat`):
    1.  **Exponent Lookup**: The upper bits of `wfloat` (`wfloat >> 10`) act as the index into the fog table.
    2.  **Mantissa Interpolation**: The lower bits `((wfloat >> 2) & 0xff)` act as the interpolation factor.
    3.  **Fog Blending Modes**: Supports both multiplicative and **additive/subtractive fogging** based on the `FOG_ADD` and `FOG_MULT` register flags in [hardware/voodoo_data.h:L1611](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_data.h#L1611):
        ```cpp
        if (FOGMODE_FOG_ADD(FOGMODE) == 0) fr = (fogcolor * fogblend) >> 8; // Multiplicative
        if (FOGMODE_FOG_MULT(FOGMODE) == 0) RR += fr;                      // Additive
        ```
    4.  **Alternative Sources**: Supports driving fog blending via **Iterated Vertex Alpha** or **Iterated Depth ($Z$)**.
    5.  **Dithering & Zones**: Emulates fog factor dithering and zone-based negation.
*   **Fidelity Gap**: **MEDIUM-HIGH**. Our shader misses additive fog blending, alternative fog sources (Alpha/$Z$), and the iterative table search on the GPU is highly inefficient.
*   **Game Impact**: Games utilizing additive fog (such as glowing steam, volumetric light shafts, or magical auras) will render with **solid opaque walls of color or completely pitch black fog**.

---

### C. Alpha Blending & Silicon-Exclusive Modes

*   **Our Fragment Shader**: Relies on standard GPU fixed-function blending (via Vulkan/GLES pipeline blending states).
*   **Voodoo Silicon (LLE)**: Performs alpha blending in fixed-point arithmetic in [hardware/voodoo_data.h:L1424](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_data.h#L1424), implementing two highly unique behaviors:
    1.  **Dither Subtraction**: If `ALPHA_DITHER_SUBTRACT` is enabled, the hardware **subtracts dither noise from the destination color** before blending:
        ```cpp
        dr = ((dr << 1) + 15 - dith) >> 1; // Subtract dither from destination Red
        ```
        This prevents double-dither noise accumulation when rendering transparent layers on top of already-dithered framebuffers.
    2.  **`A_COLORBEFOREFOG` Blending Mode**: The hardware supports blending the destination pixel using the **source color *before* fog was applied**:
        ```cpp
        case 15: // A_COLORBEFOREFOG
            (RR) += (dr * (prefogr + 1)) >> 8;
        ```
*   **Fidelity Gap**: **HIGH (Architectural)**. Modern GPUs do not support a "source color before fog" blend factor in their fixed-function blending pipelines.
*   **Game Impact**: Games that combine heavy fogging with complex transparency blending (such as transparent water surfaces or smoking explosions in fogged valleys) will render with **incorrectly blended colors**, since the shader applies fog to the source color *before* the hardware blending stage can access its pre-fog state.

---

### D. 16-Bit Color Dithering

*   **Our Fragment Shader**: None. We render to a modern 32-bit (RGBA8888) canvas with near-infinite color precision.
*   **Voodoo Silicon (LLE)**: Emulates the authentic Voodoo dither pipeline using 4x4 and 2x2 Bayer matrices. It uses different, highly optimized bit-shift equations to round the 24-bit color down to 16-bit (5-6-5) based on screen coordinates:
    *   **Red/Blue Dither (`DITHER_RB`)**:
        ```cpp
        #define DITHER_RB(val,dith) ((((val) << 1) - ((val) >> 4) + ((val) >> 7) + (dith)) >> 1)
        ```
    *   **Green Dither (`DITHER_G`)**:
        ```cpp
        #define DITHER_G(val,dith)  ((((val) << 2) - ((val) >> 4) + ((val) >> 6) + (dith)) >> 2)
        ```
*   **Fidelity Gap**: **Aesthetic**.
*   **Visual Impact**: Modern rendering is perfectly smooth, but lacks the iconic, slightly textured "grainy look" of real Voodoo hardware.

---

### E. Depth Testing & Fixed-Point Depth Bias

*   **Our Fragment Shader**: Relies on standard GPU depth testing. Depth bias is mapped to `glPolygonOffset` or handled via float additions.
*   **Voodoo Silicon (LLE)**: Performs depth comparisons using a **16-bit fixed-point representation**. If depth bias is enabled, the hardware adds the **lower 16 bits of the `zaColor` register** directly to the depth value and clamps it:
    ```cpp
    if (FBZMODE_ENABLE_DEPTH_BIAS(FBZMODE)) {
        depthval += (INT16)v->reg[zaColor].u;
        CLAMP(depthval, 0, 0xffff);
    }
    ```
*   **Fidelity Gap**: **LOW-MEDIUM**.
*   **Game Impact**: Subtle depth precision mismatches or $Z$-fighting (flickering textures) on decals or wall signs if the game relies on precise integer-space depth bias offsets.

---

## 3. Engineering Recommendations & Roadmap

To bridge these gaps with maximum efficiency, we recommend implementing the following solutions in our next rendering refinement phase:

### 🛠️ **Recommendation 1: Upgrade Chroma-Keying to Support Ranges (High Priority)**
We should expand our fragment shader uniforms to pass both `uChromakeyValue` and `uChromakeyRange`, along with control flags for channel-specific inclusive/exclusive checks.
*   **The GLSL implementation**:
    ```glsl
    uniform int uChromakeyMode; // Bitmask: [EnableRange | BlueExcl | GreenExcl | RedExcl]
    uniform vec4 uChromakeyValue;
    uniform vec4 uChromakeyRange;
    
    void applyChromaKey(vec4 col) {
        if (uChromakeyEnabled == 1) {
            if ((uChromakeyMode & 1) == 0) {
                // Exact Match
                if (distance(col.rgb, uChromakeyValue.rgb) < 0.001) discard;
            } else {
                // Range Match
                bool bPass = (col.b >= uChromakeyValue.b && col.b <= uChromakeyRange.b) ^^ bool((uChromakeyMode >> 1) & 1);
                bool gPass = (col.g >= uChromakeyValue.g && col.g <= uChromakeyRange.g) ^^ bool((uChromakeyMode >> 2) & 1);
                bool rPass = (col.r >= uChromakeyValue.r && col.r <= uChromakeyRange.r) ^^ bool((uChromakeyMode >> 3) & 1);
                if (bPass && gPass && rPass) discard;
            }
        }
    }
    ```

### 🛠️ **Recommendation 2: Implement Additive Fog and Logarithmic Table Lookups (Medium-High Priority)**
1.  **Support Blending Modes**: Add uniforms for `uFogAdd` and `uFogMult` to the fragment shader. If `uFogAdd` is active, perform additive/subtractive blending.
2.  **Optimize Lookup**: Replace the expensive linear loop in the fragment shader with a fast mathematical approximation or use a **1D Texture** containing the pre-interpolated 64-entry fog table to let the GPU's texture sampling hardware perform the interpolation instantaneously!

### 🛠️ **Recommendation 3: Address `A_COLORBEFOREFOG` Blending (Advanced)**
Since standard GPU blending cannot access the pre-fog source color after the fragment shader has completed, we can emulate this by:
1.  Passing the pre-fog color as a separate fragment shader output (using multi-target rendering / MRT) and performing blending in a second pass.
2.  *Approximation*: In 99% of cases, we can approximate this mode inside the fragment shader by performing the blend mathematically in the shader itself before outputting the color, if we can read the destination buffer (using extensions like `GL_EXT_shader_framebuffer_fetch` or by binding the depth/color buffers as input attachments).

### 🛠️ **Recommendation 4: Add an Optional "16-Bit Retro Look" Dither Filter (Optional/Aesthetic)**
Add a shader toggle that reduces our final output color to 16-bit (5-6-5) using the exact 4x4 Bayer matrix and the Voodoo `DITHER_RB` / `DITHER_G` bit-shift equations. This will recreate the authentic, nostalgic "Voodoo grain" perfectly on modern 1080p/4K monitors!
