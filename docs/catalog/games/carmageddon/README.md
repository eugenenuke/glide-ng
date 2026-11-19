# Carmageddon (3dfx Voodoo / Voodoo2 DOS Patches)

| Field | Value |
| :--- | :--- |
| **Category** | Commercial Game |
| **Year** | 1997 (Voodoo2 patch: 1998) |
| **Platform(s)** | `DOS/32 (GLIDE2X.OVL via DOSBox Glide passthrough)` |
| **Glide API & Version** | Glide 2.x (`2.56` on Voodoo2 / `2.48` on Voodoo Graphics) |
| **Recommended Card Profile** | `Voodoo2` (default, for `Voodo2C.exe` / `voodo2S.exe`) · `VoodooGraphics` (for `CARMAV.EXE`) |
| **Verified Backends** | Vulkan · OpenGL ES · Software (AVX2 / AVX-512 / Scalar) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Voodoo2) · [`glide-ng.voodoo1.ini`](./glide-ng.voodoo1.ini) (Voodoo1 SST-1) |
| **Status** | Verified |

---

## Overview

Stainless Software's 1997 vehicular combat classic *Carmageddon* received DOS-only 3dfx Glide 2.x patches that upgrade the BRender software engine to hardware-accelerated 640×480 texture-mapped rendering with W-buffer depth, `GR_TEXFMT_P_8` palettized textures, and alpha-blended skies/glass.

Because the engine uses persistent `grLfbLock(GR_LFB_WRITE_ONLY)` overlays for menus, race briefings, and the in-car Pratcam, *Carmageddon* is a flagship stress test for `glide-ng`'s LFB staging, 8-bit palette upload, and DOSBox `GLIDE2X.OVL` passthrough pipeline.

---

## Editions, Patches & Executables

Full releases (especially *Carmageddon Max Pack* and GOG/CD archives with community patches applied) commonly contain up to **four** executables in the game directory. Pick the right one for your edition:

| Executable | Renderer / Patch | Recommended? | Target Edition & Notes |
| :--- | :--- | :---: | :--- |
| **`Voodo2C.exe`** | June 1998 Stainless unofficial Voodoo2 patch | **Yes (Base / Max Pack)** | Best executable for *Carmageddon* and *Max Pack* under `glide-ng`'s default `Voodoo2` profile. Dynamically loads `GLIDE2X.OVL` (129 Glide 2.x imports). Requires the Final Beta patch data files (already bundled in *Max Pack*). |
| **`voodo2S.exe`** | June 1998 Stainless unofficial Voodoo2 patch | **Yes (Splat Pack)** | Same Voodoo2 Glide 2.x patch built specifically for the *Splat Pack* expansion. |
| **`CARMAV.EXE`** | Oct 1997 "Final Beta" Voodoo1 / Rush patch (`CARV24ROW.ZIP`) | **Alternative (SST-1)** | Original Glide 2.x 3dfx patch targeting Voodoo Graphics (SST-1) and Voodoo Rush (`-vgraphics` / `-vrush`). Runs cleanly on `glide-ng` with either `Voodoo2` or [`glide-ng.voodoo1.ini`](./glide-ng.voodoo1.ini). |
| **`CARMA.EXE`** | Pure DOS Software / VESA (`640×480×8`) | No (No 3dfx) | Pure software renderer — contains no 3dfx/Glide code and will not load `glide-ng`. |

> **Patch Order for Base CD Installs (Non-Max Pack)**:
> 1. Perform a full DOS installation of *Carmageddon* (or *Splat Pack*).
> 2. Extract the official **Final Beta 3dfx patch** (`CARV24ROW.ZIP` / `CARMAV.EXE` + data files) into the game root, preserving subdirectories.
> 3. Copy **`Voodo2C.exe`** (base / Max Pack) or **`voodo2S.exe`** (Splat Pack) into the game root.

---

## Setup & Launch (DOSBox + `GLIDE2X.OVL`)

1. Ensure your DOSBox build has Glide passthrough enabled (`[glide] glide=true`, `grport=600`, `lfb=full_noaux` or `full`) and resolves `glide-ng`'s `libglide2x.so` (installed system-wide, or via `LD_LIBRARY_PATH=/path/to/glide-ng/lib` when running from a local build; see [Building & Installing](../../../user-guide/building-and-installing.md)).
2. Place a compatible DOSBox `GLIDE2X.OVL` in the mounted DOS directory (or on the DOS `PATH`).
3. Copy [`glide-ng.ini`](./glide-ng.ini) into the host directory from which you launch DOSBox (or pass `GLIDE_NG_CONFIG=/path/to/glide-ng.ini`).
4. Launch DOSBox and run the Glide executable:

```bash
# Host launch (Linux, pointing DOSBox at glide-ng's libglide2x.so and preset)
GLIDE_NG_CONFIG="$(pwd)/docs/catalog/games/carmageddon/glide-ng.ini" \
  dosbox -c "mount c /path/to/carmageddon" -c "c:" -c "Voodo2C.exe"
```

To test the authentic 1997 SST-1 (`VoodooGraphics`, 4 MB FB / 1×2 MB TMU) path with `CARMAV.EXE`, swap in [`glide-ng.voodoo1.ini`](./glide-ng.voodoo1.ini):

```bash
GLIDE_NG_CONFIG="$(pwd)/docs/catalog/games/carmageddon/glide-ng.voodoo1.ini" \
  dosbox -c "mount c /path/to/carmageddon" -c "c:" -c "CARMAV.EXE"
```

---

## Quirks, Known Issues & Controls

### Engine Notes & Quirks
* **Persistent LFB writes (`GR_LFB_WRITE_ONLY`)**: Menus, car select screens, and the in-car Pratcam write directly into the 16-bit RGB565 framebuffer while 3D rendering is active. `glide-ng` handles this cleanly across Vulkan, OpenGL ES, and Software with full menu/HUD visibility and zero skybox/seam artifacts.
* **`P_8` palette uploads**: The game streams 256-color `GR_TEXFMT_P_8` palettes during transitions and lighting changes; no special override is needed.
* **3dfx startup splash**: Enabled by default; set `no_splash = true` (already enabled in [`glide-ng.ini`](./glide-ng.ini)) to jump straight into the game intro.
* **Useful CLI Switches** (supported by `CARMAV.EXE`, `Voodo2C.exe`, and `voodo2S.exe`):
  * `-vgraphics` — Force Voodoo Graphics mode.
  * `-vrush` — Force Voodoo Rush (or Voodoo2 in `Voodo2C.exe` / `voodo2S.exe`) mode.
  * `-nosound` — Disable DOS sound hardware probing if debugging audio/IRQ issues.
  * `-nocutscenes` — Skip intro FMV cutscenes for faster startup.

### Useful Controls
* Arrow keys — Drive / Steer · `Space` — Handbrake · `Backspace` — Recover car · `C` — Cycle camera · `Tab` — Map · `F1` — Cockpit / Pratcam toggle · `Esc` — Menu / Quit
