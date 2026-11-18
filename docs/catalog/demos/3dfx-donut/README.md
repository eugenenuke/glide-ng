# 3dfx Donut (`donut.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Official 3dfx Tech Demo |
| **Year** | 1998 |
| **Platform(s)** | `Win32 / Wine / Proton` |
| **Glide API & Version** | Glide 2.x (`glide2x.dll`, `2.56`) |
| **Recommended Card Profile** | `Voodoo2` (default) |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified (pixel-identical across all 3 backends) |

---

## Overview

3dfx's classic 1998 procedural reflection showcase (`donut.exe`) renders a glossy chrome torus with live procedural reflection maps streamed continuously to texture memory. It exercises inclusive-bound `grTexDownloadMipMapLevelPartial` mipmap strip updates and coplanar `GR_DEPTHBUFFER_WBUFFER` (`LEQUAL`) passes.

---

## Setup & Launch

1. Copy `glide2x.dll` from `build_release/win32/bin/` (or `build_win32/bin/`) and [`glide-ng.ini`](./glide-ng.ini) next to `donut.exe`.
2. Launch under Wine / Proton:

```bash
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./donut.exe
```

---

## Quirks, Known Issues & Controls

### Quirks & Behavior
* **Automatic `Win32Embedded` hosting**: `donut.exe` opens a maximized `WS_POPUP` host window and calls `grSstWinOpen(0, ...)`. `glide-ng` resolves `hWnd == 0` to the demo's active process window and embeds directly inside it.
* **Linear gamma (`gamma = 1.0`)**: Looks crisp and balanced at default linear gamma; no gamma lift is needed.
* **High-DPI / multi-monitor Wine tip**: If the maximized popup stretches awkwardly across multiple monitors, constrain it with `wine explorer /desktop=Glide,640x480 ./donut.exe`.

### Useful Controls
* `Esc` — Quit
