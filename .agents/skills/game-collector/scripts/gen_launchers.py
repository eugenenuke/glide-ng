#!/usr/bin/env python3
"""Generate per-backend wrapper launcher scripts for clean, isolated log capture.

Never overwrites glide-ng.log between backend runs: each launcher writes to its
own distinct file inside <bundle>/logs/ so a user can quickly click through
Vulkan 1x, Vulkan 4x MSAA, OpenGL ES/WGL, Software, and debug/trace capture.
"""

from __future__ import annotations

import argparse
import os
import shlex
import stat
from pathlib import Path
from typing import Optional, Sequence


def make_exec(path: Path) -> None:
    if os.name == "posix":
        mode = path.stat().st_mode
        path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_posix_launchers(
    out_dir: Path,
    platform: str,
    cmd: str,
    dll_override: str,
    card_alt: str,
    wine_desktop: str,
) -> None:
    logs_dir = out_dir / "logs"
    screens_dir = out_dir / "screens"
    logs_dir.mkdir(parents=True, exist_ok=True)
    screens_dir.mkdir(parents=True, exist_ok=True)

    header = "#!/usr/bin/env bash\nset -euo pipefail\nBUNDLE_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\nmkdir -p \"$BUNDLE_DIR/logs\" \"$BUNDLE_DIR/screens\"\n"
    extra_env = ""
    wine_wrap = cmd
    if platform == "wine":
        extra_env = f'export WINEDLLOVERRIDES={shlex.quote(dll_override)}\n'
        if wine_desktop:
            wine_wrap = f"wine explorer /desktop=GlideTest,{wine_desktop} {cmd}"
        elif not cmd.lstrip().startswith("wine"):
            wine_wrap = f"wine {cmd}"

    variants = [
        ("run_01_vk_1x.sh", "vulkan", "", "1", "nearest", "vk_1x", "info", True),
        ("run_02_vk_hq.sh", "vulkan", "", "4", "linear", "vk_hq", "info", False),
        ("run_03_gles.sh", "opengl_es", "", "1", "nearest", "gles", "info", False),
        ("run_04_sw.sh", "software", "", "1", "nearest", "sw", "info", False),
        (f"run_05_{card_alt}.sh", "vulkan", card_alt, "1", "nearest", f"alt_{card_alt}", "info", False),
    ]

    for fname, backend, card, msaa, pfilt, stem, level, with_perf in variants:
        log_path = f'$BUNDLE_DIR/logs/{stem}.log'
        body = [header, extra_env]
        body.append(f'export GLIDE_NG_BACKEND={shlex.quote(backend)}\n')
        if card:
            body.append(f'export GLIDE_NG_CARD_MODEL={shlex.quote(card)}\n')
        body.append(f'export GLIDE_NG_MSAA={msaa}\n')
        body.append(f'export GLIDE_NG_PRESENTATION_FILTER={pfilt}\n')
        if stem == "vk_hq":
            body.append("export FX_GLIDE_NO_DITHER_SUB=1\n")
        body.append(f'export GLIDE_NG_LOG_LEVEL={level}\n')
        if platform == "wine":
            body.append(f'export GLIDE_NG_LOG_FILE="Z:${{BUNDLE_DIR}}/logs/{stem}.log"\n')
            body.append(f'# (On some Wine setups prefer copying ./glide-ng.log after exit)\n')
        else:
            body.append(f'export GLIDE_NG_LOG_FILE="{log_path}"\n')
        if with_perf and platform != "wine":
            body.append(f'export GLIDE_NG_PERF_LOG="$BUNDLE_DIR/logs/{stem}_perf.csv"\n')
        body.append(f'echo "[glide-collector] Launching ({stem}) -> logs/{stem}.log"\n')
        body.append(f'{wine_wrap} "$@"\n')
        p = out_dir / fname
        p.write_text("".join(body), encoding="utf-8")
        make_exec(p)

    # Debug/crash/hang capture launcher (console mirrored + stdbuf flushed via tee)
    dbg = [
        header,
        extra_env,
        'export GLIDE_NG_BACKEND="${GLIDE_NG_BACKEND:-vulkan}"\n',
        'export GLIDE_NG_LOG_LEVEL="${GLIDE_NG_LOG_LEVEL:-debug}"\n',
        'export GLIDE_NG_LOG_CONSOLE=1\n',
        'STAMP="$(date +%Y%m%d_%H%M%S)"\n',
        'LOG_BASE="$BUNDLE_DIR/logs/debug_${STAMP}"\n',
    ]
    if platform == "wine":
        dbg.append('export WINEDEBUG="${WINEDEBUG:-err+all,fixme-all,+seh,+loaddll}"\n')
        dbg.append('export GLIDE_NG_LOG_FILE="Z:${LOG_BASE}_glide.log"\n')
    else:
        dbg.append('export GLIDE_NG_LOG_FILE="${LOG_BASE}_glide.log"\n')
    dbg.append('echo "[glide-collector] Debug run -> ${LOG_BASE}_{glide,console}.log"\n')
    dbg.append(f'stdbuf -oL -eL {wine_wrap} "$@" 2>&1 | tee "${{LOG_BASE}}_console.log"\n')
    dp = out_dir / "run_debug_capture.sh"
    dp.write_text("".join(dbg), encoding="utf-8")
    make_exec(dp)


def write_windows_launchers(out_dir: Path, cmd: str, card_alt: str) -> None:
    (out_dir / "logs").mkdir(parents=True, exist_ok=True)
    (out_dir / "screens").mkdir(parents=True, exist_ok=True)

    variants = [
        ("run_01_vk_1x.ps1", "vulkan", "", "1", "nearest", "vk_1x", True),
        ("run_02_vk_hq.ps1", "vulkan", "", "4", "linear", "vk_hq", False),
        ("run_03_gles.ps1", "opengl_es", "", "1", "nearest", "gles", False),
        ("run_04_sw.ps1", "software", "", "1", "nearest", "sw", False),
        (f"run_05_{card_alt}.ps1", "vulkan", card_alt, "1", "nearest", f"alt_{card_alt}", False),
    ]
    for fname, backend, card, msaa, pfilt, stem, with_perf in variants:
        lines = [
            "$ErrorActionPreference = 'Stop'",
            "$Bundle = Split-Path -Parent $MyInvocation.MyCommand.Path",
            "New-Item -ItemType Directory -Force -Path \"$Bundle\\logs\", \"$Bundle\\screens\" | Out-Null",
            f"$env:GLIDE_NG_BACKEND = '{backend}'",
            f"$env:GLIDE_NG_MSAA = '{msaa}'",
            f"$env:GLIDE_NG_PRESENTATION_FILTER = '{pfilt}'",
            "$env:GLIDE_NG_LOG_LEVEL = 'info'",
            f"$env:GLIDE_NG_LOG_FILE = \"$Bundle\\logs\\{stem}.log\"",
        ]
        if card:
            lines.append(f"$env:GLIDE_NG_CARD_MODEL = '{card}'")
        if stem == "vk_hq":
            lines.append("$env:FX_GLIDE_NO_DITHER_SUB = '1'")
        if with_perf:
            lines.append(f"$env:GLIDE_NG_PERF_LOG = \"$Bundle\\logs\\{stem}_perf.csv\"")
        lines.append(f"Write-Host '[glide-collector] Launching {stem} -> logs\\{stem}.log'")
        lines.append(cmd)
        (out_dir / fname).write_text("\n".join(lines) + "\n", encoding="utf-8")

    dbg = "\n".join([
        "$Bundle = Split-Path -Parent $MyInvocation.MyCommand.Path",
        "New-Item -ItemType Directory -Force -Path \"$Bundle\\logs\", \"$Bundle\\screens\" | Out-Null",
        "$Stamp = Get-Date -Format 'yyyyMMdd_HHmmss'",
        "if (-not $env:GLIDE_NG_BACKEND) { $env:GLIDE_NG_BACKEND = 'vulkan' }",
        "$env:GLIDE_NG_LOG_LEVEL = 'debug'",
        "$env:GLIDE_NG_LOG_CONSOLE = '1'",
        "$env:GLIDE_NG_LOG_FILE = \"$Bundle\\logs\\debug_${Stamp}_glide.log\"",
        f"Write-Host \"[glide-collector] Debug run -> logs\\debug_${{Stamp}}_glide.log\"",
        f"{cmd} *>&1 | Tee-Object -FilePath \"$Bundle\\logs\\debug_${{Stamp}}_console.log\"",
    ]) + "\n"
    (out_dir / "run_debug_capture.ps1").write_text(dbg, encoding="utf-8")


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Emit isolated per-backend test launcher scripts.")
    ap.add_argument("--out-dir", "-o", type=Path, required=True, help="Bundle output directory")
    ap.add_argument(
        "--platform",
        choices=("dosbox", "wine", "windows"),
        required=True,
        help="Target runner type",
    )
    ap.add_argument(
        "--cmd",
        required=True,
        help="Base launch command (e.g. 'dosbox -conf dosbox.conf' or './GAME.EXE')",
    )
    ap.add_argument(
        "--dll-override",
        default="glide,glide2x,glide3x=n",
        help="WINEDLLOVERRIDES string for --platform wine",
    )
    ap.add_argument(
        "--alt-card",
        default="voodoo1",
        help="Alternate card_model script name (e.g. voodoo1, voodoo3, voodoo5)",
    )
    ap.add_argument(
        "--wine-desktop",
        default="",
        help="Optional virtual desktop size for Wine (e.g. 640x480 or 800x600)",
    )
    args = ap.parse_args(argv)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.platform == "windows":
        write_windows_launchers(args.out_dir, args.cmd, args.alt_card)
    else:
        write_posix_launchers(
            args.out_dir,
            args.platform,
            args.cmd,
            args.dll_override,
            args.alt_card,
            args.wine_desktop,
        )
    print(f"Generated 6 launcher scripts in {args.out_dir}/ (plus logs/ and screens/ folders).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
