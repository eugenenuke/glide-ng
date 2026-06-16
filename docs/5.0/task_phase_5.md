# Phase 5 Operational Task Tracker: Virtual TMUs & Uber-Shader Engine

This tracker monitors our step-by-step progress during the implementation and verification of **Phase 5** for the modernized 3dfx `glide-ng` engine.

---

### Task 5.1: Virtual TMU Memory Management Layer
- `[ ]` **5.1.1**: Author precise MIP-map footprint calculation routines in `grTexCalcMemRequired` adhering exactly to 3dfx byte alignment specifications.
- `[ ]` **5.1.2**: Author legacy `grTexDownloadMipMap` and `grTexDownloadMipMapLevel` decoding routines in `glide2_entry.cpp` and `glide3_entry.cpp`.
- `[ ]` **5.1.3**: Implement the virtual TMU memory allocation storage pool inside `VulkanBackend.cpp`. Enforce active `TmuMemoryMb` silicon ceilings and record detailed memory consumption warnings.

### Task 5.2: Programmable Uber-Shader Engine
- `[ ]` **5.2.1**: Implement active register combiner Setters (`grColorCombine`, `grAlphaCombine`, `grTexCombine`) across both frontend wrapper entries.
- `[ ]` **5.2.2**: Implement `DownloadMipMap` and `BindTexture` state caching logic in `VulkanBackend.cpp`.
- `[ ]` **5.2.3**: Author the authentic software texture sampler (`SampleTexture`) and register combiners inside `RasterizeSoftwareTriangle`. Enforce precise texel clamping (`WRAP` vs `CLAMP`) and filtering interpolation (`POINT` vs `BILINEAR`).

### Task 5.3: Catch2 Automated Verification Suite
- `[ ]` **5.3.1**: Author `Phase 5: Virtual TMU & Uber-Shader Texturing Suites` inside `tests/test_architecture.cpp`.
- `[ ]` **5.3.2**: Assert exact TMU memory footprint calculations.
- `[ ]` **5.3.3**: Download a retro texture pattern, bind it via `grTexSource`, and assert exact texel lookups.
- `[ ]` **5.3.4**: Assert register combiner execution (Decal overwrite vs. Modulate multiplication).
- `[ ]` **5.3.5**: Assert exact subpixel bilinear filtering interpolation across adjacent texel clusters.

---

### Final Milestone Verification
- `[ ]` Compile and execute `./bin/GlideWrapperTests` to confirm 100% all-green mathematical success with zero memory leaks.
