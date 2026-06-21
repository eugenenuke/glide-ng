# Instructions for Visual Quality & Regression Analysis Agent

This document serves as the authoritative, step-by-step instruction guide for a future AI coding agent to perform a comprehensive visual quality assurance and regression analysis pass across all Glide 2.x and Glide 3.x tests and all rendering backends.

---

## 1. Objective

Your task is to orchestrate, execute, capture, and visually analyze the entire test suite of the Glide wrapper. You will compile your findings into a single, highly structured visual report named **`visual_matrix_report.md`** inside the conversation artifacts directory. 

The report must allow a human validator to easily verify visual rendering correctness across all three backends:
1.  **Vulkan** (GPU)
2.  **OpenGL ES 3.2** (GPU)
3.  **Software** (CPU Reference Rasterizer)

---

## 2. Report Structure & Specifications

The final report must strictly follow these structural requirements:

### A. Sections
The report must be split into two main sections:
1.  **Glide 2.x Test Matrix**
2.  **Glide 3.x Test Matrix**

### B. Per-Test Visualization
Under each test (e.g. `test00` to `test39`), you must present:
1.  **The Code Analysis**: A brief (1-2 sentence) summary of what the test draws based on its C source code, and what the expected behavior is on real 3dfx Voodoo hardware.
2.  **The Backend Comparison**: A Markdown carousel displaying the captured screenshots for the 3 backends.
3.  **The Visual Parity Verdict**: A brief description noting any visual differences, rendering errors, clipping issues, or shading anomalies between the backends.

### C. Capturing Multi-Mode and Animated Tests
*   **Multi-Mode / Subtests**: Some tests (e.g. `test10` culling, `test12` LFB, `test22` texture formats) cycle through different modes or configurations. You must capture a complete set of screenshots **per mode**.
*   **Moving / Animated Objects**: For tests that draw moving geometry (e.g., `test16` spinning splash logo, `test21` rotating gears), you must capture **at least two screenshots at different time intervals** to demonstrate motion and verify frame buffer clearing.

---

## 3. Environment & Display Setup

To run interactive graphical tests and programmatically capture screenshots in the workspace, you must resolve the active X11 display server and its authentication token.

### The Display Initialization Sequence
Execute the following commands in your shell before running any tests:

```bash
# Set the default display target
export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/794553

# Dynamically extract the Mutter/Xwayland authority token
MUTTER_AUTH=$(ps aux | grep -o '/run/user/[0-9]*/\.mutter-Xwaylandauth\.[a-zA-Z0-9]*' | head -n 1)
if [ -n "$MUTTER_AUTH" ]; then
    export XAUTHORITY=$MUTTER_AUTH
    echo "Resolved XAUTHORITY: $XAUTHORITY"
else
    export XAUTHORITY=~/.Xauthority
    echo "Fallback to default XAUTHORITY: $XAUTHORITY"
fi

# Set the Glide library path
export LD_LIBRARY_PATH=/usr/local/google/home/eianiuk/gemini/3dfx/build/lib
```

---

## 4. Test Orchestration & Execution

### A. Finding Test Executables
The test binaries are located in the repository submodules:
*   **Glide 2.x Tests**: `./refs/glide/glide2x/sst1/glide/tests/testXX.exe`
*   **Glide 3.x Tests**: `./refs/glide/glide3x/h3/glide3/tests/testXX.exe`

*(Note: Despite the `.exe` extension, these are standard Linux ELF executables.)*

### B. Targeting Window Titles
When a test is launched, our wrapper creates an SDL window. The window title is determined dynamically by the active rendering backend. You must target the exact window title when using capture utilities:
*   **Vulkan**: `"3dfx glide-ng Presentation Console (Vulkan)"`
*   **OpenGL ES**: `"3dfx glide-ng Presentation Console (OpenGL ES 3.2)"`
*   **Software**: `"3dfx glide-ng Presentation Console (Software)"`

---

## 5. Automated Screen Capture Script Pattern

Use the following bash script structure as a template to orchestrate running a test, waiting for initialization, capturing the window, and performing clean-up:

```bash
#!/bin/bash
# (Insert the Display Initialization Sequence here)

TEST_BIN=$1
TEST_NAME=$2
BACKEND=$3
OUTPUT_PNG=$4
SLEEP_TIME=${5:-3.0}

# Configure wrapper backend
export GLIDE_WRAPPER_BACKEND=$BACKEND

# Resolve the target window title
if [ "$BACKEND" == "vulkan" ]; then
    WIN_TITLE="3dfx glide-ng Presentation Console (Vulkan)"
elif [ "$BACKEND" == "opengl_es" ]; then
    WIN_TITLE="3dfx glide-ng Presentation Console (OpenGL ES 3.2)"
else
    WIN_TITLE="3dfx glide-ng Presentation Console (Software)"
fi

echo "Launching $TEST_NAME on $BACKEND..."
$TEST_BIN -r 640x480 >/dev/null 2>&1 &
PID=$!

# Wait for window mapping and rendering
sleep $SLEEP_TIME

# Capture window using ImageMagick
import -window "$WIN_TITLE" "$OUTPUT_PNG"

# Terminate the test process immediately to prevent resource leaks
kill -9 $PID
echo "Captured $OUTPUT_PNG"
```

---

## 6. Analytical Correctness (No Static Image Diffing)

Do **NOT** perform a simple pixel-by-pixel static image comparison against external reference images. Font rendering, sub-pixel alignments, rounding variances, and GPU hardware quantization (such as 16-bit RGB565 dithering) will naturally cause minor pixel-level deviations.

Instead, you must **evaluate visual correctness analytically**:
1.  **Read the C source code of the test** (e.g., `test10.c`) to understand what geometry, depth, blending, and culling modes are configured.
2.  **Verify geometry presence**: Ensure the expected shapes (triangles, lines, points) are rendered in their correct positions.
3.  **Verify culling/winding logic**: In winding tests (e.g. `test10`), verify that only the expected clockwise or counter-clockwise triangles are rendered.
4.  **Verify LFB writes**: In LFB tests (e.g. `test11`/`test12`), verify that the direct pixel buffer writes (and text logos) are drawn with the correct orientation and aspect ratios.
5.  **Verify AA and Fogging**: Ensure that anti-aliasing (when enabled) and fogging smoothly blend primitives into the background without harsh, blocky pixelation.

---

## 7. Report Template

Format your **`visual_matrix_report.md`** using the following Markdown structure:

```markdown
# Glide Emulation Visual Quality & Parity Report

---

## 1. Glide 3.x Test Matrix

### test10: Triangle Culling & Winding
* **Description**: Sets up culling for positive and negative winding, drawing two triangles (clockwise and counter-clockwise) under upper-left and lower-left screen origins.
* **Expected Behavior**: Only one triangle (Blue) should be visible on startup. Pressing space toggles culling, showing only the other triangle (Red).

````carousel
![Vulkan](/path/to/test10_vulkan.png)
<!-- slide -->
![OpenGL ES](/path/to/test10_opengl_es.png)
<!-- slide -->
![Software](/path/to/test10_software.png)
````

* **Parity Analysis**: Perfect parity across all backends. Winding direction is correctly inverted when the origin swaps to lower-left, culling the appropriate triangle on both GPU and CPU renderers.

---

### test16: 3dfx Splash Screen & Shameless Plug (Animated)
* **Description**: Renders the spinning 3D logo shield followed by the translucent watermark plug overlay.
* **Expected Behavior**: Smoothly animated spinning shield with metallic red/orange/yellow shading. Watermark is semi-transparent in the bottom-right.

````carousel
![Vulkan Frame 1](/path/to/test16_vk_frame1.png)
<!-- slide -->
![Vulkan Frame 2](/path/to/test16_vk_frame2.png)
<!-- slide -->
![Software Frame 1](/path/to/test16_sw_frame1.png)
<!-- slide -->
![Software Frame 2](/path/to/test16_sw_frame2.png)
````

* **Parity Analysis**: The splash animation renders smoothly on all backends. Dynamic vertex layout states are correctly saved and restored between the console and splash screens.
```
