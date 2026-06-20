# Glide Emulation Troubleshooting Playbook

This document outlines the authoritative diagnostic procedures, logging systems, screen-capturing workflows, and unit testing practices used to troubleshoot and verify the modernized Glide 2.x and 3.x wrappers.

---

## 1. Diagnostics & Logging System

Our wrapper features a unified logging architecture that tracks API entry points, backend transitions, shader compilation, and texture allocation.

### A. Configuring Log Levels
Logging is controlled globally via a local JSON profile named `glide_config.json` placed in the application's working directory:

```json
{
  "logLevel": 4
}
```

The supported log levels correspond to the following enumeration:
*   `0` (**OFF**): Silences all logs (for production/performance).
*   `1` (**CRITICAL**): Severe errors only (e.g., driver initialization failures).
*   `2` (**WARN**): Unexpected behaviors, unimplemented features, or fallback paths.
*   `3` (**INFO**): Key life cycle steps, emulation profiles, and performance telemetry.
*   `4` (**DEBUG**): Verbose trace logs (e.g., texture allocations, shader bindings, individual draw states).

#### Environment Variable Overrides & Precedence
Log levels and console targets can also be dynamically overridden using environment variables:
*   `GLIDE_WRAPPER_LOG_LEVEL=<0-4>`: Sets the active log level.
*   `GLIDE_WRAPPER_LOG_TO_CONSOLE=1`: Routes all log messages to standard output (useful for unit test runner visibility).

**Precedence / Preference Order**:
1.  **Environment Variables** (Highest precedence; overrides all others).
2.  **`glide_config.json`** (Medium precedence; overrides default settings).
3.  **Baseline Defaults** (Lowest precedence; fallback when no variables/config are present).

### B. Output Destination
By default, the active Glide frontend initializes logging to **`glide-ng.log`** inside the current working directory of the executing application. When troubleshooting a test:
1.  Run the test executable.
2.  Open `glide-ng.log` in the same directory.
3.  Filter for `[WARN]` to identify missing API queries or unsupported parameter configurations.

---

## 2. Test Suite Screen Capturing & Conversion

To visually inspect rendering accuracy (especially when diagnosing "black screen" bugs), use the built-in screen capture features of the 3dfx test suite.

### A. Capturing a Frame Dump
All official tests (e.g., `test00.exe` to `test39.exe`) support a command-line argument `-d <filename>` to capture the first rendered frame:
```bash
# Launch test06 and dump its frame buffer under Vulkan
export GLIDE_WRAPPER_BACKEND=vulkan
./test06.exe -d test06_dump.ppm
```

### B. Converting Proprietary 3dfx RLE Dumps
The screen dump written by the tests is **not** a standard PPM file. It is a proprietary 3dfx format (`IMAGE_SRLE`) containing RLE-compressed 16-bit color buffer data.

To convert this dump to standard formats:
1.  **Decompress to standard PPM**: Use our precompiled utility `rle_to_ppm` located in the build directory:
    ```bash
    ./build/bin/rle_to_ppm test06_dump.ppm test06_real.ppm
    ```
2.  **Convert to PNG for viewing**: Use ImageMagick's `convert` tool:
    ```bash
    convert test06_real.ppm test06.png
    ```
3.  You can now open the resulting `test06.png` directly in your workspace or markdown documentation.

---

## 3. Catch2 Unit Testing & TDD Workflow

We use a Test-Driven Development (TDD) loop and Catch2 to prevent regressions.

### A. Structure of Unit Tests
All unit tests are located in `tests/test_glide3.cpp` (for Glide 3.x) and `tests/test_architecture.cpp` (for Glide 2.x/core structures).
Tests load the compiled shared library `libglide3x.so` dynamically using `dlopen`/`dlsym` and invoke APIs in a sandbox.

### B. Running the Unit Tests
To compile and run the unit test suite:
```bash
# Rebuild the tests target
cmake --build build -j$(nproc)

# Run the test runner
./build/bin/GlideWrapperTests
```

---

## 4. Playbook in Action: Case Studies
 
### Case Study 1: The `test06` Black Screen (W-Buffering & Q-Param)
1.  **The Symptom**: `test06` under Glide 2 rendered two interpenetrating triangles correctly, but under Glide 3, it produced a black screen.
2.  **Log Diagnostics**:
    *   Inspected `refs/glide/glide3x/h3/glide3/tests/glide-ng.log`.
    *   Discovered multiple warning lines:
        `[WARN] [Frontend] grGet (Glide 3.x) unknown or unsupported pname=39`
    *   Forensically matched `39` to `0x27` which is `GR_WDEPTH_MIN_MAX` in `glide.h`.
3.  **Code Inspection**:
    *   Inspected `src/core/VertexLayout.cpp` and noticed that `GR_PARAM_Q` (`0x04`), which provides the `oow` (1/W) offset in Glide 3.x, was missing from `SetParamOffsetGlide3`'s switch-case.
    *   Inspected `src/glide3/glide3_entry.cpp` and found that `grGet` did not support depth buffer range queries like `GR_WDEPTH_MIN_MAX` or `GR_ZDEPTH_MIN_MAX`.
4.  **The Fix**:
    *   Mapped `GR_PARAM_Q` (`0x04`) to `m_offsetOOW` inside `SetParamOffsetGlide3`.
    *   Added support for `GR_WDEPTH_MIN_MAX` and `GR_ZDEPTH_MIN_MAX` in `grGet`, returning `{0, 65535}`.
5.  **Verification**:
    *   Recompiled the wrapper.
    *   Ran `./test06.exe -d test06_vulkan_g3.ppm` and successfully converted it to a beautiful PNG showing the interpenetrating triangles.

### Case Study 2: The `test10` Triangle Culling & Origin Swapping
1.  **The Symptom**: A culling test that draws clockwise and counter-clockwise triangles under both `GR_ORIGIN_UPPER_LEFT` and `GR_ORIGIN_LOWER_LEFT`, ensuring that one is discarded and the other is rendered.
2.  **The Mechanics**:
    *   When the display origin changes to `GR_ORIGIN_LOWER_LEFT` (1), the Y-axis is inverted during rasterization.
    *   Inverting one axis mathematically flips the winding of all triangles (Clockwise becomes Counter-Clockwise and vice versa).
    *   On real Voodoo hardware, the backface culling engine evaluates the winding based on the *logical, pre-flipped* coordinates supplied by the game, not the *post-flipped* coordinates rasterized on screen. Evaluating culling on post-flipped coordinates would cull both triangles, causing a **black screen**.
3.  **The Solution**:
    *   In our Software rasterizer (`src/backends/software/SoftwareBackend.cpp`), we compute the signed triangle area and evaluate backface culling (`m_cullMode`) using the **pre-flipped** coordinates, and then apply Y-flipping afterward during rasterization.
    *   In Vulkan (`src/backends/vulkan/VulkanBackend.cpp`), we configure the GPU pipeline's `vk::FrontFace` to swap (from `eClockwise` to `eCounterClockwise`) when `sstOrigin` is `LOWER_LEFT` to mirror this winding swap on hardware.
4.  **Unit Testing**:
    *   Added `SECTION("Dynamic API Verification for LFB Lock, Write and Origin Swapping")` to `test_glide3.cpp` which validates that coordinate row-flipping is applied symmetrically during locks and unlocks depending on the active origin.

### Case Study 3: The `test12` Linear Frame Buffer (LFB) Lock & Write Pipeline
1.  **The Symptom**: Directly locking the frame buffer, writing raw RGB565 pixel values into the CPU pointer, and unlocking.
2.  **The Architecture**:
    *   Since modern GPUs do not allow direct CPU writes to active framebuffers, we allocate a CPU staging buffer inside `grLfbLock` (`g_lfbLock.stagingBuffer`).
    *   The game writes directly to the staging buffer pointer (`info.lfbPtr`).
    *   During `grLfbUnlock`, if the write mode is `GR_LFB_WRITE_ONLY`, we transfer the staging buffer contents to the GPU.
    *   In Vulkan, this is executed by `FlushLFBToGPU()`, which copies the CPU buffer to a Vulkan host-visible staging buffer and submits a transfer command (`vkCmdCopyBufferToImage`) before performing swapchain presentation.
3.  **Origin Row Flipping**:
    *   If the lock origin is `GR_ORIGIN_LOWER_LEFT`, the CPU rows are written upside-down relative to the GPU framebuffer.
    *   During `grLfbLock`, we read the active frame buffer and flip the rows in memory.
    *   During `grLfbUnlock`, we flip the rows in the staging buffer *again* before uploading to the GPU, ensuring perfect top-to-bottom spatial consistency.
4.  **Verification**:
    *   Validated in Catch2 by locking with both origins, writing a red pixel at logical (0,0), unlocking, and reading back the frame buffer using `ReadLFB` to verify it maps to physical (0, 479) under `LOWER_LEFT`.

### Case Study 4: The `test16` 3dfx Splash Screen & Shameless Plug
1.  **The Symptom**: The spinning 3dfx logo and the translucent watermark plug did not render under Glide 3.x, though they worked under Glide 2.x.
2.  **The Root Cause**:
    *   In Glide 2.x, the `GlideSplashAnimator` (which decodes raw 3D mesh assets and matrices and runs the geometry pipeline) was correctly initialized inside `grSstWinOpen`.
    *   In Glide 3.x, `grSstWinOpen` in `glide3_entry.cpp` completely omitted the splash animator initialization!
    *   Furthermore, `GlideSplashAnimator.cpp` was only compiled under the `glide2x` library target in `src/CMakeLists.txt`. Because `libglide3x.so` did not link against `libglide2x.so`, it suffered from **undefined reference** linking errors when referencing the splash animator.
3.  **The Fix**:
    *   **Unified Core Target**: Moved `core/GlideSplashAnimator.cpp` to the static `GlideWrapperCore` target in `src/CMakeLists.txt`, making its compiled object file shared and linked by both `libglide2x.so` and `libglide3x.so`.
    *   **Glide 3.x Integration**: Added the splash animator initialization `SetSplashAnimator(std::make_unique<GlideSplashAnimator>())` to `grSstWinOpen` and cleanup to `grSstWinClose` in `src/glide3/glide3_entry.cpp`.
    *   **Symbol Exporting**: Implemented `grColorMask` (which was called by the splash animator but missing in the Glide 3.x frontend) and exposed the global `s_sstOrigin` symbol so the animator could resolve the active screen orientation.
4.  **Verification**:
    *   Added a Catch2 integration test calling `grSplash` and `grEnable(GR_SHAMELESS_PLUG)` to verify the animation frames and translucent watermarks execute and swap buffers with 100% stability.

### Case Study 5: The `test10` Key Interception & Culling Winding Discrepancy Triage
1.  **The Symptom**: In Glide 3.x, `test10.exe` (winding and culling test) displayed both triangles on startup and ignored the `<SPACE>` key pressed in the window, whereas the Glide 2.x version worked perfectly (showing a single blue triangle on startup and toggling on `<SPACE>`).
2.  **Root Cause Analysis & TDD Loop**:
    *   **Unit Testing**: We implemented a rigorous Catch2 unit test `Dynamic API Verification for Backface Culling and Winding` in [tests/test_glide3.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/test_glide3.cpp). This test draws a Clockwise triangle in Blue and a Counter-Clockwise triangle in Red, sets `grCullMode`, and uses `ReadLFB` to read back the center pixels of the backbuffer to verify culling is programmatically applied on the GPU/Software backends.
    *   **The Result**: The unit test **passed 100% successfully**, proving that our wrapper's internal culling and winding engines were completely correct!
    *   **Forensic Submodule Discovery**: We investigated why the space key was ignored *only* in Glide 3.x's interactive run. We discovered that there are **two distinct copies** of the test utility helper `linutil.c` in the repository submodules:
        1.  [refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c) (used by Glide 2.x tests), which was correctly patched with a weak symbol bridge to check the global flag `g_glideWrapperSdlKeyHit` set by GLFW/SDL window events.
        2.  [refs/glide/swlibs/fxmisc/linutil.c](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/swlibs/fxmisc/linutil.c) (used by Glide 3.x tests), which was **completely unpatched** and lacked the virtual keyboard hook! Because of this, it only polled `stdin` via terminal select, completely ignoring keypresses in the graphical window.
    *   **Historical Source Reference**: Note that the Glide 2 original source files for tests are located in [refs/glide/glide2x/sst1/glide/tests](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/glide2x/sst1/glide/tests).
3.  **The Fix**:
    *   **Keyboard Hook Porting**: Patched [refs/glide/swlibs/fxmisc/linutil.c](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/swlibs/fxmisc/linutil.c) to port the weak symbol `g_glideWrapperSdlKeyHit` check and `isatty(0)` guard, matching the Glide 2.x bridge.
    *   **Rebuilding Tests**: Rebuilt the Glide 3.x test suite with the patched `linutil.o` by overriding `TOP` on the command line:
        ```bash
        make -f Makefile.linux test06.exe test10.exe test12.exe test16.exe \
            TOP=../../../.. \
            CFLAGS="-std=gnu89 -Wall -O2 -I/usr/local/google/home/eianiuk/gemini/3dfx/include/glide3 -I/usr/local/google/home/eianiuk/gemini/3dfx/include/3dfx -I/usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/swlibs/fxmisc -DH3" \
            LDFLAGS="-L/usr/local/google/home/eianiuk/gemini/3dfx/build/lib -Wl,-rpath,/usr/local/google/home/eianiuk/gemini/3dfx/build/lib" \
            LDLIBS="-lglide3x -lm"
        ```
4.  **Verification**:
    *   Running the rebuilt `test10.exe` now successfully captures graphical keypresses in the window, enabling seamless culling toggling on `<SPACE>`.
    *   All Catch2 unit tests pass cleanly, guarding against culling and winding regressions.

