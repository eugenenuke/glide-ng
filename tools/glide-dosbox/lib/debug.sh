# Debug scenarios and glide-ng override env generation for glide-dosbox.

parse_backend_val() {
  local val="$1"
  case "$val" in
    vk|vulkan)
      OPT_BACKEND="vulkan"
      OPT_BACKEND_TAG="vk"
      ;;
    gl|gles|opengl_es)
      OPT_BACKEND="opengl_es"
      OPT_BACKEND_TAG="gl"
      ;;
    sw|software)
      OPT_BACKEND="software"
      OPT_BACKEND_TAG="sw"
      ;;
    *)
      echo "Error: invalid backend '$val' (expected vk, gl, or sw)." >&2
      exit 1
      ;;
  esac
}

ensure_logs_parent() {
  local p="$1"
  [[ -z "$p" ]] && return 0
  local base="${p%%@*}"
  if [[ "$base" == ./logs/* ]]; then
    if [[ "$base" == */ ]]; then
      mkdir -p "${base%/}"
    else
      mkdir -p "$(dirname "$base")"
    fi
  fi
}

resolve_debug_and_cli_env() {
  local tag_prefix="$1"
  local variant_tag="${2:-}"

  SCENARIO_LABEL=""
  if [[ -n "$OPT_DEBUG" ]]; then
    case "$OPT_DEBUG" in
      1|perf)
        SCENARIO_LABEL="perf"
        [[ -z "$OPT_HUD" ]] && OPT_HUD="full"
        [[ -z "$OPT_PERF" ]] && OPT_PERF="__AUTO__"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO__"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="info"
        ;;
      2|diag|debug)
        SCENARIO_LABEL="diag"
        [[ -z "$OPT_HUD" ]] && OPT_HUD="full"
        [[ -z "$OPT_PERF" ]] && OPT_PERF="__AUTO__"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO__"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="debug"
        OPT_CONSOLE=1
        ;;
      3|trace)
        SCENARIO_LABEL="trace"
        [[ -z "$OPT_HUD" ]] && OPT_HUD="full"
        [[ -z "$OPT_PERF" ]] && OPT_PERF="__AUTO__"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="trace"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO_TRACE__"
        [[ -z "$OPT_LOG_CATS" ]] && OPT_LOG_CATS="api,lfb,tex,perf"
        [[ -z "$OPT_TRACE_FRAMES" ]] && OPT_TRACE_FRAMES="100-105"
        ;;
      3:*|trace:*)
        SCENARIO_LABEL="trace"
        local trace_range="${OPT_DEBUG#*:}"
        if [[ -z "$trace_range" ]]; then
          echo "Error: --debug=$OPT_DEBUG requires a frame range after ':' (e.g. --debug=3:100-105)." >&2
          exit 1
        fi
        [[ -z "$OPT_HUD" ]] && OPT_HUD="full"
        [[ -z "$OPT_PERF" ]] && OPT_PERF="__AUTO__"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="trace"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO_TRACE__"
        [[ -z "$OPT_LOG_CATS" ]] && OPT_LOG_CATS="api,lfb,tex,perf"
        [[ -z "$OPT_TRACE_FRAMES" ]] && OPT_TRACE_FRAMES="$trace_range"
        ;;
      4|visual|snap)
        SCENARIO_LABEL="visual"
        [[ -z "$OPT_HUD" ]] && OPT_HUD="full"
        [[ -z "$OPT_PERF" ]] && OPT_PERF="__AUTO__"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO__"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="info"
        [[ -z "$OPT_SNAP" ]] && OPT_SNAP="__AUTO_VISUAL__"
        ;;
      wsi)
        SCENARIO_LABEL="wsi"
        [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="debug"
        [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO_WSI__"
        [[ -z "$OPT_LOG_CATS" ]] && OPT_LOG_CATS="wsi,general"
        OPT_CONSOLE=1
        OPT_WINDOWED=1
        ;;
      *)
        echo "Error: unknown --debug scenario '$OPT_DEBUG'." >&2
        echo "Valid scenarios: 1|perf, 2|diag|debug, 3|trace[:A-B], 4|visual|snap, wsi" >&2
        exit 1
        ;;
    esac
  fi

  # Fill defaults implied by standalone --trace=A-B when not already specified.
  if [[ -n "$OPT_TRACE_FRAMES" ]]; then
    [[ -z "$OPT_LOG_LEVEL" ]] && OPT_LOG_LEVEL="trace"
    [[ -z "$OPT_LOG_CATS" ]] && OPT_LOG_CATS="api,lfb,tex,perf"
    [[ -z "$OPT_LOG_FILE" ]] && OPT_LOG_FILE="__AUTO_TRACE__" || true
  fi

  TAG="${tag_prefix}${OPT_BACKEND_TAG:+_${OPT_BACKEND_TAG}}${variant_tag:+_${variant_tag}}${OPT_TAG:+_${OPT_TAG}}"
  [[ "$OPT_PERF" == "__AUTO__" ]] && OPT_PERF="./logs/perf_${TAG}.csv"
  [[ "$OPT_LOG_FILE" == "__AUTO__" ]] && OPT_LOG_FILE="./logs/${TAG}.log"
  [[ "$OPT_LOG_FILE" == "__AUTO_TRACE__" ]] && OPT_LOG_FILE="./logs/trace_${TAG}.log"
  [[ "$OPT_LOG_FILE" == "__AUTO_WSI__" ]] && OPT_LOG_FILE="./logs/wsi_${TAG}.log"
  [[ "$OPT_SNAP" == "__AUTO_VISUAL__" ]] && OPT_SNAP="./logs/shots_${TAG}/@60+60:15"

  CLI_ENV=()
  [[ -n "$OPT_BACKEND" ]] && CLI_ENV+=("GLIDE_NG_BACKEND=$OPT_BACKEND")
  [[ -n "$OPT_HUD" ]] && CLI_ENV+=("GLIDE_NG_HUD=$OPT_HUD")
  [[ -n "$OPT_PERF" ]] && CLI_ENV+=("GLIDE_NG_PERF_LOG=$OPT_PERF")
  [[ -n "$OPT_LOG_LEVEL" ]] && CLI_ENV+=("GLIDE_NG_LOG_LEVEL=$OPT_LOG_LEVEL")
  [[ -n "$OPT_LOG_FILE" ]] && CLI_ENV+=("GLIDE_NG_LOG_FILE=$OPT_LOG_FILE")
  [[ -n "$OPT_LOG_CATS" ]] && CLI_ENV+=("GLIDE_NG_LOG_CATEGORIES=$OPT_LOG_CATS")
  (( OPT_CONSOLE )) && CLI_ENV+=("GLIDE_NG_LOG_CONSOLE=1")
  [[ -n "$OPT_TRACE_FRAMES" ]] && CLI_ENV+=("GLIDE_NG_TRACE_FRAMES=$OPT_TRACE_FRAMES")
  [[ -n "$OPT_SNAP" ]] && CLI_ENV+=("FX_GLIDE_SNAPSHOT=$OPT_SNAP")
  (( OPT_UNCAP )) && CLI_ENV+=("GLIDE_NG_VSYNC=0" "GLIDE_NG_MAX_FPS=0")
  [[ -n "$OPT_MSAA" ]] && CLI_ENV+=("GLIDE_NG_MSAA=$OPT_MSAA")
  (( OPT_NO_SPLASH )) && CLI_ENV+=("GLIDE_NG_NO_SPLASH=1")
  [[ -n "$OPT_CARD" ]] && CLI_ENV+=("GLIDE_NG_CARD_MODEL=$OPT_CARD")
  [[ -n "$OPT_TMU_MEM" ]] && CLI_ENV+=("GLIDE_NG_TMU_MEM_MB=$OPT_TMU_MEM") || true
}
