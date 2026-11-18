# 3dfx Kitty Hawk (`aademo.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx VSA-100 / Voodoo5 Tech Demo |
| **Year** | 2000 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 3.x (`glide3x.dll`, `3.10`) |
| **Recommended Card Profile** | **`Voodoo5`** (4× Spatial AA) · `Voodoo4` (2× Spatial AA) |
| **Verified Backends** | Vulkan (4× MSAA) · OpenGL 3.3 Core / WGL (4× MSAA) · Software (2×2 SSAA, AVX2 / AVX-512) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Voodoo5 4× AA) · [`glide-ng.voodoo4.ini`](./glide-ng.voodoo4.ini) (Voodoo4 2× AA) |
| **Status** | Verified (consistent across all 3 backends) |

---

## Overview

Part of 3dfx's Voodoo4/Voodoo5 (**VSA-100 / Napalm**) Glide 3.x showcase family alongside *Museum* and *Rocket Burger*, *Kitty Hawk* (`aademo.exe`) demonstrates the 1903 Wright Flyer with live toggleable **Spatial Anti-Aliasing** (`grSstWinOpenExt`, `grEnable`/`grDisable(GR_AA_MULTI_SAMPLE)`), T-Buffer sample masking (`grTBufferWriteMaskExt`), split-screen aliased vs. 4× AA comparison (`F9`), and packed 32-bit `GR_PARAM_PARGB` vertex colors.

---

## Setup & Launch

1. Copy `glide3x.dll` from `build_release/win32/bin/` (or `build_win32/bin/`) and [`glide-ng.ini`](./glide-ng.ini) next to `aademo.exe`.
2. Launch under Wine / Proton (pass `-model 'data\kittyhawk\kittyhawk.gxf'` to skip the Win32 Open File dialog):

```bash
# Optional virtual desktop primer avoids a cold-start Wine 10 + GNOME Wayland fullscreen race:
wine explorer /desktop=V5,1024x768 cmd /c exit && \
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan \
  wine explorer /desktop=V5,1024x768 ./aademo.exe \
  -res 1024x768 -pixfmt aa4rgb565 -model 'data\kittyhawk\kittyhawk.gxf'

# High-DPI (e.g. 200% scaling) or Software AVX-512 on Zen 4:
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 \
  GLIDE_NG_HUD=fps,backend \
  wine explorer /desktop=V5,1024x768 ./aademo.exe \
  -res 1024x768 -pixfmt aa4rgb565 -model 'data\kittyhawk\kittyhawk.gxf'
```

For **2× Spatial AA** (`Voodoo4`), copy [`glide-ng.voodoo4.ini`](./glide-ng.voodoo4.ini) as `glide-ng.ini` and pass `-pixfmt aa2rgb565`.

---

## Quirks, Known Issues & Controls

### Quirks & Behavior
* **Card model gating**: `aademo.exe` requires **`card_model = Voodoo5`** (4× AA: `"Voodoo5, 4 sample, Spatial Anti-Aliasing"`) or **`Voodoo4`** (2× AA cap with `-pixfmt aa2rgb565`) so `GR_EXTENSION` advertises `PIXEXT` and `grSstWinOpenExt` resolves. Older card profiles (`voodoo1`–`voodoo3`) authentically gate out `grSstWinOpenExt`, causing the demo to fall back to non-AA `grSstWinOpen`.
* **No arguments opens file dialog**: Launching `aademo.exe` bare opens a Win32 Open File dialog (`comdlg32`) prompting for a `.gxf` file. Passing `-model 'data\kittyhawk\kittyhawk.gxf'` (or running `go.bat`, whose path is 8.3-safe) jumps directly into the Wright Flyer showcase.
* **Loading screen & fast load times**: During `.gxf` parsing and texture upload, the demo draws a golden hover-car loading screen (`Loading` at top-left) and briefly shows `"Press any key to continue"` (its own `PeekMessageA` loop falls straight through into the flythrough when the Win32 queue is empty). Uncapping frame pacing during loading (`GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0`) shortens load time to ~1.5–4s.
* **HiDPI / Wine 10 Wayland sizing**: Match `/desktop=V5,<W>x<H>` to `-res <W>x<H>` (e.g. `1024x768` or `1600x1200` on 200% HiDPI) and avoid maximizing the Wine virtual desktop window. If a cold launch opens fullscreen with black margins on GNOME Wayland, run `wine explorer /desktop=V5,1024x768 cmd /c exit` once first (or press `Alt+F11`).
* **Linear gamma (`gamma = 1.0`)**: Well-balanced out of the box.

### Useful Controls
* `F1` — Help menu · `F2` — Live Spatial AA toggle · `F5`/`F6` — Camera modes
* `F7` — Free Flight (NumPad `4/8/6/2` + mouse, `Home`/`End` speed)
* `F9` — Live single-frame Aliased vs. 4× AA split-screen (yellow vertical divider)
* `F12` — TGA screenshot · `Pause` / `Left` / `Right` — Frame step · `Esc` — Quit
