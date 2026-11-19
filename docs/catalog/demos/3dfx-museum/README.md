# 3dfx Museum (`museum.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx VSA-100 / Voodoo5 Tech Demo |
| **Year** | 1999 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 3.x (`glide3x.dll`, `3.10`) |
| **Recommended Card Profile** | **`Voodoo5`** (4× Spatial AA + Soft Reflectance) |
| **Verified Backends** | Vulkan (4× MSAA) · OpenGL 3.3 Core / WGL (4× MSAA) · Software (2×2 SSAA, AVX2 / AVX-512) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (consistent across all 3 backends) |

---

## Overview

3dfx's Voodoo5 *Museum* (`museum.exe`) showcases VSA-100 **4-sample T-Buffer Soft Reflectance** (`grTBufferWriteMaskExt` per-sample jittered reflections on the translucent marble floor) alongside **4× Spatial Anti-Aliasing** (`grSstWinOpenExt`).

---

## Setup & Launch

1. Copy `glide-ng`'s `glide3x.dll` and [`glide-ng.ini`](./glide-ng.ini) next to `museum.exe`.
2. Launch `museum.exe` directly with the full long model path (do **not** use `go.bat` under Linux/Wine — see quirks below):

```bash
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan \
  wine explorer /desktop=V5,1024x768 ./museum.exe \
  -res 1024x768 -pixfmt aa4rgb565 -model 'data\DistressedMuseum\museum.gxf'
```

*(Alternate clean gallery scene: `-model 'data\museum\museum.gxf'`.)*

---

## Quirks, Known Issues & Controls

### Quirks & Fixes
* **`go.bat` fails under Wine (`couldn't open file data\distre~1\museum.gxf`)**: The original Windows 95/98 `go.bat` hardcodes the 8.3 short directory name `distre~1`, which Linux filesystems do not synthesize. Always launch `museum.exe` directly with `-model 'data\DistressedMuseum\museum.gxf'`.
* **Voodoo5 profile required for Soft Reflectance**: [`glide-ng.ini`](./glide-ng.ini) sets `card_model = Voodoo5` (`api_version = 3.10`) so `GR_EXTENSION` exposes `PIXEXT` and `grSstWinOpenExt` / `grTBufferWriteMaskExt` resolve. (For 2× AA on Voodoo4, use `GLIDE_NG_CARD_MODEL=voodoo4` with `-pixfmt aa2rgb565`.)
* **Demo-native `F3` & `F9` behavior (expected, not a wrapper bug)**:
  * **`F3` (Soft Reflectance toggle)**: Toggles between **blurred reflection** (starting at a subtle default blur grade of `0.02`) and **sharp mirror reflection** — it does *not* disable reflections. Press **`Up` arrow** several times to increase the blur grade and make `F3` ON vs. OFF immediately obvious!
  * **`F9` (Split-Screen)**: Cycles 3 modes: Full screen → **Split Mode 1** (Left: `No Spatial Anti-Aliasing` + `No Reflectance` with reflections off; Right: current `F2`/`F3` state) → **Split Mode 2** (Left: forced `No Spatial Anti-Aliasing` + sharp mirror; Right: forced `Spatial Anti-Aliasing` + blurred Soft Reflectance). Because Split Mode 2 hardcodes both halves internally, `F2`/`F3` have no visible effect while Split Mode 2 is active.
* **Wine 10 + GNOME Wayland cold start**: Prepend `wine explorer /desktop=V5,1024x768 cmd /c exit &&` if the first cold launch shows black borders.

### Useful Controls
* `Space` / any key — Start flythrough after loading
* `F1` — Options menu · `F2` — Spatial AA toggle · `F3` — Soft Reflectance toggle
* `Up` / `Down` — Increase / decrease reflectance blur grade
* `F5`/`F6` — Camera views · `F7` — Free Flight (mouse + NumPad arrows) · `F9` — Split-screen cycle
* `Esc` — Quit
