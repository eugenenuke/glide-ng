# 3dfx Glide API Modernization Wrapper

An open-source, highly modular drop-in replacement wrapper that translates authentic **3dfx Glide (Glide 2.x and Glide 3.x)** graphics calls into high-performance modern graphics APIs (**Vulkan**, **EGL**, and **OpenGL**).

This wrapper breathes new life into classic 1990s and early 2000s speed-sensitive PC games and interactive applications, enabling them to run flawlessly on modern x86/x64 systems without requiring original hardware accelerators like the Voodoo Graphics (SST-1), Voodoo2 (CVG), Voodoo3 (H3), or Voodoo5 (H5).

---

## Key Features & Architecture

* **Flawless C ABI Compatibility**: Compiles into un-mangled dynamic replacement libraries (`glide2x.dll`/`libglide2x.so` and `glide3x.dll`/`libglide3x.so`) matching the original 3dfx SDK specifications exactly.
* **Polymorphic Multi-Backend System**: The Glide state machine is entirely uncoupled from the rendering execution. You can hot-swap rendering backends instantly (Vulkan for ultra-low latency, EGL for lightweight embedded contexts).
* **Configurable Hardware Emulation**: Old retro games often glitch or run at hyper-speed on modern processors. The wrapper enforces exact Voodoo video RAM limits, active TMU (Texture Mapping Unit) limits, and classic AGP/PCI bus delays via `glide_config.json` or `GLIDE_WRAPPER_*` environment overrides.
* **Automated Runtime Logging**: Automatically writes `glide_wrapper.log` to track game texture requirements, host capabilities, and active emulation parameters.
* **Off-Screen Golden Testing**: Features Catch2 integration test suites that execute headless Glide rendering passes and perform bit-exact golden image comparisons.

---

## Software Prerequisites & Installation

To compile the wrapper and execute the automated unit tests, you must have a C++20 capable toolchain and modern graphics headers installed.

### Debian / Ubuntu / gLinux Environments
Run the following terminal command to install all mandatory development packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake catch2 libvulkan-dev libegl-dev libgl-dev libsdl2-dev
```

### Visual Testing vs Headless Execution
By default, when applications pass `0` for `hWnd` (such as legacy reference tests), the wrapper automatically spawns an interactive centered **SDL2 Presentation Window** to enable direct visual inspection.
For automated CI testing without desktop popups, enforce headless mode via:
```bash
GLIDE_FORCE_NO_WINDOW=1 ./bin/GlideWrapperTests
```

### Windows (MinGW-w64 / MSVC)
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
