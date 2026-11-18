#!/usr/bin/env bash
set -euo pipefail

# Bump glide-ng semantic version in root VERSION and stamp CHANGELOG.md.
# Never commits or pushes automatically.

usage() {
  cat <<'EOF'
Usage: scripts/bump_version.sh <X.Y.Z[-prerelease]> [--dry-run]

Examples:
  scripts/bump_version.sh 0.8.0-beta.2
  scripts/bump_version.sh 0.9.0-rc.1
  scripts/bump_version.sh 1.0.0 --dry-run
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage >&2
  exit 1
fi

NEW_VERSION="$1"
DRY_RUN=0
if [[ $# -eq 2 ]]; then
  if [[ "$2" == "--dry-run" ]]; then
    DRY_RUN=1
  else
    usage >&2
    exit 1
  fi
fi

SEMVER_RE='^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$'
if [[ ! "$NEW_VERSION" =~ $SEMVER_RE ]]; then
  echo "Error: '$NEW_VERSION' is not a valid SemVer 2.0 version (expected X.Y.Z or X.Y.Z-prerelease)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION_FILE="$REPO_ROOT/VERSION"
CHANGELOG_FILE="$REPO_ROOT/CHANGELOG.md"

CURRENT_VERSION="(none)"
if [[ -f "$VERSION_FILE" ]]; then
  CURRENT_VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
fi

if [[ "$CURRENT_VERSION" == "$NEW_VERSION" ]]; then
  echo "Error: VERSION is already '$NEW_VERSION'." >&2
  exit 1
fi

TODAY="$(date -u +%Y-%m-%d)"

echo "Bumping glide-ng version: $CURRENT_VERSION -> $NEW_VERSION ($TODAY)"

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "[dry-run] Would write '$NEW_VERSION' to $VERSION_FILE"
  if [[ -f "$CHANGELOG_FILE" ]]; then
    echo "[dry-run] Would stamp '## [$NEW_VERSION] - $TODAY' below '## [Unreleased]' in $CHANGELOG_FILE"
  fi
  exit 0
fi

printf '%s\n' "$NEW_VERSION" > "$VERSION_FILE"
echo "Updated $VERSION_FILE"

if [[ -f "$CHANGELOG_FILE" ]]; then
  if grep -q "^## \[$NEW_VERSION\]" "$CHANGELOG_FILE"; then
    echo "CHANGELOG.md already has an entry for [$NEW_VERSION]; leaving CHANGELOG.md unchanged."
  elif grep -q "^## \[Unreleased\]" "$CHANGELOG_FILE"; then
    python3 - "$CHANGELOG_FILE" "$NEW_VERSION" "$TODAY" <<'PYEOF'
import sys

path, ver, today = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8") as f:
    text = f.read()

marker = "## [Unreleased]\n"
replacement = f"## [Unreleased]\n\n## [{ver}] - {today}\n"
updated = text.replace(marker, replacement, 1)
with open(path, "w", encoding="utf-8") as f:
    f.write(updated)
PYEOF
    echo "Stamped '## [$NEW_VERSION] - $TODAY' in $CHANGELOG_FILE"
  else
    echo "Warning: '## [Unreleased]' heading not found in $CHANGELOG_FILE; update it manually."
  fi
fi

cat <<EOF

Next steps:
  1. Review changes and polish release notes in CHANGELOG.md:
       git diff VERSION CHANGELOG.md
  2. Build and run headless tests:
       cmake --build build -j\$(nproc)
       GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ctest --test-dir build --output-on-failure
  3. Commit and create an annotated tag:
       git add VERSION CHANGELOG.md
       git commit -m "chore(release): bump version to v${NEW_VERSION}"
       git tag -a "v${NEW_VERSION}" -m "glide-ng v${NEW_VERSION}"
  4. Push the branch and tag when ready:
       git push origin main "v${NEW_VERSION}"
EOF
