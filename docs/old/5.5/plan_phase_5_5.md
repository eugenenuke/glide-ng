# Implementation Plan - Phase 5.5: SDL2 Visual Presentation Console & Reference Test Validation

This document outlines the systematic architectural plan to introduce native **SDL2 Window Presentation**, active Wayland/X11 Vulkan surface swapchains, implicit Glide initialization, and absolute compatibility with external retro reference tests (e.g., `test00.c`, `test18.c`).

---

## 1. Executive Summary & Prerequisites

Currently, our emulation engine forces a headless CI/CD offscreen buffer when applications pass `0` for `hWnd`. To provide an authentic, premium developer experience and enable direct visual validation, we will default to instantiating an interactive **SDL2 Presentation Window** when `hWnd == 0`, while reserving headless execution for automated CI runners via an explicit environment override (`GLIDE_FORCE_NO_WINDOW=1`).

**Core Prerequisites added to `README.md`**:
- **SDL2 Development Libraries**: `libsdl2-dev` (Debian/Ubuntu/Mint), `sdl2` (Arch/Manjaro), `SDL2` (Fedora/RHEL).

---

## 2. Systematic Implementation Workflow

### Stage 1: Prerequisite Documentation & Tracker Setup *(Completed)*
- [x] Update `README.md` to document SDL2 development packages and visual testing environment variables.
- [x] Update `docs/glide_progress_tracker.md` to incorporate Phase 5.5.
- [x] Initialize operational task tracker (`docs/5.5/task_phase_5_5.md`).

### Stage 2: Unified SDL2 & Vulkan Surface Integration
- [ ] **CMake Linkage**: Update `src/CMakeLists.txt` to discover and link SDL2 (`find_package(SDL2 REQUIRED)`).
- [ ] **Configuration Tokens**: In `WrapperConfig.h`, introduce `bool forceNoWindow{false};`. Map it to the `GLIDE_FORCE_NO_WINDOW` environment variable.
- [ ] **SDL2 Window Instantiation**: In `VulkanBackend::AttachWindow`, when `hWnd == 0` and `forceNoWindow == false`:
  - Initialize SDL2 Video subsystem (`SDL_Init(SDL_INIT_VIDEO)`).
  - Spawn an interactive desktop presentation console: `SDL_CreateWindow("3dfx glide-ng Presentation Console", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN)`.
- [ ] **Vulkan Swapchain Attachment**:
  - Invoke `SDL_Vulkan_CreateSurface` to construct an authentic Wayland/X11 `vk::SurfaceKHR`.
  - Build an interactive dual-buffer Vulkan presentation swapchain (`vk::SwapchainKHR`) backed by desktop image frames.
- [ ] **Interactive Event Loop Integration**: In `VulkanBackend::SwapBuffers`, present the active swapchain frame (`m_graphicsQueue.presentKHR`) and execute a non-blocking UI event pump (`SDL_PollEvent`) to ensure the display console stays perfectly crisp and responsive.

### Stage 3: External Reference Test Compatibility Singletons
- [ ] **Implicit State Initialization**: Update `glide2_entry.cpp` and `glide3_entry.cpp` to implicitly execute `grGlideInit()` if an application executes `grSstWinOpen`, `grSstQueryBoards`, or `grSstQueryHardware` without a prior init call.
- [ ] **Direct LFB Silicon Pointer Writes**: Many classic tests lock the Linear Framebuffer (`grLfbGetWritePtr`, `grLfbGetReadPtr`, `grLfbLock`) and manipulate pixels directly in host CPU loops.
  - *Implementation*: Return a pointer to our cohesive mapped staging buffer (`m_headlessPixelMap`).
  - *Synchronization*: During `grBufferSwap`, if the LFB was modified by host CPU loops, execute an automated GPU command copy (`vk::CmdCopyBufferToImage`) to push the altered staging buffer pixels onto the active presentation swapchain image!
- [ ] **Attribute Layout Harmonization**: Ensure external C reference structs (`GrVertex`, `StandardVert`) decode smoothly without reading out-of-bounds host memory.

### Stage 4: Build & Execute Legacy Reference Tests
- [ ] **Fix Reference Test Builds**: Update `tests/CMakeLists.txt` to correctly build legacy reference apps (`Glide2xTest00`, `Glide3xTest00`). Resolve legacy macro incompatibilities or argument layout gotchas (e.g., `grSstWinOpen` argument distinctions).
- [ ] **Catch2 Automated CI Regression**: Execute `./build/bin/GlideWrapperTests` with `GLIDE_FORCE_NO_WINDOW=1` confirming 100% passing success across all 197 core assertions.
- [ ] **Interactive Visual Verification**: Execute `./build/bin/Glide2xTest00` (or `test18`) on the live desktop. Confirm the immediate appearance of our premium SDL2 window presenting authentic retro 3dfx visuals!

---

## 3. Verification & Validation Matrix

| Target binary | Execution Mode | Expected Architectural Result |
| :--- | :--- | :--- |
| `GlideWrapperTests` | `GLIDE_FORCE_NO_WINDOW=1` | 100% passing score (197 assertions) in offscreen CI memory. |
| `Glide2xTest00` | Desktop Windowed (`hWnd=0`) | Spawns stunning centered SDL2 window displaying clear blue screen. |
| `./test18` (External) | Desktop Windowed (`hWnd=0`) | Perfect dynamic symbol linkage, rendering interactive visual patterns. |
