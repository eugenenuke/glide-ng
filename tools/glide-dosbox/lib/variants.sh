# Per-game runtime variant resolution (--variant / DEFAULT_VARIANT / RUN_CMD_*).

list_defined_variants() {
  local raw
  raw="$(compgen -A variable RUN_CMD_ || true)"
  [[ -z "$raw" ]] && return 0
  printf '%s\n' "$raw" | sed 's/^RUN_CMD_//' | LC_ALL=C sort
}

format_variant_list() {
  local keys=()
  mapfile -t keys < <(list_defined_variants)
  if (( ${#keys[@]} == 0 )); then
    echo "<none>"
    return 0
  fi
  local IFS=", "
  echo "${keys[*]}"
}

find_variant_key() {
  local req="$1"
  local norm="${req//-/_}"
  [[ "$norm" =~ ^[A-Za-z0-9_]+$ ]] || return 1

  if declare -p "RUN_CMD_${norm}" >/dev/null 2>&1; then
    echo "$norm"
    return 0
  fi

  local req_lower="${norm,,}"
  local k
  while IFS= read -r k; do
    [[ -z "$k" ]] && continue
    if [[ "${k,,}" == "$req_lower" ]]; then
      echo "$k"
      return 0
    fi
  done < <(list_defined_variants)

  return 1
}

resolve_run_variant() {
  local action="$1"
  local opt_variant="$2"
  local has_exec="${3:-0}"

  ACTIVE_VARIANT=""
  ACTIVE_VARIANT_TAG=""
  VARIANT_CMD=()
  VARIANT_ENV=()
  VARIANT_MODE=""

  if [[ "$action" != "run" ]]; then
    if [[ -n "$opt_variant" ]]; then
      echo "Warning: --variant=${opt_variant} only applies to the 'run' action; ignoring." >&2
    fi
    return 0
  fi

  local explicit_cli=0
  local requested="${DEFAULT_VARIANT:-}"
  if [[ -n "$opt_variant" ]]; then
    requested="$opt_variant"
    explicit_cli=1
  fi

  if [[ "$requested" == "default" ]]; then
    local def_key=""
    if def_key="$(find_variant_key "default")"; then
      requested="$def_key"
    elif [[ -n "${DEFAULT_VARIANT:-}" && "$explicit_cli" -eq 1 ]]; then
      requested="$DEFAULT_VARIANT"
    else
      VARIANT_CMD=(${RUN_CMD[@]+"${RUN_CMD[@]}"})
      return 0
    fi
  fi

  if [[ -z "$requested" ]]; then
    if (( ${#RUN_CMD[@]} > 0 )); then
      VARIANT_CMD=("${RUN_CMD[@]}")
      return 0
    fi
    if (( ! has_exec )); then
      local defined=""
      defined="$(list_defined_variants)"
      if [[ -n "$defined" ]]; then
        echo "Error: RUN_CMD and DEFAULT_VARIANT are unset in ./game.sh; specify --variant=NAME or set DEFAULT_VARIANT (available: $(format_variant_list))." >&2
        exit 1
      fi
    fi
    return 0
  fi

  local key=""
  if ! key="$(find_variant_key "$requested")"; then
    local available
    available="$(format_variant_list)"
    if [[ "$available" == "<none>" ]]; then
      echo "Error: unknown runtime variant '${requested}' (no RUN_CMD_* variants are defined in ${GAME_DIR}/game.sh)." >&2
    else
      echo "Error: unknown runtime variant '${requested}' (available: ${available})." >&2
    fi
    exit 1
  fi

  ACTIVE_VARIANT="$key"
  if (( explicit_cli )); then
    ACTIVE_VARIANT_TAG="$key"
  fi

  local -n _vcmd_ref="RUN_CMD_${key}"
  VARIANT_CMD=(${_vcmd_ref[@]+"${_vcmd_ref[@]}"})

  if declare -p "RUN_ENV_${key}" >/dev/null 2>&1; then
    local -n _venv_ref="RUN_ENV_${key}"
    VARIANT_ENV=(${_venv_ref[@]+"${_venv_ref[@]}"})
  fi

  local mode_var="RUN_MODE_${key}"
  if [[ -n "${!mode_var:-}" ]]; then
    VARIANT_MODE="${!mode_var}"
  fi
}

cmd_list_variants() {
  local keys=()
  mapfile -t keys < <(list_defined_variants)
  local default_key=""
  if [[ -n "${DEFAULT_VARIANT:-}" ]]; then
    default_key="$(find_variant_key "$DEFAULT_VARIANT" || true)"
  fi

  echo "Runtime variants (${GAME_DIR}/game.sh):"
  if (( ${#keys[@]} == 0 )); then
    if (( ${#RUN_CMD[@]} > 0 )); then
      printf '  * default (RUN_CMD): %s\n' "${RUN_CMD[*]}"
    else
      echo "  (no RUN_CMD or RUN_CMD_* variants defined)"
    fi
    return 0
  fi

  local k marker extras mode_var env_var
  for k in "${keys[@]}"; do
    marker=" "
    [[ "$k" == "$default_key" ]] && marker="*"
    local -n _cmd_preview="RUN_CMD_${k}"
    extras=""
    mode_var="RUN_MODE_${k}"
    [[ -n "${!mode_var:-}" ]] && extras+=" [mode: ${!mode_var}]"
    if declare -p "RUN_ENV_${k}" >/dev/null 2>&1; then
      local -n _env_preview="RUN_ENV_${k}"
      (( ${#_env_preview[@]} > 0 )) && extras+=" [env: ${_env_preview[*]}]"
    fi
    printf '  %s %-16s %s%s\n' "$marker" "$k" "${_cmd_preview[*]-}" "$extras"
  done

  if (( ${#RUN_CMD[@]} > 0 )); then
    marker=" "
    [[ -z "$default_key" ]] && marker="*"
    printf '  %s %-16s %s\n' "$marker" "default" "${RUN_CMD[*]} (RUN_CMD)"
  fi
}
