---
name: vulkan-expert
description: Specialized Vulkan 1.1/1.2 backend, SPIR-V shader build, PCH isolation, and WSI swapchain expert for glide-ng.
---

# Workspace Skill: Vulkan Backend Expert

Activate this skill when analyzing, modifying, or debugging the Vulkan backend inside [src/backends/vulkan/](../../../src/backends/vulkan/).

## 1. Module Layout & Inheritance

`VulkanBackend` inherits from `GlideStateTracker` (decoupled from `SoftwareBackendBase`) and is split across focused translation units:
- `VulkanBackend.cpp` — Instance (`VK_API_VERSION_1_1` baseline; adapter probe uses `VK_API_VERSION_1_2`), device, WSI surface/swapchain lifecycle, growable descriptor pool chunks (`m_descriptorPools`), `FrontBufferFlushTimer` callback.
- `VulkanPipeline.cpp` — PSO cache keyed by Glide render state + dynamic viewport/scissor.
- `VulkanTextures.cpp` — TMU texture upload, mipmaps, palettes, format conversion.
- `VulkanLfb.cpp` — LFB staging read/write, including `GR_BUFFER_AUXBUFFER` (`buffer == 2u` / `GR_LFB_SRC_FMT_ZA16`) depth upload & readback.
- `VulkanDraw.cpp` — Batched vertex submission, push constants, trailing-edge front-buffer draw flushes.
- `VulkanHud.cpp` — On-screen HUD overlay pipeline and bitmap atlas rendering.

**PCH Rule (`GLIDE_USE_PCH`):** `src/CMakeLists.txt` precompiles `<vulkan/vulkan.hpp>` **only** for the 7 TUs that consume `VulkanBackend.h` (`core/BackendManager.cpp` + the 6 `backends/vulkan/*.cpp` files). All other TUs set `SKIP_PRECOMPILE_HEADERS ON` to avoid `<vulkan/vulkan.hpp>` header bloat and GCC SIMD flag mismatches (`-mavx2`/`-mavx512f`). On Win32 cross-builds, `VK_USE_PLATFORM_WIN32_KHR` is set as a target compile definition so the PCH stays valid.

## 2. WSI Surface & Swapchain Rules

- **Native Child Surface Integration (`NativeSurfaceHost`)**:
  - Enable `VK_KHR_surface` plus `VK_KHR_xlib_surface` (`HAVE_X11`), `VK_KHR_wayland_surface` (`HAVE_WAYLAND`), or `VK_KHR_win32_surface` (`_WIN32`).
  - Create `vk::SurfaceKHR` directly from `NativeSurfaceHost` (`vkCreateXlibSurfaceKHR` for X11 child windows, `vkCreateWaylandSurfaceKHR` for Wayland subsurfaces, `vkCreateWin32SurfaceKHR` for Win32, `SDL_Vulkan_CreateSurface` only for standalone `hWnd == 0`).
  - **`DirectColor` → `TrueColor` X11 visual swap:** Some hosts create parent windows with a `DirectColor` visual (`c_class == 5`), which produces dark/broken output on strict drivers. Query supported visuals and call `m_surfaceHost.RecreateX11ChildForVisual(...)` to ensure a supported `TrueColor` X11 child window before swapchain creation.
  - **Strict Teardown Order:** `vkDeviceWaitIdle()` → destroy swapchain image views/swapchain → destroy `vk::SurfaceKHR` → call `m_surfaceHost.Detach()` **last**. Destroying the native X11 window or `wl_surface` while `vk::SurfaceKHR` exists causes driver segfaults or Wayland protocol errors.
- **Swapchain Blit, Aspect Ratio & Synchronization**:
  - In `SwapBuffers()`, wait on `imageAvailableSemaphore` at `vk::PipelineStageFlagBits::eTransfer` (NOT `eColorAttachmentOutput`) whenever presentation uses `vkCmdClearColorImage` / `vkCmdBlitImage`.
  - Compute `PresentRect dst = NativeSurfaceHost::ComputePresentRect(guestW, guestH, swapchainW, swapchainH, preserveAspect)` and clear/letterbox/pillarbox to opaque black before presenting.
  - Recreate the swapchain cleanly on `VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`, or when `NativeSurfaceHost::SyncBounds()` reports a host resize.
- **Un-swapped Front-Buffer Writes (`FrontBufferFlushTimer`):** Arm `m_frontFlushTimer` after direct `GR_BUFFER_FRONTBUFFER` draws/LFB writes so classic menus that skip `grBufferSwap` still flush smoothly via trailing-edge present.

## 3. Shader Compilation Pipeline

- **7 GLSL shaders** live in `src/backends/vulkan/shaders/`:
  - `color.vert`, `color.frag` (standard + `#ifdef USE_DUAL_SRC_BLEND` + W-buffer variants compiled into generated headers)
  - `hud.vert`, `hud.frag`
  - `lfb_depth.frag` (`GR_BUFFER_AUXBUFFER` / `ZA16` depth write)
  - `msaa_depth_resolve.frag`
  - `present_gamma.frag` (RAMDAC gamma LUT presentation pass)
- Root `CMakeLists.txt` compiles shaders via `glslangValidator -V` and converts SPIR-V into C++ headers inside `${CMAKE_BINARY_DIR}/generated_shaders/` using `tools/spv_to_hex.py`.
- **Never** check compiled `.spv` or `.spv.h` headers into `src/backends/vulkan/shaders/`. Include the generated headers from `generated_shaders/`.

## 4. Pipeline State Objects (PSO), Pools & State Symmetry

- Cache PSOs keyed by active Glide render state; use dynamic viewport/scissor (`vk::DynamicState::eViewport`, `eScissor`) to avoid PSO thrashing.
- Allocate texture descriptor sets through the growable `m_descriptorPools` chunk array (`CreateDescriptorPool` appends a fresh chunk on exhaustion rather than failing draws).
- Flip viewport height (negative height) and front-face winding (`vk::FrontFace::eCounterClockwise`) symmetrically for `GR_ORIGIN_LOWER_LEFT`.
- Perform LFB transfers through host-visible staging buffers with explicit `vkCmdPipelineBarrier` transitions.
- Favor Push Constants for high-frequency draw parameters (combiner constants, fog, texture scales).

## 5. Validation & Debugging

- Run with Vulkan validation layers to verify barrier, layout, and WSI correctness:
  ```bash
  VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation GLIDE_NG_BACKEND=vulkan ./build/bin/glide3_cube
  ```
- Inspect `glide-ng.log` for `[Vulkan]` warnings/errors. Note that software Vulkan ICDs (such as Mesa `llvmpipe`) run noticeably slower than discrete GPUs on timing-sensitive benchmarks.
