# Bash completion for glide-dosbox (glide-dosbox / ./run.sh / launch.sh)

_glide_dosbox_extract_game_variants() {
  local cmd="${1:-}"
  local target_dir=""
  local j
  for (( j = 1; j < ${#COMP_WORDS[@]}; ++j )); do
    case "${COMP_WORDS[j]}" in
      -C|--game-dir)
        (( j + 1 < ${#COMP_WORDS[@]} )) && target_dir="${COMP_WORDS[j+1]}"
        ;;
      --game-dir=*)
        target_dir="${COMP_WORDS[j]#*=}"
        ;;
    esac
  done
  if [[ -z "$target_dir" ]]; then
    if [[ "$(basename -- "$cmd")" != "glide-dosbox" && "$cmd" == */* ]]; then
      target_dir="$(cd "$(dirname -- "$cmd")" 2>/dev/null && pwd || echo "$PWD")"
    else
      target_dir="$PWD"
    fi
  fi

  local game_sh="${target_dir}/game.sh"
  [[ -f "$game_sh" ]] || return 0
  {
    grep -oE '^[[:space:]]*RUN_CMD_[A-Za-z0-9_]+=' "$game_sh" 2>/dev/null \
      | sed -E 's/^[[:space:]]*RUN_CMD_//; s/=$//'
    if grep -qE '^[[:space:]]*RUN_CMD=\(' "$game_sh" 2>/dev/null; then
      echo "default"
    fi
  } | LC_ALL=C sort -u
}

_glide_dosbox_complete() {
  local cmd="${COMP_WORDS[0]}"
  local cur="${COMP_WORDS[COMP_CWORD]}"
  local prev=""
  (( COMP_CWORD >= 1 )) && prev="${COMP_WORDS[COMP_CWORD-1]}"
  local resolved=""

  # Only provide DOSBox Glide completion if the invoked script resolves to glide-dosbox
  # (or legacy launch.sh), so unrelated ./run.sh scripts keep default completion.
  if [[ "$cmd" == */* ]]; then
    resolved="$(readlink -f -- "$cmd" 2>/dev/null || true)"
  else
    local path_cmd
    path_cmd="$(type -P -- "$cmd" 2>/dev/null || true)"
    [[ -n "$path_cmd" ]] && resolved="$(readlink -f -- "$path_cmd" 2>/dev/null || true)"
  fi
  [[ "$resolved" == */glide-dosbox/glide-dosbox || "$resolved" == */dosbox/common/launch.sh ]] || return 0

  # Handle value completion when bash splits on '=' (default COMP_WORDBREAKS includes '=')
  local eq_flag="" eq_val=""
  if (( COMP_CWORD >= 2 )) && [[ "$prev" == "=" ]]; then
    eq_flag="${COMP_WORDS[COMP_CWORD-2]}"
    eq_val="$cur"
  elif (( COMP_CWORD >= 3 )) && [[ "${COMP_WORDS[COMP_CWORD-2]}" == "=" ]]; then
    eq_flag="${COMP_WORDS[COMP_CWORD-3]}"
    eq_val="$cur"
  elif [[ "$cur" == --*=* ]]; then
    eq_flag="${cur%%=*}"
    eq_val="${cur#*=}"
  fi

  local repo_root
  repo_root="$(cd "$(dirname "$resolved")/../.." 2>/dev/null && pwd || true)"

  if [[ -n "$eq_flag" ]]; then
    local vals=""
    case "$eq_flag" in
      --emu)
        vals="svn dbx staging"
        ;;
      --wrapper)
        vals="gng og hw"
        ;;
      --gpu)
        vals="default discrete integrated llvmpipe name:"
        ;;
      --backend)
        vals="vk gl sw"
        ;;
      --debug)
        vals="perf diag trace visual wsi 1 2 3 4"
        ;;
      --hud)
        vals="full fps fps,lfb fps,frametimes,drawcalls,lfb off"
        ;;
      --log)
        vals="info debug trace warn off"
        ;;
      --msaa)
        vals="1 2 4 8"
        ;;
      --card)
        vals="VoodooGraphics VoodooRush Voodoo2 VoodooBanshee Voodoo3 Voodoo4 Voodoo5"
        ;;
      --mode)
        vals="gng og dbx dbx-og sw dbx-sw st staging"
        ;;
      --variant|--flavor|--flavour)
        vals="$(_glide_dosbox_extract_game_variants "$cmd" | tr '\n' ' ')"
        ;;
      --catalog)
        if [[ -n "$repo_root" && -d "${repo_root}/docs/catalog/games" ]]; then
          local d
          for d in "${repo_root}/docs/catalog/games"/*; do
            [[ -d "$d" ]] && vals+="${vals:+ }$(basename "$d")"
          done
        fi
        ;;
      --game-dir|--patch-dir)
        mapfile -t COMPREPLY < <(compgen -d -- "$eq_val")
        return 0
        ;;
      --cdimg|--log-file)
        mapfile -t COMPREPLY < <(compgen -f -- "$eq_val")
        return 0
        ;;
      *)
        return 0
        ;;
    esac
    if [[ "$cur" == --*=* ]]; then
      mapfile -t COMPREPLY < <(compgen -P "${eq_flag}=" -W "$vals" -- "$eq_val")
    else
      mapfile -t COMPREPLY < <(compgen -W "$vals" -- "$eq_val")
    fi
    return 0
  fi

  case "$prev" in
    -C|--game-dir)
      mapfile -t COMPREPLY < <(compgen -d -- "$cur")
      return 0
      ;;
    --variant|--flavor|--flavour)
      local vvals
      vvals="$(_glide_dosbox_extract_game_variants "$cmd" | tr '\n' ' ')"
      mapfile -t COMPREPLY < <(compgen -W "$vvals" -- "$cur")
      return 0
      ;;
  esac

  local actions="run shell sh dos exec install patch setup conf reconfigure init doctor info"
  local profiles="gng og dbx dbx-og sw dbx-sw st staging"
  local opts="--variant= --flavor= --flavour= --list-variants --emu= --wrapper= --gpu= --vk --vulkan --gl --gles --sw --software --backend= --exec --no-exit --cd --no-patch --extract --cdimg= --patch-dir= --catalog= --mode= --force --no-symlink --game-dir= --debug --debug= --hud --hud= --perf --perf= --log --log= --log-file= --tag= --suffix= --console --trace= --snap= -w --windowed --uncap --msaa= --no-splash --card= --tmu-mem= --dry-run --install-completion --enable-completion -h --help"
  local dosbox_flags="-c -conf --conf -fs -fullscreen -exit --exit"

  if (( COMP_CWORD == 1 )); then
    if [[ "$cur" == -* ]]; then
      mapfile -t COMPREPLY < <(compgen -W "$opts" -- "$cur")
    else
      mapfile -t COMPREPLY < <(compgen -W "$actions $profiles $opts" -- "$cur")
    fi
    return 0
  fi

  if [[ "$cur" == -* ]]; then
    mapfile -t COMPREPLY < <(compgen -W "$opts $dosbox_flags" -- "$cur")
    return 0
  fi

  local has_action=0 has_profile=0 i w
  for (( i = 1; i < COMP_CWORD; ++i )); do
    w="${COMP_WORDS[i]}"
    case "$w" in
      run|shell|sh|dos|exec|install|patch|setup|conf|reconfigure|init|doctor|info)
        has_action=1
        ;;
      gng|og|dbx|dbx-og|sw|dbx-sw|st|staging)
        has_profile=1
        ;;
    esac
  done

  local suggestions=""
  (( ! has_action )) && suggestions+="$actions"
  (( ! has_profile )) && suggestions+="${suggestions:+ }$profiles"
  if [[ -n "$suggestions" ]]; then
    mapfile -t COMPREPLY < <(compgen -W "$suggestions" -- "$cur")
    return 0
  fi
}

complete -o bashdefault -o default -F _glide_dosbox_complete glide-dosbox run.sh launch.sh
