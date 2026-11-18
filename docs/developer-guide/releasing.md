# Versioning & Releasing

`glide-ng` follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html) with a single-source version file at the repository root.

---

## 1. Version Scheme

- **`0.y.z-beta.N`** — Public beta milestones prior to `1.0.0` (e.g., `0.8.0-beta.1`).
- **`0.y.z-rc.N`** — Release candidates stabilizing a milestone or `1.0.0`.
- **`X.Y.Z`** — Stable releases (`1.0.0` onward).

### Single Source of Truth (`VERSION`)

The root [`VERSION`](../../VERSION) file contains the canonical bare SemVer string (e.g., `0.8.0-beta.1`). Every artifact derives from it automatically:

| Consumer | How Version Is Propagated |
| :--- | :--- |
| **CMake** (`CMakeLists.txt`) | Reads `VERSION` (registered in `CMAKE_CONFIGURE_DEPENDS`), parses `MAJOR.MINOR.PATCH` and optional `-PRERELEASE`, and sets `project(glide-ng VERSION ...)`. |
| **C++ Core & Logs** (`tools/gen_version_header.cmake` → `build_version.h`) | Emits `GLIDE_VERSION_MAJOR`, `GLIDE_VERSION_MINOR`, `GLIDE_VERSION_PATCH`, `GLIDE_VERSION_PRERELEASE`, `GLIDE_VERSION_BASE`, and tag-aware `GLIDE_VERSION_STRING` (`WRAPPER_VERSION` in `WrapperConfig.h`). |
| **Win32 PE DLL Metadata** (`src/glide2/glide2x.rc.in`, `src/glide3/glide3x.rc.in`) | Configures Win32 `VS_VERSION_INFO` resources (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`) compiled into `glide2x.dll` and `glide3x.dll` by `windres`. |
| **Automated Verification** (`tests/test_glide3.cpp`) | `[M13][Version]` asserts that `VERSION`, `GLIDE_VERSION_BASE`, `WRAPPER_VERSION`, and numeric macros match. |

### Tag-Aware Build Strings (`GLIDE_VERSION_STRING`)

- **Clean release build on exact tag `v<VERSION>`**: `0.8.0-beta.1`
- **Development build between tags**: `0.8.0-beta.1+g<short-hash>` (plus `-dirty` when tracked files have uncommitted changes)
- **Source tarball without `.git`**: `0.8.0-beta.1`

---

## 2. Release Workflow (`scripts/bump_version.sh`)

`scripts/bump_version.sh` validates SemVer syntax, updates `VERSION`, stamps a dated heading below `## [Unreleased]` in `CHANGELOG.md`, and prints the exact verification and tagging commands without committing or pushing automatically.

### Preview a Bump (Dry Run)

```bash
./scripts/bump_version.sh 0.8.0-beta.2 --dry-run
```

### Cut a Release

1. Ensure `main` is clean and all milestone work is merged and verified.
2. Run the version bump script:
   ```bash
   ./scripts/bump_version.sh 0.8.0-beta.2
   ```
3. Polish the new release section in [`CHANGELOG.md`](../../CHANGELOG.md) and review `git diff VERSION CHANGELOG.md`.
4. Build Release (Linux + Win32) and run the headless Catch2 suite:
   ```bash
   cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
   cmake --build build_release -j$(nproc)
   GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build_release --output-on-failure
   ```
5. Commit and create an annotated git tag:
   ```bash
   git add VERSION CHANGELOG.md
   git commit -m "chore(release): bump version to v0.8.0-beta.2"
   git tag -a "v0.8.0-beta.2" -m "glide-ng v0.8.0-beta.2"
   ```
6. Push `main` and the tag:
   ```bash
   git push origin main "v0.8.0-beta.2"
   ```
