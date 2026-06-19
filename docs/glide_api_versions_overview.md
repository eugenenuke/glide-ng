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

You can configure exactly what API version string our wrapper reports back to the game by setting the `"ApiVersionOverride"` field in `glide_config.json`:

```json
{
  "CardModel": "Voodoo2",
  "ApiVersionOverride": "2.40"
}
```
This forces rigid retro games to follow their authentic retro software execution branches flawlessly!

---

## 4. Baseline Glide 2.x API: Common Feature Set

The following **136 functions** constitute the baseline Glide 2.x specification. They are common across all hardware platforms (Voodoo Graphics, Voodoo Rush, and Voodoo2) under the unified `glide2x` ABI.

### A. Lifecycle & Global State (7 functions)
*   `grGlideInit(void)` — Initializes the Glide runtime environment.
*   `grGlideShutdown(void)` — Shuts down the runtime and terminates active backends.
*   `grGlideGetVersion(char version[80])` — Retrieves the wrapper's version string.
*   `grGlideGetState(GrState *state)` — Captures the active graphics context state.
*   `grGlideSetState(const GrState *state)` — Restores a saved graphics context state.
*   `grGlideShamelessPlug(const FxBool on)` — Renders the legacy 3dfx spinning logo.
*   `grGetProcAddress(char *procName)` — Dynamically resolves extension function pointers.

### B. Hardware & Window Management (11 functions)
*   `grSstSelect(int which_sst)` — Selects the active Voodoo board (always routes to board 0).
*   `grSstWinOpen(...)` — Creates the window and initializes Software/Vulkan/GLES contexts.
*   `grSstWinClose(void)` — Closes the window and destroys backend contexts.
*   `grSstQueryHardware(GrHwConfiguration *hw)` — Queries Voodoo board characteristics.
*   `grSstQueryBoards(GrHwConfiguration *hw)` — Queries the number of boards present.
*   `grSstOrigin(GrOriginLocation_t origin)` — Configures coordinate origin orientation.
*   `grSstIdle(void)` — Blocks until the graphics pipeline completes all pending commands.
*   `grSstControl(FxU32 code)` — Direct register-level control interface.
*   `grSstVideoLine(void)` — Returns the current raster line being drawn.
*   `grSstVRetraceOn(void)` — Checks if the screen is undergoing vertical retrace.
*   `grSstIsBusy(void)` — Checks if the graphics hardware is executing commands.

### C. Buffer Control & Presentation (4 functions)
*   `grBufferClear(GrColor_t c, GrAlpha_t a, FxU16 d)` — Clears color, alpha, and depth buffers.
*   `grBufferSwap(int swap_interval)` — Swaps front and back buffers.
*   `grRenderBuffer(GrBuffer_t buffer)` — Sets the target buffer for drawing.
*   `grBufferNumPending(void)` — Returns the number of swap commands queued.

### D. Geometry & Drawing Primitives (13 functions)
*   `grDrawPoint(const GrVertex *pt)` — Renders a single pixel point.
*   `grDrawLine(const GrVertex *v1, const GrVertex *v2)` — Renders a 2D line.
*   `grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)` — Renders a Gouraud-shaded triangle.
*   `grDrawPlanarPolygon(...)` — Renders a flat-colored convex polygon.
*   `grDrawPlanarPolygonVertexList(...)` — Renders a flat-colored polygon from a vertex list.
*   `grDrawPolygon(...)` — Renders a shaded polygon.
*   `grDrawPolygonVertexList(...)` — Renders a shaded polygon from a vertex list.
*   `grDrawVertexArray(...)` — Renders primitives from a vertex array.
*   `grDrawVertexArrayLinear(...)` — Renders primitives from a linear vertex array.
*   `grCullMode(GrCullMode_t mode)` — Configures polygon backface/frontface culling.
*   `grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height)` — Sets the rendering viewport.
*   `grCoordinateSpace(GrCoordinateSpaceMode_t mode)` — Sets coordinate space mode.
*   `grVertexLayout(...)` — Sets the vertex layout properties.

### E. Color & Alpha Combiners (10 functions)
*   `grColorCombine(fnc, factor, local, other, invert)` — Configures RGB combiner equations.
*   `grAlphaCombine(fnc, factor, local, other, invert)` — Configures Alpha combiner equations.
*   `grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df)` — Configures final blending factors.
*   `grAlphaTestFunction(GrCmpFnc_t function)` — Sets the comparison function for alpha testing.
*   `grAlphaTestReferenceValue(GrAlpha_t value)` — Sets the reference value for alpha testing.
*   `grConstantColorValue(GrColor_t value)` — Sets constant combiner color.
*   `grConstantColorValue4(float a, float r, float g, float b)` — Sets constant color via floats.
*   `grChromakeyMode(GrChromakeyMode_t mode)` — Enables/disables hardware chroma-keying.
*   `grChromakeyValue(GrColor_t value)` — Sets the global chroma-key reference color.
*   `grAlphaControlsITRGBLighting(FxBool enable)` — Routes vertex alpha to control lighting.

### F. Depth & Clipping (6 functions)
*   `grDepthBufferMode(GrDepthBufferMode_t mode)` — Enables Z-buffering or W-buffering.
*   `grDepthBufferFunction(GrCmpFnc_t function)` — Configures depth comparison.
*   `grDepthMask(FxBool mask)` — Enables/disables writing depth values.
*   `grDepthBiasLevel(FxI16 level)` — Sets depth bias offset.
*   `grClipWindow(minx, miny, maxx, maxy)` — Configures the scissor clipping box.
*   `grDepthRange(float y_near, float y_far)` — Configures depth range mapping.

### G. Fogging (3 functions)
*   `grFogMode(GrFogMode_t mode)` — Enables table-based or iterated alpha fogging.
*   `grFogColorValue(GrColor_t fogcolor)` — Sets the color of the fog.
*   `grFogTable(const GrFog_t ft[])` — Uploads the 64-entry logarithmic fog table.

### H. Texture Mapping (19 functions)
*   `grTexCalcMemRequired(...)` — Calculates memory required for a texture.
*   `grTexMaxAddress(GrChipID_t tmu)` — Returns maximum memory address for a TMU.
*   `grTexMinAddress(GrChipID_t tmu)` — Returns minimum memory address for a TMU.
*   `grTexSource(tmu, startAddress, evenOdd, info)` — Binds a texture to a TMU.
*   `grTexClampMode(tmu, s_clamp, t_clamp)` — Configures texture wrapping/clamping.
*   `grTexCombine(tmu, rgbFnc, rgbFac, alphaFnc, alphaFac, ...)` — Configures texture combining.
*   `grTexFilterMode(tmu, minfilter, magfilter)` — Configures minification/magnification filters.
*   `grTexDownloadMipMap(tmu, startAddress, evenOdd, info)` — Uploads a texture mipmap chain.
*   `grTexDownloadMipMapLevel(...)` — Uploads a single mipmap level.
*   `grTexDownloadMipMapLevelPartial(...)` — Uploads a partial slice of a mipmap level.
*   `grTexMipMapMode(tmu, mode, lodBlend)` — Configures mipmap blending and LOD selection.
*   `grTexTextureMemRequired(evenOdd, info)` — Calculates memory required for a texture info struct.
*   `grTexNCCTable(GrChipID_t tmu, GrNCCTable_t table)` — Downloads Narrow Channel Compression tables.
*   `grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)` — Legacy texture combiner config.
*   `grTexDetailControl(tmu, lod_bias, detail_scale, detail_max)` — Configures detail-texture blending.
*   `grTexLodBiasValue(GrChipID_t tmu, float bias)` — Adjusts texture LOD selection bias.
*   `grTexDownloadTable(tmu, type, data)` — Downloads custom color lookup tables (e.g., paletted formats).
*   `grTexDownloadTablePartial(...)` — Downloads a slice of a color lookup table.
*   `grTexMultibase(GrChipID_t tmu, FxBool enable)` — Enables multibase texturing.

### I. Linear Frame Buffer (LFB) Access (8 functions)
*   `grLfbLock(type, buffer, writeMode, origin, pipeline, info)` — Locks VRAM, returning writable pointer.
*   `grLfbUnlock(type, buffer)` — Unlocks VRAM, submitting pixel writes to the graphics engine.
*   `grLfbWriteRegion(dstBuf, dstX, dstY, srcFmt, width, height, stride, src)` — CPU-to-VRAM block copy.
*   `grLfbReadRegion(srcBuf, srcX, srcY, width, height, stride, dst)` — VRAM-to-CPU block copy.
*   `grLfbConstantAlpha(GrAlpha_t alpha)` — Sets constant alpha value for direct LFB writes.
*   `grLfbConstantDepth(FxU16 depth)` — Sets constant depth value for direct LFB writes.
*   `grLfbWriteColorSwizzle(swizzle, swap)` — Configures channel byte swizzling for LFB writes.
*   `grLfbWriteColorFormat(colorFormat)` — Configures color pixel formats for LFB writes.

### J. Anti-Aliasing, Statistics & System Queries (18 functions)
*   `grAADrawPoint(const GrVertex *pt)` — Draws an anti-aliased sub-pixel point.
*   `grAADrawLine(const GrVertex *v1, const GrVertex *v2)` — Draws an anti-aliased line.
*   `grAADrawTriangle(...)` — Draws a triangle with edge anti-aliasing.
*   `grAADrawPolygon(...)` — Draws an anti-aliased polygon.
*   `grAADrawPolygonVertexList(...)` — Draws an anti-aliased polygon from a vertex list.
*   `grGetString(FxU32 pname)` — Retrieves driver extensions and device info.
*   `grCheckForRoom(FxI32 n)` — Stalls until the command queue has room for `n` entries.
*   `grGammaCorrectionValue(float value)` — Configures hardware monitor gamma correction.
*   `grSplash(...)` — Renders the legacy 3dfx splash screen sequence.
*   `grHints(GrHint_t hintType, FxU32 hintMask)` — Provides rendering hints.
*   `grResetTriStats(void)` — Resets drawing counters.
*   `grTriStats(FxU32 *processed, FxU32 *drawn)` — Retrieves triangle drawing statistics.
*   `grErrorSetCallback(GrErrorCallbackFnc_t fnc)` — Registers a custom error handler callback.
*   `grSstStatus(void)` — Returns the raw status register bits of the Voodoo chip.
*   `grSstResetPerfStats(void)` — Resets hardware performance metrics.
*   `grSstPerfStats(GrSstPerfStats_t *stats)` — Gathers hardware performance metrics.
*   `grSstScreenWidth(void)` — Returns screen width.
*   `grSstScreenHeight(void)` — Returns screen height.

### K. Utility Library (`gu*` - 28 functions)
*   `guAADrawTriangleWithClip(...)` — Renders an anti-aliased triangle with viewport clipping.
*   `guDrawTriangleWithClip(...)` — Renders a Gouraud triangle with viewport clipping.
*   `guDrawPolygonVertexListWithClip(...)` — Renders a polygon with viewport clipping.
*   `guAlphaSource(GrAlphaSource_t mode)` — Configures alpha combining modes.
*   `guColorCombineFunction(GrColorCombineFnc_t fnc)` — Configures color combining modes.
*   `guTexCreateColorMipMap(void)` — Generates a color-bar test mipmap pattern.
*   `gu3dfGetInfo(...)` — Parses `.3df` texture file headers.
*   `gu3dfLoad(...)` — Parses and loads `.3df` texture pixels into RAM.
*   `guTexAllocateMemory(...)` — Allocates space in TMU texture memory.
*   `guTexChangeAttributes(...)` — Modifies properties of an existing texture.
*   `guTexCombineFunction(...)` — Configures texture combiners.
*   `guTexDownloadMipMap(...)` — Downloads an entire mipmap.
*   `guTexGetCurrentMipMap(...)` — Retrieves active texture properties for a TMU.
*   `guTexGetMipMapInfo(...)` — Extracts sizing and details from a texture info struct.
*   `guTexSource(...)` — Binds texture state to a TMU.
*   *   `guFogTableIndexToW(int i)` — Maps a fog table index back to planar depth $W$.
*   `guFogGenerateExp(...)` — Populates the table with an exponential curve ($1 - e^{-d \cdot W}$).
*   `guFogGenerateExp2(...)` — Populates the table with a double-exponential curve ($1 - e^{-(d \cdot W)^2}$).
*   `guFogGenerateLinear(...)` — Populates the table with a linear curve.
*   `guEncodeRLE16(void *dst, void *src, FxU32 w, FxU32 h)` — Compresses 16-bit color pixels to RLE.
*   `guEndianSwapWords(FxU32 value)` — Swaps byte order of 32-bit words.
*   `guEndianSwapBytes(FxU16 value)` — Swaps byte order of 16-bit words.
*   *(Plus minor viewport-mapping, matrix, and texture-tiling internal utility routines).*

---

## 5. Glide Version-by-Version Incremental Diffs

When retro games target our wrapper, they execute against a historical timeline of API additions, deprecations, and removals. Here is the exact incremental diff for every subsequent version:

### A. Glide 2.1 (1996 - SST-1 Voodoo Graphics Baseline)
*   **The Original Paradigm**: Hardcoded for full-screen pass-through accelerators.
*   **Legacy Primitives (Later Removed)**:
    *   `grSstOpen` / `grSstClose` — Formed the original device opening/closing context.
    *   `grSstPassthruMode` — Toggled between 2D VGA card output and Voodoo Graphics output.
    *   `grLfbBegin` / `grLfbEnd` — Demarcated LFB memory writes under a **fixed stride of 1024 pixels**.
    *   `guFbWriteRegion` / `guFbReadRegion` — Executed screen copies.
    *   `grLfbOrigin` / `grLfbWriteMode` — Set LFB coordinate mapping.

### B. Glide 2.2 / 2.3 (1997 - Voodoo Rush & The Windowed Paradigm Shift)
*   **The Windowed Revolution**: Introduced to support Voodoo Rush, which shared the framebuffer with a 2D accelerator on the desktop.
*   **API Removals (Breaking Changes)**:
    *   `grSstOpen`, `grSstPassthruMode`, `grLfbBegin`, `grLfbEnd`, `guFbWriteRegion`, `guFbReadRegion`, `grLfbOrigin`, `grLfbWriteMode` were **completely deprecated and removed**.
    *   `glide.dll` was renamed to **`glide2x.dll`**.
*   **API Additions (The Replacements)**:
    *   `grSstWinOpen` — Replaced `grSstOpen` to allow windowed and shared desktop context creation.
    *   `grSstWinClose` — Replaced the old hardware close mechanisms.
    *   `grSstControl` — Allowed direct control and notification of system/window events.
    *   `grLfbLock` / `grLfbUnlock` — Replaced `grLfbBegin`/`grLfbEnd`. Introduced **variable stride** (`strideInBytes`) returned dynamically in `GrLfbInfo_t` to handle arbitrary desktop resolutions.
    *   `grLfbWriteRegion` / `grLfbReadRegion` — Modernized rectangle BLT routines supporting stride and clipping.

### C. Glide 2.4 / 2.42 / 2.43 (1998 - Voodoo2 Era & Hardware Extensions)
*   **Baseline Preservation**: Kept the exact same header and ABI footprint as Glide 2.3 to ensure perfect binary backward compatibility.
*   **Hardware Expansion**: Enabled support for dual-TMU configurations (`GR_TMU1` passed to `grTexCombine` and `grTexSource`) and Scan-Line Interleaving (SLI).
*   **Dynamic Extension API**:
    *   `grGetProcAddress` — Added to allow games to dynamically query and bind hardware-specific extension pointers at runtime without static linking.
*   **Voodoo2-Exclusive Hardware Extensions (Exported Symbols)**:
    *   `grChromakeyRangeExt` (or internally `_grChromaRangeExt`) — Added to support **inclusive/exclusive Chroma-Keying Ranges** (Voodoo2 CVG hardware registers). 
        > [!IMPORTANT]
        > If called on Voodoo Graphics (SST-1) or Voodoo Rush hardware, the driver throws a runtime fault (`grChromaRange not supported.`).
    *   `grAlphaControlsITRGBLighting` — Added to route vertex alpha directly to color combiner lighting.

### D. Glide 2.5 / 2.6 / 2.61 (1999 - Voodoo3 Legacy Interop & Final Release)
*   **API Additions**:
    *   `grSplashCb` — Added callback-based splash screen rendering to allow legacy Glide 2 game engines to display custom logos.

---

### E. Glide 3.x (1999 - The Modernized Redesign)

Glide 3.x was a massive, non-backward-compatible API clean-up. It eliminated legacy hardware-monitoring functions, moved the utility library out of the core, and introduced modern vertex buffering and context switching.

#### 1. Functions ADDED in Glide 3.x (7 functions)
*   `grSelectContext` — Binds a specific, independent off-screen rendering context (multi-window support).
*   `grQueryResolutions` — Queries supported monitor resolutions in a clean, non-monolithic way.
*   `grGlideGetVertexLayout` / `grGlideSetVertexLayout` — Configures modern **Vertex Layout Pointers (VLP)**, allowing arbitrary custom vertex structures.
*   `grDrawVertexArrayContiguous` — Submits contiguous vertex arrays directly.
*   `grStippleMode` / `grStipplePattern` — Configures hardware polygon stippling patterns.

#### 2. Functions REMOVED in Glide 3.x (51 functions!)
*   **All `gu*` Utility Functions (28 functions)**:
    *   *Examples*: `gu3dfLoad`, `guFogGenerateExp`, `guTexSource`, `guTexAllocateMemory`. Glide 3.x core has **zero** utility functions; applications must handle asset parsing and loading themselves.
*   **Anti-Aliasing Drawing (5 functions)**:
    *   `grAADrawPoint`, `grAADrawLine`, `grAADrawTriangle`, `grAADrawPolygon`, `grAADrawPolygonVertexList`. (Replaced by general pipeline state settings).
*   **Planar/Legacy Drawing (4 functions)**:
    *   `grDrawPlanarPolygon`, `grDrawPlanarPolygonVertexList`, `grDrawPolygon`, `grDrawPolygonVertexList`.
*   **Hardware Status & Monitoring (10 functions)**:
    *   `grSstIsBusy`, `grSstStatus`, `grSstVideoLine`, `grSstVRetraceOn`, `grSstScreenHeight`, `grSstScreenWidth`, `grSstQueryBoards`, `grSstQueryHardware`, `grSstControl`, `grSstIdle`. (Replaced by a unified, elegant query routine: `grGet(pname, plength, params)`).
*   **Performance & Rendering Counters (4 functions)**:
    *   `grSstPerfStats`, `grSstResetPerfStats`, `grResetTriStats`, `grTriStats`.
*   **Legacy Helpers**:
    *   `grGlideShamelessPlug`, `grSplashCb`, `grTexCombineFunction`, `grDrawVertexArrayLinear`, `grGlideGetVersion`.

---

## 6. Chroma-Keying & Fogging: API Placement Map

When debugging or implementing Chroma-Keying and Fogging features in our wrapper, refer to this exact specification map to understand which execution paths are active:

### A. Chroma-Keying
1.  **Exact Match Chroma-Keying**:
    *   *APIs*: `grChromakeyMode` (enable/disable) and `grChromakeyValue` (exact color key value).
    *   *Ecosystem*: Part of the baseline **Glide 2.1 / 2.40 specification**. Supported on **all hardware** (SST-1, Voodoo Rush, Voodoo2).
    *   *Logic*: Fragments matching the exact key are discarded.
2.  **Range Match Chroma-Keying**:
    *   *APIs*: `grChromakeyRangeExt` (extension, resolved via `grGetProcAddress`).
    *   *Ecosystem*: Introduced in **Glide 2.42/2.43** specifically for the Voodoo2 (CVG) chipset.
    *   *Logic*: Discards fragments falling inside (inclusive) or outside (exclusive) a color range. **Throws a runtime warning if executed on SST-1 hardware.**

### B. Fogging
1.  **Table-Based Depth Fogging**:
    *   *APIs*: `grFogMode(GR_FOG_WITH_TABLE)` and `grFogTable(ft)` (uploads the 64-entry logarithmic table).
    *   *Ecosystem*: Baseline **Glide 2.1** (all hardware).
    *   *Wrapper Math*: Implemented via our $O(1)$ unrolled logarithmic binary search and unified branch-free blending equation.
2.  **Iterated Vertex Alpha Fogging**:
    *   *APIs*: `grFogMode(GR_FOG_WITH_ITERATED_ALPHA)`.
    *   *Ecosystem*: Baseline **Glide 2.1** (all hardware).
    *   *Wrapper Math*: Bypasses the color combiner stage and reads the raw interpolated vertex alpha channel directly (`fragColor.a` in Vulkan/GLES, `iterA` in Software).
3.  **Additive & Subtractive Blending**:
    *   *APIs*: Configured by mapping `grFogMode` to `GR_FOG_ADD2` (subtractive cavern fog) or `GR_FOG_MULT2` (additive glow fog).
    *   *Ecosystem*: Baseline **Glide 2.1** (all hardware).
    *   *Wrapper Implementation*: Fully supported across our Software, Vulkan, and GLES pipelines (whereas legacy wrappers like OpenGlide incorrectly map both modes to a subtractive black shadow).


