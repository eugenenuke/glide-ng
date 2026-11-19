# Changelog

All notable changes to `glide-ng` are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

For the pre-beta engineering narrative covering early architecture decisions and milestones prior to `0.8.0-beta.1`, see [docs/HISTORY.md](docs/HISTORY.md).

---

## [Unreleased]

### Added
- **Glide 2.11 (`glide.dll` / `libglide.so`) Frontend (M14)**: Dedicated pre-2.3 SST-1 frontend (`src/glide211/`, clean-room `include/glide211/glide.h`, 137 logical C API functions / 274 Win32 PE `__stdcall` + undecorated exports) implementing `grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`/`BypassMode` with cached BAR pointer reuse, `guFbReadRegion`/`guFbWriteRegion`, authentic 2 MB TMU0 VRAM budget enforcement in `guTexAllocateMemory`, and headless `Glide211WrapperTests`.
- **Borderless Fullscreen Override**: Added `[display] fullscreen = true` / `GLIDE_NG_FULLSCREEN=1` supporting SDL2 standalone, Win32 standalone, and Win32 embedded host windows with aspect-preserving letterboxing/pillarboxing.
- **Automatic Startup Splash**: Trigger the procedural 3dfx startup splash automatically on `grSstWinOpen` / `grSstOpen` unless suppressed via `FX_GLIDE_NO_SPLASH=1`, `GLIDE_NG_NO_SPLASH=1`, or `[glide] no_splash = true`.
- **Commercial Game Catalog Entries & Visual Progression Galleries**: Added validated guides, drop-in presets, and WebP comparison galleries for *CyberGladiators* (1996, Win32 Glide 2.11), *Grand Theft Auto* (1997, DOS Glide 2.x), *Tomb Raider* (1996, DOS Glide 2.x), and expanded *Carmageddon* (1997) with Best Visuals presets and gallery captures.

### Changed
- **Vulkan Guest-Resolution RAMDAC Gamma LUT**: Refactored the Vulkan Gamma LUT pass to resolve at guest resolution (e.g. `640×480`) into double-buffered offscreen targets before the shared aspect-preserving `vkCmdBlitImage` presentation upscale, reducing fragment invocations by 7.5×–27× at 1440p/4K and removing swapchain framebuffer churn on resize.
- **Leaner Vulkan Pipeline & 32-Bit CPU Memory Footprint**: Removed speculative 8-pipeline pre-warming at startup, shrank 32-bit staging/vertex buffers and swapchain image counts on CPU Vulkan adapters (`llvmpipe`), and added a flushed warning when >1080p swapchains run on >16-core hosts with uncapped `LP_NUM_THREADS`.

### Fixed
- **Win32/Wine Pass-Through & WSI Hardening**: Added pre-`ShowWindow` host HWND resolution, `grSstPassthruMode` (`SHOW_VGA` yield / `SHOW_SST1` reclaim) surface management, structural `GWL_STYLE` (`WS_POPUP` ↔ `WS_CAPTION` / `WS_VISIBLE`) surface rebuild detection, hidden/iconic HWND skip guards, `CreateSwapchain()` exception/fallback recovery, and one-shot post-VGA foreground restore across Vulkan, OpenGL ES/WGL, and Software backends.
- **Commercial Game Rendering Fixes**:
  - *Tomb Raider* (1996): Fixed `guTexMemReset` palette persistence, W-buffer `oow > 1` clipping, depth bias, line depth health bar rendering, and edge mipmap LOD spikes.
  - *Carmageddon* (1997): Flushed persistent LFB write-locks before `grBufferClear` and framebuffer readbacks.
  - *Grand Theft Auto* (1997): Enabled MSAA centroid interpolation to eliminate texture-bleed seams on rotated sprites.
- **DOS Tools & Test Isolation**: Supported authentic 3dfx Watcom `GLIDE2X.OVL` lookup via DOS `PATH`, fixed `glide_bench` `ESC` exit handling, and isolated `GLIDE_NG_HEADLESS=1` in the Glide 2.x coordinate unsnapping unit test.

---

## [0.8.0-beta.1] - 2026-11-18

First structured beta release of `glide-ng`, establishing a self-contained repository, unified semantic versioning, and refactored user/developer documentation on top of full Glide 2.x/3.x and VSA-100 support.

### Highlights
- **Glide 2.x & Glide 3.x Frontends**: Drop-in replacement libraries (`libglide2x.so` / `glide2x.dll`, 152 exports; `libglide3x.so` / `glide3x.dll`, 129 exports) built on a shared C++20 core (`GlideWrapperCore`), preserving pure Glide 2.x ABI parity and bare authentic version strings.
- **Three Rendering Backends**:
  - **Vulkan** (primary): SPIR-V shader pipelines with Early-Z preservation for non-W-buffer draws, native hardware BC1–BC3 (`DXT1`–`DXT5`) texture upload, zero-readback GPU-side `TEXTUREBUFFER` render-to-texture, persistent pipeline cache, and post-scene RAMDAC gamma LUT + HUD overlay.
  - **OpenGL ES 3.2 / Win32 WGL OpenGL 3.3 Core** (secondary): EGL 1.5 on Linux (X11/Wayland/surfaceless headless) and WGL 3.3 Core on Win32/Wine, hardware S3TC/BCn texture upload, GPU-side RTT blit, and state-deduplicated vertex ring buffer.
  - **Software** (CPU reference rasterizer): Multi-threaded tiled rasterizer with runtime CPUID/OS-XSAVE SIMD dispatch (`scalar`, `avx2` default, `avx512` 4×4 16-pixel rasterizer, `auto`), 13 bit-exact shared SIMD kernels, vectorized Table Fog, hardware Stencil, and 32×32 Stipple.
- **Native Window & Emulator Hosting**: `NativeSurfaceHost` supporting headless CI, standalone SDL2 (Linux) / Win32 `HWND`, unpatched DOSBox-X / SVN DOSBox child surfaces (X11 child window and Wayland `wl_subsurface`), and Win32 parent-embedded `HWND` hosting with aspect-ratio-preserving presentation.
- **Win32 32-Bit (`i686`) DLLs & Wine/Proton Support**: Self-contained 32-bit `glide2x.dll` and `glide3x.dll` (zero external runtime DLL dependencies, canonical `_Func@N` `__stdcall` exports + undecorated aliases, embedded PE `VS_VERSION_INFO`) auto-built alongside Linux binaries when MinGW-w64 is installed.
- **Seven Authentic 3dfx Hardware Profiles & VSA-100 Extensions**: Emulates `VoodooGraphics` (SST-1), `VoodooRush` (SST-96), `Voodoo2` (CVG, default), `VoodooBanshee`, `Voodoo3`, `Voodoo4`, and `Voodoo5` with authentic capability/resolution queries, warn-only card-to-version checking, and VSA-100 `PIXEXT` (Spatial AA, T-Buffer, 8-bit Stencil, per-channel `grColorMaskExt`), `COMBINE`, `TEXFMT` (FXT1, DXT1–DXT5, YUV, RGB888, ARGB8888), `TEXTUREBUFFER`, `TEXUMA`, and `GETGAMMA`.
- **Unified Configuration, Diagnostics & HUD**: `glide-ng.ini`, 20 authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` environment variables, 31 `GLIDE_NG_*` overrides, tiered/category/frame-windowed logging, per-frame CPU/GPU CSV telemetry, and DXVK-style on-screen HUD (`GLIDE_NG_HUD`).
- **Portable Diagnostic & Benchmark Tools**: Pure C99 `glide_info`, `glide_bench` (9 Glide 2.x + 14 Glide 3.x scenes with GlideMark composite scoring), and `glide_envtest` targeting Linux, Win32/Wine, and optional Open Watcom v2 DOS/32 (`GLIDE2X.OVL`).
- **Compatibility Catalog & Self-Contained Tests**: Curated `docs/catalog/` with per-title guides and validated drop-in `glide-ng.ini` presets (commercial games, official 3dfx demos, hardware tests, tools), plus 106 self-contained Catch2 tests requiring zero external SDK or demo assets.

### Changed & Cleaned Up
- Adopted **GNU GPLv3 (`GPL-3.0-or-later`)** (`LICENSE`, `THIRD_PARTY_NOTICES.md`), replacing all legacy 3dfx-licensed ABI headers under `include/` with clean-room GPL-3.0 ABI headers and replacing embedded 3dfx splash/banner binary assets with a clean-room procedural **Chrome & Amber Shield** 3D `grSplash` animation and `180×90` `GR_SHAMELESS_PLUG` corner watermark.
- Unified single-source versioning via root `VERSION`, `tools/gen_version_header.cmake`, `scripts/bump_version.sh`, and Win32 PE `VS_VERSION_INFO`.
- Decoupled Catch2 test suites and standalone demos from external `refs/` and `ext_tests/` assets; removed deprecated test dumps, ad-hoc `scratch/` scripts, third-party `refs/` binaries/PDFs, and local-only `.gitmodules`.
- Refactored `docs/` into structured User Guide (`docs/user-guide/`), Developer Guide (`docs/developer-guide/`), Compatibility Catalog (`docs/catalog/`), historical retrospective (`docs/HISTORY.md`), and forward-looking roadmap (`docs/ROADMAP.md`).
