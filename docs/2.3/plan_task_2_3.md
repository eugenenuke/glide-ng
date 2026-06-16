# Implementation Plan: Task 2.3 (VSync & Context Selection)

We are finalizing **Phase 2 (Context Attachment & Presentation Buffer Swapping)**. This plan maps out the native implementation of legacy context selection and target rendering buffers, validates our artificial frame presentation pacing, and integrates golden headless lifecycle asserts modeled after authentic 3dfx `test00.c`.

---

## 1. Architectural Scope & Specifications

### Legacy Context & Buffer Normalization
* **`grSelectContext(GrContext_t context)`**: Fulfill state machine switching for multi-context Glide 3 games.
* **`grRenderBuffer(GrBuffer_t buffer)`**: Allow retro game engines to direct primitives between `GR_BUFFER_BACKBUFFER` (standard double-buffering) and `GR_BUFFER_FRONTBUFFER` (immediate single-buffer visual tearing mode).

### VSync & Artificial Frame Pacing Validation
* Verify that `VulkanBackend::SwapBuffers()` accurately executes user-spec'd frame presentation capping (`m_config.maxFps`).
* Author automated Catch2 high-resolution clock assertions evaluating timestamp deltas across repeated presentation swap iterations.

### Headless Diagnostic Fixtures (`test00.c` Integration)
* Expand our multi-lib test runner to deep-verify the exact execution sequence of authentic `test00.c` (`grGlideInit` -> `grSstWinOpen` -> `grBufferClear` -> `grBufferSwap` -> `grSstWinClose`).

---

## 2. Step-by-Step Execution Milestones (TODO Roadmap)

* [ ] **Milestone 1: Implement Legacy Context & Buffer Switching**
  * [ ] Add `grSelectContext` and `grRenderBuffer` entries to `src/glide2/glide2_entry.cpp` and `src/glide3/glide3_entry.cpp`.
  * [ ] Connect them to `BackendManager` and ensure active logging dispatches (`GLIDE_LOG`).

* [ ] **Milestone 2: Engineer Frame Pacing Timestamp Assertions**
  * [ ] Add a Catch2 test `SECTION` in `tests/test_architecture.cpp` that configures `maxFps = 60.0f` and measures total host CPU elapsed time across 10 consecutive `SwapBuffers()` invocations.
  * [ ] Assert that elapsed time satisfies our mathematical retro timing bounds ($\approx 166.7$ milliseconds).

* [ ] **Milestone 3: Fulfill `test00.c` Multi-Lib Golden Fixtures**
  * [ ] Add a Catch2 test `SECTION` executing the full `test00.c` diagnostic flow (Blue screen clear `0xFF0000`) across both dynamic target exports (`libglide2x.so` and `libglide3x.so`) in parallel.

* [ ] **Milestone 4: Diagnostic Validation & Phase 2 Sign-Off**
  * [ ] Recompile toolchain (`cmake --build . --parallel`).
  * [ ] Directly execute test validation suite (`./build/bin/GlideWrapperTests`) and assert 100% test pass rates.
  * [ ] Formally update our master project roadmap (`docs/glide_progress_tracker.md`) to mark Task 2.3 and Phase 2 as 100% finished (`[x]`).

---

## 3. Sign-Off Criteria
* All Catch2 unit assertions must compile cleanly and pass without timing failures or memory leaks.
* Both frontend multilib replacement targets must prove 100% capable of driving complete presentation buffer sequences.
