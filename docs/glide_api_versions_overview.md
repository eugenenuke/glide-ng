# 3dfx Glide Wrapper Architectural Guide: API Versions & Differences

The **3dfx `glide-ng` Modernization Wrapper** is engineered to support the two primary historical epochs of the 3dfx proprietary graphics architecture: **Glide 2.x** and **Glide 3.x**. 

Because classic 1990s and early 2000s games were hardcoded against entirely different Application Binary Interfaces (ABIs), our wrapper compiles two completely independent multilib dynamic target replacement libraries (`libglide2x.so` / `glide2x.dll` and `libglide3x.so` / `glide3x.dll`) that sit on top of one highly optimized **Shared Unified Core** (`GlideWrapperCore`).

---

## 1. Supported Runtime ABIs & Default Versioning

| Emulated Ecosystem | Target Replacement Library | Baseline Specification | Target Retro Game Era | Primary Supported Accelerators |
| :--- | :--- | :--- | :--- | :--- |
| **Glide 2.x** | `libglide2x.so` / `glide2x.dll` | **`2.43 (Up to 2.61 ABI)`** | 1996–1999 (*GLQuake*, *Tomb Raider II*, *Unreal*) | Voodoo Graphics (SST-1), Voodoo2 (CVG) |
| **Glide 3.x** | `libglide3x.so` / `glide3x.dll` | **`3.10`** | 1999–2002 (*Diablo II*, *Deus Ex*, *Serious Sam*) | Voodoo3 (H3), Voodoo4 / Voodoo5 (H5) |

---

## 2. Core Functional Differences: Glide 2.x vs. Glide 3.x

When engineering or debugging retro games against our wrapper, understand these fundamental architectural differences between the two specification suites:

### A. Context & Window Surface Initialization
* **Glide 2.x**: Relies on a highly monolithic initialization routine. Developers call `grGlideInit()` followed by `grSstWinOpen()` which accepts literal window handles and fixed integer display resolutions (e.g., `GR_RESOLUTION_800x600`).
* **Glide 3.x**: Completely separated hardware capabilities from context generation. It introduces **`grContextInit()`** and **`grWinOpen()`**, allowing applications to bind multiple independent off-screen execution contexts and dynamically resize window viewports.

### B. Vertex Struct Layouts
* **Glide 2.x**: Enforces a highly rigid, unalterable C struct layout (`GrVertex`). Every vertex submitted to drawing primitives (`grDrawTriangle`) must pass exactly identical memory allocations, regardless of whether the shader actually consumes all components (X, Y, Z, R, G, B, A, OOW, S0, T0, S1, T1).
* **Glide 3.x**: Introduced highly modernized **Vertex Layout Pointers (`grVertexLayout()`)**. Games specify exactly which floating-point components exist in their arbitrary custom vertex structures, drastically reducing host-to-device PCI bus memory bandwidth for simple 2D or UI drawing elements.

### C. Multitexturing & TMU Combiner Pipelines
* **Glide 2.x**: Multitexturing on Voodoo2 hardware is executed via highly explicit dual-pass configuration. Developers configure `grTexCombineFunction()` independently for `GR_TMU0` and `GR_TMU1`. If a user runs on a single-TMU card (Voodoo Graphics), the game engine must manually execute fallback multi-pass blending.
* **Glide 3.x**: Unified the TMU combiner formula into advanced register pipelines (`grTexCombineExt()`). It gracefully handles hardware fallback multi-passing internally and exposes dedicated extensions for modern non-power-of-two texture buffers (`grTexBufferExt`).

### D. Capabilities Polling
* **Glide 2.x**: Games execute massive nested hardware pointer queries via `grSstQueryHardware()` and `grSstQueryBoards()`, parsing raw board properties.
* **Glide 3.x**: Normalized parameter queries into a single highly elegant retrieval routine: **`grGet(pname, plength, params)`**. This maps perfectly to modern `glGetIntegerv` or Vulkan physical device property lookups.

---

## 3. How We Implement Backward Compatibility (Supersets ABI)

Within a specific specification tier (e.g., Glide 2), games were released across various sub-versions (e.g., `2.40`, `2.42`, `2.43`).

### Our Zero-Friction Emulation Strategy
Our wrapper implements an authoritative **Supersets ABI**. For example, our `libglide2x.so` binary exports every single C symbol ever introduced up to specification `2.61`. When an older `2.40` game launches, it binds perfectly to our exported function pointers without encountering missing symbol faults.

You can configure exactly what API version string our wrapper reports back to the game by setting the `"ApiVersionOverride"` field in `glide_config.json`:

```json
{
  "CardModel": "Voodoo2",
  "ApiVersionOverride": "2.40"
}
```
This forces rigid retro games to follow their authentic retro software execution branches flawlessly!

---

## 4. Historical Minor Versions & Specification Breakdown

When tuning the `"ApiVersionOverride"` field for specific retro backlog games, refer to this detailed breakdown of historical minor specification releases and their concrete additions:

### A. Glide 2.x Minor Specifications

#### `2.40` (1996 - Voodoo Graphics SST-1 Baseline)
* **Core Additions**: The foundational 3dfx proprietary graphics specification. Introduces classic 16-bit 565/4444 framebuffers, standard `GrVertex` primitives (`grDrawTriangle`, `grDrawLine`), single-chip `GR_TMU0` texture mapping, and palettized NCC/P8 texture formats.
* **Prominent Game Engines**: Original *GLQuake*, *Tomb Raider II*, *Descent II*, *Need for Speed II SE*.

#### `2.42` (1997 - Voodoo2 Multitexturing & SLI Bridge)
* **Core Additions**: Introduced dual-board Scan-Line Interleave (SLI) hardware synchronization queries (`sliDetect` in `Voodoo2Config`). Adds dual-TMU single-pass multitexturing via explicit `grTexCombineFunction` dual combiners and floating-point W-buffering depth enhancements.
* **Prominent Game Engines**: *Quake II*, *Incoming*, *Forsaken*, *Battlezone*.

#### `2.43` (1998 - The Master Voodoo2 Standard ABI)
* **Core Additions**: Added `grGetProcAddress` dynamic querying, standardized AGP host PCI command FIFO buffering, and un-gated high-resolution resolutions up to 1024x768. This is the definitive, gold-standard Glide 2 specification.
* **Prominent Game Engines**: *Unreal* (Early Build Engine), *Half-Life*, *Hexen II*, *Sin*.

#### `2.61` (1999 - Voodoo3 Legacy Interop Bridge)
* **Core Additions**: The absolute final Glide 2 update released by 3dfx. Backported select Glide 3 register state initialization properties to allow legacy Glide 2 game engines to execute perfectly on Voodoo3 and Voodoo5 hardware.
* **Prominent Game Engines**: *System Shock 2*, *Deus Ex* (Glide 2 Execution Branch), *Unreal Tournament* (Glide 2 Branch).

---

### B. Glide 3.x Minor Specifications

#### `3.0` (1999 - Voodoo3 Avenger Launch Specification)
* **Core Additions**: Completely redesigned the API landscape. Separated hardware queries from context allocation (`grContextInit`, `grWinOpen`). Introduced highly modular **Vertex Layout Pointers (`grVertexLayout()`)** and unified register combiner formulas (`grTexCombineExt`).
* **Prominent Game Engines**: *Diablo II*, *Wheel of Time*, *Rune*, *Hitman: Codename 47*.

#### `3.10` (2000 - Voodoo5 Napalm / T-Buffer Engine)
* **Core Additions**: The pinnacle of 3dfx graphics software engineering. Introduced absolute state-of-the-art extensions: **T-Buffer Multi-Sample Anti-Aliasing (MSAA)**, hardware motion blur, soft shadows, FXT1 and DXT texture block compression formats, and support for up to 2048x2048 non-square textures (`grTexBufferExt`).
* **Prominent Game Engines**: *Serious Sam*, *Deus Ex* (Glide 3 Execution Branch), *Unreal Tournament* (Glide 3 Branch), *Clive Barker's Undying*.

