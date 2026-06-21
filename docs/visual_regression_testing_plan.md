# Automated Visual Regression Testing Framework Plan

We have successfully resolved the text rendering bugs using an ad-hoc TDD approach. To scale this victory across the remaining 30 legacy Glide 2.x and Glide 3.x tests, we need a robust, automated **Visual Regression Testing Framework**.

This plan outlines the design, directory structure, pixel-parity criteria, complexity ordering of the tests, and a step-by-step implementation plan.

---

## 1. Directory Structure

To keep the codebase clean, modular, and isolated, we will organize the `tests/` directory as follows:

```
tests/
├── CMakeLists.txt                 # Build configuration for the test suite
├── run_tests.py                   # Main Python test runner, orchestrator, and comparator
├── reference_dumps/               # Golden reference PPM images (produced once via OpenGLide)
│   ├── test00_mode0_frame14.ppm
│   ├── test01_mode0_frame14.ppm
│   └── ...
├── test_dumps/                    # Temporary PPM images produced by our library (gitignored)
│   ├── test00_mode0_frame14.ppm
│   └── ...
├── framework/                     # Core test harness library
│   ├── TestRunner.h               # Base class definitions and Runner interfaces
│   ├── TestRunner.cpp             # Common test loop, LFB capture, and PPM writer
│   └── tlib_shim.cpp              # Minimal tlib mocks (removes user key-press blocking)
├── test00_clear/
│   └── test00.cpp                 # Migrated C++ test for buffer clearing
├── test01_points/
│   └── test01.cpp                 # Migrated C++ test for point drawing
├── test02_lines/
│   └── test02.cpp                 # Migrated C++ test for line drawing
└── ...
```

---

## 2. Test Framework Design

### A. The C++ Base Test Interface (`framework/TestRunner.h`)
Every migrated test will be a clean C++ class inheriting from a common `BaseTest` interface. This eliminates the legacy `main()` function per test and allows a single runner executable to orchestrate everything.

```cpp
#pragma once
#include <string>

namespace GlideTest {

class BaseTest {
public:
    virtual ~BaseTest() = default;

    // Unique name of the test (e.g. "test00")
    virtual std::string GetName() const = 0;
    
    // Setup resolutions, initialize data
    virtual void Initialize() = 0;
    
    // Configure the specific sub-mode/input configuration (for tests that cycle through features)
    virtual void SetMode(int modeIndex) { (void)modeIndex; }
    
    // Returns the total number of interactive/input modes to test
    virtual int GetTotalModes() const { return 1; }
    
    // Render a single frame. Returns false if the test should stop.
    virtual bool RenderFrame(int frameIndex) = 0;
    
    // Shutdown Glide and release resources
    virtual void Shutdown() = 0;
    
    // Frame pacing controls
    virtual int GetMaxFrames() const { return 15; } // Let Xvfb/window map completely
    virtual int GetCaptureFrameIndex() const { return 14; } // Frame to dump
};

} // namespace GlideTest
```

### B. Automated Frame Capturing (`framework/TestRunner.cpp`)
At the designated `GetCaptureFrameIndex()`, the test runner will automatically:
1. Call `grLfbReadRegion(GR_BUFFER_BACKBUFFER, ...)` to extract the 16-bit RGB565 frame buffer.
2. Convert the buffer to a standard, uncompressed 24-bit RGB `.ppm` file.
3. Write the file to `test_dumps/` (or `reference_dumps/` during golden generation).
4. Exit the process immediately—**eliminating all blocking user interaction (keyboard checks)**.

### C. The Python Test Runner & Comparator (`run_tests.py`)
A python script will orchestrate the execution and comparison:
1. **Golden Reference Mode:** If a reference dump is missing, it runs the test preloading the reference `openglide` library:
   `LD_PRELOAD=/usr/lib/libSDL2.so LD_LIBRARY_PATH=ext_tests/build_tests/libs/ xvfb-run -a ./build/bin/test_runner --test test00 --mode capture`
2. **Comparison Mode:** Runs the test using our wrapper library:
   `GLIDE_FORCE_NO_WINDOW=1 LD_LIBRARY_PATH=build/lib/ ./build/bin/test_runner --test test00 --mode run`
3. **Automated Comparison:** Compares the two PPM files using our **Pixel-Parity Criteria** (see Section 3).
4. **Report Generation:** Outputs a clean, colorized CLI report with pass/fail status and detailed pixel difference statistics.

### D. Multi-Mode Test Capture Strategy
Some legacy Glide tests (e.g., `test17` or `test20`) wait for user key presses to cycle through different rendering modes (e.g., changing texture filters, blending combinations, or mipmap levels).

To automate this without blocking on user input:
1. The test class will override `GetTotalModes()` to specify how many configurations it has.
2. The test runner will execute the test in a loop:
   ```cpp
   for (int mode = 0; mode < test->GetTotalModes(); ++mode) {
       test->Initialize();
       test->SetMode(mode);
       for (int frame = 0; frame < test->GetMaxFrames(); ++frame) {
           test->RenderFrame(frame);
           if (frame == test->GetCaptureFrameIndex()) {
               captureFrame(test->GetName(), mode);
           }
       }
       test->Shutdown();
   }
   ```
3. The captured files will be named `[test_name]_mode[mode]_frame[frame].ppm` (e.g. `test17_mode0_frame14.ppm`, `test17_mode1_frame14.ppm`, etc.). This ensures we capture and verify every single visual output state of the test!

---

## 3. Pixel-Parity Criteria

Because different rasterizers (Mesa/OpenGLide hardware vs. our custom software Vulkan rasterizer) have minor differences in float-to-int rounding, bilinear texture filtering interpolation, and color-space conversions, **strict bitwise binary equality is not practical**.

Instead, we will use a robust, mathematically sound **Color Tolerance Metric**:
* **Per-Pixel Tolerance:** A pixel is considered matching if the absolute difference in each RGB channel is within a small threshold:
  $$\max(|R_1 - R_2|, |G_1 - G_2|, |B_1 - B_2|) \le \text{Threshold}$$
  *(Default Threshold = 8, representing a 3% color variance).*
* **Overall Pass Threshold:** The test passes if the percentage of matching pixels is $\ge 98.0\%$.
* **Layout and Geometry Safety:** This easily catches coordinates displacement, missing geometries, wrong textures, and incorrect blending, while ignoring minor sub-pixel rendering or filtering noise.

---

## 4. Test Case Complexity Ordering (Glide 2.x sst1/cvg)

We have analyzed all 30 legacy tests and ordered them from simplest to most complex. We will implement and reconcile them in this exact order:

| Complexity | Test | Name / Purpose | Key Features Exercised |
| :--- | :--- | :--- | :--- |
| **1 (Low)** | `test00` | Buffer Clear | Context initialization, buffer clears |
| **2** | `test01` | Draw Points | Basic point rendering, constant color combiner |
| **3** | `test02` | Draw Lines | Basic flat line rendering |
| **4** | `test15` | Clip Rectangle | Scissor box viewport containment |
| **5** | `test28` | Planar Polygon | Flat planar polygon rendering |
| **6** | `test03` | Gouraud Lines | Vertex color interpolation along lines |
| **7** | `test04` | Gouraud Triangles | Vertex color interpolation across triangles |
| **8** | `test10` | Backface Culling | CW vs CCW triangle culling |
| **9** | `test05` | Z Buffer | Depth testing, depth write controls |
| **10** | `test06` | W Buffer | Perspective-correct depth testing |
| **11** | `test14` | Depth Bias | Depth offsets, decal layers |
| **12** | `test13` | Iterated Alpha | Vertex alpha interpolation |
| **13** | `test07` | Alpha Blend | Source/dest transparency blending |
| **14** | `test09` | Chromakey | Transparent color-key masking |
| **15** | `test08` | Fog | Linear and table-driven fog combining |
| **16** | `test22` | Multi-pass Fog | Advanced multi-pass fog blending |
| **17** | `test17` | Basic Texturing | Decal, RGB lit, and white lit textures |
| **18** | `test18` | Texture Alpha | Alpha-channel texture masking |
| **19** | `test19` | Texture Filters | Nearest-neighbor vs Bilinear filtering |
| **20** | `test20` | Mipmap Modes | LOD selection, trilinear filtering |
| **21** | `test29` | Palette Textures | 8-bit color lookup table (LUT) texturing |
| **22** | `test21` | Texture Compositing | Multi-texture (lightmap, detail, specular) |
| **23** | `test11` | LFB Direct Access | Lock, read, and write to LFB |
| **24** | `test12` | LFB coordinate transformations | LFB with rendering coordinate scaling |
| **25** | `test26` | grLfbWriteRegion | Block memory copies directly to LFB |
| **26** | `test23` | AA Points | Anti-aliased points |
| **27** | `test24` | AA Lines | Anti-aliased lines |
| **28 (High)**| `test25` | AA Triangles | Anti-aliased triangles |

---

## 5. Step-by-Step Implementation Plan

### **Phase 1: Test Suite Foundations (Target: 1 day)**
* [ ] Create the core C++ interface [TestRunner.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/framework/TestRunner.h) and [TestRunner.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/framework/TestRunner.cpp).
* [ ] Implement the automated frame-buffer PPM writer.
* [ ] Create the Python test runner [run_tests.py](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/run_tests.py) with pixel comparison logic (Delta E thresholding).
* [ ] Verify the build infrastructure inside `tests/CMakeLists.txt`.

### **Phase 2: Migrate & Reconcile Group A (Basic Rasterization) (Target: 2 days)**
* [ ] Migrate `test00` (Buffer Clear), `test01` (Points), and `test02` (Lines) into the new C++ classes.
* [ ] Generate reference golden PPMs using `openglide`.
* [ ] Run them against our library and verify they achieve pixel-parity immediately.
* [ ] Migrate `test15` (Clip Rectangle) and `test28` (Planar Polygon) to complete basic rasterization verification.

### **Phase 3: Group B & C (Interpolation, Depth, Culling) (Target: 3 days)**
* [ ] Migrate `test03` (Gouraud Lines) and `test04` (Gouraud Triangles) to verify color interpolation.
* [ ] Migrate `test10` (Culling), `test05` (Z Buffer), and `test06` (W Buffer) to verify depth operations.
* [ ] Run comparisons, debug, and patch our Vulkan/software rasterizer as needed.

---

## 6. Stuck Task Prevention & Interactive Prompt Bypassing

During automated or headless execution, background tasks must never block on user input or hang indefinitely.

### A. The Startup Keypress Hook (`tlGetCH()`)
* **The Issue**: Many legacy Glide tests (e.g., `test34`) default to a frame count of `-1`. In the test helper library (`tlib`), if `frames == -1`, it prompts `"Press A Key To Begin Test."` and calls `tlGetCH()` to block on terminal input.
* **The Impact**: In automated, headless, or CI/CD pipelines, this blocks the execution context indefinitely, resulting in a hung agent or pipeline.
* **The Solution**: 
    1. **Command-Line Overrides**: Always pass `-n <positive_integer>` (e.g., `-n 120` or `-n 99999`) to the test executable to set an explicit frame limit and completely bypass the startup keyboard wait.
    2. **Weak Symbol Mocking**: In the custom test harness (`tlib_shim.cpp`), mock the keyboard polling functions (`lin_kbhit` and `lin_getch` / `tlKbHit` and `tlGetCH`) to automatically return progress signals or immediately return no-op triggers.

### B. Automated Pipeline Watchdog Timers
* **The Best Practice**: When spawning background test runs or visual capturing loops:
    1. **Set Bounded Runtimes**: Ensure all spawned background processes are launched with explicit timeouts (e.g., wrapping execution in `timeout 30s` in bash).
    2. **Agent Watchdog Reminders**: If executing within an agentic loop, always schedule a companion one-shot watchdog timer (e.g., using the `schedule` tool set to 30–60 seconds) immediately upon launching a background task. This ensures the agent is forcefully woken up to diagnose and clean up any stuck processes if a driver deadlock or input hang occurs.

