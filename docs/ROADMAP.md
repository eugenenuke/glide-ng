# glide-ng Roadmap

Forward-looking engineering roadmap for `glide-ng`. Completed pre-beta work is archived in [docs/HISTORY.md](HISTORY.md), and released changes are tracked in [CHANGELOG.md](../CHANGELOG.md).

> **Task Verification Policy (3-State Gate)**
> - `[ ] PENDING` — Planned, not started.
> - `[~] IMPLEMENTED — AWAITING USER VERIFICATION` — Implemented and passing automated checks; awaiting manual developer confirmation.
> - `[x] DONE (USER CONFIRMED — YYYY-MM-DD)` — Verified and confirmed.

---

## Active Milestone

### M13 — Repository Cleanup, Structured Documentation & `0.8.0-beta.1` Versioning
- **Branch**: `chore/m13-repo-cleanup-docs-versioning`
- **Status**: `[~] IMPLEMENTED — AWAITING USER VERIFICATION`
- **Scope**:
  - `[~]` Decouple Catch2 suites (106 tests) and standalone demos from external `refs/` and `ext_tests/` assets; remove deprecated test dumps and legacy harnesses.
  - `[~]` Remove third-party binaries/PDFs (`refs/`), ad-hoc `scratch/` scripts, and local-only `.gitmodules` from git (preserving local references under git-ignored `vendor/refs/`).
  - `[~]` Establish single-source SemVer 2.0 versioning (`VERSION` = `0.8.0-beta.1`), tag-aware `build_version.h`, Win32 PE `VS_VERSION_INFO`, `scripts/bump_version.sh`, and `[M13][Version]` Catch2 test.
  - `[~]` Add `.agents/skills/tech-writer/SKILL.md` and refactor `docs/` into `user-guide/`, `developer-guide/`, `catalog/`, `HISTORY.md`, `ROADMAP.md`, and root `CHANGELOG.md`.
  - `[~]` Establish pure `GPL-3.0-or-later` licensing (`LICENSE`, `THIRD_PARTY_NOTICES.md`): replace all 3dfx-licensed `include/` headers with clean-room ABI headers and replace 3dfx splash/banner binary assets (`assets/`, `EmbeddedAssets.*`) with a procedural "Chrome & Amber Shield" 3D splash and `180×90` badge.
  - `[ ]` Post-merge: scrub historical third-party/dump blobs and remap author emails in git pack history (`git filter-repo`), then publish annotated tag `v0.8.0-beta.1`.

---

## Upcoming Milestones

### M14 — Glide 2.11 (`glide.dll`) Frontend & Early SST-1 Title Support (`feat/m14-glide211-dll`)
- **Status**: `[ ] PENDING`
- **Goal**: Support late-1996 / early-1997 Win32 and DOS titles linked against **`glide.dll` (Glide 2.11)** rather than `glide2x.dll`.
- **Key Technical Work**:
  - Implement a dedicated `src/glide211/` frontend library (`glide.dll` / `libglide.so`) translating the pre-2.3 SST-1 ABI (`grSstOpen`, `grSstPassthruMode`, `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`/`BypassMode` with fixed 1024-pixel stride, `guFbReadRegion`/`guFbWriteRegion`, `guTex*`) onto `GlideWrapperCore`.
  - Default `glide.dll` to the `VoodooGraphics` (SST-1) profile with API version `2.11` and authentic SST-1 defaults.
  - Validate end-to-end against *CyberGladiators* (27 dynamic `__stdcall` `GLIDE.DLL` imports) under Wine32 across Vulkan, OpenGL 3.3 Core, and Software backends, add Catch2 unit tests, and publish a `docs/catalog/games/cybergladiators/` entry.

### M15 — Flagship Standalone 3dfx Showcase Demos (`feat/m15-shrine-torus-demos`)
- **Status**: `[ ] PENDING`
- **Goal**: Ship two self-contained, authentic Glide 2.x/3.x showcase applications targeting Linux, Win32/Wine, and DOS/32 (`GLIDE2X.OVL`):
  - **`glide_shrine` (*Voodoo Temple*)**: Two-pass `GR_FOG_ADD2`/`MULT2` + `PREFOG_COLOR` lightmapped temple hall with mipmapped columns, exponential table fog, and additive torch sprites.
  - **`glide_torus` (*3dfx Reflective Hall*)**: Dual-TMU (`TMU0 + TMU1`) environment-mapped specular torus knot with translucent floor reflections, dynamic colored lights, and smooth camera orbit.

---

## Backlog (Unscheduled)

- **Clean-Room Open-Source `glide2x.ovl`**: Build an open-source Open Watcom v2 DOS/32 `glide2x.ovl` implementing the 130-entry (`0..129`) DOSBox I/O-port bridge ABI, removing reliance on third-party binary overlays (with optional future shared-memory ring-buffer transport for higher DOS throughput).
- **OpenGL ES Early-Z W-Buffer Shader Split**: Mirror the Vulkan `-DUSE_WBUFFER_DEPTH` specialization in `OpenGLESShaders.cpp` so Z-buffer and depth-off GLES/WGL pipelines omit `gl_FragDepth` writes.
- **SIMD FXT1 / YUYV Texture Decoders**: Vectorize `DecodeYuyv422` and `DecodeFxt1` in `SimdKernels` if dynamic streaming VSA-100 titles become CPU-bound during texture upload.
- **Broader Game Compatibility Catalog**: Expand `docs/catalog/games/` as additional commercial DOS and Win32 Glide 2.11 / 2.x / 3.x titles are tested.

---

## Path to `1.0.0`

Criteria for graduating from `0.x` beta to `1.0.0`:
1. Full Glide 2.11 (`glide.dll`), Glide 2.x (`glide2x.dll`/`libglide2x.so`), and Glide 3.x (`glide3x.dll`/`libglide3x.so`) ABI coverage verified on real commercial titles.
2. Stable `glide-ng.ini` schema and `GLIDE_NG_*` / `FX_GLIDE_*` configuration contract.
3. Reproducible CI builds and headless multi-backend regression tests across Linux and Win32/Wine targets.
4. Verified release packaging and documented upgrade/compatibility matrix.
