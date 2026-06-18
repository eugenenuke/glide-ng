# DosBox Voodoo Emulation Codebase Analysis

This document provides a detailed architectural analysis of the SST-1/2 (Voodoo 1/2) hardware-level software emulator found in [ext_tests/dosbox-0/](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/), compares it with our current implementation, and outlines how we can leverage it as an authoritative reference for future feature parity, debugging, and unit-testing.

---

## 1. Codebase Structure & Architecture

The DosBox Voodoo emulation is split into clean, modular components:
* **[glide.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/glide.cpp)**: A guest-to-host Glide API pass-through bridge. It intercepts Glide calls from the guest DOS application and forwards them directly to the host's `glide2x` library (which could be our library!).
* **[voodoo.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo.cpp)**: Handles hardware-level interfacing, PCI FIFO emulation, memory-mapped I/O (MMIO) register reads/writes, and coordinate systems.
* **[voodoo_emu.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_emu.cpp)**: The core software rasterization engine, implementing the physical pixel pipeline, TMUs, register combiners, and scanline processors.
* **[voodoo_data.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_data.h)**: Defines core data structures and highly optimized inline pipeline macros (like `TEXTURE_PIPELINE` and `PIXEL_PIPELINE_BEGIN`).

---

## 2. Key Architectural Comparisons

Below is a comparison of how key graphics pipeline features are implemented in the DosBox hardware-level emulator versus our current Software Backend:

### A. Coordinate Space & Sub-Pixel Precision
* **DosBox Reference**:
  * Screen coordinates are converted to **12.4 fixed-point format** (integers multiplied by `16.0`), yielding exactly $1/16$th of a pixel sub-pixel precision.
  * Because the rasterizer loops incrementally, it performs **sub-pixel coordinate snapping** before scanline iteration: it calculates the offsets `dx`/`dy` from the continuous vertex to the first pixel center (`8/16`ths, or `0.5`, in 12.4 space) and adjusts the starting registers (`startr`, `startg`, `startw`, etc.) accordingly.
* **Our Software Backend**:
  * We evaluate barycentric coordinates directly at each pixel center `(x + 0.5, y + 0.5)` for every pixel.
  * This naturally and continuously achieves infinite sub-pixel precision (limited only by standard IEEE 754 float precision) without needing manual fixed-point snapping or start-register offsets!

### B. Texture LOD & Mipmapping
* **DosBox Reference**:
  * Mipmap levels are stored in a contiguous VRAM layout. The starting byte offset for each level (`lodoffset[0..8]`) is computed during texture configuration based on the format and dimensions.
  * Level of Detail (LOD) estimation ($\lambda$) is calculated per-pixel from the exponent/magnitude of the homogeneous $W$ coordinate using a fast reciprocal+log2 lookup table (`fast_reciplog`). The hardware does not do expensive spatial derivative calculations; it directly estimates LOD from $W$.
  * Support for LOD bias, clamping (`lodmin`/`lodmax`), and LOD dithering (using the 4x4 dither matrix to transition smoothly between mipmap levels).
* **Our Software Backend**:
  * Currently only supports `mipLevel = 0` (no mipmapping or LOD calculation). It samples directly from the base texture.
  * *Parity Opportunity*: We can copy their `lodoffset` and `fast_reciplog` logic if we ever want to implement full hardware-accurate mipmapping/LOD in our software rasterizer!

### C. Texture Filtering (Bilinear Alignment)
* **DosBox Reference**:
  * In bilinear mode, it shifts texture coordinates to retain 8 bits of fraction.
  * It subtracts `0x80` (`0.5` texels) so that a coordinate of `(0.5, 0.5)` maps perfectly to the center of texel `(0, 0)`—matching standard hardware filtering alignment.
  * Fetches the 4 neighboring texels and performs a weighted average.
* **Our Software Backend**:
  * We perform bilinear filtering by calculating `u_sub = texU - 0.5f` and `v_sub = texV - 0.5f` to align texel centers, and then performing a float-based bilinear blend.
  * The mathematical alignment is identical, but their implementation is integer/fixed-point optimized.

### D. Color/Alpha Combiners & Blending
* **DosBox Reference**:
  * Implements full 3dfx register-combiner logic, selecting inputs (`c_other`, `c_local`) from iterated color, texture color, or constant register colors (`color0`/`color1`), followed by hardware blending factors.
* **Our Software Backend**:
  * We have implemented a highly similar combiner and blending pipeline (supporting texture/iterated color selection and standard source/destination blend factors).
  * *Parity Opportunity*: We can use their code as a reference to verify edge cases of combiner operations (such as detail/bump mapping combinations).

### E. 16-bit Framebuffer Dithering
* **DosBox Reference**:
  * Emulates the physical Voodoo 16-bit RGB 565/555 color quantization.
  * Applies a 4x4 or 2x2 hardware dither matrix (`dither_matrix_4x4`, `dither_matrix_2x2`) to the 24-bit color before writing to the 16-bit LFB, eliminating color-banding artifacts.
* **Our Software Backend**:
  * We render directly to a 32-bit ARGB8888 buffer (`m_headlessPixelMap`).
  * Since our headless test suite upsamples 16-bit readbacks to 24-bit PPMs anyway, our 32-bit rendering is visually superior and avoids color loss, though it does not emulate the physical 16-bit color banding of the original hardware.

---

## 3. How to Leverage This Reference

### A. Cross-Verification of Complex Features (e.g. Fog Tables)
The Voodoo hardware has a 64-entry fog table where fog density is interpolated based on eye-W. Our current software fog is a simplified linear fog. If we need to implement full hardware fog table support:
* We can directly copy their 64-entry table interpolation and index lookup logic from [voodoo_emu.cpp:L698](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_emu.cpp#L698)!

### B. Unit-Testing Our Software Rasterizer
Because the DosBox emulator is a known-good, reference software implementation, we can write unit tests that compare our Software Backend's outputs with theirs for specific operations:
1. **Combiner Logic Tests**: Feed identical vertex and texture inputs to both our `EvaluateCombinerColor` and their combiner macro, and assert that the output ARGB matches.
2. **Texture Sampling Tests**: Compare point and bilinear texel fetches for complex wrapping, clamping, and format conversions.

### C. Debugging Rendering Divergences
If a future test fails under our software backend but passes under Vulkan/OpenGL (meaning it's a software-only rendering divergence):
* We can cross-reference the failing pipeline stage (e.g. alpha testing, blending, or depth biasing) with the corresponding macro in [voodoo_data.h](file:///usr/local/google/home/eianiuk/gemini/3dfx/ext_tests/dosbox-0/src/hardware/voodoo_data.h) to see if we missed a hardware-specific register flag or edge-case behavior.
