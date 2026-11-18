# glide-ng Project History

A concise retrospective of how `glide-ng` evolved from an initial prototype to its `0.8.0-beta.1` architecture. For release-by-release changes from `0.8.0-beta.1` onward, see [CHANGELOG.md](../CHANGELOG.md).

---

## 1. Early Prototype & Three-Backend Foundation

`glide-ng` started as a clean-room C++20 exploration of translating 3dfx Glide 2.x and Glide 3.x calls onto modern graphics APIs without emulating low-level Voodoo PCI registers:

- **Decoupled Core (`GlideWrapperCore`)**: Both Glide 2.x (`libglide2x.so`) and Glide 3.x (`libglide3x.so`) were designed as thin C ABI frontends over one shared state, texture, and LFB engine (`IGraphicsBackend`).
- **Software Oracle First**: Alongside Vulkan and OpenGL ES 3.2, a pure-CPU reference software rasterizer was implemented to serve both as a ground-truth visual oracle and as an AVX2-accelerated fallback.
- **Early Lessons**: Initial monolithic backend `.cpp` files, coupled CPU framebuffer inheritance in GPU backends, and SDL window-hijacking experiments against DOSBox revealed clear maintenance and Wayland-compatibility boundaries that shaped the next major refactor.

---

## 2. The Windowing Pivot: Option A (`NativeSurfaceHost`) vs. Option B

Integrating a host Glide wrapper cleanly inside Linux DOS emulators (DOSBox-X and SVN DOSBox) under both X11/XWayland and native Wayland was the project's first major architectural fork:

- **Rejected Approach (SDL Hijacking / Overlays)**: Attempting to call `SDL_CreateWindowFrom()` or share the host's EGL/GLX context led to driver crashes, Wayland surface role violations, and input/keyboard event theft.
- **Adopted Architecture — Option A (`NativeSurfaceHost`)**: Instead of patching DOSBox ("Option B") or hijacking its context, all three backends were unified behind `NativeSurfaceHost`, which attaches a click-through native child surface directly inside unpatched host windows (`XCreateWindow` + `XShape` on a private `XOpenDisplay` connection for X11/XWayland; `wl_subsurface` for native Wayland), while keeping clean `StandaloneSDL2` and `Headless` modes for demos and CI.
- **Decision Gate Outcome**: After migrating Vulkan, OpenGL ES, and Software to `NativeSurfaceHost` — with aspect-ratio-preserving presentation, Wayland Mailbox VSync support, and modularized backend units (`GlideStateTracker` separated from `SoftwareBackendBase`, shared `GlideEntryCommon`) — unpatched DOSBox-X and SVN DOSBox worked reliably on both X11 and Wayland, closing the Option B fork as unnecessary.

---

## 3. Infrastructure & Compatibility Milestones (M1–M8)

With core windowing and backend layering stabilized, development moved to structured feature branches with Catch2 regression gates:

- **M1 — Unified `GLIDE_NG_*` Config & Phase-1 3dfx Overrides**: Replaced fragmented legacy env var prefixes with a single `GLIDE_NG_*` namespace, eliminated direct `std::getenv()` calls inside backends, and implemented initial authentic `FX_GLIDE_*` controls (`NO_SPLASH`, `SHAMELESS_PLUG`, `SWAPINTERVAL`).
- **M2 — Tiered Logging & Runtime Telemetry**: Added microsecond-timestamped tiered logging (`OFF`–`TRACE`), subsystem category filters, frame-windowed tracing (`GLIDE_NG_TRACE_FRAMES`), and zero-compile-flag per-frame CSV telemetry (`GLIDE_NG_PERF_LOG`).
- **M3 — Post-Scene On-Screen HUD**: Built a DXVK-style bitmap HUD (`GLIDE_NG_HUD`) rendered in each backend's post-scene presentation pass so guest framebuffers and Glide state remain untouched.
- **M4 — Official 3dfx SDK Test Parity**: Resolved subtle cross-backend edge cases surfaced by classic 3dfx SDK tests (`test16` shameless plug R/B channel order, `test22` multi-pass fog and `largeLod` mipmap offsets, `test25`/`test27`, and standalone event responsiveness).
- **M5 — *Carmageddon* Compatibility & GPU State Deduplication**: Bringing up *Carmageddon* under DOSBox exposed real-world 1997 game behaviors (LFB-composited menus, lazy palette swizzling, texture budget churn, and micro-batch state calls). Fixing these introduced GPU state deduplication, an LFB overlay quad fast path, and proper TMU memory budgets.
- **M6 — Portable C99 Tools & Optional DOS/32 Cross-Builds**: Added `include/glide_ng/env_vars.h` as a single C/C++ source of truth, split telemetry into `wrapper_cpu_ms` vs. `app_cpu_ms`, integrated optional Open Watcom v2 CauseWay DOS/32 builds, and shipped `glide_info` and the GPU-bound `glide_bench` suite (GlideMark).
- **M7 — Cross-Backend Profiling & Optimization**: Systematic profiling eliminated hot-path lock contention, optimized Vulkan/GLES submission and LFB overlays, and accelerated the AVX2+FMA Software rasterizer.
- **M8 — Unified `glide-ng.ini` & Phase-2 3dfx Overrides**: Migrated cleanly from JSON to human-friendly `glide-ng.ini` (`IniConfigLoader`), implemented 17 additional authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` environment variables (gamma, refresh pacing, forced bilinear/trilinear, dither disable, bounds check, PPM snapshots, buffer allocation), and added the `glide_envtest` automated visual/LFB verifier.

---

## 4. Win32/Wine, VSA-100 Silicon & AVX-512 Performance (M9–M12)

- **M9 — 32-Bit Win32 DLLs (`glide2x.dll`, `glide3x.dll`) for Wine/Proton**: Added `i686` MinGW-w64 cross-compilation producing zero-external-runtime-DLL Win32 binaries (dynamic `vulkan-1.dll`, WGL OpenGL 3.3 Core, GDI Software, native Win32 standalone and parent-embedded `HWND` hosting, and authentic `_Func@N` `__stdcall` exports) alongside initial VSA-100 Spatial AA / T-Buffer support (`Voodoo4`/`Voodoo5`).
- **M10 — Full VSA-100 Extensions & Authentic Card Matrix**: Completed the remaining VSA-100 / Glide 3.x extensions (`COMBINE`, `grAlphaBlendFunctionExt`, `TEXFMT` including FXT1/DXT1–DXT5/YUV/ARGB8888, `TEXTUREBUFFER` RTT, `TEXUMA`, `GETGAMMA`, 8-bit hardware Stencil, per-channel `grColorMaskExt`), added `VoodooBanshee`, enforced pure Glide 2.x ABI parity and bare 3-digit version reporting, and verified real-world 3dfx tech demos (*Kitty Hawk*, *Museum*, *Rocket Burger*, *Donut*, *Valley of Ra*, *Demo Room*, *Wizard's Tower*, *Surf*, and *Diamond Monster 3D Test*).
- **M11 — VSA-100 Benchmarking, Real-GPU Fast Paths & Software AVX-512**: Expanded `glide_bench` to 9 Glide 2.x + 14 Glide 3.x scenes; added runtime CPUID/OS-XSAVE SIMD dispatch (`scalar`/`avx2`/`avx512`/`auto`) with strict per-TU ISA isolation; vectorized 13 shared CPU kernels plus Table Fog, Stencil, and Stipple; implemented a native 4×4 (16-pixel) AVX-512 Software rasterizer; added hardware BC1–BC3 upload, zero-readback GPU-side RTT, and Vulkan Early-Z W-buffer shader splitting; and wired automatic MinGW Win32 builds into the primary CMake build.
- **M12 — Compatibility Catalog**: Created `docs/catalog/` with per-title guides, validated drop-in `glide-ng.ini` presets, Mesa `llvmpipe` vs. Software benchmarking recipes, and an automated Catch2 INI validation suite.

---

## 5. Pre-Beta Stabilization (M13 — `0.8.0-beta.1`)

To prepare `glide-ng` for public beta use and external contributors, M13 decoupled all tests and demos from external reference trees, removed bundled third-party binaries and obsolete scratch scripts, established single-source SemVer 2.0 versioning (`VERSION`, Win32 PE `VS_VERSION_INFO`, `scripts/bump_version.sh`), introduced a permanent technical-writing skill, and consolidated scattered notes into the current User Guide, Developer Guide, Changelog, and Roadmap.
