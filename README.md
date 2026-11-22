# glide-ng — 3dfx Glide 2.x & 3.x Modernization Wrapper

`glide-ng` is an open-source, drop-in replacement wrapper that translates authentic **3dfx Glide 2.x and Glide 3.x** graphics calls to **Vulkan**, **OpenGL ES 3.2 / OpenGL 3.3 Core**, or a multi-threaded **SIMD Software** rasterizer (AVX2 & AVX-512).

It runs classic 1990s and early-2000s DOS (via DOSBox pass-through), Win32 (via Wine/Proton or Windows), and native Linux Glide applications across seven emulated 3dfx hardware profiles — from **Voodoo Graphics (SST-1)** to **Voodoo5 (VSA-100)**.

> [!NOTE]
> **AI-Assisted Development Disclosure:** The source code, tests, shaders, and documentation in this repository were largely drafted using **Google Gemini** under human architectural guidance, issue triage, and hands-on testing. All changes are verified through strict zero-warning multi-distro CI builds (`-Werror`), the self-contained Catch2 test suite, and empirical testing across dozens of commercial Glide titles (see [`docs/catalog/`](docs/catalog/README.md)). That said, rough edges and non-idiomatic patterns are still possible — bug reports, code reviews, and pull requests are welcome.

---

## Highlights

- **Authentic C ABI Compatibility**: Builds `libglide.so` / `glide.dll` (Glide 2.11, 274 exports), `libglide2x.so` / `glide2x.dll` (Glide 2.x, 152 exports), and `libglide3x.so` / `glide3x.dll` (Glide 3.x, 129 exports) with canonical Win32 `__stdcall` `_Func@N` exports, pure Glide ABI parity, and zero external runtime DLL dependencies on Win32.
- **Three Interchangeable Backends**:
  - **Vulkan** *(default)*: SPIR-V pipelines with Early-Z preservation, hardware BC1–BC3 (`DXT1`–`DXT5`) textures, zero-readback GPU render-to-texture (`TEXTUREBUFFER`), and persistent pipeline cache.
  - **OpenGL ES 3.2 / Win32 WGL 3.3 Core**: Direct EGL 1.5 on Linux (X11/Wayland/headless) and WGL on Windows/Wine.
  - **Software**: Bit-exact CPU reference rasterizer with runtime CPUID/XSAVE SIMD dispatch (`scalar`, `avx2` default, `avx512` 4×4 16-pixel rasterizer, `auto`).
- **Complete 3dfx Hardware & VSA-100 Support**: Emulates `VoodooGraphics` (SST-1), `VoodooRush` (SST-96), `Voodoo2` (CVG, default), `VoodooBanshee`, `Voodoo3`, `Voodoo4`, and `Voodoo5`, including VSA-100 2×/4× Spatial AA, T-Buffer, 8-bit hardware Stencil, `COMBINE`, `TEXFMT` (FXT1/DXT/YUV/ARGB8888), and 2048×2048 textures.
- **Unified Configuration & Telemetry**: Configured via [`glide-ng.ini`](glide-ng.ini.example), authentic `FX_GLIDE_*` / `SST_*` / `SSTV2_*` environment variables, or `GLIDE_NG_*` overrides, with a DXVK-style on-screen HUD (`GLIDE_NG_HUD`) and per-frame CSV performance tracing.
- **Portable Diagnostics & Benchmarks**: Includes pure C99 `glide_info`, `glide_bench` (GlideMark), and `glide_envtest` for Linux, Win32/Wine, and optional Open Watcom v2 DOS/32.

---

## Quick Start

### 0. Download Pre-Built Release Binaries (No Compiler Needed)

Ready-to-use stripped archives (`runtime.zip` / `runtime.tar.xz`, `tools.zip`, optional `debug-symbols.zip`, and `SHA256SUMS.txt`) for **Windows/Wine/Proton (`win32/glide*.dll`)**, **DOSBox (`dos/GLIDE2X.OVL`)**, and **Linux x86_64** (`ubuntu-22.04`, `ubuntu-24.04`, `fedora-41`, and `arch` / CachyOS flavors) are published on [GitHub Releases](https://github.com/eugenenuke/glide-ng-alpha/releases). See [Pre-Built Release Binaries & End-User Runtime Packages](docs/user-guide/building-and-installing.md#0-using-pre-built-release-binaries-end-users) for distro selection and minimal non-dev package commands.

### 1. Build on Linux (plus Automatic Win32 DLLs if MinGW-w64 Is Installed)

```bash
# Debian / Ubuntu core dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake catch2 glslang-tools \
  libvulkan-dev libegl-dev libgl-dev libgles2-mesa-dev libsdl2-dev libx11-dev libxext-dev

# Configure and build (Release)
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(nproc)

# Run self-contained headless Catch2 test suite
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build_release --output-on-failure
```

When 32-bit `i686-w64-mingw32-g++` is installed, the standard Linux build also cross-compiles `glide.dll`, `glide2x.dll`, `glide3x.dll`, and Win32 tools automatically into `build_release/win32/bin/`. See [Building & Installing](docs/user-guide/building-and-installing.md) for Arch/Fedora packages, standalone `build_win32/` builds, and optional Open Watcom v2 DOS/32 builds.

### 2. Run a Windows Glide Title Under Wine / Steam Proton

Copy `glide.dll` (Glide 2.11), `glide2x.dll`, and/or `glide3x.dll` (and an optional title preset from [`docs/catalog/`](docs/catalog/README.md)) next to the game executable:

```bash
# Wine (standalone)
WINEDLLOVERRIDES="glide,glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./game.exe

# Steam Proton Launch Options
WINEDLLOVERRIDES="glide,glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan %command%

# Switch to Software AVX-512 with live FPS HUD
WINEDLLOVERRIDES="glide,glide2x,glide3x=n" GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 \
  GLIDE_NG_HUD=fps,backend wine ./game.exe
```

### 3. Run a DOS Glide Title Under DOSBox-X / SVN DOSBox

```bash
LD_LIBRARY_PATH="$PWD/build_release/lib:${LD_LIBRARY_PATH:-}" \
  GLIDE_NG_BACKEND=vulkan dosbox-x -conf dosbox.conf
```

---

## Documentation

Browse the full [Documentation Hub](docs/README.md), or jump directly to a guide:

- **User Guide**
  - [Building & Installing](docs/user-guide/building-and-installing.md)
  - [Configuration & Environment Variables](docs/user-guide/configuration.md) ([`glide-ng.ini.example`](glide-ng.ini.example))
  - [Hardware Profiles & Glide Versions](docs/user-guide/hardware-and-versions.md)
  - [Benchmarking & Performance](docs/user-guide/benchmarking.md)
  - [Troubleshooting](docs/user-guide/troubleshooting.md)
- **Compatibility Catalog**
  - [Game, Demo & Benchmark Compatibility Catalog](docs/catalog/README.md)
- **Developer Guide**
  - [Architecture](docs/developer-guide/architecture.md) · [API Coverage & ABI](docs/developer-guide/api-coverage.md) · [Backends](docs/developer-guide/backends.md) · [Testing](docs/developer-guide/testing.md) · [Versioning & Releasing](docs/developer-guide/releasing.md)
- **Releases & Planning**
  - [Changelog](CHANGELOG.md) · [Project History](docs/HISTORY.md) · [Roadmap](docs/ROADMAP.md)

---

## License

`glide-ng` is free software licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
