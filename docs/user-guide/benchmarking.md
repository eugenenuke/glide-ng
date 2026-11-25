# Benchmarking & Performance Guide

`glide-ng` includes a cross-platform benchmark suite (`glide2_bench` and `glide3_bench` on Linux, Win32/Wine, and DOS/32) that exercises classic Glide 2.x and VSA-100 Glide 3.x workloads and reports a composite **GlideMark** score.

For complete CLI options, CSV export flags, and drop-in benchmark presets, see [`docs/catalog/tools/glide-bench/README.md`](../catalog/tools/glide-bench/README.md).

---

## 1. Benchmark Scenes

- **Core 6 scenes** (`--scene core`, shared across Glide 2.x and Glide 3.x): `fillrate`, `tris`, `state`, `tex`, `lfb`, `multipass`.
- **Extended shared scenes** (`--scene extended`): `fog` (64-entry Table Fog), `multitex` (2-TMU modulation), `lfb_read` (LFB readback).
- **VSA-100 Glide 3.x scenes** (`--scene vsa100`, requires `Voodoo4` or `Voodoo5`): `stencil` (`PIXEXT` hardware stencil), `ext_combine` (`COMBINE` 4-input equation), `texfmt` (`DXT1`–`DXT5`, `FXT1`, `YUYV`, `ARGB_8888`), `rtt` (`TEXTUREBUFFER` render-to-texture), `tbuffer` (4× Spatial AA + T-Buffer).

---

## 2. Running Reproducible Benchmarks

Always use a **Release** build (`build_release/`) with frame pacing disabled (`GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0`) and headless mode (`GLIDE_NG_HEADLESS=1`) for repeatable measurements:

```bash
# Glide 3.x full 14-scene Voodoo5 benchmark across Vulkan, OpenGL ES, and Software
for be in vulkan opengl_es software; do
  echo "=== $be ==="
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=$be GLIDE_NG_CARD_MODEL=voodoo5 \
    GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0 \
    ./build_release/bin/glide3_bench --scene all --res 640 --no-hud --seconds 3
done
```

> **DOSBox Virtual Clock Drift Detection (`glide_bench.exe`)**: Under DOSBox (`cycles = max`), heavy synchronous host Glide wrappers can stall DOSBox's emulated PIT timer behind real wall-clock time, causing scenes such as `tex_churn` or `poly_fan` to report artificially inflated FPS. `glide_bench.exe` samples the host-backed CMOS RTC wall clock around each scene, marks drifted scenes with `*` in the summary table, and prints the estimated real wall-clock FPS in a footnote.

### Benchmarking Software SIMD Tiers (`scalar` vs. `avx2` vs. `avx512`)

```bash
for simd in scalar avx2 avx512; do
  echo "=== software ($simd) ==="
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
    GLIDE_NG_CARD_MODEL=voodoo5 GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0 \
    ./build_release/bin/glide3_bench --scene all --res 1280 --no-hud --seconds 3
done
```

### Benchmarking Against Mesa `llvmpipe` (Software Vulkan & OpenGL ES)

To compare `glide-ng`'s native CPU Software backend directly against Mesa's LLVM-JIT software Vulkan (`lavapipe`) and OpenGL ES (`llvmpipe`) drivers on a Linux host with a discrete GPU:

```bash
# Vulkan via Mesa lavapipe (adjust filename if your distro uses lvp_icd.x86_64.json)
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan GLIDE_NG_CARD_MODEL=voodoo5 \
  GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0 \
  ./build_release/bin/glide3_bench --scene all --res 640 --no-hud --seconds 3

# OpenGL ES via Mesa llvmpipe (pin Mesa EGL ICD on NVIDIA GLVND systems)
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  LIBGL_ALWAYS_SOFTWARE=1 \
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=opengl_es GLIDE_NG_CARD_MODEL=voodoo5 \
  GLIDE_NG_MAX_FPS=0 GLIDE_NG_VSYNC=0 FX_GLIDE_SWAPINTERVAL=0 \
  ./build_release/bin/glide3_bench --scene all --res 640 --no-hud --seconds 3
```

> On Linux hosts with the proprietary NVIDIA driver, `LIBGL_ALWAYS_SOFTWARE=1` alone is ignored by NVIDIA's GLVND EGL vendor (`10_nvidia.json`). Setting `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json` routes EGL to Mesa so `llvmpipe` activates cleanly.

---

## 3. Reference Results

Measured on **AMD Ryzen 9 7945HX** (16C/32T Zen 4 with AVX-512) + **NVIDIA GeForce RTX 4070 Laptop GPU**, Release build, `glide3_bench`, `Voodoo5`, headless, `--no-hud --seconds 3`.

### 3.1 640×480 Cross-Backend Comparison (FPS)

| Scene | SW Scalar | SW AVX2 | SW AVX-512 | AVX-512 / AVX2 | RTX 4070 Vulkan | RTX 4070 OpenGL ES |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `fillrate` | 67.9 | 514.1 | **668.6** | +30.1% | 2,592.3 | 2,701.6 |
| `tris` | 797.4 | 1,232.6 | **1,264.1** | +2.6% | 1,805.8 | 2,591.5 |
| `state` | 1,262.3 | 2,617.8 | **2,776.3** | +6.1% | **3,799.7** | 560.5 |
| `tex` | 700.6 | 1,418.7 | **1,566.4** | +10.4% | 1,007.3 | 541.6 |
| `lfb` | 1,016.0 | 3,559.0 | **4,163.4** | +17.0% | **5,878.8** | 4,329.3 |
| `multipass` | 369.8 | 1,243.8 | **1,371.0** | +10.2% | 3,771.1 | 4,065.8 |
| `fog` | 74.8 | 512.4 | **629.3** | +22.8% | **2,809.7** | 1,534.7 |
| `multitex` | 74.6 | 526.1 | **692.2** | +31.6% | 2,992.4 | 3,265.9 |
| `lfb_read` | 635.4 | 2,751.2 | **3,258.3** | +18.4% | 400.4 | 1,435.2 |
| `stencil` | 94.6 | 625.6 | **837.3** | +33.9% | **3,873.6** | 2,753.4 |
| `ext_combine` | 46.3 | 315.1 | **460.7** | +46.2% | 1,864.0 | 1,581.5 |
| `texfmt` | 540.0 | 2,173.1 | **2,552.2** | +17.4% | **6,244.9** | 4,433.2 |
| `rtt` | 472.7 | 1,848.3 | **2,063.4** | +11.6% | 6,193.2 | **6,328.2** |
| `tbuffer` (4× AA) | 219.0 | 352.3 | **459.2** | +30.4% | **5,098.6** | 3,955.1 |
| **Core 6 Geomean** | **511.9** | **1,477.8** | **1,660.7** | **+12.4%** | **2,711.2** | **1,828.8** |
| **Extended 8 Geomean** | **174.7** | **821.2** | **1,035.7** | **+26.1%** | **2,884.5** | **2,771.0** |
| **All-14 Composite GM** | **277.0** | **1,056.3** | **1,268.0** | **+20.0%** | **2,808.9** | **2,318.9** |

On the same Ryzen 9 7945HX CPU, `glide-ng` Software AVX-512 scores **34,963 GlideMark points** versus **9,886 points** for Vulkan Mesa `llvmpipe` (**3.54× faster**).

### 3.2 High-Resolution Software Scaling (Zen 4, AVX2 vs. AVX-512)

| Scene | 1280×1024 AVX2 | 1280×1024 AVX-512 | Delta | 1600×1200 AVX2 | 1600×1200 AVX-512 | Delta |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `fillrate` | 141.0 | **180.0** | +27.6% | 94.1 | **125.2** | +33.0% |
| `fog` | 137.4 | **169.4** | +23.2% | 95.2 | **119.4** | +25.4% |
| `multitex` | 142.1 | **185.5** | +30.6% | 98.5 | **130.1** | +32.0% |
| `stencil` | 158.0 | **212.0** | +34.2% | 108.8 | **147.8** | +35.9% |
| `ext_combine` | 83.6 | **124.3** | +48.7% | 57.9 | **87.2** | +50.4% |
| `tbuffer` (4× AA) | 90.6 | **121.5** | +34.1% | 60.8 | **81.4** | +33.9% |
| **All-14 Composite GM** | **353.3** | **436.5** | **+23.6%** | **251.7** | **315.1** | **+25.2%** |

---

## 4. Choosing the Right Backend

- **Vulkan *(recommended default)***: Fastest overall on discrete and integrated GPUs — excels at high resolutions (≥1024×768), 4× Spatial AA / T-Buffer (`tbuffer`), hardware BC1–BC3 (`texfmt`), zero-readback GPU render-to-texture (`rtt`), Early-Z depth rejection, and micro-batch state changes (`state`).
- **OpenGL ES 3.2 / Win32 WGL OpenGL 3.3 Core**: Strong fallback on older GPUs or Wine environments without a 32-bit Vulkan ICD, with native S3TC/BCn upload and GPU-side RTT blit.
- **Software (`avx2` default, `avx512` on AVX-512 CPUs)**: Ideal when no hardware GPU driver is available, when debugging rendering behavior against a bit-exact reference oracle, or for 2D/menu-heavy titles dominated by synchronous CPU LFB readbacks (`lfb_read`), where avoiding GPU-to-CPU PCIe stalls yields several times higher throughput.
