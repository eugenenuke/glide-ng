# 3dfx Demo Room (`demoroom.exe`)

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

One of 3dfx's original Voodoo Graphics (SST-1) Arcade Tool Box tech demos, *Demo Room* (`demoroom.exe`) demonstrates multi-pass projected textures, dynamic spotlight movement, and animated projected sequences (`0000..0020.3DF`) over Egyptian sandstone hieroglyph walls.

---

## Setup & Launch

1. Keep the required **3dfx ATB runtime DLLs** (`apilib.dll`, `fltdata.dll`, `lm.dll`, `mgdd.dll`, `runtime.dll`, `textureeditor.dll`, `MSVCRT40.DLL`) and `.3DF`/`.bof` assets in the same directory as `demoroom.exe`.
2. Copy `glide-ng`'s `glide2x.dll` and [`glide-ng.ini`](./glide-ng.ini) into that directory.
3. Launch under Wine / Proton:

```bash
# With glide-ng.ini copied next to demoroom.exe (sets card_model=VoodooGraphics and gamma=1.7):
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./demoroom.exe

# Or explicitly via env vars without copying glide-ng.ini:
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_CARD_MODEL=voodoo1 SST_GAMMA=1.7 wine ./demoroom.exe
```

---

## Quirks, Known Issues & Controls

### Quirks & Fixes
* **Appears nearly black on startup at `gamma = 1.0`**: *Demo Room* opens with a pure-blue projected texture (`PROJBLUE.3DF`, RGB `0,0,161`) modulating warm brown sandstone walls (`WALLMAP.3DF`, RGB `117,82,21` — very little blue channel). Authentic 1996 Voodoo Graphics (SST-1) hardware defaulted its RAMDAC LUT to **gamma `1.7`**; at linear `1.0` the initial room crushes down to average RGB `(0, 0, 14)`, leaving only the white menu bar and 3dfx starburst logo easily visible.
  * **Fix**: Use [`glide-ng.ini`](./glide-ng.ini) (`gamma = 1.7`, matching `SST_GAMMA=1.7`) and press **`Space`** to cycle through the red, green, 4-quadrant multicolor, white spotlight (`PROJWHT.3DF`), photo (`PROJGORD.3DF`), and animated (`0000..0020.3DF`) projected lights.
* **Centered 3D white arrow when holding `Up` (Expected Behavior)**: Holding `Up` pitches the projected spotlight downward toward the floor and swings the demo's intentional 3D light-direction pointer mesh (`demolite`) into view at screen center (also visible in wireframe mode via `Ctrl-W`).

### Useful Controls
* `Space` — Cycle projected light textures
* Arrow keys — Move spotlight (`Shift + Up/Down` forward/back)
* NumPad — Move camera
* `Ctrl-L` — Toggle projected light · `Ctrl-B` — Bilinear filter · `Ctrl-M` — Mipmapping · `Ctrl-W` — Wireframe
* `Esc` — Quit
