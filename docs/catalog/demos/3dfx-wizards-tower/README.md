# 3dfx The Wizard's Tower (`wiz.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx Tech Demo (Arcade Tool Box) |
| **Year** | 1996 / 1997 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 2.x (`glide2x.dll`, `2.48`) |
| **Recommended Card Profile** | **`VoodooGraphics` (SST-1)** + **`gamma = 1.7`** |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (pixel-identical across all 3 backends) |

---

## Overview

*The Wizard's Tower* (`wiz.exe`) is a 3dfx Voodoo Graphics (SST-1) Arcade Tool Box interactive castle walkthrough demonstrating multi-pass baked lightmaps (`GR_BLEND_ZERO, GR_BLEND_SRC_COLOR`), stained-glass windows, and interactive enemy spawning.

---

## Setup & Launch

1. Keep the required **3dfx ATB runtime DLLs** (`apilib.dll`, `fltdata.dll`, `lm.dll`, `mgdd.dll`, `runtime.dll`, `textureeditor.dll`, `MSVCRT40.DLL`), `wiz.bof`, and the `textures/` directory in the same folder as `wiz.exe`.
2. Copy `glide-ng`'s `glide2x.dll` and [`glide-ng.ini`](./glide-ng.ini) into that folder.
3. Launch under Wine / Proton:

```bash
# With glide-ng.ini copied next to wiz.exe (sets card_model=VoodooGraphics and gamma=1.7):
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./wiz.exe

# Or via env vars:
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_CARD_MODEL=voodoo1 SST_GAMMA=1.7 wine ./wiz.exe
```

---

## Quirks, Known Issues & Controls

### Quirks & Fixes
* **Dark Room 1 baked lightmaps**: Room 1's multiplicative lightmaps (`textures/WIZ1/*.3DF`) have very low linear pixel values (median RGB `5, 10, 11`), authored specifically for Voodoo Graphics' hardware **`1.7`** RAMDAC gamma curve. At `gamma = 1.0`, the central altar and lower stairs appear near-black while only the upper arches and stained-glass windows stay visible.
  * **Fix**: Use [`glide-ng.ini`](./glide-ng.ini) (`gamma = 1.7` / `SST_GAMMA=1.7`).
* **Pressing `Enter` opens `Invalid event file app.evt` dialog**: Pressing `Enter` invokes ATB's event playback feature (`app.evt`), which is missing or incompatible in many redistributed demo archives. Navigate interactively using the arrow keys instead.

### Useful Controls
* Arrow keys — Move / turn (`Shift + Up/Down` look up/down)
* `B` — Spawn bat · `M` — Spawn warrior · `Space` — Fire
* `P` / `Ctrl-P` — FPS readout · `Ctrl-W` — Wireframe · `Ctrl-T` — Toggle textures
* `Esc` — Quit
