# 3dfx Valley of Ra (`fight.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx Tech Demo (Arcade Tool Box) |
| **Year** | 1997 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 2.x (`glide2x.dll`, `2.56`) |
| **Recommended Card Profile** | `Voodoo2` (default) |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (pixel-identical across all 3 backends) |

---

## Overview

Built on 3dfx's **Arcade Tool Box (ATB)** framework, *Valley of Ra* (`fight.exe`) showcases Egyptian arena combat with alpha-blended cutouts, billboards, and `GR_DITHER_2x2` color/alpha dithering inside a `640×480` Win32 ATB host window.

---

## Setup & Launch

1. Keep the demo's required **3dfx ATB runtime DLLs** alongside `fight.exe` and its data files (`fight.bof`, `.DAT`, textures):
   * `apilib.dll`, `fltdata.dll`, `lm.dll`, `mgdd.dll`, `runtime.dll`, `textureeditor.dll`, `MSVCRT40.DLL`
2. Copy `glide-ng`'s `glide2x.dll` and [`glide-ng.ini`](./glide-ng.ini) into the same folder.
3. Launch under Wine / Proton:

```bash
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./fight.exe
```

---

## Quirks, Known Issues & Controls

### Quirks & Behavior
* **Runtime DLL dependency**: Launching `fight.exe` without the bundled ATB DLLs (or from outside its asset directory) fails at Win32 loader startup. Always `cd` into the demo directory before running Wine.
* **Bright outdoor palette (`gamma = 1.0`)**: Unlike the darker indoor ATB demos (*Demo Room* and *Wizard's Tower*), *Valley of Ra* is authored bright and looks natural at `gamma = 1.0`.
* **`GR_DITHER_2x2` billboards**: Exercises 2×2 ordered alpha dithering cleanly across Vulkan, OpenGL 3.3 Core (WGL), and Software.

### Useful Controls
* See `DEMO.HLP` / `README.TXT` in the demo directory · `Esc` — Quit
