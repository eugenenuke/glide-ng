# Option A (Unpatched Native Child Surface) & Codebase Remediation Tracker

> **Authoritative Execution & Tracking Document for AI Agents and Developers**
>
> **Strategy**: Start with **Option A (Unpatched DOSBox + Native Child Surface)** across Vulkan, OpenGL ES, and Software backends, preceded by safe, low-risk foundational improvements and `.agents/` skill updates. All work is broken into small, independently buildable, reversible tasks so regressions are easy to isolate and revert. **Option B** (thin ~50 LOC DOSBox client patch) is deferred to an explicit post-Option-A decision gate (Phase 4).

---

## 0. Mandatory Agent Protocol (READ BEFORE STARTING ANY TASK)

Every AI agent working from this tracker **MUST** follow these rules without exception:

1. **Strict 3-State Task Lifecycle — NEVER Mark `DONE` Without User Confirmation**:
   - `[ ]` **`PENDING`** — Not started.
   - `[~]` **`IMPLEMENTED — AWAITING USER VERIFICATION`** — Code implemented, builds cleanly, automated tests pass. **Agents MUST stop at this state for each user-verified task/phase and ask the user to validate.**
   - `[x]` **`DONE (USER CONFIRMED — YYYY-MM-DD)`** — **ONLY** set this after the user explicitly confirms the behavior works as intended. Never self-confirm visual, windowing, or DOSBox gameplay tasks.
2. **One Task = One Isolated, Reversible Git Commit**:
   - Never bundle unrelated refactors into a windowing commit, and never bundle two backends into the same commit.
   - Every task must leave `cmake --build build -j$(nproc)` green and must not regress the automated baseline recorded in Section 1.
   - If a task breaks something, revert/bisect that single commit instead of layering workarounds on top.
3. **Never Modify Pristine Reference Trees**:
   - Do **not** edit files inside `refs/` or `ext_tests/`.
   - Do **not** delete or revert uncommitted user files (`docs/glide_environment_variables.md`, `docs/env_vars_research_report.md`, `vk_perf_impacted.patch`, etc.) or prune git branches without explicit user approval (see `T0.4`).
4. **Update This Tracker In-Place**:
   - After finishing a task, update its checkbox in the Master Progress Table and its task block (`Status`, `Commit`, `Test Output`, `Notes`), then hand control back to the user with the exact manual verification command(s).

---

## 1. Verified Environment & Baseline Snapshot

Recorded before any edits are made:

- **Host Session**: Wayland (`XDG_SESSION_TYPE=wayland`, `WAYLAND_DISPLAY=wayland-0`, `DISPLAY=:0`), Mesa `llvmpipe (LLVM 21.1.8)`.
- **Verified System Libraries (`pkg-config`)**:
  - `wayland-client 1.24.0`, `wayland-egl 18.1.0`, `egl 1.5`, `glesv2 3.2`
  - `x11 1.8.13`, `xext 1.3.4` (`X11/extensions/shape.h`, `X11/extensions/XShm.h`), `xfixes 6.0.0`
  - `vulkan 1.4.341` (`VK_KHR_xlib_surface`, `VK_KHR_wayland_surface`, `VK_EXT_external_memory_dma_buf`)
  - `sdl2 2.32.72` (`sdl2-compat` → `libSDL3`), `libSDL-1.2` (`sdl12-compat`)
  - *Note*: `x11-xcb` is **not** installed — use Xlib (`X11::X11` + `X11::Xext` + `VK_KHR_xlib_surface` + `EGL_PLATFORM_X11_EXT`), not XCB.
- **Verified Automated Test Baseline**:
  - `GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide3xWrapperTests`: **PASS (31/31 test cases, 1771/1771 assertions)**. *(Currently prints noisy `libEGL`/`MESA ZINK` warnings because `SoftwareBackend::AttachWindow` ignores `forceNoWindow` — fixed in `T0.2`.)*
  - `GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide2xWrapperTests`: **24/27 passed, 3 pre-existing failures** (`test_architecture.cpp:3187` plus hardcoded-Vulkan LFB/logo subtests `test11`/`test26`/`test16`; default Vulkan run also sees a timing flake at `test_architecture.cpp:466` `elapsedMs <= 350` on `llvmpipe`).
  - `ctest` from `build/` currently reports `No tests were found` because root `CMakeLists.txt` is missing `enable_testing()` (fixed in `T0.1`).
  - `tests/run_all_tests.py` currently points to obsolete `./build/bin/GlideWrapperTests` instead of `Glide2xWrapperTests` + `Glide3xWrapperTests` (fixed in `T0.1`).

---

## 2. Master Progress Dashboard

| ID | Phase | Task Summary | Risk | Status | Commit |
| :--- | :--- | :--- | :---: | :--- | :--- |
| **T0.1** | Phase 0 | Fix `ctest` discovery (`enable_testing()`), update `tests/run_all_tests.py`, lock baseline | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `dd527a8` |
| **T0.2** | Phase 0 | Fix `SoftwareBackend::AttachWindow` ignoring `forceNoWindow` & `m_sdlRendererOwned` leak | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `5e097b8` |
| **T0.3** | Phase 0 | Audit & update `.agents/AGENTS.md`, existing 5 skills, add `wsi-window-injection` & `build-and-test` skills | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `415d0a1` |
| **T0.4** | Phase 0 | User-approved repo hygiene (`.gitignore`, untracked logs/caches, stale `subagent-*` branches) | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `133e898` |
| **T1.1** | Phase 1 | Remove `[DEBUG-GLES]` `std::cout` spam & decouple `Logger.cpp` from `<vulkan/vulkan.hpp>` | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `c477e39` |
| **T1.2** | Phase 1 | Deduplicate `color.frag` / `color_dualsrc.frag` via `-DUSE_DUAL_SRC_BLEND`; delete stale checked-in `.spv.h` | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `5f3bc86` |
| **T1.3** | Phase 1 | Fix `VulkanBackend::SwapBuffers` semaphore wait stage (`eColorAttachmentOutput` → `eTransfer`) | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `72495ce` |
| **T1.4** | Phase 1 | Stop GPU backends from calling `SoftwareBackendBase::ClearBuffer(...)` every frame | Low–Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `c87df26` |
| **T1.5** | Phase 1 | Replace fragile substring matching in `JsonConfigLoader.cpp` with key-scoped parser + Catch2 tests | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `d8b7d03` |
| **T1.6** | Phase 1 | Add `wayland-egl`/`egl` CMake discovery & guard `#if defined(UNIX)` trap until T2.7 | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `cad7d0d` |
| **T1.7** | Phase 1 | Prefer `VK_PRESENT_MODE_MAILBOX_KHR` on Wayland VSync to fix Mesa `wp_fifo_v1` stutter + Catch2 tests | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `15482fc` |
| **T2.1** | Phase 2 | Implement `NativeSurfaceHost` core + Headless + Standalone SDL2 + `ComputePresentRect` aspect math + tests | Low | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `ae65f8e` |
| **T2.2** | Phase 2 | Implement X11 / XWayland native child window in `NativeSurfaceHost` (`XOpenDisplay`, `XCreateWindow`, `XShape`) + embed test harness | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `4f40687` |
| **T2.3** | Phase 2 | Implement Native Wayland `wl_subsurface` in `NativeSurfaceHost` + SVN DOSBox `setConfig('SDL2')` export + HiDPI scaling | Med–High | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `9865fc5` |
| **T2.4** | Phase 2 | Migrate `VulkanBackend` to `NativeSurfaceHost` + aspect-ratio preserving letterbox/pillarbox blit | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `2d75259`, `985e7fa`, `1d349ee` |
| **T2.5** | Phase 2 | Migrate `OpenGLESBackend` to `NativeSurfaceHost` (Direct EGL) + aspect-ratio & Gamma LUT presentation pass | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `df40756`, `8c416e2` |
| **T2.6** | Phase 2 | Migrate `SoftwareBackend` to `NativeSurfaceHost` + aspect-ratio preserving presentation | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `b0066fd`, `47c21b4` |
| **T2.7** | Phase 2 | Retire old `PlatformWindowUtils` overlay code & late `XInitThreads()` constructor; run full Option A DOSBox matrix | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `ce4a354` |
| **T3.1** | Phase 3 | Extract `GlideStateTracker` from `SoftwareBackendBase` (remove CPU buffers from Vulkan/GLES inheritance) | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `1edabe4` |
| **T3.2** | Phase 3 | Deduplicate shared frontend code across `glide2_entry.cpp` & `glide3_entry.cpp` (`GlideEntryCommon`) | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `4ea66b7` |
| **T3.3** | Phase 3 | Split monolithic backend `.cpp` files (`VulkanBackend`, `OpenGLESBackend`, `SoftwareBackend`) into cohesive units | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `f857e49`, `1765199`, `4f00b30` |
| **T3.4** | Phase 3 | Audit & reduce `std::recursive_mutex` hot-path contention + benchmark regression check | Med | `[x] DONE (USER CONFIRMED — 2026-11-15)` | `4d64eeb` |
| **T4.0** | Phase 4 | **Decision Gate**: Evaluate Option A real-world behavior vs. triggers for Option B (~50 LOC DOSBox patch) | Gate | `[x] CLOSED — NOT NEEDED (USER CONFIRMED — 2026-11-15)` | — |

---

## 3. Detailed Phase & Task Specifications

### Phase 0: Guardrails, Test Harness Reliability & Repo Hygiene

> **Goal**: Establish a trustworthy, fast, quiet test loop and accurate agent skills *before* touching rendering or windowing code.

#### `T0.1` — Fix CTest Discovery, Repair `tests/run_all_tests.py`, and Lock Baseline
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `dd527a8`)
- **Automated Result**: `ctest --test-dir build -N` now discovers **60 tests**; `python3 tests/run_all_tests.py --suite g3 --backend sw` passed (1/1, 7.83s).
- **Target Files**:
  - `CMakeLists.txt`
  - `tests/run_all_tests.py`
- **What to Do**:
  1. Add `enable_testing()` in root `CMakeLists.txt` before `add_subdirectory(tests)` so `ctest --test-dir build --output-on-failure` discovers both `Glide2xWrapperTests` and `Glide3xWrapperTests`.
  2. Update `tests/run_all_tests.py` to execute both `Glide2xWrapperTests` and `Glide3xWrapperTests` (with `GLIDE_FORCE_NO_WINDOW=1`), instead of looking for the obsolete `GlideWrapperTests` binary.
  3. Record exact passing/failing test counts per backend (`software`, `vulkan`, `opengl_es`) in this tracker (do not hide pre-existing failures; document them clearly so regressions are obvious).
- **Automated Gate**:
  - `cmake -S . -B build && cmake --build build -j$(nproc)`
  - `GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ctest --test-dir build -R ".*" --output-on-failure` (verify tests are actually discovered and executed).
- **User Verification**: Confirm `ctest` and `python3 tests/run_all_tests.py --backend sw` work cleanly from repo root.

#### `T0.2` — Fix `SoftwareBackend::AttachWindow` Headless Guard & Renderer Leak
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `5e097b8`)
- **Automated Result**: `GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide3xWrapperTests` passed **32/32 test cases (1780 assertions)** including new `[Software][Headless]` test, with **0** `libEGL`/`MESA ZINK` warnings.
- **Target Files**:
  - `src/backends/software/SoftwareBackend.cpp` (L746–834)
  - `tests/test_glide3.cpp` (add headless attach/detach check)
- **What to Do**:
  1. At the start of `SoftwareBackend::AttachWindow`, honor `m_config.forceNoWindow` (matching `VulkanBackend` and `OpenGLESBackend`): allocate CPU buffers and set `m_headlessMode = true; m_windowAttached = true;` without calling `SDL_Init`, `SDL_CreateWindow`, or `SDL_CreateRenderer`.
  2. In the standalone windowed fallback path (`SoftwareBackend.cpp:814`), set `m_sdlRendererOwned = true;` when creating `m_sdlRenderer` so `DetachWindow()` destroys it cleanly without leaking.
- **Automated Gate**:
  - `GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide3xWrapperTests` passes **31/31** with **zero** `libEGL` / `MESA: error: ZINK` stderr spam!
- **User Verification**: Confirm headless tests run quietly and standalone software demos (`GLIDE_WRAPPER_BACKEND=software ./build/bin/glide3_cube`) still open and close cleanly.

#### `T0.3` — Update `.agents/` Manifest & Skills, Reconcile `docs/GEMINI.md`
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `415d0a1`)
- **Notes**: Consolidated `.agents/AGENTS.md` (with `docs/GEMINI.md` pointing to it), updated all 5 existing skills, added `wsi-window-injection` and `build-and-test` skills, removed `.agents/` from `.gitignore`, and tracked `.agents/` in git.
- **Target Files**:
  - `.agents/AGENTS.md` (and `docs/GEMINI.md`)
  - `.agents/skills/3dfx-expert/SKILL.md`
  - `.agents/skills/gles-expert/SKILL.md`
  - `.agents/skills/vulkan-expert/SKILL.md`
  - `.agents/skills/rasterizer-oracle/SKILL.md`
  - `.agents/skills/visual-validator/SKILL.md`
  - *New*: `.agents/skills/wsi-window-injection/SKILL.md`
  - *New*: `.agents/skills/build-and-test/SKILL.md`
- **Exact Updates Required** (from `.agents/` audit):
  1. **`.agents/AGENTS.md` & `docs/GEMINI.md`**:
     - Make `.agents/AGENTS.md` the single authoritative manifest (replace `docs/GEMINI.md` body with a short pointer to `.agents/AGENTS.md` so they never drift again).
     - Fix broken binary/path names (`src/backends/opengl_es/`, `Glide2xWrapperTests`, `Glide3xWrapperTests`).
     - Embed the **3-State User Confirmation Protocol** (`PENDING` → `IMPLEMENTED — AWAITING USER VERIFICATION` → `DONE (USER CONFIRMED)`) and point agents to `docs/plans/option_a_and_cleanup_tracker.md`.
     - Add **Incremental Bisectable Commits** policy (one task per commit, no silent edits outside task scope).
     - Clarify with user whether `.agents/` should be removed from `.gitignore` (currently line 35 ignores `.agents/`) so skill improvements are version-controlled.
  2. **`.agents/skills/gles-expert/SKILL.md`**:
     - Fix wrong path `src/backends/gles/` → `src/backends/opengl_es/` and `GlesBackend.cpp` → `OpenGLESBackend.cpp`.
     - Add direct EGL surface guidance (`EGL_PLATFORM_X11_EXT` / `EGL_PLATFORM_WAYLAND_KHR` + `wl_egl_window_create`), context isolation from host DOSBox GLX/OpenGL contexts, and the Gamma LUT presentation quad requirement (never bypass `m_glProgram` with raw `glBlitFramebuffer`).
  3. **`.agents/skills/vulkan-expert/SKILL.md`**:
     - Add shader compilation workflow (`glslangValidator` + `tools/spv_to_hex.py` via CMake `CompileShaders`, single `color.frag` with `-DUSE_DUAL_SRC_BLEND`, no checked-in `.spv.h`).
     - Add WSI & swapchain rules (`VK_KHR_xlib_surface`, `VK_KHR_wayland_surface`, `eTransfer` wait stage for blit presentation, aspect-ratio letterbox/pillarbox blit, `OUT_OF_DATE_KHR` recreation).
  4. **`.agents/skills/3dfx-expert/SKILL.md`**:
     - Fix broken path `ext_tests/dosbox-0/` → actual reference locations (`refs/dosbox-x/src/hardware/glide.cpp` and `../dosbox/qemu-xtra/dosbox/dosbox-0/`).
     - Document `grSstWinOpen` `hWnd` contract (X11 Window ID vs `SDL_Window*` vs `0`) and SVN DOSBox's `setConfig('SDL2')` / `setConfigRes` ABI extension.
  5. **`.agents/skills/rasterizer-oracle/SKILL.md`**:
     - Clarify `SoftwareBackendBase` coupling (until `T3.1` separates `GlideStateTracker`), `forceNoWindow` headless behavior, and `NativeSurfaceHost` presentation.
  6. **`.agents/skills/visual-validator/SKILL.md`**:
     - Remove hardcoded UID `export XDG_RUNTIME_DIR=/run/user/794553` → use `${XDG_RUNTIME_DIR:-/run/user/$(id -u)}`.
     - Document Wayland vs XWayland screenshot caveat (`import -window` only captures X11/XWayland top-level windows, not native Wayland subsurfaces; document offline framebuffer/PPM dump or Wayland-safe capture when validating DOSBox injection).
     - Fix `glide3_banner.cpp` reference (verify actual file `src/core/GlideSplashAnimator.cpp`) and update target binaries (`Glide2xWrapperTests`, `Glide3xWrapperTests`).
  7. **New Skill — `.agents/skills/wsi-window-injection/SKILL.md`**:
     - Authoritative runbook for `NativeSurfaceHost`, X11 child windows (`XOpenDisplay`, `XCreateWindow`, `XShapeCombineRectangles` input passthrough, `XGetGeometry`), Wayland subsurfaces (private `wl_event_queue`, roleless `wl_surface` — NEVER `SDL_CreateWindow`, empty `wl_region`, `wl_subsurface_set_desync`, parent `wl_surface_commit`, HiDPI scale), DOSBox-X (`statWMInfo` quirk, `output=opengl` vs `output=surface`) & SVN DOSBox (`sdl12-compat`, `setConfig('SDL2')`), and explicit anti-patterns (`#if defined(UNIX)`, late `XInitThreads()`).
  8. **New Skill — `.agents/skills/build-and-test/SKILL.md`**:
     - Authoritative commands for CMake configure/build, Catch2 (`Glide2xWrapperTests`, `Glide3xWrapperTests`, `ctest`), `tests/benchmark.py`, running standalone demos, and launching DOSBox-X / SVN DOSBox with `LD_LIBRARY_PATH=build/lib` and `GLIDE_WRAPPER_BACKEND={vulkan,opengl_es,software}`.
- **User Verification**: User reviews updated `.agents/` files and decides whether `.agents/` should remain gitignored or be tracked in git.

#### `T0.4` — User-Approved Repository Hygiene
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `133e898`, backup branch `backup/wip-dirty-before-option-a` at `7493494`)
- **Notes**: Preserved dirty files (`docs/glide_environment_variables.md`, `docs/env_vars_research_report.md`, `vk_perf_impacted.patch`, deleted `glide_config.json`) on `backup/wip-dirty-before-option-a`; removed all 49 stale `subagent-*` worktrees and branches; gitignored untracked `refs/{dosbox-x,mesa,openglide,openglide-ng,wrappers}/` and `.glide-vulkan-cache.bin` (untracked from git); created clean feature branch `feature/option-a-native-child-surface` off `main`.
- **What to Do** (*ONLY with explicit user confirmation per item*):
  1. Decide fate of currently modified/untracked files: `docs/glide_environment_variables.md`, deleted `glide_config.json`, `docs/env_vars_research_report.md`, `vk_perf_impacted.patch`.
  2. Clean root-level runtime artifacts (`.glide-vulkan-cache.bin`, `*.log`, `tests/sw_validate.o`) and verify `.gitignore` covers `.glide-vulkan-cache.bin` and `*.spv.h` if appropriate.
  3. Ask user before pruning any of the ~48 stale `subagent-*` git branches.
- **User Verification**: User confirms `git status` and branch list look clean before Phase 1 begins.

---

### Phase 1: Safe Pre-Option-A Fixes & De-cluttering

> **Goal**: Remove noise, compiler-macro traps, shader duplication, and wasted CPU work so Phase 2 diffs stay laser-focused on windowing/presentation.

#### `T1.1` — Remove `[DEBUG-GLES]` Spam & Decouple `Logger` from Vulkan
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `c477e39`)
- **Automated Result**: Removed all 7 `[DEBUG-GLES]`/`[DEBUG-SHADER-ERROR]`/`[DEBUG-PROGRAM-ERROR]` `std::cout` prints from `OpenGLESBackend.cpp` and moved `Logger::LogHostVulkanCapabilities()` into `VulkanBackend.cpp` so `Logger.cpp` no longer includes `<vulkan/vulkan.hpp>`. `Glide3xWrapperTests` passed 32/32 (1780 assertions).
- **Target Files**:
  - `src/backends/opengl_es/OpenGLESBackend.cpp` (L141–238, L1633, L1657)
  - `src/core/Logger.cpp` (L2)
- **What to Do**:
  1. Remove or convert leftover `std::cout << "[DEBUG-GLES] ..."` statements in `OpenGLESBackend.cpp` to `GLIDE_LOG(DEBUG, "GLES", ...)`.
  2. Remove unused `#include <vulkan/vulkan.hpp>` from `src/core/Logger.cpp` (if any helper formats `VkResult`, move it into `src/backends/vulkan/`).
- **Automated Gate**: Clean build with zero new warnings + `Glide3xWrapperTests` passes 31/31.
- **User Verification**: Confirm clean console output on a GLES demo run (`GLIDE_WRAPPER_BACKEND=opengl_es ./build/bin/glide3_cube`).

#### `T1.2` — Single-Source Vulkan Fragment Shader & Remove Checked-In `.spv.h`
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `5f3bc86`)
- **Automated Result**: Deleted `color_dualsrc.frag` and stale checked-in `color_frag.spv.h` / `color_vert.spv.h`; guarded `outPreFogColor` in `color.frag` with `#ifdef USE_DUAL_SRC_BLEND`; updated `compile_shader` in `CMakeLists.txt` to pass `-DUSE_DUAL_SRC_BLEND`. Verified clean rebuild from wiped `build/generated_shaders` generates distinct `color_frag.spv.h` (170,749 B) and `color_dualsrc_frag.spv.h` (171,505 B); `Glide3xWrapperTests` passed 32/32 (1780 assertions).
- **Target Files**:
  - `src/backends/vulkan/shaders/color.frag`
  - Delete `src/backends/vulkan/shaders/color_dualsrc.frag`
  - Delete stale checked-in `src/backends/vulkan/shaders/color_frag.spv.h` and `src/backends/vulkan/shaders/color_vert.spv.h`
  - `CMakeLists.txt` (L45–76)
- **What to Do**:
  1. Guard the 2 dual-source output lines in `color.frag` with `#ifdef USE_DUAL_SRC_BLEND ... #else ... #endif`.
  2. Extend `compile_shader(...)` in `CMakeLists.txt` to accept optional `glslangValidator` `-D` defines and compile `color_dualsrc_frag.spv.h` from `color.frag` with `-DUSE_DUAL_SRC_BLEND`.
  3. Delete `color_dualsrc.frag` and the stale checked-in `src/backends/vulkan/shaders/*.spv.h` so builds always include `${CMAKE_BINARY_DIR}/generated_shaders/*.spv.h`.
  4. Verify `#include` directives in `VulkanShaders.h` resolve strictly to `generated_shaders/`.
- **Automated Gate**: Clean rebuild from fresh shader generation (`rm -rf build/generated_shaders build/*.spv && cmake --build build -j$(nproc)`) + `Glide3xWrapperTests` + Vulkan demo smoke test.
- **User Verification**: Confirm Vulkan dual-source & standard blending demos (`glide3_cube`, `fog_showcase_demo`) render identically.

#### `T1.3` — Fix Vulkan Swapchain Semaphore Wait Stage
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `72495ce`)
- **Automated Result**: Changed `waitStages` in `VulkanBackend::SwapBuffers()` from `vk::PipelineStageFlagBits::eColorAttachmentOutput` to `vk::PipelineStageFlagBits::eTransfer`. Clean rebuild + `Glide3xWrapperTests` passed 32/32 on SW and `[Glide3][Phase4]` passed (107 assertions) on Vulkan.
- **Target Files**:
  - `src/backends/vulkan/VulkanBackend.cpp` (L1376–1377)
- **What to Do**:
  1. Change `vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};` to `vk::PipelineStageFlagBits::eTransfer` where the swapchain presentation command buffer writes to the acquired image via `vkCmdBlitImage` / `vkCmdClearColorImage`.
- **Automated Gate**: Run Vulkan demo with validation layers enabled (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`) and verify zero sync/semaphore validation errors.
- **User Verification**: Confirm Vulkan windowed demo & GTA 1 show no flicker/tearing or validation errors.

#### `T1.4` — Eliminate Redundant CPU `ClearBuffer` on GPU Backends
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `c87df26`)
- **Automated Result**: Added lazy deferred CPU clear (`ScheduleLazyCpuClear`, `MaterializeLazyCpuClear`, `MaterializeLazyCpuDepthClear`, `DiscardLazyCpuClear`) in `SoftwareBackendBase` so `VulkanBackend::ClearBuffer` and `OpenGLESBackend::ClearBuffer` do zero per-frame CPU memory fills on pure 3D frames while materializing on demand before LFB writes/flushes and discarding on GPU readback. Added `[Software][LFB][LazyClear]` unit test; `Glide3xWrapperTests` passed **33/33 (1794 assertions)**, and `Glide2xWrapperTests "*LFB*"` on Vulkan & GLES matched baseline (4/6 passed, only the 2 pre-existing ABGR/BGRA swizzle failures).
- **Target Files**:
  - `src/backends/vulkan/VulkanBackend.cpp` (L1001)
  - `src/backends/opengl_es/OpenGLESBackend.cpp` (L394)
- **What to Do**:
  1. Inspect why `VulkanBackend::ClearBuffer` and `OpenGLESBackend::ClearBuffer` still invoke `SoftwareBackendBase::ClearBuffer(...)` (check if LFB readback/dirty flags or headless `test_architecture.cpp` rely on CPU shadow buffer clears).
  2. Remove the unconditional per-frame CPU buffer clear on GPU backends (keep only lightweight state/dirty-flag updates, or trigger CPU shadow sync lazily on `ReadLFB`).
- **Automated Gate**: `Glide2xWrapperTests` (no new failures beyond baseline), `Glide3xWrapperTests` (31/31 pass), plus `test11`/`test12`/`test26` LFB checks on Vulkan & GLES.
- **User Verification**: Confirm LFB-heavy demos (`test12_replica_demo`, GTA 1) render cleanly with improved frame pacing.

#### `T1.5` — Fix `JsonConfigLoader` Key-Scoped Parsing + Unit Tests
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `d8b7d03`)
- **Automated Result**: Replaced global substring matching in `JsonConfigLoader.cpp` with depth-aware, escape-safe key-scoped JSON extractors supporting `"cardModel"`/`"model"`, `"apiVersion"`, `"backend"`, `"logToConsole"`, `"vsync"`, `"forceNoWindow"`, `"maxFps"`, `"logLevel"`, `"msaaSamples"`, `"windowScale"`. Added hermetic `[Config][JsonConfigLoader]` Catch2 tests (21 assertions); `Glide3xWrapperTests` passed **34/34 (1815 assertions)**.
- **Target Files**:
  - `src/core/JsonConfigLoader.cpp` (L24–79)
  - `tests/test_glide3.cpp` (or `tests/test_architecture.cpp`)
- **What to Do**:
  1. Replace global `content.find("\"vulkan\"")` / `content.find("\"Voodoo2\"")` substring searches with a clean key-value extractor (e.g., `ExtractJsonString(content, "backend")`, `ExtractJsonString(content, "cardModel")`, `ExtractJsonBool(content, "logToConsole")`, `ExtractJsonNumber(...)`) that respects JSON key scoping and arbitrary whitespace.
  2. Add Catch2 unit tests verifying: unknown fields containing `"vulkan"` don't hijack `"backend": "opengl_es"`, whitespace variations around `:`, boolean `true`/`false`, and environment-variable precedence.
- **Automated Gate**: New Catch2 tests pass + `Glide3xWrapperTests` 100% pass.
- **User Verification**: Confirm existing `glide_config.json` profiles load as expected.

#### `T1.6` — Prepare WSI Build Dependencies & Fix `#if defined(UNIX)` Macro Trap
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `cad7d0d`)
- **Automated Result**: Wired `HAVE_X11=1`, `wayland-egl` (`HAVE_WAYLAND_EGL=1`), and `egl` (`HAVE_EGL=1`) in `src/CMakeLists.txt` with status line `-- WSI Backends: HAVE_X11=ON, HAVE_WAYLAND=ON (1.24.0), HAVE_WAYLAND_EGL=ON (18.1.0), HAVE_EGL=ON (1.5)`. Documented `#if defined(UNIX)` guard in legacy `PlatformWindowUtils.cpp` until `T2.7` retires it. Full project rebuild (`cmake -S . -B build && cmake --build build -j$(nproc)`) + `Glide3xWrapperTests` passed **34/34 (1815 assertions)**.
- **Target Files**:
  - `src/CMakeLists.txt` (L40–53)
  - `src/core/PlatformWindowUtils.cpp` (L14, L70, L88, L189 — audit only or guard safely until T2.7 replaces it)
- **What to Do**:
  1. In `src/CMakeLists.txt`, under `if(UNIX AND NOT APPLE)`:
     - Keep `X11::X11` and `X11::Xext`.
     - Add `pkg_check_modules(WAYLAND_EGL wayland-egl)` → define `HAVE_WAYLAND_EGL=1`, include/link `WAYLAND_EGL`.
     - Add `pkg_check_modules(EGL egl)` → define `HAVE_EGL=1`, include/link `EGL`.
     - Add `HAVE_X11=1` compile definition.
  2. **Important Caution**: Do **not** blindly flip `#if defined(UNIX)` to `#if defined(__unix__)` inside the *old* `PlatformWindowUtils.cpp` before `T2.2`/`T2.3`, because flipping it would immediately activate the broken `SDL_CreateWindow` + `wl_subsurface` crash path! Instead, add a `#error`/comment guard or build `NativeSurfaceHost` cleanly with `#if defined(__unix__) && !defined(__APPLE__)` in Phase 2, then retire `PlatformWindowUtils.cpp` in `T2.7`.
- **Automated Gate**: `cmake -S . -B build && cmake --build build -j$(nproc)` confirms `HAVE_X11`, `HAVE_WAYLAND`, `HAVE_WAYLAND_EGL`, and `HAVE_EGL` are detected and linked cleanly.
- **User Verification**: Confirm CMake summary output shows all four WSI flags enabled.

#### `T1.7` — Fix Wayland Vulkan `VK_PRESENT_MODE_FIFO_KHR` (`wp_fifo_v1`) Stutter
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `15482fc`)
- **Root Cause**: Under Gnome Mutter over RDP + Mesa 26.1.6 `llvmpipe`, `VK_PRESENT_MODE_FIFO_KHR` uses Wayland `wp_fifo_v1` + `wp_commit_timer_v1` on a 30 Hz virtual output, scheduling commits +166 ms into the future and causing bursty stutter (12.6 FPS with **966.1 ms** max frame gap). Because Wayland compositors always composite atomically at VBlank (tear-free) and `FrameTracker` caps at `maxFps=60`, selecting `VK_PRESENT_MODE_MAILBOX_KHR` on Wayland surfaces avoids the stall with zero tearing.
- **Automated Result**:
  - Added `VulkanBackend::SelectPresentMode(availableModes, vsyncEnabled, isWaylandSurface, forceStrictFifo)` in `VulkanBackend.{h,cpp}` and wired it into `CreateSwapchain()` with `GLIDE_VSYNC=fifo` escape hatch.
  - Added `[Vulkan][WSI][PresentMode]` Catch2 unit test in `tests/test_glide3.cpp` covering all 7 branches; `Glide3xWrapperTests` passed **35/35 test cases (1822 assertions)**.
  - Live Wayland `WAYLAND_DEBUG=client` frame pacing check (default env, no `GLIDE_VSYNC` override):
    - `glide3_cube` (T1.7 Mailbox): **59.4 FPS**, median gap **16.8 ms**, max gap **20.3 ms** (vs. `GLIDE_VSYNC=fifo`: **12.6 FPS**, max gap **966.1 ms**).
    - `glide3_gears` (T1.7 Mailbox): **31.0 FPS**, median gap **32.7 ms**, max gap **38.8 ms**.
    - `fog_showcase_demo` (T1.7 Mailbox): **35.7 FPS**, median gap **27.7 ms**, max gap **42.3 ms**.
    - XWayland (`SDL_VIDEODRIVER=x11`) still selects `FIFO` cleanly.
- **Target Files**:
  - `src/backends/vulkan/VulkanBackend.h`
  - `src/backends/vulkan/VulkanBackend.cpp`
  - `tests/test_glide3.cpp`
- **Phase 2 Note**: When `T2.4` migrates `VulkanBackend` to `NativeSurfaceHost`, keep passing `isWaylandSurface` derived from `m_surfaceHost.GetMode()` (`WaylandSubsurface` or `StandaloneSDL2` on Wayland) to `SelectPresentMode(...)`.
- **User Verification**: Run default Vulkan demos (`GLIDE_WRAPPER_BACKEND=vulkan ./build/bin/glide3_cube`, `glide3_gears`, `fog_showcase_demo`) *without* `GLIDE_VSYNC=0` and confirm stutter is gone.

---

### Phase 2: Option A — Unpatched DOSBox Native Child Surface (`NativeSurfaceHost`)

> **Goal**: Replace floating `SDL_CreateWindow` overlays and `SoftwareBackend` SDL hijacking with a single, clean `NativeSurfaceHost` abstraction that embeds a true native child surface (X11 child `Window` or Wayland `wl_subsurface`) inside unpatched DOSBox-X and SVN DOSBox, preserving aspect ratio and passing 100% of input to DOSBox.

#### Architectural Overview of `NativeSurfaceHost`
```text
                         +-------------------------------+
                         |  grSstWinOpen(hWnd, w, h, ..) |
                         +---------------+---------------+
                                         |
                                         v
                         +-------------------------------+
                         |       NativeSurfaceHost       |
                         |  (src/core/NativeSurfaceHost) |
                         +---+-------+-------+-------+---+
                             |       |       |       |
          +------------------+   +---+       +---+   +------------------+
          v                      v               v                      v
   [Headless Mode]      [Standalone SDL2]  [X11 Child Window]   [Wayland Subsurface]
   forceNoWindow=1         hWnd == 0       XOpenDisplay(NULL)   private wl_event_queue
   No OS surface         Owned SDL_Window  XCreateWindow(parent) roleless wl_surface
                                           XShape(empty input)  empty wl_region input
                                           XGetGeometry sync    wl_subsurface_set_desync
                                                                parent wl_surface_commit
                             |               |                      |
                             +-------+-------+----------------------+
                                     |
           +-------------------------+-------------------------+
           v                         v                         v
     [VulkanBackend]         [OpenGLESBackend]         [SoftwareBackend]
  vkCreateXlibSurfaceKHR    eglGetPlatformDisplay     XShmPutImage/XPutImage
  vkCreateWaylandSurfaceKHR eglCreateWindowSurface    wl_shm (or EGL quad)
  + Letterbox vkCmdBlit     + Gamma LUT Letterbox     + Letterbox present
```

#### `T2.1` — `NativeSurfaceHost` Interface, Headless/Standalone Modes & Aspect-Ratio Math
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `ae65f8e`)
- **Automated Result**: Added [src/core/NativeSurfaceHost.h](../../src/core/NativeSurfaceHost.h) (zero X11/Wayland header pollution; opaque native handle getters) and [src/core/NativeSurfaceHost.cpp](../../src/core/NativeSurfaceHost.cpp) with exact 64-bit integer `ComputePresentRect`, `Headless` mode, and `StandaloneSDL2` mode (`SDL_HINT_NO_SIGNAL_HANDLERS=1`, resizable HiDPI window, `SyncBounds`, `SDL_QUIT`/`SDL_WINDOWEVENT_CLOSE` detection in `PollEvents`). Compiled with zero warnings across all targets; `Glide3xWrapperTests` passed **38/38 test cases (1883 assertions)** including `[WSI][NativeSurfaceHost][AspectRatio]`, `[Headless]`, and `[Standalone]`.
- **Target Files**:
  - *New*: `src/core/NativeSurfaceHost.h`
  - *New*: `src/core/NativeSurfaceHost.cpp`
  - `src/CMakeLists.txt` (add `core/NativeSurfaceHost.cpp`)
  - `tests/test_glide3.cpp` (unit tests for `ComputePresentRect` & headless lifecycle)
- **What to Do**:
  1. Define clean `NativeSurfaceHost` class API:
     - `struct PresentRect { int32_t x, y; uint32_t width, height; };`
     - `static PresentRect ComputePresentRect(uint32_t guestW, uint32_t guestH, uint32_t hostW, uint32_t hostH, bool preserveAspect = true);`
       - Handles pillarbox (16:9 host, 4:3 guest → centered horizontally, black bars left/right), letterbox (tall host, wide guest), exact match, and zero-size safety clamps.
     - `enum class Mode { None, Headless, StandaloneSDL2, X11Child, WaylandSubsurface };`
     - `bool Attach(void* hostHandle, uint32_t guestW, uint32_t guestH, bool windowed, bool forceNoWindow, const std::string& title);`
     - `void Detach();`
     - `bool SyncBounds(uint32_t& outPixelW, uint32_t& outPixelH);` (returns `true` if host surface dimensions changed since last frame).
     - `void PollEvents();` (only pumps SDL events in `StandaloneSDL2` mode; **never** steals DOSBox's SDL events in `X11Child` / `WaylandSubsurface` modes!).
     - Native handle accessors (`GetMode()`, `GetX11Display()`, `GetX11Window()`, `GetWaylandDisplay()`, `GetWaylandSurface()`, `GetWaylandScale()`, `GetSDLWindow()`, `GetDrawableSize()`).
  2. Implement `Headless` and `StandaloneSDL2` modes first (no backend switched yet — zero behavioral impact on existing code).
     - In `StandaloneSDL2` mode (`hWnd == 0` ONLY, never when attached to DOSBox): set `SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1")` so `Ctrl+C` terminates standalone demos immediately, and check `SDL_QUIT` cleanly during `PollEvents()` so clicking the window close (`X`) button exits standalone demos cleanly.
  3. Add Catch2 unit tests for `ComputePresentRect` (640×480 in 1920×1080 → `x=240, y=0, w=1440, h=1080`; 640×480 in 800×600 → `0,0,800,600`; 640×400 in 640×480 → `0,40,640,400`; `preserveAspect=false` → full stretch; zero-guard).
- **Automated Gate**: Clean build + all `ComputePresentRect` and headless lifecycle unit tests pass.
- **User Verification**: Quick review of `NativeSurfaceHost.h` interface before X11/Wayland implementations begin.

#### `T2.2` — X11 / XWayland Native Child Window in `NativeSurfaceHost` + Embed Harness
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `4f40687`)
- **Automated Result**: Implemented `Mode::X11Child` in [src/core/NativeSurfaceHost.cpp](../../src/core/NativeSurfaceHost.cpp) with safe `SDL_Window*` / raw X11 Window ID resolution, private `XOpenDisplay(nullptr)`, scoped `XErrorHandler` trap, `XCreateWindow` + `XShapeCombineRectangles(ShapeInput, empty)` click-through, and zero-roundtrip `ConfigureNotify` resize tracking in `SyncBounds()`. Renamed `Mode::None` → `Mode::Detached` so callers including `<SDL2/SDL_syswm.h>` / `<X11/Xlib.h>` never hit Xlib's `#define None 0L`. Added [demos/child_surface_embed_test.cpp](../../demos/child_surface_embed_test.cpp) and `[WSI][NativeSurfaceHost][X11]` invalid-handle test.
  - `Glide3xWrapperTests`: **39/39 test cases (1886 assertions) passed**.
  - `SDL_VIDEODRIVER=x11 ./build/bin/child_surface_embed_test --raw-handle --smoke-ms 1200`: PASS (`800x600 -> 1024x600`, `4:3 PresentRect: x=112 y=0 800x600`, `resize_1024x600_observed=1`).
  - `SDL_VIDEODRIVER=x11 ./build/bin/child_surface_embed_test --sdl-window --smoke-ms 1200`: PASS (`800x600 -> 1024x600`, `resize_1024x600_observed=1`).
- **Target Files**:
  - `src/core/NativeSurfaceHost.cpp`
  - *New test tool*: `demos/child_surface_embed_test.cpp` (or `tools/`) — small standalone host app creating a parent SDL2 (or X11) window and calling our child-surface attach/resize/click-through test
- **What to Do**:
  1. Implement X11 host handle resolution under `#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_X11)`:
     - Distinguish whether `hostHandle` is an `SDL_Window*` (via safe probe against live SDL windows or `setConfig` hint) vs. a raw X11 `Window` ID (used by DOSBox-X `statWMInfo()` on X11/XWayland and SVN DOSBox without `setConfig`).
     - Open a **private** X11 connection (`m_x11Display = XOpenDisplay(nullptr)`), completely avoiding Xlib thread races with DOSBox's `Display*`.
     - Query parent geometry via `XGetGeometry(m_x11Display, parentWin, ...)` (verify parent window is valid with a scoped `XErrorHandler` so bad handles fail gracefully instead of calling `exit(1)`!).
     - Create true child window: `XCreateWindow(m_x11Display, parentWin, 0, 0, parentW, parentH, 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask | CWBackPixel | CWBitGravity, &attrs)` with `background_pixel = BlackPixel(...)`.
     - Apply empty input shape via `XShapeCombineRectangles(m_x11Display, m_x11ChildWin, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted)` so all mouse clicks & keyboard events fall straight through to DOSBox.
     - Map child window (`XMapWindow` + `XFlush`).
  2. Implement `SyncBounds()` for X11:
     - Query `XGetGeometry(m_x11Display, m_x11ParentWin, ...)` (throttled or cheap roundtrip; or listen to `StructureNotifyMask` on parent if safe, falling back to `XGetGeometry` on resize check).
     - When parent `w`/`h` changes, call `XMoveResizeWindow(m_x11Display, m_x11ChildWin, 0, 0, parentW, parentH)` + `XFlush` and return `true` so backends recreate swapchain/viewport cleanly.
  3. Implement clean `Detach()`: `XDestroyWindow`, `XCloseDisplay` (never touches parent window!).
  4. Build minimal `child_surface_embed_test` that spawns a parent SDL2 window (`SDL_VIDEODRIVER=x11`), embeds `NativeSurfaceHost`, verifies resize tracking and click-through input reception on the parent window.
- **Automated Gate**: Clean build + `Glide3xWrapperTests` 31/31 pass + `child_surface_embed_test` builds and runs cleanly under `SDL_VIDEODRIVER=x11`.
- **User Verification**: Run `SDL_VIDEODRIVER=x11 ./build/bin/child_surface_embed_test`, resize the window, click/press keys inside it, and confirm the parent receives all input and the child surface follows seamlessly without floating/detaching.

#### `T2.3` — Native Wayland `wl_subsurface` in `NativeSurfaceHost` + SVN DOSBox `setConfig('SDL2')`
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `9865fc5`)
- **Automated Result**:
  - **Critical 64-bit `hWnd` ABI Fix**: Changed `grSstWinOpen`'s first parameter from `FxU32 hWnd` (32-bit `unsigned int` on LP64 Linux, which truncated 64-bit `SDL_Window*` and `wl_surface*` pointers passed by DOSBox!) to `AnyPtr hWnd` (`unsigned long`) in [include/glide2/glide.h](../../include/glide2/glide.h), [include/glide3/glide.h](../../include/glide3/glide.h), [src/glide2/glide2_entry.cpp](../../src/glide2/glide2_entry.cpp), and [src/glide3/glide3_entry.cpp](../../src/glide3/glide3_entry.cpp).
  - **SVN DOSBox `setConfig('SDL2')` Parity**: Exported `setConfig` (writes `0x324c4453` `'SDL2'`) and `setConfigRes` from `libglide3x.so` (matching `libglide2x.so`); verified via `nm -D` and new Catch2 test `[WSI][DOSBox][setConfig]`.
  - **Native Wayland `wl_subsurface`**: Implemented `Mode::WaylandSubsurface` in [src/core/NativeSurfaceHost.cpp](../../src/core/NativeSurfaceHost.cpp) with live `SDL_Window*` and DOSBox-X raw `wl_surface*` union-alias discovery, private `wl_event_queue` + `wl_proxy_create_wrapper`, roleless `wl_surface` + empty `wl_region` input mask, `wl_subsurface_set_desync`, `wl_shm` binding, HiDPI `wl_surface_set_buffer_scale` + `SyncBounds()` tracking, and strict `Detach()` teardown order (`wl_subsurface_destroy` → `wl_surface_destroy` → conditional parent commit → globals/queue cleanup, never disconnecting `hostDisplay`).
  - **Interactive Wayland Harness**: Extended [demos/child_surface_embed_test.cpp](../../demos/child_surface_embed_test.cpp) with double-buffered `wl_shm` rendering (`memfd_create` + `WL_SHM_FORMAT_XRGB8888`) matching the X11 visual overlay.
  - **Tests & Protocol Check**:
    - `Glide3xWrapperTests`: **40/40 test cases (1894 assertions) passed**.
    - `Glide2xWrapperTests`: **27/27 test cases (2344 assertions) passed**.
    - `SDL_VIDEODRIVER=wayland ./build/bin/child_surface_embed_test --raw-handle --smoke-ms 1200`: PASS (`Mode=WaylandSubsurface`, `800x600 -> 1024x600`, `resize_1024x600_observed=1`).
    - `SDL_VIDEODRIVER=wayland ./build/bin/child_surface_embed_test --sdl-window --smoke-ms 1200`: PASS (`Mode=WaylandSubsurface`, `resize_1024x600_observed=1`).
    - `SDL_VIDEODRIVER=x11 ./build/bin/child_surface_embed_test --raw-handle --smoke-ms 1200`: PASS (no X11 regression).
    - `WAYLAND_DEBUG=client` trace: **0 protocol errors**, confirmed private event queue isolation, `set_input_region`, `get_subsurface`, `set_desync`, and clean `wl_subsurface.destroy()`.
- **Target Files**:
  - `src/core/NativeSurfaceHost.cpp`
  - `src/glide2/glide2_entry.cpp` (export `setConfig` / `setConfigRes` for SVN DOSBox `sdl12-compat` handshake)
  - `src/glide3/glide3_entry.cpp` (export `setConfig` / `setConfigRes` if applicable)
- **What to Do**:
  1. **SVN DOSBox `sdl12-compat` Handshake**:
     - Export `extern "C" void setConfig(uint32_t flags, uint32_t* magic)` that writes `0x324c4453` (`'SDL2'`) to `*magic` when `magic != nullptr`, and store a flag noting host handles will be `SDL_Window*`! (Reference: `dosbox/qemu-xtra/dosbox/dosbox-0/src/hardware/glide.cpp`.)
  2. **Wayland Host Surface Discovery**:
     - Under `#if defined(__unix__) && !defined(__APPLE__) && defined(HAVE_WAYLAND)`:
     - Resolve `wl_display*` and `wl_surface*` from `SDL_Window*` via `SDL_GetWindowWMInfo`.
     - Handle DOSBox-X's Wayland quirk (`statWMInfo()` casts `wmi.info.x11.window` which aliases `wmi.info.wl.surface` in the union!): if `SDL_GetCurrentVideoDriver()` is `"wayland"`, first check if `hostHandle` is a live `SDL_Window*` (scanning `SDL_GetWindowFromID(1..64)`), and if not, check whether `hostHandle` matches any live SDL window's `wmi.info.wl.surface`! That cleanly recovers `(wl_display*, wl_surface*, SDL_Window*)` for **unpatched** DOSBox-X on native Wayland!
  3. **Spec-Compliant Wayland Subsurface Creation**:
     - Create a **private** `wl_event_queue` (`wl_display_create_queue`) and use a proxy wrapper (`wl_proxy_create_wrapper(hostDisplay)`) so `wl_display_roundtrip_queue` never steals or races with SDL/DOSBox's main Wayland event loop!
     - Bind `wl_compositor` and `wl_subcompositor` (and `wl_shm` for Software backend) from registry on the private queue.
     - Create a **fresh, roleless `wl_surface`** (`wl_compositor_create_surface`) — **NEVER** call `SDL_CreateWindow` for a Wayland subsurface!
     - Set an **empty input region**: `wl_region* empty = wl_compositor_create_region(...)`; `wl_surface_set_input_region(childSurface, empty)`; `wl_region_destroy(empty)`.
     - Create `wl_subsurface` via `wl_subcompositor_get_subsurface(subcompositor, childSurface, hostSurface)`.
     - Set `wl_subsurface_set_position(subsurface, 0, 0)` and `wl_subsurface_set_desync(subsurface)` so our swapchain/EGL commits present immediately without waiting for DOSBox to repaint.
     - Issue **one** `wl_surface_commit(hostSurface)` + `wl_display_flush(hostDisplay)` to latch the subsurface into the parent's compositor tree.
     - Track HiDPI scale (`SDL_GetWindowSize` logical vs `SDL_GL_GetDrawableSize` / `wl_surface_set_buffer_scale`) so `SyncBounds()` returns accurate physical pixel dimensions.
  4. **Clean Teardown (`Detach()`)**:
     - Destroy `wl_subsurface`, then child `wl_surface`, registry globals, proxy wrapper, and `wl_event_queue` (never disconnect `hostDisplay`!). Issue a `wl_surface_commit(hostSurface)` + `wl_display_flush` so the compositor removes the child cleanly.
  5. Test with `SDL_VIDEODRIVER=wayland ./build/bin/child_surface_embed_test`.
- **Automated Gate**: Clean build + `Glide3xWrapperTests` 31/31 pass + `child_surface_embed_test` runs cleanly under `SDL_VIDEODRIVER=wayland` with zero Wayland protocol errors (`WAYLAND_DEBUG=1` check).
- **User Verification**: Run `SDL_VIDEODRIVER=wayland ./build/bin/child_surface_embed_test`, resize/maximize/fullscreen the host window, verify input pass-through and clean exit.

#### `T2.4` — Migrate `VulkanBackend` to `NativeSurfaceHost` + Aspect-Ratio Blit
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commits `2d75259`, `985e7fa`, `1d349ee`)
- **User Verification Notes**: Confirmed on real GPU (CachyOS Wayland 133.33% fractional scale, `sdl2-compat`) across `glide_embed_test`, patched SVN DOSBox (GTA 1 windowed + 512/640/800 live resolution switching + startup fullscreen, zero stutter/sticky keys), and DOSBox-X (GTA 1 windowed + fullscreen). Mid-game fullscreen toggle deferring until resolution change in patched SVN DOSBox noted for Phase 4 Trigger #4.
- **Automated Result**:
  - Replaced legacy `PlatformWindowUtils` overlay window / `SDL_PumpEvents()` in [VulkanBackend.h](../../src/backends/vulkan/VulkanBackend.h) and [VulkanBackend.cpp](../../src/backends/vulkan/VulkanBackend.cpp) with `NativeSurfaceHost`:
    - `CreateInstance()` enumerates Vulkan WSI extensions directly (`VK_KHR_surface`, `VK_KHR_xlib_surface`, `VK_KHR_xcb_surface`, `VK_KHR_wayland_surface`) without touching SDL during `Initialize()`.
    - `CreatePresentationSurface()` creates `VK_KHR_xlib_surface` (`X11Child`), `VK_KHR_wayland_surface` (`WaylandSubsurface`), or `SDL_Vulkan_CreateSurface` (`StandaloneSDL2`).
    - Preserves `T1.7`'s Wayland Mailbox present-mode preference via `m_surfaceHost.IsWaylandSurface()`.
    - `SwapBuffers()` delegates event pumping to `m_surfaceHost.PollEvents()` (strict no-op for DOSBox; clean `X`/Ctrl+C exit for standalone demos), syncs resize bounds via `m_surfaceHost.SyncBounds()`, clears letterbox/pillarbox bars to opaque black with a `TransferWrite -> TransferWrite` barrier, and blits into `ComputePresentRect(...)`.
    - Fixed pre-existing `VUID-vkQueueSubmit-pSignalSemaphores-00067` validation error by sizing `m_renderFinishedSemaphores` per swapchain image (`[imageIdx]`) instead of per frame-in-flight (`[2]`).
    - Strict WSI teardown order in `DetachWindow()`: `waitIdle()` → `DestroySwapchain()` → `m_surface.reset()` → `m_surfaceHost.Detach()`.
  - Added interactive Glide 3.x host-embed harness [demos/glide_embed_test.cpp](../../demos/glide_embed_test.cpp) supporting `--raw-handle`, `--sdl-window`, `--parent-gl` (simulates DOSBox-X `output=opengl`), resize, fullscreen, `grSstWinClose`+`grSstWinOpen` reopen, and `--smoke-ms`.
  - **Tests & Validation**:
    - `Glide3xWrapperTests` (software & vulkan headless): **40/40 passed (1894 assertions)** on both.
    - `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` on `glide_embed_test` (`Wayland`, `X11`, `Wayland --parent-gl`, `X11 --parent-gl`): **0 VUID errors**, **0 Wayland protocol errors**, all resized + reopened cleanly.
    - `WAYLAND_DEBUG=client` on Vulkan `glide_embed_test`: **0 protocol errors**.
    - Standalone `GLIDE_WRAPPER_BACKEND=vulkan ./build/bin/glide3_cube`: exits cleanly on `SIGINT` (`Ctrl+C`) without `kill -9`.
- **Target Files**:
  - `src/backends/vulkan/VulkanBackend.h`
  - `src/backends/vulkan/VulkanBackend.cpp`
- **What to Do**:
  1. Replace `m_sdlWindow`, `m_overlayWindow`, `m_sdlRenderer`, `m_sdlTexture`, and `m_lastRequested*` overlay tracking with `NativeSurfaceHost m_surfaceHost`.
  2. Enable `VK_KHR_xlib_surface` (when `HAVE_X11`) and `VK_KHR_wayland_surface` (when `HAVE_WAYLAND`) in Vulkan instance creation alongside `VK_KHR_surface`.
  3. Create `m_surface` directly from `m_surfaceHost`:
     - `X11Child` → `vkCreateXlibSurfaceKHR(instance, {dpy, childWin})`
     - `WaylandSubsurface` → `vkCreateWaylandSurfaceKHR(instance, {wlDpy, wlChildSurface})`
     - `StandaloneSDL2` → `SDL_Vulkan_CreateSurface(m_surfaceHost.GetSDLWindow(), ...)`
     - Keep `T1.7`'s `SelectPresentMode(..., isWaylandSurface, forceStrictFifo)` wired to `m_surfaceHost.GetMode()` (`WaylandSubsurface` or `StandaloneSDL2` on Wayland) so Wayland VSync continues to prefer Mailbox.
  4. Update `VulkanBackend::SwapBuffers()`:
     - Call `m_surfaceHost.SyncBounds(hostW, hostH)`; if resized or `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`, recreate swapchain cleanly.
     - Compute `PresentRect dst = NativeSurfaceHost::ComputePresentRect(m_guestWidth, m_guestHeight, m_swapchainExtent.width, m_swapchainExtent.height, /*preserveAspect=*/true)` (respecting config option if user wants stretch override later).
     - Clear swapchain image to opaque black (`vkCmdClearColorImage`) when `dst` does not cover the full swapchain extent (clean letterbox/pillarbox bars), then `vkCmdBlitImage` from offscreen render target (`m_guestWidth × m_guestHeight`) into `(dst.x, dst.y) .. (dst.x + dst.width, dst.y + dst.height)`.
  5. Ensure `DetachWindow()` calls `vkDeviceWaitIdle()`, destroys swapchain + `m_surface` **before** `m_surfaceHost.Detach()` (strict WSI lifetime order!).
- **Automated Gate**: Clean build + `Glide3xWrapperTests` 31/31 + standalone Vulkan demos (`glide3_cube`, `glide2_gears`) + `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` zero WSI/sync errors.
- **User Verification**: Test **GTA 1** on **DOSBox-X** (windowed + startup fullscreen, XWayland + native Wayland) and **SVN DOSBox** using `GLIDE_WRAPPER_BACKEND=vulkan`. Confirm:
  - Embedded cleanly inside DOSBox window (no floating window).
  - Correct 4:3 aspect ratio in fullscreen (black pillarbox bars, no stretching!).
  - Keyboard/mouse work reliably and game exit returns cleanly to DOS prompt.

#### `T2.5` — Migrate `OpenGLESBackend` to `NativeSurfaceHost` (Direct EGL) + Aspect-Ratio & Gamma LUT
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commits `df40756`, `8c416e2`)
- **User Verification Notes**: Confirmed on real GPU (CachyOS) across both native Wayland (`glide_embed_test` + GTA 1 in patched SVN DOSBox) and X11 (`glide_embed_test --raw-handle --parent-gl` and `--raw-handle` after `8c416e2` fixed X11 `EGL_NATIVE_VISUAL_ID` matching and `RecreateX11ChildForVisual`).
- **Automated Result**:
  - Replaced legacy `PlatformWindowUtils` overlay window / `SDL_PumpEvents()` in [OpenGLESBackend.h](../../src/backends/opengl_es/OpenGLESBackend.h) and [OpenGLESBackend.cpp](../../src/backends/opengl_es/OpenGLESBackend.cpp) with `NativeSurfaceHost` + Direct EGL 1.5:
    - `Headless`: surfaceless EGL (`EGL_PLATFORM_SURFACELESS_MESA`, 1×1 Pbuffer fallback) with zero SDL/X11/Wayland calls.
    - `X11Child`: Direct EGL (`EGL_PLATFORM_X11_EXT`, exact `EGL_NATIVE_VISUAL_ID` match to child X11 visual, private `EGLDisplay`) + dynamic `libglvnd` GLX suspend/restore via `dlsym` so host GLX (`DOSBox-X output=opengl` / `SDL_Renderer`) never conflicts with `EGL_BAD_ACCESS`.
    - `WaylandSubsurface`: Direct EGL (`EGL_PLATFORM_WAYLAND_KHR` + `wl_egl_window_create` / `wl_egl_window_resize`, never calling `eglTerminate` on the host's borrowed `wl_display*`, `eglSwapInterval(0)` on Wayland unless `GLIDE_VSYNC=fifo`) + host EGL API/context save & restore (`EGL_OPENGL_API` ↔ `EGL_OPENGL_ES_API`).
    - `StandaloneSDL2`: `SDL_WINDOW_OPENGL` + `SDL_GL_CreateContext` with `m_surfaceHost.PollEvents()` clean quit / Ctrl+C exit.
  - Replaced raw `glBlitFramebuffer` in `SwapBuffers()` with `PresentToWindowSurface()` rendering a fullscreen quad via `m_glProgram` + `m_glGammaLutTex` into `NativeSurfaceHost::ComputePresentRect(...)` with black letterbox/pillarbox bars, exact texel-centered Gamma LUT sampling, opaque `alpha = 1.0` output (preventing Wayland subsurface transparency bleed), and unbound guest TMU samplers (`glBindSampler(0/1, 0)`).
  - Strict WSI teardown order: delete GL objects (including `m_glesTextures`) → `eglMakeCurrent(NO)` → `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` → `eglTerminate` (if owned) → `m_surfaceHost.Detach()` → restore saved host GLX/EGL context.
  - **Tests & Validation**:
    - Headless `GLIDE_WRAPPER_BACKEND=opengl_es GLIDE_FORCE_NO_WINDOW=1 DISPLAY="" WAYLAND_DISPLAY="" ./build/bin/Glide3xWrapperTests`: **40/40 test cases passed (1894 assertions)** with zero display server.
    - `Glide2xWrapperTests "OpenGL ES Gamma Correction Accuracy Validation"` (both Wayland & X11): **PASS (18/18 assertions)** — verified Gamma 1.0 → 128, Gamma 2.0 → 180, VRAM → 128.
    - `glide_embed_test` (`--smoke-ms 600`) across all 6 modes (`Wayland`/`X11` × `--raw-handle`/`--sdl-window` × `--parent-gl`/2D): **all 6 PASS**, resize + close/reopen clean, **0 GL errors**, **0 Wayland protocol errors** under `WAYLAND_DEBUG=client`.
    - Standalone `glide3_cube` on Wayland & X11: exits cleanly on `SIGINT` (`Ctrl+C`).
    - Zero compiler warnings under `-Wall -Wextra -Wpedantic -Werror`.
- **Target Files**:
  - `src/backends/opengl_es/OpenGLESBackend.h`
  - `src/backends/opengl_es/OpenGLESBackend.cpp`
- **What to Do**:
  1. Replace `m_sdlWindow`, `m_overlayWindow`, `m_glesWindow`, `m_sdlRenderer`, `m_sdlTexture`, and `SDL_GL_CreateContext` with `NativeSurfaceHost m_surfaceHost` + direct EGL state (`EGLDisplay`, `EGLContext`, `EGLSurface`, `wl_egl_window*` on Wayland).
  2. Create EGL display & surface per `NativeSurfaceHost::Mode`:
     - `X11Child`: `eglGetPlatformDisplay(EGL_PLATFORM_X11_EXT, dpy, nullptr)` + `eglCreateWindowSurface(eglDpy, cfg, (EGLNativeWindowType)childWin, nullptr)`.
     - `WaylandSubsurface`: `eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wlDpy, nullptr)` + `wl_egl_window_create(wlChildSurface, w, h)` + `eglCreateWindowSurface(...)`. On `SyncBounds()`, call `wl_egl_window_resize(m_wlEglWindow, w, h, 0, 0)`.
     - `StandaloneSDL2`: obtain native handle from `m_surfaceHost` (or use `SDL_GL_CreateContext` fallback ONLY for standalone).
     - Bind `EGL_OPENGL_ES_API`, request ES 3.2 context (`EGL_CONTEXT_MAJOR_VERSION=3, EGL_CONTEXT_MINOR_VERSION=2`), set `eglSwapInterval(eglDpy, 0)` (frame pacing is controlled by wrapper config).
     - **Critical isolation check**: Save/restore or avoid touching DOSBox's GLX context; because our EGL context is bound to our own native child surface (`X11Child` or `WaylandSubsurface`) on its own EGLDisplay, DOSBox-X `output=opengl` (GLX/EGL) will not conflict!
  3. Fix `OpenGLESBackend::SwapBuffers()` presentation (L330–331):
     - Replace raw `glBlitFramebuffer` directly to `winW × winH` (which bypassed gamma AND broke aspect ratio) with a clean presentation pass to default FBO `0`:
       - `glViewport(0, 0, hostW, hostH); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);`
       - Compute `PresentRect dst = NativeSurfaceHost::ComputePresentRect(...)` and set `glViewport(dst.x, dst.y, dst.width, dst.height);`
       - Draw fullscreen quad with `m_glProgram` (applying `m_gammaLutTexture`!) sampling the resolved offscreen color attachment.
       - Call `eglSwapBuffers(m_eglDisplay, m_eglSurface)`.
  4. Strict teardown order in `DetachWindow()`: `eglMakeCurrent(NO_SURFACE, NO_CONTEXT)` → destroy GL objects → `eglDestroyContext` → `eglDestroySurface` → `wl_egl_window_destroy` → `eglTerminate` → `m_surfaceHost.Detach()`.
- **Automated Gate**: Clean build + `Glide3xWrapperTests` 31/31 + standalone GLES demos (`glide3_cube`, `gamma_showcase_demo` verifying gamma visibly works!).
- **User Verification**: Test **GTA 1** on **DOSBox-X** (`output=opengl` AND `output=surface`, windowed + fullscreen, XWayland + Wayland) and **SVN DOSBox** using `GLIDE_WRAPPER_BACKEND=opengl_es`. Confirm gamma works, aspect ratio is preserved, and zero GL context crashes occur.

#### `T2.6` — Migrate `SoftwareBackend` to `NativeSurfaceHost` + Aspect-Ratio Presentation
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commits `b0066fd`, `47c21b4`)
- **User Verification Notes**: Confirmed working on both Wayland (`wl_shm`) and X11 (`XShmPutImage` on `TrueColor`, after `47c21b4` fixed dangling `XShmSegmentInfo` `obdata` and `DirectColor` visual inheritance).
- **Automated Result**:
  - Removed all DOSBox `SDL_Window*`/`SDL_Renderer*` hijacking and `SDL_PumpEvents()` from [SoftwareBackend.h](../../src/backends/software/SoftwareBackend.h) and [SoftwareBackend.cpp](../../src/backends/software/SoftwareBackend.cpp), migrating all 4 modes to `NativeSurfaceHost` (100% GPU/EGL-independent):
    - `Headless`: zero SDL/X11/Wayland/EGL calls.
    - `X11Child`: `X11Presenter` using `XShmPutImage` (with automatic `IPC_RMID` reclamation, trapped `XShmAttach` error fallback to `XPutImage`, and BGR mask detection) + OpenMP aspect-ratio preserving letterbox/pillarbox scaling (`BlitScaledLetterbox`).
    - `WaylandSubsurface`: `WaylandShmPresenter` using triple-buffered anonymous `memfd_create` `wl_shm` pools (`WL_SHM_FORMAT_XRGB8888`, safe retired-buffer reaping across live resizes, `wl_surface_damage_buffer`) bound to `NativeSurfaceHost`'s private `wl_event_queue` and `wp_viewport` fractional-scale destination.
    - `StandaloneSDL2`: owned `SDL_Renderer` + `SDL_Texture` presenting into `ComputePresentRect` with black bars, opaque alpha, and `m_surfaceHost.PollEvents()` clean quit / `SIGINT` exit.
  - **Tests & Validation**:
    - Headless `DISPLAY="" WAYLAND_DISPLAY="" GLIDE_HEADLESS=1 GLIDE_WRAPPER_BACKEND=software ./build/bin/Glide3xWrapperTests`: **40/40 passed (1894 assertions)** with zero display server / zero `libEGL`/`ZINK` warnings.
    - `glide_embed_test` across all 6 modes (`Wayland`/`X11` × `--raw-handle`/`--sdl-window` × `--parent-gl`/2D + SSAA 2x): **all PASS**, resize `800x600 -> 1024x600` + close/reopen clean, **0 Wayland protocol errors** under `WAYLAND_DEBUG=client`, **0 leaked SysV SHM segments** (`ipcs -m`).
    - Standalone `glide3_cube` on Wayland & X11: exits cleanly on `SIGINT`.
    - Zero compiler warnings under `-Wall -Wextra -Wpedantic -Werror`.
- **Target Files**:
  - `src/backends/software/SoftwareBackend.h`
  - `src/backends/software/SoftwareBackend.cpp`
- **What to Do**:
  1. Replace SDL window/renderer hijacking in `SoftwareBackend::AttachWindow` with `NativeSurfaceHost m_surfaceHost`.
  2. Implement clean presentation of `m_resolvedBuffer` (RGBA8888) into `ComputePresentRect(...)` with black letterbox/pillarbox bars:
     - **Recommended clean path**:
       - `X11Child`: `XPutImage` (or `XShmPutImage` via `X11::Xext`) scaling `m_resolvedBuffer` into a reusable destination buffer matching `dst.width × dst.height` (or a tiny shared EGL textured quad helper if hardware scaling is preferred — note `XShmPutImage` + `wl_shm` keeps `SoftwareBackend` 100% GPU/EGL-driver independent!).
       - `WaylandSubsurface`: `wl_shm` double-buffered `memfd_create` pool with `wl_surface_attach` + `wl_surface_damage_buffer` + `wl_surface_commit`.
       - `StandaloneSDL2`: existing SDL2 texture/renderer path (with `m_sdlRendererOwned = true`) using `SDL_RenderSetLogicalSize` or explicit `SDL_Rect` from `ComputePresentRect`.
  3. Remove all remaining DOSBox `SDL_Window*` / `SDL_Renderer*` hijacking code from `SoftwareBackend.cpp`.
- **Automated Gate**: Clean build + `Glide3xWrapperTests` 31/31 (zero `libEGL`/`ZINK` warnings in headless mode) + standalone `GLIDE_WRAPPER_BACKEND=software ./build/bin/glide3_cube`.
- **User Verification**: Test **GTA 1** on **DOSBox-X** and **SVN DOSBox** using `GLIDE_WRAPPER_BACKEND=software` (windowed + fullscreen). Confirm aspect ratio and clean exit.

#### `T2.7` — Retire Legacy Overlay Code & Late `XInitThreads()`, Run Full Option A Matrix
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `ce4a354`)
- **User Verification Notes**: Confirmed across the full matrix on real hardware (both DOSBox-X and SVN DOSBox, all 3 backends `vulkan`/`opengl_es`/`software`, X11 and native Wayland, `output=opengl` and `output=surface`). **Phase 2 (Option A) complete!**
- **Automated Result**:
  - Deleted `src/core/PlatformWindowUtils.{h,cpp}` (-471 LOC total across `T2.7`), removed from `src/CMakeLists.txt`, and removed `__attribute__((constructor)) InitializeX11Threads()` from `src/glide2/glide2_entry.cpp` and `src/glide3/glide3_entry.cpp`.
  - Clean `cmake -S . -B build && cmake --build build -j$(nproc)` with 0 compiler warnings under `-Werror`.
  - Verified zero `InitializeX11Threads` / `PlatformWindowUtils` symbols in `libglide2x.so` and `libglide3x.so` and zero `[Wrapper-Init]` stdout output.
  - Headless `Glide3xWrapperTests` (`DISPLAY="" WAYLAND_DISPLAY="" GLIDE_FORCE_NO_WINDOW=1`): **40/40 passed (1894 assertions)** across all 3 backends (`software`, `vulkan`, `opengl_es`).
  - Embedded host matrix (`glide_embed_test --smoke-ms 600`): **18/18 PASS** across `{vulkan, opengl_es, software}` × `{wayland, x11}` × `{--raw-handle --parent-gl, --raw-handle, --sdl-window}` (all resize + reopen clean, **0 Vulkan VUID errors** under `VK_LAYER_KHRONOS_validation`).
  - Standalone `glide3_cube` (`hWnd=0`): clean `SIGINT` exit across all 3 backends; **0 leaked SysV SHM segments** (`ipcs -m` delta = 0).
- **Target Files**:
  - `src/core/PlatformWindowUtils.h`, `src/core/PlatformWindowUtils.cpp` (delete or trim to empty if unused; update `src/CMakeLists.txt`)
  - `src/glide2/glide2_entry.cpp` (L10–21: remove `__attribute__((constructor))` `XInitThreads()`)
  - `src/glide3/glide3_entry.cpp` (L14: remove `__attribute__((constructor))` `XInitThreads()`)
- **What to Do**:
  1. Verify zero references remain to `PlatformWindowUtils::CreateOverlayWindow`, `HijackOrWrapWindow`, `ParentSurfaceWayland`, or `SyncOverlayWindowBounds`. Remove dead files/functions.
  2. Remove the late `dlopen`-time `XInitThreads()` constructor (safe now that every X11 call in our wrapper uses our own private `Display*` owned exclusively by `NativeSurfaceHost`).
  3. Execute the **Full Option A Verification Matrix**:

| Client | Display Mode | Output Mode | `vulkan` | `opengl_es` | `software` |
| :--- | :--- | :--- | :---: | :---: | :---: |
| **DOSBox-X** | XWayland Windowed | `output=opengl` | `[x]` | `[x]` | `[x]` |
| **DOSBox-X** | XWayland Windowed | `output=surface` | `[x]` | `[x]` | `[x]` |
| **DOSBox-X** | XWayland Startup Fullscreen | `output=opengl` | `[x]` | `[x]` | `[x]` |
| **DOSBox-X** | Native Wayland Windowed | `output=opengl` | `[x]` | `[x]` | `[x]` |
| **DOSBox-X** | Native Wayland Startup Fullscreen | `output=opengl` | `[x]` | `[x]` | `[x]` |
| **SVN DOSBox (`dosbox-0`)** | X11/XWayland (`sdl12-compat`) Windowed | `surface` | `[x]` | `[x]` | `[x]` |
| **SVN DOSBox (`dosbox-0`)** | X11/XWayland (`sdl12-compat`) Fullscreen | `surface` | `[x]` | `[x]` | `[x]` |
| **Standalone Demos** | Windowed (`hWnd=0`) | N/A | `[x]` | `[x]` | `[x]` |

- **User Verification**: User tests GTA 1 (and optionally a second Glide game) across the matrix above and confirms Option A behavior before Phase 3 or Phase 4.

---

### Phase 3: Post-Option-A Structural Refactoring & Maintainability

> **Goal**: Clean up deep architectural debt *after* Option A is stable and verified, keeping every refactor pure (zero intended behavioral change) and verified against the full Phase 2 baseline.

#### `T3.1` — Extract `GlideStateTracker` Out of `SoftwareBackendBase`
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `1edabe4`)
- **Automated Result**:
  - Extracted pure Glide 2.x/3.x pipeline state tracking (combiners, depth, blend, fog, chromakey, clip, origin, gamma LUT, texture bindings, `WrapperConfig`, and unit-test getters) into [GlideStateTracker.h](../../src/backends/GlideStateTracker.h) and [GlideStateTracker.cpp](../../src/backends/GlideStateTracker.cpp).
  - Extracted shared CPU LFB staging and pixel format conversion into [CpuLfbStaging.h](../../src/backends/CpuLfbStaging.h) and [CpuLfbStaging.cpp](../../src/backends/CpuLfbStaging.cpp) with **lazy allocation** (0 bytes allocated on GPU backends until an LFB read, write, or lock occurs).
  - Migrated `VulkanBackend` and `OpenGLESBackend` to inherit directly from `GlideStateTracker` and compose `CpuLfbStaging`, removing `m_cpuBuffers[2]`, `m_resolvedBuffer`, `m_headlessDepthBuffer`, SSAA arrays, eager GLES `m_swizzleBuffer` allocation, and dead `m_useX11BlitFallback` from GPU backends. Slimmed `SoftwareBackendBase.{h,cpp}` by -1,174 LOC.
  - Updated state-getter casts in `tests/test_architecture.cpp` and `tests/test_glide3.cpp` to `GlideStateTracker*` and added `[Architecture][GlideStateTracker]` unit tests verifying inheritance isolation (`!std::is_base_of_v<SoftwareBackendBase, VulkanBackend|OpenGLESBackend>`) and zero-byte lazy CPU LFB staging.
  - **Verification**:
    - Clean `cmake -S . -B build && cmake --build build -j$(nproc)` with **0 compiler warnings** under `-Werror`.
    - `Glide3xWrapperTests` (headless): **41/41 passed (1914 assertions)** across all 3 backends (`software`, `vulkan`, `opengl_es`).
    - `Glide2xWrapperTests` (`software`): **27/27 passed (2344 assertions)**; `"*LFB*"` on `vulkan` & `opengl_es`: **4/6 passed (107/110 assertions)** (exact match to baseline).
    - `glide_embed_test --raw-handle --parent-gl --smoke-ms 600`: **6/6 PASS** across `{vulkan, opengl_es, software}` × `{wayland, x11}` (0 Vulkan validation errors).
    - `./build/bin/sw_benchmark`: 416.3M LFB write px/s, 513.8k tris/s.
- **Target Files**:
  - *New*: `src/backends/GlideStateTracker.h`, `src/backends/GlideStateTracker.cpp`, `src/backends/CpuLfbStaging.h`, `src/backends/CpuLfbStaging.cpp`
  - `src/backends/SoftwareBackendBase.{h,cpp}`
  - `src/backends/vulkan/VulkanBackend.{h,cpp}`
  - `src/backends/opengl_es/OpenGLESBackend.{h,cpp}`
  - `src/backends/software/SoftwareBackend.{h,cpp}`
- **What to Do**:
  1. Separate pure Glide pipeline state (combiners, depth, blend, fog, chromakey, clip, origin, gamma, texture bindings, `WrapperConfig`) into `GlideStateTracker` (implementing shared `IGraphicsBackend` state setters/getters).
  2. Have `VulkanBackend` and `OpenGLESBackend` inherit from `GlideStateTracker` (or compose it), removing `m_cpuBuffers[2]`, `m_resolvedBuffer`, `m_headlessDepthBuffer`, SSAA software arrays, and dead `m_useX11BlitFallback` from GPU backends.
  3. Keep CPU buffer allocation & LFB software helpers exclusively in `SoftwareBackendBase` (or fold into `SoftwareBackend`).
- **Automated Gate**: Full build + `Glide2xWrapperTests` (no regressions) + `Glide3xWrapperTests` (31/31) + `python3 tests/benchmark.py` (within ±3%).
- **User Verification**: Confirm all 3 backends run GTA 1 and demos identically with lower RSS memory footprint on Vulkan/GLES.

#### `T3.2` — Deduplicate Glide 2 & Glide 3 Frontend Entry Helpers
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `4ea66b7`)
- **Automated Result**:
  - Extracted shared frontend helpers into `src/core/GlideEntryCommon.h` (196 LOC) and `src/core/GlideEntryCommon.cpp` (786 LOC): `SharedFrontendState`, lifecycle (`InitGlide`, `EnsureEngineInitialized`, `ShutdownGlide`, `CloseWindow`), `ResolveResolution`, `ConfigureTmuMemoryLimits`, `ApplySharedStateToBackend`, combiner/blend/alpha/depth/texture/fog/chromakey/dither/gamma dispatchers, `SanitizeVertex`, `DrawVertexList`, telemetry stats, LFB helpers, 3df mapping/loading, splash/banner rendering, and DOSBox `setConfig`/`setConfigRes` handshakes.
  - Line counts: `src/glide2/glide2_entry.cpp` 2,846 → 2,231 LOC (−615); `src/glide3/glide3_entry.cpp` 2,109 → 1,509 LOC (−600); removed unused `tableIndexToW[64]` in `glide3_entry.cpp`.
  - Clean build (`cmake --build build -j$(nproc)`): **0 warnings, 0 errors** under `-Wall -Wextra -Wpedantic -Werror`.
  - ABI export parity (`nm -D --defined-only` before vs. after): `libglide2x.so` **145/145 identical** (0 missing, 0 added); `libglide3x.so` **104/104 identical** (0 missing, 0 added).
  - Automated suites: `Glide3xWrapperTests` **42/42 passed (1956 assertions)** on `software`, `vulkan`, and `opengl_es`; `Glide2xWrapperTests` **27/27 passed (2344 assertions)** on `software`; `glide_embed_test --raw-handle --parent-gl --smoke-ms 600` **6/6 PASS** across `{wayland, x11} × {vulkan, opengl_es, software}`.
- **Target Files**:
  - *New*: `src/core/GlideEntryCommon.h`, `src/core/GlideEntryCommon.cpp`
  - `src/glide2/glide2_entry.cpp` (2,796 LOC)
  - `src/glide3/glide3_entry.cpp` (2,193 LOC)
- **What to Do**:
  1. Extract duplicated resolution tables, pixel-format/combiner/blend translation helpers, window lifecycle boilerplate, splash invocation, and `setConfig` exports into `GlideEntryCommon`.
  2. Keep exported C ABI signatures (`gr*`) untouched and verify exported symbols via `nm -D build/lib/libglide2x.so` and `libglide3x.so` before & after!
- **Automated Gate**: Symbol diff check (`nm -D --defined-only`) shows zero missing ABI exports + `Glide2xWrapperTests` + `Glide3xWrapperTests` pass.
- **User Verification**: Confirm both Glide 2.x and Glide 3.x demos + DOSBox games load and run identically.

#### `T3.3` — Split Monolithic Backend Implementation Files
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commits `f857e49`, `1765199`, `4f00b30`)
- **Automated Result**:
  - **T3.3a (`f857e49`) — Vulkan**: Split `VulkanBackend.cpp` (3,987 LOC) into `VulkanBackend.cpp` (2,105), `VulkanPipeline.cpp` (614), `VulkanTextures.cpp` (394), `VulkanLfb.cpp` (262), and `VulkanDraw.cpp` (658). 0 `-Werror` warnings; `Glide3xWrapperTests` (`vulkan`) 42/42 + `Glide2xWrapperTests` 27/27 passed.
  - **T3.3b (`1765199`) — OpenGL ES**: Split `OpenGLESBackend.cpp` (3,468 LOC) into `OpenGLESBackend.cpp` (1,796), `OpenGLESShaders.cpp` (942), `OpenGLESTextures.cpp` (222), `OpenGLESDraw.cpp` (531), plus internal `OpenGLESInternal.h`. 0 `-Werror` warnings; `Glide3xWrapperTests` (`opengl_es`) 42/42 + `Glide2xWrapperTests` 27/27 passed.
  - **T3.3c (`4f00b30`) — Software**: Split `SoftwareBackend.cpp` (5,028 LOC) into `SoftwareBackend.cpp` (1,638), `SoftwareCombiner.cpp` (742), `SoftwareRasterizer.cpp` (1,233), `SoftwareRasterizerSimd.cpp` (1,415), plus internal `SoftwareInternal.h` (all compiled with `-mavx2`). 0 `-Werror` warnings; `sw_benchmark` 522.9k tris/s (vs 499.5k baseline, +4.7%), `Glide3xWrapperTests` (`software`) 42/42 + `Glide2xWrapperTests` 27/27 passed.
  - **Embed Smoke Matrix**: `glide_embed_test --raw-handle --parent-gl --smoke-ms 500` **6/6 PASS** across `{wayland, x11} × {vulkan, opengl_es, software}`.
- **Target Files** (execute as **3 separate sub-commits**: `T3.3a` Vulkan, `T3.3b` GLES, `T3.3c` Software):
  - `VulkanBackend.cpp` (4,229 LOC) → `VulkanBackend.cpp` (lifecycle/swapchain), `VulkanPipeline.cpp` (PSOs/descriptors/shaders), `VulkanTextures.cpp`, `VulkanLfb.cpp`, `VulkanDraw.cpp`
  - `OpenGLESBackend.cpp` (2,418 LOC) → `OpenGLESBackend.cpp` (EGL/lifecycle/present), `OpenGLESShaders.cpp`, `OpenGLESTextures.cpp`, `OpenGLESDraw.cpp`
  - `SoftwareBackend.cpp` (2,099 LOC) → `SoftwareBackend.cpp` (lifecycle/present/LFB), `SoftwareRasterizer.cpp`, `SoftwareCombiner.cpp`
- **Automated Gate**: Pure mechanical file split (zero logic changes per sub-commit) + full rebuild + Catch2 suites pass after each sub-commit.
- **User Verification**: Quick spot-check after all 3 splits complete.

#### `T3.4` — Audit & Reduce `std::recursive_mutex` Hot-Path Contention
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-15)` (Commit `4d64eeb`)
- **Automated Result**:
  - **100% of `std::recursive_mutex` eliminated across `src/`** (all 161 occurrences replaced with non-recursive `std::mutex` + `_Locked` internal helpers across `BackendManager`, `LfbManager`, `TextureManager`, `GlideStateTracker`, `SoftwareBackendBase`, `SoftwareBackend`, `VulkanBackend`, and `OpenGLESBackend`).
  - Eliminated redundant double-locking on every backend state setter (`VulkanBackend::Set*`, `OpenGLESBackend::Set*`, `BindTexture`), scissor/cull sync (`GetScissorRectVulkan_Locked`, `ApplyScissorGLES_Locked`, `SyncCullStateGLES_Locked`), LFB flush (`FlushLFBToGPU_Locked`), and lazy clear materialization (`FlushBins_Locked`, `MaterializeLazyCpuClear_Locked`).
  - Clean build with **0 `-Werror` warnings** across all `src/` targets.
  - **Zero deadlocks** across all Catch2 suites: `Glide3xWrapperTests` **42/42** on `software`, `vulkan`, and `opengl_es`; `Glide2xWrapperTests` **27/27** on `software` and `opengl_es` (`26/27` on `llvmpipe` Vulkan, where the lone failure is a pre-existing 350ms wall-clock FPS-cap timing bound on software Vulkan at 398ms, not a lock issue).
  - **Embed Smoke Matrix**: `glide_embed_test --raw-handle --parent-gl --smoke-ms 500` **6/6 PASS** across `{wayland, x11} × {vulkan, opengl_es, software}`.
  - **`sw_benchmark` regression check** (post-`T3.4` vs post-`T3.3c` baseline):
    - Clear 640×480 / 1024×768 / 2048×1536: **3.81 / 3.79 / 3.78 GB/s** (vs 3.75 / 3.79 / 3.76 GB/s)
    - LFB Write Unmasked / Masked: **433.4M / 482.8M px/s** (vs 463M / 455M px/s)
    - Rasterizer: **511.8k tris/s** (vs 499.5k–523.0k tris/s baseline band — neutral)
- **Target Files**:
  - `src/backends/GlideStateTracker.h` / `SoftwareBackendBase.h`
  - `src/backends/*/` (149 existing `recursive_mutex` lock sites)
- **What to Do**:
  1. Audit call graphs to separate public API-entry locking (single non-recursive `std::mutex` at frontend/backend boundary) from internal helper methods (which assume lock is already held, using `_Locked` naming convention).
  2. Benchmark before & after with `python3 tests/benchmark.py` and `./build/bin/sw_benchmark`.
- **Automated Gate**: Zero deadlocks under Catch2 suites + benchmark shows neutral or improved throughput.
- **User Verification**: Confirm smooth gameplay in GTA 1 without stalls/deadlocks.

---

### Phase 4: Optional Decision Gate — Evaluate Option B (Thin ~50 LOC DOSBox Patch)

> **Status**: `[x] CLOSED — NOT NEEDED (USER CONFIRMED — 2026-11-15)` — Option A works cleanly across unpatched DOSBox-X and DOSBox SVN without modifying upstream emulators. Revisit only if a future DOSBox SVN edge case requires it.

After **Option A (`T2.7`)** is confirmed working by the user, evaluate these four concrete triggers:

| # | Trigger Question | Option A Sufficient? | If Triggered, Option B Patch Scope |
| :-: | :--- | :---: | :--- |
| 1 | Does mid-game **`Alt+Enter` fullscreen toggling** work acceptably in DOSBox-X without restarting Glide? | `[ ] Yes / [ ] No` | ~15 LOC in `dosbox-x/src/hardware/glide.cpp`: call `GLIDE_ResetScreen(true)` (or notify wrapper resize callback) on fullscreen toggle. |
| 2 | Does DOSBox-X on **native Wayland** (`statWMInfo()` aliasing `x11.window`) work reliably across all output modes? | `[ ] Yes / [ ] No` | ~15 LOC in `dosbox-x/src/hardware/glide.cpp`: pass actual `SDL_Window*` via `setConfig('SDL2')` handshake (matching SVN DOSBox). |
| 3 | Does DOSBox-X's **top menu bar / mapper GUI** remain usable (or acceptably hidden) while Glide is active? | `[ ] Yes / [ ] No` | ~15 LOC in `dosbox-x`: pass exact client viewport `(x, y, w, h)` offset below menu bar & pause/hide child surface when mapper opens. |
| 4 | Does **SVN DOSBox (`dosbox-0`)** handle fullscreen/windowed transitions cleanly? | `[ ] Yes / [ ] No` | ~10 LOC in `dosbox-0/src/hardware/glide.cpp`: forward resize/fullscreen notification if needed. |

- **If all 4 are "Yes"** → **Close Phase 4 as Not Needed.** Option A delivers a 100% drop-in unpatched wrapper.
- **If any trigger is "No"** → Draft a minimal `Option B` addendum building directly on `NativeSurfaceHost` (zero wasted work from Phase 2!).

---

### Post-Roadmap / Parked Game Compatibility Investigations

> **Policy**: Parked until Phase 2 (Option A) and Phase 3 (Cleanup) complete so windowing/WSI changes stay isolated from game-specific rendering fixes.

- **Carmageddon (`carma`) — Vulkan / Glide 2.x (Reported 2026-11-15)**
  - **Observed Symptoms**:
    1. Menu screens render completely black (blind navigation still allows starting a race).
    2. In-game 3D renders, but some graphics/UI elements are missing (likely same root cause as the black menus — suspect LFB writes, palette/chroma-key, or Glide 2.x 2D/UI path).
    3. Required switching window focus a couple of times after launch before keyboard input registered reliably.
  - **Status**: `[~] IMPLEMENTED IN M5 — AWAITING USER VERIFICATION` (Pass 1 fixed persistent `GR_LFB_WRITE_ONLY` write-lock flushing across `grBufferSwap`, `GR_LFB_NOIDLE` masking, zero-readback sparse alpha-masked GPU LFB overlay compositing on Vulkan & OpenGL ES, non-clobbering `grShamelessPlug`, lazy `TextureManager` palette swizzling, and Vulkan/GLES state-dedup & batching; Pass 2 fixed `grTexDownloadMipMap` full-replacement + overlapping TMU range eviction, COW texture snapshots for Software's deferred rasterizer, exact 8-bit GPU chromakey tolerance on Vulkan & GLES, per-TMU GPU texture maps, and GLES empty-mip upload crash protection; awaiting live Carmageddon verification across all 3 backends).

- **Glide 2.x SDK `test16.exe` — Shameless Plug State Leak on GPU Backends (Reported 2026-11-15)**
  - **Reference**: `refs/glide/glide2x/sst1/glide/tests/test16.c` (`grGlideShamelessPlug` toggled via `'P'`)
  - **Observed Symptoms**:
    1. Pressing `'P'` in `software` mode shows the 3dfx shameless plug overlay normally.
    2. Pressing `'P'` in `vulkan` and `opengl_es` modes shows the plug **and** inverts the triangle colors (or mirrors the picture) — likely the GPU shameless-plug overlay pass mutates backend/Glide render state (color format, combiner, or origin/projection) without restoring it.
  - **Status**: `[x] DONE IN M4 (USER CONFIRMED — 2026-11-15)` (fixed in-place ABGR swizzle corruption of `m_lfbStaging` in `VulkanBackend::ReadLFB` & `OpenGLESBackend::ReadLFB`, guarded `RenderBanner()` early-exit, and saved/restored `lfbConstantAlpha` & `lfbWriteColorFormat` in `grGlideGetState`/`grGlideSetState`).

- **Glide 2.x SDK `test22.exe` — Visual Quality Regression in Software Mode (Reported 2026-11-15)**
  - **Reference**: `refs/glide/glide2x/sst1/glide/tests/test22.c`
  - **Observed Symptoms**: Renders noticeably worse in `software` mode compared to `vulkan`.
  - **Status**: `[x] DONE IN M4 (USER CONFIRMED — 2026-11-15)` (fixed `GR_BLEND_PREFOG_COLOR` using post-fog color, `largeLod > 0` mipmap LOD offset in scalar & SIMD rasterizers, and `idx == 63` fog-table OOB read; SW vs. VK mean pixel diff reduced from 32.69 to 0.06 scalar / 0.04 SIMD).

- **Glide 2.x SDK `test25.exe` & `test27.exe` — Hang Across All Backends (Reported 2026-11-15)**
  - **Reference**: `refs/glide/glide2x/sst1/glide/tests/test25.c`, `test27.c`
  - **Observed Symptoms**: Both tests hang in all backend modes (`vulkan`, `opengl_es`, `software`).
  - **Status**: `[x] DONE IN M4 (USER CONFIRMED — 2026-11-15)` (diagnosed blocking terminal stdin `tlGetCH()` after frame 1 on Wayland CSD; fixed standalone event pumping, OpenGL ES/Software `PollEvents()` & clean close, unlocked `m_mutex` during frame pacing, and kept `SDL_INIT_VIDEO` alive across `test27` open/close cycles).
