#!/usr/bin/env python3
"""Validate screenshots and package a safe, copyright-clean report archive.

Zero third-party dependencies (pure Python 3 stdlib). Performs:
  1. Strict copyright/binary filter blocking .exe/.dll/.ovl/.iso/.hog/.mpq/...
  2. Lossless image header + zero-vision pixel check (all-black / flat-color /
     exact duplicate frame detection for PNG and binary P6 PPM files).
  3. Secret/token leak scan on small text logs/configs.
  4. Compact .tar.gz or .zip bundle output with manifest summary.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
import tarfile
import zipfile
import zlib
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

BLOCKED_EXTS = {
    ".exe", ".dll", ".dle", ".ovl", ".so", ".dylib", ".sys", ".vxd", ".drv", ".com",
    ".iso", ".bin", ".img", ".cue", ".mdf", ".mds", ".nrg", ".ccd", ".sub", ".chd",
    ".hog", ".pig", ".ham", ".mpq", ".pak", ".wad", ".grp", ".pk3", ".rff", ".art",
    ".s11", ".s22", ".mv8", ".mvl", ".bik", ".smk", ".avi", ".mpg", ".mpeg", ".mov",
    ".mp3", ".wav", ".ogg", ".flac", ".mid", ".mus", ".sf2", ".raw", ".zix", ".mix",
    ".dat", ".big", ".pod", ".bsa", ".esm", ".esp", ".vpp", ".res", ".lib", ".a",
}

ALLOWED_EXTS = {
    ".md", ".txt", ".log", ".csv", ".ini", ".cfg", ".conf", ".json", ".yml", ".yaml",
    ".png", ".webp", ".ppm", ".bmp", ".tga", ".jpg", ".jpeg",
    ".sh", ".ps1", ".bat", ".rdc", ".diff", ".patch", ".bt", ".stack",
}

SECRET_RE = re.compile(
    r"(?:github_pat_[A-Za-z0-9_]{10,}|ghp_[A-Za-z0-9]{20,}|gho_[A-Za-z0-9]{20,}|"
    r"AIza[0-9A-Za-z\-_]{30,}|-----BEGIN [A-Z ]*PRIVATE KEY-----)"
)


def png_quick_check(path: Path) -> Tuple[Optional[Tuple[int, int]], str]:
    """Decode PNG dimensions and test IDAT bytes for blank/solid-color image."""
    data = path.read_bytes()
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None, "invalid PNG signature"
    pos = 8
    w = h = 0
    bit_depth = color_type = interlace = 0
    idat_chunks: List[bytes] = []
    saw_iend = False
    while pos + 12 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        ctype = data[pos + 4 : pos + 8]
        chunk_end = pos + 8 + length
        if chunk_end + 4 > len(data):
            return (w, h) if w else None, "truncated PNG chunk"
        payload = data[pos + 8 : chunk_end]
        if ctype == b"IHDR" and length == 13:
            w, h, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", payload)
        elif ctype == b"IDAT":
            idat_chunks.append(payload)
        elif ctype == b"IEND":
            saw_iend = True
            break
        pos = chunk_end + 4
    if not saw_iend or w == 0 or h == 0:
        return None, "missing IHDR or IEND (truncated)"

    # Fast variance/non-black test for standard 8-bit RGB/RGBA non-interlaced PNGs
    if interlace == 0 and bit_depth == 8 and color_type in (2, 6) and idat_chunks:
        bpp = 3 if color_type == 2 else 4
        try:
            raw = zlib.decompress(b"".join(idat_chunks))
        except zlib.error:
            return (w, h), "corrupt IDAT zlib stream"
        stride = 1 + w * bpp
        if len(raw) < stride * h:
            return (w, h), "short decompressed PNG buffer"
        prev = bytearray(w * bpp)
        nonzero = 0
        sample_colors = set()
        step_y = max(1, h // 40)
        for y in range(h):
            off = y * stride
            ftype = raw[off]
            row = bytearray(raw[off + 1 : off + stride])
            if ftype == 1:
                for i in range(bpp, len(row)):
                    row[i] = (row[i] + row[i - bpp]) & 0xFF
            elif ftype == 2:
                for i in range(len(row)):
                    row[i] = (row[i] + prev[i]) & 0xFF
            elif ftype == 3:
                for i in range(len(row)):
                    left = row[i - bpp] if i >= bpp else 0
                    row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
            elif ftype == 4:
                for i in range(len(row)):
                    a = row[i - bpp] if i >= bpp else 0
                    b = prev[i]
                    c = prev[i - bpp] if i >= bpp else 0
                    pa = abs(b - c)
                    pb = abs(a - c)
                    pc = abs(a + b - 2 * c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    row[i] = (row[i] + pr) & 0xFF
            if y % step_y == 0:
                step_x = max(1, w // 40)
                for x in range(0, w, step_x):
                    px = x * bpp
                    r, g, b_ = row[px], row[px + 1], row[px + 2]
                    if r or g or b_:
                        nonzero += 1
                    if len(sample_colors) < 8:
                        sample_colors.add((r, g, b_))
            prev = row
        if nonzero == 0:
            return (w, h), "WARN: appears ALL-BLACK (check capture!)"
        if len(sample_colors) <= 1:
            return (w, h), "WARN: appears solid single color"
    return (w, h), "ok"


def ppm_quick_check(path: Path) -> Tuple[Optional[Tuple[int, int]], str]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        return None, "unsupported PPM header (expected P6)"
    m = re.match(rb"P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        return None, "malformed PPM header"
    w, h, maxv = int(m.group(1)), int(m.group(2)), int(m.group(3))
    pix = data[m.end() :]
    if len(pix) < w * h * 3 or maxv <= 0:
        return (w, h), "truncated PPM payload"
    step = max(3, (len(pix) // 1600) // 3 * 3)
    samples = {pix[i : i + 3] for i in range(0, len(pix) - 2, step)}
    if samples == {b"\x00\x00\x00"}:
        return (w, h), "WARN: appears ALL-BLACK"
    if len(samples) <= 1:
        return (w, h), "WARN: solid single color"
    return (w, h), "ok"


def inspect_image(path: Path) -> str:
    ext = path.suffix.lower()
    if ext == ".png":
        dims, status = png_quick_check(path)
    elif ext == ".ppm":
        dims, status = ppm_quick_check(path)
    elif ext == ".webp":
        hdr = path.read_bytes()[:32]
        if hdr[:4] == b"RIFF" and hdr[8:12] == b"WEBP":
            dims, status = None, "ok (webp)"
        else:
            dims, status = None, "invalid WebP header"
    else:
        return f"{path.stat().st_size:,} B"
    dim_s = f"{dims[0]}x{dims[1]}" if dims else "?"
    return f"{dim_s}, {path.stat().st_size:,} B — {status}"


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Verify copyright safety, sanity-check screenshots, and pack a bundle."
    )
    ap.add_argument("bundle_dir", type=Path, help="Folder containing logs/screens/NOTES.md")
    ap.add_argument(
        "--out",
        "-o",
        type=Path,
        default=None,
        help="Output archive path (.tar.gz or .zip; default: <bundle_dir>.tar.gz)",
    )
    ap.add_argument(
        "--max-file-mb",
        type=float,
        default=25.0,
        help="Reject single files larger than this size in MB (default: 25)",
    )
    ap.add_argument(
        "--check-only",
        action="store_true",
        help="Run validation checks without creating an archive",
    )
    args = ap.parse_args(argv)

    root = args.bundle_dir.expanduser().resolve()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2

    max_bytes = int(args.max_file_mb * 1024 * 1024)
    blocked: List[str] = []
    skipped_unknown: List[str] = []
    secret_hits: List[str] = []
    included: List[Path] = []
    img_hashes: Dict[str, str] = {}
    img_notes: List[str] = []

    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.is_symlink():
            continue
        rel = p.relative_to(root).as_posix()
        ext = p.suffix.lower()
        sz = p.stat().st_size

        if ext in BLOCKED_EXTS:
            blocked.append(f"{rel} ({sz:,} B — blocked extension '{ext}')")
            continue
        if ext not in ALLOWED_EXTS and p.name.upper() not in ("NOTES", "README", "VERSION"):
            skipped_unknown.append(f"{rel} ({sz:,} B — unrecognized ext '{ext or '<none>'}')")
            continue
        if sz > max_bytes:
            blocked.append(f"{rel} ({sz / 1024 / 1024:.1f} MB > {args.max_file_mb:g} MB limit)")
            continue

        # Secret/token check on plain-text files
        if ext in {".md", ".txt", ".log", ".ini", ".cfg", ".conf", ".sh", ".ps1", ".json", ".yml", ".yaml"}:
            if sz <= 4 * 1024 * 1024:
                txt = p.read_bytes().decode("utf-8", "replace")
                if SECRET_RE.search(txt):
                    secret_hits.append(rel)

        # Zero-vision screenshot check
        if ext in {".png", ".ppm", ".webp", ".bmp", ".tga", ".jpg", ".jpeg"}:
            desc = inspect_image(p)
            digest = hashlib.sha256(p.read_bytes()).hexdigest()[:16]
            dup = ""
            if digest in img_hashes:
                dup = f" [DUPLICATE of {img_hashes[digest]}]"
            else:
                img_hashes[digest] = rel
            img_notes.append(f"  - {rel}: {desc}{dup}")

        included.append(p)

    print(f"=== Bundle Check: {root.name} ({len(included)} valid files) ===")
    if img_notes:
        print("Screenshots:")
        for ln in img_notes:
            print(ln)
    if skipped_unknown:
        print("Skipped (not in allowlist):")
        for s in skipped_unknown:
            print(f"  [skip] {s}")
    if blocked:
        print("REFUSED (copyright/binary or size policy):", file=sys.stderr)
        for b in blocked:
            print(f"  [BLOCKED] {b}", file=sys.stderr)
    if secret_hits:
        print("REFUSED (possible token/key found in text file):", file=sys.stderr)
        for s in secret_hits:
            print(f"  [SECRET] {s}", file=sys.stderr)

    if blocked or secret_hits:
        print("Aborting archive creation. Remove blocked files/secrets and re-run.", file=sys.stderr)
        return 1
    if not included:
        print("error: no files to pack.", file=sys.stderr)
        return 2
    if args.check_only:
        print("Check-only OK (no archive written).")
        return 0

    out_path = args.out if args.out is not None else root.with_suffix(".tar.gz")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.suffix.lower() == ".zip":
        with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for p in included:
                arcname = f"{root.name}/{p.relative_to(root).as_posix()}"
                zf.write(p, arcname)
    else:
        with tarfile.open(out_path, "w:gz", compresslevel=9) as tf:
            for p in included:
                arcname = f"{root.name}/{p.relative_to(root).as_posix()}"
                tf.add(p, arcname=arcname, recursive=False)

    print(f"Packed {len(included)} files -> {out_path} ({out_path.stat().st_size / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
