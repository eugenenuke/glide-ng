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
| **M5** | `fix/m5-carmageddon` | Carmageddon (`carma`) compatibility & performance: black menus, missing in-game UI/HUD, perf bottleneck, initial focus/input behavior + deferred GPU state-dedup & batching optimizations | Med–High | `[~] IMPLEMENTED — AWAITING USER VERIFICATION` | — |
| **M6** | `feat/m6-dos-tools-bench` | Optional Open Watcom v2 DOS cross-build (`vendor/watcom`) + dual Linux/DOS `glide_info` + GPU/Glide-bound active visual benchmark (`glide_bench`) with in-Glide on-screen FPS & diagnostics | High | `[ ] PENDING` | — |
| *(Future)* **M7** | `feat/m7-glide-envvars-p2` | Phase-2 authentic `FX_GLIDE_*` / `SST_*` expansion (`FX_GLIDE_FBRAM`, `FX_GLIDE_TMU_MEMSIZE`, `SSTV2_NUM_TMUS`, `SST_GAMMA`, `FX_GLIDE_IGNORE_REOPEN`, `FX_GLIDE_NO_DITHER_SUB`, `FX_GLIDE_BOUNDS_CHECK`, `FX_SNAPSHOT`) | Low | `[ ] DEFERRED` | — |

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

### M5 Task Breakdown

- **`M5.1`** — Persistent LFB Write-Locks & Sparse Alpha-Masked GPU Overlay Compositing (`LfbManager.{h,cpp}`, `CpuLfbStaging.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `GlideSplashAnimator.cpp`, `VulkanLfb.cpp`, `VulkanBackend.{h,cpp}`, `VulkanDraw.cpp`, `OpenGLESBackend.{h,cpp}`, `OpenGLESDraw.cpp`, `OpenGLESShaders.cpp`):
  - Mask `GR_LFB_NOIDLE` (`type & 0x0Fu`) in `LfbManager`; separate `WriteLockState` from `ReadLockState`; initialize write-only staging buffers with a sentinel color instead of synchronous GPU `ReadLFBData()`; add lock-free `FlushActiveWriteLock()` invoked in `grBufferSwap` before `grShamelessPlug` and `SwapBuffers()`, resetting staging to sentinel in-place without reallocating the guest pointer; add `LfbManager::Reset()` on window close / shutdown.
  - Refactor `GlideSplashAnimator::RenderBanner()` to write directly via `backend->WriteLFB(...)` using a one-time flipped static 180×90 buffer — never touching `grLfbLock`/`grLfbUnlock` or Glide pipeline state.
  - Fix `CpuLfbStaging` `GR_BLEND_*` enum factors, chromakey matching, and negative `srcStride`; add sparse alpha-masked overlay tracking (`WriteOverlayLFB` with Porter-Duff `SRC OVER DST` stacking and tight `[minY..maxY]` bounds).
  - Implement zero-readback, zero-`waitIdle()` sparse LFB overlay compositing in both **Vulkan** (`VulkanLfb.cpp` reusing `hud_vert`/`hud_frag` SPIR-V, `eB8G8R8A8Unorm`, `m_renderPass`, per-frame staging ring, flushing both FRONT and BACK buffers after `FlushBatch()`) and **OpenGL ES** (`OpenGLESBackend.cpp` + `uPreserveAlpha` uniform in `OpenGLESShaders.cpp`, fixing both sparse LFB overlay and HUD alpha blending without breaking Wayland window opacity).
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M5.2`** — Lazy Palette/NCC Texture Swizzling & Deferred GPU State-Dedup / Batching (`TextureManager.{h,cpp}`, `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`, `GlideSplashAnimator.cpp`, `VulkanBackend.{h,cpp}`, `VulkanDraw.cpp`, `VulkanTextures.cpp`, `OpenGLESBackend.cpp`, `OpenGLESDraw.cpp`, `OpenGLESTextures.cpp`):
  - Replace eager per-palette re-uploads in `TextureManager::LoadTable()` with FNV-1a table hashing, identical-table `memcmp` early-out, and lazy per-TMU dirty flags (`PrepareTextureForBind` / `SyncBoundTextures`), so only bound textures drawn with a changed palette are re-swizzled and uploaded.
  - Deduplicate all ~23 state/texture setters in `OpenGLESBackend` against `GlideStateTracker` before `FlushBatch(true)`, and fix the mid-batch `m_stateCacheDirty` split bug in `OpenGLESDraw.cpp`.
  - Add cached active pipeline & descriptor resolution (`ResolveDrawState_Locked`), per-frame double-buffered vertex buffer slots (eliminating wrap `waitIdle()` stalls), and fence-synchronized deferred retirement queues (`m_retiredTextures[2]`, `m_retiredDescriptorSets[2]`) in `VulkanBackend` / `VulkanDraw` / `VulkanTextures`.
  - Hoist redundant per-face `grAlphaBlendFunction` out of the face loop in `GlideSplashAnimator::DrawFaces`.
  - Verified with M2 `GLIDE_NG_PERF_LOG` on `splash_showcase_demo`: reduced draw calls from ~1,700/frame to **13/frame** on both Vulkan (`9.39 ms` median) and OpenGL ES (`8.29 ms` median) on `llvmpipe`.
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M5.3`** — Automated `[M5]` Regressions & Full Verification Matrix (`tests/test_architecture.cpp`):
  - Added `[Glide2][M5]` Catch2 suites (`M5.1` persistent LFB write-lock menus, 3D + sparse HUD + Shameless Plug coexistence with in-band 3D preservation check, `GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE`; `M5.2` lazy `TextureManager` palette switching & state-setter deduplication).
  - Updated 3 stale `test16` shameless-plug golden values in `tests/test_architecture.cpp` to match authentic 3dfx `gbanner.c` `GR_BLEND_SRC_ALPHA` (`90/255`) translucency after fixing the `CpuLfbStaging` blend-enum bug.
  - Verified clean build (0 `-Werror` warnings), `Glide2xWrapperTests` 100% pass across `software`/`vulkan`/`opengl_es`, `Glide3xWrapperTests` 45/46 (+ `[WSI][NativeSurfaceHost]` 64/64 with `SDL_VIDEODRIVER=dummy`), 6-cell `child_surface_embed_test` matrix across X11 & Wayland, and privacy scan (0 hits).
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`

---

## 7. Upcoming Milestones (High-Level Scope — Detailed Just-In-Time)

### Milestone M6 — Optional DOS Diagnostics & GPU-Bound Benchmark (`feat/m6-dos-tools-bench`)
- Optional Open Watcom v2 toolchain detection (`vendor/watcom` or `$WATCOM`); never blocks Linux builds when absent.
- Dual Linux + DOS (`glide_info` / `glide_info.exe`) hardware/config reporter.
- Dual Linux + DOS (`glide_bench` / `glide_bench.exe`) low-CPU, Glide/GPU-bound visual benchmark (fillrate/overdraw, triangle throughput, multi-pass+fog, LFB write bandwidth) with in-Glide on-screen FPS & diagnostics HUD.
