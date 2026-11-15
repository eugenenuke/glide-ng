---
name: 3dfx-expert
description: Canonical domain authority on 3dfx Voodoo hardware architectures, Glide 2/3 APIs, and DOSBox Glide ABI extensions.
---

# Workspace Skill: 3dfx Voodoo Architecture & Glide API Expert

Activate this skill when analyzing, modifying, or auditing Glide frontend entry points inside [src/glide2/](../../../src/glide2/), [src/glide3/](../../../src/glide3/), or verifying 3dfx Voodoo hardware specifications.

## 1. 3dfx Voodoo Hardware Specifications & Limits

- **Texture Dimensions & MIP-mapping**:
  - Maximum texture size on Voodoo 1/2 is **256x256 pixels**; supported aspect ratios: 1x1, 1x2, 1x4, 1x8, 2x1, 4x1, 8x1.
  - Glide 3.x uses signed log2 integers for LOD bounds (`largeLod = 8` → 256, `0` → 1), whereas Glide 2.x uses canonical `GR_LOD_*` index constants.
- **Color Precision**:
  - Native pixel pipeline operates in **16-bit color depth** (RGB565 / ARGB1555 / ARGB4444) with authentic 2x2 / 4x4 dithering.

## 2. Glide Combiner & Blending Mathematics

- Color/Alpha combiner equation:
  $$\text{Color}_{\text{out}} = (\text{ArgA} \pm \text{ArgB}) \cdot \text{Factor} + \text{ArgC}$$
- **Glide 3.x Default Combiner States** at `grSstWinOpen`:
  - Function: `GR_COMBINE_FUNCTION_SCALE_OTHER` (`0x3`)
  - Factor: `GR_COMBINE_FACTOR_ONE` (`0x8`) (yields opaque alpha `1.0f`; zeroing this causes black screens when blending is enabled).

## 3. State Serialization Compliance (`grGlideGetState` / `grGlideSetState`)

Whenever a new pipeline state variable is added, symmetrically serialize and restore it in both Glide 2.x and Glide 3.x entry points:
1. Culling mode (`sstCullMode`)
2. Chromakeying (`chromakeyMode`, `chromakeyValue`, chromakey range)
3. Depth buffering (`depthBufferMode`, `depthFunction`, `depthMask`, `depthBias`, `depthRange`)
4. Alpha blending & alpha test (`blendFuncSource/Dest`, `alphaTestFunc/Ref`)
5. Viewport, clip boundaries, origin (`sstOrigin`), color mask, dither, stipple, fog, and combiner state.

## 4. `grSstWinOpen` Host Handle (`hWnd`) & DOSBox Glide ABI Contract

- `hWnd == 0`: Standalone application/demo/test — wrapper creates and owns its own window (unless `forceNoWindow` is set).
- `hWnd != 0`: Host emulator injection:
  - **DOSBox-X** ([refs/dosbox-x/src/hardware/glide.cpp](../../../refs/dosbox-x/src/hardware/glide.cpp)): Passes `wmi.info.x11.window` from `statWMInfo()` (on X11/XWayland this is a 32-bit X11 `Window` ID; on native Wayland it aliases `wmi.info.wl.surface` inside `SDL_SysWMinfo`).
  - **Patched SVN DOSBox (`dosbox-0`)**: Queries optional `dlsym` symbol `setConfig(uint32_t flags, uint32_t* magic)` and `setConfigRes`. If our wrapper exports `setConfig` and writes `0x324c4453` (`'SDL2'`) to `*magic`, SVN DOSBox under `sdl12-compat` passes the live `SDL_Window*` pointer instead of a raw X11 Window ID!
- Always verify exported C ABI symbols after frontend refactors using `nm -D --defined-only build/lib/libglide2x.so` and `libglide3x.so`.

## 5. Authoritative Grounding Sources (READ-ONLY)

- **Glide 2.x / 3.x Official Headers & Tests**:
  - [refs/glide/glide2x/sst1/include/](../../../refs/glide/glide2x/sst1/include/)
  - [refs/glide/glide3x/h3/glide3/src/](../../../refs/glide/glide3x/h3/glide3/src/)
  - [refs/3dfx-Glide-API/](../../../refs/3dfx-Glide-API/)
- **DOSBox Glide Integration References**:
  - DOSBox-X: [refs/dosbox-x/src/hardware/glide.cpp](../../../refs/dosbox-x/src/hardware/glide.cpp)
  - Patched SVN DOSBox (`qemu-3dfx`): `../dosbox/qemu-xtra/dosbox/dosbox-0/src/hardware/glide.cpp`
  - OpenGLide reference: [refs/openglide/](../../../refs/openglide/) and [refs/openglide-ng/](../../../refs/openglide-ng/)
