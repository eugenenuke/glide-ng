# 3dfx Glide Wrapper Environment Variables Guide

Legacy 3dfx Glide applications and games frequently tune their rendering capabilities and diagnostic reporting through environment variables. To maximize compatibility and provide powerful emulation controls, this modernized Glide wrapper fully supports both the **authentic 3dfx SDK environment variables** and a new set of **Modernized Wrapper Configuration Overrides**.

---

## 1. Authentic 3dfx SDK Environment Variables

The following environment variables are natively parsed by the legacy Glide 2.x and Glide 3.x core libraries (`gpci.c`, `minihwc.c`, etc.). Our modernized wrapper intercepts these variables to preserve classic behavior and diagnostic overrides.

| Environment Variable | Description | Emulated Wrapper Behavior |
| :--- | :--- | :--- |
| **`FX_GLIDE_SWAPINTERVAL`** | Natively overrides the VSync presentation interval (`grBufferSwap`). | Overrides swapchain FIFO sync. `0` disables VSync (immediate present), `1+` enforces frame synchronization. |
| **`FX_GLIDE_NO_SPLASH`** | Disables the rotating 3dfx Glide splash screen on application startup (`grSplash`). | Surpresses legacy startup boot animations. |
| **`FX_GLIDE_SHAMELESS_PLUG`**| Enables or disables the semi-transparent 3dfx corner watermark logo during rendering. | Toggles custom overlay watermark. |
| **`FX_GLIDE_FBRAM`** | Overrides detected Frame Buffer Interface (FBI) video RAM in megabytes. | Forces `grSstQueryHardware` to report exactly `N` MB of frame RAM. |
| **`FX_GLIDE_TMU_MEMSIZE`** | Overrides Texture Mapping Unit (TMU) dedicated memory capacity. | Scales maximum reported texture budget per chip. |
| **`FX_GLIDE_BOUNDS_CHECK`** | Enables internal geometry and triangle clipping coordinate verification. | Activates strict viewport coordinate debugging logs. |
| **`FX_GLIDE_IGNORE_REOPEN`** | Forces Glide to ignore subsequent `grSstWinOpen` calls if a context is already active. | Prevents legacy window re-initialization crashes. |
| **`FX_GLIDE_NO_DITHER_SUB`** | Disables legacy 16-bit dither subtraction algorithms. | Overrides automated dither dithering state passes. |
| **`FX_CPU`** | Overrides automated assembly CPU detection (`cpuid`). | Changes reported host capabilities (Pentium, MMX, 3DNow!). |
| **`FX_SNAPSHOT`** | Automated hotkey trigger to capture framebuffer raw image dumps. | Saves current swapchain presentation image to disk as an artifact. |
| **`FX_GLIDE_LOG_FILE`** | Redirects all `GDBG_INFO` and Glide internal diagnostic output to a specified file. | Pipes wrapper debug execution traces to custom logs. |
| **`SST_DUALHEAD`** | Controls dual-monitor initialization and multi-screen SST context sharing. | Allocates multi-window display surfaces. |
| **`SSTV2_FASTMEM`** | Enables ultra-fast SDRAM timing configurations on Voodoo2 chipsets. | Disables artificial wrapper bus latency constraints. |
| **`H3_MEM_CLOCK` / `H5`** | Overrides operating memory clock frequency in MHz for Voodoo3/4/5 series cards. | Scales internal game performance multiplier math. |

---

## 2. Modernized Wrapper Configuration Overrides

In addition to legacy parameters, our wrapper introduces the `GLIDE_WRAPPER_` namespace. These variables allow users and testers to override the physical hardware properties of the emulated Voodoo accelerator without modifying configuration files (`glide_config.json`).

| Modern Variable | Target Options / Format | Rationale & Practical Effect |
| :--- | :--- | :--- |
| **`GLIDE_WRAPPER_BACKEND`** | `VULKAN`, `EGL`, `OPENGL` | Hot-swaps the underlying active rendering engine at runtime. Excellent for instant cross-backend verification. |
| **`GLIDE_WRAPPER_CARD_MODEL`**| `SST1` (Voodoo1), `CVG` (Voodoo2), `H3` (Voodoo3), `H5` (Voodoo5) | Instantly alters the reported `GrSstType` and architectural layout exposed to the legacy application. |
| **`GLIDE_WRAPPER_FBI_MEM_MB`**| Integer (e.g., `2`, `4`, `16`, `32`) | Overrides the primary Framebuffer RAM. Games that fail to launch due to low video memory can be bypassed instantly. |
| **`GLIDE_WRAPPER_TMU_COUNT`** | `1`, `2`, `3` | Defines active Texture Mapping Units. Allows testing classic multi-texturing single-pass (`grTexCombine`) vs multi-pass fallback code. |
| **`GLIDE_WRAPPER_TMU_MEM_MB`**| Integer (e.g., `4`, `8`, `32`) | Overrides dedicated texture RAM per TMU chip. |
| **`GLIDE_WRAPPER_DRAW_DELAY_US`**| Microsecond float (e.g., `2.5`) | Emulates old AGP bus bandwidth. Introduces a micro-sleep per draw call to fix physics speedups in retro games. |
| **`GLIDE_WRAPPER_MAX_FPS`** | Integer (e.g., `30`, `60`, `120`) | Enforces a strict presentation frame cap independent of application game loops. |
| **`GLIDE_WRAPPER_SLI_MODE`** | `TRUE`, `FALSE` | Activates Voodoo2 SLI Emulation (doubling theoretical fill-rate reporting). |
