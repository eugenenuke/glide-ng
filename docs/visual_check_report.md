# Visual Verification Report: Perfect Vulkan Chromakeying & Showcase Demo Scaling

This report documents the implementation and visual verification of **Perfect Bilinear Vulkan Chromakeying** and **256x256 Showcase Demo Scaling** on the Vulkan backend of the 3dfx Glide Wrapper.

---

## 1. Perfect Bilinear Vulkan Chromakeying

### The Problem: Green Edge Halo / Fringe
In the standard Vulkan fragment shader pipeline, textures are filtered (e.g. bilinear interpolation) by the GPU's hardware sampler *before* the fragment shader receives the interpolated color. When chromakeying is enabled, the shader tests this interpolated color against the chroma-key range (e.g., pure green `0xFF00FF00`).

However, near the borders of the keyed region (such as the boundary of a magenta circle on a green background), the hardware sampler interpolates between the circle color and the background key color. This results in a mix of green and magenta (a brownish-green fringe) that lies *outside* the strict chroma-key range. Consequently, these blended edge pixels fail the chromakey test and are rendered, creating an unsightly **green halo/fringe** around transparent shapes on the Vulkan backend.

### The Solution: Pre-Interpolation Bilinear Chromakeying
To eliminate this halo, we replaced the post-interpolation texture-level chromakey block inside `color.frag` with a **manual pre-interpolation bilinear filtering chromakey** implementation:

1.  **Coordinate Calculation**: We calculate the exact continuous texel coordinates `texelCoord = uv * vec2(tw, th) - vec2(0.5)`.
2.  **Texel Fetching**: We determine the top-left integer coordinate `tl = floor(texelCoord)` and the interpolation fractional weights `f = fract(texelCoord)`. We then fetch the **4 raw, unfiltered neighbor texels** from the texture sampler using `texelFetch`:
    *   `c00` (top-left)
    *   `c10` (top-right)
    *   `c01` (bottom-left)
    *   `c11` (bottom-right)
3.  **Pre-Interpolation Keying**: We evaluate the chromakey test (`IsColorInRange`) on each of these 4 raw texels individually *before* any interpolation takes place. If a raw texel matches the key color, we instantly clear it to fully transparent `vec4(0.0)`.
4.  **Manual Bilinear Interpolation**: We manually blend the resulting keyed texels using the fractional weights `f.x` and `f.y` via `mix`:
    ```glsl
    vec4 c0 = mix(c00, c10, f.x);
    vec4 c1 = mix(c01, c11, f.x);
    texColor = mix(c0, c1, f.y);
    ```

By keying the texels *before* they are mixed, the green background color is fully discarded, and the interpolated edge pixels smoothly blend towards transparent `vec4(0.0)` instead of green. This completely eliminates the edge halo.

---

## 2. Showcase Demo & Coordinate Scaling

To showcase the high-fidelity rendering, we doubled the resolution of the procedural textures and expanded the coordinate limits in `glide3_extensions_demo.cpp`:

1.  **Buffer Sizing**: Allocated vector storages for `s_mirrorTexData`, `s_chromaTexData`, and `s_paletteTexData` were doubled from `128 * 128` to `256 * 256`.
2.  **Procedural Generators**:
    *   Updated texture generation loops to run over `256` iterations in both dimensions.
    *   In `GenerateMirrorTexture()`, scaled the quadrant boundary to `128` and the black borders to `255`.
    *   In `GenerateChromaTexture()`, centered the magenta circle at `127.5f` and doubled the radius from `40.0f` to `80.0f` to preserve relative scale.
    *   In `GeneratePaletteTexture()`, centered the radial gradient at `127.5f` and halved the distance multiplier from `4.0f` to `2.0f`, doubling the smooth radial puff's radius.
3.  **LOD Downloads**: Updated `smallLodLog2` and `largeLodLog2` from `GR_LOD_LOG2_128` to `GR_LOD_LOG2_256` for all textures (`mirrorInfo`, `chromaInfo`, `paletteInfo`).
4.  **Tiling Parity (`uvMax`)**:
    *   In the projection loop, we set:
        ```cpp
        float uvMax = (s_activeScreen == SCREEN_MIRROR) ? 512.0f : 256.0f;
        ```
    *   For **Screen 1 (Mirror)**, this maps to `[0.0, 2.0]` in normalized coordinates, triggering the `TEXMIRROR` extension's wrapping and mirroring boundaries perfectly.
    *   For **Screen 2 (Chroma)** and **Screen 3 (Palette)**, this maps to exactly `[0.0, 1.0]` in normalized coordinates, drawing a beautiful, full, centered circle/puff instead of a cropped quadrant.

---

## 3. Visual Verification Gallery

All 7 states were programmatically captured under a unified Xvfb virtual framebuffer display server on Vulkan. Below is the detailed analysis of each state.

### Screen 1: Texture Mirroring (`TEXMIRROR` Extension)

| State | Screenshot | Description |
| :--- | :---: | :--- |
| **Mirroring ON** (Seamless) | ![Mirror On](images/mirror_on.png) | Seamless 2x tiling. The asymmetric quadrants mirror perfectly across the tile boundaries, forming a stunning, contiguous diamond shape without any seams. |
| **Mirroring OFF** (Wrap) | ![Mirror Off](images/mirror_off.png) | Standard texture wrap. Obvious sharp discontinuity seams are visible where the coordinates wrap from `1.0` back to `0.0`. |

---

### Screen 2: Chromakeying (`TEXCHROMA` Extension)

| State | Screenshot | Description |
| :--- | :---: | :--- |
| **Chromakey Disabled** | ![Chroma Off](images/chroma_off.png) | The raw procedural texture is displayed. A magenta circle is rendered on a solid, bright green background box. |
| **Standard Chromakey** (Vulkan Halo) | ![Chroma Std](images/chroma_std.png) | The green background is keyed out, but an **ugly green/brown halo** is visible around the entire circumference of the circle. This is due to post-interpolation keying on Vulkan. |
| **Perfect Chromakey** (Manual Bilinear) | ![Chroma Perfect](images/chroma_perfect.png) | **Absolutely breathtaking, razor-sharp magenta circle with zero green halo!** The pre-interpolation bilinear shader code keys out the background perfectly, producing smooth transparency at the edges. |

---

### Screen 3: High-Fidelity Paletted (`PALETTE6666` Extension)

| State | Screenshot | Description |
| :--- | :---: | :--- |
| **Palette 6666 ON** (High Fidelity) | ![Palette On](images/palette_on.png) | Renders a gorgeous, smooth radial puff of light. The 6-bit alpha channel (`PALETTE_6666_EXT`) provides a highly detailed transparency gradient. |
| **Standard Palette** (Low Fidelity) | ![Palette Off](images/palette_off.png) | The radial puff exhibits harsh, jagged, blocky edges due to the standard 1-bit alpha limitation of standard P8 palettes. |

---

## 4. Conclusion

The manual pre-interpolation bilinear chromakeying implementation in `color.frag` is a complete success. It resolves a long-standing visual fidelity discrepancy in the Vulkan backend, bringing it to parity with professional modern rendering pipelines. Coupled with the 256x256 texture scaling, the extensions showcase demo looks incredibly polished and renders beautifully.
