# 3dfx Legacy Assets & Code Modernization Plan

This document outlines the strategic roadmap for modernizing the legacy assets and rendering code housed in `vendor/3dfx/`. We will return to this plan in a future phase to permanently refactor these components.

---

## 1. **Current State & Challenges**

The directory `vendor/3dfx/` contains vintage code and assets inherited from the original 3dfx Glide SDK:
*   `banner.dat`: A text-based header file containing a `static unsigned short` array of 16-bit color pixels ($180 \times 90$) representing the 3dfx powershield banner.
*   `splash.dat`: A massive text-based header file containing **17,902 lines of floating-point arrays** defining the 3D meshes (vertices, normals, and UVs) for the spinning logo animation.
*   `gsplash.cpp` & `gbanner.cpp`: C-style rendering implementations that use mutable global state, manual texture address tracking, and raw pointer arithmetic.

### **The Challenges:**
1.  **Compile-Time Penalty**: Forcing the C++ compiler to parse 18,000 lines of floating-point text tokens inside `splash.dat` heavily inflates compilation time and memory footprint.
2.  **Thread Safety & Re-Entrancy Risks**: The use of mutable global variables (`nextFreeBase`, `texturesCreated`) in `gsplash.cpp` introduces severe race conditions and memory leaks if the Glide engine is initialized on multiple threads or restarted repeatedly.
3.  **Low Modifiability**: Assets cannot be edited in standard 3D/2D tools (like Photoshop or Blender) without running custom, legacy back-and-forth conversion scripts.

---

## 2. **Target Architecture & Modernization Strategy**

We will modernize both the assets and the code without sacrificing the **zero-overhead loading** and **self-contained (single-file shared library)** requirements of a drop-in Glide translation layer.

```
+-----------------------------------+
|      Target C++ Architecture      |
+-----------------------------------+
                 |
        +--------+--------+
        |                 |
        v                 v
  [Embedded RAW]     [Thread-Safe]
  [Binary Assets]   [Class Wrappers]
        |                 |
        v                 v
  * banner.raw      * GlideSplashAnimator
  * splash.raw      * Eliminates globals
  * Zero compile    * Perfect visual
    time tax          parity preserved
```

### **Phase A: Asset Binary Embedding (Zero Compile Tax)**
1.  **De-textualize Assets**: Convert `banner.dat` and `splash.dat` from text-array source files into packed, raw binary resource files (`banner.raw`, `splash.raw`).
2.  **Compile-Time Resource Embedding**: Embed the raw binary files directly into the shared library using **CMake's binary resource compiler** or modern C++20 `std::span` mapped over compiled-in bytes (e.g., using `ld -r -b binary` or `xxd -i` under the hood).
3.  *Benefits*: 
    *   **95% reduction in asset compilation time** (the compiler no longer parses 18,000 lines of text).
    *   Zero runtime loading/parsing overhead (remains compiled in).
    *   Maintains a single, self-contained `.so` library with zero external dependencies.

### **Phase B: Code Encapsulation & Thread Safety**
1.  **Do Not Rewrite the Math**: The matrix projections, vertex lighting, and shadow-casting formulas are highly specific, vintage implementations. To guarantee **100% visual parity** and avoid regression, we will preserve the exact mathematical formulas as-is.
2.  **Class Encapsulation**: Wrap the legacy global state and drawing functions into a clean, modern C++ class (e.g., `GlideSplashAnimator`):
    *   Move all global variables (like `texturesCreated` and `nextFreeBase`) into private class member variables.
    *   Instance-initialize the animator, making it **fully thread-safe, re-entrant, and clean-up friendly**.
3.  **Modern C++ Types**: Replace raw C-style pointer arithmetic and cast macros with modern type-safe equivalents (`std::array`, `std::unique_ptr`, and `reinterpret_cast`).
4.  *Benefits*: Resolves all multi-threaded rendering race conditions and leaks, making the library highly robust for modern multi-core engines.
