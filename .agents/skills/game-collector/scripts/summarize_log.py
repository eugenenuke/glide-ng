#!/usr/bin/env python3
"""Compact summarizer for glide-ng.log and GLIDE_NG_PERF_LOG CSV files.

Extracts the startup profile banner (verifying Wrapper Build & Loaded Module),
deduplicates [WARN]/[CRIT] lines with counts and frame indices, prints resolution/
gamma/swap stats, and summarizes per-frame perf CSV telemetry (p50/p95/p99 + top
spike frames so users know exact GLIDE_NG_TRACE_FRAMES windows).
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

LOG_LINE_RE = re.compile(
    r"^\[\+([0-9]+\.[0-9]+)\]\s+\[F([0-9]+)\]\s+\[([A-Z]{4})\]\s+\[([^\]]+)\]\s*(.*)$"
)
BANNER_KEYS = (
    "Wrapper Build",
    "Loaded Module",
    "Emulated Accelerator",
    "Config File",
    "Target Presentation",
    "MSAA / Scale / Filter",
    "Active Emulation",
    "Reported API Base",
    "HUD Overlay",
    "Software SIMD",
)
RES_RE = re.compile(r"\b(\d{3,4}x\d{3,4})\b")
GAMMA_RE = re.compile(r"gamma[^0-9]*([0-9]+\.[0-9]+)", re.IGNORECASE)


def parse_log(path: Path) -> Dict[str, object]:
    banner: Dict[str, str] = {}
    warns: Counter[str] = Counter()
    crits: Counter[str] = Counter()
    warn_frames: Dict[str, Tuple[int, int]] = {}
    crit_frames: Dict[str, Tuple[int, int]] = {}
    max_frame = 0
    last_time = ""
    total_lines = 0
    resolutions: List[str] = []
    gamma_vals: List[str] = []
    tail: List[str] = []

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            total_lines += 1
            line = raw.rstrip("\r\n")
            if not line:
                continue
            tail.append(line)
            if len(tail) > 12:
                tail.pop(0)

            for key in BANNER_KEYS:
                if key not in banner and f"  {key}" in line and ":" in line:
                    _, val = line.split(":", 1)
                    banner[key] = val.strip()

            m = LOG_LINE_RE.match(line)
            if m:
                ts, frame_s, level, comp, msg = m.groups()
                frame = int(frame_s)
                if frame > max_frame:
                    max_frame = frame
                last_time = ts
                norm = f"[{comp}] {msg[:180]}"
                if level == "WARN":
                    warns[norm] += 1
                    lo, hi = warn_frames.get(norm, (frame, frame))
                    warn_frames[norm] = (min(lo, frame), max(hi, frame))
                elif level == "CRIT":
                    crits[norm] += 1
                    lo, hi = crit_frames.get(norm, (frame, frame))
                    crit_frames[norm] = (min(lo, frame), max(hi, frame))
                if any(
                    k in msg.lower()
                    for k in ("attach", "resolution", "grsstwinopen", "res=", "swapchain", "drawable", "viewport")
                ):
                    for rm in RES_RE.findall(msg):
                        if rm not in resolutions:
                            resolutions.append(rm)
                if "gamma" in msg.lower() and ("value" in msg.lower() or "correction" in msg.lower()):
                    gm = GAMMA_RE.search(msg)
                    if gm and gm.group(1) not in gamma_vals:
                        gamma_vals.append(gm.group(1))
            elif line.startswith("[glide-ng] WARN:"):
                warns[line[:180]] += 1
            elif line.startswith("Info:") and "Drawable Size:" in line:
                for rm in RES_RE.findall(line):
                    if rm not in resolutions:
                        resolutions.append(rm)

    return {
        "file": path.name,
        "lines": total_lines,
        "banner": banner,
        "max_frame": max_frame,
        "last_time_s": last_time,
        "resolutions": resolutions[:6],
        "gamma": gamma_vals[:6],
        "crits": crits,
        "warns": warns,
        "crit_frames": crit_frames,
        "warn_frames": warn_frames,
        "tail": tail,
    }


def percentile(vals: List[float], p: float) -> float:
    if not vals:
        return 0.0
    s = sorted(vals)
    idx = min(len(s) - 1, max(0, int(round((p / 100.0) * (len(s) - 1)))))
    return s[idx]


def parse_perf_csv(path: Path) -> str:
    rows: List[Dict[str, str]] = []
    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    if not rows:
        return f"PERF CSV `{path.name}`: empty (0 data rows)."

    def col_floats(names: Sequence[str]) -> List[float]:
        for name in names:
            if name in rows[0]:
                out = []
                for r in rows:
                    try:
                        out.append(float(r[name]))
                    except (ValueError, TypeError):
                        pass
                return out
        return []

    frame_ms = col_floats(("frame_ms", "frame_time_ms", "total_ms"))
    fps = col_floats(("fps", "inst_fps"))
    wrap_ms = col_floats(("wrapper_cpu_ms", "render_ms"))
    app_ms = col_floats(("app_cpu_ms",))
    pso = col_floats(("pso_builds", "vk_pso_builds"))
    lfb_r = col_floats(("lfb_reads",))
    lfb_w = col_floats(("lfb_writes",))
    tex_up = col_floats(("tex_uploads",))

    lines = [f"### Perf CSV: `{path.name}` ({len(rows)} frames)"]
    if frame_ms:
        lines.append(
            f"- **frame_ms:** min={min(frame_ms):.2f} | p50={percentile(frame_ms, 50):.2f} | "
            f"p95={percentile(frame_ms, 95):.2f} | p99={percentile(frame_ms, 99):.2f} | "
            f"max={max(frame_ms):.2f}"
        )
    if fps:
        lines.append(
            f"- **fps:** min={min(fps):.1f} | p50={percentile(fps, 50):.1f} | max={max(fps):.1f}"
        )
    if wrap_ms or app_ms:
        w_avg = (sum(wrap_ms) / len(wrap_ms)) if wrap_ms else 0.0
        a_avg = (sum(app_ms) / len(app_ms)) if app_ms else 0.0
        lines.append(f"- **avg CPU split:** wrapper={w_avg:.2f} ms | app={a_avg:.2f} ms")
    if pso or lfb_r or lfb_w or tex_up:
        lines.append(
            f"- **totals:** pso_builds={int(sum(pso))} | lfb_reads={int(sum(lfb_r))} | "
            f"lfb_writes={int(sum(lfb_w))} | tex_uploads={int(sum(tex_up))}"
        )

    frame_key = "frame" if "frame" in rows[0] else None
    ms_key = next((k for k in ("frame_ms", "frame_time_ms", "total_ms") if k in rows[0]), None)
    if ms_key:
        indexed = []
        for i, r in enumerate(rows):
            try:
                ms = float(r[ms_key])
            except (ValueError, TypeError):
                continue
            fid = r[frame_key] if frame_key else str(i)
            indexed.append((ms, fid))
        indexed.sort(reverse=True)
        top = ", ".join(f"F{fid}={ms:.1f}ms" for ms, fid in indexed[:5])
        lines.append(f"- **top 5 spike frames:** {top}")
    return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Summarize glide-ng.log and optional perf CSV files concisely."
    )
    ap.add_argument("files", nargs="+", type=Path, help="Log (.log) and/or perf (.csv) files")
    ap.add_argument("--tail", type=int, default=6, help="Tail lines shown per log with errors (default: 6)")
    ap.add_argument("--max-warns", type=int, default=12, help="Max unique WARN/CRIT lines per file")
    ap.add_argument("--out", "-o", type=Path, default=None, help="Optional Markdown file output")
    args = ap.parse_args(argv)

    logs: List[Dict[str, object]] = []
    csv_blocks: List[str] = []
    for p in args.files:
        if not p.is_file():
            print(f"warning: skipping missing file: {p}", file=sys.stderr)
            continue
        if p.suffix.lower() == ".csv":
            csv_blocks.append(parse_perf_csv(p))
        else:
            logs.append(parse_log(p))

    out: List[str] = []
    if logs:
        out.append("## Log Matrix Summary")
        out.append("| File | Build | Backend | Card | Frames | Duration | CRIT | WARN | Res |")
        out.append("| :--- | :--- | :--- | :--- | ---: | ---: | ---: | ---: | :--- |")
        builds_seen = set()
        for d in logs:
            b: Dict[str, str] = d["banner"]  # type: ignore[assignment]
            build = b.get("Wrapper Build", "?").split("(", 1)[0].strip()
            builds_seen.add(build)
            em = b.get("Active Emulation", "?")
            card = b.get("Emulated Accelerator", "?")
            cc = sum(d["crits"].values())  # type: ignore[attr-defined]
            wc = sum(d["warns"].values())  # type: ignore[attr-defined]
            res = ", ".join(d["resolutions"]) or "-"  # type: ignore[arg-type]
            try:
                dur = f"{float(d['last_time_s']):.2f}s" if d["last_time_s"] else "-"  # type: ignore[arg-type]
            except ValueError:
                dur = f"{d['last_time_s']}s"
            out.append(
                f"| `{d['file']}` | `{build}` | {em} | {card} | {d['max_frame']} | {dur} | {cc} | {wc} | {res} |"
            )
        if len(builds_seen) > 1:
            out.append(
                f"\n> **NOTE:** Multiple `Wrapper Build` versions detected across logs: `{', '.join(sorted(builds_seen))}`."
            )
        out.append("")

        for d in logs:
            b = d["banner"]  # type: ignore[assignment]
            crits: Counter[str] = d["crits"]  # type: ignore[assignment]
            warns: Counter[str] = d["warns"]  # type: ignore[assignment]
            out.append(f"### `{d['file']}` ({d['lines']} lines)")
            if "Loaded Module" in b:
                out.append(f"- **Loaded Module:** `{b['Loaded Module']}`")
            if "Config File" in b:
                out.append(f"- **Config File:** `{b['Config File']}`")
            if "Target Presentation" in b:
                out.append(f"- **Presentation:** `{b['Target Presentation']}` / MSAA `{b.get('MSAA / Scale / Filter', '?')}`")
            if d["gamma"]:
                out.append(f"- **Gamma values logged:** `{', '.join(d['gamma'])}`")  # type: ignore[arg-type]
            if crits:
                out.append("- **Unique `[CRIT]` lines:**")
                cf: Dict[str, Tuple[int, int]] = d["crit_frames"]  # type: ignore[assignment]
                for msg, cnt in crits.most_common(args.max_warns):
                    lo, hi = cf.get(msg, (0, 0))
                    out.append(f"  - `x{cnt}` (F{lo}–F{hi}): `{msg}`")
                if len(crits) > args.max_warns:
                    out.append(f"  - _... +{len(crits) - args.max_warns} more unique (raise `--max-warns`)_")
            if warns:
                out.append("- **Unique `[WARN]` lines:**")
                wf: Dict[str, Tuple[int, int]] = d["warn_frames"]  # type: ignore[assignment]
                for msg, cnt in warns.most_common(args.max_warns):
                    lo, hi = wf.get(msg, (0, 0))
                    out.append(f"  - `x{cnt}` (F{lo}–F{hi}): `{msg}`")
                if len(warns) > args.max_warns:
                    out.append(f"  - _... +{len(warns) - args.max_warns} more unique (raise `--max-warns`)_")
            if not crits and not warns:
                out.append("- **Errors / Warnings:** `0 CRIT, 0 WARN`")
            if (crits or warns) and args.tail > 0:
                tail: List[str] = d["tail"]  # type: ignore[assignment]
                out.append(f"- **Last {min(args.tail, len(tail))} lines:**")
                out.append("  ```text")
                for ln in tail[-args.tail :]:
                    out.append(f"  {ln[:200]}")
                out.append("  ```")
            out.append("")

    if csv_blocks:
        out.append("## Performance Telemetry")
        out.extend(csv_blocks)
        out.append("")

    text = "\n".join(out).rstrip() + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
