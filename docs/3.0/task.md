# `/goal` Execution Tracker: Phase 3 (Primitive Rendering)

This living task roadmap tracks our operational milestones during the automated completion of **Phase 3 (Basic Primitive Rendering & Dynamic Vertex Layouts)**.

---

## 1. Operational Execution Milestones

* [x] **Centralize Vertex Abstraction & Layout Singletons**
  * [x] Author `src/core/VertexLayout.h` and `.cpp` implementing our dynamic layout byte-offset tracker.
  * [x] Fulfill `grVertexLayout` entry point in `src/glide3/glide3_entry.cpp` and connect Glide 2 dispatches to canonical alignment.

* [x] **Engineer SPIR-V Shaders & Configure Vulkan Graphics Pipeline**
  * [x] Author `src/backends/vulkan/VulkanShaders.h` containing standalone SPIR-V Gouraud vertex and fragment shader bytecode arrays.
  * [x] Expand `VulkanBackend` to allocate a `vk::PipelineLayout`, `vk::Pipeline`, and dynamic host-visible vertex `vk::Buffer`.

* [x] **Fulfill Immediate Drawing Entry Points**
  * [x] Add pure virtual operations `DrawTriangle()`, `DrawLine()`, `DrawPoint()` to `IGraphicsBackend`.
  * [x] Fulfill `grDrawTriangle`, `grDrawLine`, `grDrawPoint` in frontend entries. Connect them to `VulkanBackend` command subpasses.

* [x] **Pixel-Perfect Catch2 Parity Verification Runner**
  * [x] Author automated integration runners in `tests/test_backend.cpp` (or `test_architecture.cpp`) rendering Gouraud triangles off-screen (800x600).
  * [x] Verify mapped video memory against mathematical pixel checksum targets across all 4 emulated accelerator profiles (`SST1`, `CVG`, `H3`, `H5`).

* [x] **Diagnostic Recompilation & Final Phase 3 Sign-Off**
  * [x] Execute parallel CMake toolchain rebuild (`cmake --build . --parallel`).
  * [x] Fulfill validation runner (`./build/bin/GlideWrapperTests`) asserting 100% test success pass rates.
  * [x] Fulfill final `walkthrough.md` report artifact and update our master roadmap (`docs/glide_progress_tracker.md`) to mark Phase 3 as 100% complete.
