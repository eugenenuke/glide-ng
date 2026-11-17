# 3dfx Glide Wrapper Architectural Guide: API Versions & Differences

The **3dfx `glide-ng` Modernization Wrapper** is engineered to support the two primary historical epochs of the 3dfx proprietary graphics architecture: **Glide 2.x** and **Glide 3.x**. 

Because classic 1990s and early 2000s games were hardcoded against entirely different Application Binary Interfaces (ABIs), our wrapper compiles two completely independent multilib dynamic target replacement libraries (`libglide2x.so` / `glide2x.dll` and `libglide3x.so` / `glide3x.dll`) that sit on top of one highly optimized **Shared Unified Core** (`GlideWrapperCore`).

---

## 1. Supported Runtime ABIs & Default Versioning

| Emulated Ecosystem | Target Replacement Library | Baseline Specification | Target Retro Game Era | Primary Supported Accelerators |
| :--- | :--- | :--- | :--- | :--- |
| **Glide 2.x** | `libglide2x.so` / `glide2x.dll` | **`2.43 (Up to 2.61 ABI)`** | 1996–1999 (*GLQuake*, *Tomb Raider II*, *Unreal*) | Voodoo Graphics (SST-1), Voodoo2 (CVG) |
| **Glide 3.x** | `libglide3x.so` / `glide3x.dll` | **`3.10`** | 1999–2002 (*Diablo II*, *Deus Ex*, *Serious Sam*) | Voodoo3 (H3), Voodoo4 / Voodoo5 (H5) |

---

## 2. Core Functional Differences: Glide 2.x vs. Glide 3.x

When engineering or debugging retro games against our wrapper, understand these fundamental architectural differences between the two specification suites:

### A. Context & Window Surface Initialization
* **Glide 2.x**: Relies on a highly monolithic initialization routine. Developers call `grGlideInit()` followed by `grSstWinOpen()` which accepts literal window handles and fixed integer display resolutions (e.g., `GR_RESOLUTION_800x600`).
* **Glide 3.x**: Completely separated hardware capabilities from context generation. It introduces **`grContextInit()`** and **`grWinOpen()`**, allowing applications to bind multiple independent off-screen execution contexts and dynamically resize window viewports.

### B. Vertex Struct Layouts
* **Glide 2.x**: Enforces a highly rigid, unalterable C struct layout (`GrVertex`). Every vertex submitted to drawing primitives (`grDrawTriangle`) must pass exactly identical memory allocations, regardless of whether the shader actually consumes all components (X, Y, Z, R, G, B, A, OOW, S0, T0, S1, T1).
* **Glide 3.x**: Introduced highly modernized **Vertex Layout Pointers (`grVertexLayout()`)**. Games specify exactly which floating-point components exist in their arbitrary custom vertex structures, drastically reducing host-to-device PCI bus memory bandwidth for simple 2D or UI drawing elements.

### C. Multitexturing & TMU Combiner Pipelines
* **Glide 2.x**: Multitexturing on Voodoo2 hardware is executed via highly explicit dual-pass configuration. Developers configure `grTexCombineFunction()` independently for `GR_TMU0` and `GR_TMU1`. If a user runs on a single-TMU card (Voodoo Graphics), the game engine must manually execute fallback multi-pass blending.
* **Glide 3.x**: Unified the TMU combiner formula into advanced register pipelines (`grTexCombineExt()`). It gracefully handles hardware fallback multi-passing internally and exposes dedicated extensions for modern non-power-of-two texture buffers (`grTexBufferExt`).

### D. Capabilities Polling
* **Glide 2.x**: Games execute massive nested hardware pointer queries via `grSstQueryHardware()` and `grSstQueryBoards()`, parsing raw board properties.
* **Glide 3.x**: Normalized parameter queries into a single highly elegant retrieval routine: **`grGet(pname, plength, params)`**. This maps perfectly to modern `glGetIntegerv` or Vulkan physical device property lookups.

---

## 3. How We Implement Backward Compatibility (Supersets ABI)

Within a specific specification tier (e.g., Glide 2), games were released across various sub-versions (e.g., `2.40`, `2.42`, `2.43`).

### Our Zero-Friction Emulation Strategy
Our wrapper implements an authoritative **Supersets ABI**. For example, our `libglide2x.so` binary exports every single C symbol ever introduced up to specification `2.61`. When an older `2.40` game launches, it binds perfectly to our exported function pointers without encountering missing symbol faults.

You can configure exactly what API version string our wrapper reports back to the game by setting `api_version` under `[hardware]` in `glide-ng.ini`:

```ini
[hardware]
card_model = Voodoo2
api_version = 2.40
```
This forces rigid retro games to follow their authentic retro software execution branches flawlessly!

---

## 4. The Historical Relationship of Minor Versions

To understand why certain version numbers appear next to each other, we must examine the physical hardware releases and the evolution of the software development kits (SDKs):

### A. Glide 2.1 vs. Glide 2.40: The Launch vs. The Formalized Spec
*   **Glide 2.1 (Late 1996)**: The raw, internal launch API developed specifically for the **SST-1 Voodoo Graphics** board. It was heavily geared towards MS-DOS, running in exclusive full-screen pass-through mode. The dynamic library was named `glide.dll` (Windows) or `libglide.so` (Linux).
*   **Glide 2.40 (Mid 1997)**: The first formalized, publicly documented API specification. It was released to support the new **Voodoo Rush** board (which integrated 2D and 3D on a single shared framebuffer). It introduced the windowed rendering paradigm, removed the old DOS-only monolithic functions, and renamed the dynamic library to **`glide2x.dll`** / **`libglide2x.so`**.
*   **The Relationship**: **Glide 2.1 is the launch state of Voodoo Graphics**, while **Glide 2.40 is the unified, windowed-ready SDK for both Voodoo Graphics and Voodoo Rush**.

### B. Glide 2.40 vs. 2.42 vs. 2.43: The Voodoo2 Evolution
*   **Glide 2.40 (1997)**: The baseline for single-TMU boards (Voodoo Graphics, Voodoo Rush). It had **no concept of a second TMU (`GR_TMU1`)**, no SLI synchronization, and no support for advanced chroma range registers.
*   **Glide 2.42 (Early 1998)**: Released alongside the **Voodoo2 (CVG)** hardware. It added multi-TMU single-pass multitexturing and dual-board Scan-Line Interleaving (SLI) support.
*   **Glide 2.43 (Mid 1998 - Gold Standard)**: The definitive, highly optimized release for Voodoo2. It introduced the dynamic extension loading system (**`grGetProcAddress`**), the Voodoo2-exclusive **Chroma-Keying Ranges (`grChromakeyRangeExt`)**, and vertex-alpha routed lighting control.
*   **Do We Care? YES!** 
    *   If a legacy game targets **2.40**, it is an older Voodoo Graphics title (e.g., *GLQuake*) and only uses exact-match chroma-keying.
    *   If a game targets **2.43**, it is a high-performance Voodoo2 title (e.g., *Unreal*, *Half-Life*) that dynamically queries extensions via `grGetProcAddress` and relies on range-match chroma-keying or multi-TMU combiners.

---

## 5. Master Map of All Glide Functions & Their Introducing Versions

This master map lists every single Glide core (`gr*`), utility (`gu*`), and custom wrapper extension function, detailing exactly when it was introduced, its hardware generation mapping, and its compatibility status.

| Function Name | API Type | Introduced In | Hardware Generation | Status / Architectural Notes |
| :--- | :---: | :---: | :---: | :--- |
| `grSstOpen` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grSstWinOpen`. |
| `grSstClose` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grSstWinClose`. |
| `grSstPassthruMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grSstControl`. |
| `grLfbBegin` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grLfbLock` (fixed 1024 stride). |
| `grLfbEnd` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grLfbUnlock`. |
| `grLfbOrigin` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**. |
| `grLfbWriteMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**. |
| `guFbWriteRegion` | Utility | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grLfbWriteRegion`. |
| `guFbReadRegion` | Utility | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in 2.2/2.3**; replaced by `grLfbReadRegion`. |
| `grDrawTriangle` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline drawing primitive; supported across all versions. |
| `grDrawLine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline drawing primitive; supported across all versions. |
| `grDrawPoint` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline drawing primitive; supported across all versions. |
| `grBufferClear` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline buffer clearing; supported across all versions. |
| `grBufferSwap` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline buffer presentation; supported across all versions. |
| `grRenderBuffer` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline render target selection; supported across all versions. |
| `grColorCombine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline color combiner unit; supported across all versions. |
| `grAlphaCombine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline alpha combiner unit; supported across all versions. |
| `grAlphaBlendFunction` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline alpha blending; supported across all versions. |
| `grAlphaTestFunction` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline alpha testing; supported across all versions. |
| `grAlphaTestReferenceValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline alpha testing; supported across all versions. |
| `grConstantColorValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline combiner helper; supported across all versions. |
| `grConstantColorValue4` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; float-based constant color. |
| `grChromakeyMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Exact chroma-key toggle; supported across all versions. |
| `grChromakeyValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Exact chroma-key color value; supported across all versions. |
| `grClipWindow` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline scissor clipping; supported across all versions. |
| `grCullMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline face culling; supported across all versions. |
| `grDepthBufferFunction` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline depth comparison; supported across all versions. |
| `grDepthBufferMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline depth mode; supported across all versions. |
| `grDepthMask` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline depth write mask; supported across all versions. |
| `grDepthBiasLevel` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline depth offset bias; supported across all versions. |
| `grDitherMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline dithering; supported across all versions. |
| `grColorMask` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline color write mask; supported across all versions. |
| `grDisableAllEffects` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Disables all pipeline effects; supported across all versions. |
| `grFogMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline fog configuration; supported across all versions. |
| `grFogColorValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline fog color; supported across all versions. |
| `grFogTable` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline 64-entry logarithmic table; supported across all versions. |
| `grTexCalcMemRequired` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture helper; supported across all versions. |
| `grTexTextureMemRequired` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture helper; supported across all versions. |
| `grTexMaxAddress` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture helper; supported across all versions. |
| `grTexMinAddress` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture helper; supported across all versions. |
| `grTexSource` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture binding; supported across all versions. |
| `grTexClampMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture wrapping; supported across all versions. |
| `grTexFilterMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture filtering; supported across all versions. |
| `grTexMipMapMode` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline mipmapping; supported across all versions. |
| `grTexCombine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture combiner; supported across all versions. |
| `grTexCombineFunction` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; alternate texture combiner. |
| `grTexDownloadMipMap` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture upload; supported across all versions. |
| `grTexDownloadMipMapLevel` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline texture upload; supported across all versions. |
| `grTexDownloadTable` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline paletted texture helper; supported across all versions. |
| `grTexNCCTable` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline compressed texture helper; supported across all versions. |
| `grTexDetailControl` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline detail-texture blending; supported across all versions. |
| `grTexLodBiasValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline LOD bias adjustment; supported across all versions. |
| `grAADrawPoint` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; anti-aliased point. |
| `grAADrawLine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; anti-aliased line. |
| `grAADrawTriangle` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; anti-aliased triangle. |
| `grAADrawPolygon` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; anti-aliased polygon. |
| `grAADrawPolygonVertexList` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; anti-aliased polygon list. |
| `grDrawPlanarPolygon` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; planar flat polygon. |
| `grDrawPlanarPolygonVertexList` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; planar flat polygon list. |
| `grDrawPolygon` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; Gouraud-shaded polygon. |
| `grDrawPolygonVertexList` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; Gouraud-shaded polygon list. |
| `grGetString` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline system query; supported across all versions. |
| `grCheckForRoom` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; queue stall check. |
| `grGammaCorrectionValue` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; hardware monitor gamma. |
| `grSplash` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline splash screen sequence; supported across all versions. |
| `grHints` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; hardware hints. |
| `grResetTriStats` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; resets stats counters. |
| `grTriStats` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; retrieves triangle stats. |
| `grSstIsBusy` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; checks if hardware is executing. |
| `grSstStatus` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; returns status register. |
| `grSstVideoLine` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; returns raster line. |
| `grSstVRetraceOn` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; checks vertical retrace. |
| `grSstScreenHeight` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; returns screen height. |
| `grSstScreenWidth` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; returns screen width. |
| `grSstQueryBoards` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; queries number of boards. |
| `grSstQueryHardware` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; queries board specifications. |
| `grSstSelect` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Selects active board; supported across all versions. |
| `grSstIdle` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; flushes and stalls pipeline. |
| `grErrorSetCallback` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline error callback; supported across all versions. |
| `grGlideInit` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline lifecycle init; supported across all versions. |
| `grGlideShutdown` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Baseline lifecycle shutdown; supported across all versions. |
| `grGlideGetVersion` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; gets wrapper version string. |
| `grGlideGetState` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Captures context state; supported across all versions. |
| `grGlideSetState` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | Restores context state; supported across all versions. |
| `grGlideShamelessPlug` | Core | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; renders 3dfx spinning logo. |
| *(All 28 `gu*` functions)* | Utility | **Glide 2.1** | SST-1 (Voodoo Graphics) | **Removed in Glide 3.x**; high-level helper functions (e.g. `gu3dfLoad`, `guFogGenerateExp`). |
| `grSstWinOpen` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Baseline windowed init; supported across all versions. |
| `grSstWinClose` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Baseline windowed shutdown; supported across all versions. |
| `grSstControl` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | **Removed in Glide 3.x**; register control interface. |
| `grLfbLock` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Locks VRAM with variable stride; supported across all versions. |
| `grLfbUnlock` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Unlocks VRAM; supported across all versions. |
| `grLfbWriteRegion` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Stride-aware CPU-to-VRAM copy; supported across all versions. |
| `grLfbReadRegion` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Stride-aware VRAM-to-CPU copy; supported across all versions. |
| `grLfbConstantAlpha` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Constant alpha for LFB writes; supported across all versions. |
| `grLfbConstantDepth` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Constant depth for LFB writes; supported across all versions. |
| `grLfbWriteColorFormat` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Pixel format for LFB writes; supported across all versions. |
| `grLfbWriteColorSwizzle` | Core | **Glide 2.2 / 2.3** | Voodoo Rush | Byte swizzling for LFB writes; supported across all versions. |
| `grTexMultibase` | Core | **Glide 2.42** | Voodoo2 (CVG) | Multi-base LOD texturing; supported across all versions. |
| `grTexMultibaseAddress` | Core | **Glide 2.42** | Voodoo2 (CVG) | Sets multi-base LOD addresses; supported across all versions. |
| `grTexDownloadMipMapLevelPartial` | Core | **Glide 2.42** | Voodoo2 (CVG) | Uploads partial mipmap slice; supported across all versions. |
| `grTexDownloadTablePartial` | Core | **Glide 2.42** | Voodoo2 (CVG) | Uploads partial LUT slice; supported across all versions. |
| `grGetProcAddress` | Core | **Glide 2.43** | Voodoo2 (CVG) | Dynamic extension resolution; supported across all versions. |
| `grAlphaControlsITRGBLighting` | Core | **Glide 2.43** | Voodoo2 (CVG) | Vertex-alpha routed lighting; supported across all versions. |
| `grCoordinateSpace` | Core | **Glide 2.43** | Voodoo2 (CVG) | Sets coordinate space mode; supported across all versions. |
| `grChromakeyRangeExt` | Core Extension | **Glide 2.43** | Voodoo2 (CVG) | **Wrapper Custom Extension**; inclusive/exclusive range chromakeying. Throws fault on SST-1! |
| `grSplashCb` | Core | **Glide 2.61** | Voodoo3 (Avenger) | **Removed in Glide 3.x**; callback-based splash screen. |
| `grSelectContext` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Context selection and multi-window routing. |
| `grQueryResolutions` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Clean, non-monolithic monitor resolution query. |
| `grGlideGetVertexLayout` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Configures custom Vertex Layout Pointers (VLP). |
| `grGlideSetVertexLayout` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Configures custom Vertex Layout Pointers (VLP). |
| `grDrawVertexArrayContiguous` | Core | **Glide 3.x** | Voodoo3 (Avenger) | High-performance contiguous vertex array submission. |
| `grStippleMode` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Configures hardware polygon stippling patterns. |
| `grStipplePattern` | Core | **Glide 3.x** | Voodoo3 (Avenger) | Configures hardware polygon stippling patterns. |

---

## 6. Incremental Version Diffs & Function Transitions

Here is the exact version-by-version breakdown of functions added, replaced, or removed throughout the historical timeline:

### A. Glide 2.1 (SST-1 Voodoo Graphics Launch Baseline)
*   **Core Feature Set**: Foundation of all rendering primitives (`grDrawTriangle`, `grDrawLine`, `grDrawPoint`), buffer swaps, state combiners (`grColorCombine`, `grAlphaCombine`), and basic table-based depth fogging.
*   **The Original Full-Screen Functions**:
    *   `grSstOpen` / `grSstClose` (Removed in 2.2/2.3)
    *   `grSstPassthruMode` (Removed in 2.2/2.3)
    *   `grLfbBegin` / `grLfbEnd` (Removed in 2.2/2.3; enforced a fixed 1024-pixel stride)
    *   `grLfbOrigin` / `grLfbWriteMode` (Removed in 2.2/2.3)
    *   `guFbWriteRegion` / `guFbReadRegion` (Removed in 2.2/2.3)

### B. Glide 2.2 / 2.3 (Voodoo Rush & Shared Desktops)
*   **The Windowed Paradigm Shift**: Breaking changes introduced to support rendering to windows and sharing the display framebuffer with a 2D accelerator.
*   **Functions REMOVED**:
    *   `grSstOpen`, `grSstPassthruMode`, `grLfbBegin`, `grLfbEnd`, `guFbWriteRegion`, `guFbReadRegion`, `grLfbOrigin`, `grLfbWriteMode` were **completely deprecated and removed**.
*   **Functions ADDED (The Replacements)**:
    *   `grSstWinOpen` (Replaced `grSstOpen`)
    *   `grSstWinClose` (Replaced the old hardware close mechanisms)
    *   `grSstControl` (Replaced `grSstPassthruMode`)
    *   `grLfbLock` / `grLfbUnlock` (Replaced `grLfbBegin`/`grLfbEnd`, returning variable strides in `GrLfbInfo_t`)
    *   `grLfbWriteRegion` / `grLfbReadRegion` (Replaced `guFbWriteRegion`/`guFbReadRegion`)
    *   `grLfbConstantAlpha`, `grLfbConstantDepth`, `grLfbWriteColorFormat`, `grLfbWriteColorSwizzle` (LFB pixel-pipeline configuration additions)

### C. Glide 2.42 (Voodoo2 Multitexturing)
*   **Multitexturing Expansion**: Kept the exact same header and ABI footprint as 2.3, but enabled support for the second TMU (`GR_TMU1`) in combiner functions.
*   **Functions ADDED**:
    *   `grTexMultibase` / `grTexMultibaseAddress` (Allows binding different texture sources per LOD level)
    *   `grTexDownloadMipMapLevelPartial` / `grTexDownloadTablePartial` (Optimized partial slices uploads)

### D. Glide 2.43 (Voodoo2 Gold Standard & Extension Architecture)
*   **Extension System**: Introduced to allow advanced games to dynamically query features without static linking.
*   **Functions ADDED**:
    *   `grGetProcAddress` (Dynamic extension loader)
    *   `grAlphaControlsITRGBLighting` (Routes vertex alpha to control lighting combiners)
    *   `grCoordinateSpace` (Switches between window-relative screen coordinates and normalized coordinates)
    *   `grChromakeyRangeExt` (The **Chroma-Keying Ranges** extension, resolved via `grGetProcAddress`).
        > [!IMPORTANT]
        > If called on Voodoo Graphics (SST-1) or Voodoo Rush hardware, the driver throws a runtime fault (`grChromaRange not supported.`).

### E. Glide 2.61 (Voodoo3 Legacy Interop)
*   **API Additions**:
    *   `grSplashCb` (Callback-based splash screen rendering to allow custom logos in legacy engines)

### F. Glide 3.x (The Modernized Redesign)
*   **The Paradigm Shift**: Massive, non-backward-compatible API clean-up. Eliminated legacy hardware-monitoring functions, moved the utility library out of the core, and introduced modern vertex buffering and context switching.
*   **Functions ADDED (7 functions)**:
    *   `grSelectContext`, `grQueryResolutions`, `grGlideGetVertexLayout` / `grGlideSetVertexLayout`, `grDrawVertexArrayContiguous`, `grStippleMode` / `grStipplePattern`.
*   **Functions REMOVED (51 functions!)**:
    *   **All `gu*` Utility Functions (28 functions)**: Core has **zero** utility functions; applications must handle texture parsing (`gu3dfLoad`) and math internally.
    *   **Anti-Aliasing Drawing (5 functions)**: `grAADrawPoint`, `grAADrawLine`, `grAADrawTriangle`, `grAADrawPolygon`, `grAADrawPolygonVertexList` (replaced by pipeline state settings).
    *   **Planar/Legacy Drawing (4 functions)**: `grDrawPlanarPolygon`, `grDrawPlanarPolygonVertexList`, `grDrawPolygon`, `grDrawPolygonVertexList`.
    *   **Hardware Status & Monitoring (10 functions)**: `grSstIsBusy`, `grSstStatus`, `grSstVideoLine`, `grSstVRetraceOn`, `grSstScreenHeight`, `grSstScreenWidth`, `grSstQueryBoards`, `grSstQueryHardware`, `grSstControl`, `grSstIdle` (replaced by unified `grGet` queries).
    *   **Performance & Rendering Counters (4 functions)**: `grSstPerfStats`, `grSstResetPerfStats`, `grResetTriStats`, `grTriStats`.
    *   **Legacy Helpers**: `grGlideShamelessPlug`, `grSplashCb`, `grTexCombineFunction`, `grDrawVertexArrayLinear`, `grGlideGetVersion`.

---

## 7. Custom Wrapper Extensions Mapping

The following table maps our custom implemented features and extensions to their correct architectural counterparts in the Glide timeline:

| Implemented Feature / Extension | Configuration Mode | Introducing Glide Version | Target HW Support | Wrapper Emu Architecture |
| :--- | :--- | :---: | :---: | :--- |
| **Exact Match Chroma-Keying** | `grChromakeyValue` | **Glide 2.1** | All Hardware | Handled via exact match fragment discard (`col == key`). |
| **Range Match Chroma-Keying** | `grChromakeyRangeExt` | **Glide 2.43** | Voodoo2+ Only | Handled via inclusive/exclusive color range check. **Throws runtime warning if board targets SST-1.** |
| **Table-Based Depth Fog** | `grFogMode(GR_FOG_WITH_TABLE)` | **Glide 2.1** | All Hardware | $O(1)$ unrolled logarithmic binary search using 6 comparisons in the fragment shader. |
| **Iterated Vertex Alpha Fog** | `grFogMode(GR_FOG_WITH_ITERATED_ALPHA)` | **Glide 2.1** | All Hardware | Bypasses color combiner; reads raw interpolated vertex alpha channel (`fragColor.a` / `iterA`). |
| **Subtractive Cavern Fog** | `grFogMode(GR_FOG_ADD2)` | **Glide 2.1** | All Hardware | Evaluates the unified branch-free hardware blending equation to perform subtractive cavern shading. |
| **Additive Glow Fog** | `grFogMode(GR_FOG_MULT2)` | **Glide 2.1** | All Hardware | Evaluates the unified branch-free hardware blending equation to perform additive glow shading. |



