# glide-ng Project History

A concise retrospective of how `glide-ng` evolved from its initial June 2026 prototype to `0.8.0-beta.1`. For release-by-release changes from `0.8.0-beta.1` onward, see [CHANGELOG.md](../CHANGELOG.md).

---

## 1. Initial Prototype & GPU Bring-Up (June 16–17, 2026)

`glide-ng` began on June 16, 2026 as a clean-room C++20 exploration of translating 3dfx Glide calls directly to modern graphics APIs without emulating low-level Voodoo PCI registers:

- **Three-Backend Foundation**: Initial commits paired Vulkan and OpenGL ES 3.2 backends with a pure-CPU Software rasterizer built to act as both a bit-exact reference oracle and a fallback renderer.
- **Hybrid Bridge to GPU-Native Pipelines**: Early Vulkan and OpenGL ES rendering used a progressive CPU/GPU hybrid bridge (sharing CPU rasterization while bringing up GPU clears and presentation) before migrating clears, points, lines, triangles, Z-buffering, W-buffering, alpha blending, winding/origin parity, scissor clipping, depth bias, 64-entry logarithmic table fog, chromakeying, and Voodoo register-combiner texturing onto native GPU shaders with dynamic Vulkan graphics pipeline caching.

---

## 2. Native Swapchain, Full Glide 2.x Coverage & Glide 3.x (June 18–21, 2026)

- **Vulkan Swapchain Migration**: Replaced CPU staging-buffer presentation in Vulkan with a native swapchain and GPU-to-GPU blit pipeline.
- **Glide 2.x API Expansion**: Expanded Glide 2.x from ~68 core entry points to full API coverage — hardware queries, MSAA, polygon fan decomposition, LFB formatting, 3D splash/banner rendering, hardware gamma LUTs, and card/version feature gating.
- **Glide 3.x Frontend (`libglide3x.so`)**: Built out the second frontend over the shared `GlideWrapperCore` library with programmable `grVertexLayout`, vertex arrays, `grGet`/`grGetString`/`grGetProcAddress`, shared `.3df` texture parsing, `LfbManager`, `MathUtils`, `GR_BLEND_PREFOG_COLOR`, and `TEXCHROMA`/`TEXMIRROR`/`PALETTE6666` extensions (including manual pre-interpolation bilinear filtering to eliminate chromakey green halos).

---

## 3. Multi-Texturing, ABI Hardening & AVX2 Software Scaling (June 22–26, 2026)

- **Dual-TMU Multi-Texturing & Mipmapping**: Added `TMU0` + `TMU1` vertex decoding, shader sampling, analytical LOD mipmapping, and aligned 16-bit 2×2/4×4 dithering across all three backends.
- **ABI & Bounds Hardening**: Fixed legacy `GrState` serialization sizes and the 108-byte `GrVertexLayout` ABI layout, added strict LFB stride, contiguous draw stride, state restoration, and texture LOD/aspect bounds validation, and migrated to persistent Vulkan pipeline caching.
- **Software AVX2 Rewrite**: Replaced early scalar/auto-vectorized CPU loops with an explicit 8-wide AVX2+FMA tiled rasterizer and OpenMP multi-threaded tile scheduler.

---

## 4. The DOSBox Windowing Pivot (June 25–26 & November 15, 2026)

Running a host Glide wrapper smoothly inside Linux DOS emulators (DOSBox-X and SVN DOSBox) across X11, XWayland, and native Wayland proved to be the project's defining architectural pivot:

- **What Failed First**: Early integration experiments tried X11 blit fallbacks, hijacking the emulator's SDL window (`SDL_CreateWindowFrom`), and spawning detached click-through overlay windows. These approaches ran into Wayland protocol role violations (re-assigning a role to an existing `xdg_toplevel` surface), GLX/EGL context conflicts with DOSBox, and keyboard event theft.
- **The Two Architectural Choices**:
  1. **Unpatched Native Child-Surface Embedding** *(chosen)*: Keep DOSBox completely unpatched and attach a click-through native child surface inside the host window (`XCreateWindow` + `XShape` over a private `XOpenDisplay` connection on X11/XWayland; a synchronized `wl_subsurface` on native Wayland), unified across all three backends through `NativeSurfaceHost`.
  2. **Patching / Forking DOSBox** *(contingent fallback)*: Carry a custom patch in DOSBox's own `glide.cpp` host bridge (~50 LOC) to create and hand over a dedicated Vulkan/EGL surface directly — reserved strictly as a backup plan in case unpatched embedding hit compositor or fullscreen blockers.
- **Outcome**: After migrating Vulkan, OpenGL ES, and Software to `NativeSurfaceHost` — with aspect-ratio-preserving presentation, Wayland Mailbox VSync support, and a clean architectural split (`GlideStateTracker` decoupled from `SoftwareBackendBase`, shared `GlideEntryCommon`, and modular backend translation units) — unpatched DOSBox-X and SVN DOSBox ran cleanly on both X11 and native Wayland. Carrying a custom DOSBox patch was formally ruled unnecessary.

---

## 5. Infrastructure, Compatibility & Performance Milestones (November 15–18, 2026)

With windowing and core architecture stabilized, work proceeded through structured feature milestones gated by Catch2 regression suites:

- **M1 — Unified `GLIDE_NG_*` Config & Core 3dfx Overrides**: Standardized on a single `GLIDE_NG_*` namespace, eliminated direct `std::getenv()` calls in backends, and added initial authentic `FX_GLIDE_*` variables (`NO_SPLASH`, `SHAMELESS_PLUG`, `SWAPINTERVAL`).
- **M2 — Tiered Logging & Runtime Telemetry**: Added microsecond-timestamped tiered logging (`OFF`–`TRACE`), subsystem category filtering, frame-windowed tracing (`GLIDE_NG_TRACE_FRAMES`), and always-available per-frame CSV telemetry (`GLIDE_NG_PERF_LOG`).
- **M3 — Post-Scene On-Screen HUD**: Built a DXVK-style bitmap HUD (`GLIDE_NG_HUD`) rendered in each backend's post-scene presentation pass without mutating guest framebuffers or Glide state.
- **M4 — Official 3dfx SDK Test Parity**: Resolved remaining cross-backend edge cases surfaced by classic SDK tests (`test16` shameless plug R/B swizzle, `test22` multi-pass pre-fog and `largeLod` mipmap offsets, `test25`/`test27`, and standalone event pumping).
- **M5 — *Carmageddon* Compatibility & GPU State Deduplication**: Bringing up *Carmageddon* under DOSBox surfaced real-world 1997 engine behaviors (LFB-composited menus, lazy palette swizzling, texture budget churn, and micro-batch state calls), motivating GPU state deduplication, an LFB overlay quad fast path, and accurate TMU VRAM budgets.
- **M6 — Portable C99 Tools & Optional DOS/32 Builds**: Added `include/glide_ng/env_vars.h` as a single C/C++ registry, split telemetry into `wrapper_cpu_ms` vs. `app_cpu_ms`, integrated optional Open Watcom v2 CauseWay DOS/32 builds, and shipped `glide_info` and the GPU-bound `glide_bench` suite (GlideMark).
- **M7 — Cross-Backend Profiling & Optimization**: Reduced hot-path mutex contention, tuned Vulkan/GLES batching and LFB overlays, and further optimized the AVX2+FMA Software rasterizer.
- **M8 — Unified `glide-ng.ini` & Extended 3dfx Overrides**: Replaced JSON configuration with `glide-ng.ini` (`IniConfigLoader`), implemented 17 additional authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` variables (gamma, refresh pacing, forced bilinear/trilinear, dither disable, bounds check, PPM snapshots, buffer overrides), and added the `glide_envtest` automated verifier.
- **M9 — 32-Bit Win32 DLLs (`glide2x.dll`, `glide3x.dll`) for Wine/Proton**: Added `i686` MinGW-w64 cross-compilation producing zero-external-runtime-DLL Win32 binaries (dynamic `vulkan-1.dll`, WGL OpenGL 3.3 Core, GDI Software, native Win32 standalone/embedded `HWND` hosting, and canonical `_Func@N` `__stdcall` exports) plus initial VSA-100 Spatial AA and T-Buffer support.
- **M10 — Full VSA-100 Extensions & Authentic Card Matrix**: Completed VSA-100 / Glide 3.x extensions (`COMBINE`, `grAlphaBlendFunctionExt`, `TEXFMT` including FXT1/DXT1–DXT5/YUV/ARGB8888, `TEXTUREBUFFER` RTT, `TEXUMA`, `GETGAMMA`, 8-bit hardware Stencil, per-channel `grColorMaskExt`), added `VoodooBanshee`, enforced pure Glide 2.x ABI parity and bare 3-digit version reporting, and verified nine classic 3dfx tech demos and hardware test apps.
- **M11 — VSA-100 Benchmarking, Real-GPU Fast Paths & Software AVX-512**: Expanded `glide_bench` to 9 Glide 2.x + 14 Glide 3.x scenes; added runtime CPUID/OS-XSAVE SIMD dispatch (`scalar`/`avx2`/`avx512`/`auto`) with per-TU ISA isolation; vectorized 13 shared CPU kernels plus Table Fog, Stencil, and Stipple; built a native 4×4 (16-pixel) AVX-512 Software rasterizer; added hardware BC1–BC3 upload, zero-readback GPU RTT, and Vulkan Early-Z W-buffer shader splitting; and integrated automatic Win32 builds into the primary CMake build.
- **M12 — Compatibility Catalog**: Added `docs/catalog/` with per-title guides, validated drop-in `glide-ng.ini` presets, Mesa `llvmpipe` vs. Software benchmarking recipes, and an automated Catch2 INI validation suite.

---

## 6. Pre-Beta Stabilization (M13 — `0.8.0-beta.1`)

To prepare `glide-ng` for public beta use and external contributors, M13 decoupled all tests and demos from external reference trees, removed bundled third-party binaries and obsolete scratch scripts, established single-source SemVer 2.0 versioning (`VERSION`, Win32 PE `VS_VERSION_INFO`, `scripts/bump_version.sh`), introduced a permanent technical-writing skill, fixed out-of-tree `ctest` discovery and Software rasterizer top-left edge tie-breaking, and reorganized documentation into the current User Guide, Developer Guide, Changelog, and Roadmap.
