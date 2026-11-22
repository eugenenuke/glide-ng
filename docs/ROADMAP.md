# glide-ng Roadmap

Forward-looking engineering roadmap for `glide-ng`. Completed pre-beta work is archived in [docs/HISTORY.md](HISTORY.md), and released changes are tracked in [CHANGELOG.md](../CHANGELOG.md).

> **Task Verification Policy (3-State Gate)**
> - `[ ] PENDING` — Planned, not started.
> - `[~] IMPLEMENTED — AWAITING USER VERIFICATION` — Implemented and passing automated checks; awaiting manual developer confirmation.
> - `[x] DONE (USER CONFIRMED — YYYY-MM-DD)` — Verified and confirmed.

---

## Recently Completed

### M14 — Glide 2.11 (`glide.dll`) Frontend & Early SST-1 Title Support
- **Branch**: `feat/m14-glide211-dll` (merged into `main`)
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-19)`
- **Scope**:
  - `[x]` Implemented dedicated `src/glide211/` frontend library (`glide.dll` / `libglide.so`) with clean-room `include/glide211/glide.h` translating the pre-2.3 SST-1 ABI (`grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`/`BypassMode` with cached BAR pointer reuse, `guFbReadRegion`/`guFbWriteRegion`, and authentic TMU0 VRAM budget enforcement in `guTexAllocateMemory`) onto `GlideWrapperCore`.
  - `[x]` Defaulted `glide.dll` / `libglide.so` to `VoodooGraphics` (SST-1, 4 MB FB, 1× 2 MB TMU) with API version `2.11`, added pre-`ShowWindow` Win32 host HWND discovery in `NativeSurfaceHost`, and implemented Vulkan `Win32Embedded` `grSstPassthruMode` (`SHOW_VGA` yield / `SHOW_SST1` reclaim) + `VK_ERROR_SURFACE_LOST_KHR` recovery.
  - `[x]` Validated end-to-end against *CyberGladiators* (27 dynamic `__stdcall` `GLIDE.DLL` imports) under Wine32 across Vulkan (`1×` & `4× MSAA`), OpenGL 3.3 Core (`1×` & `4× MSAA`), and Software (`1×` & `2×2 SSAA`), added headless multi-backend `Glide211WrapperTests`, and published `docs/catalog/games/cybergladiators/` with presets and Visual Progression Gallery.
  - `[x]` Added `[display] fullscreen = true` (`GLIDE_NG_FULLSCREEN=1`) borderless fullscreen override, dropped speculative Vulkan pipeline pre-warming, refactored Vulkan RAMDAC Gamma LUT to guest resolution + shared blit, added 32-bit CPU Vulkan VA guards, and added Win32 `SHOW_VGA → SHOW_SST1` style-change surface rebuild + one-shot foreground restore. *(Deferred follow-up for Wine32 `GLIDE_NG_FULLSCREEN=1` audio/input focus tracked in Backlog below.)*

### M13 — Repository Cleanup, Structured Documentation, GPL-3.0 Licensing & `0.8.0-beta.1` Versioning
- **Branch**: `chore/m13-repo-cleanup-docs-versioning` (merged into `main`, tagged `v0.8.0-beta.1`)
- **Status**: `[x] DONE (USER CONFIRMED — 2026-11-18)`
- **Scope**:
  - `[x]` Decouple Catch2 suites (106 tests) and standalone demos from external `refs/` and `ext_tests/` assets; remove deprecated test dumps and legacy harnesses.
  - `[x]` Remove third-party binaries/PDFs (`refs/`), ad-hoc `scratch/` scripts, and local-only `.gitmodules` from git (preserving local references under git-ignored `vendor/refs/`).
  - `[x]` Establish single-source SemVer 2.0 versioning (`VERSION` = `0.8.0-beta.1`), tag-aware `build_version.h`, Win32 PE `VS_VERSION_INFO`, `scripts/bump_version.sh`, and `[M13][Version]` Catch2 test.
  - `[x]` Add `.agents/skills/tech-writer/SKILL.md` and refactor `docs/` into `user-guide/`, `developer-guide/`, `catalog/`, `HISTORY.md`, `ROADMAP.md`, and root `CHANGELOG.md`.
  - `[x]` Establish pure `GPL-3.0-or-later` licensing (`LICENSE`, `THIRD_PARTY_NOTICES.md`): replace all 3dfx-licensed `include/` headers with clean-room ABI headers and replace 3dfx splash/banner binary assets (`assets/`, `EmbeddedAssets.*`) with a procedural "Chrome & Amber Shield" 3D splash and `180×90` badge.
  - `[x]` Scrub historical third-party/dump blobs and remap author emails in git pack history (`git filter-repo`), prune merged remote branches, and publish annotated tag `v0.8.0-beta.1`.

---

## Upcoming Milestones

### M15 — Flagship Standalone 3dfx Showcase Demos (`feat/m15-shrine-torus-demos`)
- **Status**: `[ ] PENDING`
- **Goal**: Ship two self-contained, authentic Glide 2.x/3.x showcase applications targeting Linux, Win32/Wine, and DOS/32 (`GLIDE2X.OVL`):
  - **`glide_shrine` (*Voodoo Temple*)**: Two-pass `GR_FOG_ADD2`/`MULT2` + `PREFOG_COLOR` lightmapped temple hall with mipmapped columns, exponential table fog, and additive torch sprites.
  - **`glide_torus` (*3dfx Reflective Hall*)**: Dual-TMU (`TMU0 + TMU1`) environment-mapped specular torus knot with translucent floor reflections, dynamic colored lights, and smooth camera orbit.

---

## Backlog (Unscheduled)

- **Win32/Wine Borderless Fullscreen (Resolved — Hardware Validation Pending)**: Implemented safe ANSI/Unicode Win32 `WndProc` subclassing during borderless fullscreen (bypassing legacy `WM_GETMINMAXINFO` clamps like *CyberGladiators*' 640×480 cap, swallowing `WM_WINDOWPOSCHANGED`/`WM_DISPLAYCHANGE` to prevent re-entrant `WM_SIZE` crashes like *Unreal*'s `FMemCache` assert, suppressing transient `WM_ACTIVATEAPP`/`WM_KILLFOCUS` fallout during style transitions, clearing undersized pre-fullscreen `ClipCursor` traps, restoring keyboard/DirectSound focus via `SetFocus()`, and inverse-scaling client-space `WM_MOUSE*` coordinates with automatic fallback for relative cursor-warp engines like *Hitman: Codename 47*). Verify interactively on a real Wayland/X11 audio-enabled session before removing from follow-up tracking.
- **Real 3dfx Hardware Cross-Check for *Tomb Raider* Mipmap Atlas Seams (Verified via 86Box SST-1, DOSBox-X SST-1, and 3dfx Glide SDK Specs)**: Cross-checked `LEV1_*.3DF` / `LEVEL1.PHD` mipmap chains against `86Box`'s independent `vid_voodoo_render.c`, DOSBox-X's `voodoo_data.h`, `OpenGlide`, and the official 3dfx Glide 2.4/3.0 Reference Manuals. Confirmed two separate findings: (1) *Tomb Raider*'s Caves snow-floor and snow-bank seams under in-game `Mipmapping` are an inherent consequence of Core Design's zero-gutter `64×64`-in-`256×256` texture atlases (vertices inset only `±0.5` texels of LOD 0, so bilinear filtering at LOD `k ≥ 1` mathematically bleeds `25%–47%` of adjacent dark rock/dirt/vine tiles across tile borders on **every** bilinear TMU, including real SST-1 silicon, 86Box, and DOSBox-X; OpenGlide appeared seamless only because its default `OpenGLid.ini` sets `EnableMipMaps=0`); (2) fixed the 0.5-LOD premature transition in `glide-ng` where `GR_MIPMAP_NEAREST` (`lodBlend = FXFALSE`) previously rounded instead of truncating (`floor(λ)` per Glide 2.4/3.0 manual & `lod >> 8` on SST-1 silicon). Real-card capture cross-check remains welcome as a final visual confirmation.
- **Batched Shared-Memory Ring-Buffer `glide2x.ovl` Transport**: Optional future transport extension on top of `glide-ng`'s clean-room `src/dos_ovl/glide2x_ovl.c` (which already implements drop-in I/O port `0x600` compatibility for unmodified DOSBox-SVN and DOSBox-X) to batch high-frequency draw/state calls into a shared guest ring buffer when paired with a matching host-side DOSBox patch.
- **OpenGL ES Early-Z W-Buffer Shader Split**: Mirror the Vulkan `-DUSE_WBUFFER_DEPTH` specialization in `OpenGLESShaders.cpp` so Z-buffer and depth-off GLES/WGL pipelines omit `gl_FragDepth` writes.
- **SIMD FXT1 / YUYV Texture Decoders**: Vectorize `DecodeYuyv422` and `DecodeFxt1` in `SimdKernels` if dynamic streaming VSA-100 titles become CPU-bound during texture upload.
- **Broader Game Compatibility Catalog**: Expand `docs/catalog/games/` as additional commercial DOS and Win32 Glide 2.11 / 2.x / 3.x titles are tested.

---

## Path to `1.0.0`

**Current milestone: `v0.9.0` (41 verified commercial games across DOS, Glide 2.11, Glide 2.x, Glide 3.x, 3dfx MiniGL, and `3dfxVGL.dll` ICD; modular zero-warning Catch2 test suite; multi-distro binary release packaging).**

Criteria for graduating from `0.9.x` to `1.0.0`:
1. Full Glide 2.11 (`glide.dll`), Glide 2.x (`glide2x.dll`/`libglide2x.so`), and Glide 3.x (`glide3x.dll`/`libglide3x.so`) ABI coverage verified on real commercial titles.
2. Stable `glide-ng.ini` schema and `GLIDE_NG_*` / `FX_GLIDE_*` configuration contract.
3. Reproducible CI builds and headless multi-backend regression tests across Linux and Win32/Wine targets.
4. Verified release packaging and documented upgrade/compatibility matrix.
