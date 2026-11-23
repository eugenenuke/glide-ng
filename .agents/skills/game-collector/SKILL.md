---
name: game-collector
description: Guided compatibility testing and bug-report collection for 3dfx Glide games on glide-ng. Defaults to a compact Standard workflow suitable for free-tier AI models (offloading static recon, log diffing, launcher scripts, and packaging to Python stdlib scripts), with an opt-in Deep mode for visual review, framed API tracing, crash backtraces, and catalog PR prep.
---

# `game-collector` — Guided Glide Game Testing & Report Bundle Skill

Use this skill when a user wants to test a classic 3dfx Glide game on `glide-ng`, troubleshoot a game that fails to boot, or assemble a clean, reproducible **bug report / compatibility bundle** (logs, `glide-ng.ini`, binary recon, telemetry, and screenshots).

Two operating modes share the same scripts through progressive disclosure:
- **`Standard` (default — low-token, free-tier friendly):** Script-first workflow in **3 short turns**. Keeps command output compact, avoids loading images into the prompt, skips heavy docs unless an issue occurs, and leaves install/patching/gameplay screenshots to the human.
- **`Deep` (opt-in — power/Pro users or tricky bugs):** Escalate **only** when the user asks (`"deep mode"`, `"pro"`, `"inspect screenshots"`, `"debug capture"`, `"prepare catalog PR"`) or a run crashes/renders wrong after Standard steps. Load [`references/deep_analysis.md`](references/deep_analysis.md) when escalating.

**Hard safety rules (both modes):**
1. **Never bundle or commit copyrighted game files** (`.exe`, `.dll`, `.ovl`, `.iso`, `.hog`, `.mpq`, `.pak`, `.wad`, audio/video assets). Only plain-text recon reports, `glide-ng.log`, `.ini`/`.cfg`, telemetry CSVs, screenshots, and launcher scripts belong in reports.
2. **Never dump full binaries or raw `strings` into chat context.** Always run the bundled Python 3.8+ (stdlib-only, zero `pip`) scripts inside `scripts/` and keep stdout small.
3. **Never write to or mutate installed game binaries.** Static recon is strictly read-only.
4. **Preserve every log run separately.** `glide-ng.log` overwrites on each launch — always use unique `GLIDE_NG_LOG_FILE` paths via `gen_launchers.py`.

---

## Pre-Check (Human Prerequisite)

Ask the human only for what is missing (one short message):
1. Path to the **already-installed and patched** game directory (note edition: retail CD, GOG, Steam, fan patch).
2. Target platform: **`dosbox`** (DOSBox-SVN / DOSBox-X), **`wine`** (Wine / Proton / Lutris), or **`windows`** (native Win32).
3. Goal: **compatibility check** (game works / testing matrix) or **bug triage** (symptom: crash, black screen, hang, glitch, stutter).
4. Optional mode choice: stay in **`Standard`** (default) or run **`Deep`**.

Create one output folder outside the game directory, e.g. `bundle_<game_slug>/` (`logs/`, `screens/`, `glide-ng.ini`, `binary_recon.md`, `NOTES.md`).

---

## Standard Workflow (3 Fast Turns)

### Turn 1 — Static Recon & Isolated Launchers (Read-Only)

Run `glide_recon.py` once to classify Glide linkage (`glide.dll` / `glide2x.dll` / `glide3x.dll` / `GLIDE2X.OVL` vs static `sst1Init` DOS4GW), conflicting OEM DLLs, CLI flags, and 3dfx env vars:

```bash
python3 scripts/glide_recon.py <game_dir> --out <bundle>/binary_recon.md --max-stdout 60
```

Read **only the compact stdout digest** (and any OEM/wrapper DLL warnings). Immediately flag blockers:
- **Statically linked `sst1Init` DOS binary** (`gr*` symbols baked inside `.exe`, no `GLIDE2X.OVL` reference) → cannot intercept dynamically via `GLIDE2X.OVL`; see [`references/triage_table.md`](references/triage_table.md).
- **Conflicting local `glide*.dll` / `nGlide` / `glide2x.ovl`** → warn the user to rename/back up before launching so `glide-ng` actually loads.

Generate isolated per-backend launchers so `glide-ng.log` is never overwritten:

```bash
python3 scripts/gen_launchers.py \
  --out-dir <bundle> \
  --platform <dosbox|wine|windows> \
  --cmd '<launch command, e.g. dosbox -conf dosbox.conf or ./GAME.EXE>' \
  --alt-card voodoo1
```

Optional flags: `--dll-override 'glide,glide2x,glide3x=n'` (Wine), `--wine-desktop 800x600`.
*(Check `<game_dir>` or [docs/catalog/](../../../docs/catalog/README.md) first if the game already has a drop-in `glide-ng.ini` preset.)*

### Turn 2 — Hand Off Execution to the Human

Give the human a **short numbered checklist** to run the generated scripts, play 30–60 s (menu + 1 gameplay scene), and save lossless `.png` shots under `<bundle>/screens/`:

1. `run_01_vk_1x` (`vulkan`, `msaa=1`, `nearest`, writes `logs/vk_1x.log` + perf CSV) → screenshot `01_vk_menu.png`, `02_vk_game.png`
2. `run_02_vk_hq` (`vulkan`, `msaa=4`, `linear`, `FX_GLIDE_NO_DITHER_SUB=1`) → screenshot `03_vk_hq.png` *(only if clean or testing MSAA seams)*
3. `run_03_gles` (`opengl_es`, `msaa=1`) → screenshot `04_gles_game.png`
4. `run_04_sw` (`software`, reference rasterizer, `msaa=1`) → screenshot `05_sw_game.png`
5. *(Optional)* `run_05_<alt_card>` (e.g. `voodoo1` or `voodoo5`) if probing card identity quirks.
6. If any run **fails, hangs, crashes, or shows a blank screen**, run `run_debug_capture` and **load [`references/triage_table.md`](references/triage_table.md)** for targeted fixes (max 3–4 probe iterations before escalating).

Record brief host environment specs once into `<bundle>/host_env.txt` (`uname -a`, GPU/driver, X11/Wayland, Wine/DOSBox version — see [`docs/user-guide/reporting-bugs.md`](../../../docs/user-guide/reporting-bugs.md)).

### Turn 3 — Summarize Logs, Verify & Package

Summarize all logs + optional perf CSV without reading full raw logs into chat context:

```bash
python3 scripts/summarize_log.py <bundle>/logs/*.log <bundle>/logs/*.csv --out <bundle>/log_summary.md
```

Confirm from `log_summary.md`:
- Every log's `Wrapper Build` matches the same `glide-ng` version (no mixed/stale binaries).
- `Loaded Module` points to `glide-ng`, not a leftover OEM or `system32` DLL.
- Note total `CRIT` / `WARN` counts, frame count, duration, active resolution, gamma, and top perf spike frames.

Draft a concise `<bundle>/NOTES.md` using the GitHub issue sections from [`docs/user-guide/reporting-bugs.md`](../../../docs/user-guide/reporting-bugs.md) (title/version/patch, `.exe` name + exact byte size from `binary_recon.md`, backend matrix table, quirk notes, `glide-ng.ini` preset, controls used).

Run **copyright/secret + zero-vision screenshot check** and pack the uploadable archive:

```bash
python3 scripts/pack_bundle.py <bundle> --out <bundle>.tar.gz
# or --out <bundle>.zip on Windows (add --check-only to validate without writing an archive)
```

Verify output has **0 blocked/secret errors**, no `ALL-BLACK` / `solid single color` / `[DUPLICATE]` screenshot warnings, and give the human a copy-pasteable GitHub issue summary ready to attach with `<bundle>.tar.gz`.

---

## Script & Reference Index

| Path | When to use |
| :--- | :--- |
| [`scripts/glide_recon.py`](scripts/glide_recon.py) | Turn 1: Read-only PE/DOS/LE/LX recon + conflicting DLL scan. |
| [`scripts/gen_launchers.py`](scripts/gen_launchers.py) | Turn 1: Emit 6 isolated launcher scripts (`.sh` or `.ps1`). |
| [`scripts/summarize_log.py`](scripts/summarize_log.py) | Turn 3: Deduplicated log matrix + `perf.csv` p50/p95/p99 spikes. |
| [`scripts/pack_bundle.py`](scripts/pack_bundle.py) | Turn 3: Zero-vision PNG/PPM validator, copyright/secret guard, archive packer. |
| [`references/triage_table.md`](references/triage_table.md) | **On-demand only:** Game crashes, black screens, wrong cards, audio/gamma/WSI quirks. |
| [`references/deep_analysis.md`](references/deep_analysis.md) | **Opt-in Deep mode:** Visual comparison, `FX_GLIDE_SNAPSHOT`, `GLIDE_NG_TRACE_FRAMES`, `gdb`/RenderDoc, catalog PRs. |
