# Archimedean Dynasty (1996 / Oct 1997 3dfx Patch) — glide-dosbox config
DEFAULT_MODE="dbx"
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/AD.cue"
# PATCH_DIR="" # optional host directory containing v1.120 + AD_3Dfx patches

INST_CMD=(-c "INSTALL.BAT")
CONF_CMD=(-c "cd BLUEBYTE/AD" -c "SETUP.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd BLUEBYTE/AD" -c "AD3DFX.EXE" -c "exit")
RUN_CMD_vga=(-c "cd BLUEBYTE/AD" -c "AD2.EXE" -c "exit")

post_install() {
  # Uncomment and adjust filenames if applying extracted patch archives automatically:
  # extract_patch "${PATCH_DIR}/ad_1120.zip" ./BLUEBYTE/AD
  # extract_patch "${PATCH_DIR}/ad_3dfx.zip" ./BLUEBYTE/AD
  strip_glide_ovl ./BLUEBYTE/AD
}
