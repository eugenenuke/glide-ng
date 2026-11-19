# Testing & Verification

`glide-ng` includes fully self-contained automated tests requiring zero external SDK archives, commercial games, or third-party demo binaries.

---

## 1. Automated Catch2 Test Suites

Whenever `Catch2` is installed, CMake builds three Catch2 v3 test executables and registers all test cases with `ctest`:

| Target | Source | Coverage |
| :--- | :--- | :--- |
| `Glide211WrapperTests` | `tests/test_glide211.cpp` | 3 test cases covering the Glide 2.11 (`libglide.so` / `glide.dll`) pre-2.3 SST-1 ABI: `grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`, `guFbReadRegion`/`guFbWriteRegion`, and 2 MB TMU0 VRAM budget enforcement. |
| `Glide2xWrapperTests` | `tests/test_architecture.cpp` | 42 test cases covering Glide 2.x ABI exports, hardware/version gating, combiners, multi-texturing, mipmapping, fog, LFB 16/32-bit read/write, `.3df` parsing, `grGlideGetState`/`SetState`, thread scaling, commercial game regressions, and cross-backend visual parity. |
| `Glide3xWrapperTests` | `tests/test_glide3.cpp` | 68 test cases covering Glide 3.x ABI, `grVertexLayout`, `NativeSurfaceHost`, `IniConfigLoader`, authentic 3dfx env vars, HUD/telemetry CSV, VSA-100 (`PIXEXT`, `COMBINE`, `TEXFMT`, `TEXTUREBUFFER`, `TEXUMA`, `GETGAMMA`, Spatial AA, Stencil), 13 SIMD kernel scalar/AVX2/AVX-512 bit-exactness, catalog INI validation (`[M12][Catalog]`), and SemVer parity (`[M13][Version]`). |

Synthetic `.3df` texture fixtures and temporary log paths are provided by `tests/test_helpers.h` so test runs leave the repository tree clean.

### Running Tests

```bash
# Discover all registered tests (113 total)
ctest --test-dir build -N

# Run full suite via CTest (headless Software backend)
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build --output-on-failure

# Run Glide 2.x suite across all three backends
for be in software opengl_es vulkan; do
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=$be ./build/bin/Glide2xWrapperTests
done

# Run Glide 3.x suite on Software backend
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build/bin/Glide3xWrapperTests

# Run targeted Catch2 tags
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build/bin/Glide3xWrapperTests "[M12][Catalog]"
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build/bin/Glide3xWrapperTests "[M13][Version]"
```

> **Note**: Always pass `GLIDE_NG_HEADLESS=1` in CI/headless shells. Do not set `FX_GLIDE_NO_SPLASH=1` globally when running `Glide2xWrapperTests`, because splash/shameless-plug tests explicitly verify default splash behavior.

---

## 2. Software Rasterizer & Environment Variable Verification Tools

### `sw_validate`

Verifies Software backend rasterization and SIMD invariants directly:

```bash
GLIDE_NG_HEADLESS=1 ./build/bin/sw_validate
```

### `glide2_envtest` & `glide3_envtest`

Pure C99 Glide test programs that render a 4-quadrant test card and assert LFB RGB565 readbacks against active INI and authentic 3dfx environment overrides:

```bash
# Baseline automated check
GLIDE_NG_HEADLESS=1 ./build/bin/glide2_envtest --verify --test-reopen
GLIDE_NG_HEADLESS=1 ./build/bin/glide3_envtest --verify --test-reopen

# Test specific overrides across backends
for be in vulkan opengl_es software; do
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=$be SSTV2_FORCE_TRILINEAR=1 \
    ./build/bin/glide3_envtest --verify
done
```

See [`docs/catalog/tools/glide-envtest/README.md`](../catalog/tools/glide-envtest/README.md) for the complete override verification matrix.

---

## 3. Cross-Compilation & Clean-Build Verification

Before merging changes that touch headers, CMake, core SIMD, or frontend exports:

1. **Zero warnings under `-Werror`**: Confirm both Debug (`build/`) and Release (`build_release/`) compile with zero warnings across Linux GCC, Win32 MinGW (`win32_tools`), and optional DOS Open Watcom (`dos_tools`).
2. **Per-TU ISA isolation check** (after editing `SimdKernels*` or `SoftwareRasterizer*`): Verify zero `ymm`/`zmm` instructions leak into baseline object files:
   ```bash
   objdump -d build_release/src/CMakeFiles/GlideWrapperCore.dir/core/SimdKernels.cpp.o | grep -E "ymm|zmm" || echo "Clean"
   ```
3. **Catalog INI validation**: Run `./build/bin/Glide3xWrapperTests "[M12][Catalog]"` after editing `glide-ng.ini.example` or any `docs/catalog/**/glide-ng*.ini` file.
4. **Embedded child surface test** (after touching `NativeSurfaceHost`): Run `./build/bin/child_surface_embed_test` on X11 and Wayland sessions.
