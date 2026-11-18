# `glide2_info` / `glide3_info` (`glide_info.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Hardware & Environment Diagnostic Tool (First-Party) |
| **Platform(s)** | `Linux` · `Win32 / Wine` · `DOS/32 (GLIDE2X.OVL)` |
| **Glide API & Version** | Glide 2.x (`glide2_info` / `glide_info.exe`) · Glide 3.x (`glide3_info`) |
| **Recommended Card Profile** | Any (`voodoo1` · `rush` · `voodoo2` · `banshee` · `voodoo3` · `voodoo4` · `voodoo5`) |
| **Verified Backends** | Vulkan · OpenGL ES / WGL · Software |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified |

---

## Overview

`glide2_info` and `glide3_info` inspect the active Glide library, emulated Voodoo accelerator profile, FBI/TMU memory budgets, reported bare version strings (`grGlideGetVersion` / `grGetString(GR_VERSION)`), authentic H3 `fbiRev` decoding, `GR_MAX_TEXTURE_SIZE` / `GR_BITS_RGBA`, and — in Glide 3.x — cross-verify every advertised `GR_EXTENSION` token against live `grGetProcAddress` resolution.

With `--probe` (`-p`), the tool opens an active Glide context and validates supported resolutions (`grQueryResolutions`), RGB565 LFB write/readback integrity, and TMU texture download budgets.

---

## CLI Options

```text
-v, --verbose     Print full multi-board, per-TMU, extension proc & env var report
-p, --probe       Run active HW probe (modes, LFB 565, TMU upload)
-o <file>         Write full verbose report (+ probe if -p) to file
--pause           Wait for a keypress before exiting
-h, --help        Show help message
```

---

## Usage Examples

```bash
# Linux — inspect Voodoo5 VSA-100 capabilities and verify every extension entry point
GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_HEADLESS=1 \
  ./build_release/bin/glide3_info -v --probe

# Linux — inspect all 7 emulated card profiles in a loop
for card in voodoo1 rush voodoo2 banshee voodoo3 voodoo4 voodoo5; do
  echo "=== $card ==="
  GLIDE_NG_CARD_MODEL=$card GLIDE_NG_HEADLESS=1 ./build_release/bin/glide3_info
done

# Win32 under Wine — verify 32-bit DLL loading and Vulkan ICD discovery
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=vulkan \
  wine ./build_release/win32/bin/glide3_info.exe -v --probe
```

### DOS/32 (`GLIDE2X.OVL`)
```bat
glide_info.exe -v --probe -o report.txt
```

---

## Quirks & Tips

* **Pure Glide 2.x ABI**: `glide2_info` uses strictly authentic Glide 2.x calls (`grGlideGetVersion`, `grSstQueryHardware`) and never calls `grGetString` or `grGetProcAddress` (which only exist in Glide 3.x).
* **Warn-only card/version check**: If you pass an ahistorical combination (such as `GLIDE_NG_CARD_MODEL=voodoo1 GLIDE_NG_API_VERSION=2.61`), `glide-ng` prints a `[WARN]` diagnostic while still honoring your override — useful for reproducing legacy app detection checks. See [Glide Versions & Card Matrix](../../../glide_versions_and_cards.md).
