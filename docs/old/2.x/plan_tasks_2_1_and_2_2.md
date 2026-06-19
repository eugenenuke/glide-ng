# Implementation Plan: Tasks 2.1 & 2.2 (Vulkan Surfaces)

We are advancing into **Phase 2 (Context Attachment & Presentation Buffer Swapping)**. This plan maps out the creation of our pure virtual rendering abstraction (`IGraphicsBackend`) and our first concrete Vulkan logical device, surface attachments, and headless swapchain management.

---

## 1. Architectural Scope & Specifications

### Task 2.1: Unified Backend Abstraction (`IGraphicsBackend`)
* **Core Goal**: Decouple legacy Glide frontend state managers from low-level graphics execution APIs.
* **Structural Design**:
  * Author **`src/core/IGraphicsBackend.h`** defining pure virtual lifecycle operations: `Initialize()`, `Shutdown()`, `AttachWindow()`, `DetachWindow()`, `ClearBuffer()`, and `SwapBuffers()`.
  * Ensure full thread safety and virtual destructor normalization.

### Task 2.2: Concrete Vulkan Surface Attachment (`VulkanBackend`)
* **Core Goal**: Implement `IGraphicsBackend` using modern `Vulkan-Hpp`.
* **Structural Design**:
  1. **Logical Device & Queues**: Expand `VulkanBackend::Initialize()` to fully create a `vk::Instance`, poll for graphics queues, select an optimal host adapter (`vk::PhysicalDevice`), and instantiate a logical `vk::Device`.
  2. **Window & Headless Attachments**: Implement `AttachWindow()`. If an end-user passes a valid Linux `Xlib` / `Wayland` or Windows `HWND` handle, bind a concrete `vk::SurfaceKHR` and `vk::SwapchainKHR`. 
  3. **Automated CI Headless Mode**: If `nativeWindowHandle == nullptr` *(e.g., during Catch2 validation)*, seamlessly instantiate an off-screen `vk::Image` color target and `vk::Framebuffer`. This guarantees that automated double-buffering asserts execute flawlessly in sandboxed CI/CD environments without requiring an active graphical desktop.

---

## 2. Step-by-Step Execution Milestones (TODO Roadmap)

* [ ] **Milestone 1: Author Pure Virtual Abstraction (`IGraphicsBackend`)**
  * [ ] Create `src/core/IGraphicsBackend.h` with full documentation and pure virtual lifecycle definitions.
  * [ ] Register the interface into `EmulationRegistry` or create a centralized backend provider factory singleton.

* [ ] **Milestone 2: Engineer Logical Device & Queue Selection**
  * [ ] Update `src/backends/vulkan/VulkanBackend.h` to inherit from `IGraphicsBackend`.
  * [ ] Fully implement `VulkanBackend::Initialize()` and `Shutdown()` in `src/backends/vulkan/VulkanBackend.cpp`, instantiating our `vk::Device` and active graphics `vk::Queue`.

* [ ] **Milestone 3: Build Window & Headless Surface Attachments**
  * [ ] Fulfill `AttachWindow()` and `DetachWindow()`. Fulfill off-screen target allocation (`vk::Image`, `vk::DeviceMemory`, `vk::ImageView`) when running in headless testing mode.
  * [ ] Fulfill `ClearBuffer()` and `SwapBuffers()`.

* [ ] **Milestone 4: Automated Catch2 Integration Assertions**
  * [ ] Update `tests/test_architecture.cpp` with a new `SECTION` verifying that `VulkanBackend` loads, attaches a headless window context (800x600), executes a buffer clear, swaps presentation buffers, and cleanly shuts down without throwing Vulkan exceptions or leaking memory.
  * [ ] Advance our master project roadmap (`docs/glide_progress_tracker.md`) to mark Tasks 2.1 and 2.2 as fully completed.

---

## 3. Verification & Sign-Off Criteria
* Recompilation (`cmake --build .`) must complete perfectly without C++20 virtual table or symbol link errors.
* The Catch2 verification suite must assert that `Initialize()`, `AttachWindow(nullptr, 800, 600, false)`, and `SwapBuffers()` run flawlessly under automated environments.
