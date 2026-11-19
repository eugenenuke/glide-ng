# Building & Installing glide-ng

This guide covers prerequisites, compiling `glide-ng` for Linux, cross-compiling 32-bit Win32 DLLs (`glide.dll`, `glide2x.dll`, and `glide3x.dll`) for Wine/Proton or Windows, optional DOS/32 tool builds, and installing the wrapper for games and emulators.

---

## 1. Prerequisites

`glide-ng` requires a C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+), CMake 3.20+, Python 3 (for SPIR-V header embedding), and `glslangValidator` (from `glslang-tools` or the Vulkan SDK).

### Linux (Core Wrapper & Tests)

- **Debian / Ubuntu**:
  ```bash
  sudo apt-get update
  sudo apt-get install -y build-essential cmake catch2 glslang-tools python3 \
    libvulkan-dev libegl-dev libgl-dev libgles2-mesa-dev libsdl2-dev \
    libx11-dev libxext-dev libwayland-dev pkg-config
  ```
- **Arch Linux**:
  ```bash
  sudo pacman -S base-devel cmake catch2 glslang python vulkan-headers vulkan-icd-loader \
    mesa sdl2 libx11 libxext wayland pkgconf
  ```
- **Fedora**:
  ```bash
  sudo dnf install gcc-c++ cmake catch-devel glslang python3 vulkan-headers vulkan-loader-devel \
    mesa-libEGL-devel mesa-libGLES-devel SDL2-devel libX11-devel libXext-devel wayland-devel pkgconf-pkg-config
  ```

`Catch2` is optional; when installed, the self-contained `Glide211WrapperTests`, `Glide2xWrapperTests`, and `Glide3xWrapperTests` suites build automatically with zero external SDK or demo dependencies.

### Optional: 32-Bit Win32 Cross-Compiler (MinGW-w64) & Wine

Required only to build 32-bit Windows `glide.dll`, `glide2x.dll`, `glide3x.dll`, and Win32 `.exe` utilities on Linux:

- **Debian / Ubuntu** (enable `i386` multiarch first if needed):
  ```bash
  sudo dpkg --add-architecture i386
  sudo apt-get update
  sudo apt-get install -y g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix mingw-w64-tools \
    wine wine32:i386 wine64
  ```
  Use the `-posix` MinGW-w64 variant so C++ `std::thread` and `std::mutex` are available. `libvulkan-dev` already provides `vulkan_win32.h`. If only `wine32:i386` is installed without `wine64`, initialize a 32-bit prefix once with `WINEARCH=win32 wineboot -u`.
- **Arch Linux**:
  ```bash
  sudo pacman -S mingw-w64-gcc wine
  ```
- **Fedora**:
  ```bash
  sudo dnf install mingw32-gcc-c++ mingw32-winpthreads-static wine
  ```

For running the Vulkan backend under 32-bit Wine, ensure a 32-bit Vulkan ICD is installed (e.g., `mesa-vulkan-drivers:i386` / `lib32-vulkan-radeon` / `lib32-nvidia-utils`).

### Optional: DOS/32 Cross-Compiler (Open Watcom v2)

DOS/32 diagnostic and benchmark executables (`glide_info.exe`, `glide_bench.exe`, `glide_envtest.exe`) are optional; CMake skips them automatically when Open Watcom v2 is absent.

To enable DOS/32 cross-builds (no root required):
1. Download a Linux x64 release archive from [Open Watcom v2 Releases](https://github.com/open-watcom/open-watcom-v2/releases) (for example, `open-watcom-2_0-c-linux-x64.tar.xz`).
2. Extract it into `./vendor/watcom/` (git-ignored) so `./vendor/watcom/binl64/wcc386` exists, or set the `WATCOM` environment variable to an existing Open Watcom installation.
3. Re-run CMake configure. When detected, the `dos_tools` target builds self-contained CauseWay DOS/32 executables into `<build>/dos/` that dynamically load `GLIDE2X.OVL`.

### Windows (Native Build)

- CMake 3.20+
- LunarG Vulkan SDK (`VULKAN_SDK` environment variable providing `vulkan/vulkan.hpp` and `glslangValidator`)
- Python 3
- MinGW-w64 (`i686` POSIX-threads) or MSVC (32-bit `Win32` / `x86` target for classic 32-bit Glide games)

---

## 2. Building on Linux

### Standard Release Build

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(nproc)
```

### Standard Debug Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Build Outputs

| Path | Contents |
| :--- | :--- |
| `<build>/lib/libglide2x.so` | Linux Glide 2.x shared library |
| `<build>/lib/libglide3x.so` | Linux Glide 3.x shared library |
| `<build>/bin/` | Linux tools (`glide2_info`, `glide3_info`, `glide2_bench`, `glide3_bench`, `glide2_envtest`, `glide3_envtest`, `glide2_cube`, `glide3_cube`, `glide2_gears`, `glide3_gears`), standalone demos, and Catch2 test runners |
| `<build>/win32/bin/` | Auto-built 32-bit Win32 `glide2x.dll`, `glide3x.dll`, and Win32 `.exe` tools (when MinGW-w64 is detected) |
| `<build>/dos/` | Auto-built DOS/32 `glide_info.exe`, `glide_bench.exe`, `glide_envtest.exe` (when Open Watcom v2 is detected) |

### Useful CMake Options & Targets

| Option / Target | Effect |
| :--- | :--- |
| `-DGLIDE_BUILD_WIN32=OFF` | Disable automatic MinGW-w64 Win32 sub-build during Linux builds. |
| `-DGLIDE_STRICT_WARNINGS=OFF` | Disable `-Werror` on project targets if testing an unreleased compiler. |
| `-DENABLE_TELEMETRY=ON` | Enable fine-grained internal profiling macros in addition to always-on frame telemetry. |
| `--target win32_dlls` | Build only 32-bit `glide2x.dll` and `glide3x.dll`. |
| `--target win32_tools` | Build all 32-bit Win32 DLLs and tools. |
| `--target dos_tools` | Build all Open Watcom DOS/32 executables. |

---

## 3. Standalone Win32 Cross-Build (`build_win32/`)

To build only the 32-bit Win32 binaries without configuring a Linux host tree:

```bash
cmake -S . -B build_win32 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_win32 -j$(nproc)
```

Produced Win32 binaries (`build_win32/bin/` or `build_release/win32/bin/`) are statically linked against MinGW runtimes and carry **zero external runtime DLL dependencies** — importing only standard Windows/Wine system DLLs (`KERNEL32.dll`, `msvcrt.dll`, `USER32.dll`, `GDI32.dll`, `OPENGL32.dll`), with `vulkan-1.dll` loaded dynamically at runtime when the Vulkan backend is active.

Each DLL exports:
- Canonical 3dfx Win32 `__stdcall` decorated names (`_grFoo@N`) for 1990s MSVC/Watcom executables.
- Undecorated aliases (`grFoo`) for dynamic `GetProcAddress()` callers.
- Embedded Win32 `VS_VERSION_INFO` version metadata derived from the repository `VERSION` file.

---

## 4. Installing & Running Games

### Wine & Steam Proton (Win32 Titles)

1. Copy `glide2x.dll` and/or `glide3x.dll` into the same directory as the game executable.
2. Optionally copy a per-title `glide-ng.ini` preset from the [Compatibility Catalog](../catalog/README.md) into the same folder (or pass `GLIDE_NG_CONFIG=/path/to/glide-ng.ini`).
3. Instruct Wine/Proton to load the native DLL next to the executable:

```bash
# Wine (standalone)
WINEDLLOVERRIDES="glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./game.exe

# Wine with virtual desktop (useful for older fullscreen titles)
WINEDLLOVERRIDES="glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan \
  wine explorer /desktop=Glide,800x600 ./game.exe

# Steam Proton Launch Options
WINEDLLOVERRIDES="glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan %command%
```

Switch backends without modifying any files using `GLIDE_NG_BACKEND=vulkan` (default), `GLIDE_NG_BACKEND=opengl_es` (Win32 WGL OpenGL 3.3 Core), or `GLIDE_NG_BACKEND=software` (CPU SIMD rasterizer presented via GDI `StretchDIBits`).

### DOSBox-X & Patched DOSBox-SVN (DOS/32 Glide Pass-Through)

DOSBox's Glide pass-through forwards guest `GLIDE2X.OVL` calls over I/O port `0x600` to the host `libglide2x.so`. Place the small (~31 KB) DOSBox HLE pass-through `GLIDE2X.OVL` in the game directory or on DOS `PATH`:

**Patched DOSBox-SVN (`dosbox-svn.conf`, minimal overrides):**
```ini
[sdl]
# Prevents SDL_UpdateRects SIGSEGV under sdl12-compat -> sdl2-compat -> SDL3 on Linux
output = opengl

[cpu]
cycles = max

[glide]
glide = true
```

**DOSBox-X (`dosbox-x.conf`, minimal overrides):**
```ini
[cpu]
cycles = max

[voodoo]
glide = true
lfb = full
```

```bash
# If glide-ng is installed system-wide (no LD_LIBRARY_PATH needed):
GLIDE_NG_BACKEND=vulkan dosbox-x -conf dosbox-x.conf

# Or when running directly from a local build directory:
LD_LIBRARY_PATH="$PWD/build_release/lib:${LD_LIBRARY_PATH:-}" \
  GLIDE_NG_BACKEND=vulkan dosbox-x -conf dosbox-x.conf
```

> **Choosing the right `GLIDE2X.OVL` (Pass-Through vs. Software Voodoo):**
> - **Glide pass-through (`glide = true` → `glide-ng`):** Use the DOSBox HLE pass-through `GLIDE2X.OVL` (~31 KB). Authentic hardware OVLs will not talk to the `0x600` pass-through port.
> - **Built-in Software Voodoo (`voodoo_card = software` in DOSBox-X, or `voodoo = on` in DOSBox-Staging):** These emulators implement **Voodoo Graphics (SST-1)** hardware (PCI `121A:0001`), *not* pass-through to `glide-ng`. Running DOS games or `glide_info.exe` / `glide_bench.exe` against Software Voodoo requires an authentic **SST-1** hardware `GLIDE2X.OVL` — specifically **Glide 2.42 (SST-1)** (recommended) or **2.30**. Voodoo Rush (`SST-96`), Voodoo2 (`2.5x`), Banshee/Voodoo3 (`2.6x`), and HLE pass-through OVLs will report 0 boards or mismatch errors; Glide `2.43`/`2.46` fail DOSBox's SST-1 PCI init and `2.48` faults on Ring-0 `RDMSR`.
> - **`PATH` lookup:** `glide_info.exe`, `glide_bench.exe`, and `glide_envtest.exe` search the current directory, then every directory in DOS `PATH`, then `Z:\` fallbacks, and support both HLE pass-through and authentic 3dfx Watcom `GLIDE2X.OVL` binaries (`2.3`–`2.61`).

For per-game DOSBox and `GLIDE2X.OVL` recipes (such as *Carmageddon* and *Tomb Raider*), see the [Compatibility Catalog](../catalog/README.md).

### Native Linux Applications & Demos

Standalone Linux tools and demos run directly from `<build>/bin/`:

```bash
# Inspect emulated hardware and extensions
./build_release/bin/glide2_info -v
./build_release/bin/glide3_info -v --probe

# Launch an interactive demo with on-screen HUD
GLIDE_NG_BACKEND=vulkan GLIDE_NG_HUD=fps,backend ./build_release/bin/glide3_cube
```
