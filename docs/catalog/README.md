# glide-ng Game, Demo & Benchmark Compatibility Catalog

A curated per-title catalog of real-world 3dfx Glide games, official 3dfx tech demos, hardware test utilities, and `glide-ng` diagnostic/benchmark tools — each with its own verified **compatibility guide** and **drop-in `glide-ng.ini` preset**.

> **Organization**: Titles are grouped by kind (`games/`, `demos/`, `tools/`) rather than split into separate DOS vs. Windows trees, because many classic titles exist in both DOS and Win32 editions (or ship both Glide 2.x and Glide 3.x binaries). Platform badges (**DOS/32**, **Win32 / Wine**, **Linux**) are shown in every table and title card, and multi-variant titles keep alternate presets (`glide-ng.<variant>.ini`) alongside the primary `glide-ng.ini` in the same folder.

---

## How Drop-In `glide-ng.ini` Presets Work

1. **Copy the title's `glide-ng.ini`** (from its catalog directory below) into the same folder as the game or demo executable — or point to it directly without copying via `GLIDE_NG_CONFIG=/path/to/glide-ng.ini`.
2. **Ensure `glide-ng` is available**:
   - **Linux / DOSBox pass-through**: Install `glide-ng` system-wide (no loader overrides needed), or set `LD_LIBRARY_PATH=/path/to/glide-ng/lib` when running directly from a local build directory (see [Building & Installing](../user-guide/building-and-installing.md)).
   - **Win32 / Wine / Proton**: Copy `glide-ng`'s `glide.dll` (Glide 2.11), `glide2x.dll`, and/or `glide3x.dll` next to the `.exe` and pass `WINEDLLOVERRIDES="glide,glide2x,glide3x=n"`.
3. **Override on the fly**: Configuration precedence is always (lowest → highest):
   `Built-in defaults` < `glide-ng.ini` < `Authentic 3dfx env vars (FX_GLIDE_* / SST_* / SSTV2_*)` < `GLIDE_NG_* env vars`.
   For example, `GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512` overrides `[core] backend` and `[software] simd` from any preset without editing the file.

See [Configuration & Environment Variables](../user-guide/configuration.md), [Hardware Profiles & Glide Versions](../user-guide/hardware-and-versions.md), and [`glide-ng.ini.example`](../../glide-ng.ini.example) for the complete reference of supported keys.

---

## 1. Commercial Games (`games/`)

| Title | Year | Platform(s) | Glide API | Card Profile | Backends (VK / GL / SW) | Preset | Key Quirk / Recommended Build | Status |
| :--- | :---: | :--- | :---: | :--- | :---: | :--- | :--- | :---: |
| **[Archimedean Dynasty](./games/archimedean-dynasty/README.md)** | 1996 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo1 (`2.48`) / Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/archimedean-dynasty/glide-ng.ini) · [`hq`](./games/archimedean-dynasty/glide-ng.hq.ini) · [`voodoo2`](./games/archimedean-dynasty/glide-ng.voodoo2.ini) · [`software`](./games/archimedean-dynasty/glide-ng.software.ini) | Apply Oct 1997 `AD_3Dfx.exe` patch (`AD3DFX.EXE` / `FIGHT3.EXE solo`); **set `[dosbox] memsize = 32`**; replace stock `GLIDE2X.OVL` with `build/dos/glide2x.ovl`; `Shift+F3` cycles fog gamma | Verified |
| **[Blood](./games/blood/README.md)** | 1997 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo1 (`2.48`) / Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/blood/glide-ng.ini) · [`software`](./games/blood/glide-ng.software.ini) | Use `3DFX.EXE` with `glide-ng`'s `build/dos/glide2x.ovl`; **set `[dosbox] memsize = 63`** (allocates 52 MB); set `BUILD_640X480=1` & `BUILD_GAMMA=1`; **keep `msaa = 1`** | Verified |
| **[Carmageddon](./games/carmageddon/README.md)** | 1997 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo2 (`2.56`) / Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/carmageddon/glide-ng.ini) · [`hq`](./games/carmageddon/glide-ng.hq.ini) · [`voodoo1`](./games/carmageddon/glide-ng.voodoo1.ini) · [`software`](./games/carmageddon/glide-ng.software.ini) | Use `Voodo2C.exe` (`voodo2S.exe` for Splat Pack) or `CARMAV.EXE`; **set `[dosbox] memsize = 32`** to avoid low-memory Austere Mode | Verified |
| **[CyberGladiators](./games/cybergladiators/README.md)** | 1996 | Win32 / Wine (`glide.dll`) | **Glide 2.11** | Voodoo1 (`2.11`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/cybergladiators/glide-ng.ini) · [`hq`](./games/cybergladiators/glide-ng.hq.ini) · [`software`](./games/cybergladiators/glide-ng.software.ini) | Drop `glide.dll` next to `CYBER.EXE` (`WINEDLLOVERRIDES="glide=n"`); use `/desktop=CG,800x600` (not `640x480`) to avoid title-bar bottom clipping; set **`Ctrl+O` → `Motion: smooth`** | Verified |
| **[Grand Theft Auto](./games/gta/README.md)** | 1997 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo2 (`2.56`) / Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/gta/glide-ng.ini) · [`hq`](./games/gta/glide-ng.hq.ini) · [`voodoo1`](./games/gta/glide-ng.voodoo1.ini) · [`software`](./games/gta/glide-ng.software.ini) | Use DOS `GTAFX.EXE` (Win95 build has no 3dfx); **set `[dosbox] memsize = 32`**; press **`F11`** for `800×600×16`; keep **`F8`** Frame Rate Limiter ON | Verified |
| **[Hitman: Codename 47](./games/hitman-codename-47/README.md)** | 2000 | Win32 / Wine (`glide3x.dll`) | **Glide 3.x** | Voodoo5 (`3.10`) / Voodoo3 (`3.10`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/hitman-codename-47/glide-ng.ini) · [`hq`](./games/hitman-codename-47/glide-ng.hq.ini) · [`voodoo3`](./games/hitman-codename-47/glide-ng.voodoo3.ini) · [`software`](./games/hitman-codename-47/glide-ng.software.ini) | Set `DrawDll Render3DFX.dll` in `Hitman.ini` and replace any bundled `glide3x.dll` (`WINEDLLOVERRIDES="glide3x=n"`); caps at `1024×768` (**`4× MSAA`** HQ beats stock D3D/OpenGL) | Verified |
| **[Lands of Lore: Guardians of Destiny](./games/lands-of-lore-2/README.md)** | 1997 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo1 (`2.48`) / Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/lands-of-lore-2/glide-ng.ini) · [`hq`](./games/lands-of-lore-2/glide-ng.hq.ini) · [`voodoo2`](./games/lands-of-lore-2/glide-ng.voodoo2.ini) · [`software`](./games/lands-of-lore-2/glide-ng.software.ini) | Apply `1.30e` patch (`LOLG.EXE` + `GLIDE2X.OVL`, Win95 build has no Glide); **set `[dosbox] memsize = 63`**; keep `Use 4444 = No`; mount all 4 ISOs (`Ctrl+F4` swaps discs) | Verified |
| **[MechWarrior 2: 31st Century Combat (Titanium Edition)](./games/mechwarrior-2/README.md)** | 1998 | Win32 / Wine (`glide2x.dll`) | Glide 2.x | Voodoo2 (`2.56`) / Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/mechwarrior-2/glide-ng.ini) · [`hq`](./games/mechwarrior-2/glide-ng.hq.ini) · [`voodoo1`](./games/mechwarrior-2/glide-ng.voodoo1.ini) · [`software`](./games/mechwarrior-2/glide-ng.software.ini) | Place `glide2x.dll` in game root (**not** inside `plugin/`); select `Options → Video Options → 3Dfx` (`zglide.dll`); **beats Wine `dx5draw.dll` D3D5**; see Wine `WAIL32.DLL` timer note | Verified |
| **[Pandemonium!](./games/pandemonium/README.md)** | 1997 | Win32 / Wine (`glide.dll`) | **Glide 2.11** | Voodoo1 (`2.11`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/pandemonium/glide-ng.ini) · [`hq`](./games/pandemonium/glide-ng.hq.ini) · [`software`](./games/pandemonium/glide-ng.software.ini) | Use `PANDY3.EXE` with `FULL3.CNF` → `RESOURCE.CNF` and `glide-ng`'s `glide.dll` (`WINEDLLOVERRIDES="glide=n"`); **`Esc`** toggles Win32 menu via `SHOW_VGA`; default move keys **`A`/`S`/`PgUp`/`X`** | Verified |
| **[The Elder Scrolls Adventures: Redguard](./games/redguard/README.md)** | 1998 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo2 (`2.56`) / Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/redguard/glide-ng.ini) · [`hq`](./games/redguard/glide-ng.hq.ini) · [`voodoo1`](./games/redguard/glide-ng.voodoo1.ini) · [`software`](./games/redguard/glide-ng.software.ini) | Unmodified retail `RGFX.EXE` works with `build/dos/glide2x.ovl`; **mount `C:` with `-freesize 512`** & **`memsize = 63`**; set `[3dfx] resolution=8` in `SYSTEM.INI` for native `800×600` (`4× MSAA` clean) | Verified |
| **[Tomb Raider](./games/tomb-raider/README.md)** | 1996 | DOS/32 (`GLIDE2X.OVL`) | Glide 2.x | Voodoo1 (`2.48`) / Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./games/tomb-raider/glide-ng.ini) · [`hq`](./games/tomb-raider/glide-ng.hq.ini) · [`software`](./games/tomb-raider/glide-ng.software.ini) | Use the Jun 1997 3dfx Rush patch (`TOMB.EXE`, loads `GLIDE2X.OVL`); keep in-game `Detail Levels` on `Normal` and native gamma; set `[sdl] output = opengl` in DOSBox-SVN | Verified |

---

## 2. Official 3dfx Tech Demos & Hardware Tests (`demos/`)

> **Obtaining 3dfx Demos & ATB Libraries**: Third-party 3dfx executables are not bundled in this repository. Official 3dfx tech demos and ATB runtime libraries can be found at [3dfx.retropc.se — 3dfx Artwork & Tech Demos](https://3dfx.retropc.se/artwork.html).

| Title | Year | Platform(s) | Glide API | Card Profile | Backends (VK / GL / SW) | Preset | Key Quirk / Highlights | Status |
| :--- | :---: | :--- | :---: | :--- | :---: | :--- | :--- | :---: |
| **[3dfx Donut](./demos/3dfx-donut/README.md)** (`donut.exe`) | 1998 | Win32 / Wine | Glide 2.x | Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-donut/glide-ng.ini) | Out-of-the-box (`gamma = 1.0`); dynamic reflection partial mipmaps + W-buffer | Verified |
| **[3dfx Valley of Ra](./demos/3dfx-valley-of-ra/README.md)** (`fight.exe`) | 1997 | Win32 / Wine (ATB) | Glide 2.x | Voodoo2 (`2.56`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-valley-of-ra/glide-ng.ini) | Requires 3dfx ATB runtime DLLs; `GR_DITHER_2x2` cutouts; `gamma = 1.0` | Verified |
| **[3dfx Demo Room](./demos/3dfx-demo-room/README.md)** (`demoroom.exe`) | 1997 | Win32 / Wine (ATB) | Glide 2.x | Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-demo-room/glide-ng.ini) | **`gamma = 1.7`** (SST-1 RAMDAC curve); press `Space` to cycle projected lights | Verified |
| **[3dfx Wizard's Tower](./demos/3dfx-wizards-tower/README.md)** (`wiz.exe`) | 1997 | Win32 / Wine (ATB) | Glide 2.x | Voodoo1 (`2.48`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-wizards-tower/glide-ng.ini) | **`gamma = 1.7`** for Room 1 baked lightmaps; avoid `Enter` (`app.evt` playback) | Verified |
| **[3dfx Surf](./demos/3dfx-surf/README.md)** (`surf_glide2x.exe` / `surf_glide3x.exe`) | 1998 | Win32 / Wine | **Glide 2.x & 3.x** | Voodoo2 (`2.56` / `3.03`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-surf/glide-ng.ini) | Dual Glide 2/3 showcase (`1024×768`, 2-TMU env map, dynamic LOD) | Verified |
| **[3dfx Kitty Hawk](./demos/3dfx-kitty-hawk/README.md)** (`aademo.exe`) | 2000 | Win32 / Wine | Glide 3.x | **Voodoo5** (`3.10`) / Voodoo4 | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-kitty-hawk/glide-ng.ini) · [`voodoo4`](./demos/3dfx-kitty-hawk/glide-ng.voodoo4.ini) | **4× Spatial AA & T-Buffer** (`F2`/`F9`); pass `-model 'data\kittyhawk\kittyhawk.gxf'` | Verified |
| **[3dfx Museum](./demos/3dfx-museum/README.md)** (`museum.exe`) | 1999 | Win32 / Wine | Glide 3.x | **Voodoo5** (`3.10`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-museum/glide-ng.ini) | **4× AA + Soft Reflectance** (`Up` raises blur); skip `go.bat` (`distre~1` 8.3 path) | Verified |
| **[3dfx Rocket Burger](./demos/3dfx-rocket-burger/README.md)** (`RocketBurger.exe`) | 1999 | Win32 / Wine | Glide 3.x | **Voodoo5** (`3.10`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/3dfx-rocket-burger/glide-ng.ini) | **4× AA + DoF (`F4`, frames 140–364) + Motion Blur (`F3`, 365–700)**; skip `go.bat` | Verified |
| **[Diamond Monster 3D Test](./demos/monster3d-test/README.md)** (`MNSTRTST.EXE`) | 1997 | Win32 / Wine | Glide 2.x | Voodoo1 (`2.46`) | Pass / Pass / Pass | [`glide-ng.ini`](./demos/monster3d-test/glide-ng.ini) | Click **OK** on startup dialog, `Esc` to quit; do not confuse with `MOJO.EXE` (`sst1init.dll`) | Verified |

---

## 3. Benchmarking, Diagnostic & Verification Tools (`tools/`)

| Tool | Platform(s) | Glide API | Preset(s) | Purpose & Highlights |
| :--- | :--- | :---: | :--- | :--- |
| **[`glide2_bench` / `glide3_bench`](./tools/glide-bench/README.md)** | Linux · Win32 / Wine · DOS/32 | Glide 2.x & 3.x | [`glide-ng.ini`](./tools/glide-bench/glide-ng.ini) · [`voodoo5`](./tools/glide-bench/glide-ng.voodoo5.ini) | 9 Glide 2.x + 14 Glide 3.x VSA-100 scenes, Software SIMD (`scalar`/`avx2`/`avx512`) scaling, and **Mesa `llvmpipe` (Vulkan & OpenGL ES) vs. `glide-ng` Software** benchmark recipes |
| **[`glide2_info` / `glide3_info`](./tools/glide-info/README.md)** | Linux · Win32 / Wine · DOS/32 | Glide 2.x & 3.x | [`glide-ng.ini`](./tools/glide-info/glide-ng.ini) | Emulated board/API inspection, extension vs. `grGetProcAddress` verification (`-v`), and active resolution/LFB/TMU hardware probe (`--probe`) |
| **[`glide2_envtest` / `glide3_envtest`](./tools/glide-envtest/README.md)** | Linux · Win32 / Wine · DOS/32 | Glide 2.x & 3.x | [`glide-ng.ini`](./tools/glide-envtest/glide-ng.ini) | 4-quadrant visual test card + automated RGB565 LFB verifier (`--verify --test-reopen --test-bounds`) for INI & authentic 3dfx environment variables |

---

## 4. Common Cross-Title Quirk Recipes

| Symptom / Scenario | Recommended Setting (`glide-ng.ini` or Env) | Why It Works |
| :--- | :--- | :--- |
| **1996–1997 Voodoo1 title looks too dark** (*Demo Room*, *Wizard's Tower*, *GLQuake*, *Tomb Raider*) | `[glide]` → `gamma = 1.7` (or `SST_GAMMA=1.7`) | Authentic Voodoo Graphics (SST-1) `grSstWinOpen` defaulted its RAMDAC LUT to `1.7` (Voodoo2 CVG used `1.3`). `glide-ng` defaults to linear `1.0` so modern and brighter titles are not washed out. |
| **1998 Voodoo2 title looks slightly dark** | `[glide]` → `gamma = 1.3` (or `SSTV2_GAMMA=1.3`) | Matches authentic Voodoo2 (`CVG`) hardware default RAMDAC curve. |
| **Early 1996–1997 title misdetects 2 TMUs or >4 MB VRAM** | `[hardware]` → `card_model = VoodooGraphics` (or `GLIDE_NG_CARD_MODEL=voodoo1`) | Automatically sets 4 MB FB, 1×2 MB TMU, and Glide `2.48` / `3.04` single-TMU SST-1 capabilities. |
| **Voodoo4/5 (`VSA-100`) demo checks `PIXEXT` for FSAA / T-Buffer effects** (*Kitty Hawk*, *Museum*, *Rocket Burger*) | `[hardware]` → `card_model = Voodoo5` (or `GLIDE_NG_CARD_MODEL=voodoo5`) | Advertises `PIXEXT`, `COMBINE`, and `TEXFMT` in `GR_EXTENSION`, enabling `grSstWinOpenExt`, `grTBufferWriteMaskExt`, 4× Spatial AA, Soft Reflectance, Motion Blur, and Depth of Field across all 3 backends. |
| **Force 4× anti-aliasing on any Glide 2.x or 3.x title** | `[display]` → `msaa = 4` (or `GLIDE_NG_MSAA=4`) | Activates 4× MSAA on Vulkan and OpenGL ES/WGL (offscreen MSAA FBOs) and 2×2 SSAA on the Software backend. |
| **Enable AVX-512 Software rasterization on Zen 4 / AVX-512 CPUs** | `[software]` → `simd = avx512` (or `GLIDE_NG_SW_SIMD=avx512` / `auto`) | Uses the 4×4 (16-pixel) AVX-512 rasterizer and 13 AVX-512 CPU kernels (+20–50% over default AVX2 on Zen 4; safely falls back to AVX2/Scalar on CPUs without AVX-512). |
| **Game calls `grSstWinOpen` twice without closing / flickers on reopen** | `[glide]` → `ignore_reopen = true` (or `FX_GLIDE_IGNORE_REOPEN=1`) | Suppresses redundant window teardown/recreation while active. |
| **16-bit dither banding visible on smooth gradients** | `[glide]` → `no_dither_sub = true` (or `FX_GLIDE_NO_DITHER_SUB=1`) | Keeps full 24-bit color precision without 16-bit dither subtraction. |
| **Point-sampled textures desired to be smoother** | `[glide]` → `force_bilinear = true` or `force_trilinear = true` | Overrides app's `GR_TEXTUREFILTER_POINT_SAMPLED` to bilinear/trilinear filtering. |
| **High-DPI / multi-monitor fullscreen Win32 demo opens awkwardly under Wine** | `wine explorer /desktop=Glide,1024x768 ./game.exe` (or `1600x1200` on 2× HiDPI; avoid maximizing) | Constrains Win32 fullscreen desktop bounds cleanly without disturbing host displays. Maximizing a Wine virtual desktop pins the canvas top-left with black margins. |
| **Wine 10 + GNOME Wayland `explorer /desktop=...` opens fullscreen with black borders on cold start** | `wine explorer /desktop=V5,1024x768 cmd /c exit && wine explorer /desktop=V5,1024x768 ./game.exe ...` | Priming the virtual desktop once avoids a cold `wineserver` / Mutter `_NET_WM_STATE` race (alternatively press `Alt+F11` to un-fullscreen). |
| **Win95/98 `go.bat` fails with `couldn't open file ...~1\...` under Wine** | Launch the `.exe` directly with the full long path (e.g. `-model 'data\DistressedMuseum\museum.gxf'`) | Linux filesystems do not synthesize Windows 8.3 short names (`distre~1`, `smallt~1`), and `.exe` filenames are case-sensitive on POSIX paths. |
| **Wine complains `could not load kernel32.dll, status c0000135`** | `WINEARCH=win32 wineboot -u` | Initializes a 32-bit Wine prefix when `wine32:i386` is installed without `wine64`. |
| **Host lacks a 32-bit hardware Vulkan ICD (`llvmpipe` fallback)** | Install 32-bit Vulkan ICD (`mesa-vulkan-drivers:i386` / NVIDIA 32-bit), or use `GLIDE_NG_BACKEND=software` | `glide-ng`'s Software AVX2/AVX-512 backend is typically **3–4× faster** than CPU-emulated Vulkan `llvmpipe` (see [`glide-bench` guide](./tools/glide-bench/README.md#5-comparing-mesa-llvmpipe-vulkan-and-opengl-es-vs-glide-ng-software)). |

---

## 5. Contributing a New Entry

1. Copy [`_template/`](./_template/README.md) to `docs/catalog/<games|demos|tools>/<slug>/`.
2. Fill in `README.md` with verified findings (editions/patches, launch commands, quirks, controls) and tune `glide-ng.ini` using authentic card-to-version pairs (or omit `api_version` to inherit the card's authentic default).
3. Add a row to the appropriate table in this index (`docs/catalog/README.md`).
4. Run the automated Catch2 catalog validator (`GLIDE_NG_HEADLESS=1 ./build/bin/Glide3xWrapperTests "[M12][Catalog]"`) to confirm every catalog `.ini` parses with zero unknown keys, zero invalid values, and zero card/version warnings.
