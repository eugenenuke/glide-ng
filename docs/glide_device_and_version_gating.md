# 🎮 3dfx Glide Wrapper Device and Version Gating Guide

This guide explains how to configure and tune the emulated 3dfx hardware profiles, reported Glide API versions, and autonomic feature gating in the **glide-ng** modernization wrapper.

---

## 1. Overview of the Gating Subsystem

The **glide-ng** wrapper features an autonomic, spec-faithful dynamic feature-gating subsystem. Instead of exposing all modern features to every classic application, the wrapper dynamically adjusts its API footprint and hardware reports to match specific retro hardware configurations. 

This ensures that:
*   **Legacy Compatibility**: Classic games requiring strict Glide 2.1 or 2.40 specification footprints (like Voodoo Graphics) do not crash or behave erratically when presented with unrecognized intermediate extension entry points.
*   **Cutting-Edge Emulation**: Modern engines or advanced ports targeting Glide 2.61 (Voodoo3/Avenger) can leverage callback animators, splash execution, and advanced multitexturing features seamlessly.
*   **Authentic Telemetry**: Retro diagnostic tools and games query the hardware via `grSstQueryHardware` and receive exact board configurations, RAM sizes, and TMU counts matching their expected era.

---

## 2. Active Defaults

By default, if no configuration file or environment variables are present, the wrapper initializes with the following profile:

*   **Emulated Card Model**: `Voodoo2 (CVG)`
*   **Reported API Version**: `Glide 2.43` (The gold standard for Glide 2.x gaming)
*   **FBI Framebuffer RAM**: `16 MB` (Enables resolutions up to 1024x768 without VRAM stockouts)
*   **Active TMU Chips**: `2 TMUs`
*   **Dedicated RAM per TMU**: `16 MB`
*   **Target Presentation Frame Rate**: `60 FPS` (Capped with VSync enabled)
*   **Voodoo2 SLI Mode**: `DISABLED` (For maximum classic application interop)

---

## 3. Tuning the Emulated Device Profile

You can change the active card model to match different generations of 3dfx hardware. 

### Configuration Methods

#### Method A: Environment Variables (Highest Priority)
Set `GLIDE_DEVICE` or `GLIDE_WRAPPER_CARD_MODEL` in your shell before launching the application:
```bash
export GLIDE_DEVICE=voodoo3
```

#### Method B: JSON Configuration File
Create or modify a file named `glide_config.json` in the application's working directory:
```json
{
  "model": "Voodoo3"
}
```

### Supported Device Identifiers

The wrapper maps several common aliases to their respective hardware profiles:

| Target Card Model | Allowed Case-Insensitive Identifiers / Environment Values | Default API Version |
| :--- | :--- | :--- |
| **Voodoo Graphics** | `SST1`, `voodoo1`, `VOODOO1`, `VoodooGraphics` | **Glide 2.40** |
| **Voodoo Rush** | `SST96`, `voodoo_rush`, `rush`, `RUSH`, `VoodooRush` | **Glide 2.40** |
| **Voodoo2** | `CVG`, `voodoo2`, `VOODOO2`, `Voodoo2` | **Glide 2.43** |
| **Voodoo3** | `H3`, `voodoo3`, `VOODOO3`, `Voodoo3` | **Glide 2.61** |
| **Voodoo5** | `H5`, `voodoo5`, `VOODOO5`, `Voodoo5` | **Glide 2.61** |

---

## 4. Tuning the Reported API Version

While the card model determines the default API version, you can explicitly override the reported version. This is useful for debugging or forcing a specific game path (e.g., running a Glide 2.1 game on a Voodoo3 profile with strict 2.1 gating).

### Configuration Methods

#### Method A: Environment Variables (Highest Priority)
Set `GLIDE_VERSION_OVERRIDE` or `GLIDE_WRAPPER_API_VERSION` in your shell:
```bash
export GLIDE_VERSION_OVERRIDE=2.40
```

#### Method B: JSON Configuration File
Set `"apiVersion"` in `glide_config.json`:
```json
{
  "apiVersion": "2.40"
}
```

### Supported API Versions

The wrapper supports the following Glide major/minor specifications:

| Version Identifier | Specification Milestone | Key Features Governed by Gating |
| :--- | :--- | :--- |
| `2.1` | **Glide 2.1 Baseline** | Core rasterization, vertex buffers, basic texture maps. |
| `2.40` or `2.4` | **SST-1 Specification** | Standard Glide 2.1 + `grChromakeyValue` + `grFogMode` + base rendering. |
| `2.42` | **Voodoo2 Multitexturing** | Adds core multitexturing: `grTexMultibase`, `grTexMultibaseAddress`. |
| `2.43` | **Voodoo2 Gold Standard** | Adds dynamic entry points: `grGetProcAddress`, `grChromakeyRangeExt`. |
| `2.61` | **Voodoo3 Interop Profile** | Adds callback-driven splash execution: `grSplashCb`. |

> [!NOTE]
> **Automatic Version Gating Fallback**: If you do not specify an `apiVersion` or `GLIDE_VERSION_OVERRIDE`, the wrapper automatically selects the authentic API version that was paired with the selected hardware model (e.g., selecting `VOODOO1` automatically restricts the API footprint to `2.40`!).

---

## 5. Unlocking "Super Hardware" Configurations

In the physical world, choosing a Voodoo Graphics card limited you to 4 MB of RAM and 1 TMU. However, **glide-ng** allows you to decouple the reported card model/API version from the physical emulation limits!

This allows you to create **"Super Hardware"** profiles—for example, emulating a Voodoo Graphics card (to maintain authentic rendering paths and legacy 2.40 API behavior) but allocating 16 MB of VRAM. This enables you to run classic games at high resolutions (like 800x600 or 1024x768) that would have otherwise triggered a VRAM Stockout on real hardware!

### Customizing Hardware Memory and Limits

You can tune these limits via environment variables or in `glide_config.json`:

| Environment Variable | JSON Config Property | Default Value | Description |
| :--- | :--- | :--- | :--- |
| `GLIDE_WRAPPER_FBI_MEM_MB` | `"fbiMemoryMb"` | `16` | Total FBI Framebuffer memory in MB. Controls resolution limits. |
| `GLIDE_WRAPPER_MAX_FPS` | `"maxFps"` | `60.0` | Maximum frame rate ceiling (use `0` to uncap frame rate). |
| `GLIDE_VSYNC` | `"vsync"` | `true` | Vertical synchronization. Set to `0` or `false` to disable. |
| `GLIDE_WRAPPER_MSAA` | `"msaaSamples"` | `1` | MSAA sample count. Supports `1` (disabled), `2`, `4`, `8`, `16`. |

> [!TIP]
> To run a Voodoo1 game at 800x600 in authentic `2.40` mode without stockouts, launch it like this:
> ```bash
> export GLIDE_DEVICE=voodoo1
> export GLIDE_WRAPPER_FBI_MEM_MB=16
> ./your_classic_game
> ```

---

## 6. Verifying Your Active Configuration

You can easily inspect the active profile and check which API features are active or gated out using our built-in **Resolution Matrix & Feature Checklist** tool.

Simply run any of our bundled tools or demos (such as `glide2_gears` or `glide2_cube`) with the `-l` or `--list` argument:

```bash
build/bin/glide2_gears -l
```

### Example Diagnostic Output

When initialized under the default Voodoo2 profile, the tool prints:

```text
======================================================================
3DFX GLIDE DIAGNOSTIC TOOL - RESOLUTION MATRIX (GLIDE 2 API)
Active Device: Voodoo2 (CVG) | Emulated VRAM: 16.00 MB
======================================================================
  [*]  320x200    (0.37 MB required)
  [*]  320x240    (0.44 MB required)
  [*]  400x256    (0.59 MB required)
  [*]  400x300    (0.69 MB required)
  [*]  512x384    (1.12 MB required)
  [*]  640x480    (1.76 MB required)
  [*]  800x600    (2.75 MB required)
  [*]  856x480    (2.35 MB required)
  [*]  1024x768   (4.50 MB required)
  [*]  1280x1024  (7.50 MB required)
  [*]  1600x1200  (10.99 MB required)
======================================================================
glide2_gears (Glide 2.x API)
======================================================================
Glide Version:   2.43 (Latest)
Resolution:      640x480 (1.76 MB VRAM required)
----------------------------------------------------------------------
Emulated Device: Voodoo2 (CVG) (3dfx Voodoo2)
Device Vendor:   3Dfx Interactive
VRAM Size:       16.00 MB
Active TMUs:     2 TMUs
SLI Detect:      No
----------------------------------------------------------------------
Active Driver:   3dfx glide-ng Vulkan Wrapper
----------------------------------------------------------------------
Available Minor-Version API Features:
  [2.42 Features (Voodoo2 Multitexturing)]
    [X] grTexMultibase
    [X] grTexMultibaseAddress
  [2.43 Features (Voodoo2 Gold Standard)]
    [X] grGetProcAddress
    [X] grChromakeyRangeExt
    [X] grAlphaControlsITRGBLighting
  [2.61 Features (Voodoo3 Splash Callbacks)]
    [ ] grSplashCb
----------------------------------------------------------------------
Extensions:
  [X]  BLEND
  [X]  BUFFERCLEAR
  [X]  CHROMARANGE
  [X]  COLORMASK
  [X]  LFBSTENCIL
  [X]  STENCIL
  [X]  TBUFFER
======================================================================
```

Notice that since the active API profile is `2.43`, the `2.42` and `2.43` features are fully checked (`[X]`), but the `2.61` feature (`grSplashCb`) is safely gated out (`[ ]`).

---

## 7. Gated Function Execution Behavior

When a game or application attempts to invoke a function that is gated out under the active profile:

1.  **Direct Static Calls** (e.g., calling `grTexMultibase` directly when running a `2.40` Voodoo1 profile):
    *   The wrapper interceptor detects that the function is unavailable.
    *   It logs a warning in `glide-ng.log`:
        `[WARN] [API] GATED CALL: grTexMultibase is not available under the active version/device profile.`
    *   It returns immediately and safely acts as a **no-op** to prevent memory corruption or driver crashes.
2.  **Dynamic Queries** (e.g., calling `grGetProcAddress("grSplashCb")` when running a `2.43` Voodoo2 profile):
    *   The query interceptor detects the version mismatch.
    *   It logs a warning:
        `[WARN] [Frontend] grGetProcAddress: Gating out 'grSplashCb' under active version/device profile`
    *   It returns `nullptr` to the application, perfectly matching the behavior of classic drivers which did not support that extension.
