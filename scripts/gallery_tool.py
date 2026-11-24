#!/usr/bin/env python3
"""Cross-backend visual gallery helper for glide-ng & DOSBox captures.

Pure Python 3 standard library (zero third-party pip dependencies); wraps
standard Linux utilities (`xdotool`, ImageMagick `import`/`magick`, `ffmpeg`,
`cwebp`) when needed.

Subcommands:
  capture   Periodically grab an X11 window (e.g. windowed OpenGLide DOSBox)
  extract   Sample frames from a DOSBox-X / DOSBox-Staging ZMBV .avi capture
  compare   Compute MAE / RMSE / PSNR / Diff% and amplified diff montages
  pack      Convert .ppm / .png captures into compact WebP or lossless PNGs
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


IMAGE_EXTS = {".ppm", ".png", ".bmp", ".tga", ".webp"}
FRAME_NUM_RE = re.compile(r"(?:^|[_-])(\d{2,})$")


# ---------------------------------------------------------------------------
# Pure-stdlib lossless PPM (P6) & PNG (8-bit RGB/RGBA) image codecs
# ---------------------------------------------------------------------------

def _skip_ppm_ws_and_comments(data: bytes, pos: int) -> int:
    n = len(data)
    while pos < n:
        ch = data[pos : pos + 1]
        if ch in b" \t\r\n":
            pos += 1
        elif ch == b"#":
            nl = data.find(b"\n", pos)
            pos = n if nl == -1 else nl + 1
        else:
            break
    return pos


def _read_ppm_token(data: bytes, pos: int) -> Tuple[str, int]:
    pos = _skip_ppm_ws_and_comments(data, pos)
    start = pos
    n = len(data)
    while pos < n and data[pos : pos + 1] not in b" \t\r\n#":
        pos += 1
    if start == pos:
        raise ValueError("Unexpected EOF while parsing P6 PPM header")
    return data[start:pos].decode("ascii"), pos


def read_ppm_p6(path: Path) -> Tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, pos = _read_ppm_token(data, 0)
    if magic != "P6":
        raise ValueError(f"{path}: expected P6 PPM, got {magic!r}")
    w_str, pos = _read_ppm_token(data, pos)
    h_str, pos = _read_ppm_token(data, pos)
    maxv_str, pos = _read_ppm_token(data, pos)
    w, h, maxv = int(w_str), int(h_str), int(maxv_str)
    if w <= 0 or h <= 0 or maxv not in (255, 65535):
        raise ValueError(f"{path}: unsupported PPM geometry {w}x{h} maxval={maxv}")
    # Exactly one whitespace byte separates maxval from raw RGB pixels.
    if pos >= len(data) or data[pos : pos + 1] not in b" \t\r\n":
        raise ValueError(f"{path}: malformed separator after PPM maxval")
    pix_start = pos + 1
    if maxv == 255:
        expected = w * h * 3
        if len(data) - pix_start < expected:
            raise ValueError(
                f"{path}: truncated PPM pixel payload "
                f"({len(data) - pix_start} < {expected})"
            )
        return w, h, data[pix_start : pix_start + expected]

    # Q16 ImageMagick outputs 16-bit big-endian RGB when maxval == 65535.
    expected16 = w * h * 6
    if len(data) - pix_start < expected16:
        raise ValueError(
            f"{path}: truncated 16-bit PPM pixel payload "
            f"({len(data) - pix_start} < {expected16})"
        )
    return w, h, data[pix_start : pix_start + expected16 : 2]


def write_ppm_p6(path: Path, width: int, height: int, rgb: bytes) -> None:
    expected = width * height * 3
    if len(rgb) != expected:
        raise ValueError(f"RGB buffer size {len(rgb)} != {expected}")
    path.parent.mkdir(parents=True, exist_ok=True)
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    path.write_bytes(header + rgb)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgb_fast(path: Path) -> Optional[Tuple[int, int, bytes]]:
    """Decode non-interlaced 8-bit RGB/RGBA PNGs via stdlib; return None otherwise."""
    data = path.read_bytes()
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: invalid PNG signature")
    pos = 8
    w = h = bit_depth = color_type = interlace = 0
    idat_parts: List[bytes] = []
    saw_iend = False
    while pos + 12 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        ctype = data[pos + 4 : pos + 8]
        chunk_end = pos + 8 + length
        if chunk_end + 4 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk {ctype!r}")
        payload = data[pos + 8 : chunk_end]
        if ctype == b"IHDR" and length == 13:
            w, h, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif ctype == b"IDAT":
            idat_parts.append(payload)
        elif ctype == b"IEND":
            saw_iend = True
            break
        pos = chunk_end + 4
    if not saw_iend or w <= 0 or h <= 0:
        raise ValueError(f"{path}: missing IHDR or IEND")
    if interlace != 0 or bit_depth != 8 or color_type not in (2, 6):
        return None

    bpp = 3 if color_type == 2 else 4
    raw = zlib.decompress(b"".join(idat_parts))
    row_bytes = w * bpp
    stride = 1 + row_bytes
    if len(raw) < stride * h:
        raise ValueError(f"{path}: truncated PNG IDAT payload")

    out = bytearray(w * h * 3)
    prev = bytearray(row_bytes)
    for y in range(h):
        off = y * stride
        ftype = raw[off]
        row = bytearray(raw[off + 1 : off + stride])
        if ftype == 0:
            pass
        elif ftype == 1:
            for i in range(bpp, row_bytes):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(row_bytes):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(row_bytes):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(row_bytes):
                left = row[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(left, prev[i], up_left)) & 0xFF
        else:
            raise ValueError(f"{path}: unknown PNG filter type {ftype}")
        dst_off = y * w * 3
        if bpp == 3:
            prev = row
            out[dst_off : dst_off + row_bytes] = row
        else:
            prev = bytes(row)
            del row[3::4]
            out[dst_off : dst_off + w * 3] = row
    return w, h, bytes(out)


def _png_chunk(tag: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)


def write_png_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    expected = width * height * 3
    if len(rgb) != expected:
        raise ValueError(f"RGB buffer size {len(rgb)} != {expected}")
    path.parent.mkdir(parents=True, exist_ok=True)
    row_bytes = width * 3
    raw = bytearray((row_bytes + 1) * height)
    for y in range(height):
        src = y * row_bytes
        dst = y * (row_bytes + 1)
        raw[dst] = 0  # Filter None (sub-filter could be used, zlib level 6 compacts well)
        raw[dst + 1 : dst + 1 + row_bytes] = rgb[src : src + row_bytes]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    compressed = zlib.compress(bytes(raw), level=6)
    png_bytes = (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", compressed)
        + _png_chunk(b"IEND", b"")
    )
    path.write_bytes(png_bytes)


def _find_magick() -> Optional[str]:
    return shutil.which("magick") or shutil.which("convert")


def load_image_rgb(
    path: Path, resize_to: Optional[Tuple[int, int]] = None
) -> Tuple[int, int, bytes]:
    """Load any supported image as 8-bit packed RGB bytes."""
    ext = path.suffix.lower()
    if resize_to is None:
        if ext == ".ppm":
            return read_ppm_p6(path)
        if ext == ".png":
            fast = read_png_rgb_fast(path)
            if fast is not None:
                return fast

    ffmpeg = shutil.which("ffmpeg")
    magick = _find_magick()
    if not ffmpeg and not magick:
        raise RuntimeError(
            f"Cannot decode/resize {path}: install 'ffmpeg' or ImageMagick ('magick')"
        )

    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as tf:
        tmp_ppm = Path(tf.name)
    try:
        if ffmpeg:
            cmd = [ffmpeg, "-v", "error", "-y", "-i", str(path)]
            if resize_to is not None:
                cmd.extend(["-vf", f"scale={resize_to[0]}:{resize_to[1]}:flags=bilinear"])
            cmd.extend(["-frames:v", "1", "-f", "image2", "-vcodec", "ppm", str(tmp_ppm)])
            subprocess.run(cmd, check=True)
        else:
            cmd = [str(magick), str(path)]
            if resize_to is not None:
                cmd.extend(["-resize", f"{resize_to[0]}x{resize_to[1]}!"])
            cmd.extend(["-depth", "8", f"ppm:{tmp_ppm}"])
            subprocess.run(cmd, check=True)
        return read_ppm_p6(tmp_ppm)
    finally:
        tmp_ppm.unlink(missing_ok=True)


def analyze_rgb_buffer(rgb: bytes, width: int, height: int) -> Tuple[bool, bool, str]:
    """Return (is_black, is_flat, sha256_hex) for an 8-bit RGB image."""
    total_px = width * height
    step_px = max(1, total_px // 4096)
    max_val = 0
    colors = set()
    for px in range(0, total_px, step_px):
        off = px * 3
        r, g, b = rgb[off], rgb[off + 1], rgb[off + 2]
        if r > max_val:
            max_val = r
        if g > max_val:
            max_val = g
        if b > max_val:
            max_val = b
        if len(colors) < 4:
            colors.add((r, g, b))
    is_black = max_val <= 2
    is_flat = len(colors) <= 1
    if is_black:
        # Double-check full buffer before declaring blank so a tiny bright sprite isn't dropped.
        max_full = max(rgb) if rgb else 0
        is_black = max_full <= 2
    digest = hashlib.sha256(rgb).hexdigest()
    return is_black, is_flat, digest


# ---------------------------------------------------------------------------
# Subcommand: capture (X11 periodic window screenshots via xdotool + import)
# ---------------------------------------------------------------------------

def _get_x11_wm_class(wid: str) -> str:
    if not shutil.which("xprop"):
        return ""
    res = subprocess.run(
        ["xprop", "-id", wid, "WM_CLASS"],
        capture_output=True,
        text=True,
    )
    return res.stdout.strip() if res.returncode == 0 else ""


def _pick_x11_window(match_regex: str) -> Optional[str]:
    res = subprocess.run(
        ["xdotool", "search", "--onlyvisible", "--name", match_regex],
        capture_output=True,
        text=True,
    )
    cands = [line.strip() for line in res.stdout.splitlines() if line.strip()]
    if not cands:
        res = subprocess.run(
            ["xdotool", "search", "--name", match_regex],
            capture_output=True,
            text=True,
        )
        cands = [line.strip() for line in res.stdout.splitlines() if line.strip()]
    if not cands:
        return None

    # Skip Wayland/X11 WM decorator frames (e.g. GNOME mutter-x11-frames) so we
    # grab the bare client canvas without titlebars or drop shadows.
    non_frames = [
        wid for wid in cands
        if "mutter-x11-frames" not in _get_x11_wm_class(wid).lower()
    ]
    pool = non_frames if non_frames else cands
    if len(pool) == 1:
        return pool[0]

    # Prefer the largest matching window (DOSBox-X/SDL sometimes owns a tiny helper window).
    best_wid = pool[-1]
    best_area = -1
    for wid in pool:
        g = subprocess.run(
            ["xdotool", "getwindowgeometry", wid],
            capture_output=True,
            text=True,
        )
        m = re.search(r"Geometry:\s*(\d+)x(\d+)", g.stdout)
        area = int(m.group(1)) * int(m.group(2)) if m else 0
        if area > best_area:
            best_area = area
            best_wid = wid
    return best_wid


def _list_visible_x11_windows(limit: int = 10) -> List[str]:
    res = subprocess.run(
        ["xdotool", "search", "--onlyvisible", "--name", "."],
        capture_output=True,
        text=True,
    )
    out: List[str] = []
    for wid in res.stdout.splitlines():
        wid = wid.strip()
        if not wid:
            continue
        nm = subprocess.run(
            ["xdotool", "getwindowname", wid],
            capture_output=True,
            text=True,
        )
        title = nm.stdout.strip() or "<unnamed>"
        out.append(f"{wid}: {title}")
        if len(out) >= limit:
            break
    return out


def cmd_capture(args: argparse.Namespace) -> int:
    if args.interval <= 0:
        print("Error: --interval must be > 0", file=sys.stderr)
        return 2
    if args.delay < 0 or args.count < 0:
        print("Error: --delay and --count must be >= 0", file=sys.stderr)
        return 2

    import_bin = shutil.which("import")
    xdotool_bin = shutil.which("xdotool")
    if not import_bin:
        print("Error: ImageMagick 'import' tool not found in PATH.", file=sys.stderr)
        return 2
    if not args.wid and not xdotool_bin:
        print("Error: 'xdotool' not found in PATH (or pass --wid).", file=sys.stderr)
        return 2

    spawn_cmd = list(args.command or [])
    if spawn_cmd and spawn_cmd[0] == "--":
        spawn_cmd = spawn_cmd[1:]

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    stop_requested = False

    def _on_sig(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    old_sigint = signal.signal(signal.SIGINT, _on_sig)
    old_sigterm = signal.signal(signal.SIGTERM, _on_sig)

    proc: Optional[subprocess.Popen[bytes]] = None
    try:
        if spawn_cmd:
            child_env = os.environ.copy()
            if (
                os.environ.get("WAYLAND_DISPLAY")
                and not args.wid
                and not args.no_x11_env
                and child_env.get("SDL_VIDEODRIVER") != "x11"
            ):
                prev_sdl = child_env.get("SDL_VIDEODRIVER")
                child_env["SDL_VIDEODRIVER"] = "x11"
                prev_note = f" (overrode SDL_VIDEODRIVER={prev_sdl!r})" if prev_sdl else ""
                print(
                    f"[INFO] Wayland detected: launching child with "
                    f"SDL_VIDEODRIVER=x11{prev_note} for XWayland capture."
                )
            print(f"Launching command: {' '.join(spawn_cmd)}")
            proc = subprocess.Popen(spawn_cmd, env=child_env)

        wid = args.wid
        wait_deadline = time.monotonic() + 30.0
        while not wid and not stop_requested:
            if proc is not None and proc.poll() is not None:
                print(f"Target process exited ({proc.returncode}) before window appeared.")
                return 0
            wid = _pick_x11_window(args.match)
            if wid:
                break
            if time.monotonic() >= wait_deadline:
                print(
                    f"Error: timed out waiting for X11 window matching /{args.match}/.",
                    file=sys.stderr,
                )
                visible = _list_visible_x11_windows()
                if visible:
                    print("Visible X11/XWayland windows:", file=sys.stderr)
                    for item in visible:
                        print(f"  {item}", file=sys.stderr)
                if os.environ.get("WAYLAND_DISPLAY") and not spawn_cmd:
                    print(
                        "Hint: Wayland session active; launch SDL apps with "
                        "SDL_VIDEODRIVER=x11 so XWayland exposes the window.",
                        file=sys.stderr,
                    )
                return 1
            time.sleep(0.25)

        if stop_requested or not wid:
            return 0

        print(f"Target X11 window ID: {wid} (saving PNGs to {out_dir})")
        delay_end = time.monotonic() + args.delay
        while not stop_requested and time.monotonic() < delay_end:
            if proc is not None and proc.poll() is not None:
                break
            time.sleep(min(0.25, max(0.0, delay_end - time.monotonic())))

        saved = 0
        skipped_black = 0
        skipped_dup = 0
        last_digest: Optional[str] = None
        next_tick = time.monotonic()

        while not stop_requested:
            if args.count > 0 and saved >= args.count:
                break
            if proc is not None and proc.poll() is not None:
                break

            now = time.monotonic()
            if now < next_tick:
                time.sleep(min(0.2, next_tick - now))
                continue
            next_tick = max(next_tick + args.interval, time.monotonic() + 0.05)

            if not args.wid:
                cur_wid = _pick_x11_window(args.match)
                if not cur_wid:
                    continue
                if cur_wid != wid:
                    wid = cur_wid
                    print(f"[INFO] Switched X11 window ID -> {wid}")

            try:
                imp = subprocess.run(
                    [import_bin, "-silent", "-window", wid, "-depth", "8", "ppm:-"],
                    capture_output=True,
                    timeout=3.0,
                )
            except subprocess.TimeoutExpired:
                if proc is not None and proc.poll() is not None:
                    break
                print(
                    f"[WARN] Timed out grabbing X11 window {wid}; retrying...",
                    file=sys.stderr,
                )
                continue

            if imp.returncode != 0 or not imp.stdout:
                if proc is not None and proc.poll() is not None:
                    break
                err_line = imp.stderr.decode("utf-8", "replace").strip().splitlines()
                detail = err_line[0] if err_line else f"rc={imp.returncode}"
                print(
                    f"[WARN] Frame grab failed on window {wid} ({detail}); retrying...",
                    file=sys.stderr,
                )
                continue

            with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as tf:
                tmp_path = Path(tf.name)
                tf.write(imp.stdout)
            try:
                w, h, rgb = read_ppm_p6(tmp_path)
            except Exception as exc:
                print(f"[WARN] Skipped malformed frame grab: {exc}", file=sys.stderr)
                continue
            finally:
                tmp_path.unlink(missing_ok=True)

            is_black, _is_flat, digest = analyze_rgb_buffer(rgb, w, h)
            if args.skip_black and is_black:
                skipped_black += 1
                continue
            if args.skip_dup and digest == last_digest:
                skipped_dup += 1
                continue

            out_path = out_dir / f"{args.prefix}_{saved:04d}.png"
            write_png_rgb(out_path, w, h, rgb)
            last_digest = digest
            saved += 1
            print(f"[{saved:04d}] {out_path.name} ({w}x{h})")

        print(
            f"Capture complete: {saved} saved, {skipped_black} black skipped, "
            f"{skipped_dup} duplicate skipped -> {out_dir}"
        )
        if saved == 0 and skipped_black > 0 and os.environ.get("WAYLAND_DISPLAY"):
            print(
                "[HINT] Wayland compositors (e.g. GNOME Mutter) may return black "
                "pixels for X11 'import' grabs on OpenGL XWayland surfaces; use "
                "your desktop screenshot shortcut or record raw PNGs natively.",
                file=sys.stderr,
            )
        return 0
    finally:
        signal.signal(signal.SIGINT, old_sigint)
        signal.signal(signal.SIGTERM, old_sigterm)
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


# ---------------------------------------------------------------------------
# Subcommand: extract (DOSBox ZMBV / video -> PNG frames via ffmpeg)
# ---------------------------------------------------------------------------

def cmd_extract(args: argparse.Namespace) -> int:
    if args.fps <= 0:
        print("Error: --fps must be > 0", file=sys.stderr)
        return 2
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("Error: 'ffmpeg' not found in PATH.", file=sys.stderr)
        return 2

    videos = [Path(v) for v in args.videos]
    for video in videos:
        if not video.is_file():
            print(f"Error: video file not found: {video}", file=sys.stderr)
            return 2

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    multi = len(videos) > 1
    total_saved = 0
    total_black = 0
    total_dup = 0

    for video in videos:
        with tempfile.TemporaryDirectory(prefix="glide_ng_extract_") as tmp_dir_str:
            tmp_dir = Path(tmp_dir_str)
            cmd = [ffmpeg, "-v", "error", "-y"]
            if args.start is not None:
                cmd.extend(["-ss", str(args.start)])
            cmd.extend(["-i", str(video)])
            if args.duration is not None:
                cmd.extend(["-t", str(args.duration)])
            cmd.extend(["-vf", f"fps={args.fps}", str(tmp_dir / "raw_%06d.png")])
            subprocess.run(cmd, check=True)

            extracted = sorted(tmp_dir.glob("raw_*.png"))
            saved = 0
            skipped_black = 0
            skipped_dup = 0
            last_digest: Optional[str] = None
            file_prefix = f"{args.prefix}_{video.stem}" if multi else args.prefix
            for p in extracted:
                w, h, rgb = load_image_rgb(p)
                is_black, _is_flat, digest = analyze_rgb_buffer(rgb, w, h)
                if args.skip_black and is_black:
                    skipped_black += 1
                    continue
                if args.skip_dup and digest == last_digest:
                    skipped_dup += 1
                    continue
                dst = out_dir / f"{file_prefix}_{saved:06d}.png"
                shutil.move(str(p), str(dst))
                last_digest = digest
                saved += 1

        total_saved += saved
        total_black += skipped_black
        total_dup += skipped_dup
        print(
            f"Extracted {saved} frames from {video.name} "
            f"({skipped_black} black skipped, {skipped_dup} duplicate skipped) -> {out_dir}"
        )

    if multi:
        print(
            f"Total extracted across {len(videos)} videos: {total_saved} saved "
            f"({total_black} black skipped, {total_dup} duplicate skipped)"
        )
    return 0


# ---------------------------------------------------------------------------
# Subcommand: compare (Cross-backend image metrics & amplified diff montages)
# ---------------------------------------------------------------------------

def _extract_frame_key(path: Path) -> Optional[int]:
    m = FRAME_NUM_RE.search(path.stem)
    return int(m.group(1)) if m else None


def _pair_inputs(ref: Path, cand: Path) -> List[Tuple[str, Path, Path, str]]:
    if ref.is_file() and cand.is_file():
        return [(ref.stem, ref, cand, "explicit files")]
    if not (ref.is_dir() and cand.is_dir()):
        raise ValueError("Both inputs must be files or both must be directories.")

    ref_files = sorted(
        p for p in ref.iterdir() if p.is_file() and p.suffix.lower() in IMAGE_EXTS
    )
    cand_files = sorted(
        p for p in cand.iterdir() if p.is_file() and p.suffix.lower() in IMAGE_EXTS
    )
    if not ref_files or not cand_files:
        raise ValueError("One or both directories contain no supported images.")

    # 1. Exact stem match
    cand_by_stem: Dict[str, Path] = {p.stem: p for p in cand_files}
    stem_pairs = [
        (r.stem, r, cand_by_stem[r.stem], "stem match")
        for r in ref_files
        if r.stem in cand_by_stem
    ]
    if len(stem_pairs) == len(ref_files) and len(stem_pairs) == len(cand_files):
        return stem_pairs

    # 2. Frame-number match (e.g. glide_snapshot_000300.ppm vs sr_000300.png)
    ref_by_frame: Dict[int, Path] = {}
    cand_by_frame: Dict[int, Path] = {}
    for p in ref_files:
        fk = _extract_frame_key(p)
        if fk is not None and fk not in ref_by_frame:
            ref_by_frame[fk] = p
    for p in cand_files:
        fk = _extract_frame_key(p)
        if fk is not None and fk not in cand_by_frame:
            cand_by_frame[fk] = p
    shared_frames = sorted(set(ref_by_frame) & set(cand_by_frame))
    if shared_frames and len(shared_frames) >= max(len(stem_pairs), 1):
        return [
            (
                f"F{f:06d}",
                ref_by_frame[f],
                cand_by_frame[f],
                f"frame #{f}",
            )
            for f in shared_frames
        ]

    if stem_pairs:
        return stem_pairs

    # 3. Sorted index pairing fallback
    count = min(len(ref_files), len(cand_files))
    return [
        (ref_files[i].stem, ref_files[i], cand_files[i], "index order")
        for i in range(count)
    ]


def _compute_diff_and_metrics(
    ref_rgb: bytes,
    cand_rgb: bytes,
    width: int,
    height: int,
    gain: float,
    threshold: int,
    make_diff_image: bool,
) -> Tuple[dict, Optional[bytes]]:
    total_px = width * height
    total_ch = total_px * 3
    sum_abs = 0
    sum_sq = 0
    max_delta = 0
    diff_px = 0
    diff_rgb = bytearray(total_ch) if make_diff_image else None

    for i in range(0, total_ch, 3):
        dr = abs(ref_rgb[i] - cand_rgb[i])
        dg = abs(ref_rgb[i + 1] - cand_rgb[i + 1])
        db = abs(ref_rgb[i + 2] - cand_rgb[i + 2])
        sum_abs += dr + dg + db
        sum_sq += dr * dr + dg * dg + db * db
        pm = max(dr, dg, db)
        if pm > max_delta:
            max_delta = pm
        if pm > threshold:
            diff_px += 1
        if diff_rgb is not None:
            diff_rgb[i] = min(255, int(round(dr * gain)))
            diff_rgb[i + 1] = min(255, int(round(dg * gain)))
            diff_rgb[i + 2] = min(255, int(round(db * gain)))

    mae = sum_abs / total_ch
    mse = sum_sq / total_ch
    rmse = math.sqrt(mse)
    psnr = float("inf") if mse == 0.0 else 10.0 * math.log10((255.0 * 255.0) / mse)
    diff_pct = (100.0 * diff_px) / total_px

    metrics = {
        "width": width,
        "height": height,
        "mae": round(mae, 4),
        "rmse": round(rmse, 4),
        "psnr_db": None if math.isinf(psnr) else round(psnr, 2),
        "exact_match": math.isinf(psnr),
        "diff_pct": round(diff_pct, 3),
        "max_delta": max_delta,
    }
    return metrics, (bytes(diff_rgb) if diff_rgb is not None else None)


def _compose_horizontal_triptych(
    left: bytes, mid: bytes, right: bytes, width: int, height: int
) -> Tuple[int, int, bytes]:
    row_bytes = width * 3
    out_w = width * 3
    out = bytearray(out_w * height * 3)
    for y in range(height):
        src = y * row_bytes
        dst = y * out_w * 3
        out[dst : dst + row_bytes] = left[src : src + row_bytes]
        out[dst + row_bytes : dst + 2 * row_bytes] = mid[src : src + row_bytes]
        out[dst + 2 * row_bytes : dst + 3 * row_bytes] = right[src : src + row_bytes]
    return out_w, height, bytes(out)


def _write_rgb_by_ext(path: Path, width: int, height: int, rgb: bytes) -> None:
    if path.suffix.lower() == ".ppm":
        write_ppm_p6(path, width, height, rgb)
    else:
        write_png_rgb(path, width, height, rgb)


def cmd_compare(args: argparse.Namespace) -> int:
    ref_path = Path(args.reference)
    cand_path = Path(args.candidate)
    try:
        pairs = _pair_inputs(ref_path, cand_path)
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    diff_dir = Path(args.diff_dir) if args.diff_dir else None
    diff_out = Path(args.diff_out) if args.diff_out else None
    if diff_dir:
        diff_dir.mkdir(parents=True, exist_ok=True)

    rows = []
    failed_threshold = False
    print("| Pair | Size | MAE | RMSE | PSNR (dB) | Diff% | MaxΔ | Note |")
    print("| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |")

    for label, r_file, c_file, note in pairs:
        rw, rh, r_rgb = load_image_rgb(r_file)
        cw, ch, c_rgb = load_image_rgb(c_file)
        pair_note = note
        if (cw, ch) != (rw, rh):
            if args.resize:
                cw, ch, c_rgb = load_image_rgb(c_file, resize_to=(rw, rh))
                pair_note += f", resized {c_file.name} -> {rw}x{rh}"
            else:
                raise RuntimeError(
                    f"Dimension mismatch for {label}: {r_file.name}={rw}x{rh} vs "
                    f"{c_file.name}={cw}x{ch} (pass --resize to rescale candidate)"
                )

        out_path: Optional[Path] = None
        if diff_out and len(pairs) == 1:
            out_path = diff_out
        elif diff_dir:
            suffix = "_montage.png" if args.montage else "_diff.png"
            out_path = diff_dir / f"{label}{suffix}"

        metrics, diff_rgb = _compute_diff_and_metrics(
            r_rgb,
            c_rgb,
            rw,
            rh,
            args.gain,
            args.threshold,
            make_diff_image=(out_path is not None),
        )
        metrics["label"] = label
        metrics["reference"] = str(r_file)
        metrics["candidate"] = str(c_file)

        if out_path is not None and diff_rgb is not None:
            if args.montage:
                mw, mh, montage_rgb = _compose_horizontal_triptych(
                    r_rgb, c_rgb, diff_rgb, rw, rh
                )
                _write_rgb_by_ext(out_path, mw, mh, montage_rgb)
            else:
                _write_rgb_by_ext(out_path, rw, rh, diff_rgb)
            metrics["diff_image"] = str(out_path)

        psnr_val = float("inf") if metrics["exact_match"] else float(metrics["psnr_db"])
        if args.fail_below_psnr is not None and psnr_val < args.fail_below_psnr:
            failed_threshold = True
            pair_note += " [FAIL PSNR]"

        psnr_str = "inf" if metrics["exact_match"] else f"{metrics['psnr_db']:.2f}"
        print(
            f"| `{label}` | {rw}×{rh} | {metrics['mae']:.2f} | {metrics['rmse']:.2f} | "
            f"{psnr_str} | {metrics['diff_pct']:.2f}% | {metrics['max_delta']} | {pair_note} |"
        )
        rows.append(metrics)

    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")

    return 2 if failed_threshold else 0


# ---------------------------------------------------------------------------
# Subcommand: pack (PPM / PNG -> compact WebP or lossless PNG)
# ---------------------------------------------------------------------------

def _collect_pack_inputs(raw_inputs: Sequence[str]) -> List[Path]:
    files: List[Path] = []
    seen = set()
    for item in raw_inputs:
        p = Path(item)
        if p.is_dir():
            for child in sorted(p.iterdir()):
                if (
                    child.is_file()
                    and child.suffix.lower() in IMAGE_EXTS
                    and child.resolve() not in seen
                ):
                    seen.add(child.resolve())
                    files.append(child)
        elif p.is_file() and p.resolve() not in seen:
            seen.add(p.resolve())
            files.append(p)
        else:
            print(f"[WARN] Skipping missing input: {item}", file=sys.stderr)
    return files


def cmd_pack(args: argparse.Namespace) -> int:
    inputs = _collect_pack_inputs(args.inputs)
    if not inputs:
        print("Error: no matching input images found.", file=sys.stderr)
        return 2

    out_dir = Path(args.out) if args.out else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    cwebp = shutil.which("cwebp")
    magick = _find_magick()
    ffmpeg = shutil.which("ffmpeg")

    if args.format == "webp" and not (cwebp or magick or ffmpeg):
        print(
            "Error: encoding WebP requires 'cwebp', ImageMagick ('magick'), or 'ffmpeg'.",
            file=sys.stderr,
        )
        return 2

    total_in = 0
    total_out = 0
    packed = 0

    for src in inputs:
        dst_dir = out_dir if out_dir else src.parent
        dst = dst_dir / f"{src.stem}.{args.format}"
        if dst.resolve() == src.resolve():
            print(
                f"Error: output path {dst} matches input; pass --out DIR.",
                file=sys.stderr,
            )
            return 2

        w, h, rgb = load_image_rgb(src)
        is_black, is_flat, _ = analyze_rgb_buffer(rgb, w, h)
        if is_black:
            msg = f"[WARN] {src.name} ({w}x{h}) appears completely black"
            if args.skip_black:
                print(msg + " — skipped")
                continue
            print(msg, file=sys.stderr)
        elif is_flat:
            print(f"[WARN] {src.name} ({w}x{h}) appears solid-color", file=sys.stderr)

        if args.format == "png":
            write_png_rgb(dst, w, h, rgb)
        else:
            with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as tf:
                tmp_src = Path(tf.name)
            try:
                write_ppm_p6(tmp_src, w, h, rgb)
                if cwebp:
                    cmd = [cwebp, "-quiet"]
                    if args.lossless:
                        cmd.extend(["-lossless", "-z", "6"])
                    else:
                        cmd.extend(["-q", str(args.quality)])
                    cmd.extend([str(tmp_src), "-o", str(dst)])
                    subprocess.run(cmd, check=True)
                elif magick:
                    cmd = [str(magick), str(tmp_src)]
                    if args.lossless:
                        cmd.extend(["-define", "webp:lossless=true"])
                    else:
                        cmd.extend(["-quality", str(args.quality)])
                    cmd.append(str(dst))
                    subprocess.run(cmd, check=True)
                else:
                    cmd = [
                        str(ffmpeg),
                        "-v",
                        "error",
                        "-y",
                        "-i",
                        str(tmp_src),
                        "-c:v",
                        "libwebp",
                        "-lossless",
                        "1" if args.lossless else "0",
                        "-q:v",
                        str(args.quality),
                        str(dst),
                    ]
                    subprocess.run(cmd, check=True)
            finally:
                tmp_src.unlink(missing_ok=True)

        in_sz = src.stat().st_size
        out_sz = dst.stat().st_size
        total_in += in_sz
        total_out += out_sz
        packed += 1
        print(
            f"[PACK] {src.name} ({w}x{h}, {in_sz / 1024.0:.1f} KB) -> "
            f"{dst.name} ({out_sz / 1024.0:.1f} KB)"
        )

    if packed > 0 and total_in > 0:
        ratio = 100.0 * (1.0 - (total_out / total_in))
        print(
            f"Packed {packed} image(s): {total_in / 1024.0:.1f} KB -> "
            f"{total_out / 1024.0:.1f} KB ({ratio:.1f}% saved)"
        )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Visual gallery capture, extraction, comparison & packing helper."
    )
    sub = parser.add_subparsers(dest="subcmd", required=True)

    p_cap = sub.add_parser(
        "capture",
        help="Capture periodic screenshots from an X11 window (e.g. windowed OpenGLide).",
    )
    p_cap.add_argument("--out", required=True, help="Output directory for PNG shots")
    p_cap.add_argument("--match", default="DOSBox", help="Regex for xdotool window title")
    p_cap.add_argument("--wid", help="Explicit X11 window ID (skips xdotool search)")
    p_cap.add_argument("--prefix", default="shot", help="Output filename prefix")
    p_cap.add_argument("--interval", type=float, default=2.0, help="Seconds between grabs")
    p_cap.add_argument("--delay", type=float, default=3.0, help="Initial settle delay (sec)")
    p_cap.add_argument("--count", type=int, default=0, help="Max shots (0 = until exit/Ctrl+C)")
    p_cap.add_argument("--skip-black", action="store_true", help="Drop all-black frames")
    p_cap.add_argument("--skip-dup", action="store_true", help="Drop consecutive identical frames")
    p_cap.add_argument(
        "--no-x11-env",
        action="store_true",
        help="Do not auto-inject SDL_VIDEODRIVER=x11 when spawning under Wayland",
    )
    p_cap.add_argument(
        "command",
        nargs=argparse.REMAINDER,
        help="Optional command after '--' to spawn and auto-stop on exit",
    )
    p_cap.set_defaults(func=cmd_capture)

    p_ext = sub.add_parser(
        "extract",
        help="Extract sampled PNG frames from DOSBox-X / DOSBox-Staging ZMBV video(s).",
    )
    p_ext.add_argument("videos", nargs="+", help="Input .avi / .mp4 capture file(s)")
    p_ext.add_argument("--out", required=True, help="Output directory for extracted PNGs")
    p_ext.add_argument("--prefix", default="frame", help="Output filename prefix")
    p_ext.add_argument("--fps", type=float, default=0.5, help="Output sample rate (frames/sec)")
    p_ext.add_argument("--start", help="Optional start timestamp/seconds (-ss)")
    p_ext.add_argument("--duration", help="Optional duration seconds (-t)")
    p_ext.add_argument("--skip-black", action="store_true", help="Drop all-black frames")
    p_ext.add_argument("--skip-dup", action="store_true", help="Drop consecutive identical frames")
    p_ext.set_defaults(func=cmd_extract)

    p_cmp = sub.add_parser(
        "compare",
        help="Compare two images or directories (MAE, RMSE, PSNR, Diff%%, heatmap/montage).",
    )
    p_cmp.add_argument("reference", help="Baseline image file or directory (e.g. software)")
    p_cmp.add_argument("candidate", help="Candidate image file or directory (e.g. vulkan)")
    p_cmp.add_argument("--diff-dir", help="Write per-pair diff/montage PNGs to this dir")
    p_cmp.add_argument("--diff-out", help="Write diff/montage image for single-pair compare")
    p_cmp.add_argument(
        "--montage",
        action="store_true",
        help="Write side-by-side Reference | Candidate | Amplified Diff triptych",
    )
    p_cmp.add_argument("--gain", type=float, default=4.0, help="Diff heatmap gain (default 4.0)")
    p_cmp.add_argument(
        "--threshold",
        type=int,
        default=4,
        help="Per-channel diff threshold (0-255) for Diff%% (default 4)",
    )
    p_cmp.add_argument(
        "--resize",
        action="store_true",
        help="Resize candidate to reference geometry when dimensions differ",
    )
    p_cmp.add_argument(
        "--fail-below-psnr",
        type=float,
        help="Exit non-zero (2) if any pair falls below this PSNR (dB)",
    )
    p_cmp.add_argument("--json", help="Optional path to write JSON comparison report")
    p_cmp.set_defaults(func=cmd_compare)

    p_pack = sub.add_parser(
        "pack",
        help="Convert raw .ppm / .png captures into catalog-ready .webp or .png files.",
    )
    p_pack.add_argument("inputs", nargs="+", help="Input files or directories")
    p_pack.add_argument("--out", help="Output directory (default: alongside each input)")
    p_pack.add_argument("--format", choices=("webp", "png"), default="webp")
    p_pack.add_argument("--quality", type=int, default=85, help="Lossy WebP quality (0-100)")
    p_pack.add_argument("--lossless", action="store_true", help="Use lossless WebP encoding")
    p_pack.add_argument("--skip-black", action="store_true", help="Skip all-black frames")
    p_pack.set_defaults(func=cmd_pack)

    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
