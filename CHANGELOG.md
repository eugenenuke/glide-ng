# Changelog

All notable changes to `glide-ng` are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

For the pre-beta engineering narrative covering early architecture decisions and milestones prior to `0.8.0-beta.1`, see [docs/HISTORY.md](docs/HISTORY.md).

---

## [Unreleased]

### Added
- **Screamer 2 Catalog Entry (`42` Total Commercial Games)**: Verified the dynamic `GLIDE2X.OVL` Voodoo2+ DOS patch (`s2_3dfx.exe`, `1,055,262` B) across **Vulkan** (`1×` & `4× MSAA`), **OpenGL ES 3.2**, and **Software** backends (England, Egypt, and California stages, menus, camera modes, plus `START65H.EXE` SVGA comparison) with drop-in `glide-ng*.ini` presets under `docs/catalog/games/screamer-2/`.
- **Automated Release Notes Generation**: Added [`scripts/generate_release_notes.py`](scripts/generate_release_notes.py) and wired it into [`.github/workflows/release.yml`](.github/workflows/release.yml) so future tag pushes auto-populate GitHub Release pages from `CHANGELOG.md`, archive sizes/`GLIBC` floors, and `SHA256SUMS.txt`.
- **Authentic 3dfx Environment Variables (`20 → 48` Registered) & `SSTH3_` Scan Prefix**: Expanded [`include/glide_ng/env_vars.h`](include/glide_ng/env_vars.h), `IniConfigLoader`, `glide-ng.ini.example`, and [`docs/user-guide/configuration.md`](docs/user-guide/configuration.md) with full family-alias & INI support:
  - Registered **`FX_GLIDE_REPORT_REAL_HW`** (`[glide] report_real_hw`, preserving strict 3dfx `!= NULL` semantics so any set value including `=0` reports raw `GR_SSTTYPE_Voodoo2` while unset stays `GR_SSTTYPE_VOODOO`).
  - Added **`FX_GLIDE_EMUL_RUSH`** (`[glide] emul_rush`) so `VoodooBanshee`/`Voodoo3`/`Voodoo4`/`Voodoo5` can report `GR_SSTTYPE_SST96` from `grSstQueryHardware()`.
  - Added authentic card-family aliases **`FX_GLIDE_NUM_TMU`**, **`SST_FBIMEM_SIZE`**, **`SSTV2_FBI_MEMSIZE`**, **`SSTH3_FBRAM`**, **`SST_TMUMEM_SIZE`**, **`SSTV2_TMU_MEMSIZE`**, **`SSTV2_ALLOC_COLOR`**, **`SSTV2_ALLOC_AUX`**, **`SST_TRIPLE_BUFFER`**, **`SSTV2_TEXMAP_DISABLE`**, **`SST_SLIDETECT`**, **`SSTV2_SLIDETECT`**, **`FX_GLIDE_REFRESH`**, **`FX_GLIDE_AA_SAMPLE`**, and **`FX_GLIDE_TEXTURE_UMA`** (`[glide] texture_uma`).
  - Added per-channel RGB gamma overrides **`SST_{R,G,B}GAMMA`**, **`SSTV2_{R,G,B}GAMMA`**, **`SSTH3_{R,G,B}GAMMA`**, **`FX_GLIDE_GAMMARGB`**, and **`FX_GLIDE_USE_APP_GAMMA`** (`[glide] gamma_{r,g,b,rgb}`, `use_app_gamma`), wired across `grGammaCorrectionValue`, `guGammaCorrectionRGB`, and `grLoadGammaTable` on all three backends.
  - Added **`SSTH3_`** to `GLIDE_ENV_SCAN_PREFIXES_X` so `glide2_info`/`glide3_info` automatically detect user-set Banshee/Voodoo3/4/5 variables.

### Changed
- **Authentic `GR_HARDWARE` & Hardware Revision Parity (`glide3x` / `glide2x`)**:
  - Updated Glide 3.x `grGetString(GR_HARDWARE)` to return retail `glide3x.dll` 3.10 strings with the trailing trademark tag: `"Voodoo Banshee (tm)"`, `"Voodoo3 (tm)"`, `"Voodoo4 (tm)"`, and `"Voodoo5 (tm)"` (all `<= 19` bytes, fitting legacy fixed-size stack buffers).
  - Updated Glide 2.x/3.x `grGet(GR_REVISION_FB)` on `Voodoo2` to return authentic CVG **`0x102`** (dual-TMU) / **`0x82`** (single-TMU) instead of `2` (matching `3.01`/`3.03` CVG instead of `SST-1`).
  - Forwarded `enableSli` (`sliDetect`) on `VoodooGraphics` (`SST-1` SLI / Quantum3D Obsidian) inside `grSstQueryHardware()`.

---

## [0.9.0] - 2026-11-22

First release dropping the `-beta` prerelease suffix (**41 verified commercial games**, full **Glide 2.11 / 2.x / 3.x**, **3dfx MiniGL**, and **`3dfxVGL.dll` OpenGL ICD** coverage), clean zero-warning CMake/compiler builds, hardened ELF ABI exports, modular Catch2 test suites, and pre-built multi-platform release packaging.

### Added
- **Pre-Built Multi-Platform Release Packaging & CI**: Added [`.github/workflows/release.yml`](.github/workflows/release.yml) and [`scripts/package_release.sh`](scripts/package_release.sh) emitting stripped `runtime.zip` / `runtime.tar.xz` (`win32/glide*.dll`, `dos/GLIDE2X.OVL`, and `linux-x86_64` flavors for **Ubuntu 22.04**, **Ubuntu 24.04**, **Fedora 41**, and **Arch / CachyOS**), `tools.zip`, optional `debug-symbols.zip` (detached `*.debug` files), and `SHA256SUMS.txt`. Added minimal non-dev end-user runtime package guides to [`docs/user-guide/building-and-installing.md`](docs/user-guide/building-and-installing.md).
- **Opt-In Portable Linux C++ Runtime Flag**: Added `-DGLIDE_STATIC_CPP_RUNTIME=ON|OFF` (default **`OFF`**, keeping source builds dynamically linked against system `libstdc++` and `libgcc`).
- **13 New Catalog Entries (41 Total Commercial Games)**: Added verified guides, presets, and visual progression galleries for *Formula 1* (1997, Glide 2.x), *Half-Life* (1998, `gldrv/3dfxgl.dll` MiniGL → Glide 2.x), *Interstate '76 Gold Edition* (1997/1998, Glide 2.x), *Jane's Combat Simulations: Longbow 2* (1997, Glide 2.x), *MechWarrior 2: 31st Century Combat — 3Dfx Edition* (1996, Glide 2.11 `glide.dll`), *Messiah* (2000, Glide 3.x / Voodoo5 `PIXEXT`), *Motorhead* (1998, Glide 2.x), *Outlaws* (1997, dual Glide 2.11 & 2.x), *Quake / GLQuake* (1996/1997, MiniGL → Glide 2.x), *Quake II* (1997, `3dfxgl.dll` MiniGL → Glide 2.x), *Quake III Arena* (1999, `3dfxVGL.dll` ICD → Glide 3.x), *Time Warriors* (1997, Glide 2.11 `glide.dll`), and *Turok 2: Seeds of Evil* (1999, Glide 3.x).

### Changed
- **Hardened ELF Visibility & Dead-Code Elimination (`src/CMakeLists.txt`)**: Compiled `GlideWrapperCore` with `-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden`, added anonymous linker version scripts (`src/glide211/libglide.ver`, `src/glide2/libglide2x.ver`, `src/glide3/libglide3x.ver`), `-Wl,-Bsymbolic-functions`, `-Wl,--gc-sections`, and `-Wl,--exclude-libs,ALL` (trimming `libglide2x.so` dynamic exports from 1,617 down to the exact 152 Glide C ABI symbols). Added `-Wl,--gc-sections` on MinGW-w64 Win32 DLLs.
- **Modular Catch2 Test Suite & Strict `-Werror` (`tests/`)**: Reorganized monolithic `test_architecture.cpp` and `test_glide3.cpp` into 18 domain-focused modules under `tests/glide211/`, `tests/glide2/`, `tests/glide3/`, and `tests/common/test_helpers.h`. Renamed and consolidated **131 → 64** self-descriptive, milestone-free `TEST_CASE`s using Catch2 `SECTION`s (updating tag filters to `[Catalog]` and `[Version]`, removing legacy `M*`/`Phase*`/`testXX`/`VULN-*` labels, folding standalone `sw_validate.cpp` into Catch2, and removing legacy manual embed harnesses from `demos/`). Fixed all 21 remaining warnings in the Glide 3.x test files and enabled strict `-Werror` inside `tests/CMakeLists.txt`. Added `find_package(Catch2 3 QUIET)` guard so older Catch2 v2 packages are safely skipped.
- **Documentation & Guide Refresh**: Added *CyberGladiators* manual install & Wine 96 DPI notes (`SPLASH4.AVI`) and updated `docs/developer-guide/{architecture,testing,releasing}.md`, `README.md`, and workspace skill references.

### Fixed
- **Commercial Game & ICD Compatibility Fixes**:
  - *Formula 1* (1997): Implemented coherent SST-1/CVG signed `12.4` fixed-point vertex-coordinate modulus unwrapping (`4096.0 px` period) inside `EntryCommon::UnwrapCoherent12x4Axis()` / `SanitizeTriangle()`, recovering shifted menu label glyphs while leaving wide frustum triangles untouched, and enforced the Glide 2.x `-1` sentinel "leave unchanged" contract in `guTexChangeAttributes()`.
  - *Half-Life* (1998, 3dfx MiniGL): Updated `grSstQueryHardware()` in `src/glide2/glide2_entry.cpp` (and `fbiRev` decoders in `DiagnosticInfo.cpp`, `glide_info.c`, `glide_bench.c`) to report authentic CVG `GR_SSTTYPE_VOODOO` (`0`) with `fbiRev = 0x102` (dual-TMU) / `0x82` (single-TMU) under the `Voodoo2` profile unless `FX_GLIDE_REPORT_REAL_HW` is set — unlocking `GL_SGIS_multitexture` in `3dfxgl.dll`. Restored authentic `cc_delta0mode` constant-color binding (`GR_COMBINE_LOCAL_CONSTANT`) for `GR_COLORCOMBINE_ITRGB_DELTA0` and `GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0` in `guColorCombineFunction()`.
  - *Jane's Combat Simulations: Longbow 2* (1997): Moved `SoftwareBackend` RAMDAC gamma LUT mapping from destructive in-place `SwapBuffers()` into non-destructive scanout (`ResolveFrontBuffer()`), stopping static cockpit bezels from darkening/washing out across repeated swaps and reducing `2×2` SSAA gamma work by 4×.
  - *MechWarrior 2: 31st Century Combat — 3Dfx Edition* (1996, Glide 2.11 `glide.dll`): (1) Added unsigned 3dfx IEEE-754 vertex snap biases `1<<19` (`524288.0f`, `12.4` `1/16` px) and `1<<20` (`1048576.0f`, `13.3` `1/8` px) to `VertexLayout::UnsnapAndSanitizeXY()`; (2) Added `LfbManager::ReconfigureActiveWriteLock()` and updated `glide211_entry.cpp` so callers that cache `grLfbGetWritePtr()` once at startup and toggle matching-width write modes (`565` → `555`) or bypass flags (`ENABLE` ↔ `DISABLE`) flush prior dirty bands and preserve the active staging pointer address in-place; (3) Fixed `OpenGLESBackend::FlushLFBToGPU_Locked()` to flush pending batched 3D geometry (`m_batchVertices`) before compositing `m_lfbStaging` overlays onto the FBO.
  - *Motorhead* (1998): Implemented live GPU `GR_BUFFER_AUXBUFFER` 16-bit depth readback (`grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_AUXBUFFER)` / `grLfbReadRegion`) in **`VulkanBackend`** (`vkCmdCopyImageToBuffer` + lazy `msaa_depth_resolve.frag` sample-0 resolve) and **`OpenGLESBackend`** (desktop GL 3.3 `glReadPixels(GL_DEPTH_COMPONENT, GL_FLOAT)` / GLES 3.2 pack pass + MSAA `glBlitFramebuffer`), cached per frame inside `m_auxDepthShadow16` so occlusion probes for brake lights, track lamps, and lens flares succeed across all 3 backends.
  - *Quake / GLQuake* (1996/1997, 3dfx MiniGL): Removed the static Win32 PE import on `opengl32.dll`; `glide.dll`, `glide2x.dll`, and `glide3x.dll` now lazily load base WGL and OpenGL 1.1 symbols from `%SystemRoot%\System32\opengl32.dll`, eliminating circular PE loader dependencies when games place a 3dfx MiniGL `opengl32.dll` next to the executable.
  - *Quake III Arena* (1999, `3dfxVGL.dll` OpenGL 1.1 ICD): Removed unimplemented private ICD extension tokens (`SURFACE`, `COMMAND_TRANSPORT`) from `grGetString(GR_EXTENSION)`, padded `GR_EXTENSION` with authentic leading/trailing spaces, and advertised `TEXMIRROR`/`PALETTE6666` under `Voodoo2`.
  - *Time Warriors* (1997): Fixed `grSstStatus()` to return the authentic 3dfx SST-1/CVG idle register bitfield (`0x0FFFF03F` plus active-low `SST_VRETRACE` bit 6 when outside vertical blank) instead of `0x3FF`.
  - *Turok 2: Seeds of Evil* (1999): Released Win32 WGL `HGLRC` on the calling thread after `SwapBuffers()` and `PresentFrontBufferGLES_Locked()` (eliminating `ERROR_BUSY (170)` black screens on multi-threaded games), and added `NativeSurfaceHost::HideWin32EmbeddedChildren()` / `RestoreWin32EmbeddedChildren()` to hide leftover sibling Win32 child controls during active rendering.

---

## [0.8.0-beta.4] - 2026-11-21

*(Retrospective milestone tag at commit `613cd96`; 28 verified commercial games.)*

### Added
- **13 New Catalog Entries (28 Total Commercial Games)**: Added verified guides, presets, and visual progression galleries for *Burnout: Championship Drag Racing* (1998, DOS), *Extreme Assault* (1997, DOS), *JetFighter III: Platinum* (1997, DOS), *JetFighter: Full Burn* (1998, DOS), *Myth: The Fallen Lords* (1997, Win32), *NASCAR Racing 2* (1996, DOS), *NASCAR Racing 3* (1999, Win32), *Need for Speed II: Special Edition* (1997, Win32), *Need for Speed III: Hot Pursuit* (1998, Win32), *Prost Grand Prix* (1998, DOS), *Pył* (1998, DOS), *Shadow Warrior* (1997, DOS), and *Tie Break Tennis* (1998, DOS).

### Fixed
- **Authentic 3dfx `GR_MIPMAP_NEAREST` LOD Truncation**: Aligned non-trilinear mip-level selection across Vulkan (`-127/256` sampler `mipLodBias` clamped to `maxSamplerLodBias`), OpenGL ES/WGL (`uTexSampleLodBias`), and Software scalar + SIMD (`std::floor`) with hardware `floor(rawLod + lodBias)` truncation.
- *Extreme Assault* (1997): Exposed authentic SST-1/CVG fixed `1024`-pixel LFB row stride (`2048` B @ 16-bit, `4096` B @ 32-bit when width ≤ 1024 px) in Glide 2.x `grLfbLock()`, and updated `guTexMemReset()` to preserve TMU `minFilter`/`magFilter`/`clampS`/`clampT` register state.
- *JetFighter III: Platinum* (1997): Added `GlideStateTracker::DoesCombinerReadTexture_Locked()` to gate TMU0/TMU1 activation across all backends so untextured primitives carrying stale `grTexSource()` bindings and uninitialized/subnormal `oow` values do not falsely drop pixels.
- *Prost Grand Prix* (1998): Padded `LfbManager::Lock()` backing staging buffers with a trailing vertical guard (`max(height + 64, 1024)` rows when fixed 1024-px stride is active) to absorb off-by-one scanline writes (`y == height`).

---

## [0.8.0-beta.3] - 2026-11-20

*(Retrospective milestone tag at commit `c866879`; 15 verified commercial games.)*

### Added
- **Clean-Room Open-Source DOS/32 `glide2x.ovl`**: Freestanding GPL-3.0 Open Watcom v2 DOS/32 LE DLL (`src/dos_ovl/glide2x_ovl.c`, `glide2x_ovl.lnk`) implementing the 130-entry (`0..129`) DOSBox-SVN / DOSBox-X I/O port `0x600` bridge plus Glide 2.11 `guFbReadRegion`/`guFbWriteRegion` compatibility wrappers, including the 4 symbols omitted by legacy third-party overlays.
- **Borderless Fullscreen Safe Default & Automatic Startup Splash**: Enabled `[display] fullscreen = auto` (defaulting to **`true` on Win32/Wine** and **`false` on Linux standalone**), plus automatic one-shot procedural 3dfx startup splash on `grSstWinOpen`/`grSstOpen`.
- **11 New Catalog Entries (15 Total Commercial Games)**: Added validated guides, presets, and standardized cards for *Archimedean Dynasty* (1996, DOS), *Blood* (1997, DOS), *Diablo II* (2000, Glide 3.x), *Hitman: Codename 47* (2000, Glide 3.x), *Lands of Lore: Guardians of Destiny* (1997, DOS), *MDK* (1997, Win32), *MechWarrior 2: 31st Century Combat — Titanium Edition* (1998, Win32), *Need for Speed: Porsche Unleashed* (2000, Glide 3.x), *Pandemonium!* (1997, Glide 2.11), *The Elder Scrolls Adventures: Redguard* (1998, DOS), and *Unreal* (1998, Win32).

### Fixed
- **Win32/Wine Subclassing, Focus & Fullscreen Mouse Inverse-Mapping**: Added ANSI/Unicode-safe `WndProc` subclassing during borderless fullscreen bypassing `WM_GETMINMAXINFO` clamps, guarding re-entrant `WM_SIZE`/`WM_WINDOWPOSCHANGED`/`WM_DISPLAYCHANGE` crashes, clearing undersized `ClipCursor` rectangles, preserving `SetFocus()`, and inverse-mapping `WM_MOUSE*` coordinates (`NativeSurfaceHost::MapHostPointToGuest`) with auto-fallback for cursor-warping engines.
- **Glide 3.x Startup Splash State Preservation**: Tagged implicit canonical `GR_PARAM_Z`/`GR_PARAM_W` defaults in `Glide3VertexLayout` so `grSplash()` no longer promotes untouched `zInfo` into an explicit `GR_PARAM_Z` binding, and expanded `GlideSplashAnimator::Render()`'s stack state buffer to 1024 bytes.
- **MSAA Non-Centroid Gradients & Presentation Filter Enforcement**: Separated pixel-center affine varyings for screen-space `dFdx`/`dFdy` LOD calculation from `centroid` sampling varyings in Vulkan & OpenGL ES, and wired `[display] presentation_filter` (`nearest`/`linear`) across all three backends.
- *Archimedean Dynasty* (1996): Added per-buffer nested `grLfbLock` write-lock refcounting, `GR_BUFFER_AUXBUFFER` (`ZA16`) depth upload/readback, `COMPARE_TO_BIAS` depth-bias masking across all backends (`lfb_depth.frag` + stencil mask), cached GPU `ReadLFB` snapshots, and throttled direct front-buffer flips.
- *Blood* (1997): Stored `OpenGLESBackend` offscreen FBOs top-down to match Vulkan/Software Y-down rasterization and prevent dropped 1-pixel right-triangle scanlines.
- *Diablo II* (2000): Broadcast Glide 3.x `grTexDownloadTable`/`Partial` uploads across both `TMU0` and `TMU1` and added inclusive `[start..end]` partial updates in `TextureManager::DownloadTablePartial()`.
- *Hitman: Codename 47* (2000): Implemented the full Glide 3.x `GR_CLIP_COORDS` homogeneous clipping/projection pipeline and `GR_COLORFORMAT_RGBA`/`BGRA` swizzling across all backends.
- *Lands of Lore: Guardians of Destiny* (1997): Upgraded `SoftwareBackend` line/point loops (`grDrawLine`/`grDrawPoint`) to interpolate and sample TMU0/TMU1 textures, combiners, and chromakey across Bresenham, AA, and `2×2` SSAA passes.
- *MechWarrior 2: Titanium Edition* (1998): Deferred `SoftwareBackend` worker `ThreadPool` startup/teardown to `AttachWindow()`/`DetachWindow()` and added a Win32 `RtlDllShutdownInProgress()` detach guard preventing `DllMain` loader-lock deadlocks.
- *Need for Speed: Porsche Unleashed* (2000): Added `DirectDraw` cooperative-level host-window fingerprint recovery in `NativeSurfaceHost::SyncBounds()`, released Win32 WGL `HGLRC` after `AttachWindow()`/splash, and fixed `GR_TEXTABLE_PALETTE_6666_EXT` 32-bit `ARGB8888` 6-bit quantization.
- *Unreal* (1998): Rewrote `TextureManager::DownloadMipMapPartial()` for auto-allocated compact lightmap streaming and raised the 32-bit Vulkan persistent texture staging slot size from 4 MB to 16 MB.

---

## [0.8.0-beta.2] - 2026-11-19

*(Retrospective milestone tag at commit `fda7459`; 4 verified commercial games.)*

### Added
- **Glide 2.11 (`glide.dll` / `libglide.so`) Frontend**: Dedicated pre-2.3 SST-1 frontend (`src/glide211/`, clean-room `include/glide211/glide.h`, 137 logical C API functions / 274 Win32 PE `__stdcall` + undecorated exports) implementing `grSstOpen`, `grSstPassthruMode`, fixed-1024-pixel-stride `grLfbBegin`/`End`/`GetReadPtr`/`GetWritePtr`/`Origin`/`WriteMode`/`BypassMode`, `guFbReadRegion`/`guFbWriteRegion`, and authentic 2 MB TMU0 VRAM budget enforcement.
- **Borderless Fullscreen Override**: Added `[display] fullscreen = auto | true | false` and `GLIDE_NG_FULLSCREEN` supporting aspect-preserving letterboxing/pillarboxing across SDL2 standalone, Win32 standalone, and Win32 embedded host windows.
- **3 New Catalog Entries (4 Total Commercial Games)**: Added validated guides, presets, and galleries for *CyberGladiators* (1996, Win32 Glide 2.11), *Grand Theft Auto* (1997, DOS), and *Tomb Raider* (1996, DOS), plus expanded *Carmageddon* (1997) captures.

### Changed
- **Vulkan Guest-Resolution RAMDAC Gamma LUT**: Moved Vulkan gamma LUT resolution from swapchain resolution down to guest resolution into double-buffered offscreen targets before the shared aspect-preserving `vkCmdBlitImage` upscale.
- **Leaner Vulkan Pipeline & 32-Bit CPU Adapter Footprint**: Removed speculative 8-pipeline startup pre-warming, shrank 32-bit staging/vertex buffers and swapchain counts on software Vulkan adapters (`llvmpipe`), and added a warning when >1080p swapchains run with uncapped `LP_NUM_THREADS`.

### Fixed
- *Carmageddon* (1997): Flushed persistent LFB write-locks chronologically before `grBufferClear` and framebuffer readbacks.
- *Grand Theft Auto* (1997): Enabled MSAA centroid interpolation to eliminate texture-bleed fringes on rotated sprites.
- *Tomb Raider* (1996): Fixed `guTexMemReset` palette persistence, W-buffer `oow > 1` clipping, depth bias, line-depth health bars, and dropped hardcoded `gamma = 1.7` / `force_trilinear` from catalog presets.
- **DOS Tools & Test Isolation**: Added DOS `PATH` lookup for `GLIDE2X.OVL`, fixed `glide_bench` `ESC` exit handling, and isolated `GLIDE_NG_HEADLESS=1` in unsnapping unit tests.

---

## [0.8.0-beta.1] - 2026-11-18

First structured beta release of `glide-ng`, establishing a self-contained repository, unified semantic versioning, and refactored user/developer documentation on top of full Glide 2.x/3.x and VSA-100 support.

### Highlights
- **Glide 2.x & Glide 3.x Frontends**: Drop-in replacement libraries (`libglide2x.so` / `glide2x.dll`, 152 exports; `libglide3x.so` / `glide3x.dll`, 129 exports) built on a shared C++20 core (`GlideWrapperCore`), preserving pure Glide 2.x ABI parity and bare authentic version strings.
- **Three Rendering Backends**:
  - **Vulkan** (primary): SPIR-V shader pipelines with Early-Z preservation for non-W-buffer draws, native hardware BC1–BC3 (`DXT1`–`DXT5`) texture upload, zero-readback GPU-side `TEXTUREBUFFER` render-to-texture, persistent pipeline cache, and post-scene RAMDAC gamma LUT + HUD overlay.
  - **OpenGL ES 3.2 / Win32 WGL OpenGL 3.3 Core** (secondary): EGL 1.5 on Linux (X11/Wayland/surfaceless headless) and WGL 3.3 Core on Win32/Wine, hardware S3TC/BCn texture upload, GPU-side RTT blit, and state-deduplicated vertex ring buffer.
  - **Software** (CPU reference rasterizer): Multi-threaded tiled rasterizer with runtime CPUID/OS-XSAVE SIMD dispatch (`scalar`, `avx2` default, `avx512` 4×4 16-pixel rasterizer, `auto`), 13 bit-exact shared SIMD kernels, vectorized Table Fog, hardware Stencil, and 32×32 Stipple.
- **Native Window & Emulator Hosting**: `NativeSurfaceHost` supporting headless CI, standalone SDL2 (Linux) / Win32 `HWND`, unpatched DOSBox-X / SVN DOSBox child surfaces (X11 child window and Wayland `wl_subsurface`), and Win32 parent-embedded `HWND` hosting with aspect-ratio-preserving presentation.
- **Win32 32-Bit (`i686`) DLLs & Wine/Proton Support**: Self-contained 32-bit `glide2x.dll` and `glide3x.dll` (zero external runtime DLL dependencies, canonical `_Func@N` `__stdcall` exports + undecorated aliases, embedded PE `VS_VERSION_INFO`) auto-built alongside Linux binaries when MinGW-w64 is installed.
- **Seven Authentic 3dfx Hardware Profiles & VSA-100 Extensions**: Emulates `VoodooGraphics` (SST-1), `VoodooRush` (SST-96), `Voodoo2` (CVG, default), `VoodooBanshee`, `Voodoo3`, `Voodoo4`, and `Voodoo5` with authentic capability/resolution queries, warn-only card-to-version checking, and VSA-100 `PIXEXT` (Spatial AA, T-Buffer, 8-bit Stencil, per-channel `grColorMaskExt`), `COMBINE`, `TEXFMT` (FXT1, DXT1–DXT5, YUV, RGB888, ARGB8888), `TEXTUREBUFFER`, `TEXUMA`, and `GETGAMMA`.
- **Unified Configuration, Diagnostics & HUD**: `glide-ng.ini`, 20 authentic `FX_GLIDE_*`/`SST_*`/`SSTV2_*` environment variables, 31 `GLIDE_NG_*` overrides, tiered/category/frame-windowed logging, per-frame CPU/GPU CSV telemetry, and DXVK-style on-screen HUD (`GLIDE_NG_HUD`).
- **Portable Diagnostic & Benchmark Tools**: Pure C99 `glide_info`, `glide_bench` (9 Glide 2.x + 14 Glide 3.x scenes with GlideMark composite scoring), and `glide_envtest` targeting Linux, Win32/Wine, and optional Open Watcom v2 DOS/32 (`GLIDE2X.OVL`).
- **Compatibility Catalog & Self-Contained Tests**: Curated `docs/catalog/` with per-title guides and validated drop-in `glide-ng.ini` presets (commercial games, official 3dfx demos, hardware tests, tools), plus 106 self-contained Catch2 tests requiring zero external SDK or demo assets.

### Changed & Cleaned Up
- Adopted **GNU GPLv3 (`GPL-3.0-or-later`)** (`LICENSE`, `THIRD_PARTY_NOTICES.md`), replacing all legacy 3dfx-licensed ABI headers under `include/` with clean-room GPL-3.0 ABI headers and replacing embedded 3dfx splash/banner binary assets with a clean-room procedural **Chrome & Amber Shield** 3D `grSplash` animation and `180×90` `GR_SHAMELESS_PLUG` corner watermark.
- Unified single-source versioning via root `VERSION`, `tools/gen_version_header.cmake`, `scripts/bump_version.sh`, and Win32 PE `VS_VERSION_INFO`.
- Decoupled Catch2 test suites and standalone demos from external `refs/` and `ext_tests/` assets; removed deprecated test dumps, ad-hoc `scratch/` scripts, third-party `refs/` binaries/PDFs, and local-only `.gitmodules`.
- Refactored `docs/` into structured User Guide (`docs/user-guide/`), Developer Guide (`docs/developer-guide/`), Compatibility Catalog (`docs/catalog/`), historical retrospective (`docs/HISTORY.md`), and forward-looking roadmap (`docs/ROADMAP.md`).

---

[Unreleased]: https://github.com/eugenenuke/glide-ng-alpha/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/eugenenuke/glide-ng-alpha/compare/v0.8.0-beta.4...v0.9.0
[0.8.0-beta.4]: https://github.com/eugenenuke/glide-ng-alpha/compare/v0.8.0-beta.3...v0.8.0-beta.4
[0.8.0-beta.3]: https://github.com/eugenenuke/glide-ng-alpha/compare/v0.8.0-beta.2...v0.8.0-beta.3
[0.8.0-beta.2]: https://github.com/eugenenuke/glide-ng-alpha/compare/v0.8.0-beta.1...v0.8.0-beta.2
[0.8.0-beta.1]: https://github.com/eugenenuke/glide-ng-alpha/releases/tag/v0.8.0-beta.1
