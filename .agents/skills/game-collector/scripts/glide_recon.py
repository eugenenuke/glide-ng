#!/usr/bin/env python3
"""Read-only static binary reconnaissance for classic 3dfx Glide games.

Zero third-party dependencies (pure Python 3.8+ stdlib). Inspects a game install
directory, parses Win32 PE32/PE32+ import tables directly, classifies DOS/32
extenders and dynamic GLIDE2X.OVL vs. static sst1Init builds, detects bundled
conflicting OEM/wrapper DLLs, extracts Glide extension/env-var/CLI string hints,
and summarizes small bundled readmes/configs.

Writes the full Markdown report to --out and prints only a compact, line-capped
summary to stdout so low-quota AI agents never exhaust their token budget.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

BINARY_EXTS = {".exe", ".dll", ".dle", ".ovl", ".so", ".sys", ".vxd"}
CONFIG_EXTS = {".ini", ".cfg", ".conf", ".bat", ".cmd", ".txt", ".doc", ".bug", ".nfo", ".inf"}
CONFLICT_NAME_RE = re.compile(
    r"^(glide(2x|3x)?|sst1init|3dfx(gl|vgl|ogl|spl2|spl3)?|opengl32|"
    r"fxmemmap|nglide.*|dgvoodoo.*|zeckensack.*)\.(dll|ovl|vxd|sys)$",
    re.IGNORECASE,
)
MAX_HASH_BYTES = 32 * 1024 * 1024
MAX_STRING_SCAN_BYTES = 16 * 1024 * 1024
MAX_TEXT_READ_BYTES = 128 * 1024

SKIP_DIR_NAMES = {
    ".git",
    "dosdevices",
    "windows",
    "__pycache__",
    "node_modules",
}

DOS_EXTENDER_PATTERNS: Sequence[Tuple[bytes, str]] = (
    (b"DOS/4GW", "DOS/4GW"),
    (b"DOS/4G", "DOS/4G"),
    (b"CauseWay", "CauseWay"),
    (b"DOS/32A", "DOS/32A"),
    (b"PMODE/W", "PMODE/W"),
    (b"CWSDPMI", "DJGPP/CWSDPMI"),
    (b"GO32", "DJGPP/GO32"),
    (b"SOSDIGI", "HMI SOS"),
    (b"Rational Systems", "DOS/4G(W)"),
    (b"Tenberry", "DOS/4G(W)"),
    (b"Phar Lap", "Phar Lap"),
)

DYNAMIC_OVL_MARKERS = (b"GLIDE2X.OVL", b"glide2x.ovl", b"GLIDE.OVL", b"glide.ovl")
STATIC_SST_MARKERS = (
    b"sst1Init",
    b"sst1InitMapBoard",
    b"sst1InitRegisters",
    b"sst1InitShutdown",
    b"pciMapPhysicalToLinear",
    b"fxpci",
    b"pciOpen",
)

GLIDE_FEATURES_RE = re.compile(
    r"\b(?:gr[A-Z][A-Za-z0-9_]{2,35}|gu[A-Z][A-Za-z0-9_]{2,35}|"
    r"PIXEXT|COMBINE|TEXFMT|TEXUMA|FOGCOORD|TEXMIRROR|PALETTE6666|"
    r"CHROMARANGE|CHROMA_RANGE|TEXCHROMA|TEXTUREBUFFER|GR_HARDWARE|"
    r"GR_EXTENSION|GL_SGIS_multitexture)\b"
)
ENV_VAR_RE = re.compile(r"\b(?:FX_GLIDE|SST|SSTV2|SSTH3|GLIDE_NG|BUILD|N2FX)[A-Z0-9_]{2,32}\b")
REG_KEY_RE = re.compile(r"SOFTWARE\\[^\x00-\x1f\"']{3,80}", re.IGNORECASE)
CODEC_RE = re.compile(r"\b(?:binkw32|smackw32|mss32|wail32|quartz|indeo|iv50|iv41|cinepak|"
                      r"INTRO[-_A-Z0-9]*\.MVL|[A-Z0-9_]+\.(?:AVI|MPG|MPEG|BIK|SMK|MVL|MOV))\b",
                      re.IGNORECASE)
CLI_FLAG_RE = re.compile(
    r"(?:^|[\s\"'])(?:[-/](?:3dfx|glide|glide2|glide3|voodoo1?|voodoo2|d3d|soft(?:ware)?|"
    r"nosound|nomusic|nocd|nodemo|nojoy|window(?:ed)?|fullscreen|res\w*|hires|lores|"
    r"mem\w*|debug|safemode|width|height|joy))\b",
    re.IGNORECASE,
)
README_HINT_RE = re.compile(
    r"(?:version|patch|release|v[0-9]\.[0-9]|glide|3dfx|voodoo|hz|fps|frame ?rate|"
    r"ram|memory|mb|640x480|800x600|1024x768|nosound|known bug|unsupported|"
    r"warning|important|ems|xms|mouse|cockpit|cd[- ]?rom|audio|sound|irq|dma)",
    re.IGNORECASE,
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def parse_pe_imports(data: bytes) -> Optional[Dict[str, object]]:
    """Pure-Python minimal PE32/PE32+ header and import table parser."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    (pe_off,) = struct.unpack_from("<I", data, 0x3C)
    if pe_off + 24 > len(data) or data[pe_off : pe_off + 4] != b"PE\x00\x00":
        return None

    machine, num_sections, timestamp, _, _, opt_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, pe_off + 4
    )
    opt_off = pe_off + 24
    if opt_off + opt_size > len(data) or opt_size < 2:
        return None
    (magic,) = struct.unpack_from("<H", data, opt_off)
    is_pe32_plus = magic == 0x20B
    if magic not in (0x10B, 0x20B):
        return None

    dd_off = opt_off + (112 if is_pe32_plus else 96)
    num_rva_off = opt_off + (108 if is_pe32_plus else 92)
    if num_rva_off + 4 > len(data):
        return None
    (num_rva,) = struct.unpack_from("<I", data, num_rva_off)

    sec_off = opt_off + opt_size
    sections: List[Tuple[int, int, int, int, str]] = []
    for i in range(num_sections):
        off = sec_off + i * 40
        if off + 40 > len(data):
            break
        raw_name = data[off : off + 8].split(b"\x00", 1)[0].decode("latin-1", "replace")
        vsize, vaddr, raw_sz, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((vaddr, max(vsize, raw_sz), raw_ptr, raw_sz, raw_name))

    def rva_to_off(rva: int) -> Optional[int]:
        if rva == 0:
            return None
        for vaddr, span, raw_ptr, raw_sz, _ in sections:
            if vaddr <= rva < vaddr + span:
                delta = rva - vaddr
                if delta < raw_sz and raw_ptr + delta < len(data):
                    return raw_ptr + delta
                return None
        return None

    def read_cstr(off: Optional[int], limit: int = 256) -> str:
        if off is None or off < 0 or off >= len(data):
            return ""
        end = data.find(b"\x00", off, min(len(data), off + limit))
        if end < 0:
            end = min(len(data), off + limit)
        return data[off:end].decode("latin-1", "replace")

    imports: Dict[str, List[str]] = {}
    if num_rva >= 2 and dd_off + 16 <= len(data):
        imp_rva, _imp_sz = struct.unpack_from("<II", data, dd_off + 8)
        imp_off = rva_to_off(imp_rva)
        if imp_off is not None:
            idx = 0
            while imp_off + (idx + 1) * 20 <= len(data) and idx < 256:
                orig_first, _, _, name_rva, first_thunk = struct.unpack_from(
                    "<IIIII", data, imp_off + idx * 20
                )
                idx += 1
                if orig_first == 0 and name_rva == 0 and first_thunk == 0:
                    break
                dll_name = read_cstr(rva_to_off(name_rva))
                if not dll_name:
                    continue
                syms: List[str] = []
                thunk_rva = orig_first if orig_first else first_thunk
                thunk_off = rva_to_off(thunk_rva)
                step = 8 if is_pe32_plus else 4
                ord_mask = 1 << (63 if is_pe32_plus else 31)
                if thunk_off is not None:
                    for k in range(2048):
                        pos = thunk_off + k * step
                        if pos + step > len(data):
                            break
                        val = struct.unpack_from("<Q" if is_pe32_plus else "<I", data, pos)[0]
                        if val == 0:
                            break
                        if val & ord_mask:
                            syms.append(f"@{val & 0xFFFF}")
                        else:
                            hint_off = rva_to_off(val & 0x7FFFFFFF)
                            if hint_off is not None and hint_off + 2 < len(data):
                                sym = read_cstr(hint_off + 2)
                                if sym:
                                    syms.append(sym)
                imports[dll_name] = syms

    # Detect common packers by section names
    sec_names = [s[4] for s in sections]
    packers = [n for n in sec_names if n.upper().startswith(("UPX", "ASPACK", "SECDRV", ".STXT"))]

    return {
        "kind": "PE32+" if is_pe32_plus else "PE32",
        "is_dll": bool(characteristics & 0x2000),
        "machine": f"0x{machine:04x}",
        "timestamp": timestamp,
        "sections": sec_names,
        "packers": packers,
        "imports": imports,
    }


def classify_binary(path: Path) -> Dict[str, object]:
    size = path.stat().st_size
    info: Dict[str, object] = {
        "rel": str(path),
        "size": size,
        "format": "unknown",
        "glide_linkage": "none",
        "extenders": [],
        "imports": {},
        "glide_dlls": [],
        "glide_symbols": [],
        "dyn_glide_strings": [],
        "features": [],
        "env_vars": [],
        "cli_flags": [],
        "reg_keys": [],
        "codecs": [],
        "packers": [],
        "sha256": None,
    }

    with path.open("rb") as f:
        data = f.read(min(size, MAX_STRING_SCAN_BYTES))

    if size <= MAX_HASH_BYTES:
        info["sha256"] = sha256_file(path)

    pe = parse_pe_imports(data) if size <= MAX_STRING_SCAN_BYTES else None
    if pe is not None:
        info["format"] = f"{pe['kind']} {'DLL' if pe['is_dll'] else 'EXE'} ({pe['machine']})"
        info["packers"] = pe["packers"]
        imports: Dict[str, List[str]] = pe["imports"]  # type: ignore[assignment]
        info["imports"] = imports
        gfx_dlls = []
        glide_syms: List[str] = []
        for dll, syms in imports.items():
            dl = dll.lower()
            if any(k in dl for k in ("glide", "opengl", "ddraw", "d3d", "sst", "3dfx")):
                gfx_dlls.append(dll)
            if any(k in dl for k in ("glide", "3dfx", "sst")):
                glide_syms.extend(syms[:40])
        info["glide_dlls"] = gfx_dlls
        info["glide_symbols"] = sorted(set(glide_syms))[:30]
        if any(d.lower().startswith("glide3x") for d in imports):
            info["glide_linkage"] = "Win32 Glide 3.x (glide3x.dll)"
        elif any(d.lower().startswith("glide2x") for d in imports):
            info["glide_linkage"] = "Win32 Glide 2.x (glide2x.dll)"
        elif any(d.lower() in ("glide.dll",) for d in imports):
            info["glide_linkage"] = "Win32 Glide 2.11 (glide.dll)"
        elif any(d.lower() == "opengl32.dll" for d in imports):
            info["glide_linkage"] = "Win32 OpenGL / possible MiniGL consumer"
        elif any(d.lower() in ("ddraw.dll", "d3drm.dll", "d3dim.dll") for d in imports):
            info["glide_linkage"] = "Win32 DirectDraw/D3D (may LoadLibrary glide*.dll)"
    elif data[:2] == b"MZ":
        # Check NE/LE/LX stub header at 0x3C
        hdr = ""
        if len(data) >= 0x40:
            (lfanew,) = struct.unpack_from("<I", data, 0x3C)
            if lfanew + 2 <= len(data):
                sig = data[lfanew : lfanew + 2]
                if sig in (b"LE", b"LX", b"NE", b"BW"):
                    hdr = sig.decode("ascii")
        exts = [label for marker, label in DOS_EXTENDER_PATTERNS if marker in data]
        info["extenders"] = sorted(set(exts))
        info["format"] = f"DOS MZ{('/' + hdr) if hdr else ''}" + (
            f" ({', '.join(sorted(set(exts)))})" if exts else ""
        )
        has_dyn = any(m in data for m in DYNAMIC_OVL_MARKERS)
        has_sst = any(m in data for m in STATIC_SST_MARKERS)
        if has_dyn and not has_sst:
            info["glide_linkage"] = "DOS dynamic (GLIDE2X.OVL pass-through compatible)"
        elif has_dyn and has_sst:
            info["glide_linkage"] = "DOS mixed (references GLIDE2X.OVL AND sst1 markers — inspect!)"
        elif has_sst:
            info["glide_linkage"] = "DOS static SST-1 (sst1Init — CANNOT use GLIDE2X.OVL wrapper)"
    elif data[:4] == b"\x7fELF":
        info["format"] = "ELF binary"
    elif data[:2] in (b"LE", b"LX"):
        info["format"] = f"DOS linear {data[:2].decode('ascii')} (overlay/OVL)"

    # Scan printable ASCII runs (length >= 4)
    ascii_text = "\n".join(
        m.group(0).decode("ascii", "ignore")
        for m in re.finditer(rb"[\x20-\x7e]{4,256}", data)
    )

    dyn_names = sorted(
        set(
            re.findall(
                r"\b(?:glide(?:2x|3x)?\.(?:dll|ovl)|3dfx(?:gl|vgl|ogl)\.dll|"
                r"zglide\.dll|voodoo2?a\.dll|render3dfx\.dll|sst1init\.dll)\b",
                ascii_text,
                re.IGNORECASE,
            )
        )
    )
    info["dyn_glide_strings"] = dyn_names
    if info["glide_linkage"] == "none" and dyn_names:
        info["glide_linkage"] = f"dynamic string ref: {', '.join(dyn_names[:4])}"

    feats = sorted(set(GLIDE_FEATURES_RE.findall(ascii_text)))
    envs = sorted(set(ENV_VAR_RE.findall(ascii_text)))
    regs = sorted(set(REG_KEY_RE.findall(ascii_text)))[:12]
    codecs = sorted(set(CODEC_RE.findall(ascii_text)))[:12]
    flags = sorted({s.strip().strip("\"'") for s in CLI_FLAG_RE.findall(ascii_text)})[:20]

    # Also check UTF-16LE for GLIDE/REG strings if Win32 PE
    if pe is not None:
        u16_hits = [
            m.group(0).decode("utf-16-le", "ignore")
            for m in re.finditer(rb"(?:[\x20-\x7e]\x00){5,80}", data)
        ]
        u16_text = "\n".join(u16_hits)
        regs = sorted(set(regs) | set(REG_KEY_RE.findall(u16_text)))[:12]
        envs = sorted(set(envs) | set(ENV_VAR_RE.findall(u16_text)))

    info["features"] = feats[:40]
    info["env_vars"] = envs[:25]
    info["cli_flags"] = flags
    info["reg_keys"] = regs
    info["codecs"] = codecs
    return info


def walk_game_dir(root: Path, max_depth: int) -> Tuple[List[Path], List[Path], List[Path]]:
    binaries: List[Path] = []
    texts: List[Path] = []
    all_top: List[Path] = []
    base_depth = len(root.resolve().parts)

    for cur, dirnames, filenames in os.walk(root, followlinks=False):
        cp = Path(cur)
        depth = len(cp.resolve().parts) - base_depth
        dirnames[:] = sorted(
            d for d in dirnames if d.lower() not in SKIP_DIR_NAMES and not (cp / d).is_symlink()
        )
        if depth >= max_depth:
            dirnames[:] = []
        for fn in sorted(filenames):
            p = cp / fn
            if not p.is_file():
                continue
            if depth == 0:
                all_top.append(p)
            ext = p.suffix.lower()
            if ext in BINARY_EXTS:
                binaries.append(p)
            elif ext in CONFIG_EXTS:
                texts.append(p)
    return all_top, binaries, texts


def extract_readme_highlights(path: Path, max_lines: int = 12) -> List[str]:
    try:
        raw = path.read_bytes()[:MAX_TEXT_READ_BYTES]
    except OSError:
        return []
    text = raw.decode("cp437" if raw.count(b"\x00") == 0 else "latin-1", "replace")
    hits: List[str] = []
    for line in text.splitlines():
        s = line.strip()
        if 4 <= len(s) <= 140 and README_HINT_RE.search(s):
            hits.append(s)
            if len(hits) >= max_lines:
                break
    return hits


def render_markdown(
    root: Path,
    all_top: List[Path],
    bin_infos: List[Tuple[str, Dict[str, object]]],
    conflicts: List[Tuple[str, int]],
    texts: List[Path],
) -> str:
    out: List[str] = []
    out.append(f"# Static Binary Reconnaissance: `{root.name}`")
    out.append("")
    out.append(f"- **Scanned root (redacted name):** `{root.name}/`")
    out.append(f"- **Binaries inspected:** {len(bin_infos)}")
    out.append(f"- **Small config/readme files found:** {len(texts)}")
    out.append("")

    # Section 1: Conflicting OEM / wrapper DLLs
    out.append("## 1. Existing Glide / Wrapper DLLs & OVLs Found")
    if conflicts:
        out.append("| Relative Path | Size (bytes) |")
        out.append("| :--- | ---: |")
        for rel, sz in conflicts:
            out.append(f"| `{rel}` | {sz:,} |")
    else:
        out.append("_None found inside the scanned directory tree._")
    out.append("")

    # Section 2: Candidate executables / renderer DLLs
    out.append("## 2. Executables & Graphics / Renderer Binaries")
    if bin_infos:
        out.append("| File | Size (bytes) | Format | Glide Linkage | SHA-256 (prefix) |")
        out.append("| :--- | ---: | :--- | :--- | :--- |")
        for rel, info in bin_infos:
            sha = info["sha256"]
            sha_str = f"`{sha[:12]}`" if isinstance(sha, str) else "*(>32 MB)*"
            out.append(
                f"| `{rel}` | {info['size']:,} | {info['format']} | {info['glide_linkage']} | {sha_str} |"
            )
    else:
        out.append("_No executable/library files detected._")
    out.append("")

    # Section 3: Highlighted details for Glide-relevant binaries
    glide_bins = [
        (rel, info)
        for rel, info in bin_infos
        if info["glide_linkage"] != "none"
        or info["dyn_glide_strings"]
        or info["env_vars"]
        or info["packers"]
    ]
    out.append("## 3. Detailed Hints (Glide-Relevant Binaries)")
    if not glide_bins:
        out.append("_No obvious Glide references detected in scanned binaries._")
    for rel, info in glide_bins:
        out.append(f"### `{rel}` ({info['size']:,} B)")
        if info["sha256"]:
            out.append(f"- **SHA-256:** `{info['sha256']}`")
        if info["packers"]:
            out.append(f"- **WARNING — Possible packer sections:** `{', '.join(info['packers'])}`")
        if info["glide_dlls"]:
            out.append(f"- **Imported graphics DLLs:** `{', '.join(info['glide_dlls'])}`")
        if info["glide_symbols"]:
            out.append(f"- **Imported Glide symbols (sample):** `{', '.join(info['glide_symbols'][:20])}`")
        if info["dyn_glide_strings"]:
            out.append(f"- **Dynamic Glide/renderer strings:** `{', '.join(info['dyn_glide_strings'])}`")
        if info["features"]:
            out.append(f"- **Glide API/extension strings (sample):** `{', '.join(info['features'][:25])}`")
        if info["env_vars"]:
            out.append(f"- **Env var strings:** `{', '.join(info['env_vars'])}`")
        if info["cli_flags"]:
            out.append(f"- **CLI switch hints:** `{', '.join(info['cli_flags'])}`")
        if info["reg_keys"]:
            out.append(f"- **Registry key hints:** `{', '.join(info['reg_keys'][:8])}`")
        if info["codecs"]:
            out.append(f"- **Audio/FMV/codec hints:** `{', '.join(info['codecs'][:10])}`")
        out.append("")

    # Section 4: Root directory listing (names + sizes only)
    out.append("## 4. Top-Level File Inventory")
    out.append("| File | Size (bytes) |")
    out.append("| :--- | ---: |")
    for p in all_top[:100]:
        out.append(f"| `{p.name}` | {p.stat().st_size:,} |")
    if len(all_top) > 100:
        out.append(f"| _... (+{len(all_top) - 100} more files truncated)_ | |")
    out.append("")

    # Section 5: Key snippets from small text configs/readmes
    out.append("## 5. Readme / Config Excerpts (Lines Matching Keywords)")
    shown = 0
    for tp in texts:
        rel = tp.relative_to(root).as_posix()
        sz = tp.stat().st_size
        if sz == 0 or sz > MAX_TEXT_READ_BYTES:
            continue
        if tp.suffix.lower() in {".cfg", ".ini", ".conf"} and sz <= 4096:
            try:
                content = tp.read_bytes().decode("latin-1", "replace").strip()
            except OSError:
                continue
            lines = content.splitlines()[:25]
            if lines:
                out.append(f"### `{rel}` ({sz:,} B)")
                out.append("```ini")
                out.extend(lines)
                out.append("```")
                shown += 1
        else:
            hits = extract_readme_highlights(tp, max_lines=10)
            if hits:
                out.append(f"### `{rel}` ({sz:,} B)")
                for h in hits:
                    out.append(f"- {h}")
                shown += 1
        if shown >= 15:
            out.append(f"\n_Additional text files truncated after {shown} entries._")
            break
    if shown == 0:
        out.append("_No readable text/config files matched._")
    out.append("")
    return "\n".join(out)


def render_compact_digest(
    root: Path,
    bin_infos: List[Tuple[str, Dict[str, object]]],
    conflicts: List[Tuple[str, int]],
    out_path: Optional[Path],
    max_lines: int,
) -> str:
    lines: List[str] = []
    lines.append(f"=== glide_recon digest: {root.name} ({len(bin_infos)} binaries) ===")
    if out_path is not None:
        lines.append(f"Full report saved -> {out_path}")
    if conflicts:
        lines.append("Bundled Glide/OEM files detected:")
        for rel, sz in conflicts[:10]:
            lines.append(f"  [!] {rel} ({sz:,} B)")
    else:
        lines.append("Bundled Glide/OEM files: none")

    lines.append("Primary candidate binaries:")
    shown = 0
    for rel, info in bin_infos:
        link = str(info["glide_linkage"])
        if link != "none" or rel.lower().endswith((".exe", ".ovl")):
            sha = info["sha256"]
            sha_s = sha[:10] if isinstance(sha, str) else "-"
            lines.append(f"  - {rel} ({info['size']:,} B) | {info['format']} | {link} | sha:{sha_s}")
            if info["env_vars"]:
                lines.append(f"      env: {', '.join(info['env_vars'][:8])}")
            if info["dyn_glide_strings"]:
                lines.append(f"      refs: {', '.join(info['dyn_glide_strings'][:6])}")
            if info["packers"]:
                lines.append(f"      WARN packer sections: {', '.join(info['packers'])}")
            shown += 1
            if len(lines) >= max_lines - 2:
                lines.append(f"  ... (truncated at --max-stdout={max_lines}; see full report)")
                break
    if shown == 0:
        lines.append("  (none matched; inspect full report)")
    return "\n".join(lines[:max_lines])


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Read-only static binary recon for 3dfx Glide games (stdlib only)."
    )
    ap.add_argument("game_dir", type=Path, help="Path to the installed game directory")
    ap.add_argument(
        "--out", "-o", type=Path, default=None, help="Path to write full Markdown report"
    )
    ap.add_argument(
        "--max-depth", type=int, default=4, help="Maximum subdirectory recursion depth (default: 4)"
    )
    ap.add_argument(
        "--max-stdout",
        type=int,
        default=60,
        help="Cap stdout lines to keep AI agent context small (default: 60)",
    )
    ap.add_argument(
        "--full-stdout",
        action="store_true",
        help="Print full Markdown report to stdout instead of the compact digest",
    )
    args = ap.parse_args(argv)

    root = args.game_dir.expanduser()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2

    all_top, binaries, texts = walk_game_dir(root, max_depth=args.max_depth)
    conflicts: List[Tuple[str, int]] = []
    bin_infos: List[Tuple[str, Dict[str, object]]] = []
    for p in binaries:
        rel = p.relative_to(root).as_posix()
        if CONFLICT_NAME_RE.match(p.name):
            conflicts.append((rel, p.stat().st_size))
        bin_infos.append((rel, classify_binary(p)))

    md = render_markdown(root, all_top, bin_infos, conflicts, texts)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(md, encoding="utf-8")

    if args.full_stdout:
        print(md)
    else:
        print(render_compact_digest(root, bin_infos, conflicts, args.out, args.max_stdout))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
