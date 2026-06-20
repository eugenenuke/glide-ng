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

## 4. Playbook in Action: The `test06` Case Study

The following walkthrough demonstrates how this playbook was applied to solve the `test06` (black screen on Glide 3) regression:

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
