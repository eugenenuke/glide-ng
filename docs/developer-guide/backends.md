# Rendering Backends

All three rendering backends inherit `GlideStateTracker` (implementing `IGraphicsBackend`), host surfaces through `NativeSurfaceHost`, and share `TextureManager`, `LfbManager`, `HudOverlay`, and `SimdKernels`.

---

## 1. Vulkan Backend (`src/backends/vulkan/`)

Primary GPU backend on Linux (`VK_KHR_xlib_surface`, `VK_KHR_wayland_surface`, headless off-screen) and Win32/Wine (`VK_KHR_win32_surface` with dynamic `vulkan-1.dll` loading via `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`).

### Module Layout
- `VulkanBackend.cpp` — Instance/device selection, swapchain lifecycle, `NativeSurfaceHost` integration, `SwapBuffers` presentation pass (aspect-ratio blit + RAMDAC Gamma LUT + HUD), and Wayland `MAILBOX` VSync selection.
- `VulkanPipeline.cpp` — Graphics pipeline caching (`VkPipelineCache` persisted to `.glide-ng-vkcache.bin`), blend/stencil/cull/depth PSO keys, and push-constant layout (256 bytes).
- `VulkanTextures.cpp` — Per-TMU `VkImage` / `VkImageView` / `VkSampler` cache, native hardware BC1/BC2/BC3 (`DXT1`–`DXT5`) upload when `textureCompressionBC` is supported (falling back cleanly to decoded RGBA8), and zero-readback GPU-side `TEXTUREBUFFER` RTT via `vkCmdCopyImage` with scratch backbuffer save/restore.
- `VulkanDraw.cpp` — State-deduplicated vertex batching, dynamic vertex ring buffer, and draw submission.
- `VulkanLfb.cpp` — Overlay quad fast path (`CanUseOverlayQuad`) for standard LFB writes, plus lazy CPU staging readback/writeback for arbitrary LFB blends and `ReadLFB`.
- `VulkanHud.cpp` — Post-scene HUD pass rendered directly into the presentation target.

### Shader Pipeline (`src/backends/vulkan/shaders/`)

Shaders compile at build time via `glslangValidator` and embed as SPIR-V C++ headers (`tools/spv_to_hex.py`):
- `color.vert` — Normalized `ModernVertex` projection and dual-TMU / fog / clip-space interpolation.
- `color.frag` — Single-source fragment shader compiled into four specialized SPIR-V variants:
  - Standard Z-buffer / depth-off (`color_frag_spv`) — **zero `gl_FragDepth` writes** (`0 DepthReplacing` instructions), preserving hardware Early-Z / Hi-Z rejection on real GPUs.
  - Dual-source blend (`-DUSE_DUAL_SRC_BLEND`) for `GR_BLEND_PREFOG_COLOR` and extended blend factors.
  - W-buffer (`-DUSE_WBUFFER_DEPTH`) writing exact 3dfx 16-bit W-buffer depth.
  - Combined Dual-Source + W-buffer (`-DUSE_DUAL_SRC_BLEND -DUSE_WBUFFER_DEPTH`).
- `present_gamma.frag` — Full-screen presentation blit applying the 256-entry RGB RAMDAC Gamma LUT.
- `hud.vert` / `hud.frag` — Post-scene bitmap HUD rendering.

---

## 2. OpenGL ES 3.2 / Win32 WGL 3.3 Core Backend (`src/backends/opengl_es/`)

Secondary GPU backend using Direct EGL 1.5 (`OpenGL ES 3.2`) on Linux (X11 child window, Wayland `wl_egl_window` subsurface, surfaceless headless `EGL_KHR_surfaceless_context`, or standalone SDL2) and native Win32 WGL (`OpenGL 3.3 Core`) on Windows/Wine.

### Module Layout
- `OpenGLESBackend.cpp` — Context/surface initialization, off-screen render FBO (supporting MSAA renderbuffers for Spatial AA), presentation FBO blit with Gamma LUT and HUD, and `SwapBuffers`.
- `OpenGLESShaders.cpp` — Runtime shader prologue adaptation between GLSL ES `300 es` (Linux EGL) and GLSL `330 core` (Win32 WGL), implementing full legacy + VSA-100 `COMBINE`, dual-TMU sampling, Table Fog, chromakeying, stipple, and dithering.
- `OpenGLESTextures.cpp` — Texture upload with native `GL_EXT_texture_compression_s3tc` (`DXT1`–`DXT5`) fast path, SIMD `SwapRB32` channel conversion for uncompressed uploads, and GPU-side Y-flipped `glBlitFramebuffer` for `TEXTUREBUFFER` RTT.
- `OpenGLESDraw.cpp` — State-deduplicated geometry batching over a configurable streaming VBO (`[opengl_es] vbo_size_mb`, default 8 MB) and LFB overlay quad path.

---

## 3. Software Backend (`src/backends/software/`)

Multi-threaded, tiled CPU rasterizer serving both as a **bit-exact ground-truth visual oracle** and as a high-throughput fallback when no GPU driver is available (or on LFB-readback-dominated titles).

### Module Layout
- `SoftwareBackendBase.cpp` — Double-buffered CPU color/depth/stencil/T-Buffer allocation (with 3 guard rows so 2×4 AVX2 and 4×4 AVX-512 tile stores remain unconditionally in-bounds), SSAA buffers, and LFB row mapping.
- `SoftwareBackend.cpp` — Lock-free worker thread pool (`GLIDE_NG_SW_THREADS`), tile grid management (`GLIDE_NG_SW_TILE_SIZE`, default 32×32), SIMD tier selection, `NativeSurfaceHost` presentation (SDL2 / X11 / Wayland / Win32 GDI `StretchDIBits`), and post-scene Gamma LUT + HUD.
- `SoftwareCombiner.cpp` — Reference scalar implementation of legacy and VSA-100 `COMBINE` equations, blending, alpha test, chromakeying, and Table Fog.
- `SoftwareRasterizer.cpp` — Triangle binning, edge setup, analytical point/line AA, and scalar reference pixel pipeline.
- `SoftwareRasterizerSimd.cpp` — 2×4 (8-pixel) AVX2+FMA tiled rasterizer compiled with `-mavx2 -mfma`.
- `SoftwareRasterizerAvx512.cpp` — 4×4 (16-pixel) AVX-512 (F/DQ/BW/VL) tiled rasterizer with native `__mmask16` predication and single-ZMM 4×4 Bayer dithering, compiled with `-mavx512f -mavx512dq -mavx512bw -mavx512vl`.
- `SoftwareRasterizerSimdImpl.inl` — Shared parameterized SIMD rasterizer template driving both AVX2 and AVX-512 paths (vectorized Table Fog with 0-LSB scalar parity across all 64 intervals, vectorized 8-bit hardware Stencil, and vectorized 32×32 Stipple).

### Key Software Invariants
1. **Bit-exact SIMD parity**: Scalar, AVX2, and AVX-512 paths must match pixel-for-pixel across color/alpha combiners, mipmap LOD selection, Table Fog, hardware Stencil, 32×32 Stipple, and 2×2 / 4×4 Bayer dithering.
2. **Thread determinism**: Multi-threaded tiled rendering produces output identical to single-threaded execution because each tile is owned exclusively by one worker per pass.
3. **32-bit Win32 stack safety**: MinGW builds pass `-mstackrealign` so AVX2/AVX-512 spills remain aligned even when called from 4-byte-aligned 1990s Win32 game stacks.
