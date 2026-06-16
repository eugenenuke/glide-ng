# Implementation & Verification Plan: Phase 4

This authoritative implementation plan outlines our systematic software engineering approach for completing **Phase 4: Depth (Z/W) Comparison, Alpha Blending & Polygon Culling States** within the **3dfx `glide-ng` translation translation wrapper**.

---

## 1. Groundbreaking Foundational Architecture

### Traditional 3dfx State Pipelines vs. Vulkan Physical Realities
Classic 1990s graphics accelerators (Voodoo Graphics, Voodoo2, Voodoo3) utilized highly immediate, fine-grained state toggles. Game loops could dynamically inject blending operations, adjust depth buffer masking, or switch polygon culling orientation directly between individual `grDrawTriangle` calls.

To maximize execution efficiency across modern Khronos Vulkan architectures, our backend translation engine will aggregate these fine-grained state overrides into a highly optimized, dirty-flagged **Monolithic Pipeline State Cache** while updating our headless software CI rasterizer in absolute synchronization.

---

## 2. Systematic Task Execution Roadmap

### Task 4.1: Depth Buffer Comparison Engines (`Z-Buffer` & `W-Buffer`)
* **Core APIs**: `grDepthBufferMode()`, `grDepthBufferFunction()`, `grDepthMask()`, `grDepthBiasLevel()`.
* **State Management**:
  * Track active depth mode (`GR_DEPTHBUFFER_ZBUFFER` vs. `GR_DEPTHBUFFER_WBUFFER`).
  * Translate legacy comparison operators (`GR_CMP_LESS`, `GR_CMP_LEQUAL`, `GR_CMP_ALWAYS`, etc.) exactly to Khronos `vk::CompareOp`.
  * Track depth buffer write enablement (`vk::Bool32`) and polygon depth offset slopes (`grDepthBiasLevel`).
* **Software Scanline Integration**:
  * In `VulkanBackend`, allocate a dedicated virtual depth attachment: `std::vector<float> m_headlessDepthBuffer` matching testing viewport dimensions (800x600).
  * During software Gouraud triangle scanlines, compute exact barycentric interpolated $Z$ and $W$ subpixel values.
  * Evaluate pixel depth tests prior to color buffer overwrites. If the comparison succeeds and `grDepthMask` is `FXTRUE`, update both color and depth attachments!

---

### Task 4.2: Transparency Alpha Blending & Alpha Testing Pipelines
* **Core APIs**: `grAlphaBlendFunction()`, `grAlphaTestFunction()`, `grAlphaTestReferenceValue()`.
* **State Management**:
  * Translate primary blending factors (`GR_BLEND_ZERO`, `GR_BLEND_ONE`, `GR_BLEND_SRC_ALPHA`, `GR_BLEND_ONE_MINUS_SRC_ALPHA`, etc.) to Khronos `vk::BlendFactor` options for RGB and Alpha channels independently.
  * Configure threshold alpha comparators (`GR_CMP_GREATER`, `GR_CMP_GEQUAL`) and integer reference thresholds.
* **Software Scanline Integration**:
  * **Alpha Test Punch-Through**: During pixel dispatch, compare incoming interpolated alpha against our active reference threshold. Discard occluded subpixels immediately prior to memory access.
  * **True Transparency Mixing**: If an active blend mode is detected, fetch destination pixel colors from `m_headlessPixelMap`, execute our exact mathematical mixing equations ($C_{final} = C_{src} \cdot F_{src} + C_{dst} \cdot F_{dst}$), and store the resulting subpixel-snapped RGB word!

---

### Task 4.3: Dynamic Viewport Scissor Clipping & Polygon Culling
* **Core APIs**: `grClipWindow()`, `grCullMode()`.
* **State Management**:
  * Track active user clipping rectangles (`minx, miny, maxx, maxy`) and translate them exactly to Khronos `vk::Rect2D` scissor structs.
  * Fulfill polygon winding rejection orientation: `GR_CULL_DISABLE`, `GR_CULL_NEGATIVE` (clockwise), `GR_CULL_POSITIVE` (counter-clockwise). Map these exactly to `vk::CullModeFlagBits`.
* **Software Scanline Integration**:
  * **Scissor Rejection**: Restrict scanline generation horizontal/vertical loops (`minX .. maxX`, `minY .. maxY`) exactly to the intersection of the render target bounds and the active `grClipWindow`.
  * **Winding Occlusion**: Before scan-converting primitive geometry, calculate the signed 2D cross-product screen area ($Area = (x_1 - x_0)(y_2 - y_0) - (y_1 - y_0)(x_2 - x_0)$). Instantly cull non-visible backface polygon coordinates!

---

## 3. Rigorous Catch2 Validation Suite (`tests/test_architecture.cpp`)

We will author an uncompromising multi-lib automated integration testing matrix asserting 100% mathematical checksum correctness across both shared frontends (`libglide2x.so` and `libglide3x.so`) and all emulated card permutations.

### Test Matrix Protocol
1. **Depth Occlusion Parity**: Render a solid red triangle at $Z=0.8$. Render a solid green triangle closer to the camera at $Z=0.2$. Assert that our mapped off-screen pixel memory is 100% green and contains zero occluded red subpixels.
2. **Alpha Blending Accuracy**: Render a solid blue triangle. Configure alpha blending (`GR_BLEND_SRC_ALPHA`, `GR_BLEND_ONE_MINUS_SRC_ALPHA`). Render a 50% transparent red triangle on top. Mathematically verify exactly that the resulting pixel values match ideal purple mixing ($0x7F007F$).
3. **Scissor Box Containment**: Configure `grClipWindow(100, 100, 200, 200)`. Dispatch a massive 800x600 yellow quad. Mathematically inspect all border pixels outside the 100..200 range to confirm they remain absolutely untouched.

---

## 4. Final Sign-Off Milestones

* [ ] Fulfill programmatic state caching singletons in `src/core/` and `src/backends/vulkan/`.
* [ ] Implement full suite of exported entry points in `src/glide2/glide2_entry.cpp` and `src/glide3/glide3_entry.cpp`.
* [ ] Conclude automated CI integration assertions confirming 100% test success pass rates (`./build/bin/GlideWrapperTests`).
* [ ] Update our active Master Progress Roadmap (`docs/glide_progress_tracker.md`) to mark Phase 4 as 100% complete and triumphantly transition into **Phase 5**!
