# API Coverage & Binary ABI

This document summarizes the Glide 2.11, Glide 2.x, and Glide 3.x API surfaces implemented by `glide-ng`, Win32/Linux ABI conventions, and how API-level wrapping compares to hardware register emulation.

---

## 1. Exported Library Surfaces

Linux shared libraries (`libglide.so`, `libglide2x.so`, `libglide3x.so`) and Win32 DLLs (`glide.dll`, `glide2x.dll`, `glide3x.dll`) cover all three major 3dfx Glide generations:

| Library | Target API | Exported Symbols | Canonical Win32 ABI | Notes |
| :--- | :--- | :---: | :--- | :--- |
| `libglide.so` / `glide.dll` | Glide 2.11 (SST-1) | **137** logical (**274** Win32 PE) | `__stdcall` `_grFoo@N` + undecorated `grFoo` aliases | Pre-2.3 SST-1 ABI (`grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`, `guFbReadRegion`/`guFbWriteRegion`, and 2 MB TMU0 `guTexAllocateMemory` budget enforcement). Defaults to `VoodooGraphics` (`2.11`). |
| `libglide2x.so` / `glide2x.dll` | Glide 2.30 – 2.61 | **152** | `__stdcall` `_grFoo@N` + undecorated `grFoo` aliases | Pure Glide 2.x ABI (`grGetString` and `grGetProcAddress` intentionally omitted, matching authentic 3dfx DLLs). Includes all `gr*`, `gu*`, `ConvertAndDownloadRle`, and DOSBox `setConfig` entry points. |
| `libglide3x.so` / `glide3x.dll` | Glide 3.00 – 3.10 | **129** | `__stdcall` `_grFoo@N` + undecorated `grFoo` aliases | Complete core Glide 3.x plus VSA-100 extensions (`PIXEXT`, `COMBINE`, `TEXFMT`, `TEXTUREBUFFER`, `TEXUMA`, `GETGAMMA`, `CHROMARANGE`, `TEXCHROMA`, `PALETTE6666`, `FOGCOORD`, `SURFACE`). |

### Verifying Exports After Frontend Changes

```bash
# Linux shared library symbols
nm -D --defined-only build/lib/libglide.so | awk '{print $3}' | grep -E '^(gr|gu|ConvertAndDownloadRle|setConfig)' | sort
nm -D --defined-only build/lib/libglide2x.so | awk '{print $3}' | grep -E '^(gr|gu|ConvertAndDownloadRle|setConfig)' | sort
nm -D --defined-only build/lib/libglide3x.so | awk '{print $3}' | grep -E '^(gr|gu|setConfig)' | sort

# Win32 PE DLL exports
i686-w64-mingw32-objdump -p build/win32/bin/glide.dll | sed -n '/Export Table/,/^$/p'
i686-w64-mingw32-objdump -p build/win32/bin/glide2x.dll | sed -n '/Export Table/,/^$/p'
i686-w64-mingw32-objdump -p build/win32/bin/glide3x.dll | sed -n '/Export Table/,/^$/p'
```

On MinGW Win32 builds, `src/CMakeLists.txt` also generates canonical import libraries (`libglide2x_3dfx.dll.a` and `libglide3x_3dfx.dll.a`) mapping MinGW `__stdcall` references (`Func@N`) directly to `_Func@N`, so Win32 tools compiled with MinGW link identically to authentic 1990s MSVC/Watcom executables and run drop-in against `glide-ng` as well as real 3dfx drivers or third-party wrappers.

---

## 2. Implemented Feature Coverage

| Feature Area | Glide 2.x | Glide 3.x | Details |
| :--- | :---: | :---: | :--- |
| **Windowing, Buffers & Pacing** | Full | Full | `grSstWinOpen`/`WinClose`, `grSstWinOpenExt` (VSA-100 pixel formats & 2×/4× AA), double/triple buffering, `grBufferClear`/`ClearExt`, `grBufferSwap`, `grClipWindow`, `grSstOrigin`, `grViewport`. |
| **Primitives & Vertex Pipeline** | Full | Full | Points, lines, triangles, antialiased primitives (`grAADraw*`), polygons, Glide 3 `grVertexLayout`, `grDrawVertexArray` / `Contiguous`, `grCoordinateSpace` (`WINDOW` & `CLIP`). |
| **Color / Alpha / Texture Combiners** | Full | Full | Full legacy `grColorCombine` / `grAlphaCombine` / `grTexCombine` plus VSA-100 `COMBINE` (`gr*CombineExt`, `grConstantColorValueExt`, `grAlphaBlendFunctionExt` with `ADD`/`SUB`/`REVSUB`/`MIN`/`MAX`). |
| **Multi-Texturing & Mipmapping** | Full | Full | Dual-TMU (`GR_TMU0` + `GR_TMU1`), point/bilinear/trilinear filtering, `grTexLodBiasValue`, `grTexDetailControl`, `grTexMultibase`/`Address`, partial mipmap/table downloads. |
| **Texture Formats** | Full | Full | All classic 8-bit/16-bit formats (`RGB_332`, `YIQ_422`, `A_8`, `I_8`, `AI_44`, `P_8`, `RGB_565`, `ARGB_1555`, `ARGB_4444`, `AI_88`, `AP_88`, `AYIQ_8422`, `ARGB_8332`) plus VSA-100 `TEXFMT` (`ARGB_8888`, `RGB_888`, `YUYV`/`UYVY`/`AYUV`, `FXT1`, `DXT1`–`DXT5`), up to 2048×2048 on Voodoo4/5. |
| **Depth, Fog, Stencil & Stipple** | Full | Full | Z-buffer, W-buffer (16-bit floating-point precision semantics), depth bias, 64-entry Table Fog (`WITH_FOG_TABLE`, `ADD2`, `MULT2`, `WITH_ITERATED_Z`, `FOGCOORD`), VSA-100 8-bit hardware Stencil (`PIXEXT`), 32×32 pattern Stipple, 2×2 / 4×4 color dithering. |
| **Chromakeying & Alpha Test** | Full | Full | Exact chromakey (`grChromakeyMode`/`Value`), range chromakey (`grChromakeyRangeExt` / `grChromaRangeExt`), per-TMU texture chromakey (`grTexChromaModeExt`/`RangeExt`), alpha test. |
| **Linear Framebuffer (LFB)** | Full | Full | `grLfbLock`/`Unlock`, `grLfbReadRegion`/`WriteRegion`, 16-bit & 32-bit formats, pixel-pipeline bypass and write-pipeline modes, constant alpha/depth/stencil, origin flipping. |
| **VSA-100 Render-to-Texture & AA** | — | Full | `TEXTUREBUFFER` (`grTextureBufferExt`, `grTextureAuxBufferExt`, `grAuxBufferExt`), `TEXUMA`, Spatial AA (2×/4×) & T-Buffer (`grTBufferWriteMaskExt`, `GR_AA_ORDERED` / `GR_AA_MULTISAMPLE`). |
| **State Serialization & Gamma** | Full | Full | Symmetric `grGlideGetState` / `grGlideSetState` (bounded to authentic opaque `GrState` ABI size), `grGammaCorrectionValue`, `grLoadGammaTable`, `grGetGammaTableExt`, `grSplash` / `grSplashCb`, `GR_SHAMELESS_PLUG`. |
| **Utilities & Queries** | Full | Full | Glide 2 `guTex*`, `guFog*`, `gu3df*`, `guColorCombineFunction`, `guAlphaSource`, `guEncodeRLE16`, `grSstQueryHardware`/`Boards`, `grGlideGetVersion`; Glide 3 `grGet`, `grGetString`, `grGetProcAddress`, `grQueryResolutions`, `grReset`. |

### Authentic State ABI Safety

Both `grGlideGetState` and `grGlideSetState` serialize into the caller's fixed-size `GrState` buffer (`GR_GLIDE_STATE_PAD_SIZE`). Any new frontend pipeline state must be added symmetrically to `SharedFrontendState`, `Reset()`, `grGlideGetState`, `grGlideSetState`, and `ApplySharedStateToBackend()` while verifying `static_assert(sizeof(SerializedState) <= sizeof(GrState))`.

---

## 3. API Translation vs. Hardware Register Emulation

`glide-ng` operates at the **C API translation layer**, in contrast to hardware-register emulators (such as DOSBox's `voodoo_emu.cpp`):

| Dimension | API Wrapper (`glide-ng`) | Register-Level Emulator (`voodoo_emu.cpp`) |
| :--- | :--- | :--- |
| **Interception point** | C API exports (`grDrawTriangle`, `grTexSource`, `grLfbWriteRegion`). | PCI memory-mapped I/O (MMIO) registers (`fbzMode`, `textureMode`, `triangle` trigger). |
| **Execution target** | Native host GPU (Vulkan / OpenGL ES / WGL) or multi-threaded SIMD CPU rasterizer. | Guest CPU executes guest driver → MMIO writes → software ASIC simulation. |
| **DOS integration** | Uses DOSBox's `GLIDE2X.OVL` I/O-port pass-through (`glide.cpp`), forwarding calls directly to host `libglide2x.so`. | Uses a real DOS 3dfx `GLIDE2X.OVL` writing directly to emulated PCI registers. |
| **Throughput** | Near-native GPU speed (thousands of FPS at 640×480) or hundreds of FPS on AVX2/AVX-512 Software. | CPU-bound on emulated register writes and single-threaded scanline rasterization. |

Note that when running DOS titles through DOSBox Glide pass-through, the usable Glide 2.x surface is the intersection of calls forwarded by DOSBox's bridge (`0..129` in `glide.cpp`) and `libglide2x.so`.
