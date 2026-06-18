# Software Backend SIMD Optimization & Vectorization Plan

This document outlines the architectural and implementation plan to migrate our CPU-based Software Rasterizer (`SoftwareBackend`) to use modern SIMD (Single Instruction Multiple Data) vectorization. This will dramatically increase the performance of our software rendering path, approaching the rendering speeds of hardware-based backends.

---

## 1. Selected SIMD Instruction Sets

We will implement a **Multi-Tier SIMD Targeting Strategy** using runtime dispatch. We select the following instruction sets:

| Tier | Instruction Set | Vector Width | Pixels/Cycle | Target Profile |
| :--- | :--- | :--- | :--- | :--- |
| **Tier 1 (Peak)** | **AVX2 + FMA** | 256-bit | **8 pixels** | Modern desktops and servers (Broadwell/Zen and newer). Fused Multiply-Add (FMA) will merge our barycentric math into single-cycle operations. |
| **Tier 2 (Medium)**| **SSE4.2** | 128-bit | **4 pixels** | Legacy x86_64 compatibility baseline (virtually 100% of active CPUs since 2008). |
| **Tier 3 (Fallback)**| **Scalar C++** | 32-bit | **1 pixel** | Standard fallback for non-x86 or legacy systems. |

### Why not AVX-512?
* **Fragmentation**: AVX-512 is highly fragmented across Intel/AMD processors and has been dropped by Intel on mainstream hybrid client architectures (Alder Lake/Raptor Lake/Arrow Lake).
* **Thermal Throttling**: On older Xeons, executing AVX-512 instructions can trigger CPU downclocking.
* **Diminishing Returns**: The complexity of handling AVX-512 masking does not justify the minor gains over AVX2 for our 640x480 resolution target.

---

## 2. Implementation Approach: Compiler-Assisted Auto-Vectorization

Rather than writing hard-to-read, unmaintainable manual assembly or intrinsics (via `<immintrin.h>`), we will use **Compiler-Assisted SIMD with Function Multiversioning**:
1. We write clean, standard, vector-friendly C++ code.
2. We use OpenMP SIMD annotations (`#pragma omp simd`) and compiler hints to guarantee that the compiler can vectorize the loops safely.
3. We use GCC/Clang **Target Multiversioning** (`__attribute__((target("avx2,fma")))` and `__attribute__((target("sse4.2")))`) to let the compiler compile the same C++ loop into highly optimized AVX2/FMA, SSE4.2, and Scalar assemblies automatically, generating the runtime CPUID dispatch logic for us at startup.

This gives us **95% of the performance of hand-written intrinsics with 5% of the code complexity**.

---

## 3. Step-by-Step Migration Plan

### Step 1: Align Memory Structures (Data Alignment)
For SIMD vector units to load data at maximum bandwidth, memory must be aligned to vector boundaries (32-byte alignment for AVX2, 16-byte for SSE).
* We will update our internal buffers (such as `m_cpuBuffers`, depth buffer, and texture mipmap memory) to be allocated with 32-byte alignment:
  ```cpp
  // Use aligned allocators or std::aligned_alloc
  std::vector<uint8_t, AlignedAllocator<uint8_t, 32>> m_cpuBuffers[2];
  ```

### Step 2: Restructure the Rasterizer Loop (Eliminate Inner-Loop Branching)
SIMD executes the same instruction on multiple data channels. Therefore, **branching (if-statements) inside the pixel loop is the enemy of SIMD** (it causes branch divergence and defeats vectorization).
* We will refactor the inner pixel loop of `RasterizeSoftwareTriangle` to use **masking (conditional select)** instead of branching for:
  - Depth Testing
  - Alpha Testing
  - Clipping
* Instead of `if (depthPass) writePixel()`, we will compute a lane mask (e.g., `0xFFFFFFFF` for pass, `0x00000000` for fail) and perform a masked write to the framebuffer.

### Step 3: Implement Function Multiversioning
We will split the core pixel loop out of `RasterizeSoftwareTriangle` into a separate, highly vectorizable method `ProcessPixelBlock()` and annotate it:

```cpp
// AVX2 + FMA implementation
__attribute__((target("avx2,fma")))
void SoftwareBackend::ProcessPixelBlock(int startX, int y, float* w0, float* w1, float* w2) {
    #pragma omp simd
    for (int i = 0; i < 8; ++i) {
        // Interpolate, sample texture, blend, and write 8 pixels in parallel using AVX2+FMA!
    }
}

// SSE4.2 implementation
__attribute__((target("sse4.2")))
void SoftwareBackend::ProcessPixelBlock(int startX, int y, float* w0, float* w1, float* w2) {
    #pragma omp simd
    for (int i = 0; i < 4; ++i) {
        // Interpolate, sample, blend, and write 4 pixels in parallel using SSE4.2!
    }
}

// Scalar fallback
__attribute__((target("default")))
void SoftwareBackend::ProcessPixelBlock(int startX, int y, float* w0, float* w1, float* w2) {
    // Standard 1-pixel-at-a-time loop
}
```

### Step 4: Leverage FMA (Fused Multiply-Add)
Our barycentric attribute interpolation:
$$\text{val} = w_0 \times A + w_1 \times B + w_2 \times C$$
requires 3 multiplications and 2 additions per attribute. Under FMA (supported natively on your Xeon CPU!), the compiler will compile this to:
$$\text{temp} = \text{fma}(w_0, A, \text{fma}(w_1, B, w_2 \times C))$$
reducing the entire interpolation to **only 2 hardware clock cycles**!

### Step 5: Comprehensive Validation & Benchmarking
* Run the Catch2 and visual regression test suites to guarantee **100% visual and bit-level parity** is maintained across all 20+ tests under all three dynamically dispatched paths (Scalar, SSE4.2, AVX2).
* Benchmark the rendering time of complex tests (like `test21` and `test26`) under all paths to measure the exact speedup.
