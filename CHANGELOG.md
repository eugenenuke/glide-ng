# Changelog

All notable changes to `glide-ng` are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

For the pre-beta engineering narrative covering early architecture decisions and milestones prior to `0.8.0-beta.1`, see [docs/HISTORY.md](docs/HISTORY.md).

---

## [Unreleased]

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
