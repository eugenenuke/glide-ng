# Workspace Rules & Agent Manifest (`AGENTS.md`)

This document is the authoritative system-level manifest for AI coding agents collaborating on the **`glide-ng` (3dfx Glide 2.x / 3.x Modernization Wrapper)** repository.

> **Active Roadmap**: See [docs/ROADMAP.md](../docs/ROADMAP.md) for current and upcoming milestones, and [docs/HISTORY.md](../docs/HISTORY.md) / [CHANGELOG.md](../CHANGELOG.md) for completed work.

---

## 1. Repository Architecture

```text
3dfx/
├── .agents/                          # Authoritative agent rules & domain skills
│   ├── AGENTS.md                     # This manifest
│   └── skills/                       # Specialized skills (loaded on demand)
├── docs/                             # Structured documentation
│   ├── README.md                     # Documentation hub
│   ├── ROADMAP.md                    # Active & upcoming milestones
│   ├── HISTORY.md                    # Concise pre-beta retrospective
│   ├── user-guide/                   # End-user manuals
│   ├── developer-guide/              # Contributor technical references
│   └── catalog/                      # Per-title guides & drop-in glide-ng.ini presets
├── include/                          # Public Glide headers (3dfx/, glide2/, glide3/, glide_ng/)
├── src/
│   ├── core/                         # Config, Logger, NativeSurfaceHost, VertexLayout, Textures, LFB, SIMD
│   ├── glide211/                     # Glide 2.11 C ABI exports (libglide.so / glide.dll)
│   ├── glide2/                       # Glide 2.x C ABI exports (libglide2x.so / glide2x.dll)
│   ├── glide3/                       # Glide 3.x C ABI exports (libglide3x.so / glide3x.dll)
│   ├── dos_ovl/                      # Clean-room GPL-3.0 DOS/32 GLIDE2X.OVL bridge
│   └── backends/
│       ├── vulkan/                   # Primary GPU backend (Vulkan / SPIR-V)
│       ├── opengl_es/                # Secondary GPU backend (EGL OpenGL ES 3.2 / Win32 WGL 3.3 Core)
│       └── software/                 # CPU reference rasterizer (Scalar / AVX2 / AVX-512)
├── tests/                            # Modular self-contained Catch2 suites (glide211/, glide2/, glide3/)
├── demos/                            # Standalone Glide 2/3 visual & validation demos
├── tools/                            # Portable C99 tools (glide_info, glide_bench, glide_envtest, cube/gears)
├── cmake/                            # Cross-build integrations (MinGW-w64 Win32, Open Watcom DOS/32)
├── scripts/                          # Release & maintenance utilities (bump_version.sh, package_release.sh)
├── VERSION                           # Single source of truth for SemVer 2.0 version
└── vendor/                           # Local git-ignored toolchains, games, demos, & optional reference clones
```

---

## 2. Mandatory Execution & Tracking Policies

### Policy A: Strict 3-State Task Confirmation Gate
When working from [docs/ROADMAP.md](../docs/ROADMAP.md) or any milestone tracker:
- `[ ] PENDING` — Not started.
- `[~] IMPLEMENTED — AWAITING USER VERIFICATION` — Code compiles cleanly and automated tests pass. **Agents MUST stop at this state and ask the developer to confirm.**
- `[x] DONE (USER CONFIRMED — YYYY-MM-DD)` — **NEVER** mark a task `DONE` until the developer explicitly confirms it works as intended.

### Policy B: Controllable, Bisectable Commits
- **One logical unit = one isolated git commit.** Never mix unrelated cleanups with rendering/windowing changes.
- Every commit must compile cleanly (`cmake --build build -j$(nproc)`, zero `-Werror` warnings across Linux, Win32 MinGW, and optional DOS Watcom) and must not regress the Catch2 test baseline.
- Prefer `git revert` / `git bisect` over layering speculative workarounds when a regression appears.

### Policy C: Catch2 TDD Mandate
- Every bug fix, config parser change, state addition, or math/geometry helper **must** be accompanied by a Catch2 unit test under [tests/glide211/](../tests/glide211/), [tests/glide2/](../tests/glide2/), or [tests/glide3/](../tests/glide3/).
- Run headless verification with `GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build --output-on-failure` (see the `build-and-test` skill for exact commands).

### Policy D: Local Third-Party Directories (`vendor/` and `ext_tests/`)
- **NEVER** edit, stage, or commit files inside git-ignored `vendor/` (which holds local games, toolchains, and optional reference clones under `vendor/refs/`) or `ext_tests/`.
- Always point external DOSBox/Wine runs to freshly built outputs under `build/lib/` or `build_release/win32/bin/`.

### Policy E: Platform, WSI & Public-Repo Safety Rules
- Use standard compiler macros: `#if defined(__unix__) && !defined(__APPLE__)` (NEVER `#if defined(UNIX)`).
- Never call `SDL_CreateWindow()` and attempt `wl_subcompositor_get_subsurface()` on the same `wl_surface` (fatal Wayland role violation).
- Never call `XInitThreads()` inside a `dlopen` constructor after a host client (DOSBox) has already opened X11; use a private `XOpenDisplay(nullptr)` connection instead.
- All backends must honor `m_config.forceNoWindow` (`GLIDE_NG_HEADLESS=1`) without initializing OS windowing/EGL/Vulkan presentation surfaces.
- This is a **public** repository: never commit personal filesystem paths, usernames, hostnames, or third-party proprietary binaries.

---

## 3. Domain Skills Directory

Load the relevant skill under `.agents/skills/` before modifying files in its domain:

| Skill | When to Load |
| :--- | :--- |
| `tech-writer` | Authoring, refactoring, or reviewing any Markdown documentation (`README.md`, `CHANGELOG.md`, `docs/**`). |
| `wsi-window-injection` | Modifying `NativeSurfaceHost`, X11 child windows, Wayland subsurfaces, Win32 `HWND` hosting, aspect-ratio blitting, or DOSBox-X / SVN DOSBox integration. |
| `build-and-test` | Configuring CMake, running `ctest`, `Glide2xWrapperTests`, `Glide3xWrapperTests`, `glide_bench`, Win32/DOS cross-builds, or launching DOSBox/Wine against build outputs. |
| `3dfx-expert` | Modifying `src/glide2/`, `src/glide3/`, state serialization (`grGlideGetState`/`SetState`), Glide ABI exports, or verifying 3dfx hardware specs. |
| `vulkan-expert` | Modifying `src/backends/vulkan/` (swapchain, PSOs, shaders, SPIR-V build, barriers, LFB, GPU RTT). |
| `gles-expert` | Modifying `src/backends/opengl_es/` (EGL/WGL surface/context, GLSL ES 3.20 / GL 3.30 shaders, Gamma LUT presentation). |
| `rasterizer-oracle` | Modifying `src/backends/software/` or `src/core/SimdKernels*` (reference rasterizer, AVX2/AVX-512 SIMD, LFB row flipping). |
| `visual-validator` | Capturing cross-backend visual parity snapshots (`FX_GLIDE_SNAPSHOT`) or running interactive demos and verification harnesses. |

---

## 4. Review & Verification Workflow

For non-trivial changes, follow this disciplined loop:
1. **Plan & Scope**: Confirm the exact milestone/task in [docs/ROADMAP.md](../docs/ROADMAP.md) and keep edits strictly within scope.
2. **Implement**: Load relevant domain skill(s), write minimal clean C++20, and add/update Catch2 unit tests.
3. **Self-Audit**: Check DRYness, state serialization symmetry (`grGlideGetState`/`SetState`), resource ownership/teardown order, and compiler warnings (`-Wall -Wextra -Wpedantic -Werror`).
4. **Verify**: Build cleanly and run automated tests (`ctest` / `Glide2xWrapperTests` / `Glide3xWrapperTests` / `glide_envtest`).
5. **Commit & Hand Off**: Create isolated commits, mark tasks `[~] IMPLEMENTED — AWAITING USER VERIFICATION` in [docs/ROADMAP.md](../docs/ROADMAP.md), and provide exact manual test commands for developer sign-off.
