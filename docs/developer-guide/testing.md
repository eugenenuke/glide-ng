# Testing & Verification

`glide-ng` includes fully self-contained, modular automated tests requiring zero external SDK archives, commercial games, or third-party demo binaries. All test targets compile cleanly under strict `-Wall -Wextra -Wpedantic -Werror`.

---

## 1. Automated Catch2 Test Suites (`tests/`)

Whenever `Catch2` v3 is installed, CMake builds three Catch2 v3 test executables and registers all **64** feature-named test cases with `ctest`:

| Target | Source Layout | Coverage |
| :--- | :--- | :--- |
| **`Glide211WrapperTests`** *(3 test cases)* | `tests/glide211/test_glide211_abi.cpp` | Glide 2.11 (`libglide.so` / `glide.dll`) pre-2.3 SST-1 ABI: `grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr` in-place mode reconfiguration, `guFbReadRegion`/`guFbWriteRegion`, unsigned `1<<19`/`1<<20` snap biases, and 2 MB TMU0 VRAM budget enforcement. |
| **`Glide2xWrapperTests`** *(25 test cases)* | `tests/glide2/*.cpp` (8 domain modules) | `test_glide2_abi.cpp` (exports & gating), `test_glide2_core_units.cpp` (`gu*`, `MathUtils`, `LfbManager`, `VertexLayout`, `.3df`, `PALETTE6666`), `test_glide2_geometry.cpp` (lines, Gouraud, framebuffer/depth, unsnapping, 12.4 wrap), `test_glide2_pipeline.cpp` (alpha, fog, multi-pass fog, chromakey, text, state, gamma), `test_glide2_lfb.cpp` (stride, aperture guard, persistent write-locks, `AUXBUFFER` depth readback), `test_glide2_textures.cpp`, `test_glide2_config_simd.cpp`, and `test_glide2_regressions.cpp`. |
| **`Glide3xWrapperTests`** *(36 test cases)* | `tests/glide3/*.cpp` (9 domain modules) | `test_glide3_state.cpp` (`grGet`, caps, state save/restore, splash), `test_glide3_pipeline.cpp` (text, `grVertexLayout`, W-buffer, `GR_CLIP_COORDS`, pre-fog, `FOGCOORD`, `PARGB`/`RGBA`/`BGRA`, software Z-buffer/blend stack), `test_glide3_textures.cpp` (`.3df`, mipmapping, `TEXMIRROR`, `TEXCHROMA`, `GR_MIPMAP_NEAREST`, `MSAA` centroid/gradients, palette broadcast), `test_glide3_vsa100.cpp` (VSA-100 `PIXEXT`, `Stencil`, `TEXFMT`, `COMBINE`, `TEXTUREBUFFER`, `BC1–BC3`, `GETGAMMA`), `test_glide3_lfb_depth.cpp`, `test_glide3_infra.cpp` (`IniConfigLoader`, env vars, `NativeSurfaceHost`, `Logger`, `Telemetry`, `HUD`), `test_glide3_simd.cpp`, `test_glide3_simd_vec.cpp` (`-mavx2`), and `test_glide3_catalog.cpp` (`[Catalog]`, `[Version]`). |

Shared synthetic `.3df` texture builders, `OpenGlideLibrary`/`OpenGlideSelfHandle` helpers, and temporary path RAII guards live in `tests/common/test_helpers.h`. Each test binary statically links `GlideWrapperCore` together with its own frontend entry `.cpp` (`-rdynamic`) so white-box tests inspecting internal C++ singletons share one coherent instance while production `libglide*.so` shared libraries hide all internal C++ symbols behind ELF version scripts (`libglide*.ver`). Black-box ABI/export tests `dlopen()` the real compiled `.so` artifacts under `lib/`.

### Running Tests

```bash
# Discover all registered tests (64 total)
ctest --test-dir build_release -N

# Run full suite via CTest (headless Software backend)
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build_release --output-on-failure -j$(nproc)

# Run Glide 2.11 suite
GLIDE_NG_HEADLESS=1 ./build_release/bin/Glide211WrapperTests

# Run Glide 2.x suite across all three backends
for be in software opengl_es vulkan; do
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=$be ./build_release/bin/Glide2xWrapperTests
done

# Run Glide 3.x suite on Software backend
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build_release/bin/Glide3xWrapperTests

# Run targeted Catch2 feature tags
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build_release/bin/Glide3xWrapperTests "[Catalog]"
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build_release/bin/Glide3xWrapperTests "[Version]"
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build_release/bin/Glide3xWrapperTests "[VSA100]"
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build_release/bin/Glide3xWrapperTests "[SIMD]"
```

> **Note**: Always pass `GLIDE_NG_HEADLESS=1` in CI/headless shells. Do not set `FX_GLIDE_NO_SPLASH=1` globally when running `Glide2xWrapperTests`, because splash/shameless-plug tests verify default splash behavior.

---

## 2. Environment Variable Verification Tools (`glide2_envtest` & `glide3_envtest`)

Pure C99 Glide test programs that render a 4-quadrant test card and assert LFB RGB565 readbacks against active INI and authentic 3dfx environment overrides:

```bash
# Baseline automated check
GLIDE_NG_HEADLESS=1 ./build_release/bin/glide2_envtest --verify --test-reopen
GLIDE_NG_HEADLESS=1 ./build_release/bin/glide3_envtest --verify --test-reopen

# Test specific overrides across backends
for be in vulkan opengl_es software; do
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=$be SSTV2_FORCE_TRILINEAR=1 \
    ./build_release/bin/glide3_envtest --verify
done
```

See [`docs/catalog/tools/glide-envtest/README.md`](../catalog/tools/glide-envtest/README.md) for the complete override verification matrix.

---

## 3. Cross-Compilation & Clean-Build Verification

Before merging changes that touch headers, CMake, core SIMD, or frontend exports:

1. **Zero warnings under `-Werror` (`-Wdev`)**: Confirm both Debug (`build/`) and Release (`build_release/`) configure and compile with zero warnings across Linux GCC (including `tests/`), Win32 MinGW (`win32_tools`), and optional DOS Open Watcom (`dos_tools`).
2. **Linux ELF ABI export check**: Verify only canonical C Glide symbols (`gr*`, `gu*`, `setConfig`, `setConfigRes`, and `ConvertAndDownloadRle` for Glide 2.11/2.x) are exported via `libglide*.ver`:
   ```bash
   nm -D --defined-only build_release/lib/libglide2x.so | grep -v ' [TW] \(gr\|gu\|setConfig\|ConvertAndDownloadRle\)'
   ```
3. **Per-TU ISA isolation check** (after editing `SimdKernels*` or `SoftwareRasterizer*`): Verify zero `ymm`/`zmm` instructions leak into baseline object files:
   ```bash
   objdump -d build_release/src/CMakeFiles/GlideWrapperCore.dir/core/SimdKernels.cpp.o | grep -E "ymm|zmm" || echo "Clean"
   ```
4. **Catalog INI validation**: Run `./build_release/bin/Glide3xWrapperTests "[Catalog]"` after editing `glide-ng.ini.example` or any `docs/catalog/**/glide-ng*.ini` file.
