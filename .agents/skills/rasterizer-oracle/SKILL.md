---
name: rasterizer-oracle
description: Software reference rasterizer, winding math, LFB coordinate mapping, and headless oracle guidelines.
---

# Workspace Skill: Software Backend & Rasterizer Oracle

Activate this skill when analyzing, modifying, or debugging the CPU reference rasterizer inside [src/backends/software/](../../../src/backends/software/) or [src/backends/SoftwareBackendBase.h](../../../src/backends/SoftwareBackendBase.h).

## 1. Ground-Truth Oracle & Math

- **Authoritative Reference**: The Software backend must remain 100% compliant with 3dfx Voodoo 1/2 specifications so Vulkan and OpenGL ES outputs can always be validated against it.
- **Winding & Backface Culling**:
  - Evaluate backface culling on the **logical, pre-flipped** coordinates supplied by the application before display origin Y-flipping:
    $$\text{Area} = (x_B - x_A)(y_C - y_A) - (y_B - y_A)(x_C - x_A)$$
  - Discard or accept based on the sign matching active `grCullMode`.

## 2. Linear Frame Buffer (LFB) Coordinate Mapping

- When `sstOrigin == GR_ORIGIN_UPPER_LEFT`, rows map 1-to-1 in memory.
- When `sstOrigin == GR_ORIGIN_LOWER_LEFT`, flip rows vertically:
  $$\text{PhysicalRow} = \text{ScreenHeight} - 1 - \text{LogicalRow}$$
- Symmetrically flip on both LFB lock/read and unlock/write so readbacks remain spatially consistent.

## 3. Headless Execution, Presentation & Base-Class Coupling

- **Headless Mode (`forceNoWindow`)**:
  - `SoftwareBackend::AttachWindow()` MUST respect `m_config.forceNoWindow` (`GLIDE_FORCE_NO_WINDOW=1` / `GLIDE_HEADLESS=1`) and skip all OS window/SDL/EGL initialization. Never call `SDL_PumpEvents()` when headless.
- **Presentation (`NativeSurfaceHost`)**:
  - Do not hijack DOSBox's `SDL_Window*` / `SDL_Renderer*`. Present `m_resolvedBuffer` through `NativeSurfaceHost` (`XPutImage`/`XShmPutImage` on X11 child windows, `wl_shm` on Wayland subsurfaces, SDL2 only in standalone `hWnd == 0` mode) with aspect-ratio letterboxing.
  - Always track resource ownership flags (`m_sdlWindowOwned`, `m_sdlRendererOwned`) cleanly on standalone paths.
- **`SoftwareBackendBase` Coupling Warning**:
  - Until `T3.1` extracts `GlideStateTracker`, `VulkanBackend` and `OpenGLESBackend` still inherit from `SoftwareBackendBase`. Any edit to `SoftwareBackendBase.{h,cpp}` affects all three backends — rebuild and test all three when touching it.

## 4. Performance & Threading

- Keep tiled rasterization (`ThreadPool`, SIMD `SimdVec.h` AVX2 loops, OpenMP) free of per-pixel heap allocations and data races.
- Validate SIMD/rasterizer changes with `tests/test_simd_vec.cpp`, `./build/bin/sw_validate`, and `./build/bin/sw_benchmark`.
