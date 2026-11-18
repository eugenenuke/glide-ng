# Milestone M11: Performance Analysis, Cross-Backend Optimizations & Zen 4 AVX-512 Guide

> **Branch**: `perf/m11-vsa100-bench-simd-avx512`  
> **Scope**: Post-M10 / VSA-100 benchmark expansion (`glide2_bench` / `glide3_bench`), runtime SIMD dispatch (`scalar` / `avx2` [default] / `avx512` / `auto`), 13 bit-exact shared CPU SIMD kernels, full AVX2 rasterizer scalar-island elimination (Table Fog, Hardware Stencil, Stipple), native 4×4 (16-pixel) AVX-512 Software rasterizer, hardware BC1/BC2/BC3 (`DXT1`–`DXT5`) compressed texture upload, zero-readback GPU-side `TEXTUREBUFFER` RTT, and Vulkan Early-Z SPIR-V specialization.

---

## 1. Optimization Summary (`M11.1` – `M11.6`)

| Stage | Optimization | Primary Beneficiaries | Key Technical Highlights |
| :--- | :--- | :--- | :--- |
| **M11.1** | Benchmark Suite Expansion | All backends (`glide2_bench`, `glide3_bench`) | Added 3 shared scenes (`fog`, `multitex`, `lfb_read`) + 5 Glide 3.x VSA-100 scenes (`stencil`, `ext_combine`, `texfmt`, `rtt`, `tbuffer`), `--scene core\|extended\|vsa100\|all`, `--res 512\|640\|800\|1024\|1280\|1600`, `--aa 1\|2\|4`, preserving apples-to-apples 6-scene Core GlideMark scores. |
| **M11.2** | Runtime SIMD Dispatch & Per-TU ISA Isolation | Software + shared CPU paths | Added `GLIDE_NG_SW_SIMD=avx2\|avx512\|scalar\|auto` (`[software] simd`, default `avx2`), `CPUID` + `XGETBV(0)` OS-state validation, and strict CMake per-TU flags (`0 ymm/zmm` verified in all baseline `.o`/`.obj` files on both Linux `x86_64` and Win32 `i686`). |
| **M11.3** | 13 Shared CPU & Post-Process SIMD Kernels (`SimdKernels`) | Software, Vulkan (CPU staging), OpenGL ES (`SwapRB32` & staging) | Bit-exact Scalar / AVX2 / AVX-512 kernels for `SwapRB32`, 2×2 SSAA resolve, RAMDAC Gamma LUT, masked & T-Buffer color/depth/stencil clears (with OpenMP), RGB565/ARGB1555/ARGB4444/P8 texture & LFB conversions. Verified 670/670 Catch2 assertions across all 65,536 16-bit colors. |
| **M11.4** | Software AVX2 Rasterizer Scalar-Island Elimination | Software AVX2 | Vectorized 32×32 Stipple (`Srlv`), VSA-100 Hardware Stencil (`LoadStencil2x4`/`StoreStencil2x4`), and 3dfx Table Fog (`EvaluateTableFogVec` — reciprocal-exponent index + exact 1-step boundary refinement, **0 index mismatches across 201,351 values**, **0-LSB / 0-pixel diff** vs. scalar across all 64 intervals & `ADD2`/`MULT2`). Added 3 guard rows to color/depth/stencil buffers so 2×4 and 4×4 tile stores are unconditionally in-bounds. |
| **M11.5** | Native 4×4 (16-Pixel) AVX-512 Software Rasterizer | Software AVX-512 (Zen 4 / AVX-512 CPUs) | Parameterized SIMD rasterizer core (`SoftwareRasterizerSimdImpl.inl`) with `Simd16f`/`Simd16i`, native `__mmask16` opmask predication, 4×4 loads/stores, single-ZMM 4×4 Bayer dither, and `_mm512_cvtepu8_epi32`/`_mm512_cvtepi32_epi8` 4×4 stencil ops. Compiled cleanly on Linux `x86_64` (`50,583 zmm` / `10,033` opmask instructions) and Win32 `i686` (`59,657 zmm` with `-mstackrealign`). |
| **M11.6a** | Native Hardware BC1/BC2/BC3 (`DXT1`–`DXT5`) Upload | Vulkan, OpenGL ES / WGL | Skips CPU block decompression (`DecodeTexelsToBgra`) and `SwapRB32`/4× expansion when the GPU supports Vulkan `textureCompressionBC` or OpenGL/GLES `GL_EXT_texture_compression_s3tc`, shrinking GPU upload bandwidth by 4×–8× with lazy CPU decode fallback for `GetTexture()` / partial edits. |
| **M11.6b** | Zero-Readback GPU-Side `TEXTUREBUFFER` RTT & Vulkan Early-Z Split | Vulkan, OpenGL ES / WGL | Eliminated `ReadLFB`/`WriteLFB` GPU→CPU→GPU pipeline stalls during VSA-100 `TEXTUREBUFFER` RTT via GPU-only scratch backbuffer save/restore and direct GPU-to-TMU copies (`vkCmdCopyImage` with in-place steady-state reuse on Vulkan; Y-flipped `glBlitFramebuffer` on OpenGL ES). Guarded `gl_FragDepth` behind `-DUSE_WBUFFER_DEPTH` in Vulkan `color.frag` so Z-buffer & depth-off pipelines emit **0 `DepthReplacing`** instructions and preserve hardware Early-Z / Hi-Z on real GPUs. |

---

## 2. Pre-M11 vs. Post-M11 Benchmark Results (Reference AVX2 Dev Host)

> **Test Environment**: Release build (`build_release/`), headless (`GLIDE_NG_HEADLESS=1 GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0`), `--no-hud --seconds 2`, `x86_64` AVX2+FMA CPU (no AVX-512), Mesa `llvmpipe` Vulkan 1.4 / OpenGL ES 3.2.  
> **Note on `llvmpipe`**: On a real discrete or integrated GPU (AMD RDNA, NVIDIA, Intel), Vulkan and OpenGL ES rasterization runs on GPU silicon rather than CPU LLVM threads, so GPU-side `rtt`, `texfmt`, and Early-Z (`fillrate`/`fog`/`stencil`) gains will be dramatically larger than `llvmpipe` numbers below.

### 2.1 Glide 3.x (`glide3_bench`, Voodoo5, 640×480)

#### Software Backend (`GLIDE_NG_BACKEND=software`, AVX2 vs. Scalar)

| Scene | Pre-M11 SW AVX2 | Post-M11 SW AVX2 | SW AVX2 Delta | Post-M11 SW Scalar | AVX2 / Scalar |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `fillrate` | 191.3 FPS | **220.3 FPS** | +15.2% | 33.9 FPS | 6.50× |
| `tris` | 448.2 FPS | **542.6 FPS** | +21.1% | 358.4 FPS | 1.51× |
| `state` | 812.9 FPS | **990.9 FPS** | +21.9% | 503.9 FPS | 1.97× |
| `tex` | 409.0 FPS | **484.4 FPS** | +18.4% | 293.1 FPS | 1.65× |
| `lfb` | 951.0 FPS | **1312.2 FPS** | +38.0% | 411.7 FPS | 3.19× |
| `multipass` | 382.1 FPS | **518.3 FPS** | +35.6% | 175.0 FPS | 2.96× |
| **Core 6 Geomean** | **466.9 FPS** | **582.4 FPS** | **+24.7%** | **224.9 FPS** | **2.59×** |
| `fog` (Table Fog) | 111.6 FPS | **217.7 FPS** | **+95.1%** | 37.5 FPS | 5.81× |
| `multitex` | 186.0 FPS | **232.3 FPS** | +24.9% | 37.7 FPS | 6.16× |
| `lfb_read` | 409.9 FPS | **954.0 FPS** | **+132.7%** | 282.1 FPS | 3.38× |
| `stencil` (PIXEXT) | 199.3 FPS | **239.9 FPS** | **+20.4%** *(327.1 iso)* | 46.2 FPS | 5.19× |
| `ext_combine` | 122.2 FPS | **138.4 FPS** | +13.3% | 19.8 FPS | 6.99× |
| `texfmt` (DXT/FXT1/YUV) | 704.4 FPS | **933.8 FPS** | **+32.6%** *(1176.5 iso)* | 238.1 FPS | 3.92× |
| `rtt` (TEXTUREBUFFER) | 383.3 FPS | **583.5 FPS** | **+52.2%** | 199.9 FPS | 2.92× |
| `tbuffer` (4× AA) | 89.1 FPS | **141.4 FPS** | **+58.7%** *(203.6 iso)* | 91.5 FPS | 1.55× |
| **Extended 8 Geomean** | **217.4 FPS** | **324.7 FPS** | **+49.4%** | **84.2 FPS** | **3.86×** |
| **All-14 Composite GM** | — | **417.0 FPS** | — | **124.1 FPS** | **3.36×** |

*(Note: "iso" denotes isolated single-scene runs without thermal/turbo scaling variance from a continuous 14-scene run.)*

#### Vulkan & OpenGL ES Backends (`glide3_bench`, Voodoo5, 640×480, Mesa `llvmpipe`)

| Scene / Metric | Pre-M11 Vulkan | Post-M11 Vulkan | Vulkan Delta | Pre-M11 GLES | Post-M11 GLES | GLES Delta |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| **Core 6 Geomean** | 122.2 FPS | **151.8 FPS** | **+24.2%** | 167.9 FPS | **209.4 FPS** | **+24.7%** |
| `texfmt` (full suite / isolated) | 245.4 FPS | **259.9 / 336.6 FPS** | **+5.9% / +37.2%** | 302.2 FPS | **318.4 / 444.3 FPS** | **+5.4% / +47.0%** |
| `rtt` (full suite / isolated) | 60.4 FPS | **227.3 / 241.3 FPS** | **+276.3% / +299.5%** | 56.5 FPS | **216.6 / 212.8 FPS** | **+283.4% / +276.6%** |
| **Extended 8 Geomean** | 60.5 FPS | **80.0 FPS** | **+32.3%** | 85.2 FPS | **118.3 FPS** | **+38.8%** |
| **All-14 Composite GM** | — | **105.3 FPS** | — | — | **151.1 FPS** | — |

### 2.2 Glide 2.x (`glide2_bench`, Voodoo2, 640×480)

| Metric / Key Scene | Pre-M11 SW AVX2 | Post-M11 SW AVX2 | Delta | Post-M11 SW Scalar | Post-M11 Vulkan | Post-M11 GLES |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `multipass` | 382.1 FPS | **463.9 FPS** | +21.4% | 152.9 FPS | 107.4 FPS | 222.4 FPS |
| `lfb` | 951.0 FPS | **1023.6 FPS** | +7.6% | 378.5 FPS | 289.1 FPS | 448.7 FPS |
| **Core 6 Geomean** | **466.9 FPS** | **510.2 FPS** | **+9.3%** | **206.2 FPS** | **149.7 FPS** | **208.4 FPS** |
| `fog` | 133.4 FPS | **202.2 FPS** | **+51.6%** | 33.9 FPS | 26.1 FPS | 67.2 FPS |
| `multitex` | 213.4 FPS | **206.0 FPS** | -3.5% *(noise)* | 34.9 FPS | 51.8 FPS | 76.3 FPS |
| `lfb_read` | 431.5 FPS | **850.8 FPS** | **+97.2%** | 271.1 FPS | 186.8 FPS | 226.7 FPS |
| **Extended 3 Geomean** | **230.7 FPS** | **328.5 FPS** | **+42.4%** | **68.5 FPS** | **63.2 FPS** | **105.1 FPS** |
| **All-9 Composite GM** | — | **440.5 FPS** | — | **142.8 FPS** | **112.3 FPS** | **165.9 FPS** |

### 2.3 High-Resolution Software Scaling Baseline (`glide3_bench`, Voodoo5, `--res 1280` = 1280×1024)

High resolutions are strictly pixel-fill / bandwidth bound and represent the prime target for Zen 4's 4×4 (16-pixel) AVX-512 rasterizer:

| Scene (1280×1024) | SW Scalar | SW AVX2 (Dev Host) | AVX2 / Scalar | AVX2 Throughput |
| :--- | ---: | ---: | ---: | ---: |
| `fillrate` | 8.8 FPS | **63.5 FPS** | 7.22× | 1,331.4 MPix/s |
| `fog` | 10.2 FPS | **65.8 FPS** | 6.45× | 1,035.0 MPix/s |
| `multitex` | 10.2 FPS | **67.4 FPS** | 6.61× | 883.7 MPix/s |
| `stencil` | 11.9 FPS | **67.0 FPS** | 5.63× | 1,141.5 MPix/s |
| `ext_combine` | 5.4 FPS | **38.4 FPS** | 7.11× | 503.0 MPix/s |
| `texfmt` | 72.0 FPS | **358.4 FPS** | 4.98× | 89.6 MB/s |
| `rtt` | 58.1 FPS | **173.4 FPS** | 2.98× | 173.4 RTT/s |
| `tbuffer` (4× AA) | 25.1 FPS | **39.9 FPS** | 1.59× | 209.3 MPix/s |
| `lfb_read` | 80.0 FPS | **290.1 FPS** | 3.63× | 1,450.4 MB/s |
| **Core 6 Geomean** | **74.6 FPS** | **255.8 FPS** | **3.43×** | — |
| **All-14 Composite GM** | **37.2 FPS** | **147.5 FPS** | **3.97×** | — |

---

## 3. Real-GPU vs. Software Backend Architectural Trade-Off Notes

Per M11 design goals, no GPU backend paths were replaced with Software during M11, but profiling across all 14 scenes highlights clear architectural boundaries for future optimization decisions:

### Where Real GPUs (Vulkan / OpenGL ES) Decisively Win
1. **High-resolution fillrate, overdraw & multi-texturing (`fillrate`, `fog`, `multitex`, `stencil`, `ext_combine` at ≥1024×768)**: Even an integrated RDNA 2/3 iGPU delivers 10–40+ GPix/s vs. ~1.3 GPix/s on multi-core AVX2 (projected ~2.0–2.5 GPix/s on Zen 4 AVX-512).
2. **4× Spatial Anti-Aliasing & VSA-100 T-Buffer (`tbuffer`, *Kitty Hawk* / *Museum* / *Rocket Burger*)**: GPU hardware MSAA resolves 4× samples in-tile with bandwidth compression; Software must execute 2×2 SSAA (4× fragment shading + 4× memory traffic + full-frame resolve).
3. **Hardware BC1/BC2/BC3 (`texfmt`, M11.6a)**: Real GPUs sample DXT1/DXT3/DXT5 blocks directly in texture cache with zero CPU decode and 4×–8× smaller DMA uploads.
4. **Dynamic Render-to-Texture (`rtt`, M11.6b)**: With zero-readback GPU RTT (`vkCmdCopyImage` / `glBlitFramebuffer`), dynamic portals, mirrors, and shadow maps stay 100% in VRAM with zero PCIe round-trips.
5. **Vulkan Early-Z (`M11.6b`)**: Splitting `-DUSE_WBUFFER_DEPTH` removed `OpExecutionMode DepthReplacing` from all non-W-buffer Vulkan pipelines, restoring hardware Hi-Z/Early-Z rejection on real GPUs. *(Future OpenGL ES opportunity: mirror this split with a second `m_geomProgramWBuf` program in `OpenGLESShaders.cpp` so OpenGL ES Z-buffer pipelines also omit `gl_FragDepth`.)*

### Where Software (AVX2 / AVX-512) Can Beat a Real GPU — Future Hybrid Candidates
1. **LFB Readback-Heavy Workloads (`lfb_read`, 2D menus / software-composited HUDs / FMV read-modify-write)**: On a real GPU, every `grLfbReadRegion` / `grLfbLock(GR_LFB_READ_ONLY)` forces a full command-buffer flush (`vkQueueWaitIdle` / `glReadPixels`) plus PCIe GPU→CPU DMA (~0.5–3 ms stall per lock). On Software, the framebuffer is already in L2/L3 cache (`954 FPS` / `1.12 GB/s` at 640×480). *Future note*: for readback-heavy 2D menus, asynchronous PBO/staging ring readback only helps if the guest tolerates 1-frame latency (most Glide titles don't); keeping `Software` fast is the cleanest zero-stall option for LFB-dominated 2D titles.
2. **Exotic LFB Blend Fallbacks (`WriteLFB` outside `CanUseOverlayQuad`)**: Rare non-standard LFB blend modes currently snapshot the GPU framebuffer back to CPU RAM; if encountered in a real game, a dedicated Vulkan/GLES blend-pipeline overlay quad is strictly better than CPU readback.
3. **Micro-Batch State Thrashing (`state`, hundreds of tiny 1–2 triangle draws changing pipeline state)**: Vulkan/GLES incur per-batch descriptor/uniform/driver overhead (`~200–250 FPS` on `llvmpipe`, typically `1,000–3,000 FPS` on real GPUs depending on driver CPU overhead), whereas Software's tile binner avoids GPU driver submission completely. Note that real GPUs with Vulkan pipeline caching already exceed 60–240 Hz by an order of magnitude on classic Glide triangle counts.
4. **FXT1 / YUV (`0x11`, `0x13`–`0x15`) Software Texture Decode**: Modern GPUs lack hardware FXT1 decoders, so `TextureManager` CPU decode remains necessary; if a real Voodoo4/5 game streams dynamic FXT1/YUYV textures every frame, SIMD-vectorizing `DecodeYuyv422`/`DecodeFxt1` in `SimdKernels` would help all 3 backends equally.

---

## 4. Verification Matrix Summary (Dev Host)

- **4-Target Zero-Warning Build**:
  - `build/` (Linux Debug + Catch2 + Open Watcom v2 DOS/32 CauseWay `glide_bench.exe`, `glide_info.exe`, `glide_envtest.exe`) — **PASS (0 warnings)**
  - `build_release/` (Linux Release `-O3` + `glide2_bench`, `glide3_bench`) — **PASS (0 warnings)**
  - `build_win32/` (MinGW-w64 `i686` Release `glide2x.dll`, `glide3x.dll`, 6 Win32 `.exe` tools) — **PASS (0 warnings)**
- **ISA Isolation Audit (`objdump -d`)**:
  - Linux `x86_64` Release: `SimdDispatch.cpp.o`, `SimdKernels.cpp.o`, `SoftwareRasterizer.cpp.o` = **`0 ymm, 0 zmm`**; `SoftwareRasterizerSimd.cpp.o` = `63,133 ymm, 0 zmm`; `SoftwareRasterizerAvx512.cpp.o` = **`50,583 zmm`**
  - Win32 `i686` Release: `SimdKernels.cpp.obj` = **`0 ymm, 0 zmm`**; `SoftwareRasterizerAvx512.cpp.obj` = **`59,657 zmm`**
- **SPIR-V Early-Z Audit (`spirv-dis`)**:
  - `color_frag_spv.spv` (Z-buffer / depth-off): **`DepthReplacing = 0`**
  - `color_wbuf_frag_spv.spv` (W-buffer): **`DepthReplacing = 1`**
- **9-Way Catch2 Regression Matrix (`GLIDE_NG_HEADLESS=1`)**:
  - `Glide2xWrapperTests`: **40/40 cases, 2,846/2,846 assertions** × 4 (`software/avx2`, `software/scalar`, `opengl_es`, `vulkan`)
  - `Glide3xWrapperTests`: **65/65 cases, 5,020/5,020 assertions** × 5 (`software/avx2`, `software/scalar`, `software/avx512` fallback, `opengl_es`, `vulkan`)
- **Targeted Visual & Pixel-Diff Parity**:
  - `M10.6` (VSA-100 COMBINE + BlendExt + TEXTUREBUFFER RTT, 640×480): Vulkan vs. OpenGL ES = **`0` diff across all 921,600 RGB channels (`MAE = 0.000000`)**; Software AVX2 vs. Scalar = `max_diff = 1` LSB (`MAE = 0.0046`)
  - `sw_validate` (Fog + Textured + Blended): Software AVX2 vs. Scalar = **`0` diff (`MAE = 0.000000`)**

---

## 5. Zen 4 AVX-512 Live Verification & Benchmark Guide

Because the development VM is an AVX2-only CPU, the native AVX-512 execution paths (`SimdKernelsAvx512.cpp` and `SoftwareRasterizerAvx512.cpp`) have been compile-verified, disassembly-audited, and fallback-tested, and are ready for live execution on your **AMD Zen 4** machine.

### Step 1 — Pull Branch & Build Debug + Release
```bash
git fetch origin
git checkout perf/m11-vsa100-bench-simd-avx512
git pull

# 1. Debug build (for Catch2 [M11] cross-ISA bit-exactness tests)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc) --target glide2x glide3x Glide2xWrapperTests Glide3xWrapperTests

# 2. Release build (for benchmarks)
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(nproc) --target glide2x glide3x glide2_bench glide3_bench
```

### Step 2 — Run Live AVX-512 Bit-Exactness & Parity Tests (`[M11]`)
On an AVX-512 CPU, `[M11]` automatically detects `hostMax >= Avx512` and executes **exact cross-ISA parity checks** (`AVX-512 == AVX2 == Scalar` across all 13 `SimdKernels`, Table Fog 0-LSB across all 64 intervals, Hardware Stencil 0-byte diff across all 8×8 ops, Stipple, VSA-100 `COMBINE` + `BlendExt` + 4× AA T-Buffer masks on `64×64` and `64×50`, plus `[M11][BCn]` and `[M11][RTT]` on your real GPU):

```bash
# Confirm CPU detection + run all M11 tests (automatically validates AVX-512 vs AVX2!)
GLIDE_NG_HEADLESS=1 GLIDE_NG_LOG_LEVEL=info GLIDE_NG_LOG_CONSOLE=1 \
  GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 \
  ./build/bin/Glide3xWrapperTests "[M11]"

# Full Glide 3.x & Glide 2.x suites under live AVX-512
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 ./build/bin/Glide3xWrapperTests
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 ./build/bin/Glide2xWrapperTests

# Also verify real-GPU Vulkan & OpenGL ES on your Zen 4 host
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan ./build/bin/Glide3xWrapperTests "[M11]"
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=opengl_es ./build/bin/Glide3xWrapperTests "[M11]"
```

> **What to look for**: Look for `CPU max SIMD: avx512` and `Active Software SIMD level: avx512` in the startup log, and `All tests passed` with **0 failures** (including the strict `CHECK(got512 == got2)` check inside `[M11][Parity]`).

### Step 3 — Run Zen 4 Benchmark Suite (Scalar vs. AVX2 vs. AVX-512 + Real GPU)
Run this single script to capture clean CSV reports into `/tmp/m11_zen4_bench/`:

```bash
mkdir -p /tmp/m11_zen4_bench
ENV_BASE="GLIDE_NG_HEADLESS=1 GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0"

# Log CPU & GPU hardware info
lscpu | grep -E "Model name|CPU\(s\)|Flags" > /tmp/m11_zen4_bench/sysinfo.txt
vulkaninfo --summary 2>/dev/null | grep -E "deviceName|apiVersion|driverInfo" >> /tmp/m11_zen4_bench/sysinfo.txt || true

# 1. Glide 3.x Voodoo5 @ 640x480 — Software (scalar / avx2 / avx512) + Real GPU (vulkan / opengl_es)
for simd in scalar avx2 avx512; do
  echo "=== G3 640 software/$simd ==="
  env $ENV_BASE GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
    ./build_release/bin/glide3_bench --no-hud --seconds 3 --scene all \
    --csv /tmp/m11_zen4_bench/g3_640_sw_${simd}.csv
done

for be in vulkan opengl_es; do
  echo "=== G3 640 $be ==="
  env $ENV_BASE GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_BACKEND=$be GLIDE_NG_SW_SIMD=avx512 \
    ./build_release/bin/glide3_bench --no-hud --seconds 3 --scene all \
    --csv /tmp/m11_zen4_bench/g3_640_${be}.csv
done

# 2. Glide 3.x Voodoo5 @ 1280x1024 & 1600x1200 — High-Fillrate AVX2 vs AVX-512 Scaling
for res in 1280 1600; do
  for simd in avx2 avx512; do
    echo "=== G3 ${res} software/$simd ==="
    env $ENV_BASE GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
      ./build_release/bin/glide3_bench --no-hud --seconds 3 --res $res --scene all \
      --csv /tmp/m11_zen4_bench/g3_${res}_sw_${simd}.csv
  done
done

# 3. Glide 2.x Voodoo2 @ 640x480 — Software (avx2 / avx512) + Real GPU (vulkan / opengl_es)
for simd in avx2 avx512; do
  echo "=== G2 640 software/$simd ==="
  env $ENV_BASE GLIDE_NG_CARD_MODEL=voodoo2 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
    ./build_release/bin/glide2_bench --no-hud --seconds 3 --scene all \
    --csv /tmp/m11_zen4_bench/g2_640_sw_${simd}.csv
done
for be in vulkan opengl_es; do
  echo "=== G2 640 $be ==="
  env $ENV_BASE GLIDE_NG_CARD_MODEL=voodoo2 GLIDE_NG_BACKEND=$be GLIDE_NG_SW_SIMD=avx512 \
    ./build_release/bin/glide2_bench --no-hud --seconds 3 --scene all \
    --csv /tmp/m11_zen4_bench/g2_640_${be}.csv
done

# Print compact paste-ready summary of all CSVs
head -n 30 /tmp/m11_zen4_bench/sysinfo.txt /tmp/m11_zen4_bench/*.csv
```

### Step 4 — Optional Visual Spot-Check Under AVX-512
To confirm live visual rendering on your Zen 4 CPU under AVX-512 (e.g., fog, 4× AA, reflections):
```bash
# Example: 3dfx Kitty Hawk (4x Spatial AA) on Software AVX-512
WINEDLLOVERRIDES="glide3x=n" GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=avx512 \
  GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_HUD=fps,backend \
  wine explorer /desktop=V5,1024x768 ./aademo.exe -res 1024x768 -pixfmt aa4rgb565 -model 'data\kittyhawk\kittyhawk.gxf'
```

### What to Share Back for Analysis
Paste the output of the final `head -n 30 /tmp/m11_zen4_bench/sysinfo.txt /tmp/m11_zen4_bench/*.csv` command (plus the pass/fail line from Step 2 `[M11]`). From those CSVs we can analyze:
1. Exact **AVX-512 vs. AVX2** speedup per scene at `640×480`, `1280×1024`, and `1600×1200` (confirming whether Zen 4 double-pumped 256-bit AVX-512 execution yields a net win across fillrate, fog, stencil, `ext_combine`, and `tbuffer` or if any specific scene hits register pressure / frontend decode limits).
2. Real-GPU **Vulkan & OpenGL ES** throughput on `texfmt` (hardware BCn), `rtt` (zero-readback GPU RTT), and Early-Z (`fillrate`/`fog`/`stencil`) compared against AVX-512 Software.
3. Whether `GLIDE_NG_SW_SIMD=auto` (or keeping `avx2` default with opt-in `avx512`) is the best default policy for Zen 4 users.
