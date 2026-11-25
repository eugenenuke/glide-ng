# Burnout: Championship Drag Racing (1998 / v1.23 Glide Patch) — glide-dosbox config
DEFAULT_MODE="dbx"

CDIMG=""      # e.g. "${MEDIA_DIR}/BURNOUT.cue"
# PATCH_DIR="" # optional host directory containing v1.23 Glide patch (burn123h.exe)

INST_CMD=(-c "INSTALL.EXE")
CONF_CMD=(-c "cd BURNOUT" -c "SETSOUND.BAT" -c "exit")
PATCH_CMD=(-c "cd BURNOUT" -c "burn123h.exe -d -o" -c "exit")
SHELL_CMD=()
RUN_CMD=(-c "cd BURNOUT" -c "BURNOUT.EXE" -c "exit")

post_install() {
  echo 'D:\' > ./BURNOUT/BURNOUT.CFG
}

pre_patch() {
  # Uncomment and adjust filename if extracting the v1.23 patch archive automatically:
  # extract_patch "${PATCH_DIR}/burn123h.zip" ./BURNOUT
  :
}

post_patch() {
  strip_glide_ovl ./BURNOUT
}
