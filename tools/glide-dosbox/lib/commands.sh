# Standalone commands (help, init, doctor, completion install) for glide-dosbox.

usage() {
  cat <<'EOF'
Usage:
  glide-dosbox [ACTION] [PROFILE] [OPTIONS...] [--] [extra dosbox args...]
  glide-dosbox init [--cdimg=PATH] [--patch-dir=DIR] [--catalog=SLUG] [--mode=PROFILE] [--force] [--no-symlink]
  glide-dosbox doctor
  glide-dosbox --install-completion

Actions:
  run                      Launch game via RUN_CMD (default action)
  shell, sh, dos, exec     Open interactive DOSBox prompt (uses SHELL_CMD; empty () by default; never runs RUN_CMD)
  install                  Run INST_CMD (D: if CDIMG set), post_install(), then auto-patch (unless --no-patch)
  patch                    Run pre_patch(), PATCH_CMD (mounts P: if PATCH_DIR set), and post_patch()
  setup, conf, reconfigure Run CONF_CMD using admin profile
  init                     Create starter ./game.sh (+ ./run.sh symlink, optional ./glide-ng.ini)
  doctor, info             Check binaries, OVL/shared libraries, GPU ICDs, and ./game.sh health

Profiles & Orthogonal Overrides:
  gng                      DOSBox-SVN + glide-ng (default unless DEFAULT_MODE overridden)
  og                       DOSBox-SVN + OpenGLide
  dbx                      DOSBox-X + glide-ng (defaults SDL_VIDEODRIVER=x11)
  dbx-og                   DOSBox-X + OpenGLide
  sw, dbx-sw               DOSBox-X built-in software Voodoo (no glide-ng)
  st, staging              DOSBox Staging built-in software Voodoo (no glide-ng)
  --emu=EMU                Override emulator (svn | dbx | staging) — e.g. `run.sh og --emu=dbx`
  --wrapper=WRAP           Override Glide target (gng | og | hw)

GPU & Backend Selection:
  --vk, --vulkan           Force glide-ng Vulkan backend
  --gl, --gles             Force glide-ng OpenGL ES backend
  --sw, --software         Force glide-ng Software CPU backend
  --backend=BACKEND        Explicit glide-ng backend (vk | gl | sw)
  --gpu=GPU                Select host GPU (default | discrete | integrated | llvmpipe | name:<substr>)

Execution & Shell Control:
  --variant=NAME           Select per-game runtime variant from RUN_CMD_<NAME> (`--flavor=`, `--flavour=`)
  --list-variants          List runtime variants defined in ./game.sh and exit
  --exec CMD               Replace DOS commands with `-c CMD` (repeatable)
  --no-exit                Strip trailing `-c exit` so DOSBox stays open after commands finish
  --cd                     In `shell` (or `--exec`), start on D: when CDIMG is set
  --no-patch               In `install`, skip automatic `PATCH_CMD` / patch hook chaining
  --extract                In `patch`, extract PATCH_ARCHIVE / PATCH_ARCHIVES into PATCH_DEST (default .)
  --game-dir=DIR, -C DIR   Run against game directory DIR instead of invoked symlink/CWD

Debug Scenarios (--debug[=SCENARIO], writes under ./logs/):
  --debug, --debug=1, --debug=perf
                           Full HUD + per-frame perf CSV + info log
  --debug=2, --debug=diag, --debug=debug
                           Perf scenario + debug logging + console output
  --debug=3[:A-B], --debug=trace[:A-B]
                           Perf scenario + trace on api,lfb,tex,perf (default frames 100-105)
  --debug=4, --debug=visual, --debug=snap
                           Perf scenario + periodic FX_GLIDE_SNAPSHOT (./logs/shots_<tag>/@60+60:15)
  --debug=wsi              WSI/general debug log + console output + windowed mode

Everyday Overrides (take precedence over game.sh and glide-ng.ini):
  --hud[=SPEC]             Enable HUD overlay (default: full)
  --perf[=FILE]            Record per-frame perf CSV (default: ./logs/perf_<tag>.csv)
  --log[=LEVEL]            Set log level (default: debug) and default ./logs/<tag>.log
  --log-file=FILE          Override glide-ng log file path
  --tag=SUFFIX             Append custom suffix to auto-generated log/perf/shot filenames (`--suffix=`)
  --console                Mirror glide-ng log output to stdout
  --trace=A-B              Trace api,lfb,tex,perf for frame range A-B (implies level=trace)
  --snap=SPEC              Set FX_GLIDE_SNAPSHOT (e.g. shots/@60+60:15)
  -w, --windowed           Force windowed mode via configs/dosbox-windowed.conf
  --uncap                  Disable VSync and FPS cap (GLIDE_NG_VSYNC=0, GLIDE_NG_MAX_FPS=0)
  --msaa=N                 Override MSAA sample count (1, 2, 4, 8, 16)
  --no-splash              Skip automatic 3dfx startup splash animation
  --card=MODEL             Override card_model (VoodooGraphics, Voodoo2, Voodoo3, Voodoo5, ...)
  --tmu-mem=MB             Override per-TMU RAM in MB (GLIDE_NG_TMU_MEM_MB)
  --dry-run                Print resolved command and environment without launching DOSBox

Setup / Help:
  --install-completion     Install Bash completions for glide-dosbox, run.sh, and launch.sh
  -h, --help               Show this help message

Configuration Paths & Notes:
  - Host config file: ${XDG_CONFIG_HOME:-$HOME/.config}/glide-ng/dosbox-launcher.conf (override: GLIDE_DOSBOX_CONF).
    See templates/launcher.conf.example; auto-discovers repo build_release/ or build/ outputs when omitted.
  - Per-game files: ./game.sh, optional ./glide-ng.ini, optional ./dosbox-override.conf.
  - Extra DOSBox flags after action/profile or `--` are forwarded verbatim.

Examples:
  ./run.sh --vk --debug=1
  ./run.sh --variant=hires
  ./run.sh og --emu=dbx
  ./run.sh shell --cd
  ./run.sh --exec "dir /w" --no-exit
  ./run.sh --vk --gpu=discrete -w
EOF
}

resolve_completion_target() {
  if [[ -f "${INVOKED_TOOL_DIR}/completions/glide-dosbox.bash" ]]; then
    echo "${INVOKED_TOOL_DIR}/completions/glide-dosbox.bash"
    return 0
  fi
  if [[ -L "$INVOKED_SCRIPT" ]]; then
    local link_dest link_dir
    link_dest="$(readlink "$INVOKED_SCRIPT")"
    if [[ "$link_dest" != /* ]]; then
      link_dest="$(cd "$(dirname "$INVOKED_SCRIPT")" && pwd)/$link_dest"
    fi
    link_dir="$(dirname "$link_dest")"
    if [[ -f "${link_dir}/completions/glide-dosbox.bash" ]]; then
      echo "${link_dir}/completions/glide-dosbox.bash"
      return 0
    fi
  fi
  echo "${TOOL_DIR}/completions/glide-dosbox.bash"
}

install_completion() {
  local comp_dir="${XDG_DATA_HOME:-$HOME/.local/share}/bash-completion/completions"
  local target
  target="$(resolve_completion_target)"
  mkdir -p "$comp_dir"
  local name
  for name in glide-dosbox run.sh launch.sh; do
    ln -sf "$target" "${comp_dir}/${name}"
  done
  echo "Installed Bash completion symlinks in ${comp_dir}:"
  echo "  glide-dosbox -> ${target}"
  echo "  run.sh       -> ${target}"
  echo "  launch.sh    -> ${target}"
  echo "Activates automatically via bash-completion (open a new shell or re-source completion)."
}

escape_dquote() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  s="${s//\$/\\\$}"
  echo "$s"
}

cmd_init() {
  local cdimg="" patch_dir="" catalog="" mode="" force=0 no_symlink=0
  local arg
  for arg in "$@"; do
    case "$arg" in
      --cdimg=*) cdimg="${arg#*=}" ;;
      --patch-dir=*) patch_dir="${arg#*=}" ;;
      --catalog=*) catalog="${arg#*=}" ;;
      --mode=*) mode="${arg#*=}" ;;
      --force) force=1 ;;
      --no-symlink) no_symlink=1 ;;
      *)
        echo "Error: unknown init option '$arg'." >&2
        echo "Usage: glide-dosbox init [--cdimg=PATH] [--patch-dir=DIR] [--catalog=SLUG] [--mode=PROFILE] [--force] [--no-symlink]" >&2
        exit 1
        ;;
    esac
  done

  if [[ -n "$mode" ]] && ! is_known_profile "$mode"; then
    echo "Error: invalid --mode '$mode' (expected gng, og, dbx, dbx-og, sw, dbx-sw, st, or staging)." >&2
    exit 1
  fi

  mkdir -p "$GAME_DIR"
  cd "$GAME_DIR"

  if [[ -f ./game.sh && "$force" -eq 0 ]]; then
    echo "Error: ${GAME_DIR}/game.sh already exists (pass --force to overwrite)." >&2
    exit 1
  fi

  cp "${TOOL_DIR}/templates/game.sh.template" ./game.sh

  if [[ -n "$cdimg" ]]; then
    local q_cdimg
    q_cdimg="$(escape_dquote "$cdimg")"
    sed -i "s|^CDIMG=\"\"|CDIMG=\"${q_cdimg}\"|" ./game.sh
  fi
  if [[ -n "$patch_dir" ]]; then
    local q_pdir
    q_pdir="$(escape_dquote "$patch_dir")"
    sed -i "s|^# PATCH_DIR=\"\"|PATCH_DIR=\"${q_pdir}\"|" ./game.sh
  fi
  if [[ -n "$mode" ]]; then
    sed -i "s|^# DEFAULT_MODE=\"gng\"|DEFAULT_MODE=\"${mode}\"|" ./game.sh
  fi

  echo "Created ${GAME_DIR}/game.sh"

  if [[ "$no_symlink" -eq 0 ]]; then
    local link_target
    if [[ "$INVOKED_BASE" == "glide-dosbox" ]]; then
      link_target="${INVOKED_TOOL_DIR}/glide-dosbox"
    elif [[ -L "$INVOKED_SCRIPT" ]]; then
      link_target="$(readlink "$INVOKED_SCRIPT")"
      [[ "$link_target" != /* ]] && link_target="${TOOL_DIR}/glide-dosbox"
    else
      link_target="${TOOL_DIR}/glide-dosbox"
    fi
    if [[ ! -e ./run.sh || -L ./run.sh || "$force" -eq 1 ]]; then
      ln -sf "$link_target" ./run.sh
      echo "Linked ${GAME_DIR}/run.sh -> ${link_target}"
    else
      echo "Note: ${GAME_DIR}/run.sh exists and is not a symlink; left untouched."
    fi
  fi

  if [[ -n "$catalog" ]]; then
    if [[ -n "$REPO_ROOT" && -f "${REPO_ROOT}/docs/catalog/games/${catalog}/glide-ng.ini" ]]; then
      if [[ -f ./glide-ng.ini && "$force" -eq 0 ]]; then
        echo "Note: ${GAME_DIR}/glide-ng.ini already exists; left untouched."
      else
        cp "${REPO_ROOT}/docs/catalog/games/${catalog}/glide-ng.ini" ./glide-ng.ini
        echo "Copied catalog preset docs/catalog/games/${catalog}/glide-ng.ini -> ${GAME_DIR}/glide-ng.ini"
      fi
    else
      echo "Warning: catalog preset not found for '${catalog}'." >&2
    fi
  fi
}

print_check_row() {
  local status="$1" label="$2" detail="$3"
  printf '  [%-4s] %-18s %s\n' "$status" "$label" "$detail"
}

check_binary_caps() {
  local label="$1" bin_path="$2"
  command -v getcap >/dev/null 2>&1 || return 0
  local caps=""
  caps="$(getcap -- "$bin_path" 2>/dev/null || true)"
  [[ -z "$caps" ]] && return 0
  caps="${caps#"$bin_path"}"
  caps="${caps#"${caps%%[![:space:]]*}"}"
  print_check_row "WARN" "${label} caps" "file capabilities set (${caps}); glibc AT_SECURE strips LD_LIBRARY_PATH, ignoring GNG_LIBS/OG_LIBS"
  return 1
}

cmd_doctor() {
  local issues=0

  local conf_status="not found, using defaults"
  [[ -n "$LAUNCHER_CONF_LOADED" ]] && conf_status="loaded"
  echo "glide-dosbox doctor"
  echo "  Tool dir      : ${TOOL_DIR}"
  echo "  Game dir      : ${GAME_DIR}"
  echo "  User config   : ${LAUNCHER_CONF} (${conf_status})"
  [[ -n "$REPO_ROOT" ]] && echo "  Repo root     : ${REPO_ROOT}"

  echo ""
  echo "Emulators & Wrappers:"
  if [[ -n "$DOSBOX_SVN" && -x "$DOSBOX_SVN" ]]; then
    print_check_row "OK" "DOSBOX_SVN" "$DOSBOX_SVN"
    check_binary_caps "DOSBOX_SVN" "$DOSBOX_SVN" || issues=$((issues + 1))
  else
    print_check_row "WARN" "DOSBOX_SVN" "${DOSBOX_SVN:-not found}"
    issues=$((issues + 1))
  fi

  if [[ -n "$DOSBOX_X" && -x "$DOSBOX_X" ]]; then
    print_check_row "OK" "DOSBOX_X" "$DOSBOX_X"
    check_binary_caps "DOSBOX_X" "$DOSBOX_X" || issues=$((issues + 1))
  else
    print_check_row "WARN" "DOSBOX_X" "${DOSBOX_X:-not found}"
  fi

  if [[ -n "$DOSBOX_STAGING" && -x "$DOSBOX_STAGING" ]]; then
    print_check_row "OK" "DOSBOX_STAGING" "$DOSBOX_STAGING"
  else
    print_check_row "INFO" "DOSBOX_STAGING" "${DOSBOX_STAGING:-not configured}"
  fi

  if [[ -n "$GNG_LIBS" ]] && compgen -G "${GNG_LIBS}/libglide2x.so*" >/dev/null; then
    print_check_row "OK" "GNG_LIBS" "$GNG_LIBS"
  else
    print_check_row "WARN" "GNG_LIBS" "${GNG_LIBS:-not found (libglide2x.so missing)}"
    issues=$((issues + 1))
  fi

  if [[ -n "$GNG_OVL_DIR" && -n "$(discover_first_ovl_dir "$GNG_OVL_DIR")" ]]; then
    print_check_row "OK" "GNG_OVL_DIR" "$GNG_OVL_DIR"
  else
    print_check_row "WARN" "GNG_OVL_DIR" "${GNG_OVL_DIR:-not found (GLIDE2X.OVL missing; build with Open Watcom or set path)}"
    issues=$((issues + 1))
  fi

  if [[ -n "$OG_LIBS" ]]; then
    if [[ -d "$OG_LIBS" ]]; then
      print_check_row "OK" "OG_LIBS" "$OG_LIBS"
    else
      print_check_row "WARN" "OG_LIBS" "$OG_LIBS (directory missing)"
    fi
  else
    print_check_row "INFO" "OG_LIBS" "(not configured)"
  fi

  if [[ -n "$OG_OVL_DIR" ]]; then
    print_check_row "OK" "OG_OVL_DIR" "$OG_OVL_DIR"
  else
    print_check_row "INFO" "OG_OVL_DIR" "(not configured; og falls back to GNG_OVL_DIR)"
  fi

  if [[ -n "$HW_OVL_DIR" ]]; then
    print_check_row "OK" "HW_OVL_DIR" "$HW_OVL_DIR"
  else
    print_check_row "INFO" "HW_OVL_DIR" "(not configured; sw/staging expect SST-1 GLIDE2X.OVL on PATH or game dir)"
  fi

  echo ""
  echo "Host GPU Discovery:"
  local nv_icd mesa_icd
  nv_icd="$(find_glvnd_egl_icd nvidia)"
  mesa_icd="$(find_glvnd_egl_icd mesa)"
  print_check_row "INFO" "EGL NVIDIA ICD" "${nv_icd:-none detected}"
  print_check_row "INFO" "EGL Mesa ICD" "${mesa_icd:-none detected}"

  echo ""
  echo "Game Directory (${GAME_DIR}):"
  if [[ -f "${GAME_DIR}/game.sh" ]]; then
    reset_game_vars
    # shellcheck disable=SC1091
    source "${GAME_DIR}/game.sh"
    print_check_row "OK" "game.sh" "found (DEFAULT_MODE=${DEFAULT_MODE:-gng}, DEFAULT_ADMIN_MODE=${DEFAULT_ADMIN_MODE:-gng})"
    if [[ -n "${CDIMG:-}" ]]; then
      if [[ -f "$CDIMG" ]]; then
        print_check_row "OK" "CDIMG" "$CDIMG"
      else
        print_check_row "WARN" "CDIMG" "$CDIMG (file missing)"
        issues=$((issues + 1))
      fi
    else
      print_check_row "INFO" "CDIMG" "(none)"
    fi

    if [[ -n "${PATCH_DIR:-}" ]]; then
      if [[ -d "$PATCH_DIR" ]]; then
        print_check_row "OK" "PATCH_DIR" "$PATCH_DIR"
      else
        print_check_row "WARN" "PATCH_DIR" "$PATCH_DIR (directory missing)"
        issues=$((issues + 1))
      fi
    fi

    [[ -f "${GAME_DIR}/glide-ng.ini" ]] \
      && print_check_row "OK" "glide-ng.ini" "present" \
      || print_check_row "INFO" "glide-ng.ini" "(absent; using defaults)"

    [[ -f "${GAME_DIR}/dosbox-override.conf" ]] \
      && print_check_row "OK" "dosbox-override" "present" \
      || print_check_row "INFO" "dosbox-override" "(absent)"

    local variant_keys=()
    mapfile -t variant_keys < <(list_defined_variants)
    if (( ${#variant_keys[@]} > 0 )); then
      local vdef=""
      [[ -n "${DEFAULT_VARIANT:-}" ]] && vdef="$(find_variant_key "$DEFAULT_VARIANT" || true)"
      local formatted=() vk
      for vk in "${variant_keys[@]}"; do
        if [[ "$vk" == "$vdef" ]]; then
          formatted+=("${vk} (default)")
        else
          formatted+=("$vk")
        fi
      done
      local vjoined=""
      local IFS=", "
      vjoined="${formatted[*]}"
      print_check_row "OK" "Variants" "$vjoined"
    fi

    local ovls
    ovls="$(find_local_glide_ovls "$GAME_DIR")"
    if [[ -n "$ovls" ]]; then
      print_check_row "WARN" "Local OVL" "found local GLIDE2X.OVL (may shadow V:\\ in gng/og): ${ovls//$'\n'/, }"
    else
      print_check_row "OK" "Local OVL" "clean (none found under depth 3)"
    fi
  else
    print_check_row "INFO" "game.sh" "not present in current directory"
  fi

  echo ""
  if (( issues == 0 )); then
    echo "Doctor summary: healthy (0 warnings)."
  else
    echo "Doctor summary: ${issues} warning(s) above."
  fi
}
