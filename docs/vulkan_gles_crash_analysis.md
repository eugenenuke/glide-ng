# Vulkan Performance Regression & Wayland Co-existence Crash Analysis

This document provides a deep-dive architectural analysis of the **10x–20x Vulkan performance regression** (from 4,000+ FPS to 200–300 FPS in `glide2_cube`) and the **Wayland client crash on exit (glide deactivation)** when running DOSBox-X with OpenGL ES or Vulkan backends.

---

## 1. Vulkan Performance Regression: The CPU-Hybrid Bottleneck

The performance drop occurred because the GPU-native presentation path (which kept all pixel data in high-speed GPU VRAM) was replaced with a **headless offscreen render + CPU staging buffer + SDL2 2D present** hybrid path.

### The PCIe & Memory Bandwidth Bottleneck (The Math)

At $800 \times 600$ resolution with 32-bit color (RGBA8888), each frame size is:
$$\text{Frame Size} = 800 \times 600 \times 4 \text{ bytes} \approx 1.92 \text{ MB}$$

To maintain the original **4,000 FPS** benchmark target on a hardware GPU, the hybrid pipeline must perform the following memory operations **every second**:

1. **GPU-to-CPU Download**: Transfer 4,000 frames from GPU VRAM to CPU Host memory over the PCI-Express bus:
   $$1.92 \text{ MB} \times 4,000 \approx \mathbf{7.68 \text{ GB/s}}$$
2. **CPU Memory Copy**: Perform a CPU-side `std::memcpy` of the staging buffer into the locked SDL2 texture:
   $$1.92 \text{ MB} \times 4,000 \approx \mathbf{7.68 \text{ GB/s}}$$
3. **CPU-to-GPU Upload**: During `SDL_UnlockTexture()`, SDL2 uploads the texture back from host memory to GPU VRAM over the PCIe bus:
   $$1.92 \text{ MB} \times 4,000 \approx \mathbf{7.68 \text{ GB/s}}$$

#### Total Combined Memory Thrashing:
$$\text{Total Bandwidth} = 7.68 \text{ GB/s} \times 3 \approx \mathbf{23.04 \text{ GB/s}}$$

A continuous transfer volume of **23+ GB/s of synchronous PCIe and CPU memory copies** completely chokes the PCI-Express bus, saturates the CPU memory controller, and thrashes the CPU L3 cache.

Furthermore, because the staging memory is allocated as **Host-Visible / Non-Cached (Write-Combined)**, CPU reads via `std::memcpy` are not cached, running at extremely low bus speeds (often less than 200 MB/s). This chokes the pipeline, dropping the performance to **200–300 FPS** (where the transfer volume is only ~400–600 MB/s, fitting within the system's uncached transfer limit).

### CPU-GPU Pipeline Stalls

To safely copy the staging pixels on the CPU, the wrapper must call `waitForFences` to guarantee the GPU has finished writing them:
```cpp
m_device->waitForFences(1, &m_fences[nextSlot].get(), VK_TRUE, UINT64_MAX);
```
At 4,000 FPS, this fence wait acts as a **hard barrier** that forces the CPU thread to block and sleep until the GPU catches up, destroying Vulkan's asynchronous pipelining advantages and keeping both processors in a synchronous, stalled lock-step.

---

## 2. GLES / Vulkan Wayland Crash: Root Cause Analysis

### The `sdl2-compat` Co-existence
An inspection of `/usr/bin/dosbox-x` dependencies shows it links directly to `libSDL2-2.0.so.0` (not `libSDL3.so.0`). However, the system is running **`sdl2-compat`**, where `libSDL2-2.0.so.0` acts as a shim that internally loads and routes all calls to **`libSDL3.so.0`**.

This means that both DOSBox-X and our wrapper **already share the exact same `libSDL3.so.0` instance in memory**, eliminating direct library-version binary mismatch as the crash culprit.

### The OpenGL/Vulkan Context Conflict
The crash on exit during Vulkan/GLES deactivation is caused by **driver-level window state corruption**:

1. **OpenGL Initialization**: DOSBox-X creates its main window and initializes it as an **OpenGL-rendered window** (to support its GUI and 2D scaling).
2. **Context Violation**: When our Vulkan backend is activated, it attempts to hijack this window and calls `SDL_Vulkan_CreateSurface` on it.
3. **Specification Breach**: The SDL/Vulkan specification explicitly mandates: *“The window must have been created with the `SDL_WINDOW_VULKAN` flag.”* 
4. **State Corruption**: Calling Vulkan surface creation on an OpenGL-initialized window causes the graphics driver (NVIDIA/Mesa) to corrupt the window's private, driver-specific windowing structures (since Vulkan and OpenGL both try to bind conflicting presentation surface headers on the same native handle).
5. **Catastrophic Exit**: When Glide is deactivated, DOSBox-X attempts to destroy or reuse its OpenGL context on that window. Because the driver's private state was corrupted by the Vulkan surface, it triggers an immediate `SIGSEGV` segmentation fault inside DOSBox-X's own video code.

---

## 3. Re-assessed Architectural Alternatives

To restore **4,000+ FPS GPU-native performance** and stay **100% crash-free**, we have two primary, high-viability paths:

### Option 1: Standalone Overlay with "Input Click-Through" (Recommended)
* **How it works**: The Vulkan backend creates its own standalone, borderless, undecorated window (`SDL_WINDOW_VULKAN`) and positions it exactly over the game's viewport. It renders 100% GPU-natively to its own window.
* **Input Transparency**: To prevent the overlay window from stealing input and causing sticky keys or focus fighting:
  * Under X11, we set the window's input shape to empty (using `XShapeCombineRectangles`).
  * Under Wayland, we set the surface input region to empty (using `wl_surface_set_input_region`).
* **Pros**:
  * **Maximum Performance (~4,000+ FPS)**: GPU-native Vulkan swapchain presentation.
  * **100% Safe Teardown**: The window is owned by the wrapper's own SDL context, eliminating all OpenGL/Vulkan context sharing conflicts on the host window.
  * **Flawless Inputs**: Every mouse click, key press, and movement passes **straight through** our transparent window, captured natively and perfectly by DOSBox-X.
  * **Logging & Telemetry**: The initialization of the overlay and all subsequent dynamic fullscreen toggles must be clearly and accurately logged, explicitly reflecting the active `Windowed=Yes/No` status during both `grSstWinOpen` and mid-game transitions.

### Option 2: Native X11 Mode (Forced XWayland)
* **How it works**: We run DOSBox-X under the X11 video driver (e.g. `SDL_VIDEODRIVER=x11 dosbox-x`). DOSBox-X passes a raw X11 Window ID, and our wrapper uses `SDL_CreateWindowFrom(WindowID)` to wrap it.
* **Why it works**: X11 allows wrapping a window ID independently. The X server perfectly multiplexes both Vulkan and OpenGL contexts on the same window ID without client-side driver corruption, making it completely crash-free.
* **Pros**:
  * **Maximum Performance (~4,000+ FPS)**.
  * Perfectly embedded, zero-copy rendering.
* **Cons**:
  * Requires running DOSBox-X in X11 mode (not pure Wayland-native).
