# glide-ng Configuration & Environment Variables Reference

This document is the authoritative reference for configuring **glide-ng** via `glide-ng.ini` and environment variables.

> **Namespace Policy**: All wrapper-specific variables use the canonical **`GLIDE_NG_*`** prefix. Legacy `GLIDE_WRAPPER_*` and unprefixed `GLIDE_*` names are **not** supported.
>
> **Single Source of Truth**: All implemented variable names and X-macro registries live in [`include/glide_ng/env_vars.h`](../include/glide_ng/env_vars.h), shared by both `IniConfigLoader` and `glide_info`. When adding a new environment variable, register it there first so tooling picks it up automatically.

---

## Configuration Precedence

Settings are resolved in strict order from lowest to highest priority:

1. **Built-in Defaults** (`WrapperConfig` defaults + selected card-model hardware profile defaults)
2. **INI Config File** (`./glide-ng.ini`, or the path specified by `GLIDE_NG_CONFIG`; see [`glide-ng.ini.example`](../glide-ng.ini.example))
3. **Authentic 3dfx Glide Variables** (`FX_GLIDE_*` / `SST_*` / `SSTV2_*`)
4. **glide-ng Wrapper Overrides** (`GLIDE_NG_*` — highest priority)

When a card model is chosen (via INI or `GLIDE_NG_CARD_MODEL`), its hardware defaults (`api_version`, `fbi_memory_mb`, `tmu_count`, `tmu_memory_mb`) only populate fields that were **not** explicitly overridden by INI or environment variables. On startup, `glide-ng` logs the effective configuration and the source (`ini`, `FX_GLIDE_*`/`SST_*`, `GLIDE_NG_*`) of every active override into `glide-ng.log`, and emits a `[WARN]` for any unrecognized INI keys.

### Status Legend

- **Implemented**: Parsed and wired into runtime behavior.
- **Stub**: Struct field or placeholder exists, but runtime behavior is not yet wired.
- **Planned**: Scheduled for an upcoming milestone (see [Roadmap](#3-configuration--diagnostics-roadmap)).

---

## 1. glide-ng Wrapper Configuration (`GLIDE_NG_*` & `glide-ng.ini`)

### 1.1 Core & Config Loading

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CONFIG` | *(N/A)* | File path | `./glide-ng.ini` | **Implemented** | Path to the INI configuration file to load instead of `./glide-ng.ini`. |
| `GLIDE_NG_BACKEND` | `[core] backend` | `vulkan` (`vk`), `opengl_es` (`gles`, `goes`), `software` (`sw`) | `vulkan` | **Implemented** | Selects the active rendering backend. Case-insensitive. |
| `GLIDE_NG_HEADLESS` | `[core] headless` | `0`/`1`, `true`/`false`, `on`/`off`, `yes`/`no` | `false` | **Implemented** | Disables OS window / surface creation for headless CI, tests, and benchmarks. |

### 1.2 Emulated 3dfx Hardware Profile

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CARD_MODEL` | `[hardware] card_model` *(or `model`)* | `voodoo1`/`sst1`, `voodoo_rush`/`rush`/`sst96`, `voodoo2`/`cvg`, `banshee`/`voodoo_banshee`, `voodoo3`/`h3`, `voodoo4`/`v4`/`h4`/`vsa100`, `voodoo5`/`v5`/`h5` | `Voodoo2` | **Implemented** | Selects the emulated 3dfx board profile, default VRAM/TMU/API limits, and extension gating (`PIXEXT` requires `voodoo4`/`voodoo5`). Unrecognized values emit a warning. |
| `GLIDE_NG_API_VERSION` | `[hardware] api_version` | `2.1`, `2.40` (`2.4`), `2.42`, `2.43`, `2.61` | Derived from card (`2.43` for Voodoo2) | **Implemented** | Overrides the reported Glide API version and feature-gating footprint. |
| `GLIDE_NG_FBI_MEM_MB` | `[hardware] fbi_memory_mb` | Positive integer (MB) | Derived from card (`16` for Voodoo2) | **Implemented** | Overrides reported Frame Buffer Interface (FBI) memory in MB. |
| `GLIDE_NG_TMU_COUNT` | `[hardware] tmu_count` | `1`, `2`, `3` | Derived from card (`2` for Voodoo2) | **Implemented** | Overrides the number of reported Texture Mapping Units (TMUs). |
| `GLIDE_NG_TMU_MEM_MB` | `[hardware] tmu_memory_mb` | Positive integer (MB) | Derived from card (`16` for Voodoo2) | **Implemented** | Overrides dedicated texture memory per TMU in MB. |
| `GLIDE_NG_SLI` | `[hardware] sli` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Reports Voodoo2 SLI capability via hardware query APIs. |
| `GLIDE_NG_VENDOR` | `[hardware] vendor` | Arbitrary string | `3Dfx Interactive` | **Implemented** | Overrides the string returned by `grGetString(GR_VENDOR)` / hardware vendor query. |
| `GLIDE_NG_RENDERER` | `[hardware] renderer` | Arbitrary string | *(backend default)* | **Implemented** | Overrides the string returned by `grGetString(GR_RENDERER)`. |

### 1.3 Display, Presentation & Frame Pacing

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_VSYNC` | `[display] vsync` *(and `vsync_strict_fifo`)* | `0`/`false`/`off`, `1`/`true`/`on`, `fifo`/`strict` | `true` *(Mailbox preferred on Wayland)* | **Implemented** | Controls vertical synchronization. Use `fifo` / `strict` to force strict FIFO sync even on Wayland. Overrides `FX_GLIDE_SWAPINTERVAL`. |
| `GLIDE_NG_MAX_FPS` | `[display] max_fps` | Float `>= 0` (`0` = uncapped) | `60.0` | **Implemented** | Software frame rate limiter in frames per second. Set `0` to disable capping. |
| `GLIDE_NG_MSAA` | `[display] msaa` *(or `msaa_samples`)* | `1`, `2`, `4`, `8`, `16` | `1` *(disabled)* | **Implemented** | Multi-sample anti-aliasing sample count (invalid values normalize to `1`). |
| `GLIDE_NG_WINDOW_SCALE` | `[display] window_scale` | Integer `1`..`8` | `1` | **Implemented** | Integer window scaling factor for standalone mode (`hWnd == 0`). |
| `GLIDE_NG_PRESENTATION_FILTER` | `[display] presentation_filter` | `nearest` (`0`), `linear` (`1`) | `nearest` (`0`) | **Implemented** | Sampling filter used when blitting the emulated framebuffer to the presentation surface. |

### 1.4 Splash & Watermark Overrides

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_NO_SPLASH` | `[glide] no_splash` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Suppresses the 3dfx startup splash screen (`grSplash`). Overrides `FX_GLIDE_NO_SPLASH`. |
| `GLIDE_NG_SHAMELESS_PLUG` | `[glide] shameless_plug` | `0`/`false` (force off), `1`/`true` (force on); INI also accepts `-1` (app-controlled) | `-1` *(app-controlled)* | **Implemented** | Overrides the 3dfx corner watermark (`grEnable(GR_SHAMELESS_PLUG)`). Overrides `FX_GLIDE_SHAMELESS_PLUG`. |

### 1.5 Logging & Diagnostics

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_LOG_LEVEL` | `[logging] level` | `0`/`off`, `1`/`crit`/`error`, `2`/`warn`, `3`/`info`, `4`/`debug`, `5`/`trace` | `3` (`info`) | **Implemented** | Controls diagnostic log verbosity. Every line includes a monotonic microsecond timestamp (`[+SSSSSS.UUUUUU]`) and frame index (`[FNNNNNN]`) with buffered file flushing. |
| `GLIDE_NG_LOG_CONSOLE` | `[logging] console` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Mirrors log output to `stdout` in addition to the log file. |
| `GLIDE_NG_LOG_FILE` | `[logging] file` | File path | `glide-ng.log` | **Implemented** | Output file path for wrapper diagnostic logs. |
| `GLIDE_NG_LOG_CATEGORIES` | `[logging] categories` | `all`, `none`, or comma-separated subset of `general`, `api`, `lfb`, `tex`, `state`, `draw`, `wsi`, `perf` (supports `-cat` negation, e.g. `-draw`) | `all` | **Implemented** | Filters `INFO`/`DEBUG`/`TRACE` log lines by subsystem category. `CRIT`, `WARN`, and the `Startup` summary banner always pass so errors and active config are never hidden. |
| `GLIDE_NG_TRACE_FRAMES` | `[logging] trace_frames` | Frame index or range: `N`, `A-B`, `A-`, `-B`, or `all` | *(unbounded)* | **Implemented** | Restricts high-frequency `TRACE` (`level=5`) output to the specified frame window (0-indexed by `grBufferSwap`) so deep per-primitive traces do not overwhelm disk or CPU. |
| `GLIDE_NG_PERF_LOG` | `[logging] perf_log` | `0`/`false`/`off` (disabled), `1`/`true`/`on`/`log` (periodic `[Perf]` summary every ~1s), or a file path (periodic `[Perf]` summary **plus** per-frame CSV trace) | `false` | **Implemented** | Enables lightweight always-compiled per-frame telemetry: FPS, frame/render/present/cap-sleep ms, **wrapper vs. app/emulator CPU breakdown** (`wrapper_cpu_ms`, `app_cpu_ms`, `draw_cpu_ms`, `tex_cpu_ms`, `lfb_cpu_ms`, `state_cpu_ms`), draw calls, mid-frame state flushes, triangles, LFB reads/writes & bytes, texture uploads & bytes, and Vulkan PSO builds. |

#### Diagnostic Tracing Recipes

```bash
# 1. Measure performance bottlenecks without flooding logs (periodic [Perf] + per-frame CSV)
GLIDE_NG_PERF_LOG=/tmp/perf.csv GLIDE_NG_LOG_CONSOLE=1 ./build/bin/glide3_cube

# 2. Capture LFB + API + Perf trace for frames 120..125 only (e.g., debugging black menus / HUD)
GLIDE_NG_LOG_LEVEL=trace GLIDE_NG_LOG_CATEGORIES=api,lfb,perf \
  GLIDE_NG_TRACE_FRAMES=120-125 GLIDE_NG_LOG_FILE=/tmp/trace.log \
  GLIDE_NG_PERF_LOG=/tmp/perf.csv <game_command>
```

### 1.6 On-Screen HUD Overlay

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_HUD` | `[hud] elements` *(plus `position`, `scale`, `opacity`)* | `0`/`false`/`off` (disabled), `1`/`fps` (live FPS counter), `full`/`all` (all metrics), or comma-separated tokens below | `0` *(disabled)* | **Implemented** | DXVK-style on-screen performance overlay rendered in each backend's post-scene presentation pass (Vulkan, OpenGL ES, Software) using an embedded bitmap font with zero Glide state or guest framebuffer mutation. |

#### Supported `GLIDE_NG_HUD` Tokens

| Token | Meaning |
| :--- | :--- |
| `fps` *(or `1`)* | Live rolling FPS counter and current frame pacing status (`60cap`, `VSync`, `Uncap`). |
| `frametimes` *(or `frametime`)* | Frame / render / present breakdown in milliseconds plus a 64-frame color-coded bar graph. |
| `backend` *(or `devinfo`)* | Active backend (`VK`, `GLES`, `SW`), emulated card model, guest resolution, and MSAA factor. |
| `api` | Active Glide frontend (`Glide2x` / `Glide3x`) and reported API version. |
| `drawcalls` *(or `draws`)* | Per-frame draw calls, mid-frame state flushes, and drawn triangles. |
| `lfb` | Per-frame LFB read/write counts and texture upload counts. |
| `full` *(or `all`)* | Shorthand enabling all HUD elements (`fps,frametimes,backend,api,drawcalls,lfb`). |
| `scale=<0.5..4.0>` | Display scale multiplier (default `1.0`). |
| `position=<corner>` *(or `pos=`)* | Corner placement: `tl`/`top-left` (default), `tr`/`top-right`, `bl`/`bottom-left`, `br`/`bottom-right`. |
| `opacity=<0.0..1.0>` | Backdrop opacity (default `0.78`). |

```bash
# Minimal FPS counter (top-left)
GLIDE_NG_HUD=fps ./build/bin/glide3_cube

# Full telemetry HUD in bottom-right corner at 1.5x scale
GLIDE_NG_HUD=full,position=br,scale=1.5 ./build/bin/glide3_cube
```

### 1.7 Backend-Specific Tuning

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_SW_THREADS` | `[software] threads` | Integer `>= 0` (`0` = auto) | `0` *(auto hardware concurrency)* | **Implemented** | Software backend worker thread count (`0` uses hardware thread count or `OMP_NUM_THREADS`). |
| `GLIDE_NG_SW_SIMD` | `[software] simd` | `avx2`, `avx512`, `scalar`, `auto` (`0`/`1`, `true`/`false` legacy aliases) | `avx2` *(CPUID + OS XSAVE guarded)* | **Implemented** | Selects the Software backend and shared CPU kernel SIMD tier. `avx2` is the default; `avx512` selects the 16-wide AVX-512 (F/DQ/BW/VL) backend on capable CPUs (e.g. AMD Zen 4/5) with automatic fallback + `[WARN]` on older CPUs. |
| `GLIDE_NG_SW_TILE_SIZE` | `[software] tile_size` | Integer `>= 16` | `32` | **Implemented** | Tile dimension (pixels) used by the tiled software rasterizer. |
| `GLIDE_NG_SW_AA` | `[software] aa` | `analytical`, `ssaa` | `analytical` | **Implemented** | Software backend anti-aliasing mode. |
| `GLIDE_NG_VK_CACHE_PATH` | `[vulkan] cache_path` | File path | `.glide-ng-vkcache.bin` | **Implemented** | File path for persisting Vulkan `VkPipelineCache` data across runs. |
| `GLIDE_NG_GLES_VBO_SIZE` | `[opengl_es] vbo_size_mb` | Integer `>= 1` (MB) | `8` | **Implemented** | OpenGL ES streaming vertex buffer size in megabytes. |

### 1.8 Stubs / Unimplemented Fields

| Environment Variable | INI Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| *(None yet)* | *(None yet)* | `drawCallDelayUs` (`WrapperConfig` field) | `0.0` | **Stub** | Placeholder field for optional AGP/PCI bus draw-call pacing; not parsed or applied at runtime. |

### Example `glide-ng.ini`

See [`glide-ng.ini.example`](../glide-ng.ini.example) for a full commented template:

```ini
[core]
backend = vulkan

[hardware]
card_model = Voodoo2
api_version = 2.43

[display]
vsync = true
max_fps = 60
msaa = 1
presentation_filter = linear

[glide]
no_splash = false

[logging]
level = info
console = false
file = glide-ng.log
```

---

## 2. Authentic 3dfx Glide Environment Variables (`FX_GLIDE_*` / `SST_*`)

Classic 3dfx Glide 2.x/3.x drivers honored a collection of `FX_GLIDE_*` and `SST_*` / `SSTV2_*` environment variables (see the historical 3dfx reference lists under `docs/Glide envvars/`). `glide-ng` implements high-value user-facing variables incrementally while keeping hardware-register-only knobs out of scope.

### 2.1 Implemented (Phase 1 — Milestone M1)

| Variable | Accepted Values | Default | Status | Wrapper Behavior & Precedence |
| :--- | :--- | :--- | :--- | :--- |
| `FX_GLIDE_NO_SPLASH` | `1`/`true` (or any non-zero value) to suppress; `0`/`false` to allow | Unset *(splash allowed)* | **Implemented** | Suppresses the 3dfx startup splash screen (`grSplash`). Overrides INI `[glide] no_splash`, overridden by `GLIDE_NG_NO_SPLASH`. |
| `FX_GLIDE_SHAMELESS_PLUG` | `1`/`true`/non-zero (force on), `0`/`false` (force off) | Unset *(app-controlled)* | **Implemented** | Forces the 3dfx corner watermark logo on or off during `grBufferSwap`. Overrides INI `[glide] shameless_plug`, overridden by `GLIDE_NG_SHAMELESS_PLUG`. |
| `FX_GLIDE_SWAPINTERVAL` | Integer `>= 0` (`0` = VSync off, `>= 1` = VSync on) | Unset *(uses wrapper default `vsync=true`)* | **Implemented** | Controls swap synchronization (`0` disables VSync; `1+` enables VSync; intervals `> 1` currently map to standard VSync=1). Overrides INI `[display] vsync`, overridden by `GLIDE_NG_VSYNC`. |

### 2.2 Implemented (Phase 2 — Milestone M8)

All Phase-2 authentic 3dfx compatibility, hardware sizing, display, rendering, and diagnostic variables are implemented across both Glide 2.x and Glide 3.x frontends with full `glide-ng.ini` parity (`Defaults < glide-ng.ini < Authentic FX_GLIDE_*/SST_* < GLIDE_NG_*`):

| Variable | INI Key (`section.key`) | Accepted Values | Default | Status | Wrapper Behavior & Precedence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `FX_GLIDE_FBRAM` | `[hardware] fbi_memory_mb` | Positive integer (MB) | Derived from card | **Implemented** | Overrides reported FBI memory in `grSstQueryHardware` and `grGet(GR_MEMORY_FB)`. Overridden by `GLIDE_NG_FBI_MEM_MB`. |
| `FX_GLIDE_TMU_MEMSIZE` | `[hardware] tmu_memory_mb` | Positive integer (MB) | Derived from card | **Implemented** | Overrides dedicated texture RAM per TMU in `grSstQueryHardware`, `grGet(GR_MEMORY_TMU)`, `grTexMaxAddress`, and `TextureManager` silicon budget. Overridden by `GLIDE_NG_TMU_MEM_MB`. |
| `SSTV2_NUM_TMUS` | `[hardware] tmu_count` | `1`, `2`, `3` | Derived from card | **Implemented** | Overrides reported TMU count (`grGet(GR_NUM_TMU)`, `grSstQueryHardware`, and `grTexMaxAddress(GR_TMU1) == 0` when set to `1`). Overridden by `GLIDE_NG_TMU_COUNT`. |
| `SST_GAMMA` / `SSTV2_GAMMA` | `[display] gamma` *(or `[glide] gamma`)* | Float `0.1`..`10.0` | Unset *(app-controlled)* | **Implemented** | Forces hardware gamma LUT correction at `grSstWinOpen` and retains the override across subsequent `grGammaCorrectionValue` / `grLoadGammaTable` calls. `SSTV2_GAMMA` takes precedence over `SST_GAMMA`. |
| `SST_SCREENREFRESH` / `SSTV2_SCREENREFRESH` | `[display] screen_refresh` *(or `[glide] screen_refresh`)* | Positive integer (Hz) | Unset *(uses `max_fps`)* | **Implemented** | Sets the display refresh pacing target (`maxFps`). `SSTV2_SCREENREFRESH` beats `SST_SCREENREFRESH`; overridden by `GLIDE_NG_MAX_FPS`. |
| `FX_GLIDE_IGNORE_REOPEN` | `[glide] ignore_reopen` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Treats redundant `grSstWinOpen` calls while a window is already open as a safe no-op returning success without tearing down or resetting the active framebuffer/state. |
| `FX_GLIDE_NO_DITHER_SUB` | `[glide] no_dither_sub` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Disables 16-bit 2×2 / 4×4 color dithering (forces effective `GR_DITHER_DISABLE`) across Vulkan, OpenGL ES, and Software backends. |
| `SSTV2_FORCE_BILINEAR` | `[glide] force_bilinear` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Forces `GR_TEXTUREFILTER_BILINEAR` min/mag filtering regardless of application `grTexFilterMode` calls. |
| `SSTV2_FORCE_TRILINEAR` | `[glide] force_trilinear` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Forces bilinear filtering **plus** trilinear mipmap blending (`lodBlend = true`, upgrading `GR_MIPMAP_DISABLE` to `GR_MIPMAP_NEAREST`). |
| `SST_TEXMAP_DISABLE` | `[glide] texmap_disable` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Diagnostic switch that bypasses texture binding/sampling (all TMU stages pass through iterated color so combiner vs. texture bugs can be isolated). |
| `FX_GLIDE_BOUNDS_CHECK` | `[glide] bounds_check` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Validates every submitted vertex against screen bounds (`[-1..W+1] x [-1..H+1]`, finite coords, `oow > 0`) with zero overhead when off; emits `[WARN] [BoundsCheck]` diagnostics (capped at 16 per session + total count summary on window close/shutdown). |
| `FX_GLIDE_ALLOC_COLOR` | `[glide] alloc_color` | `2`, `3` | App-requested (`-1`) | **Implemented** | Overrides the effective color buffer count (`2` = double, `3` = triple) logged at `grSstWinOpen`. |
| `FX_GLIDE_ALLOC_AUX` | `[glide] alloc_aux` | `0`, `1` | App-requested (`-1`) | **Implemented** | Overrides auxiliary depth/alpha buffer allocation. Setting `0` disables hardware depth testing and depth writes (`depthMode=0, depthMask=false`) regardless of `grDepthBufferMode`/`grDepthMask` calls. |
| `FX_GLIDE_SNAPSHOT` / `FX_SNAPSHOT` | `[glide] snapshot` | `0`/`off`, `1`/`true` (frame 0), `@N` or `N>=2` (frame `N`), `<path>` (frame 0), or `<path>@N` (frame `N`) | Unset *(disabled)* | **Implemented** | Captures the backbuffer on frame `N` right before `grBufferSwap` and writes an uncompressed binary `P6` `.ppm` image (`glide_snapshot_NNNNNN.ppm` by default) with accurate RGB channel order under both `ARGB` and `ABGR`. `FX_GLIDE_SNAPSHOT` takes precedence over `FX_SNAPSHOT`. |

### 2.3 Verifying Environment Variables with `glide2_envtest` / `glide3_envtest`

`glide-ng` ships a permanent pure-C99 verification tool — **`glide2_envtest`**, **`glide3_envtest`**, and DOS/32 **`build/dos/glide_envtest.exe`** — built with 100% authentic Glide API calls. It draws a 4-quadrant visual test card (TL: 16×16 mipmapped B/W seam + minified mip strip; TR: smooth gradient + flat 4×4 dither probe; BL: overlapping W-buffered depth quads + 5-step gray gamma wedge; BR: reopen signature panel) and runs LFB RGB565 readbacks + hardware queries in `--verify` mode:

```bash
# Baseline (all defaults — point-sampled, mipmap off, 4x4 dither on, depth active, reopen resets)
GLIDE_NG_HEADLESS=1 ./build/bin/glide2_envtest --verify --test-reopen

# Interactive visual inspection window (press Q or ESC to quit; on Wayland RDP add SDL_VIDEODRIVER=x11)
SDL_VIDEODRIVER=x11 ./build/bin/glide2_envtest

# 1. Hardware sizing overrides (FBI=4 MB, TMU=8 MB, 1 TMU -> TMU1=0x0)
GLIDE_NG_HEADLESS=1 FX_GLIDE_FBRAM=4 FX_GLIDE_TMU_MEMSIZE=8 SSTV2_NUM_TMUS=1 \
  ./build/bin/glide2_envtest --verify

# 2. Force bilinear filtering across the point-sampled B/W seam
GLIDE_NG_HEADLESS=1 SSTV2_FORCE_BILINEAR=1 ./build/bin/glide2_envtest --verify

# 3. Force trilinear mipmap blending (minified strip switches from Mip0 B/W to Mip1/2 green-cyan)
GLIDE_NG_HEADLESS=1 SSTV2_FORCE_TRILINEAR=1 ./build/bin/glide2_envtest --verify

# 4. Bypass texture mapping (TL left half turns pure magenta)
GLIDE_NG_HEADLESS=1 SST_TEXMAP_DISABLE=1 ./build/bin/glide2_envtest --verify

# 5. Disable 16-bit 4x4 dithering (TR flat swatch R5 spread drops to 0)
GLIDE_NG_HEADLESS=1 FX_GLIDE_NO_DITHER_SUB=1 ./build/bin/glide2_envtest --verify

# 6. Disable auxiliary depth buffer (BL far blue quad overwrites near red quad)
GLIDE_NG_HEADLESS=1 FX_GLIDE_ALLOC_AUX=0 ./build/bin/glide2_envtest --verify

# 7. Ignore redundant grSstWinOpen (BR orange signature is preserved across second WinOpen)
GLIDE_NG_HEADLESS=1 FX_GLIDE_IGNORE_REOPEN=1 \
  ./build/bin/glide2_envtest --verify --test-reopen

# 8. Out-of-bounds vertex diagnostic logging
GLIDE_NG_HEADLESS=1 FX_GLIDE_BOUNDS_CHECK=1 GLIDE_NG_LOG_CONSOLE=1 \
  ./build/bin/glide2_envtest --verify --test-bounds

# 9. Framebuffer PPM snapshot capture on frame 0
GLIDE_NG_HEADLESS=1 FX_GLIDE_SNAPSHOT=/tmp/envtest_f0.ppm@0 \
  ./build/bin/glide2_envtest --verify --frames 1

# 10. Visual gamma override (brightens the BL 5-step gray wedge despite app gamma=1.0)
SDL_VIDEODRIVER=x11 SSTV2_GAMMA=2.2 ./build/bin/glide2_envtest

# 11. Refresh-rate pacing target (30 Hz across 15 frames -> ~30.0 FPS)
GLIDE_NG_HEADLESS=1 GLIDE_NG_VSYNC=0 SSTV2_SCREENREFRESH=30 \
  ./build/bin/glide2_envtest --verify --frames 15

# 12. Splash & Shameless Plug toggles (run interactively to see splash / corner badge)
SDL_VIDEODRIVER=x11 FX_GLIDE_NO_SPLASH=1 FX_GLIDE_SHAMELESS_PLUG=1 \
  ./build/bin/glide2_envtest --splash

# Replace glide2_envtest with glide3_envtest (and GLIDE_NG_BACKEND=vulkan|opengl_es|software)
# to verify identical behavior across Glide 3.x and all three rendering backends.
```

### 2.4 Hardware-Only / Out-of-Scope Variables

Low-level 3dfx PCI, SDRAM, clock-delay, DAC, and FIFO hardware register variables (`SST_GRXCLK`, `SST_FT_CLK_DEL`, `SST_PTF*_CLK_DEL`, `SST_SLIM_*`, `SST_MEMFIFO*`, `SST_PCIFIFO_LWM`, `SST_BACKPORCH`, `SST_HSYNC`, `SST_VSYNC`, `SST_DIMENSIONS`, `SST_TREX*INIT*`, `SST_FASTMEM*`, `SST_VGA_PASS`, `FX_GLIDE_LOD_DITHER`, etc.) target physical SST-1/CVG board timing or silicon-specific dithering registers and are **not applicable** to a modern graphics API wrapper. See `docs/Glide envvars/` for the archived original 3dfx SDK lists.

---

## 3. Configuration & Diagnostics Roadmap

The configuration namespace is designed to grow cleanly alongside upcoming milestones (tracked in [docs/plans/roadmap_m1_m6_tracker.md](plans/roadmap_m1_m6_tracker.md)):

### Milestone M2 — Tiered Logging, Category Filtering & Per-Frame Performance Tracing *(Implemented)*
- **`GLIDE_NG_LOG_LEVEL=trace` (`5`)**, **`GLIDE_NG_LOG_CATEGORIES`**, **`GLIDE_NG_TRACE_FRAMES`**, and **`GLIDE_NG_PERF_LOG`** are implemented and documented in [Section 1.5](#15-logging--diagnostics).

### Milestone M3 — DXVK-Style On-Screen HUD (`GLIDE_NG_HUD`) *(Implemented)*
- **`GLIDE_NG_HUD`** (and INI `[hud]`) is implemented across Vulkan, OpenGL ES, and Software backends and documented in [Section 1.6](#16-on-screen-hud-overlay).

### Milestone M8 — Unified `glide-ng.ini` & Phase-2 Authentic 3dfx Environment Variables *(Implemented)*
- Clean break from `glide_config.json` to unified `glide-ng.ini` (`IniConfigLoader`, tracked `glide-ng.ini.example`), unified `.glide-ng-vkcache.bin` and `glide-ng.log` naming, all 17 Phase-2 authentic 3dfx variables documented in [Section 2.2](#22-implemented-phase-2--milestone-m8), and the permanent `glide2_envtest` / `glide3_envtest` / `glide_envtest.exe` verifier tool.
