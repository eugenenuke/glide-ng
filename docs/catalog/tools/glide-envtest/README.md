# `glide2_envtest` / `glide3_envtest` (`glide_envtest.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Environment Variable & Override Verifier (First-Party) |
| **Platform(s)** | `Linux` · `Win32 / Wine` · `DOS/32 (GLIDE2X.OVL)` |
| **Glide API & Version** | Glide 2.x (`glide2_envtest` / `glide_envtest.exe`) · Glide 3.x (`glide3_envtest`) |
| **Recommended Card Profile** | `Voodoo2` (default) |
| **Verified Backends** | Vulkan · OpenGL ES / WGL · Software |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) |
| **Status** | Verified |

---

## Overview

`glide2_envtest` and `glide3_envtest` render a permanent **4-quadrant visual test card** and optionally run deterministic RGB565 LFB pixel probes (`--verify`) to prove that `glide-ng.ini` settings and authentic 3dfx environment variables (`FX_GLIDE_*`, `SST_*`, `SSTV2_*`) are taking effect across Linux, Win32/Wine, and DOS/32.

---

## CLI Options

```text
--verify              Run deterministic probe pass & print verdict report
--frames <N>          Render N frames then exit (default: 5 with --verify)
--seconds <N>         Render for N seconds then exit (interactive mode)
--res <512|640|800|1024> Resolution (default: 640)
--test-reopen         Issue redundant grSstWinOpen to test FX_GLIDE_IGNORE_REOPEN
--test-bounds         Submit out-of-bounds triangle to test FX_GLIDE_BOUNDS_CHECK
--splash              Call grSplash() on startup to test FX_GLIDE_NO_SPLASH
--shameless-plug      Request app-level shameless plug to test FX_GLIDE_SHAMELESS_PLUG
--pause               Wait for keypress before exiting
-h, --help            Show help message
```

---

## Usage Examples

```bash
# Linux — automated override verification pass
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan \
  ./build_release/bin/glide2_envtest --verify --test-reopen --test-bounds

# Test specific authentic 3dfx overrides (gamma, bilinear, ignore-reopen, bounds check)
GLIDE_NG_HEADLESS=1 SST_GAMMA=1.7 SSTV2_FORCE_BILINEAR=1 \
  FX_GLIDE_IGNORE_REOPEN=1 FX_GLIDE_BOUNDS_CHECK=1 \
  ./build_release/bin/glide3_envtest --verify --test-reopen --test-bounds

# Win32 under Wine
WINEDLLOVERRIDES="glide2x=n" GLIDE_NG_BACKEND=vulkan \
  wine ./build_release/win32/bin/glide2_envtest.exe --verify --test-reopen --test-bounds
```

---

## Visual Inspection Mode

Omit `--verify` (and `GLIDE_NG_HEADLESS`) to open an interactive window displaying the 4-quadrant card (smooth gradient for `no_dither_sub`, checker texture for `force_bilinear` / `texmap_disable`, gamma ramp for `SST_GAMMA`, and corner markers for `shameless_plug` / `bounds_check`). Press `Esc` to exit.
