---
name: build-and-test
description: Verified CMake build commands, Catch2/CTest execution, benchmarks, and DOSBox launch commands for glide-ng.
---

# Workspace Skill: Build, Test & DOSBox Verification

Activate this skill whenever configuring CMake, compiling targets, running Catch2 unit tests, running benchmarks, or launching standalone demos and DOSBox clients.

## 1. Configure & Build

```bash
# Configure (from repo root)
cmake -S . -B build

# Build everything (core libs, tests, demos, tools)
cmake --build build -j$(nproc)

# Fast targeted build of Catch2 test suites only
cmake --build build -j$(nproc) --target Glide2xWrapperTests Glide3xWrapperTests
```

- Output libraries: `build/lib/libglide2x.so` and `build/lib/libglide3x.so`.
- Output binaries: `build/bin/` (`Glide2xWrapperTests`, `Glide3xWrapperTests`, `glide2_cube`, `glide3_cube`, `gamma_showcase_demo`, `sw_benchmark`, `sw_validate`, etc.).

## 2. Automated Unit Tests (Headless)

Always use `GLIDE_WRAPPER_BACKEND` (do not set both `GLIDE_WRAPPER_BACKEND` and legacy `GLIDE_BACKEND`, as `JsonConfigLoader` flags that as an environment conflict) and `GLIDE_FORCE_NO_WINDOW=1`:

```bash
# Run Glide 3.x Catch2 suite on Software backend (expected: 100% PASS)
GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide3xWrapperTests

# Run Glide 2.x Catch2 suite on Software backend
GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ./build/bin/Glide2xWrapperTests

# Discover & run via CTest
GLIDE_WRAPPER_BACKEND=software GLIDE_FORCE_NO_WINDOW=1 ctest --test-dir build --output-on-failure

# Unified matrix script (supports --suite all|g2|g3 and --backend sw|vk|gles)
python3 tests/run_all_tests.py --suite g3 --backend sw
```

> [!NOTE]
> **Known Baseline Status** (see [option_a_and_cleanup_tracker.md](../../../docs/plans/option_a_and_cleanup_tracker.md)):
> - `Glide3xWrapperTests` on `software` is 100% green (32/32 tests after `T0.2`, zero `libEGL`/`ZINK` stderr warnings).
> - `Glide2xWrapperTests` currently has 3 pre-existing failures in `test_architecture.cpp` (plus a timing flake at line 466 on `llvmpipe` Vulkan). Always compare against the recorded baseline rather than assuming 0 failures before those are fixed.

## 3. Benchmarks & Performance Checks

```bash
python3 tests/benchmark.py
GLIDE_FORCE_NO_WINDOW=1 ./build/bin/sw_benchmark
```

Performance deltas within $\pm 3\%$ on `llvmpipe` are expected system noise.

## 4. Running Standalone Demos & DOSBox Clients

Never overwrite reference libraries under `ext_tests/`. Always inject our freshly built wrapper via `LD_LIBRARY_PATH`:

```bash
# Standalone demo (hWnd == 0)
GLIDE_WRAPPER_BACKEND=vulkan ./build/bin/glide3_cube
GLIDE_WRAPPER_BACKEND=opengl_es ./build/bin/gamma_showcase_demo
GLIDE_WRAPPER_BACKEND=software ./build/bin/glide3_cube

# Enable verbose wrapper logging (outputs to ./glide-ng.log and console)
GLIDE_WRAPPER_LOG_LEVEL=4 GLIDE_WRAPPER_LOG_TO_CONSOLE=1 ...

# Run with Vulkan validation layers
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation GLIDE_WRAPPER_BACKEND=vulkan ./build/bin/glide3_cube

# Launch DOSBox-X against our wrapper (XWayland vs. native Wayland)
LD_LIBRARY_PATH="$PWD/build/lib:${LD_LIBRARY_PATH:-}" GLIDE_WRAPPER_BACKEND=vulkan SDL_VIDEODRIVER=x11 dosbox-x ...
LD_LIBRARY_PATH="$PWD/build/lib:${LD_LIBRARY_PATH:-}" GLIDE_WRAPPER_BACKEND=vulkan SDL_VIDEODRIVER=wayland dosbox-x ...
```

## 5. ABI Symbol Verification (After Frontend Refactors)

```bash
nm -D --defined-only build/lib/libglide2x.so | awk '{print $3}' | sort
nm -D --defined-only build/lib/libglide3x.so | awk '{print $3}' | sort
```
