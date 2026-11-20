# Configuration & Environment Variables

`glide-ng` is configured through a human-readable INI file (`glide-ng.ini`), authentic 3dfx environment variables (`FX_GLIDE_*`, `SST_*`, `SSTV2_*`), and wrapper-specific environment overrides (`GLIDE_NG_*`).

A commented template is provided at [`glide-ng.ini.example`](../../glide-ng.ini.example), and all recognized environment variables are defined in a single header at [`include/glide_ng/env_vars.h`](../../include/glide_ng/env_vars.h).

---

## Configuration Precedence

Settings resolve in strict order from lowest to highest priority:

1. **Built-in defaults** (plus hardware defaults derived from the selected `card_model`)
2. **INI file** (`./glide-ng.ini`, or the file path specified by `GLIDE_NG_CONFIG`)
3. **Authentic 3dfx environment variables** (`FX_GLIDE_*`, `SST_*`, `SSTV2_*`)
4. **Wrapper environment variables** (`GLIDE_NG_*` — highest priority)

When a card model is selected, its hardware defaults (`api_version`, `fbi_memory_mb`, `tmu_count`, `tmu_memory_mb`) populate only fields not explicitly overridden by INI or environment variables. On startup, `glide-ng` records the active profile and the exact source (`default`, `ini`, `FX_GLIDE_*`/`SST_*`, `GLIDE_NG_*`) of every override in `glide-ng.log` and emits `[WARN]` for unrecognized INI keys or invalid values.

All wrapper-specific environment variables use the canonical `GLIDE_NG_*` prefix.

---

## 1. Wrapper Configuration (`GLIDE_NG_*` & `glide-ng.ini`)

### 1.1 Core & Config Loading

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CONFIG` | *(env only)* | File path | `./glide-ng.ini` | Path to an alternate INI configuration file. |
| `GLIDE_NG_BACKEND` | `[core] backend` | `vulkan` (`vk`), `opengl_es` (`gles`), `software` (`sw`) | `vulkan` | Selects the rendering backend (case-insensitive). |
| `GLIDE_NG_HEADLESS` | `[core] headless` | `0`/`1`, `true`/`false`, `on`/`off`, `yes`/`no` | `false` | Runs off-screen without creating a visible OS window (for CI, tests, and benchmarks). |

### 1.2 Emulated 3dfx Hardware Profile

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_CARD_MODEL` | `[hardware] card_model` *(or `model`)* | `voodoo1`/`sst1`/`voodoographics`, `rush`/`sst96`/`voodoorush`, `voodoo2`/`cvg`, `banshee`/`voodoobanshee`, `voodoo3`/`h3`, `voodoo4`/`v4`/`h4`/`vsa100`, `voodoo5`/`v5`/`h5` | `Voodoo2` | Selects emulated board identity, default VRAM/TMU limits, and extension gating (`PIXEXT`/`COMBINE`/`TEXFMT` require `voodoo4` or `voodoo5`). |
| `GLIDE_NG_API_VERSION` | `[hardware] api_version` | Glide 2.x: `2.11`, `2.30`, `2.40`, `2.42`, `2.43`, `2.45`, `2.46`, `2.48`, `2.50`, `2.51`, `2.53`, `2.54`, `2.56`, `2.60`, `2.61`<br>Glide 3.x: `3.00`, `3.01`, `3.02`, `3.03`, `3.04`, `3.10` | Derived from card (`2.56` / `3.03` for `Voodoo2`) | Overrides reported API version string and feature-gating tier. Ahistorical card/version pairs emit a `[WARN]` while honoring the override. |
| `GLIDE_NG_FBI_MEM_MB` | `[hardware] fbi_memory_mb` | Positive integer (MB) | Derived from card (`16` for `Voodoo2`) | Overrides reported Frame Buffer Interface (FBI) memory in MB. |
| `GLIDE_NG_TMU_COUNT` | `[hardware] tmu_count` | `1`, `2`, `3` | Derived from card (`2` for `Voodoo2`) | Overrides number of reported Texture Mapping Units (TMUs). |
| `GLIDE_NG_TMU_MEM_MB` | `[hardware] tmu_memory_mb` | Positive integer (MB) | Derived from card (`16` for `Voodoo2`) | Overrides texture memory per TMU in MB. |
| `GLIDE_NG_SLI` | `[hardware] sli` | `0`/`1`, `true`/`false` | `false` | Reports Voodoo2 SLI capability via hardware query APIs. |
| `GLIDE_NG_VENDOR` | `[hardware] vendor` | Arbitrary string | `3Dfx Interactive` | Overrides `grGetString(GR_VENDOR)`. |
| `GLIDE_NG_RENDERER` | `[hardware] renderer` | Arbitrary string | *(backend default)* | Overrides `grGetString(GR_RENDERER)`. |

See [Hardware Profiles & Glide Versions](hardware-and-versions.md) for the complete card-to-version matrix and capability table.

### 1.3 Display, Presentation & Frame Pacing

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_VSYNC` | `[display] vsync` *(and `vsync_strict_fifo`)* | `0`/`false`/`off`, `1`/`true`/`on`, `fifo`/`strict` | `true` *(Mailbox preferred on Wayland)* | Controls vertical sync. Use `fifo` or `strict` to force strict FIFO sync on Wayland. Overrides `FX_GLIDE_SWAPINTERVAL`. |
| `GLIDE_NG_MAX_FPS` | `[display] max_fps` | Float `>= 0` (`0` = uncapped) | `60.0` | Software frame-rate limiter in FPS. Set `0` to uncap frame pacing. |
| `GLIDE_NG_MSAA` | `[display] msaa` *(or `msaa_samples`)* | `1`, `2`, `4`, `8`, `16` | `1` *(disabled)* | Multi-sample anti-aliasing sample count (invalid values normalize to `1`). |
| `GLIDE_NG_WINDOW_SCALE` | `[display] window_scale` | Integer `1`..`8` | `1` | Integer window scaling factor for standalone windows (`hWnd == 0`). |
| `GLIDE_NG_FULLSCREEN` | `[display] fullscreen` | `auto`, `0`/`1`, `true`/`false`, `on`/`off`, `yes`/`no` | `auto` *(Win32/Wine: `true`, Linux standalone: `false`)* | Controls borderless desktop fullscreen with aspect-preserving letterboxing/pillarboxing. Defaults to `true` on Win32/Wine (matching authentic 3dfx hardware pass-through, `nGlide`, and `dgVoodoo2`) with automatic `WndProc` clamp protection and cursor coordinate scaling; set `false` (`GLIDE_NG_FULLSCREEN=0`) to run windowed. Linux embedded DOSBox surfaces remain controlled by DOSBox's own window/fullscreen setting. |
| `GLIDE_NG_PRESENTATION_FILTER` | `[display] presentation_filter` | `nearest` (`0`), `linear` (`1`) | `nearest` (`0`) | Sampling filter used when scaling the guest framebuffer to the presentation surface. |

### 1.4 Splash & Watermark Overrides

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_NO_SPLASH` | `[glide] no_splash` | `0`/`1`, `true`/`false` | `false` | Suppresses both automatic one-shot startup splash on `grSstWinOpen()` (non-headless) and explicit `grSplash()` calls. Overrides `FX_GLIDE_NO_SPLASH`. |
| `GLIDE_NG_SHAMELESS_PLUG` | `[glide] shameless_plug` | `0`/`false` (force off), `1`/`true` (force on); INI also accepts `-1` (app-controlled) | `-1` *(app-controlled)* | Forces the 3dfx corner watermark logo on or off during `grBufferSwap`. Overrides `FX_GLIDE_SHAMELESS_PLUG`. |

### 1.5 Logging, Tracing & Performance Telemetry

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_LOG_LEVEL` | `[logging] level` | `0`/`off`, `1`/`crit`/`error`, `2`/`warn`, `3`/`info`, `4`/`debug`, `5`/`trace` | `3` (`info`) | Diagnostic verbosity. Each line carries a monotonic microsecond timestamp (`[+SSSSSS.UUUUUU]`) and frame index (`[FNNNNNN]`). |
| `GLIDE_NG_LOG_CONSOLE` | `[logging] console` | `0`/`1`, `true`/`false` | `false` | Mirrors log output to `stdout` in addition to the log file. |
| `GLIDE_NG_LOG_FILE` | `[logging] file` | File path | `glide-ng.log` | Log output file path. |
| `GLIDE_NG_LOG_CATEGORIES` | `[logging] categories` | `all`, `none`, or comma-separated subset of `general`, `api`, `lfb`, `tex`, `state`, `draw`, `wsi`, `perf` (supports `-cat` negation, e.g. `-draw`) | `all` | Filters `INFO`/`DEBUG`/`TRACE` log entries by subsystem. `CRIT`, `WARN`, and the startup summary banner always emit. |
| `GLIDE_NG_TRACE_FRAMES` | `[logging] trace_frames` | `N`, `A-B`, `A-`, `-B`, or `all` | *(unbounded)* | Restricts `TRACE` (`level=5`) output to the specified 0-indexed `grBufferSwap` frame window. |
| `GLIDE_NG_PERF_LOG` | `[logging] perf_log` | `0`/`false`/`off`, `1`/`true`/`on`/`log` (periodic `[Perf]` summary), or a file path (periodic summary **plus** per-frame CSV) | `false` | Records per-frame telemetry: FPS, frame/render/present/cap-sleep ms, `wrapper_cpu_ms` vs. `app_cpu_ms`, `draw_cpu_ms`, `tex_cpu_ms`, `lfb_cpu_ms`, `state_cpu_ms`, draw calls, state flushes, triangles, LFB reads/writes & bytes, texture uploads & bytes, and Vulkan PSO builds. |

#### Diagnostic Capture Examples

```bash
# Record per-frame performance CSV without verbose logging overhead
GLIDE_NG_PERF_LOG=/tmp/perf.csv GLIDE_NG_LOG_CONSOLE=1 ./build_release/bin/glide3_cube

# Capture API, LFB, and performance traces for frames 120..125 only
GLIDE_NG_LOG_LEVEL=trace GLIDE_NG_LOG_CATEGORIES=api,lfb,perf \
  GLIDE_NG_TRACE_FRAMES=120-125 GLIDE_NG_LOG_FILE=/tmp/trace.log \
  GLIDE_NG_PERF_LOG=/tmp/perf.csv <game_command>
```

### 1.6 On-Screen HUD Overlay

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_HUD` | `[hud] elements` *(plus `position`, `scale`, `opacity`)* | `0`/`false`/`off`, `1`/`fps`, `full`/`all`, or comma-separated tokens below | `0` *(disabled)* | DXVK-style performance overlay rendered in each backend's post-scene presentation pass with zero Glide state or guest framebuffer mutation. |

| HUD Token | Effect |
| :--- | :--- |
| `fps` *(or `1`)* | Rolling FPS counter and frame-pacing status (`60cap`, `VSync`, `Uncap`). |
| `frametimes` *(or `frametime`)* | Frame / render / present timing breakdown (ms) and a 64-frame bar graph. |
| `backend` *(or `devinfo`)* | Active backend (`VK`, `GLES`, `SW`), emulated card model, guest resolution, and MSAA factor. |
| `api` | Active frontend (`Glide2x` / `Glide3x`) and reported API version. |
| `drawcalls` *(or `draws`)* | Per-frame draw calls, mid-frame state flushes, and drawn triangles. |
| `lfb` | Per-frame LFB read/write counts and texture upload counts. |
| `full` *(or `all`)* | Enables all HUD elements (`fps,frametimes,backend,api,drawcalls,lfb`). |
| `scale=<0.5..4.0>` | Scale multiplier (default `1.0`). |
| `position=<corner>` *(or `pos=`)* | Corner placement: `tl`/`top-left` (default), `tr`/`top-right`, `bl`/`bottom-left`, `br`/`bottom-right`. |
| `opacity=<0.0..1.0>` | Backdrop opacity (default `0.78`). |

```bash
# Minimal top-left FPS and backend readout
GLIDE_NG_HUD=fps,backend ./build_release/bin/glide3_cube

# Full HUD in bottom-right corner at 1.5x scale
GLIDE_NG_HUD=full,position=br,scale=1.5 ./build_release/bin/glide3_cube
```

### 1.7 Backend-Specific Tuning

| Environment Variable | INI Key (`section.key`) | Accepted Values | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `GLIDE_NG_SW_THREADS` | `[software] threads` | Integer `>= 0` (`0` = auto) | `0` *(auto)* | Worker thread count for the Software backend (`0` uses hardware concurrency or `OMP_NUM_THREADS`). |
| `GLIDE_NG_SW_SIMD` | `[software] simd` | `avx2`, `avx512`, `scalar`, `auto` (`0`/`1` aliases accepted) | `avx2` | Selects the Software rasterizer and shared CPU kernel SIMD tier. Automatically falls back with a `[WARN]` if the host CPU or OS lacks the requested ISA. |
| `GLIDE_NG_SW_TILE_SIZE` | `[software] tile_size` | Integer `>= 16` *(rounded to power of two)* | `32` | Tile bin dimension in pixels for the Software rasterizer. |
| `GLIDE_NG_SW_AA` | `[software] aa` | `analytical`, `ssaa` | `analytical` | Software backend point/line/scene anti-aliasing mode. |
| `GLIDE_NG_VK_CACHE_PATH` | `[vulkan] cache_path` | File path | `.glide-ng-vkcache.bin` | Persistent `VkPipelineCache` file path. |
| `GLIDE_NG_GLES_VBO_SIZE` | `[opengl_es] vbo_size_mb` | Integer `>= 1` (MB) | `8` | Streaming vertex ring-buffer size in megabytes for OpenGL ES / WGL. |

---

## 2. Authentic 3dfx Environment Variables (`FX_GLIDE_*` / `SST_*` / `SSTV2_*`)

`glide-ng` implements 20 user-facing 3dfx environment variables with full `glide-ng.ini` parity:

| Variable | INI Equivalent | Accepted Values | Default | Behavior & Precedence |
| :--- | :--- | :--- | :--- | :--- |
| `FX_GLIDE_NO_SPLASH` | `[glide] no_splash` | `0`/`false`, non-zero/`true` | Unset | Suppresses automatic `grSstWinOpen()` startup splash and explicit `grSplash()` calls. Overridden by `GLIDE_NG_NO_SPLASH`. |
| `FX_GLIDE_SHAMELESS_PLUG` | `[glide] shameless_plug` | `0`/`false` (off), non-zero/`true` (on) | Unset | Forces 3dfx watermark badge on or off. Overridden by `GLIDE_NG_SHAMELESS_PLUG`. |
| `FX_GLIDE_SWAPINTERVAL` | `[glide] swap_interval` | Integer `>= 0` (`0` = VSync off, `>= 1` = VSync on) | Unset | Controls swap synchronization. Overridden by `GLIDE_NG_VSYNC`. |
| `FX_GLIDE_FBRAM` | `[hardware] fbi_memory_mb` | Positive integer (MB) | Card default | Overrides reported FBI memory. Overridden by `GLIDE_NG_FBI_MEM_MB`. |
| `FX_GLIDE_TMU_MEMSIZE` | `[hardware] tmu_memory_mb` | Positive integer (MB) | Card default | Overrides TMU texture memory budget. Overridden by `GLIDE_NG_TMU_MEM_MB`. |
| `SSTV2_NUM_TMUS` | `[hardware] tmu_count` | `1`, `2`, `3` | Card default | Overrides reported TMU count (`1` zeroes `GR_TMU1` address space). Overridden by `GLIDE_NG_TMU_COUNT`. |
| `SST_GAMMA` / `SSTV2_GAMMA` | `[glide] gamma` *(or `[display] gamma`)* | Float `0.1`..`10.0` | Unset *(app-controlled)* | Forces RAMDAC gamma LUT correction across `grSstWinOpen` and app gamma calls (`SSTV2_GAMMA` beats `SST_GAMMA`). |
| `SST_SCREENREFRESH` / `SSTV2_SCREENREFRESH` | `[glide] screen_refresh` *(or `[display] screen_refresh`)* | Positive integer (Hz) | Unset | Sets frame-rate pacing target (`SSTV2_SCREENREFRESH` beats `SST_SCREENREFRESH`). Overridden by `GLIDE_NG_MAX_FPS`. |
| `FX_GLIDE_IGNORE_REOPEN` | `[glide] ignore_reopen` | `0`/`1`, `true`/`false` | `false` | Treats redundant `grSstWinOpen` calls while a window is active as a safe no-op preserving the framebuffer and state. |
| `FX_GLIDE_NO_DITHER_SUB` | `[glide] no_dither_sub` | `0`/`1`, `true`/`false` | `false` | Disables 16-bit 2×2 / 4×4 color dithering across all backends. |
| `SSTV2_FORCE_BILINEAR` | `[glide] force_bilinear` | `0`/`1`, `true`/`false` | `false` | Forces bilinear min/mag texture filtering regardless of `grTexFilterMode`. |
| `SSTV2_FORCE_TRILINEAR` | `[glide] force_trilinear` | `0`/`1`, `true`/`false` | `false` | Forces bilinear filtering plus trilinear mipmap blending (`lodBlend = true`). |
| `SST_TEXMAP_DISABLE` | `[glide] texmap_disable` | `0`/`1`, `true`/`false` | `false` | Bypasses texture sampling so all TMU stages pass through iterated color (diagnostic switch). |
| `FX_GLIDE_BOUNDS_CHECK` | `[glide] bounds_check` | `0`/`1`, `true`/`false` | `false` | Validates submitted vertices against viewport bounds, finiteness, and `oow > 0`; logs up to 16 `[WARN] [BoundsCheck]` lines plus a session total. Zero overhead when off. |
| `FX_GLIDE_ALLOC_COLOR` | `[glide] alloc_color` | `2`, `3` | App-requested | Overrides color buffer count (double vs. triple buffering). |
| `FX_GLIDE_ALLOC_AUX` | `[glide] alloc_aux` | `0`, `1` | App-requested | Overrides aux/depth buffer allocation (`0` disables depth tests and depth writes). |
| `FX_GLIDE_SNAPSHOT` / `FX_SNAPSHOT` | `[glide] snapshot` | `0`/`off`, `1`/`true` (frame 0), `@N` or `N>=2` (frame `N`), `<path>`, or `<path>@N` | Unset | Captures the backbuffer before `grBufferSwap` on frame `N` as an uncompressed binary `P6` `.ppm` image (`glide_snapshot_NNNNNN.ppm` by default). `FX_GLIDE_SNAPSHOT` beats `FX_SNAPSHOT`. |

Low-level hardware timing, PCI FIFO, SDRAM, clock-delay, and VGA pass-through variables (`SST_GRXCLK`, `SST_FT_CLK_DEL`, `SST_MEMFIFO*`, `SST_BACKPORCH`, `SST_VGA_PASS`, etc.) target physical Voodoo silicon registers and are intentionally out of scope.

---

## 3. Example `glide-ng.ini`

```ini
[core]
backend = vulkan

[hardware]
card_model = Voodoo2
api_version = 2.56

[display]
vsync = true
max_fps = 60
msaa = 1
presentation_filter = nearest

[glide]
no_splash = false

[logging]
level = info
console = false
file = glide-ng.log
```

To verify how any combination of INI settings and authentic 3dfx environment variables behaves on the active backend, run `glide2_envtest` or `glide3_envtest` — see [`docs/catalog/tools/glide-envtest/README.md`](../catalog/tools/glide-envtest/README.md).
