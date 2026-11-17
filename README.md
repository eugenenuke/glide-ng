# 3dfx Glide API Modernization Wrapper

An open-source, highly modular drop-in replacement wrapper that translates authentic **3dfx Glide (Glide 2.x and Glide 3.x)** graphics calls into high-performance modern graphics APIs (**Vulkan**, **EGL**, and **OpenGL**).

This wrapper breathes new life into classic 1990s and early 2000s speed-sensitive PC games and interactive applications, enabling them to run flawlessly on modern x86/x64 systems without requiring original hardware accelerators like the Voodoo Graphics (SST-1), Voodoo2 (CVG), Voodoo3 (H3), or Voodoo5 (H5).

---

## Key Features & Architecture

* **Flawless C ABI Compatibility**: Compiles into un-mangled dynamic replacement libraries (`glide2x.dll`/`libglide2x.so` and `glide3x.dll`/`libglide3x.so`) matching the original 3dfx SDK specifications exactly.
* **Polymorphic Multi-Backend System**: The Glide state machine is entirely uncoupled from the rendering execution. You can hot-swap rendering backends instantly (Vulkan for ultra-low latency, EGL for lightweight embedded contexts).
* **Configurable Hardware Emulation**: Old retro games often glitch or run at hyper-speed on modern processors. The wrapper enforces exact Voodoo video RAM limits, active TMU (Texture Mapping Unit) limits, and classic overrides via `glide-ng.ini` (see [glide-ng.ini.example](./glide-ng.ini.example)), authentic `FX_GLIDE_*`/`SST_*` variables, or `GLIDE_NG_*` environment overrides.
* **Automated Runtime Logging**: Automatically writes `glide-ng.log` (configurable via `GLIDE_NG_LOG_FILE`) to track game texture requirements, host capabilities, and active emulation parameters.
* **Off-Screen Golden Testing**: Features Catch2 integration test suites that execute headless Glide rendering passes and perform bit-exact golden image comparisons.

---

## Software Prerequisites & Installation

To compile the wrapper and execute the automated unit tests, you must have a C++20 capable toolchain and modern graphics headers installed.

### Debian / Ubuntu Environments
Run the following terminal command to install all mandatory development packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake catch2 libvulkan-dev libegl-dev libgl-dev libsdl2-dev
```

### Visual Testing vs Headless Execution
By default, when applications pass `0` for `hWnd` (such as legacy reference tests), the wrapper automatically spawns an interactive centered **SDL2 Presentation Window** to enable direct visual inspection.
For automated CI testing without desktop popups, enforce headless mode via:
```bash
GLIDE_NG_HEADLESS=1 ./bin/Glide3xWrapperTests
```

### Optional: DOS Cross-Compiler (Open Watcom v2)
DOS diagnostic, benchmark, and verification `.exe` tools (`build/dos/glide_info.exe`, `build/dos/glide_bench.exe`, `build/dos/glide_envtest.exe`) are **completely optional** — CMake skips them automatically if Open Watcom v2 is not installed, so standard Linux builds never fail without it.

To enable DOS `.exe` builds (identical across Debian, Fedora, and Arch, no root required):
1. Download the official Linux x64 release tarball from [Open Watcom v2 Releases](https://github.com/open-watcom/open-watcom-v2/releases) (e.g. `open-watcom-2_0-c-linux-x64.tar.xz`).
2. Extract it into `./vendor/watcom/` (git-ignored) so `./vendor/watcom/binl64/wcc386` exists (or point `WATCOM` to an existing installation).
3. Re-run CMake configure — when detected, the `dos_tools` target automatically produces self-contained CauseWay DOS/32 executables (`build/dos/glide_info.exe`, `build/dos/glide_bench.exe`, `build/dos/glide_envtest.exe`) that dynamically load `GLIDE2X.OVL`.

### Optional: Win32 Cross-Compiler (MinGW-w64) & Wine (for `glide2x.dll` / `glide3x.dll`)
32-bit Win32 PE builds (`glide2x.dll`, `glide3x.dll`, and Win32 tools for running Windows Glide games under Wine/Proton) require a 32-bit (`i686`) MinGW-w64 POSIX-threads toolchain and Wine:

* **Debian / Ubuntu** (enable `i386` multiarch first if not already enabled):
  ```bash
  sudo dpkg --add-architecture i386
  sudo apt-get update
  sudo apt-get install -y g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix mingw-w64-tools wine wine32:i386
  ```
  *(Use the `-posix` MinGW variant so C++17 `std::thread`/`std::mutex` are supported; `libvulkan-dev` already provides `vulkan_win32.h`.)*
* **Arch Linux**:
  ```bash
  sudo pacman -S mingw-w64-gcc wine
  ```
* **Fedora**:
  ```bash
  sudo dnf install mingw32-gcc-c++ mingw32-winpthreads-static wine
  ```

### Windows (Native MinGW-w64 / MSVC)
* **CMake**: Version 3.20 or newer.
* **Vulkan SDK**: Download and install the official LunarG Vulkan SDK.
* **Catch2**: Can be pulled dynamically via CMake `FetchContent` or installed via Vcpkg/Conan.

---

## How to Build & Run Tests

```bash
# 1. Clone the repository and configure the build tree
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# 2. Compile the core wrapper libraries and Catch2 test suites
cmake --build . --parallel

# 3. Execute off-screen multi-backend Catch2 validation tests
ctest --output-on-failure
```

---

## Documentation & Project Roadmap

For detailed architectural breakdown, API milestone schedules, and environment variable specifications, please explore the `./docs` directory:
* **[Implementation Blueprint & Roadmap](./docs/glide_wrapper_project_plan.md)**
* **[Environment Variables & Overrides Guide](./docs/glide_environment_variables.md)**
