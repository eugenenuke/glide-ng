# 3dfx Surf (`surf_glide2x.exe` & `surf_glide3x.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx Tech Demo |
| **Year** | 1998 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | **Glide 2.x (`glide2x.dll`, `2.56`) & Glide 3.x (`glide3x.dll`, `3.03`)** |
| **Recommended Card Profile** | `Voodoo2` (default — 2 TMUs for single-pass environment mapping) |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (pixel-identical between Glide 2.x & Glide 3.x across all 3 backends) |

---

## Overview

3dfx's 1998 parametric surface tessellation demo ships as **both** a Glide 2.x binary (`surf_glide2x.exe`) and a Glide 3.x binary (`surf_glide3x.exe`). It opens `1024×768` triple-buffered (`Buffers=3, Aux=1`) by default and exercises Glide 3.x programmable vertex layouts (`grVertexLayout`, `grDrawVertexArray`, `grDrawVertexArrayContiguous`), 2-TMU environment mapping, fog, dynamic LOD tessellation, and specular lighting.

---

## Setup & Launch

1. Copy `glide2x.dll` and `glide3x.dll` from `build_release/win32/bin/` (or `build_win32/bin/`) and [`glide-ng.ini`](./glide-ng.ini) next to the executables.
2. Launch either build under Wine / Proton:

```bash
# Glide 3.x build
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./surf_glide3x.exe

# Glide 2.x build
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./surf_glide2x.exe
```

---

## Quirks, Known Issues & Controls

### Quirks & Tips
* **Linear gamma (`gamma = 1.0`)**: Looks ideal out of the box with no gamma adjustment.
* **Benchmarking uncapped FPS**: Pass `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` (and press `V` if the demo's own VSync toggle is active).
* **CPU-only / `llvmpipe` hosts**: At `1024×768`, `GLIDE_NG_BACKEND=software` (with `GLIDE_NG_SW_SIMD=avx512` on Zen 4 or default `avx2`) is typically 2–3× faster than Mesa `llvmpipe`, or pass `-res 640x480` / `-res 800x600`.

### Useful Controls
* Mouse — Rotate camera · `Left`/`Right` — Move camera
* `E` — Toggle 2-TMU environment map · `H` — Fog · `W` — Wireframe · `D` — Control points
* `P` — Pause waves · `T` — B-spline / Bezier · `X` — Dynamic LOD · `Y` — Specular · `V` — VSync
* `Esc` — Quit
