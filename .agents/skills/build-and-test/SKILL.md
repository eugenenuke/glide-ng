---
name: build-and-test
description: Verified CMake build commands, Catch2/CTest execution, benchmarks, Win32/DOS cross-builds, and DOSBox/Wine launch commands for glide-ng.
---

# Workspace Skill: Build, Test & Runtime Verification

Activate this skill whenever configuring CMake, compiling targets, running Catch2 unit tests, running `glide_bench`, or launching standalone demos, Wine, and DOSBox clients.

## 1. Configure & Build

```bash
# Configure Debug or Release (add -G Ninja when available for faster clean/incremental builds)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release

# Fast Linux-only build (skips MinGW-w64 Win32 cross-build when editing core/backends)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja -DGLIDE_BUILD_WIN32=OFF

# Disable precompiled header for <vulkan/vulkan.hpp> if debugging include hygiene / toolchain quirks
cmake -S . -B build -DGLIDE_USE_PCH=OFF

# Build everything (Linux libs, tests, demos, tools + auto Win32 MinGW & DOS Watcom if installed)
cmake --build build -j$(nproc)

# Fast targeted build of Catch2 test suites only
cmake --build build -j$(nproc) --target Glide211WrapperTests Glide2xWrapperTests Glide3xWrapperTests

# Build only 32-bit Win32 DLLs (when MinGW-w64 i686 is installed)
cmake --build build_release -j$(nproc) --target win32_dlls
```

- Core CMake options: `GLIDE_STRICT_WARNINGS` (`ON`), `GLIDE_USE_PCH` (`ON`, precompiles `<vulkan/vulkan.hpp>` for the 7 Vulkan-consuming TUs only), `GLIDE_BUILD_WIN32` (`ON` in `cmake/MinGwWin32.cmake`), `GLIDE_STATIC_CPP_RUNTIME` (`OFF`, enable for portable Linux release binaries), `ENABLE_TELEMETRY` (`OFF`).
- Software rasterizer build sharding (`src/CMakeLists.txt`): splits scalar (`8`), AVX2 (`4`), and AVX-512 (`4`) template instantiations under `GLIDE_RAST_SHARD` for parallel compilation.
- Version decoupling: `tools/gen_version_header.cmake` only forces rebuild of `build_version.h` consumers (`src/core/Logger.cpp`, `src/core/WrapperConfig.cpp`, `tests/glide3/test_glide3_catalog.cpp`, and Win32 `.rc` files), avoiding full-tree recompiles across commits.
- Linux libraries: `<build>/lib/libglide.so`, `<build>/lib/libglide2x.so`, and `<build>/lib/libglide3x.so`.
- Linux tools & tests: `<build>/bin/` (`Glide211WrapperTests`, `Glide2xWrapperTests`, `Glide3xWrapperTests`, `glide2_info`, `glide3_info`, `glide2_bench`, `glide3_bench`, `glide2_envtest`, `glide3_envtest`, `glide2_cube`, `glide3_cube`, `glide2_gears`, `glide3_gears`, demos).
- Auto-built Win32 binaries: `<build>/win32/bin/` (`glide.dll`, `glide2x.dll`, `glide3x.dll`, `glide*_info.exe`, `glide*_bench.exe`, `glide*_envtest.exe`).
- Auto-built DOS/32 binaries (when Open Watcom v2 is installed under `vendor/watcom` or `$WATCOM`): `<build>/dos/` (`glide2x.ovl`, `glide_info.exe`, `glide_bench.exe`, `glide_envtest.exe`).

## 2. Automated Unit Tests (Headless)

Always use canonical `GLIDE_NG_*` environment variables with `GLIDE_NG_HEADLESS=1`:

```bash
# Discover all registered Catch2 tests (66 domain test cases total)
ctest --test-dir build -N

# Run all Catch2 tests in parallel via CTest on Software backend
GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1 ctest --test-dir build -j4 --output-on-failure

# Run Glide 2.11 / 2.x / 3.x Catch2 suites directly
GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1 ./build/bin/Glide211WrapperTests
for be in software opengl_es vulkan; do
  GLIDE_NG_BACKEND=$be GLIDE_NG_HEADLESS=1 ./build/bin/Glide2xWrapperTests
done
GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1 ./build/bin/Glide3xWrapperTests

# Validate catalog presets & version macros
GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1 ./build/bin/Glide3xWrapperTests "[Catalog],[Version]"
```

*(Note: Do **not** set `FX_GLIDE_NO_SPLASH=1` when running the full `Glide2xWrapperTests` suite, as splash animation tests verify startup state.)*

## 3. Benchmarks & Verification Tools

```bash
# Automated INI / authentic 3dfx env var verification
GLIDE_NG_HEADLESS=1 ./build/bin/glide2_envtest --verify --test-reopen
GLIDE_NG_HEADLESS=1 ./build/bin/glide3_envtest --verify --test-reopen

# Uncapped Release benchmark (Voodoo5, 14 scenes)
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan GLIDE_NG_CARD_MODEL=voodoo5 \
  GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0 \
  ./build_release/bin/glide3_bench --scene all --res 640 --no-hud --seconds 3
```

## 4. Running Standalone Demos, DOSBox & Wine

```bash
# Standalone Linux demo (hWnd == 0)
GLIDE_NG_BACKEND=vulkan GLIDE_NG_HUD=fps,backend ./build_release/bin/glide3_cube

# Enable verbose wrapper logging (outputs to ./glide-ng.log and stdout)
GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1 ./build_release/bin/glide3_cube

# Run with Vulkan validation layers
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation GLIDE_NG_BACKEND=vulkan ./build/bin/glide3_cube

# Launch DOSBox-X against build_release/lib
LD_LIBRARY_PATH="$PWD/build_release/lib:${LD_LIBRARY_PATH:-}" \
  GLIDE_NG_BACKEND=vulkan dosbox-x -conf dosbox.conf

# Launch Win32 title under Wine (with glide.dll/glide2x.dll/glide3x.dll next to the .exe)
WINEDLLOVERRIDES="glide,glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./game.exe
```

## 5. ABI Symbol Verification (After Frontend Refactors)

```bash
# Expected defined Glide export counts: libglide.so=161, libglide2x.so=152, libglide3x.so=129
nm -D --defined-only build/lib/libglide.so   | awk '$2=="T"{print $3}' | grep -E '^(gr|gu|ConvertAndDownloadRle|setConfig)' | sort
nm -D --defined-only build/lib/libglide2x.so | awk '$2=="T"{print $3}' | grep -E '^(gr|gu|ConvertAndDownloadRle|setConfig)' | sort
nm -D --defined-only build/lib/libglide3x.so | awk '$2=="T"{print $3}' | grep -E '^(gr|gu|setConfig)' | sort
```
