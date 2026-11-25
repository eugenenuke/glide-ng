# glide-ng Documentation

Documentation for **glide-ng**, a modern open-source 3dfx Glide 2.x and Glide 3.x wrapper targeting Vulkan, OpenGL ES / OpenGL 3.3 Core, and SIMD Software rendering on Linux, Win32/Wine, and DOSBox.

---

## User Guide (`user-guide/`)

Task-oriented guides for building, configuring, running, benchmarking, and troubleshooting classic Glide titles.

| Guide | Topics Covered |
| :--- | :--- |
| **[Building & Installing](user-guide/building-and-installing.md)** | Prerequisites (Debian/Ubuntu, Arch, Fedora, Windows), Linux builds, automatic & standalone 32-bit Win32 MinGW-w64 DLL builds, optional Open Watcom v2 DOS/32 tools, and Wine/Proton + DOSBox setup. |
| **[DOSBox Glide Launcher](user-guide/dosbox-launcher.md)** | Modular `glide-dosbox` / `./run.sh` launcher for DOSBox-SVN, DOSBox-X, and DOSBox Staging (`init`, `install`, `patch`, `setup`, `shell`, `doctor`, `--emu` × `--wrapper`, `--gpu`, and debug scenarios). |
| **[Configuration & Overrides](user-guide/configuration.md)** | Precedence rules, `glide-ng.ini` sections, all `GLIDE_NG_*` overrides, authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` environment variables, logging, CSV telemetry, and on-screen HUD (`GLIDE_NG_HUD`). |
| **[Hardware Profiles & Glide Versions](user-guide/hardware-and-versions.md)** | All 7 emulated 3dfx board profiles (`VoodooGraphics` through `Voodoo5`), authentic Glide 2.x (`2.11`–`2.61`) and Glide 3.x (`3.00`–`3.10`) matrices, capability queries, and feature gating. |
| **[Benchmarking & Performance](user-guide/benchmarking.md)** | Running `glide2_bench` / `glide3_bench`, Software SIMD tiers (`scalar`/`avx2`/`avx512`), comparing against Mesa `llvmpipe`, reference Ryzen 9 7945HX + RTX 4070 results, and backend trade-offs. |
| **[Troubleshooting](user-guide/troubleshooting.md)** | Diagnosing missing DLLs, Wine/Proton overrides, Wayland/X11 windowing, dark SST-1 gamma, performance bottlenecks, frame snapshots (`FX_GLIDE_SNAPSHOT`), and log capture recipes. |
| **[Reporting Bugs](user-guide/reporting-bugs.md)** | Step-by-step instructions for collecting `glide-ng.log`, cross-backend checks, screenshots, crash/hang backtraces, and new game catalog submissions when filing a GitHub issue. |

---

## Compatibility Catalog (`catalog/`)

- **[Game, Demo & Benchmark Compatibility Catalog](catalog/README.md)** — Per-title guides and validated drop-in `glide-ng.ini` presets for commercial games, official 3dfx tech demos, hardware tests, and `glide-ng` utilities.

---

## Developer Guide (`developer-guide/`)

Technical references for contributors working on the wrapper core, frontends, backends, test suites, or releases.

| Guide | Topics Covered |
| :--- | :--- |
| **[Architecture](developer-guide/architecture.md)** | Core/Frontend/Backend layering, `NativeSurfaceHost` WSI hosting, `GlideStateTracker`, `TextureManager`, `LfbManager`, and runtime SIMD dispatch. |
| **[API Coverage & ABI](developer-guide/api-coverage.md)** | Glide 2.x (152 exports) and Glide 3.x (129 exports) surface summary, VSA-100 extensions, Win32 `__stdcall` export conventions, and API wrapping vs. register-level emulation. |
| **[Rendering Backends](developer-guide/backends.md)** | Vulkan SPIR-V/Early-Z/BCn/GPU-RTT pipeline, OpenGL ES 3.2 / WGL 3.3 Core backend, and Software AVX2/AVX-512 tiled rasterizer invariants. |
| **[Testing & Verification](developer-guide/testing.md)** | Modular self-contained Catch2 test suites (`Glide211WrapperTests`, `Glide2xWrapperTests`, `Glide3xWrapperTests`), `ctest`, `glide_envtest`, and catalog INI validation. |
| **[Versioning & Releasing](developer-guide/releasing.md)** | SemVer 2.0 policy, single-source `VERSION`, tag-aware `build_version.h`, Win32 PE `VS_VERSION_INFO`, `scripts/bump_version.sh`, and release tagging workflow. |

---

## Project History & Roadmap

- **[Changelog](../CHANGELOG.md)** — Release notes from `0.8.0-beta.1` onward.
- **[Project History](HISTORY.md)** — Concise retrospective of early milestones, architectural pivots, and key design decisions.
- **[Roadmap](ROADMAP.md)** — Active work, upcoming milestones (`glide.dll` Glide 2.11 support, flagship standalone demos), backlog, and path to `1.0.0`.
