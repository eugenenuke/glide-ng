# Pandemonium! (1997 — Win32 3dfx Glide 2.11)

| Field | Value |
| :--- | :--- |
| **Category** | Commercial Game |
| **Year** | 1997 (PC Win95 / 3dfx port) |
| **Platform(s)** | `Win32 / Wine (glide.dll)` |
| **Glide API & Version** | **Glide 2.11** (`glide.dll` — pre-2.3 SST-1 ABI) |
| **Recommended Card Profile** | `VoodooGraphics` (`SST-1`: 4 MB FB, 1× 2 MB TMU, API `2.11`) |
| **Verified Backends** | Vulkan · OpenGL 3.3 Core (WGL) · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Authentic SST-1, `1×`) · [`glide-ng.hq.ini`](./glide-ng.hq.ini) (`4× MSAA` + Linear) · [`glide-ng.software.ini`](./glide-ng.software.ini) (CPU Software) |
| **Frame Pacing** | `30 FPS` (engine target; presets enforce `max_fps = 30`) |
| **Status** | Verified |

---

## Overview

Toys for Bob and Crystal Dynamics' 1997 2.5D polygon platformer *Pandemonium!* is a Windows 95 release (no DOS edition exists) that includes an early **Glide 2.11 (`glide.dll`)** 3dfx port (`PANDY3.EXE`) alongside its DirectDraw software renderer (`PANDY.EXE`). At fixed `640×480`, the 3dfx renderer adds hardware bilinear texture filtering, Gouraud lighting, and translucent particle/orb blending.

Under `glide-ng`'s **`glide.dll`** frontend (`grSstOpen`, `grSstPassthruMode`, fixed-stride `grLfbBegin`/`grLfbGetWritePtr`/`grLfbEnd`), *Pandemonium!* seamlessly alternates between its hardware-accelerated 3D world and the native Win32 menu bar (`Esc` invokes `GR_PASSTHRU_SHOW_VGA` to yield the embedded window for GDI dialogs such as `Volumes` and `Keyboard Setup`, then reclaims it via `GR_PASSTHRU_SHOW_SST1`).

---

## Editions, Patches & Executables

| Executable / Driver | Renderer | Works with `glide-ng`? | Technical Notes |
| :--- | :--- | :---: | :--- |
| **`PANDY3.EXE`** *(Retail 3dfx Win32)* | Glide 2.11 (`GLIDE.DLL`) | **Yes (Recommended)** | Fixed `640×480` Glide 2.11 hardware renderer. Requires `FULL3.CNF` copied as `RESOURCE.CNF`, replacing the bundled 1997 `GLIDE.DLL` with `glide-ng`'s `glide.dll`, and removing the legacy `SST1INIT.DLL`. |
| **`PANDY.EXE`** *(Retail DirectDraw Software)* | DirectDraw 16-bit Software | No (No 3dfx) | Non-accelerated software renderer (`320×240`, `640×400`, or `640×480` with optional low-texture / flat-shading toggles for 1997 CPUs). Does not use Glide. |

> **Manual Full Install File List (`SETUP.CNF` `[Full3]`):**
> Either run the CD's `SETUP.EXE` with **"Use my 3Dfx accelerator card"** + **Full Install**, or copy `PANDY3.EXE`, `XANLIB.DLL`, `JESTERS.PKG`, `LEVEL21.PKG`, `LEVEL653.PKG`–`LEVEL683.PKG`, `LEVEL75.PKG`–`LEVEL77.PKG`, `LEVEL96.PKG`–`LEVEL97.PKG`, `LOGOS.AVI`, `INTROS.AVI`, and `OUTROS.AVI` into a game directory, then copy **`FULL3.CNF` → `RESOURCE.CNF`**.

---

## Recommended Launch Configurations (Wine32 / Windows)

Copy `glide-ng`'s 32-bit **`glide.dll`** (from `build_release/win32/bin/glide.dll` or `build/win32/bin/glide.dll`) and the desired preset (`glide-ng.ini`) into the game directory alongside `PANDY3.EXE`, and remove the legacy `SST1INIT.DLL`.

### Option A: Authentic Voodoo Graphics (`glide-ng.ini` — Vulkan or OpenGL 3.3 Core) *(Recommended)*

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" \
  wine explorer /desktop=Pandy,800x600 ./PANDY3.EXE
```

To use the Win32 WGL OpenGL 3.3 Core backend instead of Vulkan without editing `glide-ng.ini`:

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" GLIDE_NG_BACKEND=opengl_es \
  wine explorer /desktop=Pandy,800x600 ./PANDY3.EXE
```

### Option B: Best Visuals (`glide-ng.hq.ini` — `4× MSAA` + `1280×960`)

Use [`glide-ng.hq.ini`](./glide-ng.hq.ini) (`msaa = 4`, `presentation_filter = linear`, `no_dither_sub = true`) on a `1280×960` Wine virtual desktop:

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" \
GLIDE_NG_CONFIG=/path/to/glide-ng.hq.ini \
  wine explorer /desktop=Pandy,1280x960 ./PANDY3.EXE
```

### Option C: CPU Software Rasterizer (`glide-ng.software.ini`)

On hosts without a 32-bit hardware Vulkan ICD, cap `LP_NUM_THREADS=4` to preserve 32-bit Wine virtual address space under Mesa `llvmpipe`, or run `glide-ng`'s CPU Software backend directly:

```bash
WINEARCH=win32 WINEPREFIX=/path/to/wineprefix \
WINEDLLOVERRIDES="glide=n" \
GLIDE_NG_CONFIG=/path/to/glide-ng.software.ini \
  wine explorer /desktop=Pandy,800x600 ./PANDY3.EXE
```

---

## Visual Progression Gallery

All captures below are taken from `PANDY3.EXE` running under Wine32 + `glide-ng`'s `glide.dll` (Glide 2.11):

| | |
| :---: | :---: |
| ![Pandemonium Title HQ](./screenshots/01_title_3dfx_hq.webp)<br>**1. Title Screen (`800×600`, Vulkan HQ `4× MSAA`)**<br>Clean pre-2.3 Glide 2.11 logo and menu startup under [`glide-ng.hq.ini`](./glide-ng.hq.ini). | ![Pandemonium World Map](./screenshots/02_worldmap_3dfx.webp)<br>**2. 3D World Map Hub (`640×480`, Vulkan `1×`)**<br>Starting overworld hub island with bilinear texture filtering and character swap. |
| ![Pandemonium Level 1 Vulkan 1x](./screenshots/03_level1_vulkan_1x.webp)<br>**3. Level 1 *Skull Fortress* (`640×480`, Vulkan `1×`, [`glide-ng.ini`](./glide-ng.ini))**<br>Authentic SST-1 `1×` baseline showing bilinear textures, Gouraud shading, and translucent dust/orb effects with native polygon aliasing. | ![Pandemonium Level 1 Vulkan HQ](./screenshots/04_level1_vulkan_hq.webp)<br>**4. Level 1 *Skull Fortress* (`1280×960`, Vulkan HQ `4× MSAA`, [`glide-ng.hq.ini`](./glide-ng.hq.ini))**<br>4× MSAA with linear upscaling eliminates staircase edges across platforms, arches, and character geometry. |
| ![Pandemonium Level 1 GLES](./screenshots/05_level1_gles.webp)<br>**5. Level 1 *Skull Fortress* (`640×480`, OpenGL 3.3 Core WGL)**<br>Matches Vulkan lighting, transparency, and polygon sorting identically. | |

---

## Visual Quality & Performance Tuning

* **Flavour verdict (3dfx Glide vs. Software):** The **3dfx Glide 2.11 (`PANDY3.EXE`)** edition is strongly preferred over the DirectDraw software renderer (`PANDY.EXE`) — it adds hardware bilinear texture filtering, smooth translucent particle/orb blending, and clean **`4× MSAA`** anti-aliasing (`glide-ng.hq.ini`) across all three `glide-ng` backends.
* **Gamma (`1.0` default):** The retail CD's `PANDY3.BAT` explicitly sets `SST_RGAMMA=1.0`, `SST_GGAMMA=1.0`, and `SST_BGAMMA=1.0`. Leaving gamma unset in `glide-ng.ini` preserves the exact linear `1.0` default.
* **Timing & VSync (`30 FPS`):** *Pandemonium!* natively targets **`30 FPS`** (alternating `33 ms` / `34 ms` frame steps). Retail `PANDY3.EXE` calibrates its timer via 32-bit `RDTSC` and can divide-overflow on fast CPUs; community `timeGetTime`-patched executables preserve `30 FPS` pacing natively, whereas common No-CD executables stub out the timer and run uncapped. All bundled presets set `max_fps = 30` with `vsync = true` so gameplay speed stays authentic under both builds.

---

## Quirks, Known Issues & Controls

### Installation & Engine Notes
* **CD-ROM check & `CDPath` registry entry under Wine:** Unmodified `PANDY3.EXE` (including timer-patched builds) checks for the disc by reading the **`CDPath`** string value (`REG_SZ`) first from `HKCU\SOFTWARE\Crystal Dynamics\Pandemonium` (no `\1.00`) and falling back to `HKLM\SOFTWARE\Crystal Dynamics\Pandemonium\1.00`. It concatenates `PANDY.EXE` / `.AVI` directly onto `CDPath` (`CDPath` **must** end with a trailing backslash, e.g. `D:\`), requires `GetDriveTypeA()` to return **`DRIVE_CDROM` (`5`)**, and verifies `PANDY.EXE` exists via `SearchPathA()`. *(Windows MCI `sysinfo cdaudio name 1 wait` is separate and used only for Redbook CD audio music.)*

  With a physical or virtual (`cdemu`) disc mounted under Linux, map the mount root to `d:` (and optionally the block device to `d::` for CD audio) and register `CDPath`:

  ```bash
  export WINEPREFIX=/path/to/wineprefix
  ln -sfn /path/to/cdemu/mount "$WINEPREFIX/dosdevices/d:"
  # Optional, for Redbook CD audio via MCI:
  ln -sfn /dev/sr0 "$WINEPREFIX/dosdevices/d::"

  cat <<'EOF' > /tmp/pandy_cd.reg
  Windows Registry Editor Version 5.00

  [HKEY_LOCAL_MACHINE\Software\Wine\Drives]
  "d:"="cdrom"

  [HKEY_CURRENT_USER\Software\Crystal Dynamics\Pandemonium]
  "CDPath"="D:\\"

  [HKEY_LOCAL_MACHINE\Software\Crystal Dynamics\Pandemonium\1.00]
  "CDPath"="D:\\"
  EOF

  wine regedit /tmp/pandy_cd.reg
  ```

  *(Using a `.reg` file avoids shell/`reg.exe` trailing-backslash quoting quirks; setting `HKCU` also works identically in both 32-bit and 64-bit Wine prefixes.)*
* **Delayed stop after holding a movement key (engine message-pump quirk):** `PANDY3.EXE`'s own Win32 loop calls `if (PeekMessageA(...))` (not `while`), so it drains **at most 1 queued message per rendered frame** (`30 msgs/s` at `30 FPS`). Under modern Linux/Wine typematic key-repeat (`25–33+ Hz`), holding a printable movement key (`A`/`S`) emits both `WM_KEYDOWN` and a `TranslateMessage`-synthesized `WM_CHAR` (**`50–66+ msgs/s`**), backing up the thread queue ahead of `WM_KEYUP` so the character continues running briefly (or for several seconds after a long hold) once you let go. Recommended fixes:
  * **Rebind movement to non-character keys *(Verified)*:** Press **`Esc` → Options → Keyboard** (`Keyboard Setup`) and rebind **Left / Right / Up / Down** to the **Arrow keys** (and optionally avoid printable letters for held actions). Non-character keys do not emit `WM_CHAR` via `TranslateMessage`, cutting queue inflow in half so `WM_KEYUP` drains promptly at `30 FPS`.
  * **Use a joystick / gamepad:** Press **`Esc` → Options → Joystick** (`Joystick Setup`) to map up to 4 buttons (`JOY1`–`JOY4`). *Pandemonium!* polls gamepads directly via WinMM `joyGetPosEx`, bypassing the Win32 message queue entirely. *(Configure/test controllers under Wine with `wine control joy.cpl` if needed.)*
  * **Optional X11 fallback (if keeping `A`/`S`):** Disable or slow keyboard auto-repeat while playing (`xset r off` or `xset r rate 500 10`, restoring with `xset r on` afterward).
* **Native Win32 menu bar (`SHOW_VGA` ↔ `SHOW_SST1`):** Pressing **`Esc`** invokes `grSstPassthruMode(GR_PASSTHRU_SHOW_VGA)` to expose the native Windows menu bar (`Game | Options | Password`). `glide-ng` yields the embedded presentation surface cleanly for GDI dialogs (`Volumes`, `Keyboard Setup`, `Joystick Setup`) and reclaims it automatically when resuming play.
* **Startup splash:** `glide-ng` plays its procedural startup splash once automatically on `grSstOpen()`. Uncomment `no_splash = true` in `glide-ng.ini` (or pass `FX_GLIDE_NO_SPLASH=1`) to skip it.

### Useful Controls
* **Keyboard (`Esc` → Options → Keyboard):** Default bindings are `A` / `S` (Move Left / Right), `PgUp` / `X` (Move Up / Down — also swaps between Nikki and Fargus on the hub island), `Space` (Jump / Enter highlighted level), `Ctrl` (Fire / Attack), `Z` (Special: Nikki double-jump / Fargus spin attack), and `Esc` (Pause & toggle Win32 menu bar). **Rebinding movement to the Arrow keys** is strongly recommended at `30 FPS` to avoid queued `WM_CHAR` input lag when holding directions. Saved under `HKCU\SOFTWARE\Crystal Dynamics\Pandemonium` (`KbdControls`).
* **Joystick / Gamepad (`Esc` → Options → Joystick):** Supported natively through `winmm.dll` (`joyGetPosEx`, axes + `JOY1`–`JOY4`). Saved under `HKCU\SOFTWARE\Crystal Dynamics\Pandemonium` (`JoyControls`). If Wine fails to detect a gamepad, verify it is listed and enabled under `wine control joy.cpl`.
