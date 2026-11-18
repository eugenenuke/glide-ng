# 3dfx Glide Versions, Hardware Families & Card-to-Version Reference

This document is an authoritative reference of real 3dfx Glide 2.x and Glide 3.x releases, their supported 3dfx accelerator families, ABI/export deltas across versions, and how **`glide-ng`** maps card profiles to default API versions and warn-only compatibility checks.

Every version entry below was verified directly against authentic 3dfx Win32 DLLs (`glide.dll`, `glide2x.dll`, `glide3x.dll`), DOS/32 CauseWay/Linear Executable overlays (`glide2x.ovl`), and the official 3dfx open-source Glide 2.x/3.x SDKs (`sst1`, `cvg`, `h3`, `h5`).

---

## 1. Quick Summary: Emulated Cards & Default Versions in `glide-ng`

Each emulated card profile defaults to the **maximum authentic Glide 2.x and Glide 3.x version** ever shipped by 3dfx for that silicon family:

| Card Model (`GLIDE_NG_CARD_MODEL`) | Silicon / Family | Default Glide 2.x | Default Glide 3.x | Authentic Glide 2.x Range | Authentic Glide 3.x Range | Default FB / TMUs | Max Tex Size (Glide 3) | Max Resolution Tier |
| :--- | :--- | :---: | :---: | :--- | :--- | :--- | :---: | :---: |
| **`voodoo1`** (`sst1`, `voodoographics`) | SST-1 | **`2.48`** | **`3.04`** | `2.11`, `2.30`, `2.40`, `2.43`, `2.46`, `2.48` | `3.00`, `3.02`, `3.04` | 4 MB FB, 1×2 MB TMU | 256×256 | 800×600 |
| **`rush`** (`sst96`, `voodoorush`) | SST-96 | **`2.48`** | **`3.04`** | `2.40`, `2.42`, `2.43`, `2.45`, `2.46`, `2.48` | `3.00`, `3.02`, `3.04` | 6 MB FB, 1×2 MB TMU | 256×256 | 800×600 |
| **`voodoo2`** *(default)* (`cvg`) | CVG | **`2.56`** | **`3.03`** | `2.50`, `2.51`, `2.53`, `2.54`, `2.56` | `3.01`, `3.03` | 16 MB FB, 2×16 MB TMU | 256×256 | 1024×768 |
| **`banshee`** (`voodoobanshee`) | H3 (1-TMU) | **`2.61`** | **`3.10`** | `2.60`, `2.61` | `3.10` | 16 MB UMA, 1 TMU | 256×256 | 1600×1200 |
| **`voodoo3`** (`h3`, `avenger`) | H3 / Avenger | **`2.61`** | **`3.10`** | `2.60`, `2.61` | `3.10` | 16 MB UMA, 2 TMUs | 256×256 | 1600×1200 |
| **`voodoo4`** (`v4`, `h4`, `vsa100`) | VSA-100 (1-chip) | **`2.61`** | **`3.10`** | `2.61` | `3.10` | 32 MB UMA, 2 TMUs, 2× AA | **2048×2048** | 2048×1536 |
| **`voodoo5`** (`v5`, `h5`) | VSA-100 (2-chip) | **`2.61`** | **`3.10`** | `2.61` | `3.10` | 64 MB UMA, 2 TMUs, 4×/8× AA | **2048×2048** | 2048×1536 |

### Why `Voodoo2` Is the Universal Default Card
`Voodoo2` (`GLIDE_NG_CARD_MODEL=voodoo2`, Glide 2 `2.56` / Glide 3 `3.03`, 16 MB FB, 2 TMUs) is intentionally retained as the default profile for maximum DOS + Win32 compatibility:
- **Universal Glide 2.x recognition**: Virtually every 1996–2000 Glide 2 game recognizes `GR_SSTTYPE_Voodoo2` with 2 TMUs (single-pass multitexturing + lightmapping), whereas `Voodoo1`/`Rush`/`Banshee` only have 1 TMU and force slower multi-pass fallback paths.
- **No VSA-100-only surprises**: Defaulting to `Voodoo4`/`Voodoo5` would advertise `PIXEXT`/`COMBINE`/`TEXFMT`/2048×2048 textures and H3 `fbiRev` (`0x11002`), which can confuse older Glide 2 games or activate heavy multi-sample T-Buffer paths unintentionally.
- **Explicit opt-in for Voodoo4/5 demos**: Titles specifically designed for VSA-100 Spatial AA, T-Buffer, 32-bit color, or FXT1/DXT textures select `GLIDE_NG_CARD_MODEL=voodoo4` or `GLIDE_NG_CARD_MODEL=voodoo5`.

### Version Formatting & Warn-Only Enforcement Policy
- **Bare 3-digit version strings**: `grGlideGetVersion()` (Glide 2.x) and `grGetString(GR_VERSION)` (Glide 3.x) report clean, authentic bare strings (e.g., `"2.48"`, `"2.56"`, `"2.61"`, `"3.03"`, `"3.04"`, `"3.10"`) with **no** `" (glide-ng)"` suffix.
- **Pure Glide 2.x ABI**: Zero authentic 3dfx Glide 2.x DLLs (`2.11`–`2.61`) or OVLs (`2.30`–`2.61`) ever exported `grGetString` or `grGetProcAddress` — both were introduced in Glide 3.x. `libglide2x.so` and `glide2x.dll` match this authentic ABI by omitting both exports.
- **Warn-only card-to-version matrix**: Explicitly selecting an ahistorical pair (e.g. `GLIDE_NG_CARD_MODEL=voodoo1 GLIDE_NG_API_VERSION=2.61` or `voodoo2` + `3.10`) emits a `[WARN]` diagnostic to `stderr` and `glide-ng.log`, while **still honoring** the user's explicit override (never blocking or silently clamping).

---

## 2. Glide 2.x Complete Version History (`glide.dll`, `glide2x.dll`, `glide2x.ovl`)

> **Important ABI Distinction — `glide.dll` (2.11) vs. `glide2x.dll` (2.30–2.61)**:
> Glide 2.11 (`glide.dll`) predates the Glide 2.2/2.3 windowed LFB refactor and uses an incompatible binary ABI (`grSstOpen`, `grSstPassthruMode`, `grLfbBegin`/`End`/`BypassMode`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`, `guFbReadRegion`/`WriteRegion` — no `grSstWinOpen` or `grLfbLock`). Every release from `2.30` onward (`glide2x.dll` / `glide2x.ovl`) shares the unified `grSstWinOpen` + `grLfbLock` ABI.

| Version | Full Build String (Examples) | Target Hardware | Win32 DLL Exports | DOS OVL Exports | Key Changes vs. Previous Version |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **2.11** | `2.1.1` (`GLIDE.DLL`) | Voodoo Graphics (SST-1) | 131 (`glide.dll`) | — | Original SST-1 launch API. Exclusive fullscreen `grSstOpen`, `grSstPassthruMode`, fixed-1024-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`/`BypassMode`, `guFbReadRegion`/`guFbWriteRegion`. Incompatible with `glide2x.dll`. |
| **2.30** | `2.30` (`GLIDE2X.OVL`) | Voodoo Graphics (SST-1) | — | 152 | First `glide2x` ABI! Replaced `grSstOpen`/`grLfbBegin` with `grSstWinOpen`, `grSstWinClose`, `grSstControl`, `grLfbLock`/`Unlock`, `grLfbReadRegion`/`WriteRegion`, `grLfbConstantAlpha`/`Depth`, `grLfbWriteColorFormat`/`Swizzle`, `grCheckForRoom`, `ConvertAndDownloadRle`, `guEncodeRLE16`. |
| **2.42** | `2.42` | Voodoo Rush (SST-96) | 153 | 162 | First Rush-focused `glide2x.dll` build (`SST96` shared 2D/3D framebuffer windowed quirks, `GR_SSTTYPE_SST96`). Added `grTexMultibase`, `grTexMultibaseAddress`, `grTexDownloadMipMapLevelPartial`, `grTexDownloadTablePartial`, `grResetTriStats`/`grTriStats`, `grSstPerfStats`. |
| **2.43** | `2.43` | Voodoo Graphics (SST-1) | 144 | 153 | SST-1 synchronization with the 2.42 `glide2x` ABI; trimmed internal PCI/PIO debug helpers (`pciOpenEx`, `pio*`) from Win32 exports. |
| **2.45** | `2.45` | Voodoo Rush (SST-96) | 148 | *(CW-packed)* | Rush maintenance update; re-exposed `guFbReadRegion`/`guFbWriteRegion` compatibility helpers for older 2.11-ported apps. |
| **2.46** | `2.46` | Voodoo Graphics / Rush | 151 | 160 | Added `pciLinearRangeSetPermission` and `pciSetPassThroughBase` for Pentium II/K6 write-combining (USWC) acceleration; stabilized SST-1/Rush dual-board handling. |
| **2.48** | `2.48.00.0455` | Voodoo Graphics / Rush **(Final)** | 162 | 171 | Final SST-1 & Voodoo Rush Glide 2.x driver. Added AMD K6 MTRR / write-combining helpers (`pciOpenEx`, `pio*`) and final bugfixes. |
| **2.50** | `2.50` | Voodoo2 (CVG) | 151 | 160 | First Voodoo2 (CVG) release (`GR_SSTTYPE_Voodoo2`, 2 TMUs, SLI support, `SSTV2_*` env vars, 800×600 single / 1024×768 SLI). |
| **2.51** | `2.51` | Voodoo2 (CVG) | 151 | 160 | Added `SSTV2_DUALHEAD` support and Voodoo2 stability fixes. |
| **2.53** | `2.53` | Voodoo2 (CVG) | 151 | 165 | Voodoo2 optimization release (improved command FIFO / texture download paths). |
| **2.54** | `2.54` | Voodoo2 (CVG) | 151 | 169 | Added optimized texture download helpers in DOS OVL (`_grTexDownload_*`). |
| **2.56** | `2.56.00.0459` (DLL) / `2.56.00.0458` (OVL) | Voodoo2 (CVG) **(Final)** | 163 | 178 | Final Voodoo2 Glide 2.x driver. Added `SSTV2_NOREMAP` and `grCommandTransportMakeRoom` (OVL) + final CVG FIFO fixes. |
| **2.60** | `2.60.00.0350` | Voodoo Banshee / Voodoo3 (H3) | 132 | 172 | First unified H3 Glide 2.x driver. Stripped internal `pci*`/`pio*` symbols from Win32 DLL exports (clean 132-symbol table). Reports `GR_SSTTYPE_VOODOO` with `fbiRev = 0x1002` (Banshee) or `0x11002` (Voodoo3) for legacy app compatibility. Added `SSTH3_*` gamma/SGRAM variables. |
| **2.61** | `2.61.00.0438` (DLL) / `2.61.00.0518` (OVL) | Banshee / Voodoo3 / Voodoo4 / Voodoo5 **(Final)** | 132 | 178 | Final 3dfx Glide 2.x release across all H3 & VSA-100 boards. Kept identical 132 Win32 DLL exports; added `SSTH3_ALPHADITHERMODE`, `SSTH3_FBRAM`, and extended OVL texture download helpers. |

> **Hardware Identity in Glide 2.x (`grSstQueryHardware`)**:
> Because `GrSstType` only defined `GR_SSTTYPE_VOODOO` (0), `GR_SSTTYPE_SST96` (1), `GR_SSTTYPE_AT3D` (2), and `GR_SSTTYPE_Voodoo2` (3), real 3dfx `2.60`/`2.61` drivers (`h3/glide/src/gpci.c`) reported `GR_SSTTYPE_VOODOO` with **`fbiRev = 0x1002`** for Voodoo Banshee (1 TMU) and **`fbiRev = 0x11002`** for Voodoo3/Voodoo4/Voodoo5 (2 TMUs). `glide-ng` reproduces this exact encoding, and `glide2_info` decodes it cleanly.

---

## 3. Glide 3.x Complete Version History (`glide3x.dll`)

Glide 3.x introduced `grGet`, `grGetString`, `grGetProcAddress`, `grQueryResolutions`, `grReset`, programmable `grVertexLayout`, vertex arrays (`grDrawVertexArray` / `grDrawVertexArrayContiguous`), stippling, and removed all 28 `gu*` utility functions and legacy `grSstQueryHardware`/`grGlideGetVersion` entry points.

| Version | Full Build String (Examples) | Target Hardware | Win32 DLL Exports | Authentic `GR_HARDWARE` | Authentic `GR_EXTENSION` Tokens & Changes vs. Previous Version |
| :--- | :--- | :--- | :---: | :--- | :--- |
| **3.00** | `3.00` | Voodoo Graphics / Rush | ~106 | `"Voodoo Graphics"` / `"Voodoo Rush"` | Initial SST-1/Rush Glide 3.x release. Exports `grGet`, `grGetString`, `grGetProcAddress`, `grQueryResolutions`, `grReset`, `grVertexLayout`, plus legacy transitional helpers (`grSstIsBusy`, `grSstPerfStats`, `grSstVRetraceOn`, `grSstVideoLine`, `guEncodeRLE16`). `GR_EXTENSION`: `FOGALPHA` (`CHROMARANGE`/`FOGCOORD` in extended builds). |
| **3.01** | `3.01` | Voodoo2 (CVG) | ~106 | `"Voodoo2"` | First Voodoo2 Glide 3.x release. `GR_EXTENSION`: `CHROMARANGE TEXCHROMA TEXMIRROR PALETTE6666 FOGCOORD RESOLUTION`. Adds 2-TMU & SLI support. |
| **3.02** | `3.02` | Voodoo Graphics / Rush | ~106 | `"Voodoo Graphics"` / `"Voodoo Rush"` | SST-1/Rush Glide 3.x maintenance update (mirrors 2.46 PCI/MTRR fixes). |
| **3.03** | `3.03.00.0670` | Voodoo2 (CVG) **(Final)** | ~107 | `"Voodoo2"` | Final Voodoo2 Glide 3.x driver. Replaced `RESOLUTION` extension token with `COMMAND_TRANSPORT`, added `grSstConfigPipeline` and `SSTV2_NOREMAP`. |
| **3.04** | `3.04.00.0455` | Voodoo Graphics / Rush **(Final)** | ~106 | `"Voodoo Graphics"` / `"Voodoo Rush"` | Final SST-1 & Voodoo Rush Glide 3.x driver (paired with Glide 2.48). |
| **3.10** | `3.10.00.0674` | Banshee / Voodoo3 / Voodoo4 / Voodoo5 **(Final)** | 96 | `"Voodoo Banshee"`, `"Voodoo3"`, `"Voodoo4"`, `"Voodoo5"` | Final unified H3/VSA-100 Glide 3.x driver. Stripped internal `pci*`/`pio*` and obsolete `grSstIsBusy`/`VRetraceOn`/`VideoLine`, added **`grSetNumPendingBuffers`** (`@4`) and **`grSstConfigPipeline`**. Banshee/V3 expose `GETGAMMA`, `TEXUMA`, `TEXTUREBUFFER`, `TEXMIRROR`, `PALETTE6666`, `COMMAND_TRANSPORT`, `SURFACE`; **Voodoo4/Voodoo5 (VSA-100)** additionally expose **`PIXEXT`** (Spatial AA, T-Buffer, 8-bit hardware stencil, per-channel `grColorMaskExt`), **`COMBINE`** (`gr*CombineExt`, `grAlphaBlendFunctionExt`, `grConstantColorValueExt`), **`TEXFMT`** (`ARGB_8888`, `RGB_888`, `YUYV`/`UYVY`/`AYUV`, `FXT1`, `DXT1`–`DXT5`), and **2048×2048** max texture size. |

---

## 4. Authentic Hardware Capabilities (`grGet` & `grQueryResolutions`)

Verified directly against 3dfx `sst1`, `cvg`, `h3`, and `h5` `diget.c`:

- **`GR_MAX_TEXTURE_SIZE`**:
  - `256` on Voodoo Graphics, Voodoo Rush, Voodoo2, Voodoo Banshee, **and Voodoo3** (`h3/diget.c`, `h5/diget.c`: `IS_NAPALM` only).
  - **`2048`** exclusively on **Voodoo4** and **Voodoo5** (VSA-100).
- **`GR_BITS_RGBA`** (`plength = 16`, writes 4 × `FxI32` `{R, G, B, A}`):
  - `{5, 6, 5, 0}` for `GR_PIXFMT_RGB_565` / `GR_PIXFMT_AA_*_RGB_565` (default for `grSstWinOpen`).
  - `{5, 5, 5, 1}` for `GR_PIXFMT_ARGB_1555` / `GR_PIXFMT_AA_*_ARGB_1555`.
  - `{8, 8, 8, 8}` for `GR_PIXFMT_ARGB_8888` / `GR_PIXFMT_AA_*_ARGB_8888` (Voodoo4/5 `grSstWinOpenExt`).
- **`GR_BITS_DEPTH`**: `16` in 16-bit color modes, `32` (24-bit Z + 8-bit stencil) in VSA-100 32-bit `ARGB_8888` modes.
- **`grQueryResolutions(resTemplate, output)`**:
  - Returns **byte size** (`matchCount * sizeof(GrResolution)` = `matchCount * 16`), matching the standard 3dfx two-call allocation idiom (`size = grQueryResolutions(&t, NULL); buf = malloc(size); grQueryResolutions(&t, buf);`).
  - Filters resolutions by both **hardware family tier** and **available framebuffer VRAM** (`w * h * 2 * (numColorBuffers + numAuxBuffers) <= availFbBytes`, with 2× FB on Voodoo2 SLI and 2 MB minimum texture reservation on UMA H3/H5 boards).
- **`GR_SUPPORTS_PASSTHRU`**: `FXTRUE` on add-in 3D-only boards (`Voodoo1`, `Voodoo2`), `FXFALSE` on integrated 2D/3D boards (`Rush`, `Banshee`, `Voodoo3`, `Voodoo4`, `Voodoo5`).
- **`GR_TEXTURE_ALIGN`**: `8` bytes on SST-1/Rush/CVG; `16` bytes on Banshee/Voodoo3/Voodoo4/Voodoo5.

---

## 5. Note on `glide.dll` (Glide 2.11)

Early late-1996 / early-1997 Win32/DOS SST-1 titles linked directly against **`glide.dll`** (`2.11`) instead of `glide2x.dll`. Because `glide.dll` uses a completely different pre-2.3 window/LFB API (`grSstOpen`, `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`, fixed 1024-pixel LFB stride, `grSstPassthruMode`), it cannot be satisfied simply by renaming `glide2x.dll`. Supporting `glide.dll` would require a small dedicated `src/glide211/` frontend shim translating those ~12 legacy entry points onto `GlideWrapperCore` (deferred as a future optional enhancement if Glide 2.11 titles are targeted).
