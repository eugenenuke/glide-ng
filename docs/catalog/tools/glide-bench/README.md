# `glide2_bench` / `glide3_bench` (`glide_bench.exe`)

| Field | Value |
| :--- | :--- |
| **Category** | Benchmark Suite (First-Party Tool) |
| **Platform(s)** | `Linux` · `Win32 / Wine` · `DOS/32 (GLIDE2X.OVL)` |
| **Glide API & Version** | Glide 2.x (`glide2_bench` / `glide_bench.exe`) · Glide 3.x (`glide3_bench`) |
| **Recommended Card Profile** | `Voodoo2` (Glide 2.x) · **`Voodoo5`** (Glide 3.x VSA-100 scenes) |
| **Verified Backends** | Vulkan · OpenGL ES / WGL · Software (`scalar` / `avx2` / `avx512`) · Mesa `llvmpipe` (VK & GLES) |
| **Preset File(s)** | [`glide-ng.ini`](./glide-ng.ini) (Voodoo2 uncapped) · [`glide-ng.voodoo5.ini`](./glide-ng.voodoo5.ini) (Voodoo5 VSA-100 uncapped) |
| **Status** | Verified |

---

## 1. Overview & Registered Scenes

`glide2_bench` and `glide3_bench` form the built-in **GlideMark** benchmark suite for `glide-ng`. They measure real-world Glide rendering bottlenecks across all backends and platforms while keeping the **Core 6-scene GlideMark score** strictly comparable across versions and hardware.

### Registered Scenes

| Scene | Set | API | Workload Description | Primary Unit |
| :--- | :---: | :---: | :--- | :---: |
| `fillrate` | `core` | 2.x & 3.x | 16× full-screen alpha-blended overdraw | MPix/s |
| `tris` | `core` | 2.x & 3.x | 4K-triangle waving geometric mesh | KTri/s |
| `state` | `core` | 2.x & 3.x | 120 pipeline/state-change batches per frame | KDraw/s |
| `tex` | `core` | 2.x & 3.x | 64× `P_8` texture + palette streaming | MB/s |
| `lfb` | `core` | 2.x & 3.x | 3D scene + sparse RGB565 LFB HUD writes | MB/s |
| `multipass` | `core` | 2.x & 3.x | Two-pass `GR_FOG_ADD2`/`MULT2` + mipmaps | KTri/s |
| `fog` | `extended` | 2.x & 3.x | 12× full-screen 3dfx W-buffer Table Fog | MPix/s |
| `multitex` | `extended` | 2.x & 3.x | 10× dual-TMU (`TMU0 * TMU1`) bilinear fill | MPix/s |
| `lfb_read` | `extended` | 2.x & 3.x | Full-frame RGB565 LFB read + write roundtrip | MB/s |
| `stencil` | `vsa100` | **3.x only** | VSA-100 `PIXEXT` 8-bit hardware stencil shadow volumes | MPix/s |
| `ext_combine` | `vsa100` | **3.x only** | VSA-100 `COMBINE` (`gr*CombineExt` + `grAlphaBlendFunctionExt`) | MPix/s |
| `texfmt` | `vsa100` | **3.x only** | VSA-100 `TEXFMT` (`DXT1`/`DXT5` + `FXT1` + `ARGB_8888`) uploads | MB/s |
| `rtt` | `vsa100` | **3.x only** | VSA-100 `TEXTUREBUFFER` live 128×128 render-to-texture | RTT/s |
| `tbuffer` | `vsa100` | **3.x only** | VSA-100 4× Spatial AA + T-Buffer resolve | MPix/s |

### CLI Options
```text
--scene <set|list>   core, extended, vsa100, all, or comma-separated (default: all)
--res <512|640|800|1024|1280|1600>  Resolution (default: 640)
--aa <0|2|4>         VSA-100 Spatial AA samples for all scenes (glide3_bench only, default: 0)
--seconds <N>        Duration per scene in seconds (default: 5)
--frames <N>         Fixed frame count per scene (overrides --seconds)
--loop               Loop continuously until ESC or Q is pressed
--no-hud             Disable pure-Glide textured-quad in-scene HUD
-o <file>            Save human-readable summary report to file
--csv <file>         Export per-scene metrics to CSV file
--list               List registered benchmark scenes and exit
--pause              Wait for keypress before exiting
```

---

## 2. Reproducibility Rules & Binary Locations

* **Always use a Release (`-O3`) build or distribution package** before benchmarking (if building from source, see [Building & Installing](../../../user-guide/building-and-installing.md)).
* **Commands below assume `glide2_bench` / `glide3_bench` are installed on `PATH`** (if running from a local source build, prefix with `<build>/bin/`, `<build>/win32/bin/`, or `<build>/dos/`).
* **Uncap frame pacing and disable splash**: Both presets set `vsync = false`, `max_fps = 0`, `swap_interval = 0`, and `no_splash = true`. Otherwise results will clamp at 60 FPS!
* **Headless vs. Windowed**:
  * Pass `GLIDE_NG_HEADLESS=1` (with `--no-hud`) for pure rendering throughput unaffected by compositor/VSync/window-manager jitter.
  * Omit `GLIDE_NG_HEADLESS` to include swapchain / WGL / GDI presentation overhead.

---

## 3. Running Benchmarks Across Platforms

### Linux Native
```bash
# Glide 3.x VSA-100 full 14-scene benchmark on Vulkan (using the Voodoo5 preset)
GLIDE_NG_CONFIG=/path/to/glide-ng.voodoo5.ini \
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan \
  glide3_bench --no-hud --seconds 3 --scene all --csv /tmp/g3_vulkan.csv

# Glide 2.x 9-scene benchmark on OpenGL ES
GLIDE_NG_CONFIG=/path/to/glide-ng.ini \
  GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=opengl_es \
  glide2_bench --no-hud --seconds 3 --scene all --csv /tmp/g2_gles.csv
```

### Win32 Under Wine
```bash
# Uncapped windowed Glide 3.x benchmark under Wine (with glide3x.dll next to glide3_bench.exe)
WINEDLLOVERRIDES="glide3x=n" \
  GLIDE_NG_CONFIG=/path/to/glide-ng.voodoo5.ini \
  GLIDE_NG_BACKEND=vulkan \
  wine ./glide3_bench.exe --no-hud --seconds 3 --scene all
```

### DOS/32 Under DOSBox (`GLIDE2X.OVL`)
Copy `glide_bench.exe` (alongside your `GLIDE2X.OVL`, or with `GLIDE2X.OVL` on DOS `PATH`) into a mounted DOSBox directory and copy [`glide-ng.ini`](./glide-ng.ini) into the DOSBox working directory (or pass `GLIDE_NG_MAX_FPS=0 FX_GLIDE_SWAPINTERVAL=0 GLIDE_NG_NO_SPLASH=1` to DOSBox on the host):
```bat
glide_bench.exe --no-hud --seconds 3 --scene all
```

`glide_bench.exe` cross-checks DOSBox's emulated PIT timer against the host-backed CMOS RTC wall clock around each scene. If synchronous host wrapper stalls cause DOSBox virtual time to drift away from real time, affected scene names are marked with `*` and a footnote reports the estimated real-time FPS.

---

## 4. Benchmarking Software SIMD Tiers (`scalar` vs. `avx2` vs. `avx512`)

`glide-ng` defaults to **`avx2`** (`[software] simd = avx2`) and supports runtime switching between `scalar`, `avx2`, `avx512` (native 4×4 16-pixel rasterizer + 13 AVX-512 CPU kernels for Zen 4 / AVX-512 CPUs), and `auto` (picks highest supported by CPUID + OS `XGETBV`). If a requested tier is unavailable on the host CPU, `glide-ng` logs a `[WARN]` and falls back automatically (`avx512` → `avx2` → `scalar`).

```bash
mkdir -p /tmp/glide_simd_bench
CFG="/path/to/glide-ng.voodoo5.ini"

# 640x480 full comparison: scalar vs avx2 vs avx512
for simd in scalar avx2 avx512; do
  echo "=== Software/$simd (640x480) ==="
  GLIDE_NG_CONFIG="$CFG" GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
    glide3_bench --no-hud --seconds 3 --scene all \
    --csv "/tmp/glide_simd_bench/g3_640_sw_${simd}.csv"
done

# High-resolution pixel-fill scaling (1280x1024 and 1600x1200)
for res in 1280 1600; do
  for simd in avx2 avx512; do
    echo "=== Software/$simd (${res}) ==="
    GLIDE_NG_CONFIG="$CFG" GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=$simd \
      glide3_bench --no-hud --seconds 3 --res "$res" --scene all \
      --csv "/tmp/glide_simd_bench/g3_${res}_sw_${simd}.csv"
  done
done
```

---

## 5. Comparing Mesa `llvmpipe` (Vulkan and OpenGL ES) vs. `glide-ng` Software

Because `glide-ng` automatically selects discrete/integrated GPUs over CPU devices in Vulkan, and NVIDIA's proprietary GLVND EGL driver ignores `LIBGL_ALWAYS_SOFTWARE=1` on its own, you must explicitly point the loader at Mesa's software ICDs to benchmark Mesa **`llvmpipe`** (CPU LLVM JIT rasterizer) apples-to-apples against `glide-ng`'s native **Software** backend.

### Step 1 — Install Mesa Software Drivers (If Not Already Installed)

* **Arch Linux**:
  ```bash
  sudo pacman -S vulkan-swrast mesa
  # Optional for 32-bit Wine benchmarking:
  # sudo pacman -S lib32-vulkan-swrast lib32-mesa
  ```
* **Debian / Ubuntu**:
  ```bash
  sudo apt-get install -y mesa-vulkan-drivers libgl1-mesa-dri
  # Optional for 32-bit Wine benchmarking:
  # sudo apt-get install -y mesa-vulkan-drivers:i386 libgl1-mesa-dri:i386
  ```
* **Fedora**:
  ```bash
  sudo dnf install mesa-vulkan-drivers mesa-dri-drivers
  ```

Verify the ICD manifest paths on your system (the exact filename varies slightly by distro):
```bash
ls /usr/share/vulkan/icd.d/lvp_icd*.json
ls /usr/share/glvnd/egl_vendor.d/*mesa*.json
```
*(Typically `/usr/share/vulkan/icd.d/lvp_icd.json` on Arch, or `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` on Debian/Ubuntu/Fedora; Mesa EGL is `/usr/share/glvnd/egl_vendor.d/50_mesa.json` on all three.)*

### Step 2 — Run All Three CPU Rasterizers Side by Side

```bash
mkdir -p /tmp/glide_llvmpipe_vs_sw
CFG="/path/to/glide-ng.voodoo5.ini"

# Resolve distro-specific Lavapipe ICD path automatically
LVP_ICD=$(ls /usr/share/vulkan/icd.d/lvp_icd.x86_64.json /usr/share/vulkan/icd.d/lvp_icd.json 2>/dev/null | head -n 1)
echo "Using Vulkan Lavapipe ICD: $LVP_ICD"

# 1. glide-ng Native Software Backend (auto picks AVX-512 on Zen 4, AVX2 otherwise)
GLIDE_NG_CONFIG="$CFG" GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software GLIDE_NG_SW_SIMD=auto \
  glide3_bench --no-hud --seconds 3 --scene all \
  --csv /tmp/glide_llvmpipe_vs_sw/sw_auto.csv

# 2. Vulkan via Mesa llvmpipe (lavapipe)
VK_DRIVER_FILES="$LVP_ICD" \
  GLIDE_NG_CONFIG="$CFG" GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=vulkan \
  glide3_bench --no-hud --seconds 3 --scene all \
  --csv /tmp/glide_llvmpipe_vs_sw/vk_llvmpipe.csv

# 3. OpenGL ES via Mesa llvmpipe (REQUIRED on NVIDIA GLVND: force 50_mesa.json + LIBGL_ALWAYS_SOFTWARE=1!)
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  LIBGL_ALWAYS_SOFTWARE=1 \
  GLIDE_NG_CONFIG="$CFG" GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=opengl_es \
  glide3_bench --no-hud --seconds 3 --scene all \
  --csv /tmp/glide_llvmpipe_vs_sw/gles_llvmpipe.csv
```

> **Why `__EGL_VENDOR_LIBRARY_FILENAMES` is mandatory on NVIDIA systems**: On systems with NVIDIA's proprietary driver installed, GLVND loads `10_nvidia.json` ahead of `50_mesa.json`. NVIDIA's EGL implementation ignores `LIBGL_ALWAYS_SOFTWARE=1` and still renders on the RTX/GeForce GPU. Pinning `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json` routes EGL to Mesa, where `LIBGL_ALWAYS_SOFTWARE=1` cleanly activates `llvmpipe`. Confirm in `glide-ng.log` (or with `GLIDE_NG_LOG_CONSOLE=1`) that `GL_RENDERER` reports `llvmpipe` and Vulkan reports `Adapter [0] : llvmpipe`.

---

## 6. Reference Numbers (AMD Ryzen 9 7945HX Zen 4 + RTX 4070 Laptop)

From the [Benchmarking & Performance Guide](../../../user-guide/benchmarking.md) (`glide3_bench`, Voodoo5, 640×480, headless, `--no-hud --seconds 3`):

| Configuration | Core 6 Geomean | Extended 8 Geomean | All-14 Composite GM | Notes |
| :--- | ---: | ---: | ---: | :--- |
| **RTX 4070 Vulkan** | 2,711.2 FPS | 2,884.5 FPS | **2,808.9 FPS** | Hardware BCn (`6,245 FPS` `texfmt`), zero-readback GPU `rtt` (`6,193 RTT/s`) |
| **RTX 4070 OpenGL ES** | 1,828.8 FPS | 2,771.0 FPS | **2,318.9 FPS** | Zero-readback GPU `rtt` (`6,328 RTT/s`) |
| **`glide-ng` Software AVX-512** | 1,660.7 FPS | 1,035.7 FPS | **1,268.0 FPS** | **34,963 GlideMark pts** (+20.0% over AVX2; up to +50.4% at 1600×1200) |
| **`glide-ng` Software AVX2** | 1,477.8 FPS | 821.2 FPS | **1,056.3 FPS** | Default Software SIMD tier |
| **`glide-ng` Software Scalar** | 511.9 FPS | 174.7 FPS | **277.0 FPS** | Bit-exact scalar reference |
| **Vulkan Mesa `llvmpipe`** | — | — | **9,886 GlideMark pts** | `glide-ng` Software AVX-512 is **3.54× faster** than CPU `llvmpipe` Vulkan |

See the [Benchmarking & Performance Guide](../../../user-guide/benchmarking.md) for high-resolution Zen 4 scaling tables and GPU vs. Software architectural trade-offs.
