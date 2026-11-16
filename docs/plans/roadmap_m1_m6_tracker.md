# glide-ng Roadmap & Milestone Tracker (M1–M6)

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
| **M6** | `feat/m6-dos-tools-bench` | Telemetry `app_cpu_ms`/`wrapper_cpu_ms` breakdown + optional Open Watcom v2 DOS cross-build (`vendor/watcom`) + dual Linux/DOS `glide_info` + GPU/Glide-bound active visual benchmark (`glide_bench`) | High | `[~] IN PROGRESS (M6.1 IMPLEMENTED)` | — |
| **M7** | `perf/m7-profiling-optimization` | Systematic performance profiling & optimization using M6 telemetry + `glide_bench` (Linux & DOSBox) and Carmageddon dense-scene traces across Vulkan, OpenGL ES, and Software | Med–High | `[ ] PENDING` | — |
| *(Future)* **M8** | `feat/m8-glide-envvars-p2` | Phase-2 authentic `FX_GLIDE_*` / `SST_*` expansion (`FX_GLIDE_FBRAM`, `FX_GLIDE_TMU_MEMSIZE`, `SSTV2_NUM_TMUS`, `SST_GAMMA`, `FX_GLIDE_IGNORE_REOPEN`, `FX_GLIDE_NO_DITHER_SUB`, `FX_GLIDE_BOUNDS_CHECK`, `FX_SNAPSHOT`) | Low | `[ ] DEFERRED` | — |

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
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M6.2`** — Optional Open Watcom v2 DOS Cross-Build & Dual Linux/DOS `glide_info` *(design brainstorm pending with user)*:
  - **Status**: `[ ] PENDING`
- **`M6.3`** — Dual Linux/DOS GPU-Bound Visual Benchmark (`glide_bench`) *(design brainstorm pending with user)*:
  - **Status**: `[ ] PENDING`

---

## 8. Upcoming Milestones (High-Level Scope — Detailed Just-In-Time)

### Milestone M7 — Performance Profiling & Optimization (`perf/m7-profiling-optimization`)
- Profile `glide_bench` (Linux vs. DOSBox) and Carmageddon dense scenes using M6 `app_cpu_ms`/`wrapper_cpu_ms` CSV traces and host CPU profiling (`perf`).
- Target confirmed wrapper CPU/GPU bottlenecks across Vulkan, OpenGL ES, and Software with before/after benchmark and Carmageddon CSV proof.
