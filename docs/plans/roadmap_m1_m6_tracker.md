# glide-ng Roadmap & Milestone Tracker (M1–M12+)

> **Status Legend (Strict 3-State Verification Rule)**
> - `[ ] PENDING` — Not started yet.
> - `[~] IMPLEMENTED — AWAITING USER VERIFICATION` — Code implemented and automated checks green; awaiting explicit user verification.
> - `[x] DONE (USER CONFIRMED — YYYY-MM-DD)` — Explicitly verified and confirmed by the user.
>
> **Workflow Rules**
> - **1 Milestone = 1 Feature Branch** cut from `main`, merged back to `main` after user confirmation.
> - Detailed task breakdowns are finalized just-in-time at the start of each milestone so later milestones (especially M5 Carmageddon and M6 DOS tools) stay grounded in live findings.
> - Strict compiler warnings (`-Wall -Wextra -Wpedantic -Werror`) across `src/`, `tools/`, `demos/`.
> - Never modify `refs/` or `ext_tests/`.

---

## 1. Master Milestone Dashboard

| Milestone | Branch | Summary | Complexity | Status | Merge Commit |
| :---: | :--- | :--- | :---: | :--- | :---: |
| **M1** | `feat/m1-config-envvars` | Unified `GLIDE_NG_*` config & env var overhaul (no `GLIDE_WRAPPER_*` aliases) + Phase-1 authentic `FX_GLIDE_*` vars (`NO_SPLASH`, `SHAMELESS_PLUG`, `SWAPINTERVAL`) + authoritative env var reference doc | Low–Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | Merged to `main` |
| **M2** | `feat/m2-logging-perf-trace` | Tiered logging (`OFF`..`TRACE`), category filtering, frame-windowed tracing, microsecond timestamps, buffered flushing, and runtime per-frame perf breakdown (+ CSV perf log) | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | Merged to `main` |
| **M3** | `feat/m3-hud-fps` | DXVK-style on-screen HUD (`GLIDE_NG_HUD=fps`, extensible) rendered cleanly in each backend's post-scene presentation pass (Vulkan, GLES, SW) | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | Merged to `main` |
| **M4** | `fix/m4-parked-sdk-tests` | Fix parked official SDK tests: `test16` ('P' plug R/B inversion on VK/GLES), `test22` (SW multi-pass fog + `largeLod` mip offset), `test25` & `test27` + standalone WSI event responsiveness + Catch2 regressions | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | Merged to `main` |
| **M5** | `fix/m5-carmageddon` | Carmageddon (`carma`) compatibility & performance: black menus, missing in-game UI/HUD, texture/seam/sky/Pratcam glitches, lazy palette swizzling, GPU state-dedup & batching, TMU budget & Vulkan texture pool | Med–High | `[x] DONE (USER CONFIRMED — 2026-11-16)` | Merged to `main` |
| **M6** | `feat/m6-dos-tools-bench` | Telemetry `app_cpu_ms`/`wrapper_cpu_ms` breakdown + single-source-of-truth `env_vars.h` + Glide 2/3 override fixes + optional Open Watcom v2 DOS cross-build (`vendor/watcom`) + dual Linux/DOS `glide_info` + GPU-bound `glide_bench` with GlideMark score | High | `[x] DONE (USER CONFIRMED — 2026-11-16)` | Merged to `main` |
| **M7** | `perf/m7-profiling-optimization` | Systematic performance profiling & optimization across frontend, Vulkan, OpenGL ES, LFB overlay, texture streaming, and AVX2+FMA Software rasterizer | Med–High | `[x] DONE (USER CONFIRMED — 2026-11-17)` | Merged to `main` (`beed43a`) |
| **M8** | `feat/m8-ini-config-envvars-p2` | Clean break to unified `glide-ng.ini` (`IniConfigLoader`) + Phase-2 authentic 3dfx env vars (`FX_GLIDE_FBRAM`, `FX_GLIDE_TMU_MEMSIZE`, `SSTV2_NUM_TMUS`, `SST_GAMMA`, `SST_SCREENREFRESH`, `FX_GLIDE_IGNORE_REOPEN`, `FX_GLIDE_NO_DITHER_SUB`, `SSTV2_FORCE_BILINEAR/TRILINEAR`, `SST_TEXMAP_DISABLE`, `FX_GLIDE_BOUNDS_CHECK`, `FX_GLIDE_ALLOC_COLOR/AUX`, `FX_GLIDE_SNAPSHOT`) + permanent `glide_envtest` tool | Low–Med | `[x] DONE (USER CONFIRMED — 2026-11-17)` | Merged to `main` (`04034f8`) |
| **M9** | `feat/m9-win32-dll-wine` | 32-bit Win32 (`i686` MinGW-w64) `glide2x.dll` & `glide3x.dll` for Wine/Proton (zero external runtime DLLs: dynamic `vulkan-1.dll` + WGL OpenGL 3.3 Core + GDI Software, authentic `__stdcall` + `.def` `_Func@N` exports) + VSA-100 Spatial AA/T-Buffer (`Voodoo4`/`Voodoo5` gating) + Win32 `glide_info.exe`, `glide_bench.exe`, `glide_envtest.exe` | High | `[x] DONE (USER CONFIRMED — 2026-11-18)` | Merged to `main` (`b3649d5`) |
| **M10** | `feat/m10-vsa100-extensions-card-matrix` | Remaining VSA-100/Glide 3.x extensions (`COMBINE`, `grAlphaBlendFunctionExt`, FXT1/DXT/YUV/RGB888/ARGB8888 `TEXFMT`, `TEXTUREBUFFER`, `TEXUMA`, `GETGAMMA`, full Stencil, per-channel `grColorMaskExt`, `grTexMultibase`, `grTexDetailControl`), `VoodooBanshee` profile, pure Glide 2.x ABI, bare 3-digit versions, warn-only card-to-version matrix & authentic capabilities/resolutions | High | `[x] DONE (USER CONFIRMED — 2026-11-18)` | Merged to `main` (`e2ba647`) |
| **M11** | `perf/m11-vsa100-bench-simd-avx512` | Post-M10 / VSA-100 benchmarking (`glide2_bench` & `glide3_bench` expanded scenes), real-GPU Vulkan/GLES optimizations (zero-readback GPU RTT, native BC1–BC3 DXT upload), shared SW & rasterizer SIMD overhaul (Stencil, Fog, SSAA resolve, Gamma LUT, masked Clear, LFB/Texture decode), and user-selectable Software **AVX-512** (Zen 4, default AVX2) | High | `[ ] PENDING` | — |
| **M12** | `feat/m12-shrine-torus-demos` | Standalone flagship 3dfx demos: `glide_shrine` (*Voodoo Temple*) and `glide_torus` (*3dfx Reflective Hall*) targeting Linux, DOS/32, and Win32 | Med | `[ ] PENDING` | — |
| **Backlog** | — | Clean-room open-source Watcom-built `glide2x.ovl` (1:1 DOSBox `0..129` drop-in, plus optional high-throughput shared-memory DOSBox bridge) | Low–Med | `[ ] PENDING` | — |

---

## 2. Milestone M1: Config & Env Var Overhaul + Core Authentic Glide Env Vars (`feat/m1-config-envvars`)

### Goals
1. Replace fragmented `GLIDE_WRAPPER_*` and legacy `GLIDE_*` env vars with a single consistent **`GLIDE_NG_*`** namespace (clean break — no `GLIDE_WRAPPER_*` or legacy `GLIDE_*` aliases).
2. Enforce strict, documented precedence everywhere:
   `Defaults < glide_config.json (or GLIDE_NG_CONFIG) < Authentic 3dfx FX_GLIDE_* < GLIDE_NG_*`
3. Eliminate all direct `std::getenv()` calls inside backend implementations (route every setting through `WrapperConfig` + `JsonConfigLoader` with 100% JSON parity).
4. Implement Phase-1 authentic 3dfx Glide environment variables:
   - `FX_GLIDE_NO_SPLASH`
   - `FX_GLIDE_SHAMELESS_PLUG`
   - `FX_GLIDE_SWAPINTERVAL`
5. Deliver an authoritative, up-to-date user reference document ([docs/glide_environment_variables.md](../glide_environment_variables.md)) separated by section (glide-ng settings vs. authentic Glide variables), with defaults, accepted values, status (`Implemented` / `Stub` / `Planned`), and roadmap.

### M1 Task Breakdown

- **`M1.1`** — Add `/vendor/watcom/` to `.gitignore`, document optional Open Watcom v2 installation in `README.md`, and create this master roadmap tracker (`docs/plans/roadmap_m1_m6_tracker.md`).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M1.2`** — Overhaul `WrapperConfig.{h,cpp}`, `JsonConfigLoader.cpp`, and `Logger::LogExecutionSummary`:
  - Add unified fields to `WrapperConfig`: `logFile`, `vsyncStrictFifo`, `swapInterval`, `noSplash`, `shamelessPlugMode` (`-1` app / `0` off / `1` on), `swThreads`, `swUseSimd`, `swTileSize`, `swAaMode`, `vkCachePath`, `glesVboSizeMb`, plus value-source tracking (`default` / `json` / `fx_env` / `ng_env`) for startup diagnostics.
  - Implement clean precedence in `JsonConfigLoader::Load()`: `Defaults < JSON < FX_GLIDE_* < GLIDE_NG_*`.
  - Support `GLIDE_NG_CONFIG` for explicit config file override, `GLIDE_NG_LOG_LEVEL` accepting both integers (`0`–`5`) and names (`off`, `error`/`crit`, `warn`, `info`, `debug`, `trace`), and `GLIDE_NG_PRESENTATION_FILTER` accepting `nearest`/`0` and `linear`/`1`.
  - Remove duplicate env parsing from `EmulationRegistry` constructor and ensure `EnsureEngineInitialized()` loads config identically to `InitGlide()`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M1.3`** — Remove all scattered `std::getenv()` calls from backends & wire authentic Phase-1 `FX_GLIDE_*` vars:
  - `SoftwareBackendBase.cpp`: read `swAaMode` from `WrapperConfig`.
  - `SoftwareBackend.cpp`: read `vsyncStrictFifo`, `swUseSimd`, `swThreads`, `swTileSize` from `m_config`.
  - `VulkanBackend.cpp`: read `vsyncStrictFifo` and `vkCachePath` from `m_config`.
  - `OpenGLESBackend.cpp`: read `vsyncStrictFifo` and `glesVboSizeMb` from `m_config`.
  - `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`: honor `noSplash` in `RenderSplash()`, honor `shamelessPlugMode` in `ShouldRenderShamelessPlug(appEnabled)`, use `config.logFile` in `Logger::Initialize()`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M1.4`** — Migrate `tests/`, `tools/`, `demos/`, and `.agents/` from `GLIDE_WRAPPER_*`/legacy `GLIDE_*` to `GLIDE_NG_*` + add Catch2 unit tests covering:
  - Full `GLIDE_NG_*` parsing & JSON parity
  - Precedence (`Defaults < JSON < FX_GLIDE_* < GLIDE_NG_*`)
  - `FX_GLIDE_NO_SPLASH`, `FX_GLIDE_SHAMELESS_PLUG`, and `FX_GLIDE_SWAPINTERVAL` behavior
  - Rejection/ignoring of removed legacy names (confirming clean break)
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M1.5`** — Overhaul [docs/glide_environment_variables.md](../glide_environment_variables.md) into the authoritative configuration & env var reference (glide-ng settings, authentic Glide vars, defaults, status tags, and roadmap).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M1.6`** — Full verification (`cmake --build build` with 0 `-Werror` warnings, Catch2 suites across `software`/`vulkan`/`opengl_es`, 6-cell `glide_embed_test` smoke matrix, privacy check) + commit & push.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`

---

## 3. Milestone M2: Tiered Logging & Runtime Perf Tracing (`feat/m2-logging-perf-trace`)

### Goals
1. Upgrade `Logger` from 5 levels (`OFF`..`DEBUG`) with per-line `flush()` + wall-clock `localtime()` to 6 levels (`OFF`..`TRACE`) with lock-free `ShouldLog()` pre-filtering, category bitmask (`GLIDE_NG_LOG_CATEGORIES`), frame-windowed tracing (`GLIDE_NG_TRACE_FRAMES`), monotonic microsecond timestamps + frame ID (`[+SSSSSS.UUUUUU] [FNNNNNN]`), and buffered file flushing (32 KB / 250 ms / urgent `<= WARN`).
2. Add always-compiled lightweight per-frame performance counters (`FrameMetrics`) independent of `ENABLE_TELEMETRY`, exposing render vs. present vs. cap-sleep breakdown, draw calls, mid-frame state flushes, triangles processed/drawn, LFB reads/writes & bytes, texture uploads & bytes, and Vulkan PSO builds — with periodic `[Perf]` log summaries and optional per-frame CSV output (`GLIDE_NG_PERF_LOG`).
3. Instrument frontends (`GlideEntryCommon`, `glide2_entry`, `glide3_entry`), `LfbManager`, `TextureManager`, and all 3 backends (`Vulkan`, `GLES`, `Software`), demoting per-primitive draw logs from `DEBUG` to `TRACE` and fixing `FrameTracker::MarkFrameEnd` ordering so draw batches and present latency are attributed accurately to their own frame.

### M2 Task Breakdown

- **`M2.1`** — Config & env var parsing (`WrapperConfig.h`, `JsonConfigLoader.cpp`):
  - Add `logCategoryMask`, `logCategoriesSpec`, `traceFramesSpec` (`traceFrameStart`/`End`/`Active`), `perfLogEnabled`, `perfLogCsvPath` with full JSON (`logCategories`, `traceFrames`, `perfLog`) and `GLIDE_NG_*` parity.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M2.2`** — Tiered Logger overhaul (`Logger.{h,cpp}`, `GlideEntryCommon.cpp`, `VulkanBackend.cpp`):
  - Add `LogLevel::TRACE` (`5`), `LogCategory` bitmask (`general`, `api`, `lfb`, `tex`, `state`, `draw`, `wsi`, `perf`), `ParseLogCategories`/`FormatLogCategories`, lock-free `ShouldLog()`, `GLIDE_LOG_CAT`/`GLIDE_TRACE`, monotonic microsecond timestamps + `[FNNNNNN]`, buffered flushing, and updated startup profile banner.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M2.3`** — Always-compiled per-frame perf counters & CSV tracer (`Telemetry.{h,cpp}`):
  - Add `FrameMetrics`, always-compiled atomic per-frame counters (`drawCalls`, `stateFlushes`, `tris`, `lfb`, `tex`, `pso`, `presentUs`), periodic 1-second `[Perf]` log summaries, and buffered CSV export (`GLIDE_NG_PERF_LOG=<path>`).
  - Fix `FrameTracker::MarkFrameEnd` to measure true paced frame intervals (including cap sleep) and update FPS every 30 frames or 0.5s.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M2.4`** — Frontend & backend instrumentation (`GlideEntryCommon.cpp`, `LfbManager.cpp`, `TextureManager.cpp`, `glide2_entry.cpp`, `glide3_entry.cpp`, `Vulkan*`, `OpenGLES*`, `SoftwareBackend.cpp`):
  - Wire `RecordDrawCall(fromStateFlush)`, `RecordPsoBuild`, `RecordLfbRead`/`Write`, `RecordTexUpload`, and per-frame `presentStart`->`presentEnd` timing; move `MarkFrameEnd()` to end of `SwapBuffers()`; demote per-primitive draw logs from `DEBUG` to `TRACE`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M2.5`** — Unit tests, docs & verification (`tests/test_glide3.cpp`, `docs/glide_environment_variables.md`):
  - Add Catch2 tests for levels/categories/frame-windowed trace/timestamp format/JSON+env parity/end-to-end CSV & `[Perf]` output; promote M2 vars to **Implemented** in env var reference; verify build (0 `-Werror` warnings), Catch2 suites, embed smoke matrix, live demo smoke test, and privacy scan.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`

---

## 4. Milestone M3: DXVK-Style On-Screen HUD (`feat/m3-hud-fps`)

### Goals
1. Provide a DXVK-style comma-separated on-screen HUD controlled via `GLIDE_NG_HUD` and JSON `"hud"`, starting with `fps` (`GLIDE_NG_HUD=fps` / `1`) and supporting extensible telemetry tokens (`frametimes`, `backend`, `api`, `drawcalls`, `lfb`, `full`) plus layout controls (`scale=`, `position=`, `opacity=`).
2. Render the HUD cleanly in each backend's post-scene presentation pass (**Vulkan**, **OpenGL ES**, **Software**) using a zero-dependency embedded bitmap font with **zero Glide state mutation** and **zero guest framebuffer/LFB mutation**.

### M3 Task Breakdown

- **`M3.1`** — Config & env var parsing (`WrapperConfig.h`, `JsonConfigLoader.cpp`, `Logger.cpp`, `GlideEntryCommon.cpp`):
  - Add `HudElementFlags`, `HudPosition`, `hudMask`, `hudPosition`, `hudScale`, `hudOpacity`, `hudSpec`, `activeFrontendApi`, parser with full JSON + `GLIDE_NG_HUD` parity, and `HUD Overlay` summary banner line.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M3.2`** — Self-contained `HudOverlay` bitmap renderer (`HudOverlay.{h,cpp}`):
  - Embedded 8×8 ASCII bitmap font, fast-converging rolling FPS estimator, 64-frame color-coded frametime sparkline, 3dfx amber/slate themed translucent surface with revision tracking (throttled redraws so GPU textures only re-upload on change), corner placement & small-viewport clamp, and `CompositeOverBgra32` alpha-blender.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M3.3`** — Post-scene backend presentation integration:
  - **Software** (`SoftwareBackend.{h,cpp}`): non-destructive copy & alpha-blend into `m_hudPresentBuffer` before `PresentFrame()`.
  - **OpenGL ES** (`OpenGLESBackend.{h,cpp}`): dedicated RGBA8 HUD texture with revision-gated upload and alpha-blended quad pass in `PresentToWindowSurface()` (zero Glide FBO/state mutation).
  - **Vulkan** (`hud.vert`, `hud.frag`, `VulkanHud.cpp`, `VulkanBackend.{h,cpp}`, `CMakeLists.txt`): dedicated SPIR-V HUD shaders, `eLoad` swapchain render pass (`eTransferDstOptimal -> ePresentSrcKHR`), double-buffered staging buffers, and swapchain resize/recreation support (0 overhead when disabled).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M3.4`** — Tests, documentation & verification (`tests/test_glide3.cpp`, `docs/glide_environment_variables.md`, `tools/DiagnosticInfo.cpp`):
  - Add `[HUD][M3]` Catch2 tests (config parsing, rasterization, placement, BGRA32 compositing, and guest LFB isolation); promote `GLIDE_NG_HUD` to **Implemented** in env var docs; make `grGetString` optional via `dlsym` in Glide 2 tools for authentic `libglide2x` compatibility; verify build (0 `-Werror` warnings), Catch2 suites, 6-cell `glide_embed_test` HUD smoke matrix, and privacy scan.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`

---

## 5. Milestone M4: Parked Official SDK Test Fixes & Standalone WSI Responsiveness (`fix/m4-parked-sdk-tests`)

### Goals
1. Fix `test16.exe` color inversion (R/B swap) when toggling the 3dfx Shameless Plug (`'P'`) on Vulkan and OpenGL ES, and preserve full LFB write state in `grGlideGetState`/`grGlideSetState`.
2. Fix `test22.exe` visual quality regression in Software mode (both scalar and SIMD) so multi-pass fog (`GR_FOG_ADD2`/`GR_FOG_MULT2` + `GR_BLEND_PREFOG_COLOR`) and non-zero `largeLod` mipmapped textures match Vulkan/GLES pixel-for-pixel.
3. Diagnose `test25.exe` & `test27.exe` hangs and improve standalone `StandaloneSDL2` window responsiveness / clean close / repeated `grSstWinOpen`/`grSstWinClose` lifecycle across all 3 backends.

### M4 Task Breakdown

- **`M4.1`** — `test16.exe` Shameless Plug ABGR Readback & State Preservation (`VulkanLfb.cpp`, `OpenGLESBackend.cpp`, `GlideSplashAnimator.cpp`, `glide2_entry.cpp`):
  - Keep `m_lfbStaging` strictly in canonical `0xAARRGGBB` (`BGRA` bytes) in `VulkanBackend::ReadLFB` and `OpenGLESBackend::ReadLFB`, applying the 32-bit-only `ARGB -> ABGR` swizzle only to `dstData` after `m_lfbStaging.ReadRegion(...)` (matching `SoftwareBackend::ReadLFB`). Previously, `m_pixelFormatOverride == 1` (`GR_COLORFORMAT_ABGR`) swizzled `m_lfbStaging` in-place, causing `grShamelessPlug`'s readback + banner write + `FlushLFBToGPU_Locked` to re-upload the entire framebuffer with R and B inverted.
  - Guard `RenderBanner()`'s screen-dimension check before locking LFB so early returns never leak locks or skip `grGlideSetState(&state)`.
  - Save and restore `lfbConstantAlpha` and `lfbWriteColorFormat` in `GlideWrapperState` (`grGlideGetState`/`grGlideSetState`) while preserving the exact 312-byte ABI struct size.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M4.2`** — `test22.exe` Software Multi-Pass Fog & Mipmap LOD Parity (`SoftwareRasterizer.cpp`, `SoftwareRasterizerSimd.cpp`, `SoftwareCombiner.cpp`):
  - Capture `preFogR`/`preFogG`/`preFogB` prior to the fog stage and pass them into `GR_BLEND_PREFOG_COLOR` (`case 15`) across scalar, SIMD, and combiner paths (previously passed post-fog `srcC`, breaking `GR_FOG_MULT2` + `GR_BLEND_PREFOG_COLOR` multi-pass lightmaps).
  - Fix mipmap LOD clamping in `SoftwareRasterizer.cpp` and `SoftwareRasterizerSimd.cpp`: `scaleU = maxDim / 256.0f` already computes LOD relative to mip 0, so clamping to `[tex->largeLod, tex->smallLod]` and subtracting `tex->largeLod` shifted mip selection on textures with `largeLod > 0` (such as `test22`'s 64×64 `light.3df`, `largeLod = 2`).
  - Clamp fog-table index `idx = std::min(idx, 62)` before interpolating `[idx]` and `[idx + 1]` to prevent OOB reads at maximum `eyeW`.
  - Verified pixel parity on `test22` (`decal1.3df` + `light.3df`): Software vs. Vulkan mean max-channel diff dropped from **32.69 (max 120)** to **0.06 scalar / 0.04 SIMD (max 2)** across 307,200 pixels.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M4.3`** — `test25.exe`, `test27.exe`, & Standalone Window Responsiveness (`NativeSurfaceHost.{h,cpp}`, `VulkanBackend.cpp`, `OpenGLESBackend.{h,cpp}`, `SoftwareBackend.{h,cpp}`):
  - Root-caused `test25.exe` (`doHelp()` keymap screen) and `test27.exe` (hardware-cycle prompt): both prebuilt SDK binaries block their own main thread inside `tlGetCH()` (`read(0)` on terminal stdin) immediately after a single `grBufferSwap(1)`, starving `SDL_PollEvent()` on Wayland CSD (`libdecor-gtk`) and losing keyboard focus from the terminal when the new Glide window appears.
  - Added 4 ms throttling to `NativeSurfaceHost::PollEvents(bool force = false)`, pumped initial configure events inside `AttachStandaloneSDL2()`, and added post-present `PollEvents(true)` + unlocked `m_mutex` during `FrameTracker::MarkFrameEnd()` across Vulkan, OpenGL ES, and Software.
  - Implemented `PollEvents()` overrides on `OpenGLESBackend` and `SoftwareBackend` (previously inherited empty no-op), fixed `OpenGLESBackend::SwapBuffers()` to cleanly detach and exit on `StandaloneSDL2` close (previously returned `false` and stayed frozen), and fixed `SoftwareBackend::SwapBuffers()` to call `DetachWindow_Locked()` + unlock `m_mutex` before `std::exit(0)`.
  - Preserved `SDL_INIT_VIDEO` across `StandaloneSDL2` open/close cycles via a RAII lifetime guard (avoiding repeated `libdecor-gtk` `gtk_init()` teardown/re-init during `test27.exe` cycling).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`
- **`M4.4`** — Automated Catch2 `[M4]` Regressions & Full Verification (`tests/test_architecture.cpp`):
  - Added 3 `[Glide2][M4]` Catch2 test cases (44 assertions): `test16` ABGR shameless-plug readback & state preservation, `test22` two-pass `GR_FOG_ADD2`/`GR_FOG_MULT2` + `GR_BLEND_PREFOG_COLOR` + `GR_LOD_64` mip selection + fog-table boundary, and `test25`/`test27` lower-left `grAADrawTriangle` + 5 consecutive `grSstWinOpen`/`grSstWinClose` cycles.
  - Verified build (0 `-Werror` warnings), `[M4]` tests across `software` (scalar & SIMD), `vulkan`, and `opengl_es`, full `Glide2xWrapperTests` / `Glide3xWrapperTests`, prebuilt `test16`/`test22`/`test25`/`test27` PTY runs, 6-cell `child_surface_embed_test` smoke matrix, and privacy scan.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)`

---

## 6. Milestone M5: Carmageddon (`carma`) Compatibility & Performance + GPU State-Dedup (`fix/m5-carmageddon`)

### Root Causes Identified from Live M2 Traces (`carma`, DOSBox SVN `output=surface`, Wayland)
1. **Black Menus & Missing In-Game 2D UI/HUD (All 3 Backends)**:
   - Carmageddon acquires a **persistent `GR_LFB_WRITE_ONLY` lock** (`grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, ...)`) and writes 2D menu screens and in-game HUD elements directly into the locked staging pointer across multiple frames **without calling `grLfbUnlock` before each `grBufferSwap`**. Previously, `LfbManager` only flushed staging pixels on `Unlock()`.
   - `LfbManager::Lock()` compared `type == GR_LFB_WRITE_ONLY` instead of masking `(type & 0x0Fu)`, rejecting `GR_LFB_NOIDLE` (`0x10`) write locks; and write-only locks eagerly called synchronous `ReadLFBData()` (stalling GPU pipelines every frame).
   - `GlideSplashAnimator::RenderBanner()` (`grShamelessPlug`) previously called `grLfbLock`/`grLfbUnlock`, which clobbered persistent guest LFB locks and churned Glide state on every swap.
   - `CpuLfbStaging` had inverted `GR_BLEND_*` enum factors (`GR_BLEND_SRC_ALPHA=1` vs `GR_BLEND_ONE=4`), signed negative `srcStride` bugs, and full-frame GPU upload/readback paths that either clobbered 3D geometry or stalled the GPU.
2. **Severe In-Race Performance Bottleneck (~4–10 FPS)**:
   - Carmageddon switches `GR_TEXTABLE_PALETTE` frequently each frame while ~100+ `P_8` textures remain resident in TMU VRAM. `TextureManager::LoadTable()` eagerly CPU-swizzled and re-uploaded *every* resident texture on every palette change (**1,226 texture uploads / 47 MB per frame**!).
   - `VulkanDraw` performed per-triangle pipeline/descriptor map lookups, wrapped a single shared vertex buffer with `waitIdle()`, and lacked deferred retirement for overwritten textures/descriptor sets.
   - `OpenGLESBackend` flushed batches unconditionally on redundant no-op state setters, and `OpenGLESDraw` split batches on `m_stateCacheDirty` after every state change.
   - `GlideSplashAnimator::DrawFaces` called `grAlphaBlendFunction` redundantly inside the per-face loop.
3. **Pass 2 In-Race Visual Glitches & GLES Crash (Reported 2026-11-16)**:
   - **Mixed textures across all 3 backends + GLES race-start segfault**: `TextureManager::DownloadMipMap()` merged new `grTexDownloadMipMap` uploads at an existing `startAddress` into the old `VirtualTexture` instead of replacing it, leaving stale mip-0 pixels from larger previous textures (e.g., driver portrait overwriting or merging with grass/building textures) and creating empty unpopulated mip-level slots (`size() == 0`) that caused `memcpy` over NULL in `OpenGLESBackend::UploadTexture_Locked`. Additionally, non-overlapping TMU address reuse did not evict overlapping byte ranges, and `VulkanBackend`/`OpenGLESBackend` stored textures in a single flat map instead of per-TMU `[2]` maps.
   - **Missing / punched-out driver portrait on Vulkan & OpenGL ES**: Both GPU fragment shaders used an overly loose chromakey tolerance (`distance(color, key) < 0.01`), discarding dark non-key pixels (`RGB <= 2`) in Carmageddon's black-keyed (`0x0`) driver animation quad.
   - **Player car texture replaced with world decals on Software backend**: Software's deferred tile rasterizer read textures live from `TextureManager` at `FlushBins_Locked()` time instead of capturing an immutable snapshot at draw submission time, so mid-frame `grTexDownloadMipMap` / `grTexDownloadTable` updates retroactively replaced earlier geometry's textures.
   - **~20–25 FPS telemetry finding**: All 3 backends recorded ~40–55 ms `render_ms` with `< 0.75 ms` `present_ms` and ~1,250 tris/frame while `GLIDE_NG_LOG_LEVEL=4` (`DEBUG`) emitted ~4,800 log lines/frame (~330 MB over ~150 s, ~15–20 ms/frame logging overhead) alongside DOSBox CPU cycles; unlogged FPS measurement is requested before planning any final optimization pass.
4. **Pass 3 Visual Glitches & Unstable FPS (Reported 2026-11-16)**:
   - **Neon green/blue/red lines at texture seams (all 3 backends)**: Two distinct Glide combiner/format bugs — (a) `guColorCombineFunction(GR_COLORCOMBINE_ITRGB_DELTA0 / TEXTURE_TIMES_ITRGB_DELTA0)` in `glide2_entry.cpp` routed `local` to `GR_COMBINE_LOCAL_ITERATED` instead of `GR_COMBINE_LOCAL_CONSTANT`, and (b) `TextureManager::DownloadTable(GR_TEXTABLE_PALETTE)` preserved raw top-byte `0x00` alpha from 24-bit `GR_TEXFMT_P_8` palettes instead of forcing `| 0xFF000000u` opaque alpha (matching 3dfx hardware & OpenGlide), causing alpha-blended / seam-adjacent primitives and `P_8` overlays to sample transparent or mis-combined colors. Additionally, GPU/SW fragment shading lacked STW-hint-aware near-plane `divW <= 0` clipping (`GR_STWHINT_W_DIFF_TMU0/1`) and fog `1/oow` guards, and SW checked bit `0x4` instead of `0x8` for TMU1 W-diff.
   - **Sky tearing/ghosting when rotating camera (Vulkan & OpenGL ES)**: `ClearBuffer` in Vulkan, OpenGL ES, and Software ignored `grDepthMask(FXFALSE)` (and GLES ignored `grColorMask`), unconditionally clearing depth/color even when Carmageddon masked depth writes during multi-layer sky/horizon draws. Authentic Glide 2.x/3.x `grBufferClear` obeys `grDepthMask` (`SST_ZAWRMASK`) and `grColorMask`. Also, `guTexSource(GR_NULL_MIPMAP_HANDLE)` did not unbind TMU0 (`0xFFFFFFFFu`).
   - **Intermittent missing driver portrait / Pratcam (Vulkan & OpenGL ES)**: Caused by the combination of `P_8` palette `0x00` alpha (making alpha-blended `P_8` portrait overlays invisible under `SRC_ALPHA` blend), unmasked depth clears, and `guTexSource(GR_NULL_MIPMAP_HANDLE)` leaving stale TMU0 bindings. Enriched `DEBUG` logging in `glide2_entry.cpp` with full call arguments for draw/blend/alpha-test/texture/LFB calls in case further trace forensics are needed.
   - **Unstable FPS (15–60 FPS in dense scenes with `core=dynamic`, `cycles=max`)**: `glide2_entry.cpp` (`grTexMaxAddress`, `grGet(GR_MEMORY_TMU)`, `guTexMemQueryAvail`) and `glide3_entry.cpp` (`grTexMaxAddress`) reported only a fixed 2 MB TMU budget instead of `config.tmuMemoryMb` (default 16 MB), forcing Carmageddon's texture cache to thrash heavily in dense areas. On Vulkan, every evicted/re-uploaded texture also called `vkAllocateMemory` + `vkCreateImage` + `vkCreateImageView` from scratch and hashed the full mip buffer per bind instead of pooling Vulkan image resources and using the O(1) `VirtualTexture::rawDataHash`.

### M5 Task Breakdown

- **`M5.1`** — Persistent LFB Write-Locks & Sparse Alpha-Masked GPU Overlay Compositing (`LfbManager.{h,cpp}`, `CpuLfbStaging.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `GlideSplashAnimator.cpp`, `VulkanLfb.cpp`, `VulkanBackend.{h,cpp}`, `VulkanDraw.cpp`, `OpenGLESBackend.{h,cpp}`, `OpenGLESDraw.cpp`, `OpenGLESShaders.cpp`):
  - Mask `GR_LFB_NOIDLE` (`type & 0x0Fu`) in `LfbManager`; separate `WriteLockState` from `ReadLockState`; initialize write-only staging buffers with a sentinel color instead of synchronous GPU `ReadLFBData()`; add lock-free `FlushActiveWriteLock()` invoked in `grBufferSwap` before `grShamelessPlug` and `SwapBuffers()`, resetting staging to sentinel in-place without reallocating the guest pointer; add `LfbManager::Reset()` on window close / shutdown.
  - Refactor `GlideSplashAnimator::RenderBanner()` to write directly via `backend->WriteLFB(...)` using a one-time flipped static 180×90 buffer — never touching `grLfbLock`/`grLfbUnlock` or Glide pipeline state.
  - Fix `CpuLfbStaging` `GR_BLEND_*` enum factors, chromakey matching, and negative `srcStride`; add sparse alpha-masked overlay tracking (`WriteOverlayLFB` with Porter-Duff `SRC OVER DST` stacking and tight `[minY..maxY]` bounds).
  - Implement zero-readback, zero-`waitIdle()` sparse LFB overlay compositing in both **Vulkan** (`VulkanLfb.cpp` reusing `hud_vert`/`hud_frag` SPIR-V, `eB8G8R8A8Unorm`, `m_renderPass`, per-frame staging ring, flushing both FRONT and BACK buffers after `FlushBatch()`) and **OpenGL ES** (`OpenGLESBackend.cpp` + `uPreserveAlpha` uniform in `OpenGLESShaders.cpp`, fixing both sparse LFB overlay and HUD alpha blending without breaking Wayland window opacity).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M5.2`** — Lazy Palette/NCC Texture Swizzling & Deferred GPU State-Dedup / Batching (`TextureManager.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `GlideSplashAnimator.cpp`, `VulkanBackend.{h,cpp}`, `VulkanDraw.cpp`, `VulkanTextures.cpp`, `OpenGLESBackend.cpp`, `OpenGLESDraw.cpp`, `OpenGLESTextures.cpp`):
  - Replace eager per-palette re-uploads in `TextureManager::LoadTable()` with FNV-1a table hashing, identical-table `memcmp` early-out, and lazy per-TMU dirty flags (`PrepareTextureForBind` / `SyncBoundTextures`), so only bound textures drawn with a changed palette are re-swizzled and uploaded.
  - Deduplicate all ~23 state/texture setters in `OpenGLESBackend` against `GlideStateTracker` before `FlushBatch(true)`, and fix the mid-batch `m_stateCacheDirty` split bug in `OpenGLESDraw.cpp`.
  - Add cached active pipeline & descriptor resolution (`ResolveDrawState_Locked`), per-frame double-buffered vertex buffer slots (eliminating wrap `waitIdle()` stalls), and fence-synchronized deferred retirement queues (`m_retiredTextures[2]`, `m_retiredDescriptorSets[2]`) in `VulkanBackend` / `VulkanDraw` / `VulkanTextures`.
  - Hoist redundant per-face `grAlphaBlendFunction` out of the face loop in `GlideSplashAnimator::DrawFaces`.
  - Verified with M2 `GLIDE_NG_PERF_LOG` on `splash_showcase_demo`: reduced draw calls from ~1,700/frame to **13/frame** on both Vulkan (`9.39 ms` median) and OpenGL ES (`8.29 ms` median) on `llvmpipe`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M5.3`** — Pass 2 Texture Replacement / COW Snapshots, Exact GPU Chromakey & GLES Crash Fix (`TextureManager.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `color.frag`, `OpenGLESShaders.cpp`, `RasterizerState.h`, `SoftwareBackend.{h,cpp}`, `SoftwareRasterizer.cpp`, `SoftwareRasterizerSimd.cpp`, `Vulkan{Backend,Draw,Textures}.*`, `OpenGLES{Backend,Draw,Textures}.*`):
  - Separate full `grTexDownloadMipMap` (`replaceExisting=true`, fresh `VirtualTexture` + overlapping byte-range eviction on the TMU) from partial `grTexDownloadMipMapLevel` (`replaceExisting=false`, safe same-format/aspect/base-LOD COW merge with all gap mip slots pre-sized); enforce copy-on-write before mutating any shared `VirtualTexture` in `EnsureTextureReady_Locked`, `DownloadMipMapPartial`, and `DownloadTable`.
  - Tighten GPU chromakey matching from `distance < 0.01` to exact 8-bit per-channel match (`all(lessThan(abs(color - key), vec3(0.0019)))`) in both Vulkan `color.frag` (recompiled to both SPIR-V variants) and `OpenGLESShaders.cpp`.
  - Capture immutable `std::shared_ptr<const VirtualTexture> boundTex[2]` snapshots in `SoftwareBackend`'s `RasterizerState` tracked by a lock-free `m_textureUploadRevision` atomic (avoiding lock inversion and removing per-triangle `TextureManager` mutex contention in both scalar and SIMD rasterizers).
  - Split GPU texture maps into per-TMU arrays (`m_vulkanTextures[2]`, `m_glesTextures[2]`), fix `VulkanBackend` TMU1 `m_texClampS[tmu]` lookup, flush batches before retiring overwritten Vulkan textures, defensively zero-fill any short mip uploads in Vulkan & GLES so `OpenGLESBackend::UploadTexture_Locked` can never segfault, and demote normal GLES VBO ring-buffer orphaning log from `WARN` to `DEBUG`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M5.4`** — Pass 3 `ITRGB_DELTA0` / `P_8` Alpha / `grBufferClear` Masking, STW Near-Plane Guard, TMU Budget & Vulkan Texture Pool (`glide2_entry.cpp`, `glide3_entry.cpp`, `TextureManager.cpp`, `color.frag`, `OpenGLES{Draw,Shaders}.cpp`, `SoftwareBackendBase.cpp`, `SoftwareRasterizer{,Simd}.cpp`, `Vulkan{Backend,Draw,Textures}.*`):
  - Fix `GR_COLORCOMBINE_ITRGB_DELTA0` / `TEXTURE_TIMES_ITRGB_DELTA0` to use `GR_COMBINE_LOCAL_CONSTANT`, fix `guTexSource(GR_NULL_MIPMAP_HANDLE)` to unbind TMU0 (`0xFFFFFFFFu`), and force `| 0xFF000000u` opaque alpha on `GR_TEXTABLE_PALETTE` (`P_8`) entries in `TextureManager::DownloadTable`.
  - Gate `grBufferClear` depth clear on `m_depthMask` across Vulkan, OpenGL ES, and Software (and color clear on `m_colorMaskRgb || m_colorMaskAlpha` in OpenGL ES) to match authentic Glide 2.x/3.x `SST_ZAWRMASK` semantics.
  - Add STW-hint-aware `divW0`/`divW1 <= 0` near-plane fragment clipping and `1.0 / max(oow, 1e-6)` table-fog guard across Vulkan `color.frag` (both SPIR-V variants rebuilt), `OpenGLESShaders.cpp`, `SoftwareRasterizer.cpp`, and `SoftwareRasterizerSimd.cpp`; fix Vulkan TMU1 chromakey enable check (`(textureEnabled & 3u) != 0u`) and Software TMU1 STW-hint bitmask (`0x8`).
  - Report full `std::max(2u, config.tmuMemoryMb)` (default 16 MB) from `grTexMaxAddress`, `grGet(GR_MEMORY_TMU)`, and `guTexMemQueryAvail` in Glide 2.x & 3.x; add a bounded 64-slot `m_texturePool` in `VulkanBackend` to recycle fence-reclaimed `(width, height, mipLevels)` `VkImage`/`DeviceMemory`/`ImageView` handles and switch `CalculateTextureHash` to O(1) `tex.rawDataHash` when populated.
  - Enrich `glide2_entry.cpp` `DEBUG` logs with full call arguments for `grBufferClear`, `grAlphaBlendFunction`, `grAlphaTestFunction`, `grTexFilterMode`, `grTexClampMode`, `grDepthBufferFunction`, `grTexDownloadMipMap`, `grTexSource`, `grLfbLock`, and `grLfbUnlock`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M5.5`** — Automated `[M5]` Regressions & Full Verification Matrix (`tests/test_architecture.cpp`, `tests/test_glide3.cpp`):
  - Added `[Glide2][M5]` Catch2 suites covering Pass 1 (`M5.1` persistent LFB write-lock menus, 3D + sparse HUD + Shameless Plug coexistence, `GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE`), Pass 2 (`M5.2` lazy `TextureManager` palette switching & state-setter deduplication; `M5.3` full-mipmap replacement & overlapping TMU range eviction, partial `grTexDownloadMipMapLevel` gap safety, mid-frame texture/palette overwrite isolation on deferred SW rasterizer, exact 8-bit chromakey preservation on near-black RGB `(1,1,1)`/`(2,2,2)`), and Pass 3 (`[M5][Carmageddon][Pass3]`: `ITRGB_DELTA0` constant color, `P_8` palette opaque alpha, `grBufferClear` `depthMask` gating, `guTexSource(GR_NULL_MIPMAP_HANDLE)` unbind, `divW <= 0` clipping, 16 MB `grTexMaxAddress` / `guTexMemQueryAvail`; plus Glide 3 `[Glide3][M5][Pass3]` TMU budget & Vulkan texture pool reuse).
  - Updated 3 stale `test16` shameless-plug golden values (authentic `GR_BLEND_SRC_ALPHA` translucency), updated `test21` Mode 2 golden pixel to `0xFFFF0000` (authentic `P_8` opaque alpha), zero-initialized stack `GrVertex` structs in `[Glide2][Texture]` (fixing a latent Vulkan stack-garbage flake), and reordered `tests/test_glide3.cpp` `Depth Buffering` to call `bufferClear` before `depthMask(FXFALSE)`.
  - Verified clean build (0 `-Werror` warnings across `src/`, `tools/`, `demos/`), `Glide2xWrapperTests` **34/34 (2,525 assertions)** across `software` (`SW_SIMD=0` & `SW_SIMD=1`), `vulkan`, and `opengl_es` (+ 10/10 deterministic `[Glide2][Texture]` Vulkan passes), `Glide3xWrapperTests` **47/47 (2,176 assertions)** across `software`, `vulkan`, and `opengl_es`, 6-cell `glide_embed_test` matrix across X11 & Wayland, and privacy scan (0 hits).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`

---

## 7. Milestone M6: Telemetry CPU Split, Optional DOS Diagnostics & GPU-Bound Benchmark (`feat/m6-dos-tools-bench`)

### M6 Task Breakdown

- **`M6.1`** — Per-Frame `app_cpu_ms` vs. `wrapper_cpu_ms` CPU Breakdown (`Telemetry.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `TextureManager.cpp`, `LfbManager.cpp`, `GlideSplashAnimator.cpp`, `SoftwareBackend.cpp`, `Vulkan{Draw,Lfb}.cpp`, `OpenGLES{Backend,Draw}.cpp`, `docs/glide_environment_variables.md`, `tests/test_glide3.cpp`):
  - Add always-compiled, zero-overhead-when-off `CpuBucketScope` (`GLIDE_CPU_SCOPE`) with an exclusive thread-local nesting stack so same-bucket nested calls are no-ops and cross-bucket nested calls (e.g., `grDrawTriangle` triggering lazy `TextureManager::SyncBoundTextures`, or `grColorCombine` flushing `VulkanDraw::FlushBatch`) attribute exclusive nanosecond durations without double-counting.
  - Append 6 columns (`wrapper_cpu_ms`, `app_cpu_ms`, `draw_cpu_ms`, `tex_cpu_ms`, `lfb_cpu_ms`, `state_cpu_ms`) to `GLIDE_NG_PERF_LOG` CSV output (preserving full compatibility with the 18 existing columns) and surface `[app=... wrap=...: draw=... tex=... lfb=... state=...]` in the 1-second `[Perf]` summary.
  - Instrument all hot Glide 2.x/3.x entry points plus internal choke points (`FlushBins_Locked`, `FlushBatch`, `FlushLFBToGPU_Locked`, `FlushActiveWriteLock`, `RenderBanner`, `PrepareTextureForBind`, `SyncBoundTextures`).
  - Add `[Telemetry][M6]` Catch2 tests verifying disabled zero-overhead behavior, same/cross-bucket exclusivity, and end-to-end `app_cpu_ms` vs. `wrapper_cpu_ms` CSV separation.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M6.2`** — Single-Source Env Var Registry, Override Consistency Fixes, Optional Open Watcom v2 DOS Cross-Build & Unified Linux/DOS `glide_info` (`include/glide_ng/env_vars.h`, `src/core/JsonConfigLoader.cpp`, `src/glide2/glide2_entry.cpp`, `src/glide3/glide3_entry.cpp`, `cmake/OpenWatcomDos.cmake`, `CMakeLists.txt`, `tools/CMakeLists.txt`, `tools/common/glide_tool_platform.h`, `tools/common/glide2_dos_ovl.h`, `tools/glide_info/glide_info.c`, `tests/test_architecture.cpp`):
  - Extracted all 31 `GLIDE_NG_*` and 3 `FX_GLIDE_*` env var names + X-macro metadata into pure C89/C99/C++ `include/glide_ng/env_vars.h`, shared by both `JsonConfigLoader` and `glide_info`.
  - Fixed Glide 2.x/3.x override inconsistencies: `grGlideGetVersion` and `grGetString(GR_VERSION)` now honor `config.apiVersion`, `grGet(GR_MEMORY_FB)` and `grGet(GR_NUM_TMU)` read `config.fbiMemoryMb` / `config.tmuCount` after `EnsureEngineInitialized()`, and `grTexMaxAddress`/`guTexMemQueryAvail` return `0` for `tmu >= config.tmuCount` (covered by Catch2 `[Overrides][M6]`).
  - Added optional Open Watcom v2 DOS/32 CauseWay CMake cross-build (`cmake/OpenWatcomDos.cmake`) auto-detecting `vendor/watcom` or `$WATCOM`, embedding `cwstub.exe` for self-contained `.exe` binaries, and cleanly skipping when absent.
  - Built portable CauseWay LE DLL loader (`tools/common/glide2_dos_ovl.h`, `cw_safe_load_ovl` / `cw_safe_free_ovl`) working around CauseWay `cwdll.lib`'s null-`initial_EIP` far-call GPF on DOSBox/OpenGlide `GLIDE2X.OVL` while still delegating to `LoadLibrary` on authentic 3dfx hardware OVLs (`entry_cs != 0`).
  - Implemented unified C99 `glide_info` compiling for Linux (`glide2_info`, `glide3_info`), DOS/32 (`build/dos/glide_info.exe`), and Windows-ready: strictly ≤23 lines × ≤78 cols default non-graphical summary, dynamic active-only env var display, `-v`/`--verbose`, `-p`/`--probe` (resolution/LFB/TMU live verification), `-o <file>`, `--pause`, and `-h`/`--help`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`
- **`M6.3`** — Unified Linux/DOS GPU-Bound Visual Benchmark (`glide_bench`) (`tools/glide_bench/{glide_bench.h,bench_hud.c,bench_scenes.c,glide_bench.c}`, `tools/CMakeLists.txt`):
  - Built unified C99 `glide_bench` compiling for Linux (`glide2_bench`, `glide3_bench`), DOS/32 (`build/dos/glide_bench.exe`), and Windows-ready, using 100% authentic Glide 2.x/3.x calls and zero `glide-ng`-specific hooks.
  - Extensible `BenchScene` vtable registry (`kBenchScenes[]`) with 6 precomputed low-CPU scenes: `fillrate` (16x alpha overdraw), `tris` (4K-tri waving mesh), `state` (120 batches/frame state thrash), `tex` (64x `P_8` texture + palette streaming), `lfb` (3D + sparse 565 HUD write), and `multipass` (2-pass `GR_FOG_ADD2`/`MULT2` + `PREFOG_COLOR` + mipmaps + chromakey).
  - Pure-Glide textured-quad in-scene HUD (4×6 bitmap font atlas in TMU0, toggled via `--no-hud`), full CLI (`--scene`, `--res`, `--seconds`, `--frames`, `--loop`, `--no-hud`, `-o`, `--csv`, `--list`, `--pause`, `-h`), 60 Hz pacing detector (`GLIDE_NG_VSYNC=0` / `GLIDE_NG_MAX_FPS=0`), and 3DMark-style weighted geometric-mean **GlideMark Score** (`30 FPS ref = 1000 pts`).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-16)`

---

## 8. Milestone M7: Performance Profiling & Optimization (`perf/m7-profiling-optimization`)

### M7 Task Breakdown

- **`M7.1`** (`96f449a`) — Eliminate Per-API-Call `PollEvents()` & Inline Init Fast-Path (`GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`):
  - Removed redundant per-API-call `PollEvents()` invocations from hot draw/state/texture paths (pumping WSI events cleanly at frame swap) and inlined the atomic engine-init fast-path guard.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.2`** (`0581a24`, `4cd02d1`) — Frontend Per-Triangle Decode, Sanitization & Static Fast-Path Guards (`VertexLayout.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`):
  - Added canonical inline Glide 2.x vertex decoding (`DecodeGlide2CanonicalInto`) with branch-predicted finite/clamp sanitization and lightweight static fast-path guards on hot draw/state/texture entry points.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.3`** (`f164625`) — Vulkan & OpenGL ES `DrawTriangle` Fast Paths & In-Pass Bind Deduplication (`Vulkan{Backend,Draw}.*`, `OpenGLES{Backend,Draw}.*`):
  - Bypassed `ResolveDrawState_Locked` on steady-state Vulkan triangle batches, deduplicated `vkCmdBindPipeline`/`vkCmdBindDescriptorSets` within an active render pass, and added a matching steady-state triangle append fast path in `OpenGLESDraw`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.4`** (`2d72da4`) — OpenGL ES Dirty-Bitmask Uniforms, Binding Cache & Debug-Gated `glGetError` (`OpenGLES{Backend,Draw,Textures}.*`):
  - Replaced unconditional per-flush uniform uploads with a dirty-bitmask update path, cached active GL texture/program bindings, and gated `glGetError` checks so Release draw/flush paths avoid synchronous driver error polls.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.5`** (`2cd09f1`) — Texture Decode/Hash, GLES Subimage Reuse & Vulkan Pre-Frame Upload Command Buffer (`TextureManager.{h,cpp}`, `OpenGLESTextures.cpp`, `Vulkan{Backend,Textures}.*`):
  - Optimized P8/RGB565/ARGB4444 texture decoding and hashing in `TextureManager`, reused existing GLES texture storage via `glTexSubImage2D` on same-size re-uploads, and recorded Vulkan COW texture uploads onto a per-frame pre-render command buffer so mid-frame uploads no longer split the active render pass (cutting Vulkan `tex` frame time by **−88%**).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.6`** (`657d26a`) — Disjoint LFB Dirty Bands, Reused Sentinel Staging & Band-Scissored GPU Overlays (`CpuLfbStaging.{h,cpp}`, `LfbManager.{h,cpp}`, `VulkanLfb.cpp`, `OpenGLESBackend.cpp`, `SoftwareBackend.cpp`):
  - Tracked disjoint horizontal dirty bands (`DirtyBand`) instead of a single merged bounding box, reused sentinel staging buffers in place without per-frame reallocation/full-screen clears, and uploaded/composited only dirty bands via scissored GPU overlay draws (cutting `lfb` wrapper CPU by **−70% to −79%** across all 3 backends).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.7`** (`a807726`) — Software Rasterizer AVX2+FMA SIMD Default & Inner-Loop / ThreadPool Optimization (`src/CMakeLists.txt`, `WrapperConfig.h`, `SoftwareBackend.{h,cpp}`, `SoftwareInternal.h`, `SoftwareCombiner.cpp`, `SoftwareRasterizer{,Simd}.cpp`, `ThreadPool.h`):
  - Default-enabled AVX2+FMA SIMD rasterization (`swUseSimd = true`, guarded at runtime by `__builtin_cpu_supports("avx2")`, still overridable via `GLIDE_NG_SW_SIMD=0` or `"swSimd": false`).
  - Guided by Callgrind profiling: inlined `EvaluateTmuStage`/`EvaluateTmuStageSIMD` and `SampleTextureLevelSIMD`/`SampleMipLevelSIMD` with a non-mipmapped mip-0 fast path and power-of-two shift indexing, added incremental edge stepping, hoisted interior boundary/combiner/dither checks, eliminated FB reads on full-coverage opaque quads, added fast-path alpha blending, removed `always_inline` bloat on scalar templates, and eliminated `ThreadPool` atomic false sharing with chunked work-stealing.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M7.8`** (`7b6966c`) — Vulkan Pooled-Texture Descriptor Set Reuse (`VulkanBackend.h`, `VulkanTextures.cpp`):
  - Retained cached `VkDescriptorSet`s across `m_texturePool` reuse (only evicting descriptors when non-pooled textures are destroyed, on `PurgeTextures()`, or at high-water capacity `3072 / 4096`), eliminating per-frame `vkAllocateDescriptorSets`/`vkUpdateDescriptorSets`/`vkFreeDescriptorSets` churn during TMU streaming and palette swaps (cutting Vulkan `tex` `draw_cpu_ms` by **−87.2%**).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`

### M7 End-to-End Verification & Headless Release Results (`final_m7` vs. `base_clean`)
- **Build**: 0 compiler warnings across `src/`, `tools/`, `demos/` in both Release (`-O3`, `ENABLE_TELEMETRY=OFF`) and Debug builds, including Open Watcom v2 DOS/32 CauseWay tools (`glide_info.exe`, `glide_bench.exe`).
- **Automated Tests**: All 8 Catch2 suites passed (`Glide2xWrapperTests` 35/35 [2,557 assertions] and `Glide3xWrapperTests` 48/48 [2,211 assertions] across `vulkan`, `opengl_es`, `software` SIMD, and `software` scalar `GLIDE_NG_SW_SIMD=0`), all 4 X11 `glide_embed_test --smoke-ms 1500` runs passed (including `opengl_es --parent-gl`), and `glide2_info --probe` passed on all 3 backends.
- **Headless Release `glide2_bench` (no-HUD, median, software-rendered Mesa `llvmpipe` for GPU backends)**:
  - **Vulkan**: Geomean FPS `111.7 -> 175.7` (**+57.3%**); `wrapper_cpu_ms` cut across all 6 scenes (`fillrate` −24.9%, `tris` −26.7%, `state` −18.2%, `tex` −42.3% / `tot` −88.4%, `lfb` −79.2%, `multipass` −31.5%).
  - **OpenGL ES**: Geomean FPS `185.3 -> 241.6` (**+30.4%**); `wrapper_cpu_ms` cut across all 6 scenes (`fillrate` −12.3%, `tris` −7.9% / `tot` −18.9%, `state` −22.0%, `tex` −20.1%, `lfb` −69.7% / `tot` −49.6%, `multipass` −12.8%).
  - **Software**: Geomean FPS `218.4 -> 658.8` (**+201.7%**, 3.0×); `tot_ms` cut across all 6 scenes (`fillrate` −87.3%, `tris` −45.3%, `state` −59.5%, `tex` −46.9%, `lfb` −75.7%, `multipass` −63.4%).
  - **StandaloneSDL2 (Xvfb X11)**: Vulkan `fillrate` −7.1% / `tris` −15.6% / `state` −11.6%; OpenGL ES `fillrate` −12.0% / `tris` −20.1% / `state` −17.1%.

---

## 9. Milestone M8: Unified `glide-ng.ini` & Phase-2 Authentic 3dfx Env Vars (`feat/m8-ini-config-envvars-p2`)

### M8 Task Breakdown

- **`M8.1`** (`745f6ca`) — Clean Break from `glide_config.json` to Unified `glide-ng.ini` (`IniConfigLoader.{h,cpp}`, `WrapperConfig.{h,cpp}`, `Logger.cpp`, `env_vars.h`, `glide-ng.ini.example`, `README.md`, `docs/`):
  - Replaced `JsonConfigLoader` and `glide_config.json` with a zero-dependency sectioned INI parser (`IniConfigLoader`) supporting `#`/`;` comments, inline comments, UTF-8 BOM, CRLF, quoted strings, unknown-key `[WARN]` diagnostics, and strict 4-layer precedence (`Defaults < glide-ng.ini < Authentic 3dfx < GLIDE_NG_*`).
  - Unified default runtime file names (`glide-ng.ini`, `glide-ng.log`, `.glide-ng-vkcache.bin`), tracked `glide-ng.ini.example`, added `/glide-ng.ini` to `.gitignore`, removed all live JSON config references across code/tools/demos/tests/docs, and documented optional MinGW-w64 (`i686-posix`) + Wine prerequisites in `README.md`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`
- **`M8.2`** (`4f4cd56`) — Phase-2 Authentic 3dfx Env Vars, Permanent `glide_envtest` Verifier, Vulkan Reopen Fix, Tests & Docs (`env_vars.h`, `WrapperConfig.h`, `IniConfigLoader.cpp`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `VulkanBackend.cpp`, `Logger.cpp`, `tools/glide_envtest/glide_envtest.c`, `tools/common/glide2_dos_ovl.h`, `tools/CMakeLists.txt`, `tests/test_glide3.cpp`, `tests/test_architecture.cpp`, `docs/glide_environment_variables.md`):
  - Implemented all 17 Phase-2 authentic 3dfx variables with full `glide-ng.ini` parity: `FX_GLIDE_FBRAM`, `FX_GLIDE_TMU_MEMSIZE`, `SSTV2_NUM_TMUS`, `SST_GAMMA`/`SSTV2_GAMMA`, `SST_SCREENREFRESH`/`SSTV2_SCREENREFRESH`, `FX_GLIDE_IGNORE_REOPEN`, `FX_GLIDE_NO_DITHER_SUB`, `SSTV2_FORCE_BILINEAR`, `SSTV2_FORCE_TRILINEAR`, `SST_TEXMAP_DISABLE`, `FX_GLIDE_BOUNDS_CHECK`, `FX_GLIDE_ALLOC_COLOR`, `FX_GLIDE_ALLOC_AUX`, and `FX_GLIDE_SNAPSHOT`/`FX_SNAPSHOT` (binary `P6` PPM capture honoring `ARGB`/`ABGR`).
  - Fixed a Vulkan segfault (`vkCmdEndRenderPass` with stale `m_inRenderPass = true`) in `VulkanBackend::DetachWindow_Locked()` when an application re-invokes `grSstWinOpen` mid-frame without `FX_GLIDE_IGNORE_REOPEN=1`.
  - Created permanent pure-C99 verification tool (`glide2_envtest`, `glide3_envtest`, and DOS/32 `build/dos/glide_envtest.exe`) with 4-quadrant visual test card + automated RGB565 LFB probe verdicts (`--verify`, `--test-reopen`, `--test-bounds`, `--splash`, `--shameless-plug`), plus comprehensive Catch2 `[Config][EnvVars][M8]` and `[Overrides][EnvVars][M8]` tests and copy-paste usage examples in `docs/glide_environment_variables.md`.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-17)`

---

## 10. Milestone M9: Win32 32-bit `glide2x.dll` & `glide3x.dll` for Wine/Proton (`feat/m9-win32-dll-wine`)

### M9 Task Breakdown

- **`M9.1`** — MinGW-w64 `i686` Cross-Compile Toolchain, Zero-Dependency Static Linking & Authentic 3dfx `.def` Exports (`cmake/toolchains/mingw-w64-i686.cmake`, `CMakeLists.txt`, `src/CMakeLists.txt`, `src/glide2/glide2x.def`, `src/glide3/glide3x.def`, `.gitignore`):
  - Added `cmake/toolchains/mingw-w64-i686.cmake` auto-selecting `i686-w64-mingw32-{gcc,g++}-posix` (with non-suffixed fallback for Arch/Fedora) while running host `glslangValidator`/`python3` natively.
  - Staged isolated `${CMAKE_BINARY_DIR}/win32_vk_inc/{vulkan,vk_video}` header symlinks, enabled `-mstackrealign` (protecting 32-bit AVX2/SSE against 4-byte-aligned 1990s caller stacks), and statically linked MinGW C++/winpthreads runtimes (`-static`) so `glide2x.dll`, `glide3x.dll`, and all Win32 tools depend **exclusively** on built-in Win32 system DLLs (`KERNEL32.dll`, `msvcrt.dll`, `USER32.dll`, `GDI32.dll`, `OPENGL32.dll`).
  - Added compiler-verified `src/glide2/glide2x.def` (288 exports: 144 canonical `_Func@N` + 144 undecorated `Func`) and `src/glide3/glide3x.def` (204 exports: 102 canonical `_Func@N` + 102 undecorated `Func`), eliminating internal C++ symbol leaks, plus `i686-w64-mingw32-dlltool` canonical 3dfx import libraries (`libglide2x_3dfx.dll.a`, `libglide3x_3dfx.dll.a`) so MinGW-built tools import authentic `_Func@N` symbols compatible with `glide-ng`, real 3dfx drivers, nGlide, and dgVoodoo2.
  - Fixed a latent cross-API bug caught by Win32 `__stdcall` decoration: `GlideSplashAnimator::SourceTexture()` called 3-arg Glide 2 `grTexDownloadTable(GR_TMU0, ...)` (`@12`) even inside `glide3x` (where Glide 3's `grTexDownloadTable` takes 2 args, `@8`); routed directly to `TextureManager::GetInstance().DownloadTable(...)`.
  - Fixed 32-bit portability across `Logger.h` (`#undef ERROR`), `Logger.cpp` (`GetModuleHandleExA`/`GetModuleFileNameA`), `CpuLfbStaging.h` (`_aligned_malloc`/`_aligned_free`), `VulkanBackend.h`/`VulkanTextures.cpp`/`VulkanPipeline.cpp` (`VkHandleToU64` for 64-bit non-dispatchable handles on 32-bit x86), and `glide2_entry.cpp`/`glide3_entry.cpp` (`FxU32*` = `unsigned long*` vs `uint32_t*` adapters and `FX_CALL` forward declarations).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M9.2`** — Native Win32 `HWND` Surface Hosting (`src/core/NativeSurfaceHost.{h,cpp}`):
  - Implemented `Mode::Win32Window` (standalone centered Win32 `HWND` with `CS_OWNDC` and non-blocking `PeekMessageA` pump) and `Mode::Win32Embedded` (click-through child `HWND` with `WM_NCHITTEST -> HTTRANSPARENT`, `CS_OWNDC`, automatic client-rect `SyncBounds()`, and fallback to standalone if `!IsWindow`), with zero SDL2 dependency on `_WIN32`.
  - Added authentic 3dfx Win32 `hWnd == 0` active-window resolution (`FindWin32ProcessHostHwnd()`), attaching fullscreen GUI games (`WS_POPUP` + `SW_SHOWMAXIMIZED` calling `grSstWinOpen(0, ...)`, e.g. 3dfx *Donut*) directly into their own window in `Win32Embedded` mode while keeping console tools (`glide2_info`/`glide2_bench`/`glide2_envtest`) in standalone `Win32Window` mode.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M9.3`** — All Three Backends Native on Win32 + Real-App 1998 3dfx *Donut* Demo Fixes (`VulkanBackend.{h,cpp}`, `VulkanTextures.cpp`, `OpenGLESBackend.{h,cpp}`, `OpenGLESInternal.h`, `OpenGLESShaders.cpp`, `Win32GlLoader.h`, `SoftwareBackend.{h,cpp}`, `SoftwareBackendBase.cpp`, `SoftwareInternal.h`, `SoftwareRasterizer{,Simd}.cpp`, `CpuLfbStaging.cpp`, `TextureManager.cpp`, `glide2_entry.cpp`, `glide3_entry.cpp`):
  - **Vulkan**: Enabled `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`, dynamic `LoadLibraryA("vulkan-1.dll")` loader (graceful fallback if Vulkan ICD absent), and `VK_KHR_win32_surface` via `createWin32SurfaceKHRUnique`.
  - **OpenGL (WGL)**: Implemented native WGL OpenGL 3.3 Core context creation (including hidden dummy `HWND` for `GLIDE_NG_HEADLESS=1`, `wglCreateContextAttribsARB`, `wglSwapIntervalEXT`, `::SwapBuffers`), zero-dependency `Win32GlLoader.h` (52 GL 1.3–3.3 entry points + `glClearDepthf` shim), and `#version 300 es` → `#version 330 core` GLSL shader adaptation (with core dual-source blend `#if 1`).
  - **Software**: Implemented `Win32GdiPresenter` (`StretchDIBits` top-down 32-bit BGRA with aspect-preserving black letterbox/pillarbox bars) and `#ifdef _OPENMP` guards.
  - **Authentic `grTexDownloadMipMapLevelPartial`, Software W-Buffer/Combiner, `GR_DITHER_2x2` Alpha, Vulkan Gamma LUT & Win32 `ThreadPool` Parity (caught live via 1996–1998 3dfx *Donut*, *Valley of Ra*, *Demo Room*, *Wizard's Tower*, and *Surf* Glide 2/3)**: Replaced Glide 2's full-download promotion stub with true partial row updates (`TextureManager::DownloadMipMapPartial`), fixed inclusive `[start..end]` row bounds in both Glide 2 & Glide 3 (plus Vulkan `bufferImageHeight = numRows`), anchored Software `oow`/`z` barycentric interpolation at vertex `a` with `[0, 1]` `targetDepth` clamping (eliminating coplanar `GR_DEPTHBUFFER_WBUFFER` `LEQUAL` Z-fighting), fixed `factCG`/`factCB` in combiner funcs `6/7/8`, clamped combiner/channel outputs to `[0, 1]` / `[0, 255]`, fixed `GR_DITHER_2x2` 4× alpha-dither overscaling in `OpenGLESShaders.cpp` and `SoftwareRasterizerSimd.cpp`, implemented silicon-faithful RAMDAC Gamma LUT swapchain presentation in `VulkanBackend`/`VulkanHud` (`present_gamma.frag`, matching OpenGL ES & Software pixel-for-pixel while keeping guest VRAM linear and compositing HUD cleanly), demoted `GlideStateTracker::SetAAState_Locked` to `DEBUG` with idempotent early-out (eliminating per-frame `INFO` log spam in `surf_glide3x.exe`), and replaced `winpthreads` `std::condition_variable` with native Win32 `SRWLOCK` + `CONDITION_VARIABLE` + brief atomic spin in `ThreadPool.h` (+2.2× Wine Software headless / +2.0× Wine Software windowed throughput). Verified pixel-identical rendering across Vulkan, WGL OpenGL 3.3 Core, Software AVX2+FMA SIMD, and Software scalar, plus Catch2 `[Glide2][M9]` regressions.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M9.4`** — Win32 Diagnostic, Benchmark & EnvVar Verification Tools (`tools/CMakeLists.txt`, `tools/common/glide2_dos_ovl.h`, `README.md`):
  - Cross-compiled all 6 pure-C99 Win32 tools (`glide2_info.exe`, `glide3_info.exe`, `glide2_bench.exe`, `glide3_bench.exe`, `glide2_envtest.exe`, `glide3_envtest.exe`) with zero external runtime DLLs; Glide 2 tools dynamically load `glide2x.dll` via `LoadLibraryA` + `GetProcAddress("_gr...@N")`, while Glide 3 tools link against canonical `libglide3x_3dfx.dll.a` (`_gr...@N`).
  - Updated `README.md` with Win32 cross-build and Wine/Steam Proton `WINEDLLOVERRIDES="glide2x,glide3x=n"` instructions.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M9.5`** — End-to-End PE Audit, Live Wine Validation & Linux/DOS Regression Matrix:
  - **Win32 PE Audit (`i686-w64-mingw32-objdump -p`)**: Verified all 8 PE32 `i386` binaries import zero external runtime DLLs; `glide2x.dll` (288 exports) and `glide3x.dll` (204 exports) export exact canonical `_Func@N` + undecorated `Func` tables with zero C++ leaks; `glide3_{info,bench,envtest}.exe` import 100% canonical `_Func@N` symbols matching `glide3x.dll`.
  - **Live Wine Validation (`wine32-stable`)**: Passed 100% across all 3 backends (`software`, `opengl_es` WGL 3.3 Core, `vulkan` dynamic `vulkan-1.dll`): `glide2_info.exe --probe` & `glide3_info.exe --probe` (6/6), `glide2_envtest.exe` & `glide3_envtest.exe` `--verify --test-reopen --test-bounds` (6/6 + authentic 3dfx override checks), `glide2_bench.exe` & `glide3_bench.exe` headless (6/6) and windowed under Xvfb (6/6), plus live `Win32Embedded` parent-`HWND` hosting with resize, 3dfx splash enabled, and LFB RGB565 pixel verification across all 3 backends (3/3).
  - **Linux & DOS Regression Matrix**: Zero compiler warnings across Win32 Release, Linux Debug, Linux Release, and Open Watcom v2 DOS/32 CauseWay tools; all 8 Catch2 suites passed (`Glide2xWrapperTests` and `Glide3xWrapperTests` across `vulkan`, `opengl_es`, `software` SIMD, and `software` scalar with splash enabled); X11 `child_surface_embed_test` passed across all backends.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M9.6`** — Voodoo4/5 (`VSA-100`) Spatial AA, T-Buffer Sample Masking, Strict Card-Model Extension Gating & Upgraded `glide_info` (`WrapperConfig.{h,cpp}`, `IniConfigLoader.cpp`, `Logger.cpp`, `HudOverlay.cpp`, `glide2_entry.cpp`, `glide3_entry.cpp`, `Vulkan*`, `OpenGLES*`, `Software*`, `tools/glide_info/glide_info.c`, `tools/common/glide2_dos_ovl.h`, `tests/test_glide3.cpp`, `docs/compatibility_library.md`):
  - Implemented full VSA-100 Spatial Anti-Aliasing (`grSstWinOpenExt`, live mid-frame `grEnable`/`grDisable(GR_AA_MULTI_SAMPLE)`) and T-Buffer sample masking (`grTBufferWriteMaskExt`, sample-masked `grBufferClear`, per-sample depth isolation) with parity across **Vulkan** (MSAA + dual render pass), **OpenGL ES / WGL** (offscreen MSAA FBOs + `glSampleMaski`), and **Software** (2×2 SSAA), verified live on 3dfx *Kitty Hawk*, *Museum* (Soft Reflectance), and *Rocket Burger* (Depth of Field + Motion Blur).
  - Added `CardModel::Voodoo4` (`voodoo4` / `v4` / `h4` / `vsa100` — single VSA-100, 32 MB FB, 2 TMUs, 16 MB/TMU, 2× Spatial AA cap) and `[WARN]` diagnostics on unrecognized `card_model` / `api_version` values instead of silent fallback.
  - Strictly gated `grGetProcAddress` across both Glide 3.x and Glide 2.x (`IsCardFeatureAvailable` / `IsFunctionAvailable`) so VSA-100 `PIXEXT` procs resolve only on `Voodoo4`+ and `TEXCHROMA` resolves only on `Voodoo2`+ (fixing the `TEXCHROMA` leak on `Voodoo1` in Glide 2.x).
  - Upgraded `glide3_info` and `glide2_info` to word-wrap `Extensions` (no column-78 truncation), print a compact `VSA-100` summary line in Glide 3.x, and report advertised vs. `grGetProcAddress`-resolved extension entry points in `-v` verbose mode.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`

---

## 11. Milestone M10: VSA-100 / Glide 3.x Extensions, `VoodooBanshee` & Export Parity (`feat/m10-vsa100-extensions-card-matrix`)

### M10 Task Breakdown (Ordered Simplest → Most Complex)

- **`M10.1`** (`66bd55d`) — `CardModel::VoodooBanshee` Profile (`WrapperConfig.{h,cpp}`, `IniConfigLoader.cpp`, `Logger.cpp`, `HudOverlay.cpp`, `glide2_entry.cpp`, `glide3_entry.cpp`, `tests/test_architecture.cpp`, `tests/test_glide3.cpp`):
  - Added `CardModel::VoodooBanshee` (`banshee` / `voodoo_banshee` / `h3`) positioned between `Voodoo2` and `Voodoo3`: single-TMU H3 core, 16 MB FB, 16 MB UMA texture space, 256×256 max texture (Glide 3), Glide 2.61 default version, `GR_HARDWARE = "Voodoo Banshee"`, `GETGAMMA`/`TEXUMA`/`TEXMIRROR`/`PALETTE6666` capabilities, and Banshee card-model unit tests.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.2`** (`254f7ee`) — Complete Glide 2.x/3.x Export Parity & `GETGAMMA` (`include/glide{2,3}/glide.h`, `src/core/GlideEntryCommon.{h,cpp}`, `src/core/IGraphicsBackend.h`, `src/backends/GlideStateTracker.{h,cpp}`, `src/backends/vulkan/VulkanBackend.{h,cpp}`, `src/backends/opengl_es/OpenGLESBackend.{h,cpp}`, `src/backends/software/SoftwareBackend.{h,cpp}`, `src/glide2/{glide2_entry.cpp,glide2x.def}`, `src/glide3/{glide3_entry.cpp,glide3x.def}`, `tests/`):
  - Completed Glide 2.x (132/132 PE exports: `guFbReadRegion`, `guFbWriteRegion`, `guMP*`, `grSstVidMode`, `guMovie*`) and Glide 3.x (95/95 base PE exports: `grAlphaControlsITRGBLighting`, `grCheckForRoom`, `grErrorSetCallback`, `grLfbConstantDepth`, `grLfbWriteColorSwizzle`, `grSstConfigPipeline`, `grTexNCCTable`, `guEncodeRLE16`) parity against real 3dfx binaries.
  - Implemented `GETGAMMA` (`grGetGammaTableExt`, Banshee+) across Vulkan, OpenGL ES, and Software backends with round-trip RAMDAC gamma readback tests.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.3`** (`b6f39fb`) — Per-Channel `grColorMaskExt`, `TEXUMA`, `grTexMultibase` & Authentic `grTexDetailControl` (`TextureManager.{h,cpp}`, `IGraphicsBackend.h`, `GlideStateTracker.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `Vulkan*`, `OpenGLES*`, `Software*`, `tests/test_glide3.cpp`):
  - Implemented independent per-channel R/G/B/A write masking in `grColorMaskExt` (including masked clear quad support), `TEXUMA` (`GR_TEXTURE_UMA_EXT` unified TMU address space on Banshee+), `grTexMultibase` / `grTexMultibaseAddress`, and authentic `grTexDetailControl` (`DETAIL_FACTOR` / `LOD_FRACTION` / `BLEND_LOCAL`) across Vulkan, OpenGL ES, and Software (scalar + SIMD), visually verified across all backends.
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.4`** (`33c1118`) — Full VSA-100 `PIXEXT` Hardware Stencil (`IGraphicsBackend.h`, `GlideStateTracker.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `Vulkan*`, `OpenGLES*`, `Software*`, `tests/test_glide3.cpp`):
  - Implemented `GR_STENCIL_MODE_EXT`, `grStencilFuncExt`, `grStencilMaskExt`, `grStencilOpExt` (all 8 stencil ops), `grBufferClearExt` stencil clear, `grLfbConstantStencilExt`, and `grGlideGetState`/`grGlideSetState` persistence across Vulkan (`D24S8`), OpenGL ES (`GL_DEPTH24_STENCIL8`), and Software (`m_stencilBuffer`, scalar + SIMD). Visually verified (134 assertions, 0/307,200 pixel diff across all 4 configs).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.5`** (`9d1e4c6`) — Full VSA-100 `TEXFMT` Texture Format Decoders (`include/glide3/glide.h`, `src/core/3dfParser.{h,cpp}`, `src/core/GlideEntryCommon.{h,cpp}`, `src/core/TextureManager.{h,cpp}`, `src/glide3/glide3_entry.cpp`, `tests/test_glide3.cpp`):
  - Implemented all VSA-100 extended texture formats: `ARGB_8888`, `RGB_888`, `YUYV_422`, `UYVY_422`, `AYUV_444`, all 4 `FXT1` modes (`CC_HI`, `CC_CHROMA`, `CC_MIXED`, `CC_ALPHA`), and `DXT1`–`DXT5`, plus `3dfParser` & `Map3dfLodToGlide3` support for 32-bit `.3df` assets (verified on real H5 SDK `t256x32.3df`, 130 assertions across all 4 backend configs).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.6`** (`c250cb2`) — VSA-100 `COMBINE`, `grAlphaBlendFunctionExt` & `TEXTUREBUFFER` (`include/glide3/glide.h`, `src/core/{IGraphicsBackend.h,WrapperConfig.cpp,TextureManager.*,GlideEntryCommon.*}`, `src/backends/{GlideStateTracker.*,CpuLfbStaging.cpp}`, `Vulkan*`, `OpenGLES*`, `Win32GlLoader.h`, `Software*`, `glide2_entry.cpp`, `glide3_entry.cpp`, `glide3x.def`, `tools/glide_info/glide_info.c`, `tests/test_glide3.cpp`):
  - Implemented full VSA-100 `COMBINE` (`grColorCombineExt`, `grAlphaCombineExt`, `grTexColorCombineExt`, `grTexAlphaCombineExt`, `grConstantColorValueExt`) with all `GR_FUNC_MODE_*` modifiers, raw `GR_CMBX_B` bypass, `ALOCAL`/`AOTHER` propagation, `0x/2x/4x` shift, output invert, per-TMU constant colors, and selective legacy reset across Vulkan, OpenGL ES, and Software (scalar + 8-wide SIMD).
  - Implemented `grAlphaBlendFunctionExt` (`GR_BLEND_OP_ADD`/`SUB`/`REVSUB`, `GR_BLEND_SAME_COLOR_EXT`, `GR_BLEND_ONE_MINUS_SAME_COLOR_EXT`) across all backends (including dynamic `glBlendEquationSeparate` on Win32 WGL) and Glide 2/3 frontends.
  - Implemented `TEXTUREBUFFER` (`grTextureBufferExt`, `grTextureAuxBufferExt`, `grAuxBufferExt`, `GR_BUFFER_TEXTUREBUFFER_EXT`) with lossless backbuffer save/restore, mid-mode target switching (`test41.c` lifecycle), origin handling, LFB targeting, and `TextureManager::CommitRenderedTexture()` upload across all backends.
  - Strictly gated `TEXTUREBUFFER` to Banshee+ and `COMBINE`/`grAlphaBlendFunctionExt` to Voodoo4+, exported all 9 procs in `glide3x.def`, persisted state in `grGlideGetState`/`grGlideSetState`, and verified 4-quadrant visual showcase (118 assertions; 0/307,200 Vulkan vs. GLES diff).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **`M10.7`** — Pure Glide 2.x ABI, Bare 3-Digit Versions, Warn-Only Card-to-Version Matrix, Authentic Capabilities/Resolutions & Reference Docs (`WrapperConfig.{h,cpp}`, `IniConfigLoader.cpp`, `Logger.cpp`, `VertexLayout.{h,cpp}`, `include/glide2/glide.h`, `src/glide2/{glide2_entry.cpp,glide2x.def}`, `src/glide3/{glide3_entry.cpp,glide3x.def}`, `tools/glide_info/glide_info.c`, `tools/DiagnosticInfo.cpp`, `tools/glide_bench/glide_bench.c`, `demos/`, `tests/`, `docs/glide_versions_and_cards.md`, `docs/glide_device_and_version_gating.md`):
  - Removed `grGetString` and `grGetProcAddress` from Glide 2.x (`libglide2x.so` / `glide2x.dll` / `glide2x.def` / `include/glide2/glide.h`) to match authentic 3dfx Glide 2.x DLLs/OVLs (0 real 3dfx Glide 2.x binaries exported them).
  - Switched `grGlideGetVersion` (Glide 2.x) and `grGetString(GR_VERSION)` (Glide 3.x) to bare 3-digit strings with no `" (glide-ng)"` suffix, defaulting each card to its maximum authentic version (`Voodoo1`/`Rush` → `2.48`/`3.04`, `Voodoo2` → `2.56`/`3.03`, `Banshee`/`Voodoo3`/`Voodoo4`/`Voodoo5` → `2.61`/`3.10`), while retaining `Voodoo2` as the universal default card.
  - Added full historical `ApiVersion` coverage (`2.11`–`2.61`, `3.00`–`3.04`, `3.10`) with a **warn-only** card-to-version matrix check (`compatibilityWarnings` logged to `stderr` + `glide-ng.log` without blocking explicit overrides).
  - Fixed real capabilities & resolution bugs verified against 3dfx `sst1`/`cvg`/`h3`/`h5` SDKs: `GR_MAX_TEXTURE_SIZE` (`256` on Voodoo3, `2048` on Voodoo4/5 only), `GR_BITS_RGBA` (16-byte `{5,6,5,0}`/`{5,5,5,1}`/`{8,8,8,8}` + `GR_BITS_DEPTH` `16`/`32`), `grQueryResolutions` (returns byte size `count * sizeof(GrResolution)` with authentic per-card & VRAM/SLI filtering), authentic H3 `fbiRev` encoding (`0x1002` Banshee, `0x11002` Voodoo3+) in Glide 2 `grSstQueryHardware`, additional `grGet` capabilities (`GR_MEMORY_UMA`, `GR_NUM_FB`, `GR_REVISION_FB`/`TMU`, `GR_SUPPORTS_PASSTHRU`, `GR_TEXTURE_ALIGN`, `GR_STATS_*`), wired `grTBufferWriteMaskExt` (Glide 2) and `grReset` (Glide 3), and added `grSetNumPendingBuffers` (`3.10` export parity).
  - Created authoritative binary-verified reference [docs/glide_versions_and_cards.md](../glide_versions_and_cards.md) and synced [docs/glide_device_and_version_gating.md](../glide_device_and_version_gating.md).
  - **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`

### M10 End-to-End Automated Verification (`M10.1`–`M10.7`)
- **Builds**: 0 compiler warnings across `src/`, `tools/`, and `include/` in Linux Debug + Open Watcom DOS/32 (`build/`), Linux Release (`build_release/`), and Win32 MinGW-w64 `i686` Release (`build_win32/`); verified Win32 PE exports (`glide2x.dll` 305 exports — `grGetString`/`grGetProcAddress` absent; `glide3x.dll` 259 exports — `grSetNumPendingBuffers` + all VSA-100 procs present).
- **Full 8/8 Catch2 Test Matrix**: `Glide2xWrapperTests` (**39/39 cases, 2,824 assertions**) and `Glide3xWrapperTests` (**60/60 cases, 4,019 assertions**) passed 100% across `software` SIMD, `software` scalar (`GLIDE_NG_SW_SIMD=0`), `opengl_es`, and `vulkan`.
- **Diagnostic Matrix**: `build_release/bin/glide2_info -v` and `build_release/bin/glide3_info -v` verified across all 7 card models (`voodoo1`, `rush`, `voodoo2`, `banshee`, `voodoo3`, `voodoo4`, `voodoo5`) with authentic bare versions, pure Glide 2.x output, H3 `fbiRev` decoding, `GR_MAX_TEXTURE_SIZE`/`GR_BITS_RGBA`, card/VRAM-filtered `grQueryResolutions`, warn-only ahistorical check, and 100% alignment between advertised `GR_EXTENSION` tokens and `grGetProcAddress` resolution.

---

## 12. Milestone M11: Post-M10 / VSA-100 Benchmarking, Cross-Backend Optimization & Software AVX-512 (`perf/m11-vsa100-bench-simd-avx512`)

### Goals
1. Expand `glide2_bench` and `glide3_bench` to cover M10 / VSA-100 features (`fog`, `multitex`, `lfb_read`, `aa_tbuffer`, `stencil`, `ext_combine`, `texfmt`, `rtt`) plus high-res (`1280`, `1600`) and `--aa` options while preserving apples-to-apples Core 6-scene GlideMark comparability.
2. Eliminate remaining scalar islands in the Software backend and shared CPU routines (`CpuLfbStaging`, `TextureManager`, 2×2 SSAA resolve, RAMDAC Gamma LUT, masked/T-Buffer clears, Hardware Stencil, Table Fog) with bit-exact / zero-regression verification (explicitly guarding fog rounding accuracy).
3. Introduce clean ISA-isolated runtime SIMD dispatch (`GLIDE_NG_SW_SIMD=avx2|avx512|scalar|auto`, default `avx2`) and a 4×4 (16-pixel) **AVX-512** (`avx512f/dq/bw/vl`) Software backend tuned for Zen 4, plus a repeatable Zen 4 verification & benchmark guide.
4. Implement real-GPU optimizations for Vulkan and OpenGL ES (never over-optimizing for `llvmpipe`): zero-readback GPU-side `TEXTUREBUFFER` RTT and hardware BC1/BC2/BC3 (`DXT1`/`DXT3`/`DXT5`) compressed texture upload, plus collect performance notes comparing GPU vs. SW paths for future reference.

### M11 Task Breakdown (Small, Independently Verified Iterations)
- **`M11.1`** — Expand `glide2_bench` & `glide3_bench` with new classic & VSA-100 scenes (`fog`, `multitex`, `lfb_read`, `stencil`, `ext_combine`, `texfmt`, `rtt`, `tbuffer`), `--aa`, `--res 1280|1600`, and `--scene core|extended|vsa100|all`; capture pre-optimization baseline report.
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M11.2`** — Runtime SIMD dispatch architecture (`SwSimdLevel`: `avx2` default, `avx512`, `scalar`, `auto`) and strict CMake per-TU ISA isolation (moving `-mavx2 -mfma` off baseline files; adding `-mavx512f -mavx512dq -mavx512bw -mavx512vl` TUs).
  - **Status**: `[ ] PENDING`
- **`M11.3`** — SIMD-accelerate shared CPU & post-process routines (`SimdKernels`: 2×2 SSAA resolve, RAMDAC Gamma LUT, masked/T-Buffer clears, `CpuLfbStaging` RGB565/1555 conversions, `TextureManager` P8/565/1555/4444 swizzlers) across AVX2 + AVX-512 + scalar with bit-exact Catch2 unit tests.
  - **Status**: `[ ] PENDING`
- **`M11.4`** — Eliminate scalar islands in the Software AVX2 rasterizer: vectorize Hardware Stencil (`PIXEXT`) and Table Fog (exact boundary-verified reciprocal index + precomputed float LUT) + inline ext-combiner fast paths, with exhaustive fog & stencil 0-diff parity tests.
  - **Status**: `[ ] PENDING`
- **`M11.5`** — Implement 4×4 (16-pixel) **AVX-512** Software rasterizer (`SimdVec512.h`, templated `SoftwareRasterizerSimdImpl.inl`, `SoftwareRasterizerAvx512.cpp`) tuned for Zen 4 (32 ZMM, native `__mmask16`, in-register 64-entry fog LUT), plus Win32 `i686` & Linux compile verification and cross-ISA parity test harness.
  - **Status**: `[ ] PENDING`
- **`M11.6`** — Real-GPU Vulkan & OpenGL ES M10 optimizations: zero-readback GPU-side `TEXTUREBUFFER` RTT and hardware BC1/BC2/BC3 (`DXT1`/`DXT3`/`DXT5`) texture upload with automatic CPU fallback, plus GPU shader audit & perf notes.
  - **Status**: `[ ] PENDING`
- **`M11.7`** — Full regression verification (Linux Debug/Release, DOS/32 Open Watcom, Win32 MinGW `i686`, all Catch2 suites, targeted visual checks on demos/tests) + post-M11 benchmark comparison & Zen 4 AVX-512 test guide (`docs/perf_m11_analysis.md`).
  - **Status**: `[ ] PENDING`

---

## 13. Upcoming Milestones (High-Level Scope — Detailed Just-In-Time)

### Milestone M12 — Flagship Standalone 3dfx Demos (`feat/m12-shrine-torus-demos`) *(Pending)*
- Build two standalone, authentic Glide 2.x/3.x showcase tools targeting Linux, DOS/32, and Win32:
  - **`glide_shrine` (*Voodoo Temple*)**: Two-pass `GR_FOG_ADD2`/`MULT2` + `PREFOG_COLOR` lightmapped temple hall with mipmapped pillars, exponential fog, and additive torch sprites.
  - **`glide_torus` (*3dfx Reflective Hall*)**: Multi-texturing (`TMU0 + TMU1` environment-mapped specular chrome) rotating torus knot with translucent floor reflections, dynamic colored lights, and smooth camera orbit.

### Backlog (Future / Low Priority) — Clean-Room Open-Source `glide2x.ovl`
- Build an open-source Open Watcom v2 DOS/32 `glide2x.ovl` implementing the 130-entry (`0..129`) DOSBox I/O-port bridge ABI (removing dependency on prebuilt binary OVLs), with optional future paired DOSBox shared-memory ring-buffer protocol for higher DOS frame rates.
