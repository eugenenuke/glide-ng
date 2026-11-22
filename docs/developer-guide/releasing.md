# Versioning & Releasing

`glide-ng` follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html) with a single-source version file at the repository root.

---

## 1. Version Scheme

- **`0.y.z`** — Pre-`1.0.0` releases (e.g., `0.9.0`; the `0.x` major version indicates active pre-1.0 development).
- **`0.y.z-beta.N` / `0.y.z-rc.N`** — Optional pre-release previews or release candidates ahead of a milestone or `1.0.0`.
- **`X.Y.Z`** — Stable releases (`1.0.0` onward).

### Single Source of Truth (`VERSION`)

The root [`VERSION`](../../VERSION) file contains the canonical bare SemVer string (e.g., `0.9.0`). Every artifact derives from it automatically:

| Consumer | How Version Is Propagated |
| :--- | :--- |
| **CMake** (`CMakeLists.txt`) | Reads `VERSION` (registered in `CMAKE_CONFIGURE_DEPENDS`), parses `MAJOR.MINOR.PATCH` and optional `-PRERELEASE`, and sets `project(glide-ng VERSION ...)`. |
| **C++ Core & Logs** (`tools/gen_version_header.cmake` → `build_version.h`) | Emits `GLIDE_VERSION_MAJOR`, `GLIDE_VERSION_MINOR`, `GLIDE_VERSION_PATCH`, `GLIDE_VERSION_PRERELEASE`, `GLIDE_VERSION_BASE`, and tag-aware `GLIDE_VERSION_STRING` (`WRAPPER_VERSION` in `WrapperConfig.h`). |
| **Win32 PE DLL Metadata** (`src/glide211/glide.rc.in`, `src/glide2/glide2x.rc.in`, `src/glide3/glide3x.rc.in`) | Configures Win32 `VS_VERSION_INFO` resources (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`) compiled into `glide.dll`, `glide2x.dll`, and `glide3x.dll` by `windres`. |
| **Automated Verification** (`tests/glide3/test_glide3_catalog.cpp`) | `[Version]` asserts that `VERSION`, `GLIDE_VERSION_BASE`, `WRAPPER_VERSION`, and numeric macros match. |

### Tag-Aware Build Strings (`GLIDE_VERSION_STRING`)

- **Clean release build on exact tag `v<VERSION>`**: `0.9.0`
- **Development build between tags**: `0.9.0+g<short-hash>` (plus `-dirty` when tracked files have uncommitted changes)
- **Source tarball without `.git`**: `0.9.0`

---

## 2. Cutting a Release (`scripts/bump_version.sh`)

`scripts/bump_version.sh` validates SemVer syntax, updates `VERSION`, stamps a dated heading below `## [Unreleased]` in `CHANGELOG.md`, and prints the exact verification and tagging commands without committing or pushing automatically.

```bash
# Preview a version bump
./scripts/bump_version.sh 0.9.1 --dry-run

# Execute bump
./scripts/bump_version.sh 0.9.1
```

1. Ensure `main` is clean and all planned branches are merged and verified.
2. Run `./scripts/bump_version.sh <X.Y.Z>` and polish the release section in [`CHANGELOG.md`](../../CHANGELOG.md).
3. Run a clean zero-warning Release build (`-Wdev`) and verify all tests:
   ```bash
   cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -Wdev
   cmake --build build_release -j$(nproc)
   GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build_release --output-on-failure -j$(nproc)
   ```
4. Commit, create an annotated tag, and push:
   ```bash
   git add VERSION CHANGELOG.md
   git commit -m "chore(release): bump version to v0.9.1"
   git tag -a "v0.9.1" -m "glide-ng v0.9.1"
   git push origin main "v0.9.1"
   ```

---

## 3. Release Archives & Automated CI Publishing

Pushing any `v*` tag triggers [`.github/workflows/release.yml`](../../.github/workflows/release.yml) automatically (free on public GitHub repositories), building and verifying headless tests across:

- **Ubuntu 22.04 LTS** (`glibc 2.35` baseline) + **32-bit Win32** (`i686-w64-mingw32-g++-posix`) + **DOS/32** (`Open Watcom v2`)
- **Ubuntu 24.04 LTS** (`glibc 2.39`)
- **Fedora 41**
- **Arch Linux / CachyOS** (rolling)

The workflow automatically builds the GitHub Release description via [`scripts/generate_release_notes.py`](../../scripts/generate_release_notes.py) from the matching `## [<ver>]` entry in [`CHANGELOG.md`](../../CHANGELOG.md), archive sizes and `GLIBC` floors inside `dist/`, and `SHA256SUMS.txt` (marking pre-release tags containing `-` automatically with `--prerelease`).

Locally, [`scripts/package_release.sh`](../../scripts/package_release.sh) runs the exact same packaging logic and emits stripped binaries (via `strip --strip-unneeded` / `i686-w64-mingw32-strip --strip-unneeded`) into `dist/` (git-ignored):

| Archive | Contents |
| :--- | :--- |
| **`glide-ng-<ver>-runtime.zip`** | Stripped `win32/glide*.dll`, `dos/GLIDE2X.OVL`, `linux-x86_64-<distro>/libglide*.so` (with SONAME symlinks and `BUILD_INFO.txt`), `LICENSE`, `THIRD_PARTY_NOTICES.md`, `glide-ng.ini.example`, and `README.txt`. |
| **`glide-ng-<ver>-runtime.tar.xz`** | Solid-compressed copy of the same runtime layout (roughly 60% smaller thanks to cross-library deduplication; natively preserves Linux symlinks and execute bits). |
| **`glide-ng-<ver>-tools.zip`** | Stripped `glide_info`, `glide_bench`, `glide_envtest` across Win32, DOS/32, and Linux, plus Linux `glide{2,3}_cube`, `glide{2,3}_gears`, and showcase `demos/`. |
| **`glide-ng-<ver>-debug-symbols.zip`** | Optional detached `*.debug` symbol files (`objcopy --only-keep-debug`) linked to the stripped binaries for `gdb` / `addr2line` backtraces. |
| **`SHA256SUMS.txt`** | SHA-256 checksums covering every archive file. |

```bash
# Package local build_release/ directly into dist/
./scripts/package_release.sh

# When building a local portable Linux release, enable static C++ runtime:
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DGLIDE_STATIC_CPP_RUNTIME=ON
cmake --build build_release -j$(nproc)
./scripts/package_release.sh
```

> By default (`-DGLIDE_STATIC_CPP_RUNTIME=OFF`), building from source keeps all system libraries dynamically linked. Production Linux `libglide*.so` libraries hide all internal C++ symbols behind anonymous ELF version scripts (`src/glide*/libglide*.ver`) and link with `-Wl,--gc-sections`.
