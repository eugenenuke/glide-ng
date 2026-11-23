# Quick Triage Table (`glide-ng` Boot, Audio, Visual & WSI Fixes)

Load this reference **only** when a game fails to launch, crashes, freezes, renders blank/wrong, or has window/input quirks. Keep testing to **one knob at a time** so `log_summary.md` isolates the exact fix.

> All `GLIDE_NG_*` / `FX_GLIDE_*` / `SST*` variable names below match `include/glide_ng/env_vars.h` and `glide-ng.ini.example`.

---

## 1. Startup, Detection & Linkage Issues

| Symptom | Likely Cause | Fix / Probe |
| :--- | :--- | :--- |
| No `glide-ng.log` written at all | Wrong DLL/OVL loaded, Wine loading builtin/OEM DLL, or statically linked DOS binary | **Wine:** `WINEDLLOVERRIDES="glide,glide2x,glide3x=n"`. Move/rename any stale `glide*.dll` or `nGlide` copies inside the game folder or `Drivers/`/`System/`. **DOSBox:** verify `[glide]` / `glide=true` (or `glide=emu`) and drop `glide-ng`'s `GLIDE2X.OVL` inside the game folder. |
| `glide_recon.py` reports **static `sst1Init`** DOS binary (no `GLIDE2X.OVL` string) | Early 1996–1997 DOS build with Glide statically linked inside the `.exe` | Check for a newer official/fan patch using `GLIDE2X.OVL` (e.g., *Descent II* Release 6, *Tomb Raider* 3dfx, *Blood* dynamic). Otherwise requires low-level MMIO/register emulation (DOSBox-X/PCem), not a high-level Glide OVL wrapper. |
| Wrong version banner inside `glide-ng.log` (`Wrapper Build` mismatch) | Stale system library (`libglide2x.so` / `glide2x.dll`) shadowing updated build | Confirm `Loaded Module` path in the log banner (`summarize_log.py`). Check `LD_LIBRARY_PATH` (Linux) or remove older DLL copies. |
| `"3dfx card not found"` / `"Unsupported Voodoo board"` | Game probes exact `grSstQueryHardware()` board/type or old API version | Probe **`GLIDE_NG_CARD_MODEL=voodoo1`** (`VoodooGraphics`, API `2.48`), **`voodoo2`** (`2.56`), **`voodoo3`**, or **`voodoo5`** (`3.10`). On Voodoo2 checks that reject SST-1 fallback, set **`FX_GLIDE_REPORT_REAL_HW=1`** *(note: authentic 3dfx `!= NULL` rule — **unset** completely to disable; `=0` still enables!)*. |
| Game asks for **Glide 2.11** (`glide.dll` / `libglide.so`) | Early 1996–1997 title importing `glide.dll` rather than `glide2x.dll` | Place `glide-ng`'s `glide.dll` beside the executable and override `WINEDLLOVERRIDES="glide=n"`. Pair with `card_model = voodoo1` and `api_version = 2.11` (or `2.43`). |
| Game asks for **`Banshee`/`Rush`** quirks (`grSstQueryHardware` Rush ID) | Older title expecting SST-96 | Try `GLIDE_NG_CARD_MODEL=rush` or **`FX_GLIDE_EMUL_RUSH=1`**. |
| Game opens and immediately closes/re-opens the window cleanly mid-startup or crashes on 2nd `grSstWinOpen` | Redundant reopen inside same process | Test **`FX_GLIDE_IGNORE_REOPEN=1`** (or INI `ignore_reopen = true`). |
| Game detects too much FBI/TMU RAM and overflows signed 16/32-bit int checks | Classic DOS/Win32 binary expects 2–4 MB FBI/TMU memory | Lower memory caps: **`GLIDE_NG_FBI_MEM_MB=4`**, **`GLIDE_NG_TMU_MEM_MB=4`**, **`GLIDE_NG_TMU_COUNT=1`** (or `2`). |
| **Wine / DirectDraw black/crash** before Glide initializes | Wine's OpenGL/Vulkan D3D shim colliding with the embedded Glide window | In the Wine prefix registry (`wine regedit`), set `HKCU\Software\Wine\Direct3D\renderer` = **`no3d`** (disables hardware D3D on older DirectDraw menus; note exact impact in `NOTES.md`). |
| **DOSBox crash / DPMI page fault / OOM** on startup | Extender (`DOS/4GW`, `CauseWay`, `DOS/32A`) conflict with EMS or >16–32 MB RAM | Inside `dosbox.conf`: set `memsize=16` (or `32`), **`ems=false`**, `xms=true`. If `DOS/4GW` crashes or stalls, test swapping in `DOS/32A` cleanly on a separate run without overwriting the original `.exe`. |

---

## 2. Black Screen, Stale Menus, Windowing & Fullscreen

| Symptom | Likely Cause | Fix / Probe |
| :--- | :--- | :--- |
| Blank/black menu that only appears after pressing a key or moving the mouse | Direct front-buffer writes without `grBufferSwap` | Record a lightweight front-buffer log (`GLIDE_NG_LOG_LEVEL=trace GLIDE_NG_LOG_CATEGORIES=lfb,wsi,api`) and test across **`vulkan`**, **`opengl_es`**, and **`software`** to verify `FrontBufferFlushTimer` / swap cadence. |
| Black screen with audio playing (`Wayland` / `X11`) | Compositor surface mismatch or EGL/Vulkan host window loss | Test `GLIDE_NG_FULLSCREEN=0` (windowed), then try **`SDL_VIDEODRIVER=x11`** (under Wayland/XWayland for DOSBox). Verify with `software` backend to isolate WSI vs rendering. |
| Wine fullscreen grabs/misaligns, loses focus on `Alt+Tab`, or stretches awkwardly | Unmanaged host fullscreen or desktop scaling | Launch inside a virtual desktop: `wine explorer /desktop=Game,800x600 ./GAME.EXE` (or `--wine-desktop 800x600` in `gen_launchers.py`), and test **`GLIDE_NG_FULLSCREEN=0`**. |
| `Alt+Tab` or DOSBox resolution switch hangs | Video mode transitions inside strict FIFO vsync | Try `GLIDE_NG_VSYNC=false` with `GLIDE_NG_MAX_FPS=60` (and inspect a `GLIDE_NG_LOG_CATEGORIES=wsi,general` debug log). |

---

## 3. Visual Glitches, Gamma & Quick A/B Probes

Run these **three cheap yes/no toggles** right away and include the answers in the bug report:

1. **Backend A/B:** Compare `vulkan` (`msaa=1`) vs **`opengl_es`** vs **`software`** (reference rasterizer).
   - Wrong on **one** GPU backend only → backend bug (shader/state/MSAA/sync).
   - Wrong on **all three** (including `software`) → shared Glide core / app state / vertex layout bug.
2. **Texture bypass:** **`SST_TEXMAP_DISABLE=1`** — checks whether the artifact comes from texture upload/filtering/chroma-key vs vertex geometry/combiner color.
3. **Bounds check:** **`FX_GLIDE_BOUNDS_CHECK=1`** — checks `glide-ng.log` for `[WARN] [BoundsCheck]` (`NaN`, `Inf`, huge `x/y`, or bad `oow`).

| Symptom | Likely Cause | Fix / Probe |
| :--- | :--- | :--- |
| Scene is too dark / washed out | Authentic CRT gamma difference or ignored app gamma ramp | Tune **`SST_GAMMA=1.7`** (typical classic 3dfx desktop value, try `1.3`–`1.7`). To ignore broken app gamma ramps: **`FX_GLIDE_USE_APP_GAMMA=0`**. |
| Bright seams / thin tile cracks around UI or 2D sprites on `msaa=4` | MSAA edge sampling against non-coverage-padded 2D UI quads | Switch to **`GLIDE_NG_MSAA=1`** and **`GLIDE_NG_PRESENTATION_FILTER=nearest`** to verify seams disappear. |
| Posterized 16-bit banding in skyboxes / fog | Authentic 16-bit `RGB565` sub-dither emulation | Try **`FX_GLIDE_NO_DITHER_SUB=1`** (clean color precision on Vulkan/GLES HQ preset). |
| Blurry pixel art / fuzzy HUD text | Linear upscaling or forced bilinear | Set **`GLIDE_NG_PRESENTATION_FILTER=nearest`** (and avoid `SSTV2_FORCE_BILINEAR=1`). |
| Tearing / runaway physics / broken cutscenes (`>1000 FPS`) | Uncapped frame rate on fast modern host | Cap with **`GLIDE_NG_MAX_FPS=60`** (or `30` for specific titles) and `GLIDE_NG_VSYNC=true`. |
| Missing `depth` / Z-fighting or excessive sorting flicker | Missing aux buffer or triple-buffer expectation | Check `FX_GLIDE_ALLOC_AUX=1`, `FX_GLIDE_ALLOC_COLOR=2` (or `3` / `SST_TRIPLE_BUFFER=1`). |
| Wrong/inverted palette or garbled textures (`Glide3` VSA-100 titles) | Combined texture UMA mode check | Probe **`FX_GLIDE_TEXTURE_UMA=0`** vs **`1`** under `card_model = voodoo5`. |

---

## 4. Crashes, Hangs & Audio

| Symptom | First Triage Check |
| :--- | :--- |
| Crash or silent exit before/after splash | Add **`GLIDE_NG_NO_SPLASH=1`** to confirm splash isn't crashing; collect **`run_debug_capture`** (`GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1`). On Wine, keep `WINEDEBUG=err+all,fixme-all,+seh,+loaddll`. |
| Sound glitches / hangs inside sound driver | Temporarily launch with the game's **`-nosound`** / disabled audio flag (`recon` text hints) to verify whether the hang is audio-driver or Glide related. |
| Hung loading screen (5–10 s wait) | Note **CPU usage** (`~0%` = deadlock/wait vs `100%` = spin loop), **audio** (silent vs looping vs buzzing), and whether `Esc` / `Alt+Tab` responds. Never kill before grabbing `gdb` thread stacks in **Deep** mode! |
| Crash with Vulkan only (`opengl_es` and `software` OK) | Re-run with **`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`** (needs distro `vulkan-validationlayers` package). |
