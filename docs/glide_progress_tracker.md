# 3dfx Glide Wrapper Modernization Progress Tracker

This document serves as the authoritative, highly detailed **Implementation Progress Tracker**. It is organized into an up to three-level bulleted structure (Phase -> Major Component Task -> Verification Sub-task).

We will continually update and refine this tracker as we complete operational milestones, alter architectural strategies, or expand our testing scope.

---

## Current Overall Progress Status
**Active Operational Milestone**: Phase 6 (Advanced Fog Atmospheric Effects & Direct LFB Memory Access) <br>
**Completed Phases**: `6.5 / 7` (Phase 7: Multi-Backend completed ahead of schedule; Phase 6 remaining)

---

## Detailed Task Breakdown

* **Phase 1: Foundation, Build Architecture & Automated Logging** *(Completed)*
  * `[x]` **Task 1.1: CMake Multilib Build Hierarchy Setup**
    * `[x]` Configure primary root `CMakeLists.txt` with C++20 standard enforcement.
    * `[x]` Set up independent target outputs for `glide2x` (`glide2x.dll`/`libglide2x.so`) and `glide3x` (`glide3x.dll`/`libglide3x.so`).
    * `[x]` Integrate Catch2 test framework runner (`ctest`) and reference inclusion paths from `./refs`.
  * `[x]` **Task 1.2: Core Automated Logging Engine**
    * `[x]` Implement RAII file logger writing `glide_wrapper.log` to the active game working directory.
    * `[x]` Expose 5-tier runtime logging verbosity (`OFF`, `CRITICAL`, `WARN`, `INFO`, `DEBUG`).
    * `[x]` Implement automatic startup summaries recording game display requests, host Vulkan/EGL capabilities, and active Voodoo emulation caps.
  * `[x]` **Task 1.3: Extensible Configuration System (`IConfigLoader`)**
    * `[x]` Define abstract `IConfigLoader` parser interface.
    * `[x]` Implement concrete `JsonConfigLoader` targeting `glide_config.json`.
    * `[x]` Build runtime resolution hierarchy: (1) App Args -> (2) Environment (`GLIDE_WRAPPER_*`) -> (3) `glide_config.json` -> (4) Base Defaults.
  * `[x]` **Task 1.4: Emulation Registry & Feature-Gating Registry**
    * `[x]` Implement `WrapperConfig` storage tracking emulated hardware properties (Voodoo model, FBI RAM, TMU count, and artificial delays).
    * `[x]` Build automatic defaulting logic: set `glide2x` default version to `2.43` and `glide3x` default version to `3.10`.
    * `[x]` Implement strict legacy feature-gating (e.g., disabling single-pass multi-texturing if card model is set to `SST1` / Voodoo1).
  * `[x]` **Task 1.5: Foundational Hardware Query APIs**
    * `[x]` Implement exported legacy C functions: `grGlideInit`, `grGlideShutdown`, `grSstQueryHardware`, `grSstQueryBoards`, `grGetString`, `grGetProcAddress`.
  * `[x]` **Task 1.6: Authoritative End-User & Architectural Documentation**
    * `[x]` Author `docs/glide_vendor_overrides.md` guiding end-users on custom vendor and renderer string reporting.
    * `[x]` Author `docs/glide_api_versions_overview.md` outlining valid Glide ABI specifications and core functional differences.
    * `[x]` *Verification*: Execute Catch2 unit test validating accurate string reporting against active `WrapperConfig`.
    * `[x]` *Verification*: Execute legacy diagnostic test `./refs/3dfx-Glide-API/glide3x/h5/glide3/tests/test27.c` (cycling hardware on/off).

* **Phase 2: Context Attachment & Presentation Buffer Swapping** *(Completed)*
  * `[x]` **Task 2.1: Unified Backend Interface (`IGraphicsBackend`)**
    * `[x]` Author pure virtual C++ rendering interface decoupling frontend Glide state from low-level graphics execution.
  * `[x]` **Task 2.2: Vulkan Surface Attachment**
    * `[x]` Implement concrete `VulkanBackend` class initializing `vk::Instance`, `vk::PhysicalDevice`, and `vk::Device`.
    * `[x]` Author window attachment surface creation for native Linux (`Xlib` / `Wayland`) and automated headless CI testing.
  * `[x]` **Task 2.3: Framebuffer Execution & VSync Management**
    * `[x]` Implement `grSstWinOpen`, `grWinOpen`, `grSstWinClose`, `grBufferClear`, and `grBufferSwap`.
    * `[x]` Implement `grSelectContext` and `grRenderBuffer`. Enforce artificial frame presentation delays.
    * `[x]` *Verification*: Execute Catch2 multilib `GENERATE` automated validation matrix across all emulated card profiles.
    * `[x]` *Verification*: Execute legacy golden test sequence `test00.c` (Buffer clear and smooth double-buffered swapping).

* **Phase 3: Basic Primitive Rendering & Dynamic Vertex Layouts** *(Completed)*
  * `[x]` **Task 3.1: Modernized Vertex Layout Specification**
    * `[x]` Implement legacy layout structures and parameter parsers (`grVertexLayout`, canonical layout).
  * `[x]` **Task 3.2: Vulkan Rendering Pipeline Architecture**
    * `[x]` Generate foundational SPIR-V vertex and fragment shader vectors (`VulkanShaders.h`) for Gouraud color interpolation.
    * `[x]` Allocate immediate linear pixel buffers and execution structures.
  * `[x]` **Task 3.3: Primitive Geometry Upload & Drawing**
    * `[x]` Implement core draw entry points: `grDrawPoint`, `grDrawLine`, `grDrawTriangle`. Enforce artificial microsecond bus delays per draw call.
    * `[x]` *Verification*: Execute Catch2 automated multilib parity checks across all emulated accelerator card profiles.

* **Phase 4: Depth (Z/W) Comparison, Alpha Blending & Culling States** *(Completed)*
  * `[x]` **Task 4.1: Depth Buffer Comparison Engines**
    * `[x]` Implement legacy Z-buffer and W-buffer mode logic (`grDepthBufferMode`, `grDepthBufferFunction`, `grDepthMask`, `grCoordinateSpace`, `grDepthRange`).
    * `[x]` *Verification*: Assert exact depth intersection logic matching `test05.c` and `test06.c`.
  * `[x]` **Task 4.2: Alpha Blending & Alpha Testing Pipelines**
    * `[x]` Implement Glide alpha blend formulas (`grAlphaBlendFunction`) and strict threshold comparators (`grAlphaTestFunction`, `grAlphaTestReferenceValue`).
    * `[x]` *Verification*: Execute Catch2 integration suites evaluating accurate transparency and edge alpha tests (`test07.c`).
    * `[x]` *Bug Fix*: Corrected test suite alpha blending test parameter from `GR_BLEND_ONE` to `GR_BLEND_SRC_ALPHA` (matching expected 50% blended results).
  * `[x]` **Task 4.3: User Clipping Windows & Polygon Culling**
    * `[x]` Implement dynamic scissor test rectangle boundaries (`grClipWindow`) and backface polygon winding orientation (`grCullMode`).
    * `[x]` *Verification*: Assert precise polygon culling (`test10.c`) and visual clipping rectangles (`test15.c`).
    * `[x]` *Bug Fix*: Fixed inverted backface culling conditions where positive/negative area triangle culling was swapped.

* **Phase 5: Virtual Texture Mapping Units (TMUs) & Uber-Shader Engine** *(Completed)*
  * `[x]` **Task 5.1: Virtual TMU Memory Management Layer**
    * `[x]` Build texture allocation trackers (`grTexCalcMemRequired`, `grTexSource`). Strictly enforce user-configured `TmuMemoryMb` ceilings.
    * `[x]` Implement dynamic MIP-map data uploading (`grTexDownloadMipMap`, `grTexDownloadMipMapLevel`), clamping rules (`grTexClampMode`), and filtering modes (`grTexFilterMode`).
  * `[x]` **Task 5.2: Programmable Uber-Shader Engine**
    * `[x]` Old Glide uses flexible register combiners (`grColorCombine`, `grAlphaCombine`). Build a compiled GLSL/SPIR-V Uber-Shader engine that dynamically matches active Glide combiners to programmable graphics shaders.
    * `[x]` Support advanced multi-base composite textures (`grTexMultibase`, `grTexMultibaseAddress`).
  * `[x]` **Task 5.3: Extensive Texturing Verification Suite**
    * `[x]` *Verification*: Execute Catch2 integration validation suites confirming exact POINT / BILINEAR texture interpolation, multi-MIP swizzling, and register combiner blending parity.
    * `[x]` *Bug Fix*: Fixed anisotropic texture coordinate scaling bug in software rasterizer. Implemented isotropic scaling based on the largest texture dimension (`maxDim = max(w, h)`), resolving cut-off rendering for non-square font textures.

* **Phase 5.5: SDL2 Visual Presentation Console & Reference Test Validation** *(Completed)*
  * `[x]` **Task 5.5.1: Unified SDL2 Interactive Desktop Window Integration**
    * `[x]` Build native SDL2 interactive Wayland/X11 window generation in `VulkanBackend::AttachWindow` when `hWnd == 0`.
    * `[x]` Introduce `GLIDE_FORCE_NO_WINDOW=1` environment override for automated headless CI/CD runs.
    * `[x]` Bind presentation console swapchains and UI event loops (`SDL_PollEvent`).
  * `[x]` **Task 5.5.2: External Reference Test Compatibility Singletons**
    * `[x]` Support implicit `grGlideInit()` wakeups across `glide2x` and `glide3x` entry bindings upon window or query invocation.
    * `[x]` Synchronize Direct Linear Framebuffer CPU host writes (`grLfbGetWritePtr`, `grLfbWriteRegion`).
  * `[x]` **Task 5.5.3: Core Reference Suite Execution & Visual Sign-Off**
    * `[x]` Fulfill successful build and flawless interactive visual desktop validation across external 3dfx tests (`Glide2xTest00`, `test18.c`).

* **Phase 6: Advanced Fog Atmospheric Effects & Direct LFB Memory Access**
  * `[ ]` **Task 6.1: Atmospheric Fog Computation**
    * `[ ]` Implement Fog table math, blending modes, and global fog color values (`grFogMode`, `grFogColorValue`, `grFogTable`).
    * `[ ]` *Verification*: Execute original 3dfx atmospheric fog test (`test08.c`).
  * `[ ]` **Task 6.2: Direct Linear Frame Buffer (LFB) Memory Access**
    * `[ ]` Expose direct CPU read/write pointer locks into host staging buffers (`grLfbLock`, `grLfbUnlock`, `grLfbWriteRegion`, `grLfbReadRegion`).
    * `[ ]` Implement dynamic Glide pixel swizzling and byte ordering conversions.
    * `[ ]` *Verification*: Assert direct CPU pixel modifications (`test11.c`) and high-speed blitting routines (`test26.c`).

* **Phase 7: Multi-Backend Expansion Layer (EGL Implementation)** *(Completed)*
  * `[x]` **Task 7.1: EGL Concrete Backend Authoring**
    * `[x]` Implement concrete `EGLBackend` layer using OpenGLES / EGL display contexts.
  * `[x]` **Task 7.2: Headless Cross-Backend Matrix Parity Verification**
    * `[x]` *Verification*: Execute the entirety of Passed test suites (`test00.c` through `test26.c`) with `GLIDE_WRAPPER_BACKEND=EGL`.
    * `[x]` *Verification*: Automate pixel checksum comparisons between Vulkan output and EGL output to certify 100% platform-independent parity.
