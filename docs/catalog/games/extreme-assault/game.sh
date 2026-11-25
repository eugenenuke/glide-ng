# Extreme Assault (1997 / v1.20-v1.23 Patches) — glide-dosbox config
DEFAULT_ADMIN_MODE="dbx"
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/EA.cue"
# PATCH_DIR="" # optional host directory containing XA_E120..XA_A123 patches

INST_CMD=(-c "INSTALL.BAT")
CONF_CMD=(-c "cd BLUEBYTE/EXTREME" -c "SETSOUND.BAT" -c "exit")
PATCH_CMD=(-c "cd BLUEBYTE/EXTREME" -c "XA_E120.EXE" -c "XA_A121.EXE" -c "XA_A122.EXE" -c "XA_A123.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd BLUEBYTE/EXTREME" -c "XA_3DFX.EXE" -c "exit")
RUN_CMD_vga=(-c "cd BLUEBYTE/EXTREME" -c "XA.EXE" -c "exit")
RUN_CMD_lowcol=(-c "cd BLUEBYTE/EXTREME" -c "XA.EXE forbid16bitcolor" -c "exit")

pre_patch() {
  # Uncomment and adjust filename if extracting patch archives automatically:
  # extract_patch "${PATCH_DIR}/ea_patches.zip" ./BLUEBYTE/EXTREME
  :
}

post_patch() {
  strip_glide_ovl ./BLUEBYTE/EXTREME
}
