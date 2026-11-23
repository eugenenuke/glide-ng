---
name: 3dfx-expert
description: Canonical domain authority on 3dfx Voodoo hardware architectures, Glide 2.11/2.x/3.x APIs, environment variables, and DOSBox Glide ABI extensions.
---

# Workspace Skill: 3dfx Voodoo Architecture & Glide API Expert

Activate this skill when analyzing, modifying, or auditing Glide frontend entry points inside [src/glide211/](../../../src/glide211/) (`glide.dll` / `libglide.so`), [src/glide2/](../../../src/glide2/) (`glide2x.dll` / `libglide2x.so`), [src/glide3/](../../../src/glide3/) (`glide3x.dll` / `libglide3x.so`), or verifying 3dfx Voodoo hardware specifications.

## 1. 3dfx Voodoo Hardware Specifications & Limits

- **Texture Dimensions & MIP-mapping**:
  - **Voodoo Graphics (SST-1), Rush (SST-96), Voodoo2 (CVG):** Maximum texture size **256×256 pixels**; aspect ratios `1×1`, `1×2`, `1×4`, `1×8`, `2×1`, `4×1`, `8×1`.
  - **Banshee / Voodoo3 (H3) / Voodoo4 / Voodoo5 (VSA-100):** Up to **2048×2048** textures via `PIXEXT` / `COMBINE` / `TEXFMT` / `TEXUMA` / `FOGCOORD` / `TEXMIRROR` / `PALETTE6666` / `CHROMARANGE` / `TEXCHROMA` / `TEXTUREBUFFER` (check extension gating per card profile in `grGetProcAddress`).
  - Glide 3.x uses signed `log2` integers for LOD bounds (`largeLod = 8` → 256, `11` → 2048, `0` → 1), whereas Glide 2.x uses canonical `GR_LOD_*` index constants.
- **Color Precision**:
  - Classic pipeline operates in **16-bit color depth** (RGB565 / ARGB1555 / ARGB4444) with authentic 2×2 / 4×4 dithering (`FX_GLIDE_NO_DITHER_SUB=1` bypasses sub-dither quantization). VSA-100 (`Voodoo4`/`Voodoo5`) adds 32-bit RGBA/ARGB8888 extensions.
- **Hardware Identity (`grSstQueryHardware`)**:
  - Voodoo2 (CVG) legacy callers expect `GR_SSTTYPE_VOODOO` with `fbiRev` encoding (`0x102` dual-TMU / `0x82` single-TMU); Banshee uses `0x1002`; Voodoo3/VSA-100 use `0x11002`.
  - **`FX_GLIDE_REPORT_REAL_HW`** follows the authentic 3dfx strict **`!= NULL`** rule: *any* set env-var value (including `0`) enables it (`unset` completely to disable).
- **Vertex Snap & Precision Guards (`src/core/VertexLayout.h`)**:
  - Authentic `12.4` fixed-point snap offsets: `1 << 19` (`524288.0f`, Glide 2.11 unsigned) and `3 << 18` (`786432.0f`, Glide 2.x/3.x signed).
  - Always pass `oow` / `tmu_oow` through `VertexLayout::SanitizePositiveOow()` (`f >= FLT_MIN`), preventing subnormal floats from flushing to `0.0` under GPU FTZ and discarding lines/HUD digits.
- **Environment Variable Registry**:
  - Single source of truth lives in [include/glide_ng/env_vars.h](../../../include/glide_ng/env_vars.h) (**32** `GLIDE_NG_*` wrapper variables + **48** authentic 3dfx `FX_GLIDE_*`/`SST_*`/`SSTV2_*`/`SSTH3_*` variables). Always register new env vars there first so C++ core and C diagnostic tools stay in sync.

## 2. Glide Combiner & Blending Mathematics

- Color/Alpha combiner equation:
  $$\text{Color}_{\text{out}} = (\text{ArgA} \pm \text{ArgB}) \cdot \text{Factor} + \text{ArgC}$$
- **Glide 3.x Default Combiner States** at `grSstWinOpen`:
  - Function: `GR_COMBINE_FUNCTION_SCALE_OTHER` (`0x3`)
  - Factor: `GR_COMBINE_FACTOR_ONE` (`0x8`) (yields opaque alpha `1.0f`; zeroing this causes black screens when blending is enabled).

## 3. State Serialization Compliance (`grGlideGetState` / `grGlideSetState`)

Whenever a new pipeline state variable is added, symmetrically serialize and restore it across Glide 2.11, Glide 2.x, and Glide 3.x entry points:
1. Culling mode (`sstCullMode`)
2. Chromakeying (`chromakeyMode`, `chromakeyValue`, chromakey range)
3. Depth buffering (`depthBufferMode`, `depthFunction`, `depthMask`, `depthBias`, `depthRange`)
4. Alpha blending & alpha test (`blendFuncSource/Dest`, `alphaTestFunc/Ref`)
5. Viewport, clip boundaries, origin (`sstOrigin`), color mask, dither, stipple, fog, and combiner state.

## 4. `grSstWinOpen` Host Handle (`hWnd`) & DOSBox Glide ABI Contract

- `hWnd == 0`: Standalone application/demo/test — wrapper creates and owns its own window (unless `forceNoWindow` is set).
- `hWnd != 0`: Host emulator / Win32 window injection:
  - **DOSBox-X** (`src/hardware/glide.cpp`): Passes `wmi.info.x11.window` from `statWMInfo()` (on X11/XWayland a 32-bit X11 `Window` ID; on native Wayland it aliases `wmi.info.wl.surface` inside `SDL_SysWMinfo`).
  - **Patched SVN DOSBox (`dosbox-0`)**: Queries optional `dlsym` symbol `setConfig(uint32_t flags, uint32_t* magic)` and `setConfigRes`. If exported and the wrapper writes `0x324c4453` (`'SDL2'`) to `*magic`, SVN DOSBox under `sdl12-compat` passes the live `SDL_Window*` pointer instead of a raw X11 Window ID.
- Always verify exported C ABI symbols after frontend refactors using `nm -D --defined-only` against `libglide.so` (`161`), `libglide2x.so` (`152`), and `libglide3x.so` (`129`), matching `glide.def`, `glide2x.def`, and `glide3x.def`.

## 5. Authoritative Grounding Sources

- **In-Repo Headers & Docs**:
  - [include/glide211/](../../../include/glide211/), [include/glide2/](../../../include/glide2/), [include/glide3/](../../../include/glide3/), [include/3dfx/](../../../include/3dfx/), [include/glide_ng/env_vars.h](../../../include/glide_ng/env_vars.h)
  - [docs/user-guide/hardware-and-versions.md](../../../docs/user-guide/hardware-and-versions.md)
  - [docs/developer-guide/api-coverage.md](../../../docs/developer-guide/api-coverage.md)
- **Optional Local Reference Trees (READ-ONLY, git-ignored under `vendor/refs/` when present)**:
  - `vendor/refs/glide/` (3dfx Glide 2.x / 3.x SDKs), `vendor/refs/dosbox-x/`, `vendor/refs/openglide/`
