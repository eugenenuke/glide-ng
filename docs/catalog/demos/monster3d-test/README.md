# Diamond Monster 3D Test Tool (`MNSTRTST.EXE`)

| Field | Value |
| :--- | :--- |
| **Category** | Hardware Driver Demo / Gamma & Resolution Test Utility |
| **Year** | 1997 (Driver v4.10.01.1600, 1997–1999) |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 2.x (`glide2x.dll`, `2.46`) |
| **Recommended Card Profile** | **`VoodooGraphics` (SST-1)** |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified |

---

## Overview

Bundled with Diamond Multimedia's official **Monster 3D (Voodoo Graphics SST-1)** Windows 95/98 driver package (`v4.10.01.1600`), `MNSTRTST.EXE` is the *"Monster 3D Gamma Correction / Refresh / Resolution Test Tool"*. It opens a `640×480` double-buffered (`Buffers=2, Aux=0`) Glide 2.x window and renders spinning textured 3D geometry (cylinder, sphere, torus) alongside the Diamond Multimedia and Monster 3D logos (`CYL*.3DF`, `SPH*.3DF`, `TORUS*.3DF`, `DMMLOGO.3DF`, `MONSTR3D.3DF`).

---

## Executables in the Driver Package

| Executable | API / Imports | Supported by `glide-ng`? | Notes |
| :--- | :--- | :---: | :--- |
| **`MNSTRTST.EXE`** | Glide 2.x (`glide2x.dll`) | **Yes** | Interactive 3D Gamma / Resolution test demo. |
| **`MOJO.EXE`** | DirectDraw 5 (`DDRAW.DLL`) + **`sst1init.dll`** | **No (PCI probe)** | Low-level PCI hardware/driver version probe (`Vendor ID 0x121a`, `FXMEMMAP.VXD`) — not a Glide wrapper application. Use [`glide2_info`](../../tools/glide-info/README.md) instead to inspect `glide-ng`. |

---

## Setup & Launch

1. Keep the `.3DF` texture assets (`CYL*.3DF`, `SPH*.3DF`, `TORUS*.3DF`, `DMMLOGO.3DF`, `MONSTR3D.3DF`) in the same folder as `MNSTRTST.EXE`.
2. Copy `glide2x.dll` from `build_release/win32/bin/` (or `build_win32/bin/`) and [`glide-ng.ini`](./glide-ng.ini) into that folder.
3. Launch under Wine / Proton:

```bash
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./MNSTRTST.EXE
```

---

## Quirks, Known Issues & Controls

### Quirks & Behavior
* **Startup confirmation dialog**: Before initializing Glide (`grGlideInit`), `MNSTRTST.EXE` displays a native Win32 **"Display Information"** modal dialog showing the target resolution and refresh rate. Click **OK** (or press **`Enter`**) to begin the 3D Glide test; clicking **Cancel** exits without loading the 3D scene.
* **Automatic `Win32Embedded` hosting**: Calls `grSstWinOpen(0, GR_RESOLUTION_640x480, ...)` — `glide-ng` resolves `hWnd == 0` to `MNSTRTST.EXE`'s own Win32 window across Vulkan, OpenGL 3.3 Core (WGL), and Software.
* **Testing RAMDAC gamma**: Because `MNSTRTST.EXE` was designed specifically to verify SST-1 gamma calibration, you can experiment with `[glide] gamma = 1.7` (authentic SST-1) vs. `1.0` (linear) or `SST_GAMMA=<value>` at launch.

### Useful Controls
* `Enter` / click **OK** — Start 3D test from initial Display Information dialog
* `Esc` — Quit cleanly (`grSstWinClose` + `grGlideShutdown`)
