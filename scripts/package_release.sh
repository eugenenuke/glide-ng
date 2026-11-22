#!/usr/bin/env bash
set -euo pipefail

# Package stripped, multi-platform glide-ng release archives:
#   - glide-ng-<version>-runtime.zip      (Win32 DLLs, DOS GLIDE2X.OVL, Linux x86_64 .so flavor(s))
#   - glide-ng-<version>-runtime.tar.xz   (solid-compressed runtime bundle, preserving Linux symlinks)
#   - glide-ng-<version>-tools.zip        (Win32, DOS, and Linux tools & showcase demos)
#   - glide-ng-<version>-debug-symbols.zip (detached .debug symbol files for backtraces / gdb)
#   - SHA256SUMS.txt
#
# Supports combining multiple Linux distro flavors passed via --linux-flavor
# <label>:<lib_dir>[:<bin_dir>] (used by GitHub Actions matrix and Docker builds).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION=""
WIN32_BIN_DIR=""
DOS_DIR=""
BUILD_DIR="${REPO_ROOT}/build_release"
OUT_DIR="${REPO_ROOT}/dist"
INCLUDE_DEMOS=1
KEEP_HOST_LINUX=1
LINUX_FLAVORS=()

usage() {
  cat <<'EOF'
Usage: scripts/package_release.sh [options]

Options:
  --version <ver>           Release version string (default: read from root VERSION)
  --build-dir <dir>         Primary CMake build directory (default: build_release)
  --win32-bin <dir>         Override Win32 DLL/tool bin dir (default: <build-dir>/win32/bin)
  --dos-dir <dir>           Override DOS/32 OVL/tool dir (default: <build-dir>/dos)
  --linux-flavor <spec>     Add Linux x86_64 flavor as <label>:<lib_dir>[:<bin_dir>]
                            (can be specified multiple times; disables auto host pickup)
  --host-label <label>      Label when packaging the local host <build-dir> Linux build
  --out-dir <dir>           Output archive directory (default: dist)
  --no-demos                Exclude Linux showcase demos from tools archive
  -h, --help                Show this help text

Examples:
  # Package from local build_release/
  scripts/package_release.sh

  # Package multi-distro release assembled from CI or Docker build artifacts
  scripts/package_release.sh \
    --version 0.9.0 \
    --win32-bin artifacts/win32/bin \
    --dos-dir artifacts/dos \
    --linux-flavor ubuntu-22.04:artifacts/ubuntu/lib:artifacts/ubuntu/bin \
    --linux-flavor fedora:artifacts/fedora/lib:artifacts/fedora/bin \
    --linux-flavor arch:artifacts/arch/lib:artifacts/arch/bin
EOF
}

HOST_LABEL=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --win32-bin) WIN32_BIN_DIR="$2"; shift 2 ;;
    --dos-dir) DOS_DIR="$2"; shift 2 ;;
    --linux-flavor)
      LINUX_FLAVORS+=("$2")
      KEEP_HOST_LINUX=0
      shift 2
      ;;
    --host-label) HOST_LABEL="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --no-demos) INCLUDE_DEMOS=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "${VERSION}" ]]; then
  VERSION="$(tr -d '[:space:]' < "${REPO_ROOT}/VERSION")"
fi
if [[ -z "${WIN32_BIN_DIR}" ]]; then
  WIN32_BIN_DIR="${BUILD_DIR}/win32/bin"
fi
if [[ -z "${DOS_DIR}" ]]; then
  DOS_DIR="${BUILD_DIR}/dos"
fi

detect_host_label() {
  local id="linux" ver=""
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release || true
    id="${ID:-linux}"
    ver="${VERSION_ID:-}"
  fi
  local glibc_ver=""
  glibc_ver="$(ldd --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
  if [[ -n "${ver}" ]]; then
    echo "${id}-${ver}"
  elif [[ -n "${glibc_ver}" ]]; then
    echo "${id}-glibc${glibc_ver}"
  else
    echo "${id}"
  fi
}

if [[ "${KEEP_HOST_LINUX}" -eq 1 && -d "${BUILD_DIR}/lib" ]]; then
  if [[ -z "${HOST_LABEL}" ]]; then
    HOST_LABEL="$(detect_host_label)"
  fi
  LINUX_FLAVORS+=("${HOST_LABEL}:${BUILD_DIR}/lib:${BUILD_DIR}/bin")
fi

strip_elf_with_debug() {
  local src="$1" dst_bin="$2" dst_dbg="$3" mode="$4"
  cp -L "${src}" "${dst_bin}"
  chmod 0755 "${dst_bin}"
  if command -v objcopy >/dev/null 2>&1; then
    if objcopy --only-keep-debug "${dst_bin}" "${dst_dbg}" 2>/dev/null; then
      chmod 0644 "${dst_dbg}"
      strip "${mode}" "${dst_bin}"
      objcopy --remove-section=.gnu_debuglink "${dst_bin}" 2>/dev/null || true
      (cd "$(dirname "${dst_dbg}")" && objcopy --add-gnu-debuglink="$(basename "${dst_dbg}")" "${dst_bin}" 2>/dev/null || true)
      return 0
    fi
  fi
  strip "${mode}" "${dst_bin}"
}

strip_pe_with_debug() {
  local src="$1" dst_bin="$2" dst_dbg="$3" mode="$4"
  local pe_strip="i686-w64-mingw32-strip"
  local pe_objcopy="i686-w64-mingw32-objcopy"
  cp -L "${src}" "${dst_bin}"
  chmod 0755 "${dst_bin}"
  if command -v "${pe_objcopy}" >/dev/null 2>&1 && command -v "${pe_strip}" >/dev/null 2>&1; then
    if "${pe_objcopy}" --only-keep-debug "${dst_bin}" "${dst_dbg}" 2>/dev/null; then
      chmod 0644 "${dst_dbg}"
    fi
    "${pe_strip}" "${mode}" "${dst_bin}"
  elif command -v "${pe_strip}" >/dev/null 2>&1; then
    "${pe_strip}" "${mode}" "${dst_bin}"
  fi
}

max_glibc_floor() {
  local so="$1"
  objdump -T "${so}" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -n1 || echo "unknown"
}

max_glibcxx_floor() {
  local so="$1"
  local res
  res="$(objdump -T "${so}" 2>/dev/null | grep -oE 'GLIBCXX_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -uV | tail -n1 || true)"
  if [[ -z "${res}" ]]; then
    echo "static (embedded libstdc++)"
  else
    echo "${res}"
  fi
}

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

RT_DIR="${STAGE}/runtime/glide-ng-${VERSION}-runtime"
TOOLS_DIR="${STAGE}/tools/glide-ng-${VERSION}-tools"
DBG_DIR="${STAGE}/debug/glide-ng-${VERSION}-debug-symbols"
mkdir -p "${RT_DIR}" "${TOOLS_DIR}" "${DBG_DIR}" "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

cp "${REPO_ROOT}/LICENSE" "${REPO_ROOT}/THIRD_PARTY_NOTICES.md" "${REPO_ROOT}/glide-ng.ini.example" "${RT_DIR}/"
cp "${REPO_ROOT}/LICENSE" "${REPO_ROOT}/THIRD_PARTY_NOTICES.md" "${TOOLS_DIR}/"
cp "${REPO_ROOT}/LICENSE" "${REPO_ROOT}/THIRD_PARTY_NOTICES.md" "${DBG_DIR}/"

# ---- 1. Win32 (32-bit i686 DLLs & tools) ----
if [[ -d "${WIN32_BIN_DIR}" ]]; then
  mkdir -p "${RT_DIR}/win32" "${DBG_DIR}/win32"
  for dll in glide.dll glide2x.dll glide3x.dll; do
    if [[ -f "${WIN32_BIN_DIR}/${dll}" ]]; then
      strip_pe_with_debug "${WIN32_BIN_DIR}/${dll}" "${RT_DIR}/win32/${dll}" "${DBG_DIR}/win32/${dll}.debug" --strip-unneeded
    fi
  done
  mkdir -p "${TOOLS_DIR}/win32"
  for exe in glide2_info.exe glide3_info.exe glide2_bench.exe glide3_bench.exe glide2_envtest.exe glide3_envtest.exe; do
    if [[ -f "${WIN32_BIN_DIR}/${exe}" ]]; then
      strip_pe_with_debug "${WIN32_BIN_DIR}/${exe}" "${TOOLS_DIR}/win32/${exe}" "${DBG_DIR}/win32/${exe}.debug" --strip-all
    fi
  done
fi

# ---- 2. DOS/32 (GLIDE2X.OVL & CauseWay tools) ----
if [[ -d "${DOS_DIR}" ]]; then
  if [[ -f "${DOS_DIR}/glide2x.ovl" ]]; then
    mkdir -p "${RT_DIR}/dos"
    cp "${DOS_DIR}/glide2x.ovl" "${RT_DIR}/dos/GLIDE2X.OVL"
  elif [[ -f "${DOS_DIR}/GLIDE2X.OVL" ]]; then
    mkdir -p "${RT_DIR}/dos"
    cp "${DOS_DIR}/GLIDE2X.OVL" "${RT_DIR}/dos/GLIDE2X.OVL"
  fi
  mkdir -p "${TOOLS_DIR}/dos"
  for exe in glide_info.exe glide_bench.exe glide_envtest.exe; do
    if [[ -f "${DOS_DIR}/${exe}" ]]; then
      cp "${DOS_DIR}/${exe}" "${TOOLS_DIR}/dos/${exe}"
    fi
  done
fi

# ---- 3. Linux x86_64 shared library & tool flavors ----
FLAVOR_SUMMARY=""
LINUX_CORE_TOOLS=(
  glide2_info glide3_info
  glide2_bench glide3_bench
  glide2_envtest glide3_envtest
  glide2_cube glide3_cube
  glide2_gears glide3_gears
)
LINUX_DEMOS=(
  chromakey_range_demo fog_showcase_demo gamma_showcase_demo
  glide2_compat_demo glide3_extensions_demo glide3_state_demo
  Glide3xShowcaseDemo large_texture_showcase
  perspective_mipmapping_showcase phase1_validation_demo_g2
  phase1_validation_demo_g3 phase2_validation_demo
  splash_showcase_demo test10_replica_demo test12_replica_demo
)

for spec in "${LINUX_FLAVORS[@]}"; do
  IFS=':' read -r label lib_dir bin_dir <<< "${spec}"
  [[ -d "${lib_dir}" ]] || { echo "Missing Linux lib dir for flavor '${label}': ${lib_dir}" >&2; exit 1; }

  rt_sub="${RT_DIR}/linux-x86_64-${label}"
  dbg_sub="${DBG_DIR}/linux-x86_64-${label}"
  mkdir -p "${rt_sub}" "${dbg_sub}"

  lib_specs=(
    "libglide.so.2.11.0:libglide.so.0:libglide.so"
    "libglide2x.so.2.0.0:libglide2x.so.0:libglide2x.so"
    "libglide3x.so.3.0.0:libglide3x.so.0:libglide3x.so"
  )
  floor_glibc="unknown"
  floor_cxx="unknown"
  for ls_entry in "${lib_specs[@]}"; do
    IFS=':' read -r real_so soname_so link_so <<< "${ls_entry}"
    src_so=""
    if [[ -f "${lib_dir}/${real_so}" ]]; then
      src_so="${lib_dir}/${real_so}"
    elif [[ -f "${lib_dir}/${link_so}" ]]; then
      src_so="${lib_dir}/${link_so}"
    fi
    if [[ -n "${src_so}" ]]; then
      strip_elf_with_debug "${src_so}" "${rt_sub}/${real_so}" "${dbg_sub}/${real_so}.debug" --strip-unneeded
      ln -sf "${real_so}" "${rt_sub}/${soname_so}"
      ln -sf "${soname_so}" "${rt_sub}/${link_so}"
      if [[ "${link_so}" == "libglide2x.so" ]]; then
        floor_glibc="$(max_glibc_floor "${rt_sub}/${real_so}")"
        floor_cxx="$(max_glibcxx_floor "${rt_sub}/${real_so}")"
      fi
    fi
  done

  if [[ -f "${lib_dir}/BUILD_INFO.txt" ]]; then
    cp "${lib_dir}/BUILD_INFO.txt" "${rt_sub}/BUILD_INFO.txt"
  else
    {
      echo "glide-ng version : ${VERSION}"
      echo "Flavor           : linux-x86_64-${label}"
      echo "Max GLIBC symbol : ${floor_glibc}"
      echo "libstdc++ mode   : ${floor_cxx}"
      echo "Dynamic exports  :"
      for name in libglide.so.2.11.0 libglide2x.so.2.0.0 libglide3x.so.3.0.0; do
        if [[ -f "${rt_sub}/${name}" ]]; then
          printf "  %-22s %s exported symbols (%s)\n" \
            "${name}" \
            "$(nm -D --defined-only "${rt_sub}/${name}" | wc -l)" \
            "$(du -h "${rt_sub}/${name}" | awk '{print $1}')"
        fi
      done
      echo ""
      echo "Dynamic library dependencies (libglide2x.so):"
      readelf -d "${rt_sub}/libglide2x.so.2.0.0" 2>/dev/null | grep '(NEEDED)' || true
    } > "${rt_sub}/BUILD_INFO.txt"
  fi
  FLAVOR_SUMMARY+="- linux-x86_64-${label}/  (GLIBC floor: ${floor_glibc}, libstdc++: ${floor_cxx})"$'\n'

  if [[ -n "${bin_dir:-}" && -d "${bin_dir}" ]]; then
    tool_sub="${TOOLS_DIR}/linux-x86_64-${label}"
    mkdir -p "${tool_sub}"
    for t in "${LINUX_CORE_TOOLS[@]}"; do
      if [[ -f "${bin_dir}/${t}" ]]; then
        strip_elf_with_debug "${bin_dir}/${t}" "${tool_sub}/${t}" "${dbg_sub}/${t}.debug" --strip-all
      fi
    done
    if [[ "${INCLUDE_DEMOS}" -eq 1 ]]; then
      mkdir -p "${tool_sub}/demos"
      for d in "${LINUX_DEMOS[@]}"; do
        if [[ -f "${bin_dir}/${d}" ]]; then
          strip_elf_with_debug "${bin_dir}/${d}" "${tool_sub}/demos/${d}" "${dbg_sub}/${d}.debug" --strip-all
        fi
      done
    fi
  fi
done

# ---- 4. User-friendly README.txt inside each archive ----
cat > "${RT_DIR}/README.txt" <<EOF
glide-ng ${VERSION} — Pre-Built Runtime Libraries
License: GNU GPLv3 (GPL-3.0-or-later) — see LICENSE & THIRD_PARTY_NOTICES.md

CONTENTS
--------
- win32/      32-bit Windows / Wine / Steam Proton drop-in DLLs (ZERO external DLL deps):
                glide.dll    (Glide 2.11, pre-2.3 SST-1 titles)
                glide2x.dll  (Glide 2.x)
                glide3x.dll  (Glide 3.x / VSA-100)
- dos/        Clean-room GPL-3.0 DOS/32 pass-through overlay for DOSBox-SVN / DOSBox-X:
                GLIDE2X.OVL  (place beside the game's DOS executable or in DOS PATH)
- linux-x86_64-*/   64-bit Linux shared libraries (for DOSBox pass-through & native apps):
${FLAVOR_SUMMARY}
- glide-ng.ini.example   Template configuration file (copy as glide-ng.ini next to app)

WHICH LINUX FOLDER SHOULD I PICK?
---------------------------------
  - Ubuntu / Debian / Linux Mint / Pop!_OS -> linux-x86_64-ubuntu-22.04/ (or ubuntu-24.04/)
  - Fedora / Nobara / RHEL / Bazzite       -> linux-x86_64-fedora-41/
  - Arch / CachyOS / EndeavourOS / Manjaro -> linux-x86_64-arch/
  (Pick the directory matching your OS or whose BUILD_INFO.txt GLIBC floor is <= your
   system's \`ldd --version\`.)

Note for .zip on Linux: Always unzip using \`unzip\` (preserves SONAME symlinks), or extract
the companion \`glide-ng-${VERSION}-runtime.tar.xz\` archive with \`tar -xJf\`.

MINIMAL LINUX END-USER RUNTIME PACKAGES (no compilers / -dev packages required)
-------------------------------------------------------------------------------
  Debian / Ubuntu / Mint:
    sudo apt-get install libvulkan1 libsdl2-2.0-0 libegl1 libgles2 libgomp1 \\
      libx11-6 libxext6 libwayland-client0 libwayland-egl1

  Arch / CachyOS / EndeavourOS / Manjaro:
    sudo pacman -S vulkan-icd-loader sdl2 mesa gcc-libs libx11 libxext wayland

  Fedora / Nobara:
    sudo dnf install vulkan-loader SDL2 mesa-libEGL mesa-libGLES libgomp \\
      libX11 libXext wayland-libs-client

  (For 32-bit Win32 games under Wine/Proton, also install a 32-bit Vulkan ICD, e.g.
   \`mesa-vulkan-drivers:i386\` / \`lib32-vulkan-radeon\` / \`lib32-nvidia-utils\`.)

QUICK START
-----------
  Windows / Wine / Proton:
    Copy win32/glide*.dll next to game.exe. Under Wine/Proton set:
      WINEDLLOVERRIDES="glide,glide2x,glide3x=n"

  DOSBox-X / DOSBox-SVN (Linux):
    1. Copy dos/GLIDE2X.OVL inside your DOS game directory (replacing legacy overlay).
    2. Launch with:
         LD_LIBRARY_PATH="/path/to/linux-x86_64-<distro>:\${LD_LIBRARY_PATH:-}" \\
           dosbox-x -conf dosbox.conf

Full documentation & per-game catalog presets:
  https://github.com/eugenenuke/glide-ng-alpha/tree/main/docs
EOF

cat > "${TOOLS_DIR}/README.txt" <<EOF
glide-ng ${VERSION} — Diagnostic, Benchmark & Showcase Tools
License: GNU GPLv3 (GPL-3.0-or-later)

CONTENTS
--------
- win32/            glide2_info.exe, glide3_info.exe, glide2_bench.exe, glide3_bench.exe,
                    glide2_envtest.exe, glide3_envtest.exe
                    (Place next to win32/glide2x.dll & glide3x.dll from the runtime archive.)
- dos/              glide_info.exe, glide_bench.exe, glide_envtest.exe
                    (CauseWay DOS/32; pair with dos/GLIDE2X.OVL inside DOSBox.)
- linux-x86_64-*/   Native Linux tools + glide2_cube / glide3_cube / glide2_gears /
                    glide3_gears (+ demos/ interactive showcases).
                    Launch with LD_LIBRARY_PATH pointing at the matching runtime folder:
                      LD_LIBRARY_PATH=/path/to/linux-x86_64-<distro> ./glide2_bench
EOF

cat > "${DBG_DIR}/README.txt" <<EOF
glide-ng ${VERSION} — Detached Debug Symbols
License: GNU GPLv3 (GPL-3.0-or-later)

Optional symbol archive for crash investigation and backtraces. Not needed to run games.

Usage (Linux / gdb):
  Place the matching *.debug file next to its stripped .so (or inside a gdb debug dir):
    gdb -ex "set debug-file-directory /path/to/linux-x86_64-<distro>" ...

Usage (Win32 / MinGW / Wine):
  i686-w64-mingw32-addr2line -e glide2x.dll.debug <offset>
EOF

# ---- 5. Emit archives + SHA256SUMS.txt ----
RT_ZIP="${OUT_DIR}/glide-ng-${VERSION}-runtime.zip"
RT_TXZ="${OUT_DIR}/glide-ng-${VERSION}-runtime.tar.xz"
TOOLS_ZIP="${OUT_DIR}/glide-ng-${VERSION}-tools.zip"
DBG_ZIP="${OUT_DIR}/glide-ng-${VERSION}-debug-symbols.zip"

rm -f "${RT_ZIP}" "${RT_TXZ}" "${TOOLS_ZIP}" "${DBG_ZIP}" "${OUT_DIR}/SHA256SUMS.txt"

(cd "${STAGE}/runtime" && zip -r -y -9 -q "${RT_ZIP}" "glide-ng-${VERSION}-runtime")
(cd "${STAGE}/runtime" && tar -cJf "${RT_TXZ}" "glide-ng-${VERSION}-runtime")
(cd "${STAGE}/tools" && zip -r -y -9 -q "${TOOLS_ZIP}" "glide-ng-${VERSION}-tools")
(cd "${STAGE}/debug" && zip -r -y -9 -q "${DBG_ZIP}" "glide-ng-${VERSION}-debug-symbols")

(cd "${OUT_DIR}" && sha256sum \
  "glide-ng-${VERSION}-runtime.zip" \
  "glide-ng-${VERSION}-runtime.tar.xz" \
  "glide-ng-${VERSION}-tools.zip" \
  "glide-ng-${VERSION}-debug-symbols.zip" \
  > SHA256SUMS.txt)

echo "Packaged release archives for glide-ng v${VERSION} in ${OUT_DIR}/:"
ls -lh "${OUT_DIR}"
echo ""
cat "${OUT_DIR}/SHA256SUMS.txt"
