# Game hook helpers and pre-launch checks for glide-dosbox.

extract_patch() {
  local archive="${1:?extract_patch requires an archive path}"
  local dest="${2:-.}"

  if [[ ! -f "$archive" ]]; then
    echo "Error: patch archive not found: $archive" >&2
    return 1
  fi
  mkdir -p "$dest"

  local lower="${archive,,}"
  case "$lower" in
    *.tar.zst|*.tzst|*.tar.gz|*.tgz|*.tar.xz|*.txz|*.tar.bz2|*.tbz2|*.tar)
      tar -xf "$archive" -C "$dest"
      ;;
    *.zip)
      if command -v unzip >/dev/null 2>&1; then
        unzip -oq "$archive" -d "$dest"
      elif command -v 7z >/dev/null 2>&1; then
        7z x -y "-o${dest}" "$archive" >/dev/null
      elif command -v bsdtar >/dev/null 2>&1; then
        bsdtar -xf "$archive" -C "$dest"
      else
        echo "Error: no extractor found for .zip ($archive); install unzip, 7z, or bsdtar." >&2
        return 1
      fi
      ;;
    *.7z)
      if command -v 7z >/dev/null 2>&1; then
        7z x -y "-o${dest}" "$archive" >/dev/null
      else
        echo "Error: 7z not found for extracting $archive." >&2
        return 1
      fi
      ;;
    *.rar)
      if command -v unrar >/dev/null 2>&1; then
        unrar x -o+ -inul "$archive" "$dest/"
      elif command -v 7z >/dev/null 2>&1; then
        7z x -y "-o${dest}" "$archive" >/dev/null
      else
        echo "Error: unrar/7z not found for extracting $archive." >&2
        return 1
      fi
      ;;
    *)
      echo "Error: unsupported archive format '$archive'." >&2
      return 1
      ;;
  esac
}

backup_file() {
  local target="${1:?backup_file requires a file path}"
  if [[ ! -e "$target" ]]; then
    return 0
  fi
  local bak="${target}.bak"
  if [[ ! -e "$bak" ]]; then
    cp -p -- "$target" "$bak"
  fi
}

strip_glide_ovl() {
  local search_dir="${1:-.}"
  [[ -d "$search_dir" ]] || return 0
  local ovl
  while IFS= read -r -d '' ovl; do
    rm -f -- "$ovl"
    echo "[glide-dosbox] Removed local OVL: $ovl" >&2
  done < <(find "$search_dir" \( -path '*/logs' -o -path '*/.git' \) -prune -o \
    -type f -iname 'glide2x.ovl' -print0 2>/dev/null || true)
}

find_local_glide_ovls() {
  local search_dir="${1:-.}"
  [[ -d "$search_dir" ]] || return 0
  find "$search_dir" -maxdepth 3 \( -path '*/logs' -o -path '*/.git' \) -prune -o \
    -type f -iname 'glide2x.ovl' -print 2>/dev/null || true
}

warn_local_glide_ovl() {
  local search_dir="${1:-.}"
  local found
  found="$(find_local_glide_ovls "$search_dir")"
  if [[ -n "$found" ]]; then
    echo "Warning: local GLIDE2X.OVL found in game tree (DOS searches CWD before V:\\):" >&2
    while IFS= read -r path; do
      echo "  - $path" >&2
    done <<< "$found"
    echo "  Remove or run strip_glide_ovl so V:\\ pass-through GLIDE2X.OVL takes effect." >&2
  fi
}

strip_trailing_dosbox_exit() {
  local -n _cmds_ref="$1"
  while (( ${#_cmds_ref[@]} > 0 )); do
    local n="${#_cmds_ref[@]}"
    if (( n >= 2 )) && [[ "${_cmds_ref[n-2]}" == "-c" ]] && [[ "${_cmds_ref[n-1],,}" == "exit" ]]; then
      unset "_cmds_ref[$((n-1))]" "_cmds_ref[$((n-2))]"
      _cmds_ref=(${_cmds_ref[@]+"${_cmds_ref[@]}"})
    elif [[ "${_cmds_ref[n-1],,}" == "-exit" || "${_cmds_ref[n-1],,}" == "--exit" ]]; then
      unset "_cmds_ref[$((n-1))]"
      _cmds_ref=(${_cmds_ref[@]+"${_cmds_ref[@]}"})
    else
      break
    fi
  done
}
