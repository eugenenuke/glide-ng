# `/goal` Execution Tracker: Tasks 2.1 & 2.2 (Vulkan Surfaces)

This living task roadmap tracks our operational progress during the automated execution of **Tasks 2.1 (`IGraphicsBackend`)** and **Task 2.2 (`VulkanBackend` Logical Devices & Swapchains)**.

---

## 1. Execution Milestones

* [x] **Author Pure Virtual Abstraction (`IGraphicsBackend`)**
  * [x] Create `src/core/IGraphicsBackend.h` defining pure virtual lifecycle and presentation methods (`Initialize`, `AttachWindow`, `DetachWindow`, `ClearBuffer`, `SwapBuffers`, `Shutdown`).
  * [x] Ensure robust C++20 virtual destructor and thread-safety guidelines.

* [x] **Engineer Logical Device & Queue Management (`VulkanBackend`)**
  * [x] Modify `src/backends/vulkan/VulkanBackend.h` to inherit from `IGraphicsBackend`.
  * [x] Fulfill `VulkanBackend::Initialize()` in `src/backends/vulkan/VulkanBackend.cpp` to create a `vk::Instance`, poll for graphics queues, select an optimal `vk::PhysicalDevice`, and instantiate a logical `vk::Device`.

* [x] **Build Window & Headless CI Surface Attachments**
  * [x] Fulfill `AttachWindow()`. If `nativeWindowHandle == nullptr` *(Catch2 sandboxed mode)*, seamlessly instantiate an offscreen `vk::Image` color target and `vk::Framebuffer`.
  * [x] Fulfill `DetachWindow()`, `ClearBuffer()`, `SwapBuffers()`, and `Shutdown()`.

* [x] **Author Catch2 Multi-Lib Emulation Matrix Suite**
  * [x] Create or update `tests/test_backend.cpp` incorporating our aligned Catch2 strategy: parallel `dlopen` multi-lib dynamic runners cycling through `Catch2::GENERATE` card permutations (`SST1`, `CVG`, `H3`, `H5`).
  * [x] Assert that headless window attachment (800x600), buffer clearing, and presentation buffer swapping run flawlessly across all emulated accelerators.

* [x] **Execute Multi-Lib Compilation & Verification Sign-Off**
  * [x] Run parallel CMake toolchain build (`cmake --build . --parallel`).
  * [x] Directly execute Catch2 automated validation suite (`./build/bin/GlideWrapperTests`) and assert 100% test pass rates.
  * [x] Create final `walkthrough.md` report artifact and update our authoritative repository progress tracker (`docs/glide_progress_tracker.md`).
