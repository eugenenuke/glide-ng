# Workspace Rules & Agent Manifest (`AGENTS.md`)

This document is the single authoritative system-level manifest for AI coding agents collaborating on the **`project-ng` (3dfx Glide 2.x / 3.x Wrapper)** repository.

> **Active Tracking Plan**: See [docs/plans/option_a_and_cleanup_tracker.md](../docs/plans/option_a_and_cleanup_tracker.md) for the current phased roadmap, baseline test results, and per-task verification checklists.

---

## 1. Repository Architecture

```text
3dfx/
├── .agents/                          # Authoritative agent rules & domain skills
│   ├── AGENTS.md                     # This manifest
│   └── skills/                       # Specialized skills (loaded on demand)
├── docs/                             # Engineering playbooks, trackers, & plans
│   ├── plans/option_a_and_cleanup_tracker.md
│   └── troubleshooting_playbook.md
├── include/                          # Public Glide headers (3dfx/, glide2/, glide3/)
├── src/
│   ├── core/                         # Config, Logger, NativeSurfaceHost, VertexLayout, Textures, LFB
│   ├── glide2/glide2_entry.cpp       # Glide 2.x C ABI exports (libglide2x.so)
│   ├── glide3/glide3_entry.cpp       # Glide 3.x C ABI exports (libglide3x.so)
│   └── backends/
│       ├── vulkan/                   # Primary GPU backend (Vulkan 1.4 / SPIR-V)
│       ├── opengl_es/                # Secondary GPU backend (EGL + OpenGL ES 3.2)
│       └── software/                 # CPU reference rasterizer (ground-truth oracle)
├── tests/                            # Catch2 suites (Glide2xWrapperTests, Glide3xWrapperTests) & benchmarks
├── demos/                            # Standalone Glide 2/3 visual & validation demos
├── tools/                            # Diagnostic tools (glide_info, spv_to_hex.py, cube/gears)
├── refs/                             # Pristine reference trees (READ-ONLY)
└── ext_tests/                        # External test suites & games (READ-ONLY, gitignored)
```

---

## 2. Mandatory Execution & Tracking Policies

### Policy A: Strict 3-State Task Confirmation Gate
When working from [docs/plans/option_a_and_cleanup_tracker.md](../docs/plans/option_a_and_cleanup_tracker.md) or any project tracker:
- `[ ] PENDING` — Not started.
- `[~] IMPLEMENTED — AWAITING USER VERIFICATION` — Code compiles cleanly and automated tests pass. **Agents MUST stop at this state and ask the developer to confirm.**
- `[x] DONE (USER CONFIRMED — YYYY-MM-DD)` — **NEVER** mark a task `DONE` until the developer explicitly confirms it works as intended.

### Policy B: Controllable, Bisectable Commits
- **One task = one isolated git commit.** Never mix unrelated cleanups with windowing/rendering changes, and never migrate multiple backends in a single commit.
- Every commit must compile cleanly (`cmake --build build -j$(nproc)`) and must not regress the Catch2 test baseline.
- Prefer `git revert` / `git bisect` over layering speculative workarounds when a regression appears.

### Policy C: Catch2 TDD Mandate
- Every bug fix, config parser change, state addition, or math/geometry helper **must** be accompanied by a Catch2 unit test in [tests/test_glide3.cpp](../tests/test_glide3.cpp) or [tests/test_architecture.cpp](../tests/test_architecture.cpp).
- Run headless verification with `GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1` (see the `build-and-test` skill for exact commands and known pre-existing baseline quirks).

### Policy D: Pristine Reference & External Directories (`refs/` and `ext_tests/`)
- **NEVER** edit, stage, or commit files inside `refs/` or `ext_tests/`.
- **NEVER** overwrite pre-built reference libraries in `ext_tests/build_tests/libs/`. Always point external tests/DOSBox to our build via `LD_LIBRARY_PATH=$PWD/build/lib`.
- To experiment with a reference C test file, copy it to `./scratch/` first (Experimentation Scratch Protocol) and modify only the copy.

### Policy E: Platform & WSI Safety Rules
- Use standard compiler macros: `#if defined(__unix__) && !defined(__APPLE__)` (NEVER `#if defined(UNIX)` — GCC/Clang do not define `UNIX`).
- Never call `SDL_CreateWindow()` and attempt `wl_subcompositor_get_subsurface()` on the same `wl_surface` (fatal Wayland role violation).
- Never call `XInitThreads()` inside a `dlopen` constructor after a host client (DOSBox) has already opened X11; use a private `XOpenDisplay(nullptr)` connection instead.
- All backends must honor `m_config.forceNoWindow` (`GLIDE_NG_HEADLESS=1`) without initializing OS windowing/EGL/Vulkan presentation surfaces.

---

## 3. Domain Skills Directory

Load the relevant skill under `.agents/skills/` before modifying files in its domain:

| Skill | When to Load |
| :--- | :--- |
| `wsi-window-injection` | Modifying `NativeSurfaceHost`, `PlatformWindowUtils`, X11 child windows, Wayland subsurfaces, EGL/Vulkan WSI, aspect-ratio blitting, or DOSBox-X / SVN DOSBox integration. |
| `build-and-test` | Configuring CMake, running `ctest`, `Glide2xWrapperTests`, `Glide3xWrapperTests`, `tests/run_all_tests.py`, `tests/benchmark.py`, or launching DOSBox against `build/lib/`. |
| `3dfx-expert` | Modifying `src/glide2/`, `src/glide3/`, state serialization (`grGlideGetState`/`SetState`), Glide ABI exports (`setConfig`), or verifying Voodoo 1/2 hardware specs. |
| `vulkan-expert` | Modifying `src/backends/vulkan/` (swapchain, PSOs, shaders, SPIR-V build, barriers, LFB). |
| `gles-expert` | Modifying `src/backends/opengl_es/` (EGL surface/context, GLSL ES 3.20 shaders, Gamma LUT presentation). |
| `rasterizer-oracle` | Modifying `src/backends/software/` or `src/backends/SoftwareBackendBase.*` (reference rasterizer, SIMD, LFB row flipping). |
| `visual-validator` | Capturing visual parity screenshots, RLE frame dumps (`-d`), or running interactive legacy SDK tests via timed stdin piping. |

---

## 4. Review & Verification Workflow

For non-trivial changes, follow this disciplined loop:
1. **Plan & Scope**: Confirm the exact task ID in `docs/plans/option_a_and_cleanup_tracker.md` and keep edits strictly within that task's scope.
2. **Implement**: Load relevant domain skill(s), write minimal clean C++20, and add/update Catch2 unit tests.
3. **Self-Audit**: Check DRYness, state serialization symmetry (`grGlideGetState`/`SetState`), resource ownership/teardown order, and compiler warnings (`-Wall -Wextra -Wpedantic`).
4. **Verify**: Build cleanly and run automated tests (`ctest` / `Glide3xWrapperTests` / `Glide2xWrapperTests` / targeted demos).
5. **Commit & Hand Off**: Create one isolated commit, mark the task `[~] IMPLEMENTED — AWAITING USER VERIFICATION` in the tracker, and provide exact manual test commands for developer sign-off.
