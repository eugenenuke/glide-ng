# Architecture Overview

`glide-ng` is structured as a three-layer C++20 engine that cleanly separates legacy 3dfx C ABI surfaces from shared Glide state/resource management and pluggable rendering backends.

---

## 1. Layering Diagram

```text
  +-------------------------------+     +-------------------------------+
  |   Glide 2.x Frontend          |     |   Glide 3.x Frontend          |
  |   libglide2x.so / glide2x.dll |     |   libglide3x.so / glide3x.dll |
  |   src/glide2/glide2_entry.cpp |     |   src/glide3/glide3_entry.cpp |
  +---------------+---------------+     +---------------+---------------+
                  |                                     |
                  +------------------+------------------+
                                     v
  +---------------------------------------------------------------------+
  |                    Shared Core (GlideWrapperCore)                   |
  |  GlideEntryCommon · WrapperConfig · IniConfigLoader · Logger        |
  |  VertexLayout · TextureManager · LfbManager · Telemetry · HudOverlay|
  |  NativeSurfaceHost (WSI) · SimdDispatch / SimdKernels               |
  +----------------------------------+----------------------------------+
                                     v
                        IGraphicsBackend / GlideStateTracker
            +------------------------+------------------------+
            v                        v                        v
  +------------------+    +--------------------+    +--------------------+
  |  VulkanBackend   |    |  OpenGLESBackend   |    |  SoftwareBackend   |
  |  (SPIR-V / Vk)   |    |  (EGL 1.5 / WGL)   |    |  (Scalar/AVX2/512) |
  +------------------+    +--------------------+    +--------------------+
```

---

## 2. Repository Layout

| Path | Responsibility |
| :--- | :--- |
| `include/3dfx/`, `include/glide2/`, `include/glide3/` | Public 3dfx C headers (`3dfx.h`, `glide.h`, `glidesys.h`, `glideutl.h`, `sst1vid.h`, `g3ext.h`). |
| `include/glide_ng/env_vars.h` | Single C/C++ X-macro source of truth for all `GLIDE_NG_*` and authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` variables. |
| `src/core/` | Shared configuration, logging, WSI hosting, texture pool, LFB manager, vertex decoding, HUD overlay, telemetry, splash animator, and runtime SIMD kernels. |
| `src/glide2/` | Glide 2.x C ABI exports (`glide2_entry.cpp`, `glide2x.def`, `glide2x.rc.in`). |
| `src/glide3/` | Glide 3.x C ABI exports (`glide3_entry.cpp`, `glide3x.def`, `glide3x.rc.in`). |
| `src/backends/` | `GlideStateTracker`, `CpuLfbStaging`, `SoftwareBackendBase`, and the `vulkan/`, `opengl_es/`, and `software/` backend implementations. |
| `tests/` | Self-contained Catch2 integration/unit test suites (`Glide2xWrapperTests`, `Glide3xWrapperTests`) and `sw_validate`. |
| `tools/` | Pure C99 portable tools (`glide_info`, `glide_bench`, `glide_envtest`), interactive tools (`glidecube`, `glidegears`), build scripts (`gen_version_header.cmake`, `spv_to_hex.py`). |
| `demos/` | Standalone Glide 2.x and Glide 3.x showcase and validation demos. |
| `cmake/` | Cross-compilation toolchains and integrations (`MinGwWin32.cmake`, `OpenWatcomDos.cmake`, `toolchains/mingw-w64-i686.cmake`). |
| `scripts/` | Maintenance and release utilities (`bump_version.sh`). |

---

## 3. Core Subsystems

### Frontend Entry Layer (`src/glide2/`, `src/glide3/`, `src/core/GlideEntryCommon.*`)

- `glide2_entry.cpp` and `glide3_entry.cpp` expose authentic C ABI entry points (including `__stdcall` `_Func@N` decorations on Win32) and delegate shared state transitions, texture synchronization, vertex sanitization, LFB dispatch, splash/watermark rendering, and DOSBox `setConfig` handshakes to `GlideEntryCommon`.
- API-specific state (such as Glide 2 `guTex*` allocator handles and `GrVertex` layout versus Glide 3 `grVertexLayout` descriptors, `grGet`/`grGetString`/`grGetProcAddress`, and coordinate space) remains isolated inside each frontend translation unit.

### Configuration & Identity (`WrapperConfig`, `IniConfigLoader`)

- `EmulationRegistry` holds the active `WrapperConfig`, resolving settings in strict precedence order: built-in/card defaults < `glide-ng.ini` < authentic 3dfx environment variables < `GLIDE_NG_*` environment variables.
- Backends never call `std::getenv()` directly; all configuration passes through `WrapperConfig`.
- `EmulationRegistry::IsFunctionAvailable()` and `IsCardFeatureAvailable()` enforce card-model and API-version gating consistently across both frontends.

### Window System Integration (`NativeSurfaceHost`)

`NativeSurfaceHost` (`src/core/NativeSurfaceHost.h`) decouples all rendering backends from OS windowing headers and supports six active hosting modes:

| Mode | Platform | When Used |
| :--- | :--- | :--- |
| `Headless` | All | `GLIDE_NG_HEADLESS=1` — zero OS/SDL/WSI calls for CI and benchmarks. |
| `StandaloneSDL2` | Linux | Standalone demos/tools passing `hWnd == 0`. |
| `X11Child` | Linux (X11 / XWayland) | Click-through child `Window` (`XCreateWindow` + `XShape`) on a private `XOpenDisplay` connection inside unpatched DOSBox hosts. |
| `WaylandSubsurface` | Linux (Native Wayland) | Synchronized `wl_subsurface` attached inside unpatched Wayland DOSBox hosts. |
| `Win32Window` | Win32 / Wine | Standalone Win32 `HWND` window when `hWnd == 0` (no SDL2 dependency). |
| `Win32Embedded` | Win32 / Wine | Direct presentation into a game-supplied parent `HWND`. |

All modes compute aspect-ratio-preserving letterbox/pillarbox target rectangles via `NativeSurfaceHost::ComputePresentRect()` using exact 64-bit integer arithmetic.

### Vertex Pipeline (`VertexLayout`)

- Normalizes both Glide 2.x fixed `GrVertex` structs and Glide 3.x programmable `grVertexLayout` streams (including `GR_PARAM_ST0`, `GR_PARAM_ST1`, `GR_PARAM_Q0`, `GR_PARAM_Q1`, `GR_PARAM_FOG_EXT`, packed/float RGB/RGBA, and window/clip coordinate spaces) into a canonical `ModernVertex` consumed uniformly by all three backends.

### Texture & LFB Management (`TextureManager`, `LfbManager`, `CpuLfbStaging`)

- **`TextureManager`**: Models virtual per-TMU memory pools (`GR_TMU0`, `GR_TMU1`), handles 3dfx mipmap/aspect/LOD layouts, deferred P8/AP88 palette and YIQ/AYIQ NCC table decoding, VSA-100 formats (`ARGB_8888`, `RGB_888`, `YUYV`/`UYVY`/`AYUV`, `FXT1`, `DXT1`–`DXT5`), and raw BC1–BC3 preservation for hardware GPU upload.
- **`LfbManager`**: Implements `grLfbLock`/`Unlock` and `grLfbReadRegion`/`WriteRegion` across 16-bit (`565`, `555`, `1555`, `ZA16`) and 32-bit (`888`, `8888`, `565_DEPTH`, `555_DEPTH`, `1555_DEPTH`) write modes, origin flipping, and write-lock dirty tracking.
- **`GlideStateTracker` vs. `SoftwareBackendBase`**: GPU backends inherit only lightweight state tracking (`GlideStateTracker`) plus lazy `CpuLfbStaging`, keeping software rasterizer framebuffers and tile bins strictly inside `SoftwareBackendBase` / `SoftwareBackend`.

### Runtime SIMD Dispatch & Per-TU ISA Isolation (`SimdDispatch`, `SimdKernels`)

- `SimdDispatch` probes host CPU features via `CPUID` and validates OS register-save state via `XGETBV(0)` (`AVX` + `AVX-512` `ZMM`/`opmask` bits) before resolving the requested `SwSimdLevel` (`scalar`, `avx2`, `avx512`, `auto`).
- CMake compiles AVX2 (`SimdKernelsAvx2.cpp`, `SoftwareRasterizerSimd.cpp`) and AVX-512 (`SimdKernelsAvx512.cpp`, `SoftwareRasterizerAvx512.cpp`) in strictly isolated translation units with per-file compiler flags (`-mavx2 -mfma` and `-mavx512f -mavx512dq -mavx512bw -mavx512vl`), ensuring baseline object files emit zero `ymm`/`zmm` instructions and run safely on any x86/x86-64 CPU.
- Thirteen shared CPU kernels (`SwapRB32`, 2×2 SSAA resolve, RAMDAC Gamma LUT, masked/T-Buffer clears, RGB565/ARGB1555/ARGB4444/P8 conversions) run identically across Scalar, AVX2, and AVX-512 tiers.
