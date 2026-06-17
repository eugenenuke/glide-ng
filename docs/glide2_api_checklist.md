# Glide2 API Coverage Checklist

This document serves as the authoritative coverage map for the **glide-ng** wrapper. It lists all standard Glide2 core (`gr*`) and utility (`gu*`) functions, their hardware/emulation descriptions, and their current implementation status.

---

## API Coverage Dashboard

| API Category | Implemented | Total | Coverage % |
| :--- | :---: | :---: | :---: |
| **Core Glide2 (`gr*`)** | 45 | 99 | 45.5% |
| **Utility Library (`gu*`)** | 6 | 16 | 37.5% |
| **Unified Glide2 Specification** | **51** | **115** | **44.3%** |

---

## 1. Core Glide2 API (`gr*`)

### 1.1. Initialization & Lifecycle
- [x] `grGlideInit(void)` — Initializes the Glide runtime environment, loads configuration files, and sets up logger.
- [x] `grGlideShutdown(void)` — Shuts down the Glide runtime and terminates active rendering backends.
- [x] `grGlideGetVersion(char version[80])` — Retrieves the wrapper's version string.
- [x] `grGlideGetState(GrState *state)` — Captures and serializes the entire active graphics context state.
- [x] `grGlideSetState(const GrState *state)` — Restores the serialized graphics context state.
- [ ] `grGlideShamelessPlug(const FxBool on)` — Renders the legacy 3dfx spinning logo splash overlay.

### 1.2. SST Hardware & Window Configuration
- [x] `grSstWinOpen(...)` — Creates the rendering window and initializes Software, Vulkan, and GLES contexts.
- [x] `grSstWinClose(void)` — Closes the rendering window and destroys backend contexts.
- [x] `grSstSelect(int which_sst)` — Selects the active Voodoo board (always routes to board 0 in wrapper).
- [x] `grSstQueryHardware(GrHwConfiguration *hw)` — Queries Voodoo board characteristics (TMUs, memory size).
- [x] `grSstQueryBoards(GrHwConfiguration *hw)` — Queries the number of physical Voodoo boards present.
- [x] `grSstOrigin(GrOriginLocation_t origin)` — Configures coordinate origin orientation (upper-left vs. lower-left).
- [x] `grSstIdle(void)` — Blocks until the graphics pipeline completes all pending commands.
- [ ] `grSstControl(FxU32 code)` — Direct register level control interface for hardware-specific actions.
- [ ] `grSstVideoLine(void)` — Returns the current raster line being drawn to the monitor.
- [ ] `grSstVRetraceOn(void)` — Checks if the screen is undergoing vertical retrace.
- [ ] `grSstIsBusy(void)` — Checks if the graphics hardware is currently executing draw commands.
- [ ] `grSstScreenHeight(void)` — Returns the screen height in pixels.
- [ ] `grSstScreenWidth(void)` — Returns the screen width in pixels.
- [ ] `grSstStatus(void)` — Returns the raw status register bits of the Voodoo chip.
- [ ] `grSstPerfStats(GrSstPerfStats_t *stats)` — Gathers hardware execution performance metrics.
- [ ] `grSstResetPerfStats(void)` — Resets hardware performance metrics.

### 1.3. Buffers & Presentation
- [x] `grBufferClear(GrColor_t c, GrAlpha_t a, FxU16 d)` — Clears color, alpha, and depth buffers (GPU-native in Vulkan/GLES).
- [x] `grBufferSwap(int swap_interval)` — Swaps front and back buffers, triggering presentation and overlay compositing.
- [x] `grRenderBuffer(GrBuffer_t buffer)` — Sets the target buffer for subsequent draw commands (front, back, depth).
- [ ] `grBufferNumPending(void)` — Returns the number of swap commands queued but not yet executed.

### 1.4. Drawing & Geometry Primitives
- [x] `grDrawPoint(const GrVertex *pt)` — Renders a single pixel point (GPU-native in Vulkan/GLES).
- [x] `grDrawLine(const GrVertex *v1, const GrVertex *v2)` — Renders a 2D line with flat/Gouraud shading (GPU-native).
- [x] `grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)` — Renders a Gouraud-shaded 2D triangle (GPU-native).
- [ ] `grDrawPlanarPolygon(int n, const int *i, const GrVertex *v)` — Renders a flat-colored convex polygon.
- [ ] `grDrawPlanarPolygonVertexList(int n, const GrVertex *v)` — Renders a flat-colored polygon from a vertex list.
- [ ] `grDrawPolygon(int n, const int *i, const GrVertex *v)` — Renders a shaded polygon.
- [ ] `grDrawPolygonVertexList(int n, const GrVertex *v)` — Renders a shaded polygon from a vertex list.
- [ ] `grResetTriStats(void)` — Resets drawing counters.
- [ ] `grTriStats(FxU32 *processed, FxU32 *drawn)` — Retrieves triangle drawing statistics.

### 1.5. State Combiners & Blending
- [x] `grColorCombine(fnc, factor, local, other, invert)` — Configures the RGB color combine unit equations.
- [x] `grAlphaCombine(fnc, factor, local, other, invert)` — Configures the Alpha combine unit equations.
- [x] `grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df)` — Configures the final blending factors.
- [x] `grAlphaTestFunction(GrCmpFnc_t function)` — Sets the comparison function for alpha testing.
- [x] `grAlphaTestReferenceValue(GrAlpha_t value)` — Sets the reference value for alpha testing.
- [x] `grConstantColorValue(GrColor_t value)` — Sets the constant color value utilized in the combiner stage.
- [ ] `grConstantColorValue4(float a, float r, float g, float b)` — Sets constant color via floating-point channels.
- [ ] `grAlphaControlsITRGBLighting(FxBool enable)` — Routes vertex alpha to control color combiner lighting equations.
- [ ] `grColorMask(FxBool rgb, FxBool a)` — Enables/disables color and alpha channel write masks.
- [ ] `grDisableAllEffects(void)` — Disables blending, fog, depth, and chromakeying instantly.
- [ ] `grDitherMode(GrDitherMode_t mode)` — Configures color dithering modes for 16-bit color depths.

### 1.6. Depth Buffering & Clipping
- [x] `grDepthBufferMode(GrDepthBufferMode_t mode)` — Enables/disables Z-buffering, W-buffering, or depth write.
- [x] `grDepthBufferFunction(GrCmpFnc_t function)` — Configures the depth comparison function (e.g. less, equal, greater).
- [x] `grDepthMask(FxBool mask)` — Enables/disables writing depth values to the depth buffer.
- [x] `grDepthBiasLevel(FxI16 level)` — Sets the hardware depth bias offset (emulated in vertex shaders).
- [x] `grClipWindow(minx, miny, maxx, maxy)` — Configures the scissor clipping box (GPU-native).

### 1.7. Fogging Emulation
- [x] `grFogMode(GrFogMode_t mode)` — Enables table-based or iterated alpha fogging.
- [x] `grFogColorValue(GrColor_t fogcolor)` — Sets the color of the fog.
- [x] `grFogTable(const GrFog_t ft[])` — Uploads the 64-entry logarithmic fog table.

### 1.8. Texture Mapping Control
- [x] `grTexCalcMemRequired(...)` — Calculates the memory size required for a given texture configuration.
- [x] `grTexMaxAddress(GrChipID_t tmu)` — Returns the maximum hardware memory address for a TMU.
- [x] `grTexSource(tmu, startAddress, evenOdd, info)` — Binds a texture start address and layout to a TMU.
- [x] `grTexClampMode(tmu, s_clamp, t_clamp)` — Configures texture wrapping/clamping modes.
- [x] `grTexCombine(tmu, rgbFnc, rgbFac, alphaFnc, alphaFac, ...)` — Configures texture combining equations.
- [x] `grTexFilterMode(tmu, minfilter, magfilter)` — Configures minification/magnification filters.
- [x] `grTexDownloadMipMap(tmu, startAddress, evenOdd, info)` — Uploads a texture mipmap chain to TMU memory.
- [x] `grTexDownloadMipMapLevel(...)` — Uploads a single mipmap level to TMU memory.
- [x] `grTexMipMapMode(tmu, mode, lodBlend)` — Configures mipmap blending and LOD selection modes.
- [ ] `grTexTextureMemRequired(evenOdd, info)` — Calculates memory size required for a texture info struct.
- [x] `grTexMinAddress(GrChipID_t tmu)` — Returns the minimum memory address for a TMU.
- [ ] `grTexNCCTable(GrChipID_t tmu, GrNCCTable_t table)` — Downloads Narrow Channel Compression (NCC) tables.
- [ ] `grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)` — Alternate legacy texture combiner config.
- [ ] `grTexDetailControl(tmu, lod_bias, detail_scale, detail_max)` — Configures detail-texture blending.
- [ ] `grTexLodBiasValue(GrChipID_t tmu, float bias)` — Adjusts texture LOD selection bias.
- [ ] `grTexDownloadMipMapLevelPartial(...)` — Uploads a partial slice of a mipmap level.
- [ ] `ConvertAndDownloadRle(...)` — Decompresses and uploads Run-Length Encoded (RLE) textures.
- [x] `grTexDownloadTable(tmu, type, data)` — Downloads custom color lookup tables (e.g., for paletted formats).
- [x] `grTexDownloadTablePartial(...)` — Downloads a slice of a color lookup table.
- [ ] `grTexMultibase(GrChipID_t tmu, FxBool enable)` — Enables multibase texturing (different textures per LOD).
- [ ] `grTexMultibaseAddress(...)` — Sets the base address for multibase texturing.

### 1.9. Linear Frame Buffer (LFB) Access
- [x] `grLfbLock(type, buffer, writeMode, origin, pipeline, info)` — Locks VRAM, returning a host-writable pointer.
- [x] `grLfbUnlock(type, buffer)` — Unlocks VRAM, submitting direct CPU pixel writes to the graphics engine.
- [x] `grLfbWriteRegion(dstBuf, dstX, dstY, srcFmt, width, height, stride, src)` — Direct CPU-to-VRAM block copy.
- [x] `grLfbReadRegion(srcBuf, srcX, srcY, width, height, stride, dst)` — Direct VRAM-to-CPU block copy.
- [ ] `grLfbConstantAlpha(GrAlpha_t alpha)` — Sets constant alpha value for direct LFB writes.
- [ ] `grLfbConstantDepth(FxU16 depth)` — Sets constant depth value for direct LFB writes.
- [ ] `grLfbWriteColorSwizzle(swizzle, swap)` — Configures channel byte swizzling for LFB writes.
- [ ] `grLfbWriteColorFormat(colorFormat)` — Configures color pixel formats for LFB writes.

### 1.10. Anti-Aliasing & Special Effects
- [ ] `grAADrawPoint(const GrVertex *pt)` — Draws an anti-aliased sub-pixel point.
- [ ] `grAADrawLine(const GrVertex *v1, const GrVertex *v2)` — Draws an anti-aliased line.
- [ ] `grAADrawTriangle(a, b, c, ab_aa, bc_aa, ca_aa)` — Draws a triangle with edge anti-aliasing.
- [ ] `grAADrawPolygon(int n, const int *i, const GrVertex *v)` — Draws an anti-aliased polygon.
- [ ] `grAADrawPolygonVertexList(int n, const GrVertex *v)` — Draws an anti-aliased polygon from a vertex list.
- [x] `grGetString(FxU32 pname)` — Retrieves graphics driver extensions and device info.
- [ ] `grCheckForRoom(FxI32 n)` — Stalls until the command queue has room for `n` entries.
- [ ] `grGammaCorrectionValue(float value)` — Configures hardware monitor gamma correction.
- [ ] `grSplash(...)` — Renders a 3dfx splash screen sequence.
- [ ] `grHints(GrHint_t hintType, FxU32 hintMask)` — Provides rendering hints (e.g. sub-pixel precision).

---

## 2. Glide Utility Library (`gu*`)

### 2.1. Geometry & Clipping
- [ ] `guAADrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c)` — Renders an anti-aliased triangle with viewport clipping.
- [ ] `guDrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c)` — Renders a Gouraud triangle with viewport clipping.
- [ ] `guDrawPolygonVertexListWithClip(int n, const GrVertex *v)` — Renders a polygon with viewport clipping.

### 2.2. Combiners & Texturing
- [ ] `guAlphaSource(GrAlphaSource_t mode)` — High-level helper to configure alpha combining modes.
- [ ] `guColorCombineFunction(GrColorCombineFnc_t fnc)` — High-level helper to configure color combining modes.
- [ ] `guTexCreateColorMipMap(void)` — Utility to generate a color-bar test mipmap pattern.
- [x] `gu3dfGetInfo(const char *filename, Gu3dfInfo *info)` — Parses `.3df` texture file headers.
- [x] `gu3dfLoad(const char *filename, Gu3dfInfo *data)` — Parses and loads `.3df` texture pixels into RAM.

### 2.3. Fog Tables & Math
- [x] `guFogTableIndexToW(int i)` — Maps a fog table index logarithmic octave back to planar depth $W$.
- [x] `guFogGenerateExp(GrFog_t fogtable[], float density)` — Populates the table with an exponential curve ($1 - e^{-d \cdot W}$).
- [x] `guFogGenerateExp2(GrFog_t fogtable[], float density)` — Populates the table with a double-exponential curve ($1 - e^{-(d \cdot W)^2}$).
- [x] `guFogGenerateLinear(GrFog_t fogtable[], float nearZ, float farZ)` — Populates the table with a linear curve.

### 2.4. Endian Swapping & Encoding
- [ ] `guEncodeRLE16(void *dst, void *src, FxU32 w, FxU32 h)` — Compresses 16-bit color pixels to RLE.
- [ ] `guEndianSwapWords(FxU32 value)` — Swaps byte order of 32-bit words.
- [ ] `guEndianSwapBytes(FxU16 value)` — Swaps byte order of 16-bit words.
