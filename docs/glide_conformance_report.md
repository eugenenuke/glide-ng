# Glide API Conformance Report & Roadmap (REVISED)

This report provides a comprehensive, high-fidelity assessment of the Glide 2.x and Glide 3.x wrapper implementations in this repository. It maps the wrapper's coverage against the gold-standard historical reference SDKs, identifies critical compatibility and conformance gaps, and outlines an actionable roadmap to achieve 100% API compliance.

---

## 1. Executive Summary

The wrapper exhibits high coverage of the core Glide APIs, enabling many classic demos and tests to run successfully. However, several critical gaps—including missing linker symbols (such as `grGet` in Glide 2.x and `grTexLodBiasValue` in Glide 3.x), stubbed texture filtering pipelines, incomplete state serialization, missing vertex pipeline decoding for multi-texturing, and visual multi-texturing shader omissions in both Vulkan and OpenGL ES—prevent the wrapper from being 100% conformant.

### Conformance Metrics Summary (Corrected)

| API Version | Total Reference Functions | Fully Implemented | Stubs (No-op / Dummy) | Missing (Linker Gaps) | Coverage % (Implemented + Stubs) | Conformance % (Fully Implemented) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Glide 2.x** | **120** | **98** | **18** | **4** | 96.6% | 81.7% |
| **Glide 3.x** | **94** | **71** | **13** | **10** | 89.4% | 75.5% |

*Note: Functions like `grFinish` and `grDrawVertexArray` in the Glide 2.x headers are guarded by `#ifdef GLIDE3` and are therefore correctly excluded from the core Glide 2.x missing list, though they remain missing/stubs for Glide 3.x.*

---

## 2. Critical Gaps & Architectural Issues

Through deep-code analysis of the frontend wrappers, the vertex pipeline, and the backends (Vulkan, OpenGL ES, Software), we have identified **five critical architectural gaps**.

### A. Missing Linker Symbols (Immediate Crash Risk)
Several functions are declared in the wrapper's public headers but are completely absent from the source implementation. If a game attempts to link against or dynamically resolve these symbols, it will fail to load or crash immediately.

*   **Glide 2.x Critical Missing (4)**:
    *   `grGet`: **Highly Critical.** Declared in `include/glide2/glide.h` but completely missing in `glide2_entry.cpp`. Any game querying hardware capabilities will crash on startup.
    *   `guTexMemQueryAvail` & `guTexMemReset`: Query and reset texture allocator memory. Used by engines like early Unreal Engine and GoldSrc (Half-Life) to manage texture budgets.
    *   `guTexDownloadMipMapLevel`: Downloads a single mipmap level.
*   **Glide 3.x Critical Missing (10)**:
    *   `grTexLodBiasValue` (Used extensively in 11 test files): Adjusts texture mipmap sharpness.
    *   `grTexDownloadMipMapLevelPartial` (Used in 7 test files): Updates sub-regions of textures.
    *   `grErrorSetCallback`, `grAlphaControlsITRGBLighting`, `grStippleMode`, `grStipplePattern`, `grTexNCCTable`, `grLfbConstantDepth`, `grLfbWriteColorSwizzle`, `grLfbWriteColorFormat` (partial/missing variants).

### B. Multi-Texturing Pipeline & Shader Omissions (Visual Breakage)
Voodoo cards natively supported multi-texturing (TMU 0 and TMU 1) for lightmapping (e.g., *Quake*, *Unreal*). **Multi-texturing is completely broken across all backends** due to gaps in three layers:
1.  **Vertex Pipeline Gap**: The frontend `VertexLayout` completely lacks decoding support for TMU 1 texture coordinates (`GR_PARAM_ST1`) and scale hints (`GR_STWHINT_W_DIFF_TMU1`). The GPU receives zero/garbage coordinates for TMU 1.
2.  **Vulkan Backend Gap**: The Vulkan backend explicitly ignores any texture uploads or bindings targeting `tmu != 0` (e.g., `VulkanBackend::UploadTexture` lines 2174-2178).
3.  **OpenGL ES Backend Gap (False Positive Corrected)**: While the host-side GLES backend activates texture units and binds textures, its fragment shader completely ignores `uTexture1` and only samples from `uTexture0`.

### C. Mipmapping & Texture Filtering Disablement (Severe Visual Glitch)
While the wrapper's frontend successfully downloads complete mipmap chains, the actual rendering pipelines **completely disable mipmapping and trilinear filtering**:
1.  **Frontend Stub**: `grTexMipMapMode` is a stub in both Glide 2.x and 3.x wrappers, failing to update the backend.
2.  **Vulkan Sampler Limitation**: The Vulkan backend hardcodes `maxLod = 0.0f` and `mipmapMode = eNearest` during sampler creation, forcing the GPU to sample exclusively from LOD 0.
3.  **Software Backend Limitation**: The software rasterizer's texture sampler is hardcoded to only load and sample from `swizzledMipLevels[0]` (LOD 0).
4.  **Visual Impact**: Severe high-frequency aliasing (shimmering/sparkling) at mid-to-far distances in all games.

### D. Severe State Serialization Gaps (`grGlideGetState` / `grGlideSetState`)
In Glide 3.x, two critical pipeline states are updated on the backend but **omitted** from the serialization struct, causing global state leaks when games attempt to save/restore state:
1.  **Clip Window (`grClipWindow`)**: The clip coordinates are not saved. If a game restricts clipping to render a HUD/viewport and then restores state, the clipping window remains shrunk, clipping the main scene.
2.  **Color Mask (`grColorMask`)**: The write mask state is not saved. If a game disables color writes (e.g., during a depth prepass) and restores state, color writing remains disabled, resulting in a black screen.

### E. Color Precision & Retro Aesthetic Drift
*   **No 16-bit Downsampling**: The Voodoo Graphics hardware operates natively in 16-bit color depth (RGB565, ARGB1555, ARGB4444) and performs dithering.
*   **32-bit Framebuffer Bypass**: The software rasterizer writes colors directly as 32-bit integers without any downsampling or dithering, resulting in an unnaturally clean "modern" look rather than the authentic retro 3dfx dithered matrix. The `grDitherMode` API in the frontend wrapper is a complete stub.

---

## 3. Comprehensive API Status Matrix

### Glide 2.x Function Status
*   **Total**: 120 reference functions.
*   **Missing (4)**: `grGet`, `guTexMemQueryAvail`, `guTexMemReset`, `guTexDownloadMipMapLevel`.
*   **Stubs (18)**: `grBufferNumPending`, `grSstIsBusy`, `grSstControl`, `grSstSelect`, `grSstStatus`, `grDitherMode`, `grTexMinAddress`, `grTexMaxAddress`, `grTexDetailControl`, `grCheckForRoom`, `grTexMipMapMode`, `grTexMultibase`, `grTexMultibaseAddress`, `grColorMaskExt` (and other extension stubs).
*   **Implemented (98)**: All other core drawing, viewport, combining, and texture loading functions.

### Glide 3.x Function Status
*   **Total**: 94 reference functions.
*   **Missing (10)**: `grErrorSetCallback`, `grAlphaControlsITRGBLighting`, `grStippleMode`, `grStipplePattern`, `grTexNCCTable`, `grTexLodBiasValue`, `grTexDownloadMipMapLevelPartial`, `grLfbConstantDepth`, `grLfbWriteColorSwizzle`, `grLfbWriteColorFormat` (partial/missing variants).
*   **Stubs (13)**: `grRenderBuffer`, `grSelectContext`, `grSstSelect`, `grDitherMode`, `grReset`, `grTexMinAddress`, `grTexMaxAddress`, `grTexDetailControl`, `grTexMipMapMode`, `grTexMultibase`, `grTexMultibaseAddress`, `grLfbConstantDepth` (stubbed in some paths).
*   **Implemented (71)**: All other core drawing, viewport, combining, and texture loading functions.

---

## 4. Actionable Remediation Roadmap

To achieve 100% conformance and authentic rendering, we propose a 4-phase implementation plan.

### Phase 1: Critical Linker & Runtime Fixes (High Priority)
*Objective: Eliminate startup crashes and linker errors in Glide 2.x and 3.x applications.*

1.  **Implement Glide 2.x `grGet`**:
    *   Fully implement `grGet` in `glide2_entry.cpp` mapping queries like `GR_MEMORY_TMU` to the active config, preventing linker errors.
2.  **Implement Glide 3.x Missing Symbols**:
    *   `grTexLodBiasValue`: Cache the bias value *per TMU* and add it to the Vulkan `SamplerKey` to ensure unique sampler allocation, rather than using a single global variable.
    *   `grTexDownloadMipMapLevelPartial`: Update `TextureManager` to execute buffer-to-image copies with custom offsets and extents.
3.  **Implement Glide 2.x Missing `gu` Utilities**:
    *   `guTexMemReset`: Clear the virtual texture tracking database (`s_guTextures`) and reset the allocator pointer. Explicitly invoke `TextureManager::GetInstance().Reset()` to purge textures from the GPU backend and reset the allocation pool.
    *   `guTexMemQueryAvail`: Return the remaining space in the virtual TMU memory pool, querying `config.tmuMemoryMb` from the active profile.
    *   `guTexDownloadMipMapLevel`: Retrieve the texture descriptor and call `grTexDownloadMipMapLevel`. **Crucial**: Implement the pointer advancement side-effect on the caller's `src` double-pointer.
4.  **Late Glide 2.x Draw APIs**:
    *   Fully implement `grDrawVertexArray` and `grDrawVertexArrayLinear` in Glide 2.x using the canonical vertex decoder, enabling support for late Glide 2.x games.

### Phase 2: State Serialization & Multi-Texturing (High Priority)
*Objective: Fix severe rendering state leaks and enable multi-texturing under Vulkan and OpenGL ES.*

1.  **Fix State Serialization Omissions (Glide 3.x)**:
    *   Update `GlideWrapperState` struct to include clip window coordinates and color/alpha write masks.
    *   Update `grGlideGetState` to serialize these variables.
    *   Update `grGlideSetState` to deserialize and call `backend->SetClipWindow` and `backend->SetColorMask`.
2.  **Enable TMU 1 Vertex Decoding**:
    *   Add `m_offsetST1` and `m_offsetOOW1` to `VertexLayout`.
    *   Update `VertexLayout::DecodeVertex` to parse `GR_PARAM_ST1` and store it in `ModernVertex.tex[2]` and `ModernVertex.tex[3]`.
    *   Update `color.vert` to output the correct TMU 1 coordinates.
3.  **Enable Vulkan Multi-Texturing**:
    *   Remove the `tmu != 0` restriction in `VulkanBackend::UploadTexture` and `VulkanBackend::BindTexture`.
    *   Update Vulkan descriptor sets to bind both TMU 0 and TMU 1 samplers, matching the GLSL uniforms `Texture0` and `Texture1`.
4.  **Fix OpenGL ES Multi-Texturing Shader**:
    *   Update the GLES fragment shader to sample and combine `uTexture1` based on the active TMU combiner states, resolving the multi-texturing visual omission.

### Phase 3: Texture Mapping, Mipmapping & Filtering (Medium Priority)
*Objective: Restore mipmapping and trilinear filtering to eliminate high-frequency aliasing.*

1.  **Mipmap State Tracking**: Track `mipmapMode` and `lodBlend` from `grTexMipMapMode` in the frontend state.
2.  **Vulkan Sampler Cache Design (Corrected)**:
    *   Update `SamplerKey` to include a boolean `mipmappingEnabled` rather than making it dependent on the exact mip level count (preventing sampler cache bloat).
    *   Set `maxLod = 16.0f` when enabled, letting the `VkImageView` handle exact level clamping.
3.  **OpenGL ES Sampler Integration**: Combine `minFilter` with `mipmapMode` to select correct OpenGL mipmapped filters (e.g., `GL_LINEAR_MIPMAP_LINEAR`).
4.  **Software Sampler Integration**:
    *   Implement LOD calculation in `SoftwareBackend::renderPixel` using analytical barycentric gradients from the triangle rasterizer (rather than numerical finite differences) for optimal performance.
    *   Gate this feature behind a configuration toggle in `glide-ng.ini`.

### Phase 4: Authentic Retro Aesthetic & Advanced Features (Medium-Low Priority)
*Objective: Restore the authentic 16-bit dithered look and advanced hardware features.*

1.  **16-bit Dithering (Software Backend)**:
    *   Implement a 4x4 dither matrix lookup in `SoftwareBackend::renderPixel`.
    *   Quantize the color to 16-bit (RGB565 or ARGB1555), and then immediately upscale it back to 32-bit before writing to the presentation framebuffer, preserving the 3dfx retro dither-matrix aesthetic.
    *   Connect `grDitherMode` to toggle this dithering pipeline.
2.  **Stipple Transparency**:
    *   Pass the 32-bit stipple pattern (`grStipplePattern`) to the fragment shader.
    *   In the shader, perform a screen-space coordinate modulo check and discard fragments matching the stipple mask to emulate screen-door transparency.
3.  **Resolve Remaining Stubs**:
    *   Implement `grRenderBuffer` to transition backend render pass targets.
    *   Implement basic multibase texturing support.
