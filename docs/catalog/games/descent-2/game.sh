# Descent II (1996 / v1.2 + Dynamic 3dfx Patch) — glide-dosbox config
DEFAULT_MODE="dbx"
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/DESCENT_II.cue"
# PATCH_DIR="" # optional host directory containing v1.2 (D2PTCH12.EXE) + D2_3DFX.EXE patch

INST_CMD=(-c "INSTALL.EXE")
CONF_CMD=(-c "cd GAMES/DESCENT2" -c "SETUP.EXE" -c "exit")
PATCH_CMD=(-c "cd GAMES/DESCENT2" -c "D2PTCH12.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd GAMES/DESCENT2" -c "D2_3DFX.EXE" -c "exit")
RUN_CMD_vga=(-c "cd GAMES/DESCENT2" -c "DESCENT2.EXE" -c "exit")

pre_patch() {
  # Uncomment and adjust filenames if extracting patch archives automatically:
  # extract_patch "${PATCH_DIR}/d2_12_patch.zip" ./GAMES/DESCENT2
  # extract_patch "${PATCH_DIR}/d2_3dfx_patch.zip" ./GAMES/DESCENT2
  :
}

post_patch() {
  strip_glide_ovl ./GAMES/DESCENT2
}
