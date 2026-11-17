# 3dfx Glide Wrapper End-User Guide: Vendor & Renderer Overrides

This document guides end-users, modders, and game developers on how to configure and override the reported hardware vendor and renderer strings returned by the **3dfx `glide-ng` Modernization Wrapper**.

---

## 1. Why Vendor Strings Matter for Retro Games

When legacy 1990s games (such as *Quake II*, *Unreal*, or *Tomb Raider*) or retro benchmark suites (such as *3DMark 99*) initialize graphics, they invoke `grGetString()` to query the physical host accelerator.

### The Retro Hardware Check
Many classic game engines execute rigid ASCII substring checks on the returned identification string to verify whether an authentic 3dfx accelerator is installed:

```cpp
const char* vendor = grGetString(GR_VENDOR);
if (strstr(vendor, "3Dfx") != NULL || strstr(vendor, "3dfx") != NULL) {
    // Unlock exclusive Voodoo multi-texturing optimizations
    // Enable proprietary palette palettized textures (NCC / P8)
} else {
    // Fallback to slower unoptimized software emulation branches or crash
}
```

If a modern wrapper returns an inauthentic string like `"Antigravity AI / Vulkan"`, rigid retro games will fail their authenticity verification, resulting in degraded graphics or startup crashes.

---

## 2. Default Configuration

Out-of-the-box, **`glide-ng`** strictly returns authentic 1990s 3dfx hardware strings to maximize absolute plug-and-play compatibility with classic retro games:

| Query Parameter | Legacy API Define | Default Returned String | Rationale |
| :--- | :--- | :--- | :--- |
| **`GR_VENDOR`** | `0xa3` | `"3Dfx Interactive"` | Unlocks authentic 3dfx game engine codepaths. |
| **`GR_RENDERER`** | `0xa2` | `"3dfx glide-ng Vulkan Wrapper"` | Beautifully identifies our modern Vulkan translation engine. |
| **`GR_HARDWARE`** | `0xa1` | `"Voodoo2 (Modernized Wrapper Emulation)"` | Varies dynamically based on the configured Voodoo model. |

---

## 3. How to Override (End-User Instructions)

You can easily override these reported strings using either your local configuration profile or system environment variables.

### Method A: Configuration Profile (`glide-ng.ini`)
Place a file named `glide-ng.ini` inside your active game execution directory and set `vendor` and `renderer` under `[hardware]`:

```ini
[hardware]
card_model = Voodoo2
vendor = MyCustomBrand
renderer = Custom Vulkan Renderer
```

### Method B: Environment Variables
For automated multi-game launchers or global overrides, set the following environment variables before launching your legacy executable:

* **Linux (Bash)**:
  ```bash
  export GLIDE_NG_VENDOR="MyCustomBrand"
  export GLIDE_NG_RENDERER="Custom Vulkan Provider"
  ./my_retro_game.bin
  ```

* **Windows (PowerShell)**:
  ```powershell
  $env:GLIDE_NG_VENDOR="MyCustomBrand"
  $env:GLIDE_NG_RENDERER="Custom Vulkan Provider"
  .\my_retro_game.exe
  ```

---

## 4. Troubleshooting & Verification

To verify that your custom overrides are successfully injected, launch your game and inspect the auto-generated **`glide-ng.log`** file in your working directory. You will see an authoritative ASCII summary confirming your active reported values:

```text
========================================================
       3DFX glide-ng (v0.1.0-dev) EMULATION PROFILE
========================================================
  Emulated Accelerator : Voodoo2 (CVG)
  FBI Framebuffer RAM  : 16 MB
  Active TMU Chips     : 2
  Reported Vendor Base : MyCustomBrand
  Reported API Base    : 2.43 (Latest)
========================================================
```
