# Host GPU selection (--gpu=...) for glide-ng and Linux GLVND/PRIME/Mesa.

parse_gpu_val() {
  local val="$1"
  case "$val" in
    default|os|auto)
      OPT_GPU="default"
      ;;
    discrete|dgpu)
      OPT_GPU="discrete"
      ;;
    integrated|igpu)
      OPT_GPU="integrated"
      ;;
    llvmpipe|llvm|cpu)
      OPT_GPU="llvmpipe"
      ;;
    name:*)
      local substr="${val#name:}"
      if [[ -z "$substr" ]]; then
        echo "Error: --gpu=name:<substr> requires a non-empty adapter substring." >&2
        exit 1
      fi
      OPT_GPU="name:${substr}"
      ;;
    *)
      echo "Error: invalid --gpu value '$val' (expected default, discrete, integrated, llvmpipe, or name:<substr>)." >&2
      exit 1
      ;;
  esac
}

find_glvnd_egl_icd() {
  local kind="$1"
  if [[ "$kind" == "nvidia" && -n "${GLIDE_DOSBOX_NVIDIA_EGL_ICD:-}" ]]; then
    [[ -f "$GLIDE_DOSBOX_NVIDIA_EGL_ICD" ]] && echo "$GLIDE_DOSBOX_NVIDIA_EGL_ICD"
    return 0
  fi
  if [[ "$kind" == "mesa" && -n "${GLIDE_DOSBOX_MESA_EGL_ICD:-}" ]]; then
    [[ -f "$GLIDE_DOSBOX_MESA_EGL_ICD" ]] && echo "$GLIDE_DOSBOX_MESA_EGL_ICD"
    return 0
  fi

  local pattern=""
  case "$kind" in
    nvidia) pattern="/usr/share/glvnd/egl_vendor.d/*nvidia*.json" ;;
    mesa) pattern="/usr/share/glvnd/egl_vendor.d/*mesa*.json" ;;
    *) return 0 ;;
  esac

  local matches=()
  # shellcheck disable=SC2206
  matches=( $pattern )
  local candidate
  for candidate in "${matches[@]}"; do
    if [[ -f "$candidate" ]]; then
      echo "$candidate"
      return 0
    fi
  done
  return 0
}

append_gpu_env() {
  local gpu_mode="$1"
  GPU_ENV=()
  [[ -z "$gpu_mode" ]] && return 0

  GPU_ENV+=("GLIDE_NG_GPU=${gpu_mode}")

  case "$gpu_mode" in
    discrete)
      local nv_icd
      nv_icd="$(find_glvnd_egl_icd nvidia)"
      if [[ -n "$nv_icd" ]]; then
        GPU_ENV+=(
          "__NV_PRIME_RENDER_OFFLOAD=1"
          "__GLX_VENDOR_LIBRARY_NAME=nvidia"
          "__EGL_VENDOR_LIBRARY_FILENAMES=${nv_icd}"
        )
      else
        GPU_ENV+=("DRI_PRIME=1")
      fi
      ;;
    integrated)
      GPU_ENV+=(
        "DRI_PRIME=0"
        "__GLX_VENDOR_LIBRARY_NAME=mesa"
      )
      local mesa_icd
      mesa_icd="$(find_glvnd_egl_icd mesa)"
      [[ -n "$mesa_icd" ]] && GPU_ENV+=("__EGL_VENDOR_LIBRARY_FILENAMES=${mesa_icd}") || true
      ;;
    llvmpipe)
      GPU_ENV+=(
        "LIBGL_ALWAYS_SOFTWARE=1"
        "GALLIUM_DRIVER=llvmpipe"
        "__GLX_VENDOR_LIBRARY_NAME=mesa"
      )
      local mesa_icd
      mesa_icd="$(find_glvnd_egl_icd mesa)"
      [[ -n "$mesa_icd" ]] && GPU_ENV+=("__EGL_VENDOR_LIBRARY_FILENAMES=${mesa_icd}") || true
      ;;
    default|name:*)
      ;;
  esac
}
