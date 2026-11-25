# Configuration loading & layered path auto-discovery for glide-dosbox.

capture_cli_env_overrides() {
  CLI_DEFAULT_MODE="${DEFAULT_MODE-}"
  CLI_DEFAULT_ADMIN_MODE="${DEFAULT_ADMIN_MODE-}"
  CLI_GNG_LIBS="${GNG_LIBS-}"
  CLI_GNG_OVL_DIR="${GNG_OVL_DIR-}"
  CLI_OG_LIBS="${OG_LIBS-${OGLIBS-}}"
  CLI_OG_OVL_DIR="${OG_OVL_DIR-}"
  CLI_HW_OVL_DIR="${HW_OVL_DIR-}"
  CLI_DOSBOX_SVN="${DOSBOX_SVN-}"
  CLI_DOSBOX_X="${DOSBOX_X-}"
  CLI_DOSBOX_STAGING="${DOSBOX_STAGING-${DOSBOX_ST-}}"
  CLI_MEDIA_DIR="${MEDIA_DIR-}"
}

detect_repo_root() {
  local candidate
  for candidate in "${TOOL_DIR}/../.." "${TOOL_DIR}/.."; do
    if [[ -d "${candidate}/include/glide_ng" ]]; then
      (cd -P "$candidate" && pwd)
      return 0
    fi
  done
  echo ""
}

discover_first_bin() {
  local name path
  for name in "$@"; do
    path="$(command -v "$name" 2>/dev/null || true)"
    if [[ -n "$path" ]]; then
      echo "$path"
      return 0
    fi
  done
  echo ""
}

discover_first_lib_dir() {
  local dir
  for dir in "$@"; do
    if [[ -d "$dir" ]] && compgen -G "${dir}/libglide2x.so*" >/dev/null; then
      echo "$dir"
      return 0
    fi
  done
  echo ""
}

discover_first_ovl_dir() {
  local dir
  for dir in "$@"; do
    if [[ -f "${dir}/GLIDE2X.OVL" || -f "${dir}/glide2x.ovl" ]]; then
      echo "$dir"
      return 0
    fi
  done
  echo ""
}

load_global_config() {
  REPO_ROOT="$(detect_repo_root)"
  GNG_LIBS=""
  GNG_OVL_DIR=""
  OG_LIBS=""
  OG_OVL_DIR=""
  HW_OVL_DIR=""
  DOSBOX_SVN=""
  DOSBOX_X=""
  DOSBOX_STAGING=""
  MEDIA_DIR=""
  DEFAULT_MODE="gng"
  DEFAULT_ADMIN_MODE="gng"
  LAUNCHER_CONF="${GLIDE_DOSBOX_CONF:-${XDG_CONFIG_HOME:-$HOME/.config}/glide-ng/dosbox-launcher.conf}"
  LAUNCHER_CONF_LOADED=""

  # Optional sibling common.sh support for custom local deployments.
  if [[ -f "${TOOL_DIR}/common.sh" ]]; then
    # shellcheck disable=SC1091
    source "${TOOL_DIR}/common.sh"
  fi

  if [[ -f "$LAUNCHER_CONF" ]]; then
    # shellcheck disable=SC1090
    source "$LAUNCHER_CONF"
    LAUNCHER_CONF_LOADED="$LAUNCHER_CONF"
  fi

  # Reconcile legacy variable names (OGLIBS, DOSBOX_ST) after sourcing config files.
  if [[ -z "${OG_LIBS:-}" && -n "${OGLIBS:-}" ]]; then
    OG_LIBS="$OGLIBS"
  fi
  if [[ -z "${DOSBOX_STAGING:-}" && -n "${DOSBOX_ST:-}" ]]; then
    DOSBOX_STAGING="$DOSBOX_ST"
  fi

  # Re-apply env vars that were already set in the caller's environment.
  [[ -n "$CLI_GNG_LIBS" ]] && GNG_LIBS="$CLI_GNG_LIBS"
  [[ -n "$CLI_GNG_OVL_DIR" ]] && GNG_OVL_DIR="$CLI_GNG_OVL_DIR"
  [[ -n "$CLI_OG_LIBS" ]] && OG_LIBS="$CLI_OG_LIBS"
  [[ -n "$CLI_OG_OVL_DIR" ]] && OG_OVL_DIR="$CLI_OG_OVL_DIR"
  [[ -n "$CLI_HW_OVL_DIR" ]] && HW_OVL_DIR="$CLI_HW_OVL_DIR"
  [[ -n "$CLI_DOSBOX_SVN" ]] && DOSBOX_SVN="$CLI_DOSBOX_SVN"
  [[ -n "$CLI_DOSBOX_X" ]] && DOSBOX_X="$CLI_DOSBOX_X"
  [[ -n "$CLI_DOSBOX_STAGING" ]] && DOSBOX_STAGING="$CLI_DOSBOX_STAGING"
  [[ -n "$CLI_MEDIA_DIR" ]] && MEDIA_DIR="$CLI_MEDIA_DIR"
  [[ -n "$CLI_DEFAULT_MODE" ]] && DEFAULT_MODE="$CLI_DEFAULT_MODE"
  [[ -n "$CLI_DEFAULT_ADMIN_MODE" ]] && DEFAULT_ADMIN_MODE="$CLI_DEFAULT_ADMIN_MODE"

  # Auto-discover only paths still unset after config/env overrides.
  if [[ -n "$REPO_ROOT" ]]; then
    [[ -z "$GNG_LIBS" ]] && GNG_LIBS="$(discover_first_lib_dir \
      "${REPO_ROOT}/build_release/lib" \
      "${REPO_ROOT}/build/lib")"
    [[ -z "$GNG_OVL_DIR" ]] && GNG_OVL_DIR="$(discover_first_ovl_dir \
      "${REPO_ROOT}/build_release/dos" \
      "${REPO_ROOT}/build/dos" \
      "${TOOL_DIR}/ovl/gng")"
  elif [[ -z "$GNG_OVL_DIR" && -d "${TOOL_DIR}/ovl/gng" ]]; then
    GNG_OVL_DIR="$(discover_first_ovl_dir "${TOOL_DIR}/ovl/gng")"
  fi

  [[ -z "$OG_OVL_DIR" && -d "${TOOL_DIR}/ovl/og" ]] && OG_OVL_DIR="$(discover_first_ovl_dir "${TOOL_DIR}/ovl/og")"
  [[ -z "$HW_OVL_DIR" && -d "${TOOL_DIR}/ovl/hw" ]] && HW_OVL_DIR="$(discover_first_ovl_dir "${TOOL_DIR}/ovl/hw")"

  [[ -z "$DOSBOX_SVN" ]] && DOSBOX_SVN="$(discover_first_bin dosbox_openglide dosbox-svn)"
  [[ -z "$DOSBOX_X" ]] && DOSBOX_X="$(discover_first_bin dosbox-x)"
  [[ -z "$DOSBOX_STAGING" ]] && DOSBOX_STAGING="$(discover_first_bin dosbox-staging dosbox)" || true
}

reset_game_vars() {
  CDIMG=""
  PATCH_DIR=""
  PATCH_ARCHIVE=""
  PATCH_ARCHIVES=()
  PATCH_DEST=""
  INST_CMD=()
  CONF_CMD=()
  PATCH_CMD=()
  SHELL_CMD=()
  RUN_CMD=()
  GAME_ENV=()
}

load_game_config() {
  reset_game_vars
  if [[ ! -f ./game.sh ]]; then
    echo "Error: ${GAME_DIR}/game.sh not found." >&2
    echo "Run 'glide-dosbox init' or copy ${TOOL_DIR}/templates/game.sh.template to ./game.sh." >&2
    exit 1
  fi
  # shellcheck disable=SC1091
  source ./game.sh

  # Environment DEFAULT_MODE / DEFAULT_ADMIN_MODE overrides per-game defaults.
  [[ -n "$CLI_DEFAULT_MODE" ]] && DEFAULT_MODE="$CLI_DEFAULT_MODE"
  [[ -n "$CLI_DEFAULT_ADMIN_MODE" ]] && DEFAULT_ADMIN_MODE="$CLI_DEFAULT_ADMIN_MODE" || true
}
