# Troubleshooting Guide

Practical steps for diagnosing startup, windowing, visual, and performance issues in `glide-ng`.

---

## 1. Verify the Active Configuration First

Every `glide-ng` session writes a startup banner to `glide-ng.log` (or `stdout` when `GLIDE_NG_LOG_CONSOLE=1`). Check it first to confirm:
- Which library binary was actually loaded (`Module: ...`).
- Whether `glide-ng.ini` was found and loaded (and whether any unrecognized keys triggered `[WARN]`).
- Active backend (`vulkan`, `opengl_es`, `software`), emulated card model, reported API version, and frame-pacing settings (`vsync`, `max_fps`).

Quick sanity check using the bundled diagnostic tools:

```bash
# Linux
GLIDE_NG_LOG_CONSOLE=1 ./build_release/bin/glide2_info -v --probe

# Win32 / Wine
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_LOG_CONSOLE=1 wine ./glide2_info.exe -v --probe
```

---

## 2. Common Issues & Fixes

### Game Does Not Load `glide-ng` Under Wine / Proton

- **Symptom**: Game falls back to software rendering or reports no 3dfx card found, and no `glide-ng.log` is created.
- **Fix**:
  1. Confirm `glide.dll`, `glide2x.dll`, and/or `glide3x.dll` sits in the **same directory** as the game `.exe`.
  2. Pass `WINEDLLOVERRIDES="glide,glide2x,glide3x=n"` (or `"glide=n"` / `"glide2x=n"` / `"glide3x=n"`).
  3. Confirm which DLL the game imports (`glide.dll` for Glide 2.11, `glide2x.dll` for Glide 2.3–2.6, `glide3x.dll` for Glide 3.x, or note if it is a raw PCI probe like `sst1init.dll`):
     ```bash
     i686-w64-mingw32-objdump -p game.exe | grep "DLL Name:"
     ```

### Vulkan Fails to Initialize Under 32-Bit Wine

- **Symptom**: `glide-ng.log` reports Vulkan instance/device creation failure or missing `vulkan-1.dll` under Wine, while native 64-bit Vulkan works.
- **Fix**:
  - Install your distro's 32-bit Vulkan ICD (`mesa-vulkan-drivers:i386` on Debian/Ubuntu, `lib32-vulkan-radeon` / `lib32-vulkan-intel` / `lib32-nvidia-utils` on Arch).
  - Or switch temporarily to WGL OpenGL 3.3 Core (`GLIDE_NG_BACKEND=opengl_es`) or CPU Software (`GLIDE_NG_BACKEND=software`).

### 1996–1997 Voodoo Graphics Title Looks Too Dark

- **Symptom**: Classic SST-1 games or demos (*Demo Room*, *Wizard's Tower*, *GLQuake*, *Tomb Raider*) appear noticeably darker than expected.
- **Cause**: Original SST-1 hardware defaulted its RAMDAC gamma LUT to `1.7` (`1.3` on Voodoo2), whereas `glide-ng` defaults to linear `1.0` so brighter and modern titles are not washed out.
- **Fix**: Add `gamma = 1.7` under `[glide]` in `glide-ng.ini`, or launch with `SST_GAMMA=1.7` (or `SSTV2_GAMMA=1.3` for Voodoo2 titles).

### Frame Rate Is Capped at 30 FPS or Stutters Under Vulkan VSync

- **Symptom**: A title runs at ~30 FPS under Vulkan VSync on a 60 Hz display while Software/GLES run at 60 FPS.
- **Cause**: Strict FIFO VSync combined with software `max_fps = 60` pacing can miss one 16.6 ms refresh boundary and drop to half refresh (30 Hz).
- **Fix**:
  - Set `GLIDE_NG_MAX_FPS=0` when `vsync = true` so only VSync paces frames (or set `GLIDE_NG_VSYNC=0` with `GLIDE_NG_MAX_FPS=60`).
  - On Wayland, `glide-ng` prefers `VK_PRESENT_MODE_MAILBOX_KHR` automatically when `vsync = true`; avoid `GLIDE_NG_VSYNC=fifo` unless strict FIFO tearing prevention is specifically needed.

### Game Runs Too Fast or Menu Timing Breaks

- **Symptom**: Physics, menus, or timers run at hyper-speed on modern CPUs/GPUs.
- **Fix**: Keep `[display] max_fps = 60` (or set a lower cap such as `max_fps = 30` or `SST_SCREENREFRESH=60`).

### Voodoo4/5 Demo Reports Missing Extensions (`PIXEXT`, `COMBINE`, `TEXFMT`)

- **Symptom**: VSA-100 demos (*Kitty Hawk*, *Museum*, *Rocket Burger*) fail capability checks or disable 4× Spatial AA / T-Buffer.
- **Cause**: The default card profile is `Voodoo2` (`3.03`), which intentionally does not advertise VSA-100 extensions.
- **Fix**: Set `GLIDE_NG_CARD_MODEL=voodoo5` (or `[hardware] card_model = Voodoo5` in `glide-ng.ini`).

### Game Fails or Flickers on Redundant `grSstWinOpen`

- **Symptom**: Title calls `grSstWinOpen` twice without closing the first window, resetting or blanking the framebuffer.
- **Fix**: Enable `FX_GLIDE_IGNORE_REOPEN=1` (or `[glide] ignore_reopen = true`).

### Standalone Linux Window Issues Under Wayland / Remote Desktop

- **Symptom**: Standalone SDL2 demos or tools behave unexpectedly on certain Wayland compositors or RDP sessions.
- **Fix**: Run with `SDL_VIDEODRIVER=x11` to use XWayland, or `GLIDE_NG_HEADLESS=1` for non-interactive capture and verification.

---

## 3. Diagnostic Recipes

### Cross-Backend Visual Bisection

Because all three backends share `GlideWrapperCore` and `Software` serves as a bit-exact reference rasterizer, switching backends isolates bugs immediately:

```bash
# Compare the same frame across all three backends
for be in software vulkan opengl_es; do
  GLIDE_NG_BACKEND=$be FX_GLIDE_SNAPSHOT="/tmp/snap_${be}.ppm@10" <game_command>
done
```

- **Bug appears on all three backends**: Likely in the frontend (`src/glide2/` or `src/glide3/`), `TextureManager`, `VertexLayout`, or card/version configuration.
- **Bug appears only on Vulkan or OpenGL ES (correct on Software)**: Backend-specific shader, pipeline state, blending, or LFB overlay divergence.

### Targeted Frame Tracing Without Huge Logs

Capture `TRACE`-level API and LFB calls only for the exact frames where a glitch occurs:

```bash
GLIDE_NG_LOG_LEVEL=trace \
  GLIDE_NG_LOG_CATEGORIES=api,lfb,tex,state \
  GLIDE_NG_TRACE_FRAMES=120-125 \
  GLIDE_NG_LOG_FILE=/tmp/glide_trace.log \
  <game_command>
```

### Bottleneck Diagnosis (`wrapper_cpu_ms` vs. `app_cpu_ms`)

Enable per-frame CSV telemetry to separate game/emulator overhead from wrapper rendering cost:

```bash
GLIDE_NG_PERF_LOG=/tmp/perf.csv GLIDE_NG_HUD=fps,frametimes,drawcalls,lfb <game_command>
```

Inspect the CSV columns:
- High `app_cpu_ms` with low `wrapper_cpu_ms`: Bottleneck is in the game engine or DOSBox/Wine CPU emulation.
- High `lfb_reads` / `lfb_cpu_ms` on Vulkan/GLES: Game performs synchronous framebuffer readbacks; test `GLIDE_NG_BACKEND=software` (`GLIDE_NG_SW_SIMD=avx512` on AVX-512 CPUs) to avoid PCIe readback stalls.
- High `pso_builds` on early frames: One-time Vulkan pipeline compilation; verify `.glide-ng-vkcache.bin` is writable so subsequent runs reuse cached pipelines.
- High `cap_sleep_ms`: Frame rate is being intentionally paced by `max_fps`.
