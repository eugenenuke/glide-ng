# glide-ng Game, Demo & Tool Compatibility Library

A living reference of real-world 3dfx Glide games, official tech demos, and diagnostic tools verified on **glide-ng**, documenting per-title quirks, recommended `glide-ng.ini` / environment overrides, runtime dependencies, and controls for the best visual and performance results.

> **How per-title overrides work**: Drop a `glide-ng.ini` file right next to the game/demo executable (see [`glide-ng.ini.example`](../glide-ng.ini.example)), or pass environment overrides at launch. Precedence is always:
> `Built-in defaults < glide-ng.ini < Authentic 3dfx env vars (FX_GLIDE_* / SST_*) < GLIDE_NG_* env vars`.
> See [Environment Variables & Overrides Guide](./glide_environment_variables.md) for the full list of supported keys.

---

## 1. Quick Compatibility Matrix

| Title | Year | Platform / Runtime | Glide API | Vulkan | OpenGL ES / WGL | Software | Key Quirk / Recommended Override | Status |
| :--- | :---: | :--- | :---: | :---: | :---: | :---: | :--- | :---: |
| **Carmageddon (3dfx Voodoo patch)** | 1997 | DOS/32 (`GLIDE2X.OVL` / DOSBox) | Glide 2.x | Pass | Pass | Pass | Works out-of-the-box (`no_splash = true` optional) | Verified |
| **3dfx Donut** (`donut.exe`) | 1998 | Win32 (Wine / Proton) | Glide 2.x | Pass | Pass | Pass | Works out-of-the-box (`gamma = 1.0`) | Verified |
| **3dfx Valley of Ra** (`fight.exe`) | 1997 | Win32 (Wine / Proton, ATB) | Glide 2.x | Pass | Pass | Pass | Needs ATB runtime DLLs; `gamma = 1.0` | Verified |
| **3dfx Demo Room** (`demoroom.exe`) | 1997 | Win32 (Wine / Proton, ATB) | Glide 2.x | Pass | Pass | Pass | **`SST_GAMMA=1.7`** (`gamma = 1.7`); press `Space` for lights | Verified |
| **3dfx Wizard's Tower** (`wiz.exe`) | 1997 | Win32 (Wine / Proton, ATB) | Glide 2.x | Pass | Pass | Pass | **`SST_GAMMA=1.7`** (`gamma = 1.7`) for Room 1 lightmaps | Verified |
| **3dfx Surf** (`surf_glide2x.exe` / `surf_glide3x.exe`) | 1998 | Win32 (Wine / Proton) | **Glide 2.x & 3.x** | Pass | Pass | Pass | Works out-of-the-box (`1024×768`, 2-TMU env map) | Verified |
| **3dfx Kitty Hawk** (`aademo.exe`) | 2000 | Win32 (Wine / Proton) | Glide 3.x | Pass | Pass | Pass | Pass `-model 'data\kittyhawk\kittyhawk.gxf'`; no T-Buffer FSAA yet | Verified |
| **3dfx Museum** (`museum.exe`) | 1999 | Win32 (Wine / Proton) | Glide 3.x | Pass | Pass | Pass | `go.bat` uses 8.3 `distre~1`; pass `-model 'data\DistressedMuseum\museum.gxf'` | Verified |
| **3dfx Rocket Burger** (`RocketBurger.exe`) | 1999 | Win32 (Wine / Proton) | Glide 3.x | Pass | Pass | Pass | `go.bat` uses 8.3 `smallt~1`; pass `-model 'data\small tgas\scene.gxf'` | Verified |
| **`glide2_info` / `glide3_info`** | — | Linux / DOS / Win32 | Glide 2.x & 3.x | Pass | Pass | Pass | Use `--probe` for full resolution & adapter report | Verified |
| **`glide2_bench` / `glide3_bench`** | — | Linux / DOS / Win32 | Glide 2.x & 3.x | Pass | Pass | Pass | `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` for uncapped scores | Verified |
| **`glide2_envtest` / `glide3_envtest`** | — | Linux / DOS / Win32 | Glide 2.x & 3.x | Pass | Pass | Pass | `--verify --test-reopen --test-bounds` for automated checks | Verified |

---

## 2. Commercial Games

### Carmageddon (3dfx Voodoo Patch — `CARMAV.EXE`)
* **Year / Runtime**: 1997 · DOS/32 (`GLIDE2X.OVL` via DOSBox Glide passthrough) · Glide 2.x (`2.43`)
* **Backends Verified**: Vulkan, OpenGL ES, Software (AVX2+FMA SIMD & scalar)
* **Engine Notes**:
  * Uses persistent `grLfbLock(GR_LFB_WRITE_ONLY)` for menus, briefings, and the in-car Pratcam overlay, paired with `GR_TEXFMT_P_8` palette-swapped world textures and W-buffer depth.
  * Runs cleanly on default `glide-ng` settings with full menu/HUD visibility and sky/seam fidelity.
* **Recommended `glide-ng.ini`** *(optional polish)*:
  ```ini
  [core]
  backend = vulkan

  [glide]
  no_splash = true
  ```

---

## 3. Official 3dfx Tech Demos (Win32 / Wine)

> **Common Win32 / Wine Launch Prefix**: Place `glide2x.dll` and/or `glide3x.dll` from `build_win32/bin/` next to the `.exe`, and pass `WINEDLLOVERRIDES="glide2x,glide3x=n"` so Wine loads `glide-ng` instead of any built-in stub.

### 3dfx Donut (`donut.exe`)
* **Year / Runtime**: 1998 · Win32 (Wine / Proton) · Glide 2.x (`glide2x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Behavior**:
  * Opens a maximized `WS_POPUP` host window and calls `grSstWinOpen(0, ...)`. `glide-ng` automatically attaches in `Win32Embedded` mode to the demo's own window.
  * Streams dynamic procedural reflection mipmap strips via inclusive-bound `grTexDownloadMipMapLevelPartial` and draws coplanar `GR_DEPTHBUFFER_WBUFFER` (`LEQUAL`) passes.
  * Looks crisp and well-balanced at default `gamma = 1.0`; no overrides needed.
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./donut.exe
  ```

---

### 3dfx Valley of Ra (`fight.exe`)
* **Year / Runtime**: 1997 · Win32 (Wine / Proton, 3dfx Arcade Tool Box) · Glide 2.x (`glide2x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Packaging**:
  * **Runtime DLLs required**: `fight.exe` imports 3dfx's ATB runtime DLLs (`apilib.dll`, `fltdata.dll`, `lm.dll`, `mgdd.dll`, `runtime.dll`, `textureeditor.dll`, `MSVCRT40.DLL`). Keep them in the same directory as `fight.exe` along with the demo's asset files.
  * Uses `GR_DITHER_2x2` on alpha-blended cutouts/billboards and runs inside a `640×480` Win32 ATB window (`Win32Embedded`).
  * Looks bright and natural at default `gamma = 1.0`.
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./fight.exe
  ```

---

### 3dfx Demo Room (`demoroom.exe`)
* **Year / Runtime**: 1996/1997 · Win32 (Wine / Proton, 3dfx Arcade Tool Box) · Glide 2.x (`glide2x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Fixes**:
  * **Appears nearly black on startup at `gamma = 1.0`**: The demo starts with a pure-blue projected texture (`PROJBLUE.3DF`, RGB `0,0,161`) modulating warm brown sandstone hieroglyph walls (`WALLMAP.3DF`, RGB `117,82,21` — very little blue channel). Because 1996 Voodoo Graphics (SST-1) hardware defaulted to **RAMDAC gamma `1.7`**, running at linear `gamma = 1.0` crushes the initial room to average RGB `(0, 0, 14)`, leaving only the white menu bar and 3dfx starburst logo easily visible.
  * **Best Result**: Enable **`SST_GAMMA=1.7`** (or `gamma = 1.7` in `glide-ng.ini`), and press **`Space`** to cycle through the other projected lights (red, green, 4-quadrant multicolor, white spotlight `PROJWHT.3DF`, projected photo `PROJGORD.3DF`, and animated sequence `0000..0020.3DF`).
  * **Centered 3D white arrow when holding `Up` (Expected Behavior)**: Holding `Up` pitches the projected spotlight downward toward the floor and swings the demo's intentional 3D light-direction pointer mesh (`demolite`) into view at screen center (also visible in wireframe mode `Ctrl-W`). Note: `demoroom.exe` renders this untextured Gouraud mesh with `GR_DITHER_2x2`, opaque blend (`ONE, ZERO`), and `alpha = 0` (`TMU0 ZERO`), which verified the Software backend's non-discarding 1-bit alpha dithering fix.
  * **Runtime DLLs**: Requires the ATB DLLs (`apilib.dll`, `fltdata.dll`, `lm.dll`, `mgdd.dll`, `runtime.dll`, `textureeditor.dll`, `MSVCRT40.DLL`) and `.3DF`/`.bof` assets in the current working directory.
* **Useful Controls**: `Space` cycle projected lights · Arrow keys move light (`Shift + Up/Down` forward/back) · NumPad moves camera · `Ctrl-L` toggle projected light · `Ctrl-B` bilinear · `Ctrl-M` mipmap · `Ctrl-W` wireframe · `Esc` quit.
* **Recommended `glide-ng.ini`** (next to `demoroom.exe`):
  ```ini
  [core]
  backend = vulkan

  [glide]
  gamma = 1.7
  ```
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan SST_GAMMA=1.7 wine ./demoroom.exe
  ```

---

### 3dfx The Wizard of Tower (`wiz.exe`)
* **Year / Runtime**: 1996/1997 · Win32 (Wine / Proton, 3dfx Arcade Tool Box) · Glide 2.x (`glide2x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Fixes**:
  * **Dark Room 1 baked lightmaps**: Multi-pass `GR_BLEND_ZERO, GR_BLEND_SRC_COLOR` lightmaps (`textures/WIZ1/*.3DF`) have very low linear values (median RGB `5, 10, 11`), authored for Voodoo Graphics' `1.7` RAMDAC curve. Without gamma lift, the central altar and lower stairs appear near-black while the upper arches and stained-glass windows remain visible.
  * **Best Result**: Enable **`SST_GAMMA=1.7`** (or `gamma = 1.7` in `glide-ng.ini`).
  * **Playback key (`Enter`)**: Pressing `Enter` tries to play back `app.evt` and may display an `Invalid event file app.evt` ATB dialog depending on the archive. Navigate interactively with the arrow keys instead.
  * **Runtime DLLs**: Requires the ATB DLLs, `wiz.bof`, and `textures/` directory in the working directory.
* **Useful Controls**: Arrow keys move/turn (`Shift + Up/Down` look up/down) · `B` spawn bat · `M` spawn warrior · `Space` fire · `P` / `Ctrl-P` FPS readout · `Ctrl-W` wireframe · `Ctrl-T` textures · `Esc` quit.
* **Recommended `glide-ng.ini`** (next to `wiz.exe`):
  ```ini
  [core]
  backend = vulkan

  [glide]
  gamma = 1.7
  ```
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan SST_GAMMA=1.7 wine ./wiz.exe
  ```

---

### 3dfx Surf (`surf_glide2x.exe` & `surf_glide3x.exe`)
* **Year / Runtime**: 1998 · Win32 (Wine / Proton) · **Glide 2.x (`glide2x.dll`) & Glide 3.x (`glide3x.dll`)**
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical between Glide 2.x and Glide 3.x builds across all backends.
* **Quirks & Behavior**:
  * Opens `1024×768` triple-buffered (`Buffers=3, Aux=1`) by default and exercises Glide 3.x dynamic vertex layouts (`grVertexLayout`, `grDrawVertexArray`, `grDrawVertexArrayContiguous`), 2-TMU environment mapping (`E`), fog (`H`), wireframe (`W`), control points (`D`), and dynamic LOD (`X`).
  * Looks ideal at default `gamma = 1.0`.
  * On systems using CPU-emulated Vulkan/OpenGL (`llvmpipe`) at high monitor resolutions, either pass `-res 640x480` / `-res 800x600`, or try `GLIDE_NG_BACKEND=software` (typically 2× faster than `llvmpipe` at `1024×768`).
  * To benchmark uncapped FPS, pass `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` (and press `V` if the demo's own VSync toggle is active).
* **Useful Controls**: Mouse rotates camera · `Left`/`Right` arrows move camera · `E` environment map (2 TMUs) · `H` fog · `W` wireframe · `D` control points · `P` pause waves · `T` B-spline/Bezier · `X` dynamic LOD · `Y` specular · `V` VSync · `Esc` quit.
* **Recommended Command**:
  ```bash
  # Glide 3.x build
  WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./surf_glide3x.exe

  # Glide 2.x build
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./surf_glide2x.exe
  ```

---

### 3dfx Kitty Hawk (`aademo.exe`)
* **Year / Runtime**: 2000 · Win32 (Wine / Proton) · Glide 3.x (`glide3x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Behavior**:
  * Part of 3dfx's Voodoo5 / VSA-100 (`Napalm`) Glide 3.x demo engine family alongside *Museum* and *Rocket Burger*. Uses packed 32-bit vertex colors (`GR_PARAM_PARGB`, `0xAARRGGBB`) overlapping Glide 3.x's canonical float RGB/A offsets.
  * Launching `aademo.exe` with no arguments opens a Win32 Open File dialog (`comdlg32`) prompting for a `.gxf` scene file. Pass `-model 'data\kittyhawk\kittyhawk.gxf'` (or run `go.bat`, whose path is 8.3-compatible) to jump straight into the 1903 Wright Flyer showcase.
  * **Loading screen swaps on HiDPI `llvmpipe`**: While parsing `.gxf` geometry and uploading textures, the engine calls `grBufferSwap` hundreds of times. On CPU-emulated Vulkan (`llvmpipe`) at HiDPI monitor sizes, loading can take 30–60s unless you constrain the virtual desktop (`wine explorer /desktop=V5,1024x768`) or use `GLIDE_NG_BACKEND=software` with `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` (~4–6s load time).
  * **Voodoo5 T-Buffer extensions**: `grSstWinOpenExt` and `grTBufferWriteMaskExt` are not yet emulated under the Voodoo2 profile, so the demo gracefully runs in full 3D color while its HUD reports `"No Spatial Anti-Aliasing"`.
  * Looks crisp and well-balanced at default `gamma = 1.0`.
* **Useful Controls**: `Space` / any key dismisses the `"Press any key to continue"` prompt · `F1` options menu · `Esc` quit.
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 \
      wine explorer /desktop=V5,1024x768 ./aademo.exe \
      -res 1024x768 -pixfmt aa4rgb565 -model 'data\kittyhawk\kittyhawk.gxf'
  ```

---

### 3dfx Museum (`museum.exe`)
* **Year / Runtime**: 1999 · Win32 (Wine / Proton) · Glide 3.x (`glide3x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Fixes**:
  * **`go.bat` fails under Wine (`couldn't open file data\distre~1\museum.gxf`)**: The original `go.bat` hardcodes the Windows 95/98 8.3 short directory name `distre~1`, which Linux filesystems do not synthesize. Launch `museum.exe` directly with the full long path `-model 'data\DistressedMuseum\museum.gxf'` (or `-model 'data\museum\museum.gxf'` for the alternate clean museum scene).
  * Running `museum.exe` with no arguments opens a Win32 Open File dialog.
  * Same Voodoo5 / Napalm engine as *Kitty Hawk*: use `wine explorer /desktop=V5,1024x768` (or `GLIDE_NG_BACKEND=software`) with `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` for fast ~6–13s loading on CPU `llvmpipe`.
  * **Voodoo5 T-Buffer extensions**: Without T-Buffer emulation, the HUD notes `"No Soft Reflectance"` / `"No Spatial Anti-Aliasing"`, while the marble floor, columns, crates, *Flaming June* painting, and inverted-text whiteboard Easter egg (authored upside-down in `whiteboard.tga` to be read in the floor reflection) render cleanly.
  * Looks well-balanced at default `gamma = 1.0`.
* **Useful Controls**: `Space` / any key starts the flythrough after loading · `F1` options menu (enable **Free Flight** to explore with mouse + NumPad arrow keys) · `F2`/`F3` FSAA/Soft Reflectance toggles (T-Buffer) · `Esc` quit.
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 \
      wine explorer /desktop=V5,1024x768 ./museum.exe \
      -res 1024x768 -pixfmt aa4rgb565 -model 'data\DistressedMuseum\museum.gxf'
  ```

---

### 3dfx Rocket Burger (`RocketBurger.exe`)
* **Year / Runtime**: 1999 · Win32 (Wine / Proton) · Glide 3.x (`glide3x.dll`)
* **Backends Verified**: Vulkan, OpenGL 3.3 Core (WGL), Software (SIMD & scalar) — pixel-identical across all backends.
* **Quirks & Fixes**:
  * **`go.bat` fails under Wine (`couldn't open file data\smallt~1\scene.gxf`)**: The original `go.bat` uses the Windows 95/98 8.3 short name `smallt~1` for `small tgas` and lowercase `Rocketburger.exe`. Launch `./RocketBurger.exe` (exact casing on Linux) with `-model 'data\small tgas\scene.gxf'`.
  * Running `RocketBurger.exe` with no arguments opens a Win32 Open File dialog.
  * Same Voodoo5 / Napalm engine as *Kitty Hawk* and *Museum*: use `wine explorer /desktop=V5,640x480` (or `GLIDE_NG_BACKEND=software`) with `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0` for fast ~8–17s loading on CPU `llvmpipe`.
  * **Voodoo5 T-Buffer extensions**: Without T-Buffer emulation, Motion Blur / Depth of Field / FSAA stay gracefully disabled while the full animated chase scene (floating Rocket Burger diner, clouds, 3dfx billboard, space cop jet-bike, and hover hot-rod) renders cleanly.
  * Looks vibrant at default `gamma = 1.0`.
* **Useful Controls**: `Space` / any key dismisses `"Press any key to continue"` · `F1` options menu · `Esc` quit.
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 \
      wine explorer /desktop=V5,640x480 ./RocketBurger.exe \
      -res 640x480 -pixfmt aa4rgb565 -model 'data\small tgas\scene.gxf'
  ```

---

## 4. `glide-ng` Diagnostic, Benchmark & Verification Tools

### `glide2_info` / `glide3_info` (`glide_info.exe`)
* **Purpose**: Inspect active wrapper build, emulated Voodoo profile, TMU/FBI memory budgets, host GPU adapter, and probe supported Glide resolutions.
* **Recommended Commands**:
  ```bash
  # Linux
  GLIDE_NG_BACKEND=vulkan ./build_release/bin/glide2_info --probe
  # Win32 under Wine
  WINEDLLOVERRIDES="glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan wine ./build_win32/bin/glide3_info.exe --probe
  ```

### `glide2_bench` / `glide3_bench` (`glide_bench.exe`)
* **Purpose**: 6-scene GlideMark benchmark (`fillrate`, `tris`, `state`, `tex`, `lfb`, `multipass`).
* **Quirks for Reproducible Runs**:
  * Always disable frame pacing (`GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0`) and startup splash (`GLIDE_NG_NO_SPLASH=1`) so scores reflect actual throughput rather than the 60 FPS VSync cap.
  * Add `GLIDE_NG_HEADLESS=1` when measuring pure CPU/GPU rasterization without display/WM presentation variance; omit `GLIDE_NG_HEADLESS` to include swapchain/WGL/GDI presentation.
* **Recommended Commands**:
  ```bash
  # Uncapped windowed benchmark under Wine
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan GLIDE_NG_NO_SPLASH=1 \
      GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 wine ./build_win32/bin/glide2_bench.exe --no-hud

  # Headless compute benchmark (Linux native vs Wine apples-to-apples)
  GLIDE_NG_BACKEND=software GLIDE_NG_HEADLESS=1 GLIDE_NG_NO_SPLASH=1 \
      GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 ./build_release/bin/glide2_bench --frames 30 --no-hud
  ```

### `glide2_envtest` / `glide3_envtest` (`glide_envtest.exe`)
* **Purpose**: Permanent 4-quadrant visual test card + automated RGB565 LFB probe verifier for `glide-ng.ini` and authentic 3dfx environment variables.
* **Recommended Commands**:
  ```bash
  # Automated verification pass (reopen + bounds check)
  WINEDLLOVERRIDES="glide2x,glide3x=n" GLIDE_NG_BACKEND=vulkan \
      wine ./build_win32/bin/glide2_envtest.exe --verify --test-reopen --test-bounds
  ```

---

## 5. Common Cross-Title Quirk Recipes

| Symptom / Scenario | Recommended Setting (`glide-ng.ini` or Env) | Why It Works |
| :--- | :--- | :--- |
| **1996–1997 Voodoo1 title looks too dark** (*Demo Room*, *Wizard's Tower*, *GLQuake*, *Tomb Raider*) | `[glide]` → `gamma = 1.7` (or `SST_GAMMA=1.7`) | Authentic Voodoo Graphics (SST-1) `grSstWinOpen` defaulted the RAMDAC LUT to `1.7` (Voodoo2 CVG used `1.3`). `glide-ng` defaults to linear `1.0` so modern/bright games are not washed out. |
| **1998 Voodoo2 title looks slightly dark** | `[glide]` → `gamma = 1.3` (or `SSTV2_GAMMA=1.3`) | Matches authentic Voodoo2 (`CVG`) hardware default RAMDAC curve. |
| **Game misbehaves or misdetects 2 TMUs** (early 1996–1997 SST-1 titles) | `[hardware]` → `card_model = VoodooGraphics`, `tmu_count = 1`, `tmu_memory_mb = 2` (or `SSTV2_NUM_TMUS=1`) | Restricts reported hardware to single-TMU Voodoo Graphics layout. |
| **Game calls `grSstWinOpen` twice without closing / flickers on reopen** | `[glide]` → `ignore_reopen = true` (or `FX_GLIDE_IGNORE_REOPEN=1`) | Suppresses redundant window teardown/recreation while active. |
| **16-bit dither banding visible on smooth gradients** | `[glide]` → `no_dither_sub = true` (or `FX_GLIDE_NO_DITHER_SUB=1`) | Keeps full 24-bit color precision without 16-bit dither subtraction. |
| **Blurry textures / point-sampled retro look desired to be smoother** | `[glide]` → `force_bilinear = true` or `force_trilinear = true` | Overrides app's `GR_TEXTUREFILTER_POINT_SAMPLED` to bilinear/trilinear filtering. |
| **High-DPI / multi-monitor fullscreen Win32 demo opens awkwardly under Wine** | `wine explorer /desktop=Glide,1024x768 ./game.exe` | Constrains Win32 fullscreen desktop bounds cleanly without affecting your host monitor layout (also avoids slow `llvmpipe` full-monitor blits during loading screens). |
| **Win95/98 `go.bat` fails with `couldn't open file ...~1\...` under Wine** | Launch the `.exe` directly with the full long path (e.g. `-model 'data\DistressedMuseum\museum.gxf'`) | Linux filesystems do not synthesize Windows 8.3 short names (`distre~1`, `smallt~1`), and `.exe` casing is case-sensitive on POSIX paths. |
| **Wine complains `could not load kernel32.dll, status c0000135`** | `WINEARCH=win32 wineboot -u` | Initializes a 32-bit Wine prefix when `wine32:i386` is installed without `wine64`. |
| **Host has no 32-bit hardware Vulkan ICD (`llvmpipe` fallback)** | Install 32-bit Vulkan ICD (`mesa-vulkan-drivers:i386` / NVIDIA 32-bit), or use `GLIDE_NG_BACKEND=software` | Software AVX2+FMA backend with native Win32 `SRWLOCK`/`CV` is typically faster than CPU-emulated Vulkan `llvmpipe`. |

---

## 6. Template for New Entries

Copy this template when adding a newly tested game, demo, or tool:

```markdown
### <Title> (`<executable>`)
* **Year / Runtime**: <YYYY> · <DOS/32 | Win32 (Wine / Proton) | Linux> · <Glide 2.x | Glide 3.x>
* **Backends Verified**: Vulkan, OpenGL ES / WGL, Software
* **Quirks & Fixes**:
  * <Symptom, root cause, and workaround or wrapper note>
* **Useful Controls**: <Key controls if non-obvious>
* **Recommended `glide-ng.ini`**:
  ```ini
  [core]
  backend = vulkan
  ```
* **Recommended Command**:
  ```bash
  WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan wine ./<exe>
  ```
```
