#!/usr/bin/env python3
"""Generate a formatted GitHub Release body from CHANGELOG.md and dist/ archives.

Extracts the ``## [<version>]`` section from CHANGELOG.md, rewrites relative
repository links to tag-pinned GitHub blob URLs so links stay valid on the
GitHub Releases page, and bundles a download guide table with archive sizes,
Linux distro folder selection, runtime package install commands, and optional
embedded SHA-256 verification digests.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple


ARCHIVE_DESCRIPTIONS: List[Tuple[str, str]] = [
    (
        "glide-ng-{ver}-runtime.zip",
        "Drop-in stripped runtime binaries (**Win32 DLLs**, **DOS `GLIDE2X.OVL`**, "
        "and **4 Linux `x86_64` flavors**). Extract on Linux with `unzip` to "
        "preserve SONAME symlinks.",
    ),
    (
        "glide-ng-{ver}-runtime.tar.xz",
        "Same stripped runtime bundle using solid `.tar.xz` deduplication "
        "(recommended smaller download for Linux).",
    ),
    (
        "glide-ng-{ver}-tools.zip",
        "Stripped diagnostic, benchmark, and showcase utilities "
        "(`glide*_info`, `glide*_bench`, `glide*_envtest`, cubes/gears, demos) "
        "for Win32, DOS/32, and Linux.",
    ),
    (
        "glide-ng-{ver}-debug-symbols.zip",
        "Optional detached `*.debug` symbols for `gdb` / `addr2line` backtraces "
        "(not required to play games).",
    ),
    (
        "SHA256SUMS.txt",
        "SHA-256 verification digests for all four archives.",
    ),
]


def format_size(num_bytes: int) -> str:
    if num_bytes < 1024:
        return f"{num_bytes} B"
    if num_bytes < 1024 * 1024:
        return f"{num_bytes / 1024:.1f} KiB"
    return f"{num_bytes / (1024 * 1024):.2f} MiB"


def parse_changelog(
    changelog_text: str, version: str
) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Return (release_date, section_body, compare_url) for `version`."""
    heading_re = re.compile(
        rf"^##\s+\[{re.escape(version)}\](?:\s*-\s*([^\s\r\n]+))?\s*$",
        re.MULTILINE,
    )
    match = heading_re.search(changelog_text)
    if not match:
        return None, None, None

    release_date = match.group(1)
    start = match.end()
    rest = changelog_text[start:]

    # Stop at the next top-level changelog section or link-definition footer.
    stop_re = re.compile(r"^(?:##\s+\[|---\s*$|\[[^\]]+\]:\s*https?://)", re.MULTILINE)
    end_pos = len(rest)
    for m in stop_re.finditer(rest):
        line = m.group(0).strip()
        if line == "---":
            # Stop at horizontal rule only if followed by another section or footer.
            after = rest[m.end() :].lstrip()
            if after.startswith("## [") or re.match(r"^\[[^\]]+\]:\s*https?://", after):
                end_pos = m.start()
                break
        else:
            end_pos = m.start()
            break

    body = rest[:end_pos].strip()

    footer_re = re.compile(
        rf"^\[{re.escape(version)}\]:\s*(\S+)\s*$", re.MULTILINE
    )
    footer_match = footer_re.search(changelog_text)
    compare_url = footer_match.group(1) if footer_match else None

    return release_date, body, compare_url


def rewrite_relative_links(markdown: str, repo: str, tag: str) -> str:
    """Turn relative markdown links [text](path) into tag-pinned GitHub links."""
    base = f"https://github.com/{repo}/blob/{tag}"
    link_re = re.compile(r"(!?\[[^\]]+\]\()([^)\s]+)(\))")

    def _repl(m: re.Match[str]) -> str:
        prefix, target, suffix = m.group(1), m.group(2), m.group(3)
        if (
            target.startswith(("http://", "https://", "mailto:", "#"))
            or target.startswith("/")
        ):
            return m.group(0)
        clean = target
        while clean.startswith("./"):
            clean = clean[2:]
        return f"{prefix}{base}/{clean}{suffix}"

    return link_re.sub(_repl, markdown)


def read_glibc_floors_from_zip(runtime_zip: Path) -> Dict[str, str]:
    """Read 'Max GLIBC symbol' per flavor from BUILD_INFO.txt inside runtime.zip."""
    floors: Dict[str, str] = {}
    if not runtime_zip.is_file():
        return floors
    try:
        with zipfile.ZipFile(runtime_zip, "r") as zf:
            for name in zf.namelist():
                if not name.endswith("/BUILD_INFO.txt"):
                    continue
                parts = name.split("/")
                if len(parts) < 2:
                    continue
                folder = parts[-2]
                content = zf.read(name).decode("utf-8", errors="replace")
                m = re.search(r"^Max GLIBC symbol\s*:\s*(\S+)", content, re.MULTILINE)
                if m and m.group(1) != "unknown":
                    floors[folder] = m.group(1)
    except zipfile.BadZipFile:
        pass
    return floors


def parse_size_overrides(entries: List[str]) -> Dict[str, int]:
    sizes: Dict[str, int] = {}
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"Invalid --asset-size value (expected name=bytes): {entry}")
        name, raw = entry.split("=", 1)
        sizes[name.strip()] = int(raw.strip())
    return sizes


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate GitHub Release notes from CHANGELOG.md."
    )
    parser.add_argument(
        "--version",
        default="",
        help="Release version or tag (e.g. 0.9.0 or v0.9.0; defaults to root VERSION)",
    )
    parser.add_argument(
        "--repo",
        default=os.environ.get("GITHUB_REPOSITORY", "eugenenuke/glide-ng"),
        help="GitHub repository slug (owner/name)",
    )
    parser.add_argument(
        "--changelog",
        default="CHANGELOG.md",
        help="Path to CHANGELOG.md",
    )
    parser.add_argument(
        "--dist-dir",
        default="",
        help="Optional packaged dist/ directory containing archives and SHA256SUMS.txt",
    )
    parser.add_argument(
        "--sha256sums",
        default="",
        help="Optional explicit path to SHA256SUMS.txt (overrides --dist-dir)",
    )
    parser.add_argument(
        "--asset-size",
        action="append",
        default=[],
        help="Optional asset size override as <filename>=<bytes> (repeatable)",
    )
    parser.add_argument(
        "--output",
        default="-",
        help="Output markdown file path (default: stdout)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit non-zero if the matching version heading is missing in CHANGELOG.md",
    )
    args = parser.parse_args()

    version = args.version.strip()
    if not version:
        version_file = Path("VERSION")
        if not version_file.is_file():
            print("error: --version not provided and VERSION file missing", file=sys.stderr)
            return 2
        version = version_file.read_text(encoding="utf-8").strip()
    version = version.removeprefix("v")
    tag = f"v{version}"

    changelog_path = Path(args.changelog)
    release_date: Optional[str] = None
    body: Optional[str] = None
    compare_url: Optional[str] = None
    if changelog_path.is_file():
        text = changelog_path.read_text(encoding="utf-8")
        release_date, body, compare_url = parse_changelog(text, version)

    if body is None and args.strict:
        print(
            f"error: no '## [{version}]' section found in {changelog_path}",
            file=sys.stderr,
        )
        return 1

    asset_sizes: Dict[str, int] = {}
    dist_path = Path(args.dist_dir) if args.dist_dir else None
    if dist_path and dist_path.is_dir():
        for f in dist_path.iterdir():
            if f.is_file():
                asset_sizes[f.name] = f.stat().st_size
    asset_sizes.update(parse_size_overrides(args.asset_size))

    sha_text = ""
    sha_path: Optional[Path] = None
    if args.sha256sums:
        sha_path = Path(args.sha256sums)
    elif dist_path and (dist_path / "SHA256SUMS.txt").is_file():
        sha_path = dist_path / "SHA256SUMS.txt"
    if sha_path and sha_path.is_file():
        sha_text = sha_path.read_text(encoding="utf-8").strip()

    lines: List[str] = []

    if body:
        rewritten = rewrite_relative_links(body, args.repo, tag)
        # Put any leading intro paragraph above the archive table for crisp scanning.
        first_heading = re.search(r"^###\s+", rewritten, re.MULTILINE)
        if first_heading and first_heading.start() > 0:
            intro = rewritten[: first_heading.start()].strip()
            details = rewritten[first_heading.start() :].strip()
            lines.append(intro)
            lines.append("")
        else:
            details = rewritten
    else:
        details = ""
        lines.append(f"Pre-built multi-platform release binaries for **`glide-ng` {tag}**.")
        lines.append("")

    # Archive table
    has_sizes = any(
        tmpl.format(ver=version) in asset_sizes for tmpl, _ in ARCHIVE_DESCRIPTIONS
    )
    lines.append("### Downloads")
    lines.append("")
    if has_sizes:
        lines.append("| File | Size | Contents |")
        lines.append("| :--- | :---: | :--- |")
        for tmpl, desc in ARCHIVE_DESCRIPTIONS:
            fname = tmpl.format(ver=version)
            sz = format_size(asset_sizes[fname]) if fname in asset_sizes else "—"
            lines.append(f"| **`{fname}`** | {sz} | {desc} |")
    else:
        lines.append("| File | Contents |")
        lines.append("| :--- | :--- |")
        for tmpl, desc in ARCHIVE_DESCRIPTIONS:
            fname = tmpl.format(ver=version)
            lines.append(f"| **`{fname}`** | {desc} |")
    lines.append("")

    glibc_floors: Dict[str, str] = {}
    if dist_path and dist_path.is_dir():
        glibc_floors = read_glibc_floors_from_zip(
            dist_path / f"glide-ng-{version}-runtime.zip"
        )

    def _linux_note(folder: str) -> str:
        floor = glibc_floors.get(folder)
        if floor:
            return f"Embedded `libstdc++` / `libgcc` (max `{floor}`)"
        return "Embedded `libstdc++` / `libgcc` (see `BUILD_INFO.txt` for `GLIBC` floor)"

    # Distro & platform selection guide
    lines.extend(
        [
            "### Choosing the Right Folder (`runtime.zip` / `runtime.tar.xz`)",
            "",
            "| Platform / Distro | Folder inside archive | Runtime linking |",
            "| :--- | :--- | :--- |",
            "| **Windows / Wine / Steam Proton (32-bit)** | `win32/` (`glide.dll`, `glide2x.dll`, `glide3x.dll`) | Self-contained (zero external DLLs) |",
            "| **DOSBox-X / DOSBox-SVN (DOS/32)** | `dos/` (`GLIDE2X.OVL`) | Standalone CauseWay LE overlay |",
            f"| **Ubuntu 22.04 / Debian 12 / Mint 21 / Pop!_OS** | `linux-x86_64-ubuntu-22.04/` | {_linux_note('linux-x86_64-ubuntu-22.04')} |",
            f"| **Ubuntu 24.04 / Mint 22 / Debian testing** | `linux-x86_64-ubuntu-24.04/` | {_linux_note('linux-x86_64-ubuntu-24.04')} |",
            f"| **Fedora 41 / Nobara / RHEL / Bazzite** | `linux-x86_64-fedora-41/` | {_linux_note('linux-x86_64-fedora-41')} |",
            f"| **Arch Linux / CachyOS / EndeavourOS / Manjaro** | `linux-x86_64-arch/` | {_linux_note('linux-x86_64-arch')} |",
            "",
            "<details>",
            "<summary>Minimal Linux runtime packages (no compilers or <code>-dev</code> headers required)</summary>",
            "",
            "- **Debian / Ubuntu / Mint:**",
            "  ```bash",
            "  sudo apt-get install libvulkan1 libsdl2-2.0-0 libegl1 libgles2 libgomp1 libx11-6 libxext6 libwayland-client0 libwayland-egl1",
            "  ```",
            "- **Arch / CachyOS / EndeavourOS / Manjaro:**",
            "  ```bash",
            "  sudo pacman -S vulkan-icd-loader sdl2 mesa gcc-libs libx11 libxext wayland",
            "  ```",
            "- **Fedora / Nobara:**",
            "  ```bash",
            "  sudo dnf install vulkan-loader SDL2 mesa-libEGL mesa-libGLES libgomp libX11 libXext wayland-libs-client",
            "  ```",
            "",
            "Building from source keeps all system libraries dynamically linked by default (`-DGLIDE_STATIC_CPP_RUNTIME=OFF`). "
            f"See [`docs/user-guide/building-and-installing.md`](https://github.com/{args.repo}/blob/{tag}/docs/user-guide/building-and-installing.md) for setup notes and 32-bit Wine Vulkan tips.",
            "",
            "</details>",
            "",
        ]
    )

    if details:
        lines.append("---")
        lines.append("")
        date_suffix = f" ({release_date})" if release_date else ""
        lines.append(f"## What's Changed in `{tag}`{date_suffix}")
        lines.append("")
        lines.append(details)
        lines.append("")

    if sha_text:
        lines.extend(
            [
                "---",
                "",
                "### Checksums (`SHA256SUMS.txt`)",
                "",
                "```text",
                sha_text,
                "```",
                "",
            ]
        )

    lines.append("---")
    lines.append("")
    if compare_url:
        lines.append(
            f"**Full Changelog:** [`CHANGELOG.md`](https://github.com/{args.repo}/blob/{tag}/CHANGELOG.md) "
            f"· [Diff / Commits]({compare_url})"
        )
    else:
        lines.append(
            f"**Full Changelog:** [`CHANGELOG.md`](https://github.com/{args.repo}/blob/{tag}/CHANGELOG.md) "
            f"· [Commits](https://github.com/{args.repo}/commits/{tag})"
        )

    output_text = "\n".join(lines).rstrip() + "\n"
    if args.output == "-":
        sys.stdout.write(output_text)
    else:
        Path(args.output).write_text(output_text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
