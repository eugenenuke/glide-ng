---
name: vulkan-expert
description: Specialized Vulkan 1.4 backend, SPIR-V shader build, and WSI swapchain expert.
---

# Workspace Skill: Vulkan Backend Expert

Activate this skill when analyzing, modifying, or debugging the Vulkan backend inside [src/backends/vulkan/](file:///usr/local/google/home/eianiuk/gemini/3dfx/src/backends/vulkan/).

## 1. WSI Surface & Swapchain Rules

- **Native Child Surface Integration (`NativeSurfaceHost`)**:
  - Enable `VK_KHR_surface` plus `VK_KHR_xlib_surface` (when `HAVE_X11`) and `VK_KHR_wayland_surface` (when `HAVE_WAYLAND`) at instance creation.
  - Create `vk::SurfaceKHR` directly from `NativeSurfaceHost` (`vkCreateXlibSurfaceKHR` for X11 child windows, `vkCreateWaylandSurfaceKHR` for Wayland subsurfaces, `SDL_Vulkan_CreateSurface` only for standalone `hWnd == 0`).
  - **Strict Teardown Order**: `vkDeviceWaitIdle()` → destroy swapchain image views/swapchain → destroy `vk::SurfaceKHR` → call `m_surfaceHost.Detach()` **last**. Destroying the native X11 window or `wl_surface` while `vk::SurfaceKHR` exists causes driver segfaults or Wayland protocol errors.
- **Swapchain Blit, Aspect Ratio & Synchronization**:
  - In `SwapBuffers()`, wait on `imageAvailableSemaphore` at `vk::PipelineStageFlagBits::eTransfer` (NOT `eColorAttachmentOutput`) whenever the presentation command buffer writes to the swapchain image via `vkCmdClearColorImage` / `vkCmdBlitImage`.
  - Compute `PresentRect dst = NativeSurfaceHost::ComputePresentRect(guestW, guestH, swapchainW, swapchainH, preserveAspect)`.
  - Clear the swapchain image to opaque black when `dst` is letterboxed/pillarboxed, then `vkCmdBlitImage` from the offscreen color target (`guestW × guestH`) into `dst`.
  - Recreate the swapchain cleanly on `VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`, or when `NativeSurfaceHost::SyncBounds()` reports a host resize.

## 2. Shader Compilation Pipeline

- GLSL shaders live in `src/backends/vulkan/shaders/` (`color.vert`, `color.frag`).
- Root `CMakeLists.txt` compiles shaders via `glslangValidator -V` and converts SPIR-V into C++ headers inside `${CMAKE_BINARY_DIR}/generated_shaders/` using `tools/spv_to_hex.py`.
- **Never** check compiled `.spv` or `.spv.h` headers into `src/backends/vulkan/shaders/`. Always include the generated headers from `generated_shaders/`.
- Keep standard and dual-source blending fragment variants in a single `color.frag` controlled by `#ifdef USE_DUAL_SRC_BLEND`.

## 3. Pipeline State Objects (PSO), Memory & State Symmetry

- Cache PSOs keyed by active Glide render state; use dynamic viewport/scissor (`vk::DynamicState::eViewport`, `eScissor`) to avoid PSO thrashing.
- Flip viewport height (negative height viewport) and front-face winding (`vk::FrontFace::eCounterClockwise`) symmetrically for `GR_ORIGIN_LOWER_LEFT`.
- Perform LFB transfers through host-visible staging buffers with explicit `vkCmdPipelineBarrier` transitions.
- Favor Push Constants for high-frequency draw parameters (combiner constants, fog, texture scales).
- Do **not** call `SoftwareBackendBase::ClearBuffer(...)` inside `VulkanBackend::ClearBuffer(...)` once GPU clears own the framebuffer.

## 4. Validation & Debugging

- Run with Vulkan validation layers to verify barrier, layout, and WSI correctness:
  ```bash
  VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation GLIDE_WRAPPER_BACKEND=vulkan ./build/bin/glide3_cube
  ```
- Inspect `glide-ng.log` for `[Vulkan]` warnings/errors. Note that this host runs Mesa `llvmpipe` (software Vulkan), so timing-sensitive tests may run slower than on discrete GPUs.
