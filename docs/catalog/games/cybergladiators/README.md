# CyberGladiators (1996 — Win32 3dfx Glide 2.11)

| Field | Value |
| :--- | :--- |
| **Category** | Commercial Game |
| **Year** | 1996 |
| **Platform(s)** | `Win32 / Wine (glide.dll)` |
| **Glide API & Version** | **Glide 2.11** (`glide.dll` — pre-2.3 SST-1 ABI) |
| **Recommended Card Profile** | `VoodooGraphics` (`SST-1`: 4 MB FB, 1× 2 MB TMU, API `2.11`) |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Authentic SST-1) · [`glide-ng.hq.ini`](./glide-ng.hq.ini) (4× MSAA + Linear) · [`glide-ng.software.ini`](./glide-ng.software.ini) (CPU Software) |
| **Status** | Verified |

---

## Overview

Sierra On-Line / Dynamix's 1996 3D arena fighter *CyberGladiators* was one of the earliest Windows 95 commercial games to ship native 3dfx Voodoo Graphics acceleration, dynamically loading **`GLIDE.DLL`** (Glide 2.11) via `LoadLibrary` and resolving 27 `__stdcall` pre-2.3 SST-1 symbols (`grSstOpen`, `grSstPassthruMode`, `guTexAllocateMemory`, `guDrawTriangleWithClip`, `grGammaCorrectionValue`, etc.).

Under `glide-ng`'s dedicated **`glide.dll`** frontend:
- Both the **character select UI** (~40–46 triangles/frame) and **3D fighting arenas** (~900–1,200 triangles/frame) render natively at `640×480` with bilinear-filtered `ARGB_1555` textures, smooth Gouraud-lit metallic fighter armor, translucent attack motion trails, and flying hit sparks at a locked **60.0 FPS**.
- `grSstPassthruMode(GR_PASSTHRU_SHOW_VGA / GR_PASSTHRU_SHOW_SST1)` cleanly hands the shared Win32 window to DirectDraw / `MCIAVI` during the opening `SPLASH4.AVI` intro movie and reclaims the Vulkan/WGL/GDI surface automatically when 3D rendering begins.
- Authentic **2 MB TMU0 VRAM budget enforcement** in `guTexAllocateMemory` satisfies *CyberGladiators*' startup texture LRU pool probe (15 × `256×256` `ARGB_1555` textures followed by `128×128` allocations until `GR_NULL_MIPMAP_HANDLE` is returned).
- On modern Wine setups where *CyberGladiators*' built-in DirectDraw 2D software renderer crashes inside `wined3d` (`Out of adapter memory`), dropping in `glide.dll` bypasses the DirectDraw 3D path completely and makes the game cleanly playable out of the box.

---

## Editions, Patches & Executables

| Executable | Renderer | Works with `glide-ng`? | Technical Notes |
| :--- | :--- | :---: | :--- |
| **`CYBER.EXE`** *(Community `GetTickCount` Timer Patch)* | Glide 2.11 (`GLIDE.DLL`) | **Yes (Recommended)** | Applies an 8-byte patch (at `0x403b15` & `0x403b24`) redirecting two calls to the game's 1996 `QueryPerformanceCounter` helper (`0x403ade`) to `GetTickCount` (`0x446b28`), fixing excessive game/animation speed on modern high-frequency CPUs. |
| **`CYBER_ORIG.EXE`** *(Original 1996 CD Executable)* | Glide 2.11 (`GLIDE.DLL`) | **Yes (Fast Timing)** | Works identically with `glide.dll`, but its unpatched `QueryPerformanceCounter` timer runs too fast on modern CPUs. Use the `GetTickCount`-patched `CYBER.EXE` (or enable `vsync = true` + `max_fps = 60`). |
| **`CYBER.EXE`** *(Without `glide.dll`)* | DirectDraw 2 Software | **No (WineD3D Crash)** | Requires a 16-bit High Color desktop and crashes under modern Wine's `wined3d` with `Out of adapter memory`. Use `glide.dll` instead. |

---

## Recommended Launch Configurations (Wine32 / Windows)

Copy `glide-ng`'s 32-bit **`glide.dll`** (from `build_release/win32/bin/glide.dll` or `build_win32/bin/glide.dll`) and your chosen preset (`glide-ng.ini`) into the game's install directory (`SIERRA/CYBER/` alongside `CYBER.EXE`).

> **Use an `800×600` (or larger) Wine Virtual Desktop — Not `640×480`:**
> `CYBER.EXE` opens an overlapped Win32 window with a ~22 px title bar around its `640×480` client area. Inside `/desktop=CG,640x480`, the title bar pushes the bottom ~22 px of the canvas off-screen (clipping the `"Ctrl + O for Options / Esc to Exit"` bar). Running `/desktop=CG,800x600` displays the full unclipped `640×480` canvas cleanly.

### Option A: Authentic Voodoo Graphics (`glide-ng.ini` — Vulkan or OpenGL 3.3 Core) *(Recommended)*

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" \
  wine explorer /desktop=CG,800x600 ./CYBER.EXE
```

To use the Win32 WGL OpenGL 3.3 Core backend instead of Vulkan without editing `glide-ng.ini`:

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" GLIDE_NG_BACKEND=opengl_es \
  wine explorer /desktop=CG,800x600 ./CYBER.EXE
```

### Option B: Best Visuals (`glide-ng.hq.ini` + In-Game `Motion: smooth`)

Copy [`glide-ng.hq.ini`](./glide-ng.hq.ini) as `glide-ng.ini` (or pass `GLIDE_NG_CONFIG=glide-ng.hq.ini`):
- Enables **`msaa = 4`** (4× hardware MSAA on Vulkan / OpenGL 3.3 Core WGL) to eliminate polygonal jagged edges on fighter spikes, blades, and limbs.
- Enables **`presentation_filter = linear`** and **`no_dither_sub = true`** for smooth 24-bit metallic shading without 16-bit dither banding.
- At the Character Select screen, press **`Ctrl + O`** → set **`Motion: smooth`** for smoother fighter animation interpolation.
- **Gamma**: `CYBER.EXE` natively calls `grGammaCorrectionValue(1.6)` at startup, so no INI gamma override is required. If you want a brighter arena, pause during a match (**`Space`**) and press **`\`** to step hardware gamma live (`1.6` → `1.7` → ...).

> **32-Bit Wine + Mesa `llvmpipe` Note:**
> On hosts without a 32-bit hardware Vulkan ICD, `glide-ng` automatically shrinks its 32-bit Vulkan staging/vertex buffers and caps swapchain images on CPU adapters (`llvmpipe`) to stay within Wine32's virtual address limit. On very high core-count CPUs (24–64+ cores), if you still encounter 32-bit memory pressure under `llvmpipe`, install your GPU's 32-bit Vulkan ICD, set `LP_NUM_THREADS=4`, or use **Option C (CPU Software `2×2 SSAA`)**.

### Option C: CPU Software Rasterizer (`glide-ng.software.ini` — Zero-GPU / `2×2 SSAA`)

Copy [`glide-ng.software.ini`](./glide-ng.software.ini) as `glide-ng.ini` (and uncomment `aa = ssaa` for `2×2` supersampled anti-aliasing):

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" GLIDE_NG_BACKEND=software GLIDE_NG_SW_AA=ssaa \
  wine explorer /desktop=CG,800x600 ./CYBER.EXE
```

Renders the full 3D arena with `2×2 SSAA` at a locked **60.0 FPS** on modern AVX2/AVX-512 CPUs with zero GPU or Vulkan ICD required.

### Playing Fullscreen (`GLIDE_NG_FULLSCREEN` vs. `gamescope` vs. Direct Wine)

`CYBER.EXE` has **no** command-line fullscreen flag and explicitly disables `Alt + Enter` when Glide (`GLIDE.DLL`) is active (on real 1996 Voodoo1 hardware, the pass-through card always took over the monitor at `640×480` while Windows kept a fixed `640×480` window). Running inside `wine explorer /desktop=...` therefore stays windowed by default.

Three ways to play fullscreen on Linux:
- **Built-in `glide-ng` borderless fullscreen (`[display] fullscreen = auto` — default on Win32/Wine) — no `gamescope` or `explorer` required *(Recommended)*:** Runs `CYBER.EXE` directly so `SPLASH4.AVI` plays fullscreen via DirectDraw with native audio and keyboard focus, then `glide-ng` strips the Win32 titlebar, expands the window to fill the monitor, and scales `640×480` with aspect-preserving 4:3 black pillarboxes:
  ```bash
  WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
  WINEDLLOVERRIDES="glide=n" GLIDE_NG_PRESENTATION_FILTER=linear \
    wine ./CYBER.EXE
  ```
  *(Avoid wrapping fullscreen mode in `wine explorer /desktop=...` on regular X11/Wayland desktops — Wine's virtual desktop shrinks to a `640×480` floating window during `SPLASH4.AVI` and can steal focus/audio. Only use `explorer /desktop=...` on headless displays like `Xvfb` that lack a `640×480` RandR mode. On CPU-only `llvmpipe` hosts with >16 CPU cores at >1080p desktop resolutions, also set `LP_NUM_THREADS=4` to prevent 32-bit Wine virtual address exhaustion.)*
- **With `gamescope` (hardware DRM GPU required):** Exposes a native `640×480` nested display to Wine/DirectDraw and upscales with `linear`, `fsr` (AMD FSR 1.0), `nis`, `pixel`, or `integer` scaling *(requires a DRM GPU in `/dev/dri` supporting `VK_EXT_physical_device_drm`; will not start on CPU-only `llvmpipe` hosts)*:
  ```bash
  WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
  WINEDLLOVERRIDES="glide=n" \
    gamescope -w 640 -h 480 -W 1920 -H 1080 -r 60 -S fit -F linear --force-windows-fullscreen -f -- \
      wine ./CYBER.EXE
  ```
- **Windowed mode (`GLIDE_NG_FULLSCREEN=0` or `[display] fullscreen = false`):** Keeps `CYBER.EXE` at its native `640×480` window size once Glide 3D rendering starts.

---

## Visual Progression Gallery

All four captures below are unscaled, borderless `640×479` client-area frames from *CyberGladiators* running under Wine32 + `glide.dll` (Glide 2.11):

| | |
| :---: | :---: |
| ![Character Select via Glide 2.11](./screenshots/01_character_select.webp)<br>**1. Character Select (`640×480` · Glide 2.11 / SST-1)**<br>Even the 2D roster UI renders via `glide.dll` (~40 tris/frame, bilinear `ARGB_1555`). Full unclipped `Ctrl+O` / `Esc` footer under `/desktop=CG,800x600`. | ![3D Arena Vulkan 1x](./screenshots/02_arena_voodoo1_1x.webp)<br>**2. 3D Arena — Authentic Voodoo1 (`1×`, Vulkan)**<br>Authentic `1×` SST-1 baseline (`~1,000` tris/frame, `1.5 ms` render time, 60.0 FPS). Shows native polygon aliasing along fighter spikes, blade arm, and shins. |
| ![3D Arena 4x MSAA](./screenshots/03_arena_hq_msaa4.webp)<br>**3. 3D Arena — Enhanced (`4× MSAA` + `no_dither_sub`, OpenGL 3.3 Core)**<br>Exact same combat frame (`58` on clock) with `msaa = 4`: smooth metallic silhouettes, spikes, and limbs with crisp HUD bars and clean sprite boundaries. | ![3D Arena Software 2x2 SSAA](./screenshots/04_arena_sw_ssaa.webp)<br>**4. 3D Arena — CPU Software (`2×2 SSAA`, AVX2/AVX-512)**<br>Pure CPU Software backend with `2×2 SSAA` (`4.5 ms/frame`, locked 60.0 FPS): consistent lighting, hit sparks, and smooth anti-aliased edges. |

---

## Visual Quality & Performance Tuning

### Anti-Aliasing, Filtering & Gamma
* **Best visuals ([`glide-ng.hq.ini`](./glide-ng.hq.ini)):** Pairs `msaa = 4`, `presentation_filter = linear`, and `no_dither_sub = true` to smooth jagged blades, spikes, and arena silhouettes while eliminating 16-bit dither banding.
* **In-game `Motion: smooth`:** At the Character Select screen, press **`Ctrl + O`** and toggle **`Motion: smooth`** for interpolated fighter animation frames.
* **Gamma:** `CYBER.EXE` calls `grGammaCorrectionValue(1.6)` automatically at startup. Pause during a match (`Space`) and press **`\`** to adjust gamma live without modifying `glide-ng.ini`.

---

## Quirks, Known Issues & Controls

### Installation & Engine Notes
* **Manual install (skipping 16-bit `setup.exe` on modern Windows / Wine 10 WoW64):** Copy the uncompressed `CYBER/` folder directly from the CD image to `C:\SIERRA\CYBER\`, map `dosdevices/d:` to the CD root (if needed for audio/movies), and add the 32-bit registry values under `HKLM\Software\Sierra On-Line\CyberGladiators` (`reg add "HKLM\Software\Sierra On-Line\CyberGladiators" /v Dest /d "C:\SIERRA\CYBER\\" /f /reg:32` and `Source` = `D:\`).
* **Mandatory `%WINDIR%\sierra.ini` (`C:\windows\sierra.ini`):** `CYBER.EXE` calls `GetPrivateProfileStringA` with the bare filename `"sierra.ini"`, so it reads **`C:\windows\sierra.ini`** (*not* the game directory). Create it with Windows CRLF line endings:
  ```ini
  [Config]
  CDROM=4
  CPU=Pentium-200 MHz
  ```
  - **`CDROM=`** is parsed via `atoi()` as the CD-ROM **speed multiplier**, not a drive letter. If omitted or empty, the intro AVI is skipped; **`>= 4`** plays the `640×480` Cinepak intro (`SPLASH4.AVI`), while **`< 4`** (e.g. `2`, or a drive letter such as `D:\` which parses as `0`) falls back to the `320×240` `SPLASH.AVI`. Under Wine `mciavi32` / Cinepak (`iccvid`), the `640×480` AVI frame may render unscaled in the top-left of a larger virtual desktop.
  - **`CPU=`** parses the integer MHz substring strictly **between `'-'` and `' '`** (e.g. `Pentium-200 MHz`). Values `< 75` select low detail (`1`), `75–100` select medium (`2`), and `> 100` select high (`3`, with `>= 166` disabling frame drop). If `C:\windows\sierra.ini` is missing when launching without a `cyber.ini`, `CYBER.EXE` leaves its detail level uninitialized at **`0`**, causing the 3D arena walls to render **black** and the floor untextured grey. After adding `C:\windows\sierra.ini`, delete any previously generated `cyber.ini` inside the game folder (or press `Ctrl + O` on the Character Select screen to raise detail back to high).
* **Use an `800×600` virtual desktop (not `640×480`):** Inside `wine explorer /desktop=CG,640x480`, the ~22 px title bar clips the bottom status bar. Run `800x600` or launch directly with `wine ./CYBER.EXE`.
* **No built-in DirectDraw 3dfx fullscreen toggle:** `CYBER.EXE` disables `Alt + Enter` when `GLIDE.DLL` is active; see the fullscreen options above.
* **Startup timing on fast CPUs:** Use the `GetTickCount`-patched `CYBER.EXE` (or keep `vsync = true` + `max_fps = 60`) so animation speed stays locked to 60 FPS.

### Useful Controls

| Action | Player 1 | Player 2 |
| :--- | :--- | :--- |
| **Move / Select Fighter** | `A` / `D` / `W` / `S` | Cursor Keys (`←` / `→` / `↑` / `↓`) |
| **Confirm / Start Match / Pause** | `Space` *(press twice for 1P vs. CPU)* | `Enter` |
| **Speed Punch / Power Punch** | `K` / `I` | `Numpad 5` / `Numpad 8` |
| **Speed Kick / Power Kick** | `L` / `J` | `Numpad 6` / `Numpad 4` |
| **Options Menu (`Motion: smooth`, Arena, Time)** | `Ctrl + O` *(on Character Select)* | — |
| **Adjust 3dfx Hardware Gamma Live** | `Space` *(pause)* then `\` | — |
| **Quit Game** | `Esc` or `Alt + F4` | — |
