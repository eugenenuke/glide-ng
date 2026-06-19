# Operational Execution Tracker: Phase 4

This living task roadmap tracks our operational execution milestones during the implementation of **Phase 4 (Depth Z/W Comparison, Alpha Blending & Polygon Culling States)**.

---

## 1. Operational Milestones

* [ ] **Fulfill Depth Buffer State Pipelines & Core Engines**
  * [ ] Implement `grDepthBufferMode()`, `grDepthBufferFunction()`, `grDepthMask()`, `grDepthBiasLevel()` in frontends.
  * [ ] Add pipeline state tracking to `IGraphicsBackend` and `VulkanBackend`.
  * [ ] Allocate virtual depth buffer attachment (`m_headlessDepthBuffer`) and integrate exact subpixel depth comparisons into our software CI rasterizer.

* [ ] **Fulfill Transparency Alpha Blending & Alpha Testing Pipelines**
  * [ ] Implement `grAlphaBlendFunction()`, `grAlphaTestFunction()`, `grAlphaTestReferenceValue()` in frontends.
  * [ ] Translate classic blend factors and comparators to Khronos Vulkan structures.
  * [ ] Integrate true RGB/Alpha transparency mixing and threshold punch-through alpha rejection into our software CI rasterizer.

* [ ] **Fulfill Dynamic Scissor Clipping & Polygon Culling**
  * [ ] Track viewport clipping bounds (`grClipWindow`) and map them to active scanline generation loops.
  * [ ] Fulfill polygon winding rejection (`grCullMode`: `NEGATIVE`, `POSITIVE`, `DISABLE`) in our coordinate pre-computation phase.

* [ ] **Pixel-Perfect Multi-Lib Catch2 Validation Matrix**
  * [ ] Author automated integration runners in `tests/test_architecture.cpp` validating Phase 4.
  * [ ] Test 1: Assert perfect depth occlusion parity (closer green triangle over red).
  * [ ] Test 2: Assert precise 50% transparency alpha blending (blue over red $\rightarrow$ purple $0x7F007F$).
  * [ ] Test 3: Assert flawless scissor box containment (yellow quad clamped to 100x100 box).

* [ ] **Diagnostic Recompilation & Final Phase 4 Sign-Off**
  * [ ] Execute parallel CMake toolchain rebuild (`cmake --build . --parallel`).
  * [ ] Fulfill validation runner (`./build/bin/GlideWrapperTests`) asserting 100% test success pass rates.
  * [ ] Fulfill final `walkthrough_phase_4.md` summary report and update our master roadmap (`docs/glide_progress_tracker.md`) to mark Phase 4 as 100% complete.
