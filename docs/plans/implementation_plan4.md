# Phase 4: Authentic Retro Aesthetic & Advanced Features

This document outlines the comprehensive, step-by-step technical implementation plan for Phase 4 of the `project-ng` Glide Wrapper. The goal of this phase is to restore the authentic retro visual characteristics of 3dfx Voodoo hardware and implement advanced pipeline features symmetrically across the Software, Vulkan, and OpenGL ES backends.

---

## User Review Required

We have identified two critical architectural adjustments necessary for correctness, performance, and hardware parity. Please review these decisions:

> [!IMPORTANT]
> **1. OpenGL ES Depth Buffer Unification**
> The current OpenGL ES backend allocates and swaps two separate depth renderbuffers. This is an architectural design flaw; physical Voodoo hardware utilized a single shared depth (Z/W) buffer. In Phase 4, we will refactor the GLES backend to allocate a single depth renderbuffer and attach it to both FBOs. This ensures that switching render targets (e.g., rendering to front buffer overlays) preserves the depth state.

> [!IMPORTANT]
> **2. Vulkan Push Constants Reorganization**
> Vulkan push constants are strictly capped at 256 bytes and are currently fully utilized. To pass the new dithering and stippling states to the fragment shader without exceeding this limit (which would cause pipeline compilation failures), we will reorganize and pack "Block 8" of the push constants. We will pack the existing 16 bytes of chromakeying flags into a single 4-byte packed field, reclaiming 12 bytes of space to accommodate the new states.

---

## Open Questions

There are no outstanding open questions. The technical requirements and hardware behaviors are fully understood from the preceding investigation phase.

---

## Proposed Changes

The implementation is organized into logical components, moving from the frontend API definitions and state serialization up to the individual backend rasterization pipelines and shaders.

---

### 1. Frontend & State Serialization

We will implement the missing Glide 3.x stipple APIs, update dithering and render buffer control to propagate states, and ensure all new states are correctly serialized to prevent state leaks.

#### [MODIFY] [glide3_entry.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/glide3/glide3_entry.cpp)
*   **Implement Missing APIs**: Implement `grStippleMode(GrStippleMode_t mode)` and `grStipplePattern(GrStipplePattern_t pattern)` which are currently declared in the headers but completely missing from the source (causing linker errors).
*   **Update Dithering**: Update `grDitherMode` to cache the active mode in a static global `s_ditherMode` and propagate it to the backend via `backend->SetDitherMode`.
*   **Update Render Buffer**: Update `grRenderBuffer` (currently a stub) to propagate target buffer transitions to the backend via `backend->SetRenderBuffer`.
*   **State Serialization**: 
    *   Add `ditherMode`, `stippleMode`, and `stipplePattern` fields to the `GlideWrapperState` struct.
    *   Update `grGlideGetState` and `grGlideSetState` to serialize and deserialize these states, ensuring they do not exceed the 1024-byte client allocation limit (asserted at compile-time).

#### [MODIFY] [glide2_entry.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/glide2/glide2_entry.cpp)
*   **Update Dithering**: Update `grDitherMode` to cache and propagate the state to the backend.
*   **Implement Alpha Lighting**: Implement `grAlphaControlsITRGBLighting` (currently a stub) to cache the state, allowing the vertex pipeline to modulate vertex RGB by vertex Alpha when enabled.
*   **State Serialization**:
    *   Add `ditherMode` (1 byte) to the packed `GlideWrapperState` struct.
    *   To maintain the **exact 312-byte ABI size** required for Glide 2.x compatibility, decrease the padding array size from `pad[118]` to **`pad[117]`**.
    *   Update `grGlideGetState` and `grGlideSetState` to serialize/deserialize `ditherMode`.

---

### 2. Backend Abstraction Layer

We must extend the common backend interface to support the new pipeline states.

#### [MODIFY] [IGraphicsBackend.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/core/IGraphicsBackend.h)
*   Add the following virtual method declarations to the `IGraphicsBackend` interface:
    ```cpp
    virtual void SetDepthRange(float nearVal, float farVal) = 0;
    virtual void SetDitherMode(uint32_t mode) = 0;
    virtual void SetStippleState(uint32_t mode, uint32_t pattern) = 0;
    ```

---

### 3. Software Reference Backend

The software backend will implement true linear W-buffering, 16-bit retro dithering via a screen-space Bayer matrix, and screen-door stipple masking.

#### [MODIFY] [SoftwareBackendBase.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/SoftwareBackendBase.h)
*   Add member variables to track the new states:
    ```cpp
    float m_depthNear{0.0f};
    float m_depthFar{1.0f};
    uint32_t m_ditherMode{0};
    uint32_t m_stippleMode{0};
    uint32_t m_stipplePattern{0xFFFFFFFF};
    ```

#### [MODIFY] [SoftwareBackendBase.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/SoftwareBackendBase.cpp)
*   Implement `SetDepthRange`, `SetDitherMode`, and `SetStippleState` to store the states in the new member variables.
*   Update `ResetState` to restore these states to their default values on backend reset.

#### [MODIFY] [SoftwareBackend.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/software/SoftwareBackend.cpp)
*   **True W-Buffering**: In the `renderPixel` depth-testing stage:
    *   Instead of using the non-linear reciprocal W (`wVal`), compute the true linear eye-space depth: `w = 1.0f / wVal`.
    *   Normalize this linear depth using the active range: `targetDepth = (w - m_depthNear) / (m_depthFar - m_depthNear)`.
    *   Perform the depth comparison and write using this normalized linear depth.
*   **16-Bit Dithering**: In `WritePixelPipeline`, prior to writing color to the framebuffer:
    *   If `m_ditherMode` is active, retrieve the threshold from the hardware-accurate 4x4 or 2x2 Bayer matrices using screen coordinates `(x & 3, y & 3)`.
    *   Apply Voodoo-accurate bitwise dithering and quantization equations to downsample Red/Blue to 5-bit and Green to 6-bit.
    *   Immediately upscale the quantized 16-bit color back to 32-bit (using bit replication) before writing to the presentation buffer to preserve the retro dithered aesthetic.
*   **Stipple Transparency**: In `WritePixelPipeline`:
    *   If `m_stippleMode == GR_STIPPLE_PATTERN`, calculate the stipple bit index: `idx = ((y & 3) << 3) | (7 - (x & 7))`. If the bit in `m_stipplePattern` is `0`, discard the fragment (early return).
    *   If `m_stippleMode == GR_STIPPLE_ROTATE`, perform a bitwise rotation of `m_stipplePattern` on every written pixel and discard if the MSB is `0`.

---

### 4. Vulkan Backend

The Vulkan backend will implement active render pass transitions for render buffer control, push constant packing, and shader-level emulation of W-buffering, dithering, and stippling.

#### [MODIFY] [VulkanBackend.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/vulkan/VulkanBackend.h)
*   **Push Constants Reorganization**: Pack the 256-byte push constant struct:
    ```cpp
    // Replaced 16 bytes of texChromaEnabled/texChromaRangeMode with packed fields:
    uint32_t packedTexChroma;        // Packs ChromaKey enabled (bit 0-1) and Mode (bit 2-3)
    uint32_t stipplePattern;         // 32-bit stipple mask
    uint32_t ditherAndStippleMode;   // ditherMode (bits 0-7), stippleMode (bits 8-15)
    uint32_t reserved;               // Padding to maintain exact 256-byte layout
    ```

#### [MODIFY] [VulkanBackend.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/vulkan/VulkanBackend.cpp)
*   **Active Render Pass Transitioning**: In `SetRenderBuffer`, if the target buffer changes, invoke `FlushBatch()`. If an active render pass is open (`m_inRenderPass == true`), explicitly end it (`m_commandBuffer->endRenderPass()`) and set `m_inRenderPass = false`. This ensures subsequent draw calls begin a new render pass targeting the newly selected framebuffer.
*   **State Implementation**: Implement `SetDepthRange`, `SetDitherMode`, and `SetStippleState`.
*   **Push Constant Population**: Update `PopulatePushConstants` to pack the chromakeying flags, dither/stipple modes, stipple pattern, and depth range values into the reorganized push constant block.

#### [MODIFY] [color.frag](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/vulkan/shaders/color.frag)
*   **Shader W-Buffering**: If W-buffering is active, interpolate `fragOOW`, compute `w = 1.0 / fragOOW`, normalize it using the depth range push constants, and write it directly to `gl_FragDepth`.
*   **Shader Dithering**: Implement the Bayer matrix lookup and 16-bit color quantization/upscaling math directly in the fragment shader, controlled by the dither mode push constant.
*   **Shader Stippling**: Implement screen-space modulo checks against the 32-bit stipple pattern uniform, executing `discard` on matching fragments.

---

### 5. OpenGL ES Backend

The OpenGL ES backend will unify the depth buffer, implement dynamic FBO binding, and emulate W-buffering, dithering, and stippling in the fragment shader.

#### [MODIFY] [OpenGLESBackend.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/opengl_es/OpenGLESBackend.h)
*   Remove the duplicate depth renderbuffer tracking (`m_headlessDepthRenderbuffers[1]`).

#### [MODIFY] [OpenGLESBackend.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/opengl_es/OpenGLESBackend.cpp)
*   **Depth Buffer Unification**: In `Initialize`/`AttachWindow`, allocate only a single depth renderbuffer `m_headlessDepthRenderbuffers[0]`. Attach this single depth buffer to **both** `m_headlessFBOs[0]` and `m_headlessFBOs[1]`. Remove the depth buffer swapping logic from `SwapBuffers()`.
*   **Dynamic FBO Binding**: 
    *   Implement a helper `GetActiveGpuFbo()` to return the correct FBO based on `m_activeRenderBuffer` (where `0` is Front, and `1` is Back).
    *   Update `ClearBuffer`, `DrawPoint`, `DrawLine`, and `DrawTriangle` to call `glBindFramebuffer(GL_FRAMEBUFFER, GetActiveGpuFbo())` instead of hardcoding FBO 0.
*   **State Implementation**: Implement `SetDepthRange`, `SetDitherMode`, and `SetStippleState` to update uniforms (`uDepthRange`, `uDitherMode`, `uStippleMode`, `uStipplePattern`).
*   **Shader Integration**: Update the inline fragment shader source string (`geomFsSource`):
    *   Add W-buffering depth write (`gl_FragDepth` calculation).
    *   Add Bayer matrix dithering quantization.
    *   Add stipple pattern fragment discard logic.

---

## Showcase Demo

We will create a new, high-fidelity interactive demo to showcase and validate all Phase 4 features.

### [NEW] [phase4_showcase.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/demos/phase4_showcase.cpp)
*   **Scene Architecture**: Renders intersecting 3D planes (to demonstrate depth buffer precision and Z-vs-W interpolation artifacts) and overlapping transparent geometry (to demonstrate alpha blending, testing, and stipple transparency).
*   **Interactive Controls**:
    *   `[W]`: Cycle Depth Buffering (Disable -> Z-Buffer -> W-Buffer).
    *   `[D]`: Cycle Dithering (Disable -> 2x2 Bayer -> 4x4 Bayer).
    *   `[S]`: Cycle Stipple Transparency (Disable -> Screen-door Pattern -> Rotating Pattern).
    *   `[A]`: Cycle Alpha Blending (Toggle blending factors and separate RGB/Alpha blending).
    *   `[T]`: Cycle Alpha Testing (Toggle compare ops and reference values).
    *   `[B]`: Toggle Active Render Buffer (render scene to back buffer, or render HUD/overlays directly to the front buffer).
*   **Visual Feedback**: Displays an on-screen HUD showing the active states of all Phase 4 pipelines.

---

## Verification Plan

To satisfy the **Catch2 TDD Mandate** and ensure multi-backend visual parity, we will execute a rigorous verification pipeline.

### Automated Tests

We will implement dedicated unit tests in `tests/test_glide3.cpp` and `tests/test_architecture.cpp`. These tests will execute under the **Software reference backend** to guarantee headless, deterministic, and sandboxed validation:

1.  **Depth Buffering Tests**:
    *   Verify that Z-buffering and W-buffering result in correct draw order for intersecting geometry.
    *   Verify that W-buffering mathematically interpolates depth linearly in eye-space (by comparing pixel depth values against analytical expectations).
    *   Verify that `grDepthMask(FXFALSE)` successfully prevents depth writes while allowing color writes.
2.  **Alpha Blending & Testing Tests**:
    *   Verify all standard Glide source and destination blend factors.
    *   Verify separate RGB and Alpha blend factor operations.
    *   Verify alpha testing discard thresholds for various comparison operators (LESS, GREATER, EQUAL, etc.) against a reference value.
3.  **Dithering Tests**:
    *   Verify that enabling `GR_DITHER_4x4` or `GR_DITHER_2x2` quantizes a flat color gradient to the expected 16-bit (RGB565/1555) bit patterns, and that the resulting pixel values match the dither matrix offset math.
4.  **Stipple Transparency Tests**:
    *   Verify that rendering a quad with `GR_STIPPLE_PATTERN` discards pixels in a perfect grid matching the 32-bit stipple mask.
    *   Verify that `GR_STIPPLE_ROTATE` successfully shifts the mask on consecutive pixel writes.
5.  **State Serialization Tests**:
    *   Verify that `grGlideGetState` and `grGlideSetState` successfully serialize, deserialize, and restore the dither mode, stipple mode, stipple pattern, depth range, and write masks without any state leakage or memory corruption.

#### Test Execution Commands
```bash
# Compile the test suite
SKYBUILD=1 blaze build //tests:all

# Execute depth and alpha tests
SKYBUILD=1 blaze test //tests:test_glide3 --test_filter="Depth Buffering"
SKYBUILD=1 blaze test //tests:test_glide3 --test_filter="Alpha Blending and Testing"

# Execute dithering and stippling tests
SKYBUILD=1 blaze test //tests:test_glide3 --test_filter="Retro Aesthetic Dithering"
SKYBUILD=1 blaze test //tests:test_glide3 --test_filter="Stipple Transparency"

# Execute state serialization tests
SKYBUILD=1 blaze test //tests:test_architecture --test_filter="State Serialization"
```

### Manual Verification

1.  **Interactive Showcase**: Run `demos/phase4_showcase` under all three backends (Software, OpenGL ES, Vulkan) and interactively toggle states.
2.  **Visual Regression Check**: Capture screenshots of the showcase under the Software reference backend, Vulkan, and OpenGL ES, and visually verify that the 16-bit dithering and stipple transparency exhibit perfect parity across all three rendering pipelines.
