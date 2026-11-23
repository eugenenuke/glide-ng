---
name: rasterizer-oracle
description: Software reference rasterizer, winding math, SIMD (AVX2 / AVX-512), LFB coordinate mapping, and headless validation rules.
---

# Workspace Skill: Software Reference Rasterizer

Activate this skill when analyzing, modifying, or debugging the CPU reference rasterizer inside [src/backends/software/](../../../src/backends/software/) or [src/backends/SoftwareBackendBase.h](../../../src/backends/SoftwareBackendBase.h).

## 1. Ground-Truth Reference & Math

- **Authoritative Parity Baseline**: The Software backend serves as the ground-truth reference implementing 3dfx Voodoo specs (`VulkanBackend` and `OpenGLESBackend` parity is validated against it).
- **Winding & Backface Culling**:
  - Evaluate backface culling on the **logical, pre-flipped** coordinates supplied by the application before display origin Y-flipping:
    $$\text{Area} = (x_B - x_A)(y_C - y_A) - (y_B - y_A)(x_C - x_A)$$
  - Discard or accept based on the sign matching active `grCullMode`.
- **Non-Destructive RAMDAC Gamma Scanout**:
  - Keep raw 16-bit/32-bit framebuffer contents untouched by the gamma LUT so LFB readbacks return unmodified pixel values. Apply the gamma table non-destructively inside **`SoftwareBackend::ResolveFrontBuffer()`** during scanout/presentation.

## 2. Linear Frame Buffer (LFB) Coordinate Mapping

- When `sstOrigin == GR_ORIGIN_UPPER_LEFT`, rows map 1-to-1 in memory.
- When `sstOrigin == GR_ORIGIN_LOWER_LEFT`, flip rows vertically:
  $$\text{PhysicalRow} = \text{ScreenHeight} - 1 - \text{LogicalRow}$$
- Symmetrically flip on both LFB lock/read and unlock/write so readbacks remain spatially consistent.

## 3. Architecture & Class Hierarchy

- `GlideStateTracker` ([src/backends/GlideStateTracker.h](../../../src/backends/GlideStateTracker.h)) holds shared Glide render state for **all three backends**. `VulkanBackend` and `OpenGLESBackend` inherit directly from **`GlideStateTracker`**, **not** `SoftwareBackendBase`.
- `SoftwareBackendBase` owns CPU framebuffer/LFB backing buffers solely for `SoftwareBackend` (plus shared helpers in `CpuLfbStaging`).
- **Headless Mode (`forceNoWindow`):** `SoftwareBackend::AttachWindow()` must respect `m_config.forceNoWindow` (`GLIDE_NG_HEADLESS=1`) and skip all OS window/SDL/GDI/X11/Wayland calls. Never call `SDL_PumpEvents()` when headless.
- **Presentation (`NativeSurfaceHost`):** Present resolved pixels through `NativeSurfaceHost` (`XPutImage`/`XShmPutImage` on X11, `wl_shm` on Wayland, `StretchDIBits` on Win32, SDL2 only in standalone `hWnd == 0` mode) with aspect-ratio letterboxing. Track standalone ownership (`m_sdlWindowOwned`, `m_sdlRendererOwned`).
- **Un-swapped Front-Buffer Writes (`FrontBufferFlushTimer`):** Trailing-edge timer flushes un-swapped `GR_BUFFER_FRONTBUFFER` writes so classic menus update reliably without spinning the render loop.

## 4. SIMD Tiers, Compilation Sharding & Threading

- Tiled software rasterization runs across `ThreadPool.h` / OpenMP with three selectable SIMD tiers (`GLIDE_NG_SW_SIMD=scalar|avx2|avx512|auto`):
  - **Scalar:** `SoftwareRasterizer.cpp`
  - **AVX2 + FMA:** `SimdVec.h` + `SoftwareRasterizerSimd.cpp` + `SoftwareRasterizerSimdImpl.inl` (`-mavx2 -mfma`)
  - **AVX-512:** `SimdVec512.h` + `SoftwareRasterizerAvx512.cpp` (`-mavx512f -mavx512dq -mavx512bw -mavx512vl`, 16-pixel `4×4` kernel)
- **Parallel Compilation Sharding (`src/CMakeLists.txt`):** Heavy rasterizer templates are auto-sharded via generated wrapper files using **`GLIDE_RAST_SHARD`** (**8** scalar, **4** AVX2, **4** AVX-512 shards) to parallelize builds; these shards set `SKIP_PRECOMPILE_HEADERS ON` to avoid ISA flag mismatch with GCC PCH.
- Keep inner rasterization loops free of per-pixel heap allocations and data races.
- Validate SIMD/rasterizer edits with `Glide3xWrapperTests "[SIMD]"`, `tests/glide3/test_glide3_simd_vec.cpp`, `tests/glide3/test_glide3_simd.cpp`, and `glide3_bench`.
