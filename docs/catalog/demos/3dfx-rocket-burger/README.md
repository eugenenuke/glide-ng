# 3dfx Rocket Burger (`RocketBurger.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx VSA-100 / Voodoo5 Tech Demo |
| **Year** | 1999 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 3.x (`glide3x.dll`, `3.10`) |
| **Recommended Card Profile** | **`Voodoo5`** (4× Spatial AA + T-Buffer Motion Blur & Depth of Field) |
| **Verified Backends** | Vulkan (4× MSAA) · OpenGL 3.3 Core / WGL (4× MSAA) · Software (2×2 SSAA, AVX2 / AVX-512) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (consistent across all 3 backends) |

---

## Overview

3dfx's retro-futuristic Voodoo5 *Rocket Burger* (`RocketBurger.exe`) demo exercises all three flagship VSA-100 **T-Buffer** cinematic effects across a scripted 700-frame diner chase sequence: **4× Spatial Anti-Aliasing** (`F2`), **4-sample Aperture Depth of Field** (`F4`), and **4-sample Temporal Motion Blur** (`F3`).

---

## Setup & Launch

1. Copy `glide-ng`'s `glide3x.dll` and [`glide-ng.ini`](./glide-ng.ini) next to `RocketBurger.exe`.
2. Launch `./RocketBurger.exe` directly with exact filename casing and the full long path (do **not** use `go.bat` under Linux/Wine):

```bash
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan \
  wine explorer /desktop=V5,640x480 ./RocketBurger.exe \
  -res 640x480 -pixfmt aa4rgb565 -model 'data\small tgas\scene.gxf'
```

*(On CPU-only hosts without a hardware GPU, `GLIDE_NG_BACKEND=software` runs smoothly at ~80–94 FPS with 2×2 SSAA at `640×480`.)*

---

## Quirks, Known Issues & Controls

### Quirks & Fixes
* **`go.bat` fails under Wine (`couldn't open file data\smallt~1\scene.gxf`)**: The original `go.bat` hardcodes the Windows 8.3 short name `smallt~1` for `small tgas` and lowercase `Rocketburger.exe`. Run `./RocketBurger.exe` (exact casing) with `-model 'data\small tgas\scene.gxf'`.
* **Voodoo5 profile required for T-Buffer effects**: [`glide-ng.ini`](./glide-ng.ini) sets `card_model = Voodoo5` (`api_version = 3.10`) so `PIXEXT`, `grSstWinOpenExt`, and `grTBufferWriteMaskExt` are available.
* **Demo-native `F2` / `F3` / `F4` / `F9` behavior (verified in `RocketBurger.exe`, not a wrapper bug)**:
  * **`F2` (Spatial AA)**: In full-screen mode, `RocketBurger.exe` unconditionally calls `grEnable(GR_AA_MULTI_SAMPLE)` whenever a T-Buffer context is open — `F2` only toggles the HUD label (and disables `F3`/`F4` when turned off). The binary's only `grDisable(GR_AA_MULTI_SAMPLE)` call occurs on the **left half of `F9` split-screen**, which always renders aliased regardless of `F2`.
  * **`F4` (Depth of Field) & `F3` (Motion Blur) are timeline-gated**: The scripted flythrough runs the 4-sample DoF aperture pass only during **frames `140–364`** (billboard / diner approach) and runs the 4-sample temporal Motion Blur pass only during **frames `365–700`** (when the hover-cop and drag-racing hot rods streak past the camera). Toggling `F3` outside frames `365–700` updates the HUD status until the chase begins.
* **Linear gamma (`gamma = 1.0`)**: Vibrant and well-balanced out of the box.

### Useful Controls
* `Space` / any key — Dismiss `"Press any key to continue"`
* `F1` — Options menu · `F2` — Spatial AA · `F3` — Motion Blur (frames 365–700) · `F4` — Depth of Field (frames 140–364) · `F9` — Split-screen comparison
* `Esc` — Quit
