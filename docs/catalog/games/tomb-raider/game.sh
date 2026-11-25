# Tomb Raider (1996 / Jun 1997 3dfx Voodoo Rush Patch) — glide-dosbox config
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/TOMBENG.cue"
# PATCH_DIR="" # optional host directory containing the Jun 1997 3dfx Rush patch

INST_CMD=(-c "INSTALL.EXE")
CONF_CMD=(-c "cd TOMBRAID" -c "SETUP.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd TOMBRAID" -c "TOMB.EXE" -c "exit")
RUN_CMD_vga=(-c "cd TOMBRAID" -c "TOMBVGA.EXE" -c "exit")

post_install() {
  [[ -f ./TOMBRAID/TOMBVGA.EXE ]] || mv ./TOMBRAID/TOMB.EXE ./TOMBRAID/TOMBVGA.EXE
  # Uncomment and adjust filename if extracting the Jun 1997 3dfx Rush patch automatically:
  # extract_patch "${PATCH_DIR}/tomb_3dfx_rush.zip" ./TOMBRAID
  strip_glide_ovl ./TOMBRAID
}
