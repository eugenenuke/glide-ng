# glide-ng Configuration & Environment Variables Reference

This document is the authoritative reference for configuring **glide-ng** via `glide_config.json` and environment variables.

> **Namespace Policy**: All wrapper-specific variables use the canonical **`GLIDE_NG_*`** prefix. Legacy `GLIDE_WRAPPER_*` and unprefixed `GLIDE_*` names are **not** supported.

---

## Configuration Precedence

Settings are resolved in strict order from lowest to highest priority:

1. **Built-in Defaults** (`WrapperConfig` defaults + selected card-model hardware profile defaults)
2. **JSON Config File** (`./glide_config.json`, or the path specified by `GLIDE_NG_CONFIG`)
3. **Authentic 3dfx Glide Variables** (`FX_GLIDE_*` / `SST_*`)
4. **glide-ng Wrapper Overrides** (`GLIDE_NG_*` — highest priority)

When a card model is chosen (via JSON or `GLIDE_NG_CARD_MODEL`), its hardware defaults (`apiVersion`, `fbiMemoryMb`, `tmuCount`, `tmuMemoryMb`) only populate fields that were **not** explicitly overridden by JSON or `GLIDE_NG_*`. On startup, `glide-ng` logs the effective configuration and the source (`json`, `FX_GLIDE_*`, `GLIDE_NG_*`) of every active override into the log file.

### Status Legend

- **Implemented**: Parsed and wired into runtime behavior.
- **Stub**: Struct field or placeholder exists, but runtime behavior is not yet wired.
- **Planned**: Scheduled for an upcoming milestone (see [Roadmap](#3-configuration--diagnostics-roadmap)).

---

## 1. glide-ng Wrapper Configuration (`GLIDE_NG_*` & `glide_config.json`)

### 1.1 Core & Config Loading

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CONFIG` | *(N/A)* | File path | `./glide_config.json` | **Implemented** | Path to the JSON configuration file to load instead of `./glide_config.json`. |
| `GLIDE_NG_BACKEND` | `"backend"` | `vulkan` (`vk`), `opengl_es` (`gles`, `goes`), `software` (`sw`) | `vulkan` | **Implemented** | Selects the active rendering backend. Case-insensitive. |
| `GLIDE_NG_HEADLESS` | `"headless"` *(or `"forceNoWindow"`)* | `0`/`1`, `true`/`false`, `on`/`off`, `yes`/`no` | `false` | **Implemented** | Disables OS window / surface creation for headless CI, tests, and benchmarks. |

### 1.2 Emulated 3dfx Hardware Profile

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CARD_MODEL` | `"cardModel"` *(or `"model"`)* | `voodoo1`/`sst1`, `voodoo_rush`/`rush`/`sst96`, `voodoo2`/`cvg`, `voodoo3`/`h3`, `voodoo5`/`h5` | `Voodoo2` | **Implemented** | Selects the emulated 3dfx board profile and default VRAM/TMU/API limits. |
| `GLIDE_NG_API_VERSION` | `"apiVersion"` | `2.1`, `2.40` (`2.4`), `2.42`, `2.43`, `2.61`, `3.0` | Derived from card (`2.43` for Voodoo2) | **Implemented** | Overrides the reported Glide API version and feature-gating footprint. |
| `GLIDE_NG_FBI_MEM_MB` | `"fbiMemoryMb"` | Positive integer (MB) | Derived from card (`16` for Voodoo2) | **Implemented** | Overrides reported Frame Buffer Interface (FBI) memory in MB. |
| `GLIDE_NG_TMU_COUNT` | `"tmuCount"` | `1`, `2`, `3` | Derived from card (`2` for Voodoo2) | **Implemented** | Overrides the number of reported Texture Mapping Units (TMUs). |
| `GLIDE_NG_TMU_MEM_MB` | `"tmuMemoryMb"` | Positive integer (MB) | Derived from card (`16` for Voodoo2) | **Implemented** | Overrides dedicated texture memory per TMU in MB. |
| `GLIDE_NG_SLI` | `"enableSli"` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Reports Voodoo2 SLI capability via hardware query APIs. |
| `GLIDE_NG_VENDOR` | `"vendor"` | Arbitrary string | `""` *(authentic 3dfx string)* | **Implemented** | Overrides the string returned by `grGetString(GR_VENDOR)` / hardware vendor query. |
| `GLIDE_NG_RENDERER` | `"renderer"` | Arbitrary string | `""` *(backend default)* | **Implemented** | Overrides the string returned by `grGetString(GR_RENDERER)`. |

### 1.3 Display, Presentation & Frame Pacing

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_VSYNC` | `"vsync"` *(and `"vsyncStrictFifo"`)* | `0`/`false`/`off`, `1`/`true`/`on`, `fifo`/`strict` | `true` *(Mailbox preferred on Wayland)* | **Implemented** | Controls vertical synchronization. Use `fifo` / `strict` to force strict FIFO sync even on Wayland. Overrides `FX_GLIDE_SWAPINTERVAL`. |
| `GLIDE_NG_MAX_FPS` | `"maxFps"` | Float `>= 0` (`0` = uncapped) | `60.0` | **Implemented** | Software frame rate limiter in frames per second. Set `0` to disable capping. |
| `GLIDE_NG_MSAA` | `"msaaSamples"` | `1`, `2`, `4`, `8`, `16` | `1` *(disabled)* | **Implemented** | Multi-sample anti-aliasing sample count (invalid values normalize to `1`). |
| `GLIDE_NG_WINDOW_SCALE` | `"windowScale"` | Integer `1`..`8` | `1` | **Implemented** | Integer window scaling factor for standalone mode (`hWnd == 0`). |
| `GLIDE_NG_PRESENTATION_FILTER` | `"presentationFilter"` | `nearest` (`0`), `linear` (`1`) | `nearest` (`0`) | **Implemented** | Sampling filter used when blitting the emulated framebuffer to the presentation surface. |

### 1.4 Splash & Watermark Overrides

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_NO_SPLASH` | `"noSplash"` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Suppresses the 3dfx startup splash screen (`grSplash`). Overrides `FX_GLIDE_NO_SPLASH`. |
| `GLIDE_NG_SHAMELESS_PLUG` | `"shamelessPlug"` | `0`/`false` (force off), `1`/`true` (force on); JSON also accepts `-1` (app-controlled) | `-1` *(app-controlled)* | **Implemented** | Overrides the 3dfx corner watermark (`grEnable(GR_SHAMELESS_PLUG)`). Overrides `FX_GLIDE_SHAMELESS_PLUG`. |

### 1.5 Logging & Diagnostics

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_LOG_LEVEL` | `"logLevel"` | `0`/`off`, `1`/`crit`/`error`, `2`/`warn`, `3`/`info`, `4`/`debug`, `5`/`trace` | `3` (`info`) | **Implemented** | Controls diagnostic log verbosity. Every line includes a monotonic microsecond timestamp (`[+SSSSSS.UUUUUU]`) and frame index (`[FNNNNNN]`) with buffered file flushing. |
| `GLIDE_NG_LOG_CONSOLE` | `"logToConsole"` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Mirrors log output to `stdout` in addition to the log file. |
| `GLIDE_NG_LOG_FILE` | `"logFile"` | File path | `glide-ng.log` | **Implemented** | Output file path for wrapper diagnostic logs. |
| `GLIDE_NG_LOG_CATEGORIES` | `"logCategories"` | `all`, `none`, or comma-separated subset of `general`, `api`, `lfb`, `tex`, `state`, `draw`, `wsi`, `perf` (supports `-cat` negation, e.g. `-draw`) | `all` | **Implemented** | Filters `INFO`/`DEBUG`/`TRACE` log lines by subsystem category. `CRIT`, `WARN`, and the `Startup` summary banner always pass so errors and active config are never hidden. |
| `GLIDE_NG_TRACE_FRAMES` | `"traceFrames"` | Frame index or range: `N`, `A-B`, `A-`, `-B`, or `all` | *(unbounded)* | **Implemented** | Restricts high-frequency `TRACE` (`level=5`) output to the specified frame window (0-indexed by `grBufferSwap`) so deep per-primitive traces do not overwhelm disk or CPU. |
| `GLIDE_NG_PERF_LOG` | `"perfLog"` | `0`/`false`/`off` (disabled), `1`/`true`/`on`/`log` (periodic `[Perf]` summary every ~1s), or a file path (periodic `[Perf]` summary **plus** per-frame CSV trace) | `false` | **Implemented** | Enables lightweight always-compiled per-frame telemetry: FPS, frame/render/present/cap-sleep ms, draw calls, mid-frame state flushes, triangles, LFB reads/writes & bytes, texture uploads & bytes, and Vulkan PSO builds. |

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

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_HUD` | `"hud"` | `0`/`false`/`off` (disabled), `1`/`fps` (live FPS counter), `full`/`all` (all metrics), or comma-separated tokens below | `0` *(disabled)* | **Implemented** | DXVK-style on-screen performance overlay rendered in each backend's post-scene presentation pass (Vulkan, OpenGL ES, Software) using an embedded bitmap font with zero Glide state or guest framebuffer mutation. |

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

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_SW_THREADS` | `"swThreads"` | Integer `>= 0` (`0` = auto) | `0` *(auto hardware concurrency)* | **Implemented** | Software backend worker thread count (`0` uses hardware thread count or `OMP_NUM_THREADS`). |
| `GLIDE_NG_SW_SIMD` | `"swSimd"` | `0`/`1`, `true`/`false` | `false` | **Implemented** | Enables/disables AVX2 SIMD rasterization fast paths in the software backend. |
| `GLIDE_NG_SW_TILE_SIZE` | `"swTileSize"` | Integer `>= 16` | `32` | **Implemented** | Tile dimension (pixels) used by the tiled software rasterizer. |
| `GLIDE_NG_SW_AA` | `"swAa"` | `analytical`, `ssaa` | `analytical` | **Implemented** | Software backend anti-aliasing mode. |
| `GLIDE_NG_VK_CACHE_PATH` | `"vkCachePath"` | File path | `.glide-vulkan-cache.bin` | **Implemented** | File path for persisting Vulkan `VkPipelineCache` data across runs. |
| `GLIDE_NG_GLES_VBO_SIZE` | `"glesVboSize"` | Integer `>= 1` (MB) | `8` | **Implemented** | OpenGL ES streaming vertex buffer size in megabytes. |

### 1.8 Stubs / Unimplemented Fields

| Environment Variable | JSON Key | Accepted Values | Default | Status | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| *(None yet)* | *(None yet)* | `drawCallDelayUs` (`WrapperConfig` field) | `0.0` | **Stub** | Placeholder field for optional AGP/PCI bus draw-call pacing; not parsed or applied at runtime. |

### Example `glide_config.json`

```json
{
  "backend": "vulkan",
  "cardModel": "Voodoo2",
  "apiVersion": "2.43",
  "vsync": true,
  "maxFps": 60,
  "msaaSamples": 1,
  "presentationFilter": "linear",
  "noSplash": false,
  "logLevel": "info",
  "logToConsole": false,
  "logFile": "glide-ng.log"
}
```

---

## 2. Authentic 3dfx Glide Environment Variables (`FX_GLIDE_*` / `SST_*`)

Classic 3dfx Glide 2.x/3.x drivers honored a collection of `FX_GLIDE_*` and `SST_*` / `SSTV2_*` environment variables (see the historical 3dfx reference lists under `docs/Glide envvars/`). `glide-ng` implements high-value user-facing variables incrementally while keeping hardware-register-only knobs out of scope.

### 2.1 Implemented (Phase 1 — Milestone M1)

| Variable | Accepted Values | Default | Status | Wrapper Behavior & Precedence |
| :--- | :--- | :--- | :--- | :--- |
| `FX_GLIDE_NO_SPLASH` | `1`/`true` (or any non-zero value) to suppress; `0`/`false` to allow | Unset *(splash allowed)* | **Implemented** | Suppresses the 3dfx startup splash screen (`grSplash`). Overrides JSON `"noSplash"`, overridden by `GLIDE_NG_NO_SPLASH`. |
| `FX_GLIDE_SHAMELESS_PLUG` | `1`/`true`/non-zero (force on), `0`/`false` (force off) | Unset *(app-controlled)* | **Implemented** | Forces the 3dfx corner watermark logo on or off during `grBufferSwap`. Overrides JSON `"shamelessPlug"`, overridden by `GLIDE_NG_SHAMELESS_PLUG`. |
| `FX_GLIDE_SWAPINTERVAL` | Integer `>= 0` (`0` = VSync off, `>= 1` = VSync on) | Unset *(uses wrapper default `vsync=true`)* | **Implemented** | Controls swap synchronization (`0` disables VSync; `1+` enables VSync; intervals `> 1` currently map to standard VSync=1). Overrides JSON `"vsync"`, overridden by `GLIDE_NG_VSYNC`. |

### 2.2 Planned (Phase 2 — Future Milestone M7)

These authentic Glide variables are high-value for classic game compatibility, visual tweaking, and debugging, and are scheduled for Phase 2 implementation:

| Variable | Category | Status | Planned Behavior |
| :--- | :--- | :--- | :--- |
| `SST_GAMMA` / `SSTV2_GAMMA` | Display | **Planned** | Initial gamma correction factor override before application gamma calls. |
| `SST_SCREENREFRESH` / `SSTV2_SCREENREFRESH` | Display | **Planned** | Requested display refresh rate hint / pacing target. |
| `FX_GLIDE_BOUNDS_CHECK` | Debug | **Planned** | Enables strict vertex/viewport bounds diagnostics when tracing rendering bugs. |
| `FX_GLIDE_IGNORE_REOPEN` | Compatibility | **Planned** | Safely handles redundant `grSstWinOpen` calls without tearing down active contexts. |
| `FX_GLIDE_NO_DITHER_SUB` | Rendering | **Planned** | Disables 16-bit dither subtraction in the color pipeline. |
| `FX_GLIDE_LOD_DITHER` | Rendering | **Planned** | Controls texture LOD dithering behavior. |
| `SSTV2_FORCE_BILINEAR` / `SSTV2_FORCE_TRILINEAR` | Rendering | **Planned** | Forces bilinear or trilinear texture filtering regardless of application state. |
| `SST_TEXMAP_DISABLE` | Debug | **Planned** | Diagnostic switch to bypass texture sampling (useful for isolating combiner vs. texture bugs). |
| `FX_GLIDE_ALLOC_COLOR` / `FX_GLIDE_ALLOC_AUX` | Buffering | **Planned** | Overrides allocated color/aux buffer counts (`2`/`3` color, `0`/`1` aux). |
| `FX_GLIDE_SNAPSHOT` | Diagnostics | **Planned** | Framebuffer capture trigger for dumping presentation frames to disk. |

### 2.3 Hardware-Only / Out-of-Scope Variables

Low-level 3dfx PCI, SDRAM, clock-delay, DAC, and FIFO hardware register variables (`SST_GRXCLK`, `SST_FT_CLK_DEL`, `SST_PTF*_CLK_DEL`, `SST_SLIM_*`, `SST_MEMFIFO*`, `SST_PCIFIFO_LWM`, `SST_BACKPORCH`, `SST_HSYNC`, `SST_VSYNC`, `SST_DIMENSIONS`, `SST_TREX*INIT*`, `SST_FASTMEM*`, `SST_VGA_PASS`, etc.) target physical SST-1/CVG board timing registers and are **not applicable** to a modern graphics API wrapper. See `docs/Glide envvars/` for the archived original 3dfx SDK lists.

---

## 3. Configuration & Diagnostics Roadmap

The configuration namespace is designed to grow cleanly alongside upcoming diagnostics and usability milestones (tracked in [docs/plans/roadmap_m1_m6_tracker.md](plans/roadmap_m1_m6_tracker.md)):

### Milestone M2 — Tiered Logging, Category Filtering & Per-Frame Performance Tracing *(Implemented)*
- **`GLIDE_NG_LOG_LEVEL=trace` (`5`)**, **`GLIDE_NG_LOG_CATEGORIES`**, **`GLIDE_NG_TRACE_FRAMES`**, and **`GLIDE_NG_PERF_LOG`** are now implemented and documented in [Section 1.5](#15-logging--diagnostics).

### Milestone M3 — DXVK-Style On-Screen HUD (`GLIDE_NG_HUD`) *(Implemented)*
- **`GLIDE_NG_HUD`** (and JSON `"hud"`) is now implemented across Vulkan, OpenGL ES, and Software backends and documented in [Section 1.6](#16-on-screen-hud-overlay).

### Milestone M7 — Phase-2 Authentic 3dfx Environment Variables
- Expands `FX_GLIDE_*` / `SST_*` coverage with the compatibility, rendering, and diagnostic variables listed in [Section 2.2](#22-planned-phase-2--future-milestone-m7).
