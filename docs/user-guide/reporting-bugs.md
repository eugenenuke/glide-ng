# Reporting Bugs & Compatibility Issues

Thank you for helping improve `glide-ng`! Because classic 3dfx titles vary widely in patches, extenders, renderers, and driver behavior, a clear log and a quick backend check usually turn a mysterious freeze or glitch into an actionable fix within minutes.

> **Important — Do NOT upload copyrighted game files.** Never attach commercial `.exe`/`.dll` binaries, `.iso`/`.bin` disc images, data archives (`.hog`, `.mpq`, `.pak`, `.wad`, etc.), or full sound/video files to a GitHub issue. Plain-text logs, `.ini`/`.cfg` files, CSV telemetry, and screenshots are all we need.

> **Optional AI-assisted collection:** Contributors using an AI coding CLI (or running the standalone Python 3 stdlib scripts directly) can use the [`game-collector`](../../.agents/skills/game-collector/SKILL.md) skill to run read-only binary recon, generate isolated per-backend launchers, deduplicate `glide-ng.log` / `perf.csv` summaries, sanity-check screenshots, and pack a copyright-clean report archive.

---

## Before You Open an Issue (30-Second Checklist)

1. **Check existing guides first:** Many classic titles have game-specific tweaks (e.g., `gamma = 1.7`, `max_fps = 60`, DOSBox `memsize`, or Wine `renderer=no3d`) already documented in the [Compatibility Catalog](../catalog/README.md) and the [Troubleshooting Guide](troubleshooting.md).
2. **Confirm which `glide-ng` build actually loaded:** Open the top of **`glide-ng.log`** (created next to the game executable or in the working directory) and verify both lines inside the startup banner:
   ```text
   Wrapper Build        : v0.9.0+g<hash> (branch:main, Release)
   Loaded Module        : /path/to/libglide2x.so   (or C:\...\glide3x.dll)
   ```
   A leftover OEM `glide*.dll`, `nGlide` copy, or stale system `libglide*.so` in another folder (such as `Drivers/`, `System/`, or `C:\windows\system32\`) can silently shadow an updated release.
3. **Save your log *before* restarting the game:** `glide-ng.log` is overwritten on every launch. Copy or rename it (or set a unique `GLIDE_NG_LOG_FILE`) right after the issue occurs.
4. **Run the quick backend A/B test:** `glide-ng`'s **Software** backend serves as our reference rasterizer. Trying the exact same spot with one other backend immediately shows whether a bug is in a GPU backend (`vulkan` / `opengl_es`) or in the shared Glide core:
   - `GLIDE_NG_BACKEND=vulkan` *(default GPU backend)*
   - `GLIDE_NG_BACKEND=opengl_es` *(OpenGL ES 3.2 on Linux / OpenGL 3.3 Core WGL on Windows/Wine)*
   - `GLIDE_NG_BACKEND=software` *(CPU reference rasterizer)*

---

## The Essentials (Include for Every Bug Report)

* **Game & version:** Title, release/storefront (retail CD, GOG, Steam, etc.), exact patch version, and the executable name/size you launched (e.g., `D2_3DFX.EXE`, `1,374,737` bytes).
* **Glide API / platform:** DOSBox-SVN / DOSBox-X (`GLIDE2X.OVL`), Wine / Lutris / Steam Proton, or native Windows (`glide.dll` / `glide2x.dll` / `glide3x.dll` / MiniGL / `3dfxVGL.dll`).
* **Host environment:** OS/distro, session type (`X11` / `Wayland` / `Windows`), GPU + driver version (Mesa / NVIDIA / AMD), Wine/Proton or DOSBox version.
* **`glide-ng` version & files:** Your **`glide-ng.log`** and the **`glide-ng.ini`** (or `dosbox.conf` / launcher env vars) you used.
* **Exact reproduction steps:** Starting from a menu, mission, or transition (note whether it happens *every time*, *intermittently*, or *only after N minutes*).
* **Backend A/B result:** One line noting what happened on `vulkan` (1× and 4× MSAA if used), `opengl_es`, and `software`.

---

## Capturing Logs by Platform

By default, `glide-ng` writes `info`-level logs to `glide-ng.log`. When chasing a **crash or hang**, increase verbosity to **`debug`** and enable console mirroring (**`GLIDE_NG_LOG_CONSOLE=1`**) so messages are flushed immediately right up to the failure. *(Inside `glide-ng.log`, `[CRIT]` and `[WARN]` flush immediately; `[INFO]`/`[DBUG]`/`[TRCE]` buffer up to 32 KB / 250 ms.)*

### Linux — DOSBox-SVN / DOSBox-X
```bash
GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1 \
  GLIDE_NG_LOG_FILE="$PWD/glide-ng-debug.log" \
  stdbuf -oL -eL dosbox -conf dosbox.conf 2>&1 | tee console.log
```

### Linux — Standalone Wine / Steam Proton
```bash
# Standalone Wine (swap glide2x for glide or glide3x as needed)
WINEDLLOVERRIDES="glide,glide2x,glide3x=n" \
  GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1 \
  GLIDE_NG_LOG_FILE="Z:\\tmp\\glide-ng-debug.log" \
  stdbuf -oL -eL wine ./GAME.EXE 2>&1 | tee console.log
```
* **Steam Proton (Launch Options):**
  ```text
  WINEDLLOVERRIDES="glide,glide2x,glide3x=n" GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_FILE=/tmp/glide-ng-debug.log %command%
  ```
* **Lutris:** Add `GLIDE_NG_LOG_LEVEL=debug`, `GLIDE_NG_LOG_CONSOLE=1`, and `GLIDE_NG_LOG_FILE=/tmp/glide-ng-debug.log` under **Configure → System options → Environment variables**, then view or save the game log (`Show logs` or `lutris -d`).

### Windows (PowerShell)
```powershell
$env:GLIDE_NG_LOG_LEVEL = "debug"
$env:GLIDE_NG_LOG_CONSOLE = "1"
$env:GLIDE_NG_LOG_FILE = "$PWD\glide-ng-debug.log"
.\GAME.EXE
```

---

## What Else to Collect (By Symptom)

Only capture what matches your issue:

### 1. Visual Glitch (wrong colors, missing lines/geometry, bad fog, HUD clipping, flicker)
* **Screenshots (lossless `.png` preferred):** Capture the **same scene** across `vulkan`, `opengl_es`, and `software` (plus `msaa = 1` vs `msaa = 4` if anti-aliasing triggers seams/halos). Turn off the `glide-ng` HUD (`GLIDE_NG_HUD=0`) unless reporting a HUD bug.
* **Expected reference (if known):** Link or note how the scene looks on native software/D3D, original hardware, or a video screenshot.
* **Two quick yes/no tests** (greatly speed up triage):
  * `FX_GLIDE_BOUNDS_CHECK=1` — check whether `glide-ng.log` logs any `[WARN] [BoundsCheck]` lines.
  * `SST_TEXMAP_DISABLE=1` — note whether the glitch stays visible with texture mapping bypassed.

### 2. Freeze / Hang / Deadlock (e.g., stuck on a level transition or loading screen)
* Wait **5–10 seconds** once hung and note:
  * Is the CPU at **~0%** (waiting/deadlocked) or **100%** (spinning)?
  * Is **audio** still looping, stuck/buzzing, or completely silent?
  * Do **`Alt+Tab` / `Esc`** still respond?
* Grab the **`glide-ng.log`** and **`console.log`** (with `GLIDE_NG_LOG_LEVEL=debug` and `GLIDE_NG_LOG_CONSOLE=1`).
* **If on Linux, attach a live thread backtrace *before* killing the frozen process** (see *Advanced Captures* below).

### 3. Crash / Immediate Exit (`SIGSEGV`, access violation, silent exit)
* Attach **`glide-ng.log`** (with `GLIDE_NG_LOG_LEVEL=debug` and `GLIDE_NG_LOG_CONSOLE=1`) **plus** terminal output (`console.log`).
* **Wine / Proton:** Add `WINEDEBUG=err+all,fixme-all,+seh,+loaddll` to reveal missing DLLs or exception addresses.
* **Native Windows:** Include the matching **Event Viewer → Windows Logs → Application** error snippet (faulting module name, exception code, offset).

### 4. Blank / Black / Stale Screen (menus not updating unless moving the mouse or pressing a key)
* Check whether **audio still plays** and keyboard/mouse presses still advance menus blindly.
* Capture a lightweight `lfb,wsi,api` log while tapping menu keys for a few seconds:
  ```bash
  GLIDE_NG_LOG_LEVEL=trace GLIDE_NG_LOG_CATEGORIES=lfb,wsi,api \
    GLIDE_NG_LOG_FILE=frontbuf.log <game_command>
  ```

### 5. Windowing, Focus, Fullscreen, or `Alt+Tab` Issues
* Mention your desktop/compositor (`GNOME`, `KDE Plasma`, `Hyprland`, `Sway`, `Xfwm`, `Windows 11`, etc.), `XDG_SESSION_TYPE` (`x11`/`wayland`), monitor count, resolution, and DPI scaling (e.g. `150%`).
* Test windowed vs. borderless fullscreen (**`GLIDE_NG_FULLSCREEN=0`** vs. **`1`**) and on Wine try a virtual desktop (**`wine explorer /desktop=Game,800x600`**).
* Attach a WSI debug log:
  ```bash
  GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CATEGORIES=wsi,general \
    GLIDE_NG_LOG_FILE=wsi.log <game_command>
  ```

### 6. Low FPS, Stutters, Audio Crackling, or Memory Growth
* Record per-frame telemetry CSV (very low overhead):
  ```bash
  GLIDE_NG_PERF_LOG=perf.csv GLIDE_NG_LOG_FILE=glide-ng.log <game_command>
  ```
* Include **one screenshot with `GLIDE_NG_HUD=full`** captured right during the stutter/drop so `frametimes`, `drawcalls`, and `lfb` counts are visible.

---

## Optional Advanced Captures

<details>
<summary><strong>1. Quick Linux system info command</strong></summary>

```bash
{
  uname -a
  grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release 2>/dev/null
  echo "Session: XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-} SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-}"
  lspci | grep -iE 'vga|3d|display'
  command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>&1 | head -n 35
  command -v glxinfo >/dev/null && glxinfo -B 2>&1 | head -n 20
  command -v wine >/dev/null && wine --version
  command -v dosbox >/dev/null && dosbox --version
} > host_env.txt 2>&1
```

</details>

<details>
<summary><strong>2. Capture a compact, frame-windowed API trace (visual glitches)</strong></summary>

Full `trace` logs grow fast because every triangle logs separately. Scope them to just 2–5 frames around the glitch using the `[FNNNNNN]` frame counter from `glide-ng.log`:

```bash
GLIDE_NG_LOG_LEVEL=trace \
  GLIDE_NG_TRACE_FRAMES=1419-1422 \
  GLIDE_NG_LOG_CATEGORIES=api,state,tex,lfb,draw \
  GLIDE_NG_LOG_FILE=trace.log \
  FX_GLIDE_SNAPSHOT=frame.ppm@1419,1420 \
  <game_command>
```

To capture multiple evenly spaced frames across a run (for comparing `vulkan`, `opengl_es`, and `software`), use periodic snapshot syntax (`FX_GLIDE_SNAPSHOT=shots/@300+120:6`, creating parent folders automatically). For a longer debug run without per-triangle spam, drop triangle draw logs using `-draw`:

```bash
GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CATEGORIES=-draw \
  GLIDE_NG_LOG_FILE=nodraw.log <game_command>
```

</details>

<details>
<summary><strong>3. Grab a live thread backtrace on a frozen Linux process (<code>gdb</code>)</strong></summary>

For pre-built release archives, extracting the matching `debug-symbols.zip` archive next to the libraries resolves full C++ file names and line numbers.

```bash
# Find the hung process PID (e.g. dosbox or game.exe)
ps aux | grep -iE 'dosbox|wine|\.exe'

# Capture all thread stacks without terminating the process
gdb -batch -p <PID> -ex "set pagination off" -ex "info threads" \
    -ex "thread apply all bt" > backtrace.txt 2>&1
```

*(If `ptrace` blocks attaching, rerun with `sudo gdb ...`. If `coredumpctl` is enabled after a crash, `coredumpctl info -1 > crash.txt` also works.)*

</details>

<details>
<summary><strong>4. Vulkan validation layers (Vulkan-only crashes, hangs, or glitches)</strong></summary>

With your distro's `vulkan-validationlayers` package installed (or the LunarG Vulkan SDK on Windows):

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1 \
  GLIDE_NG_LOG_FILE=vkval.log <game_command> 2>&1 | tee vkval_stderr.log
```

On Linux kernel GPU hangs/resets, also check the tail of `dmesg`: `dmesg -T | tail -n 50`.

</details>

<details>
<summary><strong>5. RenderDoc captures &amp; archive compression</strong></summary>

* If a bug affects **only** Vulkan or OpenGL ES (while `software` is clean), a single-frame **RenderDoc** `.rdc` capture lets maintainers inspect every draw call, texture, and shader without needing the game installed.
* Compress large plain-text logs with `.zip`, `.tar.gz`, or `.zst` (text logs typically shrink 15×–30×) and convert raw `.ppm` snapshots to `.png` or `.webp` before uploading (e.g., `python3 scripts/gallery_tool.py pack shots/ --format png`). You can also measure cross-backend differences with `python3 scripts/gallery_tool.py compare shots/sw shots/vk --diff-dir shots/diff --montage`.

</details>

---

## Submitting a New Compatibility Report / Catalog Card

Tested a Glide title that **isn't** in the [Compatibility Catalog](../catalog/README.md) yet? Open an issue or PR (see [`docs/catalog/_template/README.md`](../catalog/_template/README.md)) including:

1. **Title, release year, edition, and patch sequence** (exact names & byte sizes of the game executable and any official/community 3dfx patches — no binary files attached!).
2. **`glide-ng.ini` preset** (and DOSBox `memsize`/`ems`/`xms` or Wine registry/DLL notes if non-default).
3. **`glide-ng.log` runs** confirming `vulkan`, `opengl_es`, and `software` status (plus any alternate card profile such as `VoodooGraphics` or `Voodoo5`).
4. **4–8 clean lossless `.png` screenshots** showing the menu, a gameplay view across backends (plus a native non-Glide software/VGA baseline if available), effects/fog/HUD, and key quirks or controls.

---

## Copy-Paste GitHub Issue Template

```markdown
### Summary
<!-- Brief one-line description of the issue -->

### Game & Setup
- **Game / Release / Patch:** <!-- e.g. Descent II v1.2 + D2_3DFX.EXE Release 6 -->
- **Glide API & Entry:** <!-- e.g. glide2x.dll (Win32) / GLIDE2X.OVL (DOS) / glide3x.dll / MiniGL -->
- **Platform / Launcher:** <!-- e.g. Arch Linux + Lutris (Wine 9.x) / DOSBox-SVN / Windows 11 -->
- **OS / Session / Display:** <!-- e.g. Fedora 41, Wayland (KDE), 2560x1440 @ 100% -->
- **GPU & Driver:** <!-- e.g. AMD RX 7800 XT (Mesa 25.x RADV) / NVIDIA 570.xx -->
- **`glide-ng` Build (`Wrapper Build` line from `glide-ng.log`):** <!-- e.g. v0.9.0+g<hash> -->
- **Verified `Loaded Module` path matches updated `glide-ng` file?** Yes / No

### Quick Backend Check
- `vulkan` (`msaa = 1`): Works / Broken / Crash / Untested
- `vulkan` (`msaa = 4`): Works / Broken / Crash / Untested
- `opengl_es`: Works / Broken / Crash / Untested
- `software`: Works / Broken / Crash / Untested
- Card profile tested: <!-- e.g. Voodoo2 / VoodooGraphics / Voodoo5 -->

### Steps to Reproduce
1. ...
2. ...
3. ...

**Frequency:** Always / Intermittent (~X of Y tries) / After ~N minutes of play

### Expected vs. Actual Behavior
<!-- What should happen, and what actually happened (include audio/CPU behavior for hangs) -->

### Attachments
<!-- Drag-and-drop zipped glide-ng.log, console.log, glide-ng.ini, perf.csv, and screenshots (NO copyrighted game files!) -->
```
