# Deep Analysis & Catalog PR Workflow (Opt-In Pro Mode)

Load this reference **only** when the user asks for **Deep / Pro** verification (visual diffing across backends, targeted API tracing, crash backtraces, RenderDoc captures, or preparing a `docs/catalog/` PR).

Keep outputs scoped: never paste giant raw `trace` files, `objdump` listings, or multi-megabyte archives directly into context.

---

## 1. Visual Cross-Backend Audit (Same Scene, Clean HUD)

Disable the HUD (`GLIDE_NG_HUD=0`) for gallery shots. Capture matching lossless `.png` frames at the **same camera spot** across all three backends (plus a baseline non-Glide renderer if the game ships one, e.g. DOS VGA/SVGA or Win32 Software/D3D):

| Slot | Filename convention | Purpose |
| :--- | :--- | :--- |
| Title / menu | `01_menu_vk.png` | Verifies font clarity, gamma, front-buffer LFB flushes, chroma-key edges |
| Baseline (software rasterizer) | `02_scene_sw.png` | Reference frame for alpha/fog/z-order/texturing parity |
| Vulkan 1× (`nearest`) | `03_scene_vk_1x.png` | Sharp authentic check against `sw` baseline |
| Vulkan 4× (`linear`, `no_dither_sub=1`) | `04_scene_vk_hq.png` | Checks MSAA seams, alpha-test edges, hairline UI gaps |
| OpenGL ES / WGL | `05_scene_gles.png` | Parity check vs Vulkan + `sw` |
| Effects / fog / HUD / cockpit | `06_fx_hud.png` | Tests table fog, translucent explosions, `grLfbWriteRegion`, `grGlideSetState` |
| Perf HUD frame *(bug report only)* | `07_hud_full.png` | Single shot with `GLIDE_NG_HUD=full` during slowdowns/stutters |

When reviewing screenshots visually, compare **`vk_1x` vs `sw`** and **`gles` vs `sw`** specifically for:
- Missing wireframe/HUD digits or clipped polygons (`oow` precision / vertex stride)
- Wrong fog color/density or flat skybox colors (`grFogMode` / `FOGCOORD` / `oow` clamp)
- Chroma-key halos or flipped/black transparent UI sprites (`grChromakeyMode` / `grAlphaTestFunction`)
- Un-swapped dark/stale menu patches (`FrontBufferFlushTimer` / direct LFB front writes)
- Dark/crushed blacks vs CRT gamma (`SST_GAMMA=1.3`..`1.7`)
- Texture seams / 1 px gaps on `msaa=4`

---

## 2. Frame-Windowed API Trace, Multi-Frame Snapshots & Visual Diffs

Full `trace` logs grow fast because every triangle is printed individually. Always identify the problem frame number **`[FNNNNNN]`** from the `info`/`debug` run (`summarize_log.py` highlights top spike/warn frames), then capture a **2–5 frame window** plus deterministic `.ppm` snapshot(s):

```bash
GLIDE_NG_BACKEND=vulkan \
  GLIDE_NG_LOG_LEVEL=trace \
  GLIDE_NG_TRACE_FRAMES=1419-1422 \
  GLIDE_NG_LOG_CATEGORIES=api,state,tex,lfb,draw \
  GLIDE_NG_LOG_FILE=logs/trace_1419_1422.log \
  FX_GLIDE_SNAPSHOT=screens/frame.ppm@1419,1420,1421 \
  <launch_command>
```

For single-run progression galleries across `vulkan`, `opengl_es`, and `software`, use periodic interval syntax (`@<start>+<step>[:<max>]` or `<dir>/@<start>+<step>[:<max>]`):

```bash
# Capture every 120 frames starting at frame 300 (up to 6 shots) into screens/vk/
FX_GLIDE_SNAPSHOT=screens/vk/@300+120:6 <launch_command>

# Periodic X11 window capture (OpenGLide / non-glide-ng DOSBox):
python3 scripts/gallery_tool.py capture --out screens/og --match "DOSBox" --interval 2.0 --skip-black --skip-dup

# Extract sampled frames from a DOSBox-X / Staging ZMBV AVI capture:
python3 scripts/gallery_tool.py extract capture/game_000.avi --out screens/dbx-sw --fps 0.5 --skip-black

# Compare candidate captures against Software baseline (+ side-by-side amplified diff montages):
python3 scripts/gallery_tool.py compare screens/sw screens/vk --diff-dir screens/diff --montage --gain 4

# Convert raw .ppm / .png shots into compact WebP or PNG assets:
python3 scripts/gallery_tool.py pack screens/vk --out docs/catalog/games/<slug>/screenshots
```

For longer state/texture runs without triangle spam, drop `draw`:

```bash
GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CATEGORIES=-draw \
  GLIDE_NG_LOG_FILE=logs/nodraw.log <launch_command>
```

`pack_bundle.py` inspects `P6` `.ppm`, `.png`, and `.webp` headers as well.

---

## 3. Crash / Hang Backtraces & GPU Validation

### Live thread backtrace on a frozen Linux process (`gdb`)
*(Extract the matching `debug-symbols.zip` next to `libglide*.so` for source lines when using a release binary.)*

```bash
PID=$(pgrep -n -f 'dosbox|wine|\.exe')
gdb -batch -p "$PID" \
  -ex "set pagination off" \
  -ex "info threads" \
  -ex "thread apply all bt" > logs/backtrace.txt 2>&1
```

If already exited with `SIGSEGV`, grab `coredumpctl info -1 > logs/crash.txt 2>&1` (if enabled).

### Wine crash hints
Run with `WINEDEBUG=err+all,fixme-all,+seh,+loaddll` and check `logs/*_console.log` for:
- `Call from ... to unimplemented function` (missing export or ordinal mismatch)
- Access violation offsets (`0x00000000` null deref vs high address in `glide*.dll`)
- `Loaded Module` vs unexpected `system32` shim loading ahead of `glide-ng`

### Vulkan validation layer capture (Vulkan-only bugs)

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  GLIDE_NG_LOG_LEVEL=debug GLIDE_NG_LOG_CONSOLE=1 \
  GLIDE_NG_LOG_FILE=logs/vkval.log \
  <launch_command> 2>&1 | tee logs/vkval_console.log
```

### RenderDoc single-frame capture (GPU-only rendering bugs)
If **`software` renders fine** while `vulkan` or `opengl_es` is wrong, capture **one** `.rdc` frame with RenderDoc (compress with `.zst`/`.zip` if small; note `pack_bundle.py`'s `--max-file-mb` default is `25 MB`).

---

## 4. Surgical Binary Inspection (Read-Only, Private Scratch Only)

When `glide_recon.py` isn't enough (e.g., hunting the exact `grSstQueryHardware` branch, undocumented CLI flag, or embedded `.dll` version guard):
- Run **`objdump -p`** / **`llvm-readobj`** on PE imports or small targeted **`strings -n 6`** searches filtered by regex (`grSst|grGlide|Voodoo|SST|FX_GLIDE|-nosound|-3dfx`).
- Summarize findings as **factual, high-level notes** (`"checks fbiRev >= 0x102"`, `"supports -3dfx and -640x480"`).
- **Never** copy-paste large decompiled code blocks or copyrighted binary slices into public GitHub issues or catalog pages.

---

## 5. Preparing a `docs/catalog/` PR (Repo Maintainers / Contributors)

Follow [`docs/catalog/_template/README.md`](../../../../docs/catalog/_template/README.md) and [`tech-writer`](../../tech-writer/SKILL.md):

1. Create `docs/catalog/games/<slug>/` with:
   - **`glide-ng.ini`** (minimal validated drop-in preset; no PII or machine paths)
   - **`README.md`** (present tense, third-person voice, editions/executables table with exact byte sizes, backend matrix, quirks/controls, relative links only)
   - Optional `screenshots/` gallery encoded as lightweight `.webp` (`cwebp -q 82` or lossless for crisp HUD shots) — keep total size compact.
2. Add the title entry row inside `docs/catalog/README.md` (alphabetical, accurate counts).
3. **Validate the catalog INI against the live loader:**
   ```bash
   GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software \
     ./build/bin/Glide3xWrapperTests "[Catalog]"
   ```
4. Verify zero leaked paths (`/home/`, `/Users/`, `C:\Users\`), broken relative links, or copyrighted binary files before committing.
