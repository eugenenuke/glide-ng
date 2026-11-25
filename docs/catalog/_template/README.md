# <Title> (`<executable>`)

| Field | Value |
| :--- | :--- |
| **Category** | Game / Demo / Tool |
| **Year** | `<YYYY>` |
| **Platform(s)** | `DOS/32 (GLIDE2X.OVL)` · `Win32 / Wine` · `Linux` |
| **Glide API & Version** | Glide 2.x (`2.56`) / Glide 3.x (`3.10`) |
| **Recommended Card Profile** | `Voodoo2` (default) / `VoodooGraphics` / `Voodoo5` |
| **Verified Backends** | Vulkan · OpenGL ES / WGL · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified |

---

## Overview

Brief 2–3 sentence summary of what the game, demo, or tool is, which Glide features it exercises (e.g., W-buffer, 2-TMU multitexturing, `P_8` palettes, persistent LFB lock, VSA-100 4× Spatial AA / T-Buffer), and how it looks/behaves on `glide-ng`.

---

## Editions, Patches & Executables

List known versions, official/unofficial 3dfx patches, and which `.exe` to run (if the release ships multiple binaries):

| Executable | Renderer / Patch | Recommended? | Notes |
| :--- | :--- | :---: | :--- |
| `<game_3dfx.exe>` | 3dfx Glide 2.x / 3.x | **Yes** | Primary Glide binary |

---

## Setup & Launch

1. Copy [`glide-ng.ini`](./glide-ng.ini) into the same folder as `<executable>` (or pass `GLIDE_NG_CONFIG=/path/to/glide-ng.ini`). For DOSBox titles that ship a portable `game.sh` (and optional `dosbox-override.conf`), scaffold the game directory with `glide-dosbox init --catalog=<slug> --cdimg=/path/to/disc.cue`.
2. *(Linux / DOSBox)* Install `glide-ng` system-wide (or set `LD_LIBRARY_PATH=/path/to/glide-ng/lib` when running from a local build). *(Win32 / Wine)* Copy `glide-ng`'s `glide2x.dll` and/or `glide3x.dll` into the same folder.
3. Launch:

```bash
# Win32 / Wine example
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./<executable>
```

---

## Quirks, Known Issues & Controls

### Quirks & Fixes
* **Quirk name**: Symptom, root cause, and exact workaround or preset key.

### Useful Controls
* `Esc` — Quit
