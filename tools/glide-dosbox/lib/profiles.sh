# Emulator x Wrapper matrix resolution and profile selection for glide-dosbox.

is_known_profile() {
  case "$1" in
    gng|og|dbx|dbx-og|sw|dbx-sw|st|staging) return 0 ;;
    *) return 1 ;;
  esac
}

normalize_emu() {
  case "$1" in
    svn|dosbox-svn) echo "svn" ;;
    dbx|dosbox-x) echo "dbx" ;;
    st|staging|dosbox-staging) echo "staging" ;;
    *)
      echo "Error: unknown emulator '$1' (expected svn, dbx, or staging)." >&2
      return 1
      ;;
  esac
}

normalize_wrapper() {
  case "$1" in
    gng|glide-ng) echo "gng" ;;
    og|openglide) echo "og" ;;
    hw|sw|voodoo) echo "hw" ;;
    *)
      echo "Error: unknown wrapper '$1' (expected gng, og, or hw)." >&2
      return 1
      ;;
  esac
}

split_profile_preset() {
  case "$1" in
    gng)
      RESOLVED_EMU="svn"
      RESOLVED_WRAPPER="gng"
      ;;
    og)
      RESOLVED_EMU="svn"
      RESOLVED_WRAPPER="og"
      ;;
    dbx)
      RESOLVED_EMU="dbx"
      RESOLVED_WRAPPER="gng"
      ;;
    dbx-og)
      RESOLVED_EMU="dbx"
      RESOLVED_WRAPPER="og"
      ;;
    sw|dbx-sw)
      RESOLVED_EMU="dbx"
      RESOLVED_WRAPPER="hw"
      ;;
    st|staging)
      RESOLVED_EMU="staging"
      RESOLVED_WRAPPER="hw"
      ;;
    *)
      echo "Error: unknown profile '$1' (expected gng, og, dbx, dbx-og, sw, dbx-sw, st, or staging)." >&2
      return 1
      ;;
  esac
}

canonical_profile_tag() {
  local emu="$1" wrapper="$2"
  case "${emu}:${wrapper}" in
    svn:gng) echo "gng" ;;
    svn:og) echo "og" ;;
    dbx:gng) echo "dbx" ;;
    dbx:og) echo "dbx-og" ;;
    dbx:hw) echo "dbx-sw" ;;
    staging:hw) echo "staging" ;;
    *) echo "${emu}-${wrapper}" ;;
  esac
}

resolve_runtime_profile() {
  local explicit_profile="$1"
  local action="$2"
  local opt_emu="$3"
  local opt_wrapper="$4"

  local base_profile=""
  if [[ -n "$explicit_profile" ]]; then
    base_profile="$explicit_profile"
  else
    case "$action" in
      install|patch|setup|conf|reconfigure|shell|sh|dos|exec)
        base_profile="${DEFAULT_ADMIN_MODE:-${DEFAULT_MODE:-gng}}"
        ;;
      *)
        base_profile="${DEFAULT_MODE:-gng}"
        ;;
    esac
  fi

  split_profile_preset "$base_profile" || exit 1
  local orig_emu="$RESOLVED_EMU"
  local orig_wrapper="$RESOLVED_WRAPPER"
  local final_emu="$RESOLVED_EMU"
  local final_wrapper="$RESOLVED_WRAPPER"

  if [[ -n "$opt_emu" ]]; then
    final_emu="$(normalize_emu "$opt_emu")" || exit 1
  fi
  if [[ -n "$opt_wrapper" ]]; then
    final_wrapper="$(normalize_wrapper "$opt_wrapper")" || exit 1
  fi

  # Reject unsupported emulator + wrapper pairs before launching DOSBox.
  case "${final_emu}:${final_wrapper}" in
    svn:hw)
      echo "Error: DOSBox-SVN does not support built-in software Voodoo (--wrapper=hw); use --emu=dbx or --emu=staging." >&2
      exit 1
      ;;
    staging:gng|staging:og)
      echo "Error: DOSBox Staging does not support Glide pass-through (--wrapper=$final_wrapper); use --emu=svn or --emu=dbx, or --wrapper=hw." >&2
      exit 1
      ;;
  esac

  EMU="$final_emu"
  WRAPPER="$final_wrapper"
  if [[ "$final_emu" != "$orig_emu" || "$final_wrapper" != "$orig_wrapper" ]]; then
    PROFILE_TAG="$(canonical_profile_tag "$final_emu" "$final_wrapper")"
  else
    PROFILE_TAG="$base_profile"
  fi

  BIN=""
  CONF_FLAG="-conf"
  BASE_CONF=""
  OVL_DIR=""
  WRAPPER_ENV=()

  case "${EMU}:${WRAPPER}" in
    svn:gng)
      BIN="$DOSBOX_SVN"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-svn.conf"
      OVL_DIR="$GNG_OVL_DIR"
      [[ -n "$GNG_LIBS" ]] && WRAPPER_ENV=(LD_LIBRARY_PATH="${GNG_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
      ;;
    svn:og)
      BIN="$DOSBOX_SVN"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-svn-og.conf"
      OVL_DIR="${OG_OVL_DIR:-$GNG_OVL_DIR}"
      [[ -n "$OG_LIBS" ]] && WRAPPER_ENV=(LD_LIBRARY_PATH="${OG_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
      ;;
    dbx:gng)
      BIN="$DOSBOX_X"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-x.conf"
      OVL_DIR="$GNG_OVL_DIR"
      WRAPPER_ENV=(SDL_VIDEODRIVER="${DBX_SDL_VIDEODRIVER:-x11}")
      [[ -n "$GNG_LIBS" ]] && WRAPPER_ENV+=(LD_LIBRARY_PATH="${GNG_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
      ;;
    dbx:og)
      BIN="$DOSBOX_X"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-x.conf"
      OVL_DIR="${OG_OVL_DIR:-$GNG_OVL_DIR}"
      WRAPPER_ENV=(SDL_VIDEODRIVER="${DBX_SDL_VIDEODRIVER:-x11}")
      [[ -n "$OG_LIBS" ]] && WRAPPER_ENV+=(LD_LIBRARY_PATH="${OG_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
      ;;
    dbx:hw)
      BIN="$DOSBOX_X"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-x-sw.conf"
      OVL_DIR="$HW_OVL_DIR"
      WRAPPER_ENV=(SDL_VIDEODRIVER="${DBX_SDL_VIDEODRIVER:-x11}")
      ;;
    staging:hw)
      BIN="$DOSBOX_STAGING"
      CONF_FLAG="--conf"
      BASE_CONF="${TOOL_DIR}/configs/dosbox-staging.conf"
      OVL_DIR="$HW_OVL_DIR"
      WRAPPER_ENV=()
      ;;
  esac
}
