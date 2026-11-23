---
name: release-manager
description: Step-by-step SemVer release checklist for glide-ng covering VERSION bumps, CHANGELOG.md footer links, generate_release_notes.py dry-runs, local package_release.sh archives, annotated git tags, and the GitHub Actions release workflow.
---

# Workspace Skill: Release Manager (`glide-ng`)

Activate this skill when preparing, bumping, verifying, tagging, or publishing a `glide-ng` release (including pre-release tags such as `vX.Y.Z-rc.1`). Cross-check against [`docs/developer-guide/releasing.md`](../../../docs/developer-guide/releasing.md) and root [`VERSION`](../../../VERSION).

---

## 1. Pre-Flight Checks

1. Confirm a clean `main` branch synced with `origin/main` (`git status -s`, `git log -n 10 --oneline`).
2. Review all commits since the previous tag:
   ```bash
   PREV_TAG=$(git describe --tags --abbrev=0)
   git log "${PREV_TAG}..HEAD" --oneline
   ```
3. Check for doc hygiene (follow [`tech-writer`](../tech-writer/SKILL.md)): no local paths/PII, updated relative links, clean `CHANGELOG.md` entries grouped under Keep-a-Changelog headings (`Added`, `Changed`, `Fixed`, `Removed`). Omit purely internal CI/chore noise unless user-facing.

---

## 2. Bump Version & Polish `CHANGELOG.md`

[`scripts/bump_version.sh`](../../../scripts/bump_version.sh) validates SemVer 2.0, writes root `VERSION`, and stamps `## [<ver>] - YYYY-MM-DD` under `## [Unreleased]` without committing:

```bash
./scripts/bump_version.sh <X.Y.Z> --dry-run
./scripts/bump_version.sh <X.Y.Z>
```

**Manual step (not edited automatically by `bump_version.sh`):** update the comparison links at the bottom of [`CHANGELOG.md`](../../../CHANGELOG.md):
```markdown
[Unreleased]: https://github.com/eugenenuke/glide-ng/compare/v<NEW>...HEAD
[<NEW>]: https://github.com/eugenenuke/glide-ng/compare/v<PREV>...v<NEW>
```

Dry-run the exact GitHub release notes generator with `--strict` to verify the new heading parses cleanly:
```bash
python3 scripts/generate_release_notes.py --version <X.Y.Z> --strict
```

---

## 3. Clean Release Build & Local Verification

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -Wdev
cmake --build build_release -j$(nproc)
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software \
  ctest --test-dir build_release -j4 --output-on-failure
```

Confirm `[Version]` and `[Catalog]` checks pass:
```bash
GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software \
  ./build_release/bin/Glide3xWrapperTests "[Catalog],[Version]"
```

Optional local archive smoke-test (outputs stripped artifacts + `SHA256SUMS.txt` into git-ignored `dist/`):
```bash
./scripts/package_release.sh --version <X.Y.Z>
ls -lh dist/
cat dist/SHA256SUMS.txt
```
*(For portable Linux release builds, configure with `-DGLIDE_STATIC_CPP_RUNTIME=ON`.)*

---

## 4. Commit, Annotated Tag & Push

Never use lightweight tags or push unverified commits. Always create an **annotated** `v*` tag:

```bash
git add VERSION CHANGELOG.md
git commit -m "chore(release): bump version to v<X.Y.Z>"
git tag -a "v<X.Y.Z>" -m "glide-ng v<X.Y.Z>"
git push origin main "v<X.Y.Z>"
```

- Pushing `v<X.Y.Z>` triggers [`.github/workflows/release.yml`](../../../.github/workflows/release.yml) automatically (`5` jobs: Ubuntu 22.04 + Win32 + DOS/32, Ubuntu 24.04, Fedora 41, Arch Linux, and `package-and-release`).
- Versions containing a hyphen (e.g. `0.9.2-rc.1`) are marked `--prerelease` automatically.
- **Artifact storage hygiene:** The 4 distro staging artifacts use `retention-days: 1` (consumed immediately by `package-and-release`), and manual `workflow_dispatch` dry-run bundles (`glide-ng-release-archives`) use `retention-days: 7`. Published GitHub Release assets remain permanent and do not count against Actions artifact storage.

---

## 5. Post-Push Verification

1. Wait for all 5 workflow jobs (`gh run list --limit 3` or GitHub Actions tab) to turn green.
2. Verify the published GitHub Release lists `runtime.zip`, `runtime.tar.xz`, `tools.zip`, `debug-symbols.zip`, and `SHA256SUMS.txt`, and that the rendered changelog matches `generate_release_notes.py --strict`.
3. Never delete or re-tag an already published release tag unless explicitly approved by the maintainer.
