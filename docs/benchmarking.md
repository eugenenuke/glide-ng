# 3dfx Glide Modernization Wrapper: Benchmarking & Performance Guide

This document defines the benchmarking architecture, execution guidelines, and profiling methodologies used to optimize the Glide 2.x/3.x modernization wrapper.

---

## 1. Why We Benchmark

Since the entire graphics stack (including OpenGLide, our wrapper, and the target backend) executes on a headless Linux host under **software GPU emulation (Mesa LLVMpipe)**, our primary performance tuning goal is minimizing **host CPU cycles**.

Specifically, our optimizations target:
1.  **Driver Command Recording Overhead**: Eliminating redundant Vulkan/GLES state transitions, descriptor updates, and `vkCmdDraw` calls via dynamic draw batching.
2.  **CPU-GPU Pipeline Stalls**: Avoiding blocking operations like `vkDeviceWaitIdle()` or readbacks during texture uploads and frame presentations.
3.  **Host-Visible Memory Copies**: Reducing vertex buffer mapping and pixel copying overhead.

---

## 2. The Reference Benchmarks

We measure performance using two legacy C test binaries compiled in the `ext_tests/build_tests/` directory:

### A. `test14` (Depth Bias)
*   **Characteristics**: Solid geometry rendering with Gouraud shading, minimal state changes, and **no texturing**.
*   **Tuning Scope**: Represents the **raw geometry throughput** of the vertex pipeline and the CPU VBO ring-buffer accumulation efficiency.

### B. `test21` (Texture Compositing)
*   **Characteristics**: Multi-pass texture compositing combined with a high-frequency text overlay (`tlib`'s `tlConRender` drawing the console character quads).
*   **Tuning Scope**: Represents a **high-stress rendering loop**. It draws hundreds of small quads per frame, testing the limits of constant color interception, texture binding states, and draw call batching.

---

## 3. The Official Benchmark Runner: `tests/benchmark.py`

To eliminate Python interpreter latency, image capture disk IO, and pixel comparison verification overhead, we use a dedicated timing script:

*   **Location**: [tests/benchmark.py](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/benchmark.py)
*   **Methodology**:
    1.  Executes the target binary for exactly **120 frames** using the `-n 120` frame-limit flag.
    2.  Performs a **warm-up run** first: This allows the Vulkan backend to compile its SPIR-V shader pipelines, allocate dynamic ring buffers, perform layout transitions, and load texture assets into memory.
    3.  Performs an **active timing run**: Measures the elapsed execution time with microsecond precision using `time.perf_counter()`.
    4.  Calculates and displays the average **FPS (Frames Per Second)** and **Frame Time (ms)**.

### Running the Benchmarks
To run the benchmarks under Vulkan Headless mode:
```bash
# Execute from the workspace root
python3 tests/benchmark.py
```

---

## 4. Critical Pitfalls & Troubleshooting

### The "6.92 FPS" Silent Software Fallback
> [!WARNING]
> If you run the legacy test binaries from the wrong working directory, they will fail to locate their local `.3df` texture files (e.g. `decal1.3df`, `lava.3df`, `light.3df` in `ext_tests/build_tests/`).
>
> When texture files are missing, the legacy C tests pass uninitialized memory structs containing garbage dimensions to Glide. The Glide wrapper's texture manager will calculate a garbage **1.4 GB texture footprint**, fail the silicon budget check, and print a `[CRIT]` warning.
>
> In response, the wrapper **silently falls back to the CPU Software rasterizer** to avoid crashing, causing the Vulkan backend to drop from **40.57 FPS** to **6.92 FPS**!
>
> **Prevention**: Always ensure the benchmark runner executes the test subprocesses with the working directory explicitly set to the directory containing the texture assets (`ext_tests/build_tests/`). Our [tests/benchmark.py](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/benchmark.py) script handles this automatically.

---

## 5. Current Performance Leaderboard (test21 Headless)

1.  **OpenGLide (Mesa GL) [Gold Standard]**: **277.24 FPS** (3.61 ms)
2.  **Our Vulkan (Stage 4 - Current Optimized)**: **40.57 FPS** (24.65 ms)
3.  **Our Vulkan (Stage 3 - Prior Record)**: **34.84 FPS** (28.70 ms)
4.  **Our GLES (Stage 0 - Control Group)**: **33.39 FPS** (29.95 ms)
5.  **Our Vulkan (Stage 0 - Headless Baseline)**: **27.08 FPS** (36.93 ms)
6.  **Our CPU Software Backend**: **6.98 FPS** (143.24 ms)
