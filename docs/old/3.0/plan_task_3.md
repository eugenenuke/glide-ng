# Implementation Plan: Phase 3 (Primitive Rendering)

This implementation plan outlines our strategy for executing **Phase 3 (Basic Primitive Rendering & Dynamic Vertex Layouts)**. We will fulfill legacy layout specification, engineer a modern Vulkan graphics pipeline equipped with SPIR-V color interpolation shaders, and implement immediate drawing routines validated by automated off-screen Catch2 checksums.

---

## 1. Architectural Architecture & Specifications

### 1.1 Modernized Vertex Layout Specification (`Task 3.1`)
* **Glide 2.x Normalization**: Fixed `GrVertex` structure decoding (`x, y, z, r, g, b, a`).
* **Glide 3.x Normalization**: Implement `grVertexLayout(param, offset, mode)` to maintain a programmable byte-offset map. When games submit custom vertex pointers, our frontend dynamically translates them into our canonical C++ `ModernVertex` representation:
  ```cpp
  struct ModernVertex {
      std::array<float, 4> pos;   // X, Y, Z, W (normalized coordinate space)
      std::array<float, 4> color; // R, G, B, A (normalized 0.0f .. 1.0f)
  };
  ```

### 1.2 Vulkan Graphics Pipeline Architecture (`Task 3.2`)
* **Embedded SPIR-V Shaders**: To ensure 100% self-contained compilation without relying on external host CLI tools (`glslc`), we will author a standalone `VulkanShaders.h` utility containing pristine SPIR-V bytecode arrays for our `vk::ShaderModule` singletons (supporting Gouraud color interpolation).
* **Pipeline States**: Configure `vk::PipelineVertexInputStateCreateInfo` mapped to `ModernVertex`, set up a solid/wireframe rasterizer (`vk::PolygonMode::eFill` / `eLine`), disable culling for Phase 3 (`vk::CullModeFlagBits::eNone`), and allocate a host-visible, coherent `vk::Buffer` for immediate vertex geometry uploads.

### 1.3 Primitive Geometry Upload & Drawing (`Task 3.3`)
* Imbplement core Glide primitive dispatches: `grDrawPoint`, `grDrawLine`, `grDrawTriangle`, and `grDrawVertexArray`.
* **Simulated Bus Pacing**: Enforce synthetic microsecond delays per draw call (`m_config.drawCallDelayUs`) to mirror historic PCI/AGP bus saturations and prevent logic desynchronization.

---

## 2. Step-by-Step Execution Milestones (TODO Roadmap)

* [ ] **Milestone 1: Centralize Vertex Abstraction & Layout Singletons**
  * [ ] Author `src/core/VertexLayout.h` and `.cpp` implementing our dynamic layout offset tracker.
  * [ ] Add `grVertexLayout` entry point to `src/glide3/glide3_entry.cpp`. Connect Glide 2 dispatches to a fixed canonical layout.

* [ ] **Milestone 2: Engineer SPIR-V Shaders & Vulkan Pipeline**
  * [ ] Author `src/backends/vulkan/VulkanShaders.h` containing standalone SPIR-V Gouraud vertex and fragment shader bytecode.
  * [ ] Expand `VulkanBackend` to allocate a `vk::PipelineLayout`, `vk::Pipeline`, and our dynamic host-visible vertex `vk::Buffer`.

* [ ] **Milestone 3: Fulfill Immediate Drawing Entry Points**
  * [ ] Add pure virtual operations `DrawTriangle()`, `DrawLine()`, `DrawPoint()` to `IGraphicsBackend`.
  * [ ] Fulfill `grDrawTriangle`, `grDrawLine`, `grDrawPoint` in frontend entries. Connect them to `VulkanBackend` subpass record chains.

* [ ] **Milestone 4: Pixel-Perfect Catch2 Parity Verification**
  * [ ] Author automated integration runners in `tests/test_backend.cpp` rendering a Gouraud-interpolated triangle off-screen (800x600).
  * [ ] Map host video memory and compute SHA-256 / mathematical pixel checksums proving accurate color rasterization.

* [ ] **Milestone 5: Diagnostic Verification & Phase 3 Sign-Off**
  * [ ] Recompile toolchain (`cmake --build . --parallel`).
  * [ ] Execute automated validation suite (`./build/bin/GlideWrapperTests`) asserting 100% successful pass rates.
  * [ ] Formally update our master project roadmap (`docs/glide_progress_tracker.md`) to mark Phase 3 as 100% finished.

---

## 3. Sign-Off Criteria
* All 4 official emulated accelerator profiles (`SST1`, `CVG`, `H3`, `H5`) must execute the basic drawing pipeline without Vulkan validation errors, visual artifacts, or memory leaks.
* Catch2 offscreen pixel checksums must prove absolutely identical to mathematical rendering standards.
