# Tomb Raider (1996 — 3dfx DOS Rush Patch)

| Field | Value |
| :--- | :--- |
| **Category** | Commercial Game |
| **Year** | 1996 (3dfx Rush patch: June 1997) |
| **Platform(s)** | `DOS/32 (GLIDE2X.OVL via DOSBox Glide pass-through)` |
| **Glide API & Version** | Glide 2.x (`2.48` on Voodoo Graphics / `2.56` on Voodoo2) |
| **Recommended Card Profile** | `VoodooGraphics` (authentic SST-1) · `Voodoo2` (for enhanced preset) |
| **Verified Backends** | Vulkan · OpenGL ES · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Recommended SST-1) · [`glide-ng.hq.ini`](./glide-ng.hq.ini) (4× MSAA + Trilinear) · [`glide-ng.software.ini`](./glide-ng.software.ini) (CPU Software) |
| **Status** | Verified |

---

## Overview

Core Design's 1996 action-adventure landmark *Tomb Raider* received official DOS 3dfx patches in 1997 adding 640×480 perspective-correct texture mapping, W-buffer depth sorting (including near-plane `oow > 1.0` geometry), 8-bit palettized (`GR_TEXFMT_P_8`) textures with persistent palettes across `guTexMemReset()`, optional mipmapping, and line-drawn HUD gauges (`grDrawLine` with depth bias for Lara's health/oxygen bars).

---

## Editions, Patches & Executables

Classic DOS installs and community collections commonly ship up to **three** executables in `TOMBRAID/`:

| Executable | Renderer / Patch | Works with `glide-ng`? | Notes |
| :--- | :--- | :---: | :--- |
| **`TOMBRUSH.EXE`** *(Jun 1997, ~874 KB)* | Official 3dfx Voodoo Rush / Glide 2.x patch | **Yes (Recommended)** | Dynamically loads `GLIDE2X.OVL` at runtime, making it the required binary for DOSBox Glide pass-through + `glide-ng`. Despite the filename, it runs cleanly on `VoodooGraphics` (SST-1) and `Voodoo2` profiles. |
| **`TOMB211.EXE`** *(Feb 1997, ~1.14 MB, from `TR_3DFX.211`)* | Early 3dfx Glide 2.11 SST-1 patch | No *(SW Voodoo only)* | Statically links Glide 2.11 inside the `.exe` and probes SST-1 PCI registers directly — never loads `GLIDE2X.OVL`. Useful only in DOSBox-X / DOSBox-Staging software Voodoo mode. |
| **`TOMB.EXE`** *(~769 KB)* | Pure DOS Software VGA (`320×200`) | No | Software renderer; does not use Glide. |

---

## Recommended DOSBox Configurations

Keep the DOSBox configuration minimal — only non-default overrides are shown below.

### Option A: Patched DOSBox-SVN (Glide Pass-Through → `glide-ng`) *(Recommended)*

```ini
# dosbox-svn.conf (minimal overrides)
[sdl]
# Avoids SDL_UpdateRects SIGSEGV under sdl12-compat -> sdl2-compat -> SDL3 on modern Linux
output = opengl

[cpu]
cycles = max

[glide]
glide = true
```

Launch with the HLE pass-through `GLIDE2X.OVL` on DOS `PATH` (or in `TOMBRAID\`) and the CD image mounted on `D:`:

```bash
LD_LIBRARY_PATH="$PWD/build_release/lib:${LD_LIBRARY_PATH:-}" \
  GLIDE_NG_CONFIG="$PWD/docs/catalog/games/tomb-raider/glide-ng.ini" \
  ./dosbox-svn -conf dosbox-svn.conf \
  -c "imgmount D TOMBENG.cue -t iso" \
  -c "mount C /path/to/TombRaider" \
  -c "C:" \
  -c "SET PATH=C:\\OVLs\\PASS;%PATH%" \
  -c "cd TOMBRAID" \
  -c "TOMBRUSH.EXE"
```

> **Tip (`TOMB.BAT` vs. `[autoexec]`):** Passing a `.BAT` file as a positional argument to DOSBox can bypass `[autoexec]` `SET PATH=...` lines. Either put `SET PATH=...` inside `TOMB.BAT` or pass it via `-c` as above.

### Option B: DOSBox-X (Glide Pass-Through → `glide-ng`)

```ini
# dosbox-x-pass.conf (minimal overrides)
[cpu]
cycles = max

[voodoo]
glide = true
lfb = full
```

Launch with `LD_LIBRARY_PATH` pointing at `build_release/lib` and the HLE pass-through `GLIDE2X.OVL` in `TOMBRAID\` or on DOS `PATH`:

```bash
LD_LIBRARY_PATH="$PWD/build_release/lib:${LD_LIBRARY_PATH:-}" \
  GLIDE_NG_CONFIG="$PWD/docs/catalog/games/tomb-raider/glide-ng.ini" \
  dosbox-x -conf dosbox-x-pass.conf -nopromptfolder \
  -c "imgmount D TOMBENG.cue -t iso" \
  -c "mount C /path/to/TombRaider" \
  -c "C:" \
  -c "SET PATH=C:\\OVLs\\PASS;%PATH%" \
  -c "cd TOMBRAID" \
  -c "TOMBRUSH.EXE"
```

### Reference Comparison: DOSBox-X Software Voodoo (No `glide-ng`)

If comparing against DOSBox-X's built-in low-level SST-1 software emulator:

```ini
# dosbox-x-sw.conf
[cpu]
cycles = max

[voodoo]
voodoo_card = software
glide = false
```

Software Voodoo emulates **Voodoo Graphics (SST-1)** and requires a real **SST-1** `GLIDE2X.OVL` — specifically **Glide 2.42 (SST-1)** (or `2.30`), *not* the Voodoo Rush OVL, *not* Voodoo2 (`2.5x`), *not* `2.43+`/`2.48`, and *not* the HLE pass-through OVL.

---

## Visual Quality & Performance Tuning

### Mipmapping vs. Normal (In-Game Detail Levels)
* Open the Ring Menu → **Detail Levels** and select **`Mipmapping`** (instead of `Normal`).
* **Visual quality:** `Mipmapping` eliminates shimmering and pixel crawling on distant cave walls, floors, and angled surfaces. `glide-ng`'s analytical quotient-rule LOD (Vulkan/GLES) and active-lane edge LOD (Software) keep mipmapped surfaces seamless without dark triangle borders.
* **Performance:** Negligible cost on `glide-ng` (on original Voodoo1 silicon, mipmapping improved texture-cache locality at a ~33% TMU memory cost).

### Anti-Aliasing & Smoothing
* *Tomb Raider* has **no built-in anti-aliasing option** (Detail Levels only toggles `Normal` vs. `Mipmapping`).
* To smooth polygon edges and soften 1 px geometry seams:
  * **Vulkan / OpenGL ES:** Use [`glide-ng.hq.ini`](./glide-ng.hq.ini) (`msaa = 4`, `presentation_filter = linear`, `force_trilinear = true`, `no_dither_sub = true`).
  * **Software backend:** Add `GLIDE_NG_SW_AA=ssaa` (`[software] aa = ssaa`) for 2×2 supersampling, and/or `GLIDE_NG_PRESENTATION_FILTER=linear`.

### Gamma / Brightness
* Authentic SST-1 hardware applied a `1.7` RAMDAC gamma curve. All presets set `gamma = 1.7` so dark cave interiors match original Voodoo1 output. You can also fine-tune brightness inside the game's Ring Menu → **Gamma**.

### Speed & Backend Selection
* **GPU hosts:** `backend = vulkan` (or `opengl_es`) easily sustains locked 60 FPS even with `msaa = 4`.
* **CPU-only hosts / VMs:** [`glide-ng.software.ini`](./glide-ng.software.ini) (`backend = software`, `simd = auto`) uses `glide-ng`'s multi-threaded AVX2/AVX-512 tiled rasterizer, which runs **5.5×–10.1× faster** in wall-clock FPS than DOSBox-X's synchronous software Voodoo emulation across the 9-scene `glide_bench` suite.

---

## Quirks, Known Issues & Controls

### Engine Notes & Quirks
* **Occasional dynamic 1 px vertical black cracks between wall sections:** Core Design's 1996 engine projects all room vertices to integer screen coordinates and clips room portals on integer pixel boundaries, leaving genuine 1-pixel vertical gaps in the submitted Glide vertex stream. These exact same vertical cracks occur on authentic 3dfx SST-1 hardware and DOSBox-X software Voodoo. Enabling `presentation_filter = linear` (included in [`glide-ng.hq.ini`](./glide-ng.hq.ini)) softens them visually.
* **Startup splash:** `glide-ng` plays its procedural startup splash once automatically on `grSstWinOpen()` (matching authentic 3dfx Glide). Set `no_splash = true` in `glide-ng.ini` (or `FX_GLIDE_NO_SPLASH=1`) to skip straight to the intro.
* **DOSBox-SVN `SIGSEGV` on Linux:** Always set `[sdl] output = opengl` (not under `[glide]`) when running `dosbox-svn` on systems using `sdl12-compat` → `sdl2-compat` → `SDL3`, otherwise VGA mode switches during the Eidos/Core FMV intro crash in `SDL_UpdateRects()`.

### Useful Controls
* `Arrow keys` — Move / Turn · `Shift` — Walk (no ledge drop) · `Alt` — Jump · `Ctrl` — Action / Fire · `Space` — Draw / Holster weapons · `Esc` — Ring Menu / Inventory · `F1`–`F6` — Quick menu shortcuts
