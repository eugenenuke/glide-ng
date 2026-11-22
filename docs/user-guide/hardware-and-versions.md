# Hardware Profiles & Glide Versions

`glide-ng` emulates seven authentic 3dfx accelerator profiles spanning **Voodoo Graphics (SST-1)** through **Voodoo5 (VSA-100)**, along with historically accurate Glide 2.x (`2.11`–`2.61`) and Glide 3.x (`3.00`–`3.10`) version reporting, capability queries, and feature gating.

---

## 1. Emulated Hardware Profiles

Each profile defaults to the maximum authentic Glide 2.x and Glide 3.x version shipped by 3dfx for that silicon family:

| `card_model` (`GLIDE_NG_CARD_MODEL`) | Silicon / Family | Default Glide 2.x | Default Glide 3.x | Authentic Glide 2.x Range | Authentic Glide 3.x Range | Default FB / TMUs | Max Texture Size (Glide 3) | Max Resolution Tier |
| :--- | :--- | :---: | :---: | :--- | :--- | :--- | :---: | :---: |
| **`voodoo1`** (`sst1`, `voodoographics`) | SST-1 | **`2.48`** | **`3.04`** | `2.11`, `2.30`, `2.40`, `2.43`, `2.46`, `2.48` | `3.00`, `3.02`, `3.04` | 4 MB FB, 1×2 MB TMU | 256×256 | 800×600 |
| **`rush`** (`sst96`, `voodoorush`) | SST-96 | **`2.48`** | **`3.04`** | `2.40`, `2.42`, `2.43`, `2.45`, `2.46`, `2.48` | `3.00`, `3.02`, `3.04` | 6 MB FB, 1×2 MB TMU | 256×256 | 800×600 |
| **`voodoo2`** *(default)* (`cvg`) | CVG | **`2.56`** | **`3.03`** | `2.50`, `2.51`, `2.53`, `2.54`, `2.56` | `3.01`, `3.03` | 16 MB FB, 2×16 MB TMU | 256×256 | 1024×768 |
| **`banshee`** (`voodoobanshee`) | H3 (1-TMU) | **`2.61`** | **`3.10`** | `2.60`, `2.61` | `3.10` | 16 MB UMA, 1 TMU | 256×256 | 1600×1200 |
| **`voodoo3`** (`h3`, `avenger`) | H3 / Avenger | **`2.61`** | **`3.10`** | `2.60`, `2.61` | `3.10` | 16 MB UMA, 2 TMUs | 256×256 | 1600×1200 |
| **`voodoo4`** (`v4`, `h4`, `vsa100`) | VSA-100 (1-chip) | **`2.61`** | **`3.10`** | `2.61` | `3.10` | 32 MB UMA, 2 TMUs, 2× AA | **2048×2048** | 2048×1536 |
| **`voodoo5`** (`v5`, `h5`) | VSA-100 (2-chip) | **`2.61`** | **`3.10`** | `2.61` | `3.10` | 64 MB UMA, 2 TMUs, 4×/8× AA | **2048×2048** | 2048×1536 |

### Why `Voodoo2` Is the Default Profile

- **Broadest classic recognition**: Most 1996–2000 Glide 2.x titles and 3dfx MiniGL drivers (*GLQuake*, *Quake II*, *Half-Life*) expect `GR_SSTTYPE_VOODOO` from `grSstQueryHardware()`. Matching authentic 3dfx CVG `glide2x.dll` (2.50–2.56), the `Voodoo2` profile reports `GR_SSTTYPE_VOODOO` with **`fbiRev = 0x102`** (dual-TMU, `0x82` single-TMU) unless **`FX_GLIDE_REPORT_REAL_HW=1`** is set, which returns the raw `GR_SSTTYPE_Voodoo2` (`3`) enum. With 2 TMUs enabled, titles take fast single-pass multitexturing/lightmapping paths, whereas single-TMU boards (`VoodooGraphics`, `VoodooRush`, `VoodooBanshee`) trigger multi-pass fallbacks.
- **No VSA-100 surprises for older titles**: Defaulting to `Voodoo4`/`Voodoo5` advertises `PIXEXT`/`COMBINE`/`TEXFMT`, 2048×2048 textures, and H3 `fbiRev = 0x11002`, which can trigger unintended multi-sample T-Buffer or 32-bit paths in older titles.
- **Explicit opt-in for VSA-100 features**: Titles requiring Voodoo4/5 Spatial AA, T-Buffer effects, hardware Stencil, FXT1/DXT textures, or 2048×2048 textures select `GLIDE_NG_CARD_MODEL=voodoo4` or `GLIDE_NG_CARD_MODEL=voodoo5`.

### Version Reporting & Warn-Only Card/Version Policy

- **Bare authentic version strings**: `grGlideGetVersion()` (Glide 2.x) and `grGetString(GR_VERSION)` (Glide 3.x) return clean strings (`"2.48"`, `"2.56"`, `"2.61"`, `"3.03"`, `"3.04"`, `"3.10"`) without wrapper suffixes that could break strict game version parsers.
- **Pure Glide 2.x ABI**: Authentic 3dfx Glide 2.x binaries never exported `grGetString` or `grGetProcAddress` (both were introduced in Glide 3.x); `libglide2x.so` and `glide2x.dll` omit both exports to match.
- **Warn-only compatibility check**: Requesting an ahistorical pair (such as `GLIDE_NG_CARD_MODEL=voodoo1 GLIDE_NG_API_VERSION=2.61`) logs a `[WARN]` diagnostic to `stderr` and `glide-ng.log` while still honoring the explicit override.

---

## 2. Glide 2.x Release Reference

| Version | Target Hardware | Key Capabilities & Milestones |
| :--- | :--- | :--- |
| **2.11** | Voodoo Graphics (SST-1) | Original `glide.dll` launch ABI (`grSstOpen`, `grSstPassthruMode`, fixed-1024-stride `grLfbBegin`/`End`, `guFbReadRegion`/`WriteRegion`). Incompatible binary ABI with `glide2x.dll`. |
| **2.30** | Voodoo Graphics (SST-1) | First `glide2x` ABI (`grSstWinOpen`/`WinClose`, `grSstControl`, `grLfbLock`/`Unlock`, `grLfbReadRegion`/`WriteRegion`, `ConvertAndDownloadRle`, `guEncodeRLE16`). |
| **2.42 – 2.48** | SST-1 & Voodoo Rush (SST-96) | Added `grTexMultibase`, `grTexMultibaseAddress`, partial mipmap/table downloads, `grTriStats`, `grSstPerfStats`. `2.48` is the final SST-1/Rush Glide 2.x release. |
| **2.50 – 2.56** | Voodoo2 (CVG) | Introduced dual-TMU single-pass multitexturing, SLI, `SSTV2_*` variables, and 1024×768 SLI support. Reports `GR_SSTTYPE_VOODOO` with `fbiRev = 0x102` (dual-TMU) / `0x82` (single-TMU) by default (`GR_SSTTYPE_Voodoo2` when `FX_GLIDE_REPORT_REAL_HW=1` is set). `2.56` is the final Voodoo2 Glide 2.x release. |
| **2.60 – 2.61** | Banshee, Voodoo3, Voodoo4, Voodoo5 | Unified H3/VSA-100 Glide 2.x driver. Reports `GR_SSTTYPE_VOODOO` with `fbiRev = 0x1002` (Banshee, 1 TMU) or `fbiRev = 0x11002` (Voodoo3/4/5, 2 TMUs) for legacy compatibility. |

---

## 3. Glide 3.x Release Reference

Glide 3.x introduced `grGet`, `grGetString`, `grGetProcAddress`, `grQueryResolutions`, `grReset`, programmable `grVertexLayout`, vertex arrays (`grDrawVertexArray` / `grDrawVertexArrayContiguous`), and stippling while removing the `gu*` utility library and `grSstQueryHardware`/`grGlideGetVersion`.

| Version | Target Hardware | `GR_HARDWARE` | Advertised `GR_EXTENSION` Tokens & Additions |
| :--- | :--- | :--- | :--- |
| **3.00 / 3.02 / 3.04** | Voodoo Graphics / Rush | `"Voodoo Graphics"` / `"Voodoo Rush"` | Core Glide 3.x + `FOGALPHA`, `CHROMARANGE`, `FOGCOORD`. `3.04` is final for SST-1/Rush. |
| **3.01 / 3.03** | Voodoo2 (CVG) | `"Voodoo2"` | Adds 2-TMU & SLI support; `CHROMARANGE`, `TEXCHROMA`, `TEXMIRROR`, `PALETTE6666`, `FOGCOORD`, `COMMAND_TRANSPORT`. `3.03` is final for Voodoo2. |
| **3.10** | Banshee / Voodoo3 | `"Voodoo Banshee"` / `"Voodoo3"` | Adds `GETGAMMA`, `TEXUMA`, `TEXTUREBUFFER`, `SURFACE`, `grSetNumPendingBuffers`, `grSstConfigPipeline`. |
| **3.10** | Voodoo4 / Voodoo5 (VSA-100) | `"Voodoo4"` / `"Voodoo5"` | Additionally exposes **`PIXEXT`** (Spatial AA, T-Buffer, 8-bit hardware Stencil, per-channel `grColorMaskExt`), **`COMBINE`** (`gr*CombineExt`, `grAlphaBlendFunctionExt`, `grConstantColorValueExt`), **`TEXFMT`** (`ARGB_8888`, `RGB_888`, `YUYV`/`UYVY`/`AYUV`, `FXT1`, `DXT1`–`DXT5`), and **2048×2048** max textures. |

---

## 4. Authentic Hardware Capabilities & Feature Gating

### `grGet` & `grQueryResolutions` Rules

- **`GR_MAX_TEXTURE_SIZE`**: `256` on `VoodooGraphics`, `VoodooRush`, `Voodoo2`, `VoodooBanshee`, and `Voodoo3`; **`2048`** exclusively on `Voodoo4` and `Voodoo5`.
- **`GR_BITS_RGBA`**: `{5, 6, 5, 0}` for RGB565 modes, `{5, 5, 5, 1}` for ARGB1555 modes, and `{8, 8, 8, 8}` for VSA-100 32-bit `ARGB_8888` modes opened via `grSstWinOpenExt`.
- **`GR_BITS_DEPTH`**: `16` in 16-bit color modes; `32` (24-bit depth + 8-bit stencil) in VSA-100 32-bit `ARGB_8888` modes.
- **`GR_SUPPORTS_PASSTHRU`**: `FXTRUE` on add-in 3D-only boards (`VoodooGraphics`, `Voodoo2`); `FXFALSE` on integrated 2D/3D boards (`VoodooRush`, `VoodooBanshee`, `Voodoo3`, `Voodoo4`, `Voodoo5`).
- **`GR_TEXTURE_ALIGN`**: `8` bytes on SST-1/Rush/CVG; `16` bytes on Banshee/Voodoo3/Voodoo4/Voodoo5.
- **`grQueryResolutions(resTemplate, output)`**: Returns byte size (`matchCount * 16`) and filters modes by both card resolution tier and available framebuffer VRAM (`w * h * 2 * (colorBufs + auxBufs) <= availFbBytes`, with 2× FB for Voodoo2 SLI and a 2 MB minimum texture reservation on UMA boards).

### Feature Gating Behavior

Extension and generation-specific entry points are gated by the active card model and API version:

| Feature Group | Minimum Card / Version | Gated Entry Points |
| :--- | :--- | :--- |
| Voodoo2 Multitexturing & Partial Uploads | `Voodoo2` (`2.42+`) | `grTexMultibase`, `grTexMultibaseAddress`, `grTexDownloadMipMapLevelPartial`, `grTexDownloadTablePartial` |
| Chroma Range & Extended Lighting | `Voodoo2` (`2.43+`) | `grChromakeyRangeExt`, `grTexChromaModeExt`, `grTexChromaRangeExt`, `grAlphaControlsITRGBLighting`, `grCoordinateSpace` |
| H3 / Banshee Extensions | `VoodooBanshee` (`2.61` / `3.10`) | `grSplashCb`, `grGetGammaTableExt`, `grTextureBufferExt`, `grTextureAuxBufferExt`, `grAuxBufferExt` |
| VSA-100 (`PIXEXT` & `COMBINE`) | `Voodoo4` (`3.10`) | `grSstWinOpenExt`, `grTBufferWriteMaskExt`, `grColorMaskExt`, `grStencil*Ext`, `grBufferClearExt`, `grLfbConstantStencilExt`, `grAlphaBlendFunctionExt`, `gr*CombineExt`, `grConstantColorValueExt` |

When an unsupported extension is queried via Glide 3.x `grGetProcAddress`, `glide-ng` returns `NULL`. When a gated symbol is called directly while outside the active card profile, it logs a `[WARN]` diagnostic and returns cleanly without crashing.

### Custom Memory & Super-Hardware Overrides

Any card profile's reported VRAM and TMU count can be raised or lowered independently via INI or environment variables (for example, testing a 4 MB SST-1 budget or unlocking 64 MB of texture memory on Voodoo2):

```bash
GLIDE_NG_CARD_MODEL=voodoo2 GLIDE_NG_FBI_MEM_MB=32 GLIDE_NG_TMU_COUNT=2 GLIDE_NG_TMU_MEM_MB=64 \
  ./build_release/bin/glide2_info -v --probe
```

---

## 5. `glide.dll` / `libglide.so` (Glide 2.11)

Early 1996–1997 SST-1 titles (such as *CyberGladiators*) link against **`glide.dll`** (`2.11`), which predates the `glide2x` windowed/LFB ABI (`grSstOpen` instead of `grSstWinOpen`, `grSstPassthruMode` for VGA/SST-1 switching, and `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr` with a fixed 1024-pixel stride instead of `grLfbLock`).

Because the binary ABI differs, renaming `glide2x.dll` to `glide.dll` does not work. Instead, `glide-ng` builds a dedicated **`glide.dll`** (Win32) and **`libglide.so`** (Linux) frontend target that implements the full Glide 2.11 pre-2.3 SST-1 ABI and automatically defaults to **`VoodooGraphics`** (4 MB FB, 1× 2 MB TMU) with API version **`2.11`** unless overridden via `glide-ng.ini` or `GLIDE_NG_*` environment variables.
