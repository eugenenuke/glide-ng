#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LAUNCHER_DIR="${REPO_ROOT}/tools/glide-dosbox"
LAUNCHER_BIN="${LAUNCHER_DIR}/glide-dosbox"

PASS_COUNT=0
FAIL_COUNT=0

pass() {
  PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
  echo "FAIL: $*" >&2
  FAIL_COUNT=$((FAIL_COUNT + 1))
}

assert_contains() {
  local haystack="$1" needle="$2" label="$3"
  if [[ "$haystack" == *"$needle"* ]]; then
    pass
  else
    fail "${label}: expected to find '${needle}' in output:\n${haystack}"
  fi
}

assert_not_contains() {
  local haystack="$1" needle="$2" label="$3"
  if [[ "$haystack" != *"$needle"* ]]; then
    pass
  else
    fail "${label}: did NOT expect to find '${needle}' in output:\n${haystack}"
  fi
}

assert_file_exists() {
  local path="$1" label="$2"
  if [[ -e "$path" ]]; then
    pass
  else
    fail "${label}: expected file/symlink to exist: ${path}"
  fi
}

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Isolate from host environment and user config files.
unset DEFAULT_MODE DEFAULT_ADMIN_MODE GNG_LIBS GNG_OVL_DIR OG_LIBS OGLIBS OG_OVL_DIR HW_OVL_DIR
unset DOSBOX_SVN DOSBOX_X DOSBOX_STAGING DOSBOX_ST MEDIA_DIR
export HOME="${TMP_ROOT}/home"
export XDG_CONFIG_HOME="${TMP_ROOT}/xdg_cfg"
export XDG_DATA_HOME="${TMP_ROOT}/xdg_data"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"

# 1. Bash syntax validation across all launcher scripts.
bash -n "$LAUNCHER_BIN"
for f in "${LAUNCHER_DIR}/lib/"*.sh "${LAUNCHER_DIR}/completions/glide-dosbox.bash"; do
  bash -n "$f"
done
pass

# 2. Public-repo PII check (must not embed /home/<user>).
if grep -rn '/home/' "$LAUNCHER_DIR" >/dev/null 2>&1; then
  fail "PII check: found hardcoded '/home/' under tools/glide-dosbox/"
else
  pass
fi

# Create controlled fake bins, libs, OVLs, and ICDs.
FAKE_BIN="${TMP_ROOT}/fake_bin"
FAKE_GNG_LIBS="${TMP_ROOT}/gng_libs"
FAKE_OG_LIBS="${TMP_ROOT}/og_libs"
FAKE_GNG_OVL="${TMP_ROOT}/ovl/gng"
FAKE_OG_OVL="${TMP_ROOT}/ovl/og"
FAKE_HW_OVL="${TMP_ROOT}/ovl/hw"
FAKE_ICD="${TMP_ROOT}/icd"
mkdir -p "$FAKE_BIN" "$FAKE_GNG_LIBS" "$FAKE_OG_LIBS" "$FAKE_GNG_OVL" "$FAKE_OG_OVL" "$FAKE_HW_OVL" "$FAKE_ICD"

for exe in dosbox_openglide dosbox-x dosbox-staging; do
  cat > "${FAKE_BIN}/${exe}" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  chmod +x "${FAKE_BIN}/${exe}"
done
: > "${FAKE_GNG_LIBS}/libglide2x.so.2.0.0"
: > "${FAKE_GNG_OVL}/glide2x.ovl"
: > "${FAKE_OG_OVL}/GLIDE2X.OVL"
: > "${FAKE_HW_OVL}/GLIDE2X.OVL"
: > "${FAKE_ICD}/10_nvidia.json"
: > "${FAKE_ICD}/50_mesa.json"

CONF_FILE="${TMP_ROOT}/launcher.conf"
cat > "$CONF_FILE" <<EOF
GNG_LIBS="${FAKE_GNG_LIBS}"
GNG_OVL_DIR="${FAKE_GNG_OVL}"
OG_LIBS="${FAKE_OG_LIBS}"
OG_OVL_DIR="${FAKE_OG_OVL}"
HW_OVL_DIR="${FAKE_HW_OVL}"
DOSBOX_SVN="${FAKE_BIN}/dosbox_openglide"
DOSBOX_X="${FAKE_BIN}/dosbox-x"
DOSBOX_STAGING="${FAKE_BIN}/dosbox-staging"
DEFAULT_MODE="gng"
DEFAULT_ADMIN_MODE="gng"
EOF

export GLIDE_DOSBOX_CONF="$CONF_FILE"
export GLIDE_DOSBOX_NVIDIA_EGL_ICD="${FAKE_ICD}/10_nvidia.json"
export GLIDE_DOSBOX_MESA_EGL_ICD="${FAKE_ICD}/50_mesa.json"

# 3. --help output check (outside a game dir).
OUT="$("$LAUNCHER_BIN" --help)"
assert_contains "$OUT" "shell, sh, dos, exec" "--help shell action"
assert_contains "$OUT" "--emu=EMU" "--help orthogonal emu"
assert_contains "$OUT" "--wrapper=WRAP" "--help orthogonal wrapper"
assert_contains "$OUT" "--gpu=GPU" "--help gpu selector"
assert_contains "$OUT" "--install-completion" "--help completion"

# 4. doctor outside and inside a game dir.
OUT="$(cd "$TMP_ROOT" && "$LAUNCHER_BIN" doctor)"
assert_contains "$OUT" "[OK  ] DOSBOX_SVN" "doctor checks DOSBOX_SVN"
assert_contains "$OUT" "[OK  ] GNG_LIBS" "doctor checks GNG_LIBS"
assert_contains "$OUT" "0 warnings" "doctor outside game dir clean"

# 5. init + catalog preset copy + symlink invocation + non-overwrite guard.
GAME_INIT="${TMP_ROOT}/game_init"
OUT="$(cd "$TMP_ROOT" && "$LAUNCHER_BIN" init --game-dir="$GAME_INIT" --cdimg="${TMP_ROOT}/disc.cue" --patch-dir="${TMP_ROOT}/patches" --mode=dbx --catalog=tomb-raider)"
assert_file_exists "${GAME_INIT}/game.sh" "init game.sh"
assert_file_exists "${GAME_INIT}/run.sh" "init run.sh symlink"
assert_file_exists "${GAME_INIT}/glide-ng.ini" "init catalog glide-ng.ini"
if "$LAUNCHER_BIN" init --game-dir="$GAME_INIT" >/dev/null 2>&1; then
  fail "init should refuse overwriting existing game.sh without --force"
else
  pass
fi

# 6. Symlinked tool dir (`<tmp>/bin/glide-dosbox -> tools/glide-dosbox`) + --install-completion.
STABLE_BIN="${TMP_ROOT}/bin"
mkdir -p "$STABLE_BIN"
ln -sfn "$LAUNCHER_DIR" "${STABLE_BIN}/glide-dosbox"
OUT="$("${STABLE_BIN}/glide-dosbox/glide-dosbox" --install-completion)"
assert_file_exists "${XDG_DATA_HOME}/bash-completion/completions/glide-dosbox" "completion glide-dosbox"
assert_file_exists "${XDG_DATA_HOME}/bash-completion/completions/run.sh" "completion run.sh"
assert_file_exists "${XDG_DATA_HOME}/bash-completion/completions/launch.sh" "completion launch.sh"
COMP_LINK="$(readlink "${XDG_DATA_HOME}/bash-completion/completions/run.sh")"
assert_contains "$COMP_LINK" "${STABLE_BIN}/glide-dosbox/completions/glide-dosbox.bash" "completion symlink preserves stable dir"

# 7. Comprehensive game fixture covering all modes, orthogonal overrides, GPU, debug, OVL warning.
GAME_DIR="${TMP_ROOT}/test_game"
PATCH_DIR_REAL="${TMP_ROOT}/patches_real"
mkdir -p "${GAME_DIR}/GAME" "$PATCH_DIR_REAL"
: > "${TMP_ROOT}/game.cue"
ln -sf "${STABLE_BIN}/glide-dosbox/glide-dosbox" "${GAME_DIR}/run.sh"

cat > "${GAME_DIR}/game.sh" <<'EOF'
INST_CMD=(-c "INSTALL.EXE")
CONF_CMD=(-c "cd GAME" -c "SETUP.EXE" -c "exit")
PATCH_CMD=(-c "xcopy /y P:\\*.* GAME\\" -c "cd GAME" -c "PATCH.EXE" -c "exit")
SHELL_CMD=()
RUN_CMD=(-c "cd GAME" -c "GAME3DFX.EXE" -c "exit")
GAME_ENV=(CUSTOM_GAME_FLAG=1)
EOF
printf 'CDIMG="%s"\nPATCH_DIR="%s"\n' "${TMP_ROOT}/game.cue" "$PATCH_DIR_REAL" >> "${GAME_DIR}/game.sh"

# Symlink ./run.sh invoked from arbitrary CWD resolves GAME_DIR to test_game.
OUT="$(cd /tmp && "${GAME_DIR}/run.sh" --dry-run)"
assert_contains "$OUT" "cwd=${GAME_DIR} action=run profile=gng" "symlink run resolves GAME_DIR"
assert_contains "$OUT" "GAME3DFX.EXE" "default run invokes RUN_CMD"
assert_contains "$OUT" "CUSTOM_GAME_FLAG=1" "GAME_ENV preserved"

# Direct glide-dosbox invocation uses PWD as GAME_DIR.
OUT="$(cd "$GAME_DIR" && "${STABLE_BIN}/glide-dosbox/glide-dosbox" --dry-run)"
assert_contains "$OUT" "cwd=${GAME_DIR} action=run profile=gng" "direct glide-dosbox resolves CWD"

# Orthogonal override: og + --emu=dbx -> dbx-og.
OUT="$("${GAME_DIR}/run.sh" og --emu=dbx --dry-run)"
assert_contains "$OUT" "profile=dbx-og emu=dbx wrapper=og tag=dbx-og" "og --emu=dbx resolves to dbx-og"
assert_contains "$OUT" "${FAKE_BIN}/dosbox-x" "og --emu=dbx selects dosbox-x"
assert_contains "$OUT" "SDL_VIDEODRIVER=x11" "og --emu=dbx sets SDL_VIDEODRIVER=x11"
assert_contains "$OUT" "LD_LIBRARY_PATH=${FAKE_OG_LIBS}" "og --emu=dbx uses OG_LIBS"
assert_contains "$OUT" "${FAKE_OG_OVL}" "og --emu=dbx mounts OG OVL"

# Built-in preset dbx-og + sw + staging.
OUT="$("${GAME_DIR}/run.sh" dbx-og --dry-run)"
assert_contains "$OUT" "profile=dbx-og" "dbx-og preset"
OUT="$("${GAME_DIR}/run.sh" sw --dry-run)"
assert_contains "$OUT" "profile=sw emu=dbx wrapper=hw" "sw preset"
assert_contains "$OUT" "dosbox-x-sw.conf" "sw uses dosbox-x-sw.conf"
OUT="$("${GAME_DIR}/run.sh" staging --dry-run)"
assert_contains "$OUT" "profile=staging emu=staging wrapper=hw" "staging preset"
assert_contains "$OUT" "--conf ${LAUNCHER_DIR}/configs/dosbox-staging.conf" "staging uses --conf"

# Invalid emulator x wrapper pairs must fail.
if "${GAME_DIR}/run.sh" --emu=svn --wrapper=hw --dry-run >/dev/null 2>&1; then
  fail "expected --emu=svn --wrapper=hw to fail"
else
  pass
fi
if "${GAME_DIR}/run.sh" --emu=staging --wrapper=gng --dry-run >/dev/null 2>&1; then
  fail "expected --emu=staging --wrapper=gng to fail"
else
  pass
fi

# shell mode: empty SHELL_CMD by default, mounts D: & P:, never runs RUN_CMD, --cd starts on D:.
OUT="$("${GAME_DIR}/run.sh" shell --dry-run)"
assert_contains "$OUT" "action=shell" "shell action"
assert_contains "$OUT" "imgmount\\ d" "shell mounts CD D:"
assert_contains "$OUT" "mount\\ p" "shell mounts PATCH_DIR P:"
assert_contains "$OUT" "-c c:" "shell defaults to C:"
assert_not_contains "$OUT" "GAME3DFX.EXE" "shell never runs RUN_CMD"

OUT="$("${GAME_DIR}/run.sh" shell --cd --dry-run)"
assert_contains "$OUT" "-c d:" "shell --cd starts on D:"

# patch & install (chaining & --no-patch).
OUT="$("${GAME_DIR}/run.sh" patch --dry-run)"
assert_contains "$OUT" "action=patch" "patch action"
assert_contains "$OUT" "mount\\ p" "patch mounts P:"
assert_contains "$OUT" "PATCH.EXE" "patch runs PATCH_CMD"

OUT="$("${GAME_DIR}/run.sh" install --dry-run)"
assert_contains "$OUT" "action=install" "install action"
assert_contains "$OUT" "INSTALL.EXE" "install runs INST_CMD"
assert_contains "$OUT" "action=patch" "install chains patch by default"

OUT="$("${GAME_DIR}/run.sh" install --no-patch --dry-run)"
assert_not_contains "$OUT" "action=patch" "install --no-patch skips patch phase"

# --exec & --no-exit overrides.
OUT="$("${GAME_DIR}/run.sh" --exec "DIR /W" --dry-run)"
assert_contains "$OUT" "-c DIR\\ /W" "--exec runs custom DOS command"
assert_not_contains "$OUT" "GAME3DFX.EXE" "--exec skips RUN_CMD"

OUT="$("${GAME_DIR}/run.sh" --no-exit --dry-run)"
assert_contains "$OUT" "GAME3DFX.EXE" "--no-exit preserves main command"
assert_not_contains "$OUT" "-c exit" "--no-exit strips trailing -c exit"

# --gpu flags.
OUT="$("${GAME_DIR}/run.sh" --vk --gpu=discrete --dry-run)"
assert_contains "$OUT" "GLIDE_NG_GPU=discrete" "--gpu=discrete sets GLIDE_NG_GPU"
assert_contains "$OUT" "__NV_PRIME_RENDER_OFFLOAD=1" "--gpu=discrete sets NVIDIA PRIME"

OUT="$("${GAME_DIR}/run.sh" --gl --gpu=integrated --dry-run)"
assert_contains "$OUT" "GLIDE_NG_GPU=integrated" "--gpu=integrated sets GLIDE_NG_GPU"
assert_contains "$OUT" "DRI_PRIME=0" "--gpu=integrated sets DRI_PRIME=0"

OUT="$("${GAME_DIR}/run.sh" --gl --gpu=llvmpipe --dry-run)"
assert_contains "$OUT" "GLIDE_NG_GPU=llvmpipe" "--gpu=llvmpipe sets GLIDE_NG_GPU"
assert_contains "$OUT" "LIBGL_ALWAYS_SOFTWARE=1" "--gpu=llvmpipe sets LIBGL_ALWAYS_SOFTWARE"

OUT="$("${GAME_DIR}/run.sh" --vk --gpu=name:RTX --dry-run)"
assert_contains "$OUT" "GLIDE_NG_GPU=name:RTX" "--gpu=name:RTX sets GLIDE_NG_GPU"

if "${GAME_DIR}/run.sh" --gpu=bogus --dry-run >/dev/null 2>&1; then
  fail "expected --gpu=bogus to fail"
else
  pass
fi

# Debug scenario 3 + everyday flags.
OUT="$("${GAME_DIR}/run.sh" --vk --debug=3:50-55 --tag=smoke --msaa=4 --uncap --no-splash --card=Voodoo2 --tmu-mem=4 -w --dry-run)"
assert_contains "$OUT" "GLIDE_NG_BACKEND=vulkan" "--vk flag"
assert_contains "$OUT" "GLIDE_NG_TRACE_FRAMES=50-55" "--debug=3 trace frames"
assert_contains "$OUT" "GLIDE_NG_LOG_FILE=./logs/trace_gng_vk_smoke.log" "trace log filename"
assert_contains "$OUT" "GLIDE_NG_MSAA=4" "--msaa=4"
assert_contains "$OUT" "GLIDE_NG_VSYNC=0" "--uncap vsync"
assert_contains "$OUT" "GLIDE_NG_NO_SPLASH=1" "--no-splash"
assert_contains "$OUT" "GLIDE_NG_CARD_MODEL=Voodoo2" "--card"
assert_contains "$OUT" "GLIDE_NG_TMU_MEM_MB=4" "--tmu-mem"
assert_contains "$OUT" "dosbox-windowed.conf" "-w windowed"

# Local GLIDE2X.OVL conflict warning check.
: > "${GAME_DIR}/GAME/GLIDE2X.OVL"
OUT_WARN="$("${GAME_DIR}/run.sh" --dry-run 2>&1)"
assert_contains "$OUT_WARN" "Warning: local GLIDE2X.OVL found in game tree" "local OVL warning"

# 8. Unit test hook helpers (backup_file, extract_patch, strip_glide_ovl) + legacy config aliases.
HELP_DIR="${TMP_ROOT}/helpers_test"
mkdir -p "${HELP_DIR}/sub" "${HELP_DIR}/dest"
echo "orig" > "${HELP_DIR}/target.exe"
(
  source "${LAUNCHER_DIR}/lib/helpers.sh"
  backup_file "${HELP_DIR}/target.exe"
  echo "modified" > "${HELP_DIR}/target.exe"
  backup_file "${HELP_DIR}/target.exe" # must NOT overwrite existing .bak
  [[ "$(cat "${HELP_DIR}/target.exe.bak")" == "orig" ]]

  echo "hi" > "${HELP_DIR}/sub/patch.txt"
  tar -czf "${HELP_DIR}/patch.tar.gz" -C "${HELP_DIR}/sub" patch.txt
  extract_patch "${HELP_DIR}/patch.tar.gz" "${HELP_DIR}/dest"
  [[ "$(cat "${HELP_DIR}/dest/patch.txt")" == "hi" ]]

  : > "${HELP_DIR}/dest/GLIDE2X.OVL"
  strip_glide_ovl "${HELP_DIR}/dest" >/dev/null 2>&1
  [[ ! -e "${HELP_DIR}/dest/GLIDE2X.OVL" ]]
)
pass

LEGACY_CONF="${TMP_ROOT}/legacy.conf"
cat > "$LEGACY_CONF" <<EOF
GNG_LIBS="${FAKE_GNG_LIBS}"
GNG_OVL_DIR="${FAKE_GNG_OVL}"
OGLIBS="${FAKE_OG_LIBS}"
DOSBOX_SVN="${FAKE_BIN}/dosbox_openglide"
DOSBOX_X="${FAKE_BIN}/dosbox-x"
DOSBOX_ST="${FAKE_BIN}/dosbox-staging"
EOF
OUT="$(GLIDE_DOSBOX_CONF="$LEGACY_CONF" "${GAME_DIR}/run.sh" og --dry-run 2>/dev/null)"
assert_contains "$OUT" "LD_LIBRARY_PATH=${FAKE_OG_LIBS}" "legacy OGLIBS alias"
OUT="$(GLIDE_DOSBOX_CONF="$LEGACY_CONF" "${GAME_DIR}/run.sh" st --dry-run 2>/dev/null)"
assert_contains "$OUT" "${FAKE_BIN}/dosbox-staging" "legacy DOSBOX_ST alias"

if (( FAIL_COUNT > 0 )); then
  echo "test_glide_dosbox.sh: ${PASS_COUNT} passed, ${FAIL_COUNT} failed." >&2
  exit 1
fi
echo "test_glide_dosbox.sh: all ${PASS_COUNT} checks passed."
