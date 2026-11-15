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
| **M1** | `feat/m1-config-envvars` | Unified `GLIDE_NG_*` config & env var overhaul (no `GLIDE_WRAPPER_*` aliases) + Phase-1 authentic `FX_GLIDE_*` vars (`NO_SPLASH`, `SHAMELESS_PLUG`, `SWAPINTERVAL`) + authoritative env var reference doc | Low–Med | `[~] IMPLEMENTED — AWAITING USER VERIFICATION` | — |
| **M2** | `feat/m2-logging-perf-trace` | Tiered logging (`OFF`..`TRACE`), category filtering, frame-windowed tracing, microsecond timestamps, buffered flushing, and runtime per-frame perf breakdown (+ CSV perf log) | Med | `[ ] PENDING` | — |
| **M3** | `feat/m3-hud-fps` | DXVK-style on-screen HUD (`GLIDE_NG_HUD=fps`, extensible) rendered cleanly in each backend's post-scene presentation pass (Vulkan, GLES, SW) | Med | `[ ] PENDING` | — |
| **M4** | `fix/m4-parked-sdk-tests` | Fix parked official SDK tests: `test16` ('P' plug state leak on VK/GLES), `test22` (SW multi-pass fog quality vs VK), `test25` & `test27` (hangs in all modes) + Catch2 regressions | Med | `[ ] PENDING` | — |
| **M5** | `fix/m5-carmageddon` | Carmageddon (`carma`) compatibility & performance: black menus, missing in-game UI/HUD, perf bottleneck, initial focus/input behavior (using M2 traces & M3 HUD) | Med–High | `[ ] PENDING` | — |
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
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M1.2`** — Overhaul `WrapperConfig.{h,cpp}`, `JsonConfigLoader.cpp`, and `Logger::LogExecutionSummary`:
  - Add unified fields to `WrapperConfig`: `logFile`, `vsyncStrictFifo`, `swapInterval`, `noSplash`, `shamelessPlugMode` (`-1` app / `0` off / `1` on), `swThreads`, `swUseSimd`, `swTileSize`, `swAaMode`, `vkCachePath`, `glesVboSizeMb`, plus value-source tracking (`default` / `json` / `fx_env` / `ng_env`) for startup diagnostics.
  - Implement clean precedence in `JsonConfigLoader::Load()`: `Defaults < JSON < FX_GLIDE_* < GLIDE_NG_*`.
  - Support `GLIDE_NG_CONFIG` for explicit config file override, `GLIDE_NG_LOG_LEVEL` accepting both integers (`0`–`5`) and names (`off`, `error`/`crit`, `warn`, `info`, `debug`, `trace`), and `GLIDE_NG_PRESENTATION_FILTER` accepting `nearest`/`0` and `linear`/`1`.
  - Remove duplicate env parsing from `EmulationRegistry` constructor and ensure `EnsureEngineInitialized()` loads config identically to `InitGlide()`.
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M1.3`** — Remove all scattered `std::getenv()` calls from backends & wire authentic Phase-1 `FX_GLIDE_*` vars:
  - `SoftwareBackendBase.cpp`: read `swAaMode` from `WrapperConfig`.
  - `SoftwareBackend.cpp`: read `vsyncStrictFifo`, `swUseSimd`, `swThreads`, `swTileSize` from `m_config`.
  - `VulkanBackend.cpp`: read `vsyncStrictFifo` and `vkCachePath` from `m_config`.
  - `OpenGLESBackend.cpp`: read `vsyncStrictFifo` and `glesVboSizeMb` from `m_config`.
  - `GlideEntryCommon.{h,cpp}`, `glide2_entry.cpp`, `glide3_entry.cpp`: honor `noSplash` in `RenderSplash()`, honor `shamelessPlugMode` in `ShouldRenderShamelessPlug(appEnabled)`, use `config.logFile` in `Logger::Initialize()`.
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M1.4`** — Migrate `tests/`, `tools/`, `demos/`, and `.agents/` from `GLIDE_WRAPPER_*`/legacy `GLIDE_*` to `GLIDE_NG_*` + add Catch2 unit tests covering:
  - Full `GLIDE_NG_*` parsing & JSON parity
  - Precedence (`Defaults < JSON < FX_GLIDE_* < GLIDE_NG_*`)
  - `FX_GLIDE_NO_SPLASH`, `FX_GLIDE_SHAMELESS_PLUG`, and `FX_GLIDE_SWAPINTERVAL` behavior
  - Rejection/ignoring of removed legacy names (confirming clean break)
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M1.5`** — Overhaul [docs/glide_environment_variables.md](../glide_environment_variables.md) into the authoritative configuration & env var reference (glide-ng settings, authentic Glide vars, defaults, status tags, and roadmap).
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **`M1.6`** — Full verification (`cmake --build build` with 0 `-Werror` warnings, Catch2 suites across `software`/`vulkan`/`opengl_es`, 6-cell `glide_embed_test` smoke matrix, privacy check) + commit & push.
  - **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`

---

## 3. Upcoming Milestones (High-Level Scope — Detailed Just-In-Time)

### Milestone M2 — Tiered Logging & Runtime Perf Tracing (`feat/m2-logging-perf-trace`)
- 6 verbosity levels (`OFF`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`), category bitmask (`api,lfb,tex,state,draw,wsi,perf`), frame-windowed trace (`GLIDE_NG_TRACE_FRAMES`), microsecond monotonic timestamps + frame ID, buffered file flushing.
- Always-compiled lightweight per-frame perf counters (`render_ms`, `swap_wait_ms`, draws, tris, LFB locks R/W & bytes, tex uploads, PSO builds), periodic summary logging, and optional CSV dump (`GLIDE_NG_PERF_LOG`).

### Milestone M3 — DXVK-Style On-Screen HUD (`feat/m3-hud-fps`)
- `GLIDE_NG_HUD` (and JSON `"hud"`) supporting `fps` first (with `scale=N`, `position=tl|tr|bl|br`), extensible to `frametimes`, `backend`, `api`, `drawcalls`, `lfb`.
- Crisp embedded bitmap font composited in each backend's post-scene presentation pass (Vulkan, GLES, SW) with zero Glide state mutation.

### Milestone M4 — Parked Official SDK Test Fixes (`fix/m4-parked-sdk-tests`)
- `test16`: fix `RenderBanner()` / `grGlideGetState`/`grGlideSetState` color/state leak on Vulkan & GLES.
- `test22`: fix Software backend multi-pass fog (`GR_FOG_ADD2`/`MULT2`, `GR_BLEND_PREFOG_COLOR`) quality parity vs Vulkan.
- `test25` & `test27`: diagnose hang with M2 tracing and fix across all backends.

### Milestone M5 — Carmageddon (`carma`) Compatibility & Performance (`fix/m5-carmageddon`)
- Capture M2 `LFB,API,PERF` logs & CSV traces from menus and race.
- Fix black menus, missing in-game UI/HUD, performance bottleneck, and initial focus/input quirk; validate with M3 HUD across all backends.

### Milestone M6 — Optional DOS Diagnostics & GPU-Bound Benchmark (`feat/m6-dos-tools-bench`)
- Optional Open Watcom v2 toolchain detection (`vendor/watcom` or `$WATCOM`); never blocks Linux builds when absent.
- Dual Linux + DOS (`glide_info` / `glide_info.exe`) hardware/config reporter.
- Dual Linux + DOS (`glide_bench` / `glide_bench.exe`) low-CPU, Glide/GPU-bound visual benchmark (fillrate/overdraw, triangle throughput, multi-pass+fog, LFB write bandwidth) with in-Glide on-screen FPS & diagnostics HUD.
